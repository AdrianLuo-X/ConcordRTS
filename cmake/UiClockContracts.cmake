# W007-01: ordinary Contract profile, evaluated from its actual score owner.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "W007 UI clock contracts require the actual ordinary-contract match-score owner")
endif()
add_executable(concord_ui_clock_contract
    tests/infrastructure/UiClockContract.cpp)
set(_w007_ui_contract_targets concord_ui_clock_contract)
if(WIN32)
    add_executable(concord_ui_clock_win32_reference
        tests/infrastructure/UiClockWin32Reference.cpp
        src/runtime/host/RuntimeUiClockWin32.cpp)
    list(APPEND _w007_ui_contract_targets concord_ui_clock_win32_reference)
endif()
foreach(_w007_ui_target IN LISTS _w007_ui_contract_targets)
    target_include_directories(${_w007_ui_target} PRIVATE src)
    target_compile_features(${_w007_ui_target} PRIVATE cxx_std_20)
    target_compile_options(${_w007_ui_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(${_w007_ui_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${_w007_ui_target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()
# No Shell/render/audio/network/device links or compatibility CRT definitions.
# The native reference compiles the actual production Win32 provider.
unset(_w007_ui_target)
unset(_w007_ui_contract_targets)

