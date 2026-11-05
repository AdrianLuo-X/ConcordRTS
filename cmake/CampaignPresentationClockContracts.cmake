# W007 Campaign production presentation time; explicit material output required.
if(WIN32)
    add_test(NAME runtime_campaign_presentation_clock_contract_test COMMAND concord_runtime
        --campaign-ui-smoke-test "unused-campaign-clock.bmp")
    set_tests_properties(runtime_campaign_presentation_clock_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_CAMPAIGN_PRESENTATION_CLOCK_ONLY=1"
        TIMEOUT 60 LABELS "windows;no-assets;campaign;clock;contract")
endif()
