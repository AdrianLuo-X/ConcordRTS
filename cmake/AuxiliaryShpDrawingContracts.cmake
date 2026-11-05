# W02003 standard auxiliary SHP ABI and private Native profile probes.
include_guard(GLOBAL)
add_executable(concord_auxiliary_shp_drawing_value_contract tests/infrastructure/auxiliary_shp_drawing/AuxiliaryShpDrawingValueContract.cpp)
target_include_directories(concord_auxiliary_shp_drawing_value_contract PRIVATE src)
target_compile_features(concord_auxiliary_shp_drawing_value_contract PRIVATE cxx_std_20)
target_compile_options(concord_auxiliary_shp_drawing_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_auxiliary_shp_drawing_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
 set_property(TARGET concord_auxiliary_shp_drawing_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(WIN32)
 concord_add_profiled_leaf(concord_auxiliary_shp_drawing_runtime_probe tools/diagnostics/p12/native/AuxiliaryShpDrawingWin32CompileProbe.cpp concord_runtime Runtime)
 concord_add_profiled_leaf(concord_auxiliary_shp_drawing_contract_probe tools/diagnostics/p12/native/AuxiliaryShpDrawingWin32CompileProbe.cpp concord_runtime_match_score_contract Contract)
endif()
if(CONCORD_MSVC_ABI)
 target_compile_definitions(concord_auxiliary_shp_drawing_value_contract PRIVATE _CRT_SECURE_NO_WARNINGS)
 if(TARGET concord_auxiliary_shp_drawing_runtime_probe)
  target_compile_definitions(concord_auxiliary_shp_drawing_runtime_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(concord_auxiliary_shp_drawing_contract_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
 endif()
endif()
add_test(NAME auxiliary_shp_drawing_value_contract_test COMMAND concord_auxiliary_shp_drawing_value_contract)
set_tests_properties(auxiliary_shp_drawing_value_contract_test PROPERTIES TIMEOUT 30 LABELS "standard;auxiliary-shp;interface;ABI")

if(WIN32)
 add_test(NAME auxiliary_shp_drawing_native_contract_test
  COMMAND concord_runtime --command-smoke-test --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
   --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
   --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
 set_tests_properties(auxiliary_shp_drawing_native_contract_test PROPERTIES
  ENVIRONMENT "CONCORD_AUXILIARY_SHP_DRAWING_CONTRACT=1"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 300 RUN_SERIAL TRUE
  LABELS "windows;auxiliary-shp;original-native-reference;production-port")
endif()
