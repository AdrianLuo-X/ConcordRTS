# W009-02: same production Team controller, ordinary owner flags and no device links.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Team key contract requires its actual ordinary owner")
endif()
add_executable(concord_team_key_controller_contract
    tests/infrastructure/TeamKeyControllerContract.cpp
    src/ui/input/RuntimeTeamKeyController.cpp)
target_include_directories(concord_team_key_controller_contract PRIVATE src)
target_compile_features(concord_team_key_controller_contract PRIVATE cxx_std_20)
target_compile_options(concord_team_key_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_team_key_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_team_key_controller_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
