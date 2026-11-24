set(material_parent "${OUTPUT_PARENT}")
if(NOT "$ENV{CONCORD_RANDOM_MAP_TEST_OUTPUT}" STREQUAL "")
    set(material_parent "$ENV{CONCORD_RANDOM_MAP_TEST_OUTPUT}")
endif()
if(NOT IS_ABSOLUTE "${material_parent}")
    message(FATAL_ERROR "Random-map test material requires an absolute output directory")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(output "${material_parent}/${TEST_KIND}-${run_id}")
if(EXISTS "${output}")
    message(FATAL_ERROR "Random-map test output already exists: ${output}")
endif()

set(resources "${PROJECT_ROOT}/RedAlert2")
set(local_data "${PROJECT_ROOT}/local_data")
if(TEST_KIND STREQUAL "generation")
    execute_process(COMMAND "${TEST_EXE}" "${output}" "${local_data}" "${resources}"
        RESULT_VARIABLE exit_code)
elseif(TEST_KIND STREQUAL "gameplay")
    file(MAKE_DIRECTORY "${output}")
    # This stock test exercises normal production independent of player settings.
    set(config "${output}/runtime.ini")
    file(WRITE "${config}" "[Runtime]\n")
    set(ENV{CONCORD_COMMAND_SMOKE_RANDOM_MAP_ONLY} 1)
    set(ENV{CONCORD_RANDOM_MAP_OUTPUT} "${output}/materials")
    execute_process(COMMAND "${TEST_EXE}" --command-smoke-test --config "${config}"
        --local-data-root "${local_data}" --resource-root "${resources}"
        --map "${PROJECT_ROOT}/tests/fixtures/aircraft_motion_flat_lake.map"
        WORKING_DIRECTORY "${output}" RESULT_VARIABLE exit_code)
else()
    message(FATAL_ERROR "Unknown random-map test kind: ${TEST_KIND}")
endif()
message(STATUS "Random-map test output: ${output}")
if(NOT "${exit_code}" STREQUAL "0")
    message(FATAL_ERROR "Random-map ${TEST_KIND} actual exit: ${exit_code}")
endif()
