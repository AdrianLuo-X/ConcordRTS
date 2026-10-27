# Same source, test name, warnings, CRT and floating point contract.
# Only unused default window/device libraries leave this pure test.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_executable(concord_auxiliary_animation_contract ../RuntimeAuxiliaryAnimationContract.cpp)
target_include_directories(concord_auxiliary_animation_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_auxiliary_animation_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()
