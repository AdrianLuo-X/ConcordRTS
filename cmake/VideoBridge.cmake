# The retail Bink 1.0q decoder is 32-bit, while the normal runtime build is
# 64-bit.  Build the small decoder bridge with a Win32 sub-build and place it
# beside concord_runtime; the menu exchanges BGRA frames through shared memory.
if (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    include(ExternalProject)
    ExternalProject_Add(
        concord_bink_decoder32
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools"
        BINARY_DIR "${CMAKE_BINARY_DIR}/bink_decoder32"
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        CMAKE_GENERATOR_PLATFORM Win32
        BUILD_ALWAYS TRUE
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        INSTALL_COMMAND ""
    )
    # ExternalProject has its own build stamps.  Stage the helper through an
    # always-run target so a decoder-only source edit cannot leave the copy
    # beside concord_runtime stale when the 64-bit executable does not relink.
    add_custom_target(
        concord_bink_decoder32_stage
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/bink_decoder32/$<CONFIG>/BinkFrameExtractor.exe"
            "${CMAKE_BINARY_DIR}/$<CONFIG>/BinkFrameExtractor.exe"
        COMMENT "Staging the 32-bit Bink decoder bridge"
        VERBATIM
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
                "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l_frame.bmp"
                1
            COMMENT "Refreshing the original Yuri shell first-frame fallback"
            VERBATIM
        )
    endif()
elseif (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(tools bink_decoder32_native)
    add_dependencies(concord_runtime BinkFrameExtractor)
endif()

# Bump when shared rules structs change so every translation unit is rebuilt
