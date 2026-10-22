#pragma once

#include <string>

// A synchronous borrow of one house's harvesting balance and storage record.
// No production catalog, technology tree, presentation model or host is borrowed.
struct HouseEconomyContext {
    const std::string& owner;
    int& credits;
    int& storedResourceUnits;
    int& storedResourceValue;
    int& storedResourceGemUnits;
    int& resourceTrips;
    int& harvesterTeleports;
    int& resourceStorageCapacity;
    const int& harvesters;
    const int& refineries;
    bool computerControlled = false;
};

struct HouseResourceDelivery {
    int capacity = 0;
    int storedUnits = 0;
    int storedValue = 0;
    int storedGemUnits = 0;
    int overflowUnits = 0;
};

void normalizeHouseResourceStorage(HouseEconomyContext& context);
HouseResourceDelivery recordHouseResourceDelivery(HouseEconomyContext& context,
    int cargoUnits, int cargoGemUnits, int deliveredValue);
int houseOrePurifierBonusValue(int baseValue, int purifierCount, double bonus);
