#include "simulation/ai/RuntimeComputerTacticRecruitment.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
#include <limits>
namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
int runtimeComputerTacticGroup(const RuntimeComputerTacticRecruitmentQueries& queries, const RuntimeTeamType& type) {
    const auto* task = queries.taskForce(type.taskForceId);
    return type.group == -1 && task != nullptr ? task->group : type.group;
}

bool runtimeComputerTacticMayRecruit(const RuntimeComputerTacticRecruitmentQueries& queries,
    const RuntimeComputerAiState& ai, const RuntimeComputerTacticTeam& team,
    const RuntimeTeamType& type, const WorldObjectInstance& object) {
    if (!object.alive || object.health <= 0 || !queries.ownerMatches(object, ai.owner)
        || object.factoryExitHostId >= 0 || object.garrisonHostObjectId >= 0 || object.tankBunkerHostId >= 0
        || queries.disabledByParasite(object.instanceId)) return false;
    // Retail's MissionControl forbids recruiting units during entry,
    // unloading, harvesting and other committed interactions.
    const auto mission = toLowerAsciiLocal(object.mission);
    if (mission != "guard" && mission != "areaguard" && mission != "area guard"
        && mission != "move" && mission != "attack" && mission != "secondaryfire" && mission != "dock"
        && mission != "stop" && mission != "patrol" && mission != "hunt") return false;
    if (queries.pendingEngineerCapture(object.instanceId)
        || queries.pendingTransportTarget(object.instanceId)
        || queries.pendingOccupyTarget(object.instanceId)) return false;
    for (const auto& relation : queries.grindingRelations())
        if (relation.sourceId == object.instanceId) return false;
    if (const auto* unit = queries.unitType(object); unit) {
        if (unit->harvester || unit->resourceGatherer
            || (unit->airportBound && (object.airborne || !queries.hasAttackAmmo(object)))) return false;
    }
    bool assigned = false;
    const int group = runtimeComputerTacticGroup(queries, type);
    for (const auto& [house, tactics] : queries.ownerTactics())
        for (const auto& [id, existing] : tactics.teams) {
            if (std::find(existing.objectIds.begin(), existing.objectIds.end(), object.instanceId)
                == existing.objectIds.end()) continue;
            assigned = true;
            if (&existing == &team || existing.teamTypeId == team.teamTypeId) return false;
            const auto* oldType = queries.teamType(existing.teamTypeId);
            if (oldType == nullptr || !oldType->recruitable
                || oldType->priority >= type.priority) return false;
            // A Recruiter may draw from another authored group. Default
            // skirmish groups are -1; -2 explicitly accepts any group.
            if (!type.recruiter && group != -2 && runtimeComputerTacticGroup(queries, *oldType) != group) return false;
        }
    if (!assigned && !type.recruiter && group != -2 && group != -1) return false;
    return true;
}

bool recruitRuntimeComputerTacticMembers(const RuntimeComputerTacticRecruitmentState& state, const RuntimeComputerTacticRecruitmentQueries& queries, RuntimeComputerTacticRecruitmentEffects& effects,
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
        for (const auto& object : queries.objects()) {
            if (toUpperAsciiLocal(object.type) != key
                || !runtimeComputerTacticMayRecruit(queries, ai, team, type, object)) continue;
            const double distance = queries.cellDistance(ai.baseCenter, queries.objectCell(object));
            if (distance < bestDistance || (distance == bestDistance && object.instanceId < recruitId)) {
                recruitId = object.instanceId; bestDistance = distance;
            }
        }
        if (recruitId < 0) { ++missing; continue; }
        effects.detachMember(recruitId);
        effects.eraseBehavior(recruitId);
        team.objectIds.push_back(recruitId);
        if (team.launched) team.joiningObjectIds.push_back(recruitId);
        if (auto* recruit = effects.mutableObject(recruitId)) recruit->mission = "Guard";
        missing = team.missingMembers.erase(missing);
        team.lastProgressTick = state.tick; changed = true;
    }
    return changed;
}

bool joinRuntimeComputerTacticReinforcements(const RuntimeComputerTacticRecruitmentState& state, const RuntimeComputerTacticRecruitmentQueries& queries, RuntimeComputerTacticRecruitmentEffects& effects,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    bool changed = false;
    for (auto next = team.joiningObjectIds.begin(); next != team.joiningObjectIds.end();) {
        auto* member = effects.mutableObject(*next);
        if (!member) { next = team.joiningObjectIds.erase(next); continue; }
        if (member->factoryExitHostId >= 0 || member->mission == "FactoryExit") { ++next; continue; }
        member->veterancy = std::max(member->veterancy, type.veteranLevel - 1);
        const int memberId = member->instanceId;
        RuntimeTeamMissionState companions;
        for (int id : team.objectIds) if (id != memberId) companions.objectIds.push_back(id);
        RuntimeCellPoint destination = queries.missionCenter(companions);
        bool ordered = false;
        if (auto mission = state.teamMissions.find(team.missionInstanceId);
            mission != state.teamMissions.end()) {
            auto& current = mission->second;
            if (std::find(current.objectIds.begin(), current.objectIds.end(), memberId) == current.objectIds.end())
                current.objectIds.push_back(memberId);
            const auto script = state.scripts.find(current.scriptId);
            const int action = script != state.scripts.end() && current.stepIndex < script->second.steps.size()
                ? script->second.steps[current.stepIndex].action : -1;
            if (current.stepIssued && (action == 0 || action == 46))
                if (const auto* target = queries.object(current.targetObjectId))
                    ordered = effects.issueTeamAttack( {memberId}, *target) > 0;
            if (current.targetCell.x >= 0) destination = current.targetCell;
        }
        if (!ordered && destination.x >= 0) (void)effects.issueTeamMove( {memberId}, destination);
        next = team.joiningObjectIds.erase(next); changed = true;
    }
    return changed;
}
}
