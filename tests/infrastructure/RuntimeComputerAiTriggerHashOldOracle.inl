#pragma once
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
// Frozen original private DTO, bit/canonical primitives and original mix fragment.
// Test-only reference: no production world, scheduler or simulation tick.
namespace original_ai_trigger_hash {
struct RuntimeCampaignAiTriggerDefinition {
    std::string id;
    std::string name;
    std::string primaryTeamId;
    std::string owner;
    int techLevel = 0;
    int conditionType = -1;
    std::string conditionObject;
    int conditionNumber = 0;
    int conditionComparator = 3;
    double currentWeight = 0.0;
    double minimumWeight = 0.0;
    double maximumWeight = 0.0;
    bool availableInSkirmish = false;
    bool baseDefense = false;
    int side = -1;
    int scope = 0;
    std::string secondaryTeamId;
    std::array<bool, 3> difficultyEnabled{{true, true, true}};
    bool enabled = true;
    bool valid = false;
    int timesExecuted = 0;
    int timesCompleted = 0;
    int timesFailed = 0;
    int nextEligibleTick = 0;
};
inline std::string trimAsciiLocal(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}
inline std::string toUpperAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character))
        );
    }
    return value;
}

inline std::uint64_t runtimeStableHashString(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char character : value) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline void runtimeStableHashMix(std::uint64_t& hash, long long value) {
    hash ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ull
        + (hash << 6)
        + (hash >> 2);
}

inline void runtimeStableHashMixString(
    std::uint64_t& hash,
    const std::string& value
) {
    runtimeStableHashMix(
        hash,
        static_cast<long long>(runtimeStableHashString(value))
    );
}

inline void runtimeMultiplayerAuthoritativeMixBool(
    std::uint64_t& hash,
    bool value
) {
    runtimeStableHashMix(hash, value ? 1 : 0);
}

inline void runtimeMultiplayerAuthoritativeMixUnsigned(
    std::uint64_t& hash,
    std::uint64_t value
) {
    runtimeStableHashMix(hash, static_cast<long long>(value));
}

inline void runtimeMultiplayerAuthoritativeMixDouble(
    std::uint64_t& hash,
    double value
) {
    constexpr std::uint64_t canonicalNan = 0x7ff8000000000000ull;
    const std::uint64_t bits = std::isnan(value)
        ? canonicalNan
        : value == 0.0
            ? 0ull
            : std::bit_cast<std::uint64_t>(value);
    runtimeMultiplayerAuthoritativeMixUnsigned(hash, bits);
}

inline std::string runtimeMultiplayerCanonicalIdentifier(
    const std::string& value
) {
    return toUpperAsciiLocal(trimAsciiLocal(value));
}
template <typename Definition>
inline void mix(std::uint64_t& hash, const std::vector<Definition>& definitions) {
    struct View { const std::vector<Definition>& computerAiTriggers; } triggers{definitions};
    runtimeStableHashMix(hash, static_cast<long long>(triggers.computerAiTriggers.size()));
    for (const auto& trigger : triggers.computerAiTriggers) {
        for (const auto* identifier : {&trigger.id, &trigger.primaryTeamId, &trigger.secondaryTeamId,
                &trigger.owner, &trigger.conditionObject})
            runtimeStableHashMixString(hash, runtimeMultiplayerCanonicalIdentifier(*identifier));
        for (int value : {trigger.techLevel, trigger.conditionType, trigger.conditionNumber,
                trigger.conditionComparator, trigger.side, trigger.scope}) runtimeStableHashMix(hash, value);
        for (double value : {trigger.currentWeight, trigger.minimumWeight, trigger.maximumWeight})
            runtimeMultiplayerAuthoritativeMixDouble(hash, value);
        for (bool value : {trigger.availableInSkirmish, trigger.baseDefense, trigger.enabled, trigger.valid})
            runtimeMultiplayerAuthoritativeMixBool(hash, value);
        for (bool value : trigger.difficultyEnabled) runtimeMultiplayerAuthoritativeMixBool(hash, value);
    }

}
}
