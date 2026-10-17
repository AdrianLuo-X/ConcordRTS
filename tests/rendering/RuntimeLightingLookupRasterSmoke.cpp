#include "render/lighting/RuntimeLightingLookupRaster.h"
#include "render/lighting/RuntimeLightingSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
struct Lookup {
    std::array<std::uint8_t, 256> red{}, green{}, blue{};
    RuntimeLightingLookupRaster raster;
    explicit Lookup(const RuntimeLightingTint& tint) {
        for (std::uint32_t value = 0; value < 256; ++value) {
            const auto color = runtimeApplyLightingTintToPackedDibColor(value * 0x010101U, tint);
            red[value] = static_cast<std::uint8_t>(color >> 16U);
            green[value] = static_cast<std::uint8_t>(color >> 8U);
            blue[value] = static_cast<std::uint8_t>(color);
        }
        raster = runtimeMakeLightingLookupRaster(red, green, blue);
    }
};

std::uint32_t randomValue(std::uint32_t& seed) {
    seed ^= seed << 13U; seed ^= seed >> 17U; seed ^= seed << 5U;
    return seed;
}

enum class Path : std::size_t { Scalar, Sse2, Avx2 };
constexpr std::array<Path, 3> paths{Path::Scalar, Path::Sse2, Path::Avx2};
const char* pathName(Path path) {
    switch (path) {
    case Path::Scalar: return "scalar";
    case Path::Sse2: return "sse2";
    case Path::Avx2: return "avx2";
    }
    return "unknown";
}
bool pathAvailable(Path path) {
    if (path == Path::Scalar) return true;
#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    if (path == Path::Sse2) return true;
#endif
#if defined(_MSC_VER) && defined(_M_X64)
    if (path == Path::Avx2) return runtimeLightingAvx2CpuSupported();
#endif
    return false;
}
struct LeafEvidence {
    std::uint64_t scalarPixels = 0, sse2Pixels = 0, avx2Pixels = 0;
    std::uint64_t sse2Calls = 0, avx2Calls = 0;
};

// Direct calls plus the returned prefix prove that the requested ISA leaf
// ran. This instrumentation belongs to the test, not the production hot loop.
bool applyLeaf(Path path, std::uint32_t* row, std::size_t count,
    const Lookup& lookup, LeafEvidence& evidence) {
    if (!pathAvailable(path)) return false;
    std::size_t prefix = 0;
    if (lookup.raster.exact) {
#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        if (path == Path::Sse2) {
            prefix = runtimeApplyLightingLookupSse2Leaf(row, count, lookup.raster);
            if (prefix != count - count % 4) return false;
            evidence.sse2Pixels += prefix;
            ++evidence.sse2Calls;
        }
#endif
#if defined(_MSC_VER) && defined(_M_X64)
        if (path == Path::Avx2) {
            prefix = runtimeApplyLightingLookupAvx2Leaf(row, count, lookup.raster);
            if (prefix != count - count % 8) return false;
            evidence.avx2Pixels += prefix;
            ++evidence.avx2Calls;
        }
#endif
    }
    if (prefix < count || path == Path::Scalar)
        runtimeApplyLightingLookupScalarLeaf(row + prefix, count - prefix,
            lookup.red, lookup.green, lookup.blue);
    evidence.scalarPixels += count - prefix;
    return true;
}

std::uint32_t expectedPixel(std::uint32_t pixel, const Lookup& lookup) {
    return (pixel & 0xff000000U)
        | (static_cast<std::uint32_t>(lookup.red[(pixel >> 16U) & 255U]) << 16U)
        | (static_cast<std::uint32_t>(lookup.green[(pixel >> 8U) & 255U]) << 8U)
        | static_cast<std::uint32_t>(lookup.blue[pixel & 255U]);
}

bool verifyFeatureMasks() {
    constexpr unsigned required = (1U << 26U) | (1U << 27U) | (1U << 28U);
    constexpr unsigned avx2 = 1U << 5U;
    if (!runtimeLightingAvx2FeatureBitsAllow(7U, required, 6U, avx2)) return false;
    std::size_t rejected = 0;
    for (unsigned maximumLeaf : {0U, 1U, 6U}) {
        if (runtimeLightingAvx2FeatureBitsAllow(maximumLeaf, required, 6U, avx2)) return false;
        ++rejected;
    }
    for (unsigned bit : {26U, 27U, 28U}) {
        if (runtimeLightingAvx2FeatureBitsAllow(7U, required & ~(1U << bit), 6U, avx2)) return false;
        ++rejected;
    }
    for (std::uint64_t xcr0 : {0ULL, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL}) {
        if (runtimeLightingAvx2FeatureBitsAllow(7U, required, xcr0, avx2)) return false;
        ++rejected;
    }
    if (runtimeLightingAvx2FeatureBitsAllow(7U, required, 6U, 0U)) return false;
    ++rejected;
    if (!runtimeLightingAvx2FeatureBitsAllow(0xffU, required | 0x123U,
        0xffffU, avx2 | 0x123U)) return false;
    std::cout << "safe-feature-gate rejected=" << rejected
        << " cases (CPUID leaf/XSAVE/OSXSAVE/AVX/XCR0-XMM/YMM/AVX2); no unsupported ISA forced\n";
    return true;
}

