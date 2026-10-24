# W027: real production components, never copies or simplified algorithms.
include_guard(GLOBAL)

function(concord_add_world_core_profile target owner profile)
    concord_add_profiled_leaf(${target} src/world/model/GameWorld.cpp ${owner} ${profile})
    target_sources(${target} PRIVATE
        src/data/maps/MapDatabase.cpp src/data/rules/RulesDatabase.cpp
        src/data/art/TheaterDatabase.cpp src/data/rules/ProductionRulesLoader.cpp)
endfunction()

# Core-only configuration supplies a real production-profile fixture owner.
# Full product uses concord_runtime. S11 preserves distinct Contract Debug ABI.
function(concord_register_production_core runtime_owner)
    if(NOT TARGET concord_ini_runtime_leaf)
        concord_add_profiled_leaf(concord_ini_runtime_leaf src/core/io/IniFile.cpp ${runtime_owner} Runtime)
    endif()
    concord_add_world_core_profile(concord_world_core_runtime_leaf ${runtime_owner} Runtime)
    concord_add_world_core_profile(concord_world_core_hash_contract_leaf concord_game_world_deterministic_hash_smoke Contract)
    concord_add_world_core_profile(concord_world_core_map_contract_leaf concord_map_data_smoke Contract)
    concord_add_profiled_leaf(concord_ini_world_hash_contract_leaf src/core/io/IniFile.cpp concord_game_world_deterministic_hash_smoke Contract)
    concord_add_profiled_leaf(concord_ini_world_map_contract_leaf src/core/io/IniFile.cpp concord_map_data_smoke Contract)
    target_link_libraries(concord_world_core_runtime_leaf PRIVATE concord_ini_runtime_leaf)
    target_link_libraries(concord_world_core_hash_contract_leaf PRIVATE concord_ini_world_hash_contract_leaf)
    target_link_libraries(concord_world_core_map_contract_leaf PRIVATE concord_ini_world_map_contract_leaf)
    target_link_libraries(concord_game_world_deterministic_hash_smoke PRIVATE concord_world_core_hash_contract_leaf)
    target_link_libraries(concord_map_data_smoke PRIVATE concord_world_core_map_contract_leaf)

    concord_add_profiled_leaf(concord_gameplay_command_runtime_leaf src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp ${runtime_owner} Runtime)
    concord_add_profiled_leaf(concord_gameplay_command_contract_leaf src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp concord_multiplayer_gameplay_command_smoke Contract)
    concord_add_profiled_leaf(concord_match_setup_runtime_leaf src/network/protocol/RuntimeMultiplayerMatchSetup.cpp ${runtime_owner} Runtime)
    concord_add_profiled_leaf(concord_match_setup_contract_leaf src/network/protocol/RuntimeMultiplayerMatchSetup.cpp concord_multiplayer_match_setup_smoke Contract)
    concord_add_profiled_leaf(concord_session_runtime_leaf src/network/session/RuntimeMultiplayerSessionSystem.cpp ${runtime_owner} Runtime)
    concord_add_profiled_leaf(concord_session_contract_leaf src/network/session/RuntimeMultiplayerSessionSystem.cpp concord_multiplayer_lockstep_smoke Contract)
    concord_add_profiled_leaf(concord_lockstep_runtime_leaf src/network/lockstep/RuntimeMultiplayerLockstep.cpp ${runtime_owner} Runtime)
    concord_add_profiled_leaf(concord_lockstep_contract_leaf src/network/lockstep/RuntimeMultiplayerLockstep.cpp concord_multiplayer_lockstep_smoke Contract)
    target_link_libraries(concord_session_runtime_leaf PRIVATE concord_match_setup_runtime_leaf concord_snapshot_transfer_runtime_leaf)
    target_link_libraries(concord_session_contract_leaf PRIVATE concord_match_setup_contract_leaf concord_snapshot_transfer_contract_leaf)
    target_link_libraries(concord_lockstep_runtime_leaf PRIVATE concord_gameplay_command_runtime_leaf concord_session_runtime_leaf)
    target_link_libraries(concord_lockstep_contract_leaf PRIVATE concord_gameplay_command_contract_leaf concord_session_contract_leaf)
    target_link_libraries(concord_multiplayer_gameplay_command_smoke PRIVATE concord_gameplay_command_contract_leaf)
    target_link_libraries(concord_multiplayer_match_setup_smoke PRIVATE concord_match_setup_contract_leaf)
    target_link_libraries(concord_multiplayer_lockstep_smoke PRIVATE concord_lockstep_contract_leaf)
    target_link_libraries(${runtime_owner} PRIVATE concord_world_core_runtime_leaf
        concord_gameplay_command_runtime_leaf concord_match_setup_runtime_leaf
        concord_session_runtime_leaf concord_lockstep_runtime_leaf)
endfunction()

if(NOT CONCORD_PORTABLE_CORE)
    # The original full GameWorld fixture entry, compiled with the actual Runtime
    # profile and linked to the exact Runtime archive also used by the real game.
    add_executable(concord_production_core_hash_smoke tests/infrastructure/GameWorldDeterministicHashSmoke.cpp)
    target_include_directories(concord_production_core_hash_smoke PRIVATE src)
    target_compile_features(concord_production_core_hash_smoke PRIVATE cxx_std_20)
    target_compile_options(concord_production_core_hash_smoke PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_production_core_hash_smoke PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET concord_production_core_hash_smoke PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    endif()
    concord_register_production_core(concord_runtime)
    target_link_libraries(concord_production_core_hash_smoke PRIVATE concord_world_core_runtime_leaf)
endif()
