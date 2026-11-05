# W007-02 time-only contract: real production private MCI consumer and old body.
if(WIN32)
    add_executable(concord_menu_sfx_clock_contract
        tests/audio/RuntimeMenuSfxClockContract.cpp
        src/runtime/host/RuntimeUiClockWin32.cpp)
    target_include_directories(concord_menu_sfx_clock_contract PRIVATE src tests/audio)
    target_link_libraries(concord_menu_sfx_clock_contract PRIVATE concord_menu_sound_win32_contract)
    if(CONCORD_MSVC_FRONTEND)
        concord_target_compile_options(concord_menu_sfx_clock_contract PRIVATE /utf-8 /W4 /WX /permissive-)
    endif()
    add_test(NAME menu_sfx_clock_contract_test COMMAND concord_menu_sfx_clock_contract)
    set_tests_properties(menu_sfx_clock_contract_test PROPERTIES TIMEOUT 60
        LABELS "windows;no-assets;native-audio;clock;contract")
endif()
