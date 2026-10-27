# W019-02 private Win32 DIB/DC owner; one product CPP, S11 ABI variants.
include_guard(GLOBAL)
if(NOT WIN32)
    return()
endif()
concord_add_profiled_leaf(concord_skirmish_back_buffer_win32 src/ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_skirmish_back_buffer_win32_contract_leaf src/ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.cpp concord_runtime_match_score_contract Contract)
target_link_libraries(concord_runtime PRIVATE concord_skirmish_back_buffer_win32)
add_executable(concord_skirmish_back_buffer_win32_contract tests/infrastructure/RuntimeSkirmishBackBufferWin32Contract.cpp)
target_include_directories(concord_skirmish_back_buffer_win32_contract PRIVATE src)
target_compile_features(concord_skirmish_back_buffer_win32_contract PRIVATE cxx_std_20)
target_link_libraries(concord_skirmish_back_buffer_win32_contract PRIVATE concord_skirmish_back_buffer_win32_contract_leaf)
target_compile_options(concord_skirmish_back_buffer_win32_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_skirmish_back_buffer_win32_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_skirmish_back_buffer_win32_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Runtime already has gdi32 in its verified Windows platform link contract.
# Keep that consumer capability once; do not propagate a duplicate through a
# Runtime static archive. The independent native Contract declares its own link.
target_link_libraries(concord_skirmish_back_buffer_win32_contract_leaf PRIVATE gdi32)
target_link_libraries(concord_skirmish_back_buffer_win32_contract PRIVATE concord_menu_pixel_surface_contract_leaf)

# W018-01/W019-01 native fixture in a narrowly linked child scope.
add_subdirectory(tests/infrastructure/main_back_buffer)
