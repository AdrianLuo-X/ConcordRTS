include_guard(GLOBAL)
# A new isolated scope keeps the executor fixture free of CMake's default
# Windows business/device libraries. This does not replace any root include.
add_subdirectory(tests/infrastructure/task_executor)
# Link the library into the product build, without creating an executor or
# enabling a consumer. S35 owns the first production invocation and its config.
target_link_libraries(concord_runtime PRIVATE concord_bounded_task_executor)
