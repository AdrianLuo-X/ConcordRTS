bool testMapTransferProtocol() {
    bool success = true;
    std::string error;
    const std::vector<std::uint8_t> bytes(runtimeMultiplayerSnapshotChunkBytes * 2 + 19, 0x63);
    RuntimeMultiplayerMapRequest request{17, {"multimd/custom.map", 0}};
    success &= expect(runtimeMultiplayerContentFingerprint({{request.map.canonicalId, bytes}}, request.map.contentFingerprint, error), "map fingerprint");
    RuntimeMultiplayerMapOffer offer;
    success &= expect(makeRuntimeMultiplayerMapOffer(request, bytes, offer, error), "bounded map descriptor: " + error);
    RuntimeMultiplayerSnapshotChunkProducer producer;
    RuntimeMultiplayerSnapshotChunkAssembler assembler;
    success &= expect(producer.begin(offer.transfer, bytes).accepted && assembler.begin(offer.transfer).accepted, "map chunk engines begin");
    RuntimeMultiplayerSnapshotChunk chunk;
    success &= expect(producer.currentChunk(chunk, error), "first map chunk");
    RuntimeMultiplayerSnapshotChunkAck ack{request.requestId, 1, 0};
    const std::vector<RuntimeMultiplayerMessage> messages{
        message(RuntimeMultiplayerMessageType::LobbyMapRequest, request),
        message(RuntimeMultiplayerMessageType::LobbyMapOffer, offer),
        message(RuntimeMultiplayerMessageType::LobbyMapChunk, chunk),
        message(RuntimeMultiplayerMessageType::LobbyMapAck, ack)
    };
    for (const auto& original : messages) {
        auto packet = encode(original, success, "map codec");
        RuntimeMultiplayerMessage parsed;
        success &= expect(decodeRuntimeMultiplayerMessage(packet, parsed, error) && parsed == original, "map codec round trip");
        const auto sentinel = parsed;
        packet.payload.push_back(0);
        success &= expect(!decodeRuntimeMultiplayerMessage(packet, parsed, error) && parsed == sentinel, "map trailing bytes are transactional");
        packet = encode(original, success, "map truncation"); packet.payload.pop_back();
        success &= expect(!decodeRuntimeMultiplayerMessage(packet, parsed, error) && parsed == sentinel, "truncated map packet is transactional");
        RuntimeMultiplayerSessionState state;
        const auto before = state;
        const auto ordinary = apply(state, original, "host", success, "map noncanonical");
        const auto trusted = applyTrusted(state, original, success, "map noncanonical trusted");
        success &= expect(!ordinary.accepted && !trusted.accepted && state.commands().empty(), "map control never becomes a deterministic event");
    }
    for (const char* badId : {"../escape.map", "multimd/../escape.map", "D:/escape.map", "multimd\\escape.map", "multimd//escape.map", "MULTIMD/test.map"}) {
        auto bad = request; bad.map.canonicalId = badId;
        RuntimeNetworkPacket packet;
        success &= expect(!encodeRuntimeMultiplayerMessage(message(RuntimeMultiplayerMessageType::LobbyMapRequest, bad), packet, error), "unsafe map id rejected");
    }
    auto badOffer = offer; badOffer.transfer.byteCount = runtimeMultiplayerSnapshotDefaultMaximumBytes + 1;
    success &= expect(!validateRuntimeMultiplayerMapOffer(badOffer, error), "oversized map offer rejected");
    badOffer = offer; ++badOffer.request.requestId;
    success &= expect(!validateRuntimeMultiplayerMapOffer(badOffer, error), "map descriptor bound to request");
    auto corrupted = bytes; corrupted.back() ^= 1;
    success &= expect(!validateRuntimeMultiplayerMapBytes(request.map, corrupted, error), "room fingerprint rejects altered map");
    success &= expect(!validateRuntimeMultiplayerMapBytes(request.map, {}, error), "empty map rejected");
    const auto first = assembler.acceptChunk(chunk);
    success &= expect(first.accepted && !first.complete && assembler.acceptChunk(chunk).duplicate, "partial and duplicate map chunk");
    auto different = chunk; different.bytes[0] ^= 1;
    success &= expect(!assembler.acceptChunk(different).accepted, "conflicting duplicate map rejected");
    assembler.reset(); assembler.begin(offer.transfer);
    while (!producer.complete()) {
        producer.currentChunk(chunk, error);
        const auto accepted = assembler.acceptChunk(chunk);
        if (!expect(accepted.accepted, "map chunks admitted in order")) { success = false; break; }
        assembler.latestAcknowledgement(ack, error);
        success &= expect(producer.acknowledge(ack).accepted, "exact map chunk ack");
    }
    std::vector<std::uint8_t> completed;
    success &= expect(assembler.takeCompletedBytes(completed, error) && completed == bytes, "complete map SHA-256 and byte count match");
    std::cout << "Map protocol: bounded requests, private chunk channel, partial/corrupt/duplicate checks passed.\n";
    return success;
}
