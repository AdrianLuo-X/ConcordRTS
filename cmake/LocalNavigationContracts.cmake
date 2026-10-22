# W009-03: same production local navigation controller, ordinary owner flags and no device links.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Local navigation contract requires its actual ordinary owner")
endif()
add_executable(concord_local_navigation_controller_contract
    tests/infrastructure/LocalNavigationControllerContract.cpp
    src/ui/input/RuntimeLocalNavigationController.cpp)
target_include_directories(concord_local_navigation_controller_contract PRIVATE src)
target_compile_features(concord_local_navigation_controller_contract PRIVATE cxx_std_20)
target_compile_options(concord_local_navigation_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_local_navigation_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_local_navigation_controller_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
