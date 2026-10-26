if(CONCORD_PORTABLE_CORE)
    include("${CMAKE_CURRENT_LIST_DIR}/CoreTestSuite.cmake")
    return()
endif()

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

# Appended after the original registrations; root include order stays intact.
include(cmake/TestProfiles.cmake)

# W010-01: new right pointer owner, standard contract and actual native path.
add_test(NAME runtime_map_right_pointer_contract_test COMMAND concord_map_right_pointer_contract)
set_tests_properties(runtime_map_right_pointer_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60 LABELS "no-assets;platform-input;right-pointer")
if(WIN32)
    add_test(NAME runtime_map_right_pointer_integration_smoke_test COMMAND concord_runtime --command-smoke-test
        --config "${CMAKE_SOURCE_DIR}/tests/fixtures/map_right_pointer_runtime.ini"
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
    set_tests_properties(runtime_map_right_pointer_integration_smoke_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 600 RUN_SERIAL TRUE
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MAP_RIGHT_POINTER_ONLY=1;CONCORD_MAP_RIGHT_POINTER_OUTPUT=${CMAKE_BINARY_DIR}/map_right_pointer_integration"
        LABELS "assets;right-pointer;windows-reference")
endif()

# W010-02: new left pointer owner, standard contract and actual native path.
add_test(NAME runtime_map_left_pointer_contract_test COMMAND concord_map_left_pointer_contract)
set_tests_properties(runtime_map_left_pointer_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60 LABELS "no-assets;platform-input;left-pointer")
if(WIN32)
    add_test(NAME runtime_map_left_pointer_integration_smoke_test COMMAND concord_runtime --command-smoke-test
        --config "${CMAKE_SOURCE_DIR}/tests/fixtures/map_left_pointer_runtime.ini"
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
    set_tests_properties(runtime_map_left_pointer_integration_smoke_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 1800 RUN_SERIAL TRUE
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MAP_LEFT_POINTER_ONLY=1;CONCORD_MAP_LEFT_POINTER_OUTPUT=${CMAKE_BINARY_DIR}/map_left_pointer_integration"
        LABELS "assets;left-pointer;windows-reference")
endif()

# W010-03: complete shared pointer admission, diplomacy and sidebar paging.
add_test(NAME pointer_ingress_controller_contract_test COMMAND concord_pointer_ingress_contract)
set_tests_properties(pointer_ingress_controller_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60 LABELS "no-assets;platform-input;pointer-ingress;sidebar")
if(WIN32)
    add_test(NAME runtime_pointer_ingress_integration_smoke_test COMMAND concord_runtime --command-smoke-test
        --config "${CMAKE_SOURCE_DIR}/tests/fixtures/pointer_ingress_runtime.ini"
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2" --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
    set_tests_properties(runtime_pointer_ingress_integration_smoke_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 600 RUN_SERIAL TRUE
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_POINTER_INGRESS_ONLY=1;CONCORD_POINTER_INGRESS_OUTPUT=${CMAKE_BINARY_DIR}/pointer_ingress_integration"
        LABELS "assets;pointer-ingress;sidebar;windows-reference")
endif()

# W010-04: complete cursor helper and WM_SETCURSOR policy.
add_test(NAME cursor_controller_contract_test COMMAND concord_cursor_controller_contract)
set_tests_properties(cursor_controller_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60 LABELS "no-assets;platform-input;cursor")
if(WIN32)
    add_test(NAME runtime_cursor_integration_smoke_test
        COMMAND "${CMAKE_COMMAND}"
            "-DGAME_EXECUTABLE=$<TARGET_FILE:concord_runtime>"
            "-DFIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/cursor_runtime.ini"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DOUTPUT_PARENT=${CMAKE_BINARY_DIR}/cursor-runs"
            -P "${CMAKE_SOURCE_DIR}/cmake/RunCursorNative.cmake")
    set_tests_properties(runtime_cursor_integration_smoke_test PROPERTIES
        TIMEOUT 1800 RUN_SERIAL TRUE LABELS "assets;cursor;windows-reference")
endif()

add_test(NAME input_cancellation_controller_contract_test COMMAND concord_input_cancellation_contract)
# W012-01: each registered run owns its config/cwd/save/output/TEMP.
if(WIN32)
    add_test(NAME runtime_input_cancellation_integration_smoke_test
        COMMAND "${CMAKE_COMMAND}"
            "-DGAME_EXECUTABLE=$<TARGET_FILE:concord_runtime>"
            "-DFIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/input_cancellation_runtime.ini"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DOUTPUT_PARENT=${CMAKE_BINARY_DIR}/input-cancellation-runs"
            -P "${CMAKE_SOURCE_DIR}/cmake/RunInputCancellationNative.cmake")
    set_tests_properties(runtime_input_cancellation_integration_smoke_test PROPERTIES
        TIMEOUT 7300 RUN_SERIAL TRUE LABELS "assets;input-cancellation;windows-reference")
endif()

# S15-01: append without moving any original CTest declaration.
add_test(NAME chronosphere_relocation_contract_test COMMAND concord_chronosphere_relocation_contract)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/s15-01-chrono-standard-cwd")
set_tests_properties(chronosphere_relocation_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/s15-01-chrono-standard-cwd"
    TIMEOUT 60 LABELS "no-assets;world-relocation")

# S16-01: append, retaining every complete prior registration and its backtrace.
add_test(NAME house_economy_context_contract_test COMMAND concord_house_economy_context_contract)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/s16-01-house-standard-cwd")
set_tests_properties(house_economy_context_contract_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/s16-01-house-standard-cwd"
    TIMEOUT 60 LABELS "no-assets;house-economy")

# S26-01: no socket owner, session, Shell, window or player files in this target.
add_test(NAME runtime_lobby_map_transfer_contract_test COMMAND concord_lobby_map_transfer_contract)
set_tests_properties(runtime_lobby_map_transfer_contract_test PROPERTIES
    TIMEOUT 60 LABELS "no-assets;lobby-map-state-machine")

add_test(NAME window_lifecycle_contract_test COMMAND concord_window_lifecycle_contract)
set_tests_properties(window_lifecycle_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;window-lifecycle")
if(WIN32)
    add_test(NAME window_host_win32_contract_test COMMAND concord_window_host_win32_contract)
    set_tests_properties(window_host_win32_contract_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "no-assets;window-lifecycle;windows-reference")
endif()

add_test(NAME skirmish_setup_contract_test COMMAND concord_skirmish_setup_contract)
set_tests_properties(skirmish_setup_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;skirmish-controller")
add_test(NAME window_parent_win32_contract_test COMMAND concord_window_parent_win32_contract)
set_tests_properties(window_parent_win32_contract_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "no-assets;window-parent;windows-reference")

add_test(NAME skirmish_controller_contract_test COMMAND concord_skirmish_controller_contract)
set_tests_properties(skirmish_controller_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;skirmish-controller")

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    add_test(NAME runtime_skirmish_window_lifecycle_test COMMAND concord_runtime
        --config "${CMAKE_BINARY_DIR}/w014-window-lifecycle-private.ini"
        --skirmish-ui-smoke-test "${CMAKE_BINARY_DIR}/w014-window-lifecycle/menu.bmp"
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
        --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    set_tests_properties(runtime_skirmish_window_lifecycle_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "CONCORD_SKIRMISH_WINDOW_LIFECYCLE_ONLY=1"
        LABELS "windows-reference;local-assets;skirmish-window")
endif()

add_test(NAME westwood_font_codec_contract_test COMMAND concord_westwood_font_contract)
set_tests_properties(westwood_font_codec_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;font-byte-codec")

add_test(NAME menu_image_contract_test COMMAND concord_menu_image_contract)
set_tests_properties(menu_image_contract_test PROPERTIES TIMEOUT 60 LABELS "menu-pixels;no-assets")

add_test(NAME color_values_contract_test COMMAND concord_color_values_contract)
add_test(NAME color_values_win32_contract_test COMMAND concord_color_values_win32_contract)
set_tests_properties(color_values_contract_test color_values_win32_contract_test PROPERTIES TIMEOUT 60 LABELS "color-values")

add_test(NAME skirmish_bitmap_text_contract_test COMMAND concord_skirmish_bitmap_text_contract)
set_tests_properties(skirmish_bitmap_text_contract_test PROPERTIES TIMEOUT 120 LABELS "bitmap-text;no-assets")

add_test(NAME menu_pixel_surface_contract_test COMMAND concord_menu_pixel_surface_contract)
set_tests_properties(menu_pixel_surface_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;menu-pixel-surface")

if(TARGET concord_skirmish_back_buffer_win32_contract)
    add_test(NAME skirmish_back_buffer_win32_contract_test COMMAND concord_skirmish_back_buffer_win32_contract)
    set_tests_properties(skirmish_back_buffer_win32_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;win32-buffer")
endif()

add_test(NAME skirmish_music_policy_contract_test COMMAND concord_skirmish_music_policy_contract)
set_tests_properties(skirmish_music_policy_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;music-policy")
if(TARGET concord_skirmish_music_win32_contract)
    add_test(NAME skirmish_music_win32_contract_test COMMAND concord_skirmish_music_win32_contract "${CMAKE_CURRENT_BINARY_DIR}/music-native-contract")
    set_tests_properties(skirmish_music_win32_contract_test PROPERTIES TIMEOUT 90 LABELS "no-assets;win32-music")
endif()

if(TARGET concord_tcp_boundary_contract)
    add_test(NAME tcp_native_boundary_contract_test COMMAND concord_tcp_boundary_contract)
    set_tests_properties(tcp_native_boundary_contract_test PROPERTIES TIMEOUT 120 LABELS "no-assets;windows;tcp-boundary")
endif()

add_test(NAME snapshot_session_contract_test COMMAND concord_snapshot_session_contract)
set_tests_properties(snapshot_session_contract_test PROPERTIES TIMEOUT 60 LABELS "no-assets;snapshot-session")

add_test(NAME production_core_hash_smoke_test COMMAND concord_production_core_hash_smoke)
set_tests_properties(production_core_hash_smoke_test PROPERTIES TIMEOUT 30 LABELS "no-assets;logic;production-core")

# S34: root testing is enabled after DevelopmentTargets creates the new scope.
if(TARGET concord_bounded_task_executor_contract)
    add_test(NAME bounded_task_executor_contract_test COMMAND concord_bounded_task_executor_contract)
    set_tests_properties(bounded_task_executor_contract_test PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE LABELS "no-assets;bounded-tasks;logic")
endif()

if(TARGET concord_grand_cannon_raster_contract)
    add_test(NAME grand_cannon_raster_contract_test COMMAND concord_grand_cannon_raster_contract)
    set_tests_properties(grand_cannon_raster_contract_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "no-assets;bounded-tasks;render")
endif()
if(WIN32 AND TARGET concord_runtime AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2")
    add_test(NAME runtime_grand_cannon_prewarm_contract_test
        COMMAND concord_runtime --loading-smoke-test --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    set_tests_properties(runtime_grand_cannon_prewarm_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_GRAND_CANNON_PREWARM_VERIFY=1" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 600 RUN_SERIAL TRUE LABELS "assets;bounded-tasks;render")
endif()
