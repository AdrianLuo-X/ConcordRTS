bool runtimeMoveAllowsCrowdBodyRoute(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    const bool transportApproach = runtimeUnitTransportApproachAllowsBodyDetour(state, object, behavior);
    if (behavior.kind != RuntimeBehaviorKind::Move || behavior.autoAcquired
        || behavior.targetObjectId >= 0
        || (behavior.pendingAllowedOccupiedObjectId >= 0 && !transportApproach)
        || object.airborne) return false;

    // A legal Fortress contact owns this occupied endpoint. The ordinary
    // crowd search uses non-crushing cells and must not exchange it for air.
    if (runtimeOmniCrushOwnsOccupiedEndpoint(state, object, behavior.moveTarget))
        return false;

    // These activities own a precise entrance, service cell or resource target.
    const int id = object.instanceId;
    if (runtimeFactoryDeliveryOwnsEgress(state, id)
        || runtimeCloneDeliveryOwnsDoorway(state, id)
        || state.pendingEngineerCaptures.contains(id) || state.pendingEngineerActions.contains(id)
        || state.pendingOccupyTargets.contains(id)
        || (state.pendingTransportTargets.contains(id) && !transportApproach)
        || std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [id](const RuntimeGrindingRelation& entry) { return entry.sourceId == id; })
        || std::any_of(state.repairDepots.relations().begin(), state.repairDepots.relations().end(),
            [id](const RuntimeRepairDepotRelation& entry) { return entry.unitId == id; })
        || (!object.harvestAutoSearchSuspended
            && (runtimeObjectIsHarvester(state, object) || runtimeObjectIsMobileSlaveMiner(state, object))))
        return false;
    return true;
}

// Exact small-command body routes do not grant a nearby arrival.
bool runtimeMoveAllowsNearbyCrowdArrival(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    return !state.pendingTransportTargets.contains(object.instanceId)
        && !behavior.pendingIgnoredObjectIds.empty()
        && runtimeMoveAllowsCrowdBodyRoute(state, object, behavior);
}

struct RuntimeSharedMoveCrowdSearch {
    bool applicable = false;
    bool settledBarrier = false;
    bool transientBarrier = false;
    bool foreignBarrier = false;
    bool exhausted = true;
    bool inwardRoute = false;
    int idealRadius = 0;
    std::vector<RuntimeCellPoint> path;
};

void runtimeSetCrowdInfantryPreviewAtCell(
    const RuntimeWindowState& state, WorldObjectInstance& preview,
    const RuntimeCellPoint& cell, int subCell
) {
    preview.x=cell.x; preview.y=cell.y; preview.cell=runtimeCellKey(cell.x,cell.y);
    preview.subCell=subCell; preview.onBridge=runtimeCellIsBridge(state,cell.x,cell.y);
    preview.hasRenderPosition=preview.renderMoveActive=false;
}

// Preview the same destination slot as the next actual batch. Only the first
// leg may use an existing reservation; later legs start at their canonical body.
bool runtimeCrowdBodyTransitionIsPassable(
    const RuntimeWindowState& state,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupied,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const WorldObjectInstance& preview, bool actualSource,
    PathMovementMode mode, int& destinationSubCell,
    std::vector<int>* blockingObjectIds=nullptr, bool allowCrushableDestination=false
) {
    if (blockingObjectIds) blockingObjectIds->clear();
    destinationSubCell=preview.subCell;
    if (!runtimeCellTransitionIsPassable(state,terrain,occupied,from,to,
            preview,-1,nullptr,allowCrushableDestination,mode)) return false;
    if (!runtimeInfantryUsesGroundSubcells(preview)) return true;
    if (actualSource) destinationSubCell=runtimeInfantryBodyPreviewDestinationSubcell(state,preview,to);
    else {
        const auto available=runtimeAvailableInfantrySubcells(state,preview,to);
        destinationSubCell=RuntimeSubcellSystemState::selectAvailableSubCell(available,preview.subCell);
    }
    return destinationSubCell>=0 && !runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(
        state,preview,from,to,destinationSubCell,blockingObjectIds);
}

