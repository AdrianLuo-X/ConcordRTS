#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CONCORD_LIGHTING_HAS_SSE2 1
#else
#define CONCORD_LIGHTING_HAS_SSE2 0
#endif

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#include <intrin.h>
#include <immintrin.h>
#include <cstdlib>
#define CONCORD_LIGHTING_HAS_PRIVATE_AVX2 1
#else
#define CONCORD_LIGHTING_HAS_PRIVATE_AVX2 0
#endif
// A coefficient is accepted only when every entry of the existing, rounded
// color lookup is reproduced exactly. Some floating-point half ties cannot
// share one coefficient; those tables retain the scalar lookup path.
struct RuntimeLightingLookupRaster {
    bool exact = false;
    bool identity = false;
    std::array<std::uint16_t, 8> fraction{};
    std::array<std::uint16_t, 8> whole{};
};

inline bool runtimeLightingExactCoefficient(
    const std::array<std::uint8_t, 256>& table, std::uint32_t& coefficient
) {
    if (table[0] != 0) return false;
    constexpr std::uint32_t scale = 65536;
    std::uint32_t minimum = 0, maximum = 16 * scale;
    for (std::uint32_t input = 1; input < 256; ++input) {
        const std::uint32_t output = table[input];
        const std::uint32_t lower = output == 0 ? 0 : output * scale - scale / 2;
        const std::uint32_t candidateMinimum = (lower + input - 1) / input;
        if (candidateMinimum > minimum) minimum = candidateMinimum;
        if (output < 255) {
            const std::uint32_t upper = ((output + 1) * scale - scale / 2 - 1) / input;
            if (upper < maximum) maximum = upper;
        }
        if (minimum > maximum) return false;
    }
    coefficient = minimum;
    // Keep the proof next to the use; this also protects later changes to the
    // interval solver or the accepted multiplier range.
    for (std::uint32_t input = 0; input < 256; ++input) {
        const auto transformed = (input * coefficient + scale / 2) / scale;
        if ((transformed < 255 ? transformed : 255) != table[input]) return false;
    }
    return true;
}

inline RuntimeLightingLookupRaster runtimeMakeLightingLookupRaster(
    const std::array<std::uint8_t, 256>& red,
    const std::array<std::uint8_t, 256>& green,
    const std::array<std::uint8_t, 256>& blue
) {
    RuntimeLightingLookupRaster result;
    std::uint32_t r = 0, g = 0, b = 0;
    result.exact = runtimeLightingExactCoefficient(red, r)
        && runtimeLightingExactCoefficient(green, g)
        && runtimeLightingExactCoefficient(blue, b);
    if (!result.exact) return result;
    result.identity = true;
    for (std::size_t value = 0; value < 256; ++value)
        result.identity = result.identity && red[value] == value
            && green[value] == value && blue[value] == value;
    const std::array<std::uint32_t, 4> channels{b, g, r, 65536};
    for (std::size_t lane = 0; lane < result.whole.size(); ++lane) {
        result.fraction[lane] = static_cast<std::uint16_t>(channels[lane % 4] & 65535U);
        result.whole[lane] = static_cast<std::uint16_t>(channels[lane % 4] >> 16U);
    }
    return result;
}

