# W007-02 selected real world-audio timing contract; host output is supplied
# explicitly by the local verification runner, not written to player files.
add_test(NAME runtime_chronosphere_caller_clock_contract_test COMMAND concord_runtime
    --command-smoke-test --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
    --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
set_tests_properties(runtime_chronosphere_caller_clock_contract_test PROPERTIES
    ENVIRONMENT "CONCORD_COMMAND_SMOKE_CHRONOSPHERE_CALLER_CLOCK_ONLY=1"
    TIMEOUT 180 LABELS "windows;requires-game-assets;audio;clock;contract")
