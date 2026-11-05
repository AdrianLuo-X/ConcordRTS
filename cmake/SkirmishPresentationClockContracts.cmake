# W007 uses the existing SkirmishServices clock virtual; no new production provider.
if(WIN32)
    add_test(NAME runtime_skirmish_presentation_clock_contract_test COMMAND concord_runtime
        --skirmish-ui-smoke-test "unused-skirmish-clock.bmp")
    set_tests_properties(runtime_skirmish_presentation_clock_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_SKIRMISH_PRESENTATION_CLOCK_ONLY=1"
        TIMEOUT 60 LABELS "windows;local-assets;skirmish;clock;contract")
endif()
