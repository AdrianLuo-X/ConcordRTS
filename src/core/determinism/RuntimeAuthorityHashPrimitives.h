#pragma once
#include "core/io/RuntimeSaveTextFields.h"
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

// Exact original authority hash operations, shared once by their real callers.
namespace runtime_authority_hash {
using runtime_save_text::trimAsciiLocal;
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
}