// Ordinary group movement may exchange slots when the assigned inner slot is
// sealed. Search the real local walkable space, including side steps, instead
// of assuming that one blocked step proves the whole destination is full.
RuntimeSharedMoveCrowdSearch findRuntimeCloserSharedMoveSlot(
    RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    RuntimeSharedMoveCrowdSearch result;
    if ((!behavior.pendingHasSharedGoal && !runtimeInfantryUsesGroundSubcells(object))
        || behavior.moveCooldownTicks > 0 || object.renderMoveActive
        || !(behavior.pendingHasSharedGoal
            ? runtimeMoveAllowsNearbyCrowdArrival(state, object, behavior)
            : runtimeMoveAllowsCrowdBodyRoute(state, object, behavior))) return result;
    const auto current = runtimeCellPointForObject(object);
    const auto center = behavior.pendingHasSharedGoal ? behavior.pendingSharedGoal : behavior.moveTarget;
    const auto radius = [&](const RuntimeCellPoint& cell) {
        return std::max(std::abs(cell.x-center.x), std::abs(cell.y-center.y));
    };
    const int currentRadius = radius(current);
    // Formation slots and this bounded search use square cell rings. Apply
    // the existing near-goal radius in the same metric, including its corners.
    const int searchArrivalRadius=behavior.pendingHasSharedGoal
        ? runtimeSharedMoveArrivalRadius(behavior) : 3;
    bool distantBodyDetour=false;
    if (currentRadius > searchArrivalRadius) {
        if (behavior.pendingHasSharedGoal || behavior.pathRequestPending || behavior.path.empty()) return result;
        // A distant ordinary foot order needs a local physical detour only
        // when its actual next reserved body segment is obstructed. Clear
        // far-away routes keep the ordinary path without a bounded BFS.
        (void)runtimeTickOccupancyLookup(state);
        if (!runtimeInfantryBodySegmentAdmissionBlocked(state,object,current,behavior.path.front())) return result;
        distantBodyDetour=true;
    }
    int infantry = 0, vehicles = 0;
    for (int id : behavior.pendingIgnoredObjectIds) {
        const auto* member = findObjectById(state, id);
        if (!member || !member->alive || member->airborne
            || runtimeObjectMovementMode(state, *member) == PathMovementMode::Air) continue;
        if (runtimeInfantryUsesGroundSubcells(*member)) ++infantry;
        else ++vehicles;
    }
    const int cells = std::max(1, vehicles+(infantry+2)/3);
    while ((2*result.idealRadius+1)*(2*result.idealRadius+1) < cells) ++result.idealRadius;
    const bool foot=runtimeInfantryUsesGroundSubcells(object);
    if (currentRadius <= result.idealRadius && (!foot || current==behavior.moveTarget)) return result;

    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto here = terrain->find(runtimeCellKey(current.x, current.y));
    if (here == terrain->end() || !here->second) return result;
    const auto mode = runtimeObjectMovementMode(state, object);
    if (mode == PathMovementMode::Air) return result;
    const auto hereInfo = runtimeCachedTerrainInfoForCell(state, *here->second);
    const auto sameSurface = [&](const RuntimeCellPoint& cell) {
        const auto at = terrain->find(runtimeCellKey(cell.x, cell.y));
        if (at == terrain->end() || !at->second
            || runtimeEffectiveTerrainLevel(*at->second) != runtimeEffectiveTerrainLevel(*here->second)) return false;
        const auto info = runtimeCachedTerrainInfoForCell(state, *at->second);
        return info.passable && runtimeAuthoredLandTypeAllowsMover(state, info, object)
            && runtimeMapQueryPassabilityAllows(state, cell, object, mode)
            && (mode == PathMovementMode::Amphibious || hereInfo.water == info.water
                || hereInfo.shore || info.shore || hereInfo.bridge || info.bridge);
    };
    // A nearby plateau or another water surface is still a travel destination.
    if (!sameSurface(center) || !sameSurface(behavior.moveTarget)) return result;
    result.applicable = true;
    const auto& occupied = runtimeTickOccupancyLookup(state);
    // Preserve a genuinely usable committed route before doing any BFS. A
    // passable first step alone is insufficient: its inner end may be sealed.
    if (!behavior.pathRequestPending && !behavior.path.empty() && behavior.path.size() <= 24
        && (behavior.path.back() == behavior.moveTarget
            || (!behavior.pendingHasSharedGoal && radius(behavior.path.back()) < currentRadius
                && runtimeCellDistance(behavior.path.back(),center)+0.001
                    < runtimeCellDistance(current,center)))
        && (radius(behavior.moveTarget) < currentRadius || foot)) {
        RuntimeCellPoint previous = current;
        WorldObjectInstance preview=object;
        bool usable = true, actualSource=true;
        for (const auto& next : behavior.path) {
            int destinationSubCell=preview.subCell;
            if (!sameSurface(next) || !runtimeCrowdBodyTransitionIsPassable(state,*terrain,occupied,
                    previous,next,preview,actualSource,mode,destinationSubCell)) { usable=false; break; }
            previous=next; actualSource=false;
            if (foot) runtimeSetCrowdInfantryPreviewAtCell(state,preview,next,destinationSubCell);
        }
        if (usable) { result.inwardRoute=true; return result; }
    }
    // The navigation occupancy map keeps only one representative per cell.
    // A full infantry cell can contain both settled and departing occupants,
    // or be temporarily full because its last subcell is reserved. Index that
    // evidence once per local search, and classify each blocked cell once.
    bool infantryBarriersIndexed = false;
    std::unordered_map<int,std::vector<const WorldObjectInstance*>> infantryByCell;
    std::set<std::pair<int,bool>> reservedInfantryCells;
    std::unordered_set<int> notedBarrierCells;
    const auto noteOccupant = [&](const WorldObjectInstance* other, bool onBridge) {
        if (!other || !other->alive || other->airborne
            || other->instanceId == object.instanceId || other->onBridge != onBridge
            || (other->kind != WorldObjectKind::Unit && other->kind != WorldObjectKind::Infantry)) return;
        if (!runtimeOwnersAreAllied(state, object.owner, other->owner)) {
            result.foreignBarrier = true; return;
        }
        const auto motion = state.behaviors.find(other->instanceId);
        const bool settled = !other->renderMoveActive && (motion == state.behaviors.end()
            || (motion->second.moveCooldownTicks <= 0 && !motion->second.pathRequestPending
                && motion->second.path.empty()
                && (motion->second.state == RuntimeBehaviorState::Complete
                    || motion->second.state == RuntimeBehaviorState::Idle
                    || (motion->second.kind == RuntimeBehaviorKind::Move
                        ? runtimeCellPointForObject(*other) == motion->second.moveTarget
                        : motion->second.kind == RuntimeBehaviorKind::None
                            || motion->second.kind == RuntimeBehaviorKind::Attack))));
        if (settled) result.settledBarrier = true;
        else result.transientBarrier = true;
    };
    const auto noteBarrier = [&](const RuntimeCellPoint& cell) {
        if ((radius(cell) >= currentRadius && (!foot || cell!=behavior.moveTarget))
            || !sameSurface(cell)) return;
        const int key = runtimeCellKey(cell.x,cell.y);
        if (!notedBarrierCells.insert(key).second) return;
        // A departure cell can be logically empty while its body is still
        // walking out. It is temporary traffic, never a sealed inner ring.
        if (runtimeGroundDepartureHoldsCell(state, object, cell))
            result.transientBarrier = true;
        if (!infantryBarriersIndexed) {
            for (const auto& member : state.world.getObjects()) {
                if (member.alive && !member.airborne && member.kind == WorldObjectKind::Infantry)
                    infantryByCell[runtimeCellKey(member.x,member.y)].push_back(&member);
            }
            for (const auto& [id,reservation] : state.subcells.reservations()) {
                if (id == object.instanceId) continue;
                const auto* member = findObjectById(state,id);
                if (!member || !member->alive || member->airborne
                    || member->kind != WorldObjectKind::Infantry) continue;
                const auto& destination = reservation.destination;
                reservedInfantryCells.emplace(runtimeCellKey(destination.x,destination.y),
                    destination.onBridge);
            }
            infantryBarriersIndexed=true;
        }
        const bool onBridge = runtimeSubcellLocationForCell(state,cell).onBridge;
        const auto at = occupied.find(key);
        if (at != occupied.end()) noteOccupant(findObjectById(state,at->second),onBridge);
        const auto infantryAt = infantryByCell.find(key);
        if (infantryAt != infantryByCell.end())
            for (const auto* member : infantryAt->second) noteOccupant(member,onBridge);
        if (reservedInfantryCells.contains({key,onBridge})) result.transientBarrier=true;
    };
    const auto noteBodyBarrier = [&](const RuntimeCellPoint& cell, const std::vector<int>& ids) {
        if (radius(cell) >= currentRadius && cell!=behavior.moveTarget) return;
        // The body oracle already matched the physical/future bridge layer.
        // Keep the original owner/motion classification without filtering it
        // again by the object's prematurely committed navigation layer.
        for (int id:ids) {
            const auto* other=findObjectById(state,id);
            if (other) noteOccupant(other,other->onBridge);
        }
    };
    struct Node { RuntimeCellPoint cell; int parent; int distance; int subCell; };
    std::vector<Node> nodes{{current, -1, 0, object.subCell}};
    std::unordered_set<int> visited{runtimeCellKey(current.x, current.y)};
    std::set<std::pair<int,int>> visitedBody{{runtimeCellKey(current.x,current.y),object.subCell}};
    int best = -1, exactBest=-1;
    std::tuple<int,int,int,int,int> bestScore{INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX};
    const int searchRadius = currentRadius+1;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const Node node = nodes[index];
        const int cellRadius = radius(node.cell);
        if (foot && index>0 && node.cell==behavior.moveTarget && exactBest<0)
            exactBest=static_cast<int>(index);
        if (index > 0 && cellRadius < currentRadius
            && (behavior.pendingHasSharedGoal
                || runtimeCellDistance(node.cell,center)+0.001 < runtimeCellDistance(current,center))) {
            const int dx=node.cell.x-center.x, dy=node.cell.y-center.y;
            const auto score = std::make_tuple(std::max(cellRadius, result.idealRadius),
                node.distance, cellRadius, dx*dx+dy*dy, runtimeCellKey(node.cell.x,node.cell.y));
            if (best < 0 || score < bestScore) { best=static_cast<int>(index); bestScore=score; }
        }
        if (node.distance >= 24) { result.exhausted=false; continue; }
        WorldObjectInstance preview=object;
        if (foot && index>0) runtimeSetCrowdInfantryPreviewAtCell(state,preview,node.cell,node.subCell);
        for (const auto& next : runtimeNeighborsOf(node.cell)) {
            const bool outsideSearch=distantBodyDetour
                ? std::max(std::abs(next.x-current.x),std::abs(next.y-current.y))>8
                : radius(next)>searchRadius;
            if (outsideSearch || !sameSurface(next)) continue;
            const int key = runtimeCellKey(next.x,next.y);
            if (!foot && visited.contains(key)) continue;
            // No soft group occupancy: moving friends, diagonal corners and
            // occupied infantry subcells remain real obstacles to this route.
            int destinationSubCell=preview.subCell;
            std::vector<int> bodyBlockers;
            if (!runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, node.cell,
                    next, preview, index==0, mode, destinationSubCell, foot?&bodyBlockers:nullptr)) {
                noteBarrier(next); noteBodyBarrier(next,bodyBlockers);
                if (next.x != node.cell.x && next.y != node.cell.y) {
                    noteBarrier({next.x,node.cell.y}); noteBarrier({node.cell.x,next.y});
                }
                continue;
            }
            if (foot && visitedBody.contains({key,destinationSubCell})) continue;
            if (nodes.size() >= 512) { result.exhausted=false; continue; }
            if (foot) visitedBody.insert({key,destinationSubCell});
            else visited.insert(key);
            nodes.push_back({next, static_cast<int>(index), node.distance+1, destinationSubCell});
        }
    }
    if (exactBest>=0) best=exactBest;
    if (best >= 0) {
        for (int index=best; nodes[index].parent>=0; index=nodes[index].parent)
            result.path.push_back(nodes[index].cell);
        std::reverse(result.path.begin(),result.path.end());
    }
    return result;
}

