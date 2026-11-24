#include "simulation/ai/RuntimeComputerTacticReservations.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include <algorithm>
namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
bool runtimeComputerTacticReservesObject(const RuntimeComputerTacticObjectReservationQueries& queries, int objectId) {
    if (queries.pendingTransportTarget(objectId) || queries.pendingEngineerCapture(objectId)
        || queries.pendingOccupyTarget(objectId)) return true;
    for (const auto& relation : queries.grindingRelations())
        if (relation.sourceId == objectId) return true;
    for (const auto& [owner, tactics] : queries.ownerTactics())
        for (const auto& [id, team] : tactics.teams)
            if (std::find(team.objectIds.begin(), team.objectIds.end(), objectId)
                != team.objectIds.end()) return true;
    return false;
}

bool runtimeComputerTacticReservesSuperWeapon(const RuntimeComputerTacticSuperReservationState& state,
    const std::string& owner, const SuperWeaponRule* weapon) {
    if (state.campaignMode || !weapon) return false;
    const auto identity = toLowerAsciiLocal(weapon->id + " " + weapon->type);
    const bool chrono = identity.find("chrono") != std::string::npos;
    const bool iron = identity.find("ironcurtain") != std::string::npos;
    if (!chrono && !iron) return false;
    const auto house = state.computerTacticsByOwner.find(toLowerAsciiLocal(trimAsciiLocal(owner)));
    if (house == state.computerTacticsByOwner.end()) return false;
    for (const auto& [id, team] : house->second.teams) {
        if (team.missionFinished) continue;
        const auto type = state.teams.find(team.teamTypeId);
        if (type == state.teams.end()) continue;
        const auto script = state.scripts.find(type->second.scriptId);
        if (script == state.scripts.end()) continue;
        const auto mission = state.teamMissions.find(team.missionInstanceId);
        const std::size_t start = mission == state.teamMissions.end() ? 0 : mission->second.stepIndex;
        for (std::size_t step = start; step < script->second.steps.size(); ++step) {
            const int action = script->second.steps[step].action;
            if ((iron && action == 55) || (chrono && (action == 56 || action == 57))) return true;
        }
    }
    return false;
}
}
