bool runtimeGrindingEntryContinuesToHost(
    const RuntimeWindowState& state, int hostId, int sourceId
) {
    const auto* source = findObjectById(state, sourceId);
    if (!source || runtimeObjectConflictsWithGrinding(state, sourceId)) return false;
    for (int id : source->transportPassengerIds) {
        if (runtimeObjectConflictsWithGrinding(state, id, sourceId)) return false;
    }
    return state.grinding.hasActiveEntry(makeRuntimeGrindingContext(state), hostId, sourceId);
}

void emitRuntimeGrindingCompletionSounds(
    RuntimeWindowState& state, const std::vector<RuntimeGrindingAction>& actions
) noexcept {
    // Only committed removals reach here. A transport and its cargo are one
    // recycling event, and an audio failure cannot roll back its refund.
    try {
        const std::string sound = runtimeRulesSoundSection(state, "EnterGrinderSound", "GrinderGrinding");
        for (const auto& action : actions) {
            if (action.kind != RuntimeGrindingActionKind::RemoveUnit
                || action.removalRole != RuntimeGrindingRemovalRole::Root) continue;
            if (const auto* host = findObjectById(state, action.hostId)) {
                emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(*host));
            }
        }
    } catch (...) {
        // Sound is presentation; the consumed objects and credits are final.
    }
}