bool prepareRuntimeCloserSharedMoveSlot(
    RuntimeWindowState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior
) {
    const auto search = findRuntimeCloserSharedMoveSlot(state, object, behavior);
    if (!search.applicable || search.inwardRoute) return false;
    const auto current = runtimeCellPointForObject(object);
    if (search.path.empty()) {
        // A path-queue fallback can already equal this outer cell. Wait for
        // actual passing traffic before declaring that fallback an arrival.
        if (current != behavior.moveTarget || !search.transientBarrier) return false;
        behavior.state = RuntimeBehaviorState::Blocked;
        return true;
    }
    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    // Small commands have no shared formation center. Their body detour must
    // retain the exact goal, including a partial safe approach to a sealed ring.
    if (behavior.pendingHasSharedGoal) behavior.moveTarget = search.path.back();
    behavior.pendingPathStart = current;
    behavior.pendingPathGoal = behavior.moveTarget;
    behavior.pendingPathTowardRange = behavior.pendingAllowNearbyTarget = false;
    behavior.pendingTargetObjectId = -1; behavior.pendingRangeCells = 0.0;
    behavior.pathRequestPending = false;
    state.pendingPathObjectIds.erase(std::remove(state.pendingPathObjectIds.begin(),
        state.pendingPathObjectIds.end(),object.instanceId),state.pendingPathObjectIds.end());
    replaceRuntimeBehaviorPath(behavior,search.path);
    behavior.blockedTicks=0; behavior.nextRepathTick=state.tick;
    behavior.state=RuntimeBehaviorState::Queued;
    state.simulationStatus="group closer slot #"+std::to_string(object.instanceId);
    return true;
}

