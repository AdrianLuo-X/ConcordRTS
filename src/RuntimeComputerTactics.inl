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

std::string runtimeComputerTacticEnemy(const RuntimeWindowState& state,
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

bool runtimeComputerTacticCondition(RuntimeWindowState& state,
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

bool runtimeComputerTacticTemplateBuildable(const RuntimeWindowState& state,
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

void reportRuntimeComputerTacticOutcome(RuntimeWindowState& state,
    RuntimeComputerTacticsState& tactics, RuntimeComputerTacticTeam& team) {
    if (team.secondary || team.outcomeReported) return;
    team.outcomeReported = true;
    const auto trigger = std::find_if(state.triggers.computerAiTriggers.begin(),
        state.triggers.computerAiTriggers.end(), [&](const auto& item) { return item.id == team.triggerId; });
    if (trigger == state.triggers.computerAiTriggers.end()) return;
    auto [entry, inserted] = tactics.history.try_emplace(team.triggerId);
    auto& history = entry->second;
    if (inserted) history.weight = trigger->currentWeight;
    const auto& rules = state.rules.getComputerTacticsRules();
    // Retail applies the prior win/loss record, then records this outcome.
    const double track = history.successes - 0.5 * (history.successes + history.failures);
    const double delta = team.achievedGreatSuccess
        ? rules.successWeightDelta + std::max(0.0, track)
        : rules.failureWeightDelta + std::min(0.0, track * rules.trackRecordCoefficient);
    history.weight = std::clamp(history.weight + delta, trigger->minimumWeight, trigger->maximumWeight);
    if (team.achievedGreatSuccess) ++history.successes;
    else ++history.failures;
}

bool selectRuntimeComputerTactic(RuntimeWindowState& state, RuntimeComputerAiState& ai,
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

#include "RuntimeComputerTacticsRecruitment.inl"

bool advanceRuntimeComputerTactics(RuntimeWindowState& state, RuntimeComputerAiState& ai) {
    if (state.triggers.computerAiTriggers.empty()) return false;
    auto& tactics = state.triggers.computerTacticsByOwner[toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    bool changed = advanceRuntimeComputerTacticProduction(state, ai, tactics);
    // World scans and team selection need not run at the production tick rate.
    if (state.tick < ai.nextThinkTick) return changed;
    const auto& rules = state.rules.getComputerTacticsRules();
    std::vector<int> finished;
    for (auto& [id, team] : tactics.teams) {
        const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId);
        if (!type) { finished.push_back(id); continue; }
        for (auto member = team.objectIds.begin(); member != team.objectIds.end();) {
            if (runtimeComputerTacticMemberPresent(state, ai, *member)) { ++member; continue; }
            std::erase(team.joiningObjectIds, *member);
            member = team.objectIds.erase(member); changed = true;
        }
        if (team.launched) {
            const auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
            if (mission != state.triggers.teamMissions.end())
                team.achievedGreatSuccess |= mission->second.achievedGreatSuccess;
            if (team.missionFinished && (!team.baseDefense || team.objectIds.empty())) {
                finished.push_back(id); continue;
            }
            changed = joinRuntimeComputerTacticReinforcements(state, team, *type) || changed;
            if (!type->reinforce || team.objectIds.empty()) continue;
        }
        const auto previousMissing = team.missingMembers;
        refreshRuntimeComputerTacticMissing(state, team, *type);
        changed = previousMissing != team.missingMembers || changed;
        changed = recruitRuntimeComputerTacticMembers(state, ai, tactics, team, *type) || changed;
        if (team.launched) continue;
        if (team.missingMembers.empty() && !team.objectIds.empty()) {
            bool exiting = false;
            for (int objectId : team.objectIds) {
                const auto* object = findObjectById(state, objectId);
                if (object && (object->factoryExitHostId >= 0 || object->mission == "FactoryExit")) exiting = true;
            }
            if (!exiting) {
                team.launched = true;
                team.missionInstanceId = state.triggers.nextTeamMissionInstanceId;
                auto launchType = *type; launchType.house = ai.owner;
                for (int objectId : team.objectIds)
                    if (auto* object = findMutableObjectById(state, objectId))
                        object->veterancy = std::max(object->veterancy, type->veteranLevel - 1);
                issueRuntimeTriggerTeamOrder(state, launchType, team.objectIds, team.triggerId, team.instanceId);
                ++ai.attackWaves; ai.attacksOrdered += static_cast<int>(team.objectIds.size());
                ai.status = "launched " + type->name; changed = true;
            }
        } else if (state.tick - team.lastProgressTick
            > runtimeLegacyScriptTicks(state, rules.dissolveUnfilledTeamDelay)) finished.push_back(id);
    }
    for (int id : finished) {
        auto found = tactics.teams.find(id);
        if (found == tactics.teams.end()) continue;
        reportRuntimeComputerTacticOutcome(state, tactics, found->second);
        // A paid unit already in progress completes as a free-to-recruit unit.
        for (auto& order : tactics.production) if (order.teamInstanceId == id) order.teamInstanceId = -1;
        state.triggers.teamMissions.erase(found->second.missionInstanceId);
        tactics.teams.erase(found); changed = true;
    }
    changed = selectRuntimeComputerTactic(state, ai, tactics) || changed;
    // New teams first recruit available members; do not start an unnecessary
    // paid unit merely because selection follows the maintenance pass.
    for (auto& [id, team] : tactics.teams) if (team.createdTick == state.tick && !team.launched) {
        if (const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId))
            changed = recruitRuntimeComputerTacticMembers(state, ai, tactics, team, *type) || changed;
    }

    // Economic recovery shares the same factory queue and takes the next free
    // slot before optional combat members. It never interrupts a paid order.
    const auto metrics = runtimeComputerAiMetrics(state, ai);
    auto economic = runtimeComputerAiSelectNeededHarvester(state, ai, metrics);
    if (!economic && runtimeIsMegawealthMode(state) && runtimeComputerAiNeedsOilEngineer(state, ai))
        economic = runtimeComputerAiSelectUnitForRole(state, ai, RuntimeComputerAiUnitRole::Engineer);
    if (economic) changed = queueRuntimeComputerTacticProduction(state, ai, *economic) || changed;
    std::vector<int> priority;
    for (const auto& [id, team] : tactics.teams) if (!team.missingMembers.empty()) priority.push_back(id);
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    const auto random = runtimeStableHashString(ai.owner + ":fill:" + std::to_string(state.tick));
    if (!priority.empty() && random % 100 >= static_cast<unsigned>(rules.fillEarliestTeamProbability[difficulty]))
        std::rotate(priority.begin(), priority.begin() + random % priority.size(), priority.end());
    for (int id : priority) {
        auto& team = tactics.teams.at(id);
        for (const auto& missing : team.missingMembers) {
            const auto item = runtimeComputerAiBuildItem(state, missing, ai.owner);
            if (item) changed = queueRuntimeComputerTacticProduction(state, ai, *item, id) || changed;
        }
    }
    return changed;
}
