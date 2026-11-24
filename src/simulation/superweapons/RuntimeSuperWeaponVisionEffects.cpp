#include "RuntimeSuperWeaponVisionEffects.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/vision/RuntimeVision.h"
#include <algorithm>
bool concord::simulation::superweapons::applyRuntimeSuperWeaponVisionEvent(
    bool enabled, const SuperWeaponEvent& event, RuntimeVisionSystem& vision) {
    return enabled
        && (event.kind == "SpyPlaneRevealArea"
            || event.kind == "PsychicRevealArea")
        && vision.revealArea(
            event.owner,
            event.targetCell.x,
            event.targetCell.y,
            std::max(1, event.value)
        );
}
