set(
    CONCORD_RUNTIME_ANIM_PALETTE
    "${CMAKE_SOURCE_DIR}/resources/palettes/anim.pal"
)
if (EXISTS "${CONCORD_RUNTIME_ANIM_PALETTE}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources/palettes")
    configure_file(
        "${CONCORD_RUNTIME_ANIM_PALETTE}"
        "${CMAKE_BINARY_DIR}/resources/palettes/anim.pal"
        COPYONLY
    )
endif()

add_executable(concord_rules_dump
    src/assets/codecs/PalDecoder.cpp
    src/analysis/cli/main.cpp
    src/data/art/ArtDatabase.cpp
    src/assets/inspection/AssetInspector.cpp
    src/assets/audio/AudioCatalog.cpp
    src/assets/audio/AudioDecoder.cpp
    src/assets/audio/AudioSampleResolver.cpp
    src/analysis/sandbox/AiTriggerSandbox.cpp
    src/data/campaign/CampaignDatabase.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/runtime/campaign/RuntimeCampaignMovieResolver.cpp
    src/analysis/sandbox/CombatSandbox.cpp
    src/analysis/cli/CommandLineOptions.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/analysis/sandbox/EconomySandbox.cpp
    src/analysis/reports/EngineStatus.cpp
    src/world/model/GameWorld.cpp
    src/analysis/reports/JsonExporter.cpp
    src/data/maps/MapDatabase.cpp
    src/assets/preview/MapPreviewExporter.cpp
    src/assets/preview/MapPreviewResourceContext.cpp
    src/assets/archive/MixArchive.cpp
    src/analysis/reports/ModCompatibilityScanner.cpp
    src/analysis/reports/ObjectReferenceValidator.cpp
    src/simulation/navigation/pathfinding/PathRequestQueue.cpp
    src/analysis/reports/PerformancePlan.cpp
    src/analysis/sandbox/ReplaySandbox.cpp
    src/data/rules/RulesDatabase.cpp
    src/data/rules/TechTreeAnalyzer.cpp
    src/data/art/TheaterDatabase.cpp
    src/analysis/sandbox/VictoryCondition.cpp
    src/assets/archive/WestwoodCrypto.cpp
)

