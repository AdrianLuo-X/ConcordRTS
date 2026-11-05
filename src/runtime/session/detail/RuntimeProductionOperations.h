#pragma once

#include <queue>
#include <numeric>
#include <atomic>
#include <bit>
#include <iostream>
#include <iterator>
#include <thread>
#include <tuple>
#include <cstdlib>
#include <cwchar>
#include "RuntimeProductionHostState.h"
#include "assets/audio/AudioSampleResolver.h"
#include "assets/inspection/GameResourceMount.h"
#include "assets/terrain/RuntimeTerrainTmpQuery.h"
#include "audio/backend/RuntimeAsyncSoundDevice.h"
#include "audio/backend/RuntimeLegacyWaveDevice.h"
#include "audio/backend/RuntimeSfxChannelPool.h"
#include "audio/backend/RuntimeVoiceAudio.h"
#include "audio/backend/RuntimeVoicePath.h"
#include "audio/events/RuntimeAircraftAuxAudio.h"
#include "audio/events/RuntimeCreditAudio.h"
#include "audio/events/RuntimeObjectAmbientAudio.h"
#include "audio/events/RuntimePowerAudioFeedback.h"
#include "audio/events/RuntimeSuperWeaponBuildingAudio.h"
#include "audio/events/RuntimeTurretSoundAudio.h"
#include "audio/spatial/RuntimeAudioVariation.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "audio/world/RuntimeWorldAudioSystem.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeAtomicFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/players/PlayerSlotRole.h"
#include "core/results/RuntimeMatchScore.h"
#include "data/art/ArtDatabase.h"
#include "data/art/TheaterDatabase.h"
#include "data/campaign/CampaignProgress.h"
#include "data/campaign/CooperativeScenarioDatabase.h"
#include "data/maps/MapDatabase.h"
#include "data/maps/RuntimeRandomMap.h"
#include "data/rules/ModExtensionTypes.h"
#include "data/rules/ProductionRules.h"
#include "data/rules/RulesDatabase.h"
#include "data/rules/TechTreeAnalyzer.h"
#include "network/cooperative/RuntimeCooperativeSessionSystem.h"
#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include "network/snapshots/RuntimeMultiplayerResumeSave.h"
#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"
#include "render/buildings/RuntimeBioReactorAnimation.h"
#include "render/core/RuntimeMapColorValues.h"
#include "render/core/RuntimeMapRenderer.h"
#include "render/core/RuntimeMapTerrainFrame.h"
#include "render/core/RuntimeSpriteFrameImage.h"
#include "render/geometry/RuntimeCameraGeometryMemo.h"
#include "render/geometry/RuntimeInfantryMuzzleQuery.h"
#include "render/geometry/RuntimeMapSurfacePick.h"
#include "render/geometry/RuntimeMarineMuzzleQuery.h"
#include "render/geometry/RuntimeObjectBodyAnchor.h"
#include "render/geometry/RuntimeObjectShpBoundsQuery.h"
#include "render/geometry/RuntimeObjectWorldRenderPlacement.h"
#include "render/geometry/RuntimeProjectileScreenPoint.h"
#include "render/geometry/RuntimeSpriteHitSpan.h"
#include "render/geometry/RuntimeStructureMuzzleQuery.h"
#include "render/geometry/RuntimeStructureTurretScreenOffset.h"
#include "render/geometry/RuntimeVoxelMuzzleQuery.h"
#include "render/lighting/RuntimeLightingSystem.h"
#include "render/lighting/RuntimeMapLight.h"
#include "render/units/RuntimeArcticArt.h"
#include "render/units/RuntimeInfantryDeathAnimation.h"
#include "render/voxel/RuntimeFutureVoxelBootstrap.h"
#include "render/voxel/RuntimeVoxelDebris.h"
#include "render/voxel/RuntimeVoxelLoadingPrewarm.h"
#include "runtime/campaign/RuntimeCampaignDataSource.h"
#include "runtime/campaign/RuntimeCampaignMovieResolver.h"
#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeReplayEventSnapshot.h"
#include "runtime/host/../../../tests/rendering/RuntimeBuildingDepthOracle.h"
#include "runtime/host/RuntimeShell.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include "runtime/performance/RuntimeSimulationQueryReuse.h"
#include "runtime/performance/RuntimeSimulationStageAdmission.h"
#include "runtime/performance/diagnostics/RuntimeFrameDiagnostics.h"
#include "runtime/performance/diagnostics/RuntimeMassMovementRejectionDiagnostics.h"
#include "runtime/persistence/RuntimeLoadTransaction.h"
#include "runtime/persistence/RuntimeSaveDocument.h"
#include "runtime/persistence/RuntimeSaveIdentity.h"
#include "runtime/replay/RuntimeMatchReplay.h"
#include "runtime/timing/RuntimeRofTiming.h"
#include "runtime/timing/RuntimeRuleTime.h"
#include "simulation/abilities/parasites/RuntimeSquidGrappleMotion.h"
#include "simulation/abilities/psionic/RuntimeDisguiseBlink.h"
#include "simulation/abilities/psionic/RuntimeMagBeamSystem.h"
#include "simulation/abilities/psionic/RuntimePsychicDetectionNoSensor.h"
#include "simulation/abilities/psionic/RuntimePsychicStatusSystem.h"
#include "simulation/abilities/psionic/RuntimeSpyDetectionNoop.h"
#include "simulation/abilities/psionic/RuntimeSpySystem.h"
#include "simulation/abilities/radiation/RuntimeRadiation.h"
#include "simulation/abilities/spawning/RuntimeDiskDrain.h"
#include "simulation/abilities/spawning/RuntimeDiskLaser.h"
#include "simulation/abilities/unit_special/RuntimeSpecialDetail.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/abilities/unit_special/RuntimeTimedBomb.h"
#include "simulation/ai/RuntimeAircraftMotion.h"
#include "simulation/ai/RuntimeAircraftProductionMatcher.h"
#include "simulation/ai/RuntimeAircraftServiceSystem.h"
#include "simulation/ai/RuntimeAirstrikeSystem.h"
#include "simulation/ai/RuntimeAutonomousOrderScope.h"
#include "simulation/ai/RuntimeComputerAiTriggerHash.h"
#include "simulation/ai/RuntimeComputerMining.h"
#include "simulation/ai/RuntimeComputerMiningCodec.h"
#include "simulation/ai/RuntimeComputerTactics.h"
#include "simulation/ai/RuntimeComputerTacticsCodec.h"
#include "simulation/ai/RuntimeSecretLabReadOnlyMemo.h"
#include "simulation/buildings/RuntimeBuildingCollapse.h"
#include "simulation/buildings/RuntimeBuildingSale.h"
#include "simulation/buildings/RuntimeGarrisonSystem.h"
#include "simulation/buildings/RuntimeGateSystem.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "simulation/buildings/RuntimeInfantryServiceSystem.h"
#include "simulation/buildings/RuntimeRepairDepotAnimation.h"
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/buildings/RuntimeSpecialBuildingSystem.h"
#include "simulation/buildings/RuntimeStructureRepack.h"
#include "simulation/buildings/RuntimeTankBunker.h"
#include "simulation/combat/damage/RuntimeCrewSurvivors.h"
#include "simulation/combat/damage/RuntimeDeathWeaponSystem.h"
#include "simulation/combat/damage/RuntimeWreck.h"
#include "simulation/combat/projectiles/RuntimeMissileAcceleration.h"
#include "simulation/combat/projectiles/RuntimeMissileExhaust.h"
#include "simulation/combat/projectiles/RuntimeMissilePresentation.h"
#include "simulation/combat/projectiles/RuntimeProjectileSpeed.h"
#include "simulation/combat/targeting/RuntimeCombatDetails.h"
#include "simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"
#include "simulation/combat/targeting/RuntimeWeaponFireFlh.h"
#include "simulation/economy/HouseEconomyContext.h"
#include "simulation/economy/RuntimeBuildingEconomy.h"
#include "simulation/economy/RuntimeBuildingPower.h"
#include "simulation/economy/RuntimeEconomyFixedStep.h"
#include "simulation/effects/RuntimeParticleSystem.h"
#include "simulation/mod_extensions/RuntimeModExtensions.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/movement/aircraft/RuntimeJumpjetKinematics.h"
#include "simulation/movement/ground/RuntimeCommittedGroundMotion.h"
#include "simulation/movement/ground/RuntimeMovementClassification.h"
#include "simulation/movement/ground/RuntimeVehicleArrivalMotion.h"
#include "simulation/movement/ground/RuntimeVehicleStartupMotion.h"
#include "simulation/movement/naval/RuntimeShipyardProductionAnimation.h"
#include "simulation/navigation/occupancy/RuntimeEmptySubcellAvailability.h"
#include "simulation/navigation/occupancy/RuntimeGroundPoseOccupancy.h"
#include "simulation/navigation/occupancy/RuntimeMovementEmptyCellProof.h"
#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"
#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"
#include "simulation/navigation/pathfinding/PathGoalTypes.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/RuntimePathDenseAuthoredLand.h"
#include "simulation/navigation/pathfinding/RuntimePathDynamicPreparation.h"
#include "simulation/navigation/pathfinding/RuntimePathDynamicResourceFusion.h"
#include "simulation/navigation/pathfinding/RuntimePathDynamicSparseQueries.h"
#include "simulation/navigation/pathfinding/RuntimePathDynamicWorkset.h"
#include "simulation/navigation/pathfinding/RuntimePathQueueLayoutStorage.h"
#include "simulation/navigation/pathfinding/RuntimePathQueueTerrainStorage.h"
#include "simulation/navigation/pathfinding/RuntimePathRepresentativeOccupants.h"
#include "simulation/navigation/pathfinding/diagnostics/RuntimePathDynamicSparseAdmissionDiagnostics.h"
#include "simulation/navigation/terrain/RuntimeNavigationTerrainQuery.h"
#include "simulation/navigation/terrain/RuntimeTerrainMetadataMap.h"
#include "simulation/navigation/terrain/RuntimeTerrainTypes.h"
#include "simulation/navigation/terrain/RuntimeTrackedAuthoredLand.h"
#include "simulation/production/RuntimeBuildupTiming.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include "simulation/production/RuntimeFactoryExitAnimation.h"
#include "simulation/production/RuntimeProductionFixedStep.h"
#include "simulation/production/RuntimeProductionLanes.h"
#include "simulation/production/RuntimeProductionTiming.h"
#include "simulation/superweapons/BuildingSuperWeapons.h"
#include "simulation/superweapons/RuntimeNuclearFlight.h"
#include "simulation/superweapons/SuperWeaponNuclearFlight.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/triggers/RuntimeCampaignAiTriggerDefinition.h"
#include "simulation/triggers/RuntimeTriggerPersistence.h"
#include "simulation/triggers/RuntimeTriggerReinforcements.h"
#include "simulation/triggers/RuntimeTriggerScheduler.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include "simulation/vision/RuntimeVision.h"
#include "simulation/vision/RuntimeVisionPhase.h"
#include "simulation/vision/RuntimeVisionTerrainCoordinateBatch.h"
#include "simulation/vision/RuntimeVisionTerrainCoordinateLease.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/input/RuntimeMapLeftPointerController.h"
#include "ui/input/RuntimeMapRightPointerController.h"
#include "ui/input/RuntimeMenuValues.h"
#include "ui/input/RuntimeObserver.h"
#include "ui/input/RuntimePlayerCommands.h"
#include "ui/input/RuntimePlayerCommunication.h"
#include "ui/input/RuntimePlayerDefeatNotice.h"
#include "ui/input/RuntimePlayerWaypoints.h"
#include "ui/input/RuntimeReferencePresentationController.h"
#include "ui/selection/diagnostics/RuntimePaintProjectionSignatureContractOracle.h"
#include "ui/sidebar/RuntimeSidebarPageController.h"
#include "ui/sidebar/diagnostics/RuntimeSidebarOwnedQueryDiagnostics.h"
#include "ui/skirmish/RuntimeSkirmishMenu.h"
#include "ui/time/RuntimeUiClock.h"
#include "ui/values/RuntimeColorValues.h"
#include "world/model/GameWorld.h"
#include "world/model/GameWorldSubCellOffset.h"
#include "world/queries/RuntimeHistoryQuery.h"
#include "world/queries/RuntimeHistoryStageRuleMemo.h"
#include "world/queries/RuntimeObjectIndex.h"
#include "world/values/RuntimeCellPoint.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <type_traits>

namespace concord::runtime::session_detail {
bool runtimeSessionGeometryAvailable(const RuntimeProductionHostState& state) noexcept ;
using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::splitRuntimeTokenList;
using runtime_save_text::splitRuntimeSaveFields;
using runtime_save_text::runtimeSaveField;
using runtime_save_text::parseRuntimeInt;
using runtime_save_text::parseRuntimeIntOrDefault;
using runtime_save_text::parseRuntimeBool;


struct RuntimePalettePathMemoScope;
class RuntimeHumanHouseScope;
class RuntimeMoveCommandDiagnosticScope;
class RuntimeNestedStageDiagnosticScope;
struct RuntimeSnapshotActiveStateScope;
struct RuntimeSfxVoiceBackend;
class RuntimeBuildingDepartureReadOnlyQueryScope;
class RuntimePaintSecretLabPhaseScope;
struct RuntimePaintTechTreeMemoScope;
struct RuntimeIdleVariantPass;
class RuntimePlannerTerrainQueries;
class RuntimePlannerColdCellLookups;
class RuntimePlannerIndexedTopology;
class RuntimeNavigationTerrainRendererAdapter;
class RuntimePlannerLandPermissionScope;
class RuntimePlannerInvisibleBuildingScope;
class RuntimePlannerEmptyCellCensusScope;
struct RuntimeInfantryAvoidanceSubcellBatch;
class RuntimeManualFormationMappedFilter;
struct RuntimeAirTraffic;
struct RuntimeAirMoveContinuation;
class RuntimeGroundDepartureQueryBatch;
class RuntimePlannerVisitedCellKeys;
struct RuntimePlannerPassabilityKey;
struct RuntimePlannerPassabilityKeyHash;
class RuntimePlannerCellPassabilityScope;
class RuntimePlannerCellPassabilityReadOnlyLease;
class RuntimePathSnapshotOccupancy;
class RuntimePathTerrainQueryScope;
class RuntimePathSnapshotMetadataQueries;
class RuntimePathSnapshotConditionalQueries;
class RuntimePathDynamicPreparation;
class RuntimePathDynamicPreparationWorksetExit;
class RuntimePlannerConnectivityKernel;
class RuntimeHostSaveIdentityNormalization;
struct RuntimeHarvesterResourceAccess;
struct RuntimeMultiplayerSnapshotStagedState;
class RuntimeComputerMiningFeedbackScope;
class RuntimeInfantryWaterPoseQuery;
class RuntimeInfantryWaterBehaviorContextScope;
struct RuntimeCombatOpportunityOwnerSummary;
struct RuntimeCombatOpportunityRangeBound;
struct RuntimeCombatOpportunityCandidates;
class RuntimeSkirmishStartingConnectivity;
class RuntimeChronoMoveTerrainView;
struct RuntimeStrictSlotVehicleCertificateStats;
class RuntimeMixedLocalComponentCertificate;
#define RUNTIME_COOPERATIVE_OBSERVATIONS(OP) \
    OP(campaignPreviousCellByObject) \
    OP(campaignPreviousHealthByObject) \
    OP(campaignInitialHealthByObject) \
    OP(campaignPreviousAliveByObject) \
    OP(campaignInitialOwnerByObject) \
    OP(campaignPreviousOwnerByObject) \
    OP(campaignAttackedTickByObject) \
    OP(campaignAttackerHouseByObject) \
    OP(campaignDestroyedTickByObject) \
    OP(campaignDestroyerHouseByObject) \
    OP(campaignEnteredTickByTargetObject) \
    OP(campaignEntrantHouseByTargetObject) \
    OP(campaignSpiedTickByTargetObject) \
    OP(campaignSpyHouseByTargetObject) \
    OP(campaignBridgeDestroyedTickByCell)

using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::splitRuntimeTokenList;
using runtime_save_text::splitRuntimeSaveFields;
using runtime_save_text::runtimeSaveField;
using runtime_save_text::parseRuntimeInt;
using runtime_save_text::parseRuntimeIntOrDefault;
using runtime_save_text::parseRuntimeBool;
using ::runtimeCampaignTriggerById;
using runtime_authority_hash::runtimeStableHashString;
using runtime_authority_hash::runtimeStableHashMix;
using runtime_authority_hash::runtimeStableHashMixString;
using runtime_save_text::parseRuntimeDoubleOrDefault;
using runtime_save_text::runtimeBoolText;
using runtime_authority_hash::runtimeMultiplayerAuthoritativeMixBool;
using runtime_authority_hash::runtimeMultiplayerAuthoritativeMixUnsigned;
using runtime_authority_hash::runtimeMultiplayerAuthoritativeMixDouble;
using runtime_authority_hash::runtimeMultiplayerCanonicalIdentifier;
std::string runtimeEncodePresentationUtf8(const std::wstring& localized,const std::string& key);
constexpr RuntimeMapColorValue sessionPackedColor_rgb(int red,int green,int blue) noexcept {
    return RuntimeRgbColorValue::fromChannels(static_cast<std::uint8_t>(red),static_cast<std::uint8_t>(green),static_cast<std::uint8_t>(blue)).packedRgb();
}
constexpr int sessionPackedColor_red(RuntimeMapColorValue value) noexcept {return RuntimeRgbColorValue::fromPackedRgb(value).red();}
constexpr int sessionPackedColor_green(RuntimeMapColorValue value) noexcept {return RuntimeRgbColorValue::fromPackedRgb(value).green();}
constexpr int sessionPackedColor_blue(RuntimeMapColorValue value) noexcept {return RuntimeRgbColorValue::fromPackedRgb(value).blue();}
inline constexpr double runtimeCratePowerupDataLimit = 1000000.0;

inline constexpr int runtimeMaximumCreditBalance =
    std::numeric_limits<int>::max();

inline constexpr int runtimeSidebarPowerAnimationStepMs = 50;

inline constexpr int runtimeSidebarPowerAnimationUnitsPerStep = 10;

inline constexpr int runtimeSidebarWidth = 168;

inline constexpr int runtimeMaxReadyPathRequestsPerTick = 1;

inline constexpr int runtimeMaxReadyPathCandidateScanPerTick = 48;

inline constexpr int runtimeRepathCooldownTicks = 12;

inline constexpr int runtimeMovingBlockerWaitTicks = 24;

inline constexpr int runtimeMaxLocalAvoidanceAttemptsPerTick = 96;

inline constexpr int runtimePathExpandedNodeBudget = 2048;

inline constexpr int runtimePathBatchExpandedNodeBudget = 2048;

inline constexpr int runtimeTerrainOccupantIdBase =
    std::numeric_limits<int>::min() / 2;

inline constexpr int runtimeSidebarProductionSpeedMultiplier = 8;

inline constexpr int runtimeSidebarBuildingProductionSpeedMultiplier = 16;

inline constexpr int runtimeProjectileSaveSchemaV1 = 1;

inline constexpr int runtimeProjectileSaveSchemaV2 = 2;

inline constexpr int runtimeProjectileSaveSchemaV3 = 3;

inline constexpr int runtimeProjectileSaveSchemaV4 = 4;

inline constexpr int runtimeProjectileSaveSchemaV5 = 5;

inline constexpr int runtimeProjectileSaveSchemaV6 = 6;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV1 = 82U;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV2 = 85U;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV3 = 93U;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV4 = 94U;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV5 = 97U;

inline constexpr std::size_t runtimeProjectileSaveFieldCountV6 = 99U;

inline constexpr int runtimeProductionLanesSectionSchemaV1 = 1;

inline constexpr int runtimeProductionLanesSectionSchemaV2 = 2;

inline constexpr int runtimeProductionLanesSectionSchemaV3Alias = 3;

inline constexpr int runtimeProductionLanesSectionCurrentSchema =
    runtimeProductionLanesSectionSchemaV2;

inline constexpr std::size_t runtimeSlaveMinerSavedMoveMaximumNodes = 100000U;

inline constexpr std::size_t runtimeJumpjetSavedMoveMaximumNodes = 100000U;

inline constexpr std::size_t runtimeClonePendingDeliveriesMaximumCount = 100000U;

inline constexpr std::uint32_t runtimeProductionProgressMicros = 1000000U;

inline constexpr int runtimeOreMaxBails = 12;

inline constexpr int runtimeOreLowestBails = 1;

inline constexpr int runtimeOreFieldSpreadSpawnDensityFrame = runtimeOreLowestBails - 1;

inline constexpr int runtimeGeneratedOreVariantCount = 12;

inline constexpr std::int64_t runtimeMagBeamQ16One = 1LL << 16U;

inline constexpr std::int64_t runtimeMagBeamGroundToleranceQ16 =
    runtimeMagBeamQ16One / 256;

enum class RuntimeObjectExitQueueResult : std::uint8_t {
    Queued = 0,
    DuplicatePending,
    AlreadyFinalized,
    ObjectMissing
};

struct RuntimeObjectExitResult {
    bool accepted = false;
    bool duplicate = false;
    RuntimeDeathWeaponResult deathWeapon;
};

struct RuntimeWeaponProfile {
    bool hasWeaponSet = false;
    bool requiresDeployment = false;
    bool deployed = false;
    bool occupied = false;
    bool openTransport = false;
    bool hasTurret = false;
    const WeaponType* weapon = nullptr;
    const ProjectileType* projectile = nullptr;
    const WarheadType* warhead = nullptr;
    RuntimeWeaponSlot slot = RuntimeWeaponSlot::None;
    std::string weaponId;
    std::string projectileId;
    std::string warheadId;
    std::string selectionReason;
    int indexedWeaponIndex = -1;
    int occupantMuzzleIndex = -1;
};

struct RuntimeBuildOrder {
    RuntimeBuildCatalogItem item;
    int remainingTicks = 0;
    bool paid = false;
    bool onHold = false;
};

struct RuntimeSpyObserverProjection {
    bool active = false;
    RuntimeSpyDisguiseIdentity identity =
        RuntimeSpyDisguiseIdentity::Infantry;
    std::string type;
    std::string owner;
};

enum class RuntimeHighBridgeHeadType {
    None,
    TopLeft,
    BottomRight,
    TopRight,
    BottomLeft,
    MiddleTopLeftBottomRight,
    MiddleTopRightBottomLeft
};

struct RuntimeSidebarPowerDisplay {
    int produced = 0;
    int used = 0;
    bool animating = false;
};

struct RuntimeParasiteActionApplyResult {
    bool accepted = true;
    bool changed = false;
};

struct RuntimeProjectedPoint {
    int x = 0;
    int y = 0;
};

struct RuntimeRadarProjection {
    bool valid = false;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    int fullMapWidth = 0;
};

struct RuntimeRadarDxyPoint {
    double x = 0.0;
    double y = 0.0;
};

struct RuntimeRadarMapSize {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct RuntimeProjectileTargetPosition {
    RuntimeCellPoint cell;
    double x = 0.0;
    double y = 0.0;
    int level = 0;
};

enum class RuntimeStructureOccupancyMode {
    FullFoundation,
    VehicleNavigation
};

struct RuntimeTechTreeDecision {
    bool staticAllowed = false;
    bool ownerAllowed = false;
    bool techAllowed = false;
    bool factoryAvailable = false;
    bool prerequisitesMet = false;
    bool enoughCredits = false;
    bool canQueue = false;
    bool canStart = false;
    bool lowPower = false;
    bool lowPowerAfterBuild = false;
    int powerNet = 0;
    int powerNetAfterBuild = 0;
    int techLevel = 0;
    int maxTechLevel = 10;
    std::string missingFactory;
    std::string missingPrerequisite;
    std::string reason;
};

enum class RuntimeMissionIntent {
    Idle,
    Moving,
    Attacking,
    Deploying,
    Deployed,
    Undeploying,
    Entering,
    Occupying,
    Repairing,
    Loading,
    Unloading
};

enum class RuntimeTargetDomain {
    Land,
    Water,
    Air
};

struct RuntimeIdleVariantPhase {
    int periodTicks = 90;
    int activeTicks = 18;
    int phase = 0;
};

struct RuntimePlannerMetadataBorrowSummary {
    const void* state = nullptr;
    const void* terrain = nullptr;
    bool enabled = false, borrowed = false;
    std::size_t ownedInfoBytes = 0, availableInfoBytes = 0;
    std::size_t sourceEntriesExamined = 0, retainedInfoEntries = 0;
};

using RuntimePlannerMetadataBorrowObserver = void(*)(void*, const RuntimePlannerMetadataBorrowSummary&);

using RuntimePlannerColdLookupObserver = void(*)(void*, std::size_t, std::size_t,
    std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t);

using RuntimePlannerIndexedTopologyObserver=void(*)(void*,bool,std::size_t,std::size_t,std::size_t);

enum class RuntimeGatePassageMode {
    Planning,
    Commit
};

enum class RuntimePlannerEmptyCellQuery { Probe, HardBlocker, Availability };

using RuntimePlannerEmptyCensusObserver = void(*)(void*,bool,std::size_t,std::size_t,std::size_t);

enum class RuntimeCrushStepResult {
    NotApplicable,
    NoVictims,
    Approaching,
    Blocked,
    Crushed
};

struct RuntimeCrushTargetScan {
    bool blocked = false;
    std::vector<int> targetIds;
};

using RuntimeCrushCandidateList = std::vector<const WorldObjectInstance*>;

using RuntimeCrushCandidateLookup =
    std::unordered_map<std::uint64_t, RuntimeCrushCandidateList>;

using RuntimePlannerEmptySubcellBooleanObserver = void(*)(void*, bool);

struct RuntimeCellGateScratchStats {
    std::size_t tailQueries = 0, exactMisses = 0, referenceOriginal = 0, indexedOriginal = 0;
};

using RuntimePlannerPassabilityObserver = void(*)(
    void*, const WorldObjectInstance&, const RuntimePlannerPassabilityKey&,
    const std::unordered_set<int>*, bool, bool);

struct RuntimePathThreatArea {
    int objectId = -1;
    int x = 0, y = 0, width = 1, height = 1;
    double range = 0.0;
    int cost = 0;
};

struct RuntimePathSnapshotConditionalQueryStats {
    bool reference=false;
    std::size_t metadataLookups=0,metadataSkipped=0;
    std::size_t occupantLookups=0,occupantSkipped=0;
};

using RuntimePathSnapshotConditionalQueryObserver=void(*)(void*,const RuntimeProductionHostState&,
    const RuntimePathSnapshotConditionalQueryStats&);

struct RuntimeQueuedPathResult {
    bool reachable = false;
    bool budgetExhausted = false;
    bool cooldownNoPath = false;
    bool realNoPath = false;
    bool cacheHit = false;
    bool sharedApproachHit = false;
    bool goalAdjusted = false;
    RuntimeCellPoint resolvedGoal;
    std::vector<RuntimeCellPoint> path;
};

using RuntimePlannerKernelLeaseObserver = void(*)(void*,std::size_t,std::size_t,std::size_t);

using RuntimePlannerCrushAliasObserver=void(*)(void*,std::size_t,std::size_t,std::size_t,std::size_t);

struct RuntimeMovePathBatchWork {
    int objectId = -1;
    RuntimeCellPoint start;
    RuntimeCellPoint goal;
    int allowedOccupiedObjectId = -1;
    std::unordered_set<int> ignoredObjectIds;
    PathApproachContext context;
};

struct RuntimeSkirmishStartingUnitPools {
    std::vector<const UnitType*> infantry;
    std::vector<const UnitType*> vehicles;
};

struct RuntimeProductionCatalogAdapter {
    const RuntimeBuildCatalogItem* item = nullptr;
    RuntimeProductionLane lane = RuntimeProductionLane::Structures;
};

struct RuntimeSelectedPlacement {
    RuntimeProductionLane lane = RuntimeProductionLane::Structures;
    RuntimeProductionLaneOrder order;
    const RuntimeBuildCatalogItem* item = nullptr;
};

struct RuntimeProductionSpawn {
    const WorldObjectInstance* anchor = nullptr;
    RuntimeCellPoint cell;
    int dockIndex = -1;
    bool aircraftDock = false;
    double renderX = 0.0;
    double renderY = 0.0;
    int level = 0;
};

struct RuntimeAircraftProductionMatchInputBuild {
    RuntimeAircraftProductionMatchInput input;
    std::optional<RuntimeAircraftProductionMatchResult> error;
};

struct RuntimeProjectileSaveCandidate {
    std::vector<RuntimeProjectileEffect> projectiles;
    int nextActorId = 1;
    int schema = 0;
};

using RuntimeOriginalOverlayWalls = std::map<int, TerrainCell>;

struct RuntimeDiskLoopSource {
    const WorldObjectInstance* object = nullptr;
    std::string event;
    std::string stage;
};

struct RuntimeStoppedMovePoseRecord {
    int objectId = -1;
    std::array<double, 3> position{};
    bool onBridge = false;
    double animationDistance = 0.0;
};

struct RuntimeCarrierFlightPoseRecord {
    int objectId = -1;
    double x = 0.0, y = 0.0, level = 0.0, animationDistance = 0.0;
    bool moving = false;
    bool hasRenderPosition = true;
    std::optional<RuntimeObjectBehavior> order;
};

struct RuntimeSavedProductionCandidate {
    RuntimeProductionLanesState lanes;
    std::optional<RuntimeProductionLane> selectedPlacementLane;
    std::uint64_t selectedPlacementSerial = 0;
    int legacyCreditRefund = 0;
};

using RuntimeSavedProductionByOwner = std::unordered_map<
    std::string,
    RuntimeSavedProductionCandidate
>;

struct RuntimeSavedAirstrikeHostCandidate {
    RuntimeAirstrikeSystemState airstrikes;
    std::map<int, RuntimeAirstrikePlaneBinding> bindingsByToken;
    std::map<int, RuntimeAirstrikeRootAuthority> authoritiesByActor;
};

enum class RuntimeLoadFaultPointForSmoke {
    None,
    AfterScratchSeed,
    AfterSavedWorldProjection,
    BeforeCommit
};

struct RuntimeOreDamageResult {
    int cellsTouched = 0;
    int bailsRemoved = 0;
    bool changed = false;
};

struct RuntimeOreOverlayIdentity {
    int overlayIndex = 102;
    int bails = runtimeOreMaxBails;
    int variant = 1;
    std::string type = "TIB01";
    std::string image = "TIB01";
    std::string family = "TIB";
    bool gem = false;
    bool fixedVariant = false;
};

struct RuntimeOreFieldGrowthResult {
    bool changed = false;
    int sources = 0;
    int densified = 0;
    int spread = 0;
};

struct RuntimeOreFieldSource {
    RuntimeCellPoint cell;
    RuntimeOreOverlayIdentity identity;
    const TiberiumType* tiberium = nullptr;
    bool growthDue = false;
    bool spreadDue = false;
};

struct RuntimeMultiplayerSnapshotMemoryImage {
    std::vector<std::uint8_t> bytes;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
};

struct RuntimeCameraWorldBounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool valid = false;
};

struct RuntimeCombinedCameraBounds {
    RuntimeCameraWorldBounds playable;
    RuntimeCameraWorldBounds full;
};

struct RuntimeCameraRevealMargins {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct RuntimeVeterancyKillCredit {
    int victimObjectId = -1;
    std::string victimOwner;
    int victimCost = 0;
    int victimVeterancy = 0;
    bool awardsExperience = false;
};

struct RuntimeInfantryWaterPose {
    bool valid = false;
    bool swimming = false;
    double x = 0.0;
    double y = 0.0;
};

struct RuntimeProjectileSourcePosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
};

struct RuntimeSonicWavePoint { double x=0.0,y=0.0,z=0.0; };

struct RuntimeSonicWaveGeometry {
    RuntimeSonicWavePoint source,target,lowerFrom,lowerTo,upperFrom,upperTo;
};

enum class RuntimePanicClockStep { Suspended, Active, Expired };

struct RuntimeProjectileTerrainCollision {
    RuntimeCellPoint cell;
    int level = 0;
    int wallObjectId = -1;
};

enum class RuntimeOwnerTransitionRepairPolicy {
    CaptureDockedUnits,
    ReleaseRelations
};

struct RuntimeOwnerTransitionRoot {
    int objectId = -1;
    std::string expectedOwner;
    std::string newOwner;
};

struct RuntimeMindControlOwnerPreparation {
    int controllerObjectId = -1;
    int targetObjectId = -1;
    int capacity = 1;
    bool permanent = false;
    bool infinite = false;
    bool allowStructures = false;
};

struct RuntimeOwnerTransitionObjectPreparation {
    int objectId = -1;
    int health = 1;
    std::string mission;
    bool poweredOperational = true;
    int powerAnimationPausedAtTick = -1;
};

struct RuntimeOwnerTransitionGarrisonPreparation {
    int occupierId = -1;
    int hostId = -1;
};

struct RuntimeOwnerTransitionBatchResult {
    bool applied = false;
    std::vector<int> changedObjectIds;
    std::vector<SpecialMechanismEvent> specialEvents;
    std::string error;
};

struct RuntimeAirImpactBodyCenter {
    bool captured = false;
    int screenX = 0;
    int screenY = 0;
    int fixedX = 0;
    int fixedY = 0;
    RuntimeParticleAirbornePresentation presentation;
    int spriteOffsetY = 0;
};

struct RuntimeCratePlacement {
    RuntimeCellPoint cell;
    bool water = false;
};

struct RuntimeCrateCollector {
    int objectId = -1;
    std::string owner;
    RuntimeCellPoint cell;
};

enum class RuntimeCrateUnitBonus {
    Armor,
    Firepower,
    Speed
};

struct RuntimeCrateAward {
    bool granted = false;
    std::string type;
    std::string animation;
    int credits = 0;
};

struct RuntimeBridgeDamageTarget {
    RuntimeCellPoint anchor;
    bool high = true;
    bool alongX = true;
    RuntimeBridgeMaterial material = RuntimeBridgeMaterial::Concrete;
    std::vector<RuntimeCellPoint> physicalCells;
};

struct RuntimeBridgeDamageResult {
    bool hit = false;
    bool destroyed = false;
    int damage = 0;
    int healthRemaining = 0;
    RuntimeCellPoint anchor;
    std::vector<int> destroyedCellKeys;
};

struct RuntimeTerrainProjectileDamageResult {
    int objectsDamaged = 0;
    int objectsDestroyed = 0;
    int totalDamage = 0;
};

struct RuntimePreparedAirstrikeHostAction {
    RuntimeAirstrikeAction action;
    int objectId = -1;
    int rootProjectileActorId = -1;
};

struct RuntimeMixedLocalComponentStats {
    std::size_t attempts=0,admitted=0,referenceFallbacks=0,guardFallbacks=0;
    std::size_t layoutFallbacks=0,walkFallbacks=0,allocationFallbacks=0;
    std::size_t layoutEntries=0,vertices=0,orientedEdges=0,coveredBest=0;
    std::size_t connectedGoalHits=0,walkSteps=0;
    std::size_t detourExpansions=0,seedVetoes=0,edgeVetoes=0,qualifiedChronoRecovery=0,idleActors=0;
    std::size_t unmappedAttempts=0,unmappedProofs=0,unmappedMemoHits=0;
    std::size_t incomingQueries=0,incomingMemoHits=0,incomingPositiveVetoes=0;
    std::size_t unmappedOriginalChecks=0,unmappedOriginalMismatches=0;
    std::size_t frontierClosureAttempts=0,frontierClosureAdmissions=0,frontierClosureMemoHits=0;
    std::size_t frontierClosureRejected=0,frontierClosurePoints=0;
    std::size_t frontierClosureReferenceVetoes=0,frontierClosureAllocationFallbacks=0;
    int rejectionReason=0,rejectionActor=-1;
    std::size_t outsideFallbacks=0,nearestFallbacks=0,wholeMaps=0,lateWholeMaps=0;
    std::size_t orderedMapsCompared=0,orderedMapMismatches=0,lateChronoWitness=0,lateAirWitness=0;
};

enum class RuntimeTimedBombArrivalResult : std::uint8_t {
    NotPending = 0,
    Completed,
    Retargeted,
    Rejected
};

struct RuntimeTransportMotionBody {
    double x = 0.0, y = 0.0, level = 0.0;
    bool onBridge = false;
};

using RuntimeTransportMotionPoint = std::array<double, 2>;

enum class RuntimeSecondaryAbility {
    None,
    Deploy,
    Undeploy,
    TransportUnload,
    GarrisonUnload
};

enum class RuntimeGrindingTeamEligibility {
    Accepted,
    Transient,
    Permanent
};

struct RuntimeCrewSurvivorSpawn {
    std::string owner;
    std::string type;
    RuntimeCellPoint cell;
    RuntimeCellPoint scatter;
    int health = 1;
    bool selected = false;
};

struct RuntimeDeathCrateSpawn {
    RuntimeCellPoint center;
    bool money = false;
    int sourceId = -1;
};

enum class RuntimeRepairDepotArrivalResult {
    NotPending,
    Attached,
    Released
};

enum class RuntimeTeamStepDisposition {
    Waiting,
    Complete,
    Restart,
    Unsupported
};

enum class RuntimeTeamStepCompletion {
    Waiting,
    Complete,
    Reissue
};

struct RuntimeGroundBodyTransitionAdmission {
    std::optional<int> destinationSubCell;
    bool bodyBlocked = false;
    bool held = false;
};

struct RuntimeSharedMoveCrowdSearch {
    bool applicable = false;
    bool settledBarrier = false;
    bool transientBarrier = false;
    bool foreignBarrier = false;
    bool exhausted = true;
    bool inwardRoute = false;
    int idealRadius = 0;
    std::vector<RuntimeCellPoint> path;
};

enum class RuntimeAircraftDockReturnResult {
    NotApplicable,
    WaitingForDock,
    Queued,
    Descending,
    Docked
};

struct RuntimePrismSupportResult {
    double outgoingDamageMultiplier = 1.0;
    std::vector<int> supporterIds;
};

struct RuntimeMissileDeckPresentation {
    int x = 0, y = 0, fixedX = 0, fixedY = 0;
};

struct RuntimeComputerAiProfile {
    int thinkIntervalTicks = 20;
    int buildRetryTicks = 30;
    int minimumBuildTicks = 90;
    int initialAttackDelayTicks = 600;
    int attackIntervalTicks = 900;
    int attackRetryTicks = 150;
    int repairIntervalTicks = 60;
    int maximumCombatUnits = 28;
    int attackThreshold = 6;
    int attackWaveSize = 7;
    int maximumDefenders = 5;
};

struct RuntimeComputerAiMetrics {
    int structures = 0;
    int mobileUnits = 0;
    int combatUnits = 0;
    int constructionYards = 0;
    int powerPlants = 0;
    int powerProduced = 0;
    int powerUsed = 0;
    int refineries = 0;
    int ordinaryRefineries = 0;
    int slaveMiners = 0;
    int harvesters = 0;
    int barracks = 0;
    int vehicleFactories = 0;
    int shipyards = 0;
    int radars = 0;
    int techCenters = 0;
    int defenses = 0;
    int superweaponStructures = 0;
};

enum class RuntimeComputerAiStructureRole {
    Power,
    Refinery,
    Barracks,
    VehicleFactory,
    Shipyard,
    Radar,
    Defense,
    Tech,
    Superweapon
};

struct RuntimeComputerAiBaseTargets {
    int defenses = 0;
    int refineries = 1;
    bool slaveRefineries = false;
};

enum class RuntimeComputerAiUnitRole {
    Harvester,
    Infantry,
    Vehicle,
    Naval,
    Engineer
};

bool pathExists(const std::filesystem::path& path);

std::string pathString(const std::filesystem::path& path);

bool containsAsciiCaseInsensitive(
    const std::string& value,
    const std::string& needle);

int runtimeSectionEntryCount(
    const IniFile& ini,
    const std::string& sectionName);

std::vector<std::string> runtimeIndexedIds(
    const IniFile& ini,
    const std::string& sectionName);

std::string runtimeWaypointKeyFromName(const std::string& value);

std::string runtimeTileRootPath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map);

std::string runtimePalettePath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map);

std::string runtimeAssetRootPath(const RuntimeConfig& config);

std::string runtimeStructureRootPath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map);

std::vector<std::string> unitPaletteCandidatesForRuntimeTheater(
    const std::string& theater);

bool runtimePresentationReferenceEnabled();

std::string resolveRuntimeStructurePalettePath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map);

std::string runtimeStructurePalettePath(
    const RuntimeConfig& config, const RuntimeMapSummary& map);

std::wstring widenUtf8(const std::string& value);

std::string formatMaybe(const std::string& value);

bool isExistingDirectory(const std::string& path);

bool isExistingRegularFile(const std::string& path);

RuntimeCampaignDataSourceOptions runtimeCampaignDataSourceOptions(
    const RuntimeConfig& config);

RuntimeIniSource resolveRuntimeIniSource(
    const RuntimeConfig& config,
    RuntimeCampaignIniKind kind);

bool loadRuntimeIniSource(
    const RuntimeConfig& config,
    RuntimeCampaignIniKind kind,
    IniFile& ini,
    RuntimeIniSource* resolvedSource = nullptr
);

std::vector<ResourceMountRequest> buildResourceMountRequests(
    const RuntimeConfig& config);

void addRuntimeLightweightResourceFile(
    ResourceMountLayerReport& layer,
    const std::filesystem::path& filePath);

void accumulateRuntimeLightweightResourceLayer(
    ResourceMountReport& report,
    const ResourceMountLayerReport& layer);

ResourceMountReport buildRuntimeLightweightResourceMountReport(
    const RuntimeConfig& config,
    const std::string& rulesPath,
    const std::string& artPath);

std::optional<RuntimeMapColorValue> runtimeColorFromHsvString(const std::string& value);

std::optional<RuntimeMapColorValue> runtimeColorFromRgbString(const std::string& value);

RuntimeOwnerRemapColorTable buildRuntimeRemapColorTable(
    const IniFile* rulesIni);

RuntimeOwnerRemapColorTable loadRuntimeRemapColorTable(
    const RuntimeConfig& config);

RuntimeMapColorValue loadRuntimeLocalRadarColor(const RuntimeConfig& config);

RuntimeOwnerRemapColorTable loadRuntimeMapOwnerRemapColors(
    const RuntimeConfig& config,
    std::unordered_set<std::string>* neutralOwners = nullptr
);

std::string runtimeDefaultModeRulesOverride(const std::string& gameMode);

std::string resolveRuntimeModeRulesOverridePath(const RuntimeConfig& config);

const IniFile::Section* runtimeIniSectionNoCase(
    const IniFile& ini,
    const std::string& wanted);

std::optional<std::string> runtimeIniSectionOptionalValueNoCase(
    const IniFile::Section* section,
    const std::string& wanted);

std::string runtimeTimedBombIntentKindName(
    RuntimeTimedBombIntentKind kind);

std::optional<RuntimeTimedBombIntentKind>
runtimeTimedBombIntentKindFromName(const std::string& value);

std::string runtimeProjectileLifecycleName(
    RuntimeProjectileLifecycleState state);

double runtimeNormalizeCratePowerupData(double data);

std::uint64_t runtimeUiClockNow(const RuntimeProductionHostState& state) noexcept;

std::uint64_t runtimeWorldAudioClockNow(const RuntimeProductionHostState& state) noexcept;

int runtimeCampaignClockSeconds(const std::string& value);

void initializeRuntimeCampaignScore(RuntimeProductionHostState& state);

int runtimeCampaignScoreElapsedTicks(const RuntimeCampaignScoreState& score);

void finalizeRuntimeCampaignScore(RuntimeProductionHostState& state, bool victory);

bool runtimeSkirmishModeMatches(const RuntimeGameSessionState& state,
    const char* mode, const char* file);

bool runtimeIsNavalWarMode(const RuntimeGameSessionState& state);

bool runtimeIsMegawealthMode(const RuntimeGameSessionState& state);

bool runtimeIsLandRushMode(const RuntimeGameSessionState& state);

bool runtimeIsUnholyAllianceMode(const RuntimeGameSessionState& state);

long long runtimeSidebarPowerAnimationElapsedSteps(
    const RuntimeProductionHostState& state);

int runtimeAdvanceSidebarPowerValue(
    int start,
    int target,
    long long elapsedSteps);

RuntimeSidebarPowerDisplay runtimeSidebarPowerDisplay(
    const RuntimeProductionHostState& state);

void snapRuntimeSidebarPowerAnimation(RuntimeProductionHostState& state);

void retargetRuntimeSidebarPowerAnimation(
    RuntimeProductionHostState& state,
    int produced,
    int used);

bool runtimeSidebarPowerAnimationActive(const RuntimeProductionHostState& state);

int runtimeCreditBalanceAfterDeposit(int currentBalance, long long deposit);

void depositRuntimeCredits(
    RuntimeGameSessionState& state,
    long long amount);

std::string runtimeSkirmishOwnerForSlot(std::size_t slotIndex);

std::string runtimeNormalizeSkirmishTeam(const std::string& configuredTeam);

std::string runtimeResolvedSkirmishCountry(
    const std::string& configured,
    std::size_t slotIndex);

void initializeRuntimeSkirmishPlayerIdentities(
    RuntimeGameSessionState& state,
    const std::string& fallbackCountry);

std::string runtimeCanonicalHumanHouseOwner(const std::string& owner);

bool runtimeComputerAiEnabledForSession(const RuntimeGameSessionState& state);

bool runtimeComputerAiPausedForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner);

RuntimeHumanHouseAuthority* findRuntimeHumanHouseAuthority(
    RuntimeGameSessionState& state,
    const std::string& owner);

const RuntimeHumanHouseAuthority* findRuntimeHumanHouseAuthority(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeNoCooldownForOwner(const RuntimeGameSessionState& state, const std::string& owner);

RuntimeEconomyState* runtimeHumanHouseEconomy(
    RuntimeProductionHostState& state,
    const std::string& owner);

const RuntimeEconomyState* runtimeHumanHouseEconomy(
    const RuntimeProductionHostState& state,
    const std::string& owner);

bool runtimeIsCooperativeMode(const RuntimeConfig& config);

std::filesystem::path runtimeCooperativeScenarioPath(
    const RuntimeConfig& config);

bool initializeRuntimeCooperativeSession(RuntimeProductionHostState& state);

std::optional<int> runtimeCooperativeSlotForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner);

std::string runtimeCountryForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeOwnersAreAllied(
    const RuntimeGameSessionState& state,
    const std::string& leftOwner,
    const std::string& rightOwner);

void recordRuntimeCampaignObjectDestruction(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& sourceOwner);

bool runtimeObserverOwner(const RuntimeGameSessionState& state, const std::string& owner);

template<class Session>
RuntimeVisionPhaseContext runtimeVisionPhaseContext(Session& session);

bool updateRuntimeVisionState(RuntimeProductionHostState& state);

bool runtimeObjectHiddenFromOwnerByStealth(
    const RuntimeGameSessionState& state,
    const std::string& observerOwner,
    const WorldObjectInstance& object);

bool runtimeObjectVisibleToOwner(
    const RuntimeGameSessionState& state,
    const std::string& observerOwner,
    const WorldObjectInstance& object);

RuntimeSnapshotCell toRuntimeSnapshotCell(const RuntimeCellPoint& cell);

std::uint64_t runtimeObjectIdSetHash(
    const std::unordered_set<int>* objectIds);

RuntimeIniSource findRuntimeSoundIniSource(
    const RuntimeGameSessionState& state);

RuntimeIniSource findRuntimeEvaIniSource(
    const RuntimeGameSessionState& state);

RuntimeLightingConfig runtimeLightingConfigForState(
    const RuntimeGameSessionState& state);

void ensureRuntimeLightingInitialized(RuntimeGameSessionState& state);

void ensureRuntimeParticlesInitialized(RuntimeGameSessionState& state);

RuntimeWorldAudioContext runtimeWorldAudioContext(
    const RuntimeProductionHostState& state);

void initializeRuntimeWorldAudio(RuntimeProductionHostState& state);

bool loadRuntimeSoundDatabase(RuntimeProductionHostState& state);

std::string stripRuntimeSoundComment(const std::string& value);

std::vector<std::string> splitRuntimeWhitespaceTokens(const std::string& value);

bool isRuntimeSemanticSidebarSoundIntent(const std::string& intent);

std::string runtimeRulesSoundSection(
    const RuntimeGameSessionState& state,
    const std::string& key,
    const std::string& fallback);

std::string runtimeEvaSectionFromEvent(const std::string& eventName);

std::string runtimeSoundSectionFromEvent(
    const RuntimeGameSessionState& state,
    const std::string& eventName);

bool runtimeSoundSectionIsEva(
    const RuntimeProductionHostState& state,
    const std::string& sectionName);

std::string runtimeEvaSubtitleTextFromSection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName);

int runtimeSoundVolumePercent(
    const RuntimeProductionHostState& state,
    const std::string& sectionName);

std::string runtimeEvaVoiceKeyForOwner(const RuntimeGameSessionState& state);

AudioBackgroundMusicFaction runtimeMusicFaction(
    const RuntimeGameSessionState& state);

std::string runtimeMusicFactionName(AudioBackgroundMusicFaction faction);

std::uint64_t advanceRuntimeMusicRandom(std::uint64_t& state);

std::size_t chooseRuntimeMusicIndex(
    std::size_t trackCount,
    std::optional<std::size_t> previousIndex,
    std::uint64_t& randomState);

void prepareRuntimeMusicPlaylist(RuntimeProductionHostState& state);

std::string runtimeEvaFallbackSample(
    const std::string& sectionName,
    const std::string& voiceKey);

int runtimeEvaPriorityFromSection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName);

bool runtimeEvaQueueRequestedBySection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName);

std::optional<std::string> resolveRuntimeSoundSampleName(
    const RuntimeProductionHostState& state,
    const std::string& eventName,
    const std::string& sectionName);

std::optional<std::string> findRuntimeSoundSamplePath(
    RuntimeProductionHostState& state,
    const std::string& sampleName);

std::uint16_t runtimeReadLe16(const unsigned char* bytes);

std::uint32_t runtimeReadLe32(const unsigned char* bytes);

int runtimeWavDurationTicks(
    const std::string& wavPath,
    int fixedTickHz);

void rememberRuntimeSoundPlayback(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record);

int runtimeEvaBusyTicksForSample(
    const std::optional<std::string>& samplePath,
    int fixedTickHz);

int runtimeEvaSubtitleDurationTicks(
    const std::string& text,
    const std::optional<std::string>& samplePath,
    int fixedTickHz);

void clearRuntimeEvaSubtitle(RuntimeProductionHostState& state);

void presentRuntimeEvaSubtitle(
    RuntimeProductionHostState& state,
    const RuntimeSoundPlaybackRecord& record);

concord::audio::RuntimeLegacyWaveDevice& runtimeEvaWaveDevice();

concord::audio::RuntimeLegacyWaveDevice& runtimeShellMusicWaveDevice();

concord::audio::RuntimeAsyncSoundDevice& runtimeAsyncSoundDevice();

void closeRuntimeEvaMciChannel();

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& object);

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForImpact(const RuntimeImpactEffect& impact);

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeProductionHostState& state,
    const RuntimeSpatialAudioRules& rules, const RuntimeSpatialAudioPosition& source);

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeProductionHostState& state,
    const std::string& event, const RuntimeSpatialAudioPosition& source);

int runtimeSpatialAudioCombinedGain(const RuntimeProductionHostState& state, int authoredPermille, int spatial16384);

int runtimeSpatialAudioGainPermille(const RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& sound, const RuntimeSpatialAudioPosition& source);

RuntimeVoiceAudio& runtimeVoiceAudioFor(RuntimeProductionHostState& state);

std::uint64_t runtimeSfxVoiceHandle(std::size_t channel);

bool playRuntimeSfxOnDedicatedChannel(RuntimeProductionHostState& state,
    const std::string& path, const RuntimeSoundPlaybackRecord& record);

void stopRuntimeSfxPlayback(RuntimeProductionHostState& state);

void applyRuntimeAudioVariation(RuntimeProductionHostState& state, RuntimeSoundPlaybackRecord& record);

std::optional<bool> runtimeEvaMciChannelIsPlaying();

bool playRuntimeEvaOnDedicatedChannel(
    const std::string& samplePath,
    int volumePercent);

void stopRuntimeEvaPlayback(RuntimeProductionHostState& state);

void enforceRuntimeSoundVoiceLimit(RuntimeProductionHostState& state,
    const std::string& event, std::uint64_t excludedHandle);

void playRuntimeResolvedSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record);

void queueRuntimeEvaSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record);

void suppressRuntimeEvaSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record,
    const std::string& reason);

void processRuntimeEvaQueue(RuntimeProductionHostState& state);

void noteRuntimeSoundIntent(RuntimeProductionHostState& state, const std::string& eventName);

std::string runtimeArtAnimationSound(const RuntimeProductionHostState& state, const ArtType* art);

void emitRuntimeSound(RuntimeProductionHostState& state, const std::string& eventName,
    std::optional<RuntimeSpatialAudioPosition> source = std::nullopt,
    bool trackCamera = false);

std::string runtimeCaptureEvaSection(const RuntimeProductionHostState& state, const BuildingType& type);

void emitRuntimeEngineerCaptureEva(RuntimeProductionHostState& state, const BuildingType* type,
    const std::string& previousOwner, const std::string& captureOwner);

void snapRuntimeCreditAudio(RuntimeProductionHostState& state);

std::optional<std::string> selectRuntimeMusicPath(
    RuntimeProductionHostState& state,
    bool advanceTrack);

bool runtimeMusicThemeIsUnspecified(const std::string& rawThemeId);

bool selectRuntimeMusicThemePath(
    RuntimeProductionHostState& state,
    const std::string& rawThemeId,
    std::string* errorMessage = nullptr
);

void emitRuntimeLowPowerWarning(
    RuntimeProductionHostState& state,
    bool includePowerOffCue);

std::optional<std::string> findRuntimeMusicPath(
    RuntimeProductionHostState& state,
    bool advanceTrack = false
);

void startRuntimeMusic(
    RuntimeProductionHostState& state,
    bool advanceTrack = false
);

void stopRuntimeMusic(RuntimeProductionHostState& state);

bool playRuntimeMusicTheme(
    RuntimeProductionHostState& state,
    const std::string& themeId,
    std::string* errorMessage = nullptr
);

RuntimeInputPixelRect runtimeMapPixelViewport(const RuntimeInputPixelRect& clientRect);

RuntimeProjectedPoint runtimeProjectCellXY(
    double x,
    double y,
    double level,
    const RuntimeMapCamera& camera);

double runtimeObjectRenderX(const WorldObjectInstance& object);

double runtimeObjectRenderY(const WorldObjectInstance& object);

double runtimeObjectRenderLevel(const WorldObjectInstance& object);

RuntimeInputPixelPoint runtimeScreenPixelPoint(
    const RuntimeProjectedPoint& point,
    const RuntimeInputPixelRect& viewport,
    const RuntimeMapCamera& camera);

std::optional<RuntimeRadarMapSize> parseRuntimeRadarMapSize(
    const std::string& value);

RuntimeRadarDxyPoint runtimeRadarDxyPoint(
    double x,
    double y,
    int fullMapWidth);

bool runtimeRadarTerrainCellIntersectsProjection(
    const RuntimeRadarProjection& projection,
    const TerrainCell& terrain);

RuntimeRadarProjection buildRuntimeRadarProjection(
    const RuntimeProductionHostState& state);

double runtimeDrawLevelForObject(const WorldObjectInstance& object);

int runtimeCellKey(int x, int y);

bool parseRuntimePositiveIntToken(
    const std::string& value,
    std::size_t begin,
    std::size_t end,
    int& parsed);

RuntimeStructureFootprint parseRuntimeFoundationSize(
    const std::string& foundation);

RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& object);

double runtimeCellDistance(
    const RuntimeCellPoint& left,
    const RuntimeCellPoint& right);

std::array<RuntimeCellPoint, 8> runtimeNeighborsOf(
    const RuntimeCellPoint& point);

std::unordered_map<int, const TerrainCell*> buildRuntimeTerrainLookup(
    const GameWorld& world);

std::unordered_map<std::string, RuntimeStructureFootprint>
buildRuntimeStructureFootprints(const RuntimeGameSessionState& state);

RuntimeStructureFootprint runtimeStructureFootprintForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectUsesStructureFootprint(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeCellPoint runtimePlacementAnchorCellForFootprint(
    const RuntimeCellPoint& topLeftCell,
    const RuntimeStructureFootprint& footprint);

RuntimeProjectileTargetPosition runtimeProjectileTargetPosition(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeCellPoint runtimeTargetCellForProjectile(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeCellPoint runtimePlacementTopLeftForAnchor(
    const RuntimeCellPoint& anchorCell,
    const RuntimeStructureFootprint& footprint);

RuntimeCellPoint runtimePlacementFoundationCenterOffset(
    const RuntimeStructureFootprint& footprint);

std::vector<RuntimeCellPoint> runtimeStructureFootprintCells(
    const RuntimeCellPoint& topLeftCell,
    const RuntimeStructureFootprint& footprint);

std::vector<RuntimeCellPoint> runtimeStructureFootprintCellsForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectMayUseStructureDriveRows(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover);

int runtimeStructureBlockedDriveRowCount(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure);

bool runtimeStructureCellInsideFoundation(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure,
    const RuntimeCellPoint& cell);

bool runtimeStructureCellIsVehicleDriveRow(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure,
    const RuntimeCellPoint& cell,
    const WorldObjectInstance& mover);

bool runtimeStructureCellBlocksVehicleNavigation(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure,
    const RuntimeCellPoint& cell);

std::vector<RuntimeCellPoint> runtimeStructureNavigationBlockerCellsForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure);

int runtimeStructureDepartureHostIdReference(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover);

int runtimeStructureDepartureHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover);

std::uint64_t runtimeFootprintCellsHash(
    const std::vector<RuntimeCellPoint>& cells);

double runtimeCellDistanceToObjectFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& object);

double runtimeWorldDistanceToObjectFootprint(
    const RuntimeGameSessionState& state,
    double worldX,
    double worldY,
    const WorldObjectInstance& object);

bool runtimeC4CellOutsideFoundation(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    const WorldObjectInstance& target);

bool runtimeC4BodyOutsideFoundation(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target);

int runtimeTerrainOccupantId(std::size_t index);

bool runtimeOccupantIdIsTerrain(int occupantId);

bool runtimeTerrainObjectBlocksGround(
    const WorldTerrainObject& object);

std::vector<RuntimeCellPoint> runtimeTerrainObjectFootprintCells(
    const WorldTerrainObject& object);

std::string runtimeTerrainObjectStateKey(
    const WorldTerrainObject& object,
    std::size_t index);

std::uint64_t runtimeTerrainObjectLayoutSignature(
    const RuntimeGameSessionState& state);

void captureRuntimeInitialTerrainObjectState(RuntimeGameSessionState& state);

std::unordered_map<int, int> buildRuntimeOccupancyLookup(
    const RuntimeGameSessionState& state,
    int ignoredObjectId = -1,
    RuntimeNavigationStats* stats = nullptr,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    bool includeStructures = true,
    std::unordered_map<int, int>* occupantCountByCell = nullptr,
    RuntimeStructureOccupancyMode structureMode =
        RuntimeStructureOccupancyMode::FullFoundation,
    std::size_t initialReserveLimit = std::numeric_limits<std::size_t>::max()
);

RuntimeGroundPoseOccupancyContext runtimeGroundPoseContext(const RuntimeGameSessionState& state);

bool runtimeGroundPoseIsSpatialCandidate(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeGroundPoseSpatialIndexIsCurrent(const RuntimeGameSessionState& state);

bool runtimeGroundPoseSpatialKeys(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,std::vector<int>& keys);

bool runtimeAllGroundBodyIsSpatialCandidate(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeAllGroundBodySpatialIndexIsCurrent(const RuntimeGameSessionState& state);

bool runtimeAppendAllGroundBodySpatialObject(RuntimeGameSessionState& state,std::size_t index);

template<class Visitor>
void forEachRuntimeAllGroundBodyCandidateAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit);

bool buildRuntimeGroundPoseSpatialIndex(RuntimeGameSessionState& state);

bool runtimeTrackGroundPoseSpatialMove(RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

template<class Visitor>
void forEachRuntimeGroundPoseCandidateAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits =nullptr);

template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupantAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits =nullptr);

bool runtimeFrameScalingReferenceMovement();

void captureRuntimeMovementEmptyCellProof(RuntimeGameSessionState& state);

bool runtimeMovementTickProvesEmptyCell(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell);

const std::unordered_map<int, int>& runtimeTickOccupancyLookup(
    RuntimeGameSessionState& state);

void updateRuntimeTickOccupancyForMove(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeCellPoint& previous,
    const RuntimeCellPoint& next);

void invalidateRuntimeTickOccupancyCache(RuntimeGameSessionState& state);

void activateRuntimeWorldObject(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object);

const UnitType* findRuntimeUnitType(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

const UnitType* runtimeAmmoRulesForObject(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

void initializeRuntimeObjectAmmo(
    WorldObjectInstance& object,
    const UnitType* unit,
    bool refillToCapacity = false
);

bool runtimeObjectHasAttackAmmo(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

bool runtimeUnitUsesJumpjetMovement(const UnitType& unit);

bool runtimeUnitUsesAutomaticJumpjetLanding(const UnitType& unit);

bool runtimeUnitUsesAirMovement(const UnitType& unit);

int runtimeUnitFlightLevelOffset(const UnitType& unit);

void applyRuntimeInitialObjectFlightState(
    const RulesDatabase& rules,
    WorldObjectInstance& object,
    bool startOnGround = false
);

void hydrateRuntimeObjectFlightState(
    GameWorld& world,
    const RulesDatabase& rules);

bool runtimeInitialObjectSubmergedState(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

bool runtimeLoadedObjectSubmergedState(
    const std::vector<std::string>& tokens,
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

void hydrateRuntimeObjectSubmergedState(
    GameWorld& world,
    const RulesDatabase& rules);

const BuildingType* findRuntimeBuildingType(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

const BuildingType* findRuntimeBuildingTypeById(
    const RulesDatabase& rules,
    const std::string& id);

const VeterancyProfile* runtimeVeterancyProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeVeterancyHasAbility(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    VeteranAbility ability);

int runtimeCloakDelayTicks(const RuntimeGameSessionState& state);

int runtimeObjectCloakTransitionTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectCanCloak(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

int runtimeObjectSensorsSight(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void initializeRuntimeObjectCloakState(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object);

double runtimeVeterancyAbilityMultiplier(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    VeteranAbility ability);

int runtimeVeterancyObjectCost(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeVeterancyObjectIsTrainable(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeVeterancyObjectAwardsExperience(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void recordRuntimeMatchBuild(RuntimeGameSessionState& state, const std::string& owner, int count = 1);

void recordRuntimeMatchObjectDestruction(
    RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const std::string& sourceOwner);

void writeRuntimeMatchScore(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeMatchScore(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

void hashRuntimeMatchScore(const RuntimeGameSessionState& state, std::uint64_t& hash);

bool runtimeVeterancyTypeListContains(
    const std::vector<std::string>& typeIds,
    const std::string& typeId);

bool runtimeCountryGrantsVeterancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void applyRuntimeProducedObjectVeterancy(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object);

void applyRuntimeInitialObjectVeterancy(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object);

void mergeRuntimeGrandCannonPrewarm(RuntimeProductionHostState& state);

const UnitType* findRuntimeUnitTypeById(
    const RulesDatabase& rules,
    const std::string& id);

WorldObjectKind runtimeKindForBuildItem(RuntimeBuildKind kind);

RuntimeBuildKind runtimeBuildKindForUnitTypeId(
    const RulesDatabase& rules,
    const std::string& id);

int runtimeLegacyBuildTimeTicks(int cost, bool wall = false);

double runtimeAuthoredBuildTimeMultiplier(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

double runtimeBuildTimeMultiplier(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

RuntimeProductionRate runtimeSidebarProductionRate(
    RuntimeBuildKind kind,
    RuntimeProductionRate authoredRate);

RuntimeProductionTimingResult runtimeProductionTimingForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    int poweredCompatibleFactoryCount,
    double powerProduced,
    double powerUsed);

int runtimeBaseBuildTimeTicks(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

RuntimeScriptType parseRuntimeScriptType(
    const IniFile& ini,
    const std::string& id);

RuntimeTaskForce parseRuntimeTaskForce(
    const IniFile& ini,
    const std::string& id);

RuntimeTeamType parseRuntimeTeamType(
    const IniFile& ini,
    const std::string& id);

int runtimeHexNibble(char value);

bool parseRuntimeAiTriggerConditionPayload(
    const std::string& value,
    int& number,
    int& comparator);

RuntimeCampaignAiTriggerDefinition parseRuntimeCampaignAiTrigger(
    const std::string& id,
    const std::string& value);

void loadRuntimeCampaignAiTriggers(
    RuntimeTriggerState& state,
    const IniFile& ini);

void loadRuntimeComputerTacticTriggers(RuntimeGameSessionState& state,
    const IniFile* globalAi, const IniFile& mapIni);

std::map<std::string, std::vector<RuntimeTriggerEvent>>
parseRuntimeEventsByTrigger(const IniFile& ini);

std::map<std::string, std::vector<RuntimeTriggerAction>>
parseRuntimeActionsByTrigger(const IniFile& ini);

std::vector<RuntimeTriggerDefinition> parseRuntimeTriggers(
    const IniFile& ini,
    const std::map<std::string, std::vector<RuntimeTriggerEvent>>& eventsByTrigger,
    const std::map<std::string, std::vector<RuntimeTriggerAction>>& actionsByTrigger);

bool runtimeBuildingCountsAsRefinery(const BuildingType& building);

int runtimeBuildingHarvesterDockCount(const BuildingType& building);

bool runtimeBuildingAcceptsHarvesterUnload(const BuildingType& building);

bool runtimeHarvesterCanUnloadAtRefinery(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery,
    const std::string* owner = nullptr
);

bool runtimeBuildingCanGatherResources(const BuildingType& building);

bool runtimeBuildingStoresResources(const BuildingType& building);

int runtimeResourceStorageCapacityForOwnerObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner);

int runtimeOwnedResourceStorageCapacityForOwner(const RuntimeGameSessionState& state, const std::string& owner);

int runtimeScaleResourceValue(int value, int acceptedUnits, int totalUnits);

bool spendRuntimeCredits(RuntimeGameSessionState& state, int requestedAmount);

void spendRuntimeComputerStoredOre(RuntimeGameSessionState& state,
    const std::string& owner, int amount);

HouseEconomyContext makeRuntimeHouseEconomyContext(RuntimeEconomyState& economy, bool computer);

void refreshRuntimeResourceStorageCapacity(const RuntimeGameSessionState& state, RuntimeEconomyState& economy);

void refreshRuntimeResourceStorageCapacity(RuntimeGameSessionState& state);

void recordRuntimeHouseResourceDelivery(RuntimeGameSessionState& state, HouseEconomyContext& context,
    RuntimeEconomyState& feedback, int units, int gems, int value);

bool runtimeHasOwnedBuilding(
    const RuntimeGameSessionState& state,
    const std::string& id);

bool runtimeHasOwnedFactory(
    const RuntimeGameSessionState& state,
    const std::string& factory);

bool runtimeHasOwnedRadar(const RuntimeGameSessionState& state);

bool runtimeRadarHasPower(const RuntimeGameSessionState& state);

bool runtimeRadarAvailable(const RuntimeGameSessionState& state);

bool runtimeHasOwnedRefinery(const RuntimeGameSessionState& state);

int runtimeComputerMiningDifficultyIndex(const std::string& difficulty);

bool runtimeOwnerListAllowsHouse(
    const std::string& ownerList,
    const std::string& house);

std::string runtimeModeEffectiveOwnerList(
    const RuntimeGameSessionState& state,
    const std::string& typeId,
    const std::string& baseOwnerList);

bool runtimeOwnerListsOverlap(
    const RuntimeGameSessionState& state,
    const std::string& leftOwnerList,
    const std::string& rightOwnerList);

int runtimeOwnerListPlanningSide(const std::string& ownerList);

int runtimeBuildItemPlanningSide(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

bool runtimeFactoryOwnerCompatibleWithBuildItem(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory,
    const RuntimeBuildCatalogItem& item);

bool runtimeOwnerHasCompatibleFactory(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item);

bool runtimeOwnerHasConstructionYard(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeOwnerHasAlternateRefineryPrerequisite(
    const RuntimeGameSessionState& state, const std::string& owner);

bool runtimePrerequisiteTokenMet(
    const RuntimeGameSessionState& state,
    const std::string& token);

bool runtimePrerequisitesMet(
    const RuntimeGameSessionState& state,
    const std::string& prerequisites,
    std::string* missingToken = nullptr
);

bool runtimePrerequisiteOverrideMet(
    const RuntimeGameSessionState& state,
    const std::string& prerequisiteOverride);

int runtimePowerNet(const RuntimeGameSessionState& state);

int runtimePowerNetAfterBuildItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

std::string runtimeBuildLimitKey(const std::string& value);

int runtimeBuildLimitMagnitude(int configuredLimit);

int runtimeLiveOwnedTypeCount(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& typeId);

bool runtimeProductionItemKeysMatch(
    std::string_view left,
    std::string_view right);

int runtimePendingBuildTypeCount(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& typeId);

int runtimeLimitedBuildHistoryCount(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& typeId);

void recordRuntimeCompletedBuild(
    RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    int count = 1
);

void initializeRuntimeLimitedBuildHistory(RuntimeGameSessionState& state);

int runtimeBuildLimitUsedCount(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    bool includePending);

std::string runtimeBuildLimitBlockReason(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    bool computerPlayer,
    bool includePending);

RuntimeSpecialBuildingContext makeRuntimeSpecialBuildingContext(
    const RuntimeGameSessionState& state);

std::optional<RuntimeFactoryPlantCostCategory>
runtimeFactoryPlantCostCategoryForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

int runtimeRawBuildCostForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

int runtimeAdjustedFactoryPlantCost(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    int baseCost,
    const RuntimeFactoryPlantCostSnapshot* costSnapshot = nullptr
);

bool refreshRuntimeFactoryPlantCatalog(RuntimeGameSessionState& state);

RuntimeSpecialBuildingUnlockKind runtimeSpecialUnlockKind(
    const RuntimeBuildCatalogItem& item);

bool runtimeBuildItemHasSecretLabUnlock(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item);

bool runtimeBuildItemMeetsStolenTechRequirements(
    const RuntimeGameSessionState& state, const std::string& owner,
    const RuntimeBuildCatalogItem& item);

RuntimeTechTreeDecision evaluateRuntimeTechTreeDecisionUnmemoized(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

RuntimeTechTreeDecision evaluateRuntimeTechTreeDecision(
    const RuntimeGameSessionState& state, const RuntimeBuildCatalogItem& item);

bool runtimeBuildItemUnlockedForSidebar(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

int runtimeUnlockedBuildOptionCount(const RuntimeGameSessionState& state);

const WeaponType* findRuntimeWeaponType(
    const RulesDatabase& rules,
    const std::string& weaponId);

const ProjectileType* findRuntimeProjectileType(
    const RulesDatabase& rules,
    const std::string& projectileId);

const WarheadType* findRuntimeWarheadType(
    const RulesDatabase& rules,
    const std::string& warheadId);

std::vector<double> parseRuntimeVerses(const std::string& verses);

int runtimeArmorIndex(const std::string& armor);

double runtimeWarheadArmorMultiplier(
    const WarheadType* warhead,
    const std::string& armor);

std::string runtimeTargetArmor(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::string runtimeTargetEffectiveArmor(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const WarheadType* warhead);

std::string chooseRuntimeImpactAnim(
    const WarheadType* warhead,
    const WeaponType& weapon);

std::string runtimeMissionName(RuntimeMissionIntent intent);

void recordRuntimeReplayEvent(
    RuntimeProductionHostState& state,
    RuntimeReplayEventSnapshot event);

void ensureRuntimeSpecialMechanismsInitialized(RuntimeGameSessionState& state);

void ensureRuntimeGarrisonsInitialized(RuntimeGameSessionState& state);

void ensureRuntimeSuperWeaponsInitialized(RuntimeGameSessionState& state);

std::vector<std::array<int, 4>> runtimeParasiteLinkedHostPoses(
    RuntimeGameSessionState& state);

bool notifyRuntimeParasiteHostsRelocated(
    RuntimeProductionHostState& state,
    const std::vector<std::array<int, 4>>& before);

SuperWeaponWorldContext runtimeSuperWeaponContext(RuntimeProductionHostState& state);

std::function<void(const SuperWeaponEvent&)> runtimeSuperWeaponEventSink(
    RuntimeProductionHostState& state);

int runtimeAiSuperDefenseProbabilityIndex(const std::string& difficulty);

bool runtimeIsForceShieldSuperWeaponRule(const SuperWeaponRule* rule);

bool runtimeComputerTacticReservesSuperWeapon(const RuntimeGameSessionState& state,
    const std::string& owner, const SuperWeaponRule* weapon);

bool advanceRuntimeSuperWeapons(RuntimeProductionHostState& state);

void applyRuntimeSuperWeaponLightingLifecycle(
    RuntimeProductionHostState& state,
    const SuperWeaponActiveLifecycleEvent& event);

std::map<std::uint64_t, RuntimeActiveLightingToken>
runtimeActiveLightingTokensForSuperWeapons(const RuntimeGameSessionState& state);

RuntimeLightingMode runtimeEffectiveLightingModeForTokens(
    const std::map<std::uint64_t, RuntimeActiveLightingToken>& activeTokens);

bool syncRuntimeLightingSuperWeaponProfiles(RuntimeProductionHostState& state);

std::string runtimeTriggerSuperWeaponId(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    int actionType,
    const std::string& targetId);

bool runtimeCampaignPreferredTargetMatchesQuarry(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    int quarry);

std::optional<SuperWeaponTarget> runtimeTriggerSuperWeaponTarget(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool fireRuntimeTriggerSuperWeapon(
    RuntimeProductionHostState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action);

int runtimeBombSightForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectHasBombDisarmWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeTimedBombDetectedForOwner(
    const RuntimeGameSessionState& state,
    int targetObjectId,
    const std::string& observerOwner);

std::optional<SpecialMechanismRenderSnapshot> runtimeSuperWeaponBuildingSnapshot(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

void appendRuntimeSuperWeaponBuildingSnapshots(RuntimeGameSessionState& state);

RuntimeNuclearFlightPose runtimeSuperWeaponNuclearFlightPose(
    const RuntimeGameSessionState& state, const SuperWeaponRenderSnapshot& missile);

int runtimeCamouflageFakeBlinkTicks(const RuntimeGameSessionState& state, int originalFrames);

void refreshRuntimeSpecialMechanismRenderSnapshots(RuntimeProductionHostState& state);

SpecialEventSink runtimeSpecialEventSink(RuntimeProductionHostState& state);

void recordRuntimeCommandReplayEvent(RuntimeProductionHostState& state);

void setRuntimeMissionIntent(
    WorldObjectInstance& object,
    RuntimeMissionIntent intent);

void recordRuntimeMissionTransition(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& object,
    const std::string& previousMission,
    const std::string& nextMission,
    const std::string& reason);

void setRuntimeMissionIntent(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeMissionIntent intent,
    const std::string& reason = "intent"
);

std::string runtimeMissionStateNameForObject(
    const WorldObjectInstance& object);

bool runtimeMissionIsLandingToDeploy(const WorldObjectInstance& object);

bool runtimeMissionIsMcvDeployPending(const WorldObjectInstance& object);

bool runtimeMissionIsDeploying(const WorldObjectInstance& object);

bool runtimeMissionIsUndeploying(const WorldObjectInstance& object);

bool runtimeMissionIsTakingOffAfterUndeploy(
    const WorldObjectInstance& object);

bool runtimeMissionAllowsDeployedWeapon(const WorldObjectInstance& object);

bool runtimeObjectIsDeployedMission(const WorldObjectInstance& object);

bool runtimeObjectHasDeployedState(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectCanUseDeployedWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::string runtimeWeaponSlotName(RuntimeWeaponSlot slot);

std::string runtimeDeployedWeaponId(const UnitType& unit);

RuntimeWeaponSlot runtimeDeployedWeaponSlot(const UnitType& unit);

RuntimeDeployProfile runtimeDeployProfileForUnit(const UnitType& unit);

RuntimeDeployProfile runtimeDeployProfileForBuilding(
    const BuildingType& building);

RuntimeDeployProfile runtimeDeployProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeEngineerProfile runtimeEngineerProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeOccupyProfile runtimeOccupyProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeTransportProfile runtimeTransportProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

const BuildingType* runtimeInfantryAbsorberType(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

int runtimeTransportPassengerSizeLimit(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& transport);

int runtimeTransportPassengerSpace(
    const RuntimeGameSessionState& state, const WorldObjectInstance& transport,
    const WorldObjectInstance& passenger);

bool runtimeTransportPassengerTypeAllowed(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport);

int runtimeTransportUsedSpace(const RuntimeGameSessionState& state, const WorldObjectInstance& transport);

bool runtimeAmphibiousTransport(const RuntimeGameSessionState& state, const WorldObjectInstance& transport);

bool runtimeTransportContainsCargo(
    const RuntimeGameSessionState& state, const WorldObjectInstance& root, int candidateId);

void syncRuntimeTransportCargoPositions(RuntimeGameSessionState& state, const WorldObjectInstance& root);

bool runtimeObjectHasTurretCapability(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

int runtimeArtSequenceFrameCount(
    const ArtType* art,
    const std::string& sequenceName);

int runtimeDeployTransitionTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const UnitType& unit,
    const std::string& mission);

bool runtimeUnitUsesFlyAuxSounds(const UnitType& type);

void queueRuntimeAircraftAuxSound(const RuntimeGameSessionState& state,
    WorldObjectInstance& object, bool takingOff, int relativeHeightLeptons = 0);

void clearRuntimeDeployRenderPosition(WorldObjectInstance& object);

bool runtimeObjectIsEnslavedSlaveWorker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectIsAttachedSlaveWorker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void setRuntimeDeployMission(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const UnitType& unit,
    const std::string& mission);

int runtimeDeployRuleFramesToTicks(
    const RuntimeGameSessionState& state,
    int authoredFrames);

bool runtimeSpawnerHasInfiniteRange(const WeaponType& weapon);

bool runtimeWeaponHasAttackRange(const WeaponType& weapon);

void addRuntimeWeaponCandidate(
    std::vector<RuntimeWeaponCandidate>& candidates,
    RuntimeWeaponSlot slot,
    const std::string& weaponId);

bool runtimeObjectIsOccupyingMission(const WorldObjectInstance& object);

bool runtimeObjectIsOpenTransportWeaponContext(
    const WorldObjectInstance& object);

std::optional<RuntimeWeaponCandidate> runtimeOpenTransportWeaponCandidate(
    const UnitType& unit);

const char* runtimeCapabilityKindKey(WorldObjectKind kind);

std::string runtimeCapabilityCacheKeyForObject(
    const WorldObjectInstance& object);

RuntimeUiProfile runtimeBuildUiProfileForCachedObjectType(
    const RuntimeGameSessionState& state,
    WorldObjectKind kind,
    const std::string& typeId,
    const UnitType* unit,
    const BuildingType* building);

RuntimeCachedCapabilityProfile runtimeBuildCachedCapabilityProfile(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void runtimeResetLastTickCapabilityStats(RuntimeGameSessionState& state);

const RuntimeCachedCapabilityProfile& runtimeCachedCapabilityProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

const std::vector<int>& runtimeGattlingStageThresholds(
    const RuntimeCachedCapabilityProfile& cached,
    bool elite);

int runtimeGattlingTimerCap(
    const RuntimeCachedCapabilityProfile& cached,
    bool elite);

RuntimeTargetDomain runtimeTargetDomainForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target);

bool runtimeWeaponSlotIsPrimary(RuntimeWeaponSlot slot);

bool runtimeWeaponSlotIsSecondary(RuntimeWeaponSlot slot);

bool runtimeWeaponSlotCanTargetDomain(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    RuntimeWeaponSlot slot,
    const WorldObjectInstance& target,
    bool forceFire = false
);

void finalizeRuntimeWeaponProfile(
    RuntimeWeaponProfile& profile,
    const RuntimeGameSessionState& state,
    RuntimeWeaponSlot slot,
    const WeaponType& weapon,
    const std::string& reason);

const RuntimeAirstrikePlaneBinding* runtimeAirstrikeBindingForObject(
    const RuntimeGameSessionState& state,
    int objectId);

bool runtimeObjectManagedByAirstrike(
    const RuntimeGameSessionState& state,
    int objectId);

const RuntimeAirstrikeRootAuthority* runtimeAirstrikeRootAuthorityForPair(
    const RuntimeGameSessionState& state,
    int strikeId,
    int planeToken);

const RuntimeAirstrikePlaneState* runtimeAirstrikePlaneStateForToken(
    const RuntimeAirstrikeSystemState& airstrikes,
    int planeToken,
    const RuntimeAirstrikeStrikeState** strikeOutput = nullptr
);

bool runtimeWeaponDisguiseRequirementSatisfied(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const WeaponType& weapon);

RuntimeWeaponProfile runtimeWeaponProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const WorldObjectInstance* target,
    bool forceFire = false
);

RuntimeWeaponProfile runtimeWeaponProfileForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeWeaponProfile runtimeGarrisonWeaponProfileForOccupant(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& occupant,
    const WorldObjectInstance* target,
    bool forceFire = false
);

RuntimePsychicObjectKind runtimePsychicKindForObject(
    const WorldObjectInstance& object);

RuntimePsychicObjectKind runtimePsychicKindForLaunchSource(
    int sourceKind,
    bool authoredAircraft);

RuntimePsychicObjectSnapshot runtimePsychicObjectSnapshot(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimePsychicRelation runtimePsychicRelationForObjects(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor,
    const WorldObjectInstance& candidate);

bool runtimePsychicCanAttackTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor,
    const WorldObjectInstance& candidate,
    bool hostTargetLegal,
    bool ordinaryRelationLegal);

bool runtimeGarrisonHostCanAttackTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    const WorldObjectInstance& target,
    bool forceFire = false
);

bool runtimeObjectIsImmobileDeployed(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeInfantryRequiresManualUndeploy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectDisabledByParasite(
    const RuntimeGameSessionState& state,
    int objectId);

bool runtimeObjectCanMove(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    bool ignoreDeployedState = false
);

std::optional<std::string> runtimeVoiceEventFromField(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object,
    const std::string& voiceKind,
    const std::string& voiceField);

std::string runtimeDamageFeedbackVoiceField(const UnitType& unit);

bool runtimePoweredUnitIsOperational(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::optional<std::string> runtimeVoiceEventForSelectedUnits(
    const RuntimeProductionHostState& state,
    const std::string& voiceKind,
    const WorldObjectInstance* targetObject = nullptr,
    bool forceFire = false
);

std::optional<std::string> runtimeSecondaryActionVoiceEvent(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object,
    const UnitType& unit);

bool runtimeDiskMoveSoundIsLoop(const RuntimeProductionHostState& state, const UnitType& unit);

std::optional<std::string> runtimeMoveSoundForSelectedUnits(
    const RuntimeProductionHostState& state);

double runtimeMovementBaseIntervalTicks(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

int runtimeMovementIntervalTicks(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

std::int64_t runtimeMovementSegmentIntervalQ16(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from, const RuntimeCellPoint& to);

bool runtimeObjectUsesVehicleArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double prepareRuntimeVehicleArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to);

bool runtimeFactoryLandVehicleEligible(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeFactoryLandDeliveryOwnsEgress(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeFactoryClearanceBehaviorIsValid(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior);

bool runtimeFactoryFastClearanceSource(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int hostId);

bool runtimeFactoryFastClearanceMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior);

bool runtimeFactoryEgressBypassesArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior);

bool runtimeFactoryEgressUsesFastClock(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior);

bool runtimeObjectUsesVehicleStartup(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double runtimeVehicleStartupAccelerationFactor(const UnitType& type);

bool runtimeVehicleStartupCannotMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double runtimeVehicleStartupAccelerationDistance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double prepareRuntimeVehicleStartup(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to, double arrivalScale);

int consumeRuntimeMovementSegmentIntervalTicks(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to);

PathMovementMode runtimeObjectMovementMode(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double runtimeLandTypePercentForMover(
    const LandType& landType,
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover);

int runtimeLandTypeCost(int baseCost, double percent);

const ArtType* runtimeArtTypeForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

const ArtType::SequenceFrame* runtimeSequenceFrameByName(
    const ArtType& art,
    const std::string& name);

bool runtimeIdleSequenceLooksSafe(
    const ArtType& art,
    const std::string& name);

std::string runtimeIdleVariantMissionForObjectReference(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::optional<RuntimeIdleVariantPhase> runtimeSafeIdleVariantPhase(
    int tick, int fixedTickHz, int instanceId);

std::string runtimeIdleVariantMissionForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    bool forceReference = false,
    RuntimeIdleVariantPass* pass = nullptr
);

bool runtimeUnitTypeLooksNaval(const UnitType& unit);

bool runtimeBuildItemLooksNaval(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

bool runtimeTextContains(const std::string& value, const std::string& needle);

bool runtimeOverlayLooksBridge(const TerrainCell& terrain);

bool runtimeOverlayIsBrokenLowBridgePlaceholder(
    const TerrainCell& terrain);

bool runtimeOverlayLooksHighBridge(const TerrainCell& terrain);

int runtimeEffectiveTerrainLevel(const TerrainCell& terrain);

bool runtimeHighBridgeFrameIsDisconnected(const TerrainCell& terrain);

bool runtimeOverlayProvidesBridgeDeck(const TerrainCell& terrain);

bool runtimeTileSetMatches(
    const TheaterDatabase& theater,
    int tileSetIndex,
    const std::string& key);

RuntimeTerrainInfo runtimeTerrainInfoForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

RuntimeTerrainInfo runtimeCachedTerrainInfoForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

inline bool runtimeComponentMetadataBorrowReference();

inline bool runtimeComponentTerrainQueryReference();

inline bool runtimeComponentColdLookupReference();

inline bool runtimeComponentIndexedTopologyReference() noexcept;

RuntimeMapRenderContext runtimeTerrainMetadataContext(
    const RuntimeGameSessionState& state);

bool runtimeCellHasDisconnectedHighBridgeDeck(
    const RuntimeGameSessionState& state, const TerrainCell& terrain);

RuntimeNavigationTerrainInfo runtimeOwnedNavigationTerrainInfo(const RuntimeTerrainInfo& source);

RuntimeTerrainInfo runtimePrivateNavigationTerrainInfo(const RuntimeNavigationTerrainInfo& source);

RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeProductionHostState& state,
    const TerrainCell& terrain,
    const RuntimeMapRenderContext& metadataContext);

RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeProductionHostState& state,
    const TerrainCell& terrain);

void rebuildRuntimeTerrainInfoCache(RuntimeProductionHostState& state);

const char* runtimeTerrainLandTypeRulesId(
    RuntimeTerrainLandType landType);

bool runtimeObjectUsesTerrainMovementSpeed(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

double runtimeMovementLandPercent(const LandType& land, const UnitType& type);

double runtimeMovementCellFloorLevel(const TerrainCell& cell,
    const RuntimeTerrainInfo& info);

double runtimeTerrainMovementSpeedMultiplier(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to);

bool runtimeAuthoredLandOrdinalAllowsMoverUncached(
    const RuntimeGameSessionState& state,
    RuntimeTerrainLandType authoredLandType,
    const WorldObjectInstance& mover);

bool runtimeAuthoredLandTypeAllowsMoverUncached(
    const RuntimeGameSessionState& state,
    const RuntimeTerrainInfo& terrainInfo,
    const WorldObjectInstance& mover);

bool runtimeAuthoredLandTypeAllowsMover(const RuntimeGameSessionState& state,
    const RuntimeTerrainInfo& terrain,const WorldObjectInstance& mover);

bool runtimeVehicleCannotSurviveOpenWaterAtCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeCellPoint cell);

bool runtimeTheaterGeneralValueMatches(
    const RuntimeGameSessionState& state,
    int value,
    std::initializer_list<const char*> keys);

std::optional<RuntimeBridgeMaterial> runtimeHighBridgeMaterialForTerrain(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

RuntimeHighBridgeHeadType runtimeHighBridgeHeadTypeForTerrain(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

bool runtimeHighBridgeHeadIsBoundary(RuntimeHighBridgeHeadType head);

RuntimeHighBridgeHeadType runtimeOppositeHighBridgeHead(
    RuntimeHighBridgeHeadType head);

RuntimeCellPoint runtimeHighBridgeDirection(RuntimeHighBridgeHeadType head);

RuntimeBridgeMaterial runtimeHighBridgeMaterialForOverlay(
    const TerrainCell& terrain);

bool runtimeHighBridgeOverlayMatchesAxis(
    const TerrainCell& terrain,
    bool alongX);

std::vector<RuntimeCellPoint> runtimeSameTileCluster(
    RuntimeProductionHostState& state,
    const TerrainCell& seed);

RuntimeCellPoint runtimeCanonicalHighBridgeBoundary(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& cluster,
    const RuntimeCellPoint& direction,
    const RuntimeCellPoint& fallback,
    int expectedLevel);

bool runtimeTerrainIsHighBridgeMiddle(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

std::optional<RuntimeBridgeRepairSpan>
runtimeBuildHighBridgeRepairSpanFromBoundary(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& hut,
    const TerrainCell& boundarySeed);

bool runtimeOverlayLooksVisibleLowBridge(const TerrainCell& terrain);

std::optional<RuntimeBridgeMaterial> runtimeLowBridgeMaterialForOverlay(
    const TerrainCell& terrain);

int runtimeLowBridgeBaseIndex(RuntimeBridgeMaterial material);

int runtimeLowBridgeOffset(const TerrainCell& terrain);

bool runtimeLowBridgeOverlayIsAlongX(const TerrainCell& terrain);

bool runtimeLowBridgeOverlayIsHead(const TerrainCell& terrain);

bool runtimeLowBridgeOverlayIsStartHead(const TerrainCell& terrain);

bool runtimeLowBridgeOverlayMatches(
    const TerrainCell& terrain,
    RuntimeBridgeMaterial material,
    bool alongX,
    bool allowPlaceholder = true
);

std::string runtimeLowBridgeOverlayName(
    RuntimeBridgeMaterial material,
    int offset);

std::optional<RuntimeBridgeRepairSpan>
runtimeBuildLowBridgeRepairSpanFromHead(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& hut,
    const TerrainCell& headTerrain);

bool runtimeBridgeRepairSpanNeedsRepair(
    const RuntimeGameSessionState& state,
    const RuntimeBridgeRepairSpan& span);

bool runtimeBridgeRepairSpanCanBeRepaired(
    const RuntimeGameSessionState& state,
    const RuntimeBridgeRepairSpan& span);

void initializeRuntimeBridgeRepairSpans(RuntimeProductionHostState& state);

void runtimeClearBridgeOverlay(TerrainCell& terrain);

bool runtimeBridgeRepairHutNeedsRepair(
    const RuntimeGameSessionState& state,
    int hutObjectId);

bool runtimeObjectIsBridgeRepairHut(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeBridgeHutDemolitionAvailable(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeWeaponUsesBridgeHutDemolition(
    const WeaponType* weapon, const WarheadType* warhead);

bool runtimeBridgeHutDemolitionCommandLegal(
    const RuntimeGameSessionState& state, const WorldObjectInstance& target,
    const WeaponType* weapon, const WarheadType* warhead,
    bool sourceHasC4 = false
);

const MapCellFacts* runtimeMapQueryCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point);

const MapStructureAnchorFact* runtimeMapQueryStructureAnchor(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure,
    const MapQuery& query);

bool runtimeCellIsAllowedOccupiedStructureFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    int allowedOccupiedObjectId,
    const WorldObjectInstance* mover = nullptr
);

RuntimeGateConfig runtimeGateConfigForState(
    const RuntimeGameSessionState& state);

bool runtimeGateObjectIsLiveGate(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeGateObjectIsLimbo(const WorldObjectInstance& object);

bool runtimeGateObjectIsGroundMover(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void rebuildRuntimeGateFootprintIndex(RuntimeGameSessionState& state);

std::uint64_t runtimeGateTopologyFingerprint(
    const RuntimeGameSessionState& state);

std::uint64_t runtimeGateStrictOpenFingerprint(
    const RuntimeGameSessionState& state);

bool runtimeGateDoorwayOccupied(
    const RuntimeGameSessionState& state,
    int gateId);

RuntimeGateContext runtimeGateContext(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeGatePassageRequest>* passageRequests = nullptr
);

void ensureRuntimeGatesInitialized(RuntimeGameSessionState& state);

std::unordered_set<int> runtimeTransparentGateObjectIds(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    RuntimeGatePassageMode mode);

RuntimeSubcellContext runtimeGateSubcellContext(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance* mover = nullptr,
    RuntimeGatePassageMode mode = RuntimeGatePassageMode::Commit
);

std::unordered_set<int> runtimeStrictOpenGateIdsAtCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& cell);

bool runtimeComponentEmptyCensusReference();

bool runtimePlannerCensusProvesEmptyCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,RuntimePlannerEmptyCellQuery kind =RuntimePlannerEmptyCellQuery::Probe);

bool runtimeCellHasHardWholeCellBlocker(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    int moverObjectId,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>& ignoredObjectIds,
    bool forceReference = false
);

bool runtimeObjectIsEffectiveCrusher(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeCrusherUsesCrushingMovement(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectOccupiesCrushCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeCellPoint& cell);

bool runtimeWeaponHasLineOfSight(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const WeaponType& weapon);

bool runtimeAllowedObjectOccupiesCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    int allowedOccupiedObjectId);

bool runtimeCellHasUnexpectedGroundOccupant(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    int moverObjectId,
    int allowedOccupiedObjectId);

bool runtimeCrusherCanCrushObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const WorldObjectInstance& target,
    const RuntimeCellPoint& destination,
    bool physicalContact = false
);

std::uint64_t runtimeCrushCandidateCellKey(int x, int y);

RuntimeCrushCandidateLookup buildRuntimeCrushCandidateLookup(
    const RuntimeGameSessionState& state,
    const std::vector<PathTerrainCell>& snapshot,
    const RuntimePathDynamicPreparationBounds* provenBounds = nullptr
);

RuntimeCrushTargetScan runtimeScanCrushTargetsAtCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& cell,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* additionallyIgnoredObjectIds = nullptr,
    const RuntimeCrushCandidateList* candidates = nullptr
);

bool runtimeCrushRepresentativeProvesBlocked(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& cell,
    const std::unordered_set<int>* additionallyIgnoredObjectIds = nullptr
);

bool runtimeCanEnterAndCrushCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& cell,
    const std::unordered_set<int>* additionallyIgnoredObjectIds = nullptr,
    const RuntimeCrushCandidateList* candidates = nullptr,
    bool forceReference = false
);

bool runtimeObjectUsesOmniCrushApproach(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeOmniCrushPendingSegment(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool runtimeOmniCrushOwnsOccupiedEndpoint(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& goal);

bool runtimeOmniCrushBodiesTouch(
    const WorldObjectInstance& crusher, const WorldObjectInstance& target);

RuntimeCrushTargetScan runtimeScanOmniCrushPhysicalContacts(
    const RuntimeGameSessionState& state, const WorldObjectInstance& crusher,
    const RuntimeCellPoint& destination);

bool cancelRuntimeBlockedOmniCrushApproach(
    RuntimeGameSessionState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

RuntimeSubcellLocation runtimeSubcellLocationForCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

bool runtimeInfantryCanUseEmptyCellAvailability(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& destination);

std::vector<int> runtimeInfantrySubcellsForProvenEmptyCell(
    const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& destination);

std::vector<int> runtimeAvailableInfantrySubcells(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& destination,
    RuntimeGatePassageMode gateMode = RuntimeGatePassageMode::Commit,
    bool forceReference = false
);

bool runtimeManualFormationMappedReference() noexcept;

bool runtimeObjectOccupiesAirNavigation(const WorldObjectInstance& object);

std::optional<RuntimeCellPoint> runtimeActiveAirSegment(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeAirCellIsClear(
    const RuntimeGameSessionState& state,
    int moverId,
    const RuntimeCellPoint& cell);

int runtimeAirDiagonalKey(const RuntimeCellPoint& from, const RuntimeCellPoint& to);

bool runtimeAirTransitionIsClear(
    const RuntimeGameSessionState& state,
    int moverId,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to);

RuntimeAirTraffic buildRuntimeAirTraffic(const RuntimeGameSessionState& state, int moverId);

std::vector<RuntimeCellPoint> findRuntimeAirPath(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal);

std::optional<RuntimeCellPoint> findRuntimeAirMoveTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    const std::unordered_set<int>* reserved = nullptr,
    const std::unordered_set<int>* replacingOrders = nullptr,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr
);

bool findRuntimeReachableAirMoveTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& anchor, int radius,
    RuntimeCellPoint& target, std::vector<RuntimeCellPoint>& path);

const std::unordered_map<int, std::vector<std::size_t>>& runtimeSolidFoundationsByCell(
    const RuntimeGameSessionState& state);

bool runtimeFoundationBlocksMovement(
    const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& cell, int allowedOccupiedObjectId,
    bool allowCrushableOccupants, RuntimeGatePassageMode gateMode);

int runtimeInfantryAuthorizedSlotlessEntryHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to);

bool runtimeInfantryBodyUsesAuthorizedSlotlessEntry(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to);

bool runtimeStoppedGroundPoseBlocksRenderMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,double nextBaseX,double nextBaseY);

template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupant(const RuntimeGameSessionState& state, Visitor visit);

bool runtimeOmniCrushPendingSegmentContainsCell(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior, const RuntimeCellPoint& cell);

std::unordered_set<int> runtimeGroundMoveSegmentPlacementCells(
    const RuntimeGameSessionState& state, int ignoredObjectId = -1
);

bool runtimeGroundDepartureBatchReference();

bool runtimeGroundDepartureHoldsCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& cell);

bool runtimeInfantryActiveSegmentCrossesTransition(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell =std::nullopt);

bool runtimeFactoryGroundSegmentAdmissionBlocked(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits =nullptr);

int runtimeInfantryBodyPreviewDestinationSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to);

bool runtimeInfantryBodySegmentAdmissionBlocked(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits =nullptr,int forcedSubCell =-1,std::vector<int>* blockingObjectIds =nullptr,
    std::optional<int> selectedDestinationSubCell =std::nullopt);

bool runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    int targetSubCell,std::vector<int>* blockingObjectIds =nullptr);

bool runtimeGroundDepartureHoldsTransition(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell =std::nullopt);

bool runtimePlannerEmptySubcellBooleanReference() noexcept;

bool runtimePlannerEmptySubcellBooleanResult(const RuntimeSubcellLocation& destination,
    int ignoredMover, const std::map<int, RuntimeSubcellReservation>& reservations);

bool runtimeCellGateScratchReference();

bool runtimeCellGateScratchExactMiss(const RuntimeGameSessionState& state, int legacyKey);

bool runtimeCellIsPassableUncached(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* softOccupiedObjectIds = nullptr,
    bool allowCrushableOccupants = false,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt
);

bool runtimeComponentPassabilityReference();

bool runtimeComponentDenseReference();

bool runtimeComponentQueryLeaseReference();

bool runtimeCellIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& point, const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* softOccupiedObjectIds = nullptr,
    bool allowCrushableOccupants = false,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt
);

bool runtimeComponentCrushAliasReference();

bool runtimePlannerEmptySubcellBooleanObserversAllowed() noexcept;

bool runtimeCellReservedByOtherRepairUnit(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    int moverObjectId);

bool runtimeCellTransitionIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& current,
    const RuntimeCellPoint& next,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* softOccupiedObjectIds = nullptr,
    bool allowCrushableDestination = true,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt,
    const RuntimePlannerTerrainQueries* terrainQueries = nullptr,
    const RuntimePlannerCellPassabilityReadOnlyLease* passabilityLease = nullptr,
    const RuntimePlannerIndexedTopology* indexedTopology = nullptr
);

std::optional<int> runtimeCellTraversalCost(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& current,
    const RuntimeCellPoint& next,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1
);

PathCell toPathCell(const RuntimeCellPoint& point);

RuntimeCellPoint toRuntimeCellPoint(const PathCell& cell);

std::vector<RuntimeCellPoint> toRuntimePath(
    const std::vector<PathCell>& path,
    const RuntimeCellPoint& start);

RuntimePathQueueStatsSnapshot toRuntimePathQueueStatsSnapshot(
    const PathRequestQueueLiveStats& stats);

std::uint64_t runtimeMapQueryApproachContextHash(
    const std::string& purpose,
    const WorldObjectInstance& target,
    const std::vector<RuntimeCellPoint>& candidates);

std::uint64_t runtimeRequesterStableHash(
    const WorldObjectInstance& object);

std::uint64_t runtimeMovementConstraintHash(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::uint64_t runtimePathTargetFootprintHash(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target);

std::uint64_t runtimePathCellListHash(
    const std::string& purpose,
    const std::vector<RuntimeCellPoint>& cells);

void populateRuntimePathContextCommon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& requester,
    PathApproachContext& context,
    int goalSlotCount);

PathApproachContext makeRuntimePathApproachContext(
    const RuntimeGameSessionState& state,
    const std::string& purpose,
    const WorldObjectInstance& requester,
    const WorldObjectInstance& target,
    const std::vector<RuntimeCellPoint>& candidates,
    int goalSlotCount = 0
);

std::vector<RuntimeCellPoint> runtimeAlternateGoalCellsNear(
    const RuntimeCellPoint& goal,
    int radius);

PathApproachContext makeRuntimeMovePathApproachContext(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& requester,
    const RuntimeCellPoint& goal,
    int groupSize,
    bool allowNearbyTarget,
    bool exactTargetRequired = false,
    std::optional<RuntimeCellPoint> sharedGoal = std::nullopt
);

void applyRuntimeMapQueryApproachStats(RuntimeProductionHostState& state);

void recordRuntimeMapQueryApproachContext(
    RuntimeProductionHostState& state,
    const std::string& purpose,
    const WorldObjectInstance& target,
    const std::vector<RuntimeCellPoint>& candidates,
    int submittedCount);

const std::vector<PathTerrainCell>& runtimeStaticPathTerrainSnapshot(
    const RuntimeGameSessionState& state);

double runtimePathThreatAvoidance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover);

std::vector<RuntimePathThreatArea> runtimePathThreatAreas(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, double coefficient);

int runtimePathThreatCost(const std::vector<RuntimePathThreatArea>& threats, const PathCell& cell);

bool runtimePathSnapshotReferenceOccupancy();

bool runtimePathSnapshotHasCellIndex(const RuntimePathTerrainSnapshotCache& cache,
    const std::vector<PathTerrainCell>& snapshot);

void applyRuntimePathFoundationConstraints(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, int allowedObjectId, bool usesCrushingMovement,
    const RuntimePathTerrainQueryScope& query, std::vector<PathTerrainCell>& snapshot);

template<class UnexpectedOccupant>
bool restoreRuntimeAllowedPathFoundation(const RuntimeGameSessionState& state,
    const RuntimePathTerrainQueryScope& query, PathMovementMode movementMode,
    UnexpectedOccupant&& unexpectedOccupant, std::vector<PathTerrainCell>& snapshot);

inline bool runtimePathSnapshotMetadataQueryReference();

const std::vector<PathTerrainCell>& buildRuntimePathTerrainSnapshot(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    bool forceReferenceOccupancy = false
);

RuntimeQueuedPathResult runtimeQueuedPathResultFromPathResult(
    const PathResult& result,
    const RuntimeCellPoint& start);

PathRequestQueue runtimeTimedPathRequestQueue(const std::vector<PathTerrainCell>& terrain,
    int tick, PathRequestQueueTerrainLayoutCache& layout);

RuntimeQueuedPathResult resolveRuntimePathWithQueue(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    const PathApproachContext& context);

std::vector<RuntimeCellPoint> findRuntimePathUntimed(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    RuntimePathQueueStatsSnapshot* pathStatsOutput = nullptr
);

std::vector<RuntimeCellPoint> findRuntimePath(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr
);

std::vector<RuntimeCellPoint> findRuntimeGreedyPathUntimed(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    int maxSteps = 160,
    const std::unordered_map<int, int>* occupiedByCellOverride = nullptr
);

std::vector<RuntimeCellPoint> findRuntimeGreedyPath(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    int maxSteps = 160,
    const std::unordered_map<int, int>* occupiedByCellOverride = nullptr
);

std::vector<RuntimeCellPoint> findRuntimePath(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr
);

double runtimeAttackRangeDistanceFromCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& firingCell,
    const WorldObjectInstance& target,
    bool cellRangefinding);

int runtimeAttackApproachRangeBucket(double rangeCells);

std::uint64_t runtimeAttackApproachCacheKey(
    const WorldObjectInstance& target,
    double rangeCells,
    bool cellRangefinding);

void resetRuntimeAttackApproachCacheForTick(RuntimeGameSessionState& state);

const RuntimeAttackApproachCacheEntry& runtimeAttackApproachEntry(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    double rangeCells,
    bool cellRangefinding = false
);

bool runtimeKirovBuildingBombProfile(
    const WorldObjectInstance& object,
    const WorldObjectInstance& target,
    const RuntimeWeaponProfile& profile);

RuntimeQueuedPathResult resolveRuntimePathTowardRange(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const WorldObjectInstance& target,
    double rangeCells,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    bool forceFire = false
);

std::optional<RuntimeCellPoint> findNearestRuntimePassableCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* reservedCells = nullptr,
    const std::unordered_set<int>* ignoredObjectIds = nullptr
);

std::optional<RuntimeCellPoint> findNearestRuntimePassableCellWithOccupancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells = nullptr,
    const std::unordered_map<int, int>* componentByCell = nullptr,
    int requiredComponent = -1,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr
);

int runtimeMoveTargetSoftPenalty(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& candidate);

std::optional<RuntimeCellPoint> findBestRuntimeFormationTargetCellWithOccupancyImpl(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells,
    const std::unordered_map<int, int>* componentByCell,
    int requiredComponent,
    bool pruneUncompetitiveCandidates,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr,
    const std::unordered_set<int>* certifiedUnmappedKeys = nullptr
);

std::optional<RuntimeCellPoint> findBestRuntimeFormationTargetCellWithOccupancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells = nullptr,
    const std::unordered_map<int, int>* componentByCell = nullptr,
    int requiredComponent = -1,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr,
    const std::unordered_set<int>* certifiedUnmappedKeys = nullptr
);

std::unordered_set<int> buildRuntimeReachableMoveCells(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const std::vector<RuntimeCellPoint>& starts,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    int allowedOccupiedObjectId = -1
);

bool runtimeComponentKernelReference();

std::unordered_map<int, int> buildRuntimeMoveComponentByCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell);

std::optional<RuntimeCellPoint> findRuntimePassableCellNear(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int maxRadiusCells = 6,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* reservedCells = nullptr,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    const std::unordered_map<int, int>* occupiedByCellOverride = nullptr,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt
);

void ensureRuntimeSubcellsInitialized(RuntimeGameSessionState& state);

void invalidateRuntimeSubcellPathState(RuntimeGameSessionState& state);

void invalidateRuntimeSubcellDynamicPathState(RuntimeGameSessionState& state);

bool runtimeSubcellReservationsEqual(
    const std::map<int, RuntimeSubcellReservation>& left,
    const std::map<int, RuntimeSubcellReservation>& right);

bool runtimeInfantryUsesGroundSubcells(const WorldObjectInstance& object);

void assignRuntimeInfantrySubcellAtCurrentLocation(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object);

void releaseRuntimeInfantrySubcellReservation(
    RuntimeGameSessionState& state,
    int objectId);

bool commitRuntimeInfantryMoveDestination(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object,
    const RuntimeCellPoint& destination);

bool runtimeInfantryReservationMatches(
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeCellPoint& destination);

bool runtimeMoveWillCompleteBeforeSubcellStep(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool runtimeAttackWillUseSubcellStep(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

void prepareRuntimeInfantryStepReservations(RuntimeGameSessionState& state);

RuntimeSubcellReconcileReport reconcileRuntimeSubcells(
    RuntimeGameSessionState& state,
    bool relocateInvalidInfantry);

WorldObjectInstance* findRuntimePathQueueObject(
    RuntimeGameSessionState& state,
    int objectId);

std::uint64_t runtimePathFailureKey(
    const RuntimeGameSessionState& state,
    const std::string& purpose,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& goal,
    int targetObjectId,
    double rangeCells);

std::uint64_t runtimePathFailureKeyForPending(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeObjectBehavior& behavior);

int runtimePathFailureCooldownTicks(int failureCount);

bool runtimePathFailureCooldownActive(
    RuntimeGameSessionState& state,
    std::uint64_t key,
    RuntimeObjectBehavior& behavior);

void rememberRuntimePathFailure(
    RuntimeGameSessionState& state,
    std::uint64_t key,
    RuntimeObjectBehavior& behavior);

void clearRuntimePathFailure(
    RuntimeGameSessionState& state,
    std::uint64_t key);

bool sameRuntimeMovePathRequest(
    const RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    bool allowNearbyTarget,
    const std::unordered_set<int>* ignoredObjectIds,
    std::optional<RuntimeCellPoint> sharedGoal = std::nullopt
);

void submitRuntimeMovePathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    bool allowNearbyTarget = false,
    int delayTicks = 0,
    std::optional<RuntimeCellPoint> sharedGoal = std::nullopt
);

void submitRuntimeAttackPathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const WorldObjectInstance& target,
    double rangeCells,
    int delayTicks = 0,
    const std::unordered_set<int>* ignoredObjectIds = nullptr
);

bool applyRuntimeMovePathResult(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeQueuedPathResult& queuedPath);

bool completeRuntimeMovePathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool completeRuntimeAttackPathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool runtimePendingMovePathCanBatch(
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior);

bool runtimeMovePathBatchCompatible(
    const RuntimeGameSessionState& state,
    const RuntimeMovePathBatchWork& first,
    const RuntimeMovePathBatchWork& candidate,
    const WorldObjectInstance& firstObject,
    const WorldObjectInstance& candidateObject);

bool processRuntimeMovePathBatch(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeMovePathBatchWork>& batch);

bool processRuntimePathRequests(RuntimeProductionHostState& state);

void rebuildRuntimeNavigationStats(RuntimeGameSessionState& state);

void refreshRuntimeNavigationOccupancyStats(RuntimeGameSessionState& state);

void refreshRuntimeWallConnectionMasks(RuntimeGameSessionState& state);

bool runtimeMapInitialVeteranSpecialFlag(const IniFile& mapIni);

void initializeRuntimeNavigation(RuntimeProductionHostState& state);

void invalidateRuntimeObjectProjectionCache(RuntimeProductionHostState& state);

bool runtimeSetObjectCloaked(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    bool cloaked,
    const std::string& reason);

bool runtimeSetObjectSubmerged(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    bool submerged,
    const std::string& reason);

bool runtimeDecloakObject(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const std::string& reason);

bool runtimeObjectUsesSubmarineReveal(const RuntimeGameSessionState& state,const WorldObjectInstance& object);

bool runtimeRevealSubmarineForActivity(RuntimeProductionHostState& state,WorldObjectInstance& object,const std::string& reason);

bool runtimeRevealObjectAfterDamage(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const std::string& reason);

void refreshRuntimeObjectOccupancyAfterLocalChange(RuntimeProductionHostState& state);

std::string chooseRuntimeEconomyOwner(const GameWorld& world);

bool runtimeCountryIsYuri(const std::string& country);

bool runtimeCountryIsSoviet(const std::string& country);

std::string runtimeConstructionYardForCountry(const std::string& country);

std::optional<std::string> runtimeStartingUnitModeOverride(
    const RuntimeGameSessionState& state,
    const UnitType& unit,
    const std::string& key);

std::string runtimeStartingUnitEffectiveList(
    const RuntimeGameSessionState& state,
    const UnitType& unit,
    const std::string& key,
    const std::string& baseValue);

bool runtimeUnitAllowedToStartInMultiplayer(
    const RuntimeGameSessionState& state,
    const UnitType& unit);

bool runtimeStartingUnitAllowedForCountry(
    const RuntimeGameSessionState& state,
    const UnitType& unit,
    const std::string& country);

RuntimeSkirmishStartingUnitPools runtimeSkirmishStartingUnitPoolsForCountry(
    const RuntimeGameSessionState& state,
    const std::string& country);

std::string runtimeSkirmishDifficultyForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner);

int runtimeDifficultyAdjustedStartingUnits(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    int requestedCount);

double runtimeDifficultyHealthScale(
    const RuntimeGameSessionState& state,
    const std::string& owner);

std::string runtimeBuildKindName(RuntimeBuildKind kind);

RuntimeBuildKind runtimeBuildKindFromName(const std::string& name);

RuntimeBuildKind runtimeBuildKindFromTechTreeKind(const std::string& kind);

bool runtimeTechTreeNodeAllowedForRuntime(
    const RuntimeGameSessionState& state,
    const TechTreeNode& node);

void addRuntimeBuildCatalogItem(
    RuntimeGameSessionState& state,
    RuntimeBuildKind kind,
    const std::string& id,
    const TechTreeNode* techNode = nullptr
);

const TechTreeNode* runtimeFindTechTreeNode(
    const TechTreeReport& report,
    const std::string& id);

bool runtimeTechTreeNodeCanEnterCatalog(
    const RuntimeGameSessionState& state,
    const TechTreeNode& node);

int runtimeBasePlanningSideForCountry(const std::string& countryId);

bool runtimeBuildItemVisibleForCountry(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& playerCountry);

bool runtimeBuildItemVisibleForPlayerCountry(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

RuntimeBuildKind runtimeBuildKindForBuildingType(
    const BuildingType& building);

void addRuntimeTechTreeBuildCatalog(RuntimeGameSessionState& state);

int addRuntimeSecretLabBuildCatalog(RuntimeGameSessionState& state);

void addRuntimeModeOverrideBuildCatalog(RuntimeGameSessionState& state);

void applyRuntimeModeRulesToBuildItem(
    const RuntimeGameSessionState& state,
    RuntimeBuildCatalogItem& item);

void loadAndApplyRuntimeSkirmishModeRules(RuntimeGameSessionState& state);

bool runtimeIsSuperweaponCatalogItem(const RuntimeBuildCatalogItem& item);

void applyRuntimeSuperweaponCatalogOption(RuntimeGameSessionState& state);

void refreshRuntimeEconomyMetricsForOwner(
    RuntimeGameSessionState& state, RuntimeEconomyState& economy);

void refreshRuntimeEconomyMetricsForCurrentOwner(RuntimeGameSessionState& state);

void rebuildRuntimeEconomyPower(RuntimeProductionHostState& state);

void initializeRuntimeLocalProductionLanes(RuntimeEconomyState& economy);

void initializeRuntimeHumanHouseAuthorities(RuntimeProductionHostState& state);

void initializeRuntimeEconomy(RuntimeProductionHostState& state);

bool initializeRuntimeTriggers(RuntimeProductionHostState& state);

int nextRuntimeObjectInstanceId(const RuntimeGameSessionState& state);

RuntimeStructureFootprint runtimeStructureFootprintForBuildItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

bool runtimeResourceOverlayBlocksPlacement(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

std::unordered_set<int> runtimeBuildingRubblePlacementCells(
    const RuntimeGameSessionState& state);

bool runtimeCellCanPlaceBuildingFast(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::string& placementOwner,
    int ignoredObjectId = -1,
    const std::unordered_set<int>* rubbleCellsOverride = nullptr
);

bool runtimeCellCanPlaceBuilding(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    int ignoredObjectId = -1,
    const std::unordered_map<int, int>* occupiedByCellOverride = nullptr,
    const std::string& placementOwner = {},
    const std::unordered_set<int>* rubbleCellsOverride = nullptr
);

void applyRuntimePlacementRenderAnchor(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object,
    RuntimeBuildKind kind,
    const RuntimeCellPoint& topLeftCell);

void applyRuntimePlacementObjectPose(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object,
    RuntimeBuildKind kind,
    const RuntimeCellPoint& topLeftCell);

int runtimeInitialProducedFacing(
    const RuntimeGameSessionState& state,
    RuntimeBuildKind kind,
    const std::string& typeId);

bool runtimeAircraftPadBuildingHasCompatibleType(
    const RulesDatabase& rules,
    const std::string& buildingTypeId);

std::map<std::string, std::size_t>
runtimeAircraftPadSlotCapacityByOwner(
    const RuntimeGameSessionState& state);

bool runtimeAircraftPadSlotCapacityAllowsStructure(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& buildingTypeId,
    const BuildingType& building);

bool validateRuntimeAircraftPadSlotCapacity(
    const RuntimeGameSessionState& state,
    std::string& errorMessage);

const SmudgeType* runtimeSmudgeTypeForId(
    const RulesDatabase& rules,
    const std::string& typeId);

std::size_t clearRuntimeSmudgesForPlacedStructure(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& structure);

RuntimeBuildupTiming runtimeStructureBuildupTiming(
    RuntimeProductionHostState& state, const WorldObjectInstance& object,
    std::string* resolvedSource = nullptr
);

void beginRuntimeStructureSaleAnimation(RuntimeProductionHostState& state, WorldObjectInstance& object);

WorldObjectInstance* addRuntimeObjectInstanceForOwner(
    RuntimeProductionHostState& state,
    const std::string& owner,
    RuntimeBuildKind kind,
    const std::string& typeId,
    const RuntimeCellPoint& cell,
    std::optional<RuntimeProductionLane> productionLane = std::nullopt,
    bool clearPlacedSmudges = true,
    bool initialTransportCargo = false,
    int initialOccupiedStructureId = -1
);

bool advanceRuntimeBuildAnimations(RuntimeProductionHostState& state);

bool advanceRuntimeInfantryProneStates(RuntimeGameSessionState& state);

bool advanceRuntimeWeaponCooldowns(RuntimeGameSessionState& state);

int runtimeFactoryAirExitCloseTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory);

bool runtimeFactoryUsesAirExit(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory, const WorldObjectInstance& produced);

std::optional<RuntimeCellPoint> runtimeFactoryAirExitSpawnCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory);

const WorldObjectInstance* runtimeFactoryAirExitContainingHost(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced);

bool runtimeFactoryAirExitWaitingForRoof(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced);

bool runtimeFactoryAirExitReadyToMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced);

bool advanceRuntimeFactoryExitAnimations(RuntimeProductionHostState& state);

const UnitType* findRuntimeProductionListedUnitType(
    const std::vector<UnitType>& types,
    const std::string& typeId);

std::optional<RuntimeProductionLane> runtimeProductionLaneForCatalogItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

std::optional<RuntimeProductionCatalogAdapter>
runtimeProductionCatalogLookup(
    const RuntimeGameSessionState& state,
    std::string_view itemKey);

std::optional<RuntimeSelectedPlacement> runtimeSelectedPlacement(
    const RuntimeProductionHostState& state);

void clearRuntimePlacementSelection(RuntimeGameSessionState& state);

void clearRuntimeInvalidPlacementSelection(RuntimeGameSessionState& state);

bool runtimeProductionOwnersMatch(
    const RuntimeGameSessionState& state,
    const std::string& left,
    const std::string& right);

bool runtimeStructureProvidesFactoryForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    const std::string& requiredFactory);

bool runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    RuntimeFactoryDeliveryDomain domain);

std::optional<RuntimeFactoryDeliveryDomain>
runtimeFactoryDeliveryHostDomain(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeProductionFactoryEligibleForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    RuntimeProductionLane lane);

void addRuntimeFactoryAnchorCandidate(
    std::vector<const WorldObjectInstance*>& anchors,
    const WorldObjectInstance& object);

void sortRuntimeFactoryAnchorCandidates(
    std::vector<const WorldObjectInstance*>& anchors);

std::vector<const WorldObjectInstance*>
findRuntimeFactoryAnchorCandidatesForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback);

const WorldObjectInstance* findRuntimeFactoryAnchorForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback);

const TerrainCell* runtimeProductionTerrainCellAt(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

bool runtimeProductionCellIsOpenSurfaceWater(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

std::optional<RuntimeCellPoint> findRuntimeOpenSurfaceWaterCellNear(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int maximumRadius,
    const std::unordered_map<int, int>* occupiedByCell = nullptr,
    const RuntimeCellPoint* excludedCell = nullptr
);

const UnitType* findRuntimePadAircraftUnitType(
    const RulesDatabase& rules,
    const std::string& typeId);

const UnitType* findRuntimePadAircraftUnitType(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

bool runtimeBuildItemUsesPadAircraft(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item);

bool runtimeAircraftPadHostStructurallyEligibleForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    const std::string& owner);

bool runtimeAircraftCanDockAtStructureForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    const std::string& owner);

int runtimeFreeAircraftDockIndex(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    int dockCount);

RuntimeProductionSpawn runtimeAircraftDockSpawn(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    int dockIndex);

bool applyRuntimeAircraftDockPose(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& aircraft,
    const WorldObjectInstance& anchor,
    int dockIndex,
    bool actualArrival = false
);

std::optional<std::array<double, 2>> runtimeRepairDepotApproachPoint(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit, const RuntimeCellPoint& target);

double runtimeRepairDepotApproachDistanceScale(const RuntimeGameSessionState& state,
    const WorldObjectInstance& unit, const RuntimeCellPoint& from, const RuntimeCellPoint& to);

bool beginRuntimeRepairDepotSameCellApproach(RuntimeProductionHostState& state,
    WorldObjectInstance& unit, RuntimeObjectBehavior& behavior);

bool runtimeObjectUsesNavalDockPresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& unit);

std::optional<std::array<double, 3>> runtimeNavalDockCommandPose(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit);

std::optional<RuntimeObjectBehavior> runtimeNavalDockCommandContinuation(
    const RuntimeGameSessionState& state, const WorldObjectInstance& unit);

void restoreRuntimeNavalDockCommandStopPose(WorldObjectInstance& unit,
    const std::optional<std::array<double, 3>>& pose);

bool applyRuntimeRepairDepotDockPose(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& unit,
    const WorldObjectInstance& host,
    int dockIndex);

bool refreshRuntimeRepairDepotDockPoses(RuntimeProductionHostState& state,
    const RuntimeProductionHostState* authoredState = nullptr);

bool runtimeAircraftDockAssignmentCompatible(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& aircraft,
    const WorldObjectInstance& anchor);

RuntimeAircraftProductionMatchInputBuild
buildRuntimeAircraftProductionMatchInputForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::optional<RuntimeAircraftProductionOrder> prospectiveOrder,
    const RuntimeRepairDepotSystemState* repairAuthority,
    bool includeExistingOrders);

RuntimeAircraftProductionMatchInputBuild
buildRuntimeAircraftProductionRecoveryInputForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    bool includeExistingOrders);

RuntimeAircraftProductionMatchResult runtimeAircraftProductionMatchForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::optional<RuntimeAircraftProductionOrder> prospectiveOrder =
        std::nullopt,
    const RuntimeRepairDepotSystemState* repairAuthority = nullptr
);

std::optional<RuntimeAircraftProductionAssignment>
runtimeAircraftProductionAssignmentForSerial(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::uint64_t serial);

bool validateRuntimePadAircraftProductionProjection(
    const RuntimeGameSessionState& state,
    std::string& errorMessage, const std::string* miningOwner = nullptr
);

std::uint64_t runtimeAircraftDockSlotKey(int anchorId, int dockIndex);

void refreshRuntimeAircraftDockAssignments(RuntimeProductionHostState& state);

bool runtimeBuildItemNeedsExitClearance(
    const RuntimeGameSessionState& state, const RuntimeBuildCatalogItem& item);

std::optional<RuntimeCellPoint> runtimeProductionOutwardCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    const RuntimeCellPoint& cell);

bool requestRuntimeFactoryExitClearance(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::vector<RuntimeCellPoint>& corridor,
    int producedObjectId = -1,
    bool airOnly = false,
    int clearanceHostId = -1
);

std::vector<RuntimeCellPoint> runtimeFactoryAirExitCorridor(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory);

void requestRuntimeFactoryAirExitClearance(RuntimeProductionHostState& state,
    const WorldObjectInstance& produced,const WorldObjectInstance& factory);

bool requestRuntimeProductionDoorClearance(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    const std::string& owner);

bool requestRuntimeProductionDoorClearanceForOwner(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback,
    bool tryAllFactories = false
);

std::optional<RuntimeProductionSpawn> findRuntimeProductionSpawnForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback);

std::optional<RuntimeCellPoint> findRuntimeProductionExitMoveTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    const WorldObjectInstance& produced,
    bool forceOpenSurfaceWater = false
);

void queueRuntimeProductionExitMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const WorldObjectInstance& anchor);

void queueRuntimeProductionExitMoveForOwner(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback);

std::optional<RuntimeCellPoint> runtimeSpecialBuildingInfantryExitCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    const std::string& owner,
    const std::string& infantryTypeId);

bool reconcileRuntimeCloneDeliveryLeases(RuntimeGameSessionState& state);

bool runtimeCloneDeliveryOwnsDoorway(
    const RuntimeGameSessionState& state, int objectId);

bool runtimePendingCloneDeliveryEligible(
    const RuntimeGameSessionState& state, const RuntimeSpecialBuildingClone& request);

std::uint64_t runtimeClonePendingDeliveriesHash(const RuntimeGameSessionState& state);

std::vector<int> advanceRuntimePendingCloneDeliveries(
    RuntimeProductionHostState& state, bool leasesAlreadyReconciled = false
);

std::vector<int> cloneRuntimeProducedInfantry(
    RuntimeProductionHostState& state, const std::string& owner,
    const std::string& infantryTypeId, int producingFactoryObjectId = -1
);

bool beginRuntimeFactoryExitAnimation(
    RuntimeGameSessionState& state,
    WorldObjectInstance& factory,
    const WorldObjectInstance& produced);

std::optional<RuntimeCellPoint> findRuntimeSpawnCellNearObjectForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    const std::string& owner);

void queueRuntimeFactoryDeliveryExitMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const RuntimeFactoryDeliveryLease& lease);

bool completeRuntimeProductionOrder(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    bool allowLegacyFactoryFallback = true,
    std::optional<RuntimeProductionLane> completedLane = std::nullopt,
    std::uint64_t completedOrderSerial = 0
);

RuntimeFactoryDeliveryReconcileInput runtimeFactoryDeliveryReconcileInput(
    const RuntimeGameSessionState& state);

bool advanceRuntimeFreeUnitDeliveries(RuntimeProductionHostState& state);

bool advanceRuntimeFactoryDeliveries(RuntimeProductionHostState& state);

bool runtimeFactoryDeliveryOwnsEgress(
    const RuntimeGameSessionState& state,
    int objectId);

bool runtimeGarrisonOwnersAllowEntry(
    const WorldObjectInstance& occupier,
    const WorldObjectInstance& host);

bool runtimeOrdinaryAttackRelationLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    bool forceFire = false
);

std::string runtimeGarrisonEntryVetoReason(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& occupier);

WorldObjectKind runtimeWorldObjectKindFromName(const std::string& name);

std::string runtimeBehaviorKindName(RuntimeBehaviorKind kind);

RuntimeBehaviorKind runtimeBehaviorKindFromName(const std::string& name);

std::string runtimeEngineerActionName(RuntimeEngineerAction action);

RuntimeEngineerAction runtimeEngineerActionFromName(const std::string& name);

std::string runtimeHarvesterPhaseName(RuntimeHarvesterPhase phase);

RuntimeHarvesterPhase runtimeHarvesterPhaseFromName(const std::string& name);

std::optional<RuntimeBuildCatalogItem> runtimeCatalogItemByKindAndType(
    const RuntimeGameSessionState& state,
    RuntimeBuildKind kind,
    const std::string& typeId);

std::optional<RuntimeBuildOrder> parseRuntimeBuildOrder(
    const RuntimeGameSessionState& state,
    const std::string& value);

std::string runtimeHexEncode(std::string_view value);

bool runtimeHexDecode(
    std::string_view encoded,
    std::string& value,
    std::size_t maximumDecodedSize = 16U * 1024U * 1024U
);

std::string serializeRuntimeHarvesterTask(const RuntimeHarvesterTask& task);

std::string runtimeSlaveMinerPhaseName(RuntimeSlaveMinerPhase phase);

RuntimeSlaveMinerPhase runtimeSlaveMinerPhaseFromName(
    const std::string& name);

std::string serializeRuntimeSlaveMinerTask(
    const RuntimeSlaveMinerTask& task);

std::optional<RuntimeSlaveMinerTask> parseRuntimeSlaveMinerTask(
    const std::string& value);

bool validateRuntimeStoredSlaveMinerState(
    const RuntimeGameSessionState& state, std::string* error);

bool parseRuntimeStrictBool(const std::string& value, bool& output);

bool parseRuntimeFiniteDouble(const std::string& value, double& output);

constexpr bool runtimeProductionLanesSectionSchemaSupported(int schema);

constexpr bool runtimeProductionLanesModernSectionSchemaSupported(int schema);

std::string serializeRuntimeProjectileEffect(
    const RuntimeProjectileEffect& projectile);

bool parseRuntimeProjectileEffect(
    const std::string& value,
    RuntimeProjectileEffect& projectile,
    int schema = runtimeProjectileSaveSchemaV6,
    bool allowExpired = false
);

bool parseRuntimeProjectilesFromSave(
    const IniFile& ini,
    RuntimeProjectileSaveCandidate& candidate,
    std::string& errorMessage);

void commitRuntimeProjectilesFromSave(
    RuntimeProductionHostState& state,
    RuntimeProjectileSaveCandidate candidate);

bool runtimeProjectileLaunchRegistrationValid(
    const RulesDatabase& rules,
    int sourceKind,
    const std::string& sourceType,
    bool authoredAircraft);

bool runtimeRestoreExactSavedObjectRegistration(
    const RulesDatabase& rules,
    WorldObjectInstance& object,
    bool namespaceIsExplicit = false
);

bool runtimeProjectileLaunchIdentityForSavedObject(
    const RulesDatabase& rules,
    const WorldObjectInstance& object,
    int& sourceKind,
    std::string& sourceType,
    bool& authoredAircraft);

bool validateAndCompleteRuntimeProjectileLaunchProvenance(
    const RuntimeGameSessionState& liveState,
    RuntimeProjectileSaveCandidate& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeDeathWeaponSystemState& deathWeapons,
    std::string& errorMessage,
    const RuntimeAirstrikeSystemState* airstrikeAuthority = nullptr
);

bool runtimePsychicSourceRegistrationValid(
    const RulesDatabase& rules,
    RuntimePsychicObjectKind kind,
    const std::string& typeId);

bool validateRuntimeSavedPsychicStatuses(
    const RuntimeGameSessionState& liveState,
    RuntimePsychicStatusSystemState& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    std::string& errorMessage);

bool runtimeSavedSectionReferencesActor(
    const IniFile& ini,
    const std::string& section,
    int objectId,
    bool& valid);

bool validateRuntimeSavedMagBeams(
    const RuntimeGameSessionState& liveState,
    const IniFile& ini,
    RuntimeMagBeamSystemState& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    const RuntimeSpecialMechanismState& specialMechanisms,
    const RuntimeRepairDepotSystemState& repairDepots,
    const RuntimeFactoryDeliveryState& factoryDeliveries,
    const RuntimeAircraftServiceSystemState& aircraftServices,
    std::string& errorMessage);

bool restoreRuntimeGattlingTimersFromSave(
    RuntimeGameSessionState& state,
    const IniFile& ini,
    std::string& errorMessage,
    int saveVersion = 0
);

std::string runtimeCrateArtImageId(
    const RulesDatabase& rules,
    const std::string& overlayTypeId);

void writeRuntimePublicBuildingLocations(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimePublicBuildingLocations(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& error);

void hashRuntimePublicBuildingLocations(std::uint64_t& hash, const RuntimeGameSessionState& state);

std::string serializeRuntimeWreck(const RuntimeWreckState& wreck, bool canonicalOwner = false);

void writeRuntimeWrecks(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeWrecks(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

void hashRuntimeWrecks(std::uint64_t& hash, const RuntimeGameSessionState& state);

std::string serializeRuntimeBuildingCollapse(const RuntimeBuildingCollapseState& collapse);

void writeRuntimeBuildingCollapses(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeBuildingCollapses(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

void hashRuntimeBuildingCollapses(std::uint64_t& hash, const RuntimeGameSessionState& state);

bool restoreRuntimeCratesFromSave(
    RuntimeGameSessionState& state,
    const IniFile& ini,
    std::string& errorMessage);

bool restoreRuntimeSmudgesFromSave(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage);

void restoreRuntimeTerrainObjectsFromSave(
    RuntimeProductionHostState& state,
    const IniFile& ini);

bool buildRuntimeOriginalOverlayWalls(const RuntimeGameSessionState& state,
    RuntimeOriginalOverlayWalls& originals, std::string& error);

bool runtimeOverlayWallIdentityMatches(const OverlayWallState& value,
    const OverlayWallState& original);

bool runtimeTerrainShowsOriginalOverlayWall(const TerrainCell& cell,
    const OverlayWallState& wall);

void clearRuntimeOverlayWallSprite(TerrainCell& cell);

bool resetRuntimeOverlayWallsForSavedProjection(RuntimeGameSessionState& state,
    std::string& error);

bool restoreRuntimeOverlayWallStates(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error);

bool writeRuntimeOverlayWallStates(const RuntimeGameSessionState& state,
    std::ostream& output, std::string& error);

bool materializeRuntimeSavedTerrainProjection(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage);

RuntimeSpatialAudioPosition runtimeWorldAudioSourcePosition(const RuntimeGameSessionState& state,
    const RuntimeWorldAudioAction& action);

void rememberRuntimeWorldAudioBackendAction(RuntimeProductionHostState& state, const RuntimeWorldAudioAction& action);

void applyRuntimeWorldAudioBackendAction(RuntimeProductionHostState& state, const RuntimeWorldAudioAction& input);

void applyRuntimeWorldAudioBackendActions(RuntimeProductionHostState& state, const std::vector<RuntimeWorldAudioAction>& actions);

void stopRuntimeWorldAudioBackend(RuntimeProductionHostState& state, bool preserveChronosphereSelection = false);

void reapRuntimeWorldAudioBackendBindings(RuntimeProductionHostState& state);

std::uint64_t runtimeDiskMoveSoundHandle(int objectId, int channel = 0);

bool advanceRuntimeDiskLoopSources(RuntimeProductionHostState& state, RuntimeSuperWeaponBuildingAudioState& audio,
    const std::vector<RuntimeDiskLoopSource>& sources, std::uint64_t nowMilliseconds,
    std::uint64_t (*handle)(int,int));

bool advanceRuntimeDiskMoveAudio(RuntimeProductionHostState& state,std::uint64_t nowMilliseconds);

std::uint64_t runtimeGattlingSoundHandle(int objectId, int channel = 0);

bool advanceRuntimeGattlingWeaponAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds);

bool manageRuntimeGattlingWeaponReport(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const WeaponType& weapon);

std::string runtimeTurretSoundForObject(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeTurretSoundObjectPresent(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

void queueRuntimeTurretSoundActivity(const RuntimeGameSessionState& state, WorldObjectInstance& object,
    const std::string& event, bool rotating);

void markRuntimeTurretSoundRotation(const RuntimeGameSessionState& state, WorldObjectInstance& object);

void finishRuntimeTurretSoundActivities(const RuntimeGameSessionState& state, GameWorld& world);

bool advanceRuntimeWorldAudio(RuntimeProductionHostState& state);

bool advanceRuntimeLighting(RuntimeProductionHostState& state);

bool prepareRuntimeAudiovisualSaveCandidates(
    const RuntimeProductionHostState& state,
    RuntimeWorldAudioSystemState& worldAudio,
    RuntimeLightingSystemState& lighting,
    RuntimeParticleSystemState& particles,
    std::string& errorMessage);

void writeRuntimeVirusDeaths(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeVirusDeaths(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool runtimePersistentWreckWaterAnimation(const RuntimeImpactEffect& impact);

bool runtimePersistentUnitDeath(const RuntimeImpactEffect& impact);

bool runtimeValidWreckWaterAnimation(const RuntimeGameSessionState& state, const RuntimeImpactEffect& impact);

void writeRuntimeUnitDeaths(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeUnitDeaths(RuntimeGameSessionState& state, const IniFile& ini, std::string& error, bool commit = true);

void writeRuntimeSquidSplashes(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeSquidSplashes(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

void writeRuntimeTransportTransferPresentation(const RuntimeGameSessionState& state, std::ostream& output,
    bool multiplayer = false);

bool loadRuntimeTransportTransferProofIntents(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& errorMessage);

bool loadRuntimeTransportTransferPresentation(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& errorMessage, bool multiplayer = false);

bool validateRuntimeTransportTransferPresentation(const RuntimeGameSessionState& state,std::string& errorMessage);

bool runtimeGroundAttackWalkPresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& walk);

void writeRuntimeInfantryWalks(const RuntimeGameSessionState& state, std::ostream& output,
    bool includeMobileUnits = false);

bool loadRuntimeInfantryWalks(RuntimeGameSessionState& state, const IniFile& ini, std::string& error,
    bool includeMobileUnits = false);

void writeRuntimeStoppedMovePoses(const RuntimeGameSessionState& state, std::ostream& output);

bool parseRuntimeStoppedMovePoses(const RuntimeGameSessionState& state, const IniFile& ini,
    std::vector<RuntimeStoppedMovePoseRecord>& pending, std::unordered_set<int>& seen, std::string& error);

void restoreRuntimeStoppedMovePose(WorldObjectInstance& object, const RuntimeStoppedMovePoseRecord& pose);

std::int64_t runtimeLegacyVehicleStartupDistanceQ16(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

bool runtimeOmniCrushAttackMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

bool runtimeGroundVehicleAttackMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

bool runtimeNavalOffsetMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

bool runtimeNavalFactoryMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

void writeRuntimeVehicleArrivals(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeVehicleArrivals(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool runtimeRepackMoveGroupActor(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move);

bool runtimeRepackMoveGroupMember(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& member);

std::optional<RuntimeRepackMoveGroupIntent> canonicalRuntimeRepackMoveGroup(
    const RuntimeGameSessionState& state, int actorId, const RuntimeObjectBehavior& move);

void refreshRuntimeRepackMoveGroup(RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, RuntimeObjectBehavior& move);

void remapRuntimeRepackMoveGroupMember(RuntimeGameSessionState& state, int oldId, int newId);

void writeRuntimeRepackMoveGroups(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeRepackMoveGroups(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error, bool preserveExactBehavior = false);

void writeRuntimeSpriteFirings(const RuntimeGameSessionState& state, std::ostream& output);

bool loadRuntimeSpriteFirings(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool runtimeHasSupportFlightPose(const WorldObjectInstance& object);

void writeRuntimeSupportFlightPoses(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeSupportFlightPoses(const IniFile& ini,
    std::vector<WorldObjectInstance>& objects, std::string& error);

void writeRuntimeComputerMining(std::ostream& output, const RuntimeGameSessionState& state);

void writeRuntimeComputerTactics(std::ostream& output, const RuntimeGameSessionState& state);

bool readRuntimeCooperativeSession(const RuntimeGameSessionState& state, const IniFile& ini,
    RuntimeCooperativeSessionState& result, std::string& error);

template<class Map> void writeRuntimeCooperativeObservationMap(std::ostream& output,
    const char* name, const Map& map);

template<class Map> bool readRuntimeCooperativeObservationMap(const IniFile& ini,
    const char* name, Map& map, std::string& error);

void writeRuntimeCooperativeObservations(std::ostream& output, const RuntimeGameSessionState& state,
    bool replayCampaign = false);

bool readRuntimeCooperativeObservations(const IniFile& ini, RuntimeTriggerState& triggers,
    std::string& error, bool replayCampaign = false);

void writeRuntimeTriggerReinforcements(const RuntimeGameSessionState& state, std::ostream& output);

bool readRuntimeTriggerReinforcements(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

std::string serializeRuntimeActiveJumpjetFlights(const RuntimeGameSessionState& state,
    bool includePresentation = true);

void writeRuntimeJumpjetFlights(const RuntimeGameSessionState& state, std::ostream& output);

bool readRuntimeJumpjetFlights(RuntimeProductionHostState& state, const IniFile& ini, std::string& error);

std::string serializeRuntimeActiveAircraftFlights(const RuntimeGameSessionState& state);

void writeRuntimeAircraftFlights(const RuntimeGameSessionState& state,std::ostream& output);

bool readRuntimeAircraftFlights(RuntimeGameSessionState& state,const IniFile& ini,std::string& error);

void writeRuntimeFireReveals(const RuntimeGameSessionState& state, std::ostream& output);

bool readRuntimeFireReveals(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

void writeRuntimeCarrierFlightPoses(const RuntimeGameSessionState& state, std::ostream& output);

bool validateRuntimeCarrierAircraftCellOrders(const RuntimeGameSessionState& state, std::string& error);

bool parseRuntimeCarrierFlightPoses(const RuntimeGameSessionState& state, const IniFile& ini,
    std::vector<RuntimeCarrierFlightPoseRecord>& poses, std::string& error);

void restoreRuntimeCarrierFlightPoses(RuntimeGameSessionState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses);

void restoreRuntimeCarrierFlightSegments(RuntimeGameSessionState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses);

bool runtimeSlaveMinerSavedMoveObjectIsEligible(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeSlaveMinerSavedMoveCellIsValid(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell);

bool runtimeSlaveMinerSavedMoveRouteIsValid(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool runtimeSlaveMinerSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary);

void writeRuntimeSlaveMinerMovePaths(
    const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeSlaveMinerMovePaths(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool runtimeJumpjetSavedMoveObjectIsEligible(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight);

bool runtimeJumpjetSavedMoveCellIsValid(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell);

bool runtimeJumpjetSavedPathKindIsEligible(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool runtimeJumpjetSavedMoveRouteIsValid(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight, const RuntimeObjectBehavior& behavior);

bool runtimeJumpjetSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary);

void writeRuntimeJumpjetMovePaths(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeJumpjetMovePaths(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool runtimeClonePendingDeliveryTextValid(const std::string& value);

bool runtimeClonePendingDeliveriesCandidateValid(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeSpecialBuildingClone>& candidate,
    std::string& error);

std::string runtimeClonePendingDeliveriesRecord(
    const RuntimeSpecialBuildingClone& request);

bool validateRuntimeClonePendingDeliveries(
    const RuntimeGameSessionState& state, std::string& error);

void writeRuntimeClonePendingDeliveries(
    const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeClonePendingDeliveries(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool writeRuntimeStateToStream(
    const RuntimeProductionHostState& state,
    std::ostream& output,
    std::string& errorMessage);

bool parseRuntimeSavedObjectCoreRecord(
    const std::vector<std::string>& tokens,
    int fallbackObjectId,
    WorldObjectInstance& object);

bool runtimeSavedAuthoredInactivePlaceholder(const RuntimeGameSessionState& context,
    const WorldObjectInstance& object);

bool parseRuntimeCompleteSavedObjectRecord(
    RuntimeGameSessionState& context,
    const std::vector<std::string>& tokens,
    int fallbackObjectId,
    int saveVersion,
    WorldObjectInstance& object,
    std::string& errorMessage,
    int savedTick = -1
);

bool parseRuntimeCompleteSavedObjects(
    RuntimeGameSessionState& context,
    const IniFile& ini,
    int saveVersion,
    std::string& errorMessage,
    std::unordered_set<int>* explicitGattlingCooldownIds = nullptr,
    std::unordered_set<int>* explicitDualWeaponCooldownIds = nullptr
);

bool parseRuntimeSavedCampaignAlliances(
    const IniFile& ini,
    int saveVersion,
    std::set<std::string>& alliances,
    std::string& errorMessage);

bool materializeRuntimeSavedTransportManifests(
    RuntimeGameSessionState& context,
    const IniFile& ini,
    int saveVersion,
    std::string& errorMessage);

bool parseRuntimeSavedBehaviorRecord(
    const std::vector<std::string>& tokens,
    const WorldObjectInstance& object,
    int saveVersion,
    int& objectId,
    RuntimeObjectBehavior& behavior);

bool validateRuntimeSavedBehaviorRecords(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    std::string& errorMessage);

RuntimeGateContext runtimeSavedGateContext(
    const RuntimeGameSessionState& state,
    const std::vector<WorldObjectInstance>& objects);

RuntimeSubcellContext runtimeSavedGateSubcellContext(
    const RuntimeGateSystemState& gates);

bool runtimeMissionAllowsPendingOccupyIntent(
    const WorldObjectInstance& object);

RuntimeGarrisonConfig runtimeSavedGarrisonConfig(
    const RuntimeGameSessionState& state);

bool validateRuntimeSavedRepairDepots(
    const RuntimeProductionHostState& liveState,
    const IniFile& ini,
    int saveVersion,
    const RuntimeGateSystemState& savedGates,
    RuntimeRepairDepotSystemState& validatedState,
    bool& hasExplicitSection,
    std::string& errorMessage);

bool validateRuntimeSavedAircraftServices(
    const RuntimeProductionHostState& liveState,
    const IniFile& ini,
    int saveVersion,
    RuntimeAircraftServiceSystemState& validatedState,
    bool& hasExplicitSection,
    std::string& errorMessage);

bool runtimeObjectHasC4Capability(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

bool runtimeObjectHasIvanCapability(
    const RulesDatabase& rules,
    const WorldObjectInstance& object);

bool runtimeTargetAcceptsTimedBombIntent(
    const RulesDatabase& rules,
    const WorldObjectInstance& target);

bool runtimeBuildingBombOrderRelationLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    bool forceFire);

bool validateRuntimeSavedTimedBombIntents(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    std::vector<std::pair<int, RuntimeTimedBombIntent>>& savedIntents,
    std::string& errorMessage);

bool validateRuntimeSavedTimedBombState(
    const RuntimeGameSessionState& state,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    std::string& errorMessage);

bool validateRuntimeSavedCarrierState(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    std::string& errorMessage);

bool validateRuntimeSavedParasiteState(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    int savedTick,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    const RuntimeFactoryDeliveryState& savedFactoryDeliveries,
    std::unordered_map<int, int>& savedStuns,
    std::string& errorMessage);

bool validateRuntimeSavedOccupyIntents(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& completeObjects,
    std::vector<std::pair<int, int>>& savedIntents,
    bool& hasExplicitSection,
    std::string& errorMessage);

bool parseRuntimeUint64Strict(
    const std::string& value,
    std::uint64_t& parsed);

bool saveRuntimeStateWithWriter(
    const RuntimeGameSessionState& state,
    const std::string& savePath,
    const RuntimeAtomicFileWriter& writer,
    std::string& errorMessage);

bool validateRuntimeSavedFactoryDeliveries(
    const RuntimeProductionHostState& state,
    const IniFile& ini,
    int saveVersion,
    int savedTick,
    const RuntimeSavedProductionCandidate& savedProduction,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    RuntimeFactoryDeliveryState& validatedState,
    std::string& errorMessage,
    const RuntimeSavedProductionByOwner* savedProductionByOwner = nullptr
);

bool runtimeProductionOrderMatchesCatalogSnapshot(
    const RuntimeGameSessionState& state,
    RuntimeProductionLane lane,
    const RuntimeProductionLaneOrder& order,
    const RuntimeProductionCatalogAdapter& catalog);

bool validateRuntimeProductionCatalogState(
    const RuntimeGameSessionState& state,
    const RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage);

bool migrateRuntimeSchema1ProductionTiming(
    const RuntimeGameSessionState& state,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage);

bool loadRuntimeV8ProductionCandidate(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage);

bool parseRuntimeV15ProductionByOwner(
    const IniFile& ini,
    RuntimeSavedProductionByOwner& productionByOwner,
    std::string& errorMessage);

bool migrateRuntimeLegacyProductionCandidate(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage);

bool parseRuntimeSavedAirstrikeHost(
    const IniFile& ini,
    int saveVersion,
    RuntimeSavedAirstrikeHostCandidate& candidate,
    std::string& errorMessage);

bool validateRuntimeSavedAirstrikeHost(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    int savedTick,
    const RuntimeSavedAirstrikeHostCandidate& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    const RuntimeSpecialMechanismState& specialMechanisms,
    const RuntimeMagBeamSystemState& magBeams,
    const RuntimeRepairDepotSystemState& repairDepots,
    const RuntimeFactoryDeliveryState& factoryDeliveries,
    const RuntimeAircraftServiceSystemState& aircraftServices,
    std::string& errorMessage);

void injectRuntimeLoadFaultForSmoke(
    RuntimeLoadFaultPointForSmoke point);

std::string normalizedRuntimeSaveMapIdentity(const std::string& rawPath);

bool validateRuntimeSaveOuterIdentity(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int version,
    std::string& errorMessage);

bool materializeRuntimeLoadStateFromIni(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage,
    bool exactOfflinePoses = false
);

bool materializeRuntimeLoadState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage);

template <typename T>
void swapRuntimeLoadedField(T& live, T& loaded) noexcept;

void swapRuntimeLoadedProductionLanes(
    RuntimeProductionLanesState& live,
    RuntimeProductionLanesState& loaded) noexcept;

void swapRuntimeLoadedEconomy(
    RuntimeEconomyState& live,
    RuntimeEconomyState& loaded) noexcept;

void swapRuntimeLoadedTriggers(
    RuntimeTriggerState& live,
    RuntimeTriggerState& loaded) noexcept;

void swapRuntimeLoadedRenderWorldCaches(
    RuntimeMapRenderCache& live,
    RuntimeMapRenderCache& loaded) noexcept;

void seedRuntimeLoadScratch(
    RuntimeProductionHostState& scratch,
    const RuntimeProductionHostState& live,
    bool forceVisionEnabled);

void commitRuntimeLoadedState(
    RuntimeProductionHostState& live,
    RuntimeProductionHostState& loaded) noexcept;

bool loadRuntimeStateWithVisionPolicy(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage,
    bool forceVisionEnabled);

bool runtimeObjectIsHarvester(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectIsChronoHarvester(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

int runtimeHarvesterCargoCapacity(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester);

int runtimeHarvesterDefaultLoadTicks(const RuntimeGameSessionState& state);

int runtimeHarvesterLoadTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester);

int runtimeHarvesterUnloadTicks(const RuntimeGameSessionState& state);

int runtimeHarvesterThinkCooldownTicks(const RuntimeGameSessionState& state);

int runtimeTiberiumBailValue(
    const RulesDatabase& rules,
    const std::string& tiberiumId,
    int fallbackValue);

const TerrainCell* findRuntimeTerrainCellAt(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

TerrainCell* findMutableRuntimeTerrainCellAt(
    RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

bool runtimeCellHasHarvestableResource(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

std::string runtimeResourceTwoDigitSuffix(int value);

int runtimeResourceMaxBailsForFamily(const std::string& family);

std::string runtimeResourceFamilyForTerrain(const TerrainCell& terrain);

int runtimeTiberiumImageForResourceFamily(const std::string& family);

std::string runtimeTiberiumFallbackIdForImage(int image);

int runtimeTiberiumFallbackValueForImage(int image);

const TiberiumType* runtimeFindTiberiumTypeByImage(
    const RulesDatabase& rules,
    int image);

const TiberiumType* runtimeTiberiumTypeForImage(
    const RulesDatabase& rules,
    int image);

const TiberiumType* runtimeTiberiumTypeForResourceFamily(
    const RulesDatabase& rules,
    const std::string& family);

int runtimeResourceValueForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

int runtimeTrailingResourceNumber(const std::string& value);

int runtimeResourceVariantFromOverlayIndex(const TerrainCell& terrain);

std::string runtimeResourceOverlayDiagnosticName(const TerrainCell& terrain);

int runtimeResourceBailCountFromTerrainIdentity(
    const TerrainCell& terrain);

int runtimeResourceMaxBailsForTerrain(const TerrainCell& terrain);

int runtimeResourceVariantForTerrain(const TerrainCell& terrain);

int runtimeGeneratedResourceVariant(
    const TerrainCell& terrain,
    const std::string& family);

void setRuntimeResourceTerrainVariant(
    TerrainCell& terrain,
    const std::string& family,
    int variant);

void setRuntimeResourceTerrainBails(
    TerrainCell& terrain,
    const std::string& family,
    int bails);

int runtimeResourceDensityForTerrain(const TerrainCell& terrain);

int runtimeResourceBailCountForTerrain(const TerrainCell& terrain);

void invalidateRuntimeResourceCaches(
    RuntimeProductionHostState& state,
    bool navigationChanged = true,
    bool radarAppearanceChanged = true
);

int runtimeResourceDensityAtCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

int runtimeHarvesterDepartureRefineryId(
    const RuntimeGameSessionState& state, const WorldObjectInstance& harvester,
    const std::string* owner = nullptr
);

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    RuntimeHarvesterResourceAccess& access,
    const RuntimeCellPoint& resource);

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    const RuntimeProductionHostState& state, const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resource,
    const std::string* owner = nullptr
);

std::optional<RuntimeCellPoint> findHarvestableRuntimeResourceNear(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& preferred,
    const std::string* owner = nullptr
);

std::optional<RuntimeCellPoint> findNearestRuntimeOreCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    std::optional<RuntimeCellPoint> scanOrigin = std::nullopt,
    bool farScan = true,
    const std::string* owner = nullptr
);

int consumeRuntimeOreAtCell(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    bool* wasGem = nullptr
);

int removeRuntimeOreBailsAtCell(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    int bailCount);

double runtimeWarheadPercentAtMaxFraction(const WarheadType& warhead);

int runtimeOreBailsForWarheadImpact(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain,
    int damage,
    const WarheadType& warhead,
    double distance,
    const std::string& impactAnimId);

RuntimeOreDamageResult applyRuntimeTiberiumWarheadOreDamage(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& impactCell,
    int damage,
    double cellSpread,
    const std::string& impactAnimId,
    const WarheadType* warhead);

std::string runtimeTwoDigitSuffix(int value);

std::string runtimeOreImageForFamily(const std::string& family, int variant);

RuntimeOreOverlayIdentity runtimeOreIdentityForVariant(
    const std::string& family,
    int variant,
    int bails);

RuntimeOreOverlayIdentity runtimeOreIdentityForCell(
    const RuntimeCellPoint& cell,
    const std::string& family = "TIB",
    int bails = 1
);

std::optional<RuntimeOreOverlayIdentity> runtimeOreIdentityFromSpawnType(
    const std::string& spawnType);

std::optional<RuntimeOreOverlayIdentity> runtimeOreIdentityFromTerrain(
    const TerrainCell& terrain);

const ArtType* runtimeArtTypeForTerrainObject(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

int runtimeTerrainObjectTiberiumSpreadRadius(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

int runtimeTerrainObjectOreGrowthRadius(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

std::optional<RuntimeOreOverlayIdentity> runtimeOreIdentityFromTerrainObjectArt(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

RuntimeOreOverlayIdentity runtimeOreIdentityNearCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

RuntimeOreOverlayIdentity runtimeOreIdentityForTerrainObject(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

const TiberiumType* runtimeTiberiumTypeForOreIdentity(
    const RulesDatabase& rules,
    const RuntimeOreOverlayIdentity& identity);

double runtimeOreRuleProbability(
    const RuntimeGameSessionState& state,
    const TiberiumType& tiberium,
    bool spread);

int runtimeOreRuleIntervalTicks(
    const TiberiumType& tiberium,
    bool spread);

bool runtimeOreRuleDue(
    const RuntimeGameSessionState& state,
    const TiberiumType& tiberium,
    bool spread);

bool runtimeAnyOreFieldRuleDue(const RuntimeGameSessionState& state);

bool runtimeOreRuleChancePass(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    const RuntimeOreOverlayIdentity& identity,
    const TiberiumType& tiberium,
    int tick,
    bool spread);

bool runtimeTerrainObjectSpawnsOre(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

int runtimeTerrainOreSpawnCooldownTicks(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

double runtimeTerrainOreSpawnProbability(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

bool runtimeTerrainOreSpawnEventDue(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object,
    int tick);

std::unordered_set<int> buildRuntimeTerrainObjectCellLookup(
    const RuntimeGameSessionState& state);

bool runtimeCellCanSpawnNewOre(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>& terrainObjectCells);

bool runtimeCellCanSpawnNewOreIgnoringTerrainObjects(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>& occupiedByCell);

bool runtimeCellCanIncreaseOreDensity(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

bool growRuntimeOreAtCell(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    const RuntimeOreOverlayIdentity& identity,
    int newOreBails = -1
);

bool runtimeOreTwinkleCellSelected(
    const TerrainCell& terrain,
    int chance);

int spawnRuntimeOreTwinkleEffects(RuntimeGameSessionState& state);

bool growRuntimeOreAroundTree(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& origin,
    const RuntimeOreOverlayIdentity& identity,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>& terrainObjectCells,
    int maxRadius);

std::optional<RuntimeCellPoint> findRuntimeOreSpreadTarget(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& origin,
    const RuntimeOreOverlayIdentity& identity,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>& terrainObjectCells,
    int tick);

RuntimeOreFieldGrowthResult advanceRuntimeOreFieldGrowth(
    RuntimeProductionHostState& state,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>& terrainObjectCells);

bool initializeRuntimeInitialOreFields(RuntimeGameSessionState& state);

bool runtimeTerrainObjectOreSpawnAllowedByMode(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object);

bool advanceRuntimeOreGrowth(RuntimeProductionHostState& state);

bool issueRuntimeHarvesterMoveToCell(
    RuntimeProductionHostState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& targetCell,
    const std::string& mission,
    std::optional<RuntimeCellPoint>* actualTarget = nullptr,
    int allowedOccupiedObjectId = -1,
    bool exactTarget = false,
    const std::string* owner = nullptr
);

const WorldObjectInstance* findRuntimeHarvesterRefineryById(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    int objectId,
    const std::string* owner = nullptr
);

std::optional<int> findNearestRuntimeRefineryId(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const std::string* owner = nullptr
);

bool runtimeHarvesterDockCellReservedByOther(
    const RuntimeGameSessionState& state,
    int harvesterId,
    int refineryId,
    const RuntimeCellPoint& candidate);

bool runtimeHarvesterQueueCellReservedByOther(
    const RuntimeGameSessionState& state,
    int harvesterId,
    int refineryId,
    const RuntimeCellPoint& candidate);

RuntimeCellPoint runtimeRefineryDefaultDockCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery);

bool runtimeHarvesterCanReachCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int allowedOccupiedObjectId);

bool runtimeChronoHarvesterCanTeleportToCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int allowedOccupiedObjectId);

double runtimeHarvesterTooFarDistanceForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester);

bool teleportRuntimeHarvesterToCell(
    RuntimeProductionHostState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int* teleportCounter = nullptr
);

std::optional<RuntimeCellPoint> findRuntimeRefineryQueueCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery);

std::optional<RuntimeCellPoint> findRuntimeRefineryDockCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery);

void normalizeRuntimeHarvesterTask(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    RuntimeHarvesterTask& task);

bool runtimeHarvesterIsAtDock(
    const WorldObjectInstance& harvester,
    const RuntimeHarvesterTask& task);

int runtimeHarvesterRefineryDockFacing(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const RuntimeCellPoint& dockCell);

bool alignRuntimeHarvesterToResource(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& resourceCell);

std::array<double, 2> runtimeWorldForwardForFacing(int facing);

void spawnRuntimeHarvesterGatherEffect(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resourceCell);

void spawnRuntimeRefinerySmokeEffects(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const WorldObjectInstance& harvester,
    RuntimeEconomyState* economyFeedback = nullptr
);

long long runtimeSquaredDistanceToCells(
    const RuntimeCellPoint& cell,
    const std::vector<RuntimeCellPoint>& cells);

std::optional<RuntimeCellPoint> findNearestRuntimeStructureResourceCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& structure,
    int maxRadius,
    const WorldObjectInstance& worker, const std::string* owner = nullptr
);

int runtimeSlaveMinerGatherIntervalTicks(
    const RuntimeGameSessionState& state,
    const BuildingType& building);

int runtimeSlaveMinerGatherBatchSize(const BuildingType& building);

int runtimeSlaveMinerScanRadius(const RuntimeGameSessionState& state);

void runtimeTransferCrateModifiers(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    WorldObjectInstance& target);

bool runtimeUnitIsMobileSlaveMiner(const UnitType& unit);

bool runtimeStructureCanRepack(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeObjectIsMobileSlaveMiner(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerDeployCellNearOre(
    const RuntimeProductionHostState& state, const WorldObjectInstance& miner,
    const RuntimeCellPoint& ore,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr, const std::string* owner = nullptr
);

void transferRuntimeSlaveMinerWorkers(
    RuntimeProductionHostState& state, int sourceId, const WorldObjectInstance& target);

std::optional<RuntimeCellPoint> findNearestRuntimeMobileSlaveMinerOreCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& miner,
    int maxRadius,
    std::optional<RuntimeCellPoint>* deployCell = nullptr, const std::string* owner = nullptr
);

bool advanceRuntimeMobileSlaveMinerEconomy(RuntimeProductionHostState& state,
    HouseEconomyContext& context, RuntimeEconomyState& feedback);

bool advanceRuntimeMobileSlaveMinerEconomy(RuntimeProductionHostState& state);

std::string runtimeSlaveMinerWorkerTypeId(const BuildingType& building);

void synchronizeRuntimeStoredSlaveMinerWorker(
    RuntimeProductionHostState& state, WorldObjectInstance& worker,
    const WorldObjectInstance& refinery);

const BuildingType* runtimeSlaveMinerParentBuilding(
    const RuntimeGameSessionState& state, const WorldObjectInstance& refinery);

bool runtimeSlaveMinerWorkerIsStored(
    const RuntimeGameSessionState& state, const WorldObjectInstance& worker);

void synchronizeRuntimeStoredSlaveMinerWorkers(RuntimeProductionHostState& state);

int runtimeSlaveMinerWorkerCapacity(const UnitType& worker);

int runtimeSlaveMinerWorkerHarvestTicks(
    const RuntimeGameSessionState& state,
    const UnitType& worker,
    const BuildingType& building);

int runtimeSlaveMinerWorkerRegenTicks(
    const RuntimeGameSessionState& state, const BuildingType& building);

bool runtimeObjectIsSlaveMinerWorker(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& workerTypeId, const std::string* owner = nullptr
);

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerSpawnCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId);

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerExitCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId, const std::string* owner = nullptr
);

bool ensureRuntimeSlaveMinerWorkers(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& refinery,
    const BuildingType& building, const std::string* owner = nullptr
);

int releaseRuntimeSlaveMinerWorkersForRefinery(
    RuntimeProductionHostState& state,
    int refineryId,
    const std::string& reason,
    const RuntimeObjectExitRequest* request = nullptr, RuntimeEconomyState* miningEconomy = nullptr
);

void cleanRuntimeSlaveMinerTasks(RuntimeProductionHostState& state, RuntimeEconomyState* miningEconomy = nullptr);

bool runtimeSlaveMinerWorkerAtRefinery(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& refinery);

bool issueRuntimeSlaveMinerReturn(
    RuntimeProductionHostState& state,
    WorldObjectInstance& worker,
    const WorldObjectInstance& refinery, const std::string* owner = nullptr
);

bool advanceRuntimeSlaveMinerWorkerTask(
    RuntimeProductionHostState& state,
    WorldObjectInstance& worker,
    RuntimeSlaveMinerTask& task, HouseEconomyContext& context, RuntimeEconomyState& feedback);

bool advanceRuntimeSlaveMinerEconomy(RuntimeProductionHostState& state,
    HouseEconomyContext& context, RuntimeEconomyState& feedback);

bool advanceRuntimeSlaveMinerEconomy(RuntimeProductionHostState& state);

int runtimeHarvesterHousePurifierBonus(const RuntimeGameSessionState& state,
    const HouseEconomyContext& context, const RuntimeEconomyState& metrics,
    int baseValue, int* purifierCountOutput);

bool advanceRuntimeHarvesterEconomy(RuntimeProductionHostState& state, HouseEconomyContext& context, RuntimeEconomyState& feedback);

bool advanceRuntimeHarvesterEconomy(RuntimeProductionHostState& state);

bool advanceRuntimeProductionEconomy(RuntimeProductionHostState& state);

bool advanceRuntimeSpecialBuildings(RuntimeProductionHostState& state);

void runtimeMultiplayerAuthoritativeMixCell(
    std::uint64_t& hash,
    const RuntimeCellPoint& cell);

std::uint64_t runtimeMultiplayerBuildCatalogItemHash(
    const RuntimeBuildCatalogItem& item);

void runtimeMultiplayerAuthoritativeMixTechTree(
    std::uint64_t& hash,
    const TechTreeReport& report,
    bool loaded);

void runtimeMultiplayerAuthoritativeMixEconomy(
    std::uint64_t& hash,
    const RuntimeEconomyState& economy);

void runtimeMultiplayerAuthoritativeMixBehavior(
    std::uint64_t& hash,
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior);

void runtimeMultiplayerAuthoritativeMixImpact(
    std::uint64_t& hash,
    const RuntimeImpactEffect& impact);

void runtimeMultiplayerAuthoritativeMixTriggers(
    std::uint64_t& hash,
    const RuntimeTriggerState& triggers);

bool runtimeMultiplayerAuthoritativeHashBoundaryStable(
    const RuntimeGameSessionState& state,
    std::string& errorMessage);

std::uint64_t runtimeWorldSimulationHash(
    const RuntimeGameSessionState& state,
    std::string& errorMessage,
    bool requireMultiplayer,
    std::vector<std::pair<std::string, std::uint64_t>>* trace = nullptr
);

std::uint64_t runtimeMultiplayerAuthoritativeStateHash(
    const RuntimeGameSessionState& state, std::string& errorMessage,
    std::vector<std::pair<std::string, std::uint64_t>>* trace = nullptr
);

std::uint64_t runtimeStandaloneSimulationStateHash(
    const RuntimeGameSessionState& state, std::string& errorMessage,
    std::vector<std::pair<std::string, std::uint64_t>>* trace = nullptr
);

std::uint64_t runtimeMultiplayerSnapshotCanonicalDoubleBits(double value);

bool runtimeMultiplayerSnapshotDoubleFromBits(
    const std::string& value,
    double& output);

std::uint64_t runtimeMultiplayerSnapshotTechTreeHash(
    const TechTreeReport& report,
    bool loaded);

std::string runtimeMultiplayerSnapshotEconomyScalars(
    const RuntimeEconomyState& economy);

bool parseRuntimeMultiplayerSnapshotEconomyScalars(
    const std::string& value,
    RuntimeEconomyState& economy);

std::string runtimeMultiplayerSnapshotCatalogRecord(
    const RuntimeBuildCatalogItem& item);

bool parseRuntimeMultiplayerSnapshotCatalogRecord(
    const std::string& value,
    RuntimeBuildCatalogItem& item);

bool runtimeMultiplayerSnapshotRebindHumanFacade(
    RuntimeGameSessionState& state,
    const std::string& requestedOwner,
    std::string& errorMessage);

bool writeRuntimeMultiplayerSnapshotHouseSections(
    const RuntimeGameSessionState& state,
    std::ostream& output,
    std::string& errorMessage);

bool materializeRuntimeMultiplayerSnapshotHouses(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage);

std::string runtimeMultiplayerSnapshotCellsText(
    const auto& cells);

bool parseRuntimeMultiplayerSnapshotCellsText(
    const std::string& value,
    std::vector<RuntimeCellPoint>& cells,
    std::size_t maximumCount = 100000U
);

std::string runtimeMultiplayerSnapshotIntsText(
    const auto& values,
    bool canonicalSort = true
);

bool parseRuntimeMultiplayerSnapshotIntsText(
    const std::string& value,
    std::vector<int>& values,
    std::size_t maximumCount = 100000U
);

std::string runtimeMultiplayerSnapshotBehaviorRecord(
    int objectId,
    const RuntimeObjectBehavior& behavior);

bool parseRuntimeMultiplayerSnapshotBehaviorRecord(
    const std::string& value,
    int& objectId,
    RuntimeObjectBehavior& behavior);

std::string runtimeImpactBodyCenterPresentationRecord(
    const RuntimeParticleAirbornePresentation& presentation,
    int spriteOffsetY);

bool parseRuntimeImpactBodyCenterPresentationRecord(
    const std::string& value,
    RuntimeParticleAirbornePresentation& presentation,
    int& spriteOffsetY);

std::string runtimeMultiplayerSnapshotImpactRecord(
    const RuntimeImpactEffect& impact);

bool parseRuntimeMultiplayerSnapshotImpactRecord(
    const std::string& value,
    RuntimeImpactEffect& impact);

void writeRuntimeMultiplayerObjectPoses(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeMultiplayerObjectPoses(RuntimeGameSessionState& state, const IniFile& ini, std::string& error);

bool writeRuntimeMultiplayerSnapshotExtension(
    const RuntimeGameSessionState& state,
    std::ostream& output,
    std::string& errorMessage);

bool materializeRuntimeMultiplayerSnapshotExtension(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage);

bool runtimeMultiplayerSnapshotSectionExcluded(
    const std::string& section);

bool runtimeMultiplayerSnapshotNumericRecordKey(
    const std::string& key,
    int& index);

bool runtimeMultiplayerSnapshotSequenceSection(
    const std::string& section);

bool writeRuntimeMultiplayerSnapshotCanonicalIni(
    const IniFile& ini,
    std::ostream& output,
    std::string& errorMessage,
    bool omitRuntimeSave = false
);

const char* runtimeMultiplayerSnapshotKindName(
    RuntimeMultiplayerSnapshotKind kind);

bool exportRuntimeMultiplayerSnapshotV15(
    const RuntimeProductionHostState& live,
    const RuntimeMultiplayerSnapshotIdentity& identity,
    RuntimeMultiplayerSnapshotMemoryImage& image,
    std::string& errorMessage);

bool validateRuntimeMultiplayerSnapshotEnvelope(
    const RuntimeGameSessionState& live,
    const IniFile& ini,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage);

std::string runtimeMultiplayerSnapshotAuthorityDifferenceSummary(
    const RuntimeProductionHostState& expected,
    const RuntimeProductionHostState& restored);

void recordRuntimeCooperativeSnapshotDifference(const RuntimeProductionHostState& live,
    const RuntimeProductionHostState& restored);

bool stageRuntimeMultiplayerSnapshot(
    const RuntimeProductionHostState& live,
    std::span<const std::uint8_t> bytes,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    RuntimeMultiplayerSnapshotStagedState& staged,
    std::string& errorMessage);

void commitRuntimeMultiplayerSnapshotAuthority(
    RuntimeProductionHostState& live,
    RuntimeMultiplayerSnapshotStagedState&& staged) noexcept;

void commitRuntimeComputerMiningWork(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeEconomyState& work, int startingCredits);

bool advanceRuntimeComputerMining(RuntimeProductionHostState& state);

bool advanceRuntimeEconomyForCurrentHumanHouse(
    RuntimeProductionHostState& state,
    bool refreshSecretLabCatalog);

bool advanceRuntimeEconomy(RuntimeProductionHostState& state);

RuntimeCameraWorldBounds runtimeCameraWorldBounds(
    const RuntimeProductionHostState& state);

RuntimeCameraWorldBounds runtimeFullTerrainCameraWorldBounds(
    const RuntimeProductionHostState& state);

bool runtimeCameraBoundsPassReference();

RuntimeCombinedCameraBounds runtimeCombinedCameraWorldBounds(
    const RuntimeProductionHostState& state,
    const RuntimeRadarProjection* completedProjection = nullptr
);

RuntimeCombinedCameraBounds runtimeMemoizedCameraWorldBounds(const RuntimeProductionHostState& state);

RuntimeCameraRevealMargins runtimeCameraRevealMargins(
    const RuntimeProductionHostState& state,
    const RuntimeCameraWorldBounds& playableBounds,
    const RuntimeCameraWorldBounds* completedFullBounds = nullptr
);

int clampRuntimeCameraAxis(
    int offset,
    int worldMinimum,
    int worldMaximum,
    int viewportExtent);

bool clampRuntimeMapCameraToPixels(
    RuntimeProductionHostState& state,
    const RuntimeInputPixelRect& viewport);

bool selectionContains(const RuntimeProductionHostState& state, int objectId);

void removeSelectedObject(RuntimeProductionHostState& state, int objectId);

bool runtimeObjectAcceptsPlayerSelection(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

void addSelectedObject(RuntimeProductionHostState& state, int objectId);

const WorldObjectInstance* findObjectById(
    const RuntimeGameSessionState& state,
    int objectId);

WorldObjectInstance* findMutableObjectById(
    RuntimeGameSessionState& state,
    int objectId);

int runtimeVeterancyPromotionThreshold(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool awardRuntimeVeterancyExperience(
    RuntimeProductionHostState& state,
    int attackerObjectId,
    const std::string& attackerOwner,
    const RuntimeVeterancyKillCredit& credit);

RuntimeInputPixelPoint approximateCellFromScreenPixelPoint(
    const RuntimeProductionHostState& state,
    const RuntimeInputPixelRect& viewport,
    RuntimeInputPixelPoint point);

int runtimeMaxHealthForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void applySelection(
    RuntimeProductionHostState& state,
    const std::vector<int>& objectIds,
    bool append,
    bool toggle);

RuntimeCellPoint formationOffsetForIndex(int index, int spacing = 1);

int terrainLevelAtRuntimeCell(
    const RuntimeGameSessionState& state,
    int x,
    int y);

bool runtimeCellIsBridge(
    const RuntimeGameSessionState& state,
    int x,
    int y);

void setRuntimeObjectCell(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object,
    const RuntimeCellPoint& point,
    bool resetRenderPosition = true
);

void refreshRuntimeBridgeMutationState(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeCellPoint>& changedCells);

void clearRuntimeRenderPosition(WorldObjectInstance& object);

void releaseRuntimeAircraftDock(WorldObjectInstance& object);

void releaseRuntimeAircraftSupportAuthority(WorldObjectInstance& object);

int runtimeAircraftCruiseLevel(const RuntimeGameSessionState& state);

void departRuntimeAircraftDock(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object);

int runtimeFacingFromRenderedDelta(double dx, double dy);

int runtimeMoveSegmentFacing(const RuntimeObjectBehavior& move);

void alignRuntimeFacingToRenderMove(WorldObjectInstance& object,
    const RuntimeObjectBehavior& move);

int runtimeMoveTargetFacing(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to);

void beginRuntimeRenderMove(
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to,
    double fromLevel,
    bool fromOnBridge,
    int cooldownTicks,
    RuntimeProductionHostState* context = nullptr
);

bool runtimeInfantryWaterContextReference();

bool runtimeInfantryWaterBatchContextReference();

void runtimeObserveInfantryWaterContextConstruction();

RuntimeInfantryWaterPose runtimeInfantryWaterPose(
    RuntimeProductionHostState& state, const WorldObjectInstance& object);

const RuntimeMapRenderContext* runtimeBorrowInfantryWaterMetadataContext(RuntimeProductionHostState& state);

void emitRuntimeInfantryWaterTransition(
    RuntimeProductionHostState& state, const WorldObjectInstance& object,
    const RuntimeInfantryWaterPose& before,
    RuntimeInfantryWaterPoseQuery* query = nullptr
);

bool runtimeGroundRenderStepWaitsForStoppedBody(RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior);

bool advanceRuntimeRenderMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

const UnitType* runtimeJumpjetProfile(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

RuntimeJumpjetFlight& runtimeJumpjetFlightFor(RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeJumpjetUsesKirovClearance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::int64_t runtimeKirovClearanceTargetQ16(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, double directionX, double directionY);

void initializeRuntimeKirovClearanceMotion(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight);

void advanceRuntimeKirovClearanceMotion(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight, const UnitType& unit);

void applyRuntimeJumpjetPose(const RuntimeGameSessionState& state, WorldObjectInstance& object);

bool advanceRuntimeJumpjetHeight(RuntimeProductionHostState& state, WorldObjectInstance& object, bool landing);

bool runtimeJumpjetShouldLand(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const UnitType& unit, const RuntimeJumpjetFlight& flight);

void advanceRuntimeJumpjetFlight(RuntimeProductionHostState& state, WorldObjectInstance& object);

bool advanceRuntimeJumpjetFlights(RuntimeProductionHostState& state);

void bindRuntimeJumpjetMoveGoal(RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& requested, const RuntimeCellPoint& assigned, int minimumRadius = 8);

int beginRuntimeJumpjetSegment(RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to);

void stopRuntimeUnstartedJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object);

void stopRuntimeJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object);

void resumeRuntimeJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, bool resumeStopped = true);

bool advanceRuntimeJumpjetSegment(RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior);

bool runtimeJumpjetCanEnter(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& to);

int runtimeJumpjetTurnStep(const RuntimeGameSessionState& state, const UnitType& unit);

int runtimeFacingFromDelta(int dx, int dy);

int normalizeRuntimeFacing(int facing);

int runtimeSignedFacingDelta(int from, int to);

int runtimeFacingAbsDifference(int left, int right);

int runtimeTurnFacingToward(int current, int desired, int step);

bool runtimeObjectHasTurret(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

std::chrono::steady_clock::time_point runtimeSmokeNow();

double runtimeSmokeElapsedMs(
    std::chrono::steady_clock::time_point start);

int runtimeTurretTurnStep(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

int runtimeBodyTurnStep(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

int runtimeAdvanceBodyFacing(const RuntimeGameSessionState& state, WorldObjectInstance& object,
    int current, int desired);

bool updateRuntimeMoveFacingToward(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& object,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to);

bool updateRuntimeWeaponFacingToward(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const WorldObjectInstance& target,
    bool forceFire = false
);

bool updateRuntimeWeaponFacingTowardBody(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object);

bool advanceRuntimeIdleWeaponFacings(RuntimeProductionHostState& state);

bool runtimeWarheadCanDamageWallTarget(
    const RuntimeGameSessionState& state,
    const WarheadType* warhead,
    const WorldObjectInstance& target);

bool runtimeObjectIsPoisonImmune(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target);

int runtimeAuthoredDamageAgainst(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    long long signedBaseDamage,
    const std::string& warheadId,
    double distanceFromImpact = 0.0,
    double outgoingDamageMultiplier = 1.0,
    bool attachedIvanBuildingBomb = false
);

int runtimeWeaponDamageAgainst(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    double distanceFromImpact = 0.0,
    double outgoingDamageMultiplier = 1.0
);

bool runtimeObjectIsAirTarget(
    const RuntimeGameSessionState&,
    const WorldObjectInstance& object);

bool runtimeObjectIsTiberiumProof(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeProjectileCanHitTarget(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target);

bool runtimeWeaponCanTargetObject(
    const RuntimeGameSessionState& state,
    const WeaponType& weapon,
    const WorldObjectInstance& target);

bool runtimeWeaponCanTargetObjectFromSource(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WeaponType& weapon,
    const WorldObjectInstance& target);

bool runtimeWarheadCanAffectTarget(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target);

int runtimeWeaponCooldownTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker, const WeaponType& weapon);

int runtimeWeaponCooldownTicks(const WeaponType& weapon, int fixedTickHz = 30);

int runtimeProjectileTravelTicks(const RulesDatabase& rules, const WeaponType& weapon,
    double distanceCells, int fixedTickHz = 30);

int nextRuntimeProjectileActorId(RuntimeGameSessionState& state);

double runtimeProjectileProgress(const RuntimeProjectileEffect& projectile);

void updateRuntimeProjectileTravelPosition(
    RuntimeProjectileEffect& projectile);

void rememberRuntimeExpiredProjectile(
    RuntimeGameSessionState& state,
    RuntimeProjectileEffect projectile);

std::string runtimeProjectileReplayRenderIntentId(
    const RuntimeProjectileEffect& projectile,
    const std::string& kind);

void recordRuntimeProjectileReplayEvent(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const std::string& kind);

bool runtimeProjectileIsNuclearDeathDetonation(const RuntimeProjectileEffect& projectile);

bool runtimeProjectileIsCrashDeathDetonation(const RuntimeProjectileEffect& projectile);

std::optional<std::array<double, 3>> runtimeTransportMuzzleAnchor(
    const RuntimeGameSessionState& state, int shooterId);

RuntimeImpactRenderActor makeRuntimeImpactRenderActor(
    const RuntimeImpactEffect& impact);

bool runtimeChronosphereSelectionVisibleToLocalOwner(
    const RuntimeGameSessionState& state, const RuntimeImpactEffect& effect);

std::optional<RuntimeImpactRenderActor> runtimeLocalChronosphereRenderActor(
    const RuntimeProductionHostState& state);

bool advanceRuntimeLocalChronospherePresentation(RuntimeProductionHostState& state);

std::uint64_t runtimeChronosphereSelectionSoundHandle(bool decay = false);

bool runtimeChronosphereSelectionAudioOwnerVisible(const RuntimeGameSessionState& state,
    const RuntimeChronosphereSelectionAudioState& audio);

bool runtimeChronosphereSelectionAudioBindingValid(const RuntimeProductionHostState& state);

void hardStopRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state);

std::uint64_t playRuntimeChronosphereSelectionSample(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec, const std::string& sample, bool decay);

std::string nextRuntimeChronosphereSelectionLoopSample(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec);

void beginRuntimeChronosphereSelectionSequence(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec, std::uint64_t nowMilliseconds, bool firstSelection);

void stopRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state, bool decay = true);

void startRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state,
    const RuntimeImpactEffect& marker, std::uint64_t nowMilliseconds);

bool advanceRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds);

const std::vector<RuntimeImpactRenderActor>* runtimeImpactRenderActorsForRender(
    const RuntimeProductionHostState& state);

std::optional<RuntimeWeaponFireFlh> runtimeFallbackWeaponFireFlh(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile);

std::optional<RuntimeWeaponFireFlh> runtimeWeaponFireFlh(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile,
    const ArtType* art);

int runtimeWeaponFiringFacing(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target);

void triggerRuntimeWeaponVisuals(
    RuntimeGameSessionState& state,
    WorldObjectInstance& attacker);

double runtimeWeaponHeightInLevels(int heightLeptons);

std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const RuntimeWeaponProfile& profile);

RuntimeProjectileSourcePosition runtimeProjectileSourcePosition(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    const RuntimeWeaponProfile& profile,
    int firingFacing);

RuntimeProjectileSourcePosition runtimeProjectileSourcePosition(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    RuntimeWeaponSlot slot,
    int firingFacing);

WorldObjectInstance runtimeWeaponFiringPose(const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile);

void alignRuntimeWeaponMuzzleAtCamera(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source, const RuntimeMapCamera& camera);

void alignRuntimeWeaponMuzzleToSprite(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source);

const SpawnedMissileProfile* runtimeSpawnedMissileProfileForWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WeaponType& weapon);

std::string runtimeWeaponAnimForFacing(
    const WeaponType& weapon,
    int firingFacing);

double runtimeEffectiveWeaponRange(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon);

double runtimeWeaponDistanceToTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon);

int runtimeAuthoredFramesToFixedTicks(
    const RuntimeGameSessionState& state,
    int frames);

int runtimeWeaponPrefireDelayTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WeaponType& weapon);

RuntimeParticleSpawnResult spawnRuntimeArtAnimationParticles(
    RuntimeGameSessionState& state,
    const std::string& animationId,
    const RuntimeParticleSpawnContext& context);

void spawnRuntimeWeaponMuzzleEffect(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    bool occupied = false,
    bool openTopped = false
);

void spawnRuntimeWeaponAttachedParticleSystem(
    RuntimeGameSessionState& state,
    const WeaponType& weapon,
    const RuntimeProjectileEffect& projectile);

long long runtimeCombatProjectileAuthoredDamage(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile, const WeaponType& weapon);

void prepareRuntimeCombatProjectile(
    RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& attacker, const WorldObjectInstance& target,
    const WeaponType& weapon, const ProjectileType* projectileType);

bool advanceRuntimeRadialProjectilePosition(const RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile);

RuntimeSonicWaveGeometry runtimeSonicWaveGeometry(const RuntimeProjectileEffect& wave);

int runtimeSonicWaveInitialDirection(const RuntimeProjectileEffect& wave);

std::vector<int> runtimeSonicWaveDamageCells(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& wave,double tail);

void spawnRuntimeSonicWave(RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& shot, const WeaponType& weapon);

void notifyRuntimeWeaponFired(RuntimeProductionHostState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon);

bool detonateRuntimeNuclearDeathWeapon(
    RuntimeProductionHostState& state,
    RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& source);

void spawnRuntimeProjectile(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    const RuntimeWeaponProfile& weaponProfile,
    int damage,
    int parentActorId = -1,
    int generation = 0,
    bool shrapnel = false,
    int sourceAnchorObjectId = -1,
    double inheritedVeteranDamageMultiplier = -1.0,
    std::uint64_t deathWeaponCascadeId = 0,
    std::uint64_t deathWeaponRequestId = 0,
    std::uint32_t deathWeaponCascadeDepth = 0,
    bool allowSpawnedMissileProfile = true,
    const RuntimeProjectileEffect* inheritedCausalProjectile = nullptr,
    int reservedActorId = -1,
    int airstrikeStrikeId = 0,
    int airstrikePlaneToken = 0,
    bool forceFire = false
);

bool runtimeCombatCanAct(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeCombatValidPassenger(const WorldObjectInstance& passenger,
    const WorldObjectInstance& host);

bool runtimeCombatSourceAvailable(const RulesDatabase& rules,
    const std::vector<WorldObjectInstance>& objects, const WorldObjectInstance& source);

void cancelRuntimeCombatFire(RuntimeProductionHostState& state, int objectId);

WorldObjectInstance runtimeCombatFiringPose(const WorldObjectInstance& shooter,
    const WorldObjectInstance* host);

RuntimeWeaponProfile runtimeCombatPassengerWeaponProfile(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance* target, bool forceFire = false);

bool runtimeCombatTargetInRange(const RuntimeGameSessionState& state,
    const WorldObjectInstance& pose, const WorldObjectInstance& target,
    const WeaponType& weapon, double rangeBonus, bool forceFire);

bool runtimeCombatOpportunityCandidatesReference();

const WorldObjectInstance* selectRuntimeCombatOpportunityTarget(
    const RuntimeGameSessionState& state, const WorldObjectInstance& pose,
    const WeaponType* fixedWeapon, double rangeBonus, bool distributed,
    int preferredTargetId = -1, bool forceFire = false, bool passengerFire = false,
    bool forceReference = false,
    RuntimeCombatOpportunityOwnerSummary* ownerSummary = nullptr,
    RuntimeCombatOpportunityCandidates* candidateIndex = nullptr
);

int fireRuntimeCombatRound(RuntimeProductionHostState& state, const WorldObjectInstance& pose,
    const WorldObjectInstance& target, const WeaponType& weapon,
    const RuntimeWeaponProfile& profile, double multiplier, int sourceAnchorId,
    bool forceFire);

int runtimeNextBurstDelayTicks(const RuntimeGameSessionState& state, const WorldObjectInstance& attacker,
    const WeaponType& weapon, int shotIndex);

int beginRuntimeCombatBurst(RuntimeProductionHostState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon,
    const RuntimeWeaponProfile& profile, double multiplier, int sourceAnchorId = -1,
    bool forceFire = false, bool independentFire = false);

bool advanceRuntimeCombatBursts(RuntimeProductionHostState& state);

bool fireRuntimeIndependentCombatWeapon(RuntimeProductionHostState& state,
    WorldObjectInstance& shooter, const WorldObjectInstance* host,
    const WorldObjectInstance& target, const RuntimeWeaponProfile& profile, bool forceFire = false,
    RuntimeCombatOpportunityCandidates* candidateIndex = nullptr);

bool advanceRuntimeCombatIndependentFire(RuntimeProductionHostState& state);

bool runtimePanicEligible(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool beginRuntimePanic(RuntimeGameSessionState& state, WorldObjectInstance& object,
    bool permanent, int threatX = -1, int threatY = -1);

RuntimePanicClockStep advanceRuntimePanicClock(
    RuntimePanicState& panic, int tick, bool suspended);

bool advanceRuntimePanics(RuntimeProductionHostState& state);

void finishRuntimePanicMovementLeg(RuntimeProductionHostState& state, int id);

void syncRuntimeProjectileTargetEndpoint(
    RuntimeProductionHostState& state,
    RuntimeProjectileEffect& projectile);

std::optional<RuntimeProjectileTerrainCollision>
findRuntimeProjectileTerrainCollision(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    double previousX,
    double previousY,
    double previousZ);

void removeRuntimeSelection(RuntimeProductionHostState& state, int objectId);

bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous);

bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool recoverRuntimeCancelledStructureEntry(
    RuntimeProductionHostState& state, WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous);

bool eraseRuntimeBehavior(RuntimeProductionHostState& state, int objectId);

void cancelRuntimeTimedBombIntentForSource(
    RuntimeProductionHostState& state,
    int sourceObjectId,
    const std::string& reason);

void cancelRuntimeTimedBombIntentsForTarget(
    RuntimeProductionHostState& state,
    int targetObjectId,
    int exceptSourceObjectId,
    const std::string& reason);

bool runtimeOwnerTransitionObjectIsRetained(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void seedRuntimeOwnerTransitionScratch(
    RuntimeProductionHostState& scratch,
    const RuntimeProductionHostState& live);

bool applyRuntimeOwnerTransitionRepairActions(
    RuntimeProductionHostState& scratch,
    const std::vector<RuntimeRepairDepotAction>& actions,
    std::set<int>& materializedActorIds,
    std::string& error);

bool evacuateRuntimeOwnerTransitionGarrisons(
    RuntimeProductionHostState& scratch,
    std::string& error);

bool evacuateRuntimeOwnerTransitionTransports(
    RuntimeProductionHostState& scratch,
    std::string& error);

void commitRuntimeOwnerTransitionScratch(
    RuntimeProductionHostState& live,
    RuntimeProductionHostState& scratch) noexcept;

std::vector<RuntimeSpecialBuildingCredit>
collectRuntimeSpecialBuildingOwnerTransitionCredits(
    const RuntimeGameSessionState& state,
    const std::map<int, std::string>& initialOwners,
    const std::map<int, std::string>& finalOwners);

RuntimeOwnerTransitionBatchResult applyRuntimeOwnerTransitionBatch(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeOwnerTransitionRoot>& roots,
    RuntimeOwnerTransitionRepairPolicy repairPolicy,
    const std::string& reason,
    const RuntimeMindControlOwnerPreparation* mindControlPreparation = nullptr,
    const std::vector<RuntimeOwnerTransitionObjectPreparation>*
        objectPreparations = nullptr,
    bool synchronizeCarrierParents = false,
    const RuntimeOwnerTransitionGarrisonPreparation* garrisonPreparation = nullptr
);

std::optional<RuntimeCellPoint> runtimeCrushScatterDestination(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    const WorldObjectInstance& crusher);

bool destroyRuntimeObjectByCrush(
    RuntimeProductionHostState& state,
    int targetObjectId,
    int crusherObjectId,
    const std::string& crusherOwner,
    RuntimeVeterancyKillCredit& credit);

bool runtimeTickOccupancyProvesEmptyCrushDestination(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& destination);

RuntimeCrushStepResult resolveRuntimeCrushAtDestination(
    RuntimeProductionHostState& state,
    int crusherObjectId,
    const RuntimeCellPoint& destination,
    int allowedOccupiedObjectId = -1,
    bool physicalContactPhase = false
);

bool advanceRuntimeOmniCrushContacts(RuntimeProductionHostState& state);

std::optional<RuntimeBuildCatalogItem> runtimeBuildItemForBuilding(
    const RuntimeGameSessionState& state,
    const std::string& buildingId, const TechTreeReport* technology = nullptr
);

std::optional<RuntimeCellPoint> findRuntimeMcvDeployCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mcv,
    const RuntimeBuildCatalogItem& buildingItem);

int runtimeMcvResolvedDeployFacing(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const UnitType& unit);

bool requestRuntimeMcvDeployment(RuntimeProductionHostState& state, int objectId,
    bool selectResult = true, std::optional<RuntimeCellPoint> requestedCell = std::nullopt);

bool advanceRuntimeMcvDeployTurns(RuntimeProductionHostState& state);

bool deployRuntimeMcv(
    RuntimeProductionHostState& state,
    int objectId,
    bool selectResult = true,
    std::optional<RuntimeCellPoint> requestedCell = std::nullopt, RuntimeEconomyState* miningEconomy = nullptr
);

bool runtimeSkirmishAllowsStructureUndeploy(
    const RuntimeGameSessionState& state,
    const BuildingType& building);

bool undeployRuntimeStructure(
    RuntimeProductionHostState& state, int objectId, int* resultObjectId = nullptr,
    bool selectResult = true
);

bool advanceRuntimeStructureRepacks(RuntimeProductionHostState& state);

std::vector<RuntimeCellPoint> runtimeSkirmishStartCandidates(
    const RuntimeGameSessionState& state);

bool runtimeComputerPlacementPreservesExits(const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item, RuntimeCellPoint cell, const std::string& owner);

std::optional<RuntimeCellPoint> findRuntimePlacementCellNear(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& origin,
    int maxRadius,
    const std::string& placementOwner = {},
    bool preserveProductionExits = false
);

std::uint32_t runtimeSkirmishStartSeed(
    const RuntimeGameSessionState& state,
    std::size_t slotIndex);

std::vector<int> runtimeDeterministicStartCandidateOrder(
    const RuntimeGameSessionState& state,
    std::size_t slotIndex,
    int candidateCount);

std::vector<int> resolveRuntimeSkirmishStartAssignments(
    const RuntimeGameSessionState& state,
    int candidateCount,
    const std::vector<std::vector<bool>>& legalBySlot);

std::vector<std::vector<bool>> runtimeSkirmishStartCandidateLegality(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& startCandidates);

std::optional<std::size_t> runtimeLocalSkirmishSlotIndex(
    const RuntimeConfig& config);

std::vector<const UnitType*> runtimeUnholyAllianceAdditionalMcvTypes(
    const RuntimeGameSessionState& state,
    const std::string& country);

int spawnRuntimeUnholyAllianceMcvs(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeCellPoint& origin,
    const std::vector<const UnitType*>& candidates,
    const RuntimeSkirmishStartingConnectivity& startingConnectivity);

int spawnRuntimeSkirmishStartingUnits(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::string& country,
    const RuntimeCellPoint& origin,
    int requestedCount);

WorldObjectInstance* spawnRuntimeSkirmishBase(RuntimeProductionHostState& state,
    const std::string& owner, const std::string& yardId, const RuntimeCellPoint& cell);

int spawnRuntimeSkirmishOpponents(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeCellPoint>& startCandidates,
    const std::vector<int>& startAssignments);

int runtimeImpactAnimationFrameDelayTicks(const ArtType* art, int fixedTickHz);

int runtimeImpactAnimationFrameForStep(
    const RuntimeImpactEffect& impact,
    int step);

bool runtimeImpactSmudgeMidpointCrossed(
    const RuntimeImpactEffect& impact,
    int elapsedBefore,
    int elapsedAfter,
    int& midpointStep);

void initializeRuntimeImpactAnimationTimeline(
    RuntimeProductionHostState& state,
    RuntimeImpactEffect& impact,
    bool force = false
);

bool runtimeIsVirusDeathAnimation(const RuntimeGameSessionState& state, const RuntimeImpactEffect& impact);

void initializeRuntimeVirusDeath(RuntimeGameSessionState& state, RuntimeImpactEffect& impact);

bool advanceRuntimeVirusDeath(RuntimeProductionHostState& state, RuntimeImpactEffect& impact);

RuntimeAirImpactBodyCenter captureRuntimeAirImpactBodyCenter(
    RuntimeProductionHostState& state, const WorldObjectInstance& target,
    const RuntimeProjectileTargetPosition& hit);

void applyRuntimeAirImpactBodyCenter(
    RuntimeImpactEffect& impact, const RuntimeAirImpactBodyCenter* center);

bool runtimeCrateTypeIs(
    const std::string& type,
    const std::string& expected);

bool runtimeCrateOwnersMatch(
    const std::string& left,
    const std::string& right);

bool runtimeCrateAnimationIsNone(const std::string& animation);

std::uint64_t runtimeCrateSeed(
    const RuntimeGameSessionState& state,
    const RuntimeCrateState* crate,
    const std::string& purpose,
    int salt);

int runtimeCrateTickAfter(const RuntimeGameSessionState& state, int delay);

int runtimeCrateLifetimeTicks(
    const RuntimeGameSessionState& state,
    const RuntimeCrateState& crate,
    int salt);

const CratePowerupRule* runtimeCrateRuleForType(
    const RuntimeGameSessionState& state,
    const std::string& type);

bool runtimeCratePowerupIsSupported(const std::string& type);

std::string runtimeCampaignCratePowerupType(int authoredType);

void runtimeConfigureCratePowerup(
    RuntimeCrateState& crate,
    const CratePowerupRule& powerup);

bool runtimeChooseCratePowerup(
    const RuntimeGameSessionState& state,
    RuntimeCrateState& crate,
    int salt);

void runtimeEnsureCratePowerup(
    const RuntimeGameSessionState& state,
    RuntimeCrateState& crate,
    int salt);

std::vector<RuntimeCratePlacement> runtimeCratePlacementCandidates(
    const RuntimeGameSessionState& state,
    const std::unordered_set<int>& reservedCells);

bool runtimeRelocateCrate(
    RuntimeGameSessionState& state,
    RuntimeCrateState& crate,
    int salt);

std::optional<RuntimeCrateCollector> runtimeCrateCollectorAt(
    const RuntimeGameSessionState& state,
    const RuntimeCrateState& crate);

std::vector<int> runtimeCrateOwnedUnitIdsInRadius(
    const RuntimeGameSessionState& state,
    const RuntimeCrateState& crate,
    const std::string& owner);

int runtimeCrateMoneyAmount(double authoredMinimum, std::uint64_t seed);

bool runtimeGrantCrateMoney(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector,
    const CratePowerupRule& powerup,
    int& grantedAmount);

bool runtimeGrantCrateHealBase(
    RuntimeProductionHostState& state,
    const RuntimeCrateCollector& collector);

double& runtimeCrateUnitBonusValue(
    WorldObjectInstance& object,
    RuntimeCrateUnitBonus bonus);

bool runtimeGrantCrateUnitBonus(
    RuntimeGameSessionState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector,
    const CratePowerupRule& powerup,
    RuntimeCrateUnitBonus bonus);

bool runtimeGrantCrateVeterancy(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector,
    const CratePowerupRule& powerup);

bool runtimeGrantCrateCloak(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector);

bool runtimeApplyCrateObjectOnlyDamage(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    int targetObjectId,
    double authoredDamage,
    const std::string& warheadId,
    int salt);

bool runtimeGrantCrateGas(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const CratePowerupRule& powerup);

bool runtimeGrantCrateTiberium(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate);

bool runtimeOwnerHasSuperWeaponBuilding(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& superWeaponId);

bool runtimeGrantCrateIcbm(
    RuntimeProductionHostState& state,
    const RuntimeCrateCollector& collector);

bool runtimeUnitTypeIsMcv(
    const RuntimeGameSessionState& state,
    const UnitType& unit);

const UnitType* runtimeFreeMcvForCollector(
    const RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector);

bool runtimeModeCrateGoodieForUnit(
    const RuntimeGameSessionState& state,
    const UnitType& unit);

bool runtimeGrantCrateUnit(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector,
    const UnitType* forcedUnit = nullptr
);

RuntimeCrateAward runtimeGrantCratePowerup(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateCollector& collector);

std::string runtimeCrateAwardSound(
    const RuntimeGameSessionState& state,
    const std::string& type);

void emitRuntimeCrateAwardFeedback(
    RuntimeProductionHostState& state,
    const RuntimeCrateState& crate,
    const RuntimeCrateAward& award);

void initializeRuntimeSkirmishCrates(
    RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& startCandidates);

const OverlayType* runtimeCrateOverlayRule(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain);

bool runtimeCrateOverlayMatches(
    const TerrainCell& terrain,
    const OverlayType* overlay,
    const std::string& configuredId);

void runtimeConfigureCampaignCrateReward(
    RuntimeGameSessionState& state,
    RuntimeCrateState& crate,
    const std::string& reward);

int initializeRuntimeCampaignOverlayCrates(RuntimeGameSessionState& state);

bool advanceRuntimeSkirmishCrates(RuntimeProductionHostState& state);

bool runtimeSkirmishObjectCountsForShortGame(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeSkirmishObjectSurvives(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeSkirmishOwnerDefeated(
    const RuntimeGameSessionState& state,
    const std::string& owner);

std::vector<std::string> runtimeMultiplayerOutcomeOwners(
    const RuntimeGameSessionState& state);

void resetRuntimePlayerDefeatNotices(RuntimeProductionHostState& state) noexcept;

void prepareRuntimePlayerDefeatNoticeLoadBaseline(RuntimeProductionHostState& state);

bool expireRuntimePlayerDefeatNotices(RuntimeProductionHostState& state, std::uint64_t nowMs);

bool appendRuntimePlayerDefeatNotice(RuntimeProductionHostState& state,
    const std::string& ownerKey, std::uint64_t nowMs);

bool observeRuntimePlayerDefeatNotices(RuntimeProductionHostState& state,
    std::uint64_t nowMs);

bool observeRuntimePlayerDefeatNotices(RuntimeProductionHostState& state);

void synchronizeRuntimeCooperativeOutcome(RuntimeProductionHostState& state);

bool advanceRuntimeCooperativeOutcome(RuntimeProductionHostState& state);

bool advanceRuntimeMultiplayerOutcome(RuntimeProductionHostState& state);

bool advanceRuntimeSkirmishOutcome(RuntimeProductionHostState& state);

void finishRuntimeCampaignMission(
    RuntimeProductionHostState& state,
    bool victory,
    const std::string& triggerId);

bool runtimeSkirmishModeTeamSetupValid(
    const RuntimeGameSessionState& state,
    std::string& reason);

bool initializeRuntimeSkirmishStart(
    RuntimeProductionHostState& state,
    std::vector<int>* loadingStartAssignments = nullptr,
    const std::vector<int>* plannedStartAssignments = nullptr
);

void spawnRuntimeImpact(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    int level,
    bool death,
    const std::string& animId = "",
    const std::string& warheadId = "",
    int damage = 0,
    double cellSpread = 0.0,
    bool bright = false,
    const std::string& particleId = "",
    const RuntimeProjectileEffect* projectile = nullptr,
    const RuntimeProjectileTargetPosition* precisePosition = nullptr,
    const RuntimeAirImpactBodyCenter* bodyCenter = nullptr,
    bool managedAnimationSound = false
);

std::uint64_t runtimeAnimationSmudgeSeed(
    const RuntimeGameSessionState& state,
    const RuntimeImpactEffect& impact);

bool runtimeImpactCanLeaveSmudge(
    const RuntimeProductionHostState& state,
    const RuntimeImpactEffect& impact,
    const TerrainCell*& terrainOut);

bool applyRuntimeAnimationSmudge(
    RuntimeProductionHostState& state,
    const RuntimeImpactEffect& impact);

int runtimeShrapnelCellRing(int dx, int dy);

int spawnRuntimeProjectileShrapnel(
    RuntimeProductionHostState& state, const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell, int impactLevel);

int spawnRuntimeProjectileAirburst(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell,
    int impactLevel);

int spawnRuntimeSpawnedMissileTerminalWeapon(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell,
    int impactLevel);

bool runtimeObjectBodyOnCollapsedBridge(
    const WorldObjectInstance& object, const std::unordered_set<int>& cells);

int queueRuntimeBridgeCollapseOccupants(
    RuntimeGameSessionState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause);

void destroyRuntimeBridgeCollapseOccupants(
    RuntimeProductionHostState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause);

std::optional<RuntimeBridgeDamageTarget> runtimeHighBridgeDamageTargetForSideLane(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& point);

std::optional<RuntimeBridgeDamageTarget> runtimeBridgeDamageTargetAtCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& requestedCell,
    int searchRadius = 0
);

std::optional<WorldObjectInstance> runtimeGroundAttackTargetObject(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

std::optional<WorldObjectInstance> runtimeBridgeAttackTargetObject(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& requestedCell,
    int searchRadius = 0
);

void runtimeSetLowBridgeOverlayOffset(
    TerrainCell& terrain,
    RuntimeBridgeMaterial material,
    int offset,
    int lane);

int runtimeBridgeSegmentDamageBand(const RuntimeGameSessionState& state, int health);

int runtimeInitialBridgeSegmentHealth(
    const RuntimeGameSessionState& state, const RuntimeBridgeDamageTarget& target);

void updateRuntimeBridgeSegmentDamageFrame(
    RuntimeProductionHostState& state, const RuntimeBridgeDamageTarget& target, int health);

RuntimeBridgeDamageResult damageRuntimeBridgeAtCell(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& requestedCell,
    int rawDamage,
    int searchRadius = 0,
    int sourceObjectId = -1,
    const std::string& sourceOwner = {},
    RuntimeDeathWeaponCause deathWeaponCause = {}
);

RuntimeBridgeDamageResult demolishRuntimeBridgeByHut(
    RuntimeProductionHostState& state, const WorldObjectInstance& hut,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause = {}
);

std::vector<const ArtType*> runtimeBuildingDebrisArtPool(
    const RuntimeGameSessionState& state,
    const BuildingType& building);

RuntimeCellPoint runtimeBuildingDebrisLandingCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& desired,
    const RuntimeCellPoint& fallback);

int spawnRuntimeBuildingDebris(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& destroyed,
    const BuildingType& building,
    const RuntimeProjectileEffect* parentProjectile,
    bool unitSource = false
);

bool beginRuntimeBuildingCollapse(
    RuntimeProductionHostState& state, const WorldObjectInstance& object, const BuildingType& building);

bool advanceRuntimeBuildingCollapses(RuntimeGameSessionState& state);

int spawnRuntimeBuildingDestructionEffects(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& destroyed,
    const BuildingType& building,
    const RuntimeProjectileEffect& projectile,
    const RuntimeProjectileTargetPosition& position,
    int damage);

void setRuntimeInfantryDeathTimeline(
    RuntimeImpactEffect& effect, int start, int count, int delay, int holdTicks);

bool spawnRuntimeInfantryDeathSequence(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const UnitType& unit,
    const RuntimeProjectileEffect& projectile);

std::string runtimeUnitDestructionAnim(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& destroyed,
    const UnitType& unit,
    const RuntimeProjectileEffect& projectile);

void syncRuntimeVoxelDebrisPosition(RuntimeProjectileEffect& debris);

int spawnRuntimeUnitDebris(RuntimeProductionHostState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause);

RuntimeDebrisSurface runtimeVoxelDebrisSurface(const RuntimeGameSessionState& state,
    const std::array<float,3>& previous, const std::array<float,3>& next);

void spawnRuntimeVoxelDebrisAnimation(RuntimeProductionHostState& state, const RuntimeProjectileEffect& debris,
    const std::string& id);

bool advanceRuntimeVoxelDebris(RuntimeProductionHostState& state, RuntimeProjectileEffect& debris);

bool beginRuntimeWreck(RuntimeProductionHostState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause, bool wasAliveBeforeDamage = false);

void spawnRuntimeWreckAnimation(RuntimeProductionHostState& state, const RuntimeWreckState& wreck,
    const WorldObjectInstance& pose, const std::string& animation, bool water, const char* phase);

bool advanceRuntimeWrecks(RuntimeProductionHostState& state);

void spawnRuntimeUnitDestructionEffects(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& destroyed,
    const UnitType& unit,
    const RuntimeProjectileEffect& projectile,
    const RuntimeProjectileTargetPosition& position,
    int damage,
    bool wasAliveBeforeDamage = false
);

void emitRuntimeUnitDeathSound(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& object,
    std::optional<RuntimeSpatialAudioPosition> source = std::nullopt,
    bool includeCrashingSound = false
);

bool runtimeProjectileHasDeathWeaponLineage(
    const RuntimeProjectileEffect& projectile);

bool runtimeProjectileHasLockedDeathWeaponDamage(
    const RuntimeProjectileEffect& projectile);

RuntimeDeathWeaponCause runtimeDeathWeaponCauseForProjectile(
    const RuntimeProjectileEffect& projectile);

double runtimeCellDistanceToTerrainObjectFootprint(
    const RuntimeCellPoint& point,
    const WorldTerrainObject& object);

double runtimeProjectilePercentAtMaxFraction(
    const RuntimeProjectileEffect& projectile);

int runtimeProjectileTerrainDamageAtDistance(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType* weapon,
    double distance);

RuntimeTerrainProjectileDamageResult applyRuntimeProjectileTerrainDamage(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const RuntimeCellPoint& impactCell,
    const WeaponType* weapon,
    const WarheadType* warhead);

const BuildingType* runtimeOverlayWallRuleForCell(
    const RuntimeGameSessionState& state, const TerrainCell& cell);

bool runtimeOverlayWallUsesCurrentOverlay(const TerrainCell& cell);

bool runtimeIsOverlayWallTargetId(int id);

bool runtimeOverlayWallBehaviorTargetIsValid(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& behavior);

std::optional<WorldObjectInstance> runtimeOverlayWallTargetAtCell(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& point);

std::optional<WorldObjectInstance> runtimeAttackGroundTargetObject(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& behavior);

void refreshRuntimeOverlayWallTopology(RuntimeGameSessionState& state);

RuntimeTerrainProjectileDamageResult applyRuntimeProjectileOverlayWallDamage(
    RuntimeProductionHostState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeCellPoint& impactCell, const WeaponType* weapon, const WarheadType* warhead);

void applyRuntimePsychicStatusActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimePsychicStatusAction>& actions);

RuntimePsychicStatusHitResult applyRuntimePsychicProjectileHit(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target,
    const WarheadType& warhead);

std::int64_t runtimeMagBeamToQ16(double value);

double runtimeMagBeamFromQ16(std::int64_t value);

std::int64_t runtimeMagBeamApproachQ16(
    std::int64_t current,
    std::int64_t target,
    std::int64_t maximumDelta);

const RuntimeMagBeamBindingState* runtimeMagBeamBindingForTarget(
    const RuntimeGameSessionState& state,
    int objectId);

const RuntimeMagBeamBindingState* runtimeMagBeamBindingForSource(
    const RuntimeGameSessionState& state,
    int objectId);

bool runtimeObjectSuspendedByMagBeam(
    const RuntimeGameSessionState& state,
    int objectId);

bool runtimeObjectOffGroundByMagBeam(
    const RuntimeGameSessionState& state,
    int objectId);

RuntimeMagBeamTargetKind runtimeMagBeamTargetKindForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const UnitType*& unit);

RuntimeMagBeamKinematics runtimeMagBeamKinematicsForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeMagBeamBindingState* binding);

RuntimeMagBeamSourceSnapshot runtimeMagBeamSourceSnapshotForBinding(
    const RuntimeGameSessionState& state,
    const RuntimeMagBeamBindingState& binding);

RuntimeMagBeamTargetSnapshot runtimeMagBeamTargetSnapshotForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const WorldObjectInstance* source,
    const std::string& temporaryLocomotorId,
    const RuntimeMagBeamBindingState* binding);

RuntimeMagBeamTargetSnapshot runtimeMagBeamTargetSnapshotForBinding(
    const RuntimeGameSessionState& state,
    const RuntimeMagBeamBindingState& binding);

bool runtimeMagBeamSourceMaintainsLock(
    const RuntimeGameSessionState& state,
    const RuntimeMagBeamBindingState& binding);

RuntimeMagBeamOccupancySnapshot runtimeMagBeamLandingOccupancy(
    const RuntimeGameSessionState& state,
    int targetObjectId,
    const RuntimeCellPoint& cell);

RuntimeMagBeamKinematics runtimeAdvanceMagBeamActiveKinematics(
    const RuntimeGameSessionState& state,
    const RuntimeMagBeamBindingState& binding,
    const WorldObjectInstance& source);

RuntimeMagBeamHostUpdate runtimeAdvanceMagBeamFallingKinematics(
    const RuntimeGameSessionState& state,
    const RuntimeMagBeamBindingState& binding,
    const WorldObjectInstance& target);

void applyRuntimeMagBeamKinematicsToObject(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const RuntimeMagBeamKinematics& kinematics,
    bool suspended);

bool applyRuntimeMagBeamFallDamage(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    int damage,
    const RuntimeMagBeamAction& action,
    const std::string& reason);

bool applyRuntimeMagBeamActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeMagBeamAction>& actions);

bool advanceRuntimeMagBeams(RuntimeProductionHostState& state);

bool reconcileRuntimeMagBeamsWithWorld(RuntimeProductionHostState& state);

void resolveRuntimeMagBeamRenderEndpoints(RuntimeProductionHostState& state,
    SpecialMechanismRenderSnapshot& snapshot, const RuntimeMagBeamBindingState& binding);

bool advanceRuntimeMagBeamVisual(RuntimeProductionHostState& state, RuntimeProjectileEffect& beam);

void spawnRuntimeMagBeamVisual(RuntimeProductionHostState& state, const RuntimeProjectileEffect& shot);

bool runtimeMagBeamWarheadCombinationIsExclusive(
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead);

bool applyRuntimeMagBeamProjectileImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead);

RuntimeAirstrikeBounds runtimeAirstrikeWorldBounds(
    const RuntimeGameSessionState& state);

RuntimeAirstrikeAircraftDefinition runtimeAirstrikeAircraftDefinition(
    const RuntimeGameSessionState& state,
    const std::string& typeId,
    bool elite);

bool runtimeAirstrikeDesignatorCombinationIsExclusive(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead);

void completeRuntimeAirstrikeDesignatorBehavior(
    RuntimeProductionHostState& state,
    int callerObjectId,
    int targetObjectId);

void resolveRuntimeAirstrikeRenderEndpoints(RuntimeProductionHostState& state,
    SpecialMechanismRenderSnapshot& snapshot);

bool applyRuntimeAirstrikeDesignatorImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType& weapon,
    const WarheadType& warhead);

bool finishRuntimeAirstrikeSalvo(RuntimeGameSessionState& state,
    RuntimeAirstrikeRootAuthority& authority);

void reconcileRuntimeAirstrikeSalvos(RuntimeGameSessionState& state);

void advanceRuntimeAirstrikeSalvos(RuntimeProductionHostState& state);

bool authorizeRuntimeAirstrikeAttributedImpact(
    RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile);

std::string runtimeConventionalImpactAnimation(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WarheadType* warhead,
    const RuntimeCellPoint& impactCell);

bool applyRuntimeFireWarheadToIce(
    RuntimeProductionHostState& state,
    const WarheadType* warhead,
    const RuntimeCellPoint& impactCell);

bool spawnRuntimeSparkyBuildingEffect(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const WarheadType* warhead,
    const WorldObjectInstance& building,
    int healthBefore,
    int maximumHealth,
    int appliedDamage);

const WorldObjectInstance* runtimeTypeImmuneSource(
    const RuntimeGameSessionState& state, int objectId);

std::string runtimeTypeImmuneHouse(const RuntimeGameSessionState& state, const std::string& owner);

bool runtimeDamageIsTypeImmune(const RuntimeGameSessionState& state,
    const WorldObjectInstance& target, const WorldObjectInstance* source,
    int authoredDamage, bool ignoreDefenses = false);

const WorldObjectInstance* runtimeFlakImpactBodyTarget(
    const RuntimeGameSessionState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeProjectileTargetPosition& hit);

bool applyRuntimeProjectileImpact(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    int attachedIvanBombTargetId = -1
);

const ParticleSystemType* runtimeVehicleDamageSmokeType(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

RuntimeParticlePoint runtimeVehicleDamageSmokeOrigin(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

int runtimeVehicleDamageSmokeScreenOffset(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

RuntimeParticleSpriteAttachment runtimeVehicleDamageSmokeSpriteAttachment(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool runtimeVehicleDamageSmokePresentation(
    RuntimeProductionHostState& state, const WorldObjectInstance& object, int screenOffsetY,
    RuntimeParticleAirbornePresentation& presentation, std::string& error,
    std::unique_ptr<RuntimeSessionAirPresentationBatch>* sharedBatch = nullptr
);

void reportRuntimeVehicleSmokePresentationFailure(
    RuntimeGameSessionState& state, int sourceId, const std::string& error);

void stopRuntimeVehicleDamageSmokeForSource(RuntimeGameSessionState& state, int sourceObjectId);

bool syncRuntimeVehicleDamageSmoke(RuntimeProductionHostState& state);

bool advanceRuntimeParticles(RuntimeProductionHostState& state);

bool applyRuntimeAirstrikeActionsAtomically(
    RuntimeProductionHostState& state,
    const RuntimeAirstrikeSystemState& airstrikeCandidate,
    const std::vector<RuntimeAirstrikeAction>& actions);

RuntimeAirstrikeTargetSnapshot runtimeAirstrikeTargetSnapshotForStrike(
    const RuntimeGameSessionState& state,
    const RuntimeAirstrikeStrikeState& strike);

bool runtimeAirstrikeCallerMatchesStrike(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& caller,
    const RuntimeAirstrikeStrikeState& strike);

void updateRuntimeAirstrikePlaneRoutes(RuntimeGameSessionState& state);

bool advanceRuntimeAirstrikes(RuntimeProductionHostState& state);

bool advanceRuntimeStructureRepairs(RuntimeProductionHostState& state);

int runtimeTiberiumHealIntervalTicks(const RuntimeGameSessionState& state);

bool advanceRuntimeTiberiumHealing(RuntimeProductionHostState& state);

bool advanceRuntimeSelfHealing(RuntimeGameSessionState& state);

bool cleanRuntimeLocalPlayerSelection(RuntimeProductionHostState& state);

void cleanRuntimeSelection(RuntimeProductionHostState& state);

void refreshRuntimeActionSummary(RuntimeProductionHostState& state);

void resetLastRuntimeCommand(
    RuntimeProductionHostState& state,
    const std::string& verb,
    const RuntimeCellPoint& targetCell,
    int targetObjectId,
    const std::string& targetObjectType);

bool runtimeObjectAllowsMoveToCell(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object, RuntimeCellPoint cell);

bool runtimeObjectCanReceiveMoveCommand(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

void runtimeCaptureMassMovementRejection(
    const RuntimeGameSessionState& state, int objectId, const WorldObjectInstance* object,
    RuntimeCellPoint click, const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail = {}, const UnitType* resolvedType = nullptr
);

void runtimeCaptureMassMovementRejectionById(
    const RuntimeGameSessionState& state, int objectId, RuntimeCellPoint click,
    const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail = {}, const UnitType* resolvedType = nullptr
);

bool runtimeChronoMoveTerrainReference();

inline bool runtimeStrictSlotVehicleReference() noexcept;

inline bool runtimeStrictSlotVehicleCanonical(const RuntimeCellPoint& p) noexcept;

inline bool runtimeStrictSlotVehicleStrictWinner(int softPenalty,std::uint64_t tie) noexcept;

bool tryRuntimeStrictSlotVehicleCertificate(
    const RuntimeGameSessionState& state,const std::vector<int>& ids,const RuntimeCellPoint& target,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupancy,int representativeId,std::size_t movableCount,
    std::vector<RuntimeCellPoint>* provenPoints =nullptr,
    std::size_t vertexLimit =4096,std::size_t edgeLimit =8192);

inline bool runtimeMixedLocalComponentReference() noexcept;

inline bool runtimeMixedKnownUnmappedReference() noexcept;

inline bool runtimeMixedBestFrontierClosureReference() noexcept;

void issueRuntimeMoveCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& targetCell,
    std::map<int, RuntimeCellPoint>* acceptedDestinations = nullptr,
    bool suppressCommandVoiceFeedback = false
);

std::vector<int> runtimeCombatSelection(const RuntimeProductionHostState& state);

bool runtimeWeaponRepairTargetLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const RuntimeWeaponProfile& profile);

void issueRuntimeAttackCommand(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject,
    bool forceFire = false
);

bool issueRuntimeCellForceFireCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& requestedCell,
    bool bridgeOnly = false
);

void issueRuntimeStopCommand(RuntimeProductionHostState& state);

bool runtimeObjectIsEngineer(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeEngineerStructureIsRepairOnly(const BuildingType& building);

bool runtimeEngineerCanCaptureStructure(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& engineer,
    const WorldObjectInstance& structure);

bool runtimeEngineerCanRepairStructure(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& engineer,
    const WorldObjectInstance& structure);

RuntimeEngineerAction runtimeEngineerActionForStructure(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& engineer,
    const WorldObjectInstance& structure);

int repairRuntimeBridgeForHut(
    RuntimeProductionHostState& state,
    int hutObjectId);

RuntimeTimedBombArrivalResult completeRuntimeTimedBombIntent(
    RuntimeProductionHostState& state,
    int sourceObjectId);

bool completeRuntimeEngineerCapture(
    RuntimeProductionHostState& state,
    int engineerId);

int runtimeTransportReservedPassengerCount(
    const RuntimeGameSessionState& state,
    int transportId,
    int excludedPassengerId = -1
);

const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(
    const RuntimeGameSessionState& state,
    int objectId);

WorldObjectInstance* findMutableRuntimeObjectByIdIncludingInactive(
    RuntimeGameSessionState& state,
    int objectId);

std::vector<RuntimeGarrisonEvacuationAction>
evacuateRuntimeGarrisonHost(
    RuntimeProductionHostState& state,
    int hostObjectId,
    RuntimeGarrisonEvacuationReason reason,
    bool destroyWhenBlocked);

void cancelRuntimePendingOccupyForTarget(
    RuntimeProductionHostState& state,
    int targetObjectId,
    const std::string& reason);

void handleRuntimeGarrisonHostDestruction(
    RuntimeProductionHostState& state,
    int hostObjectId);

bool advanceRuntimeGarrisonState(RuntimeProductionHostState& state);

const BuildingType* runtimeTankBunkerType(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectIsTurningInTankBunker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& tank);

bool runtimeObjectCanEnterTankBunker(const RuntimeGameSessionState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host, bool reserve);

std::optional<RuntimeCellPoint> runtimeTankBunkerEntryCell(RuntimeProductionHostState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host);

void positionRuntimeBunkeredTank(RuntimeGameSessionState& state, WorldObjectInstance& tank,
    const WorldObjectInstance& host);

bool completeRuntimeTankBunkerEntry(RuntimeProductionHostState& state, int tankId, int hostId);

void releaseRuntimeTankBunker(RuntimeProductionHostState& state, WorldObjectInstance& host);

bool unloadRuntimeTankBunker(RuntimeProductionHostState& state, WorldObjectInstance& object);

bool advanceRuntimeTankBunkers(RuntimeProductionHostState& state);

bool runtimeObjectCanEnterTransport(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport,
    bool includeReservations,
    bool requireBoardingPose = true
);

void refreshRuntimeTransportGunnerState(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& transport);

void destroyRuntimeTransportPassengers(
    RuntimeProductionHostState& state,
    WorldObjectInstance& transport,
    const std::string& mission);

bool runtimePersonalChronoTransportPassenger(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger);

std::optional<bool> runtimeChronoTransportDestinationAdmission(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const RuntimeCellPoint& destination);

bool runtimeChronoTransportArrivalReady(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& host);

bool completeRuntimeTransportEntry(
    RuntimeProductionHostState& state,
    int passengerId,
    bool initialCargo = false
);

bool runtimeObjectCanOccupyTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& occupier,
    const WorldObjectInstance& targetStructure,
    bool includeReservations = false,
    int ignoredOccupierId = -1
);

bool completeRuntimeOccupyEntry(
    RuntimeProductionHostState& state,
    int occupierId);

std::optional<RuntimeCellPoint> findRuntimeC4ExteriorApproachCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const WorldObjectInstance& target);

std::optional<RuntimeCellPoint> findRuntimeStructureApproachCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const WorldObjectInstance& target,
    const std::string& purpose,
    int fallbackRadiusCells);

std::optional<RuntimeCellPoint> findRuntimeEngineerEntryCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& engineer,
    const WorldObjectInstance& target,
    const std::string& purpose,
    int fallbackRadiusCells);

const BuildingType* runtimeInfantryServiceBuildingRules(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target);

bool runtimeInfantryServiceOwnersMatch(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target);

RuntimeInfantryServiceEvaluation runtimeInfantryServiceEvaluationFor(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target);

bool runtimeBehaviorIsPendingInfantryService(
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior);

bool runtimeSelectionHasInfantryServiceRelationship(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& target);

bool queueRuntimeInfantryServiceTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure);

bool consumeRuntimeBlockedInfantryServiceTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure);

bool completeRuntimeInfantryService(
    RuntimeProductionHostState& state,
    int infantryObjectId);

bool runtimeSpyCanInfiltrateTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& spy,
    const WorldObjectInstance& target);

bool queueRuntimeSpyInfiltration(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure);

bool completeRuntimeSpyInfiltration(
    RuntimeProductionHostState& state,
    int spyObjectId);

bool queueRuntimeEngineerCaptureForObject(
    RuntimeProductionHostState& state, int engineerId,
    const WorldObjectInstance& targetStructure, bool playerFeedback);

bool queueRuntimeEngineerCapture(RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure);

bool queueRuntimeOccupyTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetStructure);

bool runtimeTargetHasPendingC4Lease(
    const RuntimeGameSessionState& state,
    int targetObjectId);

std::optional<int> runtimeSelectedC4SourceIdForTarget(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject,
    bool forceFire = false
);

bool queueRuntimeTimedBombTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject,
    bool forceFire = false
);

std::optional<RuntimeCellPoint> runtimeAmphibiousTransportAccessCell(
    RuntimeProductionHostState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport, bool boarding,
    const std::unordered_set<int>* reservedCells = nullptr
);

RuntimeTransportMotionBody runtimeTransportMotionBody(const WorldObjectInstance& object);

int runtimeTransportAuthorizedBoardingHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& destination);

void restoreRuntimeTransportMotionBody(WorldObjectInstance& object,
    const RuntimeTransportMotionBody& body);

std::optional<int> runtimeTransportMotionTransferTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& accessCell, double curveLength);

bool runtimeTransportUnitBoardingPending(const RuntimeGameSessionState& state, int id);

bool runtimeUnitTransportApproachAllowsBodyDetour(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move);

bool runtimeTransportUnloadingPending(const RuntimeGameSessionState& state,
    const WorldObjectInstance& host);

bool runtimeTransportSavedBoardingEndpointAllowed(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    double toLevel, bool toBridge, std::optional<std::pair<double, bool>> validatedHostPose = std::nullopt);

bool runtimeTransportBoardingReadyForManifest(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move);

bool cancelRuntimeTransportBoardingTransfer(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move);

void cancelRuntimeTransportBoardingForMovingHost(RuntimeProductionHostState& state, int hostId);

bool advanceRuntimeTransportBoardingSegment(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move);

bool runtimeTransportTransferBodiesClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId);

bool runtimeTransportTransferTransitionClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to, int hostId, bool boarding);

bool runtimeTransportTransferFoundationClear(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId);

bool beginRuntimeTransportGroundTransfer(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const RuntimeTransportMotionBody& source, const RuntimeTransportMotionBody& destination,
    int destinationSlot, bool boarding, const std::vector<RuntimeCellPoint>& remainingPath = {});

bool advanceRuntimeUnitTransportBoarding(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host);

void prepareRuntimeTransportUnloadMotion(RuntimeProductionHostState& state, WorldObjectInstance& host);

int unloadRuntimeUnitTransportMotion(RuntimeProductionHostState& state, WorldObjectInstance& host);

bool advanceRuntimeTransportUnloading(RuntimeProductionHostState& state);

bool runtimeTransportBoardingAccessBodyClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance& host, const RuntimeCellPoint& access);

std::optional<RuntimeCellPoint> runtimeTransportEntryCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport);

bool queueRuntimeTransportTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetTransport);

bool runtimeSlaveMinerDeliveryTargetLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& target);

bool runtimeSelectionOffersSlaveDelivery(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& target);

bool queueRuntimeSlaveMinerDeliveryTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& target);

bool queueRuntimeTargetAbility(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject);

int unloadRuntimeTransport(
    RuntimeProductionHostState& state,
    WorldObjectInstance& transport);

std::string runtimeSecondaryAbilityName(RuntimeSecondaryAbility ability);

bool runtimeObjectHasSuicideDeployAction(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

RuntimeSecondaryAbility runtimeSecondaryAbilityForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectHasSecondaryAction(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool executeRuntimeSecondaryAction(
    RuntimeProductionHostState& state,
    int objectId,
    std::optional<bool> desiredDeployment = std::nullopt
);

void ensureRuntimeRepairDepotsInitialized(RuntimeGameSessionState& state);

std::string runtimeRepairDepotCanonicalOwnerId(
    const RuntimeGameSessionState& state,
    const std::string& owner);

int runtimeRepairDepotCreditBalance(
    const RuntimeProductionHostState& state,
    const std::string& canonicalOwner);

bool runtimeRepairDepotCellIsOpenWater(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell);

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotRawDockCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    int dockIndex);

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotDockCell(
    const RuntimeGameSessionState& state,
    int hostId,
    int dockIndex,
    int unitId = -1
);

std::optional<RuntimeCellPoint> runtimeRepairDepotPerimeterCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& host,
    int slotIndex);

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotQueueCell(
    RuntimeGameSessionState& state,
    int hostId,
    int queueIndex,
    int unitId);

std::optional<RuntimeRepairDepotCell> runtimeRepairDepotRallyCell(
    RuntimeProductionHostState& state,
    int hostId,
    int dockIndex,
    int unitId,
    const RuntimeRepairDepotSystemState& authority,
    const std::set<std::tuple<int, int, bool>>* batchReservedCells = nullptr
);

const RuntimeRepairDepotRelation* findRuntimeRepairDepotRelationForUnit(
    const RuntimeRepairDepotSystemState& authority,
    int unitId);

std::optional<int> runtimeRepairDepotAllowedHostForCell(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotSystemState& authority,
    int unitId,
    const RuntimeRepairDepotCell& cell);

bool runtimeRepairDepotReservationIsActive(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotSystemState& authority,
    int hostId,
    int phaseSlotIndex,
    int unitId);

RuntimeRepairDepotContext makeRuntimeRepairDepotContext(
    RuntimeProductionHostState& state,
    const RuntimeRepairDepotSystemState* authority = nullptr
);

void debitRuntimeRepairDepotCredits(
    RuntimeProductionHostState& state,
    const std::string& canonicalOwner,
    int amount);

void depositRuntimeRepairDepotCredits(
    RuntimeProductionHostState& state,
    const std::string& canonicalOwner,
    int amount);

RuntimeSpyContext makeRuntimeSpyContext(const RuntimeGameSessionState& state);

bool applyRuntimeSpyActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeSpyAction>& actions);

void initializeRuntimeSpyDefaultDisguiseForObject(
    RuntimeProductionHostState& state,
    int objectId);

void ensureRuntimeSpiesInitialized(RuntimeProductionHostState& state);

bool advanceRuntimeSpies(RuntimeProductionHostState& state);

bool runtimeSpyDisguiseConcealsFromOwner(
    const RuntimeGameSessionState& state,
    const std::string& observerOwner,
    const WorldObjectInstance& object);

bool runtimeSpyDisguiseConcealsFromObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& observer,
    const WorldObjectInstance& object);

RuntimeSpyObserverProjection runtimeSpyObserverProjectionForOwner(
    const RuntimeGameSessionState& state,
    const std::string& observerOwner,
    const WorldObjectInstance& object,
    bool ownerAppearance = false
);

bool runtimeSpyDisguiseBlocksAttack(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& observer,
    const WorldObjectInstance& target,
    bool autoAcquired);

bool runtimeSpyCanApplyMakesDisguise(
    const RuntimeGameSessionState& state,
    int sourceObjectId,
    int targetObjectId,
    const std::string& warheadId);

RuntimeGrindingContext makeRuntimeGrindingContext(
    const RuntimeGameSessionState& state,
    const RuntimeGrindingSystemState* authority = nullptr,
    bool requireEntryAuthority = true
);

bool runtimeRepairActionClaimsLocalAircraftPad(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotAction& action);

RuntimeRepairDepotContext makeRuntimeRepairDepotCandidateContext(
    RuntimeProductionHostState& state,
    const RuntimeRepairDepotSystemState& authority,
    const std::set<std::pair<int, int>>& blockedDocks);

bool runtimeObjectConflictsWithGrinding(
    const RuntimeGameSessionState& state,
    int objectId,
    int allowedTransportRootId = -1
);

const RuntimeGrindingRelation* runtimeGrindingRelationForSource(
    const RuntimeGrindingSystemState& authority,
    int sourceId);

std::optional<RuntimeCellPoint> runtimeGrindingEntryCell(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& host);

void seedRuntimeGrindingTransactionScratch(
    RuntimeProductionHostState& scratch,
    const RuntimeProductionHostState& live);

void commitRuntimeGrindingTransactionScratch(
    RuntimeProductionHostState& live,
    RuntimeProductionHostState& scratch) noexcept;

bool runtimeGrindingEntryContinuesToHost(
    const RuntimeGameSessionState& state, int hostId, int sourceId);

void emitRuntimeGrindingCompletionSounds(
    RuntimeProductionHostState& state, const std::vector<RuntimeGrindingAction>& actions) noexcept;

bool runtimeGrindingVetoIsPermanent(RuntimeGrindingVeto veto);

RuntimeGrindingTeamEligibility runtimeGrindingTeamEligibility(
    const RuntimeGameSessionState& state,
    int hostId,
    int sourceId);

bool queueRuntimeGrindingEntry(
    RuntimeProductionHostState& state,
    int hostId,
    int sourceId,
    bool playerFeedback);

bool queueRuntimeGrindingTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject);

bool completeRuntimeGrindingEntry(
    RuntimeProductionHostState& state,
    int sourceId);

int runtimeGrindingExpectedRefund(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    int purchaseValue);

bool initializeRuntimeGrindingAnimation(RuntimeProductionHostState& state, int hostId,
    RuntimeImpactEffect& cycle);

void startRuntimeGrindingAnimation(RuntimeProductionHostState& state, int hostId);

bool promoteRuntimeGrindingAnimations(RuntimeProductionHostState& state);

void writeRuntimeGrindingAnimations(const RuntimeGameSessionState& state, std::ostream& output);

bool restoreRuntimeGrindingAnimations(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error);

bool applyRuntimeGrindingActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeGrindingAction>& actions,
    RuntimeGrindingSystemState candidate);

bool advanceRuntimeGrinding(RuntimeProductionHostState& state);

bool reconcileRuntimeGrindingWithWorld(RuntimeProductionHostState& state);

bool validateRuntimeGrindingHostProjection(
    const RuntimeGameSessionState& state,
    const RuntimeGrindingSystemState& authority,
    int savedTick,
    std::string& errorMessage);

bool normalizeRuntimeLegacyAmmoWithHistoryQueries(RuntimeGameSessionState& state);

bool runtimeHasSpecialCapabilityForHistoryPhase(const RuntimeGameSessionState& state);

bool ensureRuntimeAircraftServicesInitialized(RuntimeGameSessionState& state);

RuntimeAircraftServiceContext makeRuntimeAircraftServiceContext(
    const RuntimeProductionHostState& state);

bool applyRuntimeAircraftServiceActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeAircraftServiceAction>& actions);

bool advanceRuntimeAircraftServices(RuntimeProductionHostState& state);

bool runtimeBehaviorMatchesRepairAction(
    const RuntimeGameSessionState& state,
    const RuntimeRepairDepotAction& action);

void queueRuntimeRepairDepotExactMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& unit,
    const RuntimeRepairDepotCell& target,
    int allowedOccupiedObjectId,
    bool remainsInRepairFlow,
    const std::string& reason);

bool applyRuntimeRepairDepotActions(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeRepairDepotAction>& actions);

bool releaseRuntimeRepairDepotForCommand(
    RuntimeProductionHostState& state,
    int unitId,
    const std::string& reason);

bool releaseRuntimeRepairDepotRelationsForObject(
    RuntimeProductionHostState& state,
    int objectId,
    const std::string& reason);

bool releaseRuntimeExclusiveAppliedAuthorities(
    RuntimeProductionHostState& state,
    int targetObjectId,
    const std::string& reason,
    bool releaseAircraftDock);

bool releaseRuntimePsychicAppliedAuthorities(
    RuntimeProductionHostState& state,
    int targetObjectId);

bool transitionRuntimeRepairDepotHost(
    RuntimeProductionHostState& state,
    int hostId,
    RuntimeRepairDepotHostTransitionKind kind,
    bool temporalRemoval,
    const std::string& expectedOwner,
    const std::string& newOwner = std::string()
);

RuntimeDeathWeaponContext makeRuntimeDeathWeaponContext(
    const RuntimeGameSessionState& state);

bool spawnRuntimeDeathWeaponProjectile(
    RuntimeProductionHostState& state,
    const RuntimeDeathWeaponFireRequest& request);

RuntimeObjectExitQueueResult queueRuntimeObjectExit(
    RuntimeGameSessionState& state,
    RuntimeObjectExitRequest request);

bool runtimeParasiteObjectIsContained(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeParasiteObjectHasExclusiveAuthority(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    bool asHost = false
);

bool runtimeParasiteWarheadIsExclusive(
    const WeaponType& weapon,
    const WarheadType& warhead);

bool runtimeProjectileIsParasiteLeap(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile);

void syncRuntimeParasiteLeapTargetPose(
    const RuntimeGameSessionState& state,
    RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& target);

std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& source,
    const SpecialCell& requested,
    const std::unordered_set<int>* reservedCells = nullptr,
    int terminatingHostId = -1
);

std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions);

bool validateRuntimeParasiteActionBatch(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions);

bool runtimeObjectReselectsAfterParasiteLimbo(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeObjectRejoinsTeamAfterParasiteLimbo(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeComputerTacticReservesObject(const RuntimeGameSessionState& state, int objectId);

bool finishRuntimeComputerTacticMission(RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

std::string runtimeComputerTacticTargetOwner(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

void detachRuntimeObjectFromTeamMembership(
    RuntimeGameSessionState& state,
    int objectId);

RuntimeParasiteActionApplyResult applyRuntimeParasiteActionsAtomically(
    RuntimeProductionHostState& state,
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeDeathWeaponCause& inheritedDeathWeaponCause,
    int fallbackSourceObjectId,
    bool suppressUnitDeathSounds = false
);

bool applyRuntimeSpecialMechanismActions(
    RuntimeProductionHostState& state,
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeDeathWeaponCause& inheritedDeathWeaponCause = {},
    int fallbackSourceObjectId = -1,
    bool suppressUnitDeathSounds = false
);

int runtimeParasiteRuleFramesToTicks(int frames, int fixedTickHz);

bool launchRuntimeParasiteProjectile(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    const RuntimeWeaponProfile& weaponProfile);

bool applyRuntimeParasiteProjectileImpactExclusive(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile);

bool handleRuntimeParasiteHostEvent(
    RuntimeProductionHostState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks);

bool preflightRuntimeParasiteHostEvent(
    RuntimeGameSessionState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks);

bool requestRuntimeChronoMoveWithParasite(
    RuntimeProductionHostState& state,
    int objectId,
    SpecialCell destination,
    int destinationLevel,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    std::string& reason);

bool runtimeChronoCellIsOccupied(
    const RuntimeGameSessionState& state, const std::unordered_map<int,int>& occupied,
    int x, int y, int objectId);

void spawnRuntimeChronoWarpEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event, bool playSound = true
);

bool holdRuntimeChronoRecovery(
    RuntimeGameSessionState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior);

std::optional<bool> advanceRuntimeTeleporterMove(
    RuntimeProductionHostState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, RuntimeCellPoint destination);

bool advanceRuntimeTemporalAttacks(RuntimeProductionHostState& state);

void resolveRuntimeTemporalBeamEndpoints(RuntimeProductionHostState& state);

void spawnRuntimeTemporalEraseEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event);

int runtimeCrewSide(const RuntimeGameSessionState& state, const std::string& owner);

std::vector<RuntimeCrewSurvivorSpawn> prepareRuntimeCrewSurvivors(
    const RuntimeProductionHostState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request);

void spawnRuntimeCrewSurvivors(RuntimeProductionHostState& state,
    const std::vector<RuntimeCrewSurvivorSpawn>& survivors);

std::optional<RuntimeDeathCrateSpawn> prepareRuntimeDeathCrate(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request);

bool spawnRuntimeDeathCrate(RuntimeProductionHostState& state,
    const std::optional<RuntimeDeathCrateSpawn>& drop);

RuntimeObjectExitResult finalizeRuntimeObjectExit(
    RuntimeProductionHostState& state,
    const RuntimeObjectExitRequest& request, RuntimeEconomyState* miningEconomy = nullptr
);

bool drainRuntimeObjectExitQueue(RuntimeProductionHostState& state, RuntimeEconomyState* miningEconomy = nullptr);

bool advanceRuntimeRepairDepots(RuntimeProductionHostState& state);

RuntimeRepairDepotArrivalResult completeRuntimeRepairDepotArrival(
    RuntimeProductionHostState& state,
    int unitId);

bool runtimeHarvesterManualUnloadEligible(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery);

bool runtimeSelectionOffersHarvesterUnload(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject);

bool queueRuntimeHarvesterUnloadCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance& targetObject);

bool runtimeSelectionCanUseRepairDepot(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject);

bool queueRuntimeUnitRepairCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance& targetObject);

void issueRuntimeCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance* targetObject);

bool runtimeGameplayObjectVisibleToOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const WorldObjectInstance& object);

bool validateRuntimeGameplayOwnedObjects(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::vector<std::int32_t>& objectIds,
    std::string& errorMessage);

template<typename Apply>
bool applyRuntimeGameplaySelectionCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::vector<std::int32_t>& objectIds,
    Apply&& apply);

SuperWeaponTarget runtimeGameplaySuperWeaponTarget(
    const RuntimeMultiplayerGameplaySuperWeaponTarget& target);

bool runtimeGameplaySuperWeaponReadyForCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayFireSuperWeaponCommand& command);

std::string runtimePlayerFactoryCategory(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object);

void prioritizeRuntimePlayerFactory(const RuntimeGameSessionState& state,
    const std::string& owner, std::vector<const WorldObjectInstance*>& candidates);

void queueRuntimePlayerFactoryRally(RuntimeGameSessionState& state, int factoryId, int producedId);

bool runtimePlayerActorTemporarilyCommandLocked(
    const RuntimeGameSessionState& state, const WorldObjectInstance& actor);

template<class Apply>
bool applyRuntimePlayerSelectionCommand(RuntimeProductionHostState& state,
    const std::string& owner, const std::vector<std::int32_t>& objectIds, Apply&& apply);

bool beginRuntimePlayerTravelOrder(RuntimeProductionHostState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayTargetOrderCommand& command);

bool runtimePlayerGuardDestinationAllowsTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target);

bool runtimePlayerTravellingCanAcquire(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor);

bool advanceRuntimePlayerTravelOrders(RuntimeProductionHostState& state);

bool executeRuntimePlayerTargetOrder(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection = false,
    bool suppressCommandVoiceFeedback = false
);

bool advanceRuntimePlayerWaypointOrders(RuntimeProductionHostState& state);

bool advanceRuntimePlayerCommands(RuntimeProductionHostState& state);

bool runtimeTaskForceHasBuildableUnits(
    const RuntimeGameSessionState& state,
    const RuntimeTaskForce& taskForce);

const RuntimeTeamType* findRuntimeTriggerTeamById(
    const RuntimeGameSessionState& state,
    const std::string& id);

const RuntimeTeamType* chooseRuntimeTriggerTeam(
    const RuntimeGameSessionState& state);

const RuntimeTeamType* chooseRuntimeTriggerTeamForAction(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action,
    bool allowFallback = true
);

std::optional<RuntimeCellPoint> runtimeWaypointCell(
    const RuntimeGameSessionState& state,
    const std::string& waypointId);

std::optional<RuntimeCellPoint> runtimeDefaultTriggerSpawnCell(
    const RuntimeGameSessionState& state);

const WorldObjectInstance* runtimeTriggerTargetObject(
    const RuntimeGameSessionState& state,
    const std::string& teamHouse);

bool runtimeTriggerTeamAtCapacity(const RuntimeGameSessionState& state, const RuntimeTeamType& team);

void issueRuntimeTriggerTeamOrder(
    RuntimeProductionHostState& state,
    const RuntimeTeamType& team,
    const std::vector<int>& objectIds,
    const std::string& sourceAiTriggerId = {},
    int sourceAiBuildId = -1
);

bool advanceRuntimeTriggerReinforcements(RuntimeProductionHostState& state);

bool queueRuntimeTriggerReinforcement(
    RuntimeProductionHostState& state,
    const RuntimeTeamType& team,
    std::optional<RuntimeCellPoint> authoredOrigin,
    int actionType);

std::vector<int> runtimeLiveTeamObjectIds(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

std::string runtimeTeamMissionOwner(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

bool runtimeTeamMemberNeedsGuard(const RuntimeGameSessionState& state, const WorldObjectInstance& member);

bool runtimeTeamGuardsSlower(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission);

double runtimeTeamMemberStray(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission,
    const WorldObjectInstance& member, int action);

bool runtimeTeamGuardHasStrength(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission);

RuntimeCellPoint runtimeTeamMissionCenter(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

bool runtimeTeamHasPendingWork(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission);

int issueRuntimeTeamMove(
    RuntimeProductionHostState& state,
    const std::vector<int>& objectIds,
    const RuntimeCellPoint& target);

const WorldObjectInstance* runtimeTeamNearestFriendlyFollowTarget(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    const std::vector<int>& liveIds);

int issueRuntimeTeamFollow(
    RuntimeProductionHostState& state,
    const std::vector<int>& liveIds,
    const WorldObjectInstance& target);

std::optional<RuntimeCellPoint> runtimeTeamScoutDestination(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    const std::vector<int>& liveIds);

std::optional<std::string> runtimeTeamIdleSequenceName(int argument);

int runtimeTeamIdleAnimationTicks(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    const std::string& sequenceName);

std::optional<std::string> runtimeTeamIndexedAudioId(
    const IniFile& ini,
    const std::string& sectionName,
    int zeroBasedIndex);

void emitRuntimeTeamSpeech(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::string& speechId);

int loadRuntimeTeamTrucks(
    RuntimeProductionHostState& state,
    const std::vector<int>& liveIds);

bool runtimeTeamCanLocateTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target);

std::optional<RuntimeCellPoint> runtimeTeamNavalAttackApproach(
    const RuntimeGameSessionState& state, const WorldObjectInstance& actor,
    const WorldObjectInstance& target, const WeaponType& weapon);

bool runtimeTeamActorCanInteractWithTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target);

int issueRuntimeTeamAttack(RuntimeProductionHostState& state,
    const std::vector<int>& objectIds, const WorldObjectInstance& targetObject);

bool runtimeTeamHasActorSpecificAttackTarget(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, const WorldObjectInstance& target);

bool runtimeTeamQuarryMatches(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int quarry);

const WorldObjectInstance* runtimeTeamEnemyTarget(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, int quarry);

void recordRuntimeTeamTargetInteraction(RuntimeGameSessionState& state, int actorId, int targetId);

bool runtimeTeamAircraftNeedReload(const RuntimeGameSessionState& state,
    const std::vector<int>& objectIds);

bool issueRuntimeTeamLoad(RuntimeProductionHostState& state, RuntimeTeamMissionState& mission);

bool unloadRuntimeTeamTransports(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, int argument);

const WorldObjectInstance* runtimeTeamWaypointTarget(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    const RuntimeCellPoint& waypoint,
    bool structureOnly);

const WorldObjectInstance* runtimeTeamBuildingTarget(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    int encodedBuilding,
    bool allied,
    bool approachOnly = false
);

template <typename Predicate>
const WorldObjectInstance* runtimeTeamNearestStructure(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    Predicate predicate);

int issueRuntimeTeamTargetAbility(
    RuntimeProductionHostState& state,
    const std::vector<int>& objectIds,
    const WorldObjectInstance& target);

int runtimeLegacyScriptTicks(
    const RuntimeGameSessionState& state,
    int legacyTicks);

bool runtimeTeamScriptActionSupported(int action);

RuntimeCellPoint runtimeTeamHouseCenter(const RuntimeGameSessionState& state,
    const std::string& owner, RuntimeCellPoint fallback);

RuntimeTeamStepDisposition issueRuntimeTeamGather(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, bool outsideEnemyBase);

RuntimeCellPoint runtimeTeamSuperWeaponCenter(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, const SuperWeaponRule& rule);

RuntimeTeamStepDisposition issueRuntimeTeamOwnedSuperWeapon(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, bool chronosphere, RuntimeCellPoint destination);

RuntimeTeamStepDisposition issueRuntimeTeamScriptStep(
    RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission,
    const RuntimeScriptStep& step);

RuntimeTeamStepCompletion runtimeTeamScriptStepCompletion(
    RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission,
    const RuntimeScriptStep& step);

RuntimeCampaignAiTriggerDefinition* findRuntimeCampaignAiTriggerById(
    RuntimeTriggerState& triggers,
    const std::string& id);

void reportRuntimeCampaignAiTriggerOutcome(
    RuntimeGameSessionState& state,
    const std::string& triggerId,
    bool success);

bool runtimeObjectIsPendingTeamRejoinAfterLimbo(
    const RuntimeGameSessionState& state,
    int objectId);

void noteRuntimeTeamMemberDamaged(RuntimeGameSessionState& state, int memberId, int attackerId);

bool advanceRuntimeTeamRegroup(RuntimeProductionHostState& state, RuntimeTeamMissionState& mission);

bool advanceRuntimeTeamMissions(RuntimeProductionHostState& state);

bool fireRuntimeCreateTeamTrigger(
    RuntimeProductionHostState& state,
    RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action,
    bool allowFallback = true,
    bool markTriggerFired = true
);

std::string runtimeCampaignHouseName(
    const RuntimeGameSessionState& state,
    const std::string& rawHouse);

bool runtimeCampaignHouseIsPlayer(
    const RuntimeGameSessionState& state,
    const std::string& rawHouse);

bool runtimeCampaignHouseMatches(
    const RuntimeGameSessionState& state,
    const std::string& authoredHouse,
    const std::string& objectHouse);

bool runtimeScenarioCellExplored(const RuntimeGameSessionState& state, int x, int y);

std::string runtimeCampaignEventHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event);

std::string runtimeCampaignTriggerHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger);

bool runtimeCampaignTriggerHasTag(
    const RuntimeTriggerDefinition& trigger,
    const std::string& tag);

bool runtimeCampaignObjectAttachedToTrigger(
    const RuntimeTriggerDefinition& trigger,
    const WorldObjectInstance& object);

bool runtimeCampaignCellAttachedToTrigger(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    int cell);

bool runtimeCampaignEventHouseMatchesObject(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    const std::string& objectHouse);

bool runtimeCampaignAttachedObjectEntered(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event);

bool runtimeCampaignAttachedObjectSpied(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event);

bool runtimeCampaignCellEntered(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    bool allowAircraft);

bool runtimeCampaignTaggedObjectWasAttacked(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent* houseEvent);

int runtimeCampaignPowerNetForHouse(
    const RuntimeGameSessionState& state,
    const std::string& house);

bool runtimeCampaignObjectMatchesType(
    const WorldObjectInstance& object,
    const std::string& rawType);

std::string runtimeCampaignIndexedTypeId(
    const RuntimeGameSessionState& state,
    int eventType,
    int index);

bool runtimeCampaignCrossedTaggedLine(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    bool vertical);

int runtimeCampaignObjectCount(
    const RuntimeGameSessionState& state,
    const std::string& house,
    std::optional<WorldObjectKind> kind,
    bool navalOnly,
    bool landOnly,
    const std::string& typeFilter = ""
);

void sampleRuntimeLightingThresholdEvents(RuntimeGameSessionState& state);

bool runtimeCampaignEventSatisfied(
    RuntimeProductionHostState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event);

std::string runtimeCampaignActionHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action);

std::string runtimeCampaignActionArgument(
    const RuntimeTriggerAction& action,
    std::size_t index);

int runtimeCampaignActionInteger(
    const RuntimeTriggerAction& action,
    std::size_t index,
    int fallback = 0
);

std::string runtimeCampaignWaypointId(
    const RuntimeTriggerAction& action,
    std::size_t index);

std::string runtimeCampaignActionSuperWeaponId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action);

std::string runtimeCampaignRulesListId(
    const RuntimeGameSessionState& state,
    const std::string& sectionName,
    const std::string& rawValue);

std::string runtimeCampaignActionWarheadId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action);

void spawnRuntimeCampaignFlash(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    int durationTicks);

std::string runtimeCampaignParticleSystemId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action);

const SuperWeaponRule* runtimeCampaignLightningRule(
    const RuntimeGameSessionState& state);

int runtimeCampaignIonStrikeDelayTicks(const RuntimeGameSessionState& state);

bool fireRuntimeCampaignIonStrike(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    const std::string& owner);

void destroyRuntimeCampaignObject(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const std::string& destroyerHouse,
    const std::string& reason,
    RuntimeObjectExitKind exitKind = RuntimeObjectExitKind::Destroyed,
    bool suppressDeathWeapon = false
);

void destroyRuntimeCampaignObjects(
    RuntimeProductionHostState& state,
    const std::string& house,
    const std::string& destroyerHouse,
    std::optional<WorldObjectKind> kind,
    bool navalOnly,
    bool landOnly);

bool applyRuntimeCampaignAction(
    RuntimeProductionHostState& state,
    RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action);

void snapshotRuntimeCampaignTriggerObservations(RuntimeProductionHostState& state);

template<class Session>
RuntimeTriggerContext runtimeTriggerContext(Session& session);

bool advanceRuntimeTriggers(RuntimeProductionHostState& state);

bool runtimeShouldWaitForMovingBlocker(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& next,
    const WorldObjectInstance& object,
    const std::unordered_set<int>* softBlockerIds,
    int blockedTicks,
    int& blockerObjectId);

bool runtimeGroundDepartureCapacityHoldsTransition(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to);

RuntimeGroundBodyTransitionAdmission runtimeGroundBodyTransitionAdmission(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to, bool deferBody = false
);

bool tryRuntimeLocalAvoidanceMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    bool clearPathOnMove,
    const char* reason);

int runtimeSharedMoveArrivalRadius(
    const RuntimeObjectBehavior& behavior);

bool runtimeMoveCanUsePendingLocalAvoidance(
    const RuntimeObjectBehavior& behavior,
    const WorldObjectInstance& object);

bool runtimeMoveAllowsCrowdBodyRoute(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool runtimeMoveAllowsNearbyCrowdArrival(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

void runtimeSetCrowdInfantryPreviewAtCell(
    const RuntimeGameSessionState& state, WorldObjectInstance& preview,
    const RuntimeCellPoint& cell, int subCell);

bool runtimeCrowdBodyTransitionIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupied,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const WorldObjectInstance& preview, bool actualSource,
    PathMovementMode mode, int& destinationSubCell,
    std::vector<int>* blockingObjectIds =nullptr, bool allowCrushableDestination =false
);

RuntimeSharedMoveCrowdSearch findRuntimeCloserSharedMoveSlot(
    RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

bool prepareRuntimeCloserSharedMoveSlot(
    RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior);

bool runtimeMoveCanFinishAtCrowdedDestination(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

void acceptRuntimePlayerTravelArrival(
    RuntimeGameSessionState& state, const WorldObjectInstance& object);

bool completeRuntimeMoveNearCrowdedGoal(
    RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior);

RuntimeAircraftDockReturnResult queueRuntimeAircraftReturnToDock(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const char* reason);

bool prepareRuntimeFollowMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool normalizeRuntimeLegacyC4Approach(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool prepareRuntimeAirMoveStep(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

const UnitType* runtimeFixedWingProfile(const RuntimeGameSessionState& state,const WorldObjectInstance& object);

void applyRuntimeFixedWingPose(const RuntimeGameSessionState& state,WorldObjectInstance& object);

void reconcileRuntimeAircraftFlights(RuntimeGameSessionState& state);

RuntimeAircraftFlight& runtimeAircraftFlightFor(RuntimeGameSessionState& state,const WorldObjectInstance& object);

std::int64_t runtimeFixedWingVerticalStep(const RuntimeGameSessionState& state,std::int64_t remaining);

void advanceRuntimeFixedWingDepartureHeight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,RuntimeAircraftFlight& f);

bool recoverRuntimeFixedWingHeight(RuntimeGameSessionState& state,WorldObjectInstance& object);

bool runtimeFixedWingLandingReady(RuntimeProductionHostState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,const WorldObjectInstance& host,int dockIndex);

double runtimeFixedWingCruiseInterval(const RuntimeGameSessionState& state,const WorldObjectInstance& object);

bool advanceRuntimeFixedWingToward(RuntimeProductionHostState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,double goalX,double goalY);

std::pair<double,double> runtimeFixedWingMoveGoal(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,const RuntimeObjectBehavior& behavior);

bool advanceRuntimeMoveBehavior(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool runtimeObjectHasOperationalPower(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

int runtimePrismBoostedBeamThickness(int authoredThickness, std::size_t supportCount);

bool runtimePrismTypeMatches(
    const WorldObjectInstance& object,
    const PrismSupportRules& rules);

const WeaponType* runtimePrismSupportRangeWeapon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& tower);

bool runtimePrismPrimaryWeaponMatches(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& tower,
    const WeaponType& weapon);

RuntimeProjectileSourcePosition runtimePrismBeamPoint(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& tower,
    int facing,
    int fallbackHeightLeptons);

void spawnRuntimePrismSupportBeam(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& supporter,
    const WorldObjectInstance& receiver,
    const WorldObjectInstance& attackTarget,
    const WeaponType& rangeWeapon,
    const PrismSupportRules& rules,
    int laserThickness);

RuntimePrismSupportResult applyRuntimePrismSupport(
    RuntimeProductionHostState& state,
    WorldObjectInstance& firingTower,
    const WorldObjectInstance& attackTarget,
    const WeaponType& firingWeapon);

void clearRuntimeTemporalAttack(
    RuntimeProductionHostState& state,
    int attackerObjectId,
    const RuntimeObjectBehavior& behavior,
    const std::string& reason);

bool advanceRuntimeGarrisonAttackBehavior(
    RuntimeProductionHostState& state,
    WorldObjectInstance& host,
    RuntimeObjectBehavior& behavior,
    bool& handled);

bool emitRuntimeMissileExhaust(RuntimeProductionHostState& state, const std::string& animation,
    const std::string& owner, int sourceId, const std::string& missileType,
    const std::array<double, 3>& tail, int elapsedTicks,
    const RuntimeParticleSpriteAttachment& attachment = {});

bool emitRuntimeOrdinaryMissileExhaust(RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& missile, double previousX, double previousY, double previousZ);

bool emitRuntimeSuperWeaponMissileExhaust(RuntimeProductionHostState& state);

std::string runtimeSpawnedMissileImage(const RuntimeGameSessionState& state, const WorldObjectInstance& object);

RuntimeProjectileSourcePosition runtimeMissileDeckPosition(
    const RuntimeProductionHostState& state, const WorldObjectInstance& parent, int slot);

RuntimeInputPixelPoint runtimeMissileDeckOffsetAtCamera(RuntimeProductionHostState& state,
    const WorldObjectInstance& parent, int slot, const RuntimeMapCamera& camera);

RuntimeMissileDeckPresentation runtimeMissileDeckPresentationOffset(RuntimeProductionHostState& state,
    const WorldObjectInstance& parent, int slot);

bool launchRuntimeSpawnedMissileVolley(RuntimeProductionHostState& state,
    const WorldObjectInstance& parent, const WorldObjectInstance& target,
    const WeaponType& weapon, double damageMultiplier);

void applyRuntimeMissilePose(WorldObjectInstance& object, const SpawnedMissileState& m);

void consumeRuntimeSpawnedMissile(RuntimeProductionHostState& state, const SpawnedMissileState& m,
    bool impact);

bool advanceRuntimeSpawnedMissiles(RuntimeProductionHostState& state);

void resolveRuntimeSpawnedMissileRenderSnapshots(RuntimeProductionHostState& state);

bool runtimeDiskDrainRecordActive(const RuntimeGameSessionState& state, int sourceId,
    const RuntimeDiskDrain& drain);

bool runtimeDiskDrainOwnerBlackout(const RuntimeGameSessionState& state, const std::string& owner);

bool runtimeDiskDrainDisablesObject(const RuntimeGameSessionState& state, int objectId);

void cancelRuntimeDiskDrainSource(RuntimeGameSessionState& state, int sourceId);

bool beginRuntimeDiskDrain(RuntimeProductionHostState& state, WorldObjectInstance& source,
    const WorldObjectInstance& target, const WeaponType& weapon, RuntimeObjectBehavior& behavior);

bool advanceRuntimeDiskDrains(RuntimeProductionHostState& state, bool transferMoney);

bool advanceRuntimeKirovBuildingBombApproach(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const WorldObjectInstance& target);

bool advanceRuntimeAttackBehavior(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior);

bool runtimeOwnerIsNeutralForAutoTarget(const std::string& owner);

bool runtimeOwnersAreHostileForAutoTarget(
    const RuntimeGameSessionState& state,
    const std::string& attackerOwner,
    const std::string& targetOwner);

RuntimeComputerAiProfile runtimeComputerAiProfile(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai);

bool runtimeComputerAiOwnerMatches(
    const WorldObjectInstance& object,
    const std::string& owner);

bool runtimeComputerAiHasBuilding(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& typeId);

bool runtimeComputerAiHasFactory(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& factoryType);

bool runtimeComputerAiHasRefinery(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeComputerAiHasRadar(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeComputerAiHasTechCenter(
    const RuntimeGameSessionState& state,
    const std::string& owner);

bool runtimeComputerAiPrerequisiteTokenMet(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& token);

bool runtimeComputerAiPrerequisitesMet(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item);

void applyRuntimeComputerAiModeRules(
    const RuntimeGameSessionState& state,
    RuntimeBuildCatalogItem& item);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiBuildItem(
    const RuntimeGameSessionState& state,
    const std::string& typeId,
    const std::string& owner);

bool runtimeComputerAiItemAllowed(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item);

bool runtimeComputerAiObjectIsMiner(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeComputerAiObjectCanFight(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

bool runtimeComputerAiBuildingCountsAsDefense(const BuildingType& building);

RuntimeComputerAiMetrics runtimeComputerAiMetrics(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai);

std::vector<std::string> runtimeComputerAiStructureCandidates(
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiStructureRole role);

bool runtimeComputerAiBuildingMatchesRole(
    const BuildingType& building,
    RuntimeComputerAiStructureRole role);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectStructureForRole(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiStructureRole role);

RuntimeComputerAiBaseTargets runtimeComputerAiBaseTargets(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai);

std::vector<RuntimeComputerAiStructureRole> runtimeComputerAiBasePlan(
    const RuntimeComputerAiState& ai, const RuntimeComputerAiBaseTargets& targets);

void appendRuntimeComputerAiBasePlanGoals(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile, const RuntimeComputerAiMetrics& metrics,
    std::vector<RuntimeComputerAiStructureRole>& goals);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectStructure(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics);

std::vector<std::string> runtimeComputerAiUnitCandidates(
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiUnitRole role);

bool runtimeComputerAiUnitMatchesRole(
    const RulesDatabase& rules,
    const UnitType& unit,
    RuntimeComputerAiUnitRole role);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectUnitForRole(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiUnitRole role);

std::vector<int> runtimeComputerAiNeutralOilTargets(const RuntimeGameSessionState& state);

bool runtimeComputerAiNeedsOilEngineer(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai);

bool advanceRuntimeComputerAiOilCapture(RuntimeProductionHostState& state,
    const RuntimeComputerAiState& ai);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectNeededHarvester(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiMetrics& metrics);

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectUnit(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics);

int runtimeComputerAiExpectedMaxHealth(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const WorldObjectInstance& object);

void scaleRuntimeComputerAiObjectHealth(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    WorldObjectInstance& object);

std::optional<RuntimeCellPoint> runtimeComputerAiSpawnCellNearAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    int serial);

const WorldObjectInstance* runtimeComputerAiFactoryAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item);

bool runtimeComputerAiBuildStructure(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item);

bool runtimeComputerAiBuildUnit(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item,
    int* producedId = nullptr
);

int runtimeComputerTacticLane(const RuntimeBuildCatalogItem& item);

std::optional<RuntimeProductionSpawn> runtimeComputerTacticAircraftSpawn(
    const RuntimeGameSessionState& state, const std::string& owner,
    const RuntimeBuildCatalogItem& item);

bool completeRuntimeComputerTacticUnit(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item, int& producedId);

bool queueRuntimeComputerTacticProduction(RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item,
    int teamInstanceId = -1);

bool advanceRuntimeComputerTacticProduction(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics);

bool advanceRuntimeComputerAiProduction(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics);

bool runtimeComputerAiActorCanTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target);

const WorldObjectInstance* runtimeComputerAiStrategicTarget(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const std::vector<int>* actorIds = nullptr
);

bool runtimeComputerAiIssueAttack(
    RuntimeProductionHostState& state,
    WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    bool autoAcquired,
    int approachSlot);

bool runtimeComputerAiIssueMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const RuntimeCellPoint& target,
    const std::unordered_set<int>& groupIds);

const WorldObjectInstance* runtimeComputerAiVisibleTargetNear(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    double maximumDistance);

bool advanceRuntimeComputerAiDefense(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile);

bool advanceRuntimeComputerAiTactics(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai);

bool advanceRuntimeComputerAiAttackWave(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile);

bool advanceRuntimeComputerAiRepair(
    RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile);

void updateRuntimeComputerAiBaseCenter(
    const RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai);

bool deployRuntimeComputerAiMcvs(
    RuntimeProductionHostState& state,
    const RuntimeComputerAiState& ai);

void ensureRuntimeComputerAiStates(RuntimeGameSessionState& state);

bool runtimeComputerTacticCompare(int actual, int expected, int comparator);

std::string runtimeComputerTacticEnemy(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai);

bool runtimeComputerTacticCondition(RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeCampaignAiTriggerDefinition& trigger,
    const std::string& enemy);

bool runtimeComputerTacticTemplateBuildable(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeTeamType& team);

void reportRuntimeComputerTacticOutcome(RuntimeGameSessionState& state,
    RuntimeComputerTacticsState& tactics, RuntimeComputerTacticTeam& team);

bool selectRuntimeComputerTactic(RuntimeGameSessionState& state, RuntimeComputerAiState& ai,
    RuntimeComputerTacticsState& tactics);

int runtimeComputerTacticGroup(const RuntimeGameSessionState& state, const RuntimeTeamType& type);

bool runtimeComputerTacticMemberPresent(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, int id);

void refreshRuntimeComputerTacticMissing(RuntimeGameSessionState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type);

bool runtimeComputerTacticMayRecruit(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeComputerTacticTeam& team,
    const RuntimeTeamType& type, const WorldObjectInstance& object);

bool recruitRuntimeComputerTacticMembers(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type);

bool joinRuntimeComputerTacticReinforcements(RuntimeProductionHostState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type);

bool advanceRuntimeComputerTactics(RuntimeProductionHostState& state, RuntimeComputerAiState& ai);

int runtimeComputerCaptureChoice(const ComputerTacticsRules& rules, int credits,
    bool lowPower, double healthRatio, unsigned roll, int teamOverride);

bool advanceRuntimeComputerCaptureFates(RuntimeProductionHostState& state, const std::string& ownerKey);

bool advanceRuntimeComputerAi(RuntimeProductionHostState& state);

bool runtimeCampaignAiCompare(
    int actual,
    int expected,
    int comparator);

int runtimeCampaignAiObjectCount(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const std::string& typeId,
    bool enemies);

bool runtimeCampaignAiEnemyCreditsCondition(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    int expected,
    int comparator);

bool runtimeCampaignAiOwnerChronosphereReady(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house);

bool runtimeCampaignAiConditionMet(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger);

int runtimeCampaignAiDifficultyIndex(const RuntimeGameSessionState& state);

int runtimeCampaignAiTeamInstanceCount(
    const RuntimeGameSessionState& state,
    const std::string& teamTypeId);

bool runtimeCampaignAiTriggerEligible(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger);

std::uint64_t runtimeCampaignAiSelectionHash(const std::string& value);

int runtimeCampaignAiTeamAllocationRatio(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger);

RuntimeCampaignAiTriggerDefinition* selectRuntimeCampaignAiTrigger(
    RuntimeGameSessionState& state,
    RuntimeCampaignHouseProductionState& house);

bool runtimeCampaignAiItemAllowed(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item);

const WorldObjectInstance* runtimeCampaignAiFactoryAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item);

std::optional<int> completeRuntimeCampaignAiUnit(
    RuntimeProductionHostState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item,
    RuntimeCampaignTeamBuild& build);

bool advanceRuntimeCampaignTeamBuild(
    RuntimeProductionHostState& state,
    RuntimeCampaignHouseProductionState& house,
    RuntimeCampaignTeamBuild& build);

bool runtimeCampaignTeamBuildReadyForMission(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignTeamBuild& build);

void dissolveRuntimeCampaignTeamBuild(
    RuntimeProductionHostState& state,
    const RuntimeCampaignTeamBuild& build);

bool advanceRuntimeCampaignAiProduction(RuntimeProductionHostState& state);

bool advanceRuntimeBombSightBehavior(RuntimeProductionHostState& state);

bool advanceRuntimeGuardBehavior(RuntimeProductionHostState& state);

bool runtimeGattlingFiringRunActive(const RuntimeGameSessionState& state, int id);

bool advanceRuntimeGattlingWeaponTimers(RuntimeGameSessionState& state);

bool advanceRuntimeMindControlOverload(RuntimeProductionHostState& state, const SpecialEventSink& sink);

bool advanceRuntimeSquidGrappleWater(RuntimeProductionHostState& state);

void clearRuntimeCarrierAttackPreservingPose(RuntimeProductionHostState& state,
    WorldObjectInstance& child);

std::optional<WorldObjectInstance> runtimeCarrierAircraftAttackTarget(
    const RuntimeGameSessionState& state, const RuntimeObjectBehavior& order, int targetId);

bool synchronizeRuntimeCarrierAircraftOrders(RuntimeProductionHostState& state,
    const SpecialEventSink& sink);

bool runtimeCarrierHasDeckFlight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child);

bool runtimeCarrierHasContinuousAttackFlight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child);

std::array<double, 3> runtimeCarrierDeckMouth(const WorldObjectInstance& parent);

bool runtimeCarrierAircraftHasDeckProtection(const RuntimeGameSessionState& state,
    const WorldObjectInstance& child);

void applyRuntimeCarrierDeckFlightPresentation(const RuntimeGameSessionState& state,
    WorldObjectInstance& child);

void setRuntimeCarrierDeckFlightPose(const RuntimeGameSessionState& state, WorldObjectInstance& child,
    double x, double y, double level, bool moving);

double runtimeCarrierVerticalStep(const RuntimeGameSessionState& state, double remaining);

void beginRuntimeCarrierDeckDeparture(RuntimeGameSessionState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent);

bool advanceRuntimeCarrierDeckDeparture(RuntimeGameSessionState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, const SpecialEventSink& sink);

bool advanceRuntimeCarrierAircraftToward(RuntimeProductionHostState& state, WorldObjectInstance& child,
    RuntimeObjectBehavior& behavior, double goalX, double goalY);

bool advanceRuntimeCarrierDeckReturn(RuntimeProductionHostState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, int slot, const SpecialEventSink& sink);

bool advanceRuntimeSpecialMechanisms(RuntimeProductionHostState& state);

bool runtimeCloakSuppressedBySensors(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target,
    const std::vector<const WorldObjectInstance*>& detectors);

bool advanceRuntimeCloakState(RuntimeProductionHostState& state);

bool advanceRuntimePsychicStatuses(RuntimeProductionHostState& state);

bool runtimeUsesHomingMissileGuidance(const RuntimeProjectileEffect& shot,
    const ProjectileType& type);

bool runtimeUsesMissileStartupAcceleration(const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& shot, const ProjectileType& type);

bool runtimeUsesCompactAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type);

bool runtimeUsesCloseAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type);

double runtimeHomingMissileCourseLockRuleFrames(const RuntimeProjectileEffect& shot,
    const ProjectileType& type);

bool advanceRuntimeHomingMissilePosition(const RuntimeGameSessionState& state,
    RuntimeProjectileEffect& shot, const ProjectileType& type);

bool advanceRuntimeRangedProjectilePosition(
    const RuntimeGameSessionState& state, RuntimeProjectileEffect& projectile, const ProjectileType& type);

void expireRuntimeProjectileAtCurrentPosition(
    RuntimeProjectileEffect& projectile);

bool runtimeProjectileProximityFuseTriggered(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const ProjectileType& projectileType,
    int fixedTickHz);

int runtimeProjectileTrailerSeparationTicks(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile);

bool spawnRuntimeProjectileTrailerEffect(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile);

bool syncRuntimeDiskLaserCharge(RuntimeProductionHostState& state, RuntimeProjectileEffect& shot);

void spawnRuntimeDiskLaserBeam(RuntimeProductionHostState& state, const RuntimeProjectileEffect& charge);

double runtimeSonicWaveTail(const RuntimeProjectileEffect& wave);

const WeaponType* runtimeSonicWaveCurrentWeapon(const RuntimeGameSessionState& state,
    const WorldObjectInstance& source);

bool advanceRuntimeSonicWave(RuntimeProductionHostState& state, RuntimeProjectileEffect& wave);

bool advanceRuntimeEffects(RuntimeProductionHostState& state);

void initializeRuntimeSuperWeaponAnimation(RuntimeProductionHostState& state, RuntimeImpactEffect& animation);

int runtimeSpecialDetailAnimationDurationTicks(RuntimeProductionHostState& state,
    const std::string& animationId);

int runtimeSpecialDetailAnimationMidpointTicks(RuntimeProductionHostState& state,
    const std::string& animationId);

bool runtimeSupportPowerCanPlace(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y);

bool runtimeSupportPowerFatalWaterDestination(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y);

int runtimeSupportPowerParatrooperSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int x, int y, double desiredX, double desiredY);

void consumeRuntimeSuperWeaponRelocationEvent(RuntimeGameSessionState& state,
    WorldObjectInstance& relocated, const SuperWeaponObjectLifecycleEvent& event);

void invalidateRuntimeSuperWeaponWorldQueries(RuntimeProductionHostState& state);

bool flushRuntimeSuperWeaponHostEvents(RuntimeProductionHostState& state);

bool advanceRuntimeDeployTransitions(RuntimeProductionHostState& state);

bool ensureRuntimeAreaFireBehaviors(RuntimeGameSessionState& state);

bool advanceRuntimeIdleVariants(RuntimeProductionHostState& state, bool forceReference = false);

std::optional<RuntimeCellPoint> runtimeGateBehaviorGoal(
    const RuntimeGameSessionState& state,
    const RuntimeObjectBehavior& behavior);

double runtimeGateDistanceToFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    int gateId);

std::vector<RuntimeGatePassageRequest> collectRuntimeGatePassageRequests(
    const RuntimeGameSessionState& state);

void wakeRuntimeGateBlockedPaths(RuntimeGameSessionState& state);

bool advanceRuntimeGates(RuntimeProductionHostState& state);

bool completeRuntimeWorldRefresh(RuntimeProductionHostState& state);

bool advanceRuntimeModExtensions(RuntimeGameSessionState& state);

bool advanceRuntimeSimulation(
    RuntimeProductionHostState& state,
    bool inputDragLightweightMode = false
);

void centerRuntimeCameraOnCellInPixels(
    RuntimeProductionHostState& state,
    const RuntimeInputPixelRect& clientRect,
    const RuntimeCellPoint& cell);

bool runtimeUsesGameplayIntentRouter(const RuntimeProductionHostState& state);

bool runtimeObjectIgnoresDamage(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object);

void updateRuntimeRadarUiState(RuntimeProductionHostState& state, bool radarAvailable);

bool runtimeMenuCsfFourCc(
    const std::vector<unsigned char>& bytes,
    std::size_t offset,
    const char (&tag)[5]);

bool loadRuntimeMenuCsfFile(
    const std::filesystem::path& path,
    std::unordered_map<std::string, std::wstring>& labels);

void ensureRuntimeMenuCsfLoaded(const RuntimeProductionHostState& state);

std::wstring runtimeMenuLabel(
    const RuntimeProductionHostState& state,
    const std::string& key,
    const wchar_t* fallback);

std::string runtimeCampaignLocalizedText(
    const RuntimeProductionHostState& state,
    const std::string& key);

RuntimeStartupReport buildRuntimeSessionStartupReport(
    const RuntimeConfig& config,
    GameWorld* worldOutput,
    TheaterDatabase* theaterOutput,
    ArtDatabase* artOutput,
    RulesDatabase* rulesOutput,
    MapFacts* mapFactsOutput);

RuntimeStartupReport buildRuntimeSessionStartupReport(
    const RuntimeConfig& config,
    GameWorld* worldOutput,
    TheaterDatabase* theaterOutput,
    ArtDatabase* artOutput,
    RulesDatabase* rulesOutput);

std::uint64_t runtimeMatchReplayStateHash(const RuntimeGameSessionState& state,
    std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* trace =nullptr);

std::string serializeRuntimeMatchReplayConfig(const RuntimeConfig& config);

bool deserializeRuntimeMatchReplayConfig(const std::string& text,
    RuntimeConfig& config, std::string& error);

bool writeRuntimeMatchReplayWorld(const RuntimeProductionHostState& state, std::string& data, std::string& error);

bool restoreRuntimeMatchReplayWorld(RuntimeProductionHostState& state, const std::string& configText,
    const std::string& data, int tick, std::uint64_t expectedHash, std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* failureTrace =nullptr,
    std::string* failureState =nullptr);

inline const RuntimeProductionHostState* gRuntimeSnapshotActiveState = nullptr;

inline constexpr const char* runtimeSoundIntentSidebarButtonClick = "SidebarButtonClick";

inline constexpr const char* runtimeSoundIntentSidebarTabClick = "SidebarTabClick";

inline constexpr const char* runtimeSoundIntentBuildQueued = "BuildQueued";

inline constexpr const char* runtimeSoundIntentBuildStarted = "BuildStartedEVA";

inline constexpr const char* runtimeSoundIntentTrainingStarted = "TrainingStartedEVA";

inline constexpr const char* runtimeSoundIntentBuildHeld = "BuildHeld";

inline constexpr const char* runtimeSoundIntentBuildComplete = "BuildComplete";

inline constexpr const char* runtimeSoundIntentBuildCanceled = "BuildCanceled";

inline constexpr const char* runtimeSoundIntentPlacementConfirmed = "PlacementConfirmed";

inline constexpr const char* runtimeSoundIntentCannotDeployHere = "CannotDeployHere";

inline constexpr const char* runtimeSoundIntentSidebarScold = "SidebarScold";

inline constexpr const char* runtimeSoundIntentLowPower = "LowPower";

inline constexpr const char* runtimeSoundIntentPowerOn = "PowerOn";

inline constexpr const char* runtimeSoundIntentPowerOff = "PowerOff";

inline constexpr const char* runtimeSoundIntentInsufficientFunds = "InsufficientFunds";

inline constexpr const char* runtimeSoundIntentUnitReady = "UnitReady";

inline constexpr const char* runtimeSoundIntentRadarOn = "RadarOn";

inline constexpr const char* runtimeSoundIntentRadarOff = "RadarOff";

inline constexpr const char* runtimeSoundIntentNewConstructionOptions =
    "NewConstructionOptions";

inline constexpr const char* runtimeSoundIntentBaseUnderAttack = "BaseUnderAttack";

inline constexpr const char* runtimeSoundIntentOreMinerUnderAttack =
    "OreMinerUnderAttack";

inline constexpr const char* runtimeSoundIntentUnitLost = "UnitLost";

inline constexpr const char* runtimeSoundIntentBuildingCaptured = "BuildingCaptured";

inline constexpr const char* runtimeSoundIntentStructureGarrisoned =
    "StructureGarrisoned";

inline constexpr const char* runtimeSoundIntentStructureGarrisonedSfx =
    "StructureGarrisonedSfx";

inline constexpr const char* runtimeSoundIntentBridgeRepaired = "BridgeRepaired";

inline constexpr const char* runtimeSoundIntentBuildingRepaired = "BuildingRepaired";

inline constexpr const char* runtimeSoundIntentStructureRepaired = "StructureRepaired";

inline constexpr const char* runtimeSoundIntentSellStructure = "SellStructure";

inline constexpr const char* runtimeSoundIntentMenuOpen = "MenuOpen";

inline constexpr const char* runtimeSoundIntentMenuClose = "MenuClose";

inline thread_local concord::audio::RuntimeAsyncSoundDevice* runtimeAsyncSoundOverrideForDiagnostics=nullptr;

inline thread_local RuntimeBuildingDepartureReadOnlyQueryScope*
    runtimeActiveBuildingDepartureReadOnlyQueries = nullptr;

inline thread_local int runtimeComponentMetadataBorrowReferenceOverride = -1;

inline thread_local int runtimeComponentTerrainQueryReferenceOverride = -1;

inline thread_local RuntimePlannerMetadataBorrowObserver runtimePlannerMetadataBorrowObserver = nullptr;

inline thread_local void* runtimePlannerMetadataBorrowObserverContext = nullptr;

inline thread_local int runtimeComponentColdLookupReferenceOverride = -1;

inline thread_local RuntimePlannerColdCellLookups* runtimeActivePlannerColdCellLookups = nullptr;

inline thread_local RuntimePlannerColdLookupObserver runtimePlannerColdLookupObserver = nullptr;

inline thread_local void* runtimePlannerColdLookupObserverContext = nullptr;

inline thread_local int runtimeComponentIndexedTopologyReferenceOverride=-1;

inline thread_local RuntimePlannerIndexedTopology* runtimeActivePlannerIndexedTopology=nullptr;

inline thread_local RuntimePlannerIndexedTopologyObserver runtimePlannerIndexedTopologyObserver=nullptr;

inline thread_local void* runtimePlannerIndexedTopologyObserverContext=nullptr;

inline thread_local RuntimePlannerLandPermissionScope* runtimeActivePlannerLandPermission=nullptr;

inline thread_local const RuntimePlannerInvisibleBuildingScope*
    runtimeActivePlannerInvisibleBuildings = nullptr;

inline thread_local int runtimeComponentEmptyCensusReferenceOverride = -1;

inline thread_local RuntimePlannerEmptyCellCensusScope* runtimeActivePlannerEmptyCellCensus = nullptr;

inline thread_local RuntimePlannerEmptyCensusObserver runtimePlannerEmptyCensusObserver = nullptr;

inline thread_local void* runtimePlannerEmptyCensusObserverContext = nullptr;

inline thread_local RuntimeInfantryAvoidanceSubcellBatch* runtimeActiveInfantryAvoidanceBatch = nullptr;

inline thread_local int runtimeManualFormationMappedReferenceOverride=-1;

inline thread_local int runtimeGroundDepartureBatchReferenceOverride = -1;

inline thread_local const RuntimeGroundDepartureQueryBatch* runtimeActiveGroundDepartureQueryBatch = nullptr;

inline thread_local int runtimePlannerEmptySubcellBooleanReferenceOverride = -1;

inline thread_local RuntimePlannerEmptySubcellBooleanObserver runtimePlannerEmptySubcellBooleanObserver = nullptr;

inline thread_local void* runtimePlannerEmptySubcellBooleanObserverContext = nullptr;

inline thread_local int runtimeCellGateScratchReferenceOverride = -1;

inline thread_local RuntimeCellGateScratchStats* runtimeCellGateScratchStats = nullptr;

inline thread_local int runtimeComponentPassabilityReferenceOverride = -1;

inline thread_local int runtimeComponentDenseReferenceOverride = -1;

inline thread_local int runtimeComponentQueryLeaseReferenceOverride = -1;

inline thread_local RuntimePlannerCellPassabilityScope* runtimeActivePlannerCellPassability = nullptr;

inline thread_local RuntimePlannerPassabilityObserver runtimePlannerPassabilityObserver = nullptr;

inline thread_local void* runtimePlannerPassabilityObserverContext = nullptr;

inline thread_local int runtimeComponentCrushAliasReferenceOverride = -1;

inline thread_local int runtimePathSnapshotMetadataQueryReferenceOverride = -1;

inline thread_local RuntimePathSnapshotConditionalQueryObserver runtimePathSnapshotConditionalQueryObserver=nullptr;

inline thread_local void* runtimePathSnapshotConditionalQueryObserverContext=nullptr;

inline thread_local int runtimeComponentKernelReferenceOverride = -1;

inline thread_local RuntimePlannerKernelLeaseObserver runtimePlannerKernelLeaseObserver = nullptr;

inline thread_local void* runtimePlannerKernelLeaseObserverContext = nullptr;

inline thread_local RuntimePlannerCrushAliasObserver runtimePlannerCrushAliasObserver=nullptr;

inline thread_local void* runtimePlannerCrushAliasObserverContext=nullptr;

inline thread_local RuntimeLoadFaultPointForSmoke runtimeLoadFaultPointForSmoke =
    RuntimeLoadFaultPointForSmoke::None;

inline bool (*runtimeMobileSlaveMiningDiagnosticOverride)(RuntimeProductionHostState&) = nullptr;

inline bool (*runtimeSlaveWorkerMiningDiagnosticOverride)(RuntimeProductionHostState&) = nullptr;

inline bool (*runtimeComputerMiningDiagnosticOverride)(RuntimeProductionHostState&) = nullptr;

inline thread_local int runtimeCameraBoundsPassReferenceOverride = -1;

inline thread_local int runtimeInfantryWaterContextReferenceOverride = -1;

inline thread_local int runtimeInfantryWaterBatchContextReferenceOverride = -1;

inline thread_local std::uint64_t* runtimeInfantryWaterContextConstructionObserver = nullptr;

inline thread_local RuntimeInfantryWaterBehaviorContextScope* runtimeActiveInfantryWaterBehaviorContext = nullptr;

inline thread_local int runtimeChronoMoveTerrainReferenceOverride = -1;

inline thread_local int runtimeStrictSlotVehicleReferenceOverride = -1;

inline thread_local RuntimeStrictSlotVehicleCertificateStats* runtimeStrictSlotVehicleStats=nullptr;

inline thread_local bool runtimeStrictSlotVehicleProofActive=false;

inline thread_local int runtimeMixedLocalComponentReferenceOverride=-1;

inline thread_local int runtimeMixedKnownUnmappedReferenceOverride=-1;

inline thread_local int runtimeMixedBestFrontierClosureReferenceOverride=-1;

inline thread_local RuntimeMixedLocalComponentStats* runtimeMixedLocalComponentStats=nullptr;

inline thread_local const std::vector<std::pair<int,int>>* runtimeMixedLocalComponentExpectedMap=nullptr;

inline thread_local std::size_t runtimeMixedLocalComponentFormationLimit=256;

inline thread_local bool runtimeMixedLocalComponentProofActive=false;

struct RuntimePalettePathMemoScope {
    static inline thread_local RuntimePalettePathMemoScope* active = nullptr;
    RuntimePalettePathMemoScope* previous = active;
    std::string root, theater, path;
    bool valid = false;
    RuntimePalettePathMemoScope() { active = this; }
    ~RuntimePalettePathMemoScope() { active = previous; }
};

class RuntimeHumanHouseScope {
public:
    RuntimeHumanHouseScope(
        RuntimeProductionHostState& state,
        const std::string& commandOwner
    ) : state_(&state) {
        authority_ = findRuntimeHumanHouseAuthority(state, commandOwner);
        if (!authority_ && !state.config.multiplayerActive && runtimeUsesGameplayIntentRouter(state)
            && !state.humanHouseScopeActive
            && runtimeCanonicalHumanHouseOwner(commandOwner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
            state.humanHouseScopeActive=true;valid_=true;return;
        }
        if (authority_ == nullptr || state.humanHouseScopeActive) {
            return;
        }
        if (authority_->localFacade) {
            state.humanHouseScopeActive = true;
            valid_ = true;
            return;
        }
        if (!authority_->remoteEconomy.has_value()
            || !authority_->remoteTechTree.has_value()) {
            authority_ = nullptr;
            return;
        }

        // Perform the only potentially throwing copies before moving either
        // authority record. Once constructed, the scope restores the facade
        // for normal return, early return, and stack unwinding alike.
        savedInteractionStatus_ = state.interactionStatus;
        savedSimulationStatus_ = state.simulationStatus;
        savedLocalEconomy_.emplace(std::move(state.economy));
        savedLocalTechTree_.emplace(std::move(state.techTree));
        savedLocalTechTreeLoaded_ = state.techTreeLoaded;
        state.economy = std::move(*authority_->remoteEconomy);
        state.techTree = std::move(*authority_->remoteTechTree);
        state.techTreeLoaded = authority_->remoteTechTreeLoaded;
        state.humanHouseScopeActive = true;
        state.remoteHumanHouseScopeActive = true;
        remote_ = true;
        valid_ = true;
    }

    RuntimeHumanHouseScope(const RuntimeHumanHouseScope&) = delete;
    RuntimeHumanHouseScope& operator=(const RuntimeHumanHouseScope&) = delete;

    ~RuntimeHumanHouseScope() {
        if (state_ == nullptr || !valid_) {
            return;
        }
        if (remote_) {
            *authority_->remoteEconomy = std::move(state_->economy);
            *authority_->remoteTechTree = std::move(state_->techTree);
            authority_->remoteTechTreeLoaded = state_->techTreeLoaded;
            state_->economy = std::move(*savedLocalEconomy_);
            state_->techTree = std::move(*savedLocalTechTree_);
            state_->techTreeLoaded = savedLocalTechTreeLoaded_;
            state_->interactionStatus = std::move(savedInteractionStatus_);
            state_->simulationStatus = std::move(savedSimulationStatus_);
        }
        state_->remoteHumanHouseScopeActive = false;
        state_->humanHouseScopeActive = false;
    }

    explicit operator bool() const {
        return valid_;
    }

    bool remote() const {
        return remote_;
    }

private:
    RuntimeProductionHostState* state_ = nullptr;
    RuntimeHumanHouseAuthority* authority_ = nullptr;
    std::optional<RuntimeEconomyState> savedLocalEconomy_;
    std::optional<TechTreeReport> savedLocalTechTree_;
    bool savedLocalTechTreeLoaded_ = false;
    std::string savedInteractionStatus_;
    std::string savedSimulationStatus_;
    bool remote_ = false;
    bool valid_ = false;
};

template<class Session>
class RuntimeSessionVisionPhaseOperations final : public RuntimeVisionPhaseOperations {
public:
    explicit RuntimeSessionVisionPhaseOperations(Session& session) : session_(session) {}
    std::string ownerForSlot(std::size_t slotIndex) override { return runtimeSkirmishOwnerForSlot(slotIndex); }
    bool isLandRushMode() override { return runtimeIsLandRushMode(session_); }
    bool ownersAreAllied(const std::string& left, const std::string& right) override { return runtimeOwnersAreAllied(session_,left,right); }
    std::string canonicalHumanOwner(const std::string& owner) override { return runtimeCanonicalHumanHouseOwner(owner); }
    void notifyPublicBuildingLocation(const RuntimeCellPoint& cell) noexcept override {
        session_.playerLastRadarCell = RuntimeMultiplayerGameplayCell{cell.x, cell.y};
        session_.repaintRequested = true;
    }
    void invalidateLocalProjection() noexcept override {
        session_.radarTerrainKey.clear();
        session_.projectedObjectsCacheValid = false;
        session_.repaintRequested = true;
    }
private:
    Session& session_;
};

class RuntimeMoveCommandDiagnosticScope {
    RuntimeProductionHostState* state_ = nullptr;
    const RuntimeFrameDiagnostics* diagnostics_ = nullptr;
    RuntimeFrameMoveCommandInterval::ScopeToken token_;
    std::chrono::steady_clock::time_point started_{};
public:
    explicit RuntimeMoveCommandDiagnosticScope(RuntimeProductionHostState& state) noexcept {
        if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
        if (!state.world.getSummary().loaded || state.runtimeMenuOpen || state.skirmishMatchFinished) return;
        state_ = &state;
        diagnostics_ = state.frameDiagnostics.get();
        token_ = state.diagnosticPendingMoveCommands.enter();
        if (token_.ownsTimer) started_ = std::chrono::steady_clock::now();
    }
    ~RuntimeMoveCommandDiagnosticScope() {
        if (state_ == nullptr) return;
        const bool valid = token_.ownsTimer
            && state_->frameDiagnostics.get() == diagnostics_
            && state_->frameDiagnostics->enabled()
            && state_->world.getSummary().loaded && !state_->runtimeMenuOpen
            && !state_->skirmishMatchFinished
            && state_->diagnosticPendingMoveCommands.generation() == token_.generation;
        const double elapsed = valid ? std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_).count() : 0.0;
        state_->diagnosticPendingMoveCommands.leave(token_, elapsed, valid);
    }
    RuntimeMoveCommandDiagnosticScope(const RuntimeMoveCommandDiagnosticScope&) = delete;
    RuntimeMoveCommandDiagnosticScope& operator=(const RuntimeMoveCommandDiagnosticScope&) = delete;
};

class RuntimeNestedStageDiagnosticScope {
    RuntimeProductionHostState* state_ = nullptr;
    const RuntimeFrameDiagnostics* diagnostics_ = nullptr;
    RuntimeFrameNestedStageInterval::ScopeToken token_;
    std::chrono::steady_clock::time_point started_{};
public:
    RuntimeNestedStageDiagnosticScope(RuntimeProductionHostState& state, std::uint32_t stage) noexcept {
        if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
        if (!state.world.getSummary().loaded || state.runtimeMenuOpen || state.skirmishMatchFinished) return;
        state_ = &state;
        diagnostics_ = state.frameDiagnostics.get();
        token_ = state.diagnosticPendingNestedStages.enter(stage);
        if (token_.ownsTimer) started_ = std::chrono::steady_clock::now();
    }
    ~RuntimeNestedStageDiagnosticScope() {
        if (state_ == nullptr) return;
        const bool valid = token_.ownsTimer
            && state_->frameDiagnostics.get() == diagnostics_
            && state_->frameDiagnostics->enabled()
            && state_->world.getSummary().loaded && !state_->runtimeMenuOpen
            && !state_->skirmishMatchFinished
            && state_->diagnosticPendingNestedStages.generation() == token_.generation;
        const double elapsed = valid ? std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_).count() : 0.0;
        state_->diagnosticPendingNestedStages.leave(token_, elapsed, valid);
    }
    RuntimeNestedStageDiagnosticScope(const RuntimeNestedStageDiagnosticScope&) = delete;
    RuntimeNestedStageDiagnosticScope& operator=(const RuntimeNestedStageDiagnosticScope&) = delete;
};

struct RuntimeSnapshotActiveStateScope {
    explicit RuntimeSnapshotActiveStateScope(const RuntimeProductionHostState& state)
        : previous(gRuntimeSnapshotActiveState) {
        gRuntimeSnapshotActiveState = &state;
    }

    ~RuntimeSnapshotActiveStateScope() {
        gRuntimeSnapshotActiveState = previous;
    }

    const RuntimeProductionHostState* previous = nullptr;
};

struct RuntimeSfxVoiceBackend {
    RuntimeVoiceAudio& audio;
    std::optional<bool> isPlaying(std::size_t channel) const { return audio.isPlaying(runtimeSfxVoiceHandle(channel)); }
    bool stop(std::size_t channel) const { return audio.stop(runtimeSfxVoiceHandle(channel)); }
    void close(std::size_t channel) const { audio.close(runtimeSfxVoiceHandle(channel)); }
    bool open(std::size_t channel, const std::string& path) const { return audio.open(runtimeSfxVoiceHandle(channel), concord::audio::runtimeVoiceUtf8PathFromLegacyNative(path)); }
    bool setMix(std::size_t channel, int gain, int pan) const { return audio.setMix(runtimeSfxVoiceHandle(channel), gain, pan); }
    bool playFromStart(std::size_t channel) const { return audio.playFromStart(runtimeSfxVoiceHandle(channel)); }
    int durationTicks(const std::string& path, int hz) const {
        return runtimeEvaBusyTicksForSample(std::optional<std::string>{path}, hz);
    }
};

class RuntimeBuildingDepartureReadOnlyQueryScope {
    RuntimeBuildingDepartureReadOnlyQueryScope* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const WorldObjectInstance* objectData_;
    std::size_t objectCount_;
    int tick_;
    bool enabled_;
    std::optional<int> cachedHostId_;
    std::size_t materializations_ = 0;
public:
    RuntimeBuildingDepartureReadOnlyQueryScope(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, bool enabled = true)
        : previous_(runtimeActiveBuildingDepartureReadOnlyQueries), state_(&state),
          mover_(&mover), objectData_(state.world.getObjects().data()),
          objectCount_(state.world.getObjects().size()), tick_(state.tick),
          enabled_(enabled && !runtimeHistoryQueryReference()) {
        runtimeActiveBuildingDepartureReadOnlyQueries = this;
    }
    ~RuntimeBuildingDepartureReadOnlyQueryScope() {
        runtimeActiveBuildingDepartureReadOnlyQueries = previous_;
    }
    RuntimeBuildingDepartureReadOnlyQueryScope(const RuntimeBuildingDepartureReadOnlyQueryScope&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope& operator=(const RuntimeBuildingDepartureReadOnlyQueryScope&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope(RuntimeBuildingDepartureReadOnlyQueryScope&&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope& operator=(RuntimeBuildingDepartureReadOnlyQueryScope&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    std::size_t materializations() const noexcept { return materializations_; }
    std::optional<int> find(const RuntimeGameSessionState& state, const WorldObjectInstance& mover) {
        if (!enabled_ || runtimeHistoryQueryReference() || state_ != &state || mover_ != &mover
            || tick_ != state.tick || objectData_ != state.world.getObjects().data()
            || objectCount_ != state.world.getObjects().size()) return std::nullopt;
        if (!cachedHostId_) {
            cachedHostId_ = runtimeStructureDepartureHostIdReference(state, mover);
            ++materializations_;
        }
        return cachedHostId_;
    }
};

class RuntimePaintSecretLabPhaseScope {
public:
    inline static thread_local RuntimePaintSecretLabPhaseScope* active = nullptr;
    explicit RuntimePaintSecretLabPhaseScope(const RuntimeGameSessionState& state,
        bool readonlyInputs = false) noexcept
        : previous_(active), state_(&state), reference_(runtimeSecretLabMemoReference()),
          enabled_(readonlyInputs && !reference_ && !previous_) {
        if (previous_) previous_->invalidate();
        active = this;
    }
    ~RuntimePaintSecretLabPhaseScope() { active = previous_; }
    RuntimePaintSecretLabPhaseScope(const RuntimePaintSecretLabPhaseScope&) = delete;
    RuntimePaintSecretLabPhaseScope& operator=(const RuntimePaintSecretLabPhaseScope&) = delete;
    void invalidate() noexcept {
        enabled_ = false;
        if (memo_) memo_->invalidate();
    }
    const RuntimeSpecialBuildingContext* contextFor(const RuntimeGameSessionState& state) {
        if (!enabled_ || active != this || state_ != &state) return nullptr;
        if (!context_) {
            context_.emplace(makeRuntimeSpecialBuildingContext(state));
            memo_.emplace(*context_, true, reference_);
        }
        return &*context_;
    }
private:
    RuntimePaintSecretLabPhaseScope* previous_;
    const RuntimeGameSessionState* state_;
    bool reference_, enabled_;
    // Memo dies first; the context and its callback closures outlive its use.
    std::optional<RuntimeSpecialBuildingContext> context_;
    std::optional<RuntimeSecretLabReadOnlyMemoScope> memo_;
};

struct RuntimePaintTechTreeMemoScope {
    static inline thread_local RuntimePaintTechTreeMemoScope* active = nullptr;
    RuntimePaintTechTreeMemoScope* previous = active;
    const RuntimeGameSessionState* state = nullptr;
    const RuntimeBuildCatalogItem* catalogData = nullptr;
    std::size_t catalogCount = 0;
    std::vector<std::optional<RuntimeTechTreeDecision>> decisions;
    std::uint64_t hits = 0;
    RuntimePaintTechTreeMemoScope() { active = this; }
    ~RuntimePaintTechTreeMemoScope() { active = previous; }
};

template<class Session>
class RuntimeVehicleArrivalHostQueries final : public RuntimeVehicleArrivalQueries {
public:
    explicit RuntimeVehicleArrivalHostQueries(const Session& session) noexcept : session_(session) {}
    const UnitType* unitType(const WorldObjectInstance& object) const override {
        return findRuntimeUnitType(session_.rules, object);
    }
    PathMovementMode movementMode(const WorldObjectInstance& object) const override {
        return runtimeObjectMovementMode(session_, object);
    }
    bool factoryEgressBypassesArrival(const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) const override {
        return runtimeFactoryEgressBypassesArrival(session_, object, behavior);
    }
private:
    const Session& session_;
};

struct RuntimeIdleVariantPass {
    std::unordered_map<const ArtType*, std::pair<bool, bool>> safetyByArt;
    std::size_t safetyEvaluations = 0;
    std::size_t cacheHits = 0;

    std::pair<bool, bool> safetyFor(const ArtType& art) {
        const auto found = safetyByArt.find(&art);
        if (found != safetyByArt.end()) {
            ++cacheHits;
            return found->second;
        }
        ++safetyEvaluations;
        const std::pair<bool, bool> result{
            runtimeIdleSequenceLooksSafe(art, "Idle1"),
            runtimeIdleSequenceLooksSafe(art, "Idle2")};
        safetyByArt.emplace(&art, result);
        return result;
    }
};

class RuntimePlannerTerrainQueries {
public:
    using TerrainMap = std::unordered_map<int, const TerrainCell*>;
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t minimumTerrainEntries = 4096;

    // A view is obtained explicitly for the exact current state/map. It must
    // remain inside one synchronous read-only transition call, and must not
    // outlive its owner, an invalidate, a reference-scope change, or mutation.
    // Validate ONCE immediately before the original two terrain finds; no
    // earlier movement/repair/diagonal/foundation/crush predicate is replaced.
    class ReadOnlyView {
    public:
        const TerrainCell* terrainAtKey(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) {
                if (const auto* cell = owner_->terrainPointers_[offset]) return cell;
            }
            // Missing and present-null terrain entries both take the original
            // find. No cell is generated and no failed query is remembered.
            const auto found = owner_->terrain_->find(key);
            return found == owner_->terrain_->end() ? nullptr : found->second;
        }

        // Address inspection for the primitive contract, not a replacement
        // for the original missing-metadata computation.
        const RuntimeTerrainInfo* cachedInfoAtKey(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) return owner_->infoPointerAt(offset);
            const auto found = owner_->state_->terrainInfoByCell.find(key);
            return found == owner_->state_->terrainInfoByCell.end()
                ? nullptr : std::addressof(found->second);
        }

        template<class OriginalFallback>
        RuntimeTerrainInfo infoForCellWithFallback(const TerrainCell& terrain,
            OriginalFallback&& originalFallback) const {
            // Original metadata selection is based on the returned TerrainCell
            // coordinates, which need not equal the terrain lookup map key.
            const int key = runtimeCellKey(terrain.x, terrain.y);
            std::size_t offset = 0;
            if (denseOffset(key, offset)) {
                if (const auto* info = owner_->infoPointerAt(offset)) return *info;
            }
            // A miss/out-of-domain cell calls the exact original function on
            // EVERY query. No terrain metadata is eagerly computed or inserted.
            return originalFallback(terrain);
        }

        RuntimeTerrainInfo infoForCell(const TerrainCell& terrain) const {
            return infoForCellWithFallback(terrain, [&](const TerrainCell& cell) {
                return runtimeCachedTerrainInfoForCell(*owner_->state_, cell);
            });
        }

    private:
        friend class RuntimePlannerTerrainQueries;
        explicit ReadOnlyView(const RuntimePlannerTerrainQueries* owner) : owner_(owner) {}
        const RuntimePlannerTerrainQueries* owner_;
    };

    RuntimePlannerTerrainQueries(const RuntimeGameSessionState& state,
        const TerrainMap& terrain, bool enabled = true,
        bool reference = runtimeComponentTerrainQueryReference())
        : state_(&state), terrain_(&terrain), terrainCount_(terrain.size()),
          terrainBuckets_(terrain.bucket_count()),
          infoCount_(state.terrainInfoByCell.size()),
          infoBuckets_(state.terrainInfoByCell.bucket_count()), tick_(state.tick),
          enabled_(enabled && !reference && !runtimeComponentTerrainQueryReference()
              && terrain.size() >= minimumTerrainEntries) {
        // Caller MUST pass enabled=false for original-cost/reference kernels,
        // Air, or planners which do not perform a complete large-map BFS.
        if (!enabled_) return;
        terrainPointers_.assign(denseCellCount, nullptr);
        // The map already owns this exact integer-key/address table. A borrowed
        // table never snapshots RuntimeTerrainInfo values or retains failed queries.
        borrowedInfo_ = !runtimeComponentMetadataBorrowReference()
            && infoCount_ >= minimumTerrainEntries && infoCount_ <= denseCellCount;
        if (borrowedInfo_) {
            infoGeneration_ = state.terrainInfoByCell.generation();
            const bool cold = !state.terrainInfoByCell.denseValid();
            if (cold) {
                RuntimePerfTraceScope metadataTrace("componentTerrainMetadataTable", state.tick);
                borrowedInfoPointers_ = std::addressof(state.terrainInfoByCell.densePointers());
                metadataSourceEntriesExamined_ = infoCount_;
            } else {
                borrowedInfoPointers_ = std::addressof(state.terrainInfoByCell.densePointers());
            }
            infoEntries_ = state.terrainInfoByCell.denseRetainedEntries();
        } else {
            infoPointers_.assign(denseCellCount, nullptr);
        }
        for (const auto& [key, cell] : terrain) {
            std::size_t offset = 0;
            if (cell && denseOffset(key, offset)) {
                terrainPointers_[offset] = cell;
                ++terrainEntries_;
            }
        }
        if (!borrowedInfo_) {
            metadataSourceEntriesExamined_ = infoCount_;
            for (const auto& [key, info] : state.terrainInfoByCell) {
                std::size_t offset = 0;
                if (denseOffset(key, offset)) {
                    infoPointers_[offset] = std::addressof(info);
                    ++infoEntries_;
                }
            }
        }
    }
    RuntimePlannerTerrainQueries(const RuntimePlannerTerrainQueries&) = delete;
    RuntimePlannerTerrainQueries& operator=(const RuntimePlannerTerrainQueries&) = delete;
    RuntimePlannerTerrainQueries(RuntimePlannerTerrainQueries&&) = delete;
    RuntimePlannerTerrainQueries& operator=(RuntimePlannerTerrainQueries&&) = delete;

    ~RuntimePlannerTerrainQueries() {
        if (runtimePlannerMetadataBorrowObserver)
            runtimePlannerMetadataBorrowObserver(runtimePlannerMetadataBorrowObserverContext,
                RuntimePlannerMetadataBorrowSummary{state_, terrain_, enabled_, borrowedInfo_,
                    ownedDenseInfoBytes(), denseInfoBytes(), metadataSourceEntriesExamined_, infoEntries_});
    }
    void invalidate() noexcept { enabled_ = false; }
    std::optional<ReadOnlyView> viewFor(const RuntimeGameSessionState& state,
        const TerrainMap& terrain) const {
        if (!enabled_ || runtimeComponentTerrainQueryReference()
            || state_ != &state || terrain_ != &terrain || tick_ != state.tick
            || terrainCount_ != terrain.size() || terrainBuckets_ != terrain.bucket_count()
            || infoCount_ != state.terrainInfoByCell.size()
            || infoBuckets_ != state.terrainInfoByCell.bucket_count()
            || (borrowedInfo_ && (runtimeComponentMetadataBorrowReference()
                || infoGeneration_ != state.terrainInfoByCell.generation()
                || !state.terrainInfoByCell.denseValid()))) return std::nullopt;
        return ReadOnlyView(this);
    }
    std::size_t denseTerrainBytes() const noexcept {
        return terrainPointers_.size() * sizeof(const TerrainCell*);
    }
    // Accessible table payload, not additional owned bytes. Contracts use the
    // separate ownedDenseInfoBytes() to distinguish original and borrowed routes.
    std::size_t denseInfoBytes() const noexcept {
        return (borrowedInfoPointers_ ? borrowedInfoPointers_->size() : infoPointers_.size())
            * sizeof(const RuntimeTerrainInfo*);
    }
    std::size_t denseTerrainEntries() const noexcept { return terrainEntries_; }
    std::size_t denseInfoEntries() const noexcept { return infoEntries_; }
    bool metadataBorrowed() const noexcept { return borrowedInfo_; }
    std::size_t ownedDenseInfoBytes() const noexcept {
        return infoPointers_.size() * sizeof(const RuntimeTerrainInfo*);
    }
    std::size_t metadataSourceEntriesExamined() const noexcept {
        return metadataSourceEntriesExamined_;
    }

private:
    static bool denseOffset(int key, std::size_t& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000;
        const int y = key / 1000;
        if (x >= static_cast<int>(side) || y >= static_cast<int>(side)) return false;
        offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
        return true;
    }
    const RuntimeGameSessionState* state_;
    const TerrainMap* terrain_;
    std::size_t terrainCount_, terrainBuckets_, infoCount_, infoBuckets_;
    int tick_;
    bool enabled_;
    std::vector<const TerrainCell*> terrainPointers_;
    std::vector<const RuntimeTerrainInfo*> infoPointers_;
    const std::vector<const RuntimeTerrainInfo*>* borrowedInfoPointers_ = nullptr;
    std::uint64_t infoGeneration_ = 0;
    bool borrowedInfo_ = false;
    std::size_t metadataSourceEntriesExamined_ = 0;
    const RuntimeTerrainInfo* infoPointerAt(std::size_t offset) const noexcept {
        return borrowedInfoPointers_ ? (*borrowedInfoPointers_)[offset] : infoPointers_[offset];
    }
    std::size_t terrainEntries_ = 0, infoEntries_ = 0;
};

class RuntimePlannerColdCellLookups {
public:
    using TerrainMap = RuntimePlannerTerrainQueries::TerrainMap;
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t maximumExtraBytes = 2 * denseCellCount * sizeof(void*);
    class ReadOnlyView {
        friend class RuntimePlannerColdCellLookups;
        const RuntimePlannerColdCellLookups* owner_;
        std::size_t offset_;
        int key_;
        ReadOnlyView(const RuntimePlannerColdCellLookups* owner, std::size_t offset, int key)
            : owner_(owner), offset_(offset), key_(key) {}
    public:
        const MapCellFacts* facts() const noexcept {
            ++owner_->factsQueries_;
            return owner_->facts_.empty() ? nullptr : owner_->facts_[offset_];
        }
        const TerrainCell* stateTerrain() const {
            ++owner_->stateTerrainQueries_;
            return owner_->sameTerrain_ ? owner_->terrainView_->terrainAtKey(key_)
                : owner_->stateTerrain_[offset_];
        }
        const TerrainCell* suppliedTerrain() const {
            ++owner_->suppliedTerrainQueries_;
            return owner_->terrainView_->terrainAtKey(key_);
        }
        RuntimeTerrainInfo infoForCell(const TerrainCell& cell) const {
            ++owner_->infoQueries_;
            return owner_->terrainView_->infoForCell(cell);
        }
    };
    RuntimePlannerColdCellLookups(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, const TerrainMap& terrain,
        const std::unordered_map<int, int>& occupancy,
        const RuntimePlannerTerrainQueries& terrainQueries, bool enabled = true)
        : previous_(runtimeActivePlannerColdCellLookups), state_(&state), mover_(&mover),
          terrain_(&terrain), occupancy_(&occupancy), tick_(state.tick),
          sameTerrain_(&terrain == &state.terrainByCell),
          terrainCount_(terrain.size()), terrainBuckets_(terrain.bucket_count()),
          stateTerrainCount_(state.terrainByCell.size()), stateTerrainBuckets_(state.terrainByCell.bucket_count()),
          occupancyCount_(occupancy.size()), occupancyBuckets_(occupancy.bucket_count()),
          infoGeneration_(state.terrainInfoByCell.generation()),
          factsData_(state.mapFacts.cells.data()), factsCount_(state.mapFacts.cells.size()),
          factsLookupCount_(state.mapFacts.cellIndexLookup.size()),
          factsLookupBuckets_(state.mapFacts.cellIndexLookup.bucket_count()),
          factsLoaded_(state.mapFactsLoaded),
          worldCellsData_(state.world.getTerrainCells().data()), worldCellsCount_(state.world.getTerrainCells().size()),
          objectsData_(state.world.getObjects().data()), objectsCount_(state.world.getObjects().size()) {
        // Publish even a disabled owner: it must mask an enabled outer owner.
        // All preparation is const lookup only, and allocation failure falls
        // back before publishing an enabled table.
        if (enabled && !runtimeComponentColdLookupReference()) {
            try { prepare(terrainQueries); } catch (const std::bad_alloc&) {
                terrainView_.reset(); facts_.clear(); stateTerrain_.clear(); enabled_ = false;
            }
        }
        runtimeActivePlannerColdCellLookups = this;
    }
    ~RuntimePlannerColdCellLookups() {
        runtimeActivePlannerColdCellLookups = previous_;
        if (runtimePlannerColdLookupObserver)
            runtimePlannerColdLookupObserver(runtimePlannerColdLookupObserverContext,
                enabled_ ? 1 : 0, fullValidations_, views_, fallbacks_, factsQueries_, stateTerrainQueries_,
                suppliedTerrainQueries_, infoQueries_);
    }
    RuntimePlannerColdCellLookups(const RuntimePlannerColdCellLookups&) = delete;
    RuntimePlannerColdCellLookups& operator=(const RuntimePlannerColdCellLookups&) = delete;
    RuntimePlannerColdCellLookups(RuntimePlannerColdCellLookups&&) = delete;
    RuntimePlannerColdCellLookups& operator=(RuntimePlannerColdCellLookups&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t fullValidations() const noexcept { return fullValidations_; }
    std::size_t extraBytes() const noexcept {
        return (facts_.size() + stateTerrain_.size()) * sizeof(void*);
    }
    std::optional<ReadOnlyView> viewFor(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, const TerrainMap& terrain,
        const std::unordered_map<int, int>& occupancy, const RuntimeCellPoint& point) const {
        if (!enabled_ || runtimeActivePlannerColdCellLookups != this
            || runtimeComponentColdLookupReference() || runtimeComponentTerrainQueryReference()
            || state_ != &state || mover_ != &mover || terrain_ != &terrain || occupancy_ != &occupancy
            || tick_ != state.tick || point.x < 0 || point.y < 0 || point.x >= 512 || point.y >= 512
            || infoGeneration_ != state.terrainInfoByCell.generation()) {
            ++fallbacks_; return std::nullopt;
        }
        ++views_;
        return ReadOnlyView(this, static_cast<std::size_t>(point.y) * side + point.x,
            runtimeCellKey(point.x, point.y));
    }
private:
    // Check every captured shape once after const preparation, then rely on
    // the owned kernel's lexical read-only lifetime just like its query lease.
    // The hot boundary is deliberately not a purported content-version check.
    bool sourceIdentitiesMatch() const noexcept {
        return terrainCount_ == terrain_->size() && terrainBuckets_ == terrain_->bucket_count()
            && stateTerrainCount_ == state_->terrainByCell.size() && stateTerrainBuckets_ == state_->terrainByCell.bucket_count()
            && occupancyCount_ == occupancy_->size() && occupancyBuckets_ == occupancy_->bucket_count()
            && infoGeneration_ == state_->terrainInfoByCell.generation()
            && factsLoaded_ == state_->mapFactsLoaded && factsData_ == state_->mapFacts.cells.data()
            && factsCount_ == state_->mapFacts.cells.size() && factsLookupCount_ == state_->mapFacts.cellIndexLookup.size()
            && factsLookupBuckets_ == state_->mapFacts.cellIndexLookup.bucket_count()
            && worldCellsData_ == state_->world.getTerrainCells().data() && worldCellsCount_ == state_->world.getTerrainCells().size()
            && objectsData_ == state_->world.getObjects().data() && objectsCount_ == state_->world.getObjects().size();
    }
    bool canonicalTerrain(const TerrainMap& map) const {
        for (const auto& [key, cell] : map) {
            if (!cell || cell->x < 0 || cell->y < 0 || cell->x >= 512 || cell->y >= 512
                || key != runtimeCellKey(cell->x, cell->y)
                || !terrainView_->cachedInfoAtKey(key)) return false;
        }
        return true;
    }
    void prepare(const RuntimePlannerTerrainQueries& queries) {
        RuntimePerfTraceScope trace("componentColdLookupBuild",state_->tick);
        terrainView_ = queries.viewFor(*state_, *terrain_);
        if (!terrainView_ || !canonicalTerrain(*terrain_)
            || (!sameTerrain_ && !canonicalTerrain(state_->terrainByCell))) {
            terrainView_.reset(); return;
        }
        if (!sameTerrain_) {
            stateTerrain_.assign(denseCellCount, nullptr);
            for (const auto& [key, cell] : state_->terrainByCell) {
                (void)key;
                stateTerrain_[static_cast<std::size_t>(cell->y) * side + cell->x] = cell;
            }
        }
        if (factsLoaded_ && factsCount_) {
            facts_.assign(denseCellCount, nullptr);
            for (const auto& [index, offset] : state_->mapFacts.cellIndexLookup) {
                if (index >= 0 && static_cast<std::size_t>(index) < denseCellCount && offset < factsCount_)
                    facts_[static_cast<std::size_t>(index)] = std::addressof(state_->mapFacts.cells[offset]);
            }
        }
        ++fullValidations_;
        enabled_ = sourceIdentitiesMatch();
    }
    RuntimePlannerColdCellLookups* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const TerrainMap* terrain_;
    const std::unordered_map<int, int>* occupancy_;
    int tick_;
    bool sameTerrain_, enabled_ = false;
    std::size_t terrainCount_, terrainBuckets_, stateTerrainCount_, stateTerrainBuckets_;
    std::size_t occupancyCount_, occupancyBuckets_;
    std::uint64_t infoGeneration_;
    const MapCellFacts* factsData_;
    std::size_t factsCount_, factsLookupCount_, factsLookupBuckets_;
    bool factsLoaded_;
    const TerrainCell* worldCellsData_;
    std::size_t worldCellsCount_;
    const WorldObjectInstance* objectsData_;
    std::size_t objectsCount_;
    std::optional<RuntimePlannerTerrainQueries::ReadOnlyView> terrainView_;
    std::vector<const MapCellFacts*> facts_;
    std::vector<const TerrainCell*> stateTerrain_;
    std::size_t fullValidations_ = 0;
    mutable std::size_t views_ = 0, fallbacks_ = 0, factsQueries_ = 0;
    mutable std::size_t stateTerrainQueries_ = 0, suppliedTerrainQueries_ = 0, infoQueries_ = 0;
};

class RuntimePlannerIndexedTopology {
public:
    using TerrainMap=std::unordered_map<int,const TerrainCell*>;
    using OccupancyMap=std::unordered_map<int,int>;
    static constexpr std::size_t maximumSlots=512U*512U;
    struct Geometry {
        const TerrainCell* terrain=nullptr;
        int effectiveLevel=0;
        bool water=false,shore=false,bridge=false,ramp=false;
    };
    struct Pair {
        const Geometry* current;
        const Geometry* next;
    };
private:
    RuntimePlannerIndexedTopology* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const TerrainMap* terrain_;
    const OccupancyMap* occupancy_;
    const TerrainCell* cellData_;
    std::size_t cellCount_,terrainCount_,terrainBuckets_;
    std::uint64_t metadataGeneration_;
    int tick_;
    bool enabled_=false;
    int minX_=0,maxX_=-1,minY_=0,maxY_=-1;
    std::size_t width_=0;
    std::vector<Geometry> geometry_;
    Geometry missing_;
    mutable std::size_t pairs_=0,fallbacks_=0;
    static bool canonical(int x,int y) noexcept { return x>=0 && x<512 && y>=0 && y<512; }
    const Geometry* at(const RuntimeCellPoint& point) const noexcept {
        if(point.x<minX_ || point.x>maxX_ || point.y<minY_ || point.y>maxY_) return &missing_;
        return &geometry_[static_cast<std::size_t>(point.y-minY_)*width_+static_cast<std::size_t>(point.x-minX_)];
    }
    void prepare(const RuntimeGameSessionState& state,const TerrainMap& terrain,bool enabled) {
        if(!enabled || runtimeComponentIndexedTopologyReference() || terrain.empty() || terrain.size()>maximumSlots) return;
        RuntimePerfTraceScope geometryBuildTrace("componentIndexedTopologyBuild",state.tick);
        int minX=511,maxX=0,minY=511,maxY=0;
        // First prove the entire input. An anomalous entry disables the WHOLE
        // owner, including cells whose local coordinates happen to look valid.
        for(const auto& [key,cell]:terrain) {
            if(!cell || key<0 || !canonical(cell->x,cell->y)
                || key!=runtimeCellKey(cell->x,cell->y)
                || !state.terrainInfoByCell.contains(key)) return;
            minX=std::min(minX,cell->x); maxX=std::max(maxX,cell->x);
            minY=std::min(minY,cell->y); maxY=std::max(maxY,cell->y);
        }
        minX_=minX; maxX_=maxX; minY_=minY; maxY_=maxY;
        width_=static_cast<std::size_t>(maxX-minX+1);
        const auto area=width_*static_cast<std::size_t>(maxY-minY+1);
        if(area>maximumSlots) return;
        geometry_.resize(area);
        for(const auto& [key,cell]:terrain) {
            const auto found=state.terrainInfoByCell.find(key);
            if(found==state.terrainInfoByCell.end()) return;
            const auto& info=found->second;
            // The high-bridge test is pure, but its +4 must not introduce an
            // overflow in preparation for a cell the original BFS might skip.
            const bool high=runtimeOverlayLooksHighBridge(*cell);
            const auto effective=static_cast<std::int64_t>(cell->level)+(high?4:0);
            if(effective<std::numeric_limits<int>::min()/2 || effective>std::numeric_limits<int>::max()/2) return;
            auto& target=geometry_[static_cast<std::size_t>(cell->y-minY_)*width_+static_cast<std::size_t>(cell->x-minX_)];
            target={cell,static_cast<int>(effective),info.water,info.shore,info.bridge,info.ramp};
        }
        enabled_=true; // Publish only after complete proof/allocation/fill.
    }
public:
    // enabled=false still masks an outer owner. Preparation uses the actual
    // supplied map and cached metadata answers; it never invents metadata or
    // substitutes terrain raw coordinates for a mismatching legacy map key.
    RuntimePlannerIndexedTopology(const RuntimeGameSessionState& state,const WorldObjectInstance& mover,
        const TerrainMap& terrain,const OccupancyMap& occupancy,bool enabled=true)
        :previous_(runtimeActivePlannerIndexedTopology),state_(&state),mover_(&mover),terrain_(&terrain),
          occupancy_(&occupancy),cellData_(state.world.getTerrainCells().data()),
          cellCount_(state.world.getTerrainCells().size()),terrainCount_(terrain.size()),
          terrainBuckets_(terrain.bucket_count()),metadataGeneration_(state.terrainInfoByCell.generation()),tick_(state.tick) {
        prepare(state,terrain,enabled);
        // Install only after preparation returns, so an allocation exception
        // leaves the old TLS owner intact instead of exposing a dead object.
        runtimeActivePlannerIndexedTopology=this;
    }
    ~RuntimePlannerIndexedTopology() {
        runtimeActivePlannerIndexedTopology=previous_;
        if(runtimePlannerIndexedTopologyObserver)
            runtimePlannerIndexedTopologyObserver(runtimePlannerIndexedTopologyObserverContext,
                enabled_,geometry_.size(),pairs_,fallbacks_);
    }
    RuntimePlannerIndexedTopology(const RuntimePlannerIndexedTopology&)=delete;
    RuntimePlannerIndexedTopology& operator=(const RuntimePlannerIndexedTopology&)=delete;
    RuntimePlannerIndexedTopology(RuntimePlannerIndexedTopology&&)=delete;
    RuntimePlannerIndexedTopology& operator=(RuntimePlannerIndexedTopology&&)=delete;
    void invalidate() noexcept { enabled_=false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t retainedSlots() const noexcept { return geometry_.size(); }
    std::size_t retainedBytes() const noexcept { return geometry_.size()*sizeof(Geometry); }
    std::size_t pairs() const noexcept { return pairs_; }
    std::size_t fallbacks() const noexcept { return fallbacks_; }
    static bool allowsGeometry(const Pair& pair,PathMovementMode mode) noexcept {
        const auto& current=*pair.current; const auto& next=*pair.next;
        if(mode!=PathMovementMode::Amphibious && current.water!=next.water
            && !(current.shore || next.shore || current.bridge || next.bridge)) return false;
        const int delta=std::abs(next.effectiveLevel-current.effectiveLevel);
        if(delta>1) return false;
        if(delta>0 && !(current.ramp || next.ramp || current.bridge || next.bridge)) return false;
        return true;
    }
    std::optional<Pair> pairFor(const RuntimeGameSessionState& state,const WorldObjectInstance& mover,
        const TerrainMap& terrain,const OccupancyMap& occupancy,
        const RuntimeCellPoint& current,const RuntimeCellPoint& next) const {
        // The Pair is used synchronously inside this ONE transition only.
        // In-place level/overlay/metadata VALUE edits still require invalidate
        // BEFORE the write; shape checks do not pretend to be content revisions.
        if(!enabled_ || runtimeActivePlannerIndexedTopology!=this
            || runtimeComponentIndexedTopologyReference() || state_!=&state || mover_!=&mover
            || terrain_!=&terrain || occupancy_!=&occupancy || tick_!=state.tick
            || terrainCount_!=terrain.size() || terrainBuckets_!=terrain.bucket_count()
            || cellData_!=state.world.getTerrainCells().data() || cellCount_!=state.world.getTerrainCells().size()
            || metadataGeneration_!=state.terrainInfoByCell.generation()
            || !canonical(current.x,current.y) || !canonical(next.x,next.y)) {
            ++fallbacks_; return std::nullopt;
        }
        ++pairs_; return Pair{at(current),at(next)};
    }
};

class RuntimeNavigationTerrainRendererAdapter final : public RuntimeNavigationTerrainQueryPort {
public:
    RuntimeNavigationTerrainRendererAdapter(RuntimeProductionHostState& state,
        const RuntimeMapRenderContext& metadataContext)
        : state_(state), metadataContext_(metadataContext) {}

    bool theaterLoaded() const override { return state_.theaterLoaded; }
    bool overlayWallRuleForCell(const TerrainCell& terrain) const override {
        return static_cast<bool>(runtimeOverlayWallRuleForCell(state_, terrain));
    }
    bool overlayWallUsesCurrentOverlay(const TerrainCell& terrain) const override {
        return runtimeOverlayWallUsesCurrentOverlay(terrain);
    }
    bool overlayProvidesBridgeDeck(const TerrainCell& terrain) const override {
        return runtimeOverlayProvidesBridgeDeck(terrain);
    }
    bool highBridgeFrameIsDisconnected(const TerrainCell& terrain) const override {
        return runtimeHighBridgeFrameIsDisconnected(terrain);
    }
    bool overlayIsBrokenLowBridgePlaceholder(const TerrainCell& terrain) const override {
        return runtimeOverlayIsBrokenLowBridgePlaceholder(terrain);
    }
    RuntimeNavigationTerrainInfo baseTerrainInfoForCell(const TerrainCell& terrain) const override {
        return runtimeOwnedNavigationTerrainInfo(runtimeTerrainInfoForCell(state_, terrain));
    }
    RuntimeTerrainLandType subTileLandType(const TerrainCell& terrain) override {
        return resolveRuntimeTerrainSubTileLandType(terrain, metadataContext_, state_.renderCache);
    }
    unsigned char subTileRampType(const TerrainCell& terrain) override {
        return resolveRuntimeTerrainSubTileRampType(terrain, metadataContext_, state_.renderCache);
    }
    bool cellHasDisconnectedHighBridgeDeck(const TerrainCell& terrain) const override {
        return runtimeCellHasDisconnectedHighBridgeDeck(state_, terrain);
    }

private:
    RuntimeProductionHostState& state_;
    const RuntimeMapRenderContext& metadataContext_;
};

class RuntimePlannerLandPermissionScope {
    RuntimePlannerLandPermissionScope* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const WorldObjectInstance* objects_;
    std::size_t objectCount_;
    int tick_;
    bool enabled_;
    std::array<unsigned char,12> permissions_{};
    std::size_t materializations_=0, hits_=0;
public:
    RuntimePlannerLandPermissionScope(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover,bool enabled=true)
        : previous_(runtimeActivePlannerLandPermission), state_(&state), mover_(&mover),
          objects_(state.world.getObjects().data()), objectCount_(state.world.getObjects().size()),
          tick_(state.tick), enabled_(enabled && !runtimeComponentKernelReference()) {
        // Disabled nested scopes mask their outer scope as well.
        runtimeActivePlannerLandPermission=this;
    }
    ~RuntimePlannerLandPermissionScope() { runtimeActivePlannerLandPermission=previous_; }
    RuntimePlannerLandPermissionScope(const RuntimePlannerLandPermissionScope&) = delete;
    RuntimePlannerLandPermissionScope& operator=(const RuntimePlannerLandPermissionScope&) = delete;
    RuntimePlannerLandPermissionScope(RuntimePlannerLandPermissionScope&&) = delete;
    RuntimePlannerLandPermissionScope& operator=(RuntimePlannerLandPermissionScope&&) = delete;
    void invalidate() noexcept { enabled_=false; }
    std::size_t materializations() const noexcept { return materializations_; }
    std::size_t hits() const noexcept { return hits_; }
    std::optional<bool> query(const RuntimeGameSessionState& state,
        const RuntimeTerrainInfo& terrain,const WorldObjectInstance& mover) {
        const int ordinal=static_cast<int>(terrain.authoredLandType);
        if (!enabled_ || runtimeComponentKernelReference() || state_!=&state || mover_!=&mover
            || tick_!=state.tick || objects_!=state.world.getObjects().data()
            || objectCount_!=state.world.getObjects().size() || ordinal < -1 || ordinal > 10)
            return std::nullopt;
        auto& permission=permissions_[static_cast<std::size_t>(ordinal+1)];
        if (permission) { ++hits_; return permission==2; }
        const bool allowed=runtimeAuthoredLandTypeAllowsMoverUncached(state,terrain,mover);
        permission=allowed ? 2 : 1; ++materializations_;
        return allowed;
    }
};

class RuntimePlannerInvisibleBuildingScope {
    const RuntimePlannerInvisibleBuildingScope* previous_;
    const RuntimeGameSessionState* state_;
    const void* objectData_;
    std::size_t objectCount_;
    std::unordered_set<int> ids_;
    bool enabled_;
public:
    explicit RuntimePlannerInvisibleBuildingScope(const RuntimeGameSessionState& state,
        bool enabled = true)
        : previous_(runtimeActivePlannerInvisibleBuildings), state_(&state),
          objectData_(state.world.getObjects().data()),
          objectCount_(state.world.getObjects().size()),
          enabled_(enabled && !runtimeHistoryQueryReference()
              && !runtimeFrameScalingReferenceMovement()) {
        if (enabled_) {
            // Dead invisible structures remain in the original context too.
            for (const auto& object : state.world.getObjects())
                if (runtimeInvisibleMapBuilding(&state.rules, object))
                    ids_.insert(object.instanceId);
            runtimeActivePlannerInvisibleBuildings = this;
        }
    }
    ~RuntimePlannerInvisibleBuildingScope() {
        if (enabled_) runtimeActivePlannerInvisibleBuildings = previous_;
    }
    RuntimePlannerInvisibleBuildingScope(const RuntimePlannerInvisibleBuildingScope&) = delete;
    RuntimePlannerInvisibleBuildingScope& operator=(const RuntimePlannerInvisibleBuildingScope&) = delete;
    const std::unordered_set<int>* find(const RuntimeGameSessionState& state) const {
        return enabled_ && state_ == &state
            && objectData_ == state.world.getObjects().data()
            && objectCount_ == state.world.getObjects().size() ? &ids_ : nullptr;
    }
};

class RuntimePlannerEmptyCellCensusScope {
    RuntimePlannerEmptyCellCensusScope* previous_;
    const RuntimeGameSessionState* state_;
    int tick_;
    bool enabled_ = false;
    std::vector<std::uint64_t> bits_;
    std::size_t marked_ = 0, rectangleWork_ = 0;
    mutable std::size_t proofs_ = 0, hardProofs_ = 0, availabilityProofs_ = 0;
    static constexpr int side_ = 512;
    static constexpr std::size_t maximumRectangleWork_ = 1048576;
    bool markRectangle(int x, int y, int rawWidth, int rawHeight) {
        const std::int64_t width = std::max(1,rawWidth), height = std::max(1,rawHeight);
        if (x < 0 || y < 0 || x >= side_ || y >= side_
            || width > side_ || height > side_
            || static_cast<std::int64_t>(x) + width > side_
            || static_cast<std::int64_t>(y) + height > side_) return false;
        const auto work=static_cast<std::size_t>(width*height);
        if (work>maximumRectangleWork_-rectangleWork_) return false;
        rectangleWork_+=work;
        for (int row=y; row<static_cast<std::int64_t>(y)+height; ++row)
            for (int column=x; column<static_cast<std::int64_t>(x)+width; ++column) {
                const auto index = static_cast<std::size_t>(row)*side_+column;
                auto& word = bits_[index/64]; const auto mask = std::uint64_t{1}<<(index%64);
                if (!(word&mask)) { word|=mask; ++marked_; }
            }
        return true;
    }
    bool prepare(const RuntimeGameSessionState& state) {
        RuntimePerfTraceScope trace("componentEmptyCensusBuild",state.tick);
        bits_.assign(static_cast<std::size_t>(side_)*side_/64,std::uint64_t{0});
        const auto poseContext=runtimeGroundPoseContext(state);
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive) continue;
            bool poseValid=true;
            forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
                poseValid=markRectangle(held.x,held.y,1,1) && poseValid;
            },&poseContext);
            if (!poseValid) return false;
            if (!markRectangle(object.x,object.y,1,1)) return false;
            const auto origin = runtimeCellPointForObject(object);
            if (!markRectangle(origin.x,origin.y,1,1)) return false;
            if (runtimeObjectUsesStructureFootprint(state,object)) {
                const auto footprint = runtimeStructureFootprintForObject(state,object);
                if (!markRectangle(object.x,object.y,footprint.width,footprint.height)
                    || !markRectangle(origin.x,origin.y,footprint.width,footprint.height)) return false;
            }
        }
        for (const auto& terrain : state.world.getTerrainObjects()) {
            if (!terrain.alive) continue;
            if (!markRectangle(terrain.x,terrain.y,terrain.footprintWidth,terrain.footprintHeight)) return false;
        }
        return true;
    }
public:
    explicit RuntimePlannerEmptyCellCensusScope(const RuntimeGameSessionState& state, bool enabled=true)
        : previous_(runtimeActivePlannerEmptyCellCensus), state_(&state), tick_(state.tick) {
        enabled_ = enabled && !runtimeComponentEmptyCensusReference() && prepare(state);
        // Publish only after all potentially throwing allocations/footprint
        // queries. Unsupported/disabled scopes still mask an outer census.
        runtimeActivePlannerEmptyCellCensus = this;
    }
    ~RuntimePlannerEmptyCellCensusScope() {
        runtimeActivePlannerEmptyCellCensus = previous_;
        if (runtimePlannerEmptyCensusObserver)
            runtimePlannerEmptyCensusObserver(runtimePlannerEmptyCensusObserverContext,
                enabled_,marked_,hardProofs_,availabilityProofs_);
    }
    RuntimePlannerEmptyCellCensusScope(const RuntimePlannerEmptyCellCensusScope&) = delete;
    RuntimePlannerEmptyCellCensusScope& operator=(const RuntimePlannerEmptyCellCensusScope&) = delete;
    RuntimePlannerEmptyCellCensusScope(RuntimePlannerEmptyCellCensusScope&&) = delete;
    RuntimePlannerEmptyCellCensusScope& operator=(RuntimePlannerEmptyCellCensusScope&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t marked() const noexcept { return marked_; }
    std::size_t proofs() const noexcept { return proofs_; }
    std::size_t hardProofs() const noexcept { return hardProofs_; }
    std::size_t availabilityProofs() const noexcept { return availabilityProofs_; }
    std::size_t rectangleWork() const noexcept { return rectangleWork_; }
    std::size_t bytes() const noexcept { return bits_.size()*sizeof(std::uint64_t); }
    bool provesEmpty(const RuntimeGameSessionState& state, const RuntimeCellPoint& cell,
        RuntimePlannerEmptyCellQuery kind=RuntimePlannerEmptyCellQuery::Probe) const {
        if (!enabled_ || runtimeActivePlannerEmptyCellCensus != this || state_ != &state
            || tick_ != state.tick || runtimeComponentEmptyCensusReference()
            || cell.x < 0 || cell.y < 0 || cell.x >= side_ || cell.y >= side_) return false;
        const auto index = static_cast<std::size_t>(cell.y)*side_+cell.x;
        if (bits_[index/64] & (std::uint64_t{1}<<(index%64))) return false;
        ++proofs_;
        if (kind==RuntimePlannerEmptyCellQuery::HardBlocker) ++hardProofs_;
        else if (kind==RuntimePlannerEmptyCellQuery::Availability) ++availabilityProofs_;
        return true;
    }
};

struct RuntimeInfantryAvoidanceSubcellBatch {
    const RuntimeGameSessionState* state;
    const WorldObjectInstance* mover;
    std::array<RuntimeCellPoint, 8> cells;
    std::vector<std::vector<int>> available;
    RuntimeInfantryAvoidanceSubcellBatch* previous;
    int materializations = 0;
    bool unsupported = false;
    RuntimeInfantryAvoidanceSubcellBatch(const RuntimeGameSessionState& value,
        const WorldObjectInstance& object, RuntimeCellPoint current)
        : state(&value), mover(&object), cells(runtimeNeighborsOf(current)),
          previous(runtimeActiveInfantryAvoidanceBatch) {
        runtimeActiveInfantryAvoidanceBatch = this;
    }
    ~RuntimeInfantryAvoidanceSubcellBatch() { runtimeActiveInfantryAvoidanceBatch = previous; }
    RuntimeInfantryAvoidanceSubcellBatch(const RuntimeInfantryAvoidanceSubcellBatch&) = delete;
    RuntimeInfantryAvoidanceSubcellBatch& operator=(const RuntimeInfantryAvoidanceSubcellBatch&) = delete;

    const std::vector<int>* find(const RuntimeGameSessionState& value,
        const WorldObjectInstance& object, RuntimeCellPoint cell, RuntimeGatePassageMode mode) {
        if (unsupported || state != &value || mover != &object || object.kind != WorldObjectKind::Infantry
            || mode != RuntimeGatePassageMode::Commit) return nullptr;
        const auto found = std::find(cells.begin(), cells.end(), cell);
        if (found == cells.end()) return nullptr;
        if (available.empty()) {
            // The batch API expands terrain foundations. Keep unusual maps on
            // the targeted scalar query instead of allocating an unbounded map.
            std::int64_t terrainFootprintCells = 0;
            for (const auto& terrain : value.world.getTerrainObjects()) {
                if (!terrain.alive || terrain.health <= 0) continue;
                const std::int64_t width = std::max(1, terrain.footprintWidth);
                const std::int64_t height = std::max(1, terrain.footprintHeight);
                if (width > 4096 || height > 4096
                    || width * height > 4096 - terrainFootprintCells
                    || static_cast<std::int64_t>(terrain.x) + width - 1 > std::numeric_limits<int>::max()
                    || static_cast<std::int64_t>(terrain.y) + height - 1 > std::numeric_limits<int>::max()) {
                    unsupported = true;
                    return nullptr;
                }
                terrainFootprintCells += width * height;
            }
            std::vector<RuntimeSubcellLocation> destinations;
            destinations.reserve(cells.size());
            for (const auto& destination : cells)
                destinations.push_back(runtimeSubcellLocationForCell(value, destination));
            available = value.subcells.availableSubCellsBatch(value.world.getObjects(),
                value.world.getTerrainObjects(), destinations, {object.instanceId},
                runtimeGateSubcellContext(value, &object, mode));
            ++materializations;
        }
        return &available[static_cast<std::size_t>(found - cells.begin())];
    }
};

class RuntimeManualFormationMappedFilter {
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    bool restricted_=false;
    bool hasObserver_=false;
public:
    RuntimeManualFormationMappedFilter(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover,const UnitType* knownUnit=nullptr,
        bool enabled=!runtimeManualFormationMappedReference())
        :state_(&state),mover_(&mover) {
        if(!enabled || !state.visionEnabled) return;
        const UnitType* unit=knownUnit?knownUnit:findRuntimeUnitType(state.rules,mover);
        restricted_=unit && !unit->moveToShroud;
        if(restricted_) hasObserver_=state.vision.hasObserver(mover.owner);
    }
    const RuntimeManualFormationMappedFilter* restriction() const noexcept {
        return restricted_?this:nullptr;
    }
    bool allows(const RuntimeCellPoint& cell) const {
        // Retail original predicate uses owner Mapped, even with NoFog.
        return !restricted_ || (hasObserver_
            && state_->vision.isCellExploredBy(mover_->owner,cell.x,cell.y));
    }
};

struct RuntimeAirTraffic {
    std::unordered_set<int> occupied;
    std::unordered_set<int> diagonals;

    bool canStep(const RuntimeCellPoint& from, const RuntimeCellPoint& to) const {
        return to.x >= 0 && to.y >= 0 && to.x < 512 && to.y < 512
            && occupied.find(runtimeCellKey(to.x, to.y)) == occupied.end()
            && (from.x == to.x || from.y == to.y
                || diagonals.find(runtimeAirDiagonalKey(from, to)) == diagonals.end());
    }
};

struct RuntimeAirMoveContinuation {
    bool active = false;
    bool visible = false;
    double x = 0, y = 0, level = 0;
    RuntimeObjectBehavior segment;

    RuntimeAirMoveContinuation(const WorldObjectInstance& object, const RuntimeObjectBehavior* previous) {
        active = object.airborne && previous != nullptr && previous->moveCooldownTicks > 0;
        visible = active && object.renderMoveActive && previous->renderMoveTicksRemaining > 0;
        if (active) { x = object.renderX; y = object.renderY; level = object.renderLevel; segment = *previous; }
    }

    void restore(WorldObjectInstance& object, RuntimeObjectBehavior& behavior) const {
        if (!active) return;
        behavior.moveCooldownTicks = segment.moveCooldownTicks;
        behavior.moveSegmentFrom = segment.moveSegmentFrom;
        behavior.moveIntervalRemainderQ16 = segment.moveIntervalRemainderQ16;
        if (!visible) return;
        behavior.renderFrom = segment.renderFrom;
        behavior.renderTo = segment.renderTo;
        behavior.renderFromOffsetX = segment.renderFromOffsetX;
        behavior.renderFromOffsetY = segment.renderFromOffsetY;
        behavior.renderToOffsetX = segment.renderToOffsetX;
        behavior.renderToOffsetY = segment.renderToOffsetY;
        behavior.renderArrivalDistanceAfter = segment.renderArrivalDistanceAfter;
        behavior.renderFromLevel = segment.renderFromLevel;
        behavior.renderToLevel = segment.renderToLevel;
        behavior.renderFromOnBridge = segment.renderFromOnBridge;
        behavior.renderToOnBridge = segment.renderToOnBridge;
        behavior.renderMoveTicksTotal = segment.renderMoveTicksTotal;
        behavior.renderMoveTicksRemaining = segment.renderMoveTicksRemaining;
        object.hasRenderPosition = object.renderMoveActive = true;
        object.renderX = x; object.renderY = y; object.renderLevel = level;
        object.renderOnBridge = false;
    }
};

class RuntimeGroundDepartureQueryBatch {
    const RuntimeGroundDepartureQueryBatch* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    RuntimeCellPoint origin_;
    const void* objectData_;
    std::size_t objectCount_, behaviorCount_;
    int tick_;
    bool enabled_ = false, air_ = false, moverInfantry_ = false;
    std::array<int, 9> infantryDepartures_{};
    std::array<int, 9> infantryResidents_{};
    std::array<bool, 9> nonInfantryDepartures_{};
    std::array<bool, 9> pendingOmniSegments_{};
    std::array<bool, 9> physicalInfantry_{};
    std::array<bool, 9> physicalVehicles_{};
    mutable std::uint64_t queryHits_ = 0;
    std::uint64_t behaviorVisits_ = 0, residentVisits_ = 0;

    int index(const RuntimeCellPoint& cell) const {
        const auto dx = static_cast<std::int64_t>(cell.x) - origin_.x;
        const auto dy = static_cast<std::int64_t>(cell.y) - origin_.y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return -1;
        return static_cast<int>((dy + 1) * 3 + dx + 1);
    }
public:
    RuntimeGroundDepartureQueryBatch(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, RuntimeCellPoint origin)
        : previous_(runtimeActiveGroundDepartureQueryBatch), state_(&state), mover_(&mover), origin_(origin),
          objectData_(state.world.getObjects().data()), objectCount_(state.world.getObjects().size()),
          behaviorCount_(state.behaviors.size()), tick_(state.tick) {
        enabled_ = !runtimeGroundDepartureBatchReference() && !runtimeFrameScalingReferenceMovement();
        if (enabled_) {
            air_ = runtimeObjectMovementMode(state, mover) == PathMovementMode::Air;
            moverInfantry_ = mover.kind == WorldObjectKind::Infantry;
            if (!air_) {
                for (const auto& [id, behavior] : state.behaviors) {
                    ++behaviorVisits_;
                    if (id == mover.instanceId) continue;
                    const auto* object = findObjectById(state, id);
                    if (object == nullptr || !object->alive || object->health <= 0
                        || object->kind == WorldObjectKind::Structure
                        || runtimeObjectOffGroundByMagBeam(state, id)
                        || runtimeObjectMovementMode(state, *object) == PathMovementMode::Air) continue;
                    if (runtimeOmniCrushPendingSegment(state, *object, behavior)) {
                        const auto hold = [&](RuntimeCellPoint cell) {
                            const int held = index(cell);
                            if (held >= 0) pendingOmniSegments_[held] = true;
                        };
                        hold(behavior.renderFrom); hold(behavior.renderTo);
                        if (behavior.renderFrom.x != behavior.renderTo.x
                            && behavior.renderFrom.y != behavior.renderTo.y) {
                            hold({behavior.renderFrom.x, behavior.renderTo.y});
                            hold({behavior.renderTo.x, behavior.renderFrom.y});
                        }
                        continue;
                    }
                    if (behavior.moveCooldownTicks <= 0
                        || runtimeCellPointForObject(*object) == behavior.moveSegmentFrom) continue;
                    const int destination = index(behavior.moveSegmentFrom);
                    if (destination < 0) continue;
                    if (object->kind == WorldObjectKind::Infantry) ++infantryDepartures_[destination];
                    else nonInfantryDepartures_[destination] = true;
                }
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const RuntimeCellPoint cell{origin.x+dx,origin.y+dy};
                    if(runtimeGroundPoseSpatialIndexIsCurrent(state)
                        && (cell.x<0 || cell.y<0 || cell.x>=512 || cell.y>=512)) continue;
                    forEachRuntimeAdditionalGroundPoseOccupantAtCell(state,cell,[&](const auto& object,[[maybe_unused]] const auto& held) {
                        if (object.instanceId==mover.instanceId) return;
                        const int destination=index({held.x,held.y});
                        if (destination<0) return;
                        if (object.kind==WorldObjectKind::Infantry) physicalInfantry_[destination]=true;
                        else physicalVehicles_[destination]=true;
                    });
                }
                // The scalar resident scan is conditional on a departure and
                // intentionally has no mag-beam/movement-mode/owner filter.
                if (moverInfantry_ && std::any_of(infantryDepartures_.begin(), infantryDepartures_.end(),
                    [](int count) { return count > 0; })) {
                    for (const auto& other : state.world.getObjects()) {
                        ++residentVisits_;
                        if (!other.alive || other.health <= 0 || other.airborne
                            || other.kind != WorldObjectKind::Infantry || other.instanceId == mover.instanceId) continue;
                        const int destination = index(runtimeCellPointForObject(other));
                        if (destination >= 0) ++infantryResidents_[destination];
                    }
                }
            }
        }
        runtimeActiveGroundDepartureQueryBatch = this;
    }
    ~RuntimeGroundDepartureQueryBatch() { runtimeActiveGroundDepartureQueryBatch = previous_; }
    RuntimeGroundDepartureQueryBatch(const RuntimeGroundDepartureQueryBatch&) = delete;
    RuntimeGroundDepartureQueryBatch& operator=(const RuntimeGroundDepartureQueryBatch&) = delete;
    void invalidate() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    std::uint64_t queryHits() const { return queryHits_; }
    std::uint64_t behaviorVisits() const { return behaviorVisits_; }
    std::uint64_t residentVisits() const { return residentVisits_; }
    std::optional<bool> query(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, const RuntimeCellPoint& cell) const {
        if (!enabled_ || runtimeGroundDepartureBatchReference()
            || state_ != &state || mover_ != &mover || tick_ != state.tick
            || objectData_ != state.world.getObjects().data() || objectCount_ != state.world.getObjects().size()
            || behaviorCount_ != state.behaviors.size()) return std::nullopt;
        const int destination = index(cell);
        if (destination < 0) return std::nullopt;
        ++queryHits_;
        if (air_) return false;
        if (pendingOmniSegments_[destination]) return true;
        if (moverInfantry_) return physicalVehicles_[destination] || nonInfantryDepartures_[destination]
            || (infantryDepartures_[destination] > 0
                && infantryDepartures_[destination] + infantryResidents_[destination] >= 3);
        return physicalInfantry_[destination] || physicalVehicles_[destination]
            || infantryDepartures_[destination] > 0;
    }
};

class RuntimePlannerVisitedCellKeys {
    static constexpr int maximumBitKey_ = 1048575;
    int lastBitKey_ = -1;
    std::size_t bitEntries_ = 0;
    std::vector<std::uint64_t> bits_;
    std::unordered_set<int> outside_;
public:
    explicit RuntimePlannerVisitedCellKeys(const std::unordered_map<int,const TerrainCell*>& terrain) {
        if (runtimeComponentDenseReference() || runtimeComponentPassabilityReference()) {
            outside_.reserve(terrain.size()); // Original C12 visited allocation.
            return;
        }
        for (const auto& [key, cell] : terrain) {
            (void)cell;
            if (key < 0) continue;
            lastBitKey_ = std::max(lastBitKey_, std::min(key, maximumBitKey_));
            if (lastBitKey_ == maximumBitKey_) break;
        }
        if (lastBitKey_ < 0) { outside_.reserve(terrain.size()); return; }
        bits_.assign(static_cast<std::size_t>(lastBitKey_) / 64 + 1, std::uint64_t{0});
    }
    bool contains(int key) const {
        if (key >= 0 && key <= lastBitKey_)
            return (bits_[static_cast<std::size_t>(key) / 64]
                & (std::uint64_t{1} << (static_cast<unsigned>(key) % 64))) != 0;
        return outside_.find(key) != outside_.end();
    }
    bool insert(int key) {
        if (key >= 0 && key <= lastBitKey_) {
            auto& word = bits_[static_cast<std::size_t>(key) / 64];
            const auto mask = std::uint64_t{1} << (static_cast<unsigned>(key) % 64);
            if (word & mask) return false;
            word |= mask; ++bitEntries_; return true;
        }
        return outside_.insert(key).second;
    }
    std::size_t size() const { return bitEntries_ + outside_.size(); }
    std::size_t bitmapBytes() const { return bits_.size() * sizeof(std::uint64_t); }
    std::size_t unorderedEntries() const { return outside_.size(); }
};

struct RuntimePlannerPassabilityKey {
    int x = 0, y = 0, allowed = -1;
    bool crush = false, hasMode = false;
    PathMovementMode mode = PathMovementMode::Ground;
    bool operator==(const RuntimePlannerPassabilityKey&) const = default;
};

struct RuntimePlannerPassabilityKeyHash {
    std::size_t operator()(const RuntimePlannerPassabilityKey& key) const noexcept {
        std::size_t value = std::hash<int>{}(key.x);
        const auto mix = [&](std::size_t part) {
            value ^= part + std::size_t{0x9e3779b9U} + (value << 6U) + (value >> 2U);
        };
        mix(std::hash<int>{}(key.y)); mix(std::hash<int>{}(key.allowed));
        mix(key.crush); mix(key.hasMode); mix(static_cast<std::size_t>(key.mode));
        return value;
    }
};

class RuntimePlannerCellPassabilityScope {
    friend class RuntimePlannerCellPassabilityReadOnlyLease;
    RuntimePlannerCellPassabilityScope* previous_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const std::unordered_map<int, const TerrainCell*>* terrain_;
    const std::unordered_map<int, int>* occupancy_;
    const void *objects_, *cells_, *terrainObjects_, *mapFacts_;
    std::size_t objectCount_, cellCount_, terrainObjectCount_, mapFactCount_;
    std::size_t terrainCount_, occupancyCount_, terrainBuckets_, occupancyBuckets_;
    int tick_;
    bool enabled_;
    std::size_t capacity_;
    std::unordered_map<RuntimePlannerPassabilityKey, bool, RuntimePlannerPassabilityKeyHash> values_;
    std::uint64_t hits_ = 0, misses_ = 0, fallbacks_ = 0;
    // Four exact common keys per raw coordinate: optional mode presence and
    // crush flag. Two bits hold unknown/false/true; at most 256 KiB per scope.
    static constexpr std::size_t denseCellLimit_ = 262144;
    bool densePermitted_, denseAttempted_ = false;
    int denseMinX_ = 0, denseMaxX_ = -1, denseMinY_ = 0, denseMaxY_ = -1;
    std::size_t denseWidth_ = 0, denseEntries_ = 0;
    std::uint64_t denseHits_ = 0;
    PathMovementMode denseMoverMode_ = PathMovementMode::Ground;
    std::vector<std::uint8_t> denseValues_;

    static bool canonicalDensePoint(int x, int y) {
        return x >= 0 && x < 1000 && y >= 0
            && y <= (std::numeric_limits<int>::max() - x) / 1000;
    }
    void prepareDenseDomain() {
        denseAttempted_ = true;
        std::size_t canonicalCount = 0;
        int minX = 999, maxX = -1, minY = std::numeric_limits<int>::max(), maxY = -1;
        for (const auto& [key, cell] : *terrain_) {
            (void)key;
            if (!cell || !canonicalDensePoint(cell->x, cell->y)) continue;
            ++canonicalCount;
            minX = std::min(minX, cell->x); maxX = std::max(maxX, cell->x);
            minY = std::min(minY, cell->y); maxY = std::max(maxY, cell->y);
        }
        if (!canonicalCount) return;
        const auto width = static_cast<std::uint64_t>(maxX) - minX + 1;
        const auto height = static_cast<std::uint64_t>(maxY) - minY + 1;
        const auto area = width * height;
        // Sparse/oversized domains retain the old bounded unordered memo.
        if (area > denseCellLimit_ || (area + 3) / 4 > canonicalCount) return;
        denseMinX_ = minX; denseMaxX_ = maxX; denseMinY_ = minY; denseMaxY_ = maxY;
        denseWidth_ = static_cast<std::size_t>(width);
        denseMoverMode_ = runtimeObjectMovementMode(*state_, *mover_);
        denseValues_.assign(static_cast<std::size_t>(area), std::uint8_t{0});
    }
    bool denseSlot(const RuntimePlannerPassabilityKey& key, std::size_t& offset,
        unsigned& shift) {
        if (!densePermitted_ || key.allowed != -1 || !canonicalDensePoint(key.x, key.y)) return false;
        if (!denseAttempted_) prepareDenseDomain();
        if (denseValues_.empty() || key.x < denseMinX_ || key.x > denseMaxX_
            || key.y < denseMinY_ || key.y > denseMaxY_
            || (key.hasMode ? key.mode != denseMoverMode_ : key.mode != PathMovementMode::Ground)) return false;
        offset = static_cast<std::size_t>(key.y - denseMinY_) * denseWidth_
            + static_cast<std::size_t>(key.x - denseMinX_);
        shift = ((key.hasMode ? 2U : 0U) + (key.crush ? 1U : 0U)) * 2U;
        return true;
    }
    bool explicitCapacityAvailable() const {
        // An explicitly small fixture/caller cap limits both stores together.
        // The normal 65536 unordered cap remains unchanged; dense slots have
        // their separate fixed byte bound and avoid large-map hash allocations.
        return capacity_ == 65536 || entries() < capacity_;
    }
public:
    RuntimePlannerCellPassabilityScope(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover,
        const std::unordered_map<int, const TerrainCell*>& terrain,
        const std::unordered_map<int, int>& occupancy,
        bool enabled = true, std::size_t capacity = 65536)
        : previous_(runtimeActivePlannerCellPassability), state_(&state), mover_(&mover),
          terrain_(&terrain), occupancy_(&occupancy),
          objects_(state.world.getObjects().data()), cells_(state.world.getTerrainCells().data()),
          terrainObjects_(state.world.getTerrainObjects().data()), mapFacts_(state.mapFacts.cells.data()),
          objectCount_(state.world.getObjects().size()), cellCount_(state.world.getTerrainCells().size()),
          terrainObjectCount_(state.world.getTerrainObjects().size()), mapFactCount_(state.mapFacts.cells.size()),
          terrainCount_(terrain.size()), occupancyCount_(occupancy.size()),
          terrainBuckets_(terrain.bucket_count()), occupancyBuckets_(occupancy.bucket_count()),
          tick_(state.tick), enabled_(enabled), capacity_(std::min<std::size_t>(capacity, 65536)),
          densePermitted_(enabled && capacity_ != 0 && !runtimeComponentDenseReference()) {
        // Even a disabled inner scope masks its outer scope.
        runtimeActivePlannerCellPassability = this;
    }
    ~RuntimePlannerCellPassabilityScope() { runtimeActivePlannerCellPassability = previous_; }
    RuntimePlannerCellPassabilityScope(const RuntimePlannerCellPassabilityScope&) = delete;
    RuntimePlannerCellPassabilityScope& operator=(const RuntimePlannerCellPassabilityScope&) = delete;
    void invalidate() { enabled_ = false; }
    bool matches(const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int, const TerrainCell*>& terrain,
        const std::unordered_map<int, int>& occupancy) const {
        return enabled_ && state_ == &state && mover_ == &mover && terrain_ == &terrain
            && occupancy_ == &occupancy && tick_ == state.tick
            && objects_ == state.world.getObjects().data() && objectCount_ == state.world.getObjects().size()
            && cells_ == state.world.getTerrainCells().data() && cellCount_ == state.world.getTerrainCells().size()
            && terrainObjects_ == state.world.getTerrainObjects().data()
            && terrainObjectCount_ == state.world.getTerrainObjects().size()
            && mapFacts_ == state.mapFacts.cells.data() && mapFactCount_ == state.mapFacts.cells.size()
            && terrainCount_ == terrain.size() && occupancyCount_ == occupancy.size()
            && terrainBuckets_ == terrain.bucket_count() && occupancyBuckets_ == occupancy.bucket_count();
    }
    template<class Compute>
    bool query(const RuntimePlannerPassabilityKey& key, Compute&& compute, bool& hit) {
        std::size_t offset = 0; unsigned shift = 0;
        if (denseSlot(key, offset, shift)) {
            const unsigned stored = (denseValues_[offset] >> shift) & 3U;
            if (stored) { ++hits_; ++denseHits_; hit = true; return stored == 2U; }
            ++misses_;
            const bool value = compute();
            if (explicitCapacityAvailable()) {
                denseValues_[offset] |= static_cast<std::uint8_t>((value ? 2U : 1U) << shift);
                ++denseEntries_;
            } else ++fallbacks_;
            return value;
        }
        const auto found = values_.find(key);
        if (found != values_.end()) { ++hits_; hit = true; return found->second; }
        ++misses_;
        const bool value = compute();
        if (values_.size() < capacity_ && explicitCapacityAvailable()) values_.emplace(key, value);
        else ++fallbacks_;
        return value;
    }
    std::size_t entries() const { return values_.size() + denseEntries_; }
    std::size_t unorderedEntries() const { return values_.size(); }
    std::size_t denseEntries() const { return denseEntries_; }
    std::size_t denseBytes() const { return denseValues_.size(); }
    std::uint64_t denseHits() const { return denseHits_; }
    bool denseAttempted() const { return denseAttempted_; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::uint64_t fallbacks() const { return fallbacks_; }
};

class RuntimePlannerCellPassabilityReadOnlyLease {
    friend class RuntimePlannerConnectivityKernel;
    friend struct RuntimePlannerQueryLeaseContractAccess;
    RuntimePlannerCellPassabilityScope* scope_;
    const RuntimeGameSessionState* state_;
    const WorldObjectInstance* mover_;
    const std::unordered_map<int,const TerrainCell*>* terrain_;
    const std::unordered_map<int,int>* occupancy_;
    int tick_;
    bool enabled_ = false;
    std::size_t fullValidations_ = 0;
    mutable std::size_t queries_ = 0, fallbacks_ = 0;
    using FoundationMap=std::unordered_map<int,std::vector<std::size_t>>;
    mutable const FoundationMap* aliasFoundations_=nullptr;
    // unknown / ineligible / eligible. Only the complete read-only lifetime
    // may reuse this answer; raw aliases/outside coordinates never enter it.
    mutable std::vector<std::uint8_t> aliasEligibility_;
    mutable std::size_t crushAliases_=0,aliasQualificationChecks_=0,aliasFoundationBorrows_=0,scalarQueries_=0;
    bool canAliasCrush(const RuntimeGameSessionState& state,const RuntimeCellPoint& point,int allowed) const {
        if (allowed!=-1 || runtimeComponentCrushAliasReference()
            || !runtimePlannerCensusProvesEmptyCell(state,point)) return false;
        const auto offset=static_cast<std::size_t>(point.y)*512+point.x;
        const auto index=offset/4; const unsigned shift=static_cast<unsigned>(offset%4)*2;
        if (!aliasEligibility_.empty()) {
            const auto stored=(aliasEligibility_[index]>>shift)&3U;
            if (stored) return stored==2U;
        }
        // Do not initialize an original lazy foundation cache earlier than
        // the scalar predicate would. If it is already valid, calling the
        // original helper must return this exact table (including old/stale
        // entries). Borrow it once, not once per edge. An early failing scalar
        // may leave it invalid; that query keeps its original key.
        if (!aliasFoundations_) {
            const auto& objects=state.world.getObjects();
            if (state.foundationCollisionTick!=state.tick
                || state.foundationCollisionObjects!=objects.data()
                || state.foundationCollisionObjectCount!=objects.size()) return false;
            aliasFoundations_=std::addressof(runtimeSolidFoundationsByCell(state));
            ++aliasFoundationBorrows_;
        }
        if (aliasEligibility_.empty()) aliasEligibility_.assign(512U*512U/4U,std::uint8_t{0});
        ++aliasQualificationChecks_;
        const int key=runtimeCellKey(point.x,point.y);
        const bool absent=occupancy_->find(key)==occupancy_->end()
            && aliasFoundations_->find(key)==aliasFoundations_->end();
        aliasEligibility_[index]|=static_cast<std::uint8_t>((absent?2U:1U)<<shift);
        return absent;
    }

    RuntimePlannerCellPassabilityReadOnlyLease(RuntimePlannerCellPassabilityScope* scope,
        const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy, bool enabled)
        : scope_(scope), state_(&state), mover_(&mover), terrain_(&terrain),
          occupancy_(&occupancy), tick_(state.tick) {
        if (!enabled || !scope || runtimeComponentQueryLeaseReference()
            || runtimeComponentPassabilityReference()) return;
        ++fullValidations_;
        enabled_ = runtimeActivePlannerCellPassability == scope
            && scope->matches(state,mover,terrain,occupancy);
    }
    bool matches(const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,
        const std::unordered_set<int>* soft) const {
        return enabled_ && scope_ && scope_->enabled_
            && runtimeActivePlannerCellPassability == scope_
            && state_ == &state && mover_ == &mover && terrain_ == &terrain
            && occupancy_ == &occupancy && tick_ == state.tick && soft == nullptr
            && !runtimeComponentQueryLeaseReference()
            && !runtimeComponentPassabilityReference();
    }
public:
    RuntimePlannerCellPassabilityReadOnlyLease(const RuntimePlannerCellPassabilityReadOnlyLease&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease& operator=(const RuntimePlannerCellPassabilityReadOnlyLease&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease(RuntimePlannerCellPassabilityReadOnlyLease&&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease& operator=(RuntimePlannerCellPassabilityReadOnlyLease&&) = delete;
    void invalidate() noexcept {
        // The kernel owns this memo lifetime too. Do not fall back to a still
        // enabled hot general memo after the caller invalidates for mutation.
        // A reference/disabled lease may have warmed the original wrapper's
        // owner too. Disable only the captured matching owner, independently
        // of whether this lease itself ever became enabled.
        if (scope_ && scope_->state_ == state_ && scope_->mover_ == mover_
            && scope_->terrain_ == terrain_ && scope_->occupancy_ == occupancy_
            && scope_->tick_ == tick_) scope_->invalidate();
        enabled_ = false;
    }
    std::size_t fullValidations() const noexcept { return fullValidations_; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t queries() const noexcept { return queries_; }
    std::size_t fallbacks() const noexcept { return fallbacks_; }
    std::size_t crushAliases() const noexcept { return crushAliases_; }
    std::size_t aliasQualificationChecks() const noexcept { return aliasQualificationChecks_; }
    std::size_t aliasFoundationBorrows() const noexcept { return aliasFoundationBorrows_; }
    std::size_t aliasEligibilityBytes() const noexcept { return aliasEligibility_.size(); }
    std::size_t scalarQueries() const noexcept { return scalarQueries_; }
    bool query(const RuntimeGameSessionState& state,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy, const RuntimeCellPoint& point,
        const WorldObjectInstance& mover, int allowed = -1,
        const std::unordered_set<int>* soft = nullptr, bool crush = false,
        std::optional<PathMovementMode> mode = std::nullopt) const {
        if (!matches(state,mover,terrain,occupancy,soft)) {
            ++fallbacks_;
            return runtimeCellIsPassable(state,terrain,occupancy,point,mover,allowed,soft,crush,mode);
        }
        ++queries_;
        const RuntimePlannerPassabilityKey key{point.x,point.y,allowed,crush,
            mode.has_value(),mode.value_or(PathMovementMode::Ground)};
        auto memoKey=key;
        if (crush && canAliasCrush(state,point,allowed)) {
            memoKey.crush=false; ++crushAliases_;
        }
        bool hit = false;
        const bool value = scope_->query(memoKey,[&] {
            ++scalarQueries_;
            return runtimeCellIsPassableUncached(state,terrain,occupancy,point,
                mover,allowed,soft,crush,mode);
        },hit);
        if (runtimePlannerPassabilityObserver)
            runtimePlannerPassabilityObserver(runtimePlannerPassabilityObserverContext,
                mover,key,soft,value,hit);
        return value;
    }
};

class RuntimePathSnapshotOccupancy {
public:
    RuntimePathSnapshotOccupancy(
        const RuntimeGameSessionState& state,
        const std::vector<PathTerrainCell>& snapshot,
        int moverObjectId,
        int allowedOccupiedObjectId,
        const std::unordered_set<int>& planningGateIds,
        bool includeHardBlockers,
        bool includeUnexpectedOccupants,
        const RuntimePathDynamicPreparationBounds* provenBounds = nullptr
    ) {
        if (snapshot.empty()
            || (!includeHardBlockers && !includeUnexpectedOccupants)) return;
        int minX = snapshot.front().cell.x, maxX = minX;
        int minY = snapshot.front().cell.y, maxY = minY;
        if (provenBounds != nullptr) {
            minX = provenBounds->minimumX; maxX = provenBounds->maximumX;
            minY = provenBounds->minimumY; maxY = provenBounds->maximumY;
        } else {
            for (const PathTerrainCell& cell : snapshot) {
                minX = std::min(minX, cell.cell.x);
                maxX = std::max(maxX, cell.cell.x);
                minY = std::min(minY, cell.cell.y);
                maxY = std::max(maxY, cell.cell.y);
            }
        }
        flagsByCell_.reserve(std::min(snapshot.size(),
            state.world.getObjects().size() + state.world.getTerrainObjects().size()));
        const auto addRectangle = [&](int x, int y, int width, int height,
                                      unsigned char flags) {
            const std::int64_t firstX = std::max<std::int64_t>(minX, x);
            const std::int64_t firstY = std::max<std::int64_t>(minY, y);
            const std::int64_t lastX = std::min<std::int64_t>(maxX,
                static_cast<std::int64_t>(x) + std::max(1, width) - 1);
            const std::int64_t lastY = std::min<std::int64_t>(maxY,
                static_cast<std::int64_t>(y) + std::max(1, height) - 1);
            if (firstX > lastX || firstY > lastY) return;
            const std::uint64_t columns = lastX - firstX + 1;
            const std::uint64_t rows = lastY - firstY + 1;
            const auto add = [&](int cellX, int cellY) {
                flagsByCell_[runtimeCrushCandidateCellKey(cellX, cellY)] |= flags;
            };
            // Bound unusual foundations and sparse maps by actual query count.
            if (columns > static_cast<std::uint64_t>(snapshot.size()) / rows) {
                for (const PathTerrainCell& cell : snapshot) {
                    if (cell.cell.x >= firstX && cell.cell.x <= lastX
                        && cell.cell.y >= firstY && cell.cell.y <= lastY) {
                        add(cell.cell.x, cell.cell.y);
                    }
                }
            } else {
                for (std::int64_t cellY = firstY; cellY <= lastY; ++cellY) {
                    for (std::int64_t cellX = firstX; cellX <= lastX; ++cellX) {
                        add(static_cast<int>(cellX), static_cast<int>(cellY));
                    }
                }
            }
        };
        const unsigned char terrainFlags =
            (includeHardBlockers ? HardBlocker : 0)
            | (includeUnexpectedOccupants ? UnexpectedOccupant : 0);
        for (const WorldTerrainObject& object : state.world.getTerrainObjects()) {
            if (!runtimeTerrainObjectBlocksGround(object)) continue;
            // The scalar footprint generator uses int coordinates. Retain its
            // result for authored rectangles whose endpoint would overflow int.
            const std::int64_t endX = static_cast<std::int64_t>(object.x)
                + std::max(1, object.footprintWidth) - 1;
            const std::int64_t endY = static_cast<std::int64_t>(object.y)
                + std::max(1, object.footprintHeight) - 1;
            if (endX > std::numeric_limits<int>::max()
                || endY > std::numeric_limits<int>::max()) {
                scalarTerrainFallback_ = true;
                continue;
            }
            addRectangle(object.x, object.y, object.footprintWidth,
                object.footprintHeight, terrainFlags);
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive || object.instanceId == moverObjectId
                || object.instanceId == allowedOccupiedObjectId) continue;
            const bool hard = includeHardBlockers
                && object.kind != WorldObjectKind::Infantry
                && !planningGateIds.contains(object.instanceId);
            if (!hard && !includeUnexpectedOccupants) continue;
            if (runtimeObjectOffGroundByMagBeam(state, object.instanceId)
                || (object.kind != WorldObjectKind::Structure
                    && runtimeObjectMovementMode(state, object)
                        == PathMovementMode::Air)) continue;
            const unsigned char flags = (hard ? HardBlocker : 0)
                | (includeUnexpectedOccupants ? UnexpectedOccupant : 0);
            if (object.kind == WorldObjectKind::Structure) {
                const auto footprint = runtimeStructureFootprintForObject(state, object);
                const auto origin = runtimeCellPointForObject(object);
                addRectangle(origin.x, origin.y, footprint.width, footprint.height, flags);
            } else {
                addRectangle(object.x, object.y, 1, 1, flags);
            }
        }
    }

    bool needsScalarTerrainFallback() const { return scalarTerrainFallback_; }
    bool hardBlocker(const RuntimeCellPoint& cell) const {
        return hasFlag(cell, HardBlocker);
    }
    bool unexpectedOccupant(const RuntimeCellPoint& cell) const {
        return hasFlag(cell, UnexpectedOccupant);
    }

private:
    static constexpr unsigned char HardBlocker = 1;
    static constexpr unsigned char UnexpectedOccupant = 2;
    bool hasFlag(const RuntimeCellPoint& cell, unsigned char flag) const {
        const auto it = flagsByCell_.find(runtimeCrushCandidateCellKey(cell.x, cell.y));
        return it != flagsByCell_.end() && (it->second & flag) != 0;
    }
    std::unordered_map<std::uint64_t, unsigned char> flagsByCell_;
    bool scalarTerrainFallback_ = false;
};

class RuntimePathTerrainQueryScope {
public:
    RuntimePathTerrainQueryScope(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover, int allowedObjectId,
        const std::unordered_set<int>& planningGateIds, bool usesCrushingMovement,
        bool includeRepairReservations)
        : state_(state), mover_(mover), allowedObjectId_(allowedObjectId),
          planningGateIds_(planningGateIds), usesCrushingMovement_(usesCrushingMovement),
          reference_(runtimeSimulationQueryReuseReference()) {
        if (reference_) return;
        const auto* allowed = allowedObjectId >= 0 ? findObjectById(state, allowedObjectId) : nullptr;
        if (allowed && allowed->kind == WorldObjectKind::Structure) {
            allowedObject_ = allowed;
            allowedCells_ = runtimeStructureFootprintCellsForObject(state, *allowed);
            const auto* building = findRuntimeBuildingTypeById(state.rules, allowed->type);
            allowedUsesDriveRows_ = building && building->numberImpassableRows > 0
                && runtimeObjectMayUseStructureDriveRows(state, mover);
            if (!allowedUsesDriveRows_) {
                allowedCellKeys_.reserve(allowedCells_.size());
                for (const auto& cell : allowedCells_) allowedCellKeys_.insert(exactCellKey(cell));
            }
        }
        if (includeRepairReservations) {
            repairCellLayers_.reserve(state.repairDepots.relations().size());
            for (const auto& relation : state.repairDepots.relations()) {
                if (relation.unitId == mover.instanceId) continue;
                const auto& reserved = relation.phase == RuntimeRepairDepotRelationPhase::Queued
                    ? relation.queueCell : relation.dockCell;
                if (reserved.x < 0 || reserved.y < 0) continue;
                repairCellLayers_[exactCellKey({reserved.x, reserved.y})]
                    |= static_cast<unsigned char>(reserved.onBridge ? 2 : 1);
            }
        }
    }

    bool repairReserved(const RuntimeCellPoint& cell) const {
        if (reference_) return runtimeCellReservedByOtherRepairUnit(state_, cell, mover_.instanceId);
        const auto reserved = repairCellLayers_.find(exactCellKey(cell));
        if (reserved == repairCellLayers_.end()) return false;
        const unsigned char layer = runtimeCellIsBridge(state_, cell.x, cell.y) ? 2 : 1;
        return (reserved->second & layer) != 0;
    }

    bool allowedFootprint(const RuntimeCellPoint& cell) const {
        if (reference_) return runtimeCellIsAllowedOccupiedStructureFootprint(
            state_, cell, allowedObjectId_, &mover_);
        if (!allowedObject_) return false;
        // Drive rows use the original geometric predicate, including its
        // special meaning for an invisible building with authored drive rows.
        return allowedUsesDriveRows_
            ? runtimeStructureCellIsVehicleDriveRow(state_, *allowedObject_, cell, mover_)
            : allowedCellKeys_.contains(exactCellKey(cell));
    }

    const std::vector<RuntimeCellPoint>& allowedCells() const { return allowedCells_; }

    bool foundationBlocks(const RuntimeCellPoint& cell,
        const std::vector<std::size_t>& foundationIndices, bool gateCell) const {
        for (const auto index : foundationIndices) {
            const auto& building = state_.world.getObjects()[index];
            if (!building.alive || building.instanceId == mover_.instanceId
                || (gateCell && planningGateIds_.contains(building.instanceId))) continue;
            if (runtimeMissionIsMcvDeployPending(building)) return true;
            if (building.instanceId == allowedObjectId_ && allowedFootprint(cell)) continue;
            if (usesCrushingMovement_
                && runtimeCrusherCanCrushObject(state_, mover_, building, cell)) continue;
            return true;
        }
        return false;
    }

    static std::uint64_t exactCellKey(const RuntimeCellPoint& cell) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 32U)
            | static_cast<std::uint32_t>(cell.y);
    }

private:
    const RuntimeGameSessionState& state_;
    const WorldObjectInstance& mover_;
    int allowedObjectId_;
    const std::unordered_set<int>& planningGateIds_;
    bool usesCrushingMovement_;
    bool reference_;
    const WorldObjectInstance* allowedObject_ = nullptr;
    bool allowedUsesDriveRows_ = false;
    std::vector<RuntimeCellPoint> allowedCells_;
    std::unordered_set<std::uint64_t> allowedCellKeys_;
    std::unordered_map<std::uint64_t, unsigned char> repairCellLayers_;
};

class RuntimePathSnapshotMetadataQueries {
public:
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t minimumQueries = 4096;
    static constexpr std::size_t minimumMetadataEntries = 4096;
    static constexpr std::size_t maximumMetadataEntries = denseCellCount;

    static const RuntimeTerrainInfo* originalFind(const RuntimeGameSessionState& state, int key) {
        const auto found = state.terrainInfoByCell.find(key);
        return found == state.terrainInfoByCell.end() ? nullptr : std::addressof(found->second);
    }

    class ReadOnlyView {
    public:
        const RuntimeTerrainInfo* find(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) return (*owner_->pointers_)[offset];
            return originalFind(*owner_->state_, key);
        }
    private:
        friend class RuntimePathSnapshotMetadataQueries;
        explicit ReadOnlyView(const RuntimePathSnapshotMetadataQueries* owner) : owner_(owner) {}
        const RuntimePathSnapshotMetadataQueries* owner_;
    };

    RuntimePathSnapshotMetadataQueries(const RuntimeGameSessionState& state,
        std::size_t expectedQueries, bool enabled = true,
        bool reference = runtimePathSnapshotMetadataQueryReference())
        : state_(&state), tick_(state.tick), count_(state.terrainInfoByCell.size()),
          buckets_(state.terrainInfoByCell.bucket_count()),
          generation_(state.terrainInfoByCell.generation()),
          enabled_(enabled && !reference && !runtimePathSnapshotMetadataQueryReference()
              && expectedQueries >= minimumQueries && count_ >= minimumMetadataEntries
              && count_ <= maximumMetadataEntries) {
        // Reference, Air/disabled, small query/map and oversized metadata maps
        // allocate nothing and do not scan metadata. Exact old lookup remains.
        if (!enabled_) return;
        if (!state.terrainInfoByCell.denseValid()) {
            RuntimePerfTraceScope buildTrace("pathSnapshotMetadataTable", state.tick);
            pointers_ = std::addressof(state.terrainInfoByCell.densePointers());
            sourceEntriesExamined_ = count_;
        } else {
            pointers_ = std::addressof(state.terrainInfoByCell.densePointers());
        }
        retainedEntries_ = state.terrainInfoByCell.denseRetainedEntries();
    }
    RuntimePathSnapshotMetadataQueries(const RuntimePathSnapshotMetadataQueries&) = delete;
    RuntimePathSnapshotMetadataQueries& operator=(const RuntimePathSnapshotMetadataQueries&) = delete;
    RuntimePathSnapshotMetadataQueries(RuntimePathSnapshotMetadataQueries&&) = delete;
    RuntimePathSnapshotMetadataQueries& operator=(RuntimePathSnapshotMetadataQueries&&) = delete;

    void invalidate() noexcept { enabled_ = false; }
    std::optional<ReadOnlyView> viewFor(const RuntimeGameSessionState& state) const {
        if (!enabled_ || runtimePathSnapshotMetadataQueryReference() || state_ != &state
            || tick_ != state.tick || count_ != state.terrainInfoByCell.size()
            || generation_ != state.terrainInfoByCell.generation()
            || buckets_ != state.terrainInfoByCell.bucket_count()) return std::nullopt;
        return ReadOnlyView(this);
    }
    std::size_t denseBytes() const noexcept {
        return pointers_ ? pointers_->size() * sizeof(const RuntimeTerrainInfo*) : 0;
    }
    std::size_t retainedEntries() const noexcept { return retainedEntries_; }
    std::size_t sourceEntriesExamined() const noexcept { return sourceEntriesExamined_; }

private:
    static bool denseOffset(int key, std::size_t& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000, y = key / 1000;
        if (x >= static_cast<int>(side) || y >= static_cast<int>(side)) return false;
        offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
        return true;
    }
    const RuntimeGameSessionState* state_;
    int tick_;
    std::size_t count_, buckets_;
    std::uint64_t generation_;
    bool enabled_;
    const std::vector<const RuntimeTerrainInfo*>* pointers_ = nullptr;
    std::size_t retainedEntries_ = 0, sourceEntriesExamined_ = 0;
};

class RuntimePathSnapshotConditionalQueries {
    const RuntimeProductionHostState& state_;
    RuntimePathSnapshotConditionalQueryObserver observer_;
    void* context_;
    RuntimePathSnapshotConditionalQueryStats stats_;
public:
    explicit RuntimePathSnapshotConditionalQueries(const RuntimeProductionHostState& state)
        : state_(state),observer_(runtimePathSnapshotConditionalQueryObserver),
          context_(runtimePathSnapshotConditionalQueryObserverContext) {
        stats_.reference=runtimePathSnapshotMetadataQueryReference();
    }
    ~RuntimePathSnapshotConditionalQueries() {
        // Test-only diagnostic, after all per-cell queries. It must not throw,
        // destroy/edit the state or retain its borrowed addresses.
        if(observer_) observer_(context_,state_,stats_);
    }
    RuntimePathSnapshotConditionalQueries(const RuntimePathSnapshotConditionalQueries&)=delete;
    RuntimePathSnapshotConditionalQueries& operator=(const RuntimePathSnapshotConditionalQueries&)=delete;
    template<class Find>
    auto metadata(bool used,Find&& find) -> decltype(find()) {
        if(!used && !stats_.reference) {
            if(observer_) ++stats_.metadataSkipped;
            return nullptr;
        }
        if(observer_) ++stats_.metadataLookups;
        return find();
    }
    template<class Find>
    auto occupant(bool used,Find&& find) -> decltype(find()) {
        if(!used && !stats_.reference) {
            if(observer_) ++stats_.occupantSkipped;
            return nullptr;
        }
        if(observer_) ++stats_.occupantLookups;
        return find();
    }
};

class RuntimePathDynamicPreparation {
public:
    using Workset = RuntimePathDynamicWorkset<PathTerrainCell>;
private:
    const RuntimeGameSessionState& state_;
    const WorldObjectInstance& mover_;
    std::vector<PathTerrainCell>& snapshot_;
    const Workset::Occupants& occupants_;
    const void* previous_;
    RuntimePathDynamicPreparationObserver observer_;
    void* observerContext_;
    bool nonAir_, ground_, naval_, tiberium_, eligible_, enabled_ = true;
    bool resourceFusion_; double resourcePercent_;
    bool denseAuthoredLandPolicyDispatched_=false;
    std::optional<RuntimePathSnapshotMetadataQueries> metadata_;
    std::optional<RuntimePathSnapshotMetadataQueries::ReadOnlyView> metadataView_;
    std::optional<Workset> workset_;
    RuntimePathDynamicPreparationStats stats_;
public:
    RuntimePathDynamicPreparation(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover,std::vector<PathTerrainCell>& snapshot,
        const Workset::Occupants& occupants,bool eligible,bool nonAir,bool ground,bool naval,bool tiberium,bool resourceFusion=false,double resourcePercent=100.0)
        : state_(state),mover_(mover),snapshot_(snapshot),occupants_(occupants),
          previous_(runtimeActivePathDynamicPreparation),observer_(runtimePathDynamicPreparationObserver),
          observerContext_(runtimePathDynamicPreparationObserverContext),nonAir_(nonAir),ground_(ground),naval_(naval),
          tiberium_(tiberium),eligible_(eligible),resourceFusion_(resourceFusion),resourcePercent_(resourcePercent) {
        stats_.entries=snapshot.size();stats_.reference=runtimePathDynamicPreparationReference();
        runtimeActivePathDynamicPreparation=this;
    }
    ~RuntimePathDynamicPreparation() {
        // Member Workset remains alive through the original body and tails.
        // Test observers are pure diagnostics and may not edit World/state.
        runtimeActivePathDynamicPreparation=previous_;
        runtimeRecordPathDynamicPreparation(stats_);
        if(observer_)observer_(observerContext_,&state_,stats_);
    }
    RuntimePathDynamicPreparation(const RuntimePathDynamicPreparation&) = delete;
    RuntimePathDynamicPreparation& operator=(const RuntimePathDynamicPreparation&) = delete;
    RuntimePathDynamicPreparation(RuntimePathDynamicPreparation&&) = delete;
    RuntimePathDynamicPreparation& operator=(RuntimePathDynamicPreparation&&) = delete;
    void prepareMetadata() {
        if(metadata_)return;
        metadata_.emplace(state_,snapshot_.size(),nonAir_);
        metadataView_=metadata_->viewFor(state_);
    }
    template<class VisitRepair,class LandAllows,class DenseLand=RuntimePathOriginalAuthoredLandPermission>
    void prepare(bool early,VisitRepair&& visitRepair,LandAllows&& landAllows,DenseLand denseLand={}) {
        if(workset_)return;
        if(early && (!enabled_ || stats_.reference || runtimePathDynamicPreparationReference() || previous_
                || !eligible_ || runtimePathDynamicWorksetReference()))return;
        std::optional<RuntimePerfTraceScope> preparationTrace;
        if(runtimePathDynamicPreparationTraceEnabled())
            preparationTrace.emplace("pathDynamicPreparationProof",state_.tick);
        prepareMetadata();
        const auto findMetadata=[&](int key){return metadataView_?metadataView_->find(key)
            :RuntimePathSnapshotMetadataQueries::originalFind(state_,key);};
        const auto construct=[&](auto collectBounds,auto resourcePolicy,auto landPolicy) {
            denseAuthoredLandPolicyDispatched_=std::decay_t<decltype(landPolicy)>::exactDenseAuthoredLand;
            workset_.emplace(&state_,&mover_,state_.tick,snapshot_,occupants_,eligible_ && enabled_,
                ground_,naval_,tiberium_,std::forward<VisitRepair>(visitRepair),findMetadata,
                std::forward<LandAllows>(landAllows),collectBounds,resourcePolicy,landPolicy);
        };
        // One constructor dispatch per builder, never one dispatch per row.
        // Late/reference preparation retains C33's original scan arithmetic.
        const auto dispatch=[&](auto landPolicy) {
            if(resourceFusion_) {
                if(early)construct(std::true_type{},RuntimePathDynamicResourceFusionPolicy{resourcePercent_},landPolicy);
                else construct(std::false_type{},RuntimePathDynamicResourceFusionPolicy{resourcePercent_},landPolicy);
            } else {
                if(early)construct(std::true_type{},std::false_type{},landPolicy);
                else construct(std::false_type{},std::false_type{},landPolicy);
            }
        };
        // Only the private early builder may request the exact ordinal path.
        // Public/default and late/reference calls instantiate the old policy.
        if constexpr (DenseLand::exactDenseAuthoredLand) {
            if(early && denseLand.available())dispatch(denseLand);
            else dispatch(RuntimePathOriginalAuthoredLandPermission{});
        } else dispatch(RuntimePathOriginalAuthoredLandPermission{});
        stats_.earlyPrepared=early;
        stats_.admitted=workset_->viewFor(&state_,&mover_,state_.tick,snapshot_,occupants_).has_value();
    }
    std::optional<Workset::ReadOnlyView> earlyView() {
        if(!enabled_ || !stats_.earlyPrepared || runtimePathDynamicPreparationReference()
            || runtimeActivePathDynamicPreparation!=this || !workset_)return std::nullopt;
        return workset_->viewFor(&state_,&mover_,state_.tick,snapshot_,occupants_);
    }
    const RuntimePathDynamicPreparationBounds* boundsFor(bool needed) {
        if(!needed || !earlyView() || !workset_->boundsCollected())return nullptr;
        if(collectsStats()){++stats_.boundsPassesAvoided;stats_.boundsRowsAvoided+=snapshot_.size();}
        return &workset_->bounds();
    }
    Workset& workset(){return *workset_;}
    void finishWorkset() noexcept { workset_.reset(); }
    const auto& metadataView() const noexcept{return metadataView_;}
    bool permitsDenseAuthoredLand() const noexcept {
        return enabled_ && eligible_ && !previous_ && !stats_.reference && !observer_
            && runtimeActivePathDynamicPreparation==this;
    }
    RuntimePathDenseAuthoredLandPreparationStats denseAuthoredLandObservation(bool eligible,bool acquired) {
        RuntimePathDenseAuthoredLandPreparationStats result;
        result.eligible=eligible;result.arrayAcquired=acquired;result.entries=snapshot_.size();
        result.policyDispatched=denseAuthoredLandPolicyDispatched_;
        result.proofAdmitted=earlyView().has_value();
        result.denseAdmitted=result.policyDispatched && result.proofAdmitted;
        if(workset_)result.candidates=workset_->stats().candidates;
        return result;
    }
    const std::vector<std::size_t>* infantryIndices() {
        return earlyView()?&workset_->indicesForDiagnostics():nullptr;
    }
    void recordInfantryCollection(std::size_t visited,std::size_t destinations) noexcept {
        if(!collectsStats())return;
        stats_.infantryRangeEntries+=visited;stats_.infantryOriginalRangeEntries+=snapshot_.size();
        stats_.infantryDestinations+=destinations;
    }
    bool collectsStats() const noexcept{return observer_ || runtimePathDynamicPreparationTraceEnabled();}
    const RuntimePathDynamicPreparationStats& stats() const noexcept{return stats_;}
    void invalidate() noexcept {
        enabled_=false;
        if(workset_)workset_->invalidate();
        if(metadata_)metadata_->invalidate();
        metadataView_.reset();
    }
};

class RuntimePathDynamicPreparationWorksetExit {
    RuntimePathDynamicPreparation& owner_;
public:
    explicit RuntimePathDynamicPreparationWorksetExit(RuntimePathDynamicPreparation& owner) noexcept : owner_(owner) {}
    ~RuntimePathDynamicPreparationWorksetExit(){owner_.finishWorkset();}
    RuntimePathDynamicPreparationWorksetExit(const RuntimePathDynamicPreparationWorksetExit&)=delete;
    RuntimePathDynamicPreparationWorksetExit& operator=(const RuntimePathDynamicPreparationWorksetExit&)=delete;
};

class RuntimePlannerConnectivityKernel {
    const RuntimeGameSessionState& state_;
    const WorldObjectInstance& mover_;
    const std::unordered_map<int,const TerrainCell*>& terrain_;
    const std::unordered_map<int,int>& occupancy_;
    bool reference_, enabled_ = true;
    std::optional<PathMovementMode> mode_;
    RuntimePlannerIndexedTopology indexedTopology_;
    RuntimePlannerTerrainQueries terrainQueries_;
    RuntimePlannerCellPassabilityReadOnlyLease passabilityLease_;
    RuntimePlannerColdCellLookups coldLookups_;
public:
    RuntimePlannerConnectivityKernel(const RuntimeGameSessionState& state,
        const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,
        bool reference=runtimeComponentKernelReference(),
        RuntimePlannerCellPassabilityScope* passabilityScope=nullptr)
        : state_(state), mover_(mover), terrain_(terrain), occupancy_(occupancy),
          reference_(reference),
          mode_(reference ? std::nullopt : std::optional<PathMovementMode>(runtimeObjectMovementMode(state_,mover_))),
          indexedTopology_(state,mover,terrain,occupancy,
              !reference && mode_!=PathMovementMode::Air && !runtimeComponentTerrainQueryReference()),
          terrainQueries_(state, terrain, !reference && mode_ != PathMovementMode::Air && !indexedTopology_.enabled()),
          passabilityLease_(passabilityScope,state,mover,terrain,occupancy,
              !reference && mode_ != PathMovementMode::Air),
          coldLookups_(state,mover,terrain,occupancy,terrainQueries_,
              !reference && mode_ != PathMovementMode::Air) {}
    ~RuntimePlannerConnectivityKernel() {
        if (runtimePlannerCrushAliasObserver)
            runtimePlannerCrushAliasObserver(runtimePlannerCrushAliasObserverContext,
                passabilityLease_.crushAliases(),passabilityLease_.aliasQualificationChecks(),
                passabilityLease_.aliasFoundationBorrows(),passabilityLease_.scalarQueries());
        if (runtimePlannerKernelLeaseObserver)
            runtimePlannerKernelLeaseObserver(runtimePlannerKernelLeaseObserverContext,
                leaseFullValidations(),leasedQueries(),leaseFallbacks());
    }
    RuntimePlannerConnectivityKernel(const RuntimePlannerConnectivityKernel&) = delete;
    RuntimePlannerConnectivityKernel& operator=(const RuntimePlannerConnectivityKernel&) = delete;
    RuntimePlannerConnectivityKernel(RuntimePlannerConnectivityKernel&&) = delete;
    RuntimePlannerConnectivityKernel& operator=(RuntimePlannerConnectivityKernel&&) = delete;
    void invalidate() noexcept {
        enabled_=false; coldLookups_.invalidate(); indexedTopology_.invalidate(); terrainQueries_.invalidate(); passabilityLease_.invalidate();
    }
    bool reference() const noexcept { return reference_; }
    std::optional<PathMovementMode> precomputedMode() const noexcept { return mode_; }
    bool usesOriginalCost() const noexcept {
        return reference_ || !enabled_ || mode_==PathMovementMode::Air;
    }
    std::size_t leaseFullValidations() const noexcept { return passabilityLease_.fullValidations(); }
    bool queryLeaseEnabled() const noexcept { return passabilityLease_.enabled(); }
    std::size_t leasedQueries() const noexcept { return passabilityLease_.queries(); }
    std::size_t leaseFallbacks() const noexcept { return passabilityLease_.fallbacks(); }
    bool seedIsPassable(const RuntimeCellPoint& seed) const {
        if (usesOriginalCost() || runtimeComponentQueryLeaseReference())
            return runtimeCellIsPassable(state_,terrain_,occupancy_,seed,mover_);
        // Keep the original implicit-mode, crush=false seed key. In particular
        // a failed seed still enters visited in the caller before later edges.
        return passabilityLease_.query(state_,terrain_,occupancy_,seed,mover_);
    }
    bool connects(const RuntimeCellPoint& current,const RuntimeCellPoint& next) const {
        // Air transition deliberately does not require terrain: the original
        // cost wrapper's subsequent terrain lookup is part of its eligibility.
        // Keep that exact wrapper for Air and disabled/reference kernels.
        if (usesOriginalCost())
            return runtimeCellTraversalCost(state_,terrain_,occupancy_,current,next,mover_).has_value();
        // For every non-Air mode, transition=true already proved current and
        // next terrain pointers exist. In this read-only lifetime the original
        // cost tail cannot return nullopt; its integer value is never consumed
        // by the component BFS. Keep repair, diagonal/layer/height, gate and
        // crush queries inside the original transition, with identical mode.
        return runtimeCellTransitionIsPassable(state_,terrain_,occupancy_,current,next,
            mover_,-1,nullptr,true,mode_,&terrainQueries_,
            runtimeComponentQueryLeaseReference() ? nullptr : &passabilityLease_,&indexedTopology_);
    }
};

template<class Session>
class RuntimeHostTriggerPersistenceOperations final : public RuntimeTriggerPersistenceOperations {
    Session& session_;
public:
    explicit RuntimeHostTriggerPersistenceOperations(Session& session) : session_(session) {}
    bool objectExists(int id) const override {
        return findRuntimeObjectByIdIncludingInactive(session_, id) != nullptr;
    }
    const RuntimeTeamType* findTeam(const std::string& id) const override {
        return findRuntimeTriggerTeamById(session_, id);
    }
    std::string resolveHouse(const std::string& name) const override {
        return runtimeCampaignHouseName(session_, name);
    }
    void applyTechLevel(const std::string& typeKey, int techLevel) override {
            const auto item = std::find_if(
                session_.economy.catalog.begin(),
                session_.economy.catalog.end(),
                [&typeKey](const RuntimeBuildCatalogItem& candidate) {
                    return toLowerAsciiLocal(candidate.typeId) == typeKey;
                }
            );
            if (item != session_.economy.catalog.end()) {
                item->techLevel = techLevel;
            }
    }
    bool unitTypeExists(const std::string& id) const override {
        return findRuntimeUnitTypeById(session_.rules, id) != nullptr;
    }
};

class RuntimeHostSaveIdentityNormalization final : public RuntimeSaveIdentityNormalization {
public:
    std::string mapIdentity(const std::string& path) const override {
        return normalizedRuntimeSaveMapIdentity(path);
    }
    std::string campaignSourceId(const std::string& source) const override {
        return normalizeRuntimeCampaignSourceId(source);
    }
};

struct RuntimeHarvesterResourceAccess {
    const RuntimeProductionHostState& state;
    const WorldObjectInstance& harvester;
    int departureRefineryId;
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const std::unordered_map<int, const TerrainCell*>* terrain;
    std::unordered_map<int, int> staticOccupancy;
    std::optional<std::unordered_map<int, int>> pathOccupancy;
    std::optional<std::unordered_set<int>> reachableAfterFailure;

    RuntimeHarvesterResourceAccess(const RuntimeProductionHostState& worldState, const WorldObjectInstance& miner, const std::string* owner = nullptr)
        : state(worldState), harvester(miner),
          departureRefineryId(runtimeHarvesterDepartureRefineryId(state, miner, owner)),
          terrain(&state.terrainByCell),
          staticOccupancy(buildRuntimeOccupancyLookup(state, miner.instanceId)) {
        if (terrain->empty()) {
            generatedTerrain = buildRuntimeTerrainLookup(state.world);
            terrain = &generatedTerrain;
        }
        // Another miner can share the same ore deposit. Mobile traffic must not
        // make a stationary harvester change its intake direction every tick.
        for (auto it = staticOccupancy.begin(); it != staticOccupancy.end();) {
            const auto* occupant = runtimeOccupantIdIsTerrain(it->second)
                ? nullptr : findObjectById(state, it->second);
            if (occupant != nullptr && occupant->kind != WorldObjectKind::Structure)
                it = staticOccupancy.erase(it);
            else ++it;
        }
    }

    bool canGatherFrom(const RuntimeCellPoint& from, const RuntimeCellPoint& resource) const {
        if (!runtimeCellHasHarvestableResource(state, resource)
            || std::abs(from.x - resource.x) > 1 || std::abs(from.y - resource.y) > 1)
            return false;
        const auto source = terrain->find(runtimeCellKey(from.x, from.y));
        if (source == terrain->end() || source->second == nullptr
            || runtimeCachedTerrainInfoForCell(state, *source->second).bridge)
            return false;
        // A delivery point inside a refinery is valid for unloading and
        // departure, but cannot become a stationary mining approach.
        if (!runtimeCellIsPassable(state, *terrain, staticOccupancy, from, harvester))
            return false;
        if (from == resource) return runtimeCellIsPassable(
            state, *terrain, staticOccupancy, resource, harvester);
        return runtimeCellTransitionIsPassable(
            state, *terrain, staticOccupancy, from, resource, harvester, -1, nullptr, false);
    }

    const std::unordered_map<int, int>& occupiedForPath() {
        if (!pathOccupancy) {
            pathOccupancy = buildRuntimeOccupancyLookup(state, harvester.instanceId);
        }
        return *pathOccupancy;
    }

    void rememberFailedSearch() {
        if (!reachableAfterFailure) {
            // One blocked worker can see hundreds of ore cells outside its
            // pocket. Discover its reachable ground once for this search,
            // rather than rebuilding a path request for every ore approach.
            reachableAfterFailure = buildRuntimeReachableMoveCells(
                state, harvester, {runtimeCellPointForObject(harvester)},
                *terrain, occupiedForPath(), departureRefineryId);
        }
    }
};

struct RuntimeMultiplayerSnapshotStagedState {
    std::unique_ptr<RuntimeProductionHostState> scratch;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
};

class RuntimeComputerMiningFeedbackScope {
public:
    explicit RuntimeComputerMiningFeedbackScope(RuntimeProductionHostState& state)
        : state_(state), interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          command_(state.lastCommand), human_(state.humanHouseScopeActive),
          remote_(state.remoteHumanHouseScopeActive), computer_(state.computerMiningScopeActive),
          suppress_(state.runtimeSuppressPlayerAudioFeedback) {
        state.humanHouseScopeActive = state.remoteHumanHouseScopeActive = state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
    }
    ~RuntimeComputerMiningFeedbackScope() {
        state_.interactionStatus = std::move(interaction_); state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(command_); state_.humanHouseScopeActive = human_;
        state_.remoteHumanHouseScopeActive = remote_; state_.computerMiningScopeActive = computer_;
        state_.runtimeSuppressPlayerAudioFeedback = suppress_;
    }
    RuntimeComputerMiningFeedbackScope(const RuntimeComputerMiningFeedbackScope&) = delete;
    RuntimeComputerMiningFeedbackScope& operator=(const RuntimeComputerMiningFeedbackScope&) = delete;
private:
    RuntimeProductionHostState& state_;
    std::string interaction_, simulation_;
    decltype(RuntimeProductionHostState::lastCommand) command_;
    bool human_, remote_, computer_, suppress_;
};

class RuntimeInfantryWaterPoseQuery {
public:
    explicit RuntimeInfantryWaterPoseQuery(RuntimeProductionHostState& state,
        bool reference = runtimeInfantryWaterContextReference())
        : state_(state), reference_(reference) {}
    RuntimeInfantryWaterPoseQuery(const RuntimeInfantryWaterPoseQuery&) = delete;
    RuntimeInfantryWaterPoseQuery& operator=(const RuntimeInfantryWaterPoseQuery&) = delete;

    RuntimeInfantryWaterPose pose(const WorldObjectInstance& object) {
        if (reference_) return runtimeInfantryWaterPose(state_, object);
        RuntimeInfantryWaterPose result;
        if (object.kind != WorldObjectKind::Infantry || object.airborne
            || !object.alive || object.health <= 0) return result;
        const auto* unit = findRuntimeUnitType(state_.rules, object);
        if (!unit || (unit->enterWaterSound.empty() && unit->leaveWaterSound.empty())) return result;
        result.x = object.hasRenderPosition ? object.renderX : object.x;
        result.y = object.hasRenderPosition ? object.renderY : object.y;
        if (!std::isfinite(result.x) || !std::isfinite(result.y)
            || result.x < 0 || result.x >= 511.5 || result.y < 0 || result.y >= 511.5
            || !MapQuery(state_.mapFacts).getCell(static_cast<int>(std::floor(result.x + 0.5)),
                static_cast<int>(std::floor(result.y + 0.5)))) return result;
        const auto* context = runtimeBorrowInfantryWaterMetadataContext(state_);
        if (!context) context = &metadataContext();
        result.valid = true;
        result.swimming = resolveRuntimeInfantrySwimming(object, *context, state_.renderCache);
        return result;
    }

    const RuntimeMapRenderContext& metadataContext() {
        if (!context_) {
            context_.emplace(runtimeTerrainMetadataContext(state_));
            context_->mapFacts = &state_.mapFacts;
            runtimeObserveInfantryWaterContextConstruction();
        }
        return *context_;
    }

    bool matches(const RuntimeProductionHostState& state) const { return &state_ == &state; }
    bool hasContext() const { return context_.has_value(); }
    bool reference() const { return reference_; }
private:
    RuntimeProductionHostState& state_;
    bool reference_;
    std::optional<RuntimeMapRenderContext> context_;
};

class RuntimeInfantryWaterBehaviorContextScope {
public:
    explicit RuntimeInfantryWaterBehaviorContextScope(RuntimeProductionHostState& state, bool enabled = true)
        : state_(state), tick_(state.tick), enabled_(enabled),
          previous_(runtimeActiveInfantryWaterBehaviorContext) {
        runtimeActiveInfantryWaterBehaviorContext = this;
    }
    ~RuntimeInfantryWaterBehaviorContextScope() {
        runtimeActiveInfantryWaterBehaviorContext = previous_;
    }
    RuntimeInfantryWaterBehaviorContextScope(const RuntimeInfantryWaterBehaviorContextScope&) = delete;
    RuntimeInfantryWaterBehaviorContextScope& operator=(const RuntimeInfantryWaterBehaviorContextScope&) = delete;

    void invalidate() { enabled_ = false; }
    bool hasContext() const { return query_.has_value() && query_->hasContext(); }
    const RuntimeMapRenderContext* borrow(RuntimeProductionHostState& state) {
        if (!enabled_ || &state != &state_) return nullptr;
        if (state.tick != tick_) { invalidate(); return nullptr; }
        if (!source_) source_.emplace(state);
        else if (!source_->matches(state)) { invalidate(); return nullptr; }
        if (!query_) query_.emplace(state, false);
        return &query_->metadataContext();
    }
private:
    struct Source {
        explicit Source(const RuntimeProductionHostState& state)
            : root(state.config.localDataRootPath), mapPath(state.report.map.path),
              theaterLoaded(state.theaterLoaded), tileSets(state.theater.getTileSets().data()),
              tileSetCount(state.theater.getTileSets().size()),
              tileSlots(state.theater.getTotalTileSlots()), extension(state.theater.getTileExtension()),
              cells(state.mapFacts.cells.data()), cellCount(state.mapFacts.cells.size()),
              mapHash(state.mapFacts.contentHash), lookupSize(state.mapFacts.cellIndexLookup.size()),
              lookupBuckets(state.mapFacts.cellIndexLookup.bucket_count()) {}
        bool matches(const RuntimeProductionHostState& state) const {
            return root == state.config.localDataRootPath && mapPath == state.report.map.path
                && theaterLoaded == state.theaterLoaded
                && tileSets == state.theater.getTileSets().data()
                && tileSetCount == state.theater.getTileSets().size()
                && tileSlots == state.theater.getTotalTileSlots()
                && extension == state.theater.getTileExtension()
                && cells == state.mapFacts.cells.data() && cellCount == state.mapFacts.cells.size()
                && mapHash == state.mapFacts.contentHash
                && lookupSize == state.mapFacts.cellIndexLookup.size()
                && lookupBuckets == state.mapFacts.cellIndexLookup.bucket_count();
        }
        std::string root, mapPath;
        bool theaterLoaded;
        const TheaterTileSet* tileSets;
        std::size_t tileSetCount, tileSlots;
        std::string extension;
        const MapCellFacts* cells;
        std::size_t cellCount;
        std::uint64_t mapHash;
        std::size_t lookupSize, lookupBuckets;
    };
    RuntimeProductionHostState& state_;
    int tick_;
    bool enabled_;
    RuntimeInfantryWaterBehaviorContextScope* previous_;
    std::optional<Source> source_;
    std::optional<RuntimeInfantryWaterPoseQuery> query_;
};

template<class Session>
class RuntimeOpenTransportSourceQueries final : public RuntimeOpenTransportMuzzleQueries {
public:
    explicit RuntimeOpenTransportSourceQueries(const Session& session) noexcept : session_(session) {}
    std::span<const WorldObjectInstance> objects() const override {
        return session_.world.getObjects();
    }
    const ArtType* artTypeForObject(const WorldObjectInstance& object) const override {
        return runtimeArtTypeForObject(session_, object);
    }
private:
    const Session& session_;
};

struct RuntimeCombatOpportunityOwnerSummary {
    bool valid = false;
    bool disabledForPass = false;
    const void* objectData = nullptr;
    std::size_t objectCount = 0;
    std::vector<std::string> livingOwners;
    std::unordered_map<std::string, bool> hasHostileOwner;
    int populationScans = 0;
    int emptyProofs = 0;

    void invalidate() { valid = false; }

    bool provesNoCandidate(const RuntimeGameSessionState& state,
        const WorldObjectInstance& pose, bool forceFire) {
        // A pass containing hostile owners already needs ordinary targeting.
        // Keep this conservative decision across fire invalidations so combat
        // never rebuilds the population census for every subsequent shooter.
        if (disabledForPass) return false;
        // Force fire may admit an allied/neutral preferred target. Any active
        // psychic record takes the scalar gate, including both berserk policies.
        if (forceFire) return false;
        const auto* psychic = state.psychicStatuses.findByTarget(pose.instanceId);
        if (psychic != nullptr && state.tick < psychic->expiresTick) return false;
        const auto& objects = state.world.getObjects();
        if (!valid || objectData != objects.data() || objectCount != objects.size()) {
            livingOwners.clear();
            hasHostileOwner.clear();
            std::unordered_set<std::string> seen;
            for (const auto& object : objects) {
                if (object.alive && object.health > 0 && seen.insert(object.owner).second)
                    livingOwners.push_back(object.owner);
            }
            objectData = objects.data(); objectCount = objects.size();
            valid = true;
            ++populationScans;
        }
        auto found = hasHostileOwner.find(pose.owner);
        if (found == hasHostileOwner.end()) {
            bool hostile = false;
            for (const auto& owner : livingOwners) {
                if (runtimeOwnersAreHostileForAutoTarget(state, pose.owner, owner)) {
                    hostile = true;
                    break;
                }
            }
            found = hasHostileOwner.emplace(pose.owner, hostile).first;
        }
        if (found->second) {
            disabledForPass = true;
            return false;
        }
        ++emptyProofs;
        return true;
    }
};

struct RuntimeCombatOpportunityRangeBound {
    bool enabled = false;
    double maximumBaseRange = 0.0;
    double elevationCap = 0.0;
    double elevationBonus = 0.0;
    int elevationIncrement = 1;

    RuntimeCombatOpportunityRangeBound(const RuntimeGameSessionState& state,
        const WorldObjectInstance& pose, const WeaponType* fixedWeapon,
        double rangeBonus, bool passengerFire) {
        // Specialized firing contexts retain their complete scalar selection.
        if (fixedWeapon != nullptr || passengerFire || rangeBonus != 0.0
            || pose.airborne || pose.tankBunkerHostId >= 0
            || (pose.kind != WorldObjectKind::Infantry && pose.kind != WorldObjectKind::Unit)
            || pose.activeGunnerWeaponIndex >= 0) return;
        const auto& cached = runtimeCachedCapabilityProfileForObject(state, pose);
        if (!cached.rulesResolved || cached.gattling || cached.airRangeBonus != 0.0
            || !cached.indexedWeaponCandidates.empty() || !cached.eliteIndexedWeaponCandidates.empty()
            || cached.openTransportWeapon.has_value()) return;
        const auto& elevation = state.rules.getElevationModelRules();
        if (!std::isfinite(elevation.bonusCap) || !std::isfinite(elevation.incrementBonus)) return;
        elevationCap = std::max(0.0, elevation.bonusCap);
        elevationBonus = std::max(0.0, elevation.incrementBonus);
        elevationIncrement = std::max(1, elevation.increment);
        bool foundWeapon = false;
        const auto include = [&](const auto& candidates) {
            for (const auto& candidate : candidates) {
                const WeaponType* weapon = findRuntimeWeaponType(state.rules, candidate.second);
                if (weapon == nullptr) continue;
                // Infinite spawners and unusual authored numbers cannot be
                // represented by this finite upper bound. Never truncate them.
                if (weapon->spawner || !std::isfinite(weapon->range)) return false;
                maximumBaseRange = std::max(maximumBaseRange, std::max(0.0, weapon->range));
                foundWeapon = true;
            }
            return true;
        };
        // With indexed/gunner/open-transport paths excluded, these are the
        // complete candidate lists consulted by runtimeWeaponProfileForObject.
        // Include both ranks and inactive deploy/occupy slots conservatively.
        enabled = include(cached.baseWeaponCandidates)
            && include(cached.deployWeaponCandidates)
            && include(cached.occupyWeaponCandidates) && foundWeapon;
    }

    bool excludes(const RuntimeGameSessionState& state, const WorldObjectInstance& pose,
        const WorldObjectInstance& candidate) const {
        if (!enabled) return false;
        double upper = maximumBaseRange;
        if (!candidate.airborne && pose.level > candidate.level) {
            const double height = static_cast<double>(static_cast<std::int64_t>(pose.level) - candidate.level);
            upper += std::min(elevationCap, std::floor(height / elevationIncrement)) * elevationBonus;
        }
        if (!std::isfinite(upper)) return false;
        // A footprint's nearest cell is no farther than its origin, so this
        // lower bound also covers weapons using CellRangefinding=yes. Keep a
        // relative margin; the original exact range check still decides hits.
        const auto footprint = runtimeStructureFootprintForObject(state, candidate);
        const std::int64_t lastX = static_cast<std::int64_t>(candidate.x) + std::max(1, footprint.width) - 1;
        const std::int64_t lastY = static_cast<std::int64_t>(candidate.y) + std::max(1, footprint.height) - 1;
        if (lastX > std::numeric_limits<int>::max() || lastY > std::numeric_limits<int>::max()) return false;
        const RuntimeCellPoint nearest{
            std::clamp(pose.x, candidate.x, static_cast<int>(lastX)),
            std::clamp(pose.y, candidate.y, static_cast<int>(lastY))
        };
        const std::int64_t dx = static_cast<std::int64_t>(pose.x) - nearest.x;
        const std::int64_t dy = static_cast<std::int64_t>(pose.y) - nearest.y;
        if (dx < std::numeric_limits<int>::min() || dx > std::numeric_limits<int>::max()
            || dy < std::numeric_limits<int>::min() || dy > std::numeric_limits<int>::max()) return false;
        const double distance = runtimeCellDistance(runtimeCellPointForObject(pose), nearest);
        return distance > upper + 1.0e-9 * std::max(1.0, upper);
    }
};

struct RuntimeCombatOpportunityCandidates {
    static constexpr std::int64_t bucketSize = 8;
    const RuntimeGameSessionState* stateIdentity = nullptr;
    const void* objectData = nullptr;
    std::size_t objectCount = 0;
    bool valid = false;
    bool disabledForPass = false;
    bool geometryRepresentable = true;
    std::int64_t maximumWidth = 1;
    std::int64_t maximumHeight = 1;
    std::int64_t minimumX = 0, maximumX = 0;
    std::int64_t minimumY = 0, maximumY = 0;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
    std::vector<std::size_t> orderedCandidates;
    std::uint64_t populationBuilds = 0;
    std::uint64_t indexedQueries = 0;
    std::uint64_t candidateVisits = 0;

    void invalidate() { valid = false; }

    void invalidateForWorldAction() {
        valid = false;
        // Once shots can alter the world, finish this pass with scalar reads.
        // Do not rebuild the whole population separately for every shooter.
        disabledForPass = true;
    }

    static std::int64_t coordinate(std::int64_t value) {
        const auto quotient = value / bucketSize;
        return quotient - (value % bucketSize < 0 ? 1 : 0);
    }

    static std::uint64_t key(std::int64_t x, std::int64_t y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U)
            | static_cast<std::uint32_t>(y);
    }

    void rebuild(const RuntimeGameSessionState& state) {
        const auto& objects = state.world.getObjects();
        buckets.clear();
        maximumWidth = maximumHeight = 1;
        geometryRepresentable = true;
        bool found = false;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const auto& object = objects[index];
            if (!object.alive || object.health <= 0) continue;
            const auto footprint = runtimeStructureFootprintForObject(state, object);
            const auto width = static_cast<std::int64_t>(std::max(1, footprint.width));
            const auto height = static_cast<std::int64_t>(std::max(1, footprint.height));
            const auto lastX = static_cast<std::int64_t>(object.x) + width - 1;
            const auto lastY = static_cast<std::int64_t>(object.y) + height - 1;
            // The scalar bound deliberately retains unrepresentable geometry.
            // A shortlist must likewise leave every such candidate available.
            if (lastX > std::numeric_limits<int>::max()
                || lastY > std::numeric_limits<int>::max()) geometryRepresentable = false;
            maximumWidth = std::max(maximumWidth, width);
            maximumHeight = std::max(maximumHeight, height);
            if (!found) {
                minimumX = object.x; maximumX = lastX;
                minimumY = object.y; maximumY = lastY;
                found = true;
            } else {
                minimumX = std::min(minimumX, static_cast<std::int64_t>(object.x));
                maximumX = std::max(maximumX, lastX);
                minimumY = std::min(minimumY, static_cast<std::int64_t>(object.y));
                maximumY = std::max(maximumY, lastY);
            }
            buckets[key(coordinate(object.x), coordinate(object.y))].push_back(index);
        }
        stateIdentity = &state;
        objectData = objects.data(); objectCount = objects.size();
        valid = true;
        ++populationBuilds;
    }

    // nullptr means use the unchanged scalar walk. An empty vector proves that
    // no living origin/footprint can pass the existing conservative range bound.
    const std::vector<std::size_t>* query(const RuntimeGameSessionState& state,
        const WorldObjectInstance& pose, const RuntimeCombatOpportunityRangeBound& bound) {
        if (!bound.enabled || disabledForPass) return nullptr;
        const auto& objects = state.world.getObjects();
        // Building an index costs more than walking a small population once.
        if (objects.size() < 64) return nullptr;
        const double upper = bound.maximumBaseRange + bound.elevationCap * bound.elevationBonus;
        const double padded = upper + 1.0e-9 * std::max(1.0, upper);
        if (!std::isfinite(padded) || padded > std::numeric_limits<int>::max()) return nullptr;
        if (!valid || stateIdentity != &state || objectData != objects.data()
            || objectCount != objects.size()) rebuild(state);
        if (!geometryRepresentable) return nullptr;
        // Avoid pruning where the original int-coordinate distance helper or
        // conservative scalar bound has to retain extreme coordinates.
        const auto intMin = static_cast<std::int64_t>(std::numeric_limits<int>::min());
        const auto intMax = static_cast<std::int64_t>(std::numeric_limits<int>::max());
        if (!buckets.empty()
            && (static_cast<std::int64_t>(pose.x) - minimumX > intMax
                || static_cast<std::int64_t>(pose.x) - maximumX < intMin
                || static_cast<std::int64_t>(pose.y) - minimumY > intMax
                || static_cast<std::int64_t>(pose.y) - maximumY < intMin)) return nullptr;
        const auto radius = static_cast<std::int64_t>(std::ceil(padded));
        const auto firstX = coordinate(static_cast<std::int64_t>(pose.x) - radius - maximumWidth + 1);
        const auto lastX = coordinate(static_cast<std::int64_t>(pose.x) + radius);
        const auto firstY = coordinate(static_cast<std::int64_t>(pose.y) - radius - maximumHeight + 1);
        const auto lastY = coordinate(static_cast<std::int64_t>(pose.y) + radius);
        const auto columns = lastX - firstX + 1;
        const auto rows = lastY - firstY + 1;
        // Large foundations/mod ranges keep their scalar path instead of
        // turning the sparse bucket query into a large empty-grid traversal.
        if (columns > 256 || rows > 256 || columns * rows > 256) return nullptr;
        orderedCandidates.clear();
        for (auto y = firstY; y <= lastY; ++y) for (auto x = firstX; x <= lastX; ++x) {
            const auto bucket = buckets.find(key(x, y));
            if (bucket != buckets.end())
                orderedCandidates.insert(orderedCandidates.end(), bucket->second.begin(), bucket->second.end());
        }
        // Original world order decides preferred targets and exact floating
        // comparisons. Bucket/hash iteration must not decide gameplay order.
        std::sort(orderedCandidates.begin(), orderedCandidates.end());
        ++indexedQueries;
        candidateVisits += orderedCandidates.size();
        return &orderedCandidates;
    }
};

class RuntimeSkirmishStartingConnectivity {
    const RuntimeGameSessionState& state_;
    std::unordered_map<int, const TerrainCell*> terrainByCell_;
    std::unordered_map<int, int> occupiedAtStart_;
    std::unordered_map<std::string, std::unordered_set<int>> reachableByType_;
    std::vector<RuntimeCellPoint> foundationSeeds_;
    RuntimeCellPoint mainCell_;
    int mainMcvId_ = -1;

    static std::string keyFor(const WorldObjectInstance& object) {
        return std::to_string(static_cast<int>(object.kind)) + ":"
            + toLowerAsciiLocal(trimAsciiLocal(object.type));
    }
    void prepare(RuntimeBuildKind kind, const UnitType& type,
        const std::string& owner) {
        WorldObjectInstance probe;
        probe.instanceId = -1;
        probe.kind = runtimeKindForBuildItem(kind);
        probe.owner = owner;
        probe.type = type.id;
        probe.alive = true;
        probe.health = std::max(1, type.strength);
        const std::string key = keyFor(probe);
        if (reachableByType_.contains(key)) return;
        // All inputs stay read-only until this type's traversal ends. These
        // existing query scopes avoid rescanning every world footprint per
        // map cell, and expire before the first actual formation spawn.
        RuntimeBuildingDepartureReadOnlyQueryScope departureQueries(state_, probe);
        RuntimeSubcellReadOnlyQueryScope subcellQueries(state_.world.getObjects(),
            probe.kind == WorldObjectKind::Infantry);
        RuntimePlannerCellPassabilityScope passabilityQueries(
            state_, probe, terrainByCell_, occupiedAtStart_);
        RuntimePlannerLandPermissionScope landPermissions(state_, probe);
        RuntimePlannerEmptyCellCensusScope emptyCells(state_);
        std::vector<RuntimeCellPoint> seeds;
        if (mainMcvId_ >= 0) for (const auto& cell : foundationSeeds_) {
            // Infantry cannot occupy the MCV's own whole ground cell. Other
            // original future-foundation cells are checked for real free slots.
            if (probe.kind == WorldObjectKind::Infantry && cell == mainCell_)
                continue;
            if (runtimeCellIsPassable(state_, terrainByCell_, occupiedAtStart_,
                    cell, probe, mainMcvId_)) seeds.push_back(cell);
        }
        // The reachable helper accepts seeds unconditionally; only the proven
        // legal seeds above may enter it. Its native traversal keeps diagonal
        // side blockers, terrain height changes, bridges and movement modes.
        reachableByType_.emplace(key, buildRuntimeReachableMoveCells(state_,
            probe, seeds, terrainByCell_, occupiedAtStart_, mainMcvId_));
    }
public:
    RuntimeSkirmishStartingConnectivity(const RuntimeGameSessionState& state,
        const std::string& owner, RuntimeCellPoint origin,
        const RuntimeSkirmishStartingUnitPools& pools,
        const std::vector<const UnitType*>& extraMcvs)
        : state_(state), mainCell_(origin) {
        terrainByCell_ = state.terrainByCell.empty()
            ? buildRuntimeTerrainLookup(state.world) : state.terrainByCell;
        // Capture before any formation spawn. New mobile actors may block a
        // birth cell, but must not redefine the pre-spawn terrain connectivity.
        occupiedAtStart_ = buildRuntimeOccupancyLookup(state);
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0
                || object.kind != WorldObjectKind::Unit
                || runtimeCellPointForObject(object) != origin
                || toLowerAsciiLocal(trimAsciiLocal(object.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(owner))) continue;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            if (!unit || !runtimeUnitTypeIsMcv(state, *unit)) continue;
            const auto yard = runtimeBuildItemForBuilding(state, unit->deploysInto);
            if (!yard) continue;
            mainMcvId_ = object.instanceId;
            const auto footprint = runtimeStructureFootprintForBuildItem(state, *yard);
            foundationSeeds_ = runtimeStructureFootprintCells(
                runtimePlacementTopLeftForAnchor(origin, footprint), footprint);
            break;
        }
        for (const auto* type : pools.infantry)
            if (type) prepare(RuntimeBuildKind::Infantry, *type, owner);
        for (const auto* type : pools.vehicles)
            if (type) prepare(RuntimeBuildKind::Unit, *type, owner);
        for (const auto* type : extraMcvs)
            if (type) prepare(RuntimeBuildKind::Unit, *type, owner);
    }
    std::optional<RuntimeCellPoint> findCellNear(const WorldObjectInstance& mover,
        RuntimeCellPoint desired, int maxRadiusCells,
        const std::unordered_set<int>* reservedCells = nullptr,
        int allowedOccupiedObjectId = -1) const {
        const auto connected = reachableByType_.find(keyFor(mover));
        if (connected == reachableByType_.end()) return std::nullopt;
        // Rebuild live occupancy for every birth, just as the original finder
        // does. Cached sets only veto disconnected candidate cells.
        const auto occupied = buildRuntimeOccupancyLookup(state_, mover.instanceId);
        const auto canUse = [&](RuntimeCellPoint cell) {
            const int key = runtimeCellKey(cell.x, cell.y);
            return key >= 0
                && (!reservedCells || !reservedCells->contains(key))
                && runtimeCellIsPassable(state_, terrainByCell_, occupied, cell,
                    mover, allowedOccupiedObjectId)
                && connected->second.contains(key);
        };
        if (canUse(desired)) return desired;
        for (int radius = 1; radius <= std::max(0, maxRadiusCells); ++radius)
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                    const RuntimeCellPoint candidate{desired.x + dx, desired.y + dy};
                    if (canUse(candidate)) return candidate;
                }
        return std::nullopt;
    }
};

class RuntimeChronoMoveTerrainView {
public:
    explicit RuntimeChronoMoveTerrainView(const RuntimeGameSessionState& state,
        bool reference = runtimeChronoMoveTerrainReference())
        : terrain_(&state.terrainByCell), reference_(reference) {
        RuntimePerfTraceScope trace("moveCommand.chronoTerrain", state.tick);
        if (terrain_->empty()) {
            owned_ = buildRuntimeTerrainLookup(state.world);
            terrain_ = &owned_;
            generated_ = true;
        } else if (reference_) {
            owned_ = state.terrainByCell;
            terrain_ = &owned_;
        }
    }
    const std::unordered_map<int, const TerrainCell*>& get() const { return *terrain_; }
    bool borrowed() const { return terrain_ != &owned_; }
    bool generated() const { return generated_; }
    bool reference() const { return reference_; }
    RuntimeChronoMoveTerrainView(const RuntimeChronoMoveTerrainView&) = delete;
    RuntimeChronoMoveTerrainView& operator=(const RuntimeChronoMoveTerrainView&) = delete;
private:
    std::unordered_map<int, const TerrainCell*> owned_;
    const std::unordered_map<int, const TerrainCell*>* terrain_;
    bool reference_, generated_ = false;
};

struct RuntimeStrictSlotVehicleCertificateStats {
    std::uint64_t attempts=0, admitted=0, referenceFallbacks=0, guardFallbacks=0;
    std::uint64_t layoutFallbacks=0, slotFallbacks=0, walkFallbacks=0, allocationFallbacks=0;
    std::uint64_t layoutEntries=0, vertices=0, orientedEdges=0, strictSlots=0;
    void add(const RuntimeStrictSlotVehicleCertificateStats& s) noexcept {
        attempts+=s.attempts; admitted+=s.admitted; referenceFallbacks+=s.referenceFallbacks;
        guardFallbacks+=s.guardFallbacks; layoutFallbacks+=s.layoutFallbacks;
        slotFallbacks+=s.slotFallbacks; walkFallbacks+=s.walkFallbacks;
        allocationFallbacks+=s.allocationFallbacks; layoutEntries+=s.layoutEntries;
        vertices+=s.vertices; orientedEdges+=s.orientedEdges; strictSlots+=s.strictSlots;
    }
};

class RuntimeMixedLocalComponentCertificate {
    const RuntimeGameSessionState* state_=nullptr;
    const std::unordered_map<int,const TerrainCell*>* terrain_=nullptr;
    const std::unordered_map<int,int>* occupancy_=nullptr;
    std::optional<WorldObjectInstance> mover_;
    std::unordered_map<int,unsigned char> vertices_;
    // A failed seed/edge is never published as a connected vertex. Only
    // successful mutual edges may extend the root SCC, including during detours.
    std::unordered_map<int,bool> seeds_;
    std::unordered_map<int,unsigned char> failedEdges_;
    // Independent ORIGINAL directed truth. A failed mutual edge is not evidence
    // that its forward direction failed. Destination+direction identifies raw
    // predecessor coordinates exactly, including the negative boundary cases.
    std::unordered_map<std::uint64_t,bool> incomingEdges_;
    std::unordered_set<int> knownUnmapped_;
    bool knownUnmappedEnabled_=false;
    friend struct RuntimeMixedLocalKnownUnmappedContractAccess;
    std::unordered_set<int> desiredKeys_;
    // Only complete membership checks may publish a new covered centre. Its
    // points were all classified BEFORE assignment; no new graph truth here.
    std::unordered_set<int> closedDesiredKeys_;
    std::size_t frontierClosureAttempts_=0,frontierClosureLimit_=0;
    std::vector<int> ids_;
    bool enabled_=false,attempted_=false;
    RuntimeMixedLocalComponentStats* stats_=nullptr;
    int tick_=0;
    std::size_t vertexLimit_=4096,edgeLimit_=32768,edgeCount_=0;
    RuntimeCellPoint lastGoal_{};
    int rejectionReason_=0,rejectionActor_=-1;
    enum Rejection { None,Reference,GlobalGuard,DuplicateSelected,CargoSelected,
        ActorIdentity,ActorTransport,ActorMission,ActorMoveEligibility,ActorShroud,
        ActorRules,ActorDeployed,ActorBehavior,Representative,Layout,Metadata,
        Limits,Departure,SeedLimit,SeedVeto,EdgeLimit,Disconnected,Allocation,Retry,MovableCount };
    void rejection(int reason,int actor=-1) noexcept {
        rejectionReason_=reason;rejectionActor_=actor;
        if(stats_) {stats_->rejectionReason=reason;stats_->rejectionActor=actor;}
    }
    static const char* rejectionName(int reason) noexcept {
        static constexpr const char* names[]{"none","reference","global-guard","duplicate-selected","cargo-selected",
            "actor-identity","actor-transport","actor-mission","actor-move-eligibility","actor-shroud",
            "actor-rules","actor-deployed","actor-behavior","representative","layout","metadata",
            "limits","departure","seed-limit","seed-veto","edge-limit","disconnected","allocation","retry","movable-count"};
        return reason>=0 && reason<static_cast<int>(std::size(names))?names[reason]:"unknown";
    }
    static bool canonical(RuntimeCellPoint p) noexcept {
        return p.x>=0 && p.x<512 && p.y>=0 && p.y<512;
    }
    static bool oldObserversOrScopes() noexcept {
        return runtimeStrictSlotVehicleProofActive || runtimeStrictSlotVehicleStats
            || runtimeActivePlannerCellPassability || runtimeActivePlannerColdCellLookups
            || runtimeActivePlannerEmptyCellCensus || runtimeActivePlannerIndexedTopology
            || runtimeActivePlannerInvisibleBuildings
            || runtimeActivePlannerLandPermission || runtimeActiveBuildingDepartureReadOnlyQueries
            || runtimePlannerPassabilityObserver || runtimePlannerColdLookupObserver
            || runtimePlannerEmptyCensusObserver || runtimePlannerIndexedTopologyObserver
            || runtimePlannerMetadataBorrowObserver || runtimePlannerKernelLeaseObserver
            || runtimePlannerCrushAliasObserver || runtimePlannerEmptySubcellBooleanObserver
            || runtimeCellGateScratchStats;
    }
    // Literal original geometric schedule. No score/passability/reservation
    // query changes its FIFO or insertion-cap behavior in Best.
    template<class Visitor> static bool probes(RuntimeCellPoint desired,Visitor&& visit) {
        std::deque<RuntimeCellPoint> frontier;
        std::unordered_set<int> visited;
        frontier.push_back(desired); visited.insert(runtimeCellKey(desired.x,desired.y));
        while(!frontier.empty() && visited.size()<320) {
            const auto current=frontier.front(); frontier.pop_front();
            if(std::abs(current.x-desired.x)<=16 && std::abs(current.y-desired.y)<=16
                && !visit(current)) return false;
            for(const auto& next:runtimeNeighborsOf(current)) {
                if(std::abs(next.x-desired.x)>16 || std::abs(next.y-desired.y)>16) continue;
                const int key=runtimeCellKey(next.x,next.y);
                if(key<0 || visited.contains(key)) continue;
                visited.insert(key); frontier.push_back(next);
            }
        }
        return true;
    }
    bool seed(RuntimeCellPoint point) {
        const int key=runtimeCellKey(point.x,point.y);
        if(vertices_.contains(key)) return true;
        if(!canonical(point))return false;
        const auto known=seeds_.find(key);if(known!=seeds_.end())return known->second;
        // Bounded failed candidates as well as connected vertices. Exhaustion
        // abandons this certificate and selects the complete original builder.
        if(seeds_.size()>=4096) {rejection(SeedLimit);return false;}
        const bool okay=runtimeCellIsPassable(*state_,*terrain_,*occupancy_,point,*mover_,-1,nullptr,false);
        seeds_.emplace(key,okay);if(!okay && stats_)++stats_->seedVetoes;
        return okay;
    }
    bool mutualEdge(RuntimeCellPoint from,RuntimeCellPoint to) {
        const int key=runtimeCellKey(from.x,from.y),nextKey=runtimeCellKey(to.x,to.y);
        const int d=(to.y-from.y+1)*3+(to.x-from.x+1);
        const unsigned char bit=static_cast<unsigned char>(1U<<(d<4?d:d-1));
        if(vertices_.at(key)&bit)return true;
        const auto failed=failedEdges_.find(key);
        if(failed!=failedEdges_.end() && (failed->second&bit))return false;
        if(!seed(to))return false;
        if(!vertices_.contains(nextKey) && vertices_.size()+knownUnmapped_.size()>=vertexLimit_) {rejection(SeedLimit);return false;}
        if(edgeCount_+2>edgeLimit_) {rejection(EdgeLimit);return false;}
        ++edgeCount_;if(stats_)++stats_->orientedEdges;
        bool okay=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,from,to,*mover_);
        if(okay) {++edgeCount_;if(stats_)++stats_->orientedEdges;
            okay=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,to,from,*mover_);}
        if(!okay) {failedEdges_[key]|=bit;if(stats_)++stats_->edgeVetoes;return false;}
        // Publication occurs AFTER both original directions succeed. `contains`
        // therefore always means fully connected, never just seed-tested.
        const auto inserted=vertices_.emplace(nextKey,0);if(inserted.second && stats_)++stats_->vertices;
        vertices_.at(key)|=bit;
        const int reverse=8-d;
        vertices_.at(nextKey)|=static_cast<unsigned char>(1U<<(reverse<4?reverse:reverse-1));
        return true;
    }
    bool walk(RuntimeCellPoint goal) {
        if(!canonical(goal)) return false;
        if(vertices_.contains(runtimeCellKey(goal.x,goal.y))) {
            if(stats_) ++stats_->connectedGoalHits;
            lastGoal_=goal;
            return true;
        }
        if(!seed(goal)) {if(rejectionReason_==None)rejection(SeedVeto);return false;}
        const RuntimeCellPoint root{mover_->x,mover_->y};
        if(vertices_.empty()) {
            if(!seed(root)) {if(rejectionReason_==None)rejection(SeedVeto);return false;}
            vertices_.emplace(runtimeCellKey(root.x,root.y),0);if(stats_)++stats_->vertices;
            lastGoal_=root;
        }
        if(goal==root) {lastGoal_=goal;return true;}
        // Usually the next FIFO frontier point already borders the certified
        // region. Try these edges before allocating a bounded detour frontier.
        for(const auto& from:runtimeNeighborsOf(goal)) {
            if(!canonical(from) || !vertices_.contains(runtimeCellKey(from.x,from.y)))continue;
            if(stats_)++stats_->walkSteps;
            if(mutualEdge(from,goal)) {lastGoal_=goal;return true;}
            if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
        }
        struct Candidate {RuntimeCellPoint point;int distance;std::size_t serial;};
        const auto later=[](const Candidate& a,const Candidate& b) {
            return a.distance!=b.distance?a.distance>b.distance:a.serial>b.serial;};
        std::priority_queue<Candidate,std::vector<Candidate>,decltype(later)> frontier(later);
        std::unordered_set<int> expanded;std::size_t serial=0;
        const auto distance=[&](RuntimeCellPoint p){return std::abs(goal.x-p.x)+std::abs(goal.y-p.y);};
        frontier.push({lastGoal_,distance(lastGoal_),serial++});
        while(!frontier.empty()) {
            const auto current=frontier.top().point;frontier.pop();
            if(!expanded.insert(runtimeCellKey(current.x,current.y)).second)continue;
            if(stats_)++stats_->detourExpansions;
            if(expanded.size()>4096) {rejection(SeedLimit);return false;}
            auto neighbors=runtimeNeighborsOf(current);
            // Fixed eight-entry stable insertion sort: no per-vertex scratch
            // allocation, and equal heuristic distances keep original order.
            for(std::size_t i=1;i<neighbors.size();++i) {
                const auto value=neighbors[i];std::size_t at=i;
                while(at && distance(value)<distance(neighbors[at-1])) {neighbors[at]=neighbors[at-1];--at;}
                neighbors[at]=value;
            }
            for(const auto& next:neighbors) {
                if(!canonical(next))continue;
                if(stats_)++stats_->walkSteps;
                if(!mutualEdge(current,next)) {
                    if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
                    continue;
                }
                if(next==goal) {lastGoal_=goal;return true;}
                if(!expanded.contains(runtimeCellKey(next.x,next.y)))frontier.push({next,distance(next),serial++});
            }
        }
        rejection(Disconnected);return false;
    }
    bool knownUnmapped(RuntimeCellPoint point) {
        if(!knownUnmappedEnabled_ || runtimeMixedKnownUnmappedReference() || !canonical(point))return false;
        const int key=runtimeCellKey(point.x,point.y);
        if(knownUnmapped_.contains(key)) {if(stats_)++stats_->unmappedMemoHits;return true;}
        if(vertices_.contains(key))return false;
        if(stats_)++stats_->unmappedAttempts;
        // Original seed=false alone is insufficient: crush=true or a directed
        // edge may still admit this coordinate before its failed seed is seen.
        if(seed(point))return false;
        if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
        if(vertices_.size()+knownUnmapped_.size()>=vertexLimit_) {rejection(SeedLimit);return false;}
        const auto predecessors=runtimeNeighborsOf(point);
        for(std::size_t direction=0;direction<predecessors.size();++direction) {
            const auto edgeKey=(static_cast<std::uint64_t>(static_cast<unsigned>(key))<<3)|direction;
            const auto found=incomingEdges_.find(edgeKey);
            bool incoming;
            if(found!=incomingEdges_.end()) {incoming=found->second;if(stats_)++stats_->incomingMemoHits;}
            else {
                if(edgeCount_>=edgeLimit_) {rejection(EdgeLimit);return false;}
                ++edgeCount_;if(stats_){++stats_->orientedEdges;++stats_->incomingQueries;}
                // Identical original non-Air complete-kernel callback: implicit
                // mover mode, allowed=-1 (original departure lookup), soft=null,
                // crush=true; original diagonal corners remain crush=false.
                incoming=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,
                    predecessors[direction],point,*mover_);
                incomingEdges_.emplace(edgeKey,incoming);
            }
            if(incoming) {if(stats_)++stats_->incomingPositiveVetoes;return false;}
        }
        // The complete builder can label a key only as its canonical seed or
        // after a successful incoming edge. Both possibilities are now false.
        // Publication is separate from root-SCC vertices and happens only after
        // all eight predicates succeed as NEGATIVE proofs. No -1 map entry.
        knownUnmapped_.insert(key);
        if(stats_) {
            ++stats_->unmappedProofs;
            if(runtimeMixedLocalComponentExpectedMap) {
                ++stats_->unmappedOriginalChecks;
                if(std::any_of(runtimeMixedLocalComponentExpectedMap->begin(),runtimeMixedLocalComponentExpectedMap->end(),
                        [key](const auto& entry){return entry.first==key;}))++stats_->unmappedOriginalMismatches;
            }
        }
        return true;
    }
public:
    RuntimeMixedLocalComponentCertificate()=default;
    RuntimeMixedLocalComponentCertificate(const RuntimeMixedLocalComponentCertificate&)=delete;
    RuntimeMixedLocalComponentCertificate& operator=(const RuntimeMixedLocalComponentCertificate&)=delete;
    bool enabled() const noexcept { return enabled_; }
    void invalidate() noexcept {
        enabled_=false;knownUnmappedEnabled_=false;
        closedDesiredKeys_.clear();
        knownUnmapped_.clear();incomingEdges_.clear();
    }
    const WorldObjectInstance* originalMover() const noexcept { return mover_?&*mover_:nullptr; }
    const auto& certifiedKeys() const noexcept { return vertices_; }
    const auto& knownUnmappedKeys() const noexcept {return knownUnmapped_;}
    const std::unordered_set<int>* bestUnmappedFilter() const noexcept {
        return enabled_ && knownUnmappedEnabled_ && state_ && state_->tick==tick_
            && !runtimeMixedLocalComponentReference() && !oldObserversOrScopes()
            && !runtimeMixedKnownUnmappedReference()
            && !knownUnmapped_.empty()?&knownUnmapped_:nullptr;
    }
    bool prepare(const RuntimeGameSessionState& state,const std::vector<int>& ids,RuntimeCellPoint target,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,int representativeId,std::size_t movableCount,
        std::size_t vertexLimit=4096,std::size_t edgeLimit=32768) {
        if(attempted_) {
            enabled_=false;knownUnmappedEnabled_=false;
            closedDesiredKeys_.clear();
            knownUnmapped_.clear();incomingEdges_.clear();
            if(runtimeMixedLocalComponentStats) {
                ++runtimeMixedLocalComponentStats->attempts;
                ++runtimeMixedLocalComponentStats->guardFallbacks;
                runtimeMixedLocalComponentStats->rejectionReason=Retry;
            }
            return false;
        }
        attempted_=true;
        enabled_=false; state_=nullptr; terrain_=nullptr; occupancy_=nullptr;
        mover_.reset(); vertices_.clear(); seeds_.clear(); failedEdges_.clear(); desiredKeys_.clear(); ids_.clear(); edgeCount_=0;
        closedDesiredKeys_.clear();frontierClosureAttempts_=0;
        frontierClosureLimit_=std::min<std::size_t>(ids.size(),256);
        incomingEdges_.clear();knownUnmapped_.clear();
        knownUnmappedEnabled_=!runtimeMixedKnownUnmappedReference();
        rejectionReason_=None;rejectionActor_=-1;
        stats_=runtimeMixedLocalComponentStats; if(stats_) ++stats_->attempts;
        // One coarse diagnostic per command, only when the existing Trace is
        // requested. No callbacks, clocks, or output in the graph hot loop.
        struct Diagnostic {
            RuntimeMixedLocalComponentCertificate& owner;const RuntimeGameSessionState& state;
            RuntimeCellPoint target;std::size_t selected,movable;
            ~Diagnostic() noexcept {
                try {
                static const bool requested=std::getenv("CONCORD_RUNTIME_PERF_TRACE")!=nullptr;
                if(!requested)return;
                const auto* actor=owner.rejectionActor_>=0?findObjectById(state,owner.rejectionActor_):nullptr;
                std::cerr<<"Runtime local component admission: tick="<<state.tick<<" target="<<target.x<<','<<target.y
                    <<" selected="<<selected<<" movable="<<movable<<" admitted="<<owner.enabled_
                    <<" rejection="<<owner.rejectionName(owner.rejectionReason_)<<" actor="<<owner.rejectionActor_
                    <<" mission="<<(actor?actor->mission.c_str():"")
                    <<" vertices="<<owner.vertices_.size()<<" seedTests="<<owner.seeds_.size()
                    <<" orientedEdges="<<owner.edgeCount_<<'\n';
                } catch(...) { /* Optional diagnostics must not defeat fallback. */ }
            }
        } diagnostic{*this,state,target,ids.size(),movableCount};
        const auto guardFail=[&](int reason=GlobalGuard,int actor=-1) {
            rejection(reason,actor);if(stats_)++stats_->guardFallbacks;return false;};
        if(runtimeMixedLocalComponentReference()) { rejection(Reference);if(stats_) ++stats_->referenceFallbacks; return false; }
        if(runtimeMixedLocalComponentProofActive || oldObserversOrScopes()
            || runtimeFrameScalingReferenceMovement() || state.config.multiplayerActive
            || runtimeComponentPassabilityReference() || runtimeComponentKernelReference()
            || runtimeComponentTerrainQueryReference() || runtimeComponentQueryLeaseReference()
            || ids.size()<12 || ids.size()>256 || movableCount<12 || movableCount>ids.size()
            || representativeId!=ids.front() || !std::is_sorted(ids.begin(),ids.end())
            || std::adjacent_find(ids.begin(),ids.end())!=ids.end() || !canonical(target)
            || !state.specialMechanismsInitialized || !state.repairDepots.relations().empty()
            || !state.grinding.relations().empty() || !state.specialMechanisms.parasiteLinks().empty()
            || !state.specialMechanisms.mindControlLinks().empty() || !state.specialMechanisms.temporalTargets().empty()
            || !state.specialMechanisms.carrierParents().empty() || !state.specialMechanisms.carrierChildren().empty()
            || !state.combatDetails.panics.empty() || !state.combatDetails.diskDrains.sources.empty()
            || !state.pendingEngineerCaptures.empty() || !state.pendingEngineerActions.empty()
            || !state.pendingOccupyTargets.empty() || !state.pendingTransportTargets.empty()
            || !state.pendingTimedBombIntents.empty() || !state.gateIdsByCell.empty()) return guardFail();
        struct Active { bool previous=runtimeMixedLocalComponentProofActive;
            Active() { runtimeMixedLocalComponentProofActive=true; }
            ~Active() { runtimeMixedLocalComponentProofActive=previous; } } active;
        RuntimePerfTraceScope trace("moveCommand.localComponentCertificate",state.tick);
        try {
            std::unordered_set<int> selected(ids.begin(),ids.end()),seen;
            for(const auto& object:state.world.getObjects()) {
                if(selected.contains(object.instanceId) && !seen.insert(object.instanceId).second) return guardFail(DuplicateSelected,object.instanceId);
                for(int passenger:object.transportPassengerIds) if(selected.contains(passenger)) return guardFail(CargoSelected,passenger);
            }
            std::vector<const WorldObjectInstance*> actors; actors.reserve(ids.size());
            std::size_t eligible=0;
            for(int id:ids) {
                const auto* actor=findObjectById(state,id);
                if(!actor || id<0 || !actor->alive || actor->health<=0
                    || (actor->kind!=WorldObjectKind::Unit && actor->kind!=WorldObjectKind::Infantry)
                    || actor->owner!=state.economy.owner || !canonical({actor->x,actor->y})) return guardFail(ActorIdentity,id);
                if(actor->submerged || actor->dockedToObjectId>=0 || actor->dockIndex>=0
                    || actor->tankBunkerHostId>=0 || actor->garrisonHostObjectId>=0
                    || !actor->transportPassengerIds.empty() || !actor->garrisonOccupantIds.empty())return guardFail(ActorTransport,id);
                const auto* unit=findRuntimeUnitType(state.rules,*actor);
                if(!unit || unit->harvester || runtimeObjectIsMobileSlaveMiner(state,*actor))return guardFail(ActorRules,id);
                const bool canReceive=runtimeObjectCanReceiveMoveCommand(state,*actor);
                // The rejected recovery actor is left in the ORIGINAL captured
                // occupancy. It never consumes a slot or receives an assignment.
                const bool recovery=!canReceive && unit->teleporter && !unit->harvester
                    && state.specialMechanisms.isChronoMovementLocked(id,state.tick)
                    && !state.specialMechanisms.isChronoInTransit(id) && actor->mission=="ChronoFrozen";
                const bool idle=actor->mission=="Idle" || actor->mission=="Idle1" || actor->mission=="Idle2";
                if(actor->mission!="Guard" && actor->mission!="Move" && !idle && !recovery)return guardFail(ActorMission,id);
                if(!canReceive && !recovery)return guardFail(ActorMoveEligibility,id);
                if(!runtimeObjectAllowsMoveToCell(state,*actor,target))return guardFail(ActorShroud,id);
                if(runtimeObjectOffGroundByMagBeam(state,id)
                    || runtimeObjectManagedByAirstrike(state,id))return guardFail(ActorMoveEligibility,id);
                if(runtimeObjectHasDeployedState(state,*actor))return guardFail(ActorDeployed,id);
                const auto behavior=state.behaviors.find(id);
                if(behavior!=state.behaviors.end() && (behavior->second.kind!=RuntimeBehaviorKind::Move
                    || behavior->second.returnToDeployed))return guardFail(ActorBehavior,id);
                if(recovery) {if(stats_)++stats_->qualifiedChronoRecovery;}
                else ++eligible;
                if(idle && stats_)++stats_->idleActors;
                actors.push_back(actor);
            }
            if(eligible!=movableCount)return guardFail(MovableCount);
            const auto* rootUnit=findRuntimeUnitType(state.rules,*actors.front());
            const auto& rootType=actors.front()->type;
            const bool stockGroundRoot=rootType=="MTNK" || rootType=="SREF" || rootType=="TTNK" || rootType=="FV" || rootType=="HTNK";
            if(actors.front()->kind!=WorldObjectKind::Unit || !stockGroundRoot || !rootUnit || rootUnit->teleporter
                || actors.front()->airborne || runtimeObjectMovementMode(state,*actors.front())!=PathMovementMode::Ground)
                return guardFail(Representative,actors.front()->instanceId);
            // A warm wrapper table is an exact pointer index. Do not build it
            // here just to validate admission; a cold wrapper uses original find.
            const auto* metadataPointers=state.terrainInfoByCell.denseValid()
                ? &state.terrainInfoByCell.densePointers() : nullptr;
            for(const auto& [key,value]:terrain) {
                if(stats_) ++stats_->layoutEntries;
                if(!value || !canonical({value->x,value->y}) || key!=runtimeCellKey(value->x,value->y)) {
                    rejection(Layout);
                    if(stats_) ++stats_->layoutFallbacks; return false;
                }
                const bool metadataPresent=metadataPointers
                    ? (*metadataPointers)[static_cast<std::size_t>(value->y)*512+value->x]!=nullptr
                    : state.terrainInfoByCell.contains(key);
                if(!metadataPresent) { rejection(Metadata);if(stats_) ++stats_->layoutFallbacks; return false; }
            }
            state_=&state; terrain_=&terrain; occupancy_=&occupancy; tick_=state.tick;
            mover_=*actors.front(); ids_=ids;
            vertexLimit_=std::min<std::size_t>(vertexLimit,4096);
            edgeLimit_=std::min<std::size_t>(edgeLimit,32768);
            if(!vertexLimit_ || !edgeLimit_) return guardFail(Limits);
            RuntimeBuildingDepartureReadOnlyQueryScope departure(state,*mover_);
            if(runtimeStructureDepartureHostId(state,*mover_)>=0) return guardFail(Departure);
            const auto walkFail=[&] {
                knownUnmapped_.clear();incomingEdges_.clear();
                if(stats_) ++stats_->walkFallbacks; return false;
            };
            for(const auto* actor:actors) {
                const auto* unit=findRuntimeUnitType(state.rules,*actor);
                if(runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Air && !unit->teleporter
                    && !walk({actor->x,actor->y})) return walkFail();
            }
            const std::size_t formations=std::min(ids.size(),runtimeMixedLocalComponentFormationLimit);
            for(std::size_t i=0;i<formations;++i) {
                const auto offset=formationOffsetForIndex(static_cast<int>(i),1);
                const RuntimeCellPoint desired{target.x+offset.x,target.y+offset.y};
                if(!canonical(desired) || !probes(desired,[&](RuntimeCellPoint p) {
                        return walk(p) || knownUnmapped(p);
                    })) return walkFail();
                desiredKeys_.insert(runtimeCellKey(desired.x,desired.y));
            }
            if(formations==0) return guardFail();
            if(knownUnmappedEnabled_)rejection(None);
            enabled_=true; if(stats_) ++stats_->admitted; return true;
        } catch(const std::bad_alloc&) {
            knownUnmapped_.clear();incomingEdges_.clear();enabled_=false;
            rejection(Allocation);if(stats_) ++stats_->allocationFallbacks; return false;
        }
    }
    // Values are already proven for this closed command. No query lease survives
    // assignment, and this does not claim pointer/count to be a content revision.
    bool coversBest(RuntimeCellPoint desired) {
        if(!enabled_ || runtimeMixedLocalComponentReference() || oldObserversOrScopes()
            || !state_ || state_->tick!=tick_
            || (!knownUnmapped_.empty() && runtimeMixedKnownUnmappedReference())) return false;
        if(!canonical(desired))return false;
        const int key=runtimeCellKey(desired.x,desired.y);
        // The original prepare-time centres keep their original exact fast path.
        if(desiredKeys_.contains(key)) {if(stats_)++stats_->coveredBest;return true;}
        if(!knownUnmappedEnabled_ || runtimeMixedKnownUnmappedReference())return false;
        if(runtimeMixedBestFrontierClosureReference()) {
            if(stats_)++stats_->frontierClosureReferenceVetoes;
            return false;
        }
        if(closedDesiredKeys_.contains(key)) {
            if(stats_) {++stats_->frontierClosureMemoHits;++stats_->coveredBest;}
            return true;
        }
        if(frontierClosureAttempts_>=frontierClosureLimit_) {
            if(stats_)++stats_->frontierClosureRejected;
            return false;
        }
        ++frontierClosureAttempts_;
        if(stats_)++stats_->frontierClosureAttempts;
        try {
            // Replay EVERY original FIFO candidate, including score-pruned ones.
            // Only frozen membership is consulted. In particular, this never
            // calls seed/walk/incoming or current World/metadata/rule queries.
            const bool complete=probes(desired,[&](RuntimeCellPoint point) {
                if(stats_)++stats_->frontierClosurePoints;
                if(!canonical(point))return false;
                const int candidateKey=runtimeCellKey(point.x,point.y);
                return vertices_.contains(candidateKey) || knownUnmapped_.contains(candidateKey);
            });
            if(!complete) {
                if(stats_)++stats_->frontierClosureRejected;
                return false;
            }
            closedDesiredKeys_.insert(key);
            if(stats_) {++stats_->frontierClosureAdmissions;++stats_->coveredBest;}
            return true;
        } catch(const std::bad_alloc&) {
            if(stats_)++stats_->frontierClosureAllocationFallbacks;
            return false;
        }
    }
    static std::vector<RuntimeCellPoint> probePointsForContract(RuntimeCellPoint desired) {
        std::vector<RuntimeCellPoint> output;
        (void)probes(desired,[&](RuntimeCellPoint p) { output.push_back(p); return true; });
        return output;
    }
    void noteWholeMap(const std::unordered_map<int,int>& map,bool nearest=false) {
        if(!stats_) return;
        ++stats_->wholeMaps;
        if(enabled_) {
            ++stats_->lateWholeMaps;
            if(nearest) ++stats_->nearestFallbacks; else ++stats_->outsideFallbacks;
            for(int id:ids_) {
                const auto* actor=findObjectById(*state_,id); if(!actor) continue;
                if(state_->specialMechanisms.isChronoInTransit(id)) { ++stats_->lateChronoWitness; break; }
            }
            for(int id:ids_) {
                const auto* actor=findObjectById(*state_,id); if(!actor) continue;
                const auto behavior=state_->behaviors.find(id);
                if(runtimeObjectMovementMode(*state_,*actor)==PathMovementMode::Air
                    && behavior!=state_->behaviors.end() && behavior->second.kind==RuntimeBehaviorKind::Move) {
                    ++stats_->lateAirWitness; break;
                }
            }
        }
        if(runtimeMixedLocalComponentExpectedMap) {
            ++stats_->orderedMapsCompared;
            const auto& expected=*runtimeMixedLocalComponentExpectedMap;
            auto it=map.begin(); bool equal=map.size()==expected.size();
            for(const auto& item:expected) {
                if(it==map.end() || it->first!=item.first || it->second!=item.second) { equal=false; break; }
                ++it;
            }
            if(!equal) ++stats_->orderedMapMismatches;
        }
    }
};

template<class Session>
class RuntimeSessionTriggerOperations final : public RuntimeTriggerOperations {
public:
    explicit RuntimeSessionTriggerOperations(Session& session) : session_(session) {}
    void sampleLightingThresholdEvents() override { sampleRuntimeLightingThresholdEvents(session_); }
    int campaignIonStrikeDelayTicks() override { return runtimeCampaignIonStrikeDelayTicks(session_); }
    bool fireCampaignIonStrike(const RuntimeCellPoint& cell, const std::string& owner) override { return fireRuntimeCampaignIonStrike(session_, cell, owner); }
    bool campaignEventSatisfied(const RuntimeTriggerDefinition& trigger, const RuntimeTriggerEvent& event) override { return runtimeCampaignEventSatisfied(session_, trigger, event); }
    const WorldObjectInstance* objectIncludingInactive(int objectId) override { return findRuntimeObjectByIdIncludingInactive(session_, objectId); }
    bool campaignHouseMatches(const std::string& authoredHouse, const std::string& objectHouse) override { return runtimeCampaignHouseMatches(session_, authoredHouse, objectHouse); }
    bool applyCampaignAction(RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) override { return applyRuntimeCampaignAction(session_, trigger, action); }
    void snapshotCampaignObservations() override { snapshotRuntimeCampaignTriggerObservations(session_); }
    bool advanceReinforcements() override { return advanceRuntimeTriggerReinforcements(session_); }
    bool advanceTeamMissions() override { return advanceRuntimeTeamMissions(session_); }
    bool advanceCampaignAiProduction() override { return advanceRuntimeCampaignAiProduction(session_); }
    bool fireSuperWeapon(const RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) override { return fireRuntimeTriggerSuperWeapon(session_, trigger, action); }
    bool fireCreateTeam(RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) override { return fireRuntimeCreateTeamTrigger(session_, trigger, action); }
    const RuntimeTeamType* chooseTeam() override { return chooseRuntimeTriggerTeam(session_); }
    bool cooperativeActive() override { return session_.cooperativeSession.active(); }
private:
    Session& session_;
};

bool runtimeMapQueryPassabilityAllows(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& mover,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt,
    const RuntimePlannerColdCellLookups::ReadOnlyView* coldLookups = nullptr
);


template<class Session>
RuntimeVisionPhaseContext runtimeVisionPhaseContext(Session& session) {
    return {session.world,session.rules,session.rulesLoaded,session.visionEnabled,
        session.vision,session.config,session.economy.owner,session.tick,
        session.triggers.publicBuildingLocations,session.perf.lastSimVisionMs};
}


template<class Visitor>
void forEachRuntimeAllGroundBodyCandidateAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit) {
    const auto& objects=state.world.getObjects();
    if(cell.x>=0 && cell.y>=0 && cell.x<512 && cell.y<512
        && runtimeAllGroundBodySpatialIndexIsCurrent(state)) {
        const auto& buckets=state.movementEmptyCellProof.allGroundBodyIndicesByCell;
        const auto found=buckets.find(runtimeCellKey(cell.x,cell.y));
        if(found!=buckets.end()) for(std::size_t index:found->second)
            if(index<objects.size()) visit(objects[index]);
    } else {
        // Invalid or unusual metadata retains the complete scalar safety path.
        for(const auto& object:objects) visit(object);
    }
}


template<class Visitor>
void forEachRuntimeGroundPoseCandidateAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits) {
    const auto& objects=state.world.getObjects();
    const auto inspect=[&](const WorldObjectInstance& object) {
        if(candidateVisits) ++*candidateVisits;visit(object);
    };
    if(cell.x>=0 && cell.y>=0 && cell.x<512 && cell.y<512
        && runtimeGroundPoseSpatialIndexIsCurrent(state)) {
        const auto& buckets=state.movementEmptyCellProof.additionalGroundPoseIndicesByCell;
        const auto found=buckets.find(runtimeCellKey(cell.x,cell.y));
        if(found!=buckets.end()) for(std::size_t index:found->second)
            if(index<objects.size()) inspect(objects[index]);
    } else {
        // Preserve the scalar safety path when metadata is absent or unusual.
        for(const auto& object:objects) if(object.hasRenderPosition) inspect(object);
    }
}


template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupantAtCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits) {
    const auto context=runtimeGroundPoseContext(state);
    forEachRuntimeGroundPoseCandidateAtCell(state,cell,[&](const auto& object) {
        if(runtimeObjectOffGroundByMagBeam(state,object.instanceId)
            || runtimeObjectMovementMode(state,object)==PathMovementMode::Air) return;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
            if(held.x==cell.x && held.y==cell.y) visit(object,held);
        },&context);
    },candidateVisits);
}


inline bool runtimeComponentMetadataBorrowReference() {
    if (runtimeComponentMetadataBorrowReferenceOverride >= 0)
        return runtimeComponentMetadataBorrowReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_METADATA_BORROW_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}


inline bool runtimeComponentTerrainQueryReference() {
    if (runtimeComponentTerrainQueryReferenceOverride >= 0)
        return runtimeComponentTerrainQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_TERRAIN_QUERY_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}


inline bool runtimeComponentColdLookupReference() {
    if (runtimeComponentColdLookupReferenceOverride >= 0)
        return runtimeComponentColdLookupReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_COLD_LOOKUP_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}


inline bool runtimeComponentIndexedTopologyReference() noexcept {
    if(runtimeComponentIndexedTopologyReferenceOverride>=0)
        return runtimeComponentIndexedTopologyReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_COMPONENT_INDEXED_TOPOLOGY_REFERENCE");
        // The native mixed-unit command measurements showed that preparing
        // this geometry table costs more than it saves on the first BFS.
        // Keep it opt-in for the exact contract and isolated measurements.
        return !(value && value[0]=='0' && value[1]=='\0');
    }();
    return reference;
}


template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupant(const RuntimeGameSessionState& state, Visitor visit) {
    const auto& objects=state.world.getObjects();
    const auto& proof=state.movementEmptyCellProof;
    const auto poseContext=runtimeGroundPoseContext(state);
    const auto inspect=[&](const WorldObjectInstance& object) {
        if (runtimeObjectOffGroundByMagBeam(state,object.instanceId)
            || runtimeObjectMovementMode(state,object)==PathMovementMode::Air) return;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) { visit(object,held); },&poseContext);
    };
    if (state.tickOccupancyCacheTick==state.tick && proof.objectData==objects.data()
        && proof.objectCount==objects.size()) {
        for (std::size_t index:proof.additionalGroundPoseObjectIndices)
            if (index<objects.size()) inspect(objects[index]);
    } else {
        for (const auto& object:objects) {
            if (!object.hasRenderPosition) continue;
            inspect(object);
        }
    }
}


template<class UnexpectedOccupant>
bool restoreRuntimeAllowedPathFoundation(const RuntimeGameSessionState& state,
    const RuntimePathTerrainQueryScope& query, PathMovementMode movementMode,
    UnexpectedOccupant&& unexpectedOccupant, std::vector<PathTerrainCell>& snapshot) {
    const auto& cache = state.pathTerrainCache;
    if (runtimeSimulationQueryReuseReference() || !runtimePathSnapshotHasCellIndex(cache, snapshot))
        return false;
    std::vector<std::size_t> candidates;
    for (const auto& allowedCell : query.allowedCells()) {
        const auto first = cache.staticFirstIndexByCell.find(runtimeCellKey(allowedCell.x, allowedCell.y));
        if (first == cache.staticFirstIndexByCell.end()) continue;
        for (auto index = first->second; index != std::numeric_limits<std::size_t>::max();
                index = cache.staticNextIndexByCell[index]) {
            if (snapshot[index].cell.x == allowedCell.x && snapshot[index].cell.y == allowedCell.y)
                candidates.push_back(index);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const auto index : candidates) {
        auto& cell = snapshot[index];
        const RuntimeCellPoint point{cell.cell.x, cell.cell.y};
        if (query.allowedFootprint(point)
            && (movementMode == PathMovementMode::Air || !query.repairReserved(point)))
            cell.passable = movementMode == PathMovementMode::Air || !unexpectedOccupant(point);
    }
    return true;
}


inline bool runtimePathSnapshotMetadataQueryReference() {
    if (runtimePathSnapshotMetadataQueryReferenceOverride >= 0)
        return runtimePathSnapshotMetadataQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_SNAPSHOT_METADATA_QUERY_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}


constexpr bool runtimeProductionLanesSectionSchemaSupported(int schema) {
    return schema == runtimeProductionLanesSectionSchemaV1
        || schema == runtimeProductionLanesSectionSchemaV2
        || schema == runtimeProductionLanesSectionSchemaV3Alias;
}


constexpr bool runtimeProductionLanesModernSectionSchemaSupported(int schema) {
    return schema == runtimeProductionLanesSectionSchemaV2
        || schema == runtimeProductionLanesSectionSchemaV3Alias;
}


template<class Map> void writeRuntimeCooperativeObservationMap(std::ostream& output,
    const char* name, const Map& map) {
    std::vector<int> keys;
    for (const auto& entry : map) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    output << "\n[CooperativeObservation_" << name << "]\nCount=" << keys.size() << '\n';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        output << i << '=' << keys[i] << '|';
        if constexpr (std::is_same_v<typename Map::mapped_type, std::string>)
            output << iniHexEncode(map.at(keys[i]));
        else output << static_cast<int>(map.at(keys[i]));
        output << '\n';
    }
}


template<class Map> bool readRuntimeCooperativeObservationMap(const IniFile& ini,
    const char* name, Map& map, std::string& error) {
    const std::string section = std::string("CooperativeObservation_") + name;
    int count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0 || count > 1000000) {
        error = "invalid cooperative observation header " + section; return false;
    }
    Map result;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int key = -1;
        typename Map::mapped_type value{};
        if (fields.size() != 2 || !parseRuntimeInt(fields[0], key) || key < 0) {
            error = "invalid cooperative observation record " + section; return false;
        }
        if constexpr (std::is_same_v<typename Map::mapped_type, std::string>) {
            if (!iniHexDecode(fields[1], value, 4096)) {
                error = "invalid cooperative observation owner"; return false;
            }
        } else {
            int number = 0;
            if (!parseRuntimeInt(fields[1], number)) {
                error = "invalid cooperative observation value"; return false;
            }
            if constexpr (std::is_same_v<typename Map::mapped_type, bool>)
                if (number != 0 && number != 1) { error = "invalid cooperative alive flag"; return false; }
            value = static_cast<typename Map::mapped_type>(number);
        }
        if (!result.emplace(key, std::move(value)).second) {
            error = "duplicate cooperative observation"; return false;
        }
    }
    map = std::move(result);
    return true;
}


template <typename T>
void swapRuntimeLoadedField(T& live, T& loaded) noexcept {
    static_assert(
        std::is_nothrow_swappable_v<T>,
        "runtime load commit fields must be nothrow swappable"
    );
    using std::swap;
    swap(live, loaded);
}


std::string runtimeMultiplayerSnapshotCellsText(
    const auto& cells) {
    std::ostringstream output;
    bool first = true;
    for (const RuntimeCellPoint& cell : cells) {
        if (!first) output << ';';
        first = false;
        output << cell.x << ',' << cell.y;
    }
    return output.str();
}


std::string runtimeMultiplayerSnapshotIntsText(
    const auto& values,
    bool canonicalSort) {
    std::vector<int> sorted(values.begin(), values.end());
    if (canonicalSort) {
        std::sort(sorted.begin(), sorted.end());
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0U) output << ',';
        output << sorted[index];
    }
    return output.str();
}


inline bool runtimeStrictSlotVehicleReference() noexcept {
    if (runtimeStrictSlotVehicleReferenceOverride >= 0)
        return runtimeStrictSlotVehicleReferenceOverride != 0;
    static const bool reference = [] {
        const char* v = std::getenv("CONCORD_MOVE_STRICT_SLOT_CERTIFICATE_REFERENCE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return reference;
}


inline bool runtimeStrictSlotVehicleCanonical(const RuntimeCellPoint& p) noexcept {
    return p.x>=0 && p.x<512 && p.y>=0 && p.y<512;
}


inline bool runtimeStrictSlotVehicleStrictWinner(int softPenalty,std::uint64_t tie) noexcept {
    return softPenalty>=0 && softPenalty < 530-static_cast<int>(tie&31U);
}


inline bool runtimeMixedLocalComponentReference() noexcept {
    if(runtimeMixedLocalComponentReferenceOverride>=0)
        return runtimeMixedLocalComponentReferenceOverride!=0;
    static const bool value=[] { const char* p=std::getenv("CONCORD_MOVE_LOCAL_COMPONENT_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0'; }();
    return value;
}


inline bool runtimeMixedKnownUnmappedReference() noexcept {
    if(runtimeMixedKnownUnmappedReferenceOverride>=0)
        return runtimeMixedKnownUnmappedReferenceOverride!=0;
    static const bool value=[] {const char* p=std::getenv("CONCORD_MOVE_KNOWN_UNMAPPED_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0';}();
    return value;
}


inline bool runtimeMixedBestFrontierClosureReference() noexcept {
    if(runtimeMixedBestFrontierClosureReferenceOverride>=0)
        return runtimeMixedBestFrontierClosureReferenceOverride!=0;
    static const bool value=[] {const char* p=std::getenv("CONCORD_MOVE_BEST_FRONTIER_CLOSURE_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0';}();
    return value;
}


template<typename Apply>
bool applyRuntimeGameplaySelectionCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::vector<std::int32_t>& objectIds,
    Apply&& apply) {
    RuntimeHumanHouseScope houseScope(state, owner);
    if (!houseScope) {
        return false;
    }
    const std::vector<int> savedSelection = state.selectedObjectIds;
    const std::string savedInteractionStatus = state.interactionStatus;
    const std::string savedSimulationStatus = state.simulationStatus;
    const std::string savedEconomyStatus = state.economy.status;
    const RuntimeSidebarTool savedSidebarTool = state.activeSidebarTool;
    const bool savedAudioSuppression =
        state.runtimeSuppressPlayerAudioFeedback;
    const auto savedLastCommand = state.lastCommand;
    const bool preserveOfflineUi=!state.config.multiplayerActive && !houseScope.remote()
        && runtimeUsesGameplayIntentRouter(state);
    const auto restore = [&]() {
        const auto resultingSelection=preserveOfflineUi?state.selectedObjectIds:std::vector<int>{};
        state.selectedObjectIds.clear();
        for (int objectId : savedSelection) {
            const auto* object=findObjectById(state,objectId);
            if (object && (!preserveOfflineUi || object->alive)) {
                state.selectedObjectIds.push_back(objectId);
            }
        }
        if(preserveOfflineUi) {
            // Deploy/repack may replace selected IDs. Keep new actors while
            // retaining unaffected selections in their original UI order.
            for(int id:resultingSelection) {
                const auto* object=findObjectById(state,id);
                if(object && object->alive && std::find(objectIds.begin(),objectIds.end(),id)==objectIds.end()
                    && std::find(state.selectedObjectIds.begin(),state.selectedObjectIds.end(),id)==state.selectedObjectIds.end())
                    state.selectedObjectIds.push_back(id);
            }
            state.runtimeSuppressPlayerAudioFeedback=savedAudioSuppression;
            return;
        }
        state.interactionStatus = savedInteractionStatus;
        state.simulationStatus = savedSimulationStatus;
        state.economy.status = savedEconomyStatus;
        state.activeSidebarTool = savedSidebarTool;
        state.runtimeSuppressPlayerAudioFeedback = savedAudioSuppression;
        state.lastCommand = savedLastCommand;
    };
    state.selectedObjectIds.assign(objectIds.begin(), objectIds.end());
    state.runtimeSuppressPlayerAudioFeedback =
        savedAudioSuppression || houseScope.remote();
    try {
        const bool accepted = std::forward<Apply>(apply)();
        restore();
        return accepted;
    } catch (...) {
        restore();
        throw;
    }
}


template<class Apply>
bool applyRuntimePlayerSelectionCommand(RuntimeProductionHostState& state,
    const std::string& owner, const std::vector<std::int32_t>& objectIds, Apply&& apply) {
    // Recorded network commands retain their issuing house even when viewed
    // offline. A live socket must not determine simulation ownership.
    if (state.multiplayerBridge || state.config.multiplayerActive)
        return applyRuntimeGameplaySelectionCommand(state, owner, objectIds, std::forward<Apply>(apply));
    const auto selected = state.selectedObjectIds;
    state.selectedObjectIds.assign(objectIds.begin(), objectIds.end());
    try {
        const bool accepted = apply();
        state.selectedObjectIds = selected;
        return accepted;
    } catch (...) { state.selectedObjectIds = selected; throw; }
}


template <typename Predicate>
const WorldObjectInstance* runtimeTeamNearestStructure(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission,
    Predicate predicate) {
    const RuntimeCellPoint center = runtimeTeamMissionCenter(state, mission);
    const WorldObjectInstance* best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr || !predicate(object, *building)) {
            continue;
        }
        const double distance = runtimeCellDistance(
            center,
            runtimeCellPointForObject(object)
        );
        if (best == nullptr
            || distance < bestDistance
            || (distance == bestDistance
                && object.instanceId < best->instanceId)) {
            best = &object;
            bestDistance = distance;
        }
    }
    return best;
}


template<class Session>
RuntimeTriggerContext runtimeTriggerContext(Session& session) {
    return {session.triggers, session.world, session.config, session.tick,
        session.campaignMode, session.skirmishMatchFinished, session.repaintRequested};
}


bool replaceRuntimeMultiplayerSnapshotIniValue(
    std::vector<std::uint8_t>& bytes,
    const std::string& section,
    const std::string& key,
    const std::string& replacement
) ;

bool rebindRuntimeMultiplayerSaveAsLoad(
    const RuntimeProductionHostState& state,
    std::span<const std::uint8_t> sourceBytes,
    std::uint64_t proposalId,
    std::uint64_t sourceEpoch,
    int barrierTick,
    RuntimeMultiplayerSnapshotMemoryImage& image,
    std::string& errorMessage
) ;
}

namespace concord::runtime::session_detail {
bool loadRuntimeState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage
);
bool loadRuntimeCampaignState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage
);
}
