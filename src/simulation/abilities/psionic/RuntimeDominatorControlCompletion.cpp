#include "simulation/abilities/psionic/RuntimeDominatorControlCompletion.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
namespace concord::simulation::psionic {
void completeRuntimeDominatorControl(
    const std::vector<SuperWeaponObjectLifecycleEvent>& events,
    RuntimeDominatorControlTargets& targets,RuntimeDominatorControlLinks& links) {
    for (const auto& event : events) {
            if (event.kind != SuperWeaponObjectLifecycleKind::OwnerChanged) continue;
            auto* target = targets.findTarget(event.objectId);
            if (!target || target->owner != event.newOwner) continue;
            links.establishPermanentControl(event.objectId,
                event.newOwner, event.oldOwner, event.tick);
            target->mission = "MindControlledPermanent";
        }
}
}
