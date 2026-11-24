#include "simulation/abilities/chrono/RuntimeChronoWarp.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include "world/model/GameWorld.h"
#include "world/values/RuntimeCellPoint.h"

namespace concord::simulation::abilities {

void applyRuntimeChronoWarpEvent(
    const SpecialMechanismEvent& event, bool playSound,
    RuntimeChronoWarpQueries& queries,
    RuntimeChronoWarpArrivalActions& arrival,
    RuntimeChronoWarpFeedback& feedback) {
    using runtime_save_text::toLowerAsciiLocal;
    using runtime_save_text::trimAsciiLocal;
    WorldObjectInstance* object = queries.mutableObject(event.objectId);
    if (object == nullptr || !object->alive) return;
    const bool reinforcement = event.kind == "ChronoReinforcementIn";
    const bool arriving = reinforcement || event.kind == "ChronoWarpIn";
    const auto boarding = arriving && !reinforcement
        ? queries.destinationAdmission(*object, RuntimeCellPoint{object->x, object->y})
        : std::optional<bool>{};
    // Lookup remains unconditional, including outward and reinforcement events.
    const auto* pendingHost = queries.pendingTarget(object->instanceId);
    const auto* boardingHost = boarding && *boarding && pendingHost
        ? queries.object(*pendingHost) : nullptr;
    if (arriving) {
        object->cell = queries.canonicalCell(object->x, object->y);
        object->onBridge = queries.bridge(object->x, object->y);
        if (queries.groundSubcells(*object)) {
            arrival.releaseSubcellReservation(object->instanceId);
            if (boardingHost) object->subCell = -1;
            else arrival.assignSubcell(*object);
        }
        arrival.clearRenderPosition(*object);
        arrival.refreshOccupancy();
        arrival.invalidateProjection();
    }
    const auto& rules = queries.specialRules();
    feedback.beginEffect(*object);
    const auto offset = queries.subcellOffset(*object);
    feedback.worldPosition(
        (object->hasRenderPosition ? object->renderX : object->x) + offset.x,
        (object->hasRenderPosition ? object->renderY : object->y) + offset.y,
        object->hasRenderPosition ? object->renderLevel : object->level);
    if (boardingHost) {
        const auto hull = queries.boardingPose(*boardingHost);
        feedback.worldPosition(hull.x, hull.y, hull.level);
    }
    feedback.facingAndLifecycle(object->facing, reinforcement ? "ChronoReinforcementIn"
        : arriving ? "ChronoWarpIn" : "ChronoWarpOut");
    // Retail TeleportLocomotionClass uses WarpOut at both ordinary endpoints.
    feedback.animation(reinforcement ? rules.warpInAnim : rules.warpOutAnim);
    const auto& animationId = feedback.animation();
    if (!animationId.empty() && toLowerAsciiLocal(animationId) != "none") {
        feedback.initializeAndAppend();
    }
    if (playSound) {
        const UnitType* unit = queries.unitType(*object);
        std::string sound = unit == nullptr ? std::string()
            : (arriving ? unit->chronoInSound : unit->chronoOutSound);
        if (trimAsciiLocal(sound).empty()) sound = arriving ? rules.chronoInSound : rules.chronoOutSound;
        if (!trimAsciiLocal(sound).empty()) feedback.sound("sound:" + trimAsciiLocal(sound), queries.spatialPosition(*object));
    }
}

} // namespace concord::simulation::abilities
