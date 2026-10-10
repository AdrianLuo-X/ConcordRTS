// Members and helpers of RuntimeMultiplayerLanSession::Impl. Lobby bootstrap
// never enters deterministic command history or the in-session Load protocol.
std::optional<RuntimeMultiplayerSnapshotDescriptor> lobbyResumeImage;
std::vector<std::uint8_t> lobbyResumeBytes;
RuntimeMultiplayerSnapshotChunkAssembler lobbyResumeAssembler;
std::optional<RuntimeMultiplayerSnapshotChunk> lobbyResumeLastChunk;
RuntimeMultiplayerSnapshotChunkAck lobbyResumeLastAck;
std::chrono::steady_clock::time_point lobbyResumeStartedAt{};

bool handleHostLobbyResumePacket(std::uint64_t transportPeerId, const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (message.type == RuntimeMultiplayerMessageType::Ready && lobbyResumeImage
        && std::get<RuntimeMultiplayerReadyMessage>(message.body).ready) {
        const auto peer = hostPeers.find(transportPeerId);
        if (peer == hostPeers.end() || !peer->second.lobbyResumeProducer.complete()) {
            server.disconnect(transportPeerId);
            handleHostPeerGone(transportPeerId, RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                "ready arrived before the resume save acknowledgement", events);return true;
        }
    }
    if (message.type != RuntimeMultiplayerMessageType::LobbyResumeOffer
        && message.type != RuntimeMultiplayerMessageType::LobbyResumeChunk
        && message.type != RuntimeMultiplayerMessageType::LobbyResumeAck) return false;
    const auto found = hostPeers.find(transportPeerId);
    bool accepted = false;
    if (message.type == RuntimeMultiplayerMessageType::LobbyResumeAck && lobbyResumeImage
        && found != hostPeers.end() && found->second.admitted && found->second.lobbyResumeOfferSent) {
        const auto& ack = std::get<RuntimeMultiplayerSnapshotChunkAck>(message.body);
        const auto result = found->second.lobbyResumeProducer.acknowledge(ack);
        accepted = result.accepted;
        if (result.changed) found->second.lobbyResumeChunkSent = false;
    }
    if (!accepted) {
        server.disconnect(transportPeerId);
        handleHostPeerGone(transportPeerId, RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
            "invalid lobby resume transfer direction or acknowledgement", events);
    }
    return true;
}

bool handleClientLobbyResumePacket(const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (message.type == RuntimeMultiplayerMessageType::Start && lobbyResumeImage) {
        std::uint64_t fingerprint = 0;
        std::string error;
        if (lobbyResumeBytes.empty() || !session.matchSetup()
            || !runtimeMultiplayerGameplaySetupFingerprint(*session.matchSetup(), fingerprint, error)
            || fingerprint != lobbyResumeImage->identity.gameplaySetupFingerprint
            || std::get<RuntimeMultiplayerStartMessage>(message.body).startTick != lobbyResumeImage->identity.snapshotTick + 1) {
            clientProtocolFailure("host started before a compatible resume snapshot was complete", events);return true;
        }
    }
    if (message.type != RuntimeMultiplayerMessageType::LobbyResumeOffer
        && message.type != RuntimeMultiplayerMessageType::LobbyResumeChunk
        && message.type != RuntimeMultiplayerMessageType::LobbyResumeAck) return false;
    if (message.type == RuntimeMultiplayerMessageType::LobbyResumeAck) {
        clientProtocolFailure("host sent a client-only lobby resume acknowledgement", events);return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyResumeOffer) {
        const auto& descriptor = std::get<RuntimeMultiplayerSnapshotDescriptor>(message.body);
        if (phase != RuntimeMultiplayerLanPhase::Lobby || !joinAccepted
            || (lobbyResumeImage && *lobbyResumeImage != descriptor)) {
            clientProtocolFailure("lobby resume offer changed or arrived outside admission", events);return true;
        }
        if (lobbyResumeImage) return true;
        const auto begun = lobbyResumeAssembler.begin(descriptor);
        if (!begun.accepted) { clientProtocolFailure(begun.reason, events);return true; }
        lobbyResumeImage = descriptor;
        lobbyResumeStartedAt = std::chrono::steady_clock::now();
        return true;
    }
    const auto& chunk = std::get<RuntimeMultiplayerSnapshotChunk>(message.body);
    std::string error;
    RuntimeMultiplayerSnapshotChunkAck ack;
    if (lobbyResumeLastChunk) {
        if (*lobbyResumeLastChunk != chunk) {
            clientProtocolFailure("completed lobby resume image received different bytes", events);return true;
        }
        ack = lobbyResumeLastAck;
    } else {
        if (phase != RuntimeMultiplayerLanPhase::Lobby || !lobbyResumeImage || !lobbyResumeAssembler.active()) {
            clientProtocolFailure("lobby resume chunk arrived without an admitted offer", events);return true;
        }
        const auto accepted = lobbyResumeAssembler.acceptChunk(chunk);
        if (!accepted.accepted || !lobbyResumeAssembler.latestAcknowledgement(ack, error)) {
            clientProtocolFailure(accepted.reason.empty() ? error : accepted.reason, events);return true;
        }
        if (accepted.complete) {
            if (!lobbyResumeAssembler.takeCompletedBytes(lobbyResumeBytes, error)) {
                clientProtocolFailure(error, events);return true;
            }
            lobbyResumeLastChunk = chunk;
            lobbyResumeLastAck = ack;
        }
    }
    RuntimeMultiplayerMessage response;
    response.type = RuntimeMultiplayerMessageType::LobbyResumeAck;
    response.body = ack;
    if (!sendToHost(response, error)) clientProtocolFailure(error, events);
    return true;
}

