#pragma once

#include "data/rules/RulesDatabase.h"
#include "simulation/production/RuntimeProductionLanes.h"

#include <cstdint>
#include <string>

inline constexpr int runtimeProductionTimingLegacyTickHz = 15;
inline constexpr int runtimeProductionTimingQuantumLegacyTicks = 54;
inline constexpr int runtimeProductionTimingMaximumFactories = 1000;

struct RuntimeProductionTimingInput {
    ProductionRules rules;
    int cost = 0;
    double buildTimeMultiplier = 1.0;
    bool wall = false;
    int poweredCompatibleFactoryCount = 1;
    double powerProduced = 0.0;
    double powerUsed = 0.0;
    int fixedTickHz = 30;

    bool operator==(const RuntimeProductionTimingInput& other) const {
        return rules.buildSpeed == other.rules.buildSpeed
            && rules.multipleFactory == other.rules.multipleFactory
            && rules.wallBuildSpeedCoefficient
                == other.rules.wallBuildSpeedCoefficient
            && rules.lowPowerPenaltyModifier
                == other.rules.lowPowerPenaltyModifier
            && rules.minLowPowerProductionSpeed
                == other.rules.minLowPowerProductionSpeed
            && rules.maxLowPowerProductionSpeed
                == other.rules.maxLowPowerProductionSpeed
            && cost == other.cost
            && buildTimeMultiplier == other.buildTimeMultiplier
            && wall == other.wall
            && poweredCompatibleFactoryCount
                == other.poweredCompatibleFactoryCount
            && powerProduced == other.powerProduced
            && powerUsed == other.powerUsed
            && fixedTickHz == other.fixedTickHz;
    }
};

struct RuntimeProductionTimingDuration {
    // Unquantized duration in stock 15 Hz ticks, kept finite for diagnostics.
    double rawLegacyTicks = 0.0;
    // max(54, floor(rawLegacyTicks / 54) * 54), then saturated to int.
    int legacyTicks = runtimeProductionTimingQuantumLegacyTicks;
    // ceil(legacyTicks * fixedTickHz / 15), then saturated to int.
    int fixedTicks = runtimeProductionTimingQuantumLegacyTicks * 2;

    bool operator==(const RuntimeProductionTimingDuration&) const = default;
};

enum class RuntimeProductionTimingVeto : std::uint8_t {
    None,
    InvalidRules,
    InvalidCost,
    InvalidBuildTimeMultiplier,
    InvalidFactoryCount,
    InvalidPowerSnapshot,
    InvalidFixedTickHz
};

struct RuntimeProductionTimingResult {
    bool valid = false;
    RuntimeProductionTimingVeto veto =
        RuntimeProductionTimingVeto::InvalidRules;
    std::string reason;

    double multipleFactorySpeed = 1.0;
    double lowPowerSpeed = 1.0;
    double dynamicSpeed = 1.0;
    RuntimeProductionTimingDuration base;
    RuntimeProductionTimingDuration effective;

    // Progress multiplier for an order whose totalTicks is base.fixedTicks.
    // This is the best deterministic fraction approximating
    // base.fixedTicks / effective.fixedTicks with both terms in [1, 1000].
    RuntimeProductionRate productionRate;
    bool saturated = false;

    bool operator==(const RuntimeProductionTimingResult& other) const {
        return valid == other.valid && veto == other.veto
            && reason == other.reason
            && multipleFactorySpeed == other.multipleFactorySpeed
            && lowPowerSpeed == other.lowPowerSpeed
            && dynamicSpeed == other.dynamicSpeed && base == other.base
            && effective == other.effective
            && productionRate.numerator == other.productionRate.numerator
            && productionRate.denominator == other.productionRate.denominator
            && saturated == other.saturated;
    }
};

RuntimeProductionTimingResult runtimeCalculateProductionTiming(
    const RuntimeProductionTimingInput& input
);

const char* runtimeProductionTimingVetoName(RuntimeProductionTimingVeto veto);
