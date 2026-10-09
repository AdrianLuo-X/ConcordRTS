#pragma once

#include "GameWorld.h"
#include "MapDatabase.h"

#include <iosfwd>
#include <string>
#include <vector>

struct VictoryOwnerState {
    std::string owner;
    int structures = 0;
    int units = 0;
    int infantry = 0;
    int aliveObjects = 0;
    int victoryObjects = 0;
    int nonVictoryStructures = 0;
    bool neutral = false;
    bool combatEligible = false;
    bool defeated = true;
};

struct VictoryConditionReport {
    bool success = false;
    std::string errorMessage;
    std::string trackedHouse;
    std::string mapName;
    std::string theater;
    int triggerCount = 0;
    int totalOwners = 0;
    int neutralOwners = 0;
    int activeCombatOwners = 0;
    bool missionMap = false;
    bool skirmishRulesActive = false;
    bool trackedHousePresent = false;
    bool trackedHouseDefeated = true;
    bool allEnemiesDefeated = false;
    bool skirmishVictory = false;
    bool skirmishDefeat = false;
    std::vector<VictoryOwnerState> owners;
    std::vector<std::string> remainingEnemyOwners;
};

VictoryConditionReport buildVictoryConditionReport(
    const GameWorld& world,
    const MapDatabase& map,
    const std::string& trackedHouse
);

void printVictoryConditionReport(
    std::ostream& output,
    const VictoryConditionReport& report
);
