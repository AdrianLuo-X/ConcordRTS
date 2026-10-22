// W009 parent: real Win32 producer through the existing controllers and the
// original offline/lockstep consumers. This is an in-process loopback fixture,
// not two complete running game clients or physical keyboard/IME coverage.
bool runRuntimeKeyboardParentReconciliationSmoke(
    const RuntimeConfig& config, std::ostream& output) {
    using namespace RuntimeKeyboardParentDiagnostics;
    using RuntimeCursorSmokeDetail::BorrowResources;
    using RuntimeCursorSmokeDetail::snapshot;
    using RuntimeCursorSmokeDetail::writeNewBytes;
    using RuntimePointerIngressSmokeDetail::hex;
    std::size_t checks = 0, failures = 0, cases = 0, inputs = 0, saved = 0;
    std::size_t commandEvents = 0, packetCount = 0, loopbackDelays = 0;
    const auto need = [&](bool value, const std::string& why) {
        ++checks;
        if (!value) { ++failures; output << "Keyboard parent failure: " << why << std::endl; }
        return value;
    };
    const char* requested = std::getenv("CONCORD_KEYBOARD_PARENT_OUTPUT");
    if (!need(requested && *requested, "explicit private output")) return false;
    const std::filesystem::path directory(requested);
    std::error_code ec;
    if (!need(!std::filesystem::exists(directory / "keyboard-parent-trace.txt"),
        "fresh complete trace")) return false;
    std::filesystem::create_directories(directory, ec);
    if (!need(!ec, "private output directory")) return false;
    std::ofstream trace(directory / "keyboard-parent-trace.txt", std::ios::binary);
    if (!need(static_cast<bool>(trace), "complete trace stream")) return false;
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->runtimePreferencePersistenceEnabled = false;
    stock->report = buildRuntimeStartupReport(config, &stock->world,
        &stock->theater, &stock->art, &stock->rules, &stock->mapFacts);
    stock->rulesLoaded = stock->report.rulesLoaded;
    stock->artLoaded = stock->report.artLoaded;
    stock->theaterLoaded = stock->report.theaterLoaded;
    stock->mapFactsLoaded = stock->report.map.loaded;
    if (!need(stock->report.success && stock->world.getSummary().loaded,
        "original real package startup")) return false;
    const std::string resources = config.localDataRootPath;
    RuntimeInputCancellationSmokeDetail::Registration registration;
    if (!need(registration.ready, "existing owned NOACTIVATE window class")) return false;
    struct QuitCleanup {
        ~QuitCleanup() { MSG message{}; while (PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {} }
    } quitCleanup;
    const auto full = [&](RuntimeWindowState& state, const std::string& label) {
        RuntimeSnapshotActiveStateScope active(state);
        std::ostringstream bytes; std::string reason;
        const bool result = writeRuntimeStateToStream(state, bytes, reason);
        const auto stem = "snapshot-" + std::to_string(saved++);
        const auto file = directory / (stem + ".saved.ini");
        const std::string owned = snapshot(state) + ";menu=" +
            RuntimeInputCancellationSmokeDetail::menuState(state);
        std::ifstream preferences(state.config.configPath, std::ios::binary);
        std::ostringstream preferenceBytes; preferenceBytes << preferences.rdbuf();
        const bool written = writeNewBytes(file, bytes.str())
            && writeNewBytes(directory / (stem + ".owned.txt"), owned)
            && writeNewBytes(directory / (stem + ".preferences.bin"), preferenceBytes.str());
        trace << "snapshot label=" << label << " tick=" << state.tick
            << " originalResult=" << result << " reason=" << hex(reason)
            << " file=" << hex(pathString(file)) << " bytes=" << bytes.str().size()
            << " written=" << written << " queue=" << hex(state.playerCommands.serialize())
            << " lastCommandTick=" << state.lastCommand.tick << '\n';
        return need(written && result, "whole original serialization: " + label + ": " + reason);
    };
    const auto make = [&](const std::string& label, bool networkFixture = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config = config; state.config.gameMode = "Battle";
        state.config.gameModeRulesOverride.clear(); state.config.computerAiEnabled = false;
        state.config.multiplayerActive = false; state.config.shortGame = false;
        // Seed before the original economy/world initializers, as the normal
        // multiplayer launch does. This does not change the production seed.
        if (networkFixture) state.config.simulationRandomSeed = 0x3141592653589793ULL;
        state.config.startingCredits = 100000; state.config.startingUnits = 3;
        state.config.playerCountry = "Americans"; state.config.noCooldown = false;
        state.config.showMainMenu = state.config.showSkirmishMenu = false;
        state.config.keyboardReferenceLayout = true; state.config.keyBindings.clear();
        const auto privateRoot = directory / "private" / label;
        std::filesystem::create_directories(privateRoot, ec);
        if (!need(!ec, "new private runtime root " + label)) return std::unique_ptr<RuntimeWindowState>{};
        state.config.localDataRootPath = pathString(privateRoot);
        state.config.configPath = pathString(privateRoot / "preferences.ini");
        if (!need(writeNewBytes(state.config.configPath, "[Preserved]\nSentinel=keep\n"),
            "new private preferences")) return std::unique_ptr<RuntimeWindowState>{};
        state.config.playerSlots.assign(2, {});
        for (std::size_t index = 0; index < 2; ++index) {
            auto& slot = state.config.playerSlots[index];
            slot.enabled = slot.human = true; slot.local = index == 0;
            slot.peerId = index == 0 ? "host-peer" : "client-peer";
            slot.country = index == 0 ? "Americans" : "Russians";
            slot.team = "-"; slot.startPosition = static_cast<int>(index) + 1;
            slot.name = index == 0 ? "Host" : "Client";
            const auto owner = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[owner] = slot.country;
            state.skirmishTeamByOwner[owner] = runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report = stock->report; state.world = stock->world;
        state.world.accessObjects().clear();
        state.rules = stock->rules; state.art = stock->art; state.theater = stock->theater;
        state.mapFacts = stock->mapFacts; state.mapFactsLoaded = stock->mapFactsLoaded;
        state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
        state.terrainInfoByCell = stock->terrainInfoByCell;
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        state.runtimeSoundsEnabled = state.runtimeMusicEnabled = state.runtimeSoundEffectsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        state.runtimeMusicVolume = state.runtimeSoundVolume = state.runtimeVoiceVolume = 0;
        state.visionEnabled = false;
        {
            BorrowResources originalResources(state, resources);
            initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
            if (!need(initializeRuntimeSkirmishStart(state), "original two-house start"))
                return std::unique_ptr<RuntimeWindowState>{};
            state.triggers.bootstrapComplete = true;
            loadRuntimeSoundDatabase(state);
            initializeRuntimeWorldAudio(state);
            ensureRuntimeLightingInitialized(state); ensureRuntimeParticlesInitialized(state);
            ensureRuntimeMenuKeyboardLoaded(state);
            const RECT client{0, 0, 1024, 768};
            ensureRuntimeCameraForViewport(state, runtimeMapViewport(client));
        }
        state.runtimeMenuOpen = false; state.runtimeMenuKeyCaptureIndex = -1;
        state.activeSidebarTool = RuntimeSidebarTool::None;
        state.playerCommunication = {};
        state.playerLocalCommands.followTargeting = state.playerLocalCommands.rallyTargeting = false;
        initializeRuntimeOfflineGameplayRouting(state);
        for (const auto& object : state.world.getObjects()) {
            if (runtimePlayerMobileSelectable(state, object)) {
                state.selectedObjectIds = {object.instanceId}; break;
            }
        }
        if (!need(!state.selectedObjectIds.empty(), "real owned selectable unit"))
            return std::unique_ptr<RuntimeWindowState>{};
        return storage;
    };
    const auto bind = [&](RuntimeWindowState& state, const char* action, WPARAM raw) {
        const int code = runtimePlayerCurrentKeyCode(raw); bool found = false;
        for (auto& binding : state.runtimeMenuKeyboardBindings)
            if (binding.first == action) { binding.second = code; found = true; }
        if (!found) state.runtimeMenuKeyboardBindings.emplace_back(action, code);
        RuntimeSelectionKeyWin32Host selectionHost(state, raw);
        if (std::string(action) == "StopObject"
            && !need(selectionHost.binding(RuntimeSelectionKeyBinding::Stop, 'S') == code, "real Stop host binding")) return false;
        if (std::string(action) == "GuardObject"
            && !need(selectionHost.binding(RuntimeSelectionKeyBinding::Guard, 'G') == code, "real Guard host binding")) return false;
        if (std::string(action) == "ScatterObject"
            && !need(selectionHost.binding(RuntimeSelectionKeyBinding::Scatter, 'X') == code, "real Scatter host binding")) return false;
        return need(runtimeMenuKeyboardBindingCode(state, action, -1) == code, "exact current key binding");
    };
    const auto dump = [&](const Sink& sink, const std::string& label) {
        for (std::size_t index = 0; index < sink.keys.size(); ++index) {
            const auto& key = sink.keys[index];
            trace << "key label=" << label << " index=" << index << " raw=" << key.raw
                << " code=" << key.code << " shift=" << ((key.code & runtimePlayerShiftKey) != 0)
                << " control=" << ((key.code & runtimePlayerControlKey) != 0)
                << " alt=" << ((key.code & runtimePlayerAltKey) != 0) << '\n';
        }
        for (std::size_t index = 0; index < sink.events.size(); ++index) {
            const auto& event = sink.events[index];
            trace << "event label=" << label << " index=" << index << " stage=" << event.stage
                << " observedTick=" << event.observedTick << " frameTick=" << event.frameTick
                << " consumer=" << hex(event.consumer) << " peer=" << hex(event.peer)
                << " sequence=" << event.frameSequence << " commandIndex=" << event.commandIndex
                << " accepted=" << event.accepted << " hasCommand=" << event.hasCommand
                << " encoded=" << event.encoded << " bodyIndex=" << event.command.body.index()
                << " payload=" << hex(std::string(event.payload.begin(), event.payload.end()))
                << " error=" << hex(event.error) << '\n';
            if (event.hasCommand) ++commandEvents;
        }
    };
    const auto send = [&](RuntimeWindowState& state,
        RuntimeInputCancellationSmokeDetail::Window& window, const std::string& label,
        WPARAM raw, bool repeated, UINT message, Sink& sink) {
        if (!need(RuntimeInputCancellationSmokeDetail::witness(window, trace), "actual own window identity"))
            return false;
        if (!full(state, label + "-before")) return false;
        const int before = state.tick;
        const LPARAM flags = 0x00010001 | (repeated ? (static_cast<LPARAM>(1) << 30) : 0)
            | (message == WM_SYSKEYDOWN ? (static_cast<LPARAM>(1) << 29) : 0);
        LRESULT result = 0;
        const auto sourceEventsBefore = window.events.size();
        {
            RuntimeSnapshotActiveStateScope active(state);
            BorrowResources originalResources(state, resources);
            Binding observation(sink);
            window.recording = true;
            result = SendMessageW(window.handle, message, raw, flags);
            window.recording = false;
        }
        trace << "input label=" << label << " message=" << message << " raw=" << static_cast<std::uint64_t>(raw)
            << " flags=" << static_cast<std::uint64_t>(flags) << " repeated=" << repeated
            << " source=" << (message == WM_SYSKEYDOWN ? "system" : "ordinary")
            << " tickBefore=" << before << " tickAfter=" << state.tick
            << " result=" << result << " actualWndProc=" << (window.route == 2 && !sink.keys.empty())
            << " sourceEvents=" << (window.events.size() - sourceEventsBefore) << '\n';
        ++inputs;
        return need(window.route == 2 && !sink.keys.empty() && window.events.size() > sourceEventsBefore,
                "actual Shell key samplers and original owned native dispatch reached")
            && need(state.tick == before, "input keeps original authority tick")
            && full(state, label + "-after-input");
    };
    const std::array<std::pair<const char*, WPARAM>, 3> actions{{
        {"StopObject", VK_F13}, {"GuardObject", VK_F14}, {"ScatterObject", VK_F15}}};
    for (int source = 0; source < 2; ++source) for (const auto& action : actions)
        for (bool repeated : {false, true}) {
            const std::string label = std::string("offline-") + action.first + "-"
                + std::to_string(source) + "-" + std::to_string(repeated);
            auto storage = make(label); if (!storage) return false; auto& state = *storage;
            RuntimeInputCancellationSmokeDetail::Window window(state, 2, resources, trace);
            if (!need(window.handle && bind(state, action.first, action.second), "offline native prerequisite"))
                return false;
            Sink sink;
            if (!send(state, window, label, action.second, repeated,
                source == 0 ? WM_KEYDOWN : WM_SYSKEYDOWN, sink)) return false;
            dump(sink, label);
            std::size_t applied = 0;
            for (const auto& event : sink.events) if (event.stage == "offline-apply") {
                ++applied;
                if (!need(event.accepted && event.encoded && event.observedTick == state.tick,
                    "actual complete offline command applies at input tick")) return false;
            }
            if (!need(applied == (repeated ? 0U : 1U), "repeat and exact offline apply count")) return false;
            ++cases;
        }
    for (int mode = 0; mode < 3; ++mode) {
        const std::string label = "local-ui-" + std::to_string(mode);
        auto storage = make(label); if (!storage) return false; auto& state = *storage;
        RuntimeInputCancellationSmokeDetail::Window window(state, 2, resources, trace);
        if (mode != 2 && !bind(state, mode == 0 ? "ShowCoordinates" : "CenterBase", VK_F13)) return false;
        Sink sink;
        const auto beforeWorld = state.world.deterministicSimulationStateHash();
        const auto beforeQueue = state.playerCommands.serialize();
        if (!send(state, window, label, mode == 2 ? VK_LEFT : VK_F13, false, WM_KEYDOWN, sink)) return false;
        dump(sink, label);
        if (!need(sink.events.empty() && state.world.deterministicSimulationStateHash() == beforeWorld
            && state.playerCommands.serialize() == beforeQueue,
            "local UI has no authoritative capture/apply or world/queue mutation")) return false;
        ++cases;
    }
    for (const auto& action : actions) {
        const std::string label = std::string("legacy-") + action.first;
        auto storage = make(label); if (!storage) return false; auto& state = *storage;
        state.offlineGameplayIntentRouting = false;
        RuntimeInputCancellationSmokeDetail::Window window(state, 2, resources, trace);
        if (!bind(state, action.first, action.second)) return false;
        Sink sink;
        if (!send(state, window, label, action.second, false, WM_KEYDOWN, sink)) return false;
        dump(sink, label);
        if (!need(sink.events.empty(), "legacy path remains the original direct/queue owner")) return false;
        trace << "legacy label=" << label << " inputTick=" << state.tick
            << " lastCommandTick=" << state.lastCommand.tick
            << " queueBeforeAdvance=" << hex(state.playerCommands.serialize()) << '\n';
        { RuntimeSnapshotActiveStateScope active(state); BorrowResources originalResources(state, resources);
          ++state.tick; (void)advanceRuntimeSimulation(state, false); }
        if (!full(state, label + "-after-original-tick")) return false;
        trace << "legacyAdvance label=" << label << " completedTick=" << state.tick
            << " queueAfter=" << hex(state.playerCommands.serialize()) << '\n';
        ++cases;
    }
    {
        const std::string label = "legacy-planning-commit";
        auto storage = make(label); if (!storage) return false;
        auto& state = *storage;
        state.offlineGameplayIntentRouting = false;
        const int actorId = state.selectedObjectIds.front();
        const std::string owner = runtimeCanonicalHumanHouseOwner(state.economy.owner);
        RuntimeMultiplayerGameplayCommand queuedCommand;
        std::vector<std::uint8_t> queuedPayload;
        std::string queueError;
        {
            RuntimeSnapshotActiveStateScope active(state);
            BorrowResources originalResources(state, resources);
            if (!need(dispatchRuntimePlayerWaypointFromUi(state, RuntimePlayerWaypointOperation::Begin),
                "original non-router planning begin")) return false;
            const auto* actor = findObjectById(state, actorId);
            if (!need(actor && !runtimePlayerActorTemporarilyCommandLocked(state, *actor)
                && !runtimeFactoryDeliveryOwnsEgress(state, actorId), "original queue actor admitted")) return false;
            RuntimeMultiplayerGameplayTargetOrderCommand order;
            order.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
            order.objectIds = {actorId};
            order.target = RuntimeMultiplayerGameplayCellTarget{{actor->x, actor->y}};
            if (!need(appendRuntimePlayerWaypointOrder(state, owner, order),
                "original producer appends a complete planned order")) return false;
            const auto found = state.playerCommands.waypoints.routes.find(actorId);
            if (!need(found != state.playerCommands.waypoints.routes.end()
                && found->second.points.size() == 1 && !found->second.active
                && state.playerCommands.waypoints.planningOwners.contains(owner),
                "one real pending original route before the keyboard commit")) return false;
            queuedCommand.body = found->second.points.front().order;
            if (!need(encodeRuntimeMultiplayerGameplayCommand(queuedCommand, queuedPayload, queueError),
                "complete actual queued command payload: " + queueError)) return false;
        }
        const int inputTick = state.tick;
        const auto beforeQueue = state.playerCommands.serialize();
        trace << "queuedOrder label=" << label << " inputTick=" << inputTick
            << " actor=" << actorId << " owner=" << hex(owner)
            << " payload=" << hex(std::string(queuedPayload.begin(), queuedPayload.end()))
            << " completeQueue=" << hex(beforeQueue) << '\n';
        RuntimeInputCancellationSmokeDetail::Window window(state, 2, resources, trace);
        if (!need(window.handle && bind(state, "PlanningMode", VK_F16), "real queue commit input binding")) return false;
        Sink sink;
        if (!send(state, window, label, VK_F16, false, WM_KEYDOWN, sink)) return false;
        dump(sink, label);
        if (!need(sink.events.empty() && !state.playerLocalCommands.planning
            && !state.playerCommands.waypoints.planningOwners.contains(owner)
            && !state.playerCommands.waypoints.routes.at(actorId).active
            && state.tick == inputTick,
            "real Shell commit releases the original queue without consuming at input tick")) return false;
        {
            RuntimeSnapshotActiveStateScope active(state);
            BorrowResources originalResources(state, resources);
            ++state.tick;
            (void)advanceRuntimeSimulation(state, false);
        }
        const auto after = state.playerCommands.waypoints.routes.find(actorId);
        const bool actualAccepted = after != state.playerCommands.waypoints.routes.end()
            && after->second.active;
        trace << "queuedConsume label=" << label << " inputTick=" << inputTick
            << " completedTick=" << state.tick << " lastCommandTick=" << state.lastCommand.tick
            << " accepted=" << actualAccepted << " payload="
            << hex(std::string(queuedPayload.begin(), queuedPayload.end()))
            << " completeQueue=" << hex(state.playerCommands.serialize()) << '\n';
        if (!need(actualAccepted && state.lastCommand.tick == state.tick && state.tick == inputTick + 1,
            "original simulation loop consumes the pending real command at its original tick")) return false;
        if (!full(state, label + "-after-original-tick")) return false;
        ++cases;
    }
    for (int delay : {0, 2}) {
        const std::string label = "loopback-delay-" + std::to_string(delay);
        auto hostStorage = make(label + "-host", true), clientStorage = make(label + "-client", true);
        if (!hostStorage || !clientStorage) return false;
        auto& hostState = *hostStorage; auto& clientState = *clientStorage;
        RuntimeMultiplayerLockstepConfig lockstep; lockstep.inputDelayTicks = delay;
        RuntimeMultiplayerRuntimeBridge host(lockstep), client(lockstep);
        RuntimeMultiplayerRuntimeAttachmentState hostAttachment, clientAttachment;
        RuntimeMultiplayerLanHostOptions options;
        options.session.roomId = "w009-parent-room";
        options.session.hostPeerId = "host-peer"; options.session.hostDisplayName = "Host";
        options.session.compatibility.protocolFingerprint = 0x1020304050607080ULL;
        options.session.compatibility.contentFingerprint = 0x8877665544332211ULL;
        options.session.stateHashPeriodTicks = 1;
        options.roomName = "W009 parent private loopback"; options.discoveryPort = 0; options.sessionPort = 0;
        options.enableDiscovery = false; options.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
        options.roomGeneration = 0x5730303950410000ULL + static_cast<std::uint64_t>(delay);
        std::string error;
        if (!need(host.lan().startHost(options, error), "original loopback host: " + error)) return false;
        RuntimeMultiplayerLanJoinOptions join;
        join.room.roomId = options.session.roomId; join.room.roomName = options.roomName;
        join.room.hostName = "Host"; join.room.hostAddress = "127.0.0.1";
        join.room.contentFingerprint = "8877665544332211";
        join.room.protocolVersion = runtimeLanProtocolVersion;
        join.room.sessionPort = host.lan().sessionPort(); join.room.playerCount = 1;
        join.room.maximumPlayers = static_cast<std::uint8_t>(options.session.maximumMembers);
        join.peerId = "client-peer"; join.displayName = "Client";
        join.compatibility = options.session.compatibility; join.connectTimeoutMilliseconds = 2000;
        if (!need(client.lan().join(join, error), "original loopback join: " + error)) return false;
        const auto pump = [&](bool advance) {
            RuntimeMultiplayerRuntimePumpResult hp, cp;
            { RuntimeSnapshotActiveStateScope active(hostState); BorrowResources originalResources(hostState, resources);
              hp = host.pump(advance); }
            { RuntimeSnapshotActiveStateScope active(clientState); BorrowResources originalResources(clientState, resources);
              cp = client.pump(false); }
            trace << "pump label=" << label << " requestAdvance=" << advance
                << " hostOk=" << hp.succeeded << " clientOk=" << cp.succeeded
                << " hostCommitted=" << hp.tickCommitted << " clientCommitted=" << cp.tickCommitted
                << " hostTick=" << hostState.tick << " clientTick=" << clientState.tick
                << " hostError=" << hex(hp.error) << " clientError=" << hex(cp.error) << '\n';
            return need(hp.succeeded && cp.succeeded, "original bridge pump");
        };
        const auto until = [&](const std::function<bool()>& done, bool advance) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (std::chrono::steady_clock::now() < deadline) {
                if (!pump(advance)) return false;
                if (done()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return need(false, "bounded loopback barrier");
        };
        if (!until([&]() { return host.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
            && client.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
            && host.lan().session().members().size() == 2 && client.lan().session().members().size() == 2; }, false))
            return false;
        RuntimeMultiplayerMatchSetup setup; setup.revision = 1; setup.hostPeerId = "host-peer";
        setup.map.canonicalId = "maps/w009-parent-fixture.map";
        setup.map.contentFingerprint = options.session.compatibility.contentFingerprint;
        setup.rules.gameMode = "battle"; setup.rules.rulesOverride = "mpbattlemd.ini";
        setup.rules.randomSeed = 0x3141592653589793ULL; setup.rules.fixedTickHz = 30;
        setup.rules.startingCredits = 100000; setup.rules.startingUnits = 3;
        setup.rules.techLevel = 10; setup.rules.gameSpeed = 2;
        RuntimeMultiplayerMatchMember hm; hm.peerId = "host-peer"; hm.displayName = "Host";
        hm.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
        RuntimeMultiplayerMatchMember cm = hm; cm.peerId = "client-peer"; cm.displayName = "Client";
        setup.members = {cm, hm};
        for (std::size_t index = 0; index < 2; ++index) {
            RuntimeMultiplayerMatchSlot slot; slot.slotIndex = static_cast<decltype(slot.slotIndex)>(index);
            slot.controller = RuntimeMultiplayerMatchSlotController::Human;
            slot.peerId = index == 0 ? "host-peer" : "client-peer";
            slot.countryId = "random"; slot.colorId = index == 0 ? "red" : "blue";
            slot.teamId = "-"; slot.difficultyId = "medium"; slot.startPosition = static_cast<int>(index) + 1;
            setup.slots.push_back(std::move(slot));
        }
        if (!need(host.lan().setMatchSetup(setup).succeeded, "original canonical room setup")) return false;
        if (!until([&]() { return host.lan().session().matchSetup().has_value()
            && client.lan().session().matchSetup().has_value(); }, false)) return false;
        if (!need(host.lan().setReady(true).succeeded && client.lan().setReady(true).succeeded,
            "original ready intents")) return false;
        if (!until([&]() { return host.lan().session().allMembersReady()
            && client.lan().session().allMembersReady(); }, false)) return false;
        if (!need(host.lan().startMatch(1).succeeded, "original start barrier")) return false;
        if (!until([&]() { return host.lan().phase() == RuntimeMultiplayerLanPhase::Running
            && client.lan().phase() == RuntimeMultiplayerLanPhase::Running
            && host.lockstepBound() && client.lockstepBound(); }, false)) return false;
        std::uint64_t setupHash = 0;
        if (!need(runtimeMultiplayerGameplaySetupFingerprint(setup, setupHash, error), "original setup fingerprint")) return false;
        for (auto* state : {&hostState, &clientState}) {
            state->config.multiplayerActive = true; state->offlineGameplayIntentRouting = false;
            state->config.multiplayerSetupHash = setupHash;
            state->config.multiplayerCanonicalMapId = setup.map.canonicalId;
            state->config.multiplayerMapContentFingerprint = setup.map.contentFingerprint;
            if (!need(state->config.simulationRandomSeed == setup.rules.randomSeed,
                "original launch seed was initialized before world startup")) return false;
            state->config.multiplayerTimelineEpoch = host.lan().session().timelineEpoch();
            for (std::size_t index = 0; index < 2; ++index)
                state->config.playerSlots[index].local = (state == &hostState ? index == 0 : index == 1);
        }
        {
            RuntimeSnapshotActiveStateScope active(hostState); BorrowResources originalResources(hostState, resources);
            const auto originalHash = runtimeMultiplayerAuthoritativeStateHash(hostState, error);
            trace << "initialAuthority receiver=host hash=" << originalHash << " error=" << hex(error) << '\n';
            if (!need(originalHash != 0, "original full Shell host hash: " + error)) return false;
            const bool attached = attachRuntimeMultiplayerRuntimeBridge(hostState, host, hostAttachment, error);
            trace << "attachment receiver=host accepted=" << attached << " error=" << hex(error) << '\n';
            if (!need(attached, "original full Shell host attach: " + error)) return false;
        }
        {
            RuntimeSnapshotActiveStateScope active(clientState); BorrowResources originalResources(clientState, resources);
            const auto originalHash = runtimeMultiplayerAuthoritativeStateHash(clientState, error);
            trace << "initialAuthority receiver=client hash=" << originalHash << " error=" << hex(error) << '\n';
            if (!need(originalHash != 0, "original full Shell client hash: " + error)) return false;
            const bool attached = attachRuntimeMultiplayerRuntimeBridge(clientState, client, clientAttachment, error);
            trace << "attachment receiver=client accepted=" << attached << " error=" << hex(error) << '\n';
            if (!need(attached, "original full Shell client attach: " + error)) return false;
        }
        if (!until([&]() { return host.initialStateHashVerified() && client.initialStateHashVerified(); }, false)) return false;
        RuntimeInputCancellationSmokeDetail::Window window(hostState, 2, resources, trace);
        if (!need(window.handle != nullptr, "real loopback producer HWND")) return false;
        for (const auto& action : actions) {
            const std::string itemLabel = label + "-" + action.first;
            if (!bind(hostState, action.first, action.second)) return false;
            Sink sink;
            hostState.multiplayerIntentRouter.setCaptureCallback([&](const RuntimeMultiplayerGameplayCommand& command,
                std::string& why) {
                const bool accepted = host.captureGameplayCommand(command, why);
                RuntimeKeyboardParentDiagnostics::command("mp-capture", hostState.tick,
                    hostState.economy.owner, command, accepted, why);
                return accepted;
            });
            if (!send(hostState, window, itemLabel, action.second, false, WM_KEYDOWN, sink)) return false;
            const int inputTick = hostState.tick;
            if (!need(host.pendingGameplayCommandCount() == 1 && sink.events.size() == 1
                && sink.events.front().accepted && sink.events.front().stage == "mp-capture",
                "actual capture owns one complete command before any authority mutation")) return false;
            const auto expectedPayload = sink.events.front().payload;
            {
                Binding observation(sink);
                if (!until([&]() {
                    bool hostApplied = false, clientApplied = false;
                    for (const auto& event : sink.events) if (event.stage == "mp-apply" && event.accepted
                        && event.payload == expectedPayload) {
                        hostApplied = hostApplied || event.consumer == hostState.economy.owner;
                        clientApplied = clientApplied || event.consumer == clientState.economy.owner;
                    }
                    return hostApplied && clientApplied;
                }, true)) return false;
            }
            dump(sink, itemLabel);
            std::size_t applied = 0;
            for (const auto& event : sink.events) if (event.stage == "mp-apply" && event.payload == expectedPayload) {
                ++applied;
                if (!need(event.accepted && event.encoded && event.frameTick > inputTick
                    && event.observedTick == event.frameTick - 1, "actual original apply precedes the committed tick")) return false;
                bool advanced = false;
                for (const auto& tick : sink.events) if (tick.stage == "mp-advance"
                    && tick.consumer == event.consumer && tick.frameTick == event.frameTick
                    && tick.observedTick == tick.frameTick) advanced = true;
                if (!need(advanced, "same real consumer advanced its original commit tick")) return false;
            }
            if (!need(applied == 2, "both actual Shell consumers applied this exact owned command")) return false;
            for (const auto& event : sink.events) if (event.stage == "mp-apply" && event.payload == expectedPayload) {
                bool matched = false;
                for (const auto& owned : sink.packets) {
                    if (owned.first != event.consumer) continue;
                    const auto& packet = owned.second;
                    if (packet.tick != event.frameTick || packet.peerId != event.peer
                        || packet.sequence != event.frameSequence) continue;
                    RuntimeMultiplayerGameplayFrame frame; std::string why;
                    if (!need(decodeRuntimeMultiplayerGameplayFrame(packet.payload, frame, why),
                        "complete actual received wire frame decoded: " + why)) return false;
                    if (event.commandIndex >= frame.commands.size()) continue;
                    std::vector<std::uint8_t> body;
                    if (!need(encodeRuntimeMultiplayerGameplayCommand(frame.commands[event.commandIndex], body, why),
                        "complete wire command encoded")) return false;
                    matched = body == event.payload;
                }
                if (!need(matched, "actual received peer/sequence/tick/index and complete payload join to real Shell apply"))
                    return false;
            }
            for (const auto& owned : sink.packets) {
                const auto& packet = owned.second;
                trace << "receivedPacket label=" << itemLabel << " receiver="
                    << (owned.first == hostState.economy.owner ? "host" : "client")
                    << " source=before-original-apply tick=" << packet.tick
                    << " peer=" << hex(packet.peerId) << " sequence=" << packet.sequence
                    << " epoch=" << packet.timelineEpoch
                    << " payload=" << hex(std::string(packet.payload.begin(), packet.payload.end())) << '\n';
                ++packetCount;
            }
            if (!full(hostState, itemLabel + "-host-after-commit")
                || !full(clientState, itemLabel + "-client-after-commit")) return false;
            hostState.multiplayerIntentRouter.setCaptureCallback({});
            ++cases;
        }
        ++loopbackDelays;
    }
    trace << "summary checks=" << checks << " cases=" << cases << " inputs=" << inputs
        << " saved=" << saved << " commandEvents=" << commandEvents << " packets=" << packetCount
        << " loopbackDelays=" << loopbackDelays << " failures=" << failures << '\n';
    trace.flush();
    if (!need(static_cast<bool>(trace), "whole trace flush")) return false;
    output << "Keyboard parent reconciliation passed: checks=" << checks << " cases=" << cases
        << " inputs=" << inputs << " saved=" << saved << " commandEvents=" << commandEvents
        << " packets=" << packetCount << " loopbackDelays=" << loopbackDelays
        << " failures=" << failures << " actualWndProc=1 originalOfflineApply=1 originalShellCallbacks=1"
        << " physicalInput=not-run fullIME=not-run twoCompleteClients=not-run performance=not-run\n";
    return failures == 0;
}
