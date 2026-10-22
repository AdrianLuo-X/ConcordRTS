# S13-01: the ordinary test and runtime compile the identical production query .cpp.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Navigation terrain contract requires its existing ordinary owner")
endif()
add_executable(concord_navigation_terrain_query_contract
    tests/infrastructure/NavigationTerrainQueryContract.cpp
    src/simulation/navigation/terrain/RuntimeNavigationTerrainQuery.cpp)
target_include_directories(concord_navigation_terrain_query_contract PRIVATE src)
target_compile_features(concord_navigation_terrain_query_contract PRIVATE cxx_std_20)
target_compile_options(concord_navigation_terrain_query_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_navigation_terrain_query_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_navigation_terrain_query_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
