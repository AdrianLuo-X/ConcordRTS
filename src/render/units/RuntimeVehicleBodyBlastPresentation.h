#pragma once

#include <algorithm>
#include <string_view>

#include "data/rules/RulesDatabase.h"

struct RuntimeVehicleBlastPresentation {
    double radius = 0.0;
    double age = 0.0;
    double duration = 0.0;
    double coefficient = 0.0;
};

static inline bool runtimeVehicleBodyHasLiveBlast(
    int damage, int remainingTicks, int totalTicks, std::string_view renderIntent,
    std::string_view lifecycleState
) {
    if (damage <= 0 || remainingTicks <= 0 || totalTicks <= 0) return false;
    if (renderIntent == "warheadImpact" || renderIntent == "bombImpact"
        || renderIntent == "deathImpact" || renderIntent == "unitDestruction"
        || renderIntent == "superWeaponEffect") return true;
    // Snapshot records keep the hit lifecycle and its age, but intentionally
    // omit presentation tags. Recover the same impulse without new authority.
    return (renderIntent.empty() || renderIntent == "impact")
        && (lifecycleState == "Impact" || lifecycleState == "DeathImpact");
}

inline bool runtimeVehicleBodyWarheadCanRock(const WarheadType* wh) {
    return !(wh == nullptr || wh->parasite || wh->mindControl || wh->temporal || wh->psychedelic
        || wh->radiation || wh->psychicDamage || wh->electricAssault
        || (!wh->rocker && wh->cellSpread <= 0.0));
}

inline double runtimeVehicleBlastRockingCoefficient(const WarheadType& warhead, double directRockingCoefficient) {
    if (warhead.rocker) return directRockingCoefficient;
    // A narrow armor-piercing hit should only nudge the hull. Large area
    // explosions retain their existing response; explicit Rocker stays strong.
    return 0.65 * std::clamp(warhead.cellSpread, 0.0, 1.0);
}

inline RuntimeVehicleBlastPresentation runtimeVehicleBlastPresentation(
    const WarheadType& warhead, int totalTicks, int remainingTicks,
    double hz, double directRockingCoefficient
) {
    return {std::clamp(std::max(warhead.cellSpread, 0.5) + 1.25, 1.0, 6.0),
        (totalTicks - remainingTicks) / hz,
        std::min(0.9, totalTicks / hz),
        runtimeVehicleBlastRockingCoefficient(warhead, directRockingCoefficient)};
}
