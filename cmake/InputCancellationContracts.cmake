# W012-01: the isolated contract and production use the same capture owner.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "Input cancellation contract requires ordinary owner profile")
endif()
add_executable(concord_input_cancellation_contract
    tests/infrastructure/InputCancellationControllerContract.cpp
    src/ui/input/RuntimeInputCancellationController.cpp
    src/ui/input/RuntimeMapRightPointerController.cpp
    src/ui/input/RuntimeKeyboardRouterController.cpp)
target_include_directories(concord_input_cancellation_contract PRIVATE src)
target_compile_features(concord_input_cancellation_contract PRIVATE cxx_std_20)
target_compile_options(concord_input_cancellation_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
target_compile_definitions(concord_input_cancellation_contract PRIVATE
    "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
if(CONCORD_MSVC_ABI)
    set_property(TARGET concord_input_cancellation_contract PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
