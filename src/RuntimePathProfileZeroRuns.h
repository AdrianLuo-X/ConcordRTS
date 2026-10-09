#pragma once

#include "RuntimePathProfileHash.h"

inline constexpr std::uint64_t runtimePathProfileFnvPrimePower16 =
    runtimePathProfileFnvPrimePower8 * runtimePathProfileFnvPrimePower8;
inline constexpr std::uint64_t runtimePathProfileFnvPrimePower48 =
    runtimePathProfileFnvPrimePower16 * runtimePathProfileFnvPrimePower16
    * runtimePathProfileFnvPrimePower16;

// Caller proves water/shore/bridge/ramp/cliff == false, hazardCost == 0,
// and 1 <= cost <= 255 after the original normalization/duplicate reduction.
// All ten fields remain represented, in their original order. The passable
// int64 plus five zero int64 fields is exactly 48 bytes; the cost int64 plus
// the zero hazard int64 is exactly 16 bytes. Only XOR-zero runs are combined.
inline constexpr std::uint64_t runtimePathProfileHashFlatDry(
    std::uint64_t hash, std::int64_t key, int level, bool passable,
    int cost) noexcept {
    hash = runtimePathProfileHashFast(hash, key);
    hash = runtimePathProfileHashFast(hash, level);
    hash = (hash ^ (passable ? 1ULL : 0ULL)) * runtimePathProfileFnvPrimePower48;
    return (hash ^ static_cast<std::uint64_t>(cost)) * runtimePathProfileFnvPrimePower16;
}

inline thread_local int runtimePathProfileZeroRunsReferenceOverride = -1;
inline bool runtimePathProfileZeroRunsReference() {
    if (runtimePathProfileZeroRunsReferenceOverride >= 0)
        return runtimePathProfileZeroRunsReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_PROFILE_ZERO_RUNS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

// Construction selects the switch once. Later scope changes cannot alter an
// already constructed queue's profile, result/cache keys or owned terrain.
class RuntimePathProfileZeroRunsReferenceScope {
public:
    explicit RuntimePathProfileZeroRunsReferenceScope(bool reference)
        : previous_(runtimePathProfileZeroRunsReferenceOverride) {
        runtimePathProfileZeroRunsReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathProfileZeroRunsReferenceScope() {
        runtimePathProfileZeroRunsReferenceOverride = previous_;
    }
    RuntimePathProfileZeroRunsReferenceScope(const RuntimePathProfileZeroRunsReferenceScope&) = delete;
    RuntimePathProfileZeroRunsReferenceScope& operator=(const RuntimePathProfileZeroRunsReferenceScope&) = delete;
private:
    int previous_;
};
