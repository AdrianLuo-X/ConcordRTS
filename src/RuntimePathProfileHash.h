#pragma once

#include <cstdint>
#include <cstdlib>

inline constexpr std::uint64_t runtimePathProfileFnvPrime = 1099511628211ULL;
inline constexpr std::uint64_t runtimePathProfileFnvPrimePower3 = [] {
    std::uint64_t value = 1;
    for (int i = 0; i < 3; ++i) value *= runtimePathProfileFnvPrime;
    return value;
}();
inline constexpr std::uint64_t runtimePathProfileFnvPrimePower4 =
    runtimePathProfileFnvPrimePower3 * runtimePathProfileFnvPrime;
inline constexpr std::uint64_t runtimePathProfileFnvPrimePower7 =
    runtimePathProfileFnvPrimePower3 * runtimePathProfileFnvPrimePower4;
inline constexpr std::uint64_t runtimePathProfileFnvPrimePower8 =
    runtimePathProfileFnvPrimePower4 * runtimePathProfileFnvPrimePower4;

// Exact original eight little-endian byte FNV steps, including sign extension.
inline constexpr std::uint64_t runtimePathProfileHashOriginal(
    std::uint64_t hash, std::int64_t value) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (bits >> (byte * 8)) & 255U;
        hash *= runtimePathProfileFnvPrime;
    }
    return hash;
}

// Retain the exact C22 shortcut for independent same-executable ablation.
inline constexpr std::uint64_t runtimePathProfileHashC22(
    std::uint64_t hash, std::int64_t value) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    return bits <= 255U ? (hash ^ bits) * runtimePathProfileFnvPrimePower8
        : runtimePathProfileHashOriginal(hash, value);
}

// Only combine consecutive XOR-zero byte steps, in unsigned modulo-2^64
// arithmetic. Byte order and every nonzero byte's XOR remain unchanged.
// Coordinate keys contain x in the low uint32 and y in the high uint32;
// the masks also prove the same algebra for arbitrary signed input values.
inline constexpr std::uint64_t runtimePathProfileHashFast(
    std::uint64_t hash, std::int64_t value) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    if (bits <= 255U) return (hash ^ bits) * runtimePathProfileFnvPrimePower8;
    if ((bits & 0xFFFFFF00FFFFFF00ULL) == 0) {
        hash = (hash ^ (bits & 255U)) * runtimePathProfileFnvPrimePower4;
        return (hash ^ ((bits >> 32) & 255U)) * runtimePathProfileFnvPrimePower4;
    }
    if (bits <= 65535U) {
        hash = (hash ^ (bits & 255U)) * runtimePathProfileFnvPrime;
        return (hash ^ ((bits >> 8) & 255U)) * runtimePathProfileFnvPrimePower7;
    }
    if ((bits & 0xFFFF0000FFFF0000ULL) == 0) {
        hash = (hash ^ (bits & 255U)) * runtimePathProfileFnvPrime;
        hash = (hash ^ ((bits >> 8) & 255U)) * runtimePathProfileFnvPrimePower3;
        hash = (hash ^ ((bits >> 32) & 255U)) * runtimePathProfileFnvPrime;
        return (hash ^ ((bits >> 40) & 255U)) * runtimePathProfileFnvPrimePower3;
    }
    return runtimePathProfileHashOriginal(hash, value);
}

inline thread_local int runtimePathProfileHashReferenceOverride = -1;
inline bool runtimePathProfileHashReference() {
    if (runtimePathProfileHashReferenceOverride >= 0)
        return runtimePathProfileHashReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_PROFILE_HASH_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathProfileHashReferenceScope {
public:
    explicit RuntimePathProfileHashReferenceScope(bool reference)
        : previous_(runtimePathProfileHashReferenceOverride) {
        runtimePathProfileHashReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathProfileHashReferenceScope() {
        runtimePathProfileHashReferenceOverride = previous_;
    }
    RuntimePathProfileHashReferenceScope(const RuntimePathProfileHashReferenceScope&) = delete;
    RuntimePathProfileHashReferenceScope& operator=(const RuntimePathProfileHashReferenceScope&) = delete;
private:
    int previous_;
};

// This independent switch restores C22, rather than forcing the original
// eight-byte algorithm. Queue construction reads each switch once.
inline thread_local int runtimePathProfileSparseBytesReferenceOverride = -1;
inline bool runtimePathProfileSparseBytesReference() {
    if (runtimePathProfileSparseBytesReferenceOverride >= 0)
        return runtimePathProfileSparseBytesReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_PROFILE_SPARSE_BYTES_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathProfileSparseBytesReferenceScope {
public:
    explicit RuntimePathProfileSparseBytesReferenceScope(bool reference)
        : previous_(runtimePathProfileSparseBytesReferenceOverride) {
        runtimePathProfileSparseBytesReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathProfileSparseBytesReferenceScope() {
        runtimePathProfileSparseBytesReferenceOverride = previous_;
    }
    RuntimePathProfileSparseBytesReferenceScope(const RuntimePathProfileSparseBytesReferenceScope&) = delete;
    RuntimePathProfileSparseBytesReferenceScope& operator=(const RuntimePathProfileSparseBytesReferenceScope&) = delete;
private:
    int previous_;
};
