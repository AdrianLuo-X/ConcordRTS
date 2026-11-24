// Exact five pre-H005 policy bodies from Scenario.cpp BB90A60D055BE85024220F07FD290416CEAE8540F194DCF8DB8558992F4ED814.
// Test reference only; production never includes this file.
bool runtimeComputerTacticCompare(int actual, int expected, int comparator) {
    switch (comparator) {
    case 0: return actual < expected;
    case 1: return actual <= expected;
    case 2: return actual == expected;
    case 3: return actual >= expected;
    case 4: return actual > expected;
    case 5: return actual != expected;
    default: return false;
    }
}

std::string runtimeComputerTacticEnemy(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai) {
    const WorldObjectInstance* best = nullptr;
    double distance = std::numeric_limits<double>::max();
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !runtimeOwnersAreHostileForAutoTarget(state, ai.owner, object.owner)) continue;
        const double candidate = runtimeCellDistance(ai.baseCenter, runtimeCellPointForObject(object))
            + (object.kind == WorldObjectKind::Structure ? 0.0 : 1000.0);
        if (candidate < distance || (candidate == distance && best && object.instanceId < best->instanceId)) {
            best = &object; distance = candidate;
        }
    }
    return best ? best->owner : std::string{};
}

bool runtimeComputerTacticCondition(RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeCampaignAiTriggerDefinition& trigger,
    const std::string& enemy) {
    if (trigger.conditionType == -1) return true;
    const auto compare = [&](int value) {
        return runtimeComputerTacticCompare(value, trigger.conditionNumber, trigger.conditionComparator);
    };
    if (trigger.conditionType == 0 || trigger.conditionType == 1 || trigger.conditionType == 7) {
        int count = 0;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || toUpperAsciiLocal(object.type) != toUpperAsciiLocal(trigger.conditionObject)) continue;
            if (trigger.conditionType == 7 ? runtimeOwnerIsNeutralForAutoTarget(object.owner)
                : runtimeComputerAiOwnerMatches(object, trigger.conditionType == 1 ? ai.owner : enemy)) ++count;
        }
        return compare(count);
    }
    if (trigger.conditionType == 2 || trigger.conditionType == 3) {
        int power = 0;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || !runtimeComputerAiOwnerMatches(object, enemy)) continue;
            if (const auto* building = findRuntimeBuildingTypeById(state.rules, object.type)) power += building->power;
        }
        return !enemy.empty() && (trigger.conditionType == 2 ? power < 0 : power >= 0);
    }
    if (trigger.conditionType == 4) {
        const auto other = state.computerAiByOwner.find(toLowerAsciiLocal(trimAsciiLocal(enemy)));
        if (other != state.computerAiByOwner.end()) return compare(other->second.credits);
        // Human houses are independently authoritative, including remote peers.
        for (const auto& authority : state.humanHouseAuthorities) {
            if (runtimeCanonicalHumanHouseOwner(enemy) != authority.canonicalOwner) continue;
            if (authority.localFacade) return compare(state.economy.credits);
            return authority.remoteEconomy && compare(authority.remoteEconomy->credits);
        }
        return enemy == state.economy.owner && compare(state.economy.credits);
    }
    if (trigger.conditionType == 5 || trigger.conditionType == 6) {
        if (!state.config.superweapons) return false;
        ensureRuntimeSuperWeaponsInitialized(state);
        const std::string identity = trigger.conditionType == 5 ? "ironcurtain" : "chrono";
        for (const auto& snapshot : state.superWeapons.sidebarSnapshots(ai.owner)) {
            const auto key = toLowerAsciiLocal(snapshot.superWeaponId + " " + snapshot.name);
            if (key.find(identity) != std::string::npos
                && snapshot.state != SuperWeaponRuntimeState::Unavailable
                && snapshot.progress >= state.rules.getComputerTacticsRules().minorSuperReadyPercent) return true;
        }
    }
    return false;
}

bool runtimeComputerTacticTemplateBuildable(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeTeamType& team) {
    const auto task = state.triggers.taskForces.find(team.taskForceId);
    const auto script = state.triggers.scripts.find(team.scriptId);
    if (team.max == 0 || task == state.triggers.taskForces.end() || task->second.members.empty()
        || script == state.triggers.scripts.end() || script->second.steps.empty()) return false;
    int size = 0;
    for (const auto& member : task->second.members) {
        const auto item = runtimeComputerAiBuildItem(state, member.unitId, ai.owner);
        if (member.count <= 0 || member.count > 100 || !item
            || item->kind == RuntimeBuildKind::Building || item->kind == RuntimeBuildKind::Defense
            || !runtimeComputerAiItemAllowed(state, ai, *item)) return false;
        size += member.count;
    }
    return size <= 256;
}

