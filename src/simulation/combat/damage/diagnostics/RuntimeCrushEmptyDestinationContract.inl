// Include before the existing crush/scatter smoke, after movement helpers.
bool runRuntimeCrushEmptyDestinationContract(std::string& error) {
    const auto fail = [&](const char* reason) {
        error = std::string("empty crush destination: ") + reason;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    IniFile rules;
    if (!rules.loadFromString(
        "[InfantryTypes]\n0=SCATTER\n[VehicleTypes]\n0=CRUSHER\n"
        "[BuildingTypes]\n0=DRIVE\n1=HIDDEN\n[AircraftTypes]\n"
        "[CRUSHER]\nStrength=500\nSpeed=6\nSpeedType=Track\nCrusher=yes\n"
        "[SCATTER]\nStrength=20\nSpeed=5\nTrainable=yes\nCrushable=yes\n"
        "VeteranAbilities=SCATTER\n"
        "[DRIVE]\nStrength=100\nNumberImpassableRows=1\n"
        "[HIDDEN]\nStrength=100\nInvisibleInGame=yes\n")
        || !state.rules.loadFromIni(rules)) return fail("fixture rules did not load");
    state.rulesLoaded = true;
    state.config.fixedTickHz = 10;
    state.config.gameMode = "Battle";
    state.economy.owner = "Player";
    state.tick = 10;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimeSoundsEnabled = false;
    state.runtimePreferencePersistenceEnabled = false;
    state.structureFootprints["DRIVE"] = {3, 2, true};
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y;
        terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        RuntimeTerrainInfo info;
        info.passable = true;
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type,
                         RuntimeCellPoint cell, const char* owner = "Player") {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y);
        object.alive = true; object.health = 100; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Unit, "CRUSHER", {1, 1});
    add(2, WorldObjectKind::Unit, "CRUSHER", {4, 4});
    add(3, WorldObjectKind::Unit, "CRUSHER", {4, 4});
    add(4, WorldObjectKind::Structure, "DRIVE", {8, 8});
    add(5, WorldObjectKind::Structure, "HIDDEN", {12, 12});
    add(6, WorldObjectKind::Infantry, "SCATTER", {6, 6}, "Enemy");
    findMutableObjectById(state, 6)->veterancy = 1;
    const auto provesEmpty = [&](RuntimeCellPoint cell) {
        return runtimeTickOccupancyProvesEmptyCrushDestination(
            state, *findObjectById(state, 1), cell);
    };
    const auto matchesReference = [&]() {
        const auto before = state.world.deterministicSimulationStateHash();
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const RuntimeCellPoint cell{x, y};
            if (!provesEmpty(cell)) continue;
            const auto scalar = runtimeScanCrushTargetsAtCell(
                state, *findObjectById(state, 1), cell);
            if (scalar.blocked || !scalar.targetIds.empty()
                || resolveRuntimeCrushAtDestination(state, 1, cell)
                    != RuntimeCrushStepResult::NoVictims) return false;
        }
        return before == state.world.deterministicSimulationStateHash();
    };
    if (provesEmpty({2, 2})) return fail("unbuilt cache supplied a proof");
    (void)runtimeTickOccupancyLookup(state);
    if (!provesEmpty({2, 2}) || provesEmpty({4, 4})
        || provesEmpty({9, 8}) || provesEmpty({8, 8})
        || provesEmpty({12, 12}) || provesEmpty({13, 13})
        || state.tickOccupiedByCell.contains(runtimeCellKey(12, 12))
        || runtimeScanCrushTargetsAtCell(state, *findObjectById(state, 1), {12, 12}).blocked == false
        || !matchesReference()) return fail("empty, overlap, drive-row or invisible foundation mismatch");

    const auto move = [&](int id, RuntimeCellPoint destination) {
        auto* object = findMutableObjectById(state, id);
        const auto previous = runtimeCellPointForObject(*object);
        setRuntimeObjectCell(state, *object, destination);
        updateRuntimeTickOccupancyForMove(state, *object, previous, destination);
    };
    move(2, {5, 4});
    if (provesEmpty({4, 4}) || provesEmpty({5, 4}))
        return fail("leaving one overlapping occupant hid the other");
    move(3, {5, 5});
    if (!provesEmpty({4, 4}) || provesEmpty({5, 5}) || !matchesReference())
        return fail("same-tick departures/arrivals left a stale empty proof");

    if (provesEmpty({6, 6})
        || resolveRuntimeCrushAtDestination(state, 1, {6, 6}) != RuntimeCrushStepResult::NoVictims)
        return fail("scatter target incorrectly used the empty-cell shortcut");
    const auto* scatter = findObjectById(state, 6);
    if (!scatter || runtimeCellPointForObject(*scatter) == RuntimeCellPoint{6, 6}
        || !provesEmpty({6, 6}) || provesEmpty(runtimeCellPointForObject(*scatter))
        || !matchesReference()) return fail("scatter did not update same-tick occupancy");

    const auto validHash = state.world.deterministicSimulationStateHash();
    invalidateRuntimeTickOccupancyCache(state);
    if (provesEmpty({2, 2}) || state.tickCrushEmptyCellProofValid
        || !state.tickCrushUnindexedFoundationBounds.empty())
        return fail("invalidation retained the empty-cell proof");
    (void)runtimeTickOccupancyLookup(state);
    ++state.tick;
    if (provesEmpty({2, 2})) return fail("a stale tick supplied a proof");
    (void)runtimeTickOccupancyLookup(state);
    if (provesEmpty({-1, 2})
        || provesEmpty({std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}))
        return fail("invalid destination supplied a proof");
    auto nonVehicle = *findObjectById(state, 1);
    nonVehicle.kind = WorldObjectKind::Infantry;
    if (runtimeTickOccupancyProvesEmptyCrushDestination(state, nonVehicle, {2, 2}))
        return fail("non-vehicle used vehicle drive-row occupancy");
    if (validHash != state.world.deterministicSimulationStateHash())
        return fail("proof validation changed authoritative world state");

    add(-1, WorldObjectKind::Unit, "CRUSHER", {2, 3});
    if (provesEmpty({2, 2})) return fail("object insertion reused an old proof");
    invalidateRuntimeTickOccupancyCache(state);
    (void)runtimeTickOccupancyLookup(state);
    if (provesEmpty({2, 2})) return fail("synthetic ignored object id supplied a proof");
    state.world.accessObjects().back().alive = false;
    state.structureFootprints["HIDDEN"] = {5000, 5000, true};
    invalidateRuntimeTickOccupancyCache(state);
    (void)runtimeTickOccupancyLookup(state);
    if (provesEmpty({2, 2})) return fail("oversized exceptional foundation supplied a proof");
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    invalidateRuntimeTickOccupancyCache(state);
    (void)runtimeTickOccupancyLookup(state);
    move(5, {11, 12});
    if (provesEmpty({2, 2})) return fail("structure movement retained foundation proof");
    std::cout << "Empty crush destination contract passed: ordered moves, overlaps, drive rows, "
        "invisible foundations, scatter, invalidation; worldHash=" << validHash << "\n";
    error.clear();
    return true;
}
