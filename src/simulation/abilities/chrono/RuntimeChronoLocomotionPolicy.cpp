#include "RuntimeChronoLocomotionPolicy.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
namespace concord::simulation::abilities {
namespace {
struct ChronoMoveRequestLifetime {
    RuntimeChronoLocomotionEffects& effects;
    ~ChronoMoveRequestLifetime() noexcept { effects.finishRequestScope(); }
};
}
bool holdRuntimeChronoRecovery(
    const bool& initialized, WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const int& currentTick, const RuntimeChronoRecoveryQueries& queries,
    RuntimeChronoPoseEffects& effects) {
    if (!initialized || !queries.movementLocked(object.instanceId, currentTick)) return false;
    effects.clearRenderPosition(object);
    behavior.state = RuntimeBehaviorState::Cooldown;
    if (!queries.inTransit(object.instanceId)) object.mission = "ChronoFrozen";
    return true;
}
std::optional<bool> advanceRuntimeTeleporterMove(
    WorldObjectInstance& object, RuntimeObjectBehavior& behavior, RuntimeCellPoint destination,
    const RuntimeChronoMoveQueries& queries, RuntimeChronoLocomotionEffects& effects) {
    const UnitType* unit = queries.unitType(object);
    // Harvesters manage their drive-to-ore / blink-to-refinery cycle separately.
    if (unit == nullptr || !unit->teleporter || unit->harvester) return std::nullopt;
    const auto transportAdmission = queries.transportAdmission(object, destination);
    if (transportAdmission && !*transportAdmission) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    if (RuntimeCellPoint{object.x, object.y} == destination && !transportAdmission) return std::nullopt;
    ChronoMoveRequestLifetime requestLifetime{effects};
    if (!effects.requestMove(object, destination, transportAdmission)) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksRemaining = 0;
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Cooldown;
    effects.releaseInfantrySubcellReservation(object.instanceId);
    effects.clearRenderPosition(object);
    return true;
}
}
