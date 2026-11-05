#pragma once
#include "network/lobby/RuntimeMultiplayerLobbyFlow.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Original network/result/projection declarations; native windows stay in adapters.
enum class RuntimeMultiplayerMenuNetwork : std::uint8_t {
    Lan,
    Internet
};

struct RuntimeMultiplayerDirectEndpoint {
    std::string host;
    std::uint16_t port = 0;

    bool operator==(const RuntimeMultiplayerDirectEndpoint&) const = default;
};

struct RuntimeMultiplayerHostSelectionProjection {
    RuntimeMultiplayerLobbyHostCreateOptions lobby;
    // Complete canonical slot image applied through LobbyFlow after host
    // creation. At least one non-host slot is always Closed for a remote peer.
    std::vector<RuntimeMultiplayerMatchSlot> slots;
    std::filesystem::path localMapPath;

    bool operator==(
        const RuntimeMultiplayerHostSelectionProjection&
    ) const = default;
};

struct RuntimeMultiplayerMenuResult {
    bool accepted = false;
    RuntimeMultiplayerLaunchConfigResult launchConfig;
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> bridge;
    std::string error;

    RuntimeMultiplayerMenuResult() = default;
    ~RuntimeMultiplayerMenuResult() = default;
    RuntimeMultiplayerMenuResult(RuntimeMultiplayerMenuResult&&) noexcept =
        default;
    RuntimeMultiplayerMenuResult& operator=(
        RuntimeMultiplayerMenuResult&&
    ) noexcept = default;
    RuntimeMultiplayerMenuResult(const RuntimeMultiplayerMenuResult&) = delete;
    RuntimeMultiplayerMenuResult& operator=(
        const RuntimeMultiplayerMenuResult&
    ) = delete;
};
