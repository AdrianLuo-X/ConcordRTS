bool runRuntimeMultiplayerResumeColdSmoke(std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "cold resume: " + reason + " " + error;return false; };
    std::array<char, 32> modeBuffer{};
    std::array<wchar_t, 32768> pathBuffer{};
    GetEnvironmentVariableA("CONCORD_RESUME_SMOKE_PHASE", modeBuffer.data(), static_cast<DWORD>(modeBuffer.size()));
    const std::string phase = modeBuffer.data();
    const auto pathSize = GetEnvironmentVariableW(L"CONCORD_RESUME_SMOKE_FILE", pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (!phase.empty() && ((phase != "export" && phase != "restore") || pathSize == 0 || pathSize >= pathBuffer.size()))
        return fail("invalid process restart smoke arguments");
    RuntimeMultiplayerCompatibility compatibility;
    compatibility.protocolFingerprint = 1234567;
    compatibility.contentFingerprint = 9876543;
    RuntimeMultiplayerMatchSetup savedSetup;
    savedSetup.revision = 1;savedSetup.hostPeerId = "saved-host";
    savedSetup.map = {"snapshot-smoke-map", 0x912e7a0463bd5cc1ULL};
    savedSetup.rules.gameMode = "battle";savedSetup.rules.rulesOverride = "mpbattlemd.ini";
    savedSetup.rules.randomSeed = 0xf01842bc997ea251ULL;savedSetup.rules.startingUnits = 1;savedSetup.rules.techLevel = 8;
    savedSetup.members = {{"saved-host", "Host", RuntimeMultiplayerMatchMemberIdentity::Combatant},
        {"saved-client", "Client", RuntimeMultiplayerMatchMemberIdentity::Combatant}};
    savedSetup.slots = {
        {0, RuntimeMultiplayerMatchSlotController::Human, "saved-host", "americans", "random", "none", "medium", 1},
        {1, RuntimeMultiplayerMatchSlotController::Human, "saved-client", "russians", "random", "none", "medium", 2},
        {2, RuntimeMultiplayerMatchSlotController::Computer, "", "africans", "random", "none", "hard", 3}};
    if (!canonicalizeRuntimeMultiplayerMatchSetup(savedSetup, error)) return fail("saved recipe");
    const auto startRoom = [&](RuntimeMultiplayerLobbyFlow& host, RuntimeMultiplayerLobbyFlow& client,
        const RuntimeMultiplayerResumeSave* save) {
        RuntimeMultiplayerLanHostOptions network;
        network.enableDiscovery = false;network.sessionPort = 0;network.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
        network.roomName = "Resume cold smoke";network.session.roomId = save ? "reopened-room" : "saved-room";
        network.session.hostPeerId = save ? "new-host" : "saved-host";network.session.hostDisplayName = "Host";
        network.session.compatibility = compatibility;network.session.stateHashPeriodTicks = 1;
        RuntimeMultiplayerLobbyHostCreateOptions lobby;
        lobby.map = savedSetup.map;lobby.rules = savedSetup.rules;lobby.slotCount = 3;lobby.computerSlotIndices = {2};
        lobby.hostMember.peerId = network.session.hostPeerId;lobby.hostMember.displayName = "Host";
        lobby.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
        if (!host.startHost(network, lobby, error, save)) return false;
        RuntimeMultiplayerLanJoinOptions join;
        join.peerId = save ? "new-client" : "saved-client";join.displayName = "Client";join.compatibility = compatibility;
        join.room.roomId = network.session.roomId;join.room.roomName = network.roomName;join.room.hostName = "Host";
        join.room.hostAddress = "127.0.0.1";join.room.sessionPort = host.bridge()->lan().sessionPort();
        join.room.contentFingerprint = "000000000096b43f";join.room.protocolVersion = runtimeLanProtocolVersion;
        join.room.maximumPlayers = 8;join.room.playerCount = 1;
        if (!client.join(join, error)) return false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool published = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto hp = host.poll(), cp = client.poll();
            if (!hp.succeeded || !cp.succeeded) { error = hp.succeeded ? cp.error : hp.error;return false; }
            if (host.lobby().setup().members.size() == 2 && host.lobby().setup() == client.lobby().setup()
                && host.bridge()->lan().lobbyResumeAllPeersReady() && client.bridge()->lan().lobbyResumeSnapshotReady()) {
                if (!save && !published) {
                    for (const auto& slot : savedSetup.slots) {
                        const auto edit = host.setSlot(slot.slotIndex, slot, error);
                        if (!edit.accepted || !error.empty()) return false;
                    }
                    published = true;continue;
                }
                return true;
            }
            Sleep(1);
        }
        error = "room admission/transfer timed out";return false;
    };
    const auto startSession = [&](RuntimeMultiplayerLobbyFlow& host, RuntimeMultiplayerLobbyFlow& client, int tick) {
        // The fixture uses in-memory rules/worlds; map verification is covered
        // by the real file LobbyFlow test. Start transport directly here.
        if (!host.bridge()->lan().setReady(true).succeeded || !client.bridge()->lan().setReady(true).succeeded) return false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool started = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto hp = host.poll(), cp = client.poll();
            if (!hp.succeeded || !cp.succeeded) { error = hp.succeeded ? cp.error : hp.error;return false; }
            if (!started && host.bridge()->lan().session().allMembersReady()) {
                const auto result = host.bridge()->lan().startMatch(tick);
                if (!result.succeeded) { error = result.reason;return false; }
                started = true;
            }
            if (client.bridge()->lan().phase() == RuntimeMultiplayerLanPhase::Running) return true;
            Sleep(1);
        }
        error = "start timed out";return false;
    };
    RuntimeMultiplayerResumeSave saved;
    if (phase == "restore") {
        if (!loadRuntimeMultiplayerResumeSave(std::filesystem::path(pathBuffer.data()), saved, error)) return fail("read previous process save");
    } else {
        RuntimeMultiplayerLobbyFlow oldHost, oldClient;
        if (!startRoom(oldHost, oldClient, nullptr) || !startSession(oldHost, oldClient, 8)) return fail("original session");
        auto world = std::make_unique<RuntimeWindowState>();
        if (!configureRuntimeMultiplayerSnapshotSmokeState(*world, false, false, false, 0)) return fail("source world");
        RuntimeMultiplayerLaunchConfigResult launch;
        if (!makeRuntimeMultiplayerLaunchConfig(*oldHost.bridge()->lan().session().matchSetup(), "saved-host",
            "snapshot-smoke.map", savedSetup.map.contentFingerprint, world->config, launch, error, 1)) return fail("original launch");
        world->config = launch.config;
        RuntimeMultiplayerRuntimeAttachmentState attachment;
        if (!attachRuntimeMultiplayerRuntimeBridge(*world, *oldHost.bridge(), attachment, error)) return fail("original attach");
        auto otherWorld = std::make_unique<RuntimeWindowState>();
        if (!configureRuntimeMultiplayerSnapshotSmokeState(*otherWorld, true, true, false, 0)
            || !makeRuntimeMultiplayerLaunchConfig(*oldHost.bridge()->lan().session().matchSetup(), "saved-client", "snapshot-smoke.map",
                savedSetup.map.contentFingerprint, otherWorld->config, launch, error, 1)) return fail("original client world");
        otherWorld->config = launch.config;
        RuntimeMultiplayerRuntimeAttachmentState otherAttachment;
        if (!attachRuntimeMultiplayerRuntimeBridge(*otherWorld, *oldClient.bridge(), otherAttachment, error)
            || runtimeMultiplayerAuthoritativeStateHash(*world, error) != runtimeMultiplayerAuthoritativeStateHash(*otherWorld, error))
            return fail("original two-world authority differs");
        RuntimeMultiplayerSnapshotMemoryImage image;
        const auto identity = runtimeMultiplayerSnapshotSmokeIdentity(*world, 77, "cold-resume-77", RuntimeMultiplayerSnapshotKind::Save);
        if (!exportRuntimeMultiplayerSnapshotV15(*world, identity, image, error)
            || !readRuntimeMultiplayerResumeSave(image.bytes, saved, error)) return fail("complete save with live recipe");
        if (saved.setup != *oldHost.bridge()->lan().session().matchSetup()) return fail("live recipe differs");
    } // Both original worlds/sockets/attachments are gone before reopening.
    if (phase == "export") {
        std::ofstream file(std::filesystem::path(pathBuffer.data()), std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char*>(saved.bytes.data()), static_cast<std::streamsize>(saved.bytes.size())))
            return fail("write complete save before process exit");
        file.close();
        if (!file) return fail("flush complete save");
        std::cout << "Cold resume export: complete live world saved; both original worlds and connections destroyed.\n";
        error.clear();return true;
    }
    RuntimeMultiplayerLobbyFlow host, client;
    if (!startRoom(host, client, &saved) || !startSession(host, client, 8)) return fail("reopened session");
    auto hostWorld = std::make_unique<RuntimeWindowState>(), clientWorld = std::make_unique<RuntimeWindowState>();
    if (!configureRuntimeMultiplayerSnapshotSmokeState(*hostWorld, false, false, false, 0)
        || !configureRuntimeMultiplayerSnapshotSmokeState(*clientWorld, true, true, false, 0)) return fail("fresh worlds");
    const auto configureFresh = [&](RuntimeWindowState& world, const char* peer) {
        RuntimeMultiplayerLaunchConfigResult launch;
        if (!makeRuntimeMultiplayerLaunchConfig(host.lobby().setup(), peer, "snapshot-smoke.map", savedSetup.map.contentFingerprint,
            world.config, launch, error, 1)) return false;
        world.config = launch.config;world.tick = 0;world.economy.credits = 123;
        world.world.accessObjects().clear();world.wrecks.clear();world.buildingCollapses.clear();world.matchScores.clear();
        world.particles.reset(world.config.fixedTickHz);world.superWeapons.reset(std::vector<SuperWeaponRule>{});
        return true;
    };
    if (!configureFresh(*hostWorld, "new-host") || !configureFresh(*clientWorld, "new-client")) return fail("new peer projection");
    RuntimeMultiplayerSnapshotMemoryImage premature;
    if (rebindRuntimeMultiplayerSaveAsLoad(*hostWorld, saved.bytes, 1, 1, 0, premature, error)) return fail("normal Load accepted a future saved tick");
    error.clear();
    RuntimeMultiplayerRuntimeAttachmentState hostAttachment, clientAttachment;
    if (!attachRuntimeMultiplayerRuntimeBridge(*hostWorld, *host.bridge(), hostAttachment, error)
        || !attachRuntimeMultiplayerRuntimeBridge(*clientWorld, *client.bridge(), clientAttachment, error)) return fail("full cold restore/attach");
    if (hostWorld->tick != 7 || clientWorld->tick != 7 || hostWorld->economy.credits != 9000 || clientWorld->economy.credits != 7000
        || hostWorld->world.getObjects().size() != 2 || hostWorld->wrecks.size() != 1 || hostWorld->buildingCollapses.size() != 1
        || runtimeMultiplayerAuthoritativeStateHash(*hostWorld, error) != saved.descriptor.identity.authoritativeStateHash
        || runtimeMultiplayerAuthoritativeStateHash(*clientWorld, error) != saved.descriptor.identity.authoritativeStateHash)
        return fail("restored authority/economy/objects differs");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto hp = host.bridge()->pump(false), cp = client.bridge()->pump(false);
        if (!hp.succeeded || !cp.succeeded) { error = hp.succeeded ? cp.error : hp.error;return fail("initial hash barrier"); }
        if (host.bridge()->initialStateHashVerified() && client.bridge()->initialStateHashVerified()) break;
        Sleep(1);
    }
    if (!host.bridge()->initialStateHashVerified() || !client.bridge()->initialStateHashVerified()) return fail("initial hash consensus timed out");
    while (std::chrono::steady_clock::now() < deadline && (hostWorld->tick < 8 || clientWorld->tick < 8)) {
        const auto hp = host.bridge()->pump(hostWorld->tick < 8), cp = client.bridge()->pump(false);
        if (!hp.succeeded || !cp.succeeded) { error = hp.succeeded ? cp.error : hp.error;return fail("first resumed frame"); }
        Sleep(1);
    }
    if (hostWorld->tick != 8 || clientWorld->tick != 8
        || runtimeMultiplayerAuthoritativeStateHash(*hostWorld, error) != runtimeMultiplayerAuthoritativeStateHash(*clientWorld, error))
        return fail("first resumed frame differs");
    std::cout << "Cold resume: old room destroyed, new identities, complete world/house restore, initial hash consensus and saved next frame passed.\n";
    error.clear();return true;
}
