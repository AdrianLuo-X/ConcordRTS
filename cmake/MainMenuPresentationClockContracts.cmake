# W007 Main phase/video startup time input, separate from menu music.
if(WIN32)
    add_test(NAME runtime_main_menu_presentation_clock_contract_test COMMAND concord_runtime
        --main-menu-ui-smoke-test "unused-main-presentation-clock.bmp")
    set_tests_properties(runtime_main_menu_presentation_clock_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_MAIN_MENU_PRESENTATION_CLOCK_ONLY=1"
        TIMEOUT 60 LABELS "windows;no-assets;main-menu;clock;contract")
endif()
