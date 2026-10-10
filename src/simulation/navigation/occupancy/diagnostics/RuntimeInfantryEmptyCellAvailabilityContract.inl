bool runRuntimeInfantryEmptyCellAvailabilityContract(std::string& error) {
    const auto fail = [&](const std::string& detail) {
        error = "infantry empty-cell availability: " + detail;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    RuntimeSnapshotActiveStateScope scope(state);
    IniFile rules;
    if (!rules.loadFromString(
        "[InfantryTypes]\n0=INF\n[VehicleTypes]\n0=VEH\n1=AIR\n"
        "[BuildingTypes]\n0=HIDDEN\n1=DRIVE\n2=GATE\n"
        "[INF]\nStrength=100\nSpeed=5\nSpeedType=Foot\n"
        "[VEH]\nStrength=100\nSpeed=6\nSpeedType=Track\n"
        "[AIR]\nStrength=100\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
        "[HIDDEN]\nStrength=500\nInvisibleInGame=yes\n"
        "[DRIVE]\nStrength=500\nNumberImpassableRows=1\n"
        "[GATE]\nStrength=500\nGate=yes\n")
        || !state.rules.loadFromIni(rules)) return fail("rules did not load");
    state.rulesLoaded = true;
    state.tick = 10; state.config.fixedTickHz = 30; state.economy.owner = "Player";
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    state.structureFootprints["DRIVE"] = {3, 2, true};
    state.structureFootprints["GATE"] = {1, 1, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(cell);
        RuntimeTerrainInfo info; info.passable = true;
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = "Player"; object.alive = true; object.health = 100;
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.mission = "Guard"; object.subCell = 2;
        state.world.accessObjects().push_back(std::move(object));
    };
    for (int id = 1; id <= 4; ++id) add(id, WorldObjectKind::Infantry, "INF", id, 1);
    for (int id = 20; id < 276; ++id)
        add(id, WorldObjectKind::Infantry, "INF", 1 + id % 6, 3 + (id / 6) % 4);
    add(300, WorldObjectKind::Unit, "VEH", 4, 4);
    add(301, WorldObjectKind::Structure, "HIDDEN", 12, 12);
    add(302, WorldObjectKind::Structure, "DRIVE", 9, 12);
    add(303, WorldObjectKind::Unit, "AIR", 8, 8);
    add(304, WorldObjectKind::Structure, "GATE", 9, 7);
    findMutableObjectById(state, 303)->airborne = true;
    ensureRuntimeGatesInitialized(state);
    WorldTerrainObject terrain;
    terrain.x = 5; terrain.y = 9; terrain.cell = runtimeCellKey(5, 9);
    terrain.alive = true; terrain.health = 100;
    terrain.occupationBits = 0; terrain.occupiedSubCells = {2};
    state.world.accessTerrainObjects().push_back(terrain);
    const auto rebuild = [&]() {
        invalidateRuntimeTickOccupancyCache(state);
        (void)runtimeTickOccupancyLookup(state);
    };
    int comparisons = 0, provenQueries = 0, batchComparisons = 0;
    const auto compare = [&](const char* phase) {
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto subcellHash = state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects());
        for (int id : {1, 4}) for (auto mode : {RuntimeGatePassageMode::Planning, RuntimeGatePassageMode::Commit}) {
            const auto& mover = *findObjectById(state, id);
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                const RuntimeCellPoint cell{x, y};
                const auto original = runtimeAvailableInfantrySubcells(state, mover, cell, mode, true);
                const auto optimized = runtimeAvailableInfantrySubcells(state, mover, cell, mode, false);
                if (original != optimized)
                    return fail(std::string(phase) + " slots/order differ at "
                        + std::to_string(x) + "," + std::to_string(y));
                if (runtimeInfantryCanUseEmptyCellAvailability(state, cell)) {
                    if (runtimeInfantrySubcellsForProvenEmptyCell(state, mover, cell) != original)
                        return fail(std::string(phase) + " direct empty proof differs");
                    ++provenQueries;
                }
                ++comparisons;
            }
        }
        const auto& mover = *findObjectById(state, 4);
        for (const RuntimeCellPoint center : {RuntimeCellPoint{2, 4}, RuntimeCellPoint{8, 8}, RuntimeCellPoint{10, 12}}) {
            RuntimeInfantryAvoidanceSubcellBatch outer(state, mover, center);
            std::array<std::vector<int>, 8> expected;
            for (std::size_t i = 0; i < outer.cells.size(); ++i)
                expected[i] = runtimeAvailableInfantrySubcells(state, mover, outer.cells[i],
                    RuntimeGatePassageMode::Commit, true);
            if (outer.materializations != 0) return fail("forced reference materialized batch");
            for (std::size_t i = 0; i < outer.cells.size(); ++i) {
                const auto* slots = outer.find(state, mover, outer.cells[i], RuntimeGatePassageMode::Commit);
                if (!slots || *slots != expected[i]
                    || runtimeAvailableInfantrySubcells(state, mover, outer.cells[i],
                        RuntimeGatePassageMode::Commit, false) != expected[i])
                    return fail(std::string(phase) + " occupied/corner batch slots differ");
                ++batchComparisons;
            }
            if (outer.materializations != 1) return fail("batch did not reuse one snapshot");
            if (outer.find(state, mover, outer.cells[0], RuntimeGatePassageMode::Planning)
                || outer.find(state, *findObjectById(state, 1), outer.cells[0], RuntimeGatePassageMode::Commit))
                return fail("batch accepted a different gate mode or mover");
            {
                RuntimeInfantryAvoidanceSubcellBatch inner(state, mover, {8, 8});
                const RuntimeCellPoint target{9, 7};
                const auto* slots = inner.find(state, mover, target, RuntimeGatePassageMode::Commit);
                if (runtimeActiveInfantryAvoidanceBatch != &inner || !slots
                    || *slots != runtimeAvailableInfantrySubcells(state, mover, target,
                        RuntimeGatePassageMode::Commit, true)) return fail("nested batch differs");
            }
            if (runtimeActiveInfantryAvoidanceBatch != &outer || outer.available != std::vector<std::vector<int>>(expected.begin(), expected.end()))
                return fail("nested scope failed to restore independent cached slots");
        }
        if (worldHash != state.world.deterministicSimulationStateHash()
            || subcellHash != state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects()))
            return fail("availability query changed authoritative state");
        return true;
    };
    if (runtimeAvailableInfantrySubcells(state, *findObjectById(state, 4), {9, 7},
            RuntimeGatePassageMode::Planning, true).empty()
        || !runtimeAvailableInfantrySubcells(state, *findObjectById(state, 4), {9, 7},
            RuntimeGatePassageMode::Commit, true).empty())
        return fail("closed allied gate did not distinguish planning/commit contexts");
    for (bool bridge : {false, true}) {
        state.terrainInfoByCell.at(runtimeCellKey(8, 8)).bridge = bridge;
        for (int arrivals = 0; arrivals <= 3; ++arrivals) {
            state.subcells.clearReservations();
            std::vector<RuntimeSubcellMoveRequest> requests;
            for (int id = 1; id <= arrivals; ++id)
                requests.push_back({id, {8, 8, runtimeCellKey(8, 8), bridge}});
            const auto results = state.subcells.reserveBatch(state.world.getObjects(),
                state.world.getTerrainObjects(), requests);
            if (results.size() != requests.size()
                || std::any_of(results.begin(), results.end(), [](const auto& result) { return !result.reserved(); }))
                return fail("reservation fixture failed");
            rebuild();
            if (!runtimeMovementTickProvesEmptyCell(state, {8, 8}) || !compare("reservations/layer")) return false;
            const auto available = runtimeAvailableInfantrySubcells(state,
                *findObjectById(state, 4), {8, 8});
            if (available.size() != static_cast<std::size_t>(3 - arrivals))
                return fail("arrival reservations were bypassed");
        }
    }
    state.subcells.clearReservations();
    state.terrainInfoByCell.at(runtimeCellKey(8, 8)).bridge = false;
    auto* aircraft = findMutableObjectById(state, 303);
    // Capture with the aircraft in flight, then land within the same tick.
    // Aircraft movement deliberately does not update ground occupancy.
    rebuild();
    if (!runtimeInfantryCanUseEmptyCellAvailability(state, {8, 8}))
        return fail("airborne cell did not exercise the optimized branch");
    const auto aircraftFrom = runtimeCellPointForObject(*aircraft);
    setRuntimeObjectCell(state, *aircraft, {9, 8}, false);
    updateRuntimeTickOccupancyForMove(state, *aircraft, aircraftFrom, {9, 8});
    aircraft->airborne = false; aircraft->dockedToObjectId = 999;
    // Navigation skips a grounded docked aircraft, while subcell occupancy
    // still includes it. This is exactly why the additional guard is required.
    if (!runtimeMovementTickProvesEmptyCell(state, {9, 8})
        || runtimeInfantryCanUseEmptyCellAvailability(state, {9, 8})
        || !runtimeAvailableInfantrySubcells(state, *findObjectById(state, 4), {9, 8}).empty()
        || !compare("same-tick landed docked aircraft")) return fail("docked aircraft fallback failed");
    releaseRuntimeAircraftDock(*aircraft);
    if (runtimeInfantryCanUseEmptyCellAvailability(state, {9, 8})
        || !compare("same-tick released dock")) return fail("released dock fallback failed");
    if (!runtimeInfantryCanUseEmptyCellAvailability(state, {8, 8}))
        return fail("unrelated empty cell lost its fast path due to one aircraft");
    aircraft->airborne = true; aircraft->dockedToObjectId = -1;
    rebuild();
    ++state.tick;
    if (!compare("stale cache")) return false;
    rebuild();
    auto* moved = findMutableObjectById(state, 300);
    const auto previous = runtimeCellPointForObject(*moved);
    setRuntimeObjectCell(state, *moved, {8, 8}, false);
    updateRuntimeTickOccupancyForMove(state, *moved, previous, {8, 8});
    if (runtimeMovementTickProvesEmptyCell(state, {8, 8}) || !compare("same-tick occupied")) return false;
    {
        auto& large = state.world.accessTerrainObjects().front();
        large.footprintWidth = large.footprintHeight = 5000;
        {
            RuntimeInfantryAvoidanceSubcellBatch batch(state, *findObjectById(state, 4), {2, 4});
            if (batch.find(state, *findObjectById(state, 4), {2, 3}, RuntimeGatePassageMode::Commit)
                || batch.materializations != 0 || !batch.unsupported)
                return fail("giant terrain expanded a batch snapshot");
        }
        large.footprintWidth = large.footprintHeight = 1;
    }
    if (provenQueries == 0) return fail("optimized cells were not exercised");
    std::cout << "Infantry empty-cell availability: comparisons=" << comparisons
        << " provenQueries=" << provenQueries << " batchComparisons=" << batchComparisons
        << " reservations/layers/gates/guards/slotOrder/nestedScope=exact\n";
    error.clear();
    return true;
}
