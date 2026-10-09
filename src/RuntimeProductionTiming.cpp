#include "RuntimeProductionTiming.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace {

constexpr double maximumProductionCoefficient = 100.0;
constexpr double maximumProductionRatio = 1.0;
constexpr int maximumFixedTickHz = 1000;

bool finitePositiveAtMost(double value, double maximum) {
    return std::isfinite(value) && value > 0.0 && value <= maximum;
}

bool validProductionRules(const ProductionRules& rules) {
    return finitePositiveAtMost(
            rules.buildSpeed,
            maximumProductionCoefficient
        )
        && finitePositiveAtMost(
            rules.multipleFactory,
            maximumProductionRatio
        )
        && finitePositiveAtMost(
            rules.wallBuildSpeedCoefficient,
            maximumProductionCoefficient
        )
        && finitePositiveAtMost(
            rules.lowPowerPenaltyModifier,
            maximumProductionCoefficient
        )
        && finitePositiveAtMost(
            rules.minLowPowerProductionSpeed,
            maximumProductionRatio
        )
        && finitePositiveAtMost(
            rules.maxLowPowerProductionSpeed,
            maximumProductionRatio
        )
        && rules.minLowPowerProductionSpeed
            <= rules.maxLowPowerProductionSpeed;
}

RuntimeProductionTimingResult invalidResult(
    RuntimeProductionTimingVeto veto,
    std::string reason
) {
    RuntimeProductionTimingResult result;
    result.veto = veto;
    result.reason = std::move(reason);
    return result;
}

double finiteDouble(long double value, bool& saturated) {
    const long double maximum =
        static_cast<long double>(std::numeric_limits<double>::max());
    if (!std::isfinite(value) || value > maximum) {
        saturated = true;
        return std::numeric_limits<double>::max();
    }
    if (value < 0.0L) {
        saturated = true;
        return 0.0;
    }
    return static_cast<double>(value);
}

long double positiveSpeedFromLog(long double logarithm, bool& saturated) {
    const long double maximum =
        static_cast<long double>(std::numeric_limits<double>::max());
    const long double minimum =
        static_cast<long double>(std::numeric_limits<double>::min());
    const long double maximumLog = std::log(maximum);
    const long double minimumLog = std::log(minimum);
    if (!std::isfinite(logarithm) || logarithm > maximumLog) {
        saturated = true;
        return maximum;
    }
    if (logarithm < minimumLog) {
        saturated = true;
        return minimum;
    }
    const long double value = std::exp(logarithm);
    if (!std::isfinite(value) || value > maximum) {
        saturated = true;
        return maximum;
    }
    if (value < minimum) {
        saturated = true;
        return minimum;
    }
    return value;
}

int quantizedLegacyTicks(long double rawTicks, bool& saturated) {
    constexpr int quantum = runtimeProductionTimingQuantumLegacyTicks;
    constexpr int maximumTicks = std::numeric_limits<int>::max();
    if (!std::isfinite(rawTicks)) {
        saturated = true;
        return maximumTicks;
    }
    if (rawTicks <= static_cast<long double>(quantum)) {
        return quantum;
    }

    long double quantumUnits = rawTicks / static_cast<long double>(quantum);
    const long double nearestUnits = std::round(quantumUnits);
    const long double tolerance =
        64.0L * static_cast<long double>(
            std::numeric_limits<double>::epsilon()
        ) * std::max(1.0L, std::abs(quantumUnits));
    if (std::abs(quantumUnits - nearestUnits) <= tolerance) {
        quantumUnits = nearestUnits;
    }
    const long double buckets = std::floor(quantumUnits);
    constexpr int maximumWholeBuckets = maximumTicks / quantum;
    if (buckets > static_cast<long double>(maximumWholeBuckets)) {
        saturated = true;
        return maximumTicks;
    }
    return std::max(
        quantum,
        static_cast<int>(buckets) * quantum
    );
}

