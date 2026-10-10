void clearRuntimeCarrierAttackPreservingPose(RuntimeWindowState& state,
    WorldObjectInstance& child) {
    if (!state.behaviors.contains(child.instanceId)) return;
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double level = child.hasRenderPosition ? child.renderLevel : child.level;
    eraseRuntimeBehavior(state, child.instanceId);
    child.hasRenderPosition = true;
    child.renderMoveActive = true;
    child.renderX = x; child.renderY = y; child.renderLevel = level;
    child.renderOnBridge = false;
}

std::optional<WorldObjectInstance> runtimeCarrierAircraftAttackTarget(
    const RuntimeWindowState& state, const RuntimeObjectBehavior& order, int targetId) {
    std::optional<WorldObjectInstance> target;
    if (order.attackGround) {
        target = runtimeGroundAttackTargetObject(state, order.attackBridgeCell);
    } else if (order.attackBridge) {
        target = runtimeBridgeAttackTargetObject(state, order.attackBridgeCell);
    } else if (const auto* object = findObjectById(state, targetId)) {
        target = *object;
    }
    return target && target->instanceId == targetId ? target : std::nullopt;
}

bool synchronizeRuntimeCarrierAircraftOrders(RuntimeWindowState& state,
    const SpecialEventSink& sink) {
    bool changed = false;
    for (const auto& [childId, sortie] : state.specialMechanisms.carrierChildren()) {
        auto* child = findMutableObjectById(state, childId);
        if (!child || !child->alive || state.rules.findSpawnedMissileProfile(child->type)) continue;
        if (sortie.phase == CarrierChildPhase::Returning) {
            if (state.behaviors.contains(childId)) {
                clearRuntimeCarrierAttackPreservingPose(state, *child);
                child->mission = "CarrierReturning";
                changed = true;
            }
            continue;
        }
        if (sortie.phase != CarrierChildPhase::Launching
            && sortie.phase != CarrierChildPhase::Attacking) continue;
        const auto parentOrder = state.behaviors.find(sortie.parentObjectId);
        const auto childOrder = state.behaviors.find(childId);
        const auto target = parentOrder == state.behaviors.end()
            ? std::nullopt : runtimeCarrierAircraftAttackTarget(
                state, parentOrder->second, sortie.targetObjectId);
        bool recall = !target || !target->alive || parentOrder == state.behaviors.end()
            || parentOrder->second.kind != RuntimeBehaviorKind::Attack
            || childOrder == state.behaviors.end()
            || childOrder->second.kind != RuntimeBehaviorKind::Attack;
        if (!recall) {
            auto& behavior = childOrder->second;
            const auto& parent = parentOrder->second;
            const bool orderChanged = behavior.targetObjectId != sortie.targetObjectId
                || behavior.forceFire != parent.forceFire
                || behavior.autoAcquired != parent.autoAcquired
                || behavior.attackGround != parent.attackGround
                || behavior.attackBridge != parent.attackBridge
                || behavior.attackBridgeCell.x != parent.attackBridgeCell.x
                || behavior.attackBridgeCell.y != parent.attackBridgeCell.y;
            if (orderChanged) {
                behavior.path.clear(); behavior.pathRequestPending = false;
                if (behavior.state == RuntimeBehaviorState::Complete)
                    behavior.state = RuntimeBehaviorState::Queued;
                changed = true;
            }
            // Copy the target and its relation semantics together, after the
            // carrier state machine has selected this tick's parent target.
            behavior.targetObjectId = sortie.targetObjectId;
            behavior.forceFire = parent.forceFire;
            behavior.autoAcquired = parent.autoAcquired;
            behavior.attackGround = parent.attackGround;
            behavior.attackBridge = parent.attackBridge;
            behavior.attackBridgeCell = parent.attackBridgeCell;
            const auto profile = runtimeWeaponProfileForObject(state, *child, &*target, behavior.forceFire);
            recall = behavior.state == RuntimeBehaviorState::Complete
                || !profile.weapon || !runtimeWeaponHasAttackRange(*profile.weapon)
                || !runtimeObjectHasAttackAmmo(state.rules, *child);
        }
        if (recall) {
            changed = state.specialMechanisms.requestCarrierChildReturn(
                childId, state.tick, sink, "aircraft attack unavailable") || changed;
            clearRuntimeCarrierAttackPreservingPose(state, *child);
            child->mission = "CarrierReturning";
        }
    }
    return changed;
}
