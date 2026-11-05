if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
add_executable(concord_service_console_contract RuntimeServiceConsoleSmoke.cpp)
target_include_directories(concord_service_console_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(concord_service_console_contract PRIVATE cxx_std_20)
target_compile_options(concord_service_console_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_internet_service,$<TARGET_PROPERTY:concord_internet_service,COMPILE_OPTIONS>>")
target_compile_definitions(concord_service_console_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_internet_service,$<TARGET_PROPERTY:concord_internet_service,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_service_console_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_property(TARGET concord_service_console_contract PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(concord_service_console_contract PRIVATE concord_service_process_contract_leaf)
