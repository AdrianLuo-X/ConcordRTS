#pragma once

#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeReplayEventSnapshot.h"

#include "data/art/ArtDatabase.h"
#include "assets/inspection/GameResourceMount.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "data/art/TheaterDatabase.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct MapFacts;
class RuntimeMultiplayerRuntimeBridge;



struct RuntimeProjectileActorSnapshot {
    int actorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    std::string owner;
    std::string lifecycleState;
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    RuntimeSnapshotCell sourceCell;
    RuntimeSnapshotCell targetCell;
    RuntimeSnapshotCell currentCell;
    int sourceLevel = 0;
    int targetLevel = 0;
    int currentLevel = 0;
    double currentX = 0.0;
    double currentY = 0.0;
    double currentZ = 0.0;
    int facing = -1;
    double angleDegrees = 0.0;
    std::string trailerIntentId;
    std::string impactIntentId;
    std::string warheadImpactIntentId;
    int spawnTick = 0;
    int impactTick = 0;
};



struct RuntimeProductionLaneUiSnapshot {
    std::string lane;
    int queuedCount = 0;
    int occupancy = 0;
    bool onHold = false;
    bool hasActive = false;
    std::uint64_t activeSerial = 0;
    std::string activeTypeId;
    double activeProgress = 0.0;
    int activeRemainingTicks = 0;
    bool hasReady = false;
    std::uint64_t readySerial = 0;
    std::string readyTypeId;
};

struct RuntimeUiStateSnapshot {
    // The authoritative fixed-tick special-mechanism state hash is exposed
    // with the public snapshot so diagnostics/replay consumers cannot observe
    // a stale visual-only effect state.
    std::uint64_t specialMechanismStateHash = 0;
    std::uint64_t superWeaponStateHash = 0;
    int superWeaponCount = 0;
    int superWeaponReadyCount = 0;
    std::string selectedSuperWeaponId;
    std::string selectedSuperWeaponState;
    double selectedSuperWeaponProgress = 0.0;
    int selectedSuperWeaponRemainingTicks = 0;
    int cash = 0;
    int powerProduced = 0;
    int powerUsed = 0;
    int powerNet = 0;
    int powerDrain = 0;
    int powerSurplus = 0;
    int powerDeficit = 0;
    bool lowPower = false;
    bool radarAvailable = false;
    bool radarLowPower = false;
    std::string radarStatus;
    int resourceStorageUnits = 0;
    int resourceStorageCapacity = 0;
    int resourceStorageValue = 0;
    int resourceStorageGemUnits = 0;
    int selectedObjectId = -1;
    std::string selectedTypeId;
    int selectedStrength = 0;
    int selectedMaxStrength = 0;
    std::string selectedPip;
    std::string selectedPipScale;
    std::string selectedCameoId;
    std::string selectedPipResourceId;
    std::string selectedPipScaleResourceId;
    std::string selectedCameoResourceId;
    int selectedPipFilled = 0;
    int selectedPipTotal = 0;
    int selectedPipValue = 0;
    int selectedPipGemUnits = 0;
    int selectedAmmoCurrent = -1;
    int selectedAmmoCapacity = -1;
    // Generic parent/spawned-actor status for the selected parent unit.
    // These values are derived from RuntimeSpecialMechanismState so sidebar,
    // HUD, replay diagnostics, and other UI consumers share one authority.
    std::string selectedSpawnTypeId;
    int selectedSpawnCapacity = 0;
    int selectedSpawnReady = 0;
    int selectedSpawnDeployed = 0;
    int selectedSpawnReloadRemainingTicks = 0;
    bool hasActiveProduction = false;
    std::string activeProductionKind;
    std::string activeProductionTypeId;
    double activeProductionProgress = 0.0;
    int activeProductionRemainingTicks = 0;
    int buildQueueSize = 0;
    int unitQueueSize = 0;
    int structureQueueSize = 0;
    bool placementReady = false;
    std::string placementTypeId;
    bool readyState = false;
    double readyProgress = 0.0;
    std::vector<RuntimeProductionLaneUiSnapshot> productionLanes;
    std::string selectedPlacementLane;
    std::uint64_t selectedPlacementSerial = 0;
};

struct RuntimeMapFactsContextSnapshot {
    bool available = false;
    std::string status;
    int width = -1;
    int height = -1;
    std::size_t cellCount = 0;
    std::size_t structureAnchorCount = 0;
    std::size_t resourceCellCount = 0;
    std::size_t currentResourceCellCount = 0;
    std::size_t currentOreCellCount = 0;
    std::size_t currentGemCellCount = 0;
    int initialOreCellsSeeded = 0;
    int initialOreCellsDensified = 0;
};