// A baseline ISA dispatcher. Never force an unsupported CPU ON in tests.
inline thread_local int runtimeLightingAvx2ReferenceOverride = -1;
inline bool runtimeLightingAvx2Reference() {
    if (runtimeLightingAvx2ReferenceOverride >= 0)
        return runtimeLightingAvx2ReferenceOverride != 0;
#if CONCORD_LIGHTING_HAS_PRIVATE_AVX2
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_LIGHTING_AVX2_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
#else
    return true;
#endif
}
class RuntimeLightingAvx2ReferenceScope {
    int previous_;
public:
    explicit RuntimeLightingAvx2ReferenceScope(bool reference) noexcept
        : previous_(runtimeLightingAvx2ReferenceOverride) {
        runtimeLightingAvx2ReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeLightingAvx2ReferenceScope() { runtimeLightingAvx2ReferenceOverride = previous_; }
    RuntimeLightingAvx2ReferenceScope(const RuntimeLightingAvx2ReferenceScope&) = delete;
    RuntimeLightingAvx2ReferenceScope& operator=(const RuntimeLightingAvx2ReferenceScope&) = delete;
};
inline bool runtimeLightingAvx2FeatureBitsAllow(unsigned maximumLeaf,
    unsigned leaf1Ecx, std::uint64_t xcr0, unsigned leaf7Ebx) noexcept {
    constexpr unsigned required = (1U << 26U) | (1U << 27U) | (1U << 28U);
    return maximumLeaf >= 7U && (leaf1Ecx & required) == required
        && (xcr0 & 6U) == 6U && (leaf7Ebx & (1U << 5U)) != 0;
}
#if CONCORD_LIGHTING_HAS_PRIVATE_AVX2
// No AVX intrinsic belongs here. XGETBV is issued only after CPU XSAVE,
// OSXSAVE and AVX are all present. Root must inspect the default-ISA object.
inline __declspec(noinline) bool runtimeLightingAvx2CpuSupported() noexcept {
    static const bool supported = [] {
        int registers[4]{};
        __cpuidex(registers, 0, 0);
        const unsigned maximumLeaf = static_cast<unsigned>(registers[0]);
        if (maximumLeaf < 7U) return false;
        __cpuidex(registers, 1, 0);
        const unsigned leaf1Ecx = static_cast<unsigned>(registers[2]);
        constexpr unsigned required = (1U << 26U) | (1U << 27U) | (1U << 28U);
        if ((leaf1Ecx & required) != required) return false;
        const std::uint64_t xcr0 = _xgetbv(0);
        if ((xcr0 & 6U) != 6U) return false;
        __cpuidex(registers, 7, 0);
        return runtimeLightingAvx2FeatureBitsAllow(maximumLeaf, leaf1Ecx,
            xcr0, static_cast<unsigned>(registers[1]));
    }();
    return supported;
}
// Explicit intrinsics only in this out-of-line leaf. No TU-wide /arch change.
inline __declspec(noinline) std::size_t runtimeApplyLightingLookupAvx2Leaf(
    std::uint32_t* row, std::size_t count, const RuntimeLightingLookupRaster& raster
) noexcept {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i fraction = _mm256_broadcastsi128_si256(_mm_loadu_si128(
        reinterpret_cast<const __m128i*>(raster.fraction.data())));
    const __m256i whole = _mm256_broadcastsi128_si256(_mm_loadu_si128(
        reinterpret_cast<const __m128i*>(raster.whole.data())));
    std::size_t index = 0;
    for (; count - index >= 8; index += 8) {
        const __m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + index));
        // Unpack/pack operate within each 128-bit lane: pixels0..3 and4..7
        // retain exactly their original order without a cross-lane permute.
        const __m256i lowChannels = _mm256_unpacklo_epi8(pixels, zero);
        const __m256i highChannels = _mm256_unpackhi_epi8(pixels, zero);
        const __m256i low = _mm256_add_epi16(_mm256_mullo_epi16(lowChannels, whole),
            _mm256_add_epi16(_mm256_mulhi_epu16(lowChannels, fraction),
                _mm256_srli_epi16(_mm256_mullo_epi16(lowChannels, fraction), 15)));
        const __m256i high = _mm256_add_epi16(_mm256_mullo_epi16(highChannels, whole),
            _mm256_add_epi16(_mm256_mulhi_epu16(highChannels, fraction),
                _mm256_srli_epi16(_mm256_mullo_epi16(highChannels, fraction), 15)));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(row + index), _mm256_packus_epi16(low, high));
    }
    _mm256_zeroupper();
    return index;
}
#endif
inline void runtimeApplyLightingLookupRow(
    std::uint32_t* row, std::size_t count,
    const std::array<std::uint8_t, 256>& red,
    const std::array<std::uint8_t, 256>& green,
    const std::array<std::uint8_t, 256>& blue,
    const RuntimeLightingLookupRaster& raster, bool allowAcceleration
) {
    std::size_t index = 0;
    if (allowAcceleration && raster.exact) {
        if (raster.identity) return;
#if CONCORD_LIGHTING_HAS_PRIVATE_AVX2
        if (count >= 8 && !runtimeLightingAvx2Reference() && runtimeLightingAvx2CpuSupported())
            index = runtimeApplyLightingLookupAvx2Leaf(row, count, raster);
#endif
#if CONCORD_LIGHTING_HAS_SSE2
        const __m128i zero = _mm_setzero_si128();
        const __m128i fraction = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(raster.fraction.data()));
        const __m128i whole = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(raster.whole.data()));
        const auto transform = [&](const __m128i channels) {
            const __m128i integral = _mm_mullo_epi16(channels, whole);
            const __m128i high = _mm_mulhi_epu16(channels, fraction);
            const __m128i round = _mm_srli_epi16(_mm_mullo_epi16(channels, fraction), 15);
            // Each result is at most 255*16, below signed 16-bit saturation.
            return _mm_add_epi16(integral, _mm_add_epi16(high, round));
        };
        for (; count - index >= 4; index += 4) {
            const __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + index));
            const __m128i low = transform(_mm_unpacklo_epi8(pixels, zero));
            const __m128i high = transform(_mm_unpackhi_epi8(pixels, zero));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(row + index), _mm_packus_epi16(low, high));
        }
#endif
    }
    for (; index < count; ++index) {
        const std::uint32_t pixel = row[index];
        row[index] = (pixel & 0xff000000U)
            | (static_cast<std::uint32_t>(red[(pixel >> 16U) & 255U]) << 16U)
            | (static_cast<std::uint32_t>(green[(pixel >> 8U) & 255U]) << 8U)
            | static_cast<std::uint32_t>(blue[pixel & 255U]);
    }
}

#undef CONCORD_LIGHTING_HAS_SSE2
#undef CONCORD_LIGHTING_HAS_PRIVATE_AVX2
