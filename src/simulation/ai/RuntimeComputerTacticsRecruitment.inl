int runtimeComputerTacticGroup(const RuntimeWindowState& state, const RuntimeTeamType& type) {
    const auto task = state.triggers.taskForces.find(type.taskForceId);
    return type.group == -1 && task != state.triggers.taskForces.end() ? task->second.group : type.group;
}

bool runtimeComputerTacticMemberPresent(const RuntimeWindowState& state,
    const RuntimeComputerAiState& ai, int id) {
    const auto* member = findRuntimeObjectByIdIncludingInactive(state, id);
    return member && runtimeComputerAiOwnerMatches(*member, ai.owner)
        && (member->alive || member->mission == "InsideTransport"
            || runtimeObjectIsPendingTeamRejoinAfterLimbo(state, id));
}

void refreshRuntimeComputerTacticMissing(RuntimeWindowState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    if (team.launched && (!type.reinforce || team.objectIds.empty())) return;
    const auto task = state.triggers.taskForces.find(type.taskForceId);
    if (task == state.triggers.taskForces.end()) return;
    std::map<std::string, int> counts;
    for (int id : team.objectIds)
        if (const auto* member = findRuntimeObjectByIdIncludingInactive(state, id))
            ++counts[toUpperAsciiLocal(member->type)];
    team.missingMembers.clear();
    for (const auto& member : task->second.members) {
        const auto key = toUpperAsciiLocal(member.unitId);
        for (int i = 0; i < member.count; ++i) {
            if (counts[key] > 0) --counts[key];
            else team.missingMembers.push_back(member.unitId);
        }
    }
}

bool runtimeComputerTacticMayRecruit(const RuntimeWindowState& state,
    const RuntimeComputerAiState& ai, const RuntimeComputerTacticTeam& team,
    const RuntimeTeamType& type, const WorldObjectInstance& object) {
    if (!object.alive || object.health <= 0 || !runtimeComputerAiOwnerMatches(object, ai.owner)
        || object.factoryExitHostId >= 0 || object.garrisonHostObjectId >= 0 || object.tankBunkerHostId >= 0
        || runtimeObjectDisabledByParasite(state, object.instanceId)) return false;
    // Retail's MissionControl forbids recruiting units during entry,
    // unloading, harvesting and other committed interactions.
    const auto mission = toLowerAsciiLocal(object.mission);
    if (mission != "guard" && mission != "areaguard" && mission != "area guard"
        && mission != "move" && mission != "attack" && mission != "secondaryfire" && mission != "dock"
        && mission != "stop" && mission != "patrol" && mission != "hunt") return false;
    if (state.pendingEngineerCaptures.contains(object.instanceId)
        || state.pendingTransportTargets.contains(object.instanceId)
        || state.pendingOccupyTargets.contains(object.instanceId)) return false;
    for (const auto& relation : state.grinding.relations())
        if (relation.sourceId == object.instanceId) return false;
    if (const auto* unit = findRuntimeUnitType(state.rules, object); unit) {
        if (unit->harvester || unit->resourceGatherer
            || (unit->airportBound && (object.airborne || !runtimeObjectHasAttackAmmo(state.rules, object)))) return false;
    }
    bool assigned = false;
    const int group = runtimeComputerTacticGroup(state, type);
    for (const auto& [house, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, existing] : tactics.teams) {
            if (std::find(existing.objectIds.begin(), existing.objectIds.end(), object.instanceId)
                == existing.objectIds.end()) continue;
            assigned = true;
            if (&existing == &team || existing.teamTypeId == team.teamTypeId) return false;
            const auto oldType = state.triggers.teams.find(existing.teamTypeId);
            if (oldType == state.triggers.teams.end() || !oldType->second.recruitable
                || oldType->second.priority >= type.priority) return false;
            // A Recruiter may draw from another authored group. Default
            // skirmish groups are -1; -2 explicitly accepts any group.
            if (!type.recruiter && group != -2 && runtimeComputerTacticGroup(state, oldType->second) != group) return false;
        }
    if (!assigned && !type.recruiter && group != -2 && group != -1) return false;
    return true;
}

bool recruitRuntimeComputerTacticMembers(RuntimeWindowState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    std::map<std::string, int> inProduction;
    for (const auto& order : tactics.production)
        if (order.teamInstanceId == team.instanceId && !order.typeId.empty())
            ++inProduction[toUpperAsciiLocal(order.typeId)];
    bool changed = false;
    for (auto missing = team.missingMembers.begin(); missing != team.missingMembers.end();) {
        const auto key = toUpperAsciiLocal(*missing);
        if (inProduction[key] > 0) { --inProduction[key]; ++missing; continue; }
        int recruitId = -1; double bestDistance = std::numeric_limits<double>::max();
        for (const auto& object : state.world.getObjects()) {
            if (toUpperAsciiLocal(object.type) != key
                || !runtimeComputerTacticMayRecruit(state, ai, team, type, object)) continue;
            const double distance = runtimeCellDistance(ai.baseCenter, runtimeCellPointForObject(object));
            if (distance < bestDistance || (distance == bestDistance && object.instanceId < recruitId)) {
                recruitId = object.instanceId; bestDistance = distance;
            }
        }
        if (recruitId < 0) { ++missing; continue; }
        detachRuntimeObjectFromTeamMembership(state, recruitId);
        eraseRuntimeBehavior(state, recruitId);
        team.objectIds.push_back(recruitId);
        if (team.launched) team.joiningObjectIds.push_back(recruitId);
        if (auto* recruit = findMutableObjectById(state, recruitId)) recruit->mission = "Guard";
        missing = team.missingMembers.erase(missing);
        team.lastProgressTick = state.tick; changed = true;
    }
    return changed;
}

bool joinRuntimeComputerTacticReinforcements(RuntimeWindowState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    bool changed = false;
    for (auto next = team.joiningObjectIds.begin(); next != team.joiningObjectIds.end();) {
        auto* member = findMutableObjectById(state, *next);
        if (!member) { next = team.joiningObjectIds.erase(next); continue; }
        if (member->factoryExitHostId >= 0 || member->mission == "FactoryExit") { ++next; continue; }
        member->veterancy = std::max(member->veterancy, type.veteranLevel - 1);
        const int memberId = member->instanceId;
        RuntimeTeamMissionState companions;
        for (int id : team.objectIds) if (id != memberId) companions.objectIds.push_back(id);
        RuntimeCellPoint destination = runtimeTeamMissionCenter(state, companions);
        bool ordered = false;
        if (auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
            mission != state.triggers.teamMissions.end()) {
            auto& current = mission->second;
            if (std::find(current.objectIds.begin(), current.objectIds.end(), memberId) == current.objectIds.end())
                current.objectIds.push_back(memberId);
            const auto script = state.triggers.scripts.find(current.scriptId);
            const int action = script != state.triggers.scripts.end() && current.stepIndex < script->second.steps.size()
                ? script->second.steps[current.stepIndex].action : -1;
            if (current.stepIssued && (action == 0 || action == 46))
                if (const auto* target = findObjectById(state, current.targetObjectId))
                    ordered = issueRuntimeTeamAttack(state, {memberId}, *target) > 0;
            if (current.targetCell.x >= 0) destination = current.targetCell;
        }
        if (!ordered && destination.x >= 0) (void)issueRuntimeTeamMove(state, {memberId}, destination);
        next = team.joiningObjectIds.erase(next); changed = true;
    }
    return changed;
}
