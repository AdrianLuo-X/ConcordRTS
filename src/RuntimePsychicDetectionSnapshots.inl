    std::vector<RuntimePsychicIntent> psychicIntents;
    psychicIntents.reserve(state.behaviors.size());
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.state == RuntimeBehaviorState::Idle
            || behavior.state == RuntimeBehaviorState::Complete
            || behavior.kind == RuntimeBehaviorKind::None) continue;
        RuntimePsychicIntent intent;
        intent.objectId = id;
        intent.attack = behavior.kind == RuntimeBehaviorKind::Attack;
        if (intent.attack && !behavior.attackGround && !behavior.attackBridge)
            intent.targetObjectId = behavior.targetObjectId;
        const auto cell = intent.attack ? behavior.attackBridgeCell : behavior.moveTarget;
        intent.destination = {cell.x, cell.y};
        psychicIntents.push_back(intent);
    }
    auto psychicSnapshots = runtimePsychicDetectionSnapshots(state.world.getObjects(),
        state.rules, psychicIntents, state.economy.owner,
        [&state](const auto& left, const auto& right) { return runtimeOwnersAreAllied(state, left, right); },
        [&state](const auto& object) {
            return !state.specialMechanisms.isChronoInTransit(object.instanceId)
                && !state.specialMechanisms.isParasiteSourceLimboed(object.instanceId);
        }, true);
    state.specialMechanismRenderSnapshots.insert(state.specialMechanismRenderSnapshots.end(),
        psychicSnapshots.begin(), psychicSnapshots.end());
