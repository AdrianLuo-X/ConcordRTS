#include "simulation/ai/RuntimeComputerTacticSelection.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <limits>

namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;

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

std::string runtimeComputerTacticEnemy(const RuntimeComputerTacticSelectionQueries& queries,
    const RuntimeComputerAiState& ai) {
    const WorldObjectInstance* best = nullptr;
    double distance = std::numeric_limits<double>::max();
    for (const auto& object : queries.objects()) {
        if (!object.alive || !queries.hostile(ai.owner, object.owner)) continue;
        const double candidate = queries.cellDistance(ai.baseCenter, queries.objectCell(object))
            + (object.kind == WorldObjectKind::Structure ? 0.0 : 1000.0);
        if (candidate < distance || (candidate == distance && best && object.instanceId < best->instanceId)) {
            best = &object; distance = candidate;
        }
    }
    return best ? best->owner : std::string{};
}

bool runtimeComputerTacticCondition(const RuntimeComputerTacticSelectionQueries& queries, RuntimeComputerTacticSelectionEffects& effects,
    const RuntimeComputerAiState& ai, const RuntimeCampaignAiTriggerDefinition& trigger,
    const std::string& enemy) {
    if (trigger.conditionType == -1) return true;
    const auto compare = [&](int value) {
        return runtimeComputerTacticCompare(value, trigger.conditionNumber, trigger.conditionComparator);
    };
    if (trigger.conditionType == 0 || trigger.conditionType == 1 || trigger.conditionType == 7) {
        int count = 0;
        for (const auto& object : queries.objects()) {
            if (!object.alive || toUpperAsciiLocal(object.type) != toUpperAsciiLocal(trigger.conditionObject)) continue;
            if (trigger.conditionType == 7 ? queries.neutral(object.owner)
                : queries.ownerMatches(object, trigger.conditionType == 1 ? ai.owner : enemy)) ++count;
        }
        return compare(count);
    }
    if (trigger.conditionType == 2 || trigger.conditionType == 3) {
        int power = 0;
        for (const auto& object : queries.objects()) {
            if (!object.alive || !queries.ownerMatches(object, enemy)) continue;
            if (const auto buildingPower = queries.buildingPower(object.type)) power += *buildingPower;
        }
        return !enemy.empty() && (trigger.conditionType == 2 ? power < 0 : power >= 0);
    }
    if (trigger.conditionType == 4) {
        const auto credits = queries.enemyCredits(enemy);
        return credits && compare(*credits);
    }
    if (trigger.conditionType == 5 || trigger.conditionType == 6) {
        if (!queries.superweaponsEnabled()) return false;
        effects.ensureSuperWeaponsInitialized();
        const std::string identity = trigger.conditionType == 5 ? "ironcurtain" : "chrono";
        for (const auto& snapshot : effects.superWeaponSnapshots(ai.owner)) {
            const auto key = toLowerAsciiLocal(snapshot.superWeaponId + " " + snapshot.name);
            if (key.find(identity) != std::string::npos
                && snapshot.state != SuperWeaponRuntimeState::Unavailable
                && snapshot.progress >= queries.rules().minorSuperReadyPercent) return true;
        }
    }
    return false;
}

bool runtimeComputerTacticTemplateBuildable(const RuntimeComputerTacticSelectionQueries& queries,
    const RuntimeComputerAiState& ai, const RuntimeTeamType& team) {
    const auto task = queries.taskForces().find(team.taskForceId);
    const auto script = queries.scripts().find(team.scriptId);
    if (team.max == 0 || task == queries.taskForces().end() || task->second.members.empty()
        || script == queries.scripts().end() || script->second.steps.empty()) return false;
    int size = 0;
    for (const auto& member : task->second.members) {
        const auto item = queries.buildItem(member.unitId, ai.owner);
        if (member.count <= 0 || member.count > 100 || !item
            || item->kind == RuntimeBuildKind::Building || item->kind == RuntimeBuildKind::Defense
            || !queries.itemAllowed(ai, *item)) return false;
        size += member.count;
    }
    return size <= 256;
}

