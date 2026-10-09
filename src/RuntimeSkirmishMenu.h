#pragma once

#include "RuntimeShell.h"

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

// Original LoadProgressMgr aliases recovered from gamemd.exe and validated
// against LOADMD.MIX.  These helpers are public so runtime bootstrap and UI
// smoke tests use one authoritative country mapping.
// Startup-only map authority lookup; zero means no known advertised limit.
int resolveRuntimeMapStartingPointLimit(const RuntimeConfig& config);

std::string runtimeLoadingScreenAssetForCountry(const std::string& countryId);
std::string runtimeLoadingPaletteAssetForCountry(const std::string& countryId);
std::string runtimeLoadingBriefKeyForCountry(const std::string& countryId);

// Shows the modal 800x600 Yuri's Revenge shell with separate lobby and map
// selection pages. Original shell art is resolved from the installed MIX set.
RuntimeSkirmishSelection runRuntimeSkirmishMenu(
    const RuntimeConfig& config,
    void* hostWindow = nullptr
);

// Starts resource preparation on a worker. Neither this call nor the ready
// poll blocks the shell's message loop. Call on the UI thread; all HWNDs and
// painting remain on that thread. Unchanged resources survive menu visits.
void prewarmRuntimeSkirmishMenu(const RuntimeConfig& config);
bool runtimeSkirmishMenuReady(const RuntimeConfig& config);

// Renders the lobby to bmpPath plus sibling "*_country_dropdown.bmp" and
// "*_map_selection.bmp" views, validating the original shell and interaction
// data.
// Returns zero on success and one on validation/rendering failure.
int runRuntimeSkirmishMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& bmpPath
);
