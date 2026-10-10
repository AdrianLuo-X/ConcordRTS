bool runRuntimeAmphibiousTransportSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "amphibious transport: " + message; return false;
    };
    const auto makeState = [&](int hz) {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& s = *result;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.config.defaultMapPath = "amphibious-transport-probe";
        s.rules = live.rules; s.rulesLoaded = true; s.art = live.art; s.artLoaded = true;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        return result;
    };
    const auto add = [](RuntimeWindowState& s, RuntimeBuildKind kind, const char* type, RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstance(s, kind, type, cell);
        if (object == nullptr) return -1;
        object->mission = "Guard"; clearRuntimeRenderPosition(*object);
        return object->instanceId;
    };
    const auto water = [](RuntimeWindowState& s) {
        // This fixture supplies navigation metadata directly, like the terrain
        // parser. Reapply it after loading the synthetic map's flat tiles.
        for (int y = 1; y <= 32; ++y) for (int x = 11; x <= 20; ++x) {
            RuntimeTerrainInfo info;
            info.kind = RuntimeTerrainKind::Water; info.water = true;
            info.authoredLandType = RuntimeTerrainLandType::Water;
            s.terrainInfoByCell[runtimeCellKey(x, y)] = info;
        }
        s.pathTerrainCache.valid = s.pathTerrainCache.dynamicValid = false;
        s.pathFailureCache.clear(); s.attackApproachCache.clear();
    };
    const auto step = [](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    const auto board = [&](RuntimeWindowState& s, int hostId, const std::vector<int>& passengers) {
        applySelection(s, passengers, false, false);
        const auto* host = findObjectById(s, hostId);
        if (host == nullptr || !queueRuntimeTargetAbility(s, *host)) return false;
        for (int t = 0; t < s.config.fixedTickHz * 40 && !s.pendingTransportTargets.empty(); ++t) step(s);
        host = findObjectById(s, hostId);
        return host != nullptr && std::all_of(passengers.begin(), passengers.end(), [&](int id) {
            return std::find(host->transportPassengerIds.begin(), host->transportPassengerIds.end(), id)
                != host->transportPassengerIds.end();
        });
    };
    const auto move = [&](RuntimeWindowState& s, int hostId, RuntimeCellPoint destination) {
        applySelection(s, {hostId}, false, false); issueRuntimeMoveCommand(s, destination);
        for (int t = 0; t < s.config.fixedTickHz * 50; ++t) {
            step(s);
            const auto* host = findObjectById(s, hostId);
            if (host == nullptr) return false;
            for (int id : host->transportPassengerIds) {
                const auto* passenger = findRuntimeObjectByIdIncludingInactive(s, id);
                if (passenger == nullptr || passenger->alive || passenger->cell != host->cell) return false;
            }
            if (runtimeCellPointForObject(*host) == destination && !host->renderMoveActive) return true;
        }
        return false;
    };
    const auto savePath = pathString(std::filesystem::current_path() / "runtime_amphibious_transport_smoke.ini");
    const auto roundTrip = [&](RuntimeWindowState& s) {
        return saveRuntimeState(s, savePath, error) && loadRuntimeState(s, savePath, error);
    };
    for (const char* boatType : {"LCRF", "SAPC", "YHVR"}) {
        for (int hz : {30, 60}) {
            auto storage = makeState(hz); auto& s = *storage;
            RuntimeSnapshotActiveStateScope scope(s);
            const int hostId = add(s, RuntimeBuildKind::Unit, boatType, {10, 16});
            std::vector<int> tanks;
            for (int i = 0; i < 5; ++i) tanks.push_back(add(s, RuntimeBuildKind::Unit, "MTNK", {4, 12 + i * 2}));
            initializeRuntimeNavigation(s); water(s);
            auto* host = findMutableObjectById(s, hostId);
            if (host == nullptr || !runtimeAmphibiousTransport(s, *host)
                || runtimeTransportProfileForObject(s, *host).passengerCapacity != 12
                || runtimeTransportPassengerSizeLimit(s, *host) != 6) return fail("stock hull rules missing");
            auto* tank = findMutableObjectById(s, tanks.front());
            if (tank == nullptr) return fail("stock tank missing");
            tank->health = 231; tank->veterancy = 1; tank->veterancyExperience = 17;
            findMutableObjectById(s, tanks[1])->veterancy = 2;
            if (!findRuntimePath(s, *tank, runtimeCellPointForObject(*tank), {24, 16}).empty())
                return fail("ground vehicle crossed the water fixture without a transport");
            applySelection(s, tanks, false, false);
            if (runtimeTargetCursorIntent(s, *host) != RuntimeTargetCursorIntent::Enter
                || !queueRuntimeTargetAbility(s, *host) || s.pendingTransportTargets.size() != 4
                || runtimeTransportReservedPassengerCount(s, hostId) != 12)
                return fail(std::string(boatType) + " did not reserve four Size=3 tanks");
            // Reissuing a group order must neither double-count nor overbook.
            if (!queueRuntimeTargetAbility(s, *host) || s.pendingTransportTargets.size() != 4)
                return fail("repeated group boarding changed capacity reservations");
            if (!roundTrip(s)) return false;
            water(s);
            for (int t = 0; t < hz * 40 && !s.pendingTransportTargets.empty(); ++t) step(s);
            host = findMutableObjectById(s, hostId);
            if (host->transportPassengerIds.size() != 4 || !s.pendingTransportTargets.empty())
                return fail(std::string(boatType) + " tanks did not finish boarding: " + s.simulationStatus);
            // Compare persistent cargo state at embarkation and disembarkation.
            const int embarkedHealth = findRuntimeObjectByIdIncludingInactive(s, tanks.front())->health;
            const auto pips = runtimeCargoPipDisplayForObject(s, *host);
            applySelection(s, {tanks.back()}, false, false);
            if (!pips.available || pips.totalUnits != 12 || pips.filledUnits != 12
                || runtimeTargetCursorIntent(s, *host) != RuntimeTargetCursorIntent::NoEnter)
                return fail("full hold cursor/pips did not account for vehicle Size");
            if (!move(s, hostId, {16, 16}) || !roundTrip(s)) return fail("loaded water transit/save failed: " + error);
            water(s); host = findMutableObjectById(s, hostId);
            if (!executeRuntimeSecondaryAction(s, hostId) || host->transportPassengerIds.size() != 4)
                return fail("deep-water unload lost ground vehicles");
            if (!move(s, hostId, {23, 16}) || !executeRuntimeSecondaryAction(s, hostId))
                return fail("transport could not cross water and unload at destination");
            host = findMutableObjectById(s, hostId);
            if (!host->transportPassengerIds.empty()) return fail("shore unload left tanks aboard");
            std::set<int> exits;
            for (int i = 0; i < 4; ++i) {
                tank = findMutableObjectById(s, tanks[i]);
                if (tank == nullptr || tank->health <= 0 || tank->x <= 20 || !exits.insert(tank->cell).second
                    || runtimeCellDistance(runtimeCellPointForObject(*tank), {23, 16}) > 1.5)
                    return fail("unloaded tanks were lost, overlapped or appeared beyond the hull");
            }
            tank = findMutableObjectById(s, tanks.front());
            if (tank->health != embarkedHealth || tank->veterancy != 1 || tank->veterancyExperience != 17
                || findObjectById(s, tanks[1])->veterancy != 2)
                return fail("cargo health/veterancy changed across transit and save/load");
        }
    }
    auto storage = makeState(30); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    const int hostId = add(s, RuntimeBuildKind::Unit, "LCRF", {16, 16});
    std::vector<int> infantry;
    for (int i = 0; i < 12; ++i) infantry.push_back(add(s, RuntimeBuildKind::Infantry, "E1", {9 + i % 4, 12 + i / 4}));
    initializeRuntimeNavigation(s);
    if (!board(s, hostId, infantry)) return fail("twelve infantry did not board");
    if (!roundTrip(s)) return false;
    std::vector<int> blockers;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (dx || dy) blockers.push_back(add(s, RuntimeBuildKind::Unit, "MTNK", {16 + dx, 16 + dy}));
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(s);
    if (!executeRuntimeSecondaryAction(s, hostId)
        || findObjectById(s, hostId)->transportPassengerIds.size() != 12)
        return fail("blocked unload placed infantry beyond the blocking vehicles");
    for (std::size_t i = 0; i < blockers.size(); ++i)
        setRuntimeObjectCell(s, *findMutableObjectById(s, blockers[i]), {2 + static_cast<int>(i) * 2, 29});
    refreshRuntimeObjectOccupancyAfterLocalChange(s);
    if (!executeRuntimeSecondaryAction(s, hostId)) return fail("unload did not resume after clearing the exit");
    if (!findObjectById(s, hostId)->transportPassengerIds.empty()) return fail("full infantry hold did not unload");
    std::set<std::pair<int, int>> infantrySlots;
    for (int id : infantry) {
        const auto* unit = findObjectById(s, id);
        if (unit == nullptr || unit->subCell < 0 || !infantrySlots.emplace(unit->cell, unit->subCell).second)
            return fail("infantry exits did not use distinct ground subcells");
    }
    // Mixed cargo includes a loaded IFV: its soldier must stay in the IFV
    // through the outer boat's save/load, unloading and destruction lifecycle.
    auto nestedStorage = makeState(30); auto& n = *nestedStorage;
    RuntimeSnapshotActiveStateScope nestedScope(n);
    const int boatId = add(n, RuntimeBuildKind::Unit, "YHVR", {15, 16});
    const int ifvId = add(n, RuntimeBuildKind::Unit, "FV", {10, 12});
    const int riderId = add(n, RuntimeBuildKind::Infantry, "E1", {9, 12});
    const int mcvId = add(n, RuntimeBuildKind::Unit, "AMCV", {8, 16});
    const int bruteId = add(n, RuntimeBuildKind::Infantry, "BRUTE", {9, 18});
    const int soldierId = add(n, RuntimeBuildKind::Infantry, "E2", {8, 19});
    initializeRuntimeNavigation(n);
    auto* boat = findMutableObjectById(n, boatId);
    for (const char* type : {"LCRF", "DEST", "ORCA", "DISK"}) {
        auto candidate = *findObjectById(n, ifvId); candidate.type = type;
        if (runtimeObjectCanEnterTransport(n, candidate, *boat, false)) return fail("invalid air/naval/oversize cargo accepted");
    }
    auto enemy = *findObjectById(n, ifvId); enemy.owner = "Russians";
    if (runtimeObjectCanEnterTransport(n, enemy, *boat, false)
        || runtimeObjectCanEnterTransport(n, *boat, *boat, false)) return fail("enemy/self boarding accepted");
    if (!board(n, ifvId, {riderId}) || !board(n, boatId, {ifvId, mcvId, bruteId, soldierId}))
        return fail("mixed infantry/MCV/loaded IFV boarding failed");
    boat = findMutableObjectById(n, boatId);
    if (runtimeTransportUsedSpace(n, *boat) != 12 || !roundTrip(n)) return fail("mixed hold save failed: " + error);
    if (!move(n, boatId, {23, 23}) || !executeRuntimeSecondaryAction(n, boatId)) return fail("nested cargo transit failed");
    const auto* ifv = findObjectById(n, ifvId);
    const auto* rider = findRuntimeObjectByIdIncludingInactive(n, riderId);
    if (ifv == nullptr || rider == nullptr || ifv->transportPassengerIds != std::vector<int>{riderId}
        || rider->alive || rider->cell != ifv->cell || ifv->activeGunnerMode < 0)
        return fail("loaded IFV lost its soldier/gunner when disembarking");
    if (!roundTrip(n) || !board(n, boatId, {ifvId})) return fail("nested cargo could not reboard");
    RuntimeObjectExitRequest death;
    death.objectId = boatId; death.kind = RuntimeObjectExitKind::Destroyed; death.tick = n.tick;
    death.mission = "Destroyed"; death.reason = "amphibious cargo destruction probe";
    if (queueRuntimeObjectExit(n, std::move(death)) != RuntimeObjectExitQueueResult::Queued
        || !drainRuntimeObjectExitQueue(n)) return fail("boat destruction lifecycle did not execute");
    for (int id : {boatId, ifvId, riderId}) {
        const auto* object = findRuntimeObjectByIdIncludingInactive(n, id);
        if (object == nullptr || object->alive || object->health > 0 || !object->transportPassengerIds.empty())
            return fail("boat destruction orphaned nested passengers");
    }
    auto miningStorage = makeState(30); auto& m = *miningStorage;
    RuntimeSnapshotActiveStateScope miningScope(m);
    const int miningBoat = add(m, RuntimeBuildKind::Unit, "SAPC", {10, 16});
    const int minerId = add(m, RuntimeBuildKind::Unit, "HARV", {7, 16});
    initializeRuntimeNavigation(m); water(m);
    auto& ore = m.harvesterTasks[minerId];
    ore.cargoValue = 1200; ore.cargoUnits = 12; ore.cargoGemUnits = 2; ore.cargoHasGems = true;
    normalizeRuntimeHarvesterTask(m, *findObjectById(m, minerId), ore);
    if (!board(m, miningBoat, {minerId}) || !move(m, miningBoat, {16, 16}) || !roundTrip(m))
        return fail("laden ore miner transport/save failed: " + error);
    water(m);
    if (!move(m, miningBoat, {23, 16}) || !executeRuntimeSecondaryAction(m, miningBoat))
        return fail("laden ore miner did not unload");
    const auto cargo = m.harvesterTasks.find(minerId);
    if (findObjectById(m, minerId) == nullptr || cargo == m.harvesterTasks.end()
        || cargo->second.cargoValue != 1200 || cargo->second.cargoUnits != 12 || cargo->second.cargoGemUnits != 2)
        return fail("transporting/saving an ore miner discarded its ore");
    std::cout << "Amphibious transport: LCRF/SAPC/YHVR at 30/60 Hz, Size capacity, "
        "water crossing, full infantry hold, mixed/nested cargo, ore retention, save/load and destruction passed.\n";
    error.clear(); return true;
}
