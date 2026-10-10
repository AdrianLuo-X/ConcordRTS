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
