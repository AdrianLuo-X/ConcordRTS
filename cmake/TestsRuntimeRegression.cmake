if(CONCORD_PORTABLE_CORE)
    enable_testing()
    return()
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
# Native TMP depth and signed health-bar offsets use the live runtime producers.
add_test(NAME runtime_terrain_native_depth_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
set_tests_properties(runtime_terrain_native_depth_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_TERRAIN_NATIVE_DEPTH_ONLY=1;CONCORD_TERRAIN_NATIVE_DEPTH_OUTPUT=${CMAKE_BINARY_DIR}/vera_alignment/terrain-depth"
    TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME runtime_selection_delta_test COMMAND concord_runtime --command-smoke-test
    --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
set_tests_properties(runtime_selection_delta_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_SELECTION_DELTA_ONLY=1;CONCORD_SELECTION_DELTA_DIR=${CMAKE_BINARY_DIR}/vera_alignment/selection-delta"
    TIMEOUT 300 RUN_SERIAL TRUE)
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


if(WIN32)
    add_test(NAME runtime_navigation_terrain_query_smoke_test COMMAND concord_runtime
        --config "${CMAKE_SOURCE_DIR}/tests/fixtures/navigation_terrain_query_runtime.ini"
        --command-smoke-test
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
        --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    set_tests_properties(runtime_navigation_terrain_query_smoke_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 600 RUN_SERIAL TRUE
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_NAVIGATION_TERRAIN_QUERY_ONLY=1;CONCORD_NAVIGATION_TERRAIN_QUERY_OUTPUT=${CMAKE_BINARY_DIR}/navigation-terrain-query-output"
        LABELS "assets;navigation-terrain;renderer-integration")
endif()