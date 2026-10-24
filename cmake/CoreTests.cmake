# Exact existing required test commands; mandatory registry checked after all root includes.
add_test(
    NAME map_data_health_aircraft_smoke_test
    COMMAND concord_map_data_smoke
        "${CMAKE_SOURCE_DIR}/tests/fixtures/map_data_smoke.map"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/map_data_smoke_rules.ini"
)

add_test(
    NAME runtime_atomic_file_smoke_test
    COMMAND concord_runtime_atomic_file_smoke
)

add_test(
    NAME runtime_lan_transport_smoke_test
    COMMAND concord_lan_transport_smoke
)

add_test(
    NAME runtime_multiplayer_lockstep_smoke_test
    COMMAND concord_multiplayer_lockstep_smoke
)

add_test(
    NAME game_world_deterministic_hash_smoke_test
    COMMAND concord_game_world_deterministic_hash_smoke
)

add_test(
    NAME runtime_multiplayer_gameplay_command_smoke_test
    COMMAND concord_multiplayer_gameplay_command_smoke
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
