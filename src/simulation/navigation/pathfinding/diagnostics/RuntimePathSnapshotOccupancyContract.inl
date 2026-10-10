// Include after the path snapshot builder and all value-system helpers.
bool runRuntimePathSnapshotOccupancyContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "path snapshot occupancy equivalence: " + why;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    RuntimeSnapshotActiveStateScope scope(state);
    IniFile rules;
    if (!rules.loadFromString(
        "[InfantryTypes]\n0=INF\n[VehicleTypes]\n0=VEH\n1=AIR\n"
        "[BuildingTypes]\n0=SOLID\n1=DRIVE\n2=HIDDEN\n3=GATE\n4=REPAIR\n"
        "[INF]\nStrength=100\nSpeed=5\nSpeedType=Foot\nMovementZone=Infantry\n"
        "[VEH]\nStrength=100\nSpeed=5\nSpeedType=Track\nMovementZone=Normal\n"
        "Repairable=yes\n[AIR]\nStrength=100\nSpeed=5\n"
        "SpeedType=Winged\nMovementZone=Fly\n"
        "[SOLID]\nStrength=500\n[DRIVE]\nStrength=500\nNumberImpassableRows=1\n"
        "[HIDDEN]\nStrength=500\nInvisibleInGame=yes\n"
        "[GATE]\nStrength=500\nGate=yes\n"
        "[REPAIR]\nStrength=500\nUnitRepair=yes\nNumberOfDocks=1\n")
        || !state.rules.loadFromIni(rules)) return fail("fixture rules did not load");
    state.rulesLoaded = true;
    state.tick = 10;
    state.config.fixedTickHz = 30;
    state.economy.owner = "Player";
    state.runtimeSoundsEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.structureFootprints["SOLID"] = {3, 3, true};
    state.structureFootprints["DRIVE"] = {4, 3, true};
    // Much larger than the query area; InvisibleInGame omits navigation's
    // representative occupant but the two scalar predicates still see it.
    state.structureFootprints["HIDDEN"] = {64, 48, true};
    state.structureFootprints["GATE"] = {2, 1, true};
    state.structureFootprints["REPAIR"] = {2, 2, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        cell.level = (x + y) % 3;
        cell.hasOverlay = cell.overlayIsOre = x == 15;
        state.world.accessTerrainCells().push_back(cell);
        RuntimeTerrainInfo info;
        info.passable = true;
        info.cost = 10 + (x + y) % 5;
        info.bridge = x == 5 && (y == 8 || y == 9);
        info.water = x == 14;
        info.shore = x == 14 && y < 4;
        info.ramp = x == 13 && y == 8;
        info.cliff = x == 13 && y == 9;
        if (x == 13 && y == 10) info.passable = false;
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type,
                         int x, int y, const char* owner = "Player") {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = owner; object.x = x; object.y = y;
        object.cell = runtimeCellKey(x, y); object.alive = true;
        object.health = 100; object.mission = "Guard"; object.subCell = 2;
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Infantry, "INF", 1, 1);
    add(2, WorldObjectKind::Unit, "VEH", 2, 1);
    add(3, WorldObjectKind::Unit, "AIR", 3, 1);
    add(10, WorldObjectKind::Structure, "SOLID", 6, 6);
    add(11, WorldObjectKind::Infantry, "INF", 7, 6);
    add(12, WorldObjectKind::Unit, "VEH", 9, 2);
    add(12, WorldObjectKind::Unit, "VEH", 9, 3); // duplicate-id ignore semantics
    add(14, WorldObjectKind::Unit, "VEH", 5, 5);
    add(15, WorldObjectKind::Infantry, "INF", 5, 6);
    add(16, WorldObjectKind::Unit, "VEH", 5, 7);
    add(17, WorldObjectKind::Unit, "VEH", 5, 8);
    add(18, WorldObjectKind::Unit, "VEH", 5, 9);
    add(19, WorldObjectKind::Unit, "VEH", 5, 10);
    add(20, WorldObjectKind::Unit, "VEH", 8, 8);
    add(21, WorldObjectKind::Unit, "AIR", 8, 9);
    add(22, WorldObjectKind::Unit, "AIR", 8, 10);
    add(23, WorldObjectKind::Structure, "DRIVE", 10, 10);
    add(24, WorldObjectKind::Structure, "HIDDEN", 4, 11);
    add(25, WorldObjectKind::Structure, "GATE", 9, 5);
    add(26, WorldObjectKind::Structure, "GATE", 11, 5, "Enemy");
    add(27, WorldObjectKind::Unit, "VEH", 2, 12);
    add(28, WorldObjectKind::Unit, "VEH", 3, 12);
    add(29, WorldObjectKind::Unit, "VEH", 3, 13);
    add(30, WorldObjectKind::Unit, "VEH", 3, 14, "Enemy");
    add(40, WorldObjectKind::Structure, "REPAIR", 6, 12);
    add(41, WorldObjectKind::Structure, "SOLID", 12, 12);
    add(42, WorldObjectKind::Structure, "HIDDEN", 14, 12);
    add(43, WorldObjectKind::Structure, "SOLID", 10, 13);
    findMutableObjectById(state, 14)->health = 0;
    findMutableObjectById(state, 15)->mission = "InsideTransport";
    findMutableObjectById(state, 16)->mission = "Garrisoned";
    findMutableObjectById(state, 16)->garrisonHostObjectId = 10;
    findMutableObjectById(state, 17)->airborne = true; // ground rules remain ground
    findMutableObjectById(state, 18)->submerged = true;
    findMutableObjectById(state, 19)->onBridge = true; // deliberately opposite layer
    findMutableObjectById(state, 20)->alive = false;
    findMutableObjectById(state, 22)->airborne = true;
    findMutableObjectById(state, 27)->transportPassengerIds = {15, 28};
    findMutableObjectById(state, 28)->health = -1;
    findMutableObjectById(state, 29)->health = 50;
    findMutableObjectById(state, 30)->health = 50;
    findMutableObjectById(state, 41)->health = 0; // alive structure is still an allowed-footprint host
    findMutableObjectById(state, 42)->mission = "InsideTransport";
    findMutableObjectById(state, 43)->mission = "InsideTransport"; // no new mission eligibility gate
    const auto* zeroHealthHost = findObjectById(state, 41);
    const auto* hiddenHost = findObjectById(state, 42);
    const auto* transportedHost = findObjectById(state, 43);
    if (!zeroHealthHost || !zeroHealthHost->alive || zeroHealthHost->health != 0
        || !runtimeCellIsAllowedOccupiedStructureFootprint(state, {12, 12}, 41))
        return fail("alive zero-health structure lost its original allowed footprint");
    if (!transportedHost || !transportedHost->alive || transportedHost->mission != "InsideTransport"
        || runtimeInvisibleMapBuilding(&state.rules, *transportedHost)
        || !runtimeCellIsAllowedOccupiedStructureFootprint(state, {10, 13}, 43))
        return fail("visible transported structure lost its original allowed footprint");
    // InvisibleInGame intentionally has no navigation footprint (the original
    // footprint generator returns empty). Keep the combined hidden/transport
    // case and verify that exact scalar meaning, alongside the visible host.
    if (!hiddenHost || !hiddenHost->alive || hiddenHost->mission != "InsideTransport"
        || !runtimeInvisibleMapBuilding(&state.rules, *hiddenHost)
        || !runtimeStructureFootprintCellsForObject(state, *hiddenHost).empty()
        || runtimeCellIsAllowedOccupiedStructureFootprint(state, {14, 12}, 42))
        return fail("hidden transported structure changed its original empty navigation footprint");
    if (runtimeCellIsAllowedOccupiedStructureFootprint(state, {2, 1}, 2))
        return fail("ordinary unit incorrectly acquired an allowed structure footprint");
    if (runtimeObjectMovementMode(state, *findObjectById(state, 3)) != PathMovementMode::Air)
        return fail("AIR fixture did not resolve air movement");
    ensureRuntimeGatesInitialized(state);
    if (!runtimeTransparentGateObjectIds(state, *findObjectById(state, 1),
            RuntimeGatePassageMode::Planning).contains(25))
        return fail("allied closed gate was not planning-transparent");

    const auto terrain = [&](int x, int y, int bits, std::vector<int> subcells,
                             int width = 1, int height = 1) {
        WorldTerrainObject object;
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.alive = true; object.health = object.strength = 100;
        object.occupationBits = bits; object.occupiedSubCells = std::move(subcells);
        object.footprintWidth = width; object.footprintHeight = height;
        state.world.accessTerrainObjects().push_back(std::move(object));
    };
    terrain(2, 4, 1, {2}, 2, 2); // partial occupation is still hard in these helpers
    terrain(4, 4, 7, {2, 4, 3});
    terrain(6, 4, 8, {}); // nonzero bits, not modulo-eight normalization
    terrain(8, 4, -8, {});
    terrain(10, 4, 0, {99}); // any nonempty list, even no active subcell
    terrain(12, 4, 0, {});
    terrain(12, 6, 7, {2}); state.world.accessTerrainObjects().back().alive = false;
    terrain(12, 7, 7, {2}); state.world.accessTerrainObjects().back().health = 0;
    terrain(-1, 8, 1, {3}, 2, 1); // coordinates, not legacy cell fallback
    terrain(0, 10, 1, {4}, 0, -2); // minimum 1x1 footprint

    RuntimeRepairDepotContext repair;
    repair.rules = &state.rules; repair.objects = &state.world.getObjects();
    repair.areAllied = [](const std::string& left, const std::string& right) { return left == right; };
    repair.resolveHostDomain = [](int) { return std::optional<RuntimeRepairDepotDomain>{RuntimeRepairDepotDomain::Ground}; };
    repair.resolveUnitDomain = repair.resolveHostDomain;
    repair.resolveDockCell = [](int, int, int) {
        return std::optional<RuntimeRepairDepotCell>{{7, 7, false}};
    };
    repair.canPathTo = [](int, const RuntimeRepairDepotCell&) { return true; };
    if (!state.repairDepots.reserveDock(repair, 40, 29, state.tick).accepted
        || !runtimeCellReservedByOtherRepairUnit(state, {7, 7}, 1))
        return fail("repair reservation fixture did not reserve allowed foundation cell");

    const auto fields = [](const PathTerrainCell& cell) {
        return std::tie(cell.cell.x, cell.cell.y, cell.level, cell.passable, cell.water,
            cell.shore, cell.bridge, cell.ramp, cell.cliff, cell.resource, cell.cost, cell.hazardCost);
    };
    int cases = 0;
    int predicateCells = 0;
    const auto compare = [&](int phase) {
        for (int moverId : {1, 2, 3}) for (int allowed : {-1, 1, 2, 3, 10, 14, 15, 20, 23, 25, 28, 40, 41, 42, 43, 999}) {
            for (const auto& ignored : {std::unordered_set<int>{},
                    std::unordered_set<int>{11, 12, 14, 15, 16},
                    std::unordered_set<int>{24, 25, 26},
                    std::unordered_set<int>{1, 2, 3, 10, 23, 40}}) {
                const auto mover = *findObjectById(state, moverId);
                const auto worldHash = state.world.deterministicSimulationStateHash();
                const auto subcellHash = state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects());
                const auto gateHash = state.gates.stateHash();
                const auto repairHash = state.repairDepots.stateHash();
                const auto magHash = state.magBeams.stateHash();
                state.pathTerrainCache.dynamicValid = false;
                const std::vector<PathTerrainCell> scalar = [&] {
                    RuntimeSimulationQueryReuseReferenceScope reference(true);
                    RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(true);
                    return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                        state, mover, allowed, &ignored, true));
                }();
                state.pathTerrainCache.dynamicValid = false;
                const std::vector<PathTerrainCell> previousIndexed = [&] {
                    // Retain the old allowedId>=0 index parameter as a third
                    // independent production path, not only scalar fallback.
                    RuntimeSimulationQueryReuseReferenceScope reference(true);
                    RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(true);
                    return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                        state, mover, allowed, &ignored, false));
                }();
                const auto* oldBuffer = state.pathTerrainCache.dynamicCells.data();
                const auto oldCapacity = state.pathTerrainCache.dynamicCells.capacity();
                state.pathTerrainCache.dynamicValid = false;
                const std::vector<PathTerrainCell> previousOccupantQueries = [&] {
                    // Keep the previous production's original map lookup
                    // independently of the existing blocker/reference flags.
                    RuntimeSimulationQueryReuseReferenceScope reuse(false);
                    RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(true);
                    return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                        state, mover, allowed, &ignored, false));
                }();
                state.pathTerrainCache.dynamicValid = false;
                const std::vector<PathTerrainCell> optimized = [&] {
                    RuntimeSimulationQueryReuseReferenceScope reuse(false);
                    RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(false);
                    return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                        state, mover, allowed, &ignored, false));
                }();
                ++cases;
                const std::string context = "case=" + std::to_string(cases)
                    + " phase=" + std::to_string(phase) + " mover=" + std::to_string(moverId)
                    + " allowed=" + std::to_string(allowed);
                if (scalar.size() != optimized.size() || scalar.size() != previousIndexed.size()
                    || scalar.size()!=previousOccupantQueries.size())
                    return fail(context + " snapshot size changed");
                if (state.pathTerrainCache.dynamicCells.data() != oldBuffer
                    || state.pathTerrainCache.dynamicCells.capacity() != oldCapacity)
                    return fail(context + " equal-size snapshot did not reuse dynamic buffer capacity");
                for (std::size_t i = 0; i < scalar.size(); ++i) {
                    if (fields(scalar[i]) != fields(optimized[i]) || fields(scalar[i]) != fields(previousIndexed[i])
                        || fields(scalar[i])!=fields(previousOccupantQueries[i])) {
                        return fail(context + " index=" + std::to_string(i)
                            + " cell=" + std::to_string(scalar[i].cell.x) + "," + std::to_string(scalar[i].cell.y)
                            + " scalarPassable=" + std::to_string(scalar[i].passable)
                            + " previousIndexPassable=" + std::to_string(previousIndexed[i].passable)
                            + " previousOccupantQueryPassable=" + std::to_string(previousOccupantQueries[i].passable)
                            + " optimizedPassable=" + std::to_string(optimized[i].passable));
                    }
                }
                // Direct predicate comparison also observes differences hidden
                // by earlier terrain/passability gates in the complete builder.
                const auto gates = runtimeTransparentGateObjectIds(state, mover, RuntimeGatePassageMode::Planning);
                RuntimePathSnapshotOccupancy index(state, scalar, moverId, allowed, gates, true, true);
                if (index.needsScalarTerrainFallback()) return fail(context + " unexpected terrain fallback");
                for (const PathTerrainCell& cell : scalar) {
                    const RuntimeCellPoint point{cell.cell.x, cell.cell.y};
                    if (index.hardBlocker(point) != runtimeCellHasHardWholeCellBlocker(state, point, moverId, allowed, gates)
                        || index.unexpectedOccupant(point) != runtimeCellHasUnexpectedGroundOccupant(state, point, moverId, allowed))
                        return fail(context + " direct predicate mismatch at " + std::to_string(point.x) + "," + std::to_string(point.y));
                    ++predicateCells;
                }
                if (worldHash != state.world.deterministicSimulationStateHash()
                    || subcellHash != state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects())
                    || gateHash != state.gates.stateHash() || repairHash != state.repairDepots.stateHash()
                    || magHash != state.magBeams.stateHash()) return fail(context + " authoritative state mutated");
            }
        }
        return true;
    };
    if (!compare(0)) return false;

    RuntimeMagBeamAcquireRequest acquire;
    acquire.currentTick = state.tick;
    acquire.source = {27, "Player", "VEH", true, true};
    acquire.target.objectId = 30; acquire.target.owner = "Enemy"; acquire.target.typeId = "VEH";
    acquire.target.kind = RuntimeMagBeamTargetKind::Vehicle;
    acquire.target.present = acquire.target.alive = acquire.target.enemyToSource = true;
    acquire.target.onGround = acquire.target.liftable = true;
    acquire.target.weight = 1; acquire.target.currentStrength = 50; acquire.target.maximumStrength = 100;
    acquire.target.originalLocomotorId = "{4A582741-9839-11d1-B709-00A024DDAFD1}";
    acquire.target.kinematics.location = {3, 14, runtimeCellKey(3, 14), 0, false, false};
    acquire.target.kinematics.worldX = 3; acquire.target.kinematics.worldY = 14;
    acquire.weapon = {"MagneticBeam", "LocomotorBeam", "{92612C46-F71F-11d1-AC9F-006008055BB5}",
        true, true, true, true, 5000};
    const auto acquired = state.magBeams.acquire(acquire);
    if (!acquired.accepted) return fail("magbeam fixture acquisition failed");
    RuntimeMagBeamAdvanceInput lift;
    lift.currentTick = ++state.tick;
    lift.sources.push_back(acquire.source); lift.targets.push_back(acquire.target);
    RuntimeMagBeamHostUpdate update;
    update.bindingId = acquired.bindingId; update.kind = RuntimeMagBeamHostUpdateKind::LiftStarted;
    update.kinematics = acquire.target.kinematics; update.kinematics.worldZ = 3;
    lift.hostUpdates.push_back(update);
    if (!state.magBeams.advance(lift).advanced || !runtimeObjectOffGroundByMagBeam(state, 30))
        return fail("magbeam fixture did not leave the ground");
    if (!compare(1)) return false;

    std::cout << "Path snapshot occupancy equivalence passed: cases=" << cases
        << " predicateCells=" << predicateCells
        << " fields=12 ignored/gates/duplicates/transport/bridge/hidden/terrain/repair/magbeam/scratchReuse/allowedIndexEligibility/independentRepresentativeQueries=yes\n";
    error.clear();
    return true;
}
