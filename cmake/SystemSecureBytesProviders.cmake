include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

set(_concord_secure_profiles contract)
if(TARGET concord_runtime)
    concord_add_profiled_leaf(concord_secure_bytes_runtime_leaf
        src/core/security/RuntimeSecureBytes.cpp concord_runtime Runtime)
    target_link_libraries(concord_runtime PRIVATE concord_secure_bytes_runtime_leaf)
    list(PREPEND _concord_secure_profiles runtime)
endif()
concord_add_profiled_leaf(concord_secure_bytes_contract_leaf
    src/core/security/RuntimeSecureBytes.cpp concord_lan_transport_smoke Contract)

# Keep fixture defaults local; never alter game or other target link flags.
add_subdirectory(tests/infrastructure/system_secure_bytes)

# Original Internet helper fixture keeps its own Contract CRT profile.
if(TARGET concord_internet_multiplayer_menu_smoke)
    target_link_libraries(concord_internet_multiplayer_menu_smoke PRIVATE concord_secure_bytes_contract_leaf)
endif()

# LAN helper shares the canonical Contract producer, its Runtime uses Runtime leaf.
if(TARGET concord_multiplayer_menu_smoke)
    target_link_libraries(concord_multiplayer_menu_smoke PRIVATE concord_secure_bytes_contract_leaf)
endif()

# Service owners keep their original Contract profile; game already uses Runtime.
foreach(_secure_service_owner concord_internet_service_transport_smoke concord_internet_service_host_core)
    if(TARGET ${_secure_service_owner})
        target_link_libraries(${_secure_service_owner} PRIVATE concord_secure_bytes_contract_leaf)
    endif()
endforeach()

# Original Tunnel oracle keeps its Contract profile; game andHostCore alreadybound.
if(TARGET concord_internet_tcp_tunnel_proxy_smoke)
    target_link_libraries(concord_internet_tcp_tunnel_proxy_smoke PRIVATE concord_secure_bytes_contract_leaf)
endif()

# ServiceMain's byte adapter is owned by the original service Contract profile.
if(TARGET concord_internet_service)
    concord_add_profiled_leaf(concord_service_random_bytes_contract_leaf
        src/network/internet/private/RuntimeInternetServiceRandomBytes.cpp
        concord_internet_service Contract)
    target_link_libraries(concord_service_random_bytes_contract_leaf PRIVATE concord_secure_bytes_contract_leaf)
    target_link_libraries(concord_internet_service PRIVATE concord_service_random_bytes_contract_leaf)
    add_subdirectory(tests/internet/service_random_bytes)
endif()

# Original LAN-session Contract consumers reuse the same OS-byte producer.
foreach(_lan_noise_owner
    concord_multiplayer_lan_flow_smoke
    concord_multiplayer_runtime_bridge_smoke
    concord_multiplayer_two_process_smoke
    concord_multiplayer_lobby_flow_smoke
    concord_internet_lobby_flow_smoke
    concord_internet_tcp_tunnel_proxy_smoke
    concord_internet_service_lobby_e2e
    concord_selection_key_bridge_smoke
    concord_reference_communication_bridge_smoke
    concord_planning_factory_key_bridge_smoke
    concord_chat_text_bridge_smoke
)
    if(TARGET ${_lan_noise_owner})
        target_link_libraries(${_lan_noise_owner} PRIVATE concord_secure_bytes_contract_leaf)
    endif()
endforeach()
