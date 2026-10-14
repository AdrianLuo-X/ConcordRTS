# The 32-bit retail Bink decoder is a required product dependency on Windows.
# VS retains its original Win32 sub-build. Ninja gets an independent x86 target.
if (WIN32 AND TARGET concord_runtime AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    include(ExternalProject)
    if(CMAKE_GENERATOR MATCHES "^Visual Studio " AND CONCORD_MSVC_ABI)
        ExternalProject_Add(
            concord_bink_decoder32
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools"
            BINARY_DIR "${CMAKE_BINARY_DIR}/bink_decoder32"
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            CMAKE_GENERATOR_PLATFORM Win32
            CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}"
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
                --config $<CONFIG> --parallel 1 --verbose
            BUILD_ALWAYS TRUE
            DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND "" INSTALL_COMMAND ""
        )
        set(_bink_product "${CMAKE_BINARY_DIR}/bink_decoder32/$<CONFIG>/BinkFrameExtractor.exe")
    elseif(CMAKE_GENERATOR STREQUAL "Ninja" AND
            CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CONCORD_MSVC_FRONTEND AND CONCORD_MSVC_ABI)
        ExternalProject_Add(
            concord_bink_decoder32
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools"
            BINARY_DIR "${CMAKE_BINARY_DIR}/bink_decoder32-x86"
            CMAKE_GENERATOR Ninja
            STEP_TARGETS configure
            CMAKE_ARGS
                "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/windows-clang-cl.cmake"
                "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
                "-DCONCORD_CPU_ARCH=x86"
                "-DCONCORD_LLVM_ROOT=${CONCORD_LLVM_ROOT}"
                "-DCONCORD_MSVC_ROOT=${CONCORD_MSVC_ROOT}"
                "-DCONCORD_WINDOWS_SDK_ROOT=${CONCORD_WINDOWS_SDK_ROOT}"
                "-DCONCORD_WINDOWS_SDK_VERSION=${CONCORD_WINDOWS_SDK_VERSION}"
                "-DCONCORD_NINJA_EXECUTABLE=${CONCORD_NINJA_EXECUTABLE}"
                "-DCONCORD_MSVC_COMPATIBILITY_VERSION=${CONCORD_MSVC_COMPATIBILITY_VERSION}"
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 1 --verbose
            BUILD_ALWAYS TRUE
            DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND "" INSTALL_COMMAND ""
        )
        set(_bink_product "${CMAKE_BINARY_DIR}/bink_decoder32-x86/BinkFrameExtractor.exe")
    else()
        message(FATAL_ERROR "Windows x64 product requires an explicit x86 Bink sub-build. Supported configurations: reference VS/MSVC ABI or selected Ninja/clang-cl toolchain")
    endif()
    add_custom_target(
        concord_bink_decoder32_stage
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_bink_product}" "$<TARGET_FILE_DIR:concord_runtime>/BinkFrameExtractor.exe"
        COMMENT "Staging the 32-bit Bink decoder bridge" VERBATIM
    )
    add_dependencies(concord_bink_decoder32_stage concord_bink_decoder32)
    add_dependencies(concord_runtime concord_bink_decoder32_stage)
    if (EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll"
        AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik")
        add_custom_command(
            TARGET concord_runtime POST_BUILD
            COMMAND "$<TARGET_FILE_DIR:concord_runtime>/BinkFrameExtractor.exe"
                "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll"
                "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik"
                "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l_frame.bmp" 1
            COMMENT "Refreshing the original Yuri shell first-frame fallback" VERBATIM
        )
    endif()
elseif (WIN32 AND TARGET concord_runtime AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(tools bink_decoder32_native)
    add_dependencies(concord_runtime BinkFrameExtractor)
endif()

