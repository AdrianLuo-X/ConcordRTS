# New isolated pure target scope; the ten root include order is unchanged.
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
concord_add_profiled_leaf(concord_path_goal_candidate_task
    "${PROJECT_SOURCE_DIR}/src/simulation/navigation/pathfinding/PathGoalCandidateTask.cpp" concord_runtime Runtime)
# The product retains its original candidate producer; do not link it twice.
target_link_libraries(concord_path_goal_candidate_task PRIVATE concord_bounded_task_executor)
add_executable(concord_path_goal_candidate_task_contract PathGoalCandidateTaskContract.cpp
    "${PROJECT_SOURCE_DIR}/src/simulation/navigation/pathfinding/PathGoalCandidates.cpp")
target_include_directories(concord_path_goal_candidate_task_contract PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(concord_path_goal_candidate_task_contract PRIVATE concord_path_goal_candidate_task)
set_property(TARGET concord_path_goal_candidate_task_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_options(concord_path_goal_candidate_task_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
target_compile_definitions(concord_path_goal_candidate_task_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_FRONTEND)
    foreach(t IN ITEMS concord_path_goal_candidate_task concord_path_goal_candidate_task_contract)
        concord_target_compile_options(${t} PRIVATE /W4 /WX)
    endforeach()
endif()

add_executable(concord_path_goal_candidate_queue_differential
    ../PathGoalCandidateDifferential.cpp
    "${PROJECT_SOURCE_DIR}/src/simulation/navigation/pathfinding/PathGoalCandidates.cpp"
    "${PROJECT_SOURCE_DIR}/src/simulation/navigation/pathfinding/PathRequestQueue.cpp")
target_include_directories(concord_path_goal_candidate_queue_differential PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(concord_path_goal_candidate_queue_differential PRIVATE CONCORD_PATH_GOAL_CANDIDATE_TEST_OBSERVER
    "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
target_compile_options(concord_path_goal_candidate_queue_differential PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
set_property(TARGET concord_path_goal_candidate_queue_differential PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(concord_path_goal_candidate_queue_differential PRIVATE concord_path_goal_candidate_task concord_world_core_runtime_leaf)
