# Real archive/search/INI production sources, with only CRT/kernel file support.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_library(concord_runtime_resource_path_sources OBJECT
    "${PROJECT_SOURCE_DIR}/src/runtime/campaign/RuntimeCampaignDataSource.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/io/IniFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/MixArchive.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/WestwoodCrypto.cpp"
)
target_include_directories(concord_runtime_resource_path_sources PRIVATE "${PROJECT_SOURCE_DIR}/src")
# Keep production sources on the existing campaign-data-source target profile;
# its getenv deprecation remains visible. The new fixture itself is /W4 /WX.
add_executable(concord_runtime_resource_path_contract
    "${PROJECT_SOURCE_DIR}/tests/infrastructure/RuntimeResourcePathContract.cpp"
    $<TARGET_OBJECTS:concord_runtime_resource_path_sources>
)
target_include_directories(concord_runtime_resource_path_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_runtime_resource_path_contract PRIVATE /utf-8 /FS /W4 /WX /permissive-)
endif()