bool verify(const Lookup& lookup, std::size_t& cases, LeafEvidence& evidence,
    unsigned& alignmentMask) {
    std::uint32_t seed = 0x42ac62d1U;
    for (std::size_t offset = 0; offset < 8; ++offset) {
        for (std::size_t count : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U,
            10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 31U, 32U, 33U,
            255U, 256U, 257U, 1025U}) {
            std::vector<std::uint32_t> source(count + 16), reference(source.size());
            for (std::size_t index = 0; index < source.size(); ++index) {
                const auto value = static_cast<std::uint32_t>(index);
                source[index] = (randomValue(seed) & 0xff000000U)
                    | ((value & 255U) << 16U) | (((value * 73U) & 255U) << 8U) | ((value * 193U) & 255U);
            }
            reference = source;
            for (std::size_t index = offset; index < offset + count; ++index)
                reference[index] = expectedPixel(source[index], lookup);
            for (Path path : paths) {
                if (!pathAvailable(path)) continue;
                auto actual = source;
                alignmentMask |= 1U << ((reinterpret_cast<std::uintptr_t>(actual.data() + offset) & 31U) / 4U);
                if (!applyLeaf(path, actual.data() + offset, count, lookup, evidence)
                    || actual != reference) {
                    std::cerr << "leaf pixels/guards/prefix differ path=" << pathName(path)
                        << " count=" << count << " offset=" << offset << '\n';
                    return false;
                }
            }
            for (bool forceReference : {true, false}) {
                auto actual = source;
                RuntimeLightingAvx2ReferenceScope scope(forceReference);
                runtimeApplyLightingLookupRow(actual.data() + offset, count,
                    lookup.red, lookup.green, lookup.blue, lookup.raster, true);
                if (actual != reference) return false;
            }
            auto scalarDispatch = source;
            runtimeApplyLightingLookupRow(scalarDispatch.data() + offset, count,
                lookup.red, lookup.green, lookup.blue, lookup.raster, false);
            if (scalarDispatch != reference) return false;
            ++cases;
        }
    }
    return true;
}

bool verifyAllRgb(const Lookup& lookup, LeafEvidence& evidence) {
    constexpr std::size_t rgbCount = 1U << 24U, chunkSize = 1U << 16U;
    std::vector<std::uint32_t> source(chunkSize + 16), reference(source.size()), actual(source.size());
    for (std::size_t offset = 0; offset < 8; ++offset) {
        for (std::size_t base = 0; base < rgbCount; base += chunkSize) {
            std::fill(source.begin(), source.end(), 0xa5c37e19U);
            for (std::size_t index = 0; index < chunkSize; ++index) {
                const auto rgb = static_cast<std::uint32_t>(base + index);
                source[offset + index] = rgb | ((rgb * 73U + static_cast<std::uint32_t>(offset) * 31U) << 24U);
            }
            reference = source;
            for (std::size_t index = offset; index < offset + chunkSize; ++index)
                reference[index] = expectedPixel(source[index], lookup);
            for (Path path : paths) {
                if (!pathAvailable(path)) continue;
                actual = source;
                if (!applyLeaf(path, actual.data() + offset, chunkSize, lookup, evidence)
                    || actual != reference) {
                    std::cerr << "exhaustive RGB/alpha pixels differ path=" << pathName(path)
                        << " base=" << base << " offset=" << offset << '\n';
                    return false;
                }
            }
        }
    }
    // Also pair each possible alpha with channel endpoints, half ties and
    // saturated colors; RGB exhaustion above varies alpha independently.
    for (std::uint32_t rgb : {0U, 1U, 0x7f7f7fU, 0x808080U, 0xffffffU,
        0xff0000U, 0x00ff00U, 0x0000ffU, 0x123456U}) {
        std::array<std::uint32_t, 256> alphaSource{}, alphaExpected{};
        for (std::uint32_t alpha = 0; alpha < 256; ++alpha) {
            alphaSource[alpha] = rgb | (alpha << 24U);
            alphaExpected[alpha] = expectedPixel(alphaSource[alpha], lookup);
        }
        for (Path path : paths) {
            if (!pathAvailable(path)) continue;
            auto alphaActual = alphaSource;
            if (!applyLeaf(path, alphaActual.data(), alphaActual.size(), lookup, evidence)
                || alphaActual != alphaExpected) return false;
        }
    }
    return true;
}

