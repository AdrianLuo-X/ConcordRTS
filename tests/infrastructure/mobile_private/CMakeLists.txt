include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_mobile_private_native_contract MobilePrivateNativeContract.cpp)
    target_include_directories(concord_mobile_private_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_mobile_private_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_mobile_private_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_mobile_private_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    concord_runtime_abi_definition(concord_mobile_private_native_contract)
    concord_runtime_crt_profile(concord_mobile_private_native_contract)
    add_test(NAME mobile_private_native_contract_test COMMAND concord_mobile_private_native_contract)
    set_tests_properties(mobile_private_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;mobile-private;native-reference")
endif()












