include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_campaign_menu_controller_native_contract CampaignMenuControllerNativeContract.cpp "${CMAKE_SOURCE_DIR}/src/runtime/menu/RuntimeCampaignMenuController.cpp" "${CMAKE_SOURCE_DIR}/src/data/campaign/CampaignPresentationDecoder.cpp" "${CMAKE_SOURCE_DIR}/src/runtime/window/win32/RuntimeWindowParentWin32.cpp")
    target_include_directories(concord_campaign_menu_controller_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_campaign_menu_controller_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_campaign_menu_controller_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_campaign_menu_controller_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    concord_runtime_abi_definition(concord_campaign_menu_controller_native_contract)
    concord_runtime_crt_profile(concord_campaign_menu_controller_native_contract)
    target_link_libraries(concord_campaign_menu_controller_native_contract PRIVATE user32)
    add_test(NAME campaign_menu_controller_native_contract_test COMMAND concord_campaign_menu_controller_native_contract)
    set_tests_properties(campaign_menu_controller_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;campaign-menu-controller;native-reference")
endif()












# The public value test is built on the real host compiler without native headers/libraries.
add_executable(concord_campaign_menu_controller_value_contract CampaignMenuControllerValueContract.cpp "${CMAKE_SOURCE_DIR}/src/data/campaign/CampaignPresentationDecoder.cpp" "${CMAKE_SOURCE_DIR}/src/runtime/menu/RuntimeCampaignMenuController.cpp")
target_include_directories(concord_campaign_menu_controller_value_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(concord_campaign_menu_controller_value_contract PRIVATE cxx_std_20)
if(TARGET concord_runtime_match_score_contract)
    target_compile_options(concord_campaign_menu_controller_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_campaign_menu_controller_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
endif()
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_campaign_menu_controller_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_campaign_menu_controller_value_contract PRIVATE /utf-8 /W4 /WX /permissive-)
else()
    target_compile_options(concord_campaign_menu_controller_value_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME campaign_menu_controller_value_contract_test COMMAND concord_campaign_menu_controller_value_contract)
set_tests_properties(campaign_menu_controller_value_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;platform-values;campaign-menu-controller;contract")

if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_test(NAME runtime_campaign_host_lifecycle_smoke_test COMMAND concord_runtime
        --campaign-ui-smoke-test "${CMAKE_BINARY_DIR}/campaign-host-lifecycle.bmp"
        --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
        --local-data-root "${CMAKE_SOURCE_DIR}/local_data")
    set_tests_properties(runtime_campaign_host_lifecycle_smoke_test PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE LABELS "assets;campaign-menu;native-lifecycle"
        ENVIRONMENT "CONCORD_CAMPAIGN_HOST_LIFECYCLE_ONLY=1;CONCORD_CAMPAIGN_SMOKE_RETAIN=1")
endif()

if(MSVC OR CONCORD_MSVC_FRONTEND)
    target_compile_definitions(concord_campaign_menu_controller_value_contract PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(TARGET concord_campaign_menu_controller_native_contract)
        target_compile_definitions(concord_campaign_menu_controller_native_contract PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()
