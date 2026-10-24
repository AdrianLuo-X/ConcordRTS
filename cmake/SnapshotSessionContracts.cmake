# H043: explicit existing production session consumers, snapshot-only module.
foreach(_snapshot_session_owner IN ITEMS
    concord_chat_text_bridge_smoke
    concord_internet_lobby_flow_smoke
    concord_internet_service_lobby_e2e
    concord_internet_tcp_tunnel_proxy_smoke
    concord_multiplayer_lan_flow_smoke
    concord_multiplayer_lobby_flow_smoke
    concord_multiplayer_runtime_bridge_smoke
    concord_multiplayer_two_process_smoke
    concord_planning_factory_key_bridge_smoke
    concord_reference_communication_bridge_smoke
    concord_runtime
    concord_selection_key_bridge_smoke)
    target_sources(${_snapshot_session_owner} PRIVATE src/network/snapshots/RuntimeMultiplayerSnapshotSession.cpp)
endforeach()

add_executable(concord_snapshot_session_contract
    tests/multiplayer/RuntimeMultiplayerSnapshotSessionContract.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotSession.cpp
    src/network/protocol/RuntimeMultiplayerProtocol.cpp
    src/network/session/RuntimeMultiplayerSessionSystem.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp
    src/network/lockstep/RuntimeMultiplayerGameplayCommand.cpp
    src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp
    src/network/maps/RuntimeMultiplayerMapTransfer.cpp
    src/network/maps/RuntimeMultiplayerCompatibilityFingerprint.cpp)
target_include_directories(concord_snapshot_session_contract PRIVATE src)
target_compile_features(concord_snapshot_session_contract PRIVATE cxx_std_20)
if(CONCORD_MSVC_FRONTEND)
    concord_target_compile_options(concord_snapshot_session_contract PRIVATE /utf-8 /W4 /WX /permissive-)
endif()
