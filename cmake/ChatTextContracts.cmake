# W011-01: one production controller, ordinary contract and loopback bridge owners.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract OR NOT TARGET concord_multiplayer_runtime_bridge_smoke)
    message(FATAL_ERROR "Reference communication contracts require their actual ordinary owners")
endif()
add_executable(concord_chat_text_controller_contract
    tests/infrastructure/ChatTextControllerContract.cpp
    src/ui/input/RuntimeChatTextController.cpp)
add_executable(concord_chat_text_bridge_smoke
    tests/multiplayer/RuntimeChatTextBridgeSmoke.cpp
    src/ui/input/RuntimeChatTextController.cpp
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
foreach(_w011_target concord_chat_text_controller_contract concord_chat_text_bridge_smoke)
    if(_w011_target STREQUAL "concord_chat_text_controller_contract")
        set(_w011_owner concord_runtime_match_score_contract)
    else()
        set(_w011_owner concord_multiplayer_runtime_bridge_smoke)
    endif()
    target_include_directories(${_w011_target} PRIVATE src)
    target_compile_features(${_w011_target} PRIVATE cxx_std_20)
    target_compile_options(${_w011_target} PRIVATE
        "$<TARGET_GENEX_EVAL:${_w011_owner},$<TARGET_PROPERTY:${_w011_owner},COMPILE_OPTIONS>>")
    target_compile_definitions(${_w011_target} PRIVATE
        "$<TARGET_GENEX_EVAL:${_w011_owner},$<TARGET_PROPERTY:${_w011_owner},COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${_w011_target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()
if(WIN32)
    target_link_libraries(concord_chat_text_bridge_smoke PRIVATE ws2_32)
endif()
# The existing MultiplayerIntegration pass adds the same protocol-owned map sources.
# The pure controller target has no window, renderer, device, or network links.
unset(_w011_target)
unset(_w011_owner)