bool selectRuntimeComputerTactic(const RuntimeComputerTacticSelectionQueries& queries, RuntimeComputerTacticSelectionEffects& effects, RuntimeComputerAiState& ai,
    RuntimeComputerTacticsState& tactics) {
    if (queries.tick() < tactics.nextTriggerTick) return false;
    const auto& rules = queries.rules();
    const int difficulty = queries.difficultyIndex(ai.difficulty);
    const int delay = queries.legacyScriptTicks(rules.teamDelays[difficulty]);
    tactics.nextTriggerTick = queries.tick() + delay;
    const std::string enemy = runtimeComputerTacticEnemy(queries, ai);
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
    for (const auto& trigger : queries.triggers()) {
        if (!trigger.valid || !trigger.enabled || !trigger.availableInSkirmish
            || !trigger.difficultyEnabled[2 - difficulty] || trigger.techLevel > queries.techLevel()) continue;
        const auto authoredOwner = toLowerAsciiLocal(trimAsciiLocal(trigger.owner));
        if (authoredOwner != "<all>" && authoredOwner != "all"
            && authoredOwner != toLowerAsciiLocal(ai.country)) continue;
        if (trigger.side > 0 && trigger.side != queries.countrySide(ai.country) + 1) continue;
        const auto history = tactics.history.find(trigger.id);
        const double weight = history == tactics.history.end() ? trigger.currentWeight : history->second.weight;
        if (weight <= 0.0 || !runtimeComputerTacticCondition(queries, effects, ai, trigger, enemy)) continue;
        const auto* primary = queries.teamType( trigger.primaryTeamId);
        const auto secondaryId = toLowerAsciiLocal(trimAsciiLocal(trigger.secondaryTeamId));
        const bool hasSecondary = !secondaryId.empty() && secondaryId != "<none>";
        const auto* secondary = hasSecondary ? queries.teamType( trigger.secondaryTeamId) : nullptr;
        if (!primary || (hasSecondary && !secondary)) continue;
        std::map<std::string, int> addedCounts;
        int addedDefenses = 0, added = 0;
        bool buildable = true;
        for (const auto* team : {primary, secondary}) {
            if (!team) continue;
            // Retail tests both templates against the pre-selection count,
            // then CreateTeam checks Max again for each actual creation.
            if ((team->max >= 0 && templateCounts[team->id] >= team->max)
                || !runtimeComputerTacticTemplateBuildable(queries, ai, *team)) buildable = false;
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
        tactics.nextTriggerTick = queries.tick() + std::min(delay, std::max(1, queries.fixedTickHz() * 2));
        return false;
    }
    const auto random = runtimeStableHashString(ai.owner + ":team:" + std::to_string(++tactics.selectionSerial)
        + ":" + std::to_string(queries.tick()));
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
        const auto* type = queries.teamType(
            secondary ? selected->secondaryTeamId : selected->primaryTeamId);
        if (!type) continue;
        if (type->max >= 0 && templateCounts[type->id] >= type->max) continue;
        ++templateCounts[type->id];
        RuntimeComputerTacticTeam team;
        team.instanceId = tactics.nextTeamInstanceId++;
        team.triggerId = selected->id; team.teamTypeId = type->id; team.targetOwner = enemy;
        team.secondary = secondary; team.baseDefense = type->isBaseDefense || selected->baseDefense;
        team.createdTick = team.lastProgressTick = queries.tick();
        for (const auto& member : queries.taskForces().at(type->taskForceId).members)
            for (int count = 0; count < member.count; ++count) team.missingMembers.push_back(member.unitId);
        tactics.teams.emplace(team.instanceId, std::move(team));
    }
    ai.status = "assembling " + selected->name;
    return true;
}
}
