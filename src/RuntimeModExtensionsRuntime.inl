bool advanceRuntimeModExtensions(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeModExtensions", state.tick);
    bool changed = false;
    RuntimeInactiveModExtensionNoopStage inactiveNoop(state.rules);
    for (auto& object : state.world.accessObjects()) {
        // With no initialization, authored shield/innate effect, online shield
        // or attached effects, both hidden and dead branches are exact no-ops.
        // All other objects retain the original query and mutation sequence.
        if (inactiveNoop.canSkip(object)) continue;
        const bool hidden = object.mission == "InsideTransport" || object.mission == "InTransport"
            || object.garrisonHostObjectId >= 0
            || state.specialMechanisms.isParasiteSourceLimboed(object.instanceId)
            || state.specialMechanisms.isCarrierChildDocked(object.instanceId);
        changed = runtimeAdvanceModExtensions(object, state.rules, state.config.fixedTickHz, hidden,
            state.specialMechanisms.isTemporalFrozen(object.instanceId),
            runtimeObjectHasOperationalPower(state, object)) || changed;
    }
    return changed;
}
