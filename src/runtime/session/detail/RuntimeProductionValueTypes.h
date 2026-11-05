#pragma once
#include "RuntimeSessionGeometry.h"
#include "audio/backend/RuntimeVoiceAudio.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <limits>
#include "audio/backend/RuntimeSfxChannelPool.h"
#include "audio/events/RuntimeAircraftAuxAudio.h"
#include "audio/events/RuntimeCreditAudio.h"
#include "audio/events/RuntimeObjectAmbientAudio.h"
#include "audio/events/RuntimeSuperWeaponBuildingAudio.h"
#include "audio/events/RuntimeTurretSoundAudio.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "audio/world/RuntimeWorldAudioSystem.h"
#include "core/results/RuntimeMatchScore.h"
#include "data/rules/TechTreeAnalyzer.h"
#include "network/cooperative/RuntimeCooperativeSessionSystem.h"
#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include "render/core/RuntimeMapColorValues.h"
#include "render/core/RuntimeMapRenderer.h"
#include "render/geometry/RuntimeCameraGeometryMemo.h"
#include "render/geometry/RuntimeSpriteHitSpan.h"
#include "render/lighting/RuntimeLightingSystem.h"
#include "render/voxel/RuntimeVoxelDebris.h"
#include "runtime/contracts/RuntimeReplayEventSnapshot.h"
#include "runtime/host/RuntimeShell.h"
#include "runtime/performance/diagnostics/RuntimeFrameDiagnostics.h"
#include "simulation/abilities/psionic/RuntimeMagBeamSystem.h"
#include "simulation/abilities/psionic/RuntimePsychicStatusSystem.h"
#include "simulation/abilities/psionic/RuntimeSpySystem.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/ai/RuntimeAircraftMotion.h"
#include "simulation/ai/RuntimeAircraftServiceSystem.h"
#include "simulation/ai/RuntimeAirstrikeSystem.h"
#include "simulation/ai/RuntimeComputerMining.h"
#include "simulation/buildings/RuntimeBuildingCollapse.h"
#include "simulation/buildings/RuntimeGarrisonSystem.h"
#include "simulation/buildings/RuntimeGateSystem.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/buildings/RuntimeSpecialBuildingSystem.h"
#include "simulation/combat/damage/RuntimeDeathWeaponSystem.h"
#include "simulation/combat/damage/RuntimeWreck.h"
#include "simulation/combat/targeting/RuntimeCombatDetails.h"
#include "simulation/effects/RuntimeParticleSystem.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/movement/aircraft/RuntimeJumpjetKinematics.h"
#include "simulation/navigation/occupancy/RuntimeMovementEmptyCellProof.h"
#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"
#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"
#include "simulation/navigation/pathfinding/PathGoalTypes.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/terrain/RuntimeTerrainMetadataMap.h"
#include "simulation/navigation/terrain/RuntimeTerrainTypes.h"
#include "simulation/navigation/terrain/RuntimeTrackedAuthoredLand.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include "simulation/production/RuntimeProductionLanes.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include "simulation/vision/RuntimeVision.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/input/RuntimeMapLeftPointerController.h"
#include "ui/input/RuntimeMapRightPointerController.h"
#include "ui/input/RuntimeMenuValues.h"
#include "ui/input/RuntimePlayerCommands.h"
#include "ui/input/RuntimePlayerCommunication.h"
#include "ui/input/RuntimePlayerDefeatNotice.h"
#include "ui/input/RuntimeReferencePresentationController.h"
#include "ui/time/RuntimeUiClock.h"
#include "world/model/GameWorld.h"
#include "world/queries/RuntimeObjectIndex.h"
#include "world/values/RuntimeCellPoint.h"

