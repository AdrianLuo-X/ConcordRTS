bool eraseRuntimeBehavior(RuntimeWindowState& state, int objectId);

bool runtimeBuildItemNeedsExitClearance(
    const RuntimeWindowState& state, const RuntimeBuildCatalogItem& item
) {
    WorldObjectInstance probe;
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.type = item.typeId;
    return !runtimeBuildItemUsesPadAircraft(state, item)
        && runtimeObjectMovementMode(state, probe) != PathMovementMode::Air;
}

std::optional<RuntimeCellPoint> runtimeProductionOutwardCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& anchor,
    const RuntimeCellPoint& cell
) {
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, anchor);
    const int dx = cell.x >= anchor.x + std::max(1, footprint.width)
        ? 1 : cell.x < anchor.x ? -1 : 0;
    const int dy = cell.y >= anchor.y + std::max(1, footprint.height)
        ? 1 : cell.y < anchor.y ? -1 : 0;
    if (dx == 0 && dy == 0) {
        return std::nullopt;
    }
    return RuntimeCellPoint{cell.x + dx, cell.y + dy};
}

// Use ordinary one-cell movement so occupancy, infantry reservations and
// animation all advance together. Never remove an occupant to permit spawn.
bool requestRuntimeFactoryExitClearance(
    RuntimeWindowState& state,
    const std::string& owner,
    const std::vector<RuntimeCellPoint>& corridor,
    int producedObjectId = -1,
    bool airOnly = false,
    int clearanceHostId = -1
) {
    const auto inCorridor = [&](const RuntimeCellPoint& cell) {
        // A one-cell slot is too small for an emerging Kirov. Keep a two-cell
        // apron around the air exit; ground factory traffic retains its lane.
        const int apron = airOnly ? 2 : 0;
        return std::any_of(corridor.begin(), corridor.end(), [&](const auto& point) {
            return std::abs(cell.x - point.x) <= apron && std::abs(cell.y - point.y) <= apron;
        });
    };
    const auto canYield = [&](const WorldObjectInstance& object) {
        if (object.instanceId == producedObjectId
            || !object.alive || object.health <= 0
            || !runtimeObjectCanMove(state, object)
            || !runtimeOwnersAreAllied(state, owner, object.owner)
            || (airOnly && runtimeObjectMovementMode(state, object) != PathMovementMode::Air)
            || object.factoryExitHostId >= 0
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || object.dockedToObjectId >= 0
            || object.garrisonHostObjectId >= 0) {
            return false;
        }
        if (const auto* type = findRuntimeUnitType(state.rules, object); type && type->missileSpawn)
            return false;
        for (const auto& relation : state.repairDepots.relations()) {
            if (relation.unitId == object.instanceId) {
                return false;
            }
        }
        for (const auto& lease : state.factoryDeliveries.leases()) {
            if (lease.producedObjectId == object.instanceId) {
                return false;
            }
        }
        return true;
    };
    std::vector<int> blockers;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.instanceId == producedObjectId) {
            continue;
        }
        const auto behavior = object.renderMoveActive
            ? state.behaviors.find(object.instanceId) : state.behaviors.end();
        const bool leaving = object.renderMoveActive
            && behavior != state.behaviors.end()
            && inCorridor(behavior->second.renderFrom);
        if ((inCorridor(runtimeCellPointForObject(object)) || leaving)
            && canYield(object)) {
            blockers.push_back(object.instanceId);
        }
    }
    if (blockers.empty()) {
        return false;
    }
    std::sort(blockers.begin(), blockers.end());

    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) {
        generatedTerrain = buildRuntimeTerrainLookup(state.world);
        terrain = &generatedTerrain;
    }
    const auto occupied = buildRuntimeOccupancyLookup(state);
    // Air movement deliberately ignores ground occupancy. Give yielding
    // flyers their own occupied cells, including the visible move origin.
    std::unordered_map<int, int> airOccupied;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || runtimeObjectMovementMode(state, object) != PathMovementMode::Air) continue;
        airOccupied.emplace(runtimeCellKey(object.x, object.y), object.instanceId);
        const auto moving = state.behaviors.find(object.instanceId);
        if (object.renderMoveActive && moving != state.behaviors.end())
            airOccupied.emplace(runtimeCellKey(moving->second.renderFrom.x, moving->second.renderFrom.y), object.instanceId);
    }
    std::unordered_set<int> reserved;
    for (const auto& cell : corridor) {
        reserved.insert(runtimeCellKey(cell.x, cell.y));
    }
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind == RuntimeBehaviorKind::Move
            && behavior.state != RuntimeBehaviorState::Complete) {
            reserved.insert(runtimeCellKey(
                behavior.moveTarget.x, behavior.moveTarget.y
            ));
        }
    }

    // A full infantry cell has three occupants, so treating its representative
    // as a passable destination cannot propagate a yield request through it.
    // Follow the connected, stationary infantry crowd instead. Only the outer
    // soldier with a genuinely free next step receives an ordinary Move.
    // The finite object set bounds this search without imposing a crowd radius.
    if (!airOnly) {
        std::map<int, std::vector<int>> waiting;
        for (const auto& object : state.world.getObjects()) {
            if (object.kind != WorldObjectKind::Infantry || !canYield(object)
                || runtimeObjectMovementMode(state, object) != PathMovementMode::Ground
                || object.renderMoveActive) continue;
            const auto move = state.behaviors.find(object.instanceId);
            if (move != state.behaviors.end() && move->second.kind == RuntimeBehaviorKind::Move
                && move->second.state != RuntimeBehaviorState::Complete
                && (move->second.pathRequestPending || !move->second.path.empty())) continue;
            waiting[runtimeCellKey(object.x, object.y)].push_back(object.instanceId);
        }
        for (auto& [key, ids] : waiting) std::sort(ids.begin(), ids.end());
        std::vector<int> frontier;
        std::unordered_set<int> seen;
        for (int id : blockers) {
            const auto* object = findObjectById(state, id);
            if (!object) continue;
            const auto cell = waiting.find(runtimeCellKey(object->x, object->y));
            if (cell != waiting.end()) for (int candidate : cell->second)
                if (seen.insert(candidate).second) frontier.push_back(candidate);
        }
        for (std::size_t next = 0; next < frontier.size(); ++next) {
            auto* object = findMutableObjectById(state, frontier[next]);
            const auto current = runtimeCellPointForObject(*object);
            auto neighbors = runtimeNeighborsOf(current);
            std::sort(neighbors.begin(), neighbors.end(), [&](const auto& a, const auto& b) {
                const int da = std::abs(a.x-current.x)+std::abs(a.y-current.y);
                const int db = std::abs(b.x-current.x)+std::abs(b.y-current.y);
                return da != db ? da < db : runtimeCellKey(a.x,a.y) < runtimeCellKey(b.x,b.y);
            });
            for (const auto& candidate : neighbors) {
                const int key = runtimeCellKey(candidate.x,candidate.y);
                if (reserved.contains(key) || occupied.contains(key)
                    || !runtimeCellTransitionIsPassable(state,*terrain,occupied,current,
                        candidate,*object,-1,nullptr,false)) continue;
                eraseRuntimeBehavior(state,object->instanceId);
                RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;
                move.state=RuntimeBehaviorState::Queued;move.moveTarget=candidate;
                move.path.push_back(candidate);state.behaviors[object->instanceId]=std::move(move);
                setRuntimeMissionIntent(state,*object,RuntimeMissionIntent::Moving,"factoryExitClearance");
                return true;
            }
            for (const auto& candidate : neighbors) {
                // Pressure crosses touching walkable cells, never a wall,
                // cliff, water boundary or diagonally sealed corner.
                if (std::abs(candidate.x-current.x)+std::abs(candidate.y-current.y)!=1) continue;
                const int key=runtimeCellKey(candidate.x,candidate.y);
                const auto adjacent=waiting.find(key);
                if (adjacent==waiting.end() || runtimeCellHasHardWholeCellBlocker(
                        state,candidate,object->instanceId,-1,{})) continue;
                const auto from=terrain->find(runtimeCellKey(current.x,current.y)),to=terrain->find(key);
                if (from==terrain->end() || to==terrain->end() || !from->second || !to->second) continue;
                const auto a=runtimeCachedTerrainInfoForCell(state,*from->second);
                const auto b=runtimeCachedTerrainInfoForCell(state,*to->second);
                const int height=std::abs(runtimeEffectiveTerrainLevel(*from->second)-runtimeEffectiveTerrainLevel(*to->second));
                if (!b.passable || !runtimeAuthoredLandTypeAllowsMover(state,b,*object)
                    || !runtimeMapQueryPassabilityAllows(state,candidate,*object,PathMovementMode::Ground)
                    || (a.water!=b.water && !(a.shore||b.shore||a.bridge||b.bridge))
                    || height>1 || (height>0 && !(a.ramp||b.ramp||a.bridge||b.bridge))) continue;
                for (int id:adjacent->second) if(seen.insert(id).second)frontier.push_back(id);
            }
        }
    }

    // A bounded chain lets the outside of a vehicle crowd move first. Stable object
    // IDs and cell ordering keep simultaneous factory requests deterministic.
    std::unordered_set<int> visited;
    const auto yield = [&](auto&& self, int objectId, int depth) -> bool {
        if (depth > 4 || visited.size() >= 32
            || !visited.insert(objectId).second) {
            return false;
        }
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr || !canYield(*object)) {
            return false;
        }
        const bool fastFactoryYield = !airOnly
            && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId);
        const auto previous = state.behaviors.find(objectId);
        if (object->renderMoveActive
            || (previous != state.behaviors.end()
                && previous->second.kind == RuntimeBehaviorKind::Move
                && !inCorridor(previous->second.moveTarget)
                && (previous->second.pathRequestPending
                    || !previous->second.path.empty()
                    // Long rally commands are accepted before the movement
                    // stage requests their path. A temporary empty route is
                    // still a live order and must survive factory pressure.
                    || (fastFactoryYield
                        && previous->second.state == RuntimeBehaviorState::Queued)))) {
            return true;
        }
        const RuntimeCellPoint current = runtimeCellPointForObject(*object);
        if (!inCorridor(current) && depth == 0) {
            return false;
        }
        if (runtimeObjectMovementMode(state, *object) == PathMovementMode::Air) {
            std::vector<RuntimeCellPoint> frontier{current};
            std::map<int, RuntimeCellPoint> parent;
            parent.emplace(runtimeCellKey(current.x,current.y),current);
            const auto hasParkingSpace = [&](const RuntimeCellPoint& candidate) {
                for (const auto& other : state.world.getObjects()) {
                    if (other.instanceId == objectId || !runtimeObjectOccupiesAirNavigation(other)) continue;
                    const auto close = [&](const RuntimeCellPoint& point) {
                        return std::abs(candidate.x - point.x) < 3 && std::abs(candidate.y - point.y) < 3;
                    };
                    if (close(runtimeCellPointForObject(other))) return false;
                    const auto moving = state.behaviors.find(other.instanceId);
                    if (moving != state.behaviors.end() && moving->second.kind == RuntimeBehaviorKind::Move
                        && moving->second.state != RuntimeBehaviorState::Complete
                        && close(moving->second.moveTarget)) return false;
                }
                return true;
            };
            for (std::size_t next=0; next<frontier.size() && next<256; ++next) {
                const auto from=frontier[next];
                for (const auto& candidate:runtimeNeighborsOf(from)) {
                    const int key=runtimeCellKey(candidate.x,candidate.y);
                    const auto occupant=airOccupied.find(key);
                    if (parent.contains(key) || (occupant!=airOccupied.end() && occupant->second!=objectId)
                        || !runtimeCellTransitionIsPassable(state,*terrain,occupied,from,candidate,*object,-1,nullptr,false)) continue;
                    parent.emplace(key,from);
                    if (inCorridor(candidate)) { frontier.push_back(candidate); continue; }
                    if (reserved.contains(key)) continue;
                    if (airOnly && !hasParkingSpace(candidate)) { frontier.push_back(candidate); continue; }
                    const auto ground=occupied.find(key);
                    const auto* below=ground==occupied.end()?nullptr:findObjectById(state,ground->second);
                    if (below && below->kind==WorldObjectKind::Structure) { frontier.push_back(candidate); continue; }
                    std::deque<RuntimeCellPoint> path;
                    for (auto step=candidate;step!=current;step=parent.at(runtimeCellKey(step.x,step.y))) path.push_front(step);
                    eraseRuntimeBehavior(state,objectId);
                    RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;move.state=RuntimeBehaviorState::Queued;
                    move.moveTarget=candidate;move.path=std::move(path);state.behaviors[objectId]=std::move(move);
                    setRuntimeMissionIntent(state,*object,RuntimeMissionIntent::Moving,"factoryAirExitClearance");
                    reserved.insert(key);return true;
                }
            }
            for (const auto& candidate:runtimeNeighborsOf(current)) {
                const auto occupant=airOccupied.find(runtimeCellKey(candidate.x,candidate.y));
                if (occupant!=airOccupied.end() && occupant->second!=objectId
                    && self(self,occupant->second,depth+1)) return true;
            }
            return false;
        }
        const auto preserveInterruptedMove = [&]() {
            const auto active = state.behaviors.find(objectId);
            if (!fastFactoryYield || active == state.behaviors.end()
                || active->second.kind != RuntimeBehaviorKind::Move
                || active->second.state == RuntimeBehaviorState::Complete
                || active->second.state == RuntimeBehaviorState::Idle
                || active->second.factoryExitClearanceHostId >= 0
                || inCorridor(active->second.moveTarget)
                || active->second.moveTarget == current) return true;
            const auto& interrupted = active->second;
            // Preserve every live ordinary destination, including a failed
            // route's turn cooldown. Contextual routes keep their own executor.
            if (interrupted.targetObjectId != -1 || interrupted.forceFire
                || interrupted.attackGround || interrupted.attackBridge
                || interrupted.pendingPathTowardRange
                || interrupted.pendingAllowedOccupiedObjectId >= 0
                || interrupted.pendingHasSharedGoal || interrupted.moveCooldownTicks != 0
                || state.playerCommands.following.contains(objectId)
                || state.playerCommands.travelling.contains(objectId)
                || state.playerCommands.waypoints.routes.contains(objectId)
                || state.pendingEngineerCaptures.contains(objectId)
                || state.pendingEngineerActions.contains(objectId)
                || state.pendingOccupyTargets.contains(objectId)
                || state.pendingTransportTargets.contains(objectId)
                || state.pendingTimedBombIntents.contains(objectId)
                || std::any_of(interrupted.pendingIgnoredObjectIds.begin(),
                    interrupted.pendingIgnoredObjectIds.end(),
                    [&](int id) { return id != objectId; })) return false;
            const std::string resumeOwner = runtimeCanonicalHumanHouseOwner(object->owner);
            const RuntimeCellPoint destination = interrupted.moveTarget;
            if (resumeOwner.empty() || resumeOwner.size() > 256U
                || destination.x < 0 || destination.y < 0
                || destination.x > runtimeMultiplayerGameplayMaximumCellCoordinate
                || destination.y > runtimeMultiplayerGameplayMaximumCellCoordinate) return false;
            RuntimeMultiplayerGameplayTargetOrderCommand resume;
            resume.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
            resume.objectIds = {objectId};
            resume.target = RuntimeMultiplayerGameplayCellTarget{{destination.x, destination.y}};
            const auto queued = state.playerCommands.queues.find(objectId);
            if (queued != state.playerCommands.queues.end()) {
                if (queued->second.owner != resumeOwner) return false;
                // Only the head can already represent the interrupted step.
                // A later repeat must remain behind the earlier queued orders.
                if (!queued->second.orders.empty() && queued->second.orders.front() == resume) return true;
                if (queued->second.orders.size() >= RuntimePlayerCommandState::maximumOrdersPerObject)
                    return false;
                queued->second.orders.push_front(std::move(resume));
                if (queued->second.factoryRallyOrderIndex >= 0) ++queued->second.factoryRallyOrderIndex;
            } else {
                RuntimePlayerQueuedOrders queue;
                queue.owner = resumeOwner;
                queue.orders.push_front(std::move(resume));
                state.playerCommands.queues.emplace(objectId, std::move(queue));
            }
            return true;
        };
        auto neighbors = runtimeNeighborsOf(current);
        const auto turnDistance = [&](const RuntimeCellPoint& candidate) {
            const int facing = runtimeFacingFromDelta(candidate.x - current.x, candidate.y - current.y);
            const int delta = std::abs(facing - object->facing) % 256;
            return std::min(delta, 256 - delta);
        };
        // Fast-build traffic keeps its native ROT. Prefer a short step in its
        // current direction, including propagated crowd pressure, rather than
        // making parked predecessors reverse before the next car can leave.
        std::sort(neighbors.begin(), neighbors.end(), [&](const auto& a, const auto& b) {
            const int da = std::abs(a.x - current.x) + std::abs(a.y - current.y);
            const int db = std::abs(b.x - current.x) + std::abs(b.y - current.y);
            if (da != db) return da < db;
            if (fastFactoryYield && object->facing >= 0) {
                const int ta = turnDistance(a), tb = turnDistance(b);
                if (ta != tb) return ta < tb;
            }
            return runtimeCellKey(a.x, a.y) < runtimeCellKey(b.x, b.y);
        });
        for (const RuntimeCellPoint& candidate : neighbors) {
            const int key = runtimeCellKey(candidate.x, candidate.y);
            if (reserved.contains(key) || occupied.contains(key)
                || !runtimeCellTransitionIsPassable(
                    state, *terrain, occupied, current, candidate,
                    *object, -1, nullptr, false
                )) {
                continue;
            }
            if (!preserveInterruptedMove()) return false;
            eraseRuntimeBehavior(state, objectId);
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = candidate;
            move.path.push_back(candidate);
            if (!airOnly && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId))
                move.factoryExitClearanceHostId = clearanceHostId;
            state.behaviors[objectId] = std::move(move);
            setRuntimeMissionIntent(
                state, *object, RuntimeMissionIntent::Moving, "factoryExitClearance"
            );
            reserved.insert(key);
            return true;
        }
        for (const RuntimeCellPoint& candidate : neighbors) {
            const int key = runtimeCellKey(candidate.x, candidate.y);
            const auto occupant = occupied.find(key);
            if (reserved.contains(key) || occupant == occupied.end()
                || !runtimeCellTransitionIsPassable(
                    state, *terrain, occupied, current, candidate,
                    *object, occupant->second, nullptr, false
                )) {
                continue;
            }
            if (self(self, occupant->second, depth + 1)) {
                return true;
            }
        }
        // A unit at the outer exit may have to reverse through the door
        // before it can turn aside. The corridor is forbidden as a parking
        // destination, but it is still valid ground to travel through.
        std::vector<RuntimeCellPoint> frontier{current};
        std::map<int, RuntimeCellPoint> parent;
        parent.emplace(runtimeCellKey(current.x, current.y), current);
        for (std::size_t next = 0; next < frontier.size() && next < 32; ++next) {
            const auto from = frontier[next];
            for (const auto& candidate : runtimeNeighborsOf(from)) {
                const int key = runtimeCellKey(candidate.x, candidate.y);
                if (parent.contains(key) || occupied.contains(key)
                    || !runtimeCellTransitionIsPassable(state, *terrain, occupied, from, candidate,
                        *object, -1, nullptr, false)) continue;
                parent.emplace(key, from);
                if (inCorridor(candidate)) { frontier.push_back(candidate); continue; }
                if (reserved.contains(key)) continue;
                std::deque<RuntimeCellPoint> path;
                for (auto step = candidate; step != current; step = parent.at(runtimeCellKey(step.x, step.y)))
                    path.push_front(step);
                if (!preserveInterruptedMove()) return false;
                eraseRuntimeBehavior(state, objectId);
                RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move;
                move.state = RuntimeBehaviorState::Queued; move.moveTarget = candidate;
                move.path = std::move(path);
                if (!airOnly && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId))
                    move.factoryExitClearanceHostId = clearanceHostId;
                state.behaviors[objectId] = std::move(move);
                setRuntimeMissionIntent(state, *object, RuntimeMissionIntent::Moving, "factoryExitClearanceReverse");
                reserved.insert(key);
                return true;
            }
        }
        return false;
    };
    for (int objectId : blockers) {
        (void)yield(yield, objectId, 0);
    }
    // Keep the completed order until both the physical and visual exit clear,
    // even if the crowd needs several ticks to make space.
    return true;
}

