bool runtimeGattlingFiringRunActive(const RuntimeWindowState& state, int id) {
    const auto* object = findObjectById(state, id);
    const auto order = state.behaviors.find(id);
    if (!object || !object->alive
        || !runtimeCachedCapabilityProfileForObject(state, *object).gattling
        || !runtimeCombatCanAct(state, *object)
        || !runtimeObjectHasAttackAmmo(state.rules, *object)
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.moveCooldownTicks != 0 || object->renderMoveActive) return false;
    const auto savedHeat = state.gattlingWeaponTimers.find(id);
    // Ordinary saves reconstruct Attack as Queued before its first update.
    // An existing positive heat value still belongs to that valid firing run.
    const bool resumingHeat = order->second.state == RuntimeBehaviorState::Queued
        && savedHeat != state.gattlingWeaponTimers.end() && savedHeat->second > 0;
    if (!resumingHeat && order->second.state != RuntimeBehaviorState::Attacking
        && order->second.state != RuntimeBehaviorState::Cooldown) return false;
    const auto& behavior = order->second;
    std::optional<WorldObjectInstance> terrain;
    if (behavior.attackGround)
        terrain = runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
    else if (behavior.attackBridge)
        terrain = runtimeBridgeAttackTargetObject(state, behavior.attackBridgeCell, 0);
    const auto* target = terrain ? &*terrain : findObjectById(state, behavior.targetObjectId);
    if (!target || !target->alive) return false;
    const auto profile = runtimeWeaponProfileForObject(state, *object, target, behavior.forceFire);
    const int facing = runtimeFacingFromDelta(target->x - object->x, target->y - object->y);
    return profile.weapon
        && (profile.weapon->damage > 0 || runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
        && runtimeWeaponHasLineOfSight(state, *object, *target, *profile.weapon)
        && runtimeWeaponDistanceToTarget(state, *object, *target, *profile.weapon)
            <= runtimeEffectiveWeaponRange(state, *object, *target, *profile.weapon)
        && (facing < 0 || runtimeWeaponFiringFacing(state, *object, *target) == facing);
}

bool advanceRuntimeGattlingWeaponTimers(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeGattlingWeaponTimers", state.tick);
    std::unordered_set<int> trackedIds;
    for (const auto& [id, timer] : state.gattlingWeaponTimers) {
        (void)timer;
        trackedIds.insert(id);
    }
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind == RuntimeBehaviorKind::Attack) {
            const auto* object = findObjectById(state, id);
            if (object && runtimeCachedCapabilityProfileForObject(state, *object).gattling)
                trackedIds.insert(id);
        }
    }
    // Weapon stages advance once per original rule frame.
    const int elapsed = runtimeRuleFramesElapsed(state.tick, state.config.fixedTickHz);
    bool changed = false;
    for (int id : trackedIds) {
        const auto* object = findObjectById(state, id);
        if (!runtimeGattlingFiringRunActive(state, id)) {
            // A pause, lost target, movement, obstruction or power loss ends
            // this firing run. Cooldown between shots in the same run does not.
            changed = state.gattlingWeaponTimers.erase(id) > 0 || changed;
            continue;
        }
        if (elapsed == 0) continue;
        const auto& cached = runtimeCachedCapabilityProfileForObject(state, *object);
        int& timer = state.gattlingWeaponTimers[id];
        const int previous = timer;
        timer = static_cast<int>(std::min<long long>(runtimeGattlingTimerCap(cached, object->veterancy >= 2),
            static_cast<long long>(timer) + static_cast<long long>(std::max(1, cached.weaponRateUp)) * elapsed));
        changed = timer != previous || changed;
    }
    return changed;
}
