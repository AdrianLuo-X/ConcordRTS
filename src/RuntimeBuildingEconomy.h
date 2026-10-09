#pragma once

#include "RulesDatabase.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct RuntimeBuildingRepairCharge {
    int healed = 0;
    int cost = 0;
};

inline int runtimeBuildingSaleRefund(const BuildingType* building, double refundPercent) {
    if (building == nullptr) return 0;
    if (building->soylent > 0) return building->soylent;
    const double ratio = std::isfinite(refundPercent) ? std::max(0.0, refundPercent) : 0.0;
    return static_cast<int>(std::clamp(std::floor(std::max(0, building->cost) * ratio),
        0.0, static_cast<double>(std::numeric_limits<int>::max())));
}

inline int runtimeBuildingRepairInterval(const SpecialMechanismRules& rules, int tickHz) {
    const double rate = std::isfinite(rules.buildingRepairRateMinutes)
        ? std::clamp(rules.buildingRepairRateMinutes, 0.0, 60.0) : 0.016;
    return static_cast<int>(std::clamp(std::ceil(rate * 60.0 * std::max(1, tickHz)),
        1.0, static_cast<double>(std::numeric_limits<int>::max())));
}

inline RuntimeBuildingRepairCharge runtimeBuildingRepairCharge(
    const BuildingType* building, const SpecialMechanismRules& rules,
    int health, int maxHealth, int credits
) {
    if (building == nullptr || !building->repairable || !building->clickRepairable
        || maxHealth <= 0 || health <= 0 || health >= maxHealth || rules.repairStep <= 0) return {};
    const int step = std::min(rules.repairStep, maxHealth - health);
    const double ratio = std::isfinite(rules.repairPercent) ? std::max(0.0, rules.repairPercent) : 0.15;
    const double pricePerHp = ratio * std::max(0, building->cost) / maxHealth;
    if (pricePerHp <= 0.0) return {step, 0};
    if (credits <= 0) return {};
    // A final partial step pays only for the restored health. Never grant a
    // full repair step when the owner can afford only part of it.
    const int affordable = static_cast<int>(std::clamp(std::floor(credits / pricePerHp + 1e-9),
        0.0, static_cast<double>(step)));
    if (affordable <= 0) return {};
    const int cost = static_cast<int>(std::clamp(std::ceil(affordable * pricePerHp - 1e-9),
        1.0, static_cast<double>(credits)));
    return {affordable, cost};
}
