# P06/W019-G003-01 is a private CPU-native producer for S41 value commands.
concord_add_profiled_leaf(concord_map_native_overlay_win32 src/ui/surface/win32/MapNativeOverlayWin32.cpp concord_runtime Runtime)
target_sources(concord_map_native_overlay_win32 PRIVATE src/ui/surface/win32/MapCompositionWin32.cpp)
target_link_libraries(concord_map_native_overlay_win32 PRIVATE concord_skirmish_back_buffer_win32)
target_link_libraries(concord_runtime PRIVATE concord_map_native_overlay_win32)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_map_native_overlay_win32 PRIVATE /W4 /WX)
endif()