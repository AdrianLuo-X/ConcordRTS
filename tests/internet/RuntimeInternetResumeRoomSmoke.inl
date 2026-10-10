template<class AdapterFactory>
bool runInternetResumeRoomSmoke(SmokeContext& context, RuntimeInternetServiceHost& service,
    AdapterFactory&& makeAdapter, const std::filesystem::path& mapPath, std::uint64_t mapFingerprint) {
    context.stage = "TLS cold resume room";
    std::string error;
    const auto fail = [&](const std::string& reason) { context.expect(false, reason + ": " + error);return false; };
    RuntimeMultiplayerLobbyModel original;
    auto oldOptions = lobbyOptions(mapFingerprint);
    oldOptions.hostMember.peerId = "previous-host";
    if (!RuntimeMultiplayerLobbyModel::createHost(oldOptions, original, error)) return fail("saved lobby");
    RuntimeMultiplayerRoomMember oldClient;
    oldClient.peerId = "previous-client";oldClient.displayName = "Client";
    const std::array<RuntimeMultiplayerRoomMember, 2> oldMembers{oldOptions.hostMember, oldClient};
    const auto edit = original.syncMembers(oldMembers);
    if (!edit.accepted) { error = edit.reason;return fail("saved seats"); }
    auto saved = std::make_shared<RuntimeMultiplayerResumeSave>();
    if (!makeResumeSaveFixture(original.setup(), compatibility(), *saved, error)) return fail("saved bytes");
    RuntimeProbeWorld previous;
    previous.tick = 900;
    RuntimeMultiplayerLockstepApplyCommand historical;
    historical.tick = 250;historical.peerId = "previous-client";historical.frameSequence = 3;historical.commandIndex = 0;
    historical.command = stopCommand();previous.applied.push_back(historical);
    const auto encoded = encodeProbeWorld(previous);
    std::string text(saved->bytes.begin(), saved->bytes.end());
    const std::string oldHash = "AuthoritativeStateHash=123456789";
    const auto offset = text.find(oldHash);
    if (offset == std::string::npos) return fail("snapshot hash fixture");
    text.replace(offset, oldHash.size(), "AuthoritativeStateHash=" + std::to_string(worldHash(previous)));
    text += "[ResumeProbe]\nWorldHex=" + iniHexEncode(std::string(encoded.begin(), encoded.end())) + "\n";
    if (!readRuntimeMultiplayerResumeSave(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), *saved, error))
        return fail("saved world envelope");
    previous = {}; // Only saved bytes carry the old world from this point.
    RuntimeMultiplayerLockstepConfig lockstep;
    lockstep.inputDelayTicks = 0;
    RuntimeInternetLobbyFlow host(makeAdapter("host-peer"), lockstep), client(makeAdapter("client-peer"), lockstep);
    auto room = roomRecord();room.roomId = "cold-resume-room";room.roomName = "Cold resume";
    auto options = hostOptions(room, mapFingerprint);options.resumeSave = saved;
    if (!host.startHost(std::move(options), nowMilliseconds(), error)) return fail("resume host");
    std::array<RuntimeInternetLobbyFlow*, 1> hostOnly{&host};
    if (!pumpFlows(service, hostOnly, [&]() { return host.mode() == RuntimeInternetLobbyFlowMode::Lobby; }, context, "resume registration")) return false;
    if (!client.join(joinOptions("client-peer", "Client", RuntimeInternetRole::Client, room, 2002, 42001), nowMilliseconds(), error)) return fail("resume join");
    std::array<RuntimeInternetLobbyFlow*, 2> flows{&host, &client};
    const auto mapRoot = mapPath.parent_path() / (mapPath.stem().string() + "_resume_map");
    std::filesystem::create_directories(mapRoot);
    std::filesystem::path downloadedMap;
    bool overlappingTransfers = false;
    if (!pumpFlows(service, flows, [&]() {
        if (!host.lobbyFlow() || !client.lobbyFlow() || !host.lobbyFlow()->canonicalSetupPublished()
            || !client.lobbyFlow()->canonicalSetupPublished()) return false;
        if (!host.lobbyFlow()->localMapVerified() && !host.lobbyFlow()->verifyLocalMap(mapPath, error)) return false;
        auto& lan = client.lobbyFlow()->bridge()->lan();
        const bool mapReady = client.lobbyFlow()->downloadMissingMap(mapRoot, downloadedMap, error);
        overlappingTransfers |= lan.lobbyMapOffer() && !lan.lobbyResumeSnapshotReady();
        return host.lobbyFlow() && client.lobbyFlow() && host.lobbyFlow()->resumeReady(error)
            && client.lobbyFlow()->resumeReady(error) && host.lobbyFlow()->bridge()->lan().lobbyResumeAllPeersReady()
            && mapReady && host.lobbyFlow()->bridge()->lan().lobbyMapTransfersComplete();
    }, context, "resume save through Schannel relay")) return false;
    if (client.lobbyFlow()->bridge()->lan().lobbyResumeSnapshotBytes() != saved->bytes) return fail("TLS resume bytes differ");
    if (!overlappingTransfers || downloadedMap == mapPath) return fail("resume Save/map channel overlap and private cache");
    if (!host.lobbyFlow()->verifyLocalMap(mapPath, error) || !client.lobbyFlow()->verifyLocalMap(downloadedMap, error)
        || !host.lobbyFlow()->setReady(true).succeeded || !client.lobbyFlow()->setReady(true).succeeded) return fail("resume readiness");
    if (!pumpFlows(service, flows, [&]() { return host.lobbyFlow()->canStart(error); }, context, "resume ready consensus")) return false;
    if (!host.lobbyFlow()->startMatch().succeeded) return fail("resume start");
    if (!pumpFlows(service, flows, [&]() { return host.mode() == RuntimeInternetLobbyFlowMode::Running
        && client.mode() == RuntimeInternetLobbyFlowMode::Running; }, context, "resume sealed room")) return false;
    auto hostBridge = host.takeRunningBridge(error), clientBridge = client.takeRunningBridge(error);
    if (!hostBridge || !clientBridge) return fail("resume bridge handoff");
    RuntimeProbeWorld hostWorld, clientWorld;
    const auto restore = [&](RuntimeMultiplayerRuntimeBridge& bridge, RuntimeProbeWorld& world) {
        const auto& bytes = bridge.lan().lobbyResumeSnapshotBytes();
        IniFile ini;
        std::string decoded;
        if (!ini.loadFromString(std::string(bytes.begin(), bytes.end())) || !iniHexDecode(ini.getValue("ResumeProbe", "WorldHex", ""), decoded, 1024 * 1024)
            || !decodeProbeWorld(std::span(reinterpret_cast<const std::uint8_t*>(decoded.data()), decoded.size()), world, error)) return false;
        return bridge.lan().session().authoritativeTick() == world.tick + 1
            && worldHash(world) == saved->descriptor.identity.authoritativeStateHash;
    };
    if (!restore(*hostBridge, hostWorld) || !restore(*clientBridge, clientWorld)
        || !hostBridge->attachRuntime(runtimeCallbacksFor(hostWorld), error)
        || !clientBridge->attachRuntime(runtimeCallbacksFor(clientWorld), error)) return fail("TLS cold world restore");
    const auto deadline = nowMilliseconds() + 10000;
    bool sent = false;
    while (nowMilliseconds() < deadline) {
        if (!pollHost(service, error)) return fail("service pump");
        auto hp = hostBridge->pump(sent && hostWorld.tick < 904);
        if (!pollHost(service, error)) return fail("service flush");
        auto cp = clientBridge->pump(false);
        if (!hp.succeeded || !cp.succeeded) { error = hp.succeeded ? cp.error : hp.error;return fail("resumed frame"); }
        if (!sent && hostBridge->initialStateHashVerified() && clientBridge->initialStateHashVerified()) {
            if (!clientBridge->captureGameplayCommand(guardCommand(), error)) return fail("new peer command");
            sent = true;
        }
        if (hostWorld.tick == 904 && clientWorld.tick == 904) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!sent || hostWorld.tick != 904 || clientWorld.tick != 904 || worldHash(hostWorld) != worldHash(clientWorld)
        || hostWorld.applied.size() != 2 || clientWorld.applied.size() != 2
        || hostWorld.applied.front().peerId != "previous-client" || hostWorld.applied.back().peerId != "client-peer") {
        error = "ticks=" + std::to_string(hostWorld.tick) + "/" + std::to_string(clientWorld.tick)
            + " commands=" + std::to_string(hostWorld.applied.size()) + "/" + std::to_string(clientWorld.applied.size());
        return fail("TLS resumed world or new command differs");
    }
    hostBridge.reset();clientBridge.reset();
    const auto cleanupDeadline = nowMilliseconds() + 35000;
    while (nowMilliseconds() < cleanupDeadline) {
        if (!pollHost(service, error)) return fail("resume cleanup");
        if (service.stats().coordinatorConnections == 0 && service.stats().rooms == 0 && service.stats().activeRelayPairs == 0) {
            std::cout << "TLS cold resume: multi-chunk Save, new room/peer bindings, world hash, saved frame, new command and cleanup passed.\n";return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return fail("resume cleanup deadline");
}
