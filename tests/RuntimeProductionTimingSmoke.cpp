#include "RuntimeProductionTiming.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Production timing smoke failure: " << message << '\n';
    }
    return condition;
}

bool closeTo(double actual, double expected) {
    return std::abs(actual - expected) < 0.000001;
}

RuntimeProductionTimingInput stockInput(int cost = 1000) {
    RuntimeProductionTimingInput input;
    input.cost = cost;
    input.powerProduced = 100.0;
    input.powerUsed = 100.0;
    input.fixedTickHz = 30;
    return input;
}

bool globallyMinimumRate(const RuntimeProductionTimingResult& result) {
    const long double target =
        static_cast<long double>(result.base.fixedTicks)
        / static_cast<long double>(result.effective.fixedTicks);
    const long double chosen =
        static_cast<long double>(result.productionRate.numerator)
        / static_cast<long double>(result.productionRate.denominator);
    const long double chosenError = std::abs(chosen - target);
    for (std::uint32_t denominator = 1; denominator <= 1000U;
        ++denominator) {
        for (std::uint32_t numerator = 1; numerator <= 1000U;
            ++numerator) {
            const long double candidate =
                static_cast<long double>(numerator) / denominator;
            if (std::abs(candidate - target) < chosenError - 1.0e-18L) {
                return false;
            }
        }
    }
    return true;
}

bool testStockBaseAndMultipliers() {
    bool success = true;
    const RuntimeProductionTimingResult base =
        runtimeCalculateProductionTiming(stockInput());
    success &= expect(
        base.valid && !base.saturated
            && base.veto == RuntimeProductionTimingVeto::None
            && closeTo(base.base.rawLegacyTicks, 630.0)
            && base.base.legacyTicks == 594
            && base.base.fixedTicks == 1188
            && base.effective == base.base
            && base.productionRate.numerator == 1
            && base.productionRate.denominator == 1,
        "stock 1000-credit raw/quantized/fixed timing"
    );

    RuntimeProductionTimingInput multiplierInput = stockInput();
    multiplierInput.buildTimeMultiplier = 1.5;
    const RuntimeProductionTimingResult multiplier =
        runtimeCalculateProductionTiming(multiplierInput);
    success &= expect(
        multiplier.valid
            && closeTo(multiplier.base.rawLegacyTicks, 945.0)
            && multiplier.base.legacyTicks == 918
            && multiplier.base.fixedTicks == 1836
            && multiplier.productionRate.numerator == 1
            && multiplier.productionRate.denominator == 1,
        "BuildTimeMultiplier=1.5 did not produce 945 raw -> 918 legacy"
    );

    RuntimeProductionTimingInput wallInput = stockInput();
    wallInput.wall = true;
    const RuntimeProductionTimingResult wall =
        runtimeCalculateProductionTiming(wallInput);
    success &= expect(
        wall.valid && closeTo(wall.base.rawLegacyTicks, 1890.0)
            && wall.base.legacyTicks == 1890
            && wall.base.fixedTicks == 3780,
        "WallBuildSpeedCoefficient=3 did not quantize on the exact boundary"
    );
    return success;
}

bool testDynamicSpeed() {
    bool success = true;
    RuntimeProductionTimingInput factoriesInput = stockInput();
    factoriesInput.poweredCompatibleFactoryCount = 2;
    const RuntimeProductionTimingResult factories =
        runtimeCalculateProductionTiming(factoriesInput);
    success &= expect(
        factories.valid
            && closeTo(factories.multipleFactorySpeed, 1.25)
            && closeTo(factories.lowPowerSpeed, 1.0)
            && closeTo(factories.dynamicSpeed, 1.25)
            && closeTo(factories.effective.rawLegacyTicks, 504.0)
            && factories.effective.legacyTicks == 486
            && factories.effective.fixedTicks == 972
            && factories.productionRate.numerator == 11
            && factories.productionRate.denominator == 9,
        "two compatible factories did not apply the stock .8 multiplier"
    );

    RuntimeProductionTimingInput fullPowerInput = stockInput();
    fullPowerInput.powerProduced = 150.0;
    fullPowerInput.powerUsed = 100.0;
    const RuntimeProductionTimingResult fullPower =
        runtimeCalculateProductionTiming(fullPowerInput);
    success &= expect(
        fullPower.valid && closeTo(fullPower.lowPowerSpeed, 1.0)
            && closeTo(fullPower.dynamicSpeed, 1.0),
        "power surplus incorrectly applied a low-power penalty"
    );

    RuntimeProductionTimingInput halfPowerInput = stockInput();
    halfPowerInput.powerProduced = 50.0;
    halfPowerInput.powerUsed = 100.0;
    const RuntimeProductionTimingResult halfPower =
        runtimeCalculateProductionTiming(halfPowerInput);
    success &= expect(
        halfPower.valid && closeTo(halfPower.lowPowerSpeed, 0.5)
            && closeTo(halfPower.dynamicSpeed, 0.5)
            && closeTo(halfPower.effective.rawLegacyTicks, 1260.0)
            && halfPower.effective.legacyTicks == 1242
            && halfPower.effective.fixedTicks == 2484
            && halfPower.productionRate.numerator == 11
            && halfPower.productionRate.denominator == 23,
        "50 percent power shortage did not clamp to the stock minimum"
    );

    RuntimeProductionTimingInput slightShortageInput = stockInput();
    slightShortageInput.powerProduced = 99.0;
    slightShortageInput.powerUsed = 100.0;
    const RuntimeProductionTimingResult slightShortage =
        runtimeCalculateProductionTiming(slightShortageInput);
    success &= expect(
        slightShortage.valid
            && closeTo(slightShortage.lowPowerSpeed, 0.8),
        "small shortage did not clamp to the stock low-power maximum"
    );
    return success;
}

