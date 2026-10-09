// RuntimeMultiplayerLanSession::Impl transport-only state. No map message is
// admitted into deterministic command history or the world snapshot channel.
std::optional<RuntimeMultiplayerMatchMap> hostedMap;
std::vector<std::uint8_t> hostedMapBytes;
std::optional<RuntimeMultiplayerMapRequest> requestedMap;
std::optional<RuntimeMultiplayerMapOffer> receivedMapOffer;
std::vector<std::uint8_t> receivedMapBytes;
RuntimeMultiplayerSnapshotChunkAssembler mapAssembler;
std::optional<RuntimeMultiplayerSnapshotChunk> mapLastChunk;
RuntimeMultiplayerSnapshotChunkAck mapLastAck;
std::uint64_t nextMapRequestId = 1;
std::chrono::steady_clock::time_point mapRequestedAt{};

void resetLobbyMapTransfer() {
    hostedMap.reset(); hostedMapBytes.clear(); requestedMap.reset(); receivedMapOffer.reset();
    receivedMapBytes.clear(); mapAssembler.reset(); mapLastChunk.reset(); nextMapRequestId = 1; mapRequestedAt = {};
}

bool currentMapMatches(const RuntimeMultiplayerMatchMap& map) const {
    return session.matchSetup() && session.matchSetup()->map == map;
}

bool mapTransfersComplete() const {
    return std::all_of(hostPeers.begin(), hostPeers.end(), [&](const auto& entry) {
        const auto& peer = entry.second;
        return !peer.admitted || peer.kicked || !peer.mapRequest
            || !currentMapMatches(peer.mapRequest->map) || peer.mapProducer.complete();
    });
}

bool handleHostLobbyMapPacket(std::uint64_t id, const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    auto found = hostPeers.find(id);
    if (found == hostPeers.end()) return false;
    auto& peer = found->second;
    const auto reject = [&](const std::string& reason) {
        server.disconnect(id);
        handleHostPeerGone(id, RuntimeMultiplayerSessionAbortReason::ProtocolViolation, reason, events);
    };
    if (message.type == RuntimeMultiplayerMessageType::Ready && peer.mapRequest
        && currentMapMatches(peer.mapRequest->map) && !peer.mapProducer.complete()
        && std::get<RuntimeMultiplayerReadyMessage>(message.body).ready) {
        reject("ready arrived before requested map transfer completed"); return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapRequest) {
        const auto& request = std::get<RuntimeMultiplayerMapRequest>(message.body);
        if (phase != RuntimeMultiplayerLanPhase::Lobby || !peer.admitted) {
            reject("map request arrived outside an admitted lobby"); return true;
        }
        if (peer.mapRequest && request.requestId <= peer.mapRequest->requestId) {
            if (request.requestId == peer.mapRequest->requestId && request != *peer.mapRequest)
                reject("map request sequence was reused with different content");
            return true;
        }
        // The host may have changed maps before this request reached it. The
        // new canonical setup makes the client request a fresh sequence.
        if (!currentMapMatches(request.map)) return true;
        const auto member = std::find_if(session.members().begin(), session.members().end(),
            [&](const auto& entry) { return entry.peerId == peer.logicalPeerId; });
        if (member != session.members().end() && member->ready) {
            // requestLobbyMap queues the authenticated Ready=false first on
            // this same ordered connection. Never impersonate a peer's Ready
            // intent through the host's canonical source binding.
            reject("map request requires the participant to leave Ready first"); return true;
        }
        peer.mapRequest = request; peer.mapProducer.reset(); peer.mapOfferSent = false; peer.mapChunkSent = false;
        peer.mapStartedAt = std::chrono::steady_clock::now();
        return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapAck) {
        const auto& ack = std::get<RuntimeMultiplayerSnapshotChunkAck>(message.body);
        if (peer.mapRequest && !currentMapMatches(peer.mapRequest->map)) return true;
        if (peer.mapRequest && ack.proposalId < peer.mapRequest->requestId) return true;
        if (!peer.mapRequest || !peer.mapOfferSent || !peer.admitted) {
            reject("map acknowledgement arrived without an offer"); return true;
        }
        const auto result = peer.mapProducer.acknowledge(ack);
        if (!result.accepted) reject(result.reason);
        else if (result.changed) peer.mapChunkSent = false;
        return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapOffer
        || message.type == RuntimeMultiplayerMessageType::LobbyMapChunk) {
        reject("client sent a host-only map transfer message"); return true;
    }
    return false;
}

