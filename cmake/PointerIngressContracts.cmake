# W010-03: runtime and the isolated contract compile both complete policy owners.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Pointer ingress contract requires its ordinary owner profile")
endif()
add_executable(concord_pointer_ingress_contract
    tests/infrastructure/PointerIngressControllerContract.cpp
    src/ui/input/RuntimePointerIngressController.cpp
    src/ui/sidebar/RuntimeSidebarPageController.cpp)
target_include_directories(concord_pointer_ingress_contract PRIVATE src)
target_compile_features(concord_pointer_ingress_contract PRIVATE cxx_std_20)
target_compile_options(concord_pointer_ingress_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_pointer_ingress_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_pointer_ingress_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
