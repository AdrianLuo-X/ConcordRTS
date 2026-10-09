#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

struct CampaignProgressData {
    int version = 1;
    std::string campaignId;
    std::string difficulty = "Normal";
    std::string currentMissionId;
    bool campaignComplete = false;
    std::vector<std::string> completedMissionIds;
    std::map<std::string, int> attemptsByMission;
};

struct CampaignSaveInfo {
    std::string path;
    std::string campaignId;
    std::string missionId;
    std::string difficulty;
    std::string mapPath;
    int tick = 0;
    bool valid = false;
    std::string error;
};

struct SkirmishSavePlayerInfo {
    bool enabled = false;
    bool human = false;
    std::string name;
    std::string country = "Random";
    std::string color = "Random";
    std::string team = "-";
    std::string difficulty = "Medium";
    int startPosition = 0;
};

// Startup context persisted beside the authoritative world state.  A live
// in-match load already owns this context; a main-menu load must reconstruct
// it before mounting the map/rules and materializing the saved actors.
struct SkirmishSaveInfo {
    std::string path;
    std::string mapPath;
    int saveVersion = 0;
    int tick = 0;
    std::string gameMode = "Battle";
    std::string gameModeRulesOverride;
    std::string playerCountry;
    std::string playerColor;
    int fixedTickHz = 30;
    int gameSpeed = 3;
    int techLevel = 10;
    int startingCredits = 10000;
    int startingUnits = 10;
    bool shortGame = true;
    bool mcvRepacks = true;
    bool crates = true;
    bool superweapons = false;
    bool buildOffAlly = false;
    bool computerAiEnabled = true;
    bool noCooldown = false;
    std::vector<SkirmishSavePlayerInfo> players;
    bool valid = false;
    std::string error;
};

std::string campaignProgressPath(const std::string& localDataRoot);
std::string campaignSavePath(
    const std::string& localDataRoot,
    const std::string& campaignId,
    const std::string& missionId
);

bool loadCampaignProgress(
    const std::string& localDataRoot,
    CampaignProgressData& progress,
    std::string& errorMessage
);
bool writeCampaignProgress(
    std::ostream& output,
    const CampaignProgressData& progress,
    std::string& errorMessage
);
bool saveCampaignProgress(
    const std::string& localDataRoot,
    const CampaignProgressData& progress,
    std::string& errorMessage
);

std::vector<CampaignSaveInfo> listCampaignSaves(
    const std::string& localDataRoot
);
bool inspectCampaignSave(
    const std::string& path,
    CampaignSaveInfo& info
);

std::string skirmishSavePath(const std::string& localDataRoot);
std::vector<SkirmishSaveInfo> listSkirmishSaves(
    const std::string& localDataRoot
);
bool inspectSkirmishSave(
    const std::string& path,
    SkirmishSaveInfo& info
);

void markCampaignMissionResult(
    CampaignProgressData& progress,
    const std::string& campaignId,
    const std::string& missionId,
    const std::string& difficulty,
    const std::string& nextMissionId,
    bool victory
);