std::uint64_t pixelChecksum(const std::vector<std::uint32_t>& pixels) {
    std::uint64_t checksum = 0;
    for (const auto pixel : pixels) checksum = checksum * 31U + pixel;
    return checksum;
}

// Availability and prefix correctness have already been checked outside the
// timed interval. Keep proof counters and the CPU probe out of kernel timing.
void applyBenchmarkLeaf(Path path, std::uint32_t* row, std::size_t count, const Lookup& lookup) {
    std::size_t prefix = 0;
    if (lookup.raster.exact) {
#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        if (path == Path::Sse2)
            prefix = runtimeApplyLightingLookupSse2Leaf(row, count, lookup.raster);
#endif
#if defined(_MSC_VER) && defined(_M_X64)
        if (path == Path::Avx2)
            prefix = runtimeApplyLightingLookupAvx2Leaf(row, count, lookup.raster);
#endif
    }
    if (prefix < count || path == Path::Scalar)
        runtimeApplyLightingLookupScalarLeaf(row + prefix, count - prefix,
            lookup.red, lookup.green, lookup.blue);
}

void benchmark(const Lookup& lookup, const char* name) {
    constexpr std::size_t width = 2256, height = 1504, rounds = 12, samples = 9;
    std::vector<std::uint32_t> source(width * height);
    std::uint32_t seed = 0x49e829a3U;
    for (auto& pixel : source) pixel = randomValue(seed);
    struct Sample { double ms; std::uint64_t checksum; };
    const auto run = [&](Path path, std::size_t frames) {
        if (!pathAvailable(path)) { std::cerr << "unsupported benchmark path\n"; std::exit(2); }
        std::vector<std::uint32_t> pixels(source.size());
        Sample result{};
        for (std::size_t round = 0; round < frames; ++round) {
            // All paths receive the same original image each frame. Reset
            // cost is outside the timed kernel interval.
            pixels = source;
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t y = 0; y < height; ++y)
                applyBenchmarkLeaf(path, pixels.data() + y * width, width, lookup);
            result.ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
        result.ms /= static_cast<double>(frames);
        result.checksum = pixelChecksum(pixels);
        return result;
    };
    std::array<std::array<double, samples>, paths.size()> timings{};
    std::uint64_t expectedChecksum = 0;
    bool haveChecksum = false;
    for (Path path : paths) {
        if (!pathAvailable(path)) {
            std::cout << "kernel-benchmark name=" << name << " path=" << pathName(path) << " result=unsupported\n";
            continue;
        }
        const auto warmup = run(path, 2);
        if (!haveChecksum) { expectedChecksum = warmup.checksum; haveChecksum = true; }
        if (warmup.checksum != expectedChecksum) {
            std::cerr << "benchmark warmup pixels differ\n";
            std::exit(2);
        }
    }
    for (std::size_t sample = 0; sample < samples; ++sample) {
        // Rotate and reverse the three paths to avoid a fixed thermal/cache
        // order favoring one compiler's chosen SIMD path.
        for (std::size_t turn = 0; turn < paths.size(); ++turn) {
            const auto index = (sample + (sample % 2 ? paths.size() - 1 - turn : turn)) % paths.size();
            const auto path = paths[index];
            if (!pathAvailable(path)) continue;
            const auto result = run(path, rounds);
            if (result.checksum != expectedChecksum) {
                std::cerr << "benchmark pixels differ\n";
                std::exit(2);
            }
            timings[index][sample] = result.ms;
            std::cout << "kernel-sample name=" << name << " path=" << pathName(path)
                << " effective=" << (lookup.raster.exact ? pathName(path) : "scalar-fallback")
                << " sample=" << sample + 1 << " ms=" << result.ms << " checksum=" << result.checksum
                << '\n';
        }
    }
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (!pathAvailable(paths[index])) continue;
        std::sort(timings[index].begin(), timings[index].end());
        std::cout << "kernel-summary name=" << name << " path=" << pathName(paths[index])
            << " exact=" << lookup.raster.exact << " width=" << width << " height=" << height
            << " rounds=" << rounds << " samples=" << samples
            << " medianMs=" << timings[index][samples / 2]
            << " minMs=" << timings[index].front() << " maxMs=" << timings[index].back()
            << " checksum=" << expectedChecksum << '\n';
    }
    if (pathAvailable(Path::Sse2) && pathAvailable(Path::Avx2) && lookup.raster.exact)
        std::cout << "kernel-ratio name=" << name << " avx2OverSse2="
            << timings[2][samples / 2] / timings[1][samples / 2] << '\n';
}
}

