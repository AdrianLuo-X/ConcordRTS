#include "simulation/economy/RuntimeEconomyFixedStep.h"

#include <algorithm>
#include <limits>

namespace { constexpr int runtimeMaximumCreditBalance = std::numeric_limits<int>::max(); }

int runtimeEconomyBalanceAfterDeposit(
    int currentBalance,
    long long deposit
) {
    const int balance = std::clamp(
        currentBalance,
        0,
        runtimeMaximumCreditBalance
    );
    if (deposit <= 0) {
        return balance;
    }
    const long long available =
        static_cast<long long>(runtimeMaximumCreditBalance - balance);
    if (deposit >= available) {
        return runtimeMaximumCreditBalance;
    }
    return balance + static_cast<int>(deposit);
}

int runtimeEconomyScaleStoredValue(
    int value,
    int acceptedUnits,
    int totalUnits
) {
    if (value <= 0 || acceptedUnits <= 0 || totalUnits <= 0) {
        return 0;
    }
    if (acceptedUnits >= totalUnits) {
        return value;
    }
    const long long scaled =
        (static_cast<long long>(value) * acceptedUnits + totalUnits - 1)
        / totalUnits;
    return static_cast<int>(std::max<long long>(0, scaled));
}

bool spendRuntimeHouseCredits(
    HouseEconomyContext& house,
    int requestedAmount
) {
    const int amount = std::max(0, requestedAmount);
    if (house.credits < amount) {
        return false;
    }

    house.credits -= amount;
    const int previousStoredValue =
        std::max(0, house.storedResourceValue);
    const int storedSpend = std::min(amount, previousStoredValue);
    if (storedSpend <= 0) {
        return true;
    }

    const int remainingStoredValue = previousStoredValue - storedSpend;
    house.storedResourceUnits = runtimeEconomyScaleStoredValue(
        std::max(0, house.storedResourceUnits),
        remainingStoredValue,
        previousStoredValue
    );
    house.storedResourceGemUnits = std::clamp(
        runtimeEconomyScaleStoredValue(
            std::max(0, house.storedResourceGemUnits),
            remainingStoredValue,
            previousStoredValue
        ),
        0,
        house.storedResourceUnits
    );
    house.storedResourceValue = remainingStoredValue;
    return true;
}

RuntimeEconomyFixedStepResult advanceRuntimeHouseEconomyFixedStep(
    HouseEconomyContext& house, bool simplifiedHarvesterIncomeEnabled,
    int& incomeCooldownTicks) {
    RuntimeEconomyFixedStepResult result;
    if (simplifiedHarvesterIncomeEnabled
        && house.harvesters > 0
        && house.refineries > 0) {
        if (incomeCooldownTicks > 0) {
            --incomeCooldownTicks;
        } else {
            const int income =
                std::min(420, 140 * std::max(1, house.harvesters));
            house.credits = runtimeEconomyBalanceAfterDeposit(house.credits, income);
            incomeCooldownTicks = 180;
            ++house.resourceTrips;
            result.status =
                "harvester income +" + std::to_string(income)
                + " credits="
                + std::to_string(house.credits);
            result.changed = true;
        }
    }

    return result;
}

