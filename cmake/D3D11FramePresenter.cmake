include_guard(GLOBAL)
# Device libraries belong to this private Windows leaf, not PUBLIC/INTERFACE
# portable-core contracts. No shader is compiled for exact-size CPU-frame copy.
concord_add_profiled_leaf(concord_d3d11_frame_presenter_win32 src/runtime/presentation/win32/D3D11FramePresenterWin32.cpp concord_runtime Runtime)
target_link_libraries(concord_d3d11_frame_presenter_win32 PRIVATE d3d11 dxgi dwmapi)
target_link_libraries(concord_runtime PRIVATE concord_d3d11_frame_presenter_win32)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_d3d11_frame_presenter_win32 PRIVATE /W4 /WX)
endif()
add_subdirectory(tests/infrastructure/d3d11_frame_presenter)
