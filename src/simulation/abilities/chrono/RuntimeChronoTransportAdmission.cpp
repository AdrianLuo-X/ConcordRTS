#include "RuntimeChronoTransportAdmission.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/abilities/chrono/RuntimeChronoState.h"
namespace concord::simulation::abilities {
bool runtimePersonalChronoTransportPassenger(
    const WorldObjectInstance& passenger, const RuntimeChronoTransportQueries& queries) {
    const auto* type = queries.unitType(passenger);
    return passenger.kind == WorldObjectKind::Infantry && type
        && type->teleporter && !type->harvester;
}
std::optional<bool> runtimeChronoTransportDestinationAdmission(
    const WorldObjectInstance& passenger, const RuntimeCellPoint& destination,
    const RuntimeChronoTransportQueries& queries) {
    if (!runtimePersonalChronoTransportPassenger(passenger, queries)) return std::nullopt;
    const auto pending = queries.pendingHostId(passenger.instanceId);
    if (!pending) return std::nullopt;
    const auto* host = queries.findIncludingInactive(*pending);
    if (!host || host->kind != WorldObjectKind::Unit) return std::nullopt;
    const auto* move = queries.behavior(passenger.instanceId);
    if (!move || move->kind != RuntimeBehaviorKind::Move
        || move->pendingAllowedOccupiedObjectId != host->instanceId
        || move->moveTarget != destination || RuntimeCellPoint{host->x, host->y} != destination
        || host->airborne || host->renderMoveActive
        || !queries.canEnter(passenger, *host)) return false;
    const auto* warp = queries.moveState(passenger.instanceId);
    if (warp && queries.inTransit(passenger.instanceId)
        && (warp->destination.x != destination.x || warp->destination.y != destination.y
            || warp->destinationLevel != host->level)) return false;
    return true;
}
bool runtimeChronoTransportArrivalReady(
    const WorldObjectInstance& passenger, const WorldObjectInstance& host,
    const int& currentTick, const RuntimeChronoTransportQueries& queries) {
    const auto* warp = queries.moveState(passenger.instanceId);
    const auto admitted = runtimeChronoTransportDestinationAdmission(
        passenger, RuntimeCellPoint{host.x, host.y}, queries);
    return admitted && *admitted && warp
        && (warp->phase == ChronoPhase::WarpIn || warp->phase == ChronoPhase::Cooldown)
        && warp->completeTick <= currentTick
        && warp->destination.x == host.x && warp->destination.y == host.y
        && warp->destinationLevel == host.level
        && RuntimeCellPoint{passenger.x, passenger.y} == RuntimeCellPoint{host.x, host.y}
        && passenger.level == host.level && !passenger.renderMoveActive;
}
}
