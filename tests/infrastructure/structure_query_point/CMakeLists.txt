include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_structure_query_point_native_contract StructureQueryPointNativeContract.cpp)
    target_include_directories(concord_structure_query_point_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_structure_query_point_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_structure_query_point_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_structure_query_point_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    target_compile_definitions(concord_structure_query_point_native_contract PRIVATE CONCORD_STRUCTURE_QUERY_STANDARD_POINT=1)
    concord_runtime_abi_definition(concord_structure_query_point_native_contract)
    concord_runtime_crt_profile(concord_structure_query_point_native_contract)
    add_test(NAME structure_query_point_native_contract_test COMMAND concord_structure_query_point_native_contract)
    set_tests_properties(structure_query_point_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;structure-query-point;native-reference")
endif()











# The public value test is built on the real host compiler without native headers/libraries.
add_executable(concord_structure_query_point_value_contract StructureQueryPointValueContract.cpp)
target_include_directories(concord_structure_query_point_value_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(concord_structure_query_point_value_contract PRIVATE cxx_std_20)
if(TARGET concord_runtime_match_score_contract)
    target_compile_options(concord_structure_query_point_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_structure_query_point_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
endif()
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_structure_query_point_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_structure_query_point_value_contract PRIVATE /utf-8 /W4 /WX /permissive-)
else()
    target_compile_options(concord_structure_query_point_value_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME structure_query_point_value_contract_test COMMAND concord_structure_query_point_value_contract)
set_tests_properties(structure_query_point_value_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;platform-values;structure-query-point;contract")
