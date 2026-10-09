// Two separate game processes use the production lobby, TCP bridge, world
// callbacks and snapshot/reconnect path. Only the rendezvous port is shared
// through a file; gameplay never passes through the fixture directory.
bool runRuntimeNetworkCooperativeLanSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* roleText = std::getenv("CONCORD_COOPERATIVE_LAN_ROLE");
    const char* directoryText = std::getenv("CONCORD_COOPERATIVE_LAN_DIRECTORY");
    if (!roleText || !directoryText) { error = "cooperative LAN fixture role/directory missing"; return false; }
    const bool host = std::string(roleText) == "host";
    const std::filesystem::path directory(directoryText);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    const auto fail = [&](std::string reason) { error = std::string(roleText) + ": " + reason; return false; };
    IniFile ini; CooperativeScenarioDatabase scenarios;
    if (!ini.loadFromFile(runtimeCooperativeScenarioPath(config).generic_string())
        || !scenarios.loadFromIni(ini)) return fail("original missions unavailable");
    const auto* scenario = scenarios.findByMapId("c1a01md");
    if (!scenario) return fail("original C1A01MD unavailable");
    RuntimeMultiplayerMatchSetup initial; std::filesystem::path mapPath;
    if (!prepareRuntimeCooperativeSmokeRoom(config, *scenario, initial, mapPath, error)) return false;
    RuntimeMultiplayerCompatibility compatibility;
    if (!makeRuntimeMultiplayerMenuCompatibility(config, compatibility, error)) return false;
    RuntimeMultiplayerLobbyFlow flow;
    if (host) {
        RuntimeMultiplayerLobbyHostCreateOptions lobby;
        lobby.map = initial.map; lobby.rules = initial.rules;
        lobby.hostMember.peerId = "host"; lobby.hostMember.displayName = "Player A";
        lobby.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
        lobby.slotCount = 4; lobby.computerSlotIndices = {2, 3};
        RuntimeMultiplayerLanHostOptions network;
        network.session.roomId = "cooperative-gameplay";
        network.session.hostPeerId = "host"; network.session.hostDisplayName = "Player A";
        network.session.compatibility = compatibility; network.session.maximumMembers = 2;
        network.roomName = "Original cooperative mission";
        network.sessionPort = 0; network.discoveryPort = 0; network.enableDiscovery = false;
        network.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
        network.reconnectGraceMilliseconds = 20000;
        if (!flow.startHost(network, lobby, error)) return false;
        std::cout << roleText << " created original mission room\n";
        for (std::size_t i : {0U, 2U, 3U}) {
            std::string publish;
            const auto edit = flow.setSlot(i, initial.slots[i], publish);
            if (!edit.accepted || !publish.empty()) return fail(edit.reason + " " + publish);
        }
        std::ofstream rendezvous(directory / "port.txt");
        rendezvous << flow.bridge()->lan().sessionPort() << '\n'; rendezvous.close();
    } else {
        int port = 0;
        while (std::chrono::steady_clock::now() < deadline && port == 0) {
            std::ifstream rendezvous(directory / "port.txt"); rendezvous >> port;
            if (!port) Sleep(10);
        }
        if (port <= 0 || port > 65535) return fail("host port unavailable");
        RuntimeMultiplayerLanJoinOptions join;
        join.room.roomId = "cooperative-gameplay"; join.room.roomName = "Original cooperative mission";
        join.room.hostName = "Player A"; join.room.hostAddress = "127.0.0.1";
        join.room.contentFingerprint = cooperativeFingerprintHex(compatibility.contentFingerprint);
        join.room.protocolVersion = runtimeLanProtocolVersion;
        join.room.sessionPort = static_cast<std::uint16_t>(port);
        join.room.playerCount = 1; join.room.maximumPlayers = 2;
        join.peerId = "guest"; join.displayName = "Player B"; join.compatibility = compatibility;
        if (!flow.join(join, error)) return false;
        std::cout << roleText << " joining original mission room\n";
    }
    bool readyRequested = false;
    while (flow.bridge()->lan().phase() != RuntimeMultiplayerLanPhase::Running) {
        if (std::chrono::steady_clock::now() >= deadline) return fail("lobby/start timeout");
        const auto polled = flow.poll(1);
        if (!polled.succeeded) return fail(polled.error);
        if (polled.matchStarted) break;
        if (!flow.canonicalSetupPublished() || flow.lobby().setup().members.size() != 2) continue;
        if (!flow.localMapVerified() && !flow.verifyLocalMap(mapPath, error)) return false;
        const auto& members = flow.bridge()->lan().session().members();
        const auto local = std::find_if(members.begin(), members.end(), [&](const auto& member) {
            return member.peerId == (host ? "host" : "guest");
        });
        if (local != members.end() && !local->ready && !readyRequested) {
            const auto ready = flow.setReady(true);
            if (!ready.succeeded) return fail(ready.reason);
            readyRequested = true;
        }
        std::string reason;
        if (host && flow.canStart(reason)) {
            const auto start = flow.startMatch();
            if (!start.succeeded) return fail(start.reason);
        }
    }
    const auto setup = flow.lobby().setup();
    std::cout << roleText << " original mission room started\n";
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    RuntimeSnapshotActiveStateScope active(state);
    RuntimeMultiplayerRuntimeAttachmentState attachment;
    auto bridge = flow.takeRunningBridge(error);
    if (!bridge || !bridge->beginPreAttachMaintenance(error)) return false;
    std::cout << roleText << " loading original mission world\n";
    if (!initializeRuntimeCooperativeSmokeWorld(state, config, setup, mapPath, !host, error)) return false;
    std::cout << roleText << " loaded original mission world\n";
    if (!attachRuntimeMultiplayerRuntimeBridge(state, *bridge, attachment, error))
        return fail(error + " bridge=" + bridge->fatalReason());
    std::cout << roleText << " entered original mission as " << state.economy.owner << '\n';
    int moverId = -1; RuntimeCellPoint moverStart;
    for (const auto& object : state.world.getObjects()) {
        if (object.alive && object.owner == state.economy.owner && object.kind == WorldObjectKind::Unit) {
            moverId = object.instanceId; moverStart = runtimeCellPointForObject(object); break;
        }
    }
    if (moverId < 0) return fail("starting controllable vehicle unavailable");
    const int initialCredits = state.economy.credits;
    bool movedCommand = false, queued = false, placed = false, interrupted = false;
    bool saveProposed = false, saveCommitted = false, wroteResult = false;
    int reportedTick = -1;
    const int minimumFinishTick = 1200;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto& session = bridge->lan().session();
        const int period = session.config().stateHashPeriodTicks;
        const int finishTick = session.startTick()
            + ((std::max(0, minimumFinishTick - session.startTick()) + period - 1) / period) * period;
        if (state.tick / 100 != reportedTick) {
            reportedTick = state.tick / 100;
            std::ofstream progress(directory / (host ? "host.progress" : "guest.progress"));
            progress << "tick=" << state.tick << " epoch=" << state.config.multiplayerTimelineEpoch
                << " save=" << saveCommitted << " placed=" << placed << '\n';
        }
        if (wroteResult && std::filesystem::exists(directory / (host ? "guest.done" : "host.done"))) break;
        if (bridge->initialStateHashVerified() && !movedCommand) {
            const auto* mover = findObjectById(state, moverId);
            const auto target = mover ? findRuntimePassableCellNear(state, *mover,
                {moverStart.x + 4, moverStart.y}, 6) : std::nullopt;
            if (!target) return fail("starting vehicle destination unavailable");
            RuntimeMultiplayerGameplayTargetOrderCommand move;
            move.objectIds = {moverId}; move.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
            move.target = RuntimeMultiplayerGameplayCellTarget{{target->x, target->y}};
            if (!bridge->captureGameplayCommand(RuntimeMultiplayerGameplayCommand{move}, error)) return false;
            movedCommand = true;
        }
        if (state.tick >= 30 && !queued) {
            RuntimeMultiplayerGameplayQueueProductionCommand production; production.typeId = "GAPOWR";
            if (!bridge->captureGameplayCommand(RuntimeMultiplayerGameplayCommand{production}, error)) return false;
            queued = true;
        }
        if (!placed) {
            const auto& ready = state.economy.localProductionLanes.lanes[0].ready;
            if (ready) {
                const auto item = runtimeBuildItemForBuilding(state, "GAPOWR");
                const auto cell = item ? findRuntimePlacementCellNear(state, *item,
                    state.economy.skirmishStartCell, 16) : std::nullopt;
                if (!cell) return fail("completed power plant placement unavailable");
                RuntimeMultiplayerGameplayPlaceBuildingCommand place;
                place.productionSerial = ready->serial; place.buildingTypeId = "GAPOWR";
                place.cell = {cell->x, cell->y};
                if (!bridge->captureGameplayCommand(RuntimeMultiplayerGameplayCommand{place}, error)) return false;
                placed = true;
            }
        }
        // Save at a fixed paused barrier, then exercise the actual unexpected
        // client TCP interruption and automatic shared snapshot recovery.
        if (host && state.tick == 500 && !saveProposed) {
            RuntimeMultiplayerSnapshotIdentity identity;
            identity.proposalId = 51; identity.snapshotToken = "cooperative-paid-production";
            identity.sourceTimelineEpoch = state.config.multiplayerTimelineEpoch;
            identity.barrierTick = state.tick; identity.snapshotTick = state.tick;
            identity.gameplaySetupFingerprint = state.config.multiplayerSetupHash;
            identity.authoritativeStateHash = runtimeMultiplayerAuthoritativeStateHash(state, error);
            identity.kind = RuntimeMultiplayerSnapshotKind::Save;
            RuntimeMultiplayerSnapshotMemoryImage image;
            if (!exportRuntimeMultiplayerSnapshotV15(state, identity, image, error)) return false;
            RuntimeMultiplayerSaveLoadProposal proposal;
            proposal.timelineEpoch = state.config.multiplayerTimelineEpoch; proposal.proposalId = 51;
            proposal.kind = RuntimeMultiplayerSaveLoadKind::Save; proposal.proposerPeerId = "host";
            proposal.descriptor = image.descriptor;
            proposal.tick = identity.snapshotTick; proposal.snapshotId = identity.snapshotToken;
            proposal.snapshotFingerprint = image.descriptor.snapshotFingerprint;
            const auto saved = bridge->proposeSaveLoad(proposal, std::move(image.bytes));
            if (!saved.succeeded) return fail("cooperative save: " + saved.reason);
            std::cout << roleText << " proposed shared progress save at " << state.tick << '\n';
            saveProposed = true;
        }
        for (const auto& handshake : bridge->lan().session().saveLoadHandshakes())
            if (handshake.proposal.proposalId == 51
                && handshake.status == RuntimeMultiplayerSaveLoadStatus::Committed && !saveCommitted) {
                saveCommitted = true;
                std::cout << roleText << " shared progress save committed at " << state.tick << '\n';
            }
        if (!host && state.tick >= 650 && saveCommitted && !interrupted) {
            if (!bridge->lan().interruptClientTransportForReconnect(error)) return false;
            std::cout << roleText << " interrupted own TCP connection at " << state.tick << '\n';
            interrupted = true;
        }
        const bool advance = state.tick < finishTick && (!host || state.tick != 500 || saveCommitted);
        const auto pumped = bridge->pump(advance, 1);
        if (wroteResult && std::filesystem::exists(directory / (host ? "guest.done" : "host.done"))) break;
        if (!pumped.succeeded || bridge->fatal()) {
            for (const auto& event : pumped.events)
                std::cout << roleText << " event=" << static_cast<int>(event.kind) << ' ' << event.reason << '\n';
            return fail("tick=" + std::to_string(state.tick) + " " + pumped.error + " " + bridge->fatalReason());
        }
        if (!wroteResult && state.tick == finishTick) {
            bool verified = false;
            for (const auto& frame : bridge->lan().session().stateHashFrames())
                if (frame.tick == finishTick && frame.complete && !frame.desync) verified = true;
            if (!verified) continue;
            const auto* mover = findObjectById(state, moverId);
            if (!mover || runtimeCellPointForObject(*mover) == moverStart || !placed
                || state.economy.completedBuildings < 1 || state.economy.credits >= initialCredits
                || !saveCommitted || state.config.multiplayerTimelineEpoch < 2
                || state.triggers.firedTriggers < 1)
                return fail("gameplay/recovery incomplete: " + state.economy.status
                    + " placed=" + std::to_string(placed) + " built=" + std::to_string(state.economy.completedBuildings)
                    + " epoch=" + std::to_string(state.config.multiplayerTimelineEpoch));
            const auto hash = runtimeMultiplayerAuthoritativeStateHash(state, error);
            std::ofstream result(directory / (host ? "host.done" : "guest.done"));
            result << state.tick << ' ' << hash << ' ' << state.config.multiplayerTimelineEpoch << '\n'; result.close();
            std::cout << roleText << " actual cooperative LAN: move=yes paid construction/placement=yes original events=yes"
                << " shared save=yes TCP reconnect/resume=yes tick=" << state.tick << " hash=" << hash << '\n';
            wroteResult = true;
        }
    }
    if (!wroteResult) return fail("actual cooperative LAN gameplay timeout tick=" + std::to_string(state.tick));
    return true;
}
