#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

struct RuntimeComputerTacticHistory {
    double weight = 0.0;
    int executions = 0;
    int successes = 0;
    int failures = 0;
};

struct RuntimeComputerTacticTeam {
    int instanceId = 0;
    std::string triggerId;
    std::string teamTypeId;
    std::string targetOwner;
    std::vector<int> objectIds;
    std::vector<int> joiningObjectIds;
    std::vector<std::string> missingMembers;
    int createdTick = 0;
    int lastProgressTick = 0;
    int missionInstanceId = -1;
    bool secondary = false;
    bool baseDefense = false;
    bool launched = false;
    bool outcomeReported = false;
    bool missionFinished = false;
    bool achievedGreatSuccess = false;
};

struct RuntimeComputerTacticProduction {
    int teamInstanceId = -1;
    std::string typeId;
    int remainingTicks = 0;
    int paidCost = 0;
    int totalTicks = 0;
};

struct RuntimeComputerTacticsState {
    struct CaptureDecision {
        int controllerId = -1;
        int startedTick = 0;
        int decision = 5;
    };
    int nextTriggerTick = 0;
    int selectionSerial = 0;
    int nextTeamInstanceId = 1;
    // One shared infantry, vehicle/naval and aircraft order per house.
    std::array<RuntimeComputerTacticProduction, 3> production;
    std::map<int, RuntimeComputerTacticTeam> teams;
    std::map<std::string, RuntimeComputerTacticHistory> history;
    // Process each real control relationship once, including across a save.
    std::map<int, CaptureDecision> captureDecisions;
};
