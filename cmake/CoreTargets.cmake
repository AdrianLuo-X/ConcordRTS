# Real production core capability; OS and compiler are independent axes.
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
if(WIN32)
    # Core has no window/COM/audio/graphics backend. Keep only Kernel32 and the
    # original socket fixture's explicit ws2_32. This cache is core-only.
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib" CACHE STRING "Core OS support, no window/device defaults" FORCE)
endif()
set(CONCORD_CORE_FIXTURE_TARGETS
    concord_game_world_deterministic_hash_smoke
    concord_map_data_smoke
    concord_multiplayer_gameplay_command_smoke
    concord_multiplayer_match_setup_smoke
    concord_multiplayer_snapshot_transfer_smoke
    concord_multiplayer_compatibility_fingerprint_smoke
    concord_multiplayer_lockstep_smoke
    concord_runtime_atomic_file_smoke
    concord_lan_transport_smoke
)
add_executable(concord_game_world_deterministic_hash_smoke tests/infrastructure/GameWorldDeterministicHashSmoke.cpp)
add_executable(concord_map_data_smoke tests/maps/MapDataSmoke.cpp)
add_executable(concord_multiplayer_gameplay_command_smoke tests/multiplayer/RuntimeMultiplayerGameplayCommandSmoke.cpp)
add_executable(concord_multiplayer_match_setup_smoke tests/multiplayer/RuntimeMultiplayerMatchSetupSmoke.cpp)
add_executable(concord_multiplayer_snapshot_transfer_smoke tests/multiplayer/RuntimeMultiplayerSnapshotTransferSmoke.cpp)
add_executable(concord_multiplayer_compatibility_fingerprint_smoke tests/multiplayer/RuntimeMultiplayerCompatibilityFingerprintSmoke.cpp)
add_executable(concord_multiplayer_lockstep_smoke tests/multiplayer/RuntimeMultiplayerLockstepSmoke.cpp)
add_executable(concord_runtime_atomic_file_smoke tests/infrastructure/RuntimeAtomicFileSmoke.cpp src/core/io/RuntimeAtomicFile.cpp)
add_executable(concord_lan_transport_smoke tests/multiplayer/RuntimeLanTransportSmoke.cpp)
add_executable(concord_production_core_hash_smoke tests/infrastructure/GameWorldDeterministicHashSmoke.cpp)
foreach(_core_target IN LISTS CONCORD_CORE_FIXTURE_TARGETS)
    target_include_directories(${_core_target} PRIVATE src)
    target_compile_features(${_core_target} PRIVATE cxx_std_20)
endforeach()
target_include_directories(concord_production_core_hash_smoke PRIVATE src)
target_compile_features(concord_production_core_hash_smoke PRIVATE cxx_std_20)
include("${CMAKE_CURRENT_LIST_DIR}/SnapshotTransferLibraries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CompatibilityFingerprintLibraries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/LanTransportProviders.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ProductionCoreLibraries.cmake")
concord_register_snapshot_transfer_libraries(concord_production_core_hash_smoke)
concord_register_fingerprint_libraries(concord_production_core_hash_smoke)
concord_register_lan_transport_libraries("")
concord_register_production_core(concord_production_core_hash_smoke)
