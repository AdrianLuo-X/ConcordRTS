// Called after buildup clocks advance, outside any world-object iteration:
// transforming appends a vehicle and can reallocate the world object vector.
bool advanceRuntimeStructureRepacks(RuntimeWindowState& state) {
    std::vector<int> completed;
    for (const auto& object : state.world.getObjects()) {
        if (runtimeStructureIsRepacking(object) && object.buildAnimTicksRemaining <= 0
            && !(state.specialMechanismsInitialized
                && state.specialMechanisms.isTemporalFrozen(object.instanceId))) {
            completed.push_back(object.instanceId);
        }
    }
    bool changed = false;
    for (int id : completed) {
        const bool selected = std::find(state.selectedObjectIds.begin(),
            state.selectedObjectIds.end(), id) != state.selectedObjectIds.end();
        std::optional<RuntimeCellPoint> moveTarget;
        std::optional<RuntimeRepackMoveGroupIntent> moveGroup;
        const auto pending = state.behaviors.find(id);
        if (pending != state.behaviors.end() && pending->second.kind == RuntimeBehaviorKind::Move) {
            moveTarget = pending->second.moveTarget;
            moveGroup = pending->second.repackMoveGroup;
        }
        int vehicleId = -1;
        if (!undeployRuntimeStructure(state, id, &vehicleId, selected)) {
            // An external spawn may occupy the exit after folding began.
            // Hold the last folding frame and retry without creating a unit.
            if (auto* source = findMutableObjectById(state, id);
                source != nullptr && runtimeStructureIsRepacking(*source)) {
                source->buildAnimTicksRemaining = 1;
            }
            continue;
        }
        auto* vehicle = findMutableObjectById(state, vehicleId);
        if (vehicle != nullptr && moveTarget) {
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = *moveTarget;
            move.repackMoveGroup = std::move(moveGroup);
            state.behaviors[vehicleId] = std::move(move);
            setRuntimeMissionIntent(state, *vehicle, RuntimeMissionIntent::Moving,
                "repackMoveReady");
        }
        // Other pending yards keep this identity until the real conversion;
        // they never soft-ignore its foundation during the folding interval.
        remapRuntimeRepackMoveGroupMember(state, id, vehicleId);
        changed = true;
    }
    return changed;
}
