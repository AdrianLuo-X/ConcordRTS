# The game and portable capability compile the same owned production producer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

if(CONCORD_PORTABLE_CORE)
    set(_session_profile_owner concord_production_core_hash_smoke)
else()
    set(_session_profile_owner concord_runtime)
endif()

concord_add_profiled_leaf(concord_production_session_runtime_leaf
    src/runtime/session/RuntimeSessionTick.cpp ${_session_profile_owner} Runtime)
target_sources(concord_production_session_runtime_leaf PRIVATE
    src/runtime/session/RuntimeSessionCombat.cpp
    src/runtime/session/RuntimeSessionCommands.cpp
    src/runtime/session/RuntimeSessionFeedback.cpp
    src/runtime/session/RuntimeSessionNavigation.cpp
    src/runtime/session/RuntimeSessionPersistence.cpp
    src/runtime/session/RuntimeSessionProduction.cpp
    src/runtime/session/RuntimeSessionScenario.cpp
    src/runtime/session/RuntimeSessionWorld.cpp
    src/render/core/RuntimeProductionResources.cpp
    src/runtime/menu/RuntimeSkirmishMapResources.cpp)

if(NOT CONCORD_PORTABLE_CORE)
    target_sources(concord_runtime PRIVATE src/runtime/session/win32/RuntimeNativeSessionDevices.cpp)
    target_link_libraries(concord_runtime PRIVATE concord_production_session_runtime_leaf)
    unset(_session_profile_owner)
    return()
endif()

find_package(Threads REQUIRED)

# Existing portable world, codecs, transport and OS provider sources used by
# the production session. Native keeps its established target registrations.
concord_add_profiled_leaf(concord_production_session_support_runtime_leaf
    src/core/tasks/BoundedTaskExecutor.cpp ${_session_profile_owner} Runtime)
target_sources(concord_production_session_support_runtime_leaf PRIVATE
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
    src/assets/audio/AudioDecoder.cpp
    src/assets/audio/AudioSampleResolver.cpp
    src/assets/codecs/RuntimeTmpCodec.cpp
    src/assets/codecs/ShpCodec.cpp
    src/assets/terrain/RuntimeTerrainTmpQuery.cpp
    src/audio/events/RuntimePowerAudioFeedback.cpp
    src/audio/world/RuntimeWorldAudioSystem.cpp
    src/core/io/RuntimeAtomicFile.cpp
    src/core/results/RuntimeMatchScore.cpp
    src/core/security/RuntimeSecureBytes.cpp
    src/data/art/ArtDatabase.cpp
    src/data/campaign/CampaignProgress.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/data/maps/RuntimeMapPackage.cpp
    src/data/rules/TechTreeAnalyzer.cpp
    src/network/cooperative/RuntimeCooperativeSessionSystem.cpp
    src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerPlayerIntent.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/maps/RuntimeMultiplayerMapTransfer.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeLanTransport.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/snapshots/RuntimeMultiplayerResumeSave.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotSession.cpp
    src/render/lighting/RuntimeLightingSystem.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/runtime/campaign/RuntimeCampaignMovieResolver.cpp
    src/runtime/menu/RuntimeSkirmishController.cpp
    src/runtime/persistence/RuntimeLoadTransaction.cpp
    src/runtime/persistence/RuntimeSaveDocument.cpp
    src/runtime/persistence/RuntimeSaveIdentity.cpp
    src/runtime/replay/RuntimeMatchReplay.cpp
    src/simulation/abilities/psionic/RuntimeMagBeamSystem.cpp
    src/simulation/abilities/psionic/RuntimePsychicStatusSystem.cpp
    src/simulation/abilities/psionic/RuntimeSpySystem.cpp
    src/simulation/abilities/unit_special/RuntimeSpecialMechanisms.cpp
    src/simulation/abilities/unit_special/RuntimeTimedBomb.cpp
    src/simulation/ai/RuntimeAircraftProductionMatcher.cpp
    src/simulation/ai/RuntimeAircraftServiceSystem.cpp
    src/simulation/ai/RuntimeAirstrikeSystem.cpp
    src/simulation/ai/RuntimeComputerAiTriggerHash.cpp
    src/simulation/ai/RuntimeComputerMiningCodec.cpp
    src/simulation/ai/RuntimeComputerTacticsCodec.cpp
    src/simulation/buildings/RuntimeGarrisonSystem.cpp
    src/simulation/buildings/RuntimeGateSystem.cpp
    src/simulation/buildings/RuntimeGrindingSystem.cpp
    src/simulation/buildings/RuntimeInfantryServiceSystem.cpp
    src/simulation/buildings/RuntimeRepairDepotSystem.cpp
    src/simulation/buildings/RuntimeSpecialBuildingSystem.cpp
    src/simulation/combat/damage/RuntimeDeathWeaponSystem.cpp
    src/simulation/combat/targeting/RuntimeOpenTransportMuzzle.cpp
    src/simulation/economy/HouseEconomyContext.cpp
    src/simulation/economy/RuntimeEconomyFixedStep.cpp
    src/simulation/effects/RuntimeParticleSystem.cpp
    src/simulation/movement/ground/RuntimeCommittedGroundMotion.cpp
    src/simulation/navigation/occupancy/RuntimeSubcellSystem.cpp
    src/simulation/navigation/pathfinding/PathGoalCandidateTask.cpp
    src/simulation/navigation/pathfinding/PathGoalCandidates.cpp
    src/simulation/navigation/pathfinding/PathRequestQueue.cpp
    src/simulation/navigation/terrain/RuntimeNavigationTerrainQuery.cpp
    src/simulation/production/RuntimeFactoryDeliveryState.cpp
    src/simulation/production/RuntimeProductionFixedStep.cpp
    src/simulation/production/RuntimeProductionLanes.cpp
    src/simulation/production/RuntimeProductionTiming.cpp
    src/simulation/superweapons/RuntimeChronosphereRelocation.cpp
    src/simulation/superweapons/SuperWeaponSystem.cpp
    src/simulation/triggers/RuntimeTriggerPersistence.cpp
    src/simulation/triggers/RuntimeTriggerScheduler.cpp
    src/simulation/vision/RuntimeVision.cpp
    src/simulation/vision/RuntimeVisionPhase.cpp
    src/ui/input/RuntimeReferencePresentationController.cpp
    src/audio/backend/RuntimeUnsupportedLegacyWaveDevice.cpp
    src/audio/backend/RuntimeUnsupportedAsyncSoundDevice.cpp)
