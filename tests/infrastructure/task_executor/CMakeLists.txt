cmake_minimum_required(VERSION 3.20)

# This NEW target scope also works as a genuinely independent source project.
# The existing ten root includes and their shared scope/order are unchanged.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ConcordBoundedTaskExecutor LANGUAGES CXX)
    set(S34_STANDALONE ON)
endif()
get_filename_component(S34_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include("${S34_SOURCE_ROOT}/cmake/CompilerContract.cmake")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CONCORD_MSVC_ABI)
    # Scoped only to these new pure targets; do not inherit unused window/GDI,
    # audio, crypto or other business/device libraries from CMake's defaults.
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()

if(S34_STANDALONE)
    add_library(concord_bounded_task_executor STATIC
        "${S34_SOURCE_ROOT}/src/core/tasks/BoundedTaskExecutor.cpp")
else()
    include("${S34_SOURCE_ROOT}/cmake/ProfiledLeaf.cmake")
    concord_add_profiled_leaf(concord_bounded_task_executor
        "${S34_SOURCE_ROOT}/src/core/tasks/BoundedTaskExecutor.cpp" concord_runtime Runtime)
endif()
target_include_directories(concord_bounded_task_executor PRIVATE "${S34_SOURCE_ROOT}/src")
add_executable(concord_bounded_task_executor_contract BoundedTaskExecutorContract.cpp)
target_include_directories(concord_bounded_task_executor_contract PRIVATE "${S34_SOURCE_ROOT}/src")
target_link_libraries(concord_bounded_task_executor_contract PRIVATE concord_bounded_task_executor)
find_package(Threads REQUIRED)
target_link_libraries(concord_bounded_task_executor PRIVATE Threads::Threads)
foreach(s34_target IN ITEMS concord_bounded_task_executor concord_bounded_task_executor_contract)
    target_compile_features(${s34_target} PRIVATE cxx_std_20)
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${s34_target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
    endif()
    if(S34_STANDALONE)
        if(CONCORD_MSVC_ABI)
            target_compile_definitions(${s34_target} PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
        endif()
    elseif(s34_target STREQUAL "concord_bounded_task_executor_contract")
        target_compile_options(${s34_target} PRIVATE
            "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
        target_compile_definitions(${s34_target} PRIVATE
            "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    endif()
    if(CONCORD_MSVC_FRONTEND)
        concord_target_compile_options(${s34_target} PRIVATE /utf-8 /W4 /WX /permissive-)
    endif()
endforeach()
if(S34_STANDALONE)
    enable_testing()
    add_test(NAME bounded_task_executor_contract_test COMMAND concord_bounded_task_executor_contract)
    set_tests_properties(bounded_task_executor_contract_test PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE LABELS "no-assets;bounded-tasks;logic")
endif()
