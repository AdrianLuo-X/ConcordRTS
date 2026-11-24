#include "simulation/ai/RuntimeComputerTacticLifecycle.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "data/rules/RulesDatabase.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <map>

namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;
void reportRuntimeComputerTacticOutcome(const RuntimeComputerTacticLifecycleState& state, const RuntimeComputerTacticLifecycleQueries& queries,
    RuntimeComputerTacticsState& tactics, RuntimeComputerTacticTeam& team) {
    if (team.secondary || team.outcomeReported) return;
    team.outcomeReported = true;
    const auto trigger = std::find_if(state.computerAiTriggers.begin(),
        state.computerAiTriggers.end(), [&](const auto& item) { return item.id == team.triggerId; });
    if (trigger == state.computerAiTriggers.end()) return;
    auto [entry, inserted] = tactics.history.try_emplace(team.triggerId);
    auto& history = entry->second;
    if (inserted) history.weight = trigger->currentWeight;
    const auto& rules = queries.rules();
    // Retail applies the prior win/loss record, then records this outcome.
    const double track = history.successes - 0.5 * (history.successes + history.failures);
    const double delta = team.achievedGreatSuccess
        ? rules.successWeightDelta + std::max(0.0, track)
        : rules.failureWeightDelta + std::min(0.0, track * rules.trackRecordCoefficient);
    history.weight = std::clamp(history.weight + delta, trigger->minimumWeight, trigger->maximumWeight);
    if (team.achievedGreatSuccess) ++history.successes;
    else ++history.failures;
}

bool runtimeComputerTacticMemberPresent(const RuntimeComputerTacticLifecycleQueries& queries,
    const RuntimeComputerAiState& ai, int id) {
    const auto* member = queries.objectIncludingInactive(id);
    return member && queries.ownerMatches(*member, ai.owner)
        && (member->alive || member->mission == "InsideTransport"
            || queries.pendingTeamRejoinAfterLimbo(id));
}

void refreshRuntimeComputerTacticMissing(const RuntimeComputerTacticLifecycleState& state, const RuntimeComputerTacticLifecycleQueries& queries,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    if (team.launched && (!type.reinforce || team.objectIds.empty())) return;
    const auto task = state.taskForces.find(type.taskForceId);
    if (task == state.taskForces.end()) return;
    std::map<std::string, int> counts;
    for (int id : team.objectIds)
        if (const auto* member = queries.objectIncludingInactive(id))
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

bool advanceRuntimeComputerTactics(const RuntimeComputerTacticLifecycleState& state, const RuntimeComputerTacticLifecycleQueries& queries, RuntimeComputerTacticLifecycleEffects& effects, RuntimeComputerAiState& ai) {
    if (state.computerAiTriggers.empty()) return false;
    auto& tactics = state.computerTacticsByOwner[toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    bool changed = effects.advanceProduction(ai, tactics);
    // World scans and team selection need not run at the production tick rate.
    if (state.tick < ai.nextThinkTick) return changed;
    const auto& rules = queries.rules();
    std::vector<int> finished;
    for (auto& [id, team] : tactics.teams) {
        const auto* type = queries.teamType(team.teamTypeId);
        if (!type) { finished.push_back(id); continue; }
        for (auto member = team.objectIds.begin(); member != team.objectIds.end();) {
            if (runtimeComputerTacticMemberPresent(queries, ai, *member)) { ++member; continue; }
            std::erase(team.joiningObjectIds, *member);
            member = team.objectIds.erase(member); changed = true;
        }
        if (team.launched) {
            const auto mission = state.teamMissions.find(team.missionInstanceId);
            if (mission != state.teamMissions.end())
                team.achievedGreatSuccess |= mission->second.achievedGreatSuccess;
            if (team.missionFinished && (!team.baseDefense || team.objectIds.empty())) {
                finished.push_back(id); continue;
            }
            changed = effects.joinReinforcements(team, *type) || changed;
            if (!type->reinforce || team.objectIds.empty()) continue;
        }
        const auto previousMissing = team.missingMembers;
        refreshRuntimeComputerTacticMissing(state, queries, team, *type);
        changed = previousMissing != team.missingMembers || changed;
        changed = effects.recruitMembers(ai, tactics, team, *type) || changed;
        if (team.launched) continue;
        if (team.missingMembers.empty() && !team.objectIds.empty()) {
            bool exiting = false;
            for (int objectId : team.objectIds) {
                const auto* object = queries.object(objectId);
                if (object && (object->factoryExitHostId >= 0 || object->mission == "FactoryExit")) exiting = true;
            }
            if (!exiting) {
                team.launched = true;
                team.missionInstanceId = state.nextTeamMissionInstanceId;
                auto launchType = *type; launchType.house = ai.owner;
                for (int objectId : team.objectIds)
                    if (auto* object = effects.mutableObject(objectId))
                        object->veterancy = std::max(object->veterancy, type->veteranLevel - 1);
                effects.issueTeamOrder(launchType, team.objectIds, team.triggerId, team.instanceId);
                ++ai.attackWaves; ai.attacksOrdered += static_cast<int>(team.objectIds.size());
                ai.status = "launched " + type->name; changed = true;
            }
        } else if (state.tick - team.lastProgressTick
            > queries.legacyScriptTicks(rules.dissolveUnfilledTeamDelay)) finished.push_back(id);
    }
    for (int id : finished) {
        auto found = tactics.teams.find(id);
        if (found == tactics.teams.end()) continue;
        reportRuntimeComputerTacticOutcome(state, queries, tactics, found->second);
        // A paid unit already in progress completes as a free-to-recruit unit.
        for (auto& order : tactics.production) if (order.teamInstanceId == id) order.teamInstanceId = -1;
        state.teamMissions.erase(found->second.missionInstanceId);
        tactics.teams.erase(found); changed = true;
    }
    changed = effects.select(ai, tactics) || changed;
    // New teams first recruit available members; do not start an unnecessary
    // paid unit merely because selection follows the maintenance pass.
    for (auto& [id, team] : tactics.teams) if (team.createdTick == state.tick && !team.launched) {
        if (const auto* type = queries.teamType(team.teamTypeId))
            changed = effects.recruitMembers(ai, tactics, team, *type) || changed;
    }

    // Economic recovery shares the same factory queue and takes the next free
    // slot before optional combat members. It never interrupts a paid order.
    const auto metrics = queries.metrics(ai);
    auto economic = queries.neededHarvester(ai, metrics);
    if (!economic && queries.isMegawealthMode() && queries.needsOilEngineer(ai))
        economic = queries.selectEngineer(ai);
    if (economic) changed = effects.queueProduction(ai, *economic) || changed;
    std::vector<int> priority;
    for (const auto& [id, team] : tactics.teams) if (!team.missingMembers.empty()) priority.push_back(id);
    const int difficulty = queries.difficultyIndex(ai.difficulty);
    const auto random = runtimeStableHashString(ai.owner + ":fill:" + std::to_string(state.tick));
    if (!priority.empty() && random % 100 >= static_cast<unsigned>(rules.fillEarliestTeamProbability[difficulty]))
        std::rotate(priority.begin(), priority.begin() + random % priority.size(), priority.end());
    for (int id : priority) {
        auto& team = tactics.teams.at(id);
        for (const auto& missing : team.missingMembers) {
            const auto item = queries.buildItem(missing, ai.owner);
            if (item) changed = effects.queueProduction(ai, *item, id) || changed;
        }
    }
    return changed;
}
}
