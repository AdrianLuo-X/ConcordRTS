#pragma once

#include "simulation/economy/HouseEconomyContext.h"
#include <string>

// Per-house economic work at the existing fixed-step income site. The borrow
// never owns a host, production catalog, world, UI, native handle or callback.
struct RuntimeEconomyFixedStepResult {
    bool changed = false;
    std::string status;
};

int runtimeEconomyBalanceAfterDeposit(int balance, long long amount);
int runtimeEconomyScaleStoredValue(int value, int acceptedUnits, int totalUnits);
bool spendRuntimeHouseCredits(HouseEconomyContext& house, int requestedAmount);

// A cooldown decrement intentionally does not report changed. Income still
// precedes this house's production transaction, including in lightweight ticks.
RuntimeEconomyFixedStepResult advanceRuntimeHouseEconomyFixedStep(
    HouseEconomyContext& house, bool simplifiedHarvesterIncomeEnabled,
    int& incomeCooldownTicks);
