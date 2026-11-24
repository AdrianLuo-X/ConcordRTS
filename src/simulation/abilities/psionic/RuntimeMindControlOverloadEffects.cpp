#include "simulation/abilities/psionic/RuntimeMindControlOverloadEffects.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include <algorithm>
#include <string>
namespace concord::simulation::psionic {
bool advanceRuntimeMindControlOverloadEffects(const int& tick,const SpecialEventSink& sink,RuntimeMindControlOverloadQueries& queries,RuntimeMindControlOverloadEffects& effects) {
    const auto pulses = queries.collectPulses();
    const auto rules = queries.rulesView();
    bool changed = false;
    for (const auto& pulse : pulses) {
        auto* controller = queries.findController(pulse.controllerObjectId);
        if (controller == nullptr || !controller->alive) continue;
        int applied = 0;
        if (!queries.ignoresDamage(*controller)
            && !(queries.protectionInitialized()
                && queries.isProtected(controller->instanceId, tick))) {
            // Retail calls ReceiveDamage on the controller with C4Warhead and
            // no attacker. This damages only the controller, never its victims.
            applied = queries.damageAgainst(*controller, pulse.damage, rules.c4Warhead);
            controller->health = std::max(0, controller->health - applied);
            if (applied > 0) effects.revealAfterDamage(*controller, "mindControlOverload");
            changed = changed || applied > 0;
        }
        if (pulse.playSound && !rules.masterMindOverloadDeathSound.empty())
            effects.emitSound("sound:" + rules.masterMindOverloadDeathSound, queries.spatialPosition(*controller));
        if (sink) sink({"MindControlOverload", tick, controller->instanceId, -1, applied,
            {controller->x, controller->y}, "controlled=" + std::to_string(pulse.controlledCount)});
        if (controller->health <= 0) {
            effects.queueDestroyedExit(controller->instanceId, tick);
        }
    }
    return effects.drainExits() || changed;
}
}
