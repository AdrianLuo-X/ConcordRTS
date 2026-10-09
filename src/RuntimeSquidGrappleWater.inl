#include "RuntimeSquidGrappleMotion.h"

bool advanceRuntimeSquidGrappleWater(RuntimeWindowState& state) {
    bool changed = false;
    for (const auto& [targetId, link] : state.specialMechanisms.parasiteLinks()) {
        if (!link.organic || toLowerAsciiLocal(link.sourceTypeId) != "sqd") continue;
        const int age = state.tick - link.attachedTick;
        if (age <= 0) continue;
        const int before = runtimeSquidGrappleNativeAge(age - 1, state.config.fixedTickHz);
        const int now = runtimeSquidGrappleNativeAge(age, state.config.fixedTickHz);
        // Retail state 4 spawns three splashes when a ten-pose rocking strip
        // finishes. Integer boundary crossing also works at non-multiple Hz.
        const int cycle = (now - 30) / 40;
        if (now < 70 || before >= 30 + cycle * 40) continue;
        const auto* target = findObjectById(state, targetId);
        const auto* unit = target ? findRuntimeUnitType(state.rules, *target) : nullptr;
        if (!target || !unit || !unit->naval || unit->organic) continue;
        auto splashes = splitRuntimeTokenList(state.rules.getAudioVisualValue("SplashList"));
        splashes.erase(std::remove_if(splashes.begin(), splashes.end(), [&](const auto& id) {
            return state.art.findArtType(id) == nullptr;
        }), splashes.end());
        if (splashes.empty()) continue;
        std::uint64_t seed = runtimeStableHashString("SquidGrappleSplash/1");
        runtimeStableHashMix(seed, link.sourceObjectId);
        runtimeStableHashMix(seed, targetId);
        runtimeStableHashMix(seed, link.projectileId);
        runtimeStableHashMix(seed, cycle);
        const double yaw = (worldObjectArtFacingBucket(target->facing, 256) - 64)
            * 6.283185307179586 / 256;
        for (int slot = 0; slot < 3; ++slot) {
            runtimeStableHashMix(seed, slot);
            // Retail offsets: -128, 0, +128 longitudinal and +/-64 lateral
            // leptons, rotated with the host. Positions remain on the water.
            const double forward = (slot - 1) * 0.5;
            const double lateral = (seed & 1U) ? -0.25 : 0.25;
            RuntimeProjectileTargetPosition position;
            position.x = target->x + forward * std::cos(yaw) - lateral * std::sin(yaw);
            position.y = target->y + forward * std::sin(yaw) + lateral * std::cos(yaw);
            position.level = target->level;
            position.cell = {static_cast<int>(std::lround(position.x)), static_cast<int>(std::lround(position.y))};
            const std::string& image = splashes[(seed >> 1U) % splashes.size()];
            spawnRuntimeImpact(state, position.cell, position.level, false, image,
                "", 0, 0.0, false, "", nullptr, &position);
            auto& splash = state.impactEffects.back();
            splash.lifecycleState = "SquidSplash";
            splash.renderIntent = "squidSplash";
            // Snapshot transfer retains effect intent IDs, while discarding
            // cached image/frame fields. This intent is an animation, not a
            // damaging warhead, and reconstructs the same chosen resource.
            splash.warheadImpactIntentId = image;
            splash.attackerObjectId = link.sourceObjectId;
            splash.targetObjectId = targetId;
            splash.owner = link.owner;
            initializeRuntimeImpactAnimationTimeline(state, splash, true);
            changed = true;
        }
    }
    return changed;
}
