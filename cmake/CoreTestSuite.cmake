add_test(NAME production_core_hash_smoke_test COMMAND concord_production_core_hash_smoke)
set_tests_properties(production_core_hash_smoke_test PROPERTIES TIMEOUT 30 LABELS "no-assets;logic;production-core")
include("${CMAKE_CURRENT_LIST_DIR}/TestProfiles.cmake")
set(_core_required_tests
    game_world_deterministic_hash_smoke_test
    map_data_health_aircraft_smoke_test
    runtime_multiplayer_gameplay_command_smoke_test
    runtime_multiplayer_match_setup_smoke_test
    runtime_multiplayer_snapshot_transfer_smoke_test
    runtime_multiplayer_compatibility_fingerprint_smoke_test
    runtime_multiplayer_lockstep_smoke_test
    runtime_atomic_file_smoke_test
    runtime_lan_transport_smoke_test
    production_core_hash_smoke_test
)
get_property(_core_registered DIRECTORY PROPERTY TESTS)
list(LENGTH _core_registered _core_count)
if(NOT _core_count EQUAL 10)
    message(FATAL_ERROR "Core requires exactly 10 real tests, found ${_core_count}")
endif()
foreach(_core_test IN LISTS _core_required_tests)
    if(NOT TEST ${_core_test})
        message(FATAL_ERROR "Required core test missing: ${_core_test}")
    endif()
    foreach(_skip_property IN ITEMS SKIP_RETURN_CODE SKIP_REGULAR_EXPRESSION)
        get_test_property(${_core_test} ${_skip_property} _skip_value)
        if(NOT _skip_value STREQUAL "NOTFOUND")
            message(FATAL_ERROR "Required core test cannot skip: ${_core_test}")
        endif()
    endforeach()
    get_test_property(${_core_test} DISABLED _disabled)
    if(_disabled)
        message(FATAL_ERROR "Required core test disabled: ${_core_test}")
    endif()
endforeach()
get_filename_component(_core_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
set(_core_ctest "${_core_cmake_bin}/ctest${CMAKE_EXECUTABLE_SUFFIX}")
if(NOT EXISTS "${_core_ctest}")
    message(FATAL_ERROR "CTest is required by the core capability")
endif()
string(JOIN "|" _core_regex ${_core_required_tests})
add_custom_target(concord_core_check
    COMMAND "${_core_ctest}" --no-tests=error --output-on-failure --verbose -j 1 -R "^(${_core_regex})$"
    DEPENDS ${CONCORD_CORE_FIXTURE_TARGETS} concord_production_core_hash_smoke
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" USES_TERMINAL VERBATIM)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/portable-core-profile-$<CONFIG>.txt"
    CONTENT "profile=portable-core\nOS=${CMAKE_SYSTEM_NAME}\nwindow_backend=none\npresentation_backend=none\nstandard_libraries=${CMAKE_CXX_STANDARD_LIBRARIES}\nrequired_test_count=10\nprivate_file_and_lan_adapters=original-production\n")
