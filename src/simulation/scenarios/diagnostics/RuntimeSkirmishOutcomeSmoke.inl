bool runRuntimeSkirmishOutcomeSmokeProbe(const RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "MCV survival: " + reason;
        return false;
    };
    const std::string ownerOne = runtimeSkirmishOwnerForSlot(0);
    const std::string ownerTwo = runtimeSkirmishOwnerForSlot(1);
    const auto fixture = [&](bool multiplayer) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = live.config;
        state.config.gameMode = "Battle";
        state.config.multiplayerActive = multiplayer;
        state.config.shortGame = true;
        state.config.computerAiEnabled = false;
        state.config.playerSlots.clear();
        state.config.playerSlots.resize(2);
        for (std::size_t index = 0; index < 2; ++index) {
            auto& slot = state.config.playerSlots[index];
            slot.enabled = true;
            slot.human = index == 0 || multiplayer;
            slot.country = index == 0 ? "Americans" : "Russians";
            slot.team = "-";
        }
        state.rules = live.rules;
        state.rulesLoaded = true;
        state.art = live.art;
        state.artLoaded = true;
        state.economy.owner = ownerOne;
        state.economy.skirmishStartApplied = true;
        state.skirmishCountryByOwner[toLowerAsciiLocal(ownerOne)] = "Americans";
        state.skirmishCountryByOwner[toLowerAsciiLocal(ownerTwo)] = "Russians";
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        state.triggers.bootstrapComplete = true;
        state.tick = 10;
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 24; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            state.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(state);
        return storage;
    };
    const auto add = [](RuntimeWindowState& state, const std::string& owner,
            const char* type, RuntimeBuildKind kind, RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstanceForOwner(state, owner, kind, type, cell);
        if (object == nullptr) return -1;
        object->mission = "Guard";
        object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
        clearRuntimeRenderPosition(*object);
        return object->instanceId;
    };
    int outcomeCases = 0;
    for (bool multiplayer : {false, true}) for (const char* mcv : {"AMCV", "SMCV", "PCV"}) {
        for (bool localMcv : {false, true}) {
            auto storage = fixture(multiplayer);
            auto& state = *storage;
            const std::string& mcvOwner = localMcv ? ownerOne : ownerTwo;
            const std::string& yardOwner = localMcv ? ownerTwo : ownerOne;
            const int mcvId = add(state, mcvOwner, mcv, RuntimeBuildKind::Unit, {6, 6});
            const int yardId = add(state, yardOwner, "GACNST", RuntimeBuildKind::Building, {18, 18});
            if (mcvId < 0 || yardId < 0) return fail("stock base assets could not be created");
            if (advanceRuntimeSkirmishOutcome(state) || state.skirmishMatchFinished
                    || !state.multiplayerDefeatedOwners.empty())
                return fail(std::string(mcv) + " caused defeat or victory while still alive");
            auto* vehicle = findMutableObjectById(state, mcvId);
            vehicle->alive = false; vehicle->health = 0;
            state.tick += 10;
            if (!advanceRuntimeSkirmishOutcome(state) || !state.skirmishMatchFinished
                    || state.skirmishVictory == localMcv)
                return fail(std::string(mcv) + " destruction did not finish the match correctly");
            ++outcomeCases;
        }
    }

    // A remaining tank is sufficient only when full elimination is selected.
    for (bool shortGame : {false, true}) {
        auto storage = fixture(false);
        auto& state = *storage;
        state.config.shortGame = shortGame;
        if (add(state, ownerOne, "MTNK", RuntimeBuildKind::Unit, {6, 6}) < 0
                || add(state, ownerTwo, "GACNST", RuntimeBuildKind::Building, {18, 18}) < 0)
            return fail("ordinary unit fixture failed");
        advanceRuntimeSkirmishOutcome(state);
        if (state.skirmishMatchFinished != shortGame)
            return fail("ordinary combat vehicles changed Short Game semantics");
    }
    for (int invalidState = 0; invalidState < 3; ++invalidState) {
        auto storage = fixture(false);
        auto& state = *storage;
        const int id = add(state, ownerOne, "AMCV", RuntimeBuildKind::Unit, {6, 6});
        if (id < 0) return fail("invalid MCV fixture failed");
        auto* vehicle = findMutableObjectById(state, id);
        if (invalidState == 0) vehicle->health = 0;
        if (invalidState == 1) vehicle->alive = false;
        if (invalidState == 2) vehicle->owner = ownerTwo;
        if (!runtimeSkirmishOwnerDefeated(state, ownerOne))
            return fail("dead, inactive or captured-away MCV kept its former owner alive");
    }

    const char* boats[] = {"LCRF", "SAPC", "YHVR"};
    const char* mcvs[] = {"AMCV", "SMCV", "PCV"};
    for (int faction = 0; faction < 3; ++faction) {
        auto storage = fixture(false);
        auto& state = *storage;
        const int mcvId = add(state, ownerOne, mcvs[faction], RuntimeBuildKind::Unit, {6, 6});
        const int boatId = add(state, ownerOne, boats[faction], RuntimeBuildKind::Unit, {7, 6});
        if (mcvId < 0 || boatId < 0
                || add(state, ownerTwo, "GACNST", RuntimeBuildKind::Building, {18, 18}) < 0)
            return fail("transport fixture failed");
        applySelection(state, {mcvId}, false, false);
        if (!queueRuntimeTransportTarget(state, *findMutableObjectById(state, boatId))
                || !waitForRuntimeTransportSmokeMotion(state, boatId, mcvId))
            return fail(std::string(mcvs[faction]) + " could not board its original transport");
        // Natural boarding may end between the original ten-tick outcome gates.
        for (int syncTick = 0; state.tick % 10 != 0 && syncTick < 9; ++syncTick) {
            RuntimeSnapshotActiveStateScope active(state);
            ++state.tick; (void)advanceRuntimeParticles(state);
        }
        if (state.tick % 10 != 0) return fail("natural boarding outcome clock did not reach its scheduled gate");
        const auto* passenger = findRuntimeObjectByIdIncludingInactive(state, mcvId);
        if (!passenger || passenger->alive || passenger->health <= 0
                || runtimeSkirmishOwnerDefeated(state, ownerOne)
                || advanceRuntimeSkirmishOutcome(state) || state.skirmishMatchFinished)
            return fail("embarked MCV was counted as dead");
        // A stale manifest on a destroyed transport must never keep a house alive.
        auto* boat = findMutableObjectById(state, boatId);
        boat->alive = false; boat->health = 0;
        state.tick += 10;
        if (!advanceRuntimeSkirmishOutcome(state) || !state.skirmishMatchFinished || state.skirmishVictory)
            return fail("destroyed transport retained a surviving base");
    }
    std::cout << "MCV survival: " << outcomeCases << " offline/multiplayer local/enemy outcomes, "
        "Allied/Soviet/Yuri/captured MCVs, live cargo, destroyed hosts and full elimination passed\n";
    return true;
}
