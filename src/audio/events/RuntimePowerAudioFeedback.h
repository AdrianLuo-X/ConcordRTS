#pragma once

#include "data/rules/RulesDatabase.h"

#include <string>

std::string runtimePoweredUnitTransitionSound(
    const UnitType& unit,
    bool transitionStateObserved,
    bool localOwner,
    bool wasOperational,
    bool isOperational
);

std::string runtimeBuildingPowerTransitionSound(
    const BuildingType& building,
    bool transitionStateObserved,
    bool localOwner,
    bool wasOperational,
    bool isOperational
);
