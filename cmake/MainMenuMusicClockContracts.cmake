# W007 Main Menu music time-only native contract; material output is required.
if(WIN32)
    add_executable(concord_ui_clock_lifetime_contract
        tests/infrastructure/UiClockLifetimeContract.cpp
        src/runtime/host/RuntimeUiClockWin32.cpp)
    target_include_directories(concord_ui_clock_lifetime_contract PRIVATE src)
    if(CONCORD_MSVC_FRONTEND)
        concord_target_compile_options(concord_ui_clock_lifetime_contract PRIVATE /utf-8 /W4 /WX /permissive-)
    endif()
    add_test(NAME ui_clock_lifetime_contract_test COMMAND concord_ui_clock_lifetime_contract)
    set_tests_properties(ui_clock_lifetime_contract_test PROPERTIES TIMEOUT 30
        LABELS "windows;no-assets;native-clock;lifetime;contract")
    add_test(NAME runtime_main_menu_music_clock_contract_test COMMAND concord_runtime
        --main-menu-ui-smoke-test "unused-clock-contract.bmp")
    set_tests_properties(runtime_main_menu_music_clock_contract_test PROPERTIES
        ENVIRONMENT "CONCORD_MAIN_MENU_MUSIC_CLOCK_ONLY=1"
        TIMEOUT 60 LABELS "windows;no-assets;native-audio;clock;contract")
endif()
