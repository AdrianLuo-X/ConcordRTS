#include "simulation/economy/HouseEconomyContext.h"

#include <algorithm>
#include <cmath>

namespace {
int scaleResourceValue(int value, int acceptedUnits, int totalUnits) {
    if (value <= 0 || acceptedUnits <= 0 || totalUnits <= 0) return 0;
    if (acceptedUnits >= totalUnits) return value;
    const long long scaled = (static_cast<long long>(value) * acceptedUnits + totalUnits - 1) / totalUnits;
    return static_cast<int>(std::max<long long>(0, scaled));
}
}

void normalizeHouseResourceStorage(HouseEconomyContext& context) {
    context.storedResourceUnits = std::max(0, context.storedResourceUnits);
    context.storedResourceValue = std::max(0, context.storedResourceValue);
    context.storedResourceGemUnits = std::clamp(context.storedResourceGemUnits, 0, context.storedResourceUnits);
    const int capacity = context.resourceStorageCapacity;
    if (capacity <= 0 || context.storedResourceUnits <= capacity) return;
    const int previousUnits = context.storedResourceUnits;
    context.storedResourceUnits = capacity;
    context.storedResourceValue = scaleResourceValue(context.storedResourceValue, capacity, previousUnits);
    context.storedResourceGemUnits = scaleResourceValue(context.storedResourceGemUnits, capacity, previousUnits);
}

HouseResourceDelivery recordHouseResourceDelivery(HouseEconomyContext& context,
    int cargoUnits, int cargoGemUnits, int deliveredValue) {
    normalizeHouseResourceStorage(context);
    HouseResourceDelivery result; result.capacity = context.resourceStorageCapacity;
    const int units = std::max(0, cargoUnits);
    const int gemUnits = std::clamp(cargoGemUnits, 0, units);
    const int value = std::max(0, deliveredValue);
    if (units <= 0 && value <= 0) return result;
    int acceptedUnits = units;
    if (context.resourceStorageCapacity > 0) {
        const int availableUnits = std::max(0, context.resourceStorageCapacity - context.storedResourceUnits);
        acceptedUnits = std::min(units, availableUnits);
        result.overflowUnits = std::max(0, units - acceptedUnits);
    }
    const int acceptedValue = scaleResourceValue(value, acceptedUnits, units);
    const int acceptedGemUnits = scaleResourceValue(gemUnits, acceptedUnits, units);
    context.storedResourceUnits += acceptedUnits;
    context.storedResourceValue += acceptedValue;
    context.storedResourceGemUnits = std::clamp(context.storedResourceGemUnits + acceptedGemUnits, 0, context.storedResourceUnits);
    result.storedUnits = acceptedUnits; result.storedValue = acceptedValue; result.storedGemUnits = acceptedGemUnits;
    return result;
}

int houseOrePurifierBonusValue(int baseValue, int purifierCount, double bonus) {
    if (baseValue <= 0 || purifierCount <= 0) return 0;
    const double purifierBonus = std::max(0.0, bonus);
    if (purifierBonus <= 0.0) return 0;
    const int bonusPerPurifier = std::max(0, static_cast<int>(std::floor(static_cast<double>(baseValue) * purifierBonus)));
    return purifierCount * bonusPerPurifier;
}
