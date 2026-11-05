include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_body_anchor_query_native_contract BodyAnchorQueryNativeContract.cpp)
    target_include_directories(concord_body_anchor_query_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_body_anchor_query_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_body_anchor_query_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_body_anchor_query_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    target_compile_definitions(concord_body_anchor_query_native_contract PRIVATE CONCORD_BODY_ANCHOR_STANDARD_VALUES=1)
    concord_runtime_abi_definition(concord_body_anchor_query_native_contract)
    concord_runtime_crt_profile(concord_body_anchor_query_native_contract)
    add_test(NAME body_anchor_query_native_contract_test COMMAND concord_body_anchor_query_native_contract)
    set_tests_properties(body_anchor_query_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;body-anchor-query;native-reference")
endif()








# Register the existing early route containing the same three complete original
# missile probes. The older aggregate registration remains byte-for-byte intact.
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_test(NAME body_anchor_query_missile_complete_test COMMAND concord_runtime
        --command-smoke-test --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
        --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
        --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    set_tests_properties(body_anchor_query_missile_complete_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 900
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILE_LAUNCH_ONLY=1"
        LABELS "windows;body-anchor-query;original-complete-missile-probes")
endif()

# The public value test is built on the real host compiler without native headers/libraries.
add_executable(concord_body_anchor_query_value_contract BodyAnchorQueryValueContract.cpp)
target_include_directories(concord_body_anchor_query_value_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(concord_body_anchor_query_value_contract PRIVATE cxx_std_20)
if(TARGET concord_runtime_match_score_contract)
    target_compile_options(concord_body_anchor_query_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_body_anchor_query_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
endif()
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_body_anchor_query_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_body_anchor_query_value_contract PRIVATE /utf-8 /W4 /WX /permissive-)
else()
    target_compile_options(concord_body_anchor_query_value_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME body_anchor_query_value_contract_test COMMAND concord_body_anchor_query_value_contract)
set_tests_properties(body_anchor_query_value_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;platform-values;body-anchor-query;contract")