struct RuntimePathQueueStatsSnapshot {
    int pendingCount = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int budgetExhaustedResults = 0;
    bool lastPumpBudgetExhausted = false;
    int backpressureRejectedRequests = 0;
    int requestOverflowRejectedRequests = 0;
    int collapsedPendingRequests = 0;
    int massAttackCollapsedRequests = 0;
    int sharedApproachCacheHits = 0;
    int sharedApproachCacheMisses = 0;
    int repeatedFailureSuppressed = 0;
    int noPathCooldownHits = 0;
    int cooldownNoPathResults = 0;
    int realNoPathResults = 0;
    int equivalentStartBucketCollapses = 0;
    int goalSlotAssignments = 0;
    int fallbackCandidatesConsidered = 0;
    int fallbackRingRequests = 0;
    int fallbackTargetCacheHits = 0;
    int fallbackTargetCacheMisses = 0;
    int mapQueryApproachRequests = 0;
    int mapQueryApproachUsedResults = 0;
    int lastPumpExpandedNodes = 0;
    int cachedPathCount = 0;
    int fallbackTargetCacheEntries = 0;
    bool usesShortHashKeys = false;
    bool mapQueryApproachAvailable = false;
    bool usesBlockedComponentApproachFallback = false;
    std::string mapQueryApproachPurpose;
    int mapQueryApproachCandidates = 0;
    int mapQueryApproachSubmitted = 0;
    std::uint64_t mapQueryApproachContextHash = 0;
};

struct RuntimeMapSummary {
    bool loaded = false;
    std::string path;
    std::string name;
    std::string theater;
    std::string size;
    std::string localSize;
    int width = -1;
    int height = -1;
    int isoTiles = 0;
    int overlayCells = 0;
    int structures = 0;
    int units = 0;
    int infantry = 0;
    int triggers = 0;
};

struct RuntimeStartupReport {
    bool success = false;
    bool configFileLoaded = false;
    std::string configPath;
    std::string resourceRootPath;
    bool resourceRootExists = false;
    std::string localDataRootPath;
    bool localDataRootExists = false;
    std::string modRootPath;
    bool modRootExists = false;
    ResourceMountReport resources;
    RuntimeMapSummary map;
    std::string rulesPath;
    bool rulesLoaded = false;
    std::string artPath;
    bool artLoaded = false;
    std::string theaterPath;
    bool theaterLoaded = false;
    int theaterTileSets = 0;
    GameWorldSummary world;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

RuntimeStartupReport buildRuntimeStartupReport(
    const RuntimeConfig& config,
    GameWorld* worldOutput = nullptr,
    TheaterDatabase* theaterOutput = nullptr,
    ArtDatabase* artOutput = nullptr,
    RulesDatabase* rulesOutput = nullptr
);
RuntimeStartupReport buildRuntimeStartupReport(
    const RuntimeConfig& config,
    GameWorld* worldOutput,
    TheaterDatabase* theaterOutput,
    ArtDatabase* artOutput,
    RulesDatabase* rulesOutput,
    MapFacts* mapFactsOutput
);
void printRuntimeStartupReport(const RuntimeStartupReport& report);
int runRuntimeRenderSmokeTest(const RuntimeConfig& config);
int runRuntimeVoxelAtlasDump(
    const RuntimeConfig& config,
    const std::string& objectList
);
int runRuntimeInteractionSmokeTest(const RuntimeConfig& config);
int runRuntimeResultPresentationSmokeTest(const RuntimeConfig& config);
int runRuntimeVisionSmokeTest();
int runRuntimeVeterancySmokeTest();
int runRuntimeWeaponProjectileFieldsSmokeTest();
int runRuntimeNavalCloakSmokeTest();
int runRuntimeCrushScatterSmokeTest();
int runRuntimeHospitalArmoryModCompatibilitySmokeTest(
    const std::string& rulesPath
);
int runRuntimeAircraftServiceIntegrationSmokeTest();
int runRuntimeRepairDepotIntegrationSmokeTest();
int runRuntimeGateIntegrationSmokeTest();
int runRuntimeSubcellIntegrationSmokeTest();
int runRuntimeSpyIntegrationSmokeTest();
int runRuntimeSpecialMechanismsSmokeTest();
int runRuntimeMechanismGoldenSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputRoot,
    const std::string& referenceRoot
);
int runRuntimeCommandSmokeTest(const RuntimeConfig& config);
int runRuntimeBuildSmokeTest(const RuntimeConfig& config);
int runRuntimeTriggerSmokeTest(const RuntimeConfig& config);
int runRuntimeCampaignRuntimeSmokeTest(const RuntimeConfig& config);
int runRuntimeSkirmishBuildSmokeTest(const RuntimeConfig& config);
int runRuntimeComputerAiSmokeTest(const RuntimeConfig& config);
int runRuntimeMultiplayerHouseAuthoritySmokeTest();
int runRuntimeMultiplayerGameplayAuthoritySmokeTest();
int runRuntimeMultiplayerAuthoritativeHashSmokeTest();
int runRuntimeMultiplayerSnapshotV15SmokeTest();
int runRuntimeMultiplayerRuntimeBindingSmokeTest();
int runRuntimeWindow(
    const RuntimeConfig& config,
    bool exitAfterLoadingSmoke = false
);
int runRuntimeWindow(
    const RuntimeConfig& config,
    bool exitAfterLoadingSmoke,
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> multiplayerBridge
);

std::vector<RuntimeProjectileActorSnapshot> runtimeProjectileActorSnapshots();
std::vector<RuntimeReplayEventSnapshot> runtimeReplayEventSnapshots();
RuntimeUiStateSnapshot runtimeUiStateSnapshot();
RuntimeMapFactsContextSnapshot runtimeMapFactsContextSnapshot();
RuntimePathQueueStatsSnapshot runtimePathQueueStatsSnapshot();

