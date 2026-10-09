int runtimeMcvResolvedDeployFacing(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const UnitType& unit) {
    const auto* building = findRuntimeBuildingTypeById(state.rules, unit.deploysInto);
    const int authored = building && building->deployFacing >= 0
        ? building->deployFacing : unit.deployFacing;
    if (authored >= 0) return worldObjectFacingFromMapFacing(std::clamp(authored, 0, 7) * 32);
    // Retail construction-yard MCVs align to the lower-left isometric edge.
    return runtimeUnitTypeIsMcv(state, unit) ? 160 : object.facing;
}

bool requestRuntimeMcvDeployment(RuntimeWindowState& state, int objectId,
    bool selectResult = true, std::optional<RuntimeCellPoint> requestedCell = std::nullopt) {
    auto* mcv = findMutableObjectById(state, objectId);
    const auto* unit = mcv ? findRuntimeUnitType(state.rules, *mcv) : nullptr;
    if (!mcv || !unit || !runtimeUnitTypeIsMcv(state, *unit))
        return deployRuntimeMcv(state, objectId, selectResult, requestedCell);
    if (runtimeMissionIsMcvDeployPending(*mcv)) return true;
    if (mcv->airborne || runtimeObjectDisabledByParasite(state, objectId)
        || state.specialMechanisms.isChronoMovementLocked(objectId, state.tick)) return false;
    const auto item = runtimeBuildItemForBuilding(state, unit->deploysInto);
    const RuntimeCellPoint site = requestedCell.value_or(runtimeCellPointForObject(*mcv));
    // Pending moveTarget remains a center anchor through turning and saves.
    if (!item || !runtimeCellCanPlaceBuilding(state, *item,
        runtimePlacementTopLeftForAnchor(site, runtimeStructureFootprintForBuildItem(state, *item)),
        objectId, nullptr, mcv->owner))
        return deployRuntimeMcv(state, objectId, selectResult, requestedCell);
    const int desired = runtimeMcvResolvedDeployFacing(state, *mcv, *unit);
    const auto previous = state.behaviors.find(objectId);
    const bool committed = previous != state.behaviors.end() && previous->second.moveCooldownTicks > 0;
    if (mcv->facing == desired && !committed)
        return deployRuntimeMcv(state, objectId, selectResult, site);
    const WorldObjectInstance oldPose = *mcv;
    RuntimeObjectBehavior pending = committed ? previous->second : RuntimeObjectBehavior{};
    pending.kind = RuntimeBehaviorKind::Move; pending.state = RuntimeBehaviorState::Queued;
    pending.moveTarget = site; pending.targetObjectId = -1; pending.path.clear();
    pending.pathRequestPending = pending.pendingPathTowardRange = pending.pendingHasSharedGoal = false;
    pending.pendingPathStart = pending.pendingPathGoal = pending.pendingSharedGoal = {-1,-1};
    pending.pendingAllowedOccupiedObjectId = pending.pendingTargetObjectId = -1;
    pending.pendingIgnoredObjectIds.clear(); pending.repackMoveGroup.reset();
    pending.autoAcquired = pending.forceFire = pending.attackGround = pending.attackBridge = pending.returnToDeployed = false;
    eraseRuntimeBehavior(state, objectId);
    mcv = findMutableObjectById(state, objectId); if (!mcv) return false;
    *mcv = oldPose; // Erasing an order must not snap the already committed step.
    if (!committed) mcv->bodyFacingClock = {};
    mcv->mission = selectResult ? "McvDeployTurning" : "McvDeployTurningUnselected";
    state.behaviors[objectId] = std::move(pending);
    state.lastCommand.valid = false;
    state.economy.status = state.interactionStatus = "MCV turning to deploy";
    invalidateRuntimeTickOccupancyCache(state); invalidateRuntimeObjectProjectionCache(state);
    return true;
}

bool advanceRuntimeMcvDeployTurns(RuntimeWindowState& state) {
    std::vector<int> pendingIds;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeMissionIsMcvDeployPending(object)) pendingIds.push_back(object.instanceId);
    std::sort(pendingIds.begin(), pendingIds.end());
    bool changed = false;
    for (const int id : pendingIds) {
        auto* mcv = findMutableObjectById(state, id);
        const auto* unit = mcv ? findRuntimeUnitType(state.rules, *mcv) : nullptr;
        const auto behavior = state.behaviors.find(id);
        if (!mcv || !unit || !runtimeUnitTypeIsMcv(state, *unit) || behavior == state.behaviors.end()) {
            if (mcv) mcv->mission = "Guard";
            continue;
        }
        if (runtimeObjectDisabledByParasite(state, id) || (state.specialMechanismsInitialized
            && (state.specialMechanisms.isChronoMovementLocked(id, state.tick) || state.specialMechanisms.isTemporalFrozen(id)))) continue;
        auto& pending = behavior->second;
        if (pending.moveCooldownTicks > 0) {
            changed = advanceRuntimeRenderMove(state, *mcv, pending) || changed;
            if (--pending.moveCooldownTicks == 0) {
                pending.moveSegmentFrom = {-1,-1}; clearRuntimeRenderPosition(*mcv);
                mcv->bodyFacingClock = {}; invalidateRuntimeTickOccupancyCache(state);
            }
            continue;
        }
        const auto mission = toLowerAsciiLocal(mcv->mission);
        const bool selectResult = mission == "mcvdeployturning" || mission == "mcvdeployready";
        const bool ready = mission == "mcvdeployready" || mission == "mcvdeployreadyunselected";
        const int desired = runtimeMcvResolvedDeployFacing(state, *mcv, *unit);
        if (!ready) {
            mcv->facing = runtimeAdvanceBodyFacing(state, *mcv, mcv->facing, desired);
            mcv->turretFacing = mcv->facing;
            if (mcv->facing == desired) mcv->mission = selectResult ? "McvDeployReady" : "McvDeployReadyUnselected";
            changed = true;
            continue;
        }
        // Keep the aligned chassis visible for one tick, then atomically
        // transform. Recheck the whole foundation after the turn's delay.
        const RuntimeCellPoint site = pending.moveTarget;
        if (!deployRuntimeMcv(state, id, selectResult, site)) {
            eraseRuntimeBehavior(state, id);
            if (auto* stopped = findMutableObjectById(state, id)) stopped->mission = "Guard";
            emitRuntimeSound(state, runtimeSoundIntentCannotDeployHere);
        }
        changed = true;
    }
    return changed;
}
