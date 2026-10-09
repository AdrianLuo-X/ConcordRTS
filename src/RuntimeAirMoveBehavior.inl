bool prepareRuntimeAirMoveStep(
    RuntimeWindowState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior
) {
    if (!runtimeFactoryAirExitReadyToMove(state, object)) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        return false;
    }
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    behavior.pendingHasSharedGoal = false;
    RuntimeJumpjetFlight* flight = object.airborne && runtimeJumpjetProfile(state, object)
        ? &runtimeJumpjetFlightFor(state, object) : nullptr;
    if (flight && (flight->targetX != behavior.moveTarget.x || flight->targetY != behavior.moveTarget.y)) {
        bindRuntimeJumpjetMoveGoal(state, object, behavior.moveTarget, behavior.moveTarget);
    }
    const RuntimeCellPoint anchor = flight ? RuntimeCellPoint{flight->goalX, flight->goalY} : behavior.moveTarget;
    const auto accepts = [&](const RuntimeCellPoint& point) {
        return !flight || runtimeCellDistance(anchor, point) <= flight->goalRadius;
    };
    const auto assign = [&](const RuntimeCellPoint& point) {
        behavior.moveTarget = point;
        if (flight) { flight->targetX=point.x; flight->targetY=point.y; }
    };
    if (!behavior.path.empty()
        && runtimeAirTransitionIsClear(state, object.instanceId, current, behavior.path.front())) {
        return true;
    }
    if (state.tick < behavior.nextRepathTick) return false;

    // An idle flyer may have stopped in a previously assigned slot. Move to
    // another free slot nearby; a flyer merely passing through keeps its
    // right of way. Conflicting non-player orders use stable ID priority.
    bool targetTaken = false;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == object.instanceId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        const auto found = state.behaviors.find(other.instanceId);
        const bool leaving = found != state.behaviors.end()
            && found->second.state != RuntimeBehaviorState::Complete
            && ((found->second.kind == RuntimeBehaviorKind::Move
                    && found->second.moveTarget != behavior.moveTarget)
                || (found->second.kind == RuntimeBehaviorKind::Attack
                    && !found->second.path.empty()));
        targetTaken = targetTaken
            || (runtimeCellPointForObject(other) == behavior.moveTarget && !leaving)
            || (other.instanceId < object.instanceId && found != state.behaviors.end()
                && found->second.kind == RuntimeBehaviorKind::Move
                && found->second.state != RuntimeBehaviorState::Complete
                && found->second.moveTarget == behavior.moveTarget);
    }
    if (targetTaken && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)) {
        const auto replacement = findRuntimeAirMoveTarget(state, object, anchor);
        if (replacement.has_value() && accepts(*replacement)) assign(*replacement);
    }

    const auto started = std::chrono::steady_clock::now();
    auto path = findRuntimeAirPath(state, object, current, behavior.moveTarget);
    if (path.empty() && current != behavior.moveTarget && behavior.blockedTicks > 0
        && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)) {
        // Arrived units can surround a free inner formation slot. Reserve a
        // reachable slot on the outside instead of searching that sealed
        // pocket forever or flying through its occupants.
        RuntimeCellPoint candidate;
        std::vector<RuntimeCellPoint> alternative;
        if (findRuntimeReachableAirMoveTarget(state, object, anchor,
                flight ? flight->goalRadius : 8, candidate, alternative)) {
            assign(candidate);
            path = std::move(alternative);
        }
    }
    ++state.perf.lastSimPathRequests;
    state.perf.lastSimPathMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    replaceRuntimeBehaviorPath(behavior, std::move(path));
    behavior.pathRequestPending = false;
    behavior.pendingPathGoal = behavior.moveTarget;
    if (behavior.path.empty() && current != behavior.moveTarget) {
        ++behavior.blockedTicks;
        behavior.state = RuntimeBehaviorState::Blocked;
        behavior.nextRepathTick = state.tick + runtimeRepathCooldownTicks;
        return false;
    }
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.nextRepathTick = state.tick;
    return true;
}
