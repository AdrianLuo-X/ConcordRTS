# Same production path policy and archive reader, without the video device/IPC.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_library(concord_movie_path_sources OBJECT
    "${PROJECT_SOURCE_DIR}/src/runtime/campaign/RuntimeCampaignMovieResolver.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/video/BinkPlaybackPaths.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/MixArchive.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/WestwoodCrypto.cpp"
)
target_include_directories(concord_movie_path_sources PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_executable(concord_movie_path_contract
    "${PROJECT_SOURCE_DIR}/tests/assets/MoviePathContract.cpp"
    $<TARGET_OBJECTS:concord_movie_path_sources>)
target_include_directories(concord_movie_path_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_movie_path_contract PRIVATE /utf-8 /FS /W4 /WX /permissive-)
endif()
