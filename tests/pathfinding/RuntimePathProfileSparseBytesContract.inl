#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>

inline bool runtimePathProfileSparseBytesArithmeticChecks() {
    // Independent literal byte oracle: no production hashing helper is called.
    const auto oracle = [](std::uint64_t hash, std::uint64_t bits) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (bits >> (byte * 8)) & 255U;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    constexpr std::array<std::uint64_t, 10> seeds{
        0, 1, 14695981039346656037ULL, 1099511628211ULL,
        std::numeric_limits<std::uint64_t>::max(), 0x0123456789ABCDEFULL,
        0xFEDCBA9876543210ULL, 0x8000000000000000ULL,
        0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL};
    std::size_t scalarChecks = 0, chainChecks = 0;
    const auto check = [&](std::uint64_t seed, std::uint64_t bits) {
        const auto value = std::bit_cast<std::int64_t>(bits);
        const auto expected = oracle(seed, bits);
        ++scalarChecks;
        if (runtimePathProfileHashOriginal(seed, value) == expected
            && runtimePathProfileHashC22(seed, value) == expected
            && runtimePathProfileHashFast(seed, value) == expected) return true;
        std::cerr << "sparse-byte scalar mismatch seed=" << seed
            << " bits=" << bits << " expected=" << expected << '\n';
        return false;
    };
    constexpr std::array<std::uint32_t, 13> coordinates{
        0, 1, 254, 255, 256, 257, 479, 65534, 65535, 65536,
        65537, 0x7FFFFFFFU, 0xFFFFFFFFU};
    for (auto seed : seeds) {
        for (std::uint64_t bits = 0; bits <= 65535; ++bits)
            if (!check(seed, bits)) return false;
        for (std::uint64_t y = 0; y <= 255; ++y)
            for (std::uint64_t x = 0; x <= 255; ++x)
                if (!check(seed, (y << 32) | x)) return false;
        for (auto y : coordinates) for (auto x : coordinates)
            if (!check(seed, (std::uint64_t{y} << 32) | x)) return false;
        // Nonzero bytes at every forbidden mask position must use an exact
        // formula or the original fallback, including negative bit patterns.
        for (int byte = 0; byte < 8; ++byte) {
            for (std::uint64_t value : {1ULL, 127ULL, 128ULL, 255ULL}) {
                const auto bits = value << (8 * byte);
                for (auto boundary : {bits - 1, bits, bits + 1, ~bits})
                    if (!check(seed, boundary)) return false;
            }
        }
        std::uint64_t random = seed ^ 0x9E3779B97F4A7C15ULL;
        auto expected = seed, original = seed, c22 = seed, sparse = seed;
        for (int i = 0; i < 16384; ++i) {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            std::uint64_t bits = random;
            switch (i % 8) {
            case 0: bits &= 255U; break;
            case 1: bits &= 65535U; break;
            case 2: bits &= 0x000000FF000000FFULL; break;
            case 3: bits &= 0x0000FFFF0000FFFFULL; break;
            case 4: bits = ~std::uint64_t{random & 255U}; break;
            case 5: bits &= 0x0000FFFF0000FFFFULL; bits |= 1ULL << 16; break;
            case 6: bits &= 0x0000FFFF0000FFFFULL; bits |= 1ULL << 48; break;
            default: break;
            }
            if (!check(seed, bits)) return false;
            const auto value = std::bit_cast<std::int64_t>(bits);
            expected = oracle(expected, bits);
            original = runtimePathProfileHashOriginal(original, value);
            c22 = runtimePathProfileHashC22(c22, value);
            sparse = runtimePathProfileHashFast(sparse, value);
            ++chainChecks;
            if (original != expected || c22 != expected || sparse != expected) {
                std::cerr << "sparse-byte chain mismatch index=" << i << '\n';
                return false;
            }
        }
    }
    // Use the actual native240 DXY authored domain, not a rectangular guess.
    // Hash all ten fields in the existing sorted-coordinate profile order.
    std::size_t nativeCells = 0, nativeBytePairs = 0;
    auto expected = seeds[2], original = seeds[2], c22 = seeds[2], sparse = seeds[2];
    const auto mix = [&](std::int64_t value) {
        const auto bits = static_cast<std::uint64_t>(value);
        expected = oracle(expected, bits);
        original = runtimePathProfileHashOriginal(original, value);
        c22 = runtimePathProfileHashC22(c22, value);
        sparse = runtimePathProfileHashFast(sparse, value);
        ++chainChecks;
        return original == expected && c22 == expected && sparse == expected;
    };
    for (std::uint64_t y = 1; y <= 480; ++y) {
        for (std::uint64_t x = 1; x <= 480; ++x) {
            const auto u = static_cast<int>(x) - static_cast<int>(y) + 240;
            const auto v = static_cast<int>(x) + static_cast<int>(y) - 240;
            if (u < 0 || u >= 480 || v < 1 || v >= 480) continue;
            ++nativeCells;
            if (x <= 255 && y <= 255) ++nativeBytePairs;
            const auto key = std::bit_cast<std::int64_t>((y << 32) | x);
            const auto level = static_cast<std::int64_t>(nativeCells % 9) - 4;
            if (!mix(key) || !mix(level)) return false;
            for (int field = 0; field < 6; ++field)
                if (!mix((nativeCells >> field) & 1U)) return false;
            if (!mix(nativeCells % 65536) || !mix(nativeCells % 257)) return false;
        }
    }
    if (nativeCells != 114960 || nativeBytePairs != 36120) return false;
    const int oldOverride = runtimePathProfileSparseBytesReferenceOverride;
    const bool oldReference = runtimePathProfileSparseBytesReference();
    const int oldOriginalOverride = runtimePathProfileHashReferenceOverride;
    const bool oldOriginalReference = runtimePathProfileHashReference();
    {
        RuntimePathProfileSparseBytesReferenceScope outer(false);
        RuntimePathProfileHashReferenceScope originalScope(true);
        if (runtimePathProfileSparseBytesReference() || !runtimePathProfileHashReference()) return false;
        {
            RuntimePathProfileSparseBytesReferenceScope inner(true);
            RuntimePathProfileHashReferenceScope c22Scope(false);
            if (!runtimePathProfileSparseBytesReference() || runtimePathProfileHashReference()) return false;
            {
                RuntimePathProfileSparseBytesReferenceScope innerAgain(false);
                if (runtimePathProfileSparseBytesReference()) return false;
            }
            if (!runtimePathProfileSparseBytesReference()) return false;
        }
        if (runtimePathProfileSparseBytesReference() || !runtimePathProfileHashReference()) return false;
    }
    if (runtimePathProfileSparseBytesReferenceOverride != oldOverride
        || runtimePathProfileSparseBytesReference() != oldReference
        || runtimePathProfileHashReferenceOverride != oldOriginalOverride
        || runtimePathProfileHashReference() != oldOriginalReference) return false;
    std::cout << "sparse-byte hash arithmetic passed scalarChecks=" << scalarChecks
        << " chainSteps=" << chainChecks << " nativeCells=" << nativeCells
        << " nativeBytePairs=" << nativeBytePairs << " nativeOtherWordPairs="
        << nativeCells - nativeBytePairs << " nativeProfile=" << expected
        << " literalEightByteOracle=yes originalC22Sparse=yes nestedIndependentFlags=yes\n";
    return true;
}
