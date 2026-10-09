#pragma once

#include "IniFile.h"
#include "RulesDatabase.h"

#include <string>
#include <vector>

struct AiTriggerSandboxConfig {
    std::string aiHouseId = "Russians";
    std::string enemyHouseId = "Americans";
    std::string mcvId = "SMCV";
    int startingCredits = 10000;
    int maxTicks = 900;
    int attackWaveSize = 3;
};

struct AiTriggerSandboxEvent {
    int tick = 0;
    std::string text;
};

struct AiTriggerParsedSummary {
    bool mapLoaded = false;
    int triggers = 0;
    int events = 0;
    int actions = 0;
    int tags = 0;
    int scriptTypes = 0;
    int taskForces = 0;
    int teamTypes = 0;
    int aiTriggerTypes = 0;
    int aiTriggerEnables = 0;
    int createTeamActions = 0;
    int linkedTriggerReferences = 0;
};

struct AiTriggerTeamSummary {
    std::string id;
    std::string name;
    std::string house;
    std::string taskForceId;
    std::string taskForceName;
    std::string scriptId;
    std::string scriptName;
    int priority = 0;
    int max = 0;
    int unitCount = 0;
    int scriptStepCount = 0;
};

struct AiTriggerSandboxReport {
    bool success = false;
    std::string errorMessage;
    std::string aiHouseId;
    std::string enemyHouseId;
    std::string mcvId;
    int ticksSimulated = 0;
    bool economyComplete = false;
    bool attackWaveCreated = false;
    bool attackWaveLaunched = false;
    bool victoryTriggerFired = false;
    std::string winner;
    AiTriggerParsedSummary parsed;
    AiTriggerTeamSummary selectedTeam;
    std::vector<AiTriggerSandboxEvent> events;
};

AiTriggerSandboxReport runAiTriggerSandbox(
    const RulesDatabase& rules,
    const IniFile* mapIni,
    const AiTriggerSandboxConfig& config
);
