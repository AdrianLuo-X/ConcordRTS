#pragma once
#include <array>
#include <string>

struct RuntimeCampaignAiTriggerDefinition {
    std::string id;
    std::string name;
    std::string primaryTeamId;
    std::string owner;
    int techLevel = 0;
    int conditionType = -1;
    std::string conditionObject;
    int conditionNumber = 0;
    int conditionComparator = 3;
    double currentWeight = 0.0;
    double minimumWeight = 0.0;
    double maximumWeight = 0.0;
    bool availableInSkirmish = false;
    bool baseDefense = false;
    int side = -1;
    int scope = 0;
    std::string secondaryTeamId;
    std::array<bool, 3> difficultyEnabled{{true, true, true}};
    bool enabled = true;
    bool valid = false;
    int timesExecuted = 0;
    int timesCompleted = 0;
    int timesFailed = 0;
    int nextEligibleTick = 0;
};
