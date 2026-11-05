#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/superweapons/private/SuperWeaponObjectCellKey.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_set>
using superweapon_detail::superWeaponObjectCellKey;

// Reuse the original complete assertions as test-only source. The executable
// links the canonical production core, never its legacy diagnostic archive.
#include "simulation/superweapons/diagnostics/SuperWeaponDetailSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponRechargeSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponNoCooldownSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponForceShieldGroundSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponChronosphereGroundSmoke.inl"

int main() {
    if (!smokeSuperWeaponChronosphereGround() || !smokeSuperWeaponForceShieldGround()
        || !smokeSuperWeaponConcurrentRecharge() || !smokeSuperWeaponNoCooldown()
        || !smokeSuperWeaponDetails()) return 1;
    // Full byte records and state hashes must match at every continued tick,
    // including queued activation, active duration and retired casts.
    int observations = 0;
    for (int hz : {15, 30, 60}) {
        SuperWeaponRule rule; rule.id = "RESTORE";
        rule.type = rule.action = "PsychicReveal"; rule.targetMode = "Area";
        rule.durationFrames = 30; rule.rechargeTimeMinutes = 0.5;
        SuperWeaponSystem source; source.reset(std::vector<SuperWeaponRule>{rule});
        SuperWeaponWorldContext context; context.fixedTickHz = hz;
        context.mapWidth = context.mapHeight = 64;
        context.cellLegal = [](int, int, auto) { return true; };
        if (!source.forceReady("A", rule.id, &context)
            || !source.beginTargetSelection("A", rule.id, context)
            || !source.submitTarget("A", rule.id, {{20, 20}, -1}, context)) return 2;
        std::ostringstream initial; source.save(initial); IniFile ini;
        SuperWeaponSystem restored; restored.reset(std::vector<SuperWeaponRule>{rule});
        if (!ini.loadFromString(initial.str()) || !restored.load(ini)) return 3;
        for (int tick = 0; tick <= hz * 4; ++tick) {
            context.tick = tick; source.tick(context); restored.tick(context);
            std::ostringstream left, right; source.save(left); restored.save(right);
            if (left.str() != right.str() || source.stateHash() != restored.stateHash()) return 4;
            ++observations;
        }
    }
    std::cout << "superweapon pure core: original five complete fixtures; "
              << observations << " continued full-byte/hash observations passed\n";
    return 0;
}
