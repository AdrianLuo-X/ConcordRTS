include("${CMAKE_SOURCE_DIR}/cmake/CompilerContract.cmake")
if(CONCORD_MSVC_ABI)
    set(CMAKE_CXX_STANDARD_LIBRARIES "kernel32.lib")
endif()
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
    add_executable(concord_lan_menu_controller_native_contract LanMenuControllerNativeContract.cpp "${CMAKE_SOURCE_DIR}/src/runtime/menu/RuntimeLanMenuController.cpp" "${CMAKE_SOURCE_DIR}/src/network/lobby/RuntimeMultiplayerLobbyModel.cpp" "${CMAKE_SOURCE_DIR}/src/network/protocol/RuntimeMultiplayerMatchSetup.cpp" "${CMAKE_SOURCE_DIR}/src/runtime/window/win32/RuntimeWindowParentWin32.cpp")
    target_include_directories(concord_lan_menu_controller_native_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(concord_lan_menu_controller_native_contract PRIVATE cxx_std_20)
    target_compile_options(concord_lan_menu_controller_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_lan_menu_controller_native_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime,$<TARGET_PROPERTY:concord_runtime,COMPILE_DEFINITIONS>>")
    concord_runtime_abi_definition(concord_lan_menu_controller_native_contract)
    concord_runtime_crt_profile(concord_lan_menu_controller_native_contract)
    target_link_libraries(concord_lan_menu_controller_native_contract PRIVATE user32)
    add_test(NAME lan_menu_controller_native_contract_test COMMAND concord_lan_menu_controller_native_contract)
    set_tests_properties(lan_menu_controller_native_contract_test PROPERTIES TIMEOUT 30 LABELS "windows;lan-menu-controller;native-reference")
endif()












# The public value test is built on the real host compiler without native headers/libraries.
add_executable(concord_lan_menu_controller_value_contract LanMenuControllerValueContract.cpp "${CMAKE_SOURCE_DIR}/src/network/lobby/RuntimeMultiplayerLobbyModel.cpp" "${CMAKE_SOURCE_DIR}/src/network/protocol/RuntimeMultiplayerMatchSetup.cpp" "${CMAKE_SOURCE_DIR}/src/runtime/menu/RuntimeLanMenuController.cpp")
target_include_directories(concord_lan_menu_controller_value_contract PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(concord_lan_menu_controller_value_contract PRIVATE cxx_std_20)
if(TARGET concord_runtime_match_score_contract)
    target_compile_options(concord_lan_menu_controller_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(concord_lan_menu_controller_value_contract PRIVATE "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
endif()
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_lan_menu_controller_value_contract PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_lan_menu_controller_value_contract PRIVATE /utf-8 /W4 /WX /permissive-)
else()
    target_compile_options(concord_lan_menu_controller_value_contract PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME lan_menu_controller_value_contract_test COMMAND concord_lan_menu_controller_value_contract)
set_tests_properties(lan_menu_controller_value_contract_test PROPERTIES TIMEOUT 30 LABELS "no-assets;platform-values;lan-menu-controller;contract")


if(MSVC OR CONCORD_MSVC_FRONTEND)
 target_compile_definitions(concord_lan_menu_controller_value_contract PRIVATE _CRT_SECURE_NO_WARNINGS)
 if(TARGET concord_lan_menu_controller_native_contract)
  target_compile_definitions(concord_lan_menu_controller_native_contract PRIVATE _CRT_SECURE_NO_WARNINGS)
 endif()
endif()
