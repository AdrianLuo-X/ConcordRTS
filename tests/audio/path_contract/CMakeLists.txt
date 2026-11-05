# Same production resolver/decoder/archive/INI sources, no device or Shell.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_library(concord_audio_path_sources OBJECT
    "${PROJECT_SOURCE_DIR}/src/assets/audio/AudioSampleResolver.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/audio/AudioDecoder.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/io/IniFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/MixArchive.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/archive/WestwoodCrypto.cpp"
)
target_include_directories(concord_audio_path_sources PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_executable(concord_audio_path_contract
    "${PROJECT_SOURCE_DIR}/tests/audio/AudioPathContract.cpp"
    $<TARGET_OBJECTS:concord_audio_path_sources>
)
target_include_directories(concord_audio_path_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_audio_path_contract PRIVATE /utf-8 /FS /W4 /WX /permissive-)
endif()
