# W001: a standard-header-only contract using the ordinary Contract profile.
# Included by the existing root scope after its actual match-score owner exists.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "W001 requires the actual ordinary-contract match-score owner")
endif()
add_executable(concord_player_defeat_notice_contract
    tests/infrastructure/PlayerDefeatNoticeContract.cpp)
target_include_directories(concord_player_defeat_notice_contract PRIVATE src)
target_compile_features(concord_player_defeat_notice_contract PRIVATE cxx_std_20)
# Evaluate the owner's final private options/definitions, preserving their order.
# No Runtime ABI/CRT settings or S12-only CRT diagnostic compatibility are added.
target_compile_options(concord_player_defeat_notice_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_player_defeat_notice_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_player_defeat_notice_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
# No links or sources from Shell, rendering, audio, network, or device implementations.
