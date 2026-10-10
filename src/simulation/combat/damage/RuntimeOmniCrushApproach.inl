// Retail UnitClass 741603 directs OmniCrusher toward its eligible target;
// 741801/5F6560 overruns only at a strict XY distance below 128 leptons.
bool runtimeObjectUsesOmniCrushApproach(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    return object.alive && object.kind == WorldObjectKind::Unit
        && !object.airborne && object.dockedToObjectId < 0
        && !runtimeGateObjectIsLimbo(object) && object.garrisonHostObjectId < 0
        && object.tankBunkerHostId < 0
        && !runtimeObjectOffGroundByMagBeam(state, object.instanceId)
        && runtimeCachedCapabilityProfileForObject(state, object).omniCrusher
        && runtimeCrusherUsesCrushingMovement(state, object);
}

// Existing segment authority holds the departure cell while approaching a
// victim. No victim identity, ignored occupant, or new persistence bit is used.
bool runtimeOmniCrushPendingSegment(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    return runtimeObjectUsesOmniCrushApproach(state, object)
        && (behavior.kind == RuntimeBehaviorKind::Move
            || behavior.kind == RuntimeBehaviorKind::Attack)
        && object.hasRenderPosition && object.renderMoveActive
        && behavior.renderMoveTicksTotal > 0
        && behavior.renderFrom == runtimeCellPointForObject(object)
        && behavior.renderTo != behavior.renderFrom;
}

bool runtimeOmniCrushOwnsOccupiedEndpoint(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& goal
) {
    if (!runtimeObjectUsesOmniCrushApproach(state, object)) return false;
    const auto scan = runtimeScanCrushTargetsAtCell(state, object, goal);
    return !scan.blocked && !scan.targetIds.empty();
}

bool runtimeOmniCrushBodiesTouch(
    const WorldObjectInstance& crusher, const WorldObjectInstance& target
) {
    const auto sourceOffset = worldObjectSubCellOffset(crusher);
    const auto targetOffset = worldObjectSubCellOffset(target);
    const double dx = runtimeObjectRenderX(crusher) + sourceOffset.x
        - runtimeObjectRenderX(target) - targetOffset.x;
    const double dy = runtimeObjectRenderY(crusher) + sourceOffset.y
        - runtimeObjectRenderY(target) - targetOffset.y;
    const bool sourceBridge = crusher.hasRenderPosition
        ? crusher.renderOnBridge : crusher.onBridge;
    const bool targetBridge = target.hasRenderPosition
        ? target.renderOnBridge : target.onBridge;
    return sourceBridge == targetBridge && dx * dx + dy * dy < 0.25;
}

RuntimeCrushTargetScan runtimeScanOmniCrushPhysicalContacts(
    const RuntimeWindowState& state, const WorldObjectInstance& crusher,
    const RuntimeCellPoint& destination
) {
    auto scan = runtimeScanCrushTargetsAtCell(state, crusher, destination);
    if (scan.blocked) return scan;
    scan.targetIds.clear();
    // Navigation cells precommit ordinary victims' steps. Retail contacts the
    // actual XY bodies in its ground/bridge layer, including a departing body.
    // This query never makes those victims transparent to path planning.
    for (const auto& target : state.world.getObjects()) {
        if (target.instanceId == crusher.instanceId || !target.alive
            || runtimeGateObjectIsLimbo(target) || target.garrisonHostObjectId >= 0
            || target.tankBunkerHostId >= 0
            || !runtimeOmniCrushBodiesTouch(crusher, target)
            || !runtimeCrusherCanCrushObject(state, crusher, target, destination, true)) continue;
        scan.targetIds.push_back(target.instanceId);
    }
    std::sort(scan.targetIds.begin(), scan.targetIds.end());
    return scan;
}

bool cancelRuntimeBlockedOmniCrushApproach(
    RuntimeWindowState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior
) {
    if (!runtimeOmniCrushPendingSegment(state, object, behavior)
        || !runtimeScanCrushTargetsAtCell(state, object, behavior.renderTo).blocked)
        return false;
    // A relation or protection may change after the step was accepted. Keep
    // the departure occupancy and freeze the actual body, just as Stop does.
    object.renderMoveActive = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
    behavior.renderArrivalDistanceAfter = -1.0;
    behavior.moveSegmentFrom = {-1, -1};
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.state = RuntimeBehaviorState::Blocked;
    behavior.nextRepathTick = state.tick + 1;
    return true;
}
