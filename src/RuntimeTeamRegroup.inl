void noteRuntimeTeamMemberDamaged(RuntimeWindowState& state, int memberId, int attackerId) {
    if (attackerId < 0) return;
    for (auto& [id, mission] : state.triggers.teamMissions) {
        if (mission.completed || mission.regrouping
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), memberId) == mission.objectIds.end()
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), attackerId) != mission.objectIds.end()) continue;
        const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
        if (!type || type->suicide || mission.targetObjectId == attackerId) continue;
        const auto script = state.triggers.scripts.find(mission.scriptId);
        const int action = script != state.triggers.scripts.end() && mission.stepIndex < script->second.steps.size()
            ? script->second.steps[mission.stepIndex].action : -1;
        // Retail reforms while assembling or for an aggressive armed team.
        // The attacker is not promoted to the script's strategic target.
        if (mission.stepIssued && action != 53 && action != 54) {
            if (!type->aggressive) continue;
            const auto* leader = mission.objectIds.empty() ? nullptr : findObjectById(state, mission.objectIds.front());
            if (!leader || !runtimeWeaponProfileForObject(state, *leader).weapon) continue;
            const auto& aircraft = state.rules.getAircraftTypes();
            if (std::any_of(aircraft.begin(), aircraft.end(), [&](const auto& unit) {
                    return toUpperAsciiLocal(unit.id) == toUpperAsciiLocal(leader->type); })) continue;
        }
        // A committed entry or superweapon instruction must finish through
        // its own relationship before the squad can receive another move.
        if (action == 8 || action == 43 || action == 55 || action == 56 || action == 57
            || action == 60 || action == 62) continue;
        mission.regrouping = true;
        mission.regroupCell = {-1, -1};
        mission.regroupDestinations.clear();
        mission.regroupStartedTick = state.tick;
    }
}

bool advanceRuntimeTeamRegroup(RuntimeWindowState& state, RuntimeTeamMissionState& mission) {
    const auto live = runtimeLiveTeamObjectIds(state, mission);
    if (live.empty()) return false;
    const auto script = state.triggers.scripts.find(mission.scriptId);
    if (script == state.triggers.scripts.end() || mission.stepIndex >= script->second.steps.size()) return false;
    const int action = script->second.steps[mission.stepIndex].action;
    const bool marching = action == 0 || action == 1 || action == 3 || action == 4 || action == 16
        || action == 46 || action == 47 || action == 48 || action == 58 || action == 59;
    if (!mission.regrouping && mission.stepIssued && marching && runtimeTeamGuardHasStrength(state, mission)) {
        std::vector<const WorldObjectInstance*> supported;
        for (int id : live) if (const auto* member = findObjectById(state, id);
            member && runtimeTeamMemberNeedsGuard(state, *member)) supported.push_back(member);
        for (int id : live) {
            const auto* member = findObjectById(state, id);
            if (!member || supported.empty() || runtimeTeamMemberNeedsGuard(state, *member)) continue;
            const auto behavior = state.behaviors.find(id);
            if (behavior == state.behaviors.end() || behavior->second.path.empty()) continue;
            // World movement commits whole cells. Leave enough space for
            // occupied neighboring cells while keeping escorts with support.
            const double radius = std::max(2.0, 3.0 * runtimeTeamMemberStray(state, mission, *member, action));
            auto destination = mission.targetCell;
            if (const auto* target = findObjectById(state, mission.targetObjectId))
                destination = runtimeCellPointForObject(*target);
            if (destination.x < 0) continue;
            const auto memberCell = runtimeCellPointForObject(*member);
            const double remaining = runtimeCellDistance(memberCell, destination);
            const bool outranSupport = std::any_of(supported.begin(), supported.end(), [&](const auto* support) {
                const auto supportCell = runtimeCellPointForObject(*support);
                return runtimeCellDistance(memberCell, supportCell) > radius
                    && remaining + radius < runtimeCellDistance(supportCell, destination);
            });
            // An escort that is behind its support should catch up along
            // the current route, rather than pull the whole group backwards.
            if (outranSupport) {
                mission.regrouping = true; mission.regroupStartedTick = state.tick; break;
            }
        }
    }
    if (!mission.regrouping) return false;
    if (mission.regroupCell.x < 0) {
        mission.regroupCell = runtimeTeamMissionCenter(state, mission);
        std::unordered_set<int> reserved;
        // Place protected support first, then escorts around it. Every
        // assigned destination is a real passable cell, saved mid-regroup.
        auto members = live;
        if (runtimeTeamGuardsSlower(state, mission)) std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
            return runtimeTeamMemberNeedsGuard(state, *findObjectById(state, a))
                > runtimeTeamMemberNeedsGuard(state, *findObjectById(state, b));
        });
        for (int id : members) {
            const auto* member = findObjectById(state, id);
            if (!member || state.pendingTransportTargets.contains(id) || state.pendingEngineerCaptures.contains(id)
                || state.pendingOccupyTargets.contains(id) || member->factoryExitHostId >= 0) continue;
            const auto target = findRuntimePassableCellNear(state, *member, mission.regroupCell, 6, -1, &reserved);
            if (!target) continue;
            reserved.insert(runtimeCellKey(target->x, target->y));
            mission.regroupDestinations[id] = *target;
        }
    }
    bool arrived = true;
    for (const auto& [id, cell] : mission.regroupDestinations) {
        const auto* member = findObjectById(state, id);
        if (!member) continue;
        const bool atDestination = member->x == cell.x && member->y == cell.y;
        if (atDestination) {
            const auto moving = state.behaviors.find(id);
            if (moving != state.behaviors.end() && moving->second.kind == RuntimeBehaviorKind::Move)
                eraseRuntimeBehavior(state, id);
            continue;
        }
        arrived = false;
        const auto behavior = state.behaviors.find(id);
        if (behavior == state.behaviors.end() || behavior->second.kind != RuntimeBehaviorKind::Move
            || behavior->second.moveTarget.x != cell.x || behavior->second.moveTarget.y != cell.y)
            (void)issueRuntimeTeamMove(state, {id}, cell);
    }
    if (arrived) {
        mission.regrouping = false; mission.regroupCell = {-1, -1};
        mission.regroupDestinations.clear(); mission.regroupStartedTick = -1;
        mission.stepIssued = false;
        return false;
    }
    mission.status = "team regrouping";
    return true;
}
