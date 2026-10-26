if(CONCORD_PORTABLE_CORE)
    include("${CMAKE_CURRENT_LIST_DIR}/CoreTargets.cmake")
    return()
endif()

add_executable(concord_map_data_smoke
    # W027 same production component, explicit ABI-profile library.
    tests/maps/MapDataSmoke.cpp
)
target_include_directories(concord_map_data_smoke PRIVATE src)

add_executable(concord_game_world_deterministic_hash_smoke
    # W027 same production component, explicit ABI-profile library.
    tests/infrastructure/GameWorldDeterministicHashSmoke.cpp
)
target_include_directories(
    concord_game_world_deterministic_hash_smoke PRIVATE src
)

add_executable(concord_runtime_object_index_smoke tests/infrastructure/RuntimeObjectIndexSmoke.cpp)
target_include_directories(concord_runtime_object_index_smoke PRIVATE src)
add_executable(concord_path_request_queue_performance_smoke
    tests/pathfinding/PathRequestQueuePerformanceSmoke.cpp
    src/simulation/navigation/pathfinding/PathRequestQueue.cpp
    src/world/model/GameWorld.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/rules/RulesDatabase.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(concord_path_request_queue_performance_smoke PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_runtime_object_index_smoke PRIVATE /utf-8 /W4 /WX /permissive-)
    # Uses the same legacy map/rules sources as concord_map_data_smoke.
    concord_target_compile_options(concord_path_request_queue_performance_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_runtime_map_package_smoke
    tests/maps/RuntimeMapPackageSmoke.cpp
    src/data/maps/RuntimeMapPackage.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/core/io/RuntimeAtomicFile.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(concord_runtime_map_package_smoke PRIVATE src)

add_executable(concord_csf_codec_contract
    tests/assets/CsfCodecContract.cpp
)
target_include_directories(concord_csf_codec_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_csf_codec_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

add_executable(concord_pal_decoder_contract
    tests/infrastructure/PalDecoderContract.cpp
    src/assets/codecs/PalDecoder.cpp
)
target_include_directories(concord_pal_decoder_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_pal_decoder_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

add_executable(concord_map_preview_resource_context_contract
    tests/infrastructure/MapPreviewResourceContextContract.cpp
    src/assets/preview/MapPreviewResourceContext.cpp
)
target_include_directories(concord_map_preview_resource_context_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_map_preview_resource_context_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

add_executable(concord_runtime_match_score_contract
    tests/infrastructure/RuntimeMatchScoreContract.cpp
    src/core/results/RuntimeMatchScore.cpp
)
target_include_directories(concord_runtime_match_score_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_runtime_match_score_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

# Stable leaf library profiles; all consumers above exist before this include.
include(cmake/LeafModuleLibraries.cmake)
include(cmake/ReplaySnapshotContracts.cmake)
include(cmake/PlayerDefeatNoticeContracts.cmake)
include(cmake/InputPixelValuesContracts.cmake)
include(cmake/UiClockContracts.cmake)

add_executable(concord_campaign_progress_smoke
    tests/campaign/CampaignProgressSmoke.cpp
    src/data/campaign/CampaignProgress.cpp
    src/core/io/IniFile.cpp
    src/core/io/RuntimeAtomicFile.cpp
)
target_include_directories(concord_campaign_progress_smoke PRIVATE src)

add_executable(concord_player_slot_role_contract tests/infrastructure/PlayerSlotRoleContract.cpp)
target_include_directories(concord_player_slot_role_contract PRIVATE src)
add_executable(concord_runtime_observer_contract tests/infrastructure/RuntimeObserverContract.cpp)
target_include_directories(concord_runtime_observer_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_player_slot_role_contract PRIVATE /utf-8 /W4 /WX /permissive-)
    concord_target_compile_options(concord_runtime_observer_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

add_executable(concord_campaign_map_selection_smoke
    tests/campaign/CampaignMapSelectionSmoke.cpp
    src/data/campaign/CampaignDatabase.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/runtime/campaign/RuntimeCampaignMovieResolver.cpp
    src/assets/audio/AudioDecoder.cpp
    src/assets/audio/AudioSampleResolver.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(concord_campaign_map_selection_smoke PRIVATE src)

add_executable(concord_runtime_campaign_data_source_smoke
    tests/campaign/RuntimeCampaignDataSourceSmoke.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(
    concord_runtime_campaign_data_source_smoke PRIVATE src
)

add_executable(concord_runtime_campaign_movie_resolver_smoke
    tests/campaign/RuntimeCampaignMovieResolverSmoke.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/runtime/campaign/RuntimeCampaignMovieResolver.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(
    concord_runtime_campaign_movie_resolver_smoke PRIVATE src
)

add_executable(concord_campaign_presentation_decoder_smoke
    tests/campaign/CampaignPresentationDecoderSmoke.cpp
    src/data/campaign/CampaignPresentationDecoder.cpp
)
target_include_directories(
    concord_campaign_presentation_decoder_smoke PRIVATE src
)

add_executable(concord_rules_special_features_smoke
    tests/rules/RulesSpecialFeaturesSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_special_features_smoke PRIVATE src)
add_executable(concord_rules_poison_policy_smoke
    src/data/rules/diagnostics/RulesPoisonPolicySmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_poison_policy_smoke PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_rules_poison_policy_smoke PRIVATE /utf-8 /FS)
endif()
add_executable(concord_mod_extensions_smoke
    tests/gameplay/RuntimeModExtensionsSmoke.cpp src/core/io/IniFile.cpp src/data/rules/RulesDatabase.cpp src/analysis/reports/ModCompatibilityScanner.cpp)
target_include_directories(concord_mod_extensions_smoke PRIVATE src)
add_executable(concord_screen_shake_smoke
    tests/rendering/RuntimeScreenShakeSmoke.cpp src/core/io/IniFile.cpp src/data/rules/RulesDatabase.cpp)
target_include_directories(concord_screen_shake_smoke PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_mod_extensions_smoke PRIVATE /utf-8 /FS)
    concord_target_compile_options(concord_screen_shake_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_rules_pad_aircraft_smoke
    tests/rules/RulesPadAircraftSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_pad_aircraft_smoke PRIVATE src)

add_executable(concord_rules_production_smoke
    tests/rules/RulesProductionRulesSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_production_smoke PRIVATE src)

add_executable(concord_rules_lookup_dispatch_smoke
    tests/rules/RulesLookupDispatchSmoke.cpp src/core/io/IniFile.cpp src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_lookup_dispatch_smoke PRIVATE src)

add_executable(concord_rules_overlay_identity_smoke
    tests/rules/RulesOverlayIdentitySmoke.cpp src/core/io/IniFile.cpp src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_overlay_identity_smoke PRIVATE src)

add_executable(concord_rules_crate_smoke
    tests/rules/RulesCrateSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_rules_crate_smoke PRIVATE src)

add_executable(concord_art_firing_sequence_smoke
    tests/rendering/ArtFiringSequenceSmoke.cpp
    src/data/art/ArtDatabase.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
)
target_include_directories(concord_art_firing_sequence_smoke PRIVATE src)
add_test(NAME art_firing_sequence_smoke_test
    COMMAND concord_art_firing_sequence_smoke "${CMAKE_SOURCE_DIR}/local_data/artmd.ini")
add_executable(concord_particle_system_smoke
    tests/gameplay/RuntimeParticleSystemSmoke.cpp
    src/data/art/ArtDatabase.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/effects/RuntimeParticleSystem.cpp
)
target_include_directories(concord_particle_system_smoke PRIVATE src)

add_executable(concord_garrison_system_smoke
    tests/gameplay/RuntimeGarrisonSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeGarrisonSystem.cpp
)
target_include_directories(concord_garrison_system_smoke PRIVATE src)

add_executable(concord_subcell_system_smoke
    tests/gameplay/RuntimeSubcellSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/simulation/navigation/occupancy/RuntimeSubcellSystem.cpp
)
target_include_directories(concord_subcell_system_smoke PRIVATE src)

add_executable(concord_subcell_selective_batch_smoke
    tests/gameplay/RuntimeSubcellSelectiveBatchSmoke.cpp
    src/simulation/navigation/occupancy/RuntimeSubcellSystem.cpp
)
target_include_directories(concord_subcell_selective_batch_smoke PRIVATE src)

add_executable(concord_repair_depot_system_smoke
    tests/gameplay/RuntimeRepairDepotSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeRepairDepotSystem.cpp
)
target_include_directories(concord_repair_depot_system_smoke PRIVATE src)

add_executable(concord_factory_delivery_state_smoke
    tests/gameplay/RuntimeFactoryDeliveryStateSmoke.cpp
    src/core/io/IniFile.cpp
    src/simulation/production/RuntimeFactoryDeliveryState.cpp
)
target_include_directories(concord_factory_delivery_state_smoke PRIVATE src)

add_executable(concord_grinding_system_smoke
    tests/gameplay/RuntimeGrindingSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeGrindingSystem.cpp
)
target_include_directories(concord_grinding_system_smoke PRIVATE src)

add_executable(concord_aircraft_service_system_smoke
    tests/gameplay/RuntimeAircraftServiceSystemSmoke.cpp
    src/world/model/GameWorld.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/ai/RuntimeAircraftServiceSystem.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(concord_aircraft_service_system_smoke PRIVATE src)

add_executable(concord_aircraft_production_matcher_smoke
    tests/gameplay/RuntimeAircraftProductionMatcherSmoke.cpp
    src/simulation/ai/RuntimeAircraftProductionMatcher.cpp
)
target_include_directories(
    concord_aircraft_production_matcher_smoke PRIVATE src
)

add_executable(concord_gate_system_smoke
    tests/gameplay/RuntimeGateSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeGateSystem.cpp
)
target_include_directories(concord_gate_system_smoke PRIVATE src)

add_executable(concord_world_audio_system_smoke
    tests/audio/RuntimeWorldAudioSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/audio/world/RuntimeWorldAudioSystem.cpp
)
target_include_directories(concord_world_audio_system_smoke PRIVATE src)

add_executable(concord_audio_sample_resolver_edition_smoke
    tests/audio/AudioSampleResolverEditionSmoke.cpp
    src/assets/audio/AudioDecoder.cpp
    src/assets/audio/AudioSampleResolver.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(
    concord_audio_sample_resolver_edition_smoke PRIVATE src
)

add_executable(concord_audio_decoder_smoke
    tests/audio/AudioDecoderSmoke.cpp
    src/assets/audio/AudioDecoder.cpp src/assets/audio/AudioSampleResolver.cpp src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(concord_audio_decoder_smoke PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_audio_decoder_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_projectile_line_trail_smoke tests/rendering/ProjectileLineTrailSmoke.cpp)
target_include_directories(concord_projectile_line_trail_smoke PRIVATE src)

add_executable(concord_power_audio_feedback_smoke
    tests/audio/RuntimePowerAudioFeedbackSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/audio/events/RuntimePowerAudioFeedback.cpp
)
target_include_directories(concord_power_audio_feedback_smoke PRIVATE src)

add_executable(concord_lighting_system_smoke
    tests/rendering/RuntimeLightingSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/render/lighting/RuntimeLightingSystem.cpp
)
target_include_directories(concord_lighting_system_smoke PRIVATE src)

add_executable(concord_production_lanes_smoke
    tests/gameplay/RuntimeProductionLanesSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/production/RuntimeProductionLanes.cpp
)
target_include_directories(concord_production_lanes_smoke PRIVATE src)

add_executable(concord_production_timing_smoke
    tests/gameplay/RuntimeProductionTimingSmoke.cpp
    src/simulation/production/RuntimeProductionTiming.cpp
)
target_include_directories(concord_production_timing_smoke PRIVATE src)

add_executable(concord_runtime_atomic_file_smoke
    tests/infrastructure/RuntimeAtomicFileSmoke.cpp
    src/core/io/RuntimeAtomicFile.cpp
)
target_include_directories(concord_runtime_atomic_file_smoke PRIVATE src)

add_executable(concord_spy_system_smoke
    tests/gameplay/RuntimeSpySystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/abilities/psionic/RuntimeSpySystem.cpp
)
target_include_directories(concord_spy_system_smoke PRIVATE src)

add_executable(concord_airstrike_system_smoke
    tests/gameplay/RuntimeAirstrikeSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/simulation/ai/RuntimeAirstrikeSystem.cpp
)
target_include_directories(concord_airstrike_system_smoke PRIVATE src)

add_executable(concord_death_weapon_system_smoke
    tests/gameplay/RuntimeDeathWeaponSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/combat/damage/RuntimeDeathWeaponSystem.cpp
)
target_include_directories(concord_death_weapon_system_smoke PRIVATE src)

add_executable(concord_mag_beam_system_smoke
    tests/gameplay/RuntimeMagBeamSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/simulation/abilities/psionic/RuntimeMagBeamSystem.cpp
)
target_include_directories(concord_mag_beam_system_smoke PRIVATE src)

add_executable(concord_psychic_status_system_smoke
    tests/gameplay/RuntimePsychicStatusSystemSmoke.cpp
    src/core/io/IniFile.cpp
    src/simulation/abilities/psionic/RuntimePsychicStatusSystem.cpp
)
target_include_directories(concord_psychic_status_system_smoke PRIVATE src)

add_executable(concord_special_mechanisms_smoke
    tests/gameplay/RuntimeSpecialMechanismsSmoke.cpp
    src/core/io/IniFile.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/abilities/unit_special/RuntimeSpecialMechanisms.cpp
)
target_include_directories(concord_special_mechanisms_smoke PRIVATE src)

add_executable(concord_special_building_system_smoke
    tests/gameplay/RuntimeSpecialBuildingSystemSmoke.cpp
    src/world/model/GameWorld.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeSpecialBuildingSystem.cpp
    src/simulation/vision/RuntimeVision.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(concord_special_building_system_smoke PRIVATE src)

add_executable(concord_hospital_armory_mod_compat_smoke
    tests/gameplay/RuntimeHospitalArmoryModCompatSmoke.cpp
    src/world/model/GameWorld.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/rules/RulesDatabase.cpp
    src/simulation/buildings/RuntimeInfantryServiceSystem.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(
    concord_hospital_armory_mod_compat_smoke PRIVATE src
)

add_executable(concord_cooperative_scenario_smoke
    tests/multiplayer/CooperativeScenarioDatabaseSmoke.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(concord_cooperative_scenario_smoke PRIVATE src)

add_executable(concord_cooperative_session_system_smoke
    tests/multiplayer/RuntimeCooperativeSessionSystemSmoke.cpp
    src/network/cooperative/RuntimeCooperativeSessionSystem.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/art/TheaterDatabase.cpp
)
target_include_directories(
    concord_cooperative_session_system_smoke PRIVATE src
)

add_executable(concord_multiplayer_session_system_smoke
    tests/multiplayer/RuntimeMultiplayerSessionSystemSmoke.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_session_system_smoke PRIVATE src
)

add_executable(concord_lan_transport_smoke
    tests/multiplayer/RuntimeLanTransportSmoke.cpp
    # Same production provider, explicit S11 Contract dependency below.
)
target_include_directories(concord_lan_transport_smoke PRIVATE src)

add_executable(concord_multiplayer_protocol_smoke
    tests/multiplayer/RuntimeMultiplayerProtocolSmoke.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(concord_multiplayer_protocol_smoke PRIVATE src)

add_executable(concord_multiplayer_gameplay_command_smoke
    # W027 same production component, explicit ABI-profile library.
    tests/multiplayer/RuntimeMultiplayerGameplayCommandSmoke.cpp
)
target_include_directories(
    concord_multiplayer_gameplay_command_smoke PRIVATE src
)

add_executable(concord_multiplayer_lockstep_smoke
    # W027 same production component, explicit ABI-profile library.
    tests/multiplayer/RuntimeMultiplayerLockstepSmoke.cpp
    # H073 explicit Contract SnapshotTransfer dependency below.
)
target_include_directories(
    concord_multiplayer_lockstep_smoke PRIVATE src
)

add_executable(concord_multiplayer_weak_network_smoke
    tests/multiplayer/RuntimeMultiplayerWeakNetworkSmoke.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(
    concord_multiplayer_weak_network_smoke PRIVATE src
)

add_executable(concord_multiplayer_lan_flow_smoke
    tests/multiplayer/RuntimeMultiplayerLanFlowSmoke.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(concord_multiplayer_lan_flow_smoke PRIVATE src)

add_executable(concord_multiplayer_match_setup_smoke
    # W027 same production component, explicit ABI-profile library.
    tests/multiplayer/RuntimeMultiplayerMatchSetupSmoke.cpp
)
target_include_directories(concord_multiplayer_match_setup_smoke PRIVATE src)

add_executable(concord_multiplayer_snapshot_transfer_smoke
    tests/multiplayer/RuntimeMultiplayerSnapshotTransferSmoke.cpp
    # H073 explicit Contract SnapshotTransfer dependency below.
)
target_include_directories(
    concord_multiplayer_snapshot_transfer_smoke PRIVATE src
)

add_executable(concord_multiplayer_compatibility_fingerprint_smoke
    tests/multiplayer/RuntimeMultiplayerCompatibilityFingerprintSmoke.cpp
    # H074 explicit Contract Fingerprint dependency below.
)
target_include_directories(
    concord_multiplayer_compatibility_fingerprint_smoke PRIVATE src
)

add_executable(concord_runtime_config_smoke
    tests/infrastructure/RuntimeConfigSmoke.cpp
    src/runtime/config/RuntimeConfig.cpp
    src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/ui/settings/RuntimeSettings.cpp
    src/core/io/RuntimeAtomicFile.cpp
    src/core/io/IniFile.cpp
    src/assets/archive/MixArchive.cpp
    src/assets/archive/WestwoodCrypto.cpp
)
target_include_directories(concord_runtime_config_smoke PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_runtime_config_smoke PRIVATE /utf-8 /FS /W4 /permissive-)
endif()

add_executable(concord_multiplayer_launch_config_smoke
    tests/multiplayer/RuntimeMultiplayerLaunchConfigSmoke.cpp
    src/network/protocol/RuntimeMultiplayerLaunchConfig.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(
    concord_multiplayer_launch_config_smoke PRIVATE src
)

add_executable(concord_multiplayer_lobby_model_smoke
    tests/multiplayer/RuntimeMultiplayerLobbyModelSmoke.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(
    concord_multiplayer_lobby_model_smoke PRIVATE src
)

add_executable(concord_multiplayer_runtime_bridge_smoke
    tests/multiplayer/RuntimeMultiplayerRuntimeBridgeSmoke.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_runtime_bridge_smoke PRIVATE src
)

add_executable(concord_multiplayer_two_process_smoke
    tests/multiplayer/RuntimeMultiplayerTwoProcessSmoke.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_two_process_smoke PRIVATE src
)

add_executable(concord_multiplayer_lobby_flow_smoke
    tests/multiplayer/RuntimeMultiplayerLobbyFlowSmoke.cpp
    src/network/snapshots/RuntimeMultiplayerResumeSave.cpp src/core/io/IniFile.cpp
    src/network/lobby/RuntimeMultiplayerLobbyFlow.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/protocol/RuntimeMultiplayerLaunchConfig.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_multiplayer_lobby_flow_smoke PRIVATE src
)

add_executable(concord_multiplayer_player_intent_smoke
    tests/multiplayer/RuntimeMultiplayerPlayerIntentSmoke.cpp
    src/network/lockstep/RuntimeMultiplayerPlayerIntent.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
)
target_include_directories(
    concord_multiplayer_player_intent_smoke PRIVATE src
)

add_executable(concord_internet_connectivity_smoke
    tests/internet/RuntimeInternetConnectivitySmoke.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
)
target_include_directories(
    concord_internet_connectivity_smoke PRIVATE src
)

add_executable(concord_internet_coordinator_smoke
    tests/internet/RuntimeInternetCoordinatorSmoke.cpp
    src/network/internet/RuntimeInternetCoordinator.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
)
target_include_directories(concord_internet_coordinator_smoke PRIVATE src)

add_executable(concord_internet_matchmaking_smoke
    tests/internet/RuntimeInternetMatchmakingSmoke.cpp
    src/network/internet/RuntimeInternetMatchmaking.cpp
    src/network/transport/RuntimeInternetMatchmakingProtocol.cpp
)
target_include_directories(concord_internet_matchmaking_smoke PRIVATE src)

add_executable(concord_internet_community_smoke
    tests/internet/RuntimeInternetCommunitySmoke.cpp
    src/network/internet/RuntimeInternetCommunity.cpp
)
target_include_directories(concord_internet_community_smoke PRIVATE src)

add_executable(concord_internet_ranked_service_smoke
    tests/internet/RuntimeInternetRankedServiceSmoke.cpp src/network/internet/RuntimeInternetCommunity.cpp)
target_include_directories(concord_internet_ranked_service_smoke PRIVATE src)

add_executable(concord_internet_lobby_flow_smoke
    tests/internet/RuntimeInternetLobbyFlowSmoke.cpp
    src/network/snapshots/RuntimeMultiplayerResumeSave.cpp src/core/io/IniFile.cpp
    src/network/internet/RuntimeInternetLobbyFlow.cpp
    src/network/internet/RuntimeInternetCommunity.cpp
    src/network/internet/RuntimeInternetMatchmaking.cpp
    src/network/transport/RuntimeInternetMatchmakingProtocol.cpp
    src/network/internet/RuntimeInternetCoordinator.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
    src/network/transport/RuntimeInternetTcpTunnelProxy.cpp
    src/network/lobby/RuntimeMultiplayerLobbyFlow.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/protocol/RuntimeMultiplayerLaunchConfig.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(concord_internet_lobby_flow_smoke PRIVATE src)

add_executable(concord_internet_tcp_tunnel_proxy_smoke
    tests/internet/RuntimeInternetTcpTunnelProxySmoke.cpp
    src/network/transport/RuntimeInternetTcpTunnelProxy.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_internet_tcp_tunnel_proxy_smoke PRIVATE src
)

add_executable(concord_internet_tls_transport_smoke
    tests/internet/RuntimeInternetTlsTransportSmoke.cpp
    src/network/transport/RuntimeInternetTlsTransport.cpp
)
target_include_directories(
    concord_internet_tls_transport_smoke PRIVATE src
)

add_executable(concord_internet_service_transport_smoke
    tests/internet/RuntimeInternetServiceTransportSmoke.cpp
    src/network/transport/RuntimeInternetServiceTransport.cpp
    src/network/transport/RuntimeInternetTlsTransport.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
    src/network/session/RuntimeLanTransport.cpp
)
target_include_directories(
    concord_internet_service_transport_smoke PRIVATE src
)

add_library(concord_internet_service_host_core STATIC
    src/network/internet/RuntimeInternetServiceHost.cpp
    src/network/internet/RuntimeInternetServiceHostConfig.cpp
    src/network/transport/RuntimeInternetServiceTransport.cpp
    src/network/transport/RuntimeInternetTlsTransport.cpp
    src/network/internet/RuntimeInternetCommunity.cpp
    src/network/internet/RuntimeInternetCoordinator.cpp
    src/network/internet/RuntimeInternetMatchmaking.cpp
    src/network/transport/RuntimeInternetMatchmakingProtocol.cpp
    src/network/transport/RuntimeInternetConnectivity.cpp
    src/network/transport/RuntimeInternetTcpTunnelProxy.cpp
    src/network/session/RuntimeLanTransport.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
)
target_include_directories(concord_internet_service_host_core PUBLIC src)

add_executable(concord_internet_service
    src/network/internet/RuntimeInternetServiceMain.cpp
)
target_link_libraries(
    concord_internet_service PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_host_smoke
    tests/internet/RuntimeInternetServiceHostSmoke.cpp
)
target_link_libraries(
    concord_internet_service_host_smoke
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_lobby_e2e
    tests/internet/RuntimeInternetServiceLobbyE2E.cpp
    src/network/snapshots/RuntimeMultiplayerResumeSave.cpp src/core/io/IniFile.cpp
    src/network/internet/RuntimeInternetLobbyFlow.cpp
    src/network/lobby/RuntimeMultiplayerLobbyFlow.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/protocol/RuntimeMultiplayerLaunchConfig.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
)
target_include_directories(concord_internet_service_lobby_e2e PRIVATE src)
target_link_libraries(
    concord_internet_service_lobby_e2e
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_service_host_config_smoke
    tests/internet/RuntimeInternetServiceHostConfigSmoke.cpp
)
target_link_libraries(
    concord_internet_service_host_config_smoke
    PRIVATE concord_internet_service_host_core
)

add_executable(concord_internet_multiplayer_menu_smoke
    tests/internet/RuntimeInternetMultiplayerMenuSmoke.cpp
    src/ui/network/RuntimeInternetMultiplayerMenu.cpp
)
target_include_directories(
    concord_internet_multiplayer_menu_smoke PRIVATE src
)
target_compile_definitions(
    concord_internet_multiplayer_menu_smoke
    PRIVATE RUNTIME_INTERNET_MULTIPLAYER_MENU_PURE_HELPERS_ONLY
)

add_executable(concord_multiplayer_menu_smoke
    tests/multiplayer/RuntimeMultiplayerMenuSmoke.cpp
    src/ui/network/RuntimeMultiplayerMenu.cpp
    src/data/maps/RuntimeRandomMap.cpp src/core/io/RuntimeAtomicFile.cpp src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/data/rules/RulesDatabase.cpp src/simulation/production/RuntimeFactoryDeliveryState.cpp src/assets/archive/MixArchive.cpp src/assets/archive/WestwoodCrypto.cpp
    src/data/campaign/CooperativeScenarioDatabase.cpp
    src/core/io/IniFile.cpp
    src/data/maps/MapDatabase.cpp
    src/data/art/TheaterDatabase.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/lobby/RuntimeMultiplayerLobbyModel.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
)
target_include_directories(concord_multiplayer_menu_smoke PRIVATE src)
target_compile_definitions(
    concord_multiplayer_menu_smoke
    PRIVATE RUNTIME_MULTIPLAYER_MENU_PURE_HELPERS_ONLY _CRT_SECURE_NO_WARNINGS
)
if(CONCORD_MSVC_FRONTEND)
    # RuntimeAtomicFile's Windows error branches are shared with the runtime.
    concord_target_compile_options(concord_multiplayer_menu_smoke PRIVATE /wd4702)
endif()


add_executable(concord_shp_codec_contract
    tests/assets/ShpCodecContract.cpp
    src/assets/codecs/ShpCodec.cpp
)
target_include_directories(concord_shp_codec_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_shp_codec_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()
include(cmake/SelectionKeyContracts.cmake)

include(cmake/TeamKeyContracts.cmake)

include(cmake/LocalNavigationContracts.cmake)

include(cmake/ReferencePresentationContracts.cmake)

include(cmake/ReferenceCommunicationContracts.cmake)

include(cmake/PlanningFactoryKeyContracts.cmake)

include(cmake/CameraKeyContracts.cmake)

include(cmake/MenuKeyContracts.cmake)
include(cmake/SessionKeyContracts.cmake)

include("${CMAKE_SOURCE_DIR}/cmake/KeyboardRouterContracts.cmake")

include("${CMAKE_SOURCE_DIR}/cmake/KeyboardIngressContracts.cmake")

include(cmake/ChatTextContracts.cmake)
include(cmake/NavigationTerrainQueryContracts.cmake)

include(cmake/TmpTerrainContracts.cmake)

include(cmake/MapRightPointerContracts.cmake)

# W010-02: same production left pointer policy and window-free contract.
include(cmake/MapLeftPointerContracts.cmake)
include(cmake/PointerIngressContracts.cmake)

include(cmake/CursorControllerContracts.cmake)

include(cmake/InputCancellationContracts.cmake)
# S20: keep the legacy diagnostic entry in its own compilation unit.
target_sources(concord_special_mechanisms_smoke PRIVATE
    src/simulation/abilities/unit_special/diagnostics/RuntimeSpecialMechanismsSmoke.cpp
)

# S21: the TimedBomb owner is compiled separately by this actual consumer.
target_sources(concord_special_mechanisms_smoke PRIVATE src/simulation/abilities/unit_special/RuntimeTimedBomb.cpp)

# S22: candidate generation has an explicit source owner in every existing queue target.
target_sources(concord_path_request_queue_performance_smoke PRIVATE src/simulation/navigation/pathfinding/PathGoalCandidates.cpp)

# S23-01: explicit existing consumers share the independent production loader.
foreach(s23_production_rules_owner IN ITEMS
    concord_path_request_queue_performance_smoke
    concord_rules_special_features_smoke
    concord_rules_poison_policy_smoke
    concord_mod_extensions_smoke
    concord_screen_shake_smoke
    concord_rules_pad_aircraft_smoke
    concord_rules_production_smoke
    concord_rules_lookup_dispatch_smoke
    concord_rules_overlay_identity_smoke
    concord_rules_crate_smoke
    concord_art_firing_sequence_smoke
    concord_particle_system_smoke
    concord_garrison_system_smoke
    concord_repair_depot_system_smoke
    concord_grinding_system_smoke
    concord_aircraft_service_system_smoke
    concord_gate_system_smoke
    concord_power_audio_feedback_smoke
    concord_production_lanes_smoke
    concord_spy_system_smoke
    concord_death_weapon_system_smoke
    concord_special_mechanisms_smoke
    concord_special_building_system_smoke
    concord_hospital_armory_mod_compat_smoke
    concord_multiplayer_menu_smoke
)
    target_sources(${s23_production_rules_owner} PRIVATE src/data/rules/ProductionRulesLoader.cpp)
endforeach()
unset(s23_production_rules_owner)


# S25: the same production video owner, independently linked without menus/Shell.
add_executable(concord_bink_playback_service_contract
    tests/assets/BinkPlaybackServiceContract.cpp
    src/assets/video/BinkPlaybackService.cpp
)
target_include_directories(concord_bink_playback_service_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_bink_playback_service_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

# S26-01: explicit owners of the shared LAN session, including existing bridge contracts.
foreach(s26_lobby_map_owner IN ITEMS
    concord_runtime
    concord_multiplayer_lan_flow_smoke
    concord_multiplayer_runtime_bridge_smoke
    concord_multiplayer_two_process_smoke
    concord_multiplayer_lobby_flow_smoke
    concord_internet_lobby_flow_smoke
    concord_internet_tcp_tunnel_proxy_smoke
    concord_internet_service_lobby_e2e
    concord_selection_key_bridge_smoke
    concord_reference_communication_bridge_smoke
    concord_planning_factory_key_bridge_smoke
    concord_chat_text_bridge_smoke
)
    target_sources(${s26_lobby_map_owner} PRIVATE src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.cpp)
endforeach()
unset(s26_lobby_map_owner)

add_executable(concord_lobby_map_transfer_contract
    tests/multiplayer/RuntimeLobbyMapTransferContract.cpp
    src/network/lobby/RuntimeMultiplayerLobbyMapTransfer.cpp
    src/network/maps/RuntimeMultiplayerMapTransfer.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(concord_lobby_map_transfer_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_lobby_map_transfer_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

# S27-01: compile the existing replay envelope for the actual reset contract.
# The recorder/viewer UI remains outside this recovery-only batch.
target_sources(concord_runtime PRIVATE src/runtime/replay/RuntimeMatchReplay.cpp)
add_executable(concord_match_replay_contract
    tests/gameplay/RuntimeMatchReplaySmoke.cpp
    src/runtime/replay/RuntimeMatchReplay.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
)
target_include_directories(concord_match_replay_contract PRIVATE src)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_match_replay_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()

include(cmake/WindowLifecycleContracts.cmake)

include("${CMAKE_CURRENT_LIST_DIR}/SkirmishControllerContracts.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/WestwoodFontContracts.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/MenuImageContracts.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/ColorValueContracts.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/SkirmishBitmapTextContracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MenuPixelSurfaceContracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SkirmishBackBufferWin32Contracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SkirmishMusicContracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/LanTransportProviders.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/SnapshotSessionContracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SnapshotTransferLibraries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CompatibilityFingerprintLibraries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ProductionCoreLibraries.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/TaskExecutorContracts.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/GrandCannonPrewarmContracts.cmake")
