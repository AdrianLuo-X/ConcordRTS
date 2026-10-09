cmake_minimum_required(VERSION 3.20)

project(ConcordRTS LANGUAGES CXX)
add_executable(concord_missile_acceleration_smoke tests/RuntimeMissileAccelerationSmoke.cpp)
target_include_directories(concord_missile_acceleration_smoke PRIVATE src)
target_compile_features(concord_missile_acceleration_smoke PRIVATE cxx_std_20)
add_executable(concord_vehicle_startup_motion_smoke tests/RuntimeVehicleStartupMotionSmoke.cpp)
target_include_directories(concord_vehicle_startup_motion_smoke PRIVATE src)
target_compile_features(concord_vehicle_startup_motion_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_missile_acceleration_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_vehicle_startup_motion_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_aircraft_flight_codec_smoke tests/RuntimeAircraftFlightCodecSmoke.cpp)
target_include_directories(concord_aircraft_flight_codec_smoke PRIVATE src)
target_compile_features(concord_aircraft_flight_codec_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_aircraft_flight_codec_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()
add_executable(concord_subcell_requested_index_smoke tests/RuntimeSubcellRequestedIndexSmoke.cpp
    src/RuntimeSubcellSystem.cpp)
target_include_directories(concord_subcell_requested_index_smoke PRIVATE src)
target_compile_features(concord_subcell_requested_index_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_subcell_requested_index_smoke PRIVATE /utf-8 /W4 /O2 /Ob2 /FS)
endif()
add_executable(concord_mod_history_noop_smoke tests/RuntimeModExtensionsHistoryNoopSmoke.cpp
    src/GameWorld.cpp src/IniFile.cpp src/MapDatabase.cpp src/RulesDatabase.cpp src/TheaterDatabase.cpp)
target_include_directories(concord_mod_history_noop_smoke PRIVATE src)
target_compile_features(concord_mod_history_noop_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_mod_history_noop_smoke PRIVATE /utf-8 /W4 /O2 /Ob2 /FS)
endif()

add_executable(concord_history_owner_census_smoke tests/RuntimeHistoryOwnerCensusSmoke.cpp)
target_include_directories(concord_history_owner_census_smoke PRIVATE src)
target_compile_features(concord_history_owner_census_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_history_owner_census_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_presentation_work_budget_smoke tests/RuntimePresentationWorkBudgetSmoke.cpp)
target_include_directories(concord_presentation_work_budget_smoke PRIVATE src)
target_compile_features(concord_presentation_work_budget_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_presentation_work_budget_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_presentation_budget_remainder_smoke tests/RuntimePresentationBudgetRemainderSmoke.cpp)
target_include_directories(concord_presentation_budget_remainder_smoke PRIVATE src)
target_compile_features(concord_presentation_budget_remainder_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_presentation_budget_remainder_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_shadow_opaque_span_smoke tests/RuntimeShadowOpaqueSpanSmoke.cpp)
target_include_directories(concord_shadow_opaque_span_smoke PRIVATE src)
target_compile_features(concord_shadow_opaque_span_smoke PRIVATE cxx_std_20)
add_executable(concord_water_wake_history_smoke tests/RuntimeWaterWakeHistoryMaintenanceSmoke.cpp
    src/GameWorld.cpp src/IniFile.cpp src/MapDatabase.cpp src/RulesDatabase.cpp src/TheaterDatabase.cpp)
target_include_directories(concord_water_wake_history_smoke PRIVATE src)
target_compile_features(concord_water_wake_history_smoke PRIVATE cxx_std_20)
add_executable(concord_simulation_query_reuse_smoke tests/RuntimeSimulationQueryReuseSmoke.cpp
    src/GameWorld.cpp src/IniFile.cpp src/MapDatabase.cpp src/RulesDatabase.cpp src/TheaterDatabase.cpp)
target_include_directories(concord_simulation_query_reuse_smoke PRIVATE src)
target_compile_features(concord_simulation_query_reuse_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_shadow_opaque_span_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_water_wake_history_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_simulation_query_reuse_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_frame_diagnostics_smoke tests/RuntimeFrameDiagnosticsSmoke.cpp)
target_include_directories(concord_frame_diagnostics_smoke PRIVATE src)
target_compile_features(concord_frame_diagnostics_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_frame_diagnostics_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_game_speed_timing_smoke tests/RuntimeGameSpeedTimingSmoke.cpp)
target_include_directories(concord_game_speed_timing_smoke PRIVATE src)
target_compile_features(concord_game_speed_timing_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_game_speed_timing_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_building_depth_smoke tests/RuntimeBuildingDepthSmoke.cpp)
target_include_directories(concord_building_depth_smoke PRIVATE src)
target_compile_features(concord_building_depth_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_building_depth_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_naval_scene_depth_smoke tests/RuntimeNavalSceneDepthSmoke.cpp)
target_include_directories(concord_naval_scene_depth_smoke PRIVATE src)
target_compile_features(concord_naval_scene_depth_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_naval_scene_depth_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_lighting_lookup_raster_smoke
    tests/RuntimeLightingLookupRasterSmoke.cpp src/RuntimeLightingSystem.cpp src/IniFile.cpp)
target_include_directories(concord_lighting_lookup_raster_smoke PRIVATE src)
target_compile_features(concord_lighting_lookup_raster_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(concord_lighting_lookup_raster_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(concord_lighting_lookup_raster_smoke PRIVATE /utf-8 /FS /O2 /Ob2)
endif()

if(MSVC)
    add_executable(concord_body_tint_context_smoke tests/RuntimeBodyTintContextSmoke.cpp)
    target_include_directories(concord_body_tint_context_smoke PRIVATE src)
    target_compile_features(concord_body_tint_context_smoke PRIVATE cxx_std_20)
    target_compile_options(concord_body_tint_context_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_movement_classification_smoke tests/RuntimeMovementClassificationSmoke.cpp)
target_include_directories(concord_movement_classification_smoke PRIVATE src)
target_compile_features(concord_movement_classification_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(concord_movement_classification_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(concord_movement_classification_smoke PRIVATE /utf-8 /W4 /WX /O2 /Ob2)
endif()

add_executable(concord_voxel_debris_smoke tests/RuntimeVoxelDebrisSmoke.cpp)
target_include_directories(concord_voxel_debris_smoke PRIVATE src)
target_compile_features(concord_voxel_debris_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_voxel_debris_smoke PRIVATE /utf-8 /W4 /WX /O2 /Ob2)
endif()

add_executable(concord_internet_ranked_match_smoke
    tests/RuntimeInternetRankedMatchSmoke.cpp src/RuntimeInternetRankedMatch.cpp)
target_include_directories(concord_internet_ranked_match_smoke PRIVATE src)
target_compile_features(concord_internet_ranked_match_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_internet_ranked_match_smoke PRIVATE /utf-8 /W4 /WX /O2)
endif()

add_executable(concord_random_map_smoke tests/RuntimeRandomMapSmoke.cpp
    src/RuntimeRandomMap.cpp src/RuntimeAtomicFile.cpp src/RuntimeCampaignDataSource.cpp
    src/IniFile.cpp src/MapDatabase.cpp src/TheaterDatabase.cpp src/RulesDatabase.cpp
    src/RuntimeFactoryDeliveryState.cpp src/MixArchive.cpp src/WestwoodCrypto.cpp)
target_include_directories(concord_random_map_smoke PRIVATE src)
target_compile_features(concord_random_map_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_random_map_smoke PRIVATE /utf-8 /FS /O2)
endif()

add_executable(concord_vehicle_arrival_motion_smoke tests/RuntimeVehicleArrivalMotionSmoke.cpp)
target_include_directories(concord_vehicle_arrival_motion_smoke PRIVATE src)
target_compile_features(concord_vehicle_arrival_motion_smoke PRIVATE cxx_std_20)

add_executable(concord_building_economy_smoke
    tests/RuntimeBuildingEconomySmoke.cpp src/RulesDatabase.cpp
    src/IniFile.cpp src/RuntimeFactoryDeliveryState.cpp)
target_include_directories(concord_building_economy_smoke PRIVATE src)
target_compile_features(concord_building_economy_smoke PRIVATE cxx_std_20)

add_executable(concord_superweapon_smoke tests/SuperWeaponSmoke.cpp
    src/SuperWeaponSystem.cpp src/RulesDatabase.cpp src/IniFile.cpp
    src/RuntimeFactoryDeliveryState.cpp)
target_include_directories(concord_superweapon_smoke PRIVATE src)
target_compile_features(concord_superweapon_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_superweapon_smoke PRIVATE /utf-8 /FS)
endif()

if (MSVC)
    # /RTC1 cannot be combined with the optimized playable Debug runtime.
    # Add it back only to the non-optimized command-line tool below.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(concord_multiplayer_resume_save_smoke
    tests/RuntimeMultiplayerResumeSaveSmoke.cpp src/RuntimeMultiplayerResumeSave.cpp
    src/RuntimeMultiplayerMatchSetup.cpp src/RuntimeMultiplayerSnapshotTransfer.cpp src/IniFile.cpp)
target_include_directories(concord_multiplayer_resume_save_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_multiplayer_resume_save_smoke PRIVATE /utf-8 /FS /O2)
endif()

add_executable(concord_combat_details_smoke tests/RuntimeCombatDetailsSmoke.cpp src/IniFile.cpp)
target_include_directories(concord_combat_details_smoke PRIVATE src)
add_executable(concord_settings_hotkey_smoke tests/RuntimeSettingsHotkeySmoke.cpp
    src/RuntimeSettings.cpp src/RuntimeAtomicFile.cpp)
target_include_directories(concord_settings_hotkey_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_settings_hotkey_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_combat_details_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_building_economy_smoke PRIVATE /utf-8 /FS)
endif()

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
    src/main.cpp
    src/ArtDatabase.cpp
    src/AssetInspector.cpp
    src/AudioCatalog.cpp
    src/AudioDecoder.cpp
    src/AudioSampleResolver.cpp
    src/AiTriggerSandbox.cpp
    src/CampaignDatabase.cpp
    src/RuntimeCampaignDataSource.cpp
    src/RuntimeCampaignMovieResolver.cpp
    src/CombatSandbox.cpp
    src/CommandLineOptions.cpp
    src/CooperativeScenarioDatabase.cpp
    src/EconomySandbox.cpp
    src/EngineStatus.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/JsonExporter.cpp
    src/MapDatabase.cpp
    src/MapPreviewExporter.cpp
    src/MixArchive.cpp
    src/ModCompatibilityScanner.cpp
    src/ObjectReferenceValidator.cpp
    src/PathRequestQueue.cpp
    src/PerformancePlan.cpp
    src/ReplaySandbox.cpp
    src/RulesDatabase.cpp
    src/TechTreeAnalyzer.cpp
    src/TheaterDatabase.cpp
    src/VictoryCondition.cpp
    src/WestwoodCrypto.cpp
)

add_executable(concord_runtime
    src/RuntimeMain.cpp
    src/CampaignDatabase.cpp
    src/CampaignPresentationDecoder.cpp
    src/CampaignProgress.cpp
    src/CooperativeScenarioDatabase.cpp
    src/RuntimeCooperativeSessionSystem.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeLanTransport.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/RuntimeMultiplayerLaunchConfig.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerLobbyFlow.cpp
    src/RuntimeMultiplayerResumeSave.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerPlayerIntent.cpp
    src/RuntimeMultiplayerMenu.cpp
    src/RuntimeInternetConnectivity.cpp
    src/RuntimeInternetCommunity.cpp
    src/RuntimeInternetCoordinator.cpp
    src/RuntimeInternetLobbyFlow.cpp
    src/RuntimeInternetMatchmaking.cpp
    src/RuntimeInternetMatchmakingProtocol.cpp
    src/RuntimeInternetTcpTunnelProxy.cpp
    src/RuntimeInternetTlsTransport.cpp
    src/RuntimeInternetServiceTransport.cpp
    src/RuntimeInternetMultiplayerMenu.cpp
    src/RuntimeInternetMenuSkin.cpp
    src/RuntimeAtomicFile.cpp
    src/RuntimeSettings.cpp
    src/RuntimeMainMenu.cpp
    src/RuntimeCampaignMenu.cpp
    src/RuntimeCampaignDataSource.cpp
    src/RuntimeCampaignMovieResolver.cpp
    src/RuntimeShell.cpp
    src/RuntimeVoiceAudio.cpp
    src/RuntimeSkirmishMenu.cpp
    src/RuntimeRandomMap.cpp
    src/RuntimeMapPackage.cpp
    src/ArtDatabase.cpp
    src/AudioDecoder.cpp
    src/AudioSampleResolver.cpp
    src/GameResourceMount.cpp
    src/GameWorld.cpp
    src/RuntimeMapRenderer.cpp
    src/RuntimeLightingSystem.cpp
    src/RuntimeGarrisonSystem.cpp
    src/RuntimeGateSystem.cpp
    src/RuntimeAircraftServiceSystem.cpp
    src/RuntimeAircraftProductionMatcher.cpp
    src/RuntimeRepairDepotSystem.cpp
    src/RuntimeFactoryDeliveryState.cpp
    src/RuntimeGrindingSystem.cpp
    src/RuntimeSubcellSystem.cpp
    src/RuntimeWorldAudioSystem.cpp
    src/RuntimeParticleSystem.cpp
    src/RuntimeProductionLanes.cpp
    src/RuntimeProductionTiming.cpp
    src/RuntimePowerAudioFeedback.cpp
    src/RuntimeDeathWeaponSystem.cpp
    src/RuntimeAirstrikeSystem.cpp
    src/RuntimeMagBeamSystem.cpp
    src/RuntimePsychicStatusSystem.cpp
    src/RuntimeSpySystem.cpp
    src/RuntimeSpecialMechanisms.cpp
    src/RuntimeSpecialBuildingSystem.cpp
    src/RuntimeInfantryServiceSystem.cpp
    src/SuperWeaponSystem.cpp
    src/RuntimeVision.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/MixArchive.cpp
    src/PathRequestQueue.cpp
    src/RulesDatabase.cpp
    src/TechTreeAnalyzer.cpp
    src/TheaterDatabase.cpp
    src/WestwoodCrypto.cpp
)

add_executable(concord_map_data_smoke
    tests/MapDataSmoke.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(concord_map_data_smoke PRIVATE src)

add_executable(concord_game_world_deterministic_hash_smoke
    tests/GameWorldDeterministicHashSmoke.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(
    concord_game_world_deterministic_hash_smoke PRIVATE src
)

add_executable(concord_runtime_object_index_smoke tests/RuntimeObjectIndexSmoke.cpp)
target_include_directories(concord_runtime_object_index_smoke PRIVATE src)
add_executable(concord_path_request_queue_performance_smoke
    tests/PathRequestQueuePerformanceSmoke.cpp
    src/PathRequestQueue.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(concord_path_request_queue_performance_smoke PRIVATE src)
if (MSVC)
    target_compile_options(concord_runtime_object_index_smoke PRIVATE /utf-8 /W4 /WX /permissive-)
    # Uses the same legacy map/rules sources as concord_map_data_smoke.
    target_compile_options(concord_path_request_queue_performance_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_runtime_map_package_smoke
    tests/RuntimeMapPackageSmoke.cpp
    src/RuntimeMapPackage.cpp
    src/IniFile.cpp
    src/MixArchive.cpp
    src/RuntimeAtomicFile.cpp
    src/WestwoodCrypto.cpp
)
target_include_directories(concord_runtime_map_package_smoke PRIVATE src)

add_executable(concord_campaign_progress_smoke
    tests/CampaignProgressSmoke.cpp
    src/CampaignProgress.cpp
    src/IniFile.cpp
    src/RuntimeAtomicFile.cpp
)
target_include_directories(concord_campaign_progress_smoke PRIVATE src)

add_executable(concord_campaign_map_selection_smoke
    tests/CampaignMapSelectionSmoke.cpp
    src/CampaignDatabase.cpp
    src/RuntimeCampaignDataSource.cpp
    src/RuntimeCampaignMovieResolver.cpp
    src/AudioDecoder.cpp
    src/AudioSampleResolver.cpp
    src/IniFile.cpp
    src/MixArchive.cpp
    src/WestwoodCrypto.cpp
)
target_include_directories(concord_campaign_map_selection_smoke PRIVATE src)

add_executable(concord_runtime_campaign_data_source_smoke
    tests/RuntimeCampaignDataSourceSmoke.cpp
    src/RuntimeCampaignDataSource.cpp
    src/IniFile.cpp
    src/MixArchive.cpp
    src/WestwoodCrypto.cpp
)
target_include_directories(
    concord_runtime_campaign_data_source_smoke PRIVATE src
)

add_executable(concord_runtime_campaign_movie_resolver_smoke
    tests/RuntimeCampaignMovieResolverSmoke.cpp
    src/RuntimeCampaignDataSource.cpp
    src/RuntimeCampaignMovieResolver.cpp
    src/IniFile.cpp
    src/MixArchive.cpp
    src/WestwoodCrypto.cpp
)
target_include_directories(
    concord_runtime_campaign_movie_resolver_smoke PRIVATE src
)

add_executable(concord_campaign_presentation_decoder_smoke
    tests/CampaignPresentationDecoderSmoke.cpp
    src/CampaignPresentationDecoder.cpp
)
target_include_directories(
    concord_campaign_presentation_decoder_smoke PRIVATE src
)

add_executable(concord_rules_special_features_smoke
    tests/RulesSpecialFeaturesSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_rules_special_features_smoke PRIVATE src)
add_executable(concord_rules_poison_policy_smoke
    src/RulesPoisonPolicySmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_rules_poison_policy_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_rules_poison_policy_smoke PRIVATE /utf-8 /FS)
endif()
add_executable(concord_mod_extensions_smoke
    tests/RuntimeModExtensionsSmoke.cpp src/IniFile.cpp src/RulesDatabase.cpp src/ModCompatibilityScanner.cpp)
target_include_directories(concord_mod_extensions_smoke PRIVATE src)
add_executable(concord_screen_shake_smoke
    tests/RuntimeScreenShakeSmoke.cpp src/IniFile.cpp src/RulesDatabase.cpp)
target_include_directories(concord_screen_shake_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_mod_extensions_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_screen_shake_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_rules_pad_aircraft_smoke
    tests/RulesPadAircraftSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_rules_pad_aircraft_smoke PRIVATE src)

add_executable(concord_rules_production_smoke
    tests/RulesProductionRulesSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_rules_production_smoke PRIVATE src)

add_executable(concord_rules_lookup_dispatch_smoke
    tests/RulesLookupDispatchSmoke.cpp src/IniFile.cpp src/RulesDatabase.cpp
)
target_include_directories(concord_rules_lookup_dispatch_smoke PRIVATE src)

add_executable(concord_rules_overlay_identity_smoke
    tests/RulesOverlayIdentitySmoke.cpp src/IniFile.cpp src/RulesDatabase.cpp
)
target_include_directories(concord_rules_overlay_identity_smoke PRIVATE src)

add_executable(concord_rules_crate_smoke
    tests/RulesCrateSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_rules_crate_smoke PRIVATE src)

add_executable(concord_art_firing_sequence_smoke
    tests/ArtFiringSequenceSmoke.cpp
    src/ArtDatabase.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
)
target_include_directories(concord_art_firing_sequence_smoke PRIVATE src)
add_test(NAME art_firing_sequence_smoke_test
    COMMAND concord_art_firing_sequence_smoke "${CMAKE_SOURCE_DIR}/local_data/artmd.ini")
add_executable(concord_particle_system_smoke
    tests/RuntimeParticleSystemSmoke.cpp
    src/ArtDatabase.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeParticleSystem.cpp
)
target_include_directories(concord_particle_system_smoke PRIVATE src)

add_executable(concord_garrison_system_smoke
    tests/RuntimeGarrisonSystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeGarrisonSystem.cpp
)
target_include_directories(concord_garrison_system_smoke PRIVATE src)

add_executable(concord_subcell_system_smoke
    tests/RuntimeSubcellSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimeSubcellSystem.cpp
)
target_include_directories(concord_subcell_system_smoke PRIVATE src)

add_executable(concord_subcell_selective_batch_smoke
    tests/RuntimeSubcellSelectiveBatchSmoke.cpp
    src/RuntimeSubcellSystem.cpp
)
target_include_directories(concord_subcell_selective_batch_smoke PRIVATE src)

add_executable(concord_repair_depot_system_smoke
    tests/RuntimeRepairDepotSystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeRepairDepotSystem.cpp
)
target_include_directories(concord_repair_depot_system_smoke PRIVATE src)

add_executable(concord_factory_delivery_state_smoke
    tests/RuntimeFactoryDeliveryStateSmoke.cpp
    src/IniFile.cpp
    src/RuntimeFactoryDeliveryState.cpp
)
target_include_directories(concord_factory_delivery_state_smoke PRIVATE src)

add_executable(concord_grinding_system_smoke
    tests/RuntimeGrindingSystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeGrindingSystem.cpp
)
target_include_directories(concord_grinding_system_smoke PRIVATE src)

add_executable(concord_aircraft_service_system_smoke
    tests/RuntimeAircraftServiceSystemSmoke.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/RuntimeAircraftServiceSystem.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(concord_aircraft_service_system_smoke PRIVATE src)

add_executable(concord_aircraft_production_matcher_smoke
    tests/RuntimeAircraftProductionMatcherSmoke.cpp
    src/RuntimeAircraftProductionMatcher.cpp
)
target_include_directories(
    concord_aircraft_production_matcher_smoke PRIVATE src
)

add_executable(concord_gate_system_smoke
    tests/RuntimeGateSystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeGateSystem.cpp
)
target_include_directories(concord_gate_system_smoke PRIVATE src)

add_executable(concord_world_audio_system_smoke
    tests/RuntimeWorldAudioSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimeWorldAudioSystem.cpp
)
target_include_directories(concord_world_audio_system_smoke PRIVATE src)

add_executable(concord_audio_sample_resolver_edition_smoke
    tests/AudioSampleResolverEditionSmoke.cpp
    src/AudioDecoder.cpp
    src/AudioSampleResolver.cpp
    src/IniFile.cpp
    src/MixArchive.cpp
    src/WestwoodCrypto.cpp
)
target_include_directories(
    concord_audio_sample_resolver_edition_smoke PRIVATE src
)

add_executable(concord_audio_decoder_smoke
    tests/AudioDecoderSmoke.cpp
    src/AudioDecoder.cpp src/AudioSampleResolver.cpp src/IniFile.cpp
    src/MixArchive.cpp src/WestwoodCrypto.cpp
)
target_include_directories(concord_audio_decoder_smoke PRIVATE src)
if (MSVC)
    target_compile_options(concord_audio_decoder_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_projectile_line_trail_smoke tests/ProjectileLineTrailSmoke.cpp)
target_include_directories(concord_projectile_line_trail_smoke PRIVATE src)

add_executable(concord_power_audio_feedback_smoke
    tests/RuntimePowerAudioFeedbackSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimePowerAudioFeedback.cpp
)
target_include_directories(concord_power_audio_feedback_smoke PRIVATE src)

add_executable(concord_lighting_system_smoke
    tests/RuntimeLightingSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimeLightingSystem.cpp
)
target_include_directories(concord_lighting_system_smoke PRIVATE src)

add_executable(concord_production_lanes_smoke
    tests/RuntimeProductionLanesSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeProductionLanes.cpp
)
target_include_directories(concord_production_lanes_smoke PRIVATE src)

add_executable(concord_production_timing_smoke
    tests/RuntimeProductionTimingSmoke.cpp
    src/RuntimeProductionTiming.cpp
)
target_include_directories(concord_production_timing_smoke PRIVATE src)

add_executable(concord_runtime_atomic_file_smoke
    tests/RuntimeAtomicFileSmoke.cpp
    src/RuntimeAtomicFile.cpp
)
target_include_directories(concord_runtime_atomic_file_smoke PRIVATE src)

add_executable(concord_spy_system_smoke
    tests/RuntimeSpySystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeSpySystem.cpp
)
target_include_directories(concord_spy_system_smoke PRIVATE src)

add_executable(concord_airstrike_system_smoke
    tests/RuntimeAirstrikeSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimeAirstrikeSystem.cpp
)
target_include_directories(concord_airstrike_system_smoke PRIVATE src)

add_executable(concord_death_weapon_system_smoke
    tests/RuntimeDeathWeaponSystemSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeDeathWeaponSystem.cpp
)
target_include_directories(concord_death_weapon_system_smoke PRIVATE src)

add_executable(concord_mag_beam_system_smoke
    tests/RuntimeMagBeamSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimeMagBeamSystem.cpp
)
target_include_directories(concord_mag_beam_system_smoke PRIVATE src)

add_executable(concord_psychic_status_system_smoke
    tests/RuntimePsychicStatusSystemSmoke.cpp
    src/IniFile.cpp
    src/RuntimePsychicStatusSystem.cpp
)
target_include_directories(concord_psychic_status_system_smoke PRIVATE src)

add_executable(concord_special_mechanisms_smoke
    tests/RuntimeSpecialMechanismsSmoke.cpp
    src/IniFile.cpp
    src/RulesDatabase.cpp
    src/RuntimeSpecialMechanisms.cpp
)
target_include_directories(concord_special_mechanisms_smoke PRIVATE src)

add_executable(concord_special_building_system_smoke
    tests/RuntimeSpecialBuildingSystemSmoke.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/RuntimeSpecialBuildingSystem.cpp
    src/RuntimeVision.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(concord_special_building_system_smoke PRIVATE src)

add_executable(concord_hospital_armory_mod_compat_smoke
    tests/RuntimeHospitalArmoryModCompatSmoke.cpp
    src/GameWorld.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/RulesDatabase.cpp
    src/RuntimeInfantryServiceSystem.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(
    concord_hospital_armory_mod_compat_smoke PRIVATE src
)

add_executable(concord_cooperative_scenario_smoke
    tests/CooperativeScenarioDatabaseSmoke.cpp
    src/CooperativeScenarioDatabase.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(concord_cooperative_scenario_smoke PRIVATE src)

add_executable(concord_cooperative_session_system_smoke
    tests/RuntimeCooperativeSessionSystemSmoke.cpp
    src/RuntimeCooperativeSessionSystem.cpp
    src/CooperativeScenarioDatabase.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/TheaterDatabase.cpp
)
target_include_directories(
    concord_cooperative_session_system_smoke PRIVATE src
)

add_executable(concord_multiplayer_session_system_smoke
    tests/RuntimeMultiplayerSessionSystemSmoke.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_session_system_smoke PRIVATE src
)

add_executable(concord_lan_transport_smoke
    tests/RuntimeLanTransportSmoke.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(concord_lan_transport_smoke PRIVATE src)

add_executable(concord_multiplayer_protocol_smoke
    tests/RuntimeMultiplayerProtocolSmoke.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(concord_multiplayer_protocol_smoke PRIVATE src)

add_executable(concord_multiplayer_gameplay_command_smoke
    tests/RuntimeMultiplayerGameplayCommandSmoke.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
)
target_include_directories(
    concord_multiplayer_gameplay_command_smoke PRIVATE src
)

add_executable(concord_multiplayer_lockstep_smoke
    tests/RuntimeMultiplayerLockstepSmoke.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_lockstep_smoke PRIVATE src
)

add_executable(concord_multiplayer_weak_network_smoke
    tests/RuntimeMultiplayerWeakNetworkSmoke.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_weak_network_smoke PRIVATE src
)

add_executable(concord_multiplayer_lan_flow_smoke
    tests/RuntimeMultiplayerLanFlowSmoke.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(concord_multiplayer_lan_flow_smoke PRIVATE src)

add_executable(concord_multiplayer_match_setup_smoke
    tests/RuntimeMultiplayerMatchSetupSmoke.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(concord_multiplayer_match_setup_smoke PRIVATE src)

add_executable(concord_multiplayer_snapshot_transfer_smoke
    tests/RuntimeMultiplayerSnapshotTransferSmoke.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_snapshot_transfer_smoke PRIVATE src
)

add_executable(concord_multiplayer_compatibility_fingerprint_smoke
    tests/RuntimeMultiplayerCompatibilityFingerprintSmoke.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
)
target_include_directories(
    concord_multiplayer_compatibility_fingerprint_smoke PRIVATE src
)

add_executable(concord_multiplayer_launch_config_smoke
    tests/RuntimeMultiplayerLaunchConfigSmoke.cpp
    src/RuntimeMultiplayerLaunchConfig.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(
    concord_multiplayer_launch_config_smoke PRIVATE src
)

add_executable(concord_multiplayer_lobby_model_smoke
    tests/RuntimeMultiplayerLobbyModelSmoke.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(
    concord_multiplayer_lobby_model_smoke PRIVATE src
)

add_executable(concord_multiplayer_runtime_bridge_smoke
    tests/RuntimeMultiplayerRuntimeBridgeSmoke.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_runtime_bridge_smoke PRIVATE src
)

add_executable(concord_multiplayer_two_process_smoke
    tests/RuntimeMultiplayerTwoProcessSmoke.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_two_process_smoke PRIVATE src
)

add_executable(concord_multiplayer_lobby_flow_smoke
    tests/RuntimeMultiplayerLobbyFlowSmoke.cpp
    src/RuntimeMultiplayerResumeSave.cpp src/IniFile.cpp
    src/RuntimeMultiplayerLobbyFlow.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerLaunchConfig.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_lobby_flow_smoke PRIVATE src
)

add_executable(concord_multiplayer_player_intent_smoke
    tests/RuntimeMultiplayerPlayerIntentSmoke.cpp
    src/RuntimeMultiplayerPlayerIntent.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
)
target_include_directories(
    concord_multiplayer_player_intent_smoke PRIVATE src
)

add_executable(concord_internet_connectivity_smoke
    tests/RuntimeInternetConnectivitySmoke.cpp
    src/RuntimeInternetConnectivity.cpp
)
target_include_directories(
    concord_internet_connectivity_smoke PRIVATE src
)

add_executable(concord_internet_coordinator_smoke
    tests/RuntimeInternetCoordinatorSmoke.cpp
    src/RuntimeInternetCoordinator.cpp
    src/RuntimeInternetConnectivity.cpp
)
target_include_directories(concord_internet_coordinator_smoke PRIVATE src)

add_executable(concord_internet_matchmaking_smoke
    tests/RuntimeInternetMatchmakingSmoke.cpp
    src/RuntimeInternetMatchmaking.cpp
    src/RuntimeInternetMatchmakingProtocol.cpp
)
target_include_directories(concord_internet_matchmaking_smoke PRIVATE src)

add_executable(concord_internet_community_smoke
    tests/RuntimeInternetCommunitySmoke.cpp
    src/RuntimeInternetCommunity.cpp
)
target_include_directories(concord_internet_community_smoke PRIVATE src)

add_executable(concord_internet_ranked_service_smoke
    tests/RuntimeInternetRankedServiceSmoke.cpp src/RuntimeInternetCommunity.cpp)
target_include_directories(concord_internet_ranked_service_smoke PRIVATE src)

add_executable(concord_internet_lobby_flow_smoke
    tests/RuntimeInternetLobbyFlowSmoke.cpp
    src/RuntimeMultiplayerResumeSave.cpp src/IniFile.cpp
    src/RuntimeInternetLobbyFlow.cpp
    src/RuntimeInternetCommunity.cpp
    src/RuntimeInternetMatchmaking.cpp
    src/RuntimeInternetMatchmakingProtocol.cpp
    src/RuntimeInternetCoordinator.cpp
    src/RuntimeInternetConnectivity.cpp
    src/RuntimeInternetTcpTunnelProxy.cpp
    src/RuntimeMultiplayerLobbyFlow.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerLaunchConfig.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(concord_internet_lobby_flow_smoke PRIVATE src)

add_executable(concord_internet_tcp_tunnel_proxy_smoke
    tests/RuntimeInternetTcpTunnelProxySmoke.cpp
    src/RuntimeInternetTcpTunnelProxy.cpp
    src/RuntimeMultiplayerLanSession.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_internet_tcp_tunnel_proxy_smoke PRIVATE src
)

add_executable(concord_internet_tls_transport_smoke
    tests/RuntimeInternetTlsTransportSmoke.cpp
    src/RuntimeInternetTlsTransport.cpp
)
target_include_directories(
    concord_internet_tls_transport_smoke PRIVATE src
)

add_executable(concord_internet_service_transport_smoke
    tests/RuntimeInternetServiceTransportSmoke.cpp
    src/RuntimeInternetServiceTransport.cpp
    src/RuntimeInternetTlsTransport.cpp
    src/RuntimeInternetConnectivity.cpp
    src/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_internet_service_transport_smoke PRIVATE src
)

add_library(concord_internet_service_host_core STATIC
    src/RuntimeInternetServiceHost.cpp
    src/RuntimeInternetServiceHostConfig.cpp
    src/RuntimeInternetServiceTransport.cpp
    src/RuntimeInternetTlsTransport.cpp
    src/RuntimeInternetCommunity.cpp
    src/RuntimeInternetCoordinator.cpp
    src/RuntimeInternetMatchmaking.cpp
    src/RuntimeInternetMatchmakingProtocol.cpp
    src/RuntimeInternetConnectivity.cpp
    src/RuntimeInternetTcpTunnelProxy.cpp
    src/RuntimeLanTransport.cpp
    src/RuntimeMultiplayerProtocol.cpp
    src/RuntimeMultiplayerSessionSystem.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
    src/RuntimeMultiplayerSnapshotTransfer.cpp
    src/RuntimeMultiplayerGameplayCommand.cpp
    src/RuntimeMultiplayerLockstep.cpp
)
target_include_directories(concord_internet_service_host_core PUBLIC src)

add_executable(concord_internet_service
    src/RuntimeInternetServiceMain.cpp
)
target_link_libraries(
    concord_internet_service PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_host_smoke
    tests/RuntimeInternetServiceHostSmoke.cpp
)
target_link_libraries(
    concord_internet_service_host_smoke
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_lobby_e2e
    tests/RuntimeInternetServiceLobbyE2E.cpp
    src/RuntimeMultiplayerResumeSave.cpp src/IniFile.cpp
    src/RuntimeInternetLobbyFlow.cpp
    src/RuntimeMultiplayerLobbyFlow.cpp
    src/RuntimeMultiplayerRuntimeBridge.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerLaunchConfig.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/RuntimeMultiplayerLanSession.cpp
)
target_include_directories(concord_internet_service_lobby_e2e PRIVATE src)
target_link_libraries(
    concord_internet_service_lobby_e2e
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_host_config_smoke
    tests/RuntimeInternetServiceHostConfigSmoke.cpp
)
target_link_libraries(
    concord_internet_service_host_config_smoke
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_multiplayer_menu_smoke
    tests/RuntimeInternetMultiplayerMenuSmoke.cpp
    src/RuntimeInternetMultiplayerMenu.cpp
)
target_include_directories(
    concord_internet_multiplayer_menu_smoke PRIVATE src
)
target_compile_definitions(
    concord_internet_multiplayer_menu_smoke
    PRIVATE RUNTIME_INTERNET_MULTIPLAYER_MENU_PURE_HELPERS_ONLY
)

add_executable(concord_multiplayer_menu_smoke
    tests/RuntimeMultiplayerMenuSmoke.cpp
    src/RuntimeMultiplayerMenu.cpp
    src/RuntimeRandomMap.cpp src/RuntimeAtomicFile.cpp src/RuntimeCampaignDataSource.cpp
    src/RulesDatabase.cpp src/RuntimeFactoryDeliveryState.cpp src/MixArchive.cpp src/WestwoodCrypto.cpp
    src/CooperativeScenarioDatabase.cpp
    src/IniFile.cpp
    src/MapDatabase.cpp
    src/TheaterDatabase.cpp
    src/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/RuntimeMultiplayerLobbyModel.cpp
    src/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(concord_multiplayer_menu_smoke PRIVATE src)
target_compile_definitions(
    concord_multiplayer_menu_smoke
    PRIVATE RUNTIME_MULTIPLAYER_MENU_PURE_HELPERS_ONLY _CRT_SECURE_NO_WARNINGS
)
if(MSVC)
    # RuntimeAtomicFile's Windows error branches are shared with the runtime.
    target_compile_options(concord_multiplayer_menu_smoke PRIVATE /wd4702)
endif()

# The retail Bink 1.0q decoder is 32-bit, while the normal runtime build is
# 64-bit.  Build the small decoder bridge with a Win32 sub-build and place it
# beside concord_runtime; the menu exchanges BGRA frames through shared memory.
if (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    include(ExternalProject)
    ExternalProject_Add(
        concord_bink_decoder32
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools"
        BINARY_DIR "${CMAKE_BINARY_DIR}/bink_decoder32"
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        CMAKE_GENERATOR_PLATFORM Win32
        BUILD_ALWAYS TRUE
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        INSTALL_COMMAND ""
    )
    # ExternalProject has its own build stamps.  Stage the helper through an
    # always-run target so a decoder-only source edit cannot leave the copy
    # beside concord_runtime stale when the 64-bit executable does not relink.
    add_custom_target(
        concord_bink_decoder32_stage
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/bink_decoder32/$<CONFIG>/BinkFrameExtractor.exe"
            "${CMAKE_BINARY_DIR}/$<CONFIG>/BinkFrameExtractor.exe"
        COMMENT "Staging the 32-bit Bink decoder bridge"
        VERBATIM
    )
    add_dependencies(concord_bink_decoder32_stage concord_bink_decoder32)
    add_dependencies(concord_runtime concord_bink_decoder32_stage)
    if (EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll"
        AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik")
        add_custom_command(
            TARGET concord_runtime POST_BUILD
            COMMAND "$<TARGET_FILE_DIR:concord_runtime>/BinkFrameExtractor.exe"
                "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll"
                "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik"
                "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l_frame.bmp"
                1
            COMMENT "Refreshing the original Yuri shell first-frame fallback"
            VERBATIM
        )
    endif()
elseif (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(tools bink_decoder32_native)
    add_dependencies(concord_runtime BinkFrameExtractor)
endif()

# Bump when shared rules structs change so every translation unit is rebuilt
# with the same container element layout.
set(CONCORD_RULES_ABI_REVISION 2026092202)
target_compile_definitions(
    concord_rules_dump PRIVATE
    CONCORD_RULES_ABI_REVISION=${CONCORD_RULES_ABI_REVISION}
)
target_compile_definitions(
    concord_runtime PRIVATE
    CONCORD_RULES_ABI_REVISION=${CONCORD_RULES_ABI_REVISION}
    $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
)

if (MSVC)
    target_compile_options(concord_map_data_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_game_world_deterministic_hash_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive- /wd4189 /wd4996
    )
    target_compile_options(concord_campaign_progress_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_campaign_map_selection_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(
        concord_runtime_campaign_movie_resolver_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(
        concord_campaign_presentation_decoder_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(concord_rules_special_features_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_rules_pad_aircraft_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_rules_production_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_rules_lookup_dispatch_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_rules_overlay_identity_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_rules_crate_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_aircraft_production_matcher_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(concord_particle_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_garrison_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_subcell_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_subcell_selective_batch_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_repair_depot_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_factory_delivery_state_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_grinding_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_gate_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_world_audio_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_audio_sample_resolver_edition_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(concord_power_audio_feedback_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_lighting_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_production_lanes_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_production_timing_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_runtime_atomic_file_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_runtime_map_package_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_spy_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_airstrike_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_death_weapon_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_psychic_status_system_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_special_mechanisms_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_special_building_system_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(
        concord_hospital_armory_mod_compat_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive- /wd4189 /wd4996
    )
    target_compile_options(concord_cooperative_scenario_smoke PRIVATE /utf-8 /FS)
    target_compile_options(
        concord_cooperative_session_system_smoke PRIVATE /utf-8 /FS
    )
    target_compile_options(
        concord_multiplayer_session_system_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_lan_transport_smoke PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_protocol_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_gameplay_command_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_lockstep_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_weak_network_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_lan_flow_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_match_setup_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_snapshot_transfer_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_compatibility_fingerprint_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_launch_config_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_lobby_model_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_runtime_bridge_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_two_process_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_lobby_flow_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_player_intent_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_connectivity_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_coordinator_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_matchmaking_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_lobby_flow_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_tcp_tunnel_proxy_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_tls_transport_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service_transport_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service_host_core
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service_host_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service_lobby_e2e
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_service_host_config_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_internet_multiplayer_menu_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    target_compile_options(
        concord_multiplayer_menu_smoke
        PRIVATE /utf-8 /FS /W4 /WX /permissive- /wd4189
    )
    target_compile_options(
        concord_rules_dump PRIVATE
        /utf-8
        /FS
        $<$<CONFIG:Debug>:/RTC1>
    )
    # Runtime rendering is dominated by large CPU pixel loops.  Keeping those
    # loops at /Od makes the normal Visual Studio Debug launch more than an
    # order of magnitude slower than Release and masks real gameplay costs.
    # Preserve debug symbols/assertions while compiling the playable runtime
    # with optimized code in Debug as well.
    # This target compiles its own C++ sources and links only platform import
    # libraries. Use the production CRT in every configuration as well: the
    # Debug heap otherwise adds bookkeeping to the hot simulation allocations.
    # Keep the existing Debug symbols/assertions and iterator ABI setting.
    set_property(TARGET concord_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )
    target_compile_options(
        concord_runtime PRIVATE
        /utf-8
        /FS
        /bigobj
        $<$<CONFIG:Debug>:/O2>
        $<$<CONFIG:Debug>:/Ob2>
    )
endif()

if (WIN32)
    # Explorer/shortcut launches should open only the game window.
    set_property(TARGET concord_runtime PROPERTY WIN32_EXECUTABLE TRUE)
    target_link_libraries(
        concord_runtime PRIVATE winmm ws2_32 bcrypt secur32 crypt32 comdlg32 xaudio2 ole32 xapobase
    )
    target_link_libraries(concord_multiplayer_menu_smoke PRIVATE bcrypt)
    target_link_libraries(concord_lan_transport_smoke PRIVATE ws2_32)
    target_link_libraries(concord_multiplayer_protocol_smoke PRIVATE ws2_32)
    target_link_libraries(concord_multiplayer_lan_flow_smoke PRIVATE ws2_32)
    target_link_libraries(
        concord_multiplayer_runtime_bridge_smoke PRIVATE ws2_32
    )
    target_link_libraries(
        concord_multiplayer_two_process_smoke PRIVATE ws2_32
    )
    target_link_libraries(
        concord_multiplayer_lobby_flow_smoke PRIVATE ws2_32
    )
    target_link_libraries(
        concord_internet_lobby_flow_smoke PRIVATE ws2_32 bcrypt
    )
    target_link_libraries(
        concord_internet_tcp_tunnel_proxy_smoke PRIVATE ws2_32 bcrypt
    )
    target_link_libraries(
        concord_internet_tls_transport_smoke
        PRIVATE ws2_32 secur32 crypt32 bcrypt advapi32
    )
    target_link_libraries(
        concord_internet_service_transport_smoke
        PRIVATE ws2_32 secur32 crypt32 bcrypt advapi32
    )
    target_link_libraries(
        concord_internet_service_host_core
        PUBLIC ws2_32 secur32 crypt32 bcrypt advapi32
    )
    target_link_libraries(
        concord_internet_multiplayer_menu_smoke PRIVATE bcrypt
    )
endif()

enable_testing()
add_test(NAME runtime_aircraft_flight_codec_smoke_test COMMAND concord_aircraft_flight_codec_smoke)
add_test(NAME runtime_subcell_requested_index_smoke_test COMMAND concord_subcell_requested_index_smoke)
set_tests_properties(runtime_subcell_requested_index_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_MOVEMENT_REFERENCE=0;CONCORD_HISTORY_QUERY_REFERENCE=0")
add_test(NAME runtime_mod_history_noop_test COMMAND concord_mod_history_noop_smoke)
add_test(NAME runtime_history_owner_census_test COMMAND concord_history_owner_census_smoke)
add_test(NAME runtime_presentation_work_budget_test COMMAND concord_presentation_work_budget_smoke)
add_test(NAME runtime_presentation_budget_remainder_test COMMAND concord_presentation_budget_remainder_smoke)
add_test(NAME runtime_simulation_query_reuse_test COMMAND concord_simulation_query_reuse_smoke)
add_test(NAME runtime_shadow_opaque_span_test COMMAND concord_shadow_opaque_span_smoke)
add_test(NAME runtime_water_wake_history_test COMMAND concord_water_wake_history_smoke)
add_test(NAME runtime_frame_diagnostics_test COMMAND concord_frame_diagnostics_smoke
    "${CMAKE_BINARY_DIR}/playability/diag-test")
add_test(NAME runtime_magnetron_mechanics_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_magnetron_mechanics_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_MAGNETRON_MECHANICS_ONLY=1" TIMEOUT 600)
add_test(NAME runtime_game_speed_timing_test COMMAND concord_game_speed_timing_smoke
    "${CMAKE_SOURCE_DIR}")
set_tests_properties(runtime_game_speed_timing_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME runtime_building_depth_core_test COMMAND concord_building_depth_smoke
    "${CMAKE_SOURCE_DIR}")
set_tests_properties(runtime_building_depth_core_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME runtime_naval_scene_depth_core_test COMMAND concord_naval_scene_depth_smoke
    "${CMAKE_SOURCE_DIR}")
set_tests_properties(runtime_naval_scene_depth_core_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
add_test(NAME runtime_lighting_lookup_raster_smoke_test COMMAND concord_lighting_lookup_raster_smoke)
if(TARGET concord_body_tint_context_smoke)
    add_test(NAME runtime_body_tint_context_smoke_test COMMAND concord_body_tint_context_smoke)
endif()
add_test(NAME runtime_movement_classification_smoke_test COMMAND concord_movement_classification_smoke)
add_test(NAME runtime_unit_alignment_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_unit_alignment_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_UNIT_ALIGNMENT_ONLY=1" TIMEOUT 600)
add_test(NAME runtime_building_alignment_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_building_alignment_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_ALIGNMENT_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_building_occlusion_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_building_occlusion_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_OCCLUSION_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_issue25_neutral_wall_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_neutral_wall_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_ISSUE25_NEUTRAL_WALL_ONLY=1;CONCORD_ISSUE25_NEUTRAL_WALL_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/03-neutral-wall/ctest" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_manual_unload_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_manual_unload_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_HARVESTER_MANUAL_UNLOAD_ISSUE25_ONLY=1;CONCORD_HARVESTER_MANUAL_UNLOAD_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/04-manual-unload/ctest" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_own_building_cursor_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_own_building_cursor_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_OWN_BUILDING_CURSOR_ISSUE25_ONLY=1;CONCORD_OWN_BUILDING_CURSOR_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/05-own-building-cursor/ctest" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_chrono_transport_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_chrono_transport_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CHRONO_TRANSPORT_ISSUE25_ONLY=1;CONCORD_CHRONO_TRANSPORT_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/06-chrono-transport/ctest" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_own_building_loss_audio_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_own_building_loss_audio_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_OWN_BUILDING_LOSS_AUDIO_ISSUE25_ONLY=1;CONCORD_OWN_BUILDING_LOSS_AUDIO_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/07-own-building-loss-audio/ctest" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_kirov_building_bomb_approach_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_kirov_building_bomb_approach_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_KIROV_BUILDING_BOMB_APPROACH_ISSUE25_ONLY=1;CONCORD_KIROV_BUILDING_BOMB_APPROACH_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/09-kirov-bomb-approach/ctest" TIMEOUT 900 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_factory_rally_audio_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_factory_rally_audio_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_FACTORY_RALLY_AUDIO_ISSUE25_SMOKE=1;CONCORD_FACTORY_RALLY_AUDIO_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/14-factory-rally-audio/ctest" TIMEOUT 600 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_patriot_turret_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_patriot_turret_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_PATRIOT_TURRET_ISSUE25_SMOKE=1;CONCORD_PATRIOT_TURRET_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/13-patriot-turret/ctest" TIMEOUT 600 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_dolphin_damage_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_dolphin_damage_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_DOLPHIN_DAMAGE_ISSUE25_SMOKE=1;CONCORD_DOLPHIN_DAMAGE_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/12-dolphin-damage/ctest" TIMEOUT 300 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_shipyard_repair_animation_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_shipyard_repair_animation_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_SHIPYARD_REPAIR_ANIMATION_ISSUE25_SMOKE=1;CONCORD_SHIPYARD_REPAIR_ANIMATION_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/11-shipyard-repair-animation/ctest" TIMEOUT 1800)

add_test(NAME runtime_issue25_spawned_missile_walls_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_spawned_missile_walls_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SPAWNED_MISSILE_WALLS_ISSUE25_ONLY=1;CONCORD_SPAWNED_MISSILE_WALLS_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/10-spawned-missile-walls/ctest" TIMEOUT 1200 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_overlay_missile_walls_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_overlay_missile_walls_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_OVERLAY_MISSILE_WALLS_ISSUE25_ONLY=1;CONCORD_OVERLAY_MISSILE_WALLS_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/10-spawned-missile-walls/overlay-ctest" TIMEOUT 1200 RUN_SERIAL TRUE)

add_test(NAME runtime_issue25_tanya_c4_approach_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_tanya_c4_approach_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_TANYA_C4_APPROACH_ISSUE25_ONLY=1;CONCORD_TANYA_C4_APPROACH_ISSUE25_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/08-tanya-c4-approach/ctest" TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_ground_foundation_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_ground_foundation_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_ISSUE25_FOUNDATION_ONLY=1;CONCORD_ISSUE25_FOUNDATION_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/02-foundation/ctest" TIMEOUT 900 RUN_SERIAL TRUE)
add_test(NAME runtime_issue25_ground_occlusion_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_ground_occlusion_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_ISSUE25_GROUND_OCCLUSION_ONLY=1;CONCORD_ISSUE25_GROUND_OCCLUSION_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/01-occlusion/ctest-ground" TIMEOUT 900)
add_test(NAME runtime_issue25_ground_markers_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_issue25_ground_markers_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_ISSUE25_GROUND_MARKER_SMOKE=1;CONCORD_ISSUE25_GROUND_MARKER_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-25/01-occlusion/ctest-markers" TIMEOUT 180)
add_test(NAME runtime_mirage_disguise_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_mirage_disguise_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_MIRAGE_DISGUISE_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_tanya_selection_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_tanya_selection_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_TANYA_SELECTION_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_chrono_effects_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_chrono_effects_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CHRONO_EFFECTS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_chronosphere_selection_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_chronosphere_selection_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_COMMAND_SMOKE_CHRONOSPHERE_SELECTION_ONLY=1;CONCORD_SUPPORT_POWERS_OUTPUT=${CMAKE_BINARY_DIR}/issue21_chronosphere"
    TIMEOUT 180)
add_test(NAME projectile_line_trail_core_test COMMAND concord_projectile_line_trail_smoke)
add_test(NAME runtime_superweapon_activation_audio_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_superweapon_activation_audio_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPERWEAPON_ACTIVATION_AUDIO_ONLY=1;CONCORD_SUPERWEAPON_ACTIVATION_AUDIO_OUTPUT=${CMAKE_BINARY_DIR}/issue21_superweapon_audio"
    TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME runtime_superweapon_death_sound_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_superweapon_death_sound_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_COMMAND_SMOKE_SUPERWEAPON_DEATH_SOUND_ONLY=1"
    TIMEOUT 180)
add_test(NAME runtime_wreck_audio_regression_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_wreck_audio_regression_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_WRECKS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_naval_spawn_attack_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_naval_spawn_attack_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_NAVAL_SPAWN_ATTACK_ONLY=1;CONCORD_NAVAL_SPAWN_OUTPUT=${CMAKE_BINARY_DIR}/issue21_naval_spawn_attack"
    TIMEOUT 240)
add_test(NAME runtime_sell_mode_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_sell_mode_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SELL_MODE_ONLY=1" TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME runtime_building_placement_feedback_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_building_placement_feedback_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_PLACEMENT_FEEDBACK_ONLY=1;CONCORD_BUILDING_PLACEMENT_FEEDBACK_OUTPUT=${CMAKE_BINARY_DIR}/issue21_building_placement_feedback"
    TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME runtime_magnetron_fall_motion_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_magnetron_fall_motion_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_MAGNETRON_FALL_MOTION_ONLY=1;CONCORD_MAGNETRON_FALL_MOTION_OUTPUT=${CMAKE_BINARY_DIR}/issue21_magnetron_fall_motion"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_gattling_continuous_fire_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_gattling_continuous_fire_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_GATTLING_CONTINUOUS_FIRE_ONLY=1;CONCORD_GATTLING_CONTINUOUS_FIRE_OUTPUT=${CMAKE_BINARY_DIR}/issue21_gattling_continuous_fire"
    TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME runtime_deploy_fire_cooldown_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_deploy_fire_cooldown_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_DEPLOY_FIRE_COOLDOWN_ONLY=1;CONCORD_DEPLOY_FIRE_COOLDOWN_OUTPUT=${CMAKE_BINARY_DIR}/issue21_deploy_fire_cooldown"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_diplomacy_page_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_diplomacy_page_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_DIPLOMACY_PAGE_ONLY=1;CONCORD_DIPLOMACY_PAGE_OUTPUT=${CMAKE_BINARY_DIR}/issue21_diplomacy_page"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_battle_fortress_crush_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_battle_fortress_crush_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BATTLE_FORTRESS_CRUSH_ONLY=1;CONCORD_BATTLE_FORTRESS_CRUSH_OUTPUT=${CMAKE_BINARY_DIR}/issue21_battle_fortress_crush"
    TIMEOUT 900 RUN_SERIAL TRUE)
add_test(NAME runtime_player_beacon_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_player_beacon_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PLAYER_BEACON_ONLY=1;CONCORD_PLAYER_BEACON_OUTPUT=${CMAKE_BINARY_DIR}/issue21_player_beacon"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_nighthawk_landing_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_nighthawk_landing_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_NIGHTHAWK_LANDING_ONLY=1;CONCORD_NIGHTHAWK_LANDING_OUTPUT=${CMAKE_BINARY_DIR}/issue21_nighthawk_landing"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_paradrop_formation_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_paradrop_formation_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PARADROP_FORMATION_ONLY=1;CONCORD_PARADROP_FORMATION_OUTPUT=${CMAKE_BINARY_DIR}/issue21_paradrop_formation"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_scatter_distance_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_scatter_distance_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SCATTER_DISTANCE_ONLY=1;CONCORD_SCATTER_DISTANCE_OUTPUT=${CMAKE_BINARY_DIR}/issue21_scatter_distance"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_factory_egress_cadence_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_factory_egress_cadence_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FACTORY_EGRESS_CADENCE_ONLY=1;CONCORD_FACTORY_EGRESS_OUTPUT=${CMAKE_BINARY_DIR}/issue21_factory_egress"
    TIMEOUT 900 RUN_SERIAL TRUE)
add_test(NAME runtime_aircraft_landing_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_aircraft_landing_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_LANDING_ONLY=1;CONCORD_AIRCRAFT_LANDING_OUTPUT=${CMAKE_BINARY_DIR}/issue21_aircraft_landing"
    TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME runtime_aircraft_turn_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_aircraft_turn_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_TURN_ONLY=1" TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_aircraft_aux_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_aircraft_aux_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_AUX_ONLY=1;CONCORD_AIRCRAFT_AUX_DIR=${CMAKE_BINARY_DIR}/issue21_aircraft_landing/aux"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_repair_depot_motion_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_repair_depot_motion_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_REPAIR_DEPOT_MOTION_ONLY=1;CONCORD_REPAIR_DEPOT_MOTION_OUTPUT=${CMAKE_BINARY_DIR}/issue21_repair_depot_motion"
    TIMEOUT 600 RUN_SERIAL TRUE)
add_test(NAME runtime_ifv_repair_targets_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_ifv_repair_targets_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_IFV_REPAIR_TARGETS_ONLY=1;CONCORD_IFV_REPAIR_OUTPUT=${CMAKE_BINARY_DIR}/issue21_ifv_repair"
    TIMEOUT 900)
add_test(NAME runtime_deployed_infantry_move_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_deployed_infantry_move_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_DEPLOYED_INFANTRY_MOVE_ONLY=1;CONCORD_DEPLOYED_INFANTRY_OUTPUT=${CMAKE_BINARY_DIR}/issue21_deployed_infantry"
    TIMEOUT 300)
add_test(NAME runtime_chaos_deploy_move_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_chaos_deploy_move_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CHAOS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_full_deploy_semantics_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_full_deploy_semantics_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FULL_DEPLOY_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_carrier_attack_regression_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_carrier_attack_regression_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CARRIER_ATTACK_ONLY=1;CONCORD_CARRIER_ATTACK_OUTPUT=${CMAKE_BINARY_DIR}/issue21_carrier_attack"
    TIMEOUT 180)
add_test(NAME runtime_grinder_queue_animation_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_grinder_queue_animation_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_GRINDER_QUEUE_ONLY=1;CONCORD_GRINDER_DIR=${CMAKE_BINARY_DIR}/issue21_grinder_queue"
    TIMEOUT 180)
add_test(NAME runtime_grinder_animation_legacy_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_grinder_animation_legacy_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_GRINDER_ANIMATION_ONLY=1;CONCORD_GRINDER_DIR=${CMAKE_BINARY_DIR}/issue21_grinder_legacy"
    TIMEOUT 180)
add_test(NAME runtime_grinder_feedback_regression_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_grinder_feedback_regression_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_GRINDER_FEEDBACK_ONLY=1"
    TIMEOUT 180)
add_test(NAME runtime_spatial_audio_regression_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_spatial_audio_regression_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SPATIAL_AUDIO_ONLY=1;CONCORD_SPATIAL_AUDIO_DIR=${CMAKE_BINARY_DIR}/issue21_spatial_audio" TIMEOUT 180)
add_test(NAME runtime_ifv_effects_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_ifv_effects_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_IFV_EFFECTS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_air_body_impact_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_air_body_impact_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_BODY_IMPACT_ONLY=1;CONCORD_AIR_BODY_IMPACT_OUTPUT=${CMAKE_BINARY_DIR}/issue21_air_body_impact"
    TIMEOUT 600)
add_test(NAME runtime_flak_air_body_impact_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_flak_air_body_impact_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FLAK_AIR_BODY_IMPACT_ONLY=1;CONCORD_FLAK_AIR_BODY_OUTPUT=${CMAKE_BINARY_DIR}/issue22_flak_air_body_impact"
    TIMEOUT 900)
add_test(NAME runtime_aegis_ifv_presentation_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_aegis_ifv_presentation_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AEGIS_IFV_PRESENTATION_ONLY=1;CONCORD_AEGIS_IFV_PRESENTATION_OUTPUT=${CMAKE_BINARY_DIR}/issue21_aegis_ifv_presentation"
    TIMEOUT 300)
add_test(NAME runtime_aegis_trajectory_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_aegis_trajectory_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AEGIS_TRAJECTORY_ONLY=1;CONCORD_AEGIS_TRAJECTORY_OUTPUT=${CMAKE_BINARY_DIR}/issue21_aegis_trajectory"
    TIMEOUT 300)
add_test(NAME audio_decoder_codecs_test COMMAND concord_audio_decoder_smoke
    "${CMAKE_BINARY_DIR}/audio_decoder_codecs_cache")
add_test(NAME runtime_screen_shake_core_test COMMAND concord_screen_shake_smoke)
add_test(NAME runtime_screen_shake_integration_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_screen_shake_integration_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SCREEN_SHAKE_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_mod_extensions_core_test COMMAND concord_mod_extensions_smoke)
set_tests_properties(runtime_mod_extensions_core_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
add_test(NAME runtime_mod_extensions_integration_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_mod_extensions_integration_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_MOD_EXTENSIONS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_infantry_water_sound_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_infantry_water_sound_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_WATER_SOUND_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_ivan_bomb_icon_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_ivan_bomb_icon_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_IVAN_ICON_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_panic_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_panic_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PANIC_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_fire_reveal_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_fire_reveal_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FIRE_REVEAL_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_temporal_allies_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_temporal_allies_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_TEMPORAL_ALLIES_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_aircraft_selection_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_aircraft_selection_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_SELECTION_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_airport_loss_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_airport_loss_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRPORT_LOSS_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_airport_host_contracts_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_airport_host_contracts_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRPORT_CONTRACTS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_aircraft_ammo_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_aircraft_ammo_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_AMMO_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_air_damage_smoke_anchor_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_air_damage_smoke_anchor_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_DAMAGE_SMOKE_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_special_damage_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_special_damage_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SPECIAL_DAMAGE_SMOKE_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_move_destination_audit_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_move_destination_audit_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_MOVE_DESTINATION_AUDIT_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_screen_destination_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_screen_destination_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SCREEN_DESTINATION_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_stock_movement_cadence_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_stock_movement_cadence_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_STOCK_MOVEMENT_CADENCE_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_harvest_rate_timing_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_harvest_rate_timing_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_HARVEST_RATE_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_parasite_healing_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_parasite_healing_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PARASITE_HEALING_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_parasite_leap_exit_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_parasite_leap_exit_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PARASITE_LEAP_EXIT_ONLY=1;CONCORD_PARASITE_LEAP_EXIT_OUTPUT=${CMAKE_BINARY_DIR}/issue22_02_parasite_leap_exit"
    TIMEOUT 900)
add_test(NAME runtime_unit_death_audio_coverage_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_unit_death_audio_coverage_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_UNIT_DEATH_AUDIO_COVERAGE_ONLY=1" TIMEOUT 900)
add_test(NAME runtime_bridge_connectivity_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_bridge_connectivity_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_NATIVE_SHROUD_ONLY=1;CONCORD_BRIDGE_CONNECTIVITY_ONLY=1;CONCORD_BRIDGE_CONNECTIVITY_OUTPUT=${CMAKE_BINARY_DIR}/issue22_03_bridge_connectivity;CONCORD_NATIVE_SHROUD_OUTPUT=${CMAKE_BINARY_DIR}/issue22_03_bridge_connectivity/scene"
    TIMEOUT 600)
add_test(NAME runtime_bridge_projectile_damage_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_bridge_projectile_damage_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BRIDGE_PROJECTILE_DAMAGE_ONLY=1;CONCORD_BRIDGE_PROJECTILE_DAMAGE_OUTPUT=${CMAKE_BINARY_DIR}/issue22_04_bridge_projectile_damage"
    TIMEOUT 900)
add_test(NAME runtime_bridge_hut_demolition_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_bridge_hut_demolition_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BRIDGE_HUT_DEMOLITION_ONLY=1;CONCORD_BRIDGE_HUT_DEMOLITION_OUTPUT=${CMAKE_BINARY_DIR}/issue22_05_bridge_hut_demolition"
    TIMEOUT 900)
add_test(NAME runtime_bridge_collapse_occupants_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_bridge_collapse_occupants_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BRIDGE_COLLAPSE_OCCUPANTS_ONLY=1;CONCORD_BRIDGE_COLLAPSE_OCCUPANTS_OUTPUT=${CMAKE_BINARY_DIR}/issue22_06_bridge_collapse_occupants"
    TIMEOUT 900)
add_test(NAME runtime_helicopter_ground_state_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_helicopter_ground_state_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_HELICOPTER_GROUND_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_aircraft_continuous_motion_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_aircraft_continuous_motion_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIRCRAFT_MOTION_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_jumpjet_kinematics_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_jumpjet_kinematics_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_JUMPJET_KINEMATICS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_combatant_selection_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_combatant_selection_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_COMBATANT_SELECTION_ONLY=1" TIMEOUT 120)
add_test(NAME runtime_chrono_reinforcements_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data")
set_tests_properties(runtime_chrono_reinforcements_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CHRONO_REINFORCEMENTS_ONLY=1" TIMEOUT 120)
add_test(NAME runtime_trigger_reinforcements_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_trigger_reinforcements_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_TRIGGER_REINFORCEMENTS_ONLY=1" TIMEOUT 120)
add_test(NAME runtime_building_placement_range_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_building_placement_range_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_RANGE_ONLY=1" TIMEOUT 60)
add_test(NAME runtime_vehicle_arrival_motion_smoke_test COMMAND concord_vehicle_arrival_motion_smoke)
add_test(NAME runtime_building_economy_smoke_test COMMAND concord_building_economy_smoke)
add_test(NAME runtime_combat_details_smoke_test COMMAND concord_combat_details_smoke)
add_test(NAME runtime_special_details_module_smoke_test COMMAND concord_special_mechanisms_smoke)
add_test(NAME runtime_settings_hotkey_smoke_test COMMAND concord_settings_hotkey_smoke
    "${CMAKE_BINARY_DIR}/runtime_hotkey_preferences_test.ini")
add_test(NAME runtime_reference_hotkeys_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_reference_hotkeys_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_REFERENCE_HOTKEYS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_remaining_controls_smoke_test COMMAND concord_runtime --command-smoke-test
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_remaining_controls_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_REMAINING_CONTROLS_ONLY=1;CONCORD_CONTROLS_OUTPUT=${CMAKE_BINARY_DIR}/remaining_controls_test" TIMEOUT 180)
add_test(NAME runtime_player_commands_integration_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_player_commands_integration_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PLAYER_COMMANDS_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_stop_position_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_stop_position_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_STOP_POSITION_ONLY=1" TIMEOUT 180)
add_test(NAME runtime_group_move_convergence_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_group_move_convergence_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_GROUP_MOVE_CONVERGENCE_ONLY=1" TIMEOUT 120)
add_test(NAME runtime_compact_group_density_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_compact_group_density_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_COMPACT_GROUPS_ONLY=1" TIMEOUT 300)
add_test(NAME runtime_feature_details_integration_smoke_test COMMAND concord_runtime --command-smoke-test)
set_tests_properties(runtime_feature_details_integration_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FEATURE_DETAILS_ONLY=1" TIMEOUT 180)

add_test(
    NAME map_data_health_aircraft_smoke_test
    COMMAND concord_map_data_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/map_data_smoke.map"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/map_data_smoke_rules.ini"
)

add_test(
    NAME runtime_map_package_smoke_test
    COMMAND concord_runtime_map_package_smoke
)

add_test(
    NAME campaign_progress_smoke_test
    COMMAND concord_campaign_progress_smoke
)

add_test(
    NAME campaign_map_selection_smoke_test
    COMMAND concord_campaign_map_selection_smoke
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/local.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini")
    add_test(
        NAME runtime_campaign_data_source_smoke_test
        COMMAND concord_runtime_campaign_data_source_smoke
            "${CMAKE_SOURCE_DIR}/RedAlert2"
            "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies01.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies02.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/movmd03.mix")
    add_test(
        NAME runtime_campaign_movie_resolver_smoke_test
        COMMAND concord_runtime_campaign_movie_resolver_smoke
            "${CMAKE_SOURCE_DIR}/RedAlert2"
            "${CMAKE_SOURCE_DIR}/local_data"
            "${CMAKE_BINARY_DIR}/runtime_campaign_movie_smoke_cache"
    )
endif()

add_test(
    NAME campaign_presentation_decoder_smoke_test
    COMMAND concord_campaign_presentation_decoder_smoke
)

add_test(
    NAME rules_special_features_smoke_test
    COMMAND concord_rules_special_features_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/special_feature_rules.ini"
)
add_test(
    NAME rules_poison_policy_smoke_test
    COMMAND concord_rules_poison_policy_smoke
        "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
)

add_test(
    NAME rules_pad_aircraft_smoke_test
    COMMAND concord_rules_pad_aircraft_smoke
        --fixture
        "${CMAKE_SOURCE_DIR}/tests/fixtures/pad_aircraft_rules.ini"
)

add_test(
    NAME rules_production_smoke_test
    COMMAND concord_rules_production_smoke
)

add_test(
    NAME rules_lookup_dispatch_smoke_test
    COMMAND concord_rules_lookup_dispatch_smoke
)

add_test(
    NAME rules_overlay_identity_smoke_test
    COMMAND concord_rules_overlay_identity_smoke
        --stock "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
)

add_test(
    NAME rules_crate_smoke_test
    COMMAND concord_rules_crate_smoke
        --fixture
        "${CMAKE_SOURCE_DIR}/tests/fixtures/crate_rules.ini"
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini")
    add_test(
        NAME rules_special_features_stock_rules_test
        COMMAND concord_rules_special_features_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
    add_test(
        NAME rules_pad_aircraft_stock_rules_test
        COMMAND concord_rules_pad_aircraft_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
    add_test(
        NAME rules_production_stock_rules_test
        COMMAND concord_rules_production_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
    add_test(
        NAME rules_crate_stock_rules_test
        COMMAND concord_rules_crate_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
endif()

add_test(
    NAME runtime_particle_system_smoke_test
    COMMAND concord_particle_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/particle_rules.ini"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/particle_art.ini"
)

if (
    EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/artmd.ini"
)
    add_test(
        NAME runtime_particle_system_stock_rules_test
        COMMAND concord_particle_system_smoke
            "${CMAKE_SOURCE_DIR}/tests/fixtures/particle_rules.ini"
            "${CMAKE_SOURCE_DIR}/tests/fixtures/particle_art.ini"
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/artmd.ini"
    )
endif()

add_test(
    NAME runtime_garrison_system_smoke_test
    COMMAND concord_garrison_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/garrison_rules.ini"
)

add_test(
    NAME runtime_subcell_system_smoke_test
    COMMAND concord_subcell_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/subcell_system_smoke.ini"
)

add_test(
    NAME runtime_subcell_selective_batch_smoke_test
    COMMAND concord_subcell_selective_batch_smoke
)
set_tests_properties(runtime_subcell_selective_batch_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_MOVEMENT_REFERENCE=0;CONCORD_FRAME_SCALING_REFERENCE=0"
)
add_test(
    NAME runtime_subcell_selective_batch_reference_smoke_test
    COMMAND concord_subcell_selective_batch_smoke
)
set_tests_properties(runtime_subcell_selective_batch_reference_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_MOVEMENT_REFERENCE=0;CONCORD_FRAME_SCALING_REFERENCE=1"
)

add_test(
    NAME runtime_repair_depot_system_smoke_test
    COMMAND concord_repair_depot_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/repair_depot_rules.ini"
)

add_test(
    NAME runtime_factory_delivery_state_smoke_test
    COMMAND concord_factory_delivery_state_smoke
)

add_test(
    NAME runtime_grinding_system_smoke_test
    COMMAND concord_grinding_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/grinding_rules.ini"
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini")
    add_test(
        NAME runtime_grinding_stock_rules_test
        COMMAND concord_grinding_system_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
endif()

add_test(
    NAME runtime_aircraft_service_system_smoke_test
    COMMAND concord_aircraft_service_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_service_rules.ini"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_service_map.map"
)

add_test(
    NAME runtime_aircraft_production_matcher_smoke_test
    COMMAND concord_aircraft_production_matcher_smoke
)

add_test(
    NAME runtime_gate_system_smoke_test
    COMMAND concord_gate_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/gate_system_rules.ini"
)

add_test(
    NAME runtime_world_audio_system_smoke_test
    COMMAND concord_world_audio_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/world_audio_sound.ini"
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/soundmd.ini")
    add_test(
        NAME runtime_world_audio_system_stock_sound_test
        COMMAND concord_world_audio_system_smoke
            "${CMAKE_SOURCE_DIR}/tests/fixtures/world_audio_sound.ini"
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/soundmd.ini"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/language/audio.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/audiomd.mix")
    add_test(
        NAME audio_sample_resolver_edition_smoke_test
        COMMAND concord_audio_sample_resolver_edition_smoke
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
            "${CMAKE_BINARY_DIR}/audio_sample_resolver_edition_cache"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini")
    add_test(
        NAME runtime_power_audio_feedback_smoke_test
        COMMAND concord_power_audio_feedback_smoke
            "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
    )
endif()

add_test(
    NAME runtime_lighting_system_smoke_test
    COMMAND concord_lighting_system_smoke
)

add_test(
    NAME runtime_production_lanes_smoke_test
    COMMAND concord_production_lanes_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/production_lanes_rules.ini"
)

add_test(
    NAME runtime_production_timing_smoke_test
    COMMAND concord_production_timing_smoke
)

add_test(
    NAME runtime_atomic_file_smoke_test
    COMMAND concord_runtime_atomic_file_smoke
)

add_test(
    NAME runtime_spy_system_smoke_test
    COMMAND concord_spy_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/spy_system_rules.ini"
)

add_test(
    NAME runtime_airstrike_system_smoke_test
    COMMAND concord_airstrike_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/airstrike_system_rules.ini"
)

add_test(
    NAME runtime_death_weapon_system_smoke_test
    COMMAND concord_death_weapon_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/death_weapon_rules.ini"
)

add_test(
    NAME runtime_mag_beam_system_smoke_test
    COMMAND concord_mag_beam_system_smoke
)

add_test(
    NAME runtime_psychic_status_system_smoke_test
    COMMAND concord_psychic_status_system_smoke
)

add_test(
    NAME runtime_special_building_system_smoke_test
    COMMAND concord_special_building_system_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/special_building_runtime_rules.ini"
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini")
    add_test(
        NAME runtime_hospital_armory_mod_compat_smoke_test
        COMMAND concord_hospital_armory_mod_compat_smoke
            "${CMAKE_SOURCE_DIR}/tests/fixtures/hospital_armory_mod_rules.ini"
            "${CMAKE_SOURCE_DIR}/tests/fixtures/hospital_armory_mod_map.map"
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
else()
    add_test(
        NAME runtime_hospital_armory_mod_compat_smoke_test
        COMMAND concord_hospital_armory_mod_compat_smoke
            "${CMAKE_SOURCE_DIR}/tests/fixtures/hospital_armory_mod_rules.ini"
            "${CMAKE_SOURCE_DIR}/tests/fixtures/hospital_armory_mod_map.map"
    )
endif()

add_test(
    NAME runtime_hospital_armory_mod_runtime_integration_smoke_test
    COMMAND concord_runtime
        --hospital-armory-mod-compat-smoke-test
        "${CMAKE_SOURCE_DIR}/tests/fixtures/hospital_armory_mod_rules.ini"
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini")
    add_test(
        NAME runtime_death_weapon_stock_rules_test
        COMMAND concord_death_weapon_system_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini"
    )
endif()

add_test(
    NAME cooperative_scenario_database_smoke_test
    COMMAND concord_cooperative_scenario_smoke
        --fixture
        "${CMAKE_SOURCE_DIR}/tests/fixtures/cooperative_scenarios.ini"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/cooperative_scenario.map"
)

add_test(
    NAME runtime_cooperative_session_system_smoke_test
    COMMAND concord_cooperative_session_system_smoke
        --fixture
        "${CMAKE_SOURCE_DIR}/tests/fixtures/cooperative_scenarios.ini"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/cooperative_scenario.map"
)

add_test(
    NAME runtime_multiplayer_session_system_smoke_test
    COMMAND concord_multiplayer_session_system_smoke
)

add_test(
    NAME runtime_lan_transport_smoke_test
    COMMAND concord_lan_transport_smoke
)

add_test(
    NAME runtime_multiplayer_protocol_smoke_test
    COMMAND concord_multiplayer_protocol_smoke
)

add_test(
    NAME runtime_multiplayer_lockstep_smoke_test
    COMMAND concord_multiplayer_lockstep_smoke
)

add_test(
    NAME runtime_multiplayer_weak_network_smoke_test
    COMMAND concord_multiplayer_weak_network_smoke
)
set_tests_properties(
    runtime_multiplayer_weak_network_smoke_test PROPERTIES TIMEOUT 180
)

add_test(
    NAME game_world_deterministic_hash_smoke_test
    COMMAND concord_game_world_deterministic_hash_smoke
)

add_test(
    NAME runtime_object_index_smoke_test
    COMMAND concord_runtime_object_index_smoke
)

add_test(
    NAME path_request_queue_performance_smoke_test
    COMMAND concord_path_request_queue_performance_smoke
)

add_test(
    NAME runtime_multiplayer_gameplay_command_smoke_test
    COMMAND concord_multiplayer_gameplay_command_smoke
)

add_test(
    NAME runtime_multiplayer_lan_flow_smoke_test
    COMMAND concord_multiplayer_lan_flow_smoke
)

add_test(
    NAME runtime_multiplayer_match_setup_smoke_test
    COMMAND concord_multiplayer_match_setup_smoke
)

add_test(
    NAME runtime_multiplayer_snapshot_transfer_smoke_test
    COMMAND concord_multiplayer_snapshot_transfer_smoke
)

add_test(
    NAME runtime_multiplayer_compatibility_fingerprint_smoke_test
    COMMAND concord_multiplayer_compatibility_fingerprint_smoke
)

add_test(
    NAME runtime_multiplayer_launch_config_smoke_test
    COMMAND concord_multiplayer_launch_config_smoke
)

add_test(
    NAME runtime_multiplayer_lobby_model_smoke_test
    COMMAND concord_multiplayer_lobby_model_smoke
)

add_test(
    NAME runtime_multiplayer_runtime_bridge_smoke_test
    COMMAND concord_multiplayer_runtime_bridge_smoke
)

add_test(
    NAME runtime_multiplayer_two_process_smoke_test
    COMMAND concord_multiplayer_two_process_smoke
)
set_tests_properties(
    runtime_multiplayer_two_process_smoke_test PROPERTIES TIMEOUT 70
)

add_test(
    NAME runtime_multiplayer_two_process_20x_stability_test
    COMMAND concord_multiplayer_two_process_smoke --repeat 20
)
set_tests_properties(
    runtime_multiplayer_two_process_20x_stability_test
    PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE
)

add_test(
    NAME runtime_multiplayer_lobby_flow_smoke_test
    COMMAND concord_multiplayer_lobby_flow_smoke
)

add_test(
    NAME runtime_multiplayer_player_intent_smoke_test
    COMMAND concord_multiplayer_player_intent_smoke
)

add_test(
    NAME runtime_internet_connectivity_smoke_test
    COMMAND concord_internet_connectivity_smoke
)

add_test(
    NAME runtime_internet_coordinator_smoke_test
    COMMAND concord_internet_coordinator_smoke
)

add_test(
    NAME runtime_internet_matchmaking_smoke_test
    COMMAND concord_internet_matchmaking_smoke
)

add_test(
    NAME runtime_internet_community_smoke_test
    COMMAND concord_internet_community_smoke
)

add_test(
    NAME runtime_internet_lobby_flow_smoke_test
    COMMAND concord_internet_lobby_flow_smoke
)

add_test(
    NAME runtime_internet_tcp_tunnel_proxy_smoke_test
    COMMAND concord_internet_tcp_tunnel_proxy_smoke
)

add_test(
    NAME runtime_internet_tls_transport_smoke_test
    COMMAND concord_internet_tls_transport_smoke
)

add_test(
    NAME runtime_internet_service_transport_smoke_test
    COMMAND concord_internet_service_transport_smoke
)

add_test(
    NAME runtime_internet_service_host_smoke_test
    COMMAND concord_internet_service_host_smoke
)
set_tests_properties(
    runtime_internet_service_host_smoke_test PROPERTIES TIMEOUT 70
)

add_test(
    NAME runtime_internet_service_lobby_e2e_test
    COMMAND concord_internet_service_lobby_e2e
)
set_tests_properties(
    runtime_internet_service_lobby_e2e_test PROPERTIES TIMEOUT 120
)

add_test(
    NAME runtime_internet_service_host_config_smoke_test
    COMMAND concord_internet_service_host_config_smoke
)

add_test(
    NAME runtime_internet_multiplayer_menu_smoke_test
    COMMAND concord_internet_multiplayer_menu_smoke
)

add_test(
    NAME runtime_multiplayer_menu_smoke_test
    COMMAND concord_multiplayer_menu_smoke
)

add_test(
    NAME runtime_multiplayer_house_authority_smoke_test
    COMMAND concord_runtime --multiplayer-house-authority-smoke-test
)

add_test(
    NAME runtime_multiplayer_gameplay_authority_smoke_test
    COMMAND concord_runtime --multiplayer-gameplay-authority-smoke-test
)

add_test(
    NAME runtime_multiplayer_authoritative_hash_smoke_test
    COMMAND concord_runtime --multiplayer-authoritative-hash-smoke-test
)

add_test(
    NAME runtime_force_attack_smoke_test
    COMMAND concord_runtime --command-smoke-test
)
set_tests_properties(runtime_force_attack_smoke_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_FORCE_ATTACK_ONLY=1"
    TIMEOUT 120
)

add_test(
    NAME runtime_multiplayer_snapshot_v15_smoke_test
    COMMAND concord_runtime --multiplayer-snapshot-v15-smoke-test
)

add_test(
    NAME runtime_multiplayer_runtime_binding_smoke_test
    COMMAND concord_runtime --multiplayer-runtime-binding-smoke-test
)

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/coopcampmd.ini"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/c1a01md.map"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/c4w01md.map")
    add_test(
        NAME cooperative_scenario_stock_data_test
        COMMAND concord_cooperative_scenario_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/coopcampmd.ini"
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd"
    )
    add_test(
        NAME runtime_cooperative_session_stock_data_test
        COMMAND concord_cooperative_session_system_smoke
            --stock
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/coopcampmd.ini"
            "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd"
    )
endif()

add_test(
    NAME runtime_vision_contract_test
    COMMAND concord_runtime --vision-smoke-test
)

add_test(
    NAME runtime_engineer_entry_smoke_test
    COMMAND concord_runtime --command-smoke-test
)
set_tests_properties(
    runtime_engineer_entry_smoke_test
    PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_ENGINEER_ONLY=1"
)

add_test(
    NAME runtime_veterancy_smoke_test
    COMMAND concord_runtime --veterancy-smoke-test
)

add_test(
    NAME runtime_weapon_projectile_fields_smoke_test
    COMMAND concord_runtime --weapon-projectile-fields-smoke-test
)

add_test(
    NAME runtime_naval_cloak_smoke_test
    COMMAND concord_runtime --naval-cloak-smoke-test
)

add_test(
    NAME runtime_crush_scatter_smoke_test
    COMMAND concord_runtime --crush-scatter-smoke-test
)

add_test(
    NAME runtime_repair_depot_integration_smoke_test
    COMMAND concord_runtime --repair-depot-integration-smoke-test
)

add_test(
    NAME runtime_aircraft_service_integration_smoke_test
    COMMAND concord_runtime --aircraft-service-integration-smoke-test
)

add_test(
    NAME runtime_gate_integration_smoke_test
    COMMAND concord_runtime --gate-integration-smoke-test
)

add_test(
    NAME runtime_subcell_integration_smoke_test
    COMMAND concord_runtime --subcell-integration-smoke-test
)

add_test(
    NAME runtime_spy_integration_smoke_test
    COMMAND concord_runtime --spy-integration-smoke-test
)

add_test(
    NAME runtime_superweapon_smoke_test
    COMMAND concord_runtime --superweapon-smoke-test
)

add_test(NAME superweapon_module_smoke_test COMMAND concord_superweapon_smoke)

add_test(
    NAME runtime_superweapon_one_shot_smoke_test
    COMMAND concord_runtime --superweapon-one-shot-smoke-test
)

foreach (special_mechanism_name IN ITEMS
    mind-control
    camouflage
    chrono-temporal
    electric
    radiation
    carrier)
    add_test(
        NAME runtime_special_mechanism_${special_mechanism_name}_smoke_test
        COMMAND concord_runtime --special-mechanisms-smoke-test
    )
    set_tests_properties(
        runtime_special_mechanism_${special_mechanism_name}_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_SPECIAL_MECHANISM=${special_mechanism_name}"
    )
endforeach()

foreach (special_mechanism_name IN ITEMS timed-bomb parasite)
    add_test(
        NAME runtime_special_mechanism_${special_mechanism_name}_smoke_test
        COMMAND concord_special_mechanisms_smoke
    )
    set_tests_properties(
        runtime_special_mechanism_${special_mechanism_name}_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_SPECIAL_MECHANISM=${special_mechanism_name}"
    )
endforeach()

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/neutral.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/cache.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll")
    add_test(
        NAME runtime_main_menu_ui_smoke_test
        COMMAND concord_runtime
            --main-menu-ui-smoke-test
            "${CMAKE_BINARY_DIR}/runtime_main_menu_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/mpyscrnl.shp")
    add_test(
        NAME runtime_skirmish_ui_smoke_test
        COMMAND concord_runtime
            --skirmish-ui-smoke-test
            "${CMAKE_BINARY_DIR}/runtime_skirmish_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )

    add_test(
        NAME runtime_loading_prewarm_smoke_test
        COMMAND concord_runtime
            --loading-smoke-test
            --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    add_test(
        NAME runtime_loading_first_frame_positions_smoke_test
        COMMAND concord_runtime --loading-smoke-test --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(runtime_loading_first_frame_positions_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_LOADING_PRESENTATION_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME runtime_loading_frame_stability_smoke_test
        COMMAND concord_runtime --loading-smoke-test --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(runtime_loading_frame_stability_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_LOADING_FRAME_STABILITY_ONLY=1;CONCORD_LOADING_FRAME_STABILITY_OUTPUT=${CMAKE_BINARY_DIR}/issue21_loading_frame_stability"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 300 RUN_SERIAL TRUE
    )
endif()

add_test(
    NAME performance_plan
    COMMAND concord_rules_dump
        --performance-plan
        --workers 4
)

function(add_runtime_theater_texture_smoke_test test_name map_path)
    if (WIN32 AND EXISTS "${map_path}")
        add_test(
            NAME ${test_name}
            COMMAND concord_runtime
                --render-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${map_path}"
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES ENVIRONMENT
                "CONCORD_RENDER_SMOKE_TERRAIN_COVERAGE=yes"
        )
    endif()
endfunction()

add_runtime_theater_texture_smoke_test(
    runtime_theater_temperate_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_snow_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xxmas.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_urban_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/hillbtwn.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_newurban_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_desert_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_lunar_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/sov06lmd.map"
)

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xmp06mw.map")
    add_test(
        NAME runtime_smudge_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xmp06mw.map"
    )
    set_tests_properties(
        runtime_smudge_render_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_SMUDGES=yes;CONCORD_RENDER_SMOKE_TERRAIN_COVERAGE=yes;CONCORD_RENDER_SMOKE_CELL=33,51;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_smudge_render.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
    add_test(
        NAME runtime_smoke_test
        COMMAND concord_runtime
            --smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_skirmish_build_dryheat_smoke_test
        COMMAND concord_runtime
            --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_computer_ai_smoke_test
        COMMAND concord_runtime
            --computer-ai-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_unholy_alliance_tech_tree_smoke_test
        COMMAND concord_runtime
            --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_unholy_alliance_tech_tree_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_SKIRMISH_UNHOLY_START_ONLY=1"
    )

    add_test(NAME runtime_skirmish_modes_technology_smoke_test
        COMMAND concord_runtime --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_skirmish_modes_technology_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_SKIRMISH_MODES_ONLY=1"
        TIMEOUT 300
    )

    add_test(NAME runtime_vehicle_arrival_integration_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_vehicle_arrival_integration_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_ARRIVAL_ONLY=1"
    )

    add_test(NAME runtime_building_ready_sidebar_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_building_ready_sidebar_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_READY_ONLY=1"
        TIMEOUT 180
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    add_test(
        NAME runtime_campaign_database_smoke_test
        COMMAND concord_runtime
            --campaign-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

add_test(
    NAME runtime_campaign_unsupported_coverage_must_fail
    COMMAND concord_runtime
        --campaign-coverage-map-test
        "${CMAKE_SOURCE_DIR}/tests/fixtures/campaign_unsupported_coverage.map"
)
set_tests_properties(
    runtime_campaign_unsupported_coverage_must_fail
    PROPERTIES WILL_FAIL TRUE
)
foreach(CAMPAIGN_COVERAGE_KIND IN ITEMS event action script)
    add_test(
        NAME runtime_campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}_must_fail
        COMMAND concord_runtime
            --campaign-coverage-map-test
            "${CMAKE_SOURCE_DIR}/tests/fixtures/campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}.map"
    )
    set_tests_properties(
        runtime_campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}_must_fail
        PROPERTIES WILL_FAIL TRUE
    )
endforeach()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/local.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/language/ra2.csf"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/Maps01.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/Maps02.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies01.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies02.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/movmd03.mix")
    add_test(
        NAME runtime_campaign_runtime_smoke_test
        COMMAND concord_runtime
            --campaign-runtime-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )

    add_test(
        NAME runtime_campaign_ui_smoke_test
        COMMAND concord_runtime
            --campaign-ui-smoke-test
            "${CMAKE_BINARY_DIR}/campaign_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )

    add_test(
        NAME runtime_campaign_ra2_yr_catalog_acceptance_test
        COMMAND concord_runtime
            --campaign-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
    set_tests_properties(
        runtime_campaign_ra2_yr_catalog_acceptance_test
        PROPERTIES PASS_REGULAR_EXPRESSION
            "campaigns=5 missions=40 available=40"
    )
endif()

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/obs_alli.pcx"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/expandmd01/mpyscrnlbar10.pcx")
    add_test(
        NAME runtime_internet_menu_skin_smoke_test
        COMMAND concord_runtime
            --internet-menu-skin-smoke-test
            "${CMAKE_BINARY_DIR}/internet_menu_skin_allied.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    add_test(
        NAME runtime_resource_field_2peaks_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(
        runtime_resource_field_2peaks_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_RESOURCE_FIELD=yes;CONCORD_RENDER_SMOKE_CELL=75,64;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_resource_field_2peaks.bmp"
    )

    add_test(
        NAME runtime_bridge_repair_2peaks_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(
        runtime_bridge_repair_2peaks_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_BRIDGE_REPAIR=yes;CONCORD_COMMAND_SMOKE_BRIDGE_SAVE=${CMAKE_BINARY_DIR}/runtime_bridge_repair_2peaks_save.ini;CONCORD_COMMAND_SMOKE_BRIDGE_BMP=${CMAKE_BINARY_DIR}/runtime_bridge_repair_2peaks.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xshrapnel.map")
    add_test(
        NAME runtime_low_bridge_repair_xshrapnel_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xshrapnel.map"
    )
    set_tests_properties(
        runtime_low_bridge_repair_xshrapnel_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_LOW_BRIDGE_REPAIR=yes"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/bridgegap.map")
    add_test(
        NAME runtime_high_bridge_bridgegap_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/bridgegap.map"
    )
    set_tests_properties(
        runtime_high_bridge_bridgegap_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_BRIDGE_VISUAL=yes;CONCORD_RENDER_SMOKE_BRIDGE_KIND=high;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_high_bridge_bridgegap.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all04dmd.map")
    add_test(
        NAME runtime_low_bridge_all04dmd_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all04dmd.map"
    )
    set_tests_properties(
        runtime_low_bridge_all04dmd_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_BRIDGE_VISUAL=yes;CONCORD_RENDER_SMOKE_BRIDGE_KIND=low;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_low_bridge_all04dmd.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    add_test(
        NAME runtime_world_all01umd_smoke_test
        COMMAND concord_runtime
            --smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_render_all01umd_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_special_mechanism_golden_smoke_test
        COMMAND concord_runtime
            --mechanism-golden-smoke-test
                "${CMAKE_BINARY_DIR}/special_mechanism_golden"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_defense_turret_directional_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_defense_turret_directional_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GTGCAN@0,GTGCAN@32,GTGCAN@64,GTGCAN@96,GTGCAN@128,GTGCAN@160,GTGCAN@192,GTGCAN@224,NALASR@0,NALASR@32,NALASR@64,NALASR@96,NALASR@128,NALASR@160,NALASR@192,NALASR@224;CONCORD_RENDER_SMOKE_COLUMNS=4;CONCORD_RENDER_SMOKE_SPACING_X=3;CONCORD_RENDER_SMOKE_SPACING_Y=3;CONCORD_RENDER_SMOKE_EXPECT_STRUCTURE_TURRETS=16;CONCORD_RENDER_SMOKE_EXPECT_STRUCTURE_TURRET_CUBOIDS=16;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_POINTS=0;CONCORD_RENDER_SMOKE_MIN_STRUCTURE_TURRET_WIDTH=64;CONCORD_RENDER_SMOKE_MIN_STRUCTURE_TURRET_HEIGHT=78;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_WIDTH=68;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_HEIGHT=82;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_defense_turret_directions.bmp"
    )

    add_test(
        NAME runtime_building_owner_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_building_owner_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GAGAP@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_BUILDING_REMAP_RULES_INI=${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini;CONCORD_BUILDING_REMAP_AUDIT_CSV=${CMAKE_BINARY_DIR}/runtime_building_owner_remap.csv;CONCORD_BUILDING_REMAP_PREVIEW_BMP=${CMAKE_BINARY_DIR}/runtime_gap_generator_states.bmp"
    )

    add_test(
        NAME runtime_tesla_yellow_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_tesla_yellow_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=TESLA@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Yellow;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_tesla_yellow_remap.bmp"
    )

    add_test(
        NAME runtime_battle_bunker_yellow_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_battle_bunker_yellow_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=NABNKR@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_EXPECT_REMAP_PIXELS=793;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_battle_bunker_yellow_remap.bmp"
    )

    add_test(
        NAME runtime_kirov_gold_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_kirov_gold_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=ZEP@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_kirov_gold_remap.bmp"
    )

    add_test(
        NAME runtime_aircraft_building_clearance_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_aircraft_building_clearance_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=CACHIG05@64,ZEP@64;CONCORD_RENDER_SMOKE_COLUMNS=1;CONCORD_RENDER_SMOKE_SPACING_X=0;CONCORD_RENDER_SMOKE_SPACING_Y=0;CONCORD_RENDER_SMOKE_AIRBORNE=yes;CONCORD_RENDER_SMOKE_FLIGHT_LEVEL_OFFSET=4;CONCORD_RENDER_SMOKE_EXPECT_AIRBORNE_FOREGROUND=1;CONCORD_RENDER_SMOKE_EXPECT_BUILDING_CLEARANCE=1;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_aircraft_building_clearance.bmp"
    )

    add_test(
        NAME runtime_docked_aircraft_foreground_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_docked_aircraft_foreground_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GAAIRC@64,ORCA@64,ORCA@64,GALITE@64,ORCA@64,ORCA@64;CONCORD_RENDER_SMOKE_COLUMNS=3;CONCORD_RENDER_SMOKE_SPACING_X=1;CONCORD_RENDER_SMOKE_SPACING_Y=1;CONCORD_RENDER_SMOKE_DOCKED_TO_STRUCTURE=yes;CONCORD_RENDER_SMOKE_EXPECT_DOCKED_AIRCRAFT_FOREGROUND=4;CONCORD_RENDER_SMOKE_EXPECT_VOXELS=4;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_docked_aircraft_foreground.bmp"
    )

    add_test(
        NAME runtime_interaction_all01umd_smoke_test
        COMMAND concord_runtime
            --interaction-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_fullscreen_render_smoke_test
        COMMAND concord_runtime
            --interaction-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_fullscreen_render_smoke_test
        PROPERTIES
            ENVIRONMENT "CONCORD_FULLSCREEN_RENDER_BENCHMARK=1;CONCORD_RENDER_DETAIL_STATS=0"
            TIMEOUT 300
    )

    add_test(
        NAME runtime_live_frame_all01umd_smoke_test
        COMMAND concord_runtime
            --config "${CMAKE_SOURCE_DIR}/tests/fixtures/runtime_live_performance.ini"
            --skip-main-menu
            --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_live_frame_all01umd_smoke_test
        PROPERTIES
            ENVIRONMENT "CONCORD_LIVE_BENCHMARK=1;CONCORD_LIVE_BENCHMARK_TICKS=600;CONCORD_RENDER_DETAIL_STATS=0"
            TIMEOUT 300
    )

    add_test(
        NAME runtime_result_presentation_smoke_test
        COMMAND concord_runtime
            --result-presentation-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_command_all01umd_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    add_test(
        NAME runtime_factory_exit_clearance_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_factory_exit_clearance_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FACTORY_EXIT_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_air_navigation_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_air_navigation_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_NAVIGATION_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_harvester_stability_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    add_test(
        NAME runtime_desolator_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_desolator_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_DESOLATOR_ONLY=1;CONCORD_DESOLATOR_BMP=${CMAKE_BINARY_DIR}/desolator_radiation.bmp"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_vehicle_damage_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_vehicle_damage_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_DAMAGE_ONLY=1;CONCORD_VEHICLE_DAMAGE_SMOKE_BMP=${CMAKE_BINARY_DIR}/vehicle_damage_smoke.bmp"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_squid_grapple_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_squid_grapple_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SQUID_GRAPPLE_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_mastermind_overload_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mastermind_overload_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MASTERMIND_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_floating_disk_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_floating_disk_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FLOATING_DISK_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_disk_drain_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_disk_drain_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_DISK_DRAIN_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_guard_rotation_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_guard_rotation_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_GUARD_ROTATION_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_ai_building_targets_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_ai_building_targets_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AI_BUILDING_TARGETS_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_type_immune_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_type_immune_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TYPE_IMMUNE_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_terrain_movement_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_terrain_movement_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TERRAIN_MOVEMENT_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_move_to_shroud_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_move_to_shroud_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MOVE_TO_SHROUD_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_fire_in_transport_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_fire_in_transport_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FIRE_IN_TRANSPORT_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_air_range_bonus_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_air_range_bonus_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_RANGE_BONUS_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_slave_miner_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_slave_miner_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SLAVE_MINER_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_slave_miner_lifecycle_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_slave_miner_lifecycle_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SLAVE_MINER_LIFECYCLE_ONLY=1"
        TIMEOUT 600
    )
    add_test(
        NAME runtime_mcv_survival_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mcv_survival_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MCV_SURVIVAL_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_mcv_repack_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mcv_repack_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MCV_REPACK_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_virus_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_virus_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VIRUS_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_virus_friendly_gas_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_virus_friendly_gas_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VIRUS_FRIENDLY_GAS_ONLY=1"
        TIMEOUT 240
    )
    add_test(
        NAME runtime_cloning_batch_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_cloning_batch_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_CLONING_BATCH_ONLY=1"
        TIMEOUT 600
    )
    set_tests_properties(
        runtime_harvester_stability_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_HARVESTER_STABILITY_ONLY=1"
    )
    add_test(
        NAME runtime_neutral_garrison_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_neutral_garrison_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_NEUTRAL_GARRISON_ONLY=1;CONCORD_GARRISON_PIPS_BMP=${CMAKE_BINARY_DIR}/garrison_capacity_pips.bmp"
    )
    add_test(
        NAME runtime_garrison_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_garrison_render_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_GARRISON_RENDER_TEST=1;CONCORD_GARRISON_RENDER_PREVIEW_BMP=${CMAKE_BINARY_DIR}/runtime_garrison_states.bmp"
    )
    add_test(
        NAME runtime_amphibious_transport_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_amphibious_transport_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AMPHIBIOUS_TRANSPORT_ONLY=1"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_tank_bunker_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_tank_bunker_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TANK_BUNKER_ONLY=1;CONCORD_TANK_BUNKER_OUTPUT=${CMAKE_BINARY_DIR}/tank_bunker"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_bioreactor_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_bioreactor_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_BIOREACTOR_ONLY=1;CONCORD_BIOREACTOR_OUTPUT=${CMAKE_BINARY_DIR}/bioreactor"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_destroyer_aircraft_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_destroyer_aircraft_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_DESTROYER_ONLY=1"
    )
    foreach(silo_theater IN ITEMS temperate urban)
        if(silo_theater STREQUAL "temperate")
            set(silo_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
        else()
            set(silo_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        endif()
        add_test(NAME runtime_nuclear_silo_${silo_theater}_smoke_test
            COMMAND concord_runtime --command-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${silo_map}")
        set_tests_properties(runtime_nuclear_silo_${silo_theater}_smoke_test PROPERTIES
            ENVIRONMENT "CONCORD_COMMAND_SMOKE_NUCLEAR_SILO_ONLY=1;CONCORD_NUCLEAR_SILO_OUTPUT=${CMAKE_BINARY_DIR}/nuclear_silo/${silo_theater}"
            RUN_SERIAL TRUE)
    endforeach()
    foreach(crate_theater IN ITEMS temperate urban)
        if(crate_theater STREQUAL "temperate")
            set(crate_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
        else()
            set(crate_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        endif()
        add_test(NAME runtime_crates_${crate_theater}_smoke_test
            COMMAND concord_runtime --command-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${crate_map}")
        set_tests_properties(runtime_crates_${crate_theater}_smoke_test PROPERTIES
            ENVIRONMENT "CONCORD_COMMAND_SMOKE_CRATES_ONLY=1;CONCORD_CRATE_OUTPUT=${CMAKE_BINARY_DIR}/crates/${crate_theater}"
            RUN_SERIAL TRUE)
    endforeach()
    add_test(
        NAME runtime_vehicle_body_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_vehicle_body_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_BODY_ONLY=1;CONCORD_VEHICLE_BODY_OUTPUT=${CMAKE_BINARY_DIR}/vehicle_dynamics"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_support_powers_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_support_powers_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_SUPPORT_POWERS_OUTPUT=${CMAKE_BINARY_DIR}/support_powers"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_missile_volley_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map"
    )
    set_tests_properties(runtime_missile_volley_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILE_VOLLEY_ONLY=1;CONCORD_MISSILE_VOLLEY_OUTPUT=${CMAKE_BINARY_DIR}/missile_volley"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_missile_flight_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_missile_flight_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILE_FLIGHT_ONLY=1;CONCORD_MISSILE_FLIGHT_OUTPUT=${CMAKE_BINARY_DIR}/missile_flight"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_spawned_missile_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_spawned_missile_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILES_ONLY=1"
    )
    add_test(
        NAME runtime_projectile_muzzle_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_projectile_muzzle_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MUZZLE_ONLY=1"
    )
    add_test(
        NAME runtime_electric_and_bomb_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_electric_and_bomb_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_ELECTRIC_ATTACK_ONLY=1"
    )
    set_tests_properties(
        runtime_render_all01umd_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_EXPECT_DAMAGE_FIRE_DRAWN=1;CONCORD_RENDER_SMOKE_MAX_DAMAGE_FIRE_MISSING=0"
    )

    add_test(
        NAME runtime_ifv_multifunction_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_ifv_multifunction_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_IFV_ONLY=yes;CONCORD_COMMAND_SMOKE_IFV_SAVE=${CMAKE_BINARY_DIR}/runtime_ifv_multifunction_save.ini;CONCORD_RENDER_USE_JAVA_VEHICLE_PNG=no"
    )

    add_test(
        NAME runtime_aircraft_return_to_dock_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_aircraft_return_to_dock_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_AIRCRAFT_RETURN_ONLY=yes"
    )

    add_test(
        NAME runtime_build_all01umd_smoke_test
        COMMAND concord_runtime
            --build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_trigger_all01umd_smoke_test
        COMMAND concord_runtime
            --trigger-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd.mix")
    add_test(
        NAME encrypted_mix_localmd_extract_ltnk
        COMMAND concord_rules_dump
            --mix "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd.mix"
            --extract-mix-file ltnk.vxl
            --output "${CMAKE_BINARY_DIR}/encrypted_mix_ltnk_probe.vxl"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini")
    add_test(
        NAME combat_sandbox_mtnk_htnk
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 600
            --distance 8
    )

    add_test(
        NAME combat_sandbox_e1_e2
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker E1
            --defender E2
            --ticks 300
            --distance 8
    )

    add_test(
        NAME combat_sandbox_sref_htnk
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker SREF
            --defender HTNK
            --ticks 600
            --distance 8
    )

    add_test(
        NAME economy_sandbox_allied
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --economy-sandbox
            --house Americans
            --mcv AMCV
            --credits 10000
            --ticks 600
    )

    add_test(
        NAME ai_trigger_sandbox_synthetic
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --ai-trigger-sandbox
            --ai-house Russians
            --enemy-house Americans
            --mcv SMCV
            --credits 10000
            --ticks 900
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        add_test(
            NAME ai_trigger_sandbox_all01umd
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
                --ai-trigger-sandbox
                --ai-house Russians
                --enemy-house Americans
                --mcv SMCV
                --credits 10000
                --ticks 900
        )
    endif()

    add_test(
        NAME mod_compat_report_fixture
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat_rules.ini"
            --art "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat_art.ini"
            --map "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat.map"
            --mod-compat-report
    )

    add_test(
        NAME mod_compat_build_limit_vanilla
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/tests/fixtures/build_limit_rules.ini"
            --mod-compat-report
    )
    set_tests_properties(
        mod_compat_build_limit_vanilla
        PROPERTIES
            PASS_REGULAR_EXPRESSION "Ares hits: 0"
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/artmd.ini" AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
        add_test(
            NAME engine_status_report
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --art "${CMAKE_SOURCE_DIR}/local_data/artmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
                --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
                --engine-status
        )
    endif()

    add_test(
        NAME tech_tree_report_allied
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --tech-tree-report
            --house Americans
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        add_test(
            NAME victory_sandbox_all01umd
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
                --victory-sandbox
                --house Americans
        )
    endif()

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
        add_test(
            NAME pathfinding_sandbox_dryheat
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
                --pathfinding-sandbox
        )
    endif()

    add_test(
        NAME audio_catalog_report
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
            --audio-report
    )

    add_test(
        NAME audio_decode_smoke
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
            --output "${CMAKE_BINARY_DIR}/audio_decode_smoke_cache"
            --audio-decode-smoke
    )

    add_test(
        NAME replay_sandbox_lockstep
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --replay-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 240
            --distance 8
            --seed 12345
    )

    add_test(
        NAME replay_sandbox_desync_detection
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --replay-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 120
            --distance 8
            --seed 12345
            --desync-at 40
    )
endif()

# Lobby map downloads share the same transport in LAN and Internet. Attach
# the implementation to every executable/static core that owns that layer.
get_property(_map_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_map_target IN LISTS _map_targets)
    get_target_property(_map_sources ${_map_target} SOURCES)
    if("src/RuntimeInternetCommunity.cpp" IN_LIST _map_sources OR
       "src/RuntimeInternetMultiplayerMenu.cpp" IN_LIST _map_sources)
        target_sources(${_map_target} PRIVATE src/RuntimeInternetRankedMatch.cpp)
        if(MSVC)
            target_compile_options(${_map_target} PRIVATE /utf-8)
        endif()
    endif()
    if("src/RuntimeInternetCommunity.cpp" IN_LIST _map_sources)
        target_sources(${_map_target} PRIVATE src/RuntimeInternetCommunityState.cpp)
        if(NOT "src/RuntimeAtomicFile.cpp" IN_LIST _map_sources)
            target_sources(${_map_target} PRIVATE src/RuntimeAtomicFile.cpp)
        endif()
        if(MSVC)
            target_compile_options(${_map_target} PRIVATE /wd4702)
        endif()
        get_target_property(_map_sources ${_map_target} SOURCES)
    endif()
    if("src/RuntimeMultiplayerProtocol.cpp" IN_LIST _map_sources OR
       "src/RuntimeMultiplayerMenu.cpp" IN_LIST _map_sources)
        foreach(_map_source src/RuntimeMultiplayerMapTransfer.cpp
                src/RuntimeMultiplayerCompatibilityFingerprint.cpp
                src/RuntimeMultiplayerSnapshotTransfer.cpp)
            if(NOT "${_map_source}" IN_LIST _map_sources)
                target_sources(${_map_target} PRIVATE ${_map_source})
            endif()
        endforeach()
    endif()
    if("src/RuntimeMultiplayerLobbyFlow.cpp" IN_LIST _map_sources OR
       "src/RuntimeMultiplayerMenu.cpp" IN_LIST _map_sources)
        foreach(_map_source src/RuntimeMultiplayerMapCache.cpp src/RuntimeAtomicFile.cpp)
            if(NOT "${_map_source}" IN_LIST _map_sources)
                target_sources(${_map_target} PRIVATE ${_map_source})
            endif()
        endforeach()
        if(MSVC)
            target_compile_options(${_map_target} PRIVATE /wd4702)
        endif()
    endif()
endforeach()

add_test(NAME runtime_no_cooldown_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_no_cooldown_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_NO_COOLDOWN_ONLY=1")
add_test(NAME runtime_support_no_cooldown_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
set_tests_properties(runtime_support_no_cooldown_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_COMMAND_SMOKE_SUPPORT_NO_COOLDOWN_ONLY=1")

add_test(NAME runtime_projectile_speed_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_projectile_speed_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_PROJECTILE_SPEED_ONLY=1;CONCORD_PROJECTILE_SPEED_OUTPUT=${CMAKE_BINARY_DIR}/smoke/feature-set-17/01-projectile-speed/checks")

add_test(NAME runtime_building_timed_bomb_policy_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_building_timed_bomb_policy_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_TIMED_BOMB_POLICY_ONLY=1" TIMEOUT 2400)

add_test(NAME runtime_demo_truck_friendly_damage_smoke_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_demo_truck_friendly_damage_smoke_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_DEMO_TRUCK_FRIENDLY_DAMAGE_ONLY=1" TIMEOUT 300)

add_test(NAME runtime_reactor_immediate_explosion_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_reactor_immediate_explosion_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_REACTOR_EXPLOSION_ONLY=1")

add_test(NAME runtime_radiation_death_anchor_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/tests/fixtures/aircraft_motion_flat_lake.map")
set_tests_properties(runtime_radiation_death_anchor_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_RADIATION_DEATH_ANCHOR_ONLY=1;CONCORD_RADIATION_DEATH_ANCHOR_OUTPUT=${CMAKE_BINARY_DIR}/issue21_radiation_death_anchor"
    TIMEOUT 900 RUN_SERIAL TRUE)

add_test(NAME runtime_radiation_death_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_radiation_death_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_RADIATION_DEATH_ONLY=1")

add_test(NAME runtime_force_shield_ground_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_force_shield_ground_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_COMMAND_SMOKE_FORCE_SHIELD_ONLY=1")

add_test(NAME runtime_support_target_pan_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map")
set_tests_properties(runtime_support_target_pan_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_COMMAND_SMOKE_SUPPORT_TARGET_PAN_ONLY=1")
