// Include after all runtime path/movement helpers and the occupancy contract.
bool runRuntimePathTerrainQueryScopeContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "path terrain query scope: " + why;
        return false;
    };
    const auto makeFixture = [&](RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope active(state);
        IniFile ini;
        if (!ini.loadFromString("[VehicleTypes]\n0=CAR\n1=BOAT\n2=PLANE\n3=OMNI\n4=MCV\n"
            "[InfantryTypes]\n0=MAN\n[BuildingTypes]\n0=SOLID\n1=DRIVE\n2=HIDDEN\n3=GATE\n4=REPAIR\n5=WALL\n"
            "[CAR]\nStrength=200\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nCrusher=yes\nRepairable=yes\n"
            "[BOAT]\nStrength=200\nSpeed=6\nSpeedType=Float\nMovementZone=Water\nNaval=yes\n"
            "[PLANE]\nStrength=200\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
            "[OMNI]\nStrength=200\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nOmniCrusher=yes\n"
            "[MCV]\nStrength=200\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nDeploysInto=SOLID\n"
            "[MAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Infantry\nCrushable=yes\n"
            "[SOLID]\nStrength=500\n[DRIVE]\nStrength=500\nNumberImpassableRows=1\n"
            "[HIDDEN]\nStrength=500\nInvisibleInGame=yes\nNumberImpassableRows=1\n"
            "[GATE]\nStrength=500\nGate=yes\n[REPAIR]\nStrength=500\nUnitRepair=yes\nNumberOfDocks=1\n"
            "[WALL]\nStrength=500\nWall=yes\nCrushable=yes\n")
            || !state.rules.loadFromIni(ini)) return false;
        state.rulesLoaded = true; state.tick = 20; state.config.fixedTickHz = 30;
        state.economy.owner = "Player"; state.visionEnabled = false;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.structureFootprints["SOLID"] = {3, 3, true};
        state.structureFootprints["DRIVE"] = {4, 3, true};
        state.structureFootprints["HIDDEN"] = {5, 4, true};
        state.structureFootprints["GATE"] = {2, 1, true};
        state.structureFootprints["REPAIR"] = {2, 2, true};
        state.structureFootprints["WALL"] = {1, 1, true};
        const auto terrain = [&](int x, int y) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.level = x >= 0 && y >= 0 && x < 25 && y < 12 ? 0 : (x + y) % 3;
            cell.hasOverlay = cell.overlayIsOre = x == 82;
            state.world.accessTerrainCells().push_back(cell);
            RuntimeTerrainInfo info;
            info.passable = !(x == 20 && y >= 15 && y <= 18);
            info.cost = 10 + (x + y) % 5;
            info.water = x >= 84; info.shore = x == 84;
            info.bridge = (x == 62 && y == 51) || (x == 86 && y == 30);
            info.cliff = x == 20 && y == 19; info.ramp = x == 20 && y == 14;
            state.terrainInfoByCell.try_emplace(runtimeCellKey(x, y), info);
        };
        for (int y = 0; y < 96; ++y) for (int x = 0; x < 96; ++x) terrain(x, y);
        terrain(10, 4); terrain(1010, 3); // duplicate plus legacy-key alias of 10,4
        terrain(-1, 0); terrain(0, -1); // all negative keys retain scalar semantics
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        const auto add = [&](int id, WorldObjectKind kind, const char* type, int x, int y,
                             const char* owner = "Player", bool alive = true) {
            WorldObjectInstance object; object.instanceId = id; object.kind = kind; object.type = type;
            object.owner = owner; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
            object.alive = alive; object.health = alive ? 100 : 0;
            object.mission = "Guard"; object.subCell = 2;
            state.world.accessObjects().push_back(std::move(object));
        };
        add(1, WorldObjectKind::Unit, "CAR", 61, 51);
        add(2, WorldObjectKind::Infantry, "MAN", 4, 4);
        add(3, WorldObjectKind::Unit, "BOAT", 85, 2);
        add(4, WorldObjectKind::Unit, "PLANE", 5, 2);
        add(5, WorldObjectKind::Unit, "CAR", 6, 2, "Enemy");
        add(6, WorldObjectKind::Unit, "OMNI", 4, 8);
        add(7, WorldObjectKind::Unit, "CAR", 62, 51);
        add(90, WorldObjectKind::Unit, "MCV", 38, 38);
        add(100, WorldObjectKind::Structure, "SOLID", 10, 4);
        add(101, WorldObjectKind::Structure, "DRIVE", 30, 30);
        add(102, WorldObjectKind::Structure, "HIDDEN", 35, 30);
        add(103, WorldObjectKind::Structure, "GATE", 20, 30);
        add(104, WorldObjectKind::Structure, "REPAIR", 60, 50);
        add(105, WorldObjectKind::Structure, "REPAIR", 70, 50, "Enemy");
        add(106, WorldObjectKind::Structure, "WALL", 9, 8, "Enemy");
        findMutableObjectById(state, 1)->health = 50;
        findMutableObjectById(state, 5)->health = 50;
        findMutableObjectById(state, 7)->health = 50;
        findMutableObjectById(state, 7)->onBridge = true;
        for (int history = 0; history < 1024; ++history)
            add(1000 + history, WorldObjectKind::Unit, "CAR", history % 96, history / 96, "Enemy", false);
        ensureRuntimeGatesInitialized(state);
        RuntimeRepairDepotContext repair;
        repair.rules = &state.rules; repair.objects = &state.world.getObjects();
        repair.areAllied = [](const std::string& a, const std::string& b) { return a == b; };
        repair.resolveHostDomain = repair.resolveUnitDomain = [](int) {
            return std::optional<RuntimeRepairDepotDomain>{RuntimeRepairDepotDomain::Ground};
        };
        repair.resolveDockCell = [](int host, int, int) {
            return std::optional<RuntimeRepairDepotCell>{{61, 51, host == 105}};
        };
        repair.resolveQueueCell = [](int, int slot, int) {
            return std::optional<RuntimeRepairDepotCell>{{62 + slot, 51, true}};
        };
        repair.canPathTo = [](int, const RuntimeRepairDepotCell&) { return true; };
        if (!state.repairDepots.reserveDock(repair, 104, 1, state.tick).accepted
            || !state.repairDepots.reserveDock(repair, 104, 7, state.tick).accepted
            || !state.repairDepots.reserveDock(repair, 105, 5, state.tick).accepted
            || !state.repairDepots.attachAtDock(repair, 104, 1, {61, 51, false}, state.tick).accepted)
            return false;
        bool docked = false, reserved = false, queued = false;
        for (const auto& relation : state.repairDepots.relations()) {
            docked |= relation.phase == RuntimeRepairDepotRelationPhase::Docked;
            reserved |= relation.phase == RuntimeRepairDepotRelationPhase::Reserved;
            queued |= relation.phase == RuntimeRepairDepotRelationPhase::Queued;
        }
        return docked && reserved && queued
            && runtimeObjectMovementMode(state, *findObjectById(state, 3)) == PathMovementMode::Naval
            && runtimeObjectMovementMode(state, *findObjectById(state, 4)) == PathMovementMode::Air
            && runtimeCrusherUsesCrushingMovement(state, *findObjectById(state, 6));
    };
    auto oldStorage = std::make_unique<RuntimeWindowState>();
    auto newStorage = std::make_unique<RuntimeWindowState>();
    auto& oldState = *oldStorage; auto& newState = *newStorage;
    if (!makeFixture(oldState) || !makeFixture(newState)) return fail("authored rules/repair phases/movement fixture");
    const auto fields = [](const PathTerrainCell& cell) {
        return std::tie(cell.cell.x, cell.cell.y, cell.level, cell.passable, cell.water,
            cell.shore, cell.bridge, cell.ramp, cell.cliff, cell.resource, cell.cost, cell.hazardCost);
    };
    const auto authoritativeHash = [](const RuntimeWindowState& state) {
        std::uint64_t hash = state.world.deterministicSimulationStateHash();
        runtimeStableHashMix(hash, state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects()));
        runtimeStableHashMix(hash, state.gates.stateHash()); runtimeStableHashMix(hash, state.repairDepots.stateHash());
        runtimeStableHashMix(hash, state.magBeams.stateHash());
        return hash;
    };
    std::size_t cases = 0, comparedCells = 0, directPredicates = 0, representativeQueryCells = 0;
    const auto mutate = [&](RuntimeWindowState& state, int phase) {
        RuntimeSnapshotActiveStateScope active(state);
        state.tick = 20 + phase;
        auto* solid = state.objectIndex.find(state.world.accessObjects(), 100);
        solid->alive = phase % 4 != 1; solid->health = phase % 4 == 2 ? 0 : 100;
        solid->mission = phase % 4 == 3 ? "InsideTransport" : "Guard";
        solid->x = 10 + phase % 3; solid->cell = runtimeCellKey(solid->x, solid->y);
        auto* hidden = findMutableObjectById(state, 102);
        hidden->mission = phase % 2 ? "InsideTransport" : "Guard";
        auto* mcv = findMutableObjectById(state, 90);
        mcv->mission = phase % 3 ? "McvDeployReady" : "Guard";
        state.behaviors[90].moveTarget = {40 + phase % 4, 40 + phase % 3};
        if (phase == 8) std::reverse(state.world.accessObjects().begin(), state.world.accessObjects().end());
        if (phase == 9) {
            auto extra = *findObjectById(state, 101); extra.instanceId = 3000;
            extra.x = 11; extra.y = 4; extra.cell = runtimeCellKey(extra.x, extra.y);
            state.world.accessObjects().push_back(extra);
        }
        if (phase == 12) {
            // Rebuild the static topology at the same size and retain all
            // duplicate/aliased offsets after a load-style coordinate change.
            auto& cells = state.world.accessTerrainCells();
            std::swap(cells[0], cells[cells.size() - 2]);
            state.terrainByCell = buildRuntimeTerrainLookup(state.world);
            state.pathTerrainCache.valid = false;
        }
    };
    for (int phase = 0; phase < 24; ++phase) {
        mutate(oldState, phase); mutate(newState, phase);
        for (int moverId : {1, 2, 3, 4, 5, 6}) for (int allowedId : {-1, 100, 101, 102}) {
            const std::unordered_set<int> ignored = phase % 2
                ? std::unordered_set<int>{90, 100, 101, 102, 103, 104, 105, 106, 3000}
                : std::unordered_set<int>{};
            const auto beforeOld = authoritativeHash(oldState), beforeNew = authoritativeHash(newState);
            const auto build = [&](RuntimeWindowState& state, bool reference, bool oldOccupantQueries) {
                RuntimeSnapshotActiveStateScope active(state);
                RuntimeSimulationQueryReuseReferenceScope mode(reference);
                RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(oldOccupantQueries);
                state.pathTerrainCache.dynamicValid = false;
                return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                    state, *findObjectById(state, moverId), allowedId, &ignored, reference));
            };
            const auto oldCells = build(oldState, true, true);
            const auto previousQueries = build(newState, false, true);
            const auto newCells = build(newState, false, false);
            const std::string context = "phase=" + std::to_string(phase) + " mover=" + std::to_string(moverId)
                + " allowed=" + std::to_string(allowedId);
            ++cases;
            if (oldCells.size() != newCells.size() || oldCells.size()!=previousQueries.size())
                return fail(context + " size");
            for (std::size_t index = 0; index < oldCells.size(); ++index) {
                if (fields(oldCells[index]) != fields(newCells[index]))
                    return fail(context + " fields at offset " + std::to_string(index));
                if (fields(previousQueries[index])!=fields(newCells[index]))
                    return fail(context + " independent original occupant lookup fields at offset " + std::to_string(index));
                ++comparedCells;
                ++representativeQueryCells;
            }
            if (beforeOld != authoritativeHash(oldState) || beforeNew != authoritativeHash(newState)
                || beforeOld != beforeNew) return fail(context + " authoritative query hash");
            if (moverId != 4) {
                RuntimeSnapshotActiveStateScope active(newState);
                RuntimeSimulationQueryReuseReferenceScope mode(false);
                const auto mover = *findObjectById(newState, moverId);
                const auto gates = runtimeTransparentGateObjectIds(newState, mover, RuntimeGatePassageMode::Planning);
                const bool crushing = runtimeCrusherUsesCrushingMovement(newState, mover);
                RuntimePathTerrainQueryScope query(newState, mover, allowedId, gates, crushing, true);
                const auto& foundations = runtimeSolidFoundationsByCell(newState);
                for (const RuntimeCellPoint point : {RuntimeCellPoint{10 + phase % 3, 4}, {1010, 3},
                        {30, 30}, {31, 30}, {35, 30}, {36, 30}, {20, 30}, {61, 51}, {62, 51},
                        {40 + phase % 4, 40 + phase % 3}, {-1, 0}}) {
                    const auto found = foundations.find(runtimeCellKey(point.x, point.y));
                    const bool optimized = found != foundations.end() && query.foundationBlocks(point,
                        found->second, newState.gateIdsByCell.contains(runtimeCellKey(point.x, point.y)));
                    if (optimized != runtimeFoundationBlocksMovement(newState, mover, point, allowedId,
                            crushing, RuntimeGatePassageMode::Planning)
                        || query.repairReserved(point) != runtimeCellReservedByOtherRepairUnit(newState, point, moverId)
                        || query.allowedFootprint(point) != runtimeCellIsAllowedOccupiedStructureFootprint(
                            newState, point, allowedId, &mover)) return fail(context + " direct qualification");
                    ++directPredicates;
                }
            }
        }
    }
    if (comparedCells < 5000000) return fail("full snapshot comparison coverage below five million cells");
    if (representativeQueryCells!=comparedCells)
        return fail("independent original/dense representative lookup did not compare every full snapshot field");

    // A cached foundation intentionally uses its original offsets for the
    // rest of its tick. Do not replace that legacy lifetime with a new scan
    // after a same-tick death/revival or a host moving to another footprint.
    const std::unordered_set<int> ignoredFoundations{90, 100, 101, 102, 103, 104, 105, 106, 3000};
    for (int mutation = 0; mutation < 3; ++mutation) {
        const auto snapshot = [&](RuntimeWindowState& state, bool reference) {
            RuntimeSnapshotActiveStateScope active(state);
            RuntimeSimulationQueryReuseReferenceScope mode(reference);
            RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(reference);
            auto* host = state.objectIndex.find(state.world.accessObjects(), 100);
            host->alive = mutation != 0;
            if (mutation == 2) { host->x = 70; host->y = 70; host->cell = runtimeCellKey(70, 70); }
            state.pathTerrainCache.dynamicValid = false;
            return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(state,
                *findObjectById(state, 6), 100, &ignoredFoundations, reference));
        };
        const auto oldCells = snapshot(oldState, true), newCells = snapshot(newState, false);
        if (oldState.foundationCollisionTick != oldState.tick
            || newState.foundationCollisionTick != newState.tick) return fail("same-tick foundation fixture was not cached");
        for (std::size_t index = 0; index < oldCells.size(); ++index)
            if (fields(oldCells[index]) != fields(newCells[index]))
                return fail("same-tick foundation death/revival/move changed offset " + std::to_string(index));
        if (authoritativeHash(oldState) != authoritativeHash(newState)) return fail("same-tick foundation mutation hash");
    }
    {
        RuntimeSnapshotActiveStateScope active(newState);
        RuntimeSimulationQueryReuseReferenceScope mode(false);
        const auto mover = *findObjectById(newState, 6);
        const auto gates = runtimeTransparentGateObjectIds(newState, mover, RuntimeGatePassageMode::Planning);
        RuntimePathTerrainQueryScope query(newState, mover, -1, gates, true, true);
        std::vector<PathTerrainCell> blocked = runtimeStaticPathTerrainSnapshot(newState);
        for (auto& cell : blocked) cell.passable = false;
        newState.foundationCollisionTick = -1;
        applyRuntimePathFoundationConstraints(newState, mover, -1, true, query, blocked);
        if (newState.foundationCollisionTick != -1) return fail("all-blocked snapshot initialized foundation cache");

        // Existing smoke fixtures can author staticCells without an index.
        // That supported case must use the original scalar final defense.
        auto firstIndices = std::move(newState.pathTerrainCache.staticFirstIndexByCell);
        auto nextIndices = std::move(newState.pathTerrainCache.staticNextIndexByCell);
        auto scalar = runtimeStaticPathTerrainSnapshot(newState), fallback = scalar;
        {
            RuntimeSimulationQueryReuseReferenceScope reference(true);
            applyRuntimePathFoundationConstraints(newState, mover, -1, true, query, scalar);
        }
        applyRuntimePathFoundationConstraints(newState, mover, -1, true, query, fallback);
        for (std::size_t index = 0; index < scalar.size(); ++index)
            if (fields(scalar[index]) != fields(fallback[index])) return fail("unindexed static fixture fallback");
        newState.pathTerrainCache.staticFirstIndexByCell = std::move(firstIndices);
        newState.pathTerrainCache.staticNextIndexByCell = std::move(nextIndices);
    }

    // Real path planning and committed movement observe these qualifications
    // across independent worlds and caches, with a strict hash each tick.
    std::size_t committedMoves = 0;
    for (int step = 0; step < 96; ++step) {
        for (const bool reference : {true, false}) {
            auto& state = reference ? oldState : newState;
            RuntimeSnapshotActiveStateScope active(state);
            RuntimeSimulationQueryReuseReferenceScope mode(reference);
            RuntimePathSnapshotOccupancyQueryReferenceScope occupantQueries(reference);
            state.tick = 100 + step;
            for (int moverId : {2, 6}) {
                auto* mover = findMutableObjectById(state, moverId);
                auto& behavior = state.behaviors[moverId];
                if (step % 24 == 0) {
                    behavior.kind = RuntimeBehaviorKind::Move; behavior.state = RuntimeBehaviorState::Queued;
                    behavior.moveTarget = {step % 48 == 0 ? 16 : 4, moverId == 2 ? 4 : 8};
                    mover->mission = "Move";
                    replaceRuntimeBehaviorPath(behavior, findRuntimePathUntimed(state, *mover,
                        runtimeCellPointForObject(*mover), behavior.moveTarget));
                }
            }
            prepareRuntimeInfantryStepReservations(state);
            for (int moverId : {2, 6}) {
                auto* mover = findMutableObjectById(state, moverId);
                const auto before = runtimeCellPointForObject(*mover);
                (void)advanceRuntimeMoveBehavior(state, *mover, state.behaviors[moverId]);
                if (!reference && before != runtimeCellPointForObject(*mover)) ++committedMoves;
            }
        }
        if (authoritativeHash(oldState) != authoritativeHash(newState))
            return fail("movement world/subcell hash at tick " + std::to_string(step));
        for (int moverId : {2, 6}) {
            const auto& old = oldState.behaviors.at(moverId); const auto& now = newState.behaviors.at(moverId);
            if (old.path != now.path || old.moveTarget != now.moveTarget || old.state != now.state
                || old.moveCooldownTicks != now.moveCooldownTicks || old.moveSegmentFrom != now.moveSegmentFrom
                || old.moveIntervalRemainderQ16 != now.moveIntervalRemainderQ16 || old.blockedTicks != now.blockedTicks)
                return fail("movement authority at tick " + std::to_string(step));
        }
    }
    if (committedMoves == 0) return fail("movement fixture did not commit a step");
    std::cout << "Path terrain query scope equivalence: cases=" << cases << " fullFieldCells=" << comparedCells
        << " independentRepresentativeQueryCells=" << representativeQueryCells
        << " fields=12 directQualifications=" << directPredicates << " exactMovementHashTicks=96 moves="
        << committedMoves << " sparseFoundation/legacyAliases/duplicateOffsets/repairLayers/MCV/driveRows=yes\n";
    error.clear();
    return true;
}
