// Session adapter only: sockets, admission, canonical setup and callbacks stay
// with their existing owner. The independent map machine owns transfer state.
RuntimeMultiplayerLobbyMapTransfer lobbyMapTransfer;

void resetLobbyMapTransfer() { lobbyMapTransfer.reset(); }

bool currentMapMatches(const RuntimeMultiplayerMatchMap& map) const {
    return session.matchSetup() && session.matchSetup()->map == map;
}

RuntimeMultiplayerLobbyMapTransfer::Input lobbyMapInput(
    std::uint64_t id, bool admitted, bool ready = false) const {
    return {id, phase == RuntimeMultiplayerLanPhase::Lobby, admitted, ready,
        session.matchSetup() ? &session.matchSetup()->map : nullptr};
}

bool mapTransfersComplete() const {
    return std::all_of(hostPeers.begin(), hostPeers.end(), [&](const auto& entry) {
        const auto& peer = entry.second;
        return !peer.admitted || peer.kicked || lobbyMapTransfer.hostComplete(peer.mapTransfer,
            session.matchSetup() ? &session.matchSetup()->map : nullptr);
    });
}

bool handleHostLobbyMapPacket(std::uint64_t id, const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    auto found = hostPeers.find(id);
    if (found == hostPeers.end()) return false;
    auto& peer = found->second;
    const auto member = std::find_if(session.members().begin(), session.members().end(),
        [&](const auto& entry) { return entry.peerId == peer.logicalPeerId; });
    const auto action = lobbyMapTransfer.receiveHost(peer.mapTransfer,
        lobbyMapInput(id, peer.admitted, member != session.members().end() && member->ready),
        message, std::chrono::steady_clock::now());
    if (action.failure != RuntimeMultiplayerLobbyMapTransfer::Failure::None) {
        server.disconnect(id);
        handleHostPeerGone(id, RuntimeMultiplayerSessionAbortReason::ProtocolViolation, action.reason, events);
    }
    return action.handled;
}

bool handleClientLobbyMapPacket(const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    const auto action = lobbyMapTransfer.receiveClient(lobbyMapInput(0, joinAccepted), message);
    if (action.failure != RuntimeMultiplayerLobbyMapTransfer::Failure::None) {
        clientProtocolFailure(action.reason, events);
    } else if (action.send) {
        std::string error;
        if (!sendToHost(*action.send, error)) clientProtocolFailure(error, events);
    }
    return action.handled;
}

void sendHostLobbyMapChunks(std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (phase != RuntimeMultiplayerLanPhase::Lobby) return;
    std::vector<std::pair<std::uint64_t, std::string>> failures;
    for (auto& [id, peer] : hostPeers) {
        if (!peer.admitted || peer.kicked) continue;
        const auto now = std::chrono::steady_clock::now();
        for (int stage = 0; stage < 2; ++stage) {
            const auto action = lobbyMapTransfer.pollHost(peer.mapTransfer, lobbyMapInput(id, peer.admitted), now);
            if (action.failure != RuntimeMultiplayerLobbyMapTransfer::Failure::None) {
                failures.emplace_back(id, action.reason); break;
            }
            if (!action.send) break;
            std::string error;
            if (!sendToPeer(id, *action.send, error)) { failures.emplace_back(id, error); break; }
            lobbyMapTransfer.sentHost(peer.mapTransfer, action, now);
            if (action.send->type != RuntimeMultiplayerMessageType::LobbyMapOffer) break;
        }
    }
    for (const auto& [id, reason] : failures) {
        server.disconnect(id);
        handleHostPeerGone(id, RuntimeMultiplayerSessionAbortReason::TransportFailure, reason, events);
    }
}

void checkClientLobbyMapDeadline(std::vector<RuntimeMultiplayerLanEvent>& events) {
    const auto action = lobbyMapTransfer.pollClient(lobbyMapInput(0, joinAccepted), std::chrono::steady_clock::now());
    if (action.failure != RuntimeMultiplayerLobbyMapTransfer::Failure::None) clientProtocolFailure(action.reason, events);
}
