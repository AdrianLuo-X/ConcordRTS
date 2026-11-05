# W007 actual Internet UI clock input; explicit material directory is required.
if(WIN32)
    add_test(NAME runtime_internet_menu_clock_contract_test COMMAND concord_runtime
        --main-menu-ui-smoke-test "unused-internet-clock.bmp")
    set_tests_properties(runtime_internet_menu_clock_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_INTERNET_MENU_CLOCK_ONLY=1"
        TIMEOUT 60 LABELS "windows;no-assets;internet-ui;clock;contract")
endif()