add_executable(concord_runtime
    src/assets/codecs/PalDecoder.cpp
    src/runtime/host/RuntimeMain.cpp
    src/data/campaign/CampaignDatabase.cpp
    src/data/campaign/CampaignPresentationDecoder.cpp
    src/data/campaign/CampaignProgress.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/network/cooperative/RuntimeCooperativeSessionSystem.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/session/RuntimeLanTransport.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/protocol/RuntimeMultiplayerLaunchConfig.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/lobby/RuntimeMultiplayerLobbyFlow.cpp
    src/network/snapshots/RuntimeMultiplayerResumeSave.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/lockstep/RuntimeMultiplayerPlayerIntent.cpp
    src/ui/network/RuntimeMultiplayerMenu.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
    src/network/internet/RuntimeInternetCommunity.cpp
    src/network/internet/RuntimeInternetCoordinator.cpp
    src/network/internet/RuntimeInternetLobbyFlow.cpp
    src/network/internet/RuntimeInternetMatchmaking.cpp
    src/network/transport/RuntimeInternetMatchmakingProtocol.cpp
    src/network/transport/RuntimeInternetTcpTunnelProxy.cpp
    src/network/transport/RuntimeInternetTlsTransport.cpp
    src/network/transport/RuntimeInternetServiceTransport.cpp
    src/ui/network/RuntimeInternetMultiplayerMenu.cpp
    src/ui/network/RuntimeInternetMenuSkin.cpp
    src/core/io/RuntimeAtomicFile.cpp
    src/ui/settings/RuntimeSettings.cpp
    src/ui/main_menu/RuntimeMainMenu.cpp
    src/ui/campaign/RuntimeCampaignMenu.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/runtime/campaign/RuntimeCampaignMovieResolver.cpp
    src/runtime/host/RuntimeShell.cpp
    src/simulation/navigation/terrain/RuntimeNavigationTerrainQuery.cpp
    src/runtime/host/RuntimeUiClockWin32.cpp
    src/ui/input/RuntimeSelectionKeyController.cpp
    src/ui/input/RuntimeTeamKeyController.cpp
    src/ui/input/RuntimeLocalNavigationController.cpp
    src/ui/input/RuntimeReferencePresentationController.cpp
    src/ui/input/RuntimeReferenceCommunicationController.cpp
    src/ui/input/RuntimeChatTextController.cpp
    src/ui/input/RuntimeMapRightPointerController.cpp
    src/ui/input/RuntimeMapLeftPointerController.cpp
    src/ui/input/RuntimePointerIngressController.cpp
    src/ui/sidebar/RuntimeSidebarPageController.cpp
    src/ui/input/RuntimePlanningFactoryKeyController.cpp
    src/ui/input/RuntimeCameraKeyController.cpp
    src/ui/input/RuntimeMenuKeyController.cpp
    src/ui/input/RuntimeSessionKeyController.cpp
    src/runtime/config/RuntimeConfig.cpp
    src/core/results/RuntimeMatchScore.cpp
    src/audio/backend/RuntimeVoiceAudio.cpp
    src/ui/skirmish/RuntimeSkirmishMenu.cpp
    src/data/maps/RuntimeRandomMap.cpp
    src/data/maps/RuntimeMapPackage.cpp
    src/data/art/ArtDatabase.cpp
    src/assets/audio/AudioDecoder.cpp
    src/assets/audio/AudioSampleResolver.cpp
    src/assets/inspection/GameResourceMount.cpp
    src/world/model/GameWorld.cpp
    src/render/core/RuntimeMapRenderer.cpp
    src/assets/codecs/RuntimeTmpCodec.cpp
    src/assets/terrain/RuntimeTerrainTmpQuery.cpp
    src/render/lighting/RuntimeLightingSystem.cpp
    src/simulation/buildings/RuntimeGarrisonSystem.cpp
    src/simulation/buildings/RuntimeGateSystem.cpp
    src/simulation/ai/RuntimeAircraftServiceSystem.cpp
    src/simulation/ai/RuntimeAircraftProductionMatcher.cpp
    src/simulation/buildings/RuntimeRepairDepotSystem.cpp
    src/simulation/production/RuntimeFactoryDeliveryState.cpp
    src/simulation/buildings/RuntimeGrindingSystem.cpp
    src/simulation/navigation/occupancy/RuntimeSubcellSystem.cpp
    src/audio/world/RuntimeWorldAudioSystem.cpp
    src/simulation/effects/RuntimeParticleSystem.cpp
    src/simulation/production/RuntimeProductionLanes.cpp
    src/simulation/production/RuntimeProductionTiming.cpp
    src/audio/events/RuntimePowerAudioFeedback.cpp
    src/simulation/combat/damage/RuntimeDeathWeaponSystem.cpp
    src/simulation/ai/RuntimeAirstrikeSystem.cpp
    src/simulation/abilities/psionic/RuntimeMagBeamSystem.cpp
    src/simulation/abilities/psionic/RuntimePsychicStatusSystem.cpp
    src/simulation/abilities/psionic/RuntimeSpySystem.cpp
    src/simulation/abilities/unit_special/RuntimeSpecialMechanisms.cpp
    src/simulation/buildings/RuntimeSpecialBuildingSystem.cpp
    src/simulation/buildings/RuntimeInfantryServiceSystem.cpp
    src/simulation/superweapons/SuperWeaponSystem.cpp
    src/simulation/vision/RuntimeVision.cpp
    src/data/maps/MapDatabase.cpp
    src/assets/archive/MixArchive.cpp
    src/simulation/navigation/pathfinding/PathRequestQueue.cpp
    src/data/rules/RulesDatabase.cpp
    src/data/rules/TechTreeAnalyzer.cpp
    src/data/art/TheaterDatabase.cpp
    src/assets/archive/WestwoodCrypto.cpp
)

# Source headers use module paths relative to src after the directory split.
target_include_directories(concord_runtime PRIVATE src)
target_include_directories(concord_rules_dump PRIVATE src)


# S09: the runtime and preview tool share one compatibility-preserving SHP decoder.
target_sources(concord_runtime PRIVATE src/assets/codecs/ShpCodec.cpp)
target_sources(concord_rules_dump PRIVATE src/assets/codecs/ShpCodec.cpp)
# W009-10: same standard keyboard router and force-held policy in production.
target_sources(concord_runtime PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui/input/RuntimeKeyboardRouterController.cpp"
)

# W009-11: same standard campaign admission and outer keyboard policy in production.
target_sources(concord_runtime PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui/input/RuntimeKeyboardIngressController.cpp"
)

# W010-04: same complete standard cursor policy in production.
target_sources(concord_runtime PRIVATE "${CMAKE_SOURCE_DIR}/src/ui/input/RuntimeCursorController.cpp")

# W012-01: same capture-interruption owner in the runtime.
target_sources(concord_runtime PRIVATE "${CMAKE_SOURCE_DIR}/src/ui/input/RuntimeInputCancellationController.cpp")
