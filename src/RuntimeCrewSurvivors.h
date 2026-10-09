#pragma once

#include "RuntimeBuildingEconomy.h"
#include "RuntimeCombatDetails.h"
#include <algorithm>
#include <cstdint>

// Retail YR BuildingClass::GetCrewCount (0x451330): refund/divisor,
// doubled divisor after capture, and a maximum of five. SurvivorRate is unused.
inline int runtimeBuildingCrewCount(const BuildingType* building,
    const CrewSurvivorRules& rules, double refundPercent, int side, bool captured) {
    if (!building || !building->crewed || side < 0 || side >= 3) return 0;
    const std::int64_t divisor = static_cast<std::int64_t>(rules.survivorDivisors[side])
        * (captured ? 2 : 1);
    if (divisor <= 0) return 0;
    return static_cast<int>(std::clamp<std::int64_t>(
        runtimeBuildingSaleRefund(building, refundPercent) / divisor, 1, 5));
}

// An exit has one deterministic random stream. Its inputs already belong to
// authoritative saved state, and the exit tombstone prevents a second roll.
struct RuntimeCrewRandom {
    std::uint64_t value;
    std::uint64_t next() { value = runtimeCombatRandomMix(value); return value; }
    int range(int low, int high) {
        return low + static_cast<int>(next() % (static_cast<std::uint64_t>(std::max(low, high) - low) + 1));
    }
    bool chance(double probability) {
        return static_cast<double>(next() >> 11) * 0x1.0p-53 < probability;
    }
};