// Share the same arrival decision with infantry reservation planning. A local
// sidestep or a newly solved path must not erase evidence of a full destination.
bool runtimeMoveCanFinishAtCrowdedDestination(
    RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    if (behavior.moveCooldownTicks > 0 || object.renderMoveActive) return false;
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    const double slotDistance = runtimeCellDistance(current, behavior.moveTarget);
    const bool nearSharedGoal = behavior.pendingHasSharedGoal
        && runtimeCellDistance(current, behavior.pendingSharedGoal)
            <= runtimeSharedMoveArrivalRadius(behavior) + 0.001;
    if (current == behavior.moveTarget || (!nearSharedGoal && slotDistance > 2.25)
        || !runtimeMoveAllowsNearbyCrowdArrival(state, object, behavior)) return false;

    const int id = object.instanceId;
    const auto& occupied = runtimeTickOccupancyLookup(state);
    const auto settledAlly = [&](const WorldObjectInstance* other) {
        if (!other || !other->alive || other->instanceId==id || other->airborne || other->renderMoveActive
            || (other->kind != WorldObjectKind::Unit && other->kind != WorldObjectKind::Infantry)
            || !runtimeOwnersAreAllied(state, object.owner, other->owner)) return false;
        const auto active = state.behaviors.find(other->instanceId);
        if (active == state.behaviors.end()) return true;
        const auto& motion = active->second;
        if (motion.moveCooldownTicks > 0 || motion.pathRequestPending || !motion.path.empty()) return false;
        return motion.state == RuntimeBehaviorState::Complete
            || motion.state == RuntimeBehaviorState::Idle
            || (motion.kind == RuntimeBehaviorKind::Move
                ? runtimeCellPointForObject(*other) == motion.moveTarget
                : motion.kind == RuntimeBehaviorKind::None || motion.kind == RuntimeBehaviorKind::Attack);
    };
    const auto settledAllyAt = [&](const RuntimeCellPoint& cell) {
        const auto found = occupied.find(runtimeCellKey(cell.x, cell.y));
        return found!=occupied.end() && settledAlly(findObjectById(state,found->second));
    };
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto mode = runtimeObjectMovementMode(state, object);
    if (mode == PathMovementMode::Air) return false;

    // Straight-line proximity across a cliff or separate water surface is not
    // arrival. Keep taking the route to the ramp/shore until on the goal's level.
    const auto here = terrain->find(runtimeCellKey(current.x, current.y));
    if (here == terrain->end() || !here->second) return false;
    const auto hereInfo = runtimeCachedTerrainInfoForCell(state, *here->second);
    const auto sameArrivalSurface = [&](const RuntimeCellPoint& cell) {
        const auto at = terrain->find(runtimeCellKey(cell.x, cell.y));
        if (at == terrain->end() || !at->second
            || runtimeEffectiveTerrainLevel(*at->second) != runtimeEffectiveTerrainLevel(*here->second)) return false;
        const auto info = runtimeCachedTerrainInfoForCell(state, *at->second);
        return info.passable && runtimeAuthoredLandTypeAllowsMover(state, info, object)
            && runtimeMapQueryPassabilityAllows(state, cell, object, mode)
            && (mode == PathMovementMode::Amphibious || hereInfo.water == info.water
                || hereInfo.shore || info.shore || hereInfo.bridge || info.bridge);
    };
    if (!sameArrivalSurface(behavior.moveTarget)) return false;

    const auto sharedSearch = findRuntimeCloserSharedMoveSlot(state, object, behavior);
    if (sharedSearch.applicable) {
        if (sharedSearch.inwardRoute || !sharedSearch.path.empty() || sharedSearch.transientBarrier) return false;
        // A genuinely sealed inner ring permits a stable outer arrival. A
        // sideways first step alone must not restart an endless orbit of it.
        if (sharedSearch.exhausted && sharedSearch.settledBarrier && !sharedSearch.foreignBarrier) return true;
    }

    // Preserve a usable route around the crowd, including a first step that
    // temporarily goes sideways. A direct approach being blocked is not enough
    // to discard an already solved route to an available destination.
    bool crowdBlocksProgress = false, bodyBarrierDeferred = false;
    const auto noteBodyBlockers = [&](const std::vector<int>& ids) {
        if (ids.empty()) return;
        for (int blocker:ids) if (!settledAlly(findObjectById(state,blocker))) {
            bodyBarrierDeferred=true; return;
        }
        crowdBlocksProgress=true;
    };
    int destinationSubCell=object.subCell;
    std::vector<int> bodyBlockers;
    if (!behavior.path.empty() && behavior.path.back() == behavior.moveTarget) {
        if (runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, current,
                behavior.path.front(), object, true, mode, destinationSubCell,
                &bodyBlockers, true)) return false;
        noteBodyBlockers(bodyBlockers);
    }

    // A free inner slot can be sealed off by units that have already arrived.
    // Keep advancing while any closer step is available; terrain by itself or
    // a unit merely passing through is not evidence of a crowded destination.
    for (const auto& next : runtimeNeighborsOf(current)) {
        if (runtimeCellDistance(next, behavior.moveTarget) + 0.001 >= slotDistance) continue;
        if (runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, current, next,
                object, true, mode, destinationSubCell, &bodyBlockers, true)) return false;
        noteBodyBlockers(bodyBlockers);
        if (!settledAllyAt(next) || !sameArrivalSurface(next)) continue;
        if (next.x != current.x && next.y != current.y) {
            const RuntimeCellPoint horizontal{next.x, current.y}, vertical{current.x, next.y};
            const auto clearOrAllied = [&](const RuntimeCellPoint& cell) {
                return sameArrivalSurface(cell)
                    && (!occupied.contains(runtimeCellKey(cell.x, cell.y)) || settledAllyAt(cell));
            };
            if (!clearOrAllied(horizontal) || !clearOrAllied(vertical)) continue;
        }
        crowdBlocksProgress = true;
    }
    return crowdBlocksProgress && !bodyBarrierDeferred;
}

