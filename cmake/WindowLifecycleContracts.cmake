# W013-01. One production implementation, with the S11 Runtime/Contract ABI profiles.
include_guard(GLOBAL)
concord_add_profiled_leaf(concord_window_lifecycle src/runtime/window/RuntimeWindowLifecycle.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_window_host_win32 src/runtime/window/win32/RuntimeWindowHostWin32.cpp concord_runtime Runtime)
# Contract Debug uses MDd/iterator2; production Debug keeps MD/iterator0.
# Both variants compile these exact production sources; no implementation copy.
concord_add_profiled_leaf(concord_window_lifecycle_contract_leaf src/runtime/window/RuntimeWindowLifecycle.cpp concord_runtime_match_score_contract Contract)
concord_add_profiled_leaf(concord_window_host_win32_contract_leaf src/runtime/window/win32/RuntimeWindowHostWin32.cpp concord_runtime_match_score_contract Contract)
target_link_libraries(concord_runtime PRIVATE concord_window_lifecycle concord_window_host_win32)
add_executable(concord_window_lifecycle_contract tests/infrastructure/RuntimeWindowLifecycleContract.cpp)
target_link_libraries(concord_window_lifecycle_contract PRIVATE concord_window_lifecycle_contract_leaf)
if(WIN32)
    add_executable(concord_window_host_win32_contract tests/infrastructure/RuntimeWindowHostWin32Contract.cpp)
    target_link_libraries(concord_window_host_win32_contract PRIVATE concord_window_host_win32_contract_leaf concord_window_lifecycle_contract_leaf)
endif()
foreach(w013_target IN ITEMS concord_window_lifecycle_contract concord_window_host_win32_contract)
    if(TARGET ${w013_target})
        target_include_directories(${w013_target} PRIVATE src)
        target_compile_features(${w013_target} PRIVATE cxx_std_20)
        target_compile_options(${w013_target} PRIVATE
            "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
        target_compile_definitions(${w013_target} PRIVATE
            "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
        if(CONCORD_MSVC_ABI)
            set_property(TARGET ${w013_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endif()
endforeach()
unset(w013_target)