bool selectRuntimeComputerTactic(RuntimeGameSessionState& state, RuntimeComputerAiState& ai,
    RuntimeComputerTacticsState& tactics) {
    if (state.tick < tactics.nextTriggerTick) return false;
    const auto& rules = state.rules.getComputerTacticsRules();
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    const int delay = runtimeLegacyScriptTicks(state, rules.teamDelays[difficulty]);
    tactics.nextTriggerTick = state.tick + delay;
    const std::string enemy = runtimeComputerTacticEnemy(state, ai);
    if (enemy.empty()) return false;
    std::map<std::string, int> templateCounts;
    int defenses = 0;
    for (const auto& [id, team] : tactics.teams) {
        ++templateCounts[team.teamTypeId];
        if (team.baseDefense) ++defenses;
    }
    const bool needDefense = rules.useMinimumDefenseRule && defenses < rules.minimumDefensiveTeams[difficulty];
    struct Choice { const RuntimeCampaignAiTriggerDefinition* trigger; double weight; bool defense; };
    std::vector<Choice> choices;
    bool defenseAvailable = false;
    for (const auto& trigger : state.triggers.computerAiTriggers) {
        if (!trigger.valid || !trigger.enabled || !trigger.availableInSkirmish
            || !trigger.difficultyEnabled[2 - difficulty] || trigger.techLevel > state.economy.techLevel) continue;
        const auto authoredOwner = toLowerAsciiLocal(trimAsciiLocal(trigger.owner));
        if (authoredOwner != "<all>" && authoredOwner != "all"
            && authoredOwner != toLowerAsciiLocal(ai.country)) continue;
        if (trigger.side > 0 && trigger.side != runtimeBasePlanningSideForCountry(ai.country) + 1) continue;
        const auto history = tactics.history.find(trigger.id);
        const double weight = history == tactics.history.end() ? trigger.currentWeight : history->second.weight;
        if (weight <= 0.0 || !runtimeComputerTacticCondition(state, ai, trigger, enemy)) continue;
        const auto* primary = findRuntimeTriggerTeamById(state, trigger.primaryTeamId);
        const auto secondaryId = toLowerAsciiLocal(trimAsciiLocal(trigger.secondaryTeamId));
        const bool hasSecondary = !secondaryId.empty() && secondaryId != "<none>";
        const auto* secondary = hasSecondary ? findRuntimeTriggerTeamById(state, trigger.secondaryTeamId) : nullptr;
        if (!primary || (hasSecondary && !secondary)) continue;
        std::map<std::string, int> addedCounts;
        int addedDefenses = 0, added = 0;
        bool buildable = true;
        for (const auto* team : {primary, secondary}) {
            if (!team) continue;
            // Retail tests both templates against the pre-selection count,
            // then CreateTeam checks Max again for each actual creation.
            if ((team->max >= 0 && templateCounts[team->id] >= team->max)
                || !runtimeComputerTacticTemplateBuildable(state, ai, *team)) buildable = false;
            if (team->max >= 0 && templateCounts[team->id] + addedCounts[team->id] >= team->max) continue;
            ++addedCounts[team->id];
            ++added;
            if (team->isBaseDefense || trigger.baseDefense) ++addedDefenses;
        }
        if (!buildable || static_cast<int>(tactics.teams.size()) + added > rules.totalTeamCap[difficulty]
            || defenses + addedDefenses > rules.maximumDefensiveTeams[difficulty]) continue;
        defenseAvailable |= addedDefenses > 0;
        choices.push_back({&trigger, weight, addedDefenses > 0});
    }
    double total = 0.0;
    for (const auto& choice : choices)
        if (!needDefense || !defenseAvailable || choice.defense) total += choice.weight;
    if (total <= 0.0) {
        // Retry prerequisites during an opening, while preserving the authored
        // interval between successful selections.
        tactics.nextTriggerTick = state.tick + std::min(delay, std::max(1, state.config.fixedTickHz * 2));
        return false;
    }
    const auto random = runtimeStableHashString(ai.owner + ":team:" + std::to_string(++tactics.selectionSerial)
        + ":" + std::to_string(state.tick));
    double roll = static_cast<double>(random % 1000000) / 1000000.0 * total;
    const RuntimeCampaignAiTriggerDefinition* selected = nullptr;
    for (const auto& choice : choices) {
        if (needDefense && defenseAvailable && !choice.defense) continue;
        selected = choice.trigger;
        roll -= choice.weight;
        if (roll < 0.0) break;
    }
    if (!selected) return false;
    auto [record, inserted] = tactics.history.try_emplace(selected->id);
    if (inserted) record->second.weight = selected->currentWeight;
    ++record->second.executions;
    for (bool secondary : {false, true}) {
        const auto* type = findRuntimeTriggerTeamById(state,
            secondary ? selected->secondaryTeamId : selected->primaryTeamId);
        if (!type) continue;
        if (type->max >= 0 && templateCounts[type->id] >= type->max) continue;
        ++templateCounts[type->id];
        RuntimeComputerTacticTeam team;
        team.instanceId = tactics.nextTeamInstanceId++;
        team.triggerId = selected->id; team.teamTypeId = type->id; team.targetOwner = enemy;
        team.secondary = secondary; team.baseDefense = type->isBaseDefense || selected->baseDefense;
        team.createdTick = team.lastProgressTick = state.tick;
        for (const auto& member : state.triggers.taskForces.at(type->taskForceId).members)
            for (int count = 0; count < member.count; ++count) team.missingMembers.push_back(member.unitId);
        tactics.teams.emplace(team.instanceId, std::move(team));
    }
    ai.status = "assembling " + selected->name;
    return true;
}
