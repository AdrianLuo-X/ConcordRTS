bool runtimeComputerTacticReservesSuperWeapon(const RuntimeWindowState& state,
    const std::string& owner, const SuperWeaponRule* weapon) {
    if (state.campaignMode || !weapon) return false;
    const auto identity = toLowerAsciiLocal(weapon->id + " " + weapon->type);
    const bool chrono = identity.find("chrono") != std::string::npos;
    const bool iron = identity.find("ironcurtain") != std::string::npos;
    if (!chrono && !iron) return false;
    const auto house = state.triggers.computerTacticsByOwner.find(toLowerAsciiLocal(trimAsciiLocal(owner)));
    if (house == state.triggers.computerTacticsByOwner.end()) return false;
    for (const auto& [id, team] : house->second.teams) {
        if (team.missionFinished) continue;
        const auto type = state.triggers.teams.find(team.teamTypeId);
        if (type == state.triggers.teams.end()) continue;
        const auto script = state.triggers.scripts.find(type->second.scriptId);
        if (script == state.triggers.scripts.end()) continue;
        const auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
        const std::size_t start = mission == state.triggers.teamMissions.end() ? 0 : mission->second.stepIndex;
        for (std::size_t step = start; step < script->second.steps.size(); ++step) {
            const int action = script->second.steps[step].action;
            if ((iron && action == 55) || (chrono && (action == 56 || action == 57))) return true;
        }
    }
    return false;
}
