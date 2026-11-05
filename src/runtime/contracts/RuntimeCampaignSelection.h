#pragma once
#include "data/campaign/CampaignDatabase.h"
#include "runtime/contracts/RuntimeSkirmishSelection.h"
#include <string>

// The original result fields, order and defaults; no native parent or view state.
struct RuntimeCampaignSelection {
    bool accepted = false;
    bool loadedFromSave = false;
    bool loadedSkirmishSave = false;
    int loadedSkirmishFixedTickHz = 30;
    int loadedSkirmishTechLevel = 10;
    std::string savePath;
    RuntimeSkirmishSelection skirmish;
    CampaignMissionDefinition mission;

    std::string campaignId;
    std::string missionId;
    std::string sourceId;
    std::string difficulty = "Normal";
    std::string playerHouse;
    std::string nextMissionId;
    std::string introVideoPath;
    std::string postScoreVideoPath;
    std::string briefingKey;
    std::string mapPath;
    std::string playerCountry;
    std::wstring playerCountryDisplayName;
    std::wstring playerCountryBrief;
    std::string loadingScreenAssetId;
};