int main(int argc, char** argv) {
    std::cout << std::fixed << std::setprecision(6);
#if defined(__clang__)
    std::cout << "compiler=clang version=" << __clang_version__ << '\n';
#elif defined(_MSC_VER)
    std::cout << "compiler=msvc version=" << _MSC_FULL_VER << '\n';
#else
    std::cout << "compiler=other\n";
#endif
    for (Path path : paths)
        std::cout << "leaf-path path=" << pathName(path)
            << " result=" << (pathAvailable(path) ? "available" : "unsupported") << '\n';
    if (!verifyFeatureMasks()) { std::cerr << "safe feature mask gate failed\n"; return 1; }
    MapLightingProfile mapProfile;
    mapProfile.ambient = 1.0; mapProfile.ground = 0.05;
    mapProfile.red = 1.0; mapProfile.green = 0.9; mapProfile.blue = 0.9;
    const auto mapTint = runtimeLightingTintForProfile(mapProfile);
    const Lookup live(mapTint);
    if (!live.raster.exact) { std::cerr << "live map tint did not qualify\n"; return 1; }
    const Lookup identity({1, 1, 1});
    if (!identity.raster.exact || !identity.raster.identity) {
        std::cerr << "identity tint did not select identity path\n"; return 1;
    }
    std::vector<RuntimeLightingTint> tints{
        mapTint, {0, 0, 0}, {1, 1, 1}, {0.5, 0.25, 0.75}, {0.7, 0.9, 1.15},
        {1.05, 0.945, 0.945}, {16, 15.9, 2}, {-1, -0.0, 17},
        {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 0.3},
        {std::nextafter(1.0, 0.0), std::nextafter(1.0, 2.0), 1.0},
        {std::nextafter(0.5, 0.0), std::nextafter(0.5, 1.0), 0.5}
    };
    for (int value = 0; value <= 1600; ++value)
        tints.push_back({value / 100.0, ((value * 7) % 1601) / 100.0, ((value * 13) % 1601) / 100.0});
    std::size_t cases = 0, exact = 0, fallback = 0;
    LeafEvidence evidence;
    unsigned alignmentMask = 0;
    for (const auto& tint : tints) {
        const Lookup lookup(tint);
        if (!verify(lookup, cases, evidence, alignmentMask)) {
            std::cerr << "row pixels differ at tint " << exact + fallback << '\n'; return 1;
        }
        lookup.raster.exact ? ++exact : ++fallback;
    }
    // Reject tables that cannot be represented, independently of a tint's
    // source, instead of silently approximating a future nonlinear transform.
    Lookup impossible({1, 1, 1});
    impossible.red[121] = 0;
    impossible.raster = runtimeMakeLightingLookupRaster(impossible.red, impossible.green, impossible.blue);
    if (impossible.raster.exact || !verify(impossible, cases, evidence, alignmentMask) || fallback == 0) return 1;
    if (alignmentMask != 0xffU || !verifyAllRgb(live, evidence)) return 1;
    if (pathAvailable(Path::Sse2) && evidence.sse2Pixels == 0) return 1;
    if (pathAvailable(Path::Avx2) && evidence.avx2Pixels == 0) return 1;
    std::cout << "Lighting lookup: " << cases << " rows, " << exact << " exact / " << fallback
        << " fallback tints, all 16777216 RGB colors at each offset0..7 + all256alpha exact;"
        << " identity/nonexact/padding/32-byte alignment positions/tails/small sizes pass.\n";
    std::cout << "leaf-execution scalarPixels=" << evidence.scalarPixels
        << " sse2Pixels=" << evidence.sse2Pixels << " sse2Calls=" << evidence.sse2Calls
        << " avx2Pixels=" << evidence.avx2Pixels << " avx2Calls=" << evidence.avx2Calls
        << " alignmentMask=" << alignmentMask << '\n';
    if (argc > 1 && std::string_view(argv[1]) == "--benchmark") {
        benchmark(live, "live-map");
        benchmark(Lookup({0.5, 0.25, 0.75}), "mixed-channels");
        benchmark(Lookup({0.7, 0.9, 1.15}), "rounding-fallback");
    }
    return 0;
}
