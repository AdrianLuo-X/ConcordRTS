#pragma once

#include <string>
#include <vector>

struct RuntimeSkirmishPlayerSlot {
    bool enabled = false;
    bool human = false;
    std::string name;
    std::string country = "Random";
    std::string color = "Random";
    std::string team = "-";
    std::string difficulty = "Medium";
    // 0 means random; positive values are the one-based map start number.
    int startPosition = 0;
};

struct RuntimeSkirmishSelection {
    bool accepted = false;
    std::string mapPath;
    int mapStartingPointLimit = 0;
    std::string gameMode = "Battle";
    std::string gameModeRulesOverride = "MPBattleMD.ini";
    std::string playerCountry = "Americans";
    std::string playerColor = "Gold";
    // Resolved from the original RA2MD CSF and LOADMD.MIX resources.  Keeping
    // these with the accepted setup lets the synchronous match bootstrap show
    // the exact selected-country loading screen while it warms the runtime.
    std::wstring playerCountryDisplayName;
    std::wstring playerCountryBrief;
    std::string loadingScreenAssetId;
    int startingCredits = 200000;
    int unitCount = 10;
    int gameSpeed = 4;
    bool shortGame = true;
    bool mcvRepacks = true;
    bool crates = true;
    bool superweapons = false;
    bool buildOffAlly = false;
    bool computerAiEnabled = false;
    bool noCooldown = false;
    std::vector<RuntimeSkirmishPlayerSlot> slots;
};

