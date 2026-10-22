# W009-11: standard keyboard policies use the existing ordinary contract profile.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Keyboard ingress contract requires its actual ordinary owner")
endif()
add_executable(concord_keyboard_ingress_contract
    tests/infrastructure/KeyboardIngressControllerContract.cpp
    src/ui/input/RuntimeKeyboardIngressController.cpp)
target_include_directories(concord_keyboard_ingress_contract PRIVATE src)
target_compile_features(concord_keyboard_ingress_contract PRIVATE cxx_std_20)
target_compile_options(concord_keyboard_ingress_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_keyboard_ingress_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_keyboard_ingress_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
