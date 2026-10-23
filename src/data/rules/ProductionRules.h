#pragma once

struct ProductionRules {
    // Minutes required to produce a 1000-credit object before modifiers.
    double buildSpeed = 0.7;
    // Average buildup minutes; retail constructor default, overridden by rules.
    double buildupTime = 0.05;
    // Cumulative time multiplier contributed by each additional factory.
    double multipleFactory = 0.8;
    double wallBuildSpeedCoefficient = 3.0;
    double lowPowerPenaltyModifier = 1.0;
    double minLowPowerProductionSpeed = 0.5;
    double maxLowPowerProductionSpeed = 0.8;
};