bool testLowPowerPenaltyCurve() {
    bool success = true;
    struct PowerCase {
        double produced;
        double used;
        double expectedSpeed;
    };
    constexpr PowerCase cases[] = {
        {0.0, 0.0, 1.0},
        {0.0, 100.0, 0.5},
        {25.0, 100.0, 0.5},
        {50.0, 100.0, 0.5},
        {60.0, 100.0, 0.6},
        {70.0, 100.0, 0.7},
        {75.0, 100.0, 0.75},
        {80.0, 100.0, 0.8},
        {99.0, 100.0, 0.8},
        {100.0, 100.0, 1.0},
        {150.0, 100.0, 1.0}
    };
    for (const PowerCase& power : cases) {
        RuntimeProductionTimingInput input = stockInput();
        input.powerProduced = power.produced;
        input.powerUsed = power.used;
        const RuntimeProductionTimingResult result =
            runtimeCalculateProductionTiming(input);
        success &= expect(
            result.valid
                && closeTo(result.lowPowerSpeed, power.expectedSpeed)
                && closeTo(result.dynamicSpeed, power.expectedSpeed),
            "stock low-power curve at " + std::to_string(power.produced)
                + "/" + std::to_string(power.used)
        );
    }

    RuntimeProductionTimingInput shortageInput = stockInput();
    shortageInput.powerProduced = 70.0;
    const RuntimeProductionTimingResult shortage =
        runtimeCalculateProductionTiming(shortageInput);
    success &= expect(
        shortage.valid
            && closeTo(shortage.effective.rawLegacyTicks, 900.0)
            && shortage.effective.legacyTicks == 864
            && shortage.effective.fixedTicks == 1728
            && shortage.productionRate.numerator == 11
            && shortage.productionRate.denominator == 16,
        "30 percent power shortage did not propagate to production timing"
    );

    shortageInput.poweredCompatibleFactoryCount = 2;
    const RuntimeProductionTimingResult factories =
        runtimeCalculateProductionTiming(shortageInput);
    success &= expect(
        factories.valid && closeTo(factories.multipleFactorySpeed, 1.25)
            && closeTo(factories.lowPowerSpeed, 0.7)
            && closeTo(factories.dynamicSpeed, 0.875),
        "low-power penalty did not compose with the factory speed bonus"
    );

    RuntimeProductionTimingInput modifierInput = stockInput();
    modifierInput.powerProduced = 70.0;
    // Widen the clamps so they cannot conceal the authored modifier's effect.
    modifierInput.rules.minLowPowerProductionSpeed = 0.1;
    modifierInput.rules.maxLowPowerProductionSpeed = 1.0;
    modifierInput.rules.lowPowerPenaltyModifier = 2.0;
    const RuntimeProductionTimingResult doublePenalty =
        runtimeCalculateProductionTiming(modifierInput);
    success &= expect(
        doublePenalty.valid && closeTo(doublePenalty.lowPowerSpeed, 0.4),
        "modifier 2 must turn a 30 percent shortage into a 60 percent penalty"
    );
    modifierInput.rules.lowPowerPenaltyModifier = 0.5;
    const RuntimeProductionTimingResult halfPenalty =
        runtimeCalculateProductionTiming(modifierInput);
    success &= expect(
        halfPenalty.valid && closeTo(halfPenalty.lowPowerSpeed, 0.85),
        "modifier .5 must turn a 30 percent shortage into a 15 percent penalty"
    );
    return success;
}

