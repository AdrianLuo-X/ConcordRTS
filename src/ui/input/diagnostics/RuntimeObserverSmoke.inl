#include "ui/selection/diagnostics/RuntimeObserverSelectionSmoke.inl"

bool runRuntimeObserverSmoke(const RuntimeConfig& config, std::string& error) {
    std::string scene;
    const auto fail = [&](const std::string& reason) {
        error = "observer " + scene + ": " + reason;
        return false;
    };
    const char* output = std::getenv("CONCORD_OBSERVER_OUTPUT");
    const std::filesystem::path root = output && *output ? output
        : "build/smoke/gameplay-fixes/observer";
    std::error_code directoryError;
    std::filesystem::create_directories(root, directoryError);
    if (directoryError) return fail("cannot create output directory");
    int cases = 0;
    for (bool aiEnabled : {false, true}) for (bool shortGame : {false, true}) {
        scene = std::string(aiEnabled ? "AI-on" : "AI-off")
            + (shortGame ? "-short" : "-full");
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = config;
        state.config.gameMode = "Battle";
        state.config.gameModeRulesOverride = "MPBattleMD.ini";
        state.config.playerCountry = "Observer";
        state.config.multiplayerActive = false;
        state.config.computerAiEnabled = aiEnabled;
        state.config.shortGame = shortGame;
        state.config.startingUnits = 4;
        state.config.startingCredits = 100000;
        state.config.techLevel = 10;
        state.config.crates = false;
        state.config.noCooldown = false;
        state.config.playerSlots.assign(3, {});
        const char* countries[] = {"Observer", "Americans", "Russians"};
        for (std::size_t i = 0; i < 3; ++i) {
            auto& slot = state.config.playerSlots[i];
            slot.enabled = true; slot.human = i == 0; slot.local = i == 0;
            slot.country = countries[i]; slot.team = "-";
            slot.name = "Observer smoke " + std::to_string(i);
            slot.color = i == 0 ? "Gold" : i == 1 ? "Red" : "Blue";
            // A non-zero legacy observer start must not reserve a map start.
            slot.startPosition = i == 0 ? 1 : static_cast<int>(i);
        }
        state.runtimeSoundsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        state.report = buildRuntimeStartupReport(state.config, &state.world,
            &state.theater, &state.art, &state.rules, &state.mapFacts);
        state.rulesLoaded = state.report.rulesLoaded;
        state.artLoaded = state.report.artLoaded;
        state.theaterLoaded = state.report.theaterLoaded;
        if (!state.report.success) return fail("original map/rules did not load");
        initializeRuntimeNavigation(state);
        initializeRuntimeEconomy(state);
        if (!initializeRuntimeSkirmishStart(state)) return fail(state.economy.skirmishStatus);
        state.triggers.bootstrapComplete = true;
        state.visionEnabled = true;
        updateRuntimeVisionState(state);
        const std::string observer = runtimeSkirmishOwnerForSlot(0);
        const std::string first = runtimeSkirmishOwnerForSlot(1);
        const std::string second = runtimeSkirmishOwnerForSlot(2);
        const auto liveObjects = [](const RuntimeWindowState& world, const std::string& owner) {
            return std::count_if(world.world.getObjects().begin(), world.world.getObjects().end(),
                [&](const auto& object) { return object.owner == owner && object.alive && object.health > 0; });
        };
        if (!runtimeLocalObserver(state.config)
                || !runtimeObserverCountry(runtimeCountryForOwner(state, observer))
                || liveObjects(state, observer) != 0
                || liveObjects(state, first) < 2 || liveObjects(state, second) < 2
                || !state.economy.skirmishStartApplied || state.economy.skirmishStartObjectId != -1
                || runtimeOwnersAreAllied(state, first, second))
            return fail("observer spawned assets, lost identity, or suppressed combatant starts");
        for (const auto& object : state.world.getObjects()) {
            if (object.alive && object.health > 0 && (object.owner == first || object.owner == second)
                    && !runtimeObjectVisibleToOwner(state, observer, object))
                return fail("observer cannot see a live combatant through opening shroud");
        }
        const auto* aiVision = state.vision.statsFor(first);
        if (!state.visionEnabled || runtimeLocalVisionRestricted(state)
                || !runtimeRadarAvailable(state)
                || aiVision == nullptr || aiVision->validCells <= aiVision->visibleCells)
            return fail("full observer view leaked into AI vision");
        if (aiEnabled && shortGame) {
            for (const std::string& owner : {first, second}) {
                const auto base = std::find_if(state.world.getObjects().begin(), state.world.getObjects().end(),
                    [&](const auto& object) { return object.owner == owner && object.alive && object.health > 0; });
                if (base == state.world.getObjects().end()) return fail("missing combatant render center");
                RuntimeSnapshotActiveStateScope active(state);
                const auto stats = drawRuntimeStateOffscreenAtCell(state, runtimeCellPointForObject(*base),
                    800, 600, error, pathString(root / (owner + "-observer-full-map.bmp")),
                    false, false, -1, nullptr, false, nullptr, false, true);
                if (!stats || !stats->rendered || stats->visibleVoxelBodies <= 0)
                    return fail("shroud-enabled observer render hid initial combatant vehicles: " + error);
            }
        }
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        std::string rejected;
        if (state.multiplayerIntentRouter.mayIssueAuthoritativeIntent()
                || state.multiplayerIntentRouter.queueProduction("MTNK",
                    RuntimeMultiplayerGameplayProductionInsertMode::AfterActive, rejected))
            return fail("observer can issue production orders");
        // Exercise the actual UI dispatch without a replay recorder or
        // network bridge; neither their presence nor a successful recording
        // startup may be required for observer command restrictions.
        const auto inspected = std::find_if(state.world.getObjects().begin(), state.world.getObjects().end(),
            [&](const auto& object) { return object.owner == first && object.alive
                && object.health > 0 && object.kind == WorldObjectKind::Unit; });
        if (inspected == state.world.getObjects().end()) return fail("missing inspected combatant unit");
        if (!runRuntimeObserverSelectionSmoke(state, inspected->instanceId, error)) return false;
        state.selectedObjectIds = {inspected->instanceId};
        const auto commandWorldHash = state.world.deterministicSimulationStateHash();
        int captureCount = 0;
        state.multiplayerIntentRouter.setCaptureCallback(
            [&captureCount](const RuntimeMultiplayerGameplayCommand&, std::string&) {
                ++captureCount; return true;
            });
        if (dispatchRuntimeSelectionCommandFromUi(state, RuntimeMultiplayerGameplayCommandType::Stop)
            || dispatchRuntimeTargetOrderFromUi(state, RuntimeMultiplayerGameplayTargetOrderKind::Move,
                {inspected->x + 1, inspected->y + 1})
            || captureCount != 0 || state.world.deterministicSimulationStateHash() != commandWorldHash)
            return fail("UI dispatch let an observer stop/move an inspected combatant without recording");
        state.multiplayerIntentRouter.setCaptureCallback({});
        state.selectedObjectIds.clear();
        for (state.tick = 1; state.tick <= 30; ++state.tick) {
            advanceRuntimeComputerAi(state);
            advanceRuntimeSkirmishOutcome(state);
            if (state.skirmishMatchFinished || liveObjects(state, observer) != 0)
                return fail("empty observer house ended the game or acquired initial units");
        }
        if (aiEnabled && (!runtimeOwnerHasConstructionYard(state, first)
                || !runtimeOwnerHasConstructionYard(state, second)))
            return fail("computers stopped their opening deployment while watched");

        const std::string savePath = pathString(root / (scene + ".ini"));
        if (!saveRuntimeState(state, savePath, error)) return fail("save: " + error);
        SkirmishSaveInfo saved;
        if (!inspectSkirmishSave(savePath, saved)) return fail("menu save metadata: " + saved.error);
        RuntimeConfig reopened = state.config;
        reopened.playerCountry = saved.playerCountry;
        reopened.playerSlots.clear();
        for (const auto& source : saved.players) {
            RuntimeConfig::PlayerSlot slot;
            slot.enabled = source.enabled; slot.human = source.human;
            slot.country = source.country; slot.color = source.color;
            slot.team = source.team; slot.name = source.name;
            slot.difficulty = source.difficulty; slot.startPosition = source.startPosition;
            reopened.playerSlots.push_back(slot);
        }
        if (!runtimeLocalObserver(reopened) || reopened.playerSlots.size() != 3)
            return fail("front-end save metadata discarded observer role");
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, state, true);
        restored->config = reopened;
        IniFile ini;
        if (!ini.loadFromFile(savePath) || !materializeRuntimeLoadStateFromIni(*restored, ini, error))
            return fail("load: " + error);
        if (!runtimeLocalObserver(restored->config) || liveObjects(*restored, observer) != 0
                || liveObjects(*restored, first) != liveObjects(state, first)
                || liveObjects(*restored, second) != liveObjects(state, second))
            return fail("round-trip load changed role or starting assets");
        refreshRuntimeMultiplayerPlayerIntentAuthority(*restored);
        if (restored->multiplayerIntentRouter.mayIssueAuthoritativeIntent())
            return fail("loaded observer regained command authority");
        restored->tick = 40;
        advanceRuntimeSkirmishOutcome(*restored);
        if (restored->skirmishMatchFinished) return fail("loaded observer immediately loses");
        for (auto& object : restored->world.accessObjects()) if (object.owner == second) {
            object.alive = false; object.health = 0;
        }
        restored->tick = 50;
        if (!advanceRuntimeSkirmishOutcome(*restored) || !restored->skirmishMatchFinished
                || restored->skirmishVictory || restored->skirmishResult != "MATCH COMPLETE")
            return fail("last surviving combatant did not finish the watched match neutrally");
        const std::string finishedPath = pathString(root / (scene + "-complete.ini"));
        if (!saveRuntimeState(*restored, finishedPath, error)) return fail("finished save: " + error);
        auto finished = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*finished, *restored, true);
        IniFile finishedIni;
        if (!finishedIni.loadFromFile(finishedPath)
                || !materializeRuntimeLoadStateFromIni(*finished, finishedIni, error))
            return fail("finished load: " + error);
        refreshRuntimeMultiplayerPlayerIntentAuthority(*finished);
        if (!finished->skirmishMatchFinished || finished->skirmishResult != "MATCH COMPLETE"
                || finished->skirmishVictory || finished->multiplayerIntentRouter.mayIssueAuthoritativeIntent())
            return fail("finished round trip loses neutral result or grants commands");
        for (const auto* completed : {restored.get(), finished.get()}) {
            const auto rows = runtimeMatchScoreRows(*completed);
            if (rows.size() < 2 || std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                    return runtimeCanonicalHumanHouseOwner(row.owner)
                        == runtimeCanonicalHumanHouseOwner(observer);
                })) return fail("score page listed the observer as a defeated combatant");
        }
        ++cases;
    }
    std::cout << "Observer runtime: " << cases
        << " AI/ShortGame cases, no observer assets, combatant starts, full view/AI fog, "
        "command rejection, opening AI, save/load, neutral match completion passed\n";
    return true;
}
