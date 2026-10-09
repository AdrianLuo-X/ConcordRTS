#pragma once

#include "RulesDatabase.h"

#include <string>
#include <vector>

struct CombatGridPoint {
    int x = 0;
    int y = 0;
};

struct CombatSandboxConfig {
    std::string attackerId = "MTNK";
    std::string defenderId = "HTNK";
    int maxTicks = 600;
    int startDistanceCells = 8;
};

struct CombatSandboxEvent {
    int tick = 0;
    std::string text;
};

struct CombatSandboxReport {
    bool success = false;
    std::string errorMessage;
    std::string attackerId;
    std::string defenderId;
    int attackerInstanceId = 1;
    int defenderInstanceId = 2;
    int attackerStartingHp = 0;
    int defenderStartingHp = 0;
    int attackerFinalHp = 0;
    int defenderFinalHp = 0;
    CombatGridPoint attackerStartPosition;
    CombatGridPoint defenderStartPosition;
    CombatGridPoint attackerFinalPosition;
    CombatGridPoint defenderFinalPosition;
    bool attackerAlive = false;
    bool defenderAlive = false;
    int selectedInstanceId = 1;
    int ticksSimulated = 0;
    std::string winner;
    std::vector<CombatSandboxEvent> events;
};

CombatSandboxReport runCombatSandbox(
    const RulesDatabase& rules,
    const CombatSandboxConfig& config
);
