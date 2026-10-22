# W009-06: one production controller, ordinary contract and loopback bridge owners.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract OR NOT TARGET concord_multiplayer_runtime_bridge_smoke)
    message(FATAL_ERROR "Planning factory key contracts require their actual ordinary owners")
endif()
add_executable(concord_planning_factory_key_contract
    tests/infrastructure/PlanningFactoryKeyControllerContract.cpp
    src/ui/input/RuntimePlanningFactoryKeyController.cpp)
add_executable(concord_planning_factory_key_bridge_smoke
    tests/multiplayer/RuntimePlanningFactoryKeyBridgeSmoke.cpp
    src/ui/input/RuntimePlanningFactoryKeyController.cpp
    src/network/lockstep/RuntimeMultiplayerPlayerIntent.cpp
    src/network/lockstep/RuntimeMultiplayerRuntimeBridge.cpp
    src/network/session/RuntimeMultiplayerLanSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/lockstep/RuntimeMultiplayerLockstep.cpp
    src/network/session/RuntimeLanTransport.cpp)
foreach(_w009_target concord_planning_factory_key_contract concord_planning_factory_key_bridge_smoke)
    if(_w009_target STREQUAL "concord_planning_factory_key_contract")
        set(_w009_owner concord_runtime_match_score_contract)
    else()
        set(_w009_owner concord_multiplayer_runtime_bridge_smoke)
    endif()
    target_include_directories(${_w009_target} PRIVATE src)
    target_compile_features(${_w009_target} PRIVATE cxx_std_20)
    target_compile_options(${_w009_target} PRIVATE
        "$<TARGET_GENEX_EVAL:${_w009_owner},$<TARGET_PROPERTY:${_w009_owner},COMPILE_OPTIONS>>")
    target_compile_definitions(${_w009_target} PRIVATE
        "$<TARGET_GENEX_EVAL:${_w009_owner},$<TARGET_PROPERTY:${_w009_owner},COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${_w009_target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()
if(WIN32)
    target_link_libraries(concord_planning_factory_key_bridge_smoke PRIVATE ws2_32)
endif()
# The existing MultiplayerIntegration pass adds the same protocol-owned map sources.
# The pure controller target has no window, renderer, device, or network links.
unset(_w009_target)
unset(_w009_owner)

