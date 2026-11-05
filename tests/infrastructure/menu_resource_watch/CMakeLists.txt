# The production watch owner is tested without menus, graphics, audio or IPC.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_executable(concord_menu_resource_watch_contract
    "${PROJECT_SOURCE_DIR}/tests/infrastructure/MenuResourceWatchContract.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/resources/RuntimeMenuResourceWatch.cpp")
target_include_directories(concord_menu_resource_watch_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_menu_resource_watch_contract PRIVATE /utf-8 /FS /W4 /WX /permissive-)
endif()
