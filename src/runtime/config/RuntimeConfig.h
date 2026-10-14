#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Runtime launch configuration and player preferences have no dependency on
// the game window, simulation state, renderer, or startup-report types.

struct RuntimeConfig {
    std::string configPath = "local_data/runtime_config.ini";
    std::string resourceRootPath = "RedAlert2";
    std::string localDataRootPath = "local_data";
    std::string modRootPath;
    // Derived from the selected map, never from the enabled player count.
    // Zero keeps legacy behavior when no map authority is available.
    int mapStartingPointLimit = 0;
    std::string defaultMapPath =
        "local_data/resources/mix_exports/multimd/2peaks.map";
    int windowWidth = 1024;
    int windowHeight = 640;
    int fixedTickHz = 30;
    // Multiplayer simulation authority. setupHash fingerprints immutable
    // room/rules/map configuration; timelineEpoch separates restarts or host
    // migrations that may reuse a fixed tick; simulationRandomSeed is shared
    // deterministic state and is never a process-local RNG seed.
    bool multiplayerActive = false;
    std::uint64_t multiplayerSetupHash = 0;
    // Stable room metadata.  Never derive either value from a process-local
    // map path: the network setup/manifest owns both identities.
    std::string multiplayerCanonicalMapId;
    std::uint64_t multiplayerMapContentFingerprint = 0;
    std::uint64_t multiplayerCooperativeScenarioFingerprint = 0;
    std::uint64_t multiplayerTimelineEpoch = 0;
    std::uint64_t simulationRandomSeed = 0;
    int targetFps = 60;
    bool showMainMenu = true;
    bool showSkirmishMenu = true;
    std::string gameMode = "Battle";
    // The rules layer declared by MPModesMD.ini for the selected game mode.
    // The front-end supplies this value so mod-provided modes can affect the
    // runtime without hard-coding their display names.
    std::string gameModeRulesOverride;
    // CampaignMission is a distinct runtime mode. These fields are filled by
    // the data-driven campaign selector and are intentionally separate from
    // the skirmish lobby fields below.
    std::string campaignId;
    std::string campaignMissionId;
    // Stable mounted-data identity propagated from CampaignMissionDefinition.
    // "ra2" and "yr" select strict, non-overlapping runtime INI stacks.
    std::string campaignSourceId;
    std::string campaignDifficulty = "Normal";
    std::string campaignPlayerHouse;
    std::string campaignNextMissionId;
    std::string campaignIntroVideoPath;
    std::string campaignPostScoreVideoPath;
    std::string campaignBriefingKey;
    std::string campaignLoadSavePath;
    std::string campaignSavePath;
    // Filled only by the front-end Load Game flow.  The saved skirmish
    // session metadata is applied before map/rules startup, then this file is
    // materialized atomically over that matching baseline.
    std::string skirmishLoadSavePath;
    // Empty outside the front-end preserves map-authored ownership for
    // command-line smoke tests and campaign scenarios. The skirmish menu
    // always supplies an explicit country and color before a match starts.
    std::string playerCountry;
    std::string playerColor;
    std::wstring loadingCountryDisplayName;
    std::wstring loadingCountryBrief;
    std::string loadingScreenAssetId;
    int startingCredits = 200000;
    int startingUnits = 0;
    int techLevel = 10;
    int gameSpeed = 4;
    // Player-local presentation and input preferences. These values are
    // intentionally outside deterministic match authority and may be changed
    // while a campaign, skirmish, or multiplayer session is running.
    int scrollRate = 3;
    int visualDetail = 2;
    bool tooltipsEnabled = true;
    bool targetLinesEnabled = true;
    bool scrollCoastingEnabled = true;
    bool seeHiddenObjects = true;
    bool musicEnabled = true;
    bool soundEffectsEnabled = true;
    int musicVolume = 100;
    int soundVolume = 100;
    int voiceVolume = 100;
    // Empty means to use keyboardmd.ini (and finally the built-in retail
    // defaults). Once edited, all supported bindings are stored here.
    std::vector<std::pair<std::string, int>> keyBindings;
    bool keyboardReferenceLayout = false;
    bool shortGame = true;
    bool mcvRepacks = false;
    bool crates = false;
    bool superweapons = true;
    // When enabled, the default structure-placement search may use an allied
    // structure as its build-radius anchor.  False preserves the legacy
    // single-owner behavior for command-line and older INI configurations.
    bool buildOffAlly = false;
    // Offline skirmish option. Disabled computer slots still spawn normally,
    // but their strategic and passive combat behaviors remain paused.
    bool computerAiEnabled = false;
    // Offline skirmish: only the local player skips production/charge time.
    bool noCooldown = false;
    struct PlayerSlot {
        bool enabled = false;
        bool human = false;
        // Network sessions bind one stable peer identity to each human slot.
        // Offline/front-end configurations may leave peerId empty.  Exactly
        // one process-local human slot should set local=true; legacy configs
        // with no explicit local slot continue to use the first human slot.
        std::string peerId;
        bool local = false;
        std::string name;
        std::string country = "Random";
        std::string color = "Random";
        std::string team = "-";
        std::string difficulty = "Medium";
        // 0 means deterministic random; 1..N selects a numbered map start.
        int startPosition = 0;
    };
    std::vector<PlayerSlot> playerSlots;
    std::string runtimeReplayEventOutputPath =
        "build/runtime_replay_events_command.txt";
};

struct RuntimeConfigOverrides {
    std::string configPath;
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
    std::string defaultMapPath;
    int windowWidth = 0;
    int windowHeight = 0;
    int fixedTickHz = 0;
    int targetFps = 0;
    std::string runtimeReplayEventOutputPath;
};

RuntimeConfig loadRuntimeConfig(const RuntimeConfigOverrides& overrides);
// Atomically updates only player preference keys in config.configPath. Other
// sections, keys, and comments are retained so mod- or user-authored runtime
// configuration is not discarded by the Options pages.
bool saveRuntimeConfigPreferences(
    const RuntimeConfig& config,
    std::string& errorMessage
);
// Saves only the offline lobby's crate choice, preserving all other settings.
bool saveRuntimeSkirmishCratePreference(
    const RuntimeConfig& config,
    bool enabled,
    std::string& errorMessage
);
bool saveRuntimeSkirmishNoCooldownPreference(
    const RuntimeConfig& config, bool enabled, std::string& errorMessage);