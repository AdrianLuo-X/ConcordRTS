// Include after the runtime command and simulation implementations. The fixture
// exercises accepted orders and actual movement without original game assets.
bool runRuntimeGroupMoveConvergenceSmoke(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "Group move convergence: " + reason;
        return false;
    };
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString(
        "[VehicleTypes]\n0=GROUPTANK\n"
        "[InfantryTypes]\n0=GROUPFOOT\n1=GROUPFLY\n"
        "[GROUPTANK]\nStrength=200\nSpeed=8\nSpeedType=Track\nMovementZone=Normal\n"
        "[GROUPFOOT]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
        "[GROUPFLY]\nStrength=100\nSpeed=6\nJumpJet=yes\nBalloonHover=yes\n"
        "JumpjetHeight=500\nJumpjetSpeed=30\nJumpjetAccel=10\n"
        "JumpjetClimb=20\nJumpjetTurnRate=128\nJumpjetNoWobbles=yes\n")
        || !rules.loadFromIni(ini)) return fail("fixture rules did not load");

    const auto make = [&](int count, const char* type, int layout) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        state.rules = rules;
        state.rulesLoaded = true;
        state.config.fixedTickHz = 30;
        state.config.gameMode = "Battle";
        state.config.computerAiEnabled = state.config.multiplayerActive = false;
        state.config.crates = state.config.superweapons = state.config.shortGame = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.visionEnabled = false;
        state.economy.owner = "Player";
        state.economy.skirmishStartApplied = true;
        state.triggers.bootstrapComplete = true;
        RuntimeHumanHouseAuthority authority;
        authority.owner = "Player";
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true;
        state.humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            state.world.accessTerrainCells().push_back(cell);
        }
        const bool tank = std::string(type) == "GROUPTANK";
        const bool air = std::string(type) == "GROUPFLY";
        std::vector<int> selection;
        for (int index = 0; index < count; ++index) {
            RuntimeCellPoint cell{8 + index * 4, index % 2 == 0 ? 8 : 48};
            if (layout == 1) std::swap(cell.x, cell.y);
            if (layout == 2) cell = {12 + index * 16, 20};
            WorldObjectInstance object;
            object.instanceId = index + 1;
            object.mapId = "GroupMove" + std::to_string(index + 1);
            object.kind = tank ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
            object.type = type; object.owner = "Player";
            object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
            object.subCell = 2; object.health = tank ? 200 : 100;
            object.alive = object.poweredOperational = true;
            object.airborne = air; object.flightLevel = air ? 2 : 0;
            object.mission = "Guard";
            object.facing = runtimeFacingFromDelta(1, 0);
            object.harvestAutoSearchSuspended = true;
            selection.push_back(object.instanceId);
            state.world.accessObjects().push_back(std::move(object));
        }
        initializeRuntimeNavigation(state);
        for (auto& [cell, info] : state.terrainInfoByCell) {
            (void)cell;
            info = RuntimeTerrainInfo{};
            info.authoredLandType = info.movementLandType = RuntimeTerrainLandType::Clear;
        }
        applySelection(state, selection, false, false);
        return storage;
    };

    int commands = 0;
    const auto order = [&](RuntimeWindowState& state, RuntimeCellPoint click,
        int radius, bool requireClick, std::map<int, RuntimeCellPoint>& destinations,
        const std::string& label) {
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        destinations.clear();
        issueRuntimeMoveCommand(state, click, &destinations);
        ++commands;
        if (state.lastCommand.acceptedCount != static_cast<int>(state.world.getObjects().size())
            || state.lastCommand.rejectedCount != 0
            || destinations.size() != state.world.getObjects().size())
            return fail(label + " did not accept every selected unit");
        std::unordered_set<int> selected;
        std::map<int, std::pair<int, int>> allocated;
        for (const auto& object : state.world.getObjects()) selected.insert(object.instanceId);
        const auto occupied = buildRuntimeOccupancyLookup(state, -1, nullptr, &selected);
        bool usesClick = false;
        for (const auto& [id, target] : destinations) {
            const auto* object = findObjectById(state, id);
            const int distance = std::max(std::abs(target.x - click.x), std::abs(target.y - click.y));
            if (distance > radius)
                return fail(label + " retained a spread-out destination for unit " + std::to_string(id)
                    + ": distance=" + std::to_string(distance) + " limit=" + std::to_string(radius));
            if (!object || !runtimeCellIsPassable(state, state.terrainByCell, occupied, target, *object, -1))
                return fail(label + " assigned an illegal destination");
            auto& count = allocated[runtimeCellKey(target.x, target.y)];
            ++(runtimeInfantryUsesGroundSubcells(*object) ? count.first : count.second);
            if (count.first > 3 || count.second > 1 || (count.first && count.second))
                return fail(label + " overbooked destination capacity");
            const auto behavior = state.behaviors.find(id);
            if (behavior == state.behaviors.end() || behavior->second.kind != RuntimeBehaviorKind::Move
                || behavior->second.moveTarget != target)
                return fail(label + " accepted destination differs from queued movement");
            usesClick = usesClick || target == click;
        }
        if (requireClick && !usesClick) return fail(label + " left the clicked cell outside the formation");
        return true;
    };

    for (const char* type : {"GROUPTANK", "GROUPFOOT", "GROUPFLY"}) {
        for (int count : {2, 5, 11, 12}) {
            if (std::string(type) == "GROUPFLY" && count != 2 && count != 12) continue;
            const std::string label = std::string(type) + "/" + std::to_string(count);
            const int radius = count > 9 ? 2 : 1;
            auto first = make(count, type, 0);
            auto rotated = make(count, type, 1);
            std::map<int, RuntimeCellPoint> firstTargets, rotatedTargets, repeatedTargets;
            if (!order(*first, {32, 32}, radius, true, firstTargets, label + " initial")
                || !order(*rotated, {32, 32}, radius, true, rotatedTargets, label + " rotated")) return false;
            if (firstTargets != rotatedTargets)
                return fail(label + " destinations depend on the previous formation shape");
            if (!order(*first, {40, 24}, radius, true, repeatedTargets, label + " consecutive")
                || !order(*first, {32, 32}, radius, true, repeatedTargets, label + " repeated")) return false;
            if (firstTargets != repeatedTargets)
                return fail(label + " repeated click changed the accepted formation");
            int sumX = 0, sumY = 0;
            for (const auto& object : first->world.getObjects()) { sumX += object.x; sumY += object.y; }
            const RuntimeCellPoint center{
                static_cast<int>(std::lround(double(sumX) / count)),
                static_cast<int>(std::lround(double(sumY) / count))};
            if (!order(*first, center, radius, true, repeatedTargets, label + " original center")) return false;
        }
    }

    {
        auto single = make(1, "GROUPTANK", 0);
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(*single, {32, 32}, 0, true, destinations, "single unit")) return false;
    }
    {
        auto blocked = make(5, "GROUPTANK", 0);
        // A cliff under the click and one adjacent cell must still produce a
        // compact legal formation through the existing target fallback.
        for (const RuntimeCellPoint cell : {RuntimeCellPoint{32, 32}, RuntimeCellPoint{33, 32}}) {
            auto& info = blocked->terrainInfoByCell.at(runtimeCellKey(cell.x, cell.y));
            info.passable = false; info.cliff = true;
        }
        blocked->pathTerrainCache.valid = blocked->pathTerrainCache.dynamicValid = false;
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(*blocked, {32, 32}, 4, false, destinations, "obstructed click")) return false;
    }
    int arrivalTicks = 0;
    {
        auto storage = make(2, "GROUPTANK", 2);
        auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        std::map<int, RuntimeCellPoint> destinations;
        // The click is exactly the midpoint of units sixteen cells apart.
        // Translation-only orders leave both units at their starting cells.
        if (!order(state, {20, 20}, 1, true, destinations, "midpoint arrival")) return false;
        const auto arrived = [&]() {
            for (const auto& [id, target] : destinations) {
                const auto* object = findObjectById(state, id);
                const auto behavior = state.behaviors.find(id);
                if (!object || !object->alive || runtimeCellPointForObject(*object) != target
                    || object->renderMoveActive || (behavior != state.behaviors.end()
                        && behavior->second.kind == RuntimeBehaviorKind::Move)) return false;
            }
            return true;
        };
        while (arrivalTicks < 1200 && !arrived()) {
            ++state.tick; ++arrivalTicks;
            advanceRuntimeSimulation(state, false);
        }
        if (!arrived() || arrivalTicks == 0) return fail("two ground units did not converge in simulation");
        const auto* left = findObjectById(state, 1);
        const auto* right = findObjectById(state, 2);
        if (!left || !right || std::max(std::abs(left->x - right->x), std::abs(left->y - right->y)) > 2)
            return fail("arrived units retained their initial separation");
    }
    const auto addBlocker = [&](RuntimeWindowState& state, RuntimeCellPoint cell) {
        WorldObjectInstance blocker;
        blocker.instanceId = 1000 + static_cast<int>(state.world.getObjects().size());
        blocker.mapId = "GroupBlocker" + std::to_string(blocker.instanceId);
        blocker.kind = WorldObjectKind::Unit; blocker.type = "GROUPTANK";
        blocker.owner = "Player"; blocker.mission = "Guard";
        blocker.x = cell.x; blocker.y = cell.y; blocker.cell = runtimeCellKey(cell.x, cell.y);
        blocker.health = 200; blocker.alive = blocker.poweredOperational = true;
        blocker.harvestAutoSearchSuspended = true;
        const int id = blocker.instanceId;
        state.world.accessObjects().push_back(std::move(blocker));
        invalidateRuntimeTickOccupancyCache(state);
        return id;
    };
    const auto makeApproach = [&](int count, const char* type) {
        auto storage = make(count, type, 0);
        for (int id = 1; id <= count; ++id)
            setRuntimeObjectCell(*storage, *findMutableObjectById(*storage, id),
                {26 - 2 * ((id - 1) / 4), 29 + 2 * ((id - 1) % 4)}, false);
        invalidateRuntimeTickOccupancyCache(*storage);
        return storage;
    };
    const auto stopped = [](const RuntimeWindowState& state, int count) {
        for (int id = 1; id <= count; ++id) {
            const auto* object = findObjectById(state, id);
            const auto behavior = state.behaviors.find(id);
            if (!object || !object->alive || object->renderMoveActive
                || (behavior != state.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Move))
                return false;
        }
        return true;
    };
    const auto safeStep = [&](RuntimeWindowState& state, int count, const std::string& label) {
        std::map<int, RuntimeCellPoint> previous;
        for (int id = 1; id <= count; ++id) previous[id] = runtimeCellPointForObject(*findObjectById(state, id));
        ++state.tick;
        advanceRuntimeSimulation(state, false);
        std::set<std::pair<int, int>> infantrySlots;
        std::unordered_set<int> tankCells;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive) continue;
            const int cell = runtimeCellKey(object.x, object.y);
            if (object.kind == WorldObjectKind::Infantry) {
                if (!infantrySlots.insert({cell, object.subCell}).second)
                    return fail(label + " overlapped infantry subcells");
            } else if (!tankCells.insert(cell).second) return fail(label + " overlapped tanks");
            if (object.instanceId <= count) {
                const auto before = previous.at(object.instanceId);
                if (std::max(std::abs(object.x - before.x), std::abs(object.y - before.y)) > 1)
                    return fail(label + " teleported a mover");
            }
        }
        for (const auto& [slot, subcell] : infantrySlots) {
            (void)subcell;
            if (tankCells.contains(slot)) return fail(label + " moved infantry inside a blocking tank");
        }
        const auto& objects=state.world.getObjects();
        for (std::size_t first=0;first<objects.size();++first) {
            const auto& a=objects[first];
            if (!a.alive || a.airborne || (a.kind!=WorldObjectKind::Infantry && a.kind!=WorldObjectKind::Unit)) continue;
            const auto aSlot=worldObjectSubCellOffset(a);
            const bool aLayer=a.hasRenderPosition?a.renderOnBridge:a.onBridge;
            for (std::size_t second=first+1;second<objects.size();++second) {
                const auto& b=objects[second];
                if (!b.alive || b.airborne || (b.kind!=WorldObjectKind::Infantry && b.kind!=WorldObjectKind::Unit)
                    || aLayer!=(b.hasRenderPosition?b.renderOnBridge:b.onBridge)) continue;
                const auto bSlot=worldObjectSubCellOffset(b);
                const bool aFoot=a.kind==WorldObjectKind::Infantry,bFoot=b.kind==WorldObjectKind::Infantry;
                const double minimum=aFoot&&bFoot?0.5:(!aFoot&&!bFoot?1.0:0.75);
                const double distance=std::hypot(runtimeObjectRenderX(a)+aSlot.x-runtimeObjectRenderX(b)-bSlot.x,
                    runtimeObjectRenderY(a)+aSlot.y-runtimeObjectRenderY(b)-bSlot.y);
                if (distance+1e-9<minimum) return fail(label+" overlapped bodies #"
                    +std::to_string(a.instanceId)+"/#"+std::to_string(b.instanceId)
                    +" distance="+std::to_string(distance)+" minimum="+std::to_string(minimum));
            }
        }
        return true;
    };
    int congestionCases = 0;
    const auto settle = [&](RuntimeWindowState& state, int count, RuntimeCellPoint click,
        const std::string& label, std::map<int, RuntimeCellPoint>* lastQueuedTargets = nullptr) {
        for (int tick = 0; tick < 1200 && !stopped(state, count); ++tick) {
            if (lastQueuedTargets) for (int id = 1; id <= count; ++id) {
                const auto found = state.behaviors.find(id);
                if (found != state.behaviors.end()) (*lastQueuedTargets)[id] = found->second.moveTarget;
            }
            if (!safeStep(state, count, label)) return false;
        }
        if (!stopped(state, count)) {
            std::ostringstream details;
            for (int id = 1; id <= count; ++id) {
                const auto* object = findObjectById(state, id);
                const auto behavior = state.behaviors.find(id);
                details << " #" << id << "@" << object->x << ',' << object->y;
                if (behavior != state.behaviors.end()) {
                    const auto& move=behavior->second;
                    details << " target=" << move.moveTarget.x << ',' << move.moveTarget.y
                        << " replans=" << move.pathReplans << " state=" << static_cast<int>(move.state)
                        << " shared=" << move.pendingHasSharedGoal << " center=" << move.pendingSharedGoal.x
                        << ',' << move.pendingSharedGoal.y << " ignored=" << move.pendingIgnoredObjectIds.size()
                        << " blocked=" << move.blockedTicks << " queued=" << move.pathRequestPending
                        << " cooldown=" << move.moveCooldownTicks << " render=" << object->renderMoveActive
                        << " ticks=" << move.renderMoveTicksRemaining << '/' << move.renderMoveTicksTotal
                        << " path=" << move.path.size();
                    if (!move.path.empty()) details << " front=" << move.path.front().x << ',' << move.path.front().y
                        << " back=" << move.path.back().x << ',' << move.path.back().y
                        << " bodyBlocked=" << runtimeInfantryBodySegmentAdmissionBlocked(state,*object,
                            runtimeCellPointForObject(*object),move.path.front());
                    const auto search=findRuntimeCloserSharedMoveSlot(state,*object,move);
                    details << " search=" << search.applicable << '/' << search.exhausted << '/' << search.settledBarrier
                        << '/' << search.transientBarrier << '/' << search.foreignBarrier << '/' << search.inwardRoute
                        << " bodyRoute=";
                    for (const auto& cell:search.path) details << cell.x << ',' << cell.y << ';';
                }
            }
            return fail(label + " kept wandering after 1200 ticks:" + details.str());
        }
        std::map<int, RuntimeCellPoint> settled;
        for (int id = 1; id <= count; ++id) {
            const auto cell = runtimeCellPointForObject(*findObjectById(state, id));
            if (runtimeCellDistance(cell, click) > 7.25) return fail(label + " stopped too far from the click");
            settled[id] = cell;
        }
        for (int tick = 0; tick < 180; ++tick) {
            if (!safeStep(state, count, label)) return false;
            if (!stopped(state, count) || state.actionSummary.repathsThisTick != 0)
                return fail(label + " restarted movement or pathfinding after settling");
            for (const auto& [id, cell] : settled)
                if (runtimeCellPointForObject(*findObjectById(state, id)) != cell)
                    return fail(label + " drifted after settling");
        }
        ++congestionCases;
        return true;
    };

    // The goal is clear when the real command is accepted. Only afterwards do
    // unselected idle friends occupy the slots or surround still-empty slots.
    for (const char* type : {"GROUPTANK", "GROUPFOOT"}) for (int count : {2, 8}) {
        for (bool ring : {false, true}) {
            auto storage = makeApproach(count, type); auto& state = *storage;
            RuntimeSnapshotActiveStateScope snapshotScope(state);
            const std::string label = std::string(type) + "/" + std::to_string(count)
                + (ring ? " surrounded slot" : " occupied slot");
            std::map<int, RuntimeCellPoint> destinations;
            if (!order(state, {32, 32}, std::string(type) == "GROUPTANK" ? 2 : 1,
                true, destinations, label)) return false;
            std::map<int, RuntimeCellPoint> blocked;
            std::unordered_set<int> goalCells;
            for (const auto& [id, cell] : destinations) { (void)id; goalCells.insert(runtimeCellKey(cell.x, cell.y)); }
            for (const auto& [id, cell] : destinations) {
                (void)id;
                if (!ring) blocked.emplace(runtimeCellKey(cell.x, cell.y), cell);
                else for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const RuntimeCellPoint neighbor{cell.x + dx, cell.y + dy};
                    const int key = runtimeCellKey(neighbor.x, neighbor.y);
                    if (!goalCells.contains(key)) blocked.emplace(key, neighbor);
                }
            }
            for (const auto& [cell, point] : blocked) { (void)cell; addBlocker(state, point); }
            if (!settle(state, count, {32, 32}, label)) return false;
        }
    }
    // Body obstruction evidence keeps foreign blockers distinct from settled
    // allies even when the destination still has a logically available slot.
    {
        auto storage=makeApproach(1,"GROUPFOOT"); auto& state=*storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        std::map<int,RuntimeCellPoint> destinations;
        if (!order(state,{32,32},0,true,destinations,"foreign body blocked exact slot")) return false;
        for (int slot:{2,4}) {
            const int id=addBlocker(state,{32,32}); auto* resident=findMutableObjectById(state,id);
            resident->kind=WorldObjectKind::Infantry; resident->type="GROUPFOOT"; resident->subCell=slot;
        }
        const int foreignId=addBlocker(state,{31,33});
        auto* foreign=findMutableObjectById(state,foreignId);
        foreign->kind=WorldObjectKind::Infantry; foreign->type="GROUPFOOT"; foreign->subCell=2;
        foreign->owner="Enemy"; foreign->hasRenderPosition=true; foreign->renderMoveActive=false;
        foreign->renderX=31.5; foreign->renderY=32.9;
        foreign->renderLevel=foreign->level; foreign->renderOnBridge=foreign->onBridge;
        auto* object=findMutableObjectById(state,1); setRuntimeObjectCell(state,*object,{31,32},true);
        auto& behavior=state.behaviors.at(1);
        behavior.path={{32,32}}; behavior.pathRequestPending=false; behavior.blockedTicks=1;
        invalidateRuntimeTickOccupancyCache(state); (void)runtimeTickOccupancyLookup(state);
        if (runtimeAvailableInfantrySubcells(state,*object,{32,32})!=std::vector<int>{3})
            return fail("foreign body fixture lost its available exact subcell");
        const auto search=findRuntimeCloserSharedMoveSlot(state,*object,behavior);
        if (!search.applicable || !search.exhausted || !search.path.empty()
            || !search.foreignBarrier || runtimeMoveCanFinishAtCrowdedDestination(state,*object,behavior)
            || behavior.moveTarget!=RuntimeCellPoint{32,32})
            return fail("foreign body blocked slot incorrectly accepted nearby completion");
        findMutableObjectById(state,foreignId)->owner="Player";
        invalidateRuntimeTickOccupancyCache(state);
        if (!runtimeMoveCanFinishAtCrowdedDestination(state,*object,behavior))
            return fail("same sealed body geometry did not accept settled allied completion");
    }
    // Destination guard and attack-move keep their own travel intention. Its
    // retry must not restart a successfully settled ordinary Move behavior.
    for (const auto kind : {RuntimeMultiplayerGameplayTargetOrderKind::AttackMove,
            RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination}) {
        auto storage = makeApproach(2, "GROUPTANK"); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        if (!dispatchRuntimeTargetOrderFromUi(state, kind, {32, 32})
            || state.playerCommands.travelling.size() != 2) return fail("congested travel order rejected");
        std::vector<RuntimeCellPoint> slots;
        for (const auto& [id, travel] : state.playerCommands.travelling) {
            (void)id; slots.push_back({travel.destination.x, travel.destination.y});
        }
        for (const auto cell : slots) addBlocker(state, cell);
        if (!settle(state, 2, {32, 32}, kind == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
            ? "congested attack move" : "congested guard destination")) return false;
    }
    for (const char* type : {"GROUPTANK", "GROUPFOOT"}) {
        // Isolate the en-route obstruction from formation-slot traffic: a
        // second mover can legitimately occupy or reroute through that slot.
        auto storage = makeApproach(1, type); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        const std::string label = std::string(type) + " temporary blocker far from goal";
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(state, {48, 32}, 0, true, destinations, label)) return false;
        const auto initial = runtimeCellPointForObject(*findObjectById(state, 1));
        std::map<int, RuntimeCellPoint> lastQueuedTargets = destinations;
        std::vector<int> blockers;
        for (int y = 0; y < 64; ++y) blockers.push_back(addBlocker(state, {28, y}));
        for (int tick = 0; tick < 90; ++tick) {
            if (!safeStep(state, 1, label)) return false;
            const auto found = state.behaviors.find(1);
            if (found == state.behaviors.end() || found->second.kind != RuntimeBehaviorKind::Move)
                return fail(label + " incorrectly completed before the obstruction cleared");
            lastQueuedTargets[1] = found->second.moveTarget;
        }
        for (int id : blockers) findMutableObjectById(state, id)->alive = false;
        invalidateRuntimeTickOccupancyCache(state);
        if (!settle(state, 1, {48, 32}, label, &lastQueuedTargets)) return false;
        for (const auto& [id, target] : destinations) {
            const auto current = runtimeCellPointForObject(*findObjectById(state, id));
            if (current != target) {
                const auto finalTarget = lastQueuedTargets.at(id);
                return fail(label + " did not reach its available exact destination: initial="
                    + std::to_string(initial.x) + "," + std::to_string(initial.y)
                    + " assigned=" + std::to_string(target.x) + "," + std::to_string(target.y)
                    + " current=" + std::to_string(current.x) + "," + std::to_string(current.y)
                    + " lastQueued=" + std::to_string(finalTarget.x) + "," + std::to_string(finalTarget.y));
            }
        }
    }
    // Infantry share three stable subcells. Two idle occupants leave a legal
    // exact destination; only a full cell justifies stopping beside the crowd.
    for (int occupiedSlots : {2, 3}) {
        auto storage = makeApproach(1, "GROUPFOOT"); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(state, {32, 32}, 0, true, destinations, "infantry subcell setup")) return false;
        for (int index = 0; index < occupiedSlots; ++index) {
            const int id = addBlocker(state, {32, 32});
            auto* occupant = findMutableObjectById(state, id);
            occupant->kind = WorldObjectKind::Infantry; occupant->type = "GROUPFOOT";
            occupant->subCell = RuntimeSubcellSystemState::activeSubCells()[index];
        }
        auto* object = findMutableObjectById(state, 1);
        setRuntimeObjectCell(state, *object, {31, 32}, true);
        auto& behavior = state.behaviors.at(1);
        behavior.blockedTicks = 1; behavior.path = {{32, 32}}; behavior.pathRequestPending = false;
        invalidateRuntimeTickOccupancyCache(state);
        if (runtimeAvailableInfantrySubcells(state, *object, {32, 32}).size()
            != static_cast<std::size_t>(3 - occupiedSlots)) return fail("infantry subcell fixture occupancy mismatch");
        if (runtimeMoveCanFinishAtCrowdedDestination(state, *object, behavior) != (occupiedSlots == 3))
            return fail("crowded arrival ignored available infantry subcells: occupants=" + std::to_string(occupiedSlots));
    }
    // A full infantry cell cannot be classified from its lowest-id occupant:
    // another resident may be leaving, or its last slot may only be reserved.
    // Obtain shared-goal state from a real group command, then stop seven
    // members while the remaining mover approaches this controlled inner ring.
    for (int mode = 0; mode < 3; ++mode) {
        auto storage = makeApproach(8, "GROUPFOOT"); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        const std::string label = mode == 0 ? "full infantry static control"
            : mode == 1 ? "full infantry nonrepresentative mover" : "full infantry reserved slot";
        const RuntimeCellPoint entry{31, 32}, incomingSource{30, 31};
        setRuntimeObjectCell(state, *findMutableObjectById(state, 1), {30, 32}, true);
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(state, {32, 32}, 1, true, destinations, label)) return false;
        state.selectedObjectIds = {2,3,4,5,6,7,8}; issueRuntimeStopCommand(state);
        for (int y = 31; y <= 33; ++y) for (int x = 31; x <= 33; ++x)
            if (RuntimeCellPoint{x,y} != entry) addBlocker(state, {x,y});
        std::vector<int> residents;
        for (int index = 0; index < (mode == 2 ? 2 : 3); ++index) {
            const int id = addBlocker(state, entry);
            auto* object = findMutableObjectById(state, id);
            object->kind = WorldObjectKind::Infantry; object->type = "GROUPFOOT";
            object->subCell = RuntimeSubcellSystemState::activeSubCells()[index];
            residents.push_back(id);
        }
        int transientId = -1;
        if (mode == 1) {
            transientId = residents.back();
            state.selectedObjectIds = {transientId}; issueRuntimeMoveCommand(state, {29, 32});
            if (state.lastCommand.acceptedCount != 1) return fail(label + " departure rejected");
        } else if (mode == 2) {
            // The pending entrant stops before the first simulation tick. Its
            // old source must leave the primary mover's physical approach clear.
            transientId = addBlocker(state, incomingSource);
            auto* incoming = findMutableObjectById(state, transientId);
            incoming->kind = WorldObjectKind::Infantry; incoming->type = "GROUPFOOT";
            incoming->subCell = RuntimeSubcellSystemState::activeSubCells()[1];
            state.selectedObjectIds = {transientId}; issueRuntimeMoveCommand(state, entry);
            incoming = findMutableObjectById(state, transientId);
            if (state.lastCommand.acceptedCount != 1
                || !reserveRuntimeInfantryMoveDestination(state, *incoming, entry))
                return fail(label + " actual next-step reservation rejected");
        }
        invalidateRuntimeTickOccupancyCache(state);
        auto* mover = findMutableObjectById(state, 1);
        const auto& occupied = runtimeTickOccupancyLookup(state);
        const auto representative = occupied.find(runtimeCellKey(entry.x, entry.y));
        if (representative == occupied.end() || representative->second != residents.front()
            || state.behaviors.contains(residents.front())
            || !runtimeAvailableInfantrySubcells(state, *mover, entry).empty())
            return fail(label + " did not create a full cell with a stationary representative");
        const auto& behavior = state.behaviors.at(1);
        const auto search = findRuntimeCloserSharedMoveSlot(state, *mover, behavior);
        if (!search.applicable || !search.exhausted || search.inwardRoute || !search.path.empty()
            || !search.settledBarrier || search.transientBarrier != (mode != 0))
            return fail(label + " classified the whole cell from only its stationary representative");
        if (runtimeMoveCanFinishAtCrowdedDestination(state, *mover, behavior) != (mode == 0))
            return fail(label + " confused temporary occupancy with permanent congestion");
        if (mode == 2) {
            // A real stop cancels the incoming reservation; the slot is then
            // available without teleporting or replacing the primary path.
            state.selectedObjectIds = {transientId}; issueRuntimeStopCommand(state);
            if (state.subcells.reservations().contains(transientId))
                return fail(label + " stop did not release its reservation");
            const auto* incoming=findObjectById(state,transientId);
            mover=findMutableObjectById(state,1);
            if (!incoming || runtimeCellPointForObject(*incoming)!=incomingSource
                || incoming->subCell!=4 || incoming->renderMoveActive)
                return fail(label+" stopped entrant left its untouched safe source");
            if (runtimeAvailableInfantrySubcells(state,*mover,entry)!=std::vector<int>{3}
                || runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(state,*mover,
                    runtimeCellPointForObject(*mover),entry,3))
                return fail(label+" released third slot lacks a safe actual body approach");
            const auto sourceSlot=worldObjectSubCellOffset(*mover);
            WorldObjectInstance target=*mover; target.x=entry.x; target.y=entry.y; target.subCell=3;
            const auto targetSlot=worldObjectSubCellOffset(target);
            const double fromX=runtimeObjectRenderX(*mover)+sourceSlot.x;
            const double fromY=runtimeObjectRenderY(*mover)+sourceSlot.y;
            const double toX=entry.x+targetSlot.x,toY=entry.y+targetSlot.y;
            const double dx=toX-fromX,dy=toY-fromY,lengthSquared=dx*dx+dy*dy;
            for (const auto& other:state.world.getObjects()) {
                if (!other.alive || other.airborne || other.instanceId==mover->instanceId
                    || (other.kind!=WorldObjectKind::Infantry && other.kind!=WorldObjectKind::Unit)) continue;
                const auto slot=worldObjectSubCellOffset(other);
                const double bodyX=runtimeObjectRenderX(other)+slot.x,bodyY=runtimeObjectRenderY(other)+slot.y;
                const double fraction=lengthSquared>0.0
                    ? std::clamp(((bodyX-fromX)*dx+(bodyY-fromY)*dy)/lengthSquared,0.0,1.0) : 0.0;
                const double minimum=other.kind==WorldObjectKind::Infantry?0.5:0.75;
                if (std::hypot(bodyX-fromX-fraction*dx,bodyY-fromY-fraction*dy)+1e-9<minimum)
                    return fail(label+" fixture obstructed the released slot's complete body segment #"
                        +std::to_string(other.instanceId));
            }
        }
        if (!settle(state, 1, {32, 32}, label)) return false;
        if (mode != 0 && runtimeCellPointForObject(*findObjectById(state, 1)) != entry)
            return fail(label + " did not enter the inner cell after it became available");
    }
    // Being close in map coordinates is not arrival across a cliff. Both an
    // occupied high-ground goal and a high-ground friend in front retain the
    // already queued route sideways to the ramp.
    for (bool blockerAtGoal : {true, false}) {
        auto storage = makeApproach(1, "GROUPTANK"); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(state, {32, 32}, 0, true, destinations, "cliff arrival setup")) return false;
        for (auto& cell : state.world.accessTerrainCells()) {
            if (cell.x >= 31 && cell.y >= 31) cell.level = 2;
            if (cell.x == 32 && cell.y == 30) cell.level = 1;
        }
        state.terrainInfoByCell.at(runtimeCellKey(32, 30)).ramp = true;
        state.terrainInfoByCell.at(runtimeCellKey(32, 31)).ramp = true;
        state.pathTerrainCache.valid = false;
        const RuntimeCellPoint blockerCell = blockerAtGoal ? RuntimeCellPoint{32, 32} : RuntimeCellPoint{31, 32};
        const int blockerId = addBlocker(state, blockerCell);
        auto* blocker = findMutableObjectById(state, blockerId);
        setRuntimeObjectCell(state, *blocker, blockerCell, true);
        auto* object = findMutableObjectById(state, 1);
        setRuntimeObjectCell(state, *object, {30, 32}, true);
        auto& behavior = state.behaviors.at(1);
        behavior.blockedTicks = 1; behavior.pathRequestPending = false;
        behavior.path = {{30, 31}, {30, 30}, {31, 30}, {32, 30}, {32, 31}, {32, 32}};
        invalidateRuntimeTickOccupancyCache(state);
        if (runtimeMoveCanFinishAtCrowdedDestination(state, *object, behavior))
            return fail("cliff proximity incorrectly accepted crowded arrival");
        advanceRuntimeMoveBehavior(state, *object, behavior);
        if (behavior.state == RuntimeBehaviorState::Complete || behavior.moveTarget != RuntimeCellPoint{32, 32}
            || behavior.path.empty()) return fail("cliff crowd discarded the queued ramp route");
    }
    // Exact entry/egress tasks use the same mover but must not accept proximity.
    for (int mode = 0; mode < 4; ++mode) {
        auto storage = makeApproach(8, "GROUPTANK"); auto& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        std::map<int, RuntimeCellPoint> destinations;
        if (!order(state, {32, 32}, 2, true, destinations, "exact task setup")) return false;
        const auto target = destinations.at(1);
        addBlocker(state, target);
        auto* object = findMutableObjectById(state, 1);
        setRuntimeObjectCell(state, *object, {target.x - 1, target.y}, false);
        auto& behavior = state.behaviors.at(1);
        behavior.blockedTicks = 1; behavior.path = {target}; behavior.pathRequestPending = false;
        if (mode == 0) state.pendingEngineerCaptures[1] = 999;
        if (mode == 1) state.pendingOccupyTargets[1] = 999;
        if (mode == 2) state.pendingTransportTargets[1] = 999;
        if (mode == 3) {
            RuntimeFactoryDeliveryAcquireRequest request;
            request.lease.canonicalOwner = "player"; request.lease.hostId = 999;
            request.lease.producedObjectId = 1; request.lease.orderSerial = 1;
            request.lease.rallyCell = {target.x, target.y, false};
            if (!state.factoryDeliveries.acquire(request).acquired) return fail("factory egress fixture lease rejected");
        }
        invalidateRuntimeTickOccupancyCache(state);
        advanceRuntimeMoveBehavior(state, *object, behavior);
        if (behavior.state == RuntimeBehaviorState::Complete)
            return fail("exact entry/egress task accepted a nearby congested cell: mode=" + std::to_string(mode));
    }
    std::cout << "Group move congestion smoke passed: cases=" << congestionCases << " stableTicks=180\n";
    std::cout << "Group move convergence smoke passed: commands=" << commands
        << " groundCounts=2,5,11,12 airCounts=2,12 arrivalTicks=" << arrivalTicks << '\n';
    error.clear();
    return true;
}
