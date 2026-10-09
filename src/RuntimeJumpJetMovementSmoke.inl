bool runRuntimeJumpJetMovementSmokeProbe(
    RuntimeWindowState& live,
    std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "rocketeer movement: " + message;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config;
    probe.config.fixedTickHz = 30;
    probe.rules = live.rules;
    probe.art = live.art;
    probe.rulesLoaded = true;
    probe.artLoaded = true;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.economy.owner = "Americans";
    for (int y = 1; y <= 24; ++y) {
        for (int x = 1; x <= 32; ++x) {
            TerrainCell terrain;
            terrain.x = x;
            terrain.y = y;
            terrain.cellIndex512 = runtimeCellKey(x, y);
            terrain.tileIndex = 0;
            terrain.subTileIndex = 0;
            terrain.level = x >= 14 && x <= 16 ? 4 : 0;
            probe.world.accessTerrainCells().push_back(terrain);
        }
    }
    initializeRuntimeNavigation(probe);
    for (const auto& terrain : probe.world.getTerrainCells()) {
        auto& info = probe.terrainInfoByCell.at(terrain.cellIndex512);
        if (terrain.x >= 10 && terrain.x <= 12) {
            info.kind = RuntimeTerrainKind::Water;
            info.water = true;
        } else if (terrain.x >= 14 && terrain.x <= 16) {
            info.kind = RuntimeTerrainKind::Cliff;
            info.cliff = true;
            info.passable = false;
        }
    }
    auto* created = addRuntimeObjectInstance(
        probe, RuntimeBuildKind::Infantry, "JUMPJET", {4, 8}
    );
    if (created == nullptr || !created->airborne) {
        return fail("could not create the stock flying infantry");
    }
    const int rocketeerId = created->instanceId;
    created = addRuntimeObjectInstance(
        probe, RuntimeBuildKind::Unit, "MTNK", {18, 8}
    );
    if (created == nullptr) return fail("could not create ground blocker");
    const int blockerId = created->instanceId;
    created = addRuntimeObjectInstance(
        probe, RuntimeBuildKind::Unit, "MTNK", {4, 20}
    );
    if (created == nullptr) return fail("could not create chase target");
    created->owner = "Russians";
    const int enemyId = created->instanceId;

    const auto step = [&]() {
        ++probe.tick;
        advanceRuntimeJumpjetFlights(probe);
        probe.actionSummary.localAvoidanceAttemptsThisTick = 0;
        probe.actionSummary.attacksThisTick = 0;
        processRuntimePathRequests(probe);
        prepareRuntimeInfantryStepReservations(probe);
        std::vector<int> movers;
        for (const auto& [id, behavior] : probe.behaviors) {
            (void)behavior;
            movers.push_back(id);
        }
        std::sort(movers.begin(), movers.end());
        for (const int id : movers) {
            auto* object = findMutableObjectById(probe, id);
            auto behavior = probe.behaviors.find(id);
            if (object == nullptr || behavior == probe.behaviors.end()) continue;
            if (behavior->second.kind == RuntimeBehaviorKind::Move) {
                advanceRuntimeMoveBehavior(probe, *object, behavior->second);
            } else if (behavior->second.kind == RuntimeBehaviorKind::Attack) {
                advanceRuntimeAttackBehavior(probe, *object, behavior->second);
            }
        }
        reconcileRuntimeSubcells(probe, false);
        for (auto& object : probe.world.accessObjects()) applyRuntimeJumpjetPose(probe, object);
    };
    const auto flightPoseValid = [&]() {
        const auto* object = findObjectById(probe, rocketeerId);
        return object != nullptr && object->airborne
            && object->level == object->flightLevel
            && object->renderLevel >= terrainLevelAtRuntimeCell(probe, object->x, object->y)
            && !object->onBridge && object->subCell == -1
            && probe.subcells.reservations().find(rocketeerId)
                == probe.subcells.reservations().end();
    };
    bool interpolated = false;
    for (const RuntimeCellPoint destination : {
            RuntimeCellPoint{8, 8}, RuntimeCellPoint{12, 8}, RuntimeCellPoint{18, 8}}) {
        probe.selectedObjectIds = {rocketeerId};
        issueRuntimeMoveCommand(probe, destination);
        if (probe.lastCommand.acceptedCount != 1) {
            return fail("manual move command was rejected");
        }
        bool arrived = false;
        for (int tick = 0; tick < 300; ++tick) {
            const auto before = runtimeCellPointForObject(
                *findObjectById(probe, rocketeerId)
            );
            step();
            const auto* object = findObjectById(probe, rocketeerId);
            const auto after = runtimeCellPointForObject(*object);
            if (!flightPoseValid()) return fail("flight used a ground subcell/height");
            if (std::abs(after.x - before.x) > 1 || std::abs(after.y - before.y) > 1) {
                return fail("flight skipped continuous movement");
            }
            interpolated = interpolated || (object->hasRenderPosition
                && (std::abs(object->renderX - object->x) > 0.001
                    || std::abs(object->renderY - object->y) > 0.001));
            const auto behavior = probe.behaviors.find(rocketeerId);
            if (behavior != probe.behaviors.end()
                && behavior->second.state == RuntimeBehaviorState::Complete) {
                arrived = after == destination;
                break;
            }
        }
        if (!arrived) return fail("accepted move stalled: " + probe.simulationStatus);
    }
    if (!interpolated) return fail("flight never rendered an intermediate position");
    const auto* blocker = findObjectById(probe, blockerId);
    if (runtimeCellPointForObject(*blocker) != RuntimeCellPoint{18, 8}
        || buildRuntimeOccupancyLookup(probe).at(blocker->cell) != blockerId) {
        return fail("flying over a tank changed ground occupancy");
    }

    issueRuntimeMoveCommand(probe, {28, 8});
    bool started = false;
    for (int tick = 0; tick < 60; ++tick) {
        step();
        if (findObjectById(probe, rocketeerId)->renderMoveActive) {
            started = true;
            break;
        }
    }
    if (!started) return fail("second move never started");
    issueRuntimeStopCommand(probe);
    const auto stopped = runtimeCellPointForObject(*findObjectById(probe, rocketeerId));
    for (int tick = 0; tick < 120; ++tick) step();
    if (probe.lastCommand.acceptedCount != 1 || !flightPoseValid()
        || findObjectById(probe, rocketeerId)->renderMoveActive
        || runtimeCellPointForObject(*findObjectById(probe, rocketeerId)) != stopped) {
        return fail("stop did not leave the rocketeer hovering in place");
    }

    issueRuntimeAttackCommand(probe, *findObjectById(probe, enemyId));
    if (probe.lastCommand.acceptedCount != 1) return fail("attack command rejected");
    bool fired = false;
    bool chased = false;
    for (int tick = 0; tick < 600; ++tick) {
        step();
        if (!flightPoseValid()) return fail("chase used ground occupancy");
        chased = chased || runtimeCellPointForObject(
            *findObjectById(probe, rocketeerId)
        ) != stopped;
        if (probe.actionSummary.attacksThisTick > 0) {
            fired = true;
            break;
        }
    }
    if (!chased || !fired) return fail("out-of-range attack stalled: " + probe.simulationStatus);
    issueRuntimeStopCommand(probe);

    auto* rocketeer = findMutableObjectById(probe, rocketeerId);
    setRuntimeObjectCell(probe, *rocketeer, {6, 4});
    rocketeer->facing = runtimeFacingFromDelta(1, 0);
    RuntimeObjectBehavior avoidance;
    avoidance.kind = RuntimeBehaviorKind::Move;
    avoidance.moveTarget = {8, 4};
    if (!tryRuntimeLocalAvoidanceMove(probe, *rocketeer, avoidance, false, "jumpjetProbe")
        || runtimeCellPointForObject(*rocketeer) == RuntimeCellPoint{6, 4}
        || !flightPoseValid()) {
        return fail("local avoidance waited for a ground reservation");
    }

    std::vector<int> groundIds;
    for (int index = 0; index < 3; ++index) {
        created = addRuntimeObjectInstance(
            probe, RuntimeBuildKind::Infantry, "E1", {22, 4}
        );
        if (created == nullptr) return fail("ground crowd setup failed");
        groundIds.push_back(created->instanceId);
    }
    std::vector<int> groupIds;
    std::unordered_set<int> spawnCells;
    for (int index = 0; index < 5; ++index) {
        created = addRuntimeObjectInstance(
            probe, RuntimeBuildKind::Infantry, "JUMPJET", {22, 4}
        );
        if (created == nullptr || !created->airborne || created->subCell != -1
            || !spawnCells.insert(created->cell).second
            || (index == 0 && runtimeCellPointForObject(*created) != RuntimeCellPoint{22, 4})) {
            return fail("air spawn did not separate flyers independently of ground infantry");
        }
        groupIds.push_back(created->instanceId);
    }
    probe.selectedObjectIds = groupIds;
    issueRuntimeMoveCommand(probe, {28, 4});
    if (probe.lastCommand.acceptedCount != 5) return fail("group move rejected flyers");
    std::unordered_set<int> destinationCells;
    for (const int id : groupIds) {
        const auto& goal = probe.behaviors.at(id).moveTarget;
        if (!destinationCells.insert(runtimeCellKey(goal.x, goal.y)).second) {
            return fail("group command assigned overlapping air destinations");
        }
    }
    bool groupArrived = false;
    for (int tick = 0; tick < 300; ++tick) {
        step();
        groupArrived = std::all_of(groupIds.begin(), groupIds.end(), [&](int id) {
            const auto* object = findObjectById(probe, id);
            const auto behavior = probe.behaviors.find(id);
            return object != nullptr && object->airborne && object->subCell == -1
                && behavior != probe.behaviors.end()
                && behavior->second.state == RuntimeBehaviorState::Complete
                && runtimeCellPointForObject(*object) == behavior->second.moveTarget;
        });
        if (groupArrived) break;
    }
    if (!groupArrived) return fail("group flight stalled");
    for (const int id : groundIds) {
        const auto* object = findObjectById(probe, id);
        if (object == nullptr || object->airborne
            || runtimeCellPointForObject(*object) != RuntimeCellPoint{22, 4}
            || !RuntimeSubcellSystemState::isActiveSubCell(object->subCell)) {
            return fail("flight displaced ground infantry");
        }
    }
    std::cout << "JumpJet movement: manual=ok waterCliffTank=ok interpolation=ok "
              << "stopHover=ok chaseFire=ok localAvoidance=ok crowdedSpawn=ok group=5/5\n";
    return true;
}
