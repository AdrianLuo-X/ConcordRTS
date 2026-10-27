include_guard(GLOBAL)
# Device libraries belong to this private Windows leaf, not PUBLIC/INTERFACE
# portable-core contracts. S39 exact-size copy remains shaderless; S40 compiles
# SM5 bytecode offline with the selected SDK and embeds it in the private leaf.
concord_add_profiled_leaf(concord_d3d11_frame_presenter_win32 src/runtime/presentation/win32/D3D11FramePresenterWin32.cpp concord_runtime Runtime)
target_link_libraries(concord_d3d11_frame_presenter_win32 PRIVATE d3d11 dxgi dwmapi)
target_link_libraries(concord_runtime PRIVATE concord_d3d11_frame_presenter_win32)
if(NOT CONCORD_FXC_EXECUTABLE)
    if(CONCORD_WINDOWS_SDK_ROOT AND CONCORD_WINDOWS_SDK_VERSION)
        set(CONCORD_FXC_EXECUTABLE "${CONCORD_WINDOWS_SDK_ROOT}/bin/${CONCORD_WINDOWS_SDK_VERSION}/x64/fxc.exe" CACHE FILEPATH "Offline SDK shader compiler")
    else()
        # Preserve the explicit MSVC reference route as well. No IDE executable
        # or compiler DLL is shipped with the game; this is configure-time SDK discovery.
        get_filename_component(s40_sdk_root "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
        find_program(CONCORD_FXC_EXECUTABLE NAMES fxc.exe HINTS
            "${s40_sdk_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
            "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64" DOC "Existing offline SDK shader compiler")
    endif()
endif()
if(NOT EXISTS "${CONCORD_FXC_EXECUTABLE}")
    message(FATAL_ERROR "S40 needs an existing SDK fxc.exe; set CONCORD_FXC_EXECUTABLE. No installation or runtime compilation is performed.")
endif()
set(s40_shader "${PROJECT_SOURCE_DIR}/src/runtime/presentation/win32/MenuRaster.hlsl")
foreach(s40_stage IN ITEMS vs ps)
    set(s40_header "${CMAKE_CURRENT_BINARY_DIR}/s40-shaders/MenuRaster-${s40_stage}.h")
    add_custom_command(OUTPUT "${s40_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/s40-shaders"
        COMMAND "${CONCORD_FXC_EXECUTABLE}" /nologo /WX /Ges /O3 /T ${s40_stage}_5_0 /E ${s40_stage}
            /Fh "${s40_header}" /Vn s40_${s40_stage} "${s40_shader}"
        DEPENDS "${s40_shader}" "${CONCORD_FXC_EXECUTABLE}" VERBATIM)
    target_sources(concord_d3d11_frame_presenter_win32 PRIVATE "${s40_header}")
endforeach()
target_include_directories(concord_d3d11_frame_presenter_win32 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/s40-shaders")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_d3d11_frame_presenter_win32 PRIVATE /W4 /WX)
endif()
add_subdirectory(tests/infrastructure/d3d11_frame_presenter)
