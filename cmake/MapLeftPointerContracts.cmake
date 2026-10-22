# W010-02: the runtime and window-free contract compile the same left pointer owner.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Left pointer contract requires its actual ordinary owner")
endif()
add_executable(concord_map_left_pointer_contract
    tests/infrastructure/MapLeftPointerControllerContract.cpp
    src/ui/input/RuntimeMapLeftPointerController.cpp)
target_include_directories(concord_map_left_pointer_contract PRIVATE src)
target_compile_features(concord_map_left_pointer_contract PRIVATE cxx_std_20)
target_compile_options(concord_map_left_pointer_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_map_left_pointer_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_map_left_pointer_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()