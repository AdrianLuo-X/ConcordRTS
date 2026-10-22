# W010-04: runtime and isolated contract compile the same complete cursor owner.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Cursor contract requires its ordinary owner profile")
endif()
add_executable(concord_cursor_controller_contract
    tests/infrastructure/CursorControllerContract.cpp
    src/ui/input/RuntimeCursorController.cpp)
target_include_directories(concord_cursor_controller_contract PRIVATE src)
target_compile_features(concord_cursor_controller_contract PRIVATE cxx_std_20)
target_compile_options(concord_cursor_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_cursor_controller_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_cursor_controller_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