namespace concord::runtime::session_detail {

enum class RuntimeEngineerAction {
    None,
    Capture,
    Repair,
    BridgeRepair
};

enum class RuntimeTimedBombIntentKind : std::uint8_t {
    C4 = 0
};

struct RuntimeTimedBombIntent {
    RuntimeTimedBombIntentKind kind = RuntimeTimedBombIntentKind::C4;
    int targetObjectId = -1;
};

struct RuntimeAttackApproachCacheEntry {
    int tick = -1;
    int targetObjectId = -1;
    int rangeBucket = 0;
    bool mapQueryApproachAvailable = false;
    std::vector<RuntimeCellPoint> candidates;
    std::vector<RuntimeCellPoint> mapQueryCandidates;
    std::uint64_t contextHash = 0;
};

struct RuntimeAttackApproachCacheStats {
    int tick = -1;
    int builds = 0;
    int hits = 0;
    int candidates = 0;
    int submitted = 0;
};

struct RuntimePathFailureCacheEntry {
    int firstTick = -1;
    int lastTick = -1;
    int nextAllowedTick = 0;
    int failureCount = 0;
};

struct RuntimePathFailureCacheStats {
    int recordedFailures = 0;
    int repeatedFailures = 0;
    int cooldownHits = 0;
    int activeEntries = 0;
};

struct RuntimeActionSummary {
    int activeCommands = 0;
    int movingObjects = 0;
    int attackingObjects = 0;
    int blockedObjects = 0;
    int movedThisTick = 0;
    int localAvoidanceAttemptsThisTick = 0;
    int localAvoidanceMovesThisTick = 0;
    int attacksThisTick = 0;
    int projectilesInFlight = 0;
    int impactsThisTick = 0;
    int destroyedObjects = 0;
    int completedCommands = 0;
    int repathsThisTick = 0;
    int formationTargets = 0;
    int turretTurnsThisTick = 0;
};

struct RuntimeCampaignScoreState {
    int missionStartTick = 0;
    int parTicks = 0;
    std::string parTimeField;
    std::string parTimeText;
    std::string underParTitleKey;
    std::string underParMessageKey;
    std::string overParTitleKey;
    std::string overParMessageKey;
    int enemyUnitsDestroyed = 0;
    int enemyStructuresDestroyed = 0;
    int friendlyUnitsLost = 0;
    int friendlyStructuresLost = 0;
    int finishedTick = -1;
    int finalScore = 0;
    // RA2/YR's verified campaign result authority is the authored
    // under/over-Par title and message. No retail formula has been verified
    // for this compatibility-only number, so it must never derive the rank.
    std::string numericFormulaId = "concord-approximate-v1";
    bool finishedUnderPar = false;
    bool captured = false;
    // Local animation clock. It is intentionally excluded from save and
    // deterministic authority; the frozen score values above are persistent.
    RuntimeUiTimeMs presentationStartedAtMs = 0;
};

enum class RuntimeProjectileLifecycleState {
    Spawned,
    Traveling,
    Impacting,
    Expired
};

struct RuntimeProjectileEffect {
    RuntimeVoxelDebrisState voxelDebris;
    bool homingInitialized = false;
    double homingYaw = 0.0;
    double homingPitch = 0.0;
    int actorId = -1;
    int parentActorId = -1;
    int generation = 0;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    int sourceAnchorObjectId = -1;
    std::string owner;
    RuntimeCellPoint fromCell;
    RuntimeCellPoint toCell;
    RuntimeCellPoint currentCell;
    bool hasSourcePosition = false;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double sourceZ = 0.0;
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
    bool hasTargetPosition = false;
    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;
    int fromLevel = 0;
    int toLevel = 0;
    int currentLevel = 0;
    int attackerFacing = -1;
    int targetFacing = -1;
    int spawnTick = 0;
    int impactTick = 0;
    int elapsedTicks = 0;
    int damage = 0;
    // Snapshot at fire time. The shooter may promote or die while a
    // projectile is in flight; neither may retroactively change its damage.
    double veteranDamageMultiplier = 1.0;
    bool spawnedMissileDamage = false;
    int spawnedMissileBaseDamage = 0;
    int totalTicks = 1;
    int remainingTicks = 1;
    RuntimeProjectileLifecycleState lifecycle =
        RuntimeProjectileLifecycleState::Spawned;
    std::string lifecycleState = "Spawned";
    std::string renderIntent = "projectile";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string weaponAnimId;
    std::string impactAnimId;
    std::string trailerAnimId;
    std::string trailerIntentId;
    std::string warheadImpactIntentId;
    std::string reportSoundId;
    bool arcing = false;
    bool vertical = false;
    int acceleration = 0;
    bool high = false;
    bool veryHigh = false;
    bool shadow = false;
    bool animates = false;
    bool aa = false;
    bool ag = true;
    int cluster = 0;
    bool bright = false;
    bool laser = false;
    bool houseColorLaser = false;
    bool electricBolt = false;
    bool alternateElectricColor = false;
    std::string electricColor;
    bool shrapnel = false;
    bool shrapnelReleased = false;
    int laserDuration = 0;
    int laserThickness = 1;
    double cellSpread = 0.0;
    double percentAtMax = 0.0;
    bool affectsAllies = false;
    bool affectsEnemies = true;
    std::string particleId;
    double progress = 0.0;
    double currentX = 0.0;
    double currentY = 0.0;
    double currentZ = 0.0;
    double speedCellsPerTick = 0.0;
    int rot = 0;
    // Value-only provenance for projectiles emitted by DeathWeapon. Ordinary
    // projectiles keep all three fields at zero. Derived shrapnel/debris keeps
    // the originating request so a later death continues the same cascade.
    std::uint64_t deathWeaponCascadeId = 0;
    std::uint64_t deathWeaponRequestId = 0;
    std::uint32_t deathWeaponCascadeDepth = 0;
    // Schema 3 appends immutable launch attribution and reserved causal
    // markers. Keep this tail value-only: impact consumers must never infer
    // a vanished source from a current world pointer.
    int launchSourceKind = -1;
    std::string launchSourceType;
    bool launchSourceAuthoredAircraft = false;
    bool launchSourceValidated = false;
    int airstrikeStrikeId = 0;
    int airstrikePlaneToken = 0;
    bool particleDamagePulse = false;
    bool suppressWarheadParticleSpawn = false;
};

enum class RuntimeObjectExitKind : std::uint8_t {
    Destroyed = 0,
    TemporalDestroyed,
    Sold,
    Consumed,
    TransformSource,
    ScriptRemoved
};

struct RuntimeObjectExitRequest {
    int objectId = -1;
    RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed;
    int tick = -1;
    std::string mission;
    std::string reason;
    int sourceObjectId = -1;
    std::string sourceOwner;
    RuntimeDeathWeaponCause deathWeaponCause;
    bool suppressDeathWeapon = false;
    bool destructionEffectsSpawned = false;
    // Local presentation only; pending exits are drained before save/hash.
    bool unitDeathSoundRequested = false;
    bool unitDeathSoundHandled = false;
    std::optional<RuntimeSpatialAudioPosition> unitDeathSoundPosition;
    bool suppressCrew = false;
    bool c4Explosion = false;
    std::string warheadId;
    // Module damage callbacks can mark the victim inactive before the host
    // drains its exit. Preserve that it was alive for this lethal hit.
    bool wasAliveBeforeDamage = false;
};

struct RuntimeDeferredBridgeCollapse {
    std::unordered_set<int> cells;
    int sourceObjectId = -1;
    std::string sourceOwner;
    RuntimeDeathWeaponCause deathWeaponCause;
};

struct RuntimeImpactEffect {
    int projectileActorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    bool targetIsStructure = false;
    std::string owner;
    RuntimeCellPoint cell;
    int level = 0;
    bool hasWorldPosition = false;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation{};
    int bodyCenterSpriteOffsetY = 0;
    int totalTicks = 8;
    int remainingTicks = 8;
    int radius = 10;
    int frameIndex = -1;
    int facing = -1;
    // Last resolved owner position for a moving passenger's muzzle. Together
    // with worldX/Y/Level it preserves the authored offset across snapshots.
    double muzzleAnchorX = 0.0;
    double muzzleAnchorY = 0.0;
    double muzzleAnchorLevel = 0.0;
    int muzzleFacing = -1;
    int animationFrameCount = 0;
    int animationFrameDelayTicks = 1;
    int animationStartFrame = 0;
    int animationLoopStartFrame = 0;
    int animationLoopEndFrame = 0;
    int animationLoopCount = 1;
    int animationEndFrame = 0;
    int animationSequenceSteps = 0;
    int animationMidpointWidth = 0;
    int animationMidpointHeight = 0;
    int lastSmudgeMidpointStep = -1;
    // Source ownership is independent of the victim's corpse remap color.
    std::string deferredParticleAnimId;
    std::string deferredParticleOwner;
    int deferredParticleTicks = -1;
    bool death = false;
    std::string lifecycleState = "Impact";
    std::string renderIntent = "impact";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string animId;
    std::string warheadId;
    std::string warheadImpactIntentId;
    std::string trailerIntentId;
    std::string particleId;
    int damage = 0;
    double cellSpread = 0.0;
    bool bright = false;
};

enum class RuntimeWeaponSlot {
    None,
    Primary,
    Secondary,
    ElitePrimary,
    EliteSecondary,
    Deploy,
    EliteDeploy,
    Occupy,
    EliteOccupy,
    OpenTransport,
    Indexed,
    EliteIndexed
};

using RuntimeWeaponCandidate =
    std::pair<RuntimeWeaponSlot, std::string>;

struct RuntimeDeployProfile {
    bool hasProfile = false;
    bool transformsIntoStructure = false;
    bool togglesDeployedState = false;
    bool requiresDeploymentToFire = false;
    bool usesDeployWeapon = false;
    bool immobileWhenDeployed = false;
    bool deployedCrushable = false;
    bool deployToLand = false;
    int undeployDelay = -1;
    int deployFacing = -1;
    std::string deploysInto;
    std::string undeploysInto;
    std::string unloadingClass;
    std::string deployWeaponId;
    RuntimeWeaponSlot deployWeaponSlot = RuntimeWeaponSlot::None;
    double deployTime = 0.0;
};

struct RuntimeEngineerProfile {
    bool canCapture = false;
    bool canRepair = false;
    bool canBridgeRepair = false;
};

struct RuntimeOccupyProfile {
    bool canOccupy = false;
    bool targetCanBeOccupied = false;
    bool targetCanFire = false;
    int maxOccupants = 0;
    std::string occupyWeaponId;
};

struct RuntimeTransportProfile {
    bool canTransport = false;
    bool canUnload = false;
    int passengerCapacity = 0;
    std::string unloadingClass;
};

struct RuntimeUiProfile {
    int maxStrength = 256;
    std::string pip;
    std::string pipScale;
    std::string cameoId;
    bool pipsDrawForAll = false;
};

struct RuntimeCachedCapabilityProfile {
    RuntimeDeployProfile deploy;
    RuntimeEngineerProfile engineer;
    RuntimeOccupyProfile occupy;
    RuntimeTransportProfile transport;
    RuntimeUiProfile ui;
    bool hasTurret = false;
    bool rulesResolved = false;
    bool canPassiveAcquire = true;
    bool crusher = false;
    bool omniCrusher = false;
    bool crushable = false;
    bool omniCrushResistant = false;
    bool deployedCrushable = false;
    bool wall = false;
    bool naval = false;
    bool organic = false;
    bool underwater = false;
    bool spawned = false;
    int navalTargeting = 0;
    int landTargeting = 0;
    std::string speedType;
    std::string crushSound;
    bool gattling = false;
    int weaponStages = 0;
    std::vector<int> weaponStageThresholds;
    std::vector<int> eliteWeaponStageThresholds;
    int weaponRateUp = 0;
    int weaponRateDown = 0;
    double guardScanRange = 0.0;
    double airRangeBonus = 0.0;
    std::vector<RuntimeWeaponCandidate> baseWeaponCandidates;
    std::vector<RuntimeWeaponCandidate> indexedWeaponCandidates;
    std::vector<RuntimeWeaponCandidate> eliteIndexedWeaponCandidates;
    std::vector<RuntimeWeaponCandidate> deployWeaponCandidates;
    std::vector<RuntimeWeaponCandidate> occupyWeaponCandidates;
    std::optional<RuntimeWeaponCandidate> openTransportWeapon;
};

struct RuntimeCapabilityCacheStats {
    std::uint64_t profileLookups = 0;
    std::uint64_t profileHits = 0;
    std::uint64_t profileMisses = 0;
    std::uint64_t weaponLookups = 0;
    std::uint64_t weaponCandidateHits = 0;
    std::uint64_t weaponCandidateMisses = 0;
    std::uint64_t missionTransitions = 0;
    std::uint64_t lastTickProfileLookups = 0;
    std::uint64_t lastTickProfileHits = 0;
    std::uint64_t lastTickProfileMisses = 0;
    std::uint64_t lastTickWeaponLookups = 0;
    std::uint64_t lastTickWeaponCandidateHits = 0;
    std::uint64_t lastTickWeaponCandidateMisses = 0;
};

struct RuntimeCapabilityProfileCache {
    std::unordered_map<std::string, RuntimeCachedCapabilityProfile> profilesByType;
    RuntimeCapabilityCacheStats stats;
    int lastMissionDiagnosticTick = -1;
    std::vector<std::string> recentMissionDiagnostics;
};

struct RuntimeBuildCatalogItem {
    RuntimeBuildKind kind = RuntimeBuildKind::Building;
    std::string typeId;
    std::string label;
    std::string owner;
    std::string requiredHouses;
    std::string forbiddenHouses;
    std::string prerequisite;
    std::string prerequisiteOverride;
    std::string requiredFactory;
    int cost = 0;
    int buildTicks = 1;
    int powerDelta = 0;
    int techLevel = 0;
    std::optional<int> buildLimit;
    bool waterBound = false;
    bool naval = false;
    bool wall = false;
    bool techTreeAllowed = true;
    bool secretLabInjected = false;
    std::string techTreeKind;
    std::string techTreeBlockedReason;
};

enum class RuntimeHarvesterPhase {
    Idle,
    SeekingOre,
    MovingToOre,
    Loading,
    ReturningToRefinery,
    WaitingForRefineryDock,
    Docking,
    PreparingToUnload,
    Unloading
};

struct RuntimeHarvesterTask {
    RuntimeHarvesterPhase phase = RuntimeHarvesterPhase::Idle;
    RuntimeCellPoint targetOre;
    RuntimeCellPoint dockCell;
    RuntimeCellPoint queueCell;
    int targetRefineryId = -1;
    int cargoValue = 0;
    int cargoUnits = 0;
    int cargoGemUnits = 0;
    int cargoCapacity = 0;
    int workTicksRemaining = 0;
    int nextThinkTick = 0;
    bool cargoHasGems = false;
    std::string status = "idle";
};

enum class RuntimeSlaveMinerPhase {
    SeekingOre,
    MovingToOre,
    Harvesting,
    Returning,
    Stored
};

struct RuntimeSlaveMinerTask {
    RuntimeSlaveMinerPhase phase = RuntimeSlaveMinerPhase::SeekingOre;
    int refineryId = -1;
    RuntimeCellPoint targetOre;
    int cargoValue = 0;
    int cargoUnits = 0;
    int cargoGemUnits = 0;
    int workTicksRemaining = 0;
    std::string status = "idle";
};

struct RuntimeEconomyState {
    std::string owner = "Americans";
    int techLevel = 10;
    int credits = 200000;
    int powerProduced = 0;
    int powerUsed = 0;
    int harvesters = 0;
    int refineries = 0;
    int incomeCooldownTicks = 0;
    int storedResourceUnits = 0;
    int storedResourceValue = 0;
    int storedResourceGemUnits = 0;
    int resourceStorageCapacity = 0;
    int lastResourceStoredUnits = 0;
    int lastResourceStoredValue = 0;
    int lastResourceStoredGemUnits = 0;
    int lastResourceStorageOverflowUnits = 0;
    int lastResourceStorageCapacity = 0;
    std::vector<RuntimeBuildCatalogItem> catalog;
    RuntimeProductionLanesState localProductionLanes;
    bool localProductionLanesInitialized = false;
    std::optional<RuntimeProductionLane> selectedPlacementLane;
    std::uint64_t selectedPlacementSerial = 0;
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, int>
    > limitedBuildHistoryByOwner;
    RuntimeCellPoint placementCell;
    bool placementValid = false;
    std::string status = "build ready";
    int completedBuildings = 0;
    int completedUnits = 0;
    int completedInfantry = 0;
    int resourceTrips = 0;
    int harvesterTeleports = 0;
    int slaveMinerLoads = 0;
    int lastResourceBaseValue = 0;
    int lastResourcePurifierBonus = 0;
    int lastResourcePurifierCount = 0;
    RuntimeCellPoint lastHarvesterGatherCell;
    int lastHarvesterGatherBeforeBails = 0;
    int lastHarvesterGatherAfterBails = 0;
    std::string lastHarvesterGatherOverlayBefore;
    std::string lastHarvesterGatherOverlayAfter;
    RuntimeCellPoint lastRefinerySmokeCell;
    int lastRefinerySmokeEffects = 0;
    std::string lastRefinerySmokeParticleSystem;
    std::string lastRefinerySmokeParticle;
    std::string lastRefinerySmokeImage;
    RuntimeCellPoint lastHarvesterTooFarApproachTarget;
    double lastHarvesterTooFarDistance = 0.0;
    double lastHarvesterTooFarThreshold = 0.0;
    bool lastHarvesterTooFarChrono = false;
    bool simplifiedHarvesterIncomeEnabled = false;
    bool skirmishStartApplied = false;
    int skirmishStartObjectId = -1;
    RuntimeCellPoint skirmishStartCell;
    std::string skirmishStatus = "skirmish start not applied";
};

struct RuntimeHumanHouseAuthority {
    std::size_t slotIndex = 0;
    std::string owner;
    std::string canonicalOwner;
    std::string country;
    bool localFacade = false;
    std::optional<RuntimeEconomyState> remoteEconomy;
    std::optional<TechTreeReport> remoteTechTree;
    bool remoteTechTreeLoaded = false;
};

struct RuntimeComputerAiState {
    bool initialized = false;
    std::string owner;
    std::string country;
    std::string difficulty = "Medium";
    RuntimeCellPoint baseCenter;
    int credits = 0;
    int nextThinkTick = 0;
    int nextIncomeTick = 0; // Legacy save field; real mining has no income deadline.
    int nextBuildTick = 0;
    int nextAttackTick = 0;
    int nextRepairTick = 0;
    int productionSerial = 0;
    int incomeEarned = 0;
    int creditsSpent = 0;
    int structuresBuilt = 0;
    int infantryBuilt = 0;
    int unitsBuilt = 0;
    int freeUnitsReceived = 0;
    int attackWaves = 0;
    int attacksOrdered = 0;
    int defenseOrders = 0;
    int tacticalOrders = 0;
    int repairsPerformed = 0;
    int stalledBuilds = 0;
    int lastTargetObjectId = -1;
    std::string lastBuildType;
    std::string status = "waiting for skirmish start";
};

struct RuntimeResultPresentationState {
    bool resigned = false;
    bool started = false;
    bool bannerVisible = false;
    RuntimeUiTimeMs startedAtMs = 0;
    RuntimeUiTimeMs bannerDurationMs = 0;
    int scorePage = 0;
    bool continueHovered = false;
    bool continuePressed = false;
    bool scoreMusicSelected = false;
};

struct RuntimeStructureFootprint {
    int width = 1;
    int height = 1;
    bool valid = false;
};

struct RuntimeTerrainInfo {
    // Authored surface used by movement timing; reachability retains its
    // existing shoreline/bridge checks in authoredLandType below.
    RuntimeTerrainLandType movementLandType = RuntimeTerrainLandType::Unknown;
    unsigned char movementRampType = 0;
    RuntimeTerrainKind kind = RuntimeTerrainKind::Land;
    RuntimeTrackedAuthoredLand<RuntimeTerrainLandType> authoredLandType =
        RuntimeTerrainLandType::Unknown;
    bool passable = true;
    bool water = false;
    bool shore = false;
    bool cliff = false;
    bool ramp = false;
    bool bridge = false;
    int cost = 10;
};

enum class RuntimeBridgeMaterial {
    Concrete,
    Wood
};

struct RuntimeBridgeRepairCell {
    RuntimeCellPoint point;
    bool requiresOverlay = true;
    int expectedFrame = 0;
};

struct RuntimeBridgeRepairSpan {
    int hutObjectId = -1;
    bool high = true;
    bool alongX = false;
    RuntimeBridgeMaterial material = RuntimeBridgeMaterial::Concrete;
    RuntimeCellPoint startHead;
    RuntimeCellPoint endHead;
    int level = 0;
    int overlayIndex = -1;
    int intactFrame = 0;
    std::string overlayType;
    std::string overlayImage;
    std::vector<RuntimeBridgeRepairCell> cells;
};

struct RuntimeNavigationStats {
    int terrainCells = 0;
    int landCells = 0;
    int waterCells = 0;
    int shoreCells = 0;
    int cliffCells = 0;
    int rampCells = 0;
    int bridgeCells = 0;
    int blockedCells = 0;
    int occupiedCells = 0;
    int structureFootprints = 0;
    int structureFootprintCells = 0;
};

struct RuntimePlacementCellCheck {
    RuntimeCellPoint cell;
    int level = 0;
    bool valid = false;
    std::string reason;
};

struct RuntimePerfCounters {
    double lastPaintMs = 0.0;
    double lastMapDrawMs = 0.0;
    double lastOverlayDrawMs = 0.0;
    double lastSidebarDrawMs = 0.0;
    double lastSimulationMs = 0.0;
    double lastSimEffectsMs = 0.0;
    double lastSimEconomyMs = 0.0;
    double lastSimTriggersMs = 0.0;
    double lastSimBuildMs = 0.0;
    double lastSimGuardMs = 0.0;
    double lastSimMoveMs = 0.0;
    double lastSimAttackMs = 0.0;
    double lastSimPathMs = 0.0;
    double lastSimVisionMs = 0.0;
    int lastSimRepathCount = 0;
    int lastSimPathRequests = 0;
    double peakSimulationMs = 0.0;
    double peakSimMoveMs = 0.0;
    double peakSimPathMs = 0.0;
    int peakSimRepathCount = 0;
    int peakSimPathRequests = 0;
    double peakWorldObjectsMs = 0.0;
    double peakWorldObjectSortMs = 0.0;
};

struct RuntimePathTerrainSnapshotCache {
    PathRequestQueueTerrainLayoutCache queueLayout;
    bool valid = false;
    std::size_t terrainCellCount = 0;
    std::vector<PathTerrainCell> staticCells;
    // The legacy key may alias distinct coordinates. Retain every offset,
    // including duplicate terrain cells, rather than assuming a unique key.
    std::unordered_map<int, std::size_t> staticFirstIndexByCell;
    std::vector<std::size_t> staticNextIndexByCell;
    mutable bool dynamicValid = false;
    mutable int dynamicTick = -1;
    mutable std::size_t dynamicTerrainCellCount = 0;
    mutable int dynamicMoverObjectId = -1;
    mutable int dynamicAllowedOccupiedObjectId = -2;
    mutable std::uint64_t dynamicIgnoredObjectHash = 0;
    mutable std::uint64_t dynamicGateTopologyFingerprint = 0;
    mutable std::uint64_t dynamicRadiationFingerprint = 0;
    mutable std::uint64_t dynamicThreatFingerprint = 0;
    mutable PathMovementMode dynamicMovementMode = PathMovementMode::Ground;
    mutable std::vector<PathTerrainCell> dynamicCells;