void sendHostLobbyResumeChunks(std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (phase != RuntimeMultiplayerLanPhase::Lobby || !lobbyResumeImage || lobbyResumeBytes.empty()) return;
    std::vector<std::pair<std::uint64_t, std::string>> failures;
    for (auto& [id, peer] : hostPeers) {
        if (!peer.admitted || peer.kicked || peer.lobbyResumeProducer.complete()) continue;
        std::string error;
        if (!peer.lobbyResumeOfferSent) {
            const auto begun = peer.lobbyResumeProducer.begin(*lobbyResumeImage, lobbyResumeBytes);
            if (!begun.accepted) { failures.emplace_back(id, begun.reason);continue; }
            RuntimeMultiplayerMessage offer;
            offer.type = RuntimeMultiplayerMessageType::LobbyResumeOffer;
            offer.body = *lobbyResumeImage;
            if (!sendToPeer(id, offer, error)) { failures.emplace_back(id, error);continue; }
            peer.lobbyResumeOfferSent = true;
            peer.lobbyResumeStartedAt = std::chrono::steady_clock::now();
        }
        if (std::chrono::steady_clock::now() - peer.lobbyResumeStartedAt >= std::chrono::seconds(180)) {
            failures.emplace_back(id, "lobby resume snapshot transfer timed out");continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (peer.lobbyResumeChunkSent && now - peer.lobbyResumeChunkSentAt < std::chrono::milliseconds(250)) continue;
        RuntimeMultiplayerSnapshotChunk chunk;
        if (!peer.lobbyResumeProducer.currentChunk(chunk, error)) { failures.emplace_back(id, error);continue; }
        RuntimeMultiplayerMessage message;
        message.type = RuntimeMultiplayerMessageType::LobbyResumeChunk;
        message.body = std::move(chunk);
        if (!sendToPeer(id, message, error)) failures.emplace_back(id, error);
        else { peer.lobbyResumeChunkSent = true;peer.lobbyResumeChunkSentAt = now; }
    }
    for (const auto& [id, reason] : failures) {
        server.disconnect(id);
        handleHostPeerGone(id, RuntimeMultiplayerSessionAbortReason::TransportFailure, reason, events);
    }
}

void checkClientLobbyResumeDeadline(std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (phase == RuntimeMultiplayerLanPhase::Lobby && lobbyResumeImage && lobbyResumeBytes.empty()
        && lobbyResumeStartedAt != std::chrono::steady_clock::time_point{}
        && std::chrono::steady_clock::now() - lobbyResumeStartedAt >= std::chrono::seconds(180))
        clientProtocolFailure("lobby resume snapshot transfer timed out", events);
}
