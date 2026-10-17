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

add_test(NAME csf_codec_contract_test COMMAND concord_csf_codec_contract)
set_tests_properties(csf_codec_contract_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;csf-codec-contract")

add_test(NAME pal_decoder_contract_test COMMAND concord_pal_decoder_contract)
set_tests_properties(pal_decoder_contract_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;pal-codec")
if(EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/palettes/isotem.pal"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/extracted_palettes/cache_isotem.pal"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/extracted_palettes/cache_unittem.pal"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/mpyscrn.pal"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/expandmd01/mpyscrn.pal"
    AND EXISTS "${CMAKE_SOURCE_DIR}/resources/palettes/anim.pal")
    add_test(NAME pal_decoder_asset_samples_test COMMAND concord_pal_decoder_contract
        "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/palettes/isotem.pal"
        "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/extracted_palettes/cache_isotem.pal"
        "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/extracted_palettes/cache_unittem.pal"
        "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/mpyscrn.pal"
        "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/expandmd01/mpyscrn.pal"
        "${CMAKE_SOURCE_DIR}/resources/palettes/anim.pal")
    set_tests_properties(pal_decoder_asset_samples_test
        PROPERTIES TIMEOUT 30 LABELS "local-assets;pal-codec")
endif()

add_test(NAME map_preview_resource_context_contract_test
    COMMAND concord_map_preview_resource_context_contract "${CMAKE_BINARY_DIR}/s10-preview-session-contract")
set_tests_properties(map_preview_resource_context_contract_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;preview-resource-context")

add_test(NAME runtime_replay_event_snapshot_contract_test
    COMMAND concord_replay_event_snapshot_contract
        "${CMAKE_SOURCE_DIR}/tests/fixtures/runtime_replay_snapshot_valid.txt"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/runtime_replay_snapshot_invalid.txt")
add_test(NAME runtime_replay_event_snapshot_header_probe_test
    COMMAND concord_replay_event_snapshot_header_probe)
set_tests_properties(runtime_replay_event_snapshot_contract_test runtime_replay_event_snapshot_header_probe_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;runtime-contract;replay-snapshot")

add_test(NAME runtime_match_score_contract_test COMMAND concord_runtime_match_score_contract)
set_tests_properties(runtime_match_score_contract_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;match-score-contract")

add_test(NAME player_slot_role_contract_test COMMAND concord_player_slot_role_contract)
add_test(NAME runtime_observer_contract_test COMMAND concord_runtime_observer_contract)
set_tests_properties(player_slot_role_contract_test runtime_observer_contract_test
    PROPERTIES TIMEOUT 30 LABELS "no-assets;seat-role-contract")

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
    NAME runtime_config_smoke_test
    COMMAND concord_runtime_config_smoke "${CMAKE_BINARY_DIR}/runtime_config_smoke"
)
set_tests_properties(runtime_config_smoke_test PROPERTIES TIMEOUT 30)
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


add_test(NAME shp_codec_contract_test COMMAND concord_shp_codec_contract)
set_tests_properties(shp_codec_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;shp-codec-contract")