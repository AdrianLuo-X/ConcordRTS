bool runRuntimeAirNavigationSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "air navigation: " + reason; return false; };
    const auto makeProbe = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = live.config;
        state.config.fixedTickHz = 30;
        state.rules = live.rules;
        state.art = live.art;
        state.rulesLoaded = state.artLoaded = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.economy.owner = "Americans";
        for (int y = 1; y <= 60; ++y) {
            for (int x = 1; x <= 64; ++x) {
                TerrainCell terrain;
                terrain.x = x; terrain.y = y;
                terrain.cellIndex512 = runtimeCellKey(x, y);
                terrain.tileIndex = terrain.subTileIndex = 0;
                terrain.level = x >= 14 && x <= 16 ? 4 : 0;
                state.world.accessTerrainCells().push_back(terrain);
            }
        }
        initializeRuntimeNavigation(state);
        for (const auto& terrain : state.world.getTerrainCells()) {
            auto& info = state.terrainInfoByCell.at(terrain.cellIndex512);
            if (terrain.x >= 10 && terrain.x <= 12) {
                info.kind = RuntimeTerrainKind::Water; info.water = true;
            } else if (terrain.x >= 14 && terrain.x <= 16) {
                info.kind = RuntimeTerrainKind::Cliff; info.cliff = true; info.passable = false;
            }
        }
        return storage;
    };
    const auto addFlyer = [&](RuntimeWindowState& state, RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstance(state, RuntimeBuildKind::Infantry, "JUMPJET", cell);
        return object == nullptr ? -1 : object->instanceId;
    };
    const auto separated = [&](const RuntimeWindowState& state, int movedId = -1) {
        if (movedId >= 0) {
            const auto* mover = findObjectById(state, movedId);
            if (mover == nullptr || !mover->alive || !mover->airborne) return true;
            const double x = mover->hasRenderPosition ? mover->renderX : mover->x;
            const double y = mover->hasRenderPosition ? mover->renderY : mover->y;
            for (const auto& other : state.world.getObjects()) {
                if (other.instanceId == movedId || !other.alive || !other.airborne) continue;
                if (mover->cell == other.cell) return fail("two flyers occupied cell " + std::to_string(other.cell));
                const double otherX = other.hasRenderPosition ? other.renderX : other.x;
                const double otherY = other.hasRenderPosition ? other.renderY : other.y;
                if (std::hypot(x - otherX, y - otherY) < 0.49) return fail("rendered flyers intersected at tick " + std::to_string(state.tick));
            }
            return true;
        }
        std::unordered_set<int> cells;
        std::vector<const WorldObjectInstance*> flyers;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || !object.airborne) continue;
            if (!cells.insert(runtimeCellKey(object.x, object.y)).second) {
                return fail("two flyers occupied cell " + std::to_string(object.cell));
            }
            const double x = object.hasRenderPosition ? object.renderX : object.x;
            const double y = object.hasRenderPosition ? object.renderY : object.y;
            for (const auto* other : flyers) {
                const double otherX = other->hasRenderPosition ? other->renderX : other->x;
                const double otherY = other->hasRenderPosition ? other->renderY : other->y;
                if (std::hypot(x - otherX, y - otherY) < 0.49) {
                    return fail("rendered flyers intersected: " + std::to_string(object.instanceId)
                        + "/" + std::to_string(other->instanceId) + " tick=" + std::to_string(state.tick));
                }
            }
            flyers.push_back(&object);
        }
        return true;
    };
    const auto step = [&](RuntimeWindowState& state) {
        ++state.tick;
        advanceRuntimeJumpjetFlights(state);
        state.actionSummary.localAvoidanceAttemptsThisTick = 0;
        state.actionSummary.repathsThisTick = 0;
        state.actionSummary.attacksThisTick = 0;
        processRuntimePathRequests(state);
        prepareRuntimeInfantryStepReservations(state);
        std::vector<int> ids;
        for (const auto& [id, behavior] : state.behaviors) { (void)behavior; ids.push_back(id); }
        std::sort(ids.begin(), ids.end());
        for (const int id : ids) {
            auto* object = findMutableObjectById(state, id);
            const auto found = state.behaviors.find(id);
            if (object == nullptr || found == state.behaviors.end()) continue;
            if (found->second.kind == RuntimeBehaviorKind::Move) advanceRuntimeMoveBehavior(state, *object, found->second);
            if (found->second.kind == RuntimeBehaviorKind::Attack) advanceRuntimeAttackBehavior(state, *object, found->second);
            applyRuntimeJumpjetPose(state, *object);
            if (!separated(state, id)) return false;
        }
        reconcileRuntimeSubcells(state, false);
        return separated(state);
    };
    const auto arrived = [](const RuntimeWindowState& state, const std::vector<int>& ids) {
        return std::all_of(ids.begin(), ids.end(), [&](int id) {
            const auto* object = findObjectById(state, id);
            const auto found = state.behaviors.find(id);
            return object != nullptr && found != state.behaviors.end()
                && found->second.state == RuntimeBehaviorState::Complete
                && runtimeCellPointForObject(*object) == found->second.moveTarget
                && !object->renderMoveActive;
        });
    };
    const auto runMove = [&](RuntimeWindowState& state, const std::vector<int>& ids, int maximumTicks = 2400) {
        for (int tick = 0; tick < maximumTicks; ++tick) {
            if (!step(state)) return false;
            if (arrived(state, ids)) return true;
        }
        for (const int id : ids) {
            if (arrived(state, {id})) continue;
            const auto* object = findObjectById(state, id);
            const auto& b = state.behaviors.at(id);
            return fail("flight stalled #" + std::to_string(id) + " at " + std::to_string(object->x)
                + "," + std::to_string(object->y) + " goal=" + std::to_string(b.moveTarget.x)
                + "," + std::to_string(b.moveTarget.y) + " " + state.simulationStatus);
        }
        return fail("flight did not complete");
    };
    const auto uniqueGoals = [&](const RuntimeWindowState& state, const std::vector<int>& ids) {
        std::unordered_set<int> cells;
        for (const int id : ids) {
            const auto& goal = state.behaviors.at(id).moveTarget;
            if (!cells.insert(runtimeCellKey(goal.x, goal.y)).second) return fail("duplicate destination assigned");
        }
        return true;
    };

    // Check the actual short and long routes, including all eight octants.
    {
        auto storage = makeProbe(); auto& state = *storage;
        const int id = addFlyer(state, {32, 16});
        if (id < 0) return fail("could not create route probe");
        for (const RuntimeCellPoint goal : {RuntimeCellPoint{44, 20}, {48, 32}, {44, 44},
                {32, 48}, {20, 44}, {16, 32}, {20, 20}, {32, 16}, {60, 50}}) {
            state.selectedObjectIds = {id};
            const auto start = runtimeCellPointForObject(*findObjectById(state, id));
            issueRuntimeMoveCommand(state, goal);
            if (state.lastCommand.acceptedCount != 1 || state.behaviors.at(id).moveTarget != goal) return fail("open flight target rejected/adjusted");
            bool reached = false;
            for (int tick = 0; tick < 2400; ++tick) {
                if (!step(state)) return false;
                const auto* object = findObjectById(state, id);
                const double deviation = std::abs((goal.x - start.x) * (object->y - start.y)
                    - (goal.y - start.y) * (object->x - start.x))
                    / std::hypot(goal.x - start.x, goal.y - start.y);
                if (deviation > 0.51) return fail("unobstructed flight bent away from its direct line");
                if (object->flightLevel != object->level
                    || object->renderLevel < terrainLevelAtRuntimeCell(state, object->x, object->y))
                    return fail("route entered the terrain during climb");
                if (arrived(state, {id})) { reached = true; break; }
            }
            if (!reached) return fail("direct flight stalled");
        }
    }
    // Issuing a new order while above a cliff preserves height and the visible segment.
    {
        auto storage = makeProbe(); auto& state = *storage;
        const int id = addFlyer(state, {4, 8});
        state.selectedObjectIds = {id}; issueRuntimeMoveCommand(state, {28, 8});
        bool redirected = false;
        for (int tick = 0; tick < 300; ++tick) {
            if (!step(state)) return false;
            const auto* object = findObjectById(state, id);
            if (object->x != 15 || !object->renderMoveActive) continue;
            const int altitude = object->flightLevel;
            const double x = object->renderX, y = object->renderY;
            const auto originalBehavior = state.behaviors.at(id);
            const auto record = runtimeMultiplayerSnapshotBehaviorRecord(id, originalBehavior);
            RuntimeObjectBehavior restored;
            int restoredId = -1;
            if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(record, restoredId, restored)
                || restoredId != id || restored.moveSegmentFrom != originalBehavior.moveSegmentFrom
                || restored.moveCooldownTicks != originalBehavior.moveCooldownTicks) return fail("snapshot lost in-flight occupancy");
            std::ostringstream saved;
            std::string saveError;
            IniFile savedIni;
            RuntimeObjectBehavior loaded;
            if (!writeRuntimeStateToStream(state, saved, saveError)
                || !savedIni.loadFromString(saved.str())
                || !parseRuntimeSavedBehaviorRecord(splitRuntimeSaveFields(savedIni.getValue("Behaviors", "0")),
                    *object, 14, restoredId, loaded)
                || loaded.moveSegmentFrom != originalBehavior.moveSegmentFrom
                || loaded.moveCooldownTicks != originalBehavior.moveCooldownTicks) return fail("save lost in-flight occupancy: " + saveError);
            auto& display = state.behaviors.at(id);
            display.renderFrom = {60, 60};
            display.renderMoveTicksRemaining = 999;
            auto* visible = findMutableObjectById(state, id);
            const bool wasVisible = visible->renderMoveActive;
            visible->renderMoveActive = false;
            if (runtimeAirCellIsClear(state, -1, originalBehavior.moveSegmentFrom)) return fail("display reset released an in-flight departure cell");
            visible->renderMoveActive = wasVisible;
            display = restored;
            // Rebuild display data separately from the authority record.
            display.renderFrom = originalBehavior.renderFrom;
            display.renderTo = originalBehavior.renderTo;
            display.renderFromLevel = originalBehavior.renderFromLevel;
            display.renderToLevel = originalBehavior.renderToLevel;
            display.renderMoveTicksTotal = originalBehavior.renderMoveTicksTotal;
            display.renderMoveTicksRemaining = originalBehavior.renderMoveTicksRemaining;
            issueRuntimeMoveCommand(state, {22, 20});
            object = findObjectById(state, id);
            if (object->flightLevel != altitude || object->level != altitude
                || !object->renderMoveActive || object->renderX != x || object->renderY != y) return fail("redirect snapped flight position/height");
            redirected = true; break;
        }
        if (!redirected || !runMove(state, {id})) return fail("redirect did not finish: " + error);
    }
    // Air spawn spacing is independent from the three ground infantry slots.
    {
        auto storage = makeProbe(); auto& state = *storage;
        for (int index = 0; index < 3; ++index) {
            if (addRuntimeObjectInstance(state, RuntimeBuildKind::Infantry, "E1", {5, 5}) == nullptr) return fail("ground crowd setup failed");
        }
        std::vector<int> ids;
        for (int index = 0; index < 9; ++index) {
            const int id = addFlyer(state, {5, 5});
            if (id < 0 || !separated(state)) return fail("crowded spawn overlapped/rejected: " + error);
            ids.push_back(id);
        }
        if (runtimeCellPointForObject(*findObjectById(state, ids.front())) != RuntimeCellPoint{5, 5}) return fail("ground crowd obstructed flight spawn");
        state.selectedObjectIds = ids; issueRuntimeMoveCommand(state, {40, 25});
        if (!uniqueGoals(state, ids) || !runMove(state, ids)) return false;
    }
    // A dense formation must retain individual slots, even beyond shared-goal thresholds.
    std::cout << "Air navigation: direct routes, redirect and spawn passed; testing 64 flyers" << std::endl;
    {
        auto storage = makeProbe(); auto& state = *storage;
        std::vector<int> ids;
        for (int y = 4; y < 12; ++y) for (int x = 4; x < 12; ++x) ids.push_back(addFlyer(state, {x, y}));
        state.selectedObjectIds = ids; issueRuntimeMoveCommand(state, {45, 40});
        if (std::getenv("CONCORD_AIR_NAVIGATION_TRACE") != nullptr) {
            const auto* first = findObjectById(state, ids.front());
            std::cerr << "Initial dense path steps=" << findRuntimeAirPath(state, *first,
                runtimeCellPointForObject(*first), state.behaviors.at(ids.front()).moveTarget).size() << std::endl;
        }
        if (state.lastCommand.acceptedCount != 64 || !uniqueGoals(state, ids) || !runMove(state, ids, 3600)) return fail("64-flyer formation: " + error);
        for (const int id : ids) {
            if (runtimeCellDistance(runtimeCellPointForObject(*findObjectById(state, id)), {45, 40}) > 8.0) return fail("formation stopped far from the target");
        }
        // Repeat after arrival with a reversed selection order and the same target.
        std::reverse(ids.begin(), ids.end());
        state.selectedObjectIds = ids; issueRuntimeMoveCommand(state, {45, 40});
        if (!uniqueGoals(state, ids) || !runMove(state, ids, 3600)) return fail("repeat formation: " + error);
    }
    // Independent orders share destination reservations; idle occupants also count.
    {
        auto storage = makeProbe(); auto& state = *storage;
        const int parked = addFlyer(state, {34, 20});
        std::vector<int> ids;
        for (int index = 0; index < 12; ++index) ids.push_back(addFlyer(state, {4 + index, 4}));
        for (const int id : ids) {
            state.selectedObjectIds = {id}; issueRuntimeMoveCommand(state, {34, 20});
            if (state.lastCommand.acceptedCount != 1) return fail("independent move was rejected");
        }
        if (!uniqueGoals(state, ids) || !runMove(state, ids)) return false;
        if (runtimeCellPointForObject(*findObjectById(state, parked)) != RuntimeCellPoint{34, 20}) return fail("flight displaced parked unit");
    }
    // A stationary air barrier requires a real detour, not alternating greedy steps.
    {
        auto storage = makeProbe(); auto& state = *storage;
        const int id = addFlyer(state, {4, 8});
        for (int y = 4; y <= 12; ++y) addFlyer(state, {18, y});
        state.selectedObjectIds = {id}; issueRuntimeMoveCommand(state, {30, 8});
        if (!runMove(state, {id})) return fail("air barrier: " + error);
    }
    // Head-on and crossing diagonal segments remain separated while interpolating.
    for (const bool diagonal : {true, false}) {
        auto storage = makeProbe(); auto& state = *storage;
        const int first = addFlyer(state, {4, 4});
        const int second = addFlyer(state, diagonal ? RuntimeCellPoint{5, 4} : RuntimeCellPoint{30, 4});
        state.selectedObjectIds = {first}; issueRuntimeMoveCommand(state, diagonal ? RuntimeCellPoint{5, 5} : RuntimeCellPoint{30, 4});
        state.selectedObjectIds = {second}; issueRuntimeMoveCommand(state, diagonal ? RuntimeCellPoint{4, 5} : RuntimeCellPoint{4, 4});
        if (!runMove(state, {first, second})) return fail("crossing flight: " + error);
    }
    // Attack orders must use the same collision rules when approaching a common enemy.
    std::cout << "Air navigation: formations, independent orders, barrier and crossings passed" << std::endl;
    {
        auto storage = makeProbe(); auto& state = *storage;
        std::vector<int> ids;
        for (int index = 0; index < 12; ++index) ids.push_back(addFlyer(state, {4 + index % 4, 4 + index / 4}));
        auto* target = addRuntimeObjectInstanceForOwner(state, "Russians", RuntimeBuildKind::Unit, "MTNK", {40, 30});
        if (target == nullptr) return fail("attack target setup failed");
        target->health = 1000000;
        const int targetId = target->instanceId;
        state.selectedObjectIds = ids; issueRuntimeAttackCommand(state, *target);
        if (state.lastCommand.acceptedCount != 12) return fail("group attack rejected");
        bool fired = false, inRange = false;
        for (int tick = 0; tick < 2400; ++tick) {
            if (!step(state)) return false;
            fired = fired || state.actionSummary.attacksThisTick > 0;
            inRange = std::all_of(ids.begin(), ids.end(), [&](int id) {
                return runtimeCellDistanceToObjectFootprint(state,
                    runtimeCellPointForObject(*findObjectById(state, id)), *findObjectById(state, targetId)) <= 5.0;
            });
            if (inRange && fired) break;
        }
        if (!inRange || !fired) return fail("group chase did not reach firing positions");
    }
    // Ground units still occupy either whole vehicle cells or distinct infantry subcells.
    for (const bool infantry : {false, true}) {
        auto storage = makeProbe(); auto& state = *storage;
        std::vector<int> ids;
        for (int index = 0; index < 9; ++index) {
            auto* object = addRuntimeObjectInstance(state,
                infantry ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit,
                infantry ? "E1" : "MTNK", {22 + index % 3, 4 + index / 3});
            if (object == nullptr) return fail("ground movement setup failed");
            ids.push_back(object->instanceId);
        }
        for (const int id : ids) {
            state.selectedObjectIds = {id}; issueRuntimeMoveCommand(state, {40, 20});
        }
        bool finished = false;
        for (int tick = 0; tick < 1800; ++tick) {
            if (!step(state)) return false;
            std::set<std::pair<int, int>> occupied;
            for (const int id : ids) {
                const auto* object = findObjectById(state, id);
                if (!occupied.emplace(object->cell, infantry ? object->subCell : -1).second) return fail("ground units overlapped at a common destination");
            }
            finished = std::all_of(ids.begin(), ids.end(), [&](int id) {
                return state.behaviors.at(id).state == RuntimeBehaviorState::Complete;
            });
            if (finished) break;
        }
        if (!finished) return fail("ground units failed to settle near a common destination");
    }
    std::cout << "Air navigation: directOctants=8 longRoute=ok redirect=ok spawn=9 formation=64 repeat=ok "
              << "independentOrders=12 barrier=ok crossings=ok attack=12 ground=18 "
              << "saveSnapshot=ok logicalAndRenderedSeparation=ok\n";
    return true;
}