std::vector<RuntimeCellPoint> runtimeFactoryAirExitCorridor(const RuntimeWindowState& state,
    const WorldObjectInstance& factory) {
    const auto footprint=runtimeStructureFootprintForObject(state,factory);
    const int width=std::max(1,footprint.width),height=std::max(1,footprint.height);
    std::vector<RuntimeCellPoint> corridor;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        corridor.push_back({factory.x+x,factory.y+y});
    corridor.push_back({factory.x+width,factory.y+(height-1)/2});
    corridor.push_back({factory.x+width+1,factory.y+(height-1)/2});
    return corridor;
}

void requestRuntimeFactoryAirExitClearance(RuntimeWindowState& state,
    const WorldObjectInstance& produced,const WorldObjectInstance& factory) {
    if(!runtimeFactoryUsesAirExit(state,factory,produced))return;
    auto corridor=runtimeFactoryAirExitCorridor(state,factory);
    const auto move=state.behaviors.find(produced.instanceId);
    if(move!=state.behaviors.end() && move->second.kind==RuntimeBehaviorKind::Move)
        corridor.push_back(move->second.moveTarget);
    (void)requestRuntimeFactoryExitClearance(state,produced.owner,corridor,produced.instanceId,true);
}

bool requestRuntimeProductionDoorClearance(
    RuntimeWindowState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    const std::string& owner
) {
    WorldObjectInstance probe;
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.type = item.typeId;
    probe.owner = owner;
    probe.alive = true;
    const auto mode = runtimeObjectMovementMode(state, probe);
    if (runtimeBuildItemUsesPadAircraft(state, item)) {
        return false;
    }
    if (mode == PathMovementMode::Air) {
        return runtimeFactoryUsesAirExit(state,anchor,probe)
            && requestRuntimeFactoryExitClearance(state,owner,runtimeFactoryAirExitCorridor(state,anchor),-1,true);
    }
    const auto lane = runtimeProductionLaneForCatalogItem(state, item);
    if (lane && (*lane == RuntimeProductionLane::Vehicles
            || *lane == RuntimeProductionLane::Ships)
        && (state.factoryDeliveries.claimsDock(
                anchor.instanceId, *lane == RuntimeProductionLane::Ships ? 0 : -1
            )
            || state.repairDepots.claimsDock(anchor.instanceId, 0))) {
        return false;
    }
    const auto footprint = runtimeStructureFootprintForObject(state, anchor);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const auto* factory = findRuntimeBuildingTypeById(state.rules, anchor.type);
    const bool vehicleDoor = !runtimeBuildItemLooksNaval(state, item)
        && factory != nullptr && factory->weaponsFactory
        && toUpperAsciiLocal(factory->factory) == "UNITTYPE";
    const RuntimeCellPoint door = vehicleDoor
        ? RuntimeCellPoint{anchor.x + width, anchor.y + height / 2}
        : RuntimeCellPoint{anchor.x + width / 2, anchor.y + height};
    const auto outward = runtimeProductionOutwardCell(state, anchor, door);
    const auto* terrain = runtimeProductionTerrainCellAt(state, door);
    if (terrain == nullptr || !outward.has_value()) {
        return false;
    }
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
    if (!info.passable || !runtimeAuthoredLandTypeAllowsMover(state, info, probe)
        || (runtimeBuildItemLooksNaval(state, item)
            && !runtimeProductionCellIsOpenSurfaceWater(state, door))
        || (mode == PathMovementMode::Ground
            && info.water && !info.bridge && !info.shore)) {
        return false;
    }
    return requestRuntimeFactoryExitClearance(state, owner, {door, *outward},
        -1, false, vehicleDoor ? anchor.instanceId : -1);
}

bool requestRuntimeProductionDoorClearanceForOwner(
    RuntimeWindowState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback,
    bool tryAllFactories = false
) {
    const auto lane = runtimeProductionLaneForCatalogItem(state, item);
    for (const auto* anchor : findRuntimeFactoryAnchorCandidatesForOwner(
            state, item, owner, allowLegacyFallback)) {
        if (anchor == nullptr) continue;
        if (lane && (*lane == RuntimeProductionLane::Vehicles
                || *lane == RuntimeProductionLane::Ships)
            && (state.factoryDeliveries.claimsDock(
                    anchor->instanceId, *lane == RuntimeProductionLane::Ships ? 0 : -1
                ) || state.repairDepots.claimsDock(anchor->instanceId, 0))) {
            continue;
        }
        if (requestRuntimeProductionDoorClearance(state, item, *anchor, owner)) {
            return true;
        }
        if (!tryAllFactories) return false;
    }
    return false;
}
