#include "simulation/ai/RuntimeComputerTacticProduction.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
int runtimeComputerTacticLane(const RuntimeBuildCatalogItem& item) {
    return item.kind == RuntimeBuildKind::Infantry ? 0
        : toUpperAsciiLocal(item.requiredFactory) == "AIRCRAFTTYPE" ? 2 : 1;
}

std::optional<RuntimeProductionSpawn> runtimeComputerTacticAircraftSpawn(
    const RuntimeComputerTacticAircraftQueries& queries, const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    RuntimeAircraftProductionOrder order;
    order.serial = 1; order.typeId = item.typeId;
    order.phase = RuntimeAircraftProductionOrderPhase::Prospective;
    const auto build = queries.buildAircraftInput(owner, order);
    if (build.error) return std::nullopt;
    const auto match = queries.matchAircraftDocks(build.input);
    if (!match.feasible) return std::nullopt;
    for (const auto& assignment : match.assignments) {
        if (assignment.orderSerial != order.serial) continue;
        const auto* host = queries.object(assignment.hostId);
        const auto* rule = host ? queries.buildingType(host->type) : nullptr;
        if (host && rule && (!rule->powered || host->poweredOperational))
            return queries.dockSpawn(*host, assignment.dockIndex);
    }
    return std::nullopt;
}

bool completeRuntimeComputerTacticUnit(const RuntimeComputerTacticAircraftQueries& queries, RuntimeComputerTacticCompletionEffects& effects,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item, int& producedId) {
    producedId = -1;
    if (runtimeComputerTacticLane(item) != 2)
        return effects.buildUnit(ai, item, &producedId);
    effects.refreshDocks();
    const auto spawn = runtimeComputerTacticAircraftSpawn(queries, ai.owner, item);
    if (!spawn || !spawn->anchor) return false;
    const int hostId = spawn->anchor->instanceId;
    const int dockIndex = spawn->dockIndex;
    auto* aircraft = effects.addObject(ai.owner, item.kind, item.typeId, spawn->cell);
    if (!aircraft) return false;
    producedId = aircraft->instanceId;
    effects.applyVeterancy(*aircraft);
    effects.scaleAiHealth(ai, *aircraft);
    if (const auto* host = queries.object(hostId))
        effects.applyDockPose(*aircraft, *host, dockIndex);
    effects.recordCompletedBuild(ai.owner, item);
    ++ai.unitsBuilt;
    ai.lastBuildType = item.typeId;
    effects.refreshOccupancy();
    return true;
}

bool queueRuntimeComputerTacticProduction(std::map<std::string, RuntimeComputerTacticsState>& ownerTactics, const RuntimeComputerTacticQueueQueries& queries, RuntimeComputerTacticQueueEffects& effects,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item,
    int teamInstanceId) {
    auto& tactics = ownerTactics[
        toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    auto& order = tactics.production[runtimeComputerTacticLane(item)];
    if (!order.typeId.empty() || ai.credits < item.cost
        || !queries.itemAllowed(ai, item)) return false;
    if (runtimeComputerTacticLane(item) == 2
        && !runtimeComputerTacticAircraftSpawn(queries, ai.owner, item)) return false;
    order.teamInstanceId = teamInstanceId;
    order.typeId = item.typeId;
    order.remainingTicks = order.totalTicks = std::max(1, item.buildTicks);
    order.paidCost = item.cost;
    effects.spendStoredOre(ai.owner, item.cost);
    ai.credits -= item.cost;
    ai.creditsSpent += item.cost;
    ++ai.productionSerial;
    ai.status = "building " + item.typeId;
    return true;
}

bool advanceRuntimeComputerTacticProduction(const int& tick, const RuntimeComputerTacticProductionQueries& queries, RuntimeComputerTacticCompletionEffects& effects,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics) {
    bool changed = false;
    for (auto& order : tactics.production) {
        if (order.typeId.empty()) continue;
        const auto item = queries.buildItem(order.typeId, ai.owner);
        if (!item) continue;
        // A destroyed/captured factory or lost prerequisite pauses a paid
        // order. Completion waits for a real exit instead of losing its unit.
        if (!queries.itemAllowed(ai, *item)) continue;
        const auto* anchor = queries.factoryAnchor(ai, *item);
        const auto* building = anchor
            ? queries.buildingType(anchor->type) : nullptr;
        if (!anchor || !building || (building->powered && !anchor->poweredOperational)) continue;
        if (runtimeComputerTacticLane(*item) == 2
            && !runtimeComputerTacticAircraftSpawn(queries, ai.owner, *item)) continue;
        if (order.remainingTicks > 0) { --order.remainingTicks; changed = true; }
        if (order.remainingTicks > 0) continue;
        int objectId = -1;
        if (!completeRuntimeComputerTacticUnit(queries, effects, ai, *item, objectId)) continue;
        const auto team = tactics.teams.find(order.teamInstanceId);
        if (team != tactics.teams.end()) {
            team->second.objectIds.push_back(objectId);
            if (team->second.launched) team->second.joiningObjectIds.push_back(objectId);
            const auto missing = std::find_if(team->second.missingMembers.begin(),
                team->second.missingMembers.end(), [&](const auto& type) {
                    return toUpperAsciiLocal(type) == toUpperAsciiLocal(order.typeId);
                });
            if (missing != team->second.missingMembers.end()) team->second.missingMembers.erase(missing);
            team->second.lastProgressTick = tick;
        }
        order = {};
        changed = true;
    }
    return changed;
}
}