target_link_libraries(concord_production_session_support_runtime_leaf PRIVATE Threads::Threads)

add_executable(concord_production_session_recovery_smoke
    tests/infrastructure/RuntimeProductionSessionRecoverySmoke.cpp
    src/runtime/session/headless/RuntimeHeadlessMapAuthority.cpp
    src/runtime/session/headless/RuntimeHeadlessResourceQueries.cpp
    src/runtime/session/headless/RuntimePortableSessionDevices.cpp)
target_include_directories(concord_production_session_recovery_smoke PRIVATE src)
target_compile_features(concord_production_session_recovery_smoke PRIVATE cxx_std_20)
target_compile_options(concord_production_session_recovery_smoke PRIVATE
    "$<TARGET_GENEX_EVAL:${_session_profile_owner},$<TARGET_PROPERTY:${_session_profile_owner},COMPILE_OPTIONS>>")
target_compile_definitions(concord_production_session_recovery_smoke PRIVATE
    "$<TARGET_GENEX_EVAL:${_session_profile_owner},$<TARGET_PROPERTY:${_session_profile_owner},COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_production_session_recovery_smoke PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
endif()
target_link_libraries(concord_production_session_recovery_smoke PRIVATE
    concord_production_session_runtime_leaf
    concord_production_session_support_runtime_leaf
    concord_world_core_runtime_leaf
    concord_lockstep_runtime_leaf
    concord_session_runtime_leaf
    concord_gameplay_command_runtime_leaf
    concord_match_setup_runtime_leaf
    concord_snapshot_transfer_runtime_leaf
    concord_compatibility_fingerprint_runtime_leaf
    concord_ini_runtime_leaf
    Threads::Threads)
if(WIN32)
    target_link_libraries(concord_production_session_recovery_smoke PRIVATE ws2_32 bcrypt)
endif()
unset(_session_profile_owner)
