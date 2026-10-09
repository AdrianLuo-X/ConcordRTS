bool runRuntimeGroundDepartureQueryBatchContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "ground departure query batch: " + why;
        return false;
    };
    auto box = std::make_unique<RuntimeWindowState>();
    auto& state = *box;
    RuntimeSnapshotActiveStateScope active(state);
    IniFile ini;
    if (!ini.loadFromString("[VehicleTypes]\n0=CAR\n1=PLANE\n[InfantryTypes]\n0=MAN\n1=FLYMAN\n"
        "[BuildingTypes]\n0=HOUSE\n[CAR]\nStrength=200\nSpeed=8\nSpeedType=Track\nMovementZone=Normal\n"
        "[PLANE]\nStrength=200\nSpeed=8\nSpeedType=Winged\nMovementZone=Fly\n"
        "[MAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
        "[FLYMAN]\nStrength=100\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n[HOUSE]\nStrength=1000\n")
        || !state.rules.loadFromIni(ini)) return fail("fixture rules");
    state.rulesLoaded = true; state.config.fixedTickHz = 30; state.tick = 8;
    state.structureFootprints["HOUSE"] = {4, 3, true};
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
    const RuntimeCellPoint origin{10, 10};
    const auto neighbors = runtimeNeighborsOf(origin);
    const auto add = [&](int id, WorldObjectKind kind, const char* type, RuntimeCellPoint cell, bool alive = true) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = id % 3 == 0 ? "Enemy" : id % 3 == 1 ? "Player" : "Neutral";
        object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
        object.alive = alive; object.health = 100; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Infantry, "MAN", origin);
    add(2, WorldObjectKind::Unit, "CAR", {9, 9});
    for (int neighbor = 0; neighbor < 8; ++neighbor) {
        add(10 + neighbor, WorldObjectKind::Infantry, "MAN", neighbors[neighbor]);
        state.behaviors[10 + neighbor] = RuntimeObjectBehavior{};
        for (int resident = 0; resident < 3; ++resident)
            add(30 + neighbor * 3 + resident, WorldObjectKind::Infantry, resident == 2 ? "FLYMAN" : "MAN", neighbors[neighbor]);
    }
    constexpr int historicPopulation = 384;
    for (int history = 0; history < historicPopulation; ++history) {
        add(1000 + history, WorldObjectKind::Infantry, "MAN", neighbors[history % 8], false);
        auto& behavior = state.behaviors[1000 + history];
        behavior.moveCooldownTicks = 5; behavior.moveSegmentFrom = neighbors[history % 8];
    }
    auto* mover = findMutableObjectById(state, 1);
    RuntimeGroundDepartureBatchReferenceScope forceOptimized(false);
    int comparisons = 0, diagonalComparisons = 0;
    std::uint64_t cachedQueries = 0, behaviorVisits = 0, residentVisits = 0;
    const auto originalCell = [&](const WorldObjectInstance& source, RuntimeCellPoint cell) {
        RuntimeGroundDepartureBatchReferenceScope forceReference(true);
        return runtimeGroundDepartureHoldsCell(state, source, cell);
    };
    const auto originalTransition = [&](const WorldObjectInstance& source, RuntimeCellPoint from, RuntimeCellPoint to) {
        RuntimeGroundDepartureBatchReferenceScope forceReference(true);
        return runtimeGroundDepartureHoldsTransition(state, source, from, to);
    };
    const auto behaviorRecords = [&] {
        std::ostringstream saved;
        for (const auto& [id, behavior] : state.behaviors)
            saved << runtimeMultiplayerSnapshotBehaviorRecord(id, behavior) << '\n';
        return saved.str();
    };
    // Cover mixed kinds, cooldown edges, dead/zero-health records, departed vs
    // still-at-origin objects and zero/one/two/three infantry residents.
    for (int scenario = 0; scenario < 96; ++scenario) for (bool infantryMover : {false, true}) {
        mover->kind = infantryMover ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        mover->type = infantryMover ? "MAN" : "CAR";
        for (int neighbor = 0; neighbor < 8; ++neighbor) {
            auto* object = findMutableRuntimeObjectByIdIncludingInactive(state, 10 + neighbor);
            const int mode = (scenario / 4 + neighbor) % 4;
            object->kind = mode == 0 ? WorldObjectKind::Structure
                : mode == 1 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            object->type = mode == 0 ? "HOUSE" : mode == 1 ? "MAN" : mode == 2 ? "CAR" : "PLANE";
            object->alive = (scenario + neighbor) % 13 != 0;
            object->health = (scenario + neighbor) % 11 == 0 ? 0 : 100;
            object->airborne = mode == 3 && (scenario + neighbor) % 2 == 0;
            object->level = neighbor % 3; object->onBridge = neighbor % 2 != 0;
            const auto from = neighbors[neighbor];
            object->x = from.x + ((scenario + neighbor) % 5 == 0 ? 0 : 2);
            object->y = from.y + ((scenario + neighbor) % 5 == 0 ? 0 : 2);
            object->cell = runtimeCellKey(object->x, object->y);
            auto& behavior = state.behaviors.at(10 + neighbor);
            behavior.moveSegmentFrom = from;
            behavior.moveCooldownTicks = (scenario + neighbor) % 3;
            for (int resident = 0; resident < 3; ++resident) {
                auto* other = findMutableRuntimeObjectByIdIncludingInactive(state, 30 + neighbor * 3 + resident);
                other->alive = resident < (scenario + neighbor) % 4;
                other->health = (scenario + neighbor + resident) % 17 == 0 ? 0 : 100;
                other->airborne = (scenario + neighbor + resident) % 19 == 0;
            }
        }
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto behaviorsBefore = behaviorRecords();
        {
            RuntimeGroundDepartureQueryBatch batch(state, *mover, origin);
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                const RuntimeCellPoint cell{origin.x + dx, origin.y + dy};
                if (originalCell(*mover, cell) != runtimeGroundDepartureHoldsCell(state, *mover, cell))
                    return fail("neighbor/mixed/resident threshold/scalar fallback differs scenario=" + std::to_string(scenario));
                ++comparisons;
            }
            for (const auto& to : neighbors) {
                if (originalTransition(*mover, origin, to) != runtimeGroundDepartureHoldsTransition(state, *mover, origin, to))
                    return fail("diagonal three-cell departure differs");
                ++diagonalComparisons;
            }
            auto copyOfMover = *mover;
            if (batch.query(state, copyOfMover, neighbors[0]).has_value()) return fail("different mover identity reused a batch");
            const auto* otherMover = findObjectById(state, 2);
            {
                RuntimeGroundDepartureQueryBatch nested(state, *otherMover, {9, 9});
                if (originalCell(*otherMover, origin) != runtimeGroundDepartureHoldsCell(state, *otherMover, origin))
                    return fail("nested mover batch differs");
            }
            if (originalCell(*mover, neighbors[0]) != runtimeGroundDepartureHoldsCell(state, *mover, neighbors[0]))
                return fail("nested scope did not restore outer mover");
            if (batch.enabled() && (batch.behaviorVisits() != state.behaviors.size()
                || batch.residentVisits() > state.world.getObjects().size()))
                return fail("batch repeated population/behavior scans");
            cachedQueries += batch.queryHits(); behaviorVisits += batch.behaviorVisits(); residentVisits += batch.residentVisits();
        }
        if (runtimeActiveGroundDepartureQueryBatch != nullptr) return fail("scope leaked into next unit/tick");
        if (worldHash != state.world.deterministicSimulationStateHash() || behaviorsBefore != behaviorRecords())
            return fail("read-only query changed authoritative world/behavior state");
    }
    // A real lifted vehicle is excluded from departing-ground claims; resident
    // infantry deliberately retains the scalar filter regardless of ownership.
    auto* lifted = findMutableRuntimeObjectByIdIncludingInactive(state, 10);
    lifted->alive = true; lifted->health = 100; lifted->kind = WorldObjectKind::Unit; lifted->type = "CAR";
    lifted->x = 12; lifted->y = 10; lifted->airborne = false;
    state.behaviors[10].moveSegmentFrom = {11, 10}; state.behaviors[10].moveCooldownTicks = 5;
    RuntimeMagBeamAcquireRequest acquire;
    acquire.currentTick = state.tick;
    acquire.source = {2, "Neutral", "CAR", true, true};
    acquire.target.objectId = 10; acquire.target.owner = lifted->owner; acquire.target.typeId = "CAR";
    acquire.target.kind = RuntimeMagBeamTargetKind::Vehicle;
    acquire.target.present = acquire.target.alive = acquire.target.enemyToSource = true;
    acquire.target.onGround = acquire.target.liftable = true;
    acquire.target.weight = 1; acquire.target.currentStrength = acquire.target.maximumStrength = 100;
    acquire.target.originalLocomotorId = "{4A582741-9839-11d1-B709-00A024DDAFD1}";
    acquire.target.kinematics.location = {12, 10, runtimeCellKey(12, 10), 0, false, false};
    acquire.target.kinematics.worldX = 12; acquire.target.kinematics.worldY = 10;
    acquire.weapon = {"MagneticBeam", "LocomotorBeam", "{92612C46-F71F-11d1-AC9F-006008055BB5}",
        true, true, true, true, 5000};
    const auto acquired = state.magBeams.acquire(acquire);
    if (!acquired.accepted) return fail("mag-beam fixture acquire");
    RuntimeMagBeamAdvanceInput lift; lift.currentTick = ++state.tick;
    lift.sources.push_back(acquire.source); lift.targets.push_back(acquire.target);
    RuntimeMagBeamHostUpdate update;
    update.bindingId = acquired.bindingId; update.kind = RuntimeMagBeamHostUpdateKind::LiftStarted;
    update.kinematics = acquire.target.kinematics; update.kinematics.worldZ = 3;
    lift.hostUpdates.push_back(update);
    if (!state.magBeams.advance(lift).advanced || !runtimeObjectOffGroundByMagBeam(state, 10)) return fail("mag-beam fixture lift");
    {
        RuntimeGroundDepartureQueryBatch batch(state, *mover, origin);
        for (const auto& to : neighbors)
            if (originalTransition(*mover, origin, to) != runtimeGroundDepartureHoldsTransition(state, *mover, origin, to))
                return fail("lifted vehicle altered departure filtering");
        batch.invalidate();
        state.behaviors.at(10).moveCooldownTicks = 0;
        if (batch.query(state, *mover, neighbors[0]).has_value()
            || originalCell(*mover, neighbors[0]) != runtimeGroundDepartureHoldsCell(state, *mover, neighbors[0]))
            return fail("explicit mutation boundary retained a batch");
    }
    {
        RuntimeGroundDepartureQueryBatch batch(state, *mover, origin);
        ++state.tick;
        if (batch.query(state, *mover, neighbors[0]).has_value()) return fail("same batch survived a fixed tick change");
    }
    // Air movers and disabled-reference scopes preserve their scalar behavior.
    mover->kind = WorldObjectKind::Unit; mover->type = "PLANE";
    {
        RuntimeGroundDepartureQueryBatch batch(state, *mover, origin);
        for (const auto& to : neighbors)
            if (runtimeGroundDepartureHoldsCell(state, *mover, to) != originalCell(*mover, to)) return fail("air mover differs");
    }
    { RuntimeGroundDepartureBatchReferenceScope onlyNewReference(true);
      RuntimeGroundDepartureQueryBatch disabled(state, *mover, origin);
      if (disabled.enabled() || disabled.query(state, *mover, origin).has_value()) return fail("independent reference ignored"); }
    std::cout << "Ground departure batch equivalence passed: cellComparisons=" << comparisons
        << " diagonalComparisons=" << diagonalComparisons << " historicalObjects=" << historicPopulation
        << " cachedQueries=" << cachedQueries << " behaviorVisits=" << behaviorVisits << " residentVisits=" << residentVisits
        << " mixed/residentThreshold/owner/air/footprint/death/magbeam/nested/identity/tick/mutation=yes\n";
    error.clear(); return true;
}