void acceptRuntimePlayerTravelArrival(
    RuntimeWindowState& state, const WorldObjectInstance& object
) {
    const auto current = runtimeCellPointForObject(object);
    // Attack-move and guard travel must accept this arrival too, otherwise
    // their retained exact destination immediately issues another move order.
    const auto travel = state.playerCommands.travelling.find(object.instanceId);
    if (travel != state.playerCommands.travelling.end()
        && travel->second.owner == runtimeCanonicalHumanHouseOwner(object.owner))
        travel->second.destination = {current.x, current.y};
}

bool completeRuntimeMoveNearCrowdedGoal(
    RuntimeWindowState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior
) {
    if (!runtimeMoveCanFinishAtCrowdedDestination(state, object, behavior)) return false;
    const auto current = runtimeCellPointForObject(object);
    behavior.moveTarget = behavior.pendingPathGoal = current;
    behavior.state = RuntimeBehaviorState::Complete;
    behavior.path.clear();
    behavior.pathRequestPending = false;
    state.pendingPathObjectIds.erase(std::remove(state.pendingPathObjectIds.begin(),
        state.pendingPathObjectIds.end(),object.instanceId),state.pendingPathObjectIds.end());
    behavior.blockedTicks = 0;
    acceptRuntimePlayerTravelArrival(state, object);
    setRuntimeMissionIntent(state, object, RuntimeMissionIntent::Idle, "moveCrowdedGoalReached");
    clearRuntimeRenderPosition(object);
    ++state.actionSummary.completedCommands;
    return true;
}
