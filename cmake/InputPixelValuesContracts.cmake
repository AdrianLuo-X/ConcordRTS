# W002-01-input: ordinary Contract profile, evaluated from its actual owner.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "W002 input values require the actual ordinary-contract match-score owner")
endif()
add_executable(concord_input_pixel_values_contract
    tests/infrastructure/InputPixelValuesContract.cpp)
set(_w002_input_contract_targets concord_input_pixel_values_contract)
if(WIN32)
    add_executable(concord_input_pixel_values_win32_reference
        tests/infrastructure/InputPixelValuesWin32Reference.cpp)
    list(APPEND _w002_input_contract_targets concord_input_pixel_values_win32_reference)
endif()
foreach(_w002_input_target IN LISTS _w002_input_contract_targets)
    target_include_directories(${_w002_input_target} PRIVATE src)
    target_compile_features(${_w002_input_target} PRIVATE cxx_std_20)
    target_compile_options(${_w002_input_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(${_w002_input_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${_w002_input_target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()
# No links or sources from Shell, rendering, audio, network, or devices.
# The Win32 oracle is intentionally unavailable on non-Windows, never emulated.
unset(_w002_input_target)
unset(_w002_input_contract_targets)
