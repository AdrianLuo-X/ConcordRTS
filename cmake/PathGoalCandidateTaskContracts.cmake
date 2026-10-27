include_guard(GLOBAL)
add_subdirectory(tests/pathfinding/candidate_task)
target_link_libraries(concord_runtime PRIVATE concord_path_goal_candidate_task)
# These existing owners retain their own CRT/EH/FP/iterator profile.
foreach(owner IN ITEMS concord_rules_dump concord_path_request_queue_performance_smoke)
    target_sources(${owner} PRIVATE src/simulation/navigation/pathfinding/PathGoalCandidateTask.cpp src/core/tasks/BoundedTaskExecutor.cpp)
endforeach()
