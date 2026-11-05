include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

# A service-only Contract producer; no process machinery enters the game.
concord_add_profiled_leaf(concord_service_process_contract_leaf
    src/network/internet/RuntimeInternetServiceProcess.cpp
    concord_internet_service Contract)
target_link_libraries(concord_service_process_contract_leaf PRIVATE concord_internet_service_host_core)
target_link_libraries(concord_internet_service PRIVATE concord_service_process_contract_leaf)
if(WIN32)
    add_subdirectory(tests/internet/service_console)
endif()
