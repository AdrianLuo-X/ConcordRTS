include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_projectile_screen_query_native_contract ProjectileScreenQueryNativeContract.cpp)
    target_include_directories(concord_projectile_screen_query_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_projectile_screen_query_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_projectile_screen_query_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_projectile_screen_query_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    target_compile_definitions(concord_projectile_screen_query_native_contract PRIVATE CONCORD_PROJECTILE_SCREEN_STANDARD_VALUES=1)
    concord_runtime_abi_definition(concord_projectile_screen_query_native_contract)
    concord_runtime_crt_profile(concord_projectile_screen_query_native_contract)
    add_test(NAME projectile_screen_query_native_contract_test COMMAND concord_projectile_screen_query_native_contract)
    set_tests_properties(projectile_screen_query_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;projectile-screen-query;native-reference")
endif()









# Existing complete all-muzzles production diagnostic, unchanged scene/assertions.
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_test(NAME projectile_screen_query_all_muzzles_complete_test COMMAND concord_runtime
        --command-smoke-test --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
        --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    set_tests_properties(projectile_screen_query_all_muzzles_complete_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 1200
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_ALL_MUZZLES_ONLY=1"
        LABELS "windows;projectile-screen-query;original-complete-muzzles")
endif()

# The public value test is built on the real host compiler without native headers/libraries.
add_executable(concord_projectile_screen_query_value_contract ProjectileScreenQueryValueContract.cpp)
target_include_directories(concord_projectile_screen_query_value_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(concord_projectile_screen_query_value_contract PRIVATE cxx_std_20)
if(TARGET concord_runtime_match_score_contract)
    target_compile_options(concord_projectile_screen_query_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_projectile_screen_query_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
endif()
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_projectile_screen_query_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_projectile_screen_query_value_contract PRIVATE /utf-8 /W4 /WX /permissive-)
else()
    target_compile_options(concord_projectile_screen_query_value_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME projectile_screen_query_value_contract_test COMMAND concord_projectile_screen_query_value_contract)
set_tests_properties(projectile_screen_query_value_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;platform-values;projectile-screen-query;contract")
