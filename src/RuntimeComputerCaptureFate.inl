int runtimeComputerCaptureChoice(const ComputerTacticsRules& rules, int credits,
    bool lowPower, double healthRatio, unsigned roll, int teamOverride) {
    if (teamOverride >= 1 && teamOverride <= 5) return teamOverride;
    const auto& weights = credits < rules.captureLowMoneyMark ? rules.captureLowMoney
        : lowPower ? rules.captureLowPower
        : healthRatio < rules.captureWoundedMark ? rules.captureWounded : rules.captureNormal;
    int remaining = static_cast<int>(roll % 100) + 1;
    for (int index = 0; index < 4; ++index) {
        remaining -= weights[index];
        if (remaining <= 0) return index + 1;
    }
    return 5;
}

bool advanceRuntimeComputerCaptureFates(RuntimeWindowState& state, const std::string& ownerKey) {
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    const auto house = state.computerAiByOwner.find(ownerKey);
    if (house == state.computerAiByOwner.end() || !house->second.initialized
        || state.tick < house->second.nextThinkTick) return false;
    const std::string owner = house->second.owner;
    std::vector<MindControlLinkState> captures;
    for (const auto& [target, link] : state.specialMechanisms.mindControlLinks())
        if (toLowerAsciiLocal(trimAsciiLocal(link.controllerOwner)) == ownerKey) captures.push_back(link);
    bool changed = false;
    auto& decisions = state.triggers.computerTacticsByOwner[ownerKey].captureDecisions;
    changed |= std::erase_if(decisions, [&](const auto& entry) {
        const auto link = state.specialMechanisms.mindControlLinks().find(entry.first);
        return link == state.specialMechanisms.mindControlLinks().end()
            || link->second.controllerObjectId != entry.second.controllerId
            || link->second.startedTick != entry.second.startedTick
            || toLowerAsciiLocal(trimAsciiLocal(link->second.controllerOwner)) != ownerKey;
    }) > 0;
    // A grinder command commits the world and house state. Work from copied
    // links and reacquire every reference after each command.
    for (const auto& link : captures) {
        const auto* target = findObjectById(state, link.targetObjectId);
        const auto* controller = findObjectById(state, link.controllerObjectId);
        if (!target || !controller || !runtimeComputerAiOwnerMatches(*target, owner)
            || target->kind == WorldObjectKind::Structure) continue;
        auto& tactics = state.triggers.computerTacticsByOwner[ownerKey];
        if (tactics.captureDecisions.contains(link.targetObjectId)) continue;
        int teamId = -1, overrideDecision = 0;
        for (const auto& [id, team] : tactics.teams)
            if (std::find(team.objectIds.begin(), team.objectIds.end(), link.controllerObjectId) != team.objectIds.end()) {
                teamId = id;
                if (const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId))
                    overrideDecision = type->mindControlDecision;
                break;
            }
        const auto& ai = state.computerAiByOwner.at(ownerKey);
        const auto metrics = runtimeComputerAiMetrics(state, ai);
        const auto random = runtimeStableHashString(owner + ":capture:" + std::to_string(link.controllerObjectId)
            + ":" + std::to_string(link.targetObjectId) + ":" + std::to_string(link.startedTick));
        int decision = runtimeComputerCaptureChoice(state.rules.getComputerTacticsRules(), ai.credits,
            metrics.powerProduced < metrics.powerUsed,
            static_cast<double>(target->health) / std::max(1, runtimeMaxHealthForObject(state, *target)),
            static_cast<unsigned>(random % 100), overrideDecision);
        tactics.captureDecisions[link.targetObjectId] = {link.controllerObjectId, link.startedTick, decision};
        changed = true;
        if (decision == 5) continue;
        detachRuntimeObjectFromTeamMembership(state, link.targetObjectId);
        eraseRuntimeBehavior(state, link.targetObjectId);
        if (decision == 1 && teamId >= 0) {
            auto& team = tactics.teams.at(teamId);
            if (team.objectIds.size() < 256 && !team.missionFinished) {
                team.objectIds.push_back(link.targetObjectId);
                if (team.launched) team.joiningObjectIds.push_back(link.targetObjectId);
                if (auto* member = findMutableObjectById(state, link.targetObjectId)) member->mission = "Guard";
                continue;
            }
        }
        if (decision == 2 || decision == 3) {
            std::vector<std::pair<double, int>> destinations;
            for (const auto& building : state.world.getObjects()) {
                if (!building.alive || !runtimeComputerAiOwnerMatches(building, owner)
                    || building.rubble || building.buildAnimTicksRemaining > 0) continue;
                const auto* type = findRuntimeBuildingTypeById(state.rules, building.type);
                if (!type) continue;
                if (decision == 2 ? type->grinding && building.poweredOperational
                    : type->infantryAbsorb && target->kind == WorldObjectKind::Infantry
                        && static_cast<int>(building.transportPassengerIds.size())
                            + runtimeTransportReservedPassengerCount(state, building.instanceId) < type->passengers)
                    destinations.emplace_back(runtimeCellDistance(runtimeCellPointForObject(*target),
                        runtimeCellPointForObject(building)), building.instanceId);
            }
            std::sort(destinations.begin(), destinations.end());
            bool entered = false;
            for (const auto& [distance, hostId] : destinations) {
                if (decision == 2) {
                    entered = runtimeGrindingTeamEligibility(state, hostId, link.targetObjectId)
                            == RuntimeGrindingTeamEligibility::Accepted
                        && queueRuntimeGrindingEntry(state, hostId, link.targetObjectId, false);
                } else if (const auto* host = findObjectById(state, hostId)) {
                    const auto selection = state.selectedObjectIds;
                    const bool audio = state.runtimeSuppressPlayerAudioFeedback;
                    state.selectedObjectIds = {link.targetObjectId}; state.runtimeSuppressPlayerAudioFeedback = true;
                    (void)queueRuntimeTransportTarget(state, *host);
                    entered = state.pendingTransportTargets.contains(link.targetObjectId);
                    state.selectedObjectIds = selection; state.runtimeSuppressPlayerAudioFeedback = audio;
                }
                if (entered) break;
            }
            if (entered) continue;
        }
        // Retail falls back to Hunt if joining, recycling or absorption is
        // impossible; it does not remove a captured unit at a distance.
        RuntimeTeamMissionState hunting; hunting.objectIds = {link.targetObjectId};
        if (const auto* enemy = runtimeTeamEnemyTarget(state, hunting, 1))
            (void)issueRuntimeTeamAttack(state, hunting.objectIds, *enemy);
        else if (auto* member = findMutableObjectById(state, link.targetObjectId)) member->mission = "Hunt";
    }
    return changed;
}