    // MSVC's unordered_map move constructor can allocate a new sentinel.
    // Swap the existing storage directly at the allocation-free load commit.
    friend void swap(
        RuntimePathTerrainSnapshotCache& left,
        RuntimePathTerrainSnapshotCache& right
    ) noexcept {
        static_assert(noexcept(left.staticCells.swap(right.staticCells)));
        static_assert(noexcept(left.staticFirstIndexByCell.swap(right.staticFirstIndexByCell)));
        static_assert(noexcept(left.staticNextIndexByCell.swap(right.staticNextIndexByCell)));
        static_assert(noexcept(left.dynamicCells.swap(right.dynamicCells)));
        static_assert(noexcept(left.queueLayout.swap(right.queueLayout)));
        using std::swap;
        left.queueLayout.swap(right.queueLayout);
        swap(left.valid, right.valid);
        swap(left.terrainCellCount, right.terrainCellCount);
        left.staticCells.swap(right.staticCells);
        left.staticFirstIndexByCell.swap(right.staticFirstIndexByCell);
        left.staticNextIndexByCell.swap(right.staticNextIndexByCell);
        swap(left.dynamicValid, right.dynamicValid);
        swap(left.dynamicTick, right.dynamicTick);
        swap(left.dynamicTerrainCellCount, right.dynamicTerrainCellCount);
        swap(left.dynamicMoverObjectId, right.dynamicMoverObjectId);
        swap(left.dynamicAllowedOccupiedObjectId, right.dynamicAllowedOccupiedObjectId);
        swap(left.dynamicIgnoredObjectHash, right.dynamicIgnoredObjectHash);
        swap(left.dynamicGateTopologyFingerprint, right.dynamicGateTopologyFingerprint);
        swap(left.dynamicRadiationFingerprint, right.dynamicRadiationFingerprint);
        swap(left.dynamicThreatFingerprint, right.dynamicThreatFingerprint);
        swap(left.dynamicMovementMode, right.dynamicMovementMode);
        left.dynamicCells.swap(right.dynamicCells);
    }
};

struct RuntimeSnapshotMaterializeStats {
    int projectileSnapshotMaterializations = 0;
    int replayEventSnapshotMaterializations = 0;
    int uiSnapshotMaterializations = 0;
    int mapFactsSnapshotMaterializations = 0;
    int pathStatsSnapshotMaterializations = 0;
    int projectileRenderActorMaterializations = 0;
    int impactRenderActorMaterializations = 0;
    int lastProjectileSnapshotActors = 0;
    int lastReplayEvents = 0;
    int lastProjectileRenderActors = 0;
    int lastImpactRenderActors = 0;
    bool mapFactsContextReused = false;
};

struct RuntimeSnapshotCache {
    mutable std::vector<RuntimeProjectileActorSnapshot> projectileSnapshots;
    mutable std::vector<RuntimeReplayEventSnapshot> replayEventSnapshots;
    mutable std::vector<RuntimeProjectileRenderActor> projectileRenderActors;
    mutable std::vector<RuntimeImpactRenderActor> impactRenderActors;
    mutable RuntimeUiStateSnapshot uiSnapshot;
    mutable RuntimeMapFactsContextSnapshot mapFactsSnapshot;
    mutable RuntimePathQueueStatsSnapshot pathStatsSnapshot;
    mutable bool replayEventSnapshotsDirty = true;
    mutable RuntimeSnapshotMaterializeStats stats;
};

struct RuntimeProjectedObject {
    const WorldObjectInstance* object = nullptr;
    std::string observerType;
    std::string observerOwner;
    WorldObjectKind observerKind = WorldObjectKind::Infantry;
    bool spyDisguiseProjected = false;
    RuntimeInputPixelPoint center{0, 0};
    RuntimeInputPixelPoint renderCenter{0, 0};
    RuntimeInputPixelRect bounds{0, 0, 0, 0};
    RuntimeInputPixelRect spriteBounds{0, 0, 0, 0};
    bool hasMobileSpriteBounds = false;
    RuntimeInputPixelRect selectionBounds{0, 0, 0, 0};
    RuntimeInputPixelRect selectionBaseBounds{0, 0, 0, 0};
    int healthPixelOffsetY = 0;
    double drawLevel = 0.0;
    int projectedX = 0;
    int projectedY = 0;
    bool usesFootprintHit = false;
    bool hasStructureSpriteGeometry = false;
    bool airborneForeground = false;
    bool hasBodyHitClip = false;
    RuntimeInputPixelRect bodyHitClip{};
    int factoryForegroundHostId = -1;
    RuntimeInputPixelRect footprintBounds{};
    std::vector<RuntimeSpriteHitSpan> structureSpriteHitSpans;
    std::vector<RuntimeSpriteHitSpan> structureForegroundHitSpans;
    bool hasBuildingDepthVisibility = false;
    std::vector<RuntimeSpriteHitSpan> visibleBuildingHitSpans;
    bool hasHostedSortOverride = false;
    double hostedSortCellSum = 0.0;
    int footprintHalfWidth = 0;
    int footprintHalfHeight = 0;
    std::vector<RuntimeInputPixelPoint> footprintHitCenters;
};

struct RuntimeSoundPlaybackRecord {
    int tick = 0;
    std::uint64_t playbackOrder = 0;
    std::string intent;
    std::string section;
    std::string sample;
    std::string subtitleText;
    std::string path;
    std::string status;
    bool semantic = false;
    bool eva = false;
    bool playable = false;
    bool played = false;
    bool suppressed = false;
    bool queued = false;
    bool queueRequested = false;
    int priority = 0;
    int volumePercent = 100;
    int authoredGainPermille = 1000;
    int gain16384 = 16384;
    int spatialGain16384 = 16384;
    int pan16384 = 8192;
    bool spatial = false;
    bool trackCamera = false;
    RuntimeSpatialAudioPosition sourcePosition;
};

constexpr std::size_t runtimeSfxChannelCount = 8;

struct RuntimeCrateState {
    RuntimeCellPoint cell;
    bool active = true;
    int respawnTick = 0;
    // Non-zero only for authored/legacy money crates that override the
    // [Powerups] Money value.
    int value = 0;
    bool water = false;
    std::string powerupType;
    std::string powerupAnimation;
    double powerupData = 0.0;
    // Authored and destruction drops remain usable with random crates disabled.
    bool authored = false;
    // Exact overlay/art identifier chosen when the crate was created.  This
    // preserves the authored Wood/Silver/Water distinction across save/load.
    std::string imageId;
};

struct RuntimeSidebarPowerAnimation {
    bool initialized = false;
    int startProduced = 0;
    int startUsed = 0;
    int targetProduced = 0;
    int targetUsed = 0;
    int startTick = 0;
};

struct RuntimeTerrainObjectInitialState {
    int health = 1;
    bool alive = true;
};

struct RuntimeAirstrikePlaneBinding {
    int strikeId = 0;
    int planeToken = 0;
    int objectId = -1;
    int callerObjectId = -1;
    int targetObjectId = -1;
    int spawnTick = -1;
    int bombTick = -1;
    int egressStartTick = -1;
    int despawnTick = -1;
    bool elite = false;
    std::string owner;
    std::string aircraftTypeId;
    std::string weaponId;
    std::string warheadId;
    RuntimeAirstrikeRoute route;
    // Optional exact origin only for a target-loss egress before bomb release.
    // Legacy bindings omit this authority and retain their integer route.
    bool hasPreciseEgressOrigin = false;
    double preciseEgressOriginX = 0.0;
    double preciseEgressOriginY = 0.0;
};

struct RuntimeAirstrikeSalvoRound {
    int actorId = -1;
    int launchTick = -1;
    int terminalTick = -1;
    RuntimeAirstrikeCell sourceCell;
    int damage = 0;
};

struct RuntimeAirstrikeRootAuthority {
    int rootProjectileActorId = -1;
    int strikeId = 0;
    int planeToken = 0;
    int planeObjectId = -1;
    int callerObjectId = -1;
    int targetObjectId = -1;
    int launchTick = -1;
    int terminalTick = -1;
    bool elite = false;
    RuntimeAirstrikeCell bombCell;
    std::string owner;
    std::string aircraftTypeId;
    std::string weaponId;
    std::string warheadId;
    std::string projectileId;
    int damage = 0;
    double veteranDamageMultiplier = 1.0;
    int roundsRemaining = 0;
    int nextFireTick = -1;
    std::vector<RuntimeAirstrikeSalvoRound> rounds;
};

struct RuntimeActiveLightingToken {
    std::uint64_t token = 0;
    RuntimeLightingMode mode = RuntimeLightingMode::Base;
    int startedTick = -1;

    bool operator==(const RuntimeActiveLightingToken&) const = default;
};

struct RuntimeWorldAudioBackendBinding {
    std::uint64_t handleId = 0;
    std::uint64_t playbackOrder = 0;
    int gainPermille = 0;
    bool active = false;
    RuntimeWorldAudioAction action;
    RuntimeSpatialAudioPosition position;
    int gain16384 = 0;
    int pan16384 = 8192;
};

struct RuntimeLocalChronosphereMarker {
    RuntimeImpactEffect animation;
    int startedTick = 0;
    int lastAnimationStep = -1;
};

struct RuntimeChronosphereSelectionAudioState {
    RuntimeSuperWeaponBuildingSound sound;
    std::string owner;
    std::string weaponId;
    RuntimeWorldAudioCell cell;
    bool selected = false;
};

struct RuntimeSuperWeaponVisualEvent {
    SuperWeaponEvent event;
    // Frozen before TransformSource drains the old infantry's physical pose.
    // This is presentation only; the independent event and replay stay intact.
    std::optional<std::array<double, 3>> worldPosition;
    std::array<int, 4> canvasOffsets{};
};


}
