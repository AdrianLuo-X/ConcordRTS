#pragma once

#include <array>
#include <cstdint>
#include <string>

// Local presentation only: never saved or included in multiplayer authority.
struct RuntimeCreditAudioState {
    bool observed = false;
    std::string owner;
    int balance = 0;
    int pendingDirection = 0;
    std::uint64_t lastObservationMs = 0;
    std::uint64_t nextSoundMs = 0;
    std::uint64_t burstEndMs = 0;
    bool burstPlayed = false;
    bool samplesPrepared = false;
    std::string configuredPair;
    std::array<std::string, 2> sections{"CreditUp", "CreditDown"};
    std::array<int, 2> intervalMs{80, 80};
};
