#pragma once

#include "CampaignDatabase.h"
#include "RuntimeShell.h"
#include "RuntimeSkirmishMenu.h"

#include <string>

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

// Opens the RA2/YR campaign selector or compatible-save browser using the
// retail 800x600 menu assets. Back and Esc return through the pages to the
// existing hostWindow, retaining the same process and shell.
RuntimeCampaignSelection runRuntimeCampaignMenu(
    const RuntimeConfig& config,
    void* hostWindow = nullptr,
    bool loadGame = false
);

int runRuntimeCampaignMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
);