int fixedTicksFromLegacy(
    int legacyTicks,
    int fixedTickHz,
    bool& saturated
) {
    const std::int64_t scaled =
        static_cast<std::int64_t>(legacyTicks) * fixedTickHz;
    const std::int64_t ticks =
        (scaled + runtimeProductionTimingLegacyTickHz - 1)
        / runtimeProductionTimingLegacyTickHz;
    if (ticks > std::numeric_limits<int>::max()) {
        saturated = true;
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(ticks);
}

RuntimeProductionTimingDuration makeDuration(
    long double rawLegacyTicks,
    int fixedTickHz,
    bool& saturated
) {
    RuntimeProductionTimingDuration duration;
    duration.rawLegacyTicks = finiteDouble(rawLegacyTicks, saturated);
    duration.legacyTicks = quantizedLegacyTicks(rawLegacyTicks, saturated);
    duration.fixedTicks = fixedTicksFromLegacy(
        duration.legacyTicks,
        fixedTickHz,
        saturated
    );
    return duration;
}

RuntimeProductionRate boundedProductionRate(
    int baseFixedTicks,
    int effectiveFixedTicks
) {
    const std::uint64_t base = static_cast<std::uint64_t>(baseFixedTicks);
    const std::uint64_t effective =
        static_cast<std::uint64_t>(effectiveFixedTicks);
    const std::uint64_t exactGcd = std::gcd(base, effective);
    const std::uint64_t exactNumerator = base / exactGcd;
    const std::uint64_t exactDenominator = effective / exactGcd;
    if (exactNumerator <= 1000U && exactDenominator <= 1000U) {
        return {
            static_cast<std::uint32_t>(exactNumerator),
            static_cast<std::uint32_t>(exactDenominator)
        };
    }

    struct Candidate {
        std::uint32_t numerator = 1;
        std::uint32_t denominator = 1;
        std::uint64_t difference = 0;
        bool set = false;
    } best;
    const auto consider = [&](std::uint64_t rawNumerator,
                              std::uint64_t rawDenominator) {
        rawNumerator = std::clamp<std::uint64_t>(rawNumerator, 1U, 1000U);
        const std::uint64_t divisor = std::gcd(
            rawNumerator,
            rawDenominator
        );
        const std::uint32_t numerator = static_cast<std::uint32_t>(
            rawNumerator / divisor
        );
        const std::uint32_t denominator = static_cast<std::uint32_t>(
            rawDenominator / divisor
        );
        const std::uint64_t left =
            static_cast<std::uint64_t>(numerator) * effective;
        const std::uint64_t right =
            base * static_cast<std::uint64_t>(denominator);
        const std::uint64_t difference = left >= right
            ? left - right
            : right - left;

        bool better = !best.set;
        if (best.set) {
            const std::uint64_t candidateScaled =
                difference * best.denominator;
            const std::uint64_t bestScaled =
                best.difference * denominator;
            if (candidateScaled != bestScaled) {
                better = candidateScaled < bestScaled;
            } else {
                const std::uint32_t candidateComplexity =
                    numerator + denominator;
                const std::uint32_t bestComplexity =
                    best.numerator + best.denominator;
                better = candidateComplexity < bestComplexity
                    || (candidateComplexity == bestComplexity
                        && (denominator < best.denominator
                            || (denominator == best.denominator
                                && numerator < best.numerator)));
            }
        }
        if (better) {
            best = {numerator, denominator, difference, true};
        }
    };

    for (std::uint64_t denominator = 1; denominator <= 1000U;
        ++denominator) {
        const std::uint64_t scaled = base * denominator;
        const std::uint64_t lowerNumerator = scaled / effective;
        consider(lowerNumerator, denominator);
        if (lowerNumerator < std::numeric_limits<std::uint64_t>::max()) {
            consider(lowerNumerator + 1U, denominator);
        }
    }
    return {best.numerator, best.denominator};
}

} // namespace

