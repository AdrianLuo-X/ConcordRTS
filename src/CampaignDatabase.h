#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct CampaignMapSelectionPoint {
    int x = 0;
    int y = 0;
};

struct CampaignMapSelectionChoice {
    int colorIndex = -1;
    // The authored numeric edge points at an INI section.  targetStageId is
    // filled only when that section belongs to this campaign's retained
    // map-selection graph; targetMissionId is filled when it is a mission
    // stage.
    std::string targetSection;
    std::string targetStageId;
    std::string targetMissionId;
};

struct CampaignMapSelectionStage {
    std::string id;
    std::string sourceSection;
    std::string scenario;
    std::string description;
    std::string voiceOver;
    std::string mapVq;
    std::vector<std::string> overlays;
    std::string clickMap;
    // -1 means that Targets was not authored.  Otherwise the first Targets
    // value is the declared count and targets contains every complete,
    // parseable coordinate pair even when the declaration is inaccurate.
    int declaredTargetCount = -1;
    std::vector<CampaignMapSelectionPoint> targets;
    std::vector<CampaignMapSelectionChoice> choices;
};

struct CampaignMapSelectionRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

struct CampaignMapSelectionAnimation {
    int index = 0;
    std::string shpFile;
    int x = 0;
    int y = 0;
    int rate = 0;
};

struct CampaignMapSelectionAnimationConfig {
    std::string sourceSection;
    std::string palette;
    CampaignMapSelectionRect textRect;
    std::vector<CampaignMapSelectionAnimation> animations;
};

struct CampaignMapSelectionSound {
    std::string sample;
    int volume = 0;
    bool volumeSpecified = false;
};

struct CampaignMapSelectionSoundConfig {
    std::string sourceSection;
    CampaignMapSelectionSound overlay;
    CampaignMapSelectionSound targetFlyIn;
    CampaignMapSelectionSound enterRegion;
    CampaignMapSelectionSound exitRegion;
    CampaignMapSelectionSound clickRegion;
};

// A mission is deliberately described by the files discovered from the
// mounted RA2/YR data set.  The runtime must not keep a second hard-coded
// ALL01/SOV01 campaign table, because mods and language packs are allowed to
// replace the campaign selector and mission metadata.
struct CampaignMissionDefinition {
    std::string id;
    std::string campaignId;
    // Stable mounted-data identity.  sourceId is suitable for persistence
    // ("yr" or "ra2"); edition is the user-facing product name.
    std::string sourceId;
    std::string edition;
    std::string campaignSection;
    std::string mapSelectionSection;
    int ordinal = 0;

    std::string mapFileName;
    std::string mapPath;
    std::string playerHouse;
    std::string playerCountry;
    std::string nextMissionId;
    int startingCredits = 0;

    std::string displayNameKey;
    std::string descriptionKey;
    std::string briefingKey;
    std::string loadMessageKey;
    std::string loadBriefingKey;
    std::string loadingBackground640;
    std::string loadingBackground800;
    std::string loadingPalette800;
    // Raw compatibility mirrors of the mission's map-selection stage.  New
    // runtime code should consume CampaignDefinition::mapSelectionStages so
    // an empty-Scenario stage such as ALL00 is not mistaken for a mission.
    std::string voiceOver;
    std::string mapVq;
    std::string clickMap;
    std::vector<int> targetCells;
    std::vector<std::string> incomingMapSelectionStages;

    std::string introVideoId;
    std::string introVideoPath;
    std::string postScoreVideoId;
    std::string postScoreVideoPath;
    std::vector<std::string> videoIds;
    std::vector<std::string> videoPaths;

    bool available = false;
    bool official = false;
    bool debugOnly = false;
    bool oneTimeOnly = false;
    bool endOfGame = false;
    int triggerCount = 0;
    int eventCount = 0;
    int actionCount = 0;
    int tagCount = 0;
    int teamTypeCount = 0;
    int taskForceCount = 0;
    int scriptTypeCount = 0;
    std::vector<int> eventTypes;
    std::vector<int> actionTypes;
    std::vector<int> teamScriptActionTypes;
    std::vector<int> triggerMovieIndexes;
    std::vector<int> unsupportedEventTypes;
    std::vector<int> unsupportedActionTypes;
    std::vector<int> unsupportedTeamScriptActionTypes;
    std::vector<std::string> coverageErrors;
    int victoryActionCount = 0;
    int defeatActionCount = 0;
    int reachableVictoryActionCount = 0;
    int reachableDefeatActionCount = 0;
    std::string sourceMapSelection;
    std::string sourceMissionMetadata;
    std::string sourceMap;
    std::string status;
};

struct CampaignDefinition {
    std::string id;
    std::string sourceId;
    std::string edition;
    // The [Battles] route section (ALL1/SOV1/TUT1) is distinct from the
    // mapsel side section (GDI/Nod), especially in base RA2 where Boot Camp
    // and the Allied campaign share [GDI].
    std::string battleSection;
    std::string sourceSection;
    std::string textCatalogPath;
    std::string displayNameKey;
    std::string displayName;
    std::string side;
    std::vector<CampaignMissionDefinition> missions;
    std::string mapSelectionAnimsSection;
    std::string mapSelectionSoundsSection;
    CampaignMapSelectionAnimationConfig mapSelectionAnims;
    CampaignMapSelectionSoundConfig mapSelectionSounds;
    std::vector<CampaignMapSelectionStage> mapSelectionStages;

    const CampaignMapSelectionStage* findMapSelectionStage(
        const std::string& stageId
    ) const;
};

struct CampaignDatabaseOptions {
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
    bool includeDebugOnlyMissions = false;
};

struct CampaignSourceDefinition {
    std::string sourceId;
    std::string edition;
    // A MIX-backed INI is recorded as "container.mix!entry.ini".
    std::string battleIniPath;
    std::string mapSelectionIniPath;
    std::string missionIniPath;
    std::string textCatalogPath;
};

struct CampaignDatabase {
    bool loaded = false;
    std::string status;
    std::string battleIniPath;
    std::string mapSelectionIniPath;
    std::string missionIniPath;
    std::string textCatalogPath;
    std::vector<CampaignSourceDefinition> sources;
    std::vector<std::string> textCatalogPaths;
    std::vector<CampaignDefinition> campaigns;
    std::vector<std::string> warnings;

    const CampaignDefinition* findCampaign(const std::string& id) const;
    const CampaignMissionDefinition* findMission(
        const std::string& campaignId,
        const std::string& missionId
    ) const;
    std::size_t missionCount() const;
    std::size_t availableMissionCount() const;
};

CampaignDatabase loadCampaignDatabase(const CampaignDatabaseOptions& options);

// The command-line audit is intentionally independent of the Win32 UI.  It
// is used by CI and by the runtime smoke tests to prove that the complete
// multi-mission graph was discovered and that each mission points at real
// local assets.
int runCampaignDatabaseSmokeTest(
    const CampaignDatabaseOptions& options
);

// Returns non-zero when the map contains malformed or runtime-unsupported
// Trigger Event, Trigger Action, or Team Script Action records.  CI runs this
// against an intentionally invalid fixture with WILL_FAIL to prove the gate
// itself cannot silently accept a new unsupported type.
int runCampaignCoverageMapGateTest(const std::string& mapPath);
