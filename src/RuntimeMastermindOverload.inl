bool advanceRuntimeMindControlOverload(RuntimeWindowState& state, const SpecialEventSink& sink) {
    const auto pulses = state.specialMechanisms.tickMindControlOverload(
        state.world.getObjects(), state.rules, state.tick, state.config.fixedTickHz);
    const auto& rules = state.rules.getSpecialMechanismRules();
    bool changed = false;
    for (const auto& pulse : pulses) {
        auto* controller = findMutableObjectById(state, pulse.controllerObjectId);
        if (controller == nullptr || !controller->alive) continue;
        int applied = 0;
        if (!runtimeObjectIgnoresDamage(state, *controller)
            && !(state.superWeaponsInitialized
                && state.superWeapons.isObjectProtected(controller->instanceId, state.tick))) {
            // Retail calls ReceiveDamage on the controller with C4Warhead and
            // no attacker. This damages only the controller, never its victims.
            applied = runtimeAuthoredDamageAgainst(state, *controller, pulse.damage, rules.c4Warhead);
            controller->health = std::max(0, controller->health - applied);
            if (applied > 0) runtimeRevealObjectAfterDamage(state, *controller, "mindControlOverload");
            changed = changed || applied > 0;
        }
        if (pulse.playSound && !rules.masterMindOverloadDeathSound.empty())
            emitRuntimeSound(state, "sound:" + rules.masterMindOverloadDeathSound, runtimeSpatialAudioPositionForObject(*controller));
        if (sink) sink({"MindControlOverload", state.tick, controller->instanceId, -1, applied,
            {controller->x, controller->y}, "controlled=" + std::to_string(pulse.controlledCount)});
        if (controller->health <= 0) {
            RuntimeObjectExitRequest exit;
            exit.objectId = controller->instanceId;
            exit.kind = RuntimeObjectExitKind::Destroyed;
            exit.tick = state.tick;
            exit.mission = "Destroyed";
            exit.reason = "mind-control overload";
            (void)queueRuntimeObjectExit(state, std::move(exit));
        }
    }
    return drainRuntimeObjectExitQueue(state) || changed;
}
