# This pure file/path fixture needs CRT/OS file support, not CMake's default
# GUI/shell libraries. Keep this variable local to the fixture directory.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_executable(concord_runtime_file_path_contract
    "${PROJECT_SOURCE_DIR}/tests/infrastructure/RuntimeFilePathContract.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/io/IniFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/config/RuntimeConfigPaths.cpp"
)
target_include_directories(concord_runtime_file_path_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_runtime_file_path_contract PRIVATE /utf-8 /FS /W4 /WX /permissive-)
endif()
