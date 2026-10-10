bool runRuntimeBuildingDepartureReadOnlyQueryContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "building departure readonly query: " + why; return false;
    };
    const auto rulesText = [](int rows) {
        return std::string("[VehicleTypes]\n0=CAR\n1=AIR\n[InfantryTypes]\n0=MAN\n")
            + "[BuildingTypes]\n0=LANE\n1=SOLID\n2=HIDDEN\n"
              "[CAR]\nStrength=100\nSpeedType=Wheel\nMovementZone=Normal\n"
              "[AIR]\nStrength=100\nSpeedType=Winged\nMovementZone=Fly\n"
              "[MAN]\nStrength=100\nSpeedType=Foot\nMovementZone=Infantry\n"
              "[LANE]\nStrength=100\nImage=LANEART\nUnitRepair=yes\nNumberImpassableRows="
            + std::to_string(rows)
            + "\n[SOLID]\nStrength=100\nImage=LANEART\nNumberImpassableRows=0\n"
              "[HIDDEN]\nStrength=100\nImage=LANEART\nNumberImpassableRows=2\nInvisibleInGame=yes\n";
    };
    const auto load = [&](RuntimeWindowState& state, int rows) {
        IniFile rules, art;
        if (!rules.loadFromString(rulesText(rows))
            || !art.loadFromString("[LANEART]\nFoundation=4x3\n")
            || !state.rules.loadFromIni(rules) || !state.art.loadFromIni(art)) return false;
        state.rulesLoaded = state.artLoaded = true;
        const auto* lane = state.rules.findBuildingType("LANE");
        return lane != nullptr && lane->numberImpassableRows == rows && lane->unitRepair;
    };
    const auto makeObject = [](int id, WorldObjectKind kind, const char* type, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type; object.owner = "Player";
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.alive = true; object.health = 100; object.mission = "Guard";
        return object;
    };
    const auto hash = [](const RuntimeWindowState& state) {
        std::uint64_t value = state.world.deterministicSimulationStateHash();
        runtimeStableHashMix(value, state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects()));
        runtimeStableHashMix(value, state.gates.stateHash());
        runtimeStableHashMix(value, state.repairDepots.stateHash());
        runtimeStableHashMix(value, state.magBeams.stateHash());
        return value;
    };
    auto foreignStorage = std::make_unique<RuntimeWindowState>();
    auto& foreign = *foreignStorage;
    if (!load(foreign, 2)) return fail("foreign rules/art");
    foreign.tick = 7;
    foreign.world.accessObjects().push_back(makeObject(100, WorldObjectKind::Unit, "CAR", 12, 11));
    foreign.world.accessObjects().push_back(makeObject(77, WorldObjectKind::Structure, "LANE", 10, 10));
    foreign.structureFootprints = buildRuntimeStructureFootprints(foreign);
    if (!foreign.structureFootprints.contains("LANE")
        || foreign.structureFootprints.at("LANE").width != 4
        || foreign.structureFootprints.at("LANE").height != 3) return fail("foreign authored foundation");
    const auto& foreignMover = foreign.world.getObjects().front();
    std::size_t cases = 0, queries = 0;
    for (int history : {0, 1000, 10000}) for (int order = 0; order < 4; ++order)
        for (int phase = 0; phase < 16; ++phase) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        if (!load(state, 2)) return fail("rules/art readback");
        state.tick = 30 + phase;
        auto& objects = state.world.accessObjects();
        for (int index = 0; index < history; ++index) {
            auto dead = makeObject(100000 + index, WorldObjectKind::Unit, "CAR", 80, 80);
            dead.alive = false; dead.health = 0; objects.push_back(std::move(dead));
        }
        objects.push_back(makeObject(100, WorldObjectKind::Unit, "CAR", 12, 11));
        objects.push_back(makeObject(101, WorldObjectKind::Unit, "CAR", 13, 11));
        objects.push_back(makeObject(102, WorldObjectKind::Unit, "CAR", 11, 11));
        objects.push_back(makeObject(103, WorldObjectKind::Unit, "CAR", 20, 20));
        auto zeroHealth = makeObject(104, WorldObjectKind::Unit, "CAR", 12, 11);
        zeroHealth.health = 0; objects.push_back(zeroHealth);
        zeroHealth.instanceId = 105; zeroHealth.health = -1; objects.push_back(zeroHealth);
        auto deadMover = makeObject(106, WorldObjectKind::Unit, "CAR", 12, 11);
        deadMover.alive = false; deadMover.health = 0; objects.push_back(deadMover);
        objects.push_back(makeObject(107, WorldObjectKind::Infantry, "MAN", 12, 11));
        objects.push_back(makeObject(108, WorldObjectKind::Unit, "AIR", 12, 11));
        objects.push_back(makeObject(109, WorldObjectKind::Unit, "MISSING", 12, 11));
        objects.push_back(makeObject(50, WorldObjectKind::Structure, "LANE", 10, 10));
        auto zeroHost = makeObject(17, WorldObjectKind::Structure, "LANE", 10, 10);
        zeroHost.health = 0; objects.push_back(zeroHost);
        objects.push_back(makeObject(17, WorldObjectKind::Structure, "LANE", 10, 10));
        auto deadHost = makeObject(7, WorldObjectKind::Structure, "LANE", 10, 10);
        deadHost.alive = false; deadHost.health = 0; objects.push_back(deadHost);
        objects.push_back(makeObject(11, WorldObjectKind::Structure, "SOLID", 10, 10));
        objects.push_back(makeObject(5, WorldObjectKind::Structure, "HIDDEN", 10, 10));
        objects.push_back(makeObject(4, WorldObjectKind::Unit, "LANE", 10, 10));
        objects.push_back(makeObject(20, WorldObjectKind::Structure, "LANE", 11, 10));
        for (auto& object : objects) {
            if (object.instanceId == 17) {
                if (phase == 1) object.alive = false;
                if (phase == 2) object.kind = WorldObjectKind::Unit;
                if (phase == 3) object.type = "SOLID";
                if (phase == 10) object.health = 0;
            }
            if (object.instanceId == 100) {
                if (phase == 4) { object.x = 11; object.cell = runtimeCellKey(11, object.y); }
                if (phase == 5) { object.x = 13; object.cell = runtimeCellKey(13, object.y); }
                if (phase == 11) object.alive = false;
            }
        }
        if (phase == 13) objects.push_back(makeObject(3, WorldObjectKind::Structure, "LANE", 10, 10));
        if (phase == 15) objects.push_back(makeObject(-5, WorldObjectKind::Structure, "LANE", 10, 10));
        if (phase == 14 && !load(state, 4)) return fail("rules replacement outside scope");
        state.structureFootprints = buildRuntimeStructureFootprints(state);
        if (!state.structureFootprints.contains("LANE")
            || state.structureFootprints.at("LANE").width != 4
            || state.structureFootprints.at("LANE").height != 3) return fail("authored foundation readback");
        if (phase == 12 || order == 1) std::reverse(objects.begin(), objects.end());
        if (order == 2) std::rotate(objects.begin(), objects.begin() + objects.size() / 2, objects.end());
        if (order == 3) std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) {
            return a.instanceId < b.instanceId;
        });
        if (phase >= 6 && phase <= 9) {
            state.tickOccupancyCacheTick = phase == 9 ? state.tick - 1 : state.tick;
            if (phase == 7 || phase == 8) {
                const int key = runtimeCellKey(12, 11);
                state.tickGroundOccupantCountByCell[key] = phase == 7 ? 2 : 1;
                state.tickOccupiedByCell[key] = phase == 7 ? 100 : 50;
            }
        }
        const std::string detail = "history=" + std::to_string(history) + " order=" + std::to_string(order)
            + " phase=" + std::to_string(phase);
        const auto initialHash = hash(state);
        for (int moverId = 100; moverId <= 109; ++moverId) {
            const auto found = std::find_if(objects.begin(), objects.end(), [=](const auto& object) {
                return object.instanceId == moverId;
            });
            if (found == objects.end()) return fail(detail + " missing authored mover");
            const auto& mover = *found;
            const int expected = [&] {
                RuntimeHistoryQueryReferenceScope reference(true);
                return runtimeStructureDepartureHostId(state, mover);
            }();
            if (moverId == 100 && phase != 15) {
                const int golden = phase == 1 || phase == 2 || phase == 3 ? 50
                    : phase == 4 || phase == 6 || phase == 11 || phase == 14 ? -1
                    : phase == 13 ? 3 : 17;
                if (expected != golden) return fail(detail + " original lowest-ID/home-cell/occupancy golden");
            }
            RuntimeHistoryQueryReferenceScope optimized(false);
            RuntimeBuildingDepartureReadOnlyQueryScope scope(state, mover);
            if (scope.materializations() != 0) return fail(detail + " scope was not lazy");
            for (int query = 0; query < 16; ++query) {
                if (runtimeStructureDepartureHostId(state, mover) != expected)
                    return fail(detail + " original/cached departure result");
                ++queries;
            }
            if (scope.materializations() != 1) return fail(detail + " repeated or negative result not memoized");
            auto borrowed = mover;
            if (scope.find(state, borrowed) || scope.find(foreign, foreignMover)
                || runtimeStructureDepartureHostId(state, borrowed) != expected
                || runtimeStructureDepartureHostId(foreign, foreignMover) != 77
                || scope.materializations() != 1) return fail(detail + " borrowed/state identity");
            {
                RuntimeBuildingDepartureReadOnlyQueryScope disabled(state, mover, false);
                if (disabled.find(state, mover)
                    || runtimeActiveBuildingDepartureReadOnlyQueries != &disabled
                    || runtimeStructureDepartureHostId(state, mover) != expected
                    || disabled.materializations() != 0) return fail(detail + " disabled nested scope");
            }
            if (runtimeActiveBuildingDepartureReadOnlyQueries != &scope
                || runtimeStructureDepartureHostId(state, mover) != expected || scope.materializations() != 1)
                return fail(detail + " disabled scope restoration");
            {
                RuntimeBuildingDepartureReadOnlyQueryScope nested(foreign, foreignMover);
                if (nested.find(state, mover) || runtimeStructureDepartureHostId(state, mover) != expected
                    || runtimeStructureDepartureHostId(foreign, foreignMover) != 77
                    || nested.materializations() != 1) return fail(detail + " foreign nested scope");
                RuntimeHistoryQueryReferenceScope reference(true);
                if (nested.find(foreign, foreignMover)
                    || runtimeStructureDepartureHostId(foreign, foreignMover) != 77
                    || nested.materializations() != 1) return fail(detail + " reference override");
            }
            if (runtimeActiveBuildingDepartureReadOnlyQueries != &scope
                || runtimeStructureDepartureHostId(state, mover) != expected || scope.materializations() != 1)
                return fail(detail + " nested restoration");
            ++state.tick;
            if (scope.find(state, mover)) return fail(detail + " tick identity");
            const auto tickExpected = [&] { RuntimeHistoryQueryReferenceScope reference(true);
                return runtimeStructureDepartureHostId(state, mover); }();
            if (runtimeStructureDepartureHostId(state, mover) != tickExpected || scope.materializations() != 1)
                return fail(detail + " new-tick scalar fallback");
            --state.tick;
            scope.invalidate();
            if (scope.find(state, mover) || runtimeStructureDepartureHostId(state, mover) != expected
                || scope.materializations() != 1) return fail(detail + " explicit invalidation");
            if (hash(state) != initialHash) return fail(detail + " query changed world/system authority");
            ++cases;
        }
        // Defensive shape validation uses an external mover copy, whose
        // address remains stable even when world storage is replaced.
        WorldObjectInstance stableMover = makeObject(100, WorldObjectKind::Unit, "CAR", 12, 11);
        RuntimeHistoryQueryReferenceScope optimized(false);
        {
            RuntimeBuildingDepartureReadOnlyQueryScope scope(state, stableMover);
            (void)runtimeStructureDepartureHostId(state, stableMover);
            objects.push_back(makeObject(991, WorldObjectKind::Unit, "CAR", 70, 70));
            const auto after = hash(state);
            const int expected = [&] { RuntimeHistoryQueryReferenceScope reference(true);
                return runtimeStructureDepartureHostId(state, stableMover); }();
            if (scope.find(state, stableMover) || runtimeStructureDepartureHostId(state, stableMover) != expected
                || scope.materializations() != 1 || hash(state) != after) return fail(detail + " appended world fallback");
        }
        {
            RuntimeBuildingDepartureReadOnlyQueryScope scope(state, stableMover);
            (void)runtimeStructureDepartureHostId(state, stableMover);
            objects.reserve(objects.capacity() + 1);
            const auto after = hash(state);
            const int expected = [&] { RuntimeHistoryQueryReferenceScope reference(true);
                return runtimeStructureDepartureHostId(state, stableMover); }();
            if (scope.find(state, stableMover) || runtimeStructureDepartureHostId(state, stableMover) != expected
                || scope.materializations() != 1 || hash(state) != after) return fail(detail + " relocated world fallback");
        }
    }
    if (runtimeActiveBuildingDepartureReadOnlyQueries != nullptr) return fail("scope escaped its lifetime");
    std::cout << "Building departure readonly query: exactCases=" << cases << " repeatedQueries=" << queries
        << " lazyIncludingNegative/lowestID/health0/driveRows/duplicates/history/identity/nested/tick/rules/hash=yes\n";
    error.clear(); return true;
}
