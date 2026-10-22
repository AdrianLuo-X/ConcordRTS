# W009-09: standard session policy uses the existing ordinary contract profile.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Session key contract requires its actual ordinary owner")
endif()
add_executable(concord_session_key_contract
    tests/infrastructure/SessionKeyControllerContract.cpp
    src/ui/input/RuntimeSessionKeyController.cpp)
target_include_directories(concord_session_key_contract PRIVATE src)
target_compile_features(concord_session_key_contract PRIVATE cxx_std_20)
target_compile_options(concord_session_key_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_session_key_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_session_key_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
