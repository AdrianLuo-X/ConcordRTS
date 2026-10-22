#include "simulation/economy/HouseEconomyContext.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <tuple>

#include "HouseEconomyStorageOldOracle.inl"

int main() {
    int cases = 0, failures = 0;
    for (int capacity : {0, 1, 7, 100}) for (int stored : {-3, 0, 4, 11})
    for (int value : {-9, 0, 37}) for (int units : {-2, 0, 1, 5, 20})
    for (int gems : {-1, 0, 2, 50}) for (int delivered : {0, 1, 51, 999}) {
        ++cases;
        S16OldStorageState expected;
        expected.capacity = capacity; expected.economy.storedResourceUnits = stored;
        expected.economy.storedResourceValue = value; expected.economy.storedResourceGemUnits = stored + 1;
        auto actual = expected;
        auto& a = actual.economy;
        HouseEconomyContext context{a.owner, a.credits, a.storedResourceUnits, a.storedResourceValue,
            a.storedResourceGemUnits, a.resourceTrips, a.harvesterTeleports,
            a.resourceStorageCapacity, a.harvesters, a.refineries, true};
        context.resourceStorageCapacity = capacity;
        const auto result = recordHouseResourceDelivery(context, units, gems, delivered);
        s16OldrecordRuntimeResourceDeliveryToStorage(expected, units, gems, delivered);
        const auto& e = expected.economy;
        if (std::tie(a.storedResourceUnits, a.storedResourceValue, a.storedResourceGemUnits,
                a.resourceStorageCapacity, a.credits, a.resourceTrips, a.harvesterTeleports)
            != std::tie(e.storedResourceUnits, e.storedResourceValue, e.storedResourceGemUnits,
                e.resourceStorageCapacity, e.credits, e.resourceTrips, e.harvesterTeleports)
            || std::tie(result.capacity, result.storedUnits, result.storedValue, result.storedGemUnits, result.overflowUnits)
            != std::tie(e.lastResourceStorageCapacity, e.lastResourceStoredUnits, e.lastResourceStoredValue,
                e.lastResourceStoredGemUnits, e.lastResourceStorageOverflowUnits)) ++failures;
    }
    for (int base : {-1, 0, 1, 25, 175, 1000}) for (int owned : {0, 1, 3})
    for (int virtualCount : {0, 2, 4, 8}) for (double bonus : {-0.5, 0.0, 0.25, 0.37}) {
        ++cases;
        S16OldStorageState old; old.ownedPurifiers = owned; old.virtualPurifiers = virtualCount; old.rules.bonus = bonus;
        if (houseOrePurifierBonusValue(base, owned + virtualCount, bonus)
            != s16OldruntimeOrePurifierBonusValue(old, base)) ++failures;
    }
    std::cout << "House economy context contract: cases=" << cases << " failures=" << failures
        << " literalOldStorage=1 sameProductionCpp=1\n";
    return failures ? 1 : 0;
}
