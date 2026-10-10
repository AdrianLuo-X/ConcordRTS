#pragma once

#include "data/rules/RulesDatabase.h"

#include <string>
#include <vector>

struct EconomyGridPoint {
    int x = 0;
    int y = 0;
};

struct EconomySandboxConfig {
    std::string houseId = "Americans";
    std::string mcvId = "AMCV";
    std::string infantryId;
    std::string vehicleId;
    int startingCredits = 10000;
    int maxTicks = 600;
    int harvestTripTicks = 90;
    int harvestCredits = 700;
};

struct EconomySandboxEvent {
    int tick = 0;
    std::string text;
};

struct EconomyObjectSummary {
    int instanceId = 0;
    std::string kind;
    std::string typeId;
    EconomyGridPoint position;
};

struct EconomySandboxReport {
    bool success = false;
    std::string errorMessage;
    std::string houseId;
    std::string mcvId;
    std::string constructionYardId;
    int startingCredits = 0;
    int finalCredits = 0;
    int powerProduced = 0;
    int powerUsed = 0;
    int ticksSimulated = 0;
    int oreLoadsDelivered = 0;
    int infantryProduced = 0;
    int combatVehiclesProduced = 0;
    int harvestersOwned = 0;
    bool objectiveComplete = false;
    std::string winner;
    std::vector<EconomyObjectSummary> buildings;
    std::vector<EconomyObjectSummary> units;
    std::vector<EconomySandboxEvent> events;
};

EconomySandboxReport runEconomySandbox(
    const RulesDatabase& rules,
    const EconomySandboxConfig& config
);
