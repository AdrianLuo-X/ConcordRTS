#pragma once
#include "world/values/RuntimeCellPoint.h"
#include <string>

// Original owned AI domain value; field order and defaults are unchanged.
struct RuntimeComputerAiState {
    bool initialized = false;
    std::string owner;
    std::string country;
    std::string difficulty = "Medium";
    RuntimeCellPoint baseCenter;
    int credits = 0;
    int nextThinkTick = 0;
    int nextIncomeTick = 0; // Legacy save field; real mining has no income deadline.
    int nextBuildTick = 0;
    int nextAttackTick = 0;
    int nextRepairTick = 0;
    int productionSerial = 0;
    int incomeEarned = 0;
    int creditsSpent = 0;
    int structuresBuilt = 0;
    int infantryBuilt = 0;
    int unitsBuilt = 0;
    int freeUnitsReceived = 0;
    int attackWaves = 0;
    int attacksOrdered = 0;
    int defenseOrders = 0;
    int tacticalOrders = 0;
    int repairsPerformed = 0;
    int stalledBuilds = 0;
    int lastTargetObjectId = -1;
    std::string lastBuildType;
    std::string status = "waiting for skirmish start";
};
