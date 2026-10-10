bool runtimeTeamMemberNeedsGuard(const RuntimeWindowState& state, const WorldObjectInstance& member) {
    const auto* type = findRuntimeUnitType(state.rules, member);
    if (!type) return false;
    const int retailSpeed = std::min(255, std::clamp(type->speed, 0, 100) * 256 / 100);
    const auto deploy = toLowerAsciiLocal(trimAsciiLocal(type->deploysInto));
    const bool armed = !type->primaryWeapon.empty()
        || (!type->indexedWeapons.empty() && !type->indexedWeapons.front().empty());
    return !armed || type->techLevel == -1
        || (!deploy.empty() && deploy != "<none>") || retailSpeed <= 13;
}

bool runtimeTeamGuardsSlower(const RuntimeWindowState& state, const RuntimeTeamMissionState& mission) {
    const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
    return type && type->guardSlower;
}

double runtimeTeamMemberStray(const RuntimeWindowState& state, const RuntimeTeamMissionState& mission,
    const WorldObjectInstance& member, int action) {
    const auto& rules = state.rules.getComputerTacticsRules();
    double radius = action == 53 || action == 54 ? rules.relaxedStray : rules.stray;
    const auto& aircraft = state.rules.getAircraftTypes();
    if (std::any_of(aircraft.begin(), aircraft.end(), [&](const auto& type) {
            return toUpperAsciiLocal(type.id) == toUpperAsciiLocal(member.type); })) radius *= 3.0;
    if (runtimeTeamGuardsSlower(state, mission) && !runtimeTeamMemberNeedsGuard(state, member)) radius /= 3.0;
    return radius;
}

bool runtimeTeamGuardHasStrength(const RuntimeWindowState& state, const RuntimeTeamMissionState& mission) {
    const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
    if (!type || !type->guardSlower) return false;
    const auto task = state.triggers.taskForces.find(type->taskForceId);
    int expected = 0, present = 0;
    if (task != state.triggers.taskForces.end())
        for (const auto& member : task->second.members) expected += std::max(0, member.count);
    for (int id : mission.objectIds)
        if (const auto* member = findRuntimeObjectByIdIncludingInactive(state, id);
            member && (member->alive || member->mission == "InsideTransport")) ++present;
    return expected <= 2 ? present >= expected : present > expected / 3;
}
