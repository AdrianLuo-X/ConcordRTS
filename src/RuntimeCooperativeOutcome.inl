void synchronizeRuntimeCooperativeOutcome(RuntimeWindowState& state) {
    if (!state.cooperativeSession.active()) return;
    const auto status = state.cooperativeSession.status();
    state.cooperativeSessionStatus = runtimeCooperativeSessionStatusName(status);
    state.multiplayerDefeatedOwners.clear();
    if (state.cooperativeSession.hasOutcomeSnapshot()) {
        const auto& snapshot = state.cooperativeSession.lastOutcomeSnapshot();
        for (std::size_t index = 0; index < runtimeCooperativeSlotCount; ++index)
            if (snapshot.liveTechnos[index] == 0)
                state.multiplayerDefeatedOwners.insert(runtimeCanonicalHumanHouseOwner(
                    state.cooperativeSession.slots()[index].owner));
    }
    if (status == RuntimeCooperativeSessionStatus::Running) return;
    state.multiplayerWinningOwners.clear();
    state.multiplayerWinningTeams.clear();
    const int winningCoalition = status == RuntimeCooperativeSessionStatus::PlayerCoalitionVictory ? 0
        : (status == RuntimeCooperativeSessionStatus::EnemyCoalitionVictory ? 1 : -1);
    for (const auto& slot : state.cooperativeSession.slots()) {
        if (slot.coalitionId != winningCoalition) continue;
        state.multiplayerWinningOwners.insert(runtimeCanonicalHumanHouseOwner(slot.owner));
        state.multiplayerWinningTeams.insert(std::to_string(slot.coalitionId + 1));
    }
    state.skirmishMatchFinished = true;
    state.skirmishVictory = winningCoalition == 0;
    state.skirmishResult = winningCoalition < 0 ? "DRAW"
        : (state.skirmishVictory ? "VICTORY" : "DEFEAT");
    state.economy.skirmishStatus = state.skirmishResult + " (cooperative)";
    state.economy.status = state.economy.skirmishStatus;
    state.interactionStatus = state.economy.skirmishStatus;
}

bool advanceRuntimeCooperativeOutcome(RuntimeWindowState& state) {
    if (!state.cooperativeSession.active() || state.skirmishMatchFinished
        || !state.economy.skirmishStartApplied || state.tick < 5 || state.tick % 10 != 0)
        return false;
    RuntimeCooperativeLiveTechnoSnapshot snapshot;
    snapshot.tick = state.tick;
    for (const auto& object : state.world.getObjects()) {
        if (!runtimeSkirmishObjectSurvives(state, object)) continue;
        const auto slot = runtimeCooperativeSlotForOwner(state, object.owner);
        if (slot) ++snapshot.liveTechnos[static_cast<std::size_t>(*slot)];
    }
    const auto result = state.cooperativeSession.advanceOutcome(snapshot);
    if (!result.accepted) return false;
    synchronizeRuntimeCooperativeOutcome(state);
    if (result.changed) {
        for (auto& object : state.world.accessObjects())
            if (object.renderMoveActive) clearRuntimeRenderPosition(object);
        state.behaviors.clear();
    }
    return result.changed;
}
