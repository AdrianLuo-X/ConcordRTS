#pragma once
#include <array>
#include <cstdint>
#include <string>

// Original owned Internet UI entropy/projection values. Backend/service policy stays with its owner.
struct RuntimeInternetMultiplayerMenuEntropy {
    std::uint64_t queryTransactionId = 0U;
    std::uint64_t hostTransactionId = 0U;
    std::uint64_t joinTransactionId = 0U;
    std::uint64_t matchRequestId = 0U;
    std::uint64_t matchActivationTransactionId = 0U;
    std::uint64_t matchHostTransactionId = 0U;
    std::uint64_t matchJoinTransactionId = 0U;
    std::uint64_t roomGeneration = 0U;
    std::uint64_t gameplayRandomSeed = 0U;
    std::array<std::uint8_t, 16U> roomIdBytes{};

    bool operator==(
        const RuntimeInternetMultiplayerMenuEntropy&
    ) const = default;
};

struct RuntimeInternetMultiplayerRoomRow {
    std::string roomId;
    std::string text;
    bool joinable = false;

    bool operator==(
        const RuntimeInternetMultiplayerRoomRow&
    ) const = default;
};

struct RuntimeInternetMultiplayerRosterRow {
    std::string peerId;
    std::string text;
    bool local = false;
    bool kickable = false;

    bool operator==(
        const RuntimeInternetMultiplayerRosterRow&
    ) const = default;
};
