// Include after formation target helpers. This contract compares the actual
// scalar search to the score-bound search under both environment modes.
bool runRuntimeFormationScoreBoundContract(std::string& error) {
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    IniFile ini;
    if (!ini.loadFromString(
        "[InfantryTypes]\n0=FOOT\n[VehicleTypes]\n0=TRACK\n"
        "[BuildingTypes]\n0=WALL\n[AircraftTypes]\n"
        "[FOOT]\nStrength=100\nSpeed=4\nSpeedType=Foot\n"
        "[TRACK]\nStrength=300\nSpeed=6\nSpeedType=Track\n"
        "[WALL]\nStrength=200\n") || !state.rules.loadFromIni(ini)) {
        error = "formation score bound: fixture rules did not load";
        return false;
    }
    state.rulesLoaded = true;
    state.config.fixedTickHz = 30;
    state.config.gameMode = "Battle";
    state.economy.owner = "Player";
    state.structureFootprints["WALL"] = {3, 2, true};
    for (int y = 1; y <= 36; ++y) for (int x = 1; x <= 36; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y; terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        RuntimeTerrainInfo info;
        info.cost = 8 + (x * 7 + y * 11) % 11;
        info.ramp = (x + y) % 9 == 0;
        info.shore = (x * 3 + y) % 13 == 0;
        info.cliff = (x * 5 + y) % 19 == 0;
        info.passable = (x * 11 + y) % 23 != 0;
        info.kind = RuntimeTerrainKind::Land;
        info.authoredLandType = info.movementLandType = RuntimeTerrainLandType::Clear;
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type, RuntimeCellPoint cell) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = "Player"; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.subCell = 2;
        object.alive = true; object.health = 100; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Unit, "TRACK", {2, 2});
    add(2, WorldObjectKind::Infantry, "FOOT", {3, 2});
    add(3, WorldObjectKind::Infantry, "FOOT", {12, 12});
    add(4, WorldObjectKind::Unit, "TRACK", {18, 18});
    add(5, WorldObjectKind::Structure, "WALL", {22, 22});
    const auto occupied = buildRuntimeOccupancyLookup(state);
    std::unordered_set<int> reservations, reachable;
    std::unordered_map<int, int> components;
    for (int y = 1; y <= 36; ++y) for (int x = 1; x <= 36; ++x) {
        const int key = runtimeCellKey(x, y);
        components.emplace(key, x < 19 ? 1 : 2);
        if (x <= 28 && y >= 3) reachable.insert(key);
        if ((x + y) % 4 == 0) reservations.insert(key);
    }
    constexpr std::array<RuntimeCellPoint, 9> desiredCells{{
        {-2, 1}, {1, 1}, {12, 12}, {18, 18}, {22, 22},
        {28, 15}, {36, 36}, {40, 40}, {16, 6}}};
    const auto initialHash = state.world.deterministicSimulationStateHash();
    std::uint64_t resultHash = runtimeStableHashString("FormationScoreBound/v1");
    int cases = 0, absent = 0;
    for (int objectId : {1, 2}) for (int filter = 0; filter < 8; ++filter) {
        for (const auto& desired : desiredCells) {
            const auto* mover = findObjectById(state, objectId);
            const auto query = [&](bool prune) {
                return findBestRuntimeFormationTargetCellWithOccupancyImpl(state,
                    *mover, desired, -1, (filter & 1) ? &reservations : nullptr,
                    state.terrainByCell, occupied, (filter & 2) ? &reachable : nullptr,
                    (filter & 4) ? &components : nullptr, (filter & 4) ? 1 : -1, prune);
            };
            const auto expected = query(false);
            const auto actual = query(true);
            if (actual != expected) {
                error = "formation score bound: scalar destination mismatch at case " + std::to_string(cases);
                return false;
            }
            ++cases;
            if (!actual) ++absent;
            runtimeStableHashMix(resultHash, actual ? actual->x : -1);
            runtimeStableHashMix(resultHash, actual ? actual->y : -1);
        }
    }
    if (initialHash != state.world.deterministicSimulationStateHash()) {
        error = "formation score bound: search changed authoritative world";
        return false;
    }
    std::cout << "Formation score bound contract passed: cases=" << cases
        << " noTarget=" << absent << " resultHash=" << resultHash << '\n';
    error.clear();
    return true;
}
