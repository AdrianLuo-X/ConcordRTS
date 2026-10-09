bool runRuntimeCrushRepresentativeBlockerContract(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "crush representative blocker: " + reason;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    RuntimeSnapshotActiveStateScope scope(state);
    IniFile rules;
    if (!rules.loadFromString(
        "[VehicleTypes]\n0=CRUSHER\n1=AIR\n[InfantryTypes]\n0=INF\n"
        "[BuildingTypes]\n0=DRIVE\n1=GATE\n2=WALL\n3=HARDWALL\n4=HIDDEN\n"
        "[CRUSHER]\nStrength=500\nSpeed=6\nSpeedType=Track\nCrusher=yes\nCrushable=no\n"
        "[AIR]\nStrength=100\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
        "[INF]\nStrength=100\nSpeed=5\nCrushable=yes\nSpeedType=Foot\n"
        "[DRIVE]\nStrength=100\nNumberImpassableRows=1\n"
        "[GATE]\nStrength=100\nGate=yes\n"
        "[WALL]\nStrength=100\nWall=yes\nCrushable=yes\n"
        "[HARDWALL]\nStrength=100\nWall=yes\nCrushable=no\n"
        "[HIDDEN]\nStrength=100\nInvisibleInGame=yes\n")
        || !state.rules.loadFromIni(rules)) return fail("fixture rules failed");
    state.rulesLoaded = true; state.tick = 10; state.config.fixedTickHz = 30;
    state.config.gameMode = "Standard"; state.economy.owner = "Player";
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.structureFootprints["DRIVE"] = {3, 2, true};
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y);
        state.world.accessTerrainCells().push_back(cell);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type,
        int x, int y, const char* owner) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = owner; object.x = x; object.y = y;
        object.cell = runtimeCellKey(x, y); object.health = 100;
        object.alive = true; object.mission = "Guard"; object.subCell = 2;
        state.world.accessObjects().push_back(object);
    };
    add(1, WorldObjectKind::Unit, "CRUSHER", 1, 1, "Player");
    add(2, WorldObjectKind::Unit, "CRUSHER", 4, 4, "Player");
    add(3, WorldObjectKind::Unit, "CRUSHER", 4, 4, "Enemy");
    add(4, WorldObjectKind::Structure, "DRIVE", 8, 8, "Player");
    add(5, WorldObjectKind::Structure, "GATE", 10, 3, "Player");
    add(6, WorldObjectKind::Infantry, "INF", 6, 6, "Enemy");
    add(7, WorldObjectKind::Infantry, "INF", 6, 6, "Player");
    add(8, WorldObjectKind::Structure, "WALL", 3, 10, "Enemy");
    add(9, WorldObjectKind::Structure, "WALL", 4, 10, "Player");
    add(10, WorldObjectKind::Structure, "HARDWALL", 5, 10, "Enemy");
    add(11, WorldObjectKind::Unit, "AIR", 12, 12, "Enemy");
    add(12, WorldObjectKind::Structure, "HIDDEN", 12, 9, "Enemy");
    add(13, WorldObjectKind::Unit, "CRUSHER", 10, 3, "Player");
    ensureRuntimeGatesInitialized(state);
    const auto rebuild = [&]() {
        invalidateRuntimeTickOccupancyCache(state);
        (void)runtimeTickOccupancyLookup(state);
    };
    const auto& crusher = *findObjectById(state, 1);
    int comparisons = 0, blockedProofs = 0;
    const auto compare = [&](const char* phase) {
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto subcellHash = state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects());
        const auto gateHash = state.gates.stateHash();
        for (const auto& ignored : {std::unordered_set<int>{}, std::unordered_set<int>{2},
            std::unordered_set<int>{2, 3}, std::unordered_set<int>{6},
            std::unordered_set<int>{7}, std::unordered_set<int>{5, 13}}) {
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                const RuntimeCellPoint cell{x, y};
                const auto scan = runtimeScanCrushTargetsAtCell(state, crusher, cell, -1, &ignored);
                const bool expected = !scan.blocked && !scan.targetIds.empty();
                const bool scalar = runtimeCanEnterAndCrushCell(state, crusher, cell, &ignored, nullptr, true);
                const bool optimized = runtimeCanEnterAndCrushCell(state, crusher, cell, &ignored, nullptr, false);
                const bool proof = runtimeCrushRepresentativeProvesBlocked(state, crusher, cell, &ignored);
                if (scalar != expected || optimized != expected || (proof && !scan.blocked))
                    return fail(std::string(phase) + " mismatch at " + std::to_string(x) + "," + std::to_string(y));
                blockedProofs += proof ? 1 : 0;
                ++comparisons;
            }
        }
        if (worldHash != state.world.deterministicSimulationStateHash()
            || subcellHash != state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects())
            || gateHash != state.gates.stateHash()) return fail("query changed authority");
        return true;
    };
    rebuild();
    const std::unordered_set<int> ignoreFriendlyInfantry{7};
    if (!runtimeCrushRepresentativeProvesBlocked(state, crusher, {4, 4})
        || !runtimeCrushRepresentativeProvesBlocked(state, crusher, {9, 8})
        || !runtimeCanEnterAndCrushCell(state, crusher, {6, 6}, &ignoreFriendlyInfantry)
        || !runtimeCanEnterAndCrushCell(state, crusher, {3, 10})
        || !runtimeCrushRepresentativeProvesBlocked(state, crusher, {4, 10})
        || !runtimeCrushRepresentativeProvesBlocked(state, crusher, {5, 10})
        || !compare("initial friendly/enemy/wall/overlap"))
        return fail("fixture blocking and crushing expectations");
    // Explicit candidate lists are authoritative even if the map knows other
    // blockers at this cell; the new proof must never supplement such a list.
    const RuntimeCrushCandidateList onlyEnemyInfantry{findObjectById(state, 6)};
    state.tickOccupiedByCell[runtimeCellKey(6, 6)] = 7;
    if (!runtimeCanEnterAndCrushCell(state, crusher, {6, 6}, nullptr, &onlyEnemyInfantry)
        || !compare("friendly representative above crushable peer")) return fail("explicit candidate list was expanded");
    // Arbitrarily stale ids/positions can only miss the shortcut. Every positive
    // result must still be confirmed against the current object by the scanner.
    for (int representative : {9999, 1, 6, 11, 12}) {
        state.tickOccupiedByCell[runtimeCellKey(4, 4)] = representative;
        if (runtimeCrushRepresentativeProvesBlocked(state, crusher, {4, 4})
            || !compare("stale representative")) return fail("stale id manufactured a blocker");
    }
    state.tickOccupiedByCell[runtimeCellKey(4, 4)] = 2;
    auto* moved = findMutableObjectById(state, 2);
    moved->x = 5; moved->cell = runtimeCellKey(5, 4);
    if (runtimeCrushRepresentativeProvesBlocked(state, crusher, {4, 4})
        || !compare("same-tick relocated representative")) return fail("moved representative still blocked its old cell");
    moved->alive = false;
    if (!compare("inactive representative")) return false;
    ++state.tick;
    if (runtimeCrushRepresentativeProvesBlocked(state, crusher, {5, 10})
        || !compare("stale tick")) return fail("stale tick used the shortcut");
    rebuild();
    state.terrainInfoByCell.at(runtimeCellKey(6, 6)).bridge = true;
    if (!compare("bridge layer mismatch")) return false;
    findMutableObjectById(state, 6)->onBridge = true;
    if (!runtimeCanEnterAndCrushCell(state, crusher, {6, 6}, &ignoreFriendlyInfantry)
        || !compare("bridge layer matches")) return fail("bridge crush semantics changed");
    // Open the gate through the real gate state machine, retaining the cached
    // gate representative while a second live occupant remains underneath it.
    state.gates.reset({30, 1, 1});
    auto context = runtimeGateContext(state);
    const std::vector<RuntimeGatePassageRequest> requests{{5, 1}};
    context.passageRequests = &requests;
    (void)state.gates.reconcile(context);
    (void)state.gates.tick(context, state.tick);
    (void)state.gates.tick(context, state.tick + 1);
    if (state.gates.phaseOf(5) != RuntimeGatePhase::Open
        || runtimeCrushRepresentativeProvesBlocked(state, crusher, {10, 3})
        || !runtimeScanCrushTargetsAtCell(state, crusher, {10, 3}).blocked
        || !compare("open gate above blocking vehicle")) return fail("open gate filtering changed");
    state.tickOccupiedByCell[runtimeCellKey(4, 4)] = runtimeTerrainOccupantId(0);
    if (runtimeCrushRepresentativeProvesBlocked(state, crusher, {4, 4})
        || !compare("terrain representative")) return fail("terrain sentinel treated as object");
    if (blockedProofs == 0) return fail("no positive shortcut exercised");
    std::cout << "Crush representative blocker passed: comparisons=" << comparisons
        << " blockedProofs=" << blockedProofs
        << " stale/ignored/drive-row/gate/bridge/friendly/enemy/wall/overlap=exact\n";
    error.clear();
    return true;
}