bool testMinimumAndSaturation() {
    bool success = true;
    const RuntimeProductionTimingResult free =
        runtimeCalculateProductionTiming(stockInput(0));
    success &= expect(
        free.valid && closeTo(free.base.rawLegacyTicks, 0.0)
            && free.base.legacyTicks == 54
            && free.base.fixedTicks == 108
            && free.effective == free.base,
        "zero-cost production did not retain the 54 legacy tick minimum"
    );

    RuntimeProductionTimingInput extreme = stockInput(
        std::numeric_limits<int>::max()
    );
    extreme.rules.buildSpeed = 100.0;
    extreme.rules.wallBuildSpeedCoefficient = 100.0;
    extreme.buildTimeMultiplier = 100.0;
    extreme.wall = true;
    extreme.fixedTickHz = 1000;
    const RuntimeProductionTimingResult saturated =
        runtimeCalculateProductionTiming(extreme);
    success &= expect(
        saturated.valid && saturated.saturated
            && std::isfinite(saturated.base.rawLegacyTicks)
            && saturated.base.legacyTicks == std::numeric_limits<int>::max()
            && saturated.base.fixedTicks == std::numeric_limits<int>::max()
            && saturated.effective.legacyTicks
                == std::numeric_limits<int>::max()
            && saturated.effective.fixedTicks
                == std::numeric_limits<int>::max(),
        "extreme production timing did not saturate finitely"
    );
    return success;
}

bool testBoundedRateApproximation() {
    RuntimeProductionTimingInput input = stockInput(86500);
    input.powerProduced = 99.0;
    input.powerUsed = 100.0;
    const RuntimeProductionTimingResult result =
        runtimeCalculateProductionTiming(input);
    const RuntimeProductionTimingResult repeated =
        runtimeCalculateProductionTiming(input);
    return expect(
        result.valid && result == repeated
            && result.base.legacyTicks / 54 == 1009
            && result.effective.legacyTicks / 54 == 1261
            && result.productionRate.numerator >= 1
            && result.productionRate.numerator <= 1000
            && result.productionRate.denominator >= 1
            && result.productionRate.denominator <= 1000
            && std::gcd(
                result.productionRate.numerator,
                result.productionRate.denominator
            ) == 1U
            && globallyMinimumRate(result),
        "bounded production rate was not stable, reduced, and minimum-error"
    );
}

bool testInvalidInputs() {
    bool success = true;
    RuntimeProductionTimingInput invalid = stockInput();
    invalid.rules.buildSpeed = std::numeric_limits<double>::quiet_NaN();
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidRules,
        "NaN ProductionRules value was accepted"
    );

    invalid = stockInput();
    invalid.rules.minLowPowerProductionSpeed = 0.9;
    invalid.rules.maxLowPowerProductionSpeed = 0.8;
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidRules,
        "inverted low-power range was accepted"
    );

    invalid = stockInput();
    invalid.cost = -1;
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidCost,
        "negative cost was accepted"
    );

    invalid = stockInput();
    invalid.buildTimeMultiplier = 0.0;
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidBuildTimeMultiplier,
        "zero BuildTimeMultiplier was accepted"
    );

    invalid = stockInput();
    invalid.poweredCompatibleFactoryCount =
        runtimeProductionTimingMaximumFactories + 1;
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidFactoryCount,
        "oversized powered factory count was accepted"
    );

    invalid = stockInput();
    invalid.powerProduced = std::numeric_limits<double>::infinity();
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidPowerSnapshot,
        "infinite power snapshot was accepted"
    );

    invalid = stockInput();
    invalid.fixedTickHz = 0;
    success &= expect(
        runtimeCalculateProductionTiming(invalid).veto
            == RuntimeProductionTimingVeto::InvalidFixedTickHz,
        "zero fixed tick rate was accepted"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testStockBaseAndMultipliers();
    success &= testDynamicSpeed();
    success &= testLowPowerPenaltyCurve();
    success &= testMinimumAndSaturation();
    success &= testBoundedRateApproximation();
    success &= testInvalidInputs();
    success &= expect(
        std::string(runtimeProductionTimingVetoName(
            RuntimeProductionTimingVeto::InvalidPowerSnapshot
        )) == "InvalidPowerSnapshot",
        "timing veto diagnostic name drifted"
    );
    return success ? 0 : 1;
}