bool handleClientLobbyMapPacket(const RuntimeMultiplayerMessage& message,
    std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (message.type == RuntimeMultiplayerMessageType::Start && requestedMap
        && currentMapMatches(requestedMap->map) && receivedMapBytes.empty()) {
        clientProtocolFailure("host started before the requested map was complete", events); return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapRequest
        || message.type == RuntimeMultiplayerMessageType::LobbyMapAck) {
        clientProtocolFailure("host sent a client-only map transfer message", events); return true;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapOffer) {
        const auto& offer = std::get<RuntimeMultiplayerMapOffer>(message.body);
        if (!requestedMap && offer.request.requestId < nextMapRequestId) return true;
        if (requestedMap && offer.request.requestId < requestedMap->requestId) return true;
        if (phase != RuntimeMultiplayerLanPhase::Lobby || !joinAccepted || !requestedMap
            || offer.request != *requestedMap || (receivedMapOffer && offer != *receivedMapOffer)) {
            clientProtocolFailure("unsolicited or changed map transfer offer", events); return true;
        }
        if (!currentMapMatches(offer.request.map) || receivedMapOffer) return true;
        const auto begun = mapAssembler.begin(offer.transfer);
        if (!begun.accepted) { clientProtocolFailure(begun.reason, events); return true; }
        receivedMapOffer = offer; return true;
    }
    if (message.type != RuntimeMultiplayerMessageType::LobbyMapChunk) return false;
    const auto& chunk = std::get<RuntimeMultiplayerSnapshotChunk>(message.body);
    if (!requestedMap && chunk.proposalId < nextMapRequestId) return true;
    if (requestedMap && (chunk.proposalId < requestedMap->requestId
        || !currentMapMatches(requestedMap->map))) return true;
    RuntimeMultiplayerSnapshotChunkAck ack;
    std::string error;
    if (mapLastChunk) {
        if (chunk != *mapLastChunk) { clientProtocolFailure("completed map received conflicting bytes", events); return true; }
        ack = mapLastAck;
    } else {
        if (phase != RuntimeMultiplayerLanPhase::Lobby || !receivedMapOffer || !mapAssembler.active()) {
            clientProtocolFailure("map chunk arrived without an admitted offer", events); return true;
        }
        const auto result = mapAssembler.acceptChunk(chunk);
        if (!result.accepted || !mapAssembler.latestAcknowledgement(ack, error)) {
            clientProtocolFailure(result.reason.empty() ? error : result.reason, events); return true;
        }
        if (result.complete) {
            std::vector<std::uint8_t> verified;
            if (!mapAssembler.takeCompletedBytes(verified, error)
                || !validateRuntimeMultiplayerMapBytes(receivedMapOffer->request.map, verified, error)) {
                clientProtocolFailure(error, events); return true;
            }
            receivedMapBytes = std::move(verified); mapLastChunk = chunk; mapLastAck = ack;
        }
    }
    RuntimeMultiplayerMessage response;
    response.type = RuntimeMultiplayerMessageType::LobbyMapAck; response.body = ack;
    if (!sendToHost(response, error)) clientProtocolFailure(error, events);
    return true;
}

void sendHostLobbyMapChunks(std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (phase != RuntimeMultiplayerLanPhase::Lobby) return;
    std::vector<std::pair<std::uint64_t, std::string>> failures;
    for (auto& [id, peer] : hostPeers) {
        if (!peer.admitted || peer.kicked || !peer.mapRequest || peer.mapProducer.complete()) continue;
        if (!currentMapMatches(peer.mapRequest->map)) {
            peer.mapProducer.reset(); peer.mapOfferSent = false; peer.mapChunkSent = false; continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - peer.mapStartedAt >= std::chrono::seconds(180)) {
            failures.emplace_back(id, "requested map transfer timed out"); continue;
        }
        if (!hostedMap || *hostedMap != peer.mapRequest->map || hostedMapBytes.empty()) continue;
        std::string error;
        if (!peer.mapOfferSent) {
            RuntimeMultiplayerMapOffer offer;
            if (!makeRuntimeMultiplayerMapOffer(*peer.mapRequest, hostedMapBytes, offer, error)) {
                failures.emplace_back(id, error); continue;
            }
            const auto begun = peer.mapProducer.begin(offer.transfer, hostedMapBytes);
            if (!begun.accepted) { failures.emplace_back(id, begun.reason); continue; }
            RuntimeMultiplayerMessage message;
            message.type = RuntimeMultiplayerMessageType::LobbyMapOffer; message.body = std::move(offer);
            if (!sendToPeer(id, message, error)) { failures.emplace_back(id, error); continue; }
            peer.mapOfferSent = true;
        }
        if (peer.mapChunkSent && now - peer.mapChunkSentAt < std::chrono::milliseconds(250)) continue;
        RuntimeMultiplayerSnapshotChunk chunk;
        if (!peer.mapProducer.currentChunk(chunk, error)) { failures.emplace_back(id, error); continue; }
        RuntimeMultiplayerMessage message;
        message.type = RuntimeMultiplayerMessageType::LobbyMapChunk; message.body = std::move(chunk);
        if (!sendToPeer(id, message, error)) failures.emplace_back(id, error);
        else { peer.mapChunkSent = true; peer.mapChunkSentAt = now; }
    }
    for (const auto& [id, reason] : failures) {
        server.disconnect(id);
        handleHostPeerGone(id, RuntimeMultiplayerSessionAbortReason::TransportFailure, reason, events);
    }
}

void checkClientLobbyMapDeadline(std::vector<RuntimeMultiplayerLanEvent>& events) {
    if (requestedMap && !currentMapMatches(requestedMap->map)) {
        requestedMap.reset(); receivedMapOffer.reset(); receivedMapBytes.clear(); mapAssembler.reset(); mapLastChunk.reset();
    }
    if (phase == RuntimeMultiplayerLanPhase::Lobby && requestedMap && currentMapMatches(requestedMap->map)
        && receivedMapBytes.empty() && std::chrono::steady_clock::now() - mapRequestedAt >= std::chrono::seconds(180))
        clientProtocolFailure("requested map transfer timed out", events);
}
