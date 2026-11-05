# W006-01 private Windows diagnostic assembly; the production CPP is reused.
if(WIN32)
    add_executable(concord_runtime_atomic_file_win32_contract
        tests/infrastructure/RuntimeAtomicFileWin32Contract.cpp
        src/core/io/RuntimeAtomicFile.cpp)
    target_include_directories(concord_runtime_atomic_file_win32_contract PRIVATE src)
    if(CONCORD_MSVC_FRONTEND)
        concord_target_compile_options(concord_runtime_atomic_file_win32_contract PRIVATE /utf-8 /W4 /WX /permissive-
            "/FI${CMAKE_SOURCE_DIR}/tests/infrastructure/RuntimeAtomicFileWin32Hooks.h")
    else()
        concord_target_compile_options(concord_runtime_atomic_file_win32_contract PRIVATE
            -include "${CMAKE_SOURCE_DIR}/tests/infrastructure/RuntimeAtomicFileWin32Hooks.h")
    endif()
endif()