RuntimeProductionTimingResult runtimeCalculateProductionTiming(
    const RuntimeProductionTimingInput& input
) {
    if (!validProductionRules(input.rules)) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidRules,
            "production timing rules are invalid"
        );
    }
    if (input.cost < 0) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidCost,
            "production timing cost is negative"
        );
    }
    if (!finitePositiveAtMost(
            input.buildTimeMultiplier,
            maximumProductionCoefficient
        )) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidBuildTimeMultiplier,
            "production timing BuildTimeMultiplier is invalid"
        );
    }
    if (input.poweredCompatibleFactoryCount < 0
        || input.poweredCompatibleFactoryCount
            > runtimeProductionTimingMaximumFactories) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidFactoryCount,
            "production timing powered factory count is invalid"
        );
    }
    if (!std::isfinite(input.powerProduced)
        || !std::isfinite(input.powerUsed)
        || input.powerProduced < 0.0
        || input.powerUsed < 0.0) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidPowerSnapshot,
            "production timing power snapshot is invalid"
        );
    }
    if (input.fixedTickHz < 1 || input.fixedTickHz > maximumFixedTickHz) {
        return invalidResult(
            RuntimeProductionTimingVeto::InvalidFixedTickHz,
            "production timing fixed tick rate is invalid"
        );
    }

    RuntimeProductionTimingResult result;
    result.valid = true;
    result.veto = RuntimeProductionTimingVeto::None;
    result.reason = "production timing calculated";

    const int additionalFactories = std::max(
        0,
        input.poweredCompatibleFactoryCount - 1
    );
    const long double multipleFactoryLog =
        -static_cast<long double>(additionalFactories)
        * std::log(static_cast<long double>(input.rules.multipleFactory));
    const long double multipleFactorySpeed = positiveSpeedFromLog(
        multipleFactoryLog,
        result.saturated
    );
    result.multipleFactorySpeed = finiteDouble(
        multipleFactorySpeed,
        result.saturated
    );

    long double lowPowerSpeed = 1.0L;
    if (input.powerUsed > 0.0
        && input.powerProduced < input.powerUsed) {
        const long double producedRatio = std::min(
            1.0L,
            static_cast<long double>(input.powerProduced)
                / static_cast<long double>(input.powerUsed)
        );
        const long double shortage = 1.0L - producedRatio;
        // rulesmd.ini defines the penalty as shortage * modifier, applied once.
        const long double unclamped = 1.0L
            - static_cast<long double>(input.rules.lowPowerPenaltyModifier)
                * shortage;
        lowPowerSpeed = std::clamp(
            unclamped,
            static_cast<long double>(
                input.rules.minLowPowerProductionSpeed
            ),
            static_cast<long double>(
                input.rules.maxLowPowerProductionSpeed
            )
        );
    }
    result.lowPowerSpeed = finiteDouble(lowPowerSpeed, result.saturated);

    const long double dynamicLog = multipleFactoryLog
        + std::log(lowPowerSpeed);
    const long double dynamicSpeed = positiveSpeedFromLog(
        dynamicLog,
        result.saturated
    );
    result.dynamicSpeed = finiteDouble(dynamicSpeed, result.saturated);

    long double baseRawLegacyTicks = 0.0L;
    if (input.cost > 0) {
        baseRawLegacyTicks =
            static_cast<long double>(input.cost)
            * 60.0L
            * static_cast<long double>(input.rules.buildSpeed)
            * runtimeProductionTimingLegacyTickHz
            / 1000.0L;
    }
    baseRawLegacyTicks *= static_cast<long double>(
        input.buildTimeMultiplier
    );
    if (input.wall) {
        baseRawLegacyTicks *= static_cast<long double>(
            input.rules.wallBuildSpeedCoefficient
        );
    }
    const long double effectiveRawLegacyTicks = baseRawLegacyTicks == 0.0L
        ? 0.0L
        : baseRawLegacyTicks / dynamicSpeed;
    result.base = makeDuration(
        baseRawLegacyTicks,
        input.fixedTickHz,
        result.saturated
    );
    result.effective = makeDuration(
        effectiveRawLegacyTicks,
        input.fixedTickHz,
        result.saturated
    );
    result.productionRate = boundedProductionRate(
        result.base.fixedTicks,
        result.effective.fixedTicks
    );
    return result;
}

const char* runtimeProductionTimingVetoName(RuntimeProductionTimingVeto veto) {
    switch (veto) {
    case RuntimeProductionTimingVeto::None:
        return "None";
    case RuntimeProductionTimingVeto::InvalidRules:
        return "InvalidRules";
    case RuntimeProductionTimingVeto::InvalidCost:
        return "InvalidCost";
    case RuntimeProductionTimingVeto::InvalidBuildTimeMultiplier:
        return "InvalidBuildTimeMultiplier";
    case RuntimeProductionTimingVeto::InvalidFactoryCount:
        return "InvalidFactoryCount";
    case RuntimeProductionTimingVeto::InvalidPowerSnapshot:
        return "InvalidPowerSnapshot";
    case RuntimeProductionTimingVeto::InvalidFixedTickHz:
        return "InvalidFixedTickHz";
    }
    return "Unknown";
}
