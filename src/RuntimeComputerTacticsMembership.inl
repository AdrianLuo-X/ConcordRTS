bool runtimeComputerTacticReservesObject(const RuntimeWindowState& state, int objectId) {
    if (state.pendingTransportTargets.contains(objectId) || state.pendingEngineerCaptures.contains(objectId)
        || state.pendingOccupyTargets.contains(objectId)) return true;
    for (const auto& relation : state.grinding.relations())
        if (relation.sourceId == objectId) return true;
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, team] : tactics.teams)
            if (std::find(team.objectIds.begin(), team.objectIds.end(), objectId)
                != team.objectIds.end()) return true;
    return false;
}

bool finishRuntimeComputerTacticMission(RuntimeWindowState& state,
    const RuntimeTeamMissionState& mission) {
    for (auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (auto& [id, team] : tactics.teams)
            if (team.missionInstanceId == mission.instanceId) {
                team.missionFinished = true;
                team.achievedGreatSuccess |= mission.achievedGreatSuccess;
                return true;
            }
    return false;
}

std::string runtimeComputerTacticTargetOwner(const RuntimeWindowState& state,
    const RuntimeTeamMissionState& mission) {
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, team] : tactics.teams)
            if (team.missionInstanceId == mission.instanceId) {
                const auto type = state.triggers.teams.find(team.teamTypeId);
                return type != state.triggers.teams.end() && type->second.onlyTargetHouseEnemy
                    ? team.targetOwner : std::string{};
            }
    return {};
}
