#include "render/lighting/RuntimeLightingLookupRaster.h"
#include "render/lighting/RuntimeLightingSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

bool verify(const Lookup& lookup, std::size_t& cases) {
    std::uint32_t seed = 0x42ac62d1U;
    for (std::size_t offset = 0; offset < 4; ++offset) {
        for (std::size_t count : {0U, 1U, 2U, 3U, 4U, 5U, 7U, 8U, 13U, 16U, 255U, 256U, 257U, 1025U}) {
            std::vector<std::uint32_t> reference(count + 12), optimized(count + 12);
            for (std::size_t index = 0; index < reference.size(); ++index) {
                const auto value = static_cast<std::uint32_t>(index);
                reference[index] = optimized[index] = (randomValue(seed) & 0xff000000U)
                    | ((value & 255U) << 16U) | (((value * 73U) & 255U) << 8U) | ((value * 193U) & 255U);
            }
            runtimeApplyLightingLookupRow(reference.data() + offset, count,
                lookup.red, lookup.green, lookup.blue, lookup.raster, false);
            runtimeApplyLightingLookupRow(optimized.data() + offset, count,
                lookup.red, lookup.green, lookup.blue, lookup.raster, true);
            if (reference != optimized) return false;
            ++cases;
        }
    }
    return true;
}

void benchmark(const Lookup& lookup, const char* name) {
    constexpr std::size_t width = 2256, height = 1504, rounds = 60;
    std::vector<std::uint32_t> source(width * height);
    std::uint32_t seed = 0x49e829a3U;
    for (auto& pixel : source) pixel = randomValue(seed);
    const auto run = [&](bool accelerated) {
        auto pixels = source;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t round = 0; round < rounds; ++round)
            for (std::size_t y = 0; y < height; ++y)
                runtimeApplyLightingLookupRow(pixels.data() + y * width, width,
                    lookup.red, lookup.green, lookup.blue, lookup.raster, accelerated);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / rounds;
        std::uint64_t checksum = 0;
        for (const auto pixel : pixels) checksum = checksum * 31U + pixel;
        return std::make_pair(ms, checksum);
    };
    std::array<double, 3> original{}, optimized{};
    for (int pair = 0; pair < 3; ++pair) {
        const auto a = run(pair % 2 != 0);
        const auto b = run(pair % 2 == 0);
        if (a.second != b.second) { std::cerr << "benchmark pixels differ\n"; std::exit(2); }
        original[pair] = pair % 2 ? b.first : a.first;
        optimized[pair] = pair % 2 ? a.first : b.first;
    }
    std::sort(original.begin(), original.end()); std::sort(optimized.begin(), optimized.end());
    std::cout << name << " exact=" << lookup.raster.exact << " oldMs=" << original[1]
        << " newMs=" << optimized[1] << " ratio=" << optimized[1] / original[1] << '\n';
}
}

int main(int argc, char** argv) {
    MapLightingProfile mapProfile;
    mapProfile.ambient = 1.0; mapProfile.ground = 0.05;
    mapProfile.red = 1.0; mapProfile.green = 0.9; mapProfile.blue = 0.9;
    const auto mapTint = runtimeLightingTintForProfile(mapProfile);
    const Lookup live(mapTint);
    if (!live.raster.exact) { std::cerr << "live map tint did not qualify\n"; return 1; }
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
    for (const auto& tint : tints) {
        const Lookup lookup(tint);
        if (!verify(lookup, cases)) { std::cerr << "row pixels differ at tint " << exact + fallback << '\n'; return 1; }
        lookup.raster.exact ? ++exact : ++fallback;
    }
    // Reject tables that cannot be represented, independently of a tint's
    // source, instead of silently approximating a future nonlinear transform.
    Lookup impossible({1, 1, 1});
    impossible.red[121] = 0;
    impossible.raster = runtimeMakeLightingLookupRaster(impossible.red, impossible.green, impossible.blue);
    if (impossible.raster.exact || !verify(impossible, cases) || fallback == 0) return 1;

    // Every 24-bit RGB combination, with independently varying high bytes.
    std::vector<std::uint32_t> reference(1U << 24U), optimized(reference.size());
    for (std::size_t value = 0; value < reference.size(); ++value)
        reference[value] = optimized[value] = static_cast<std::uint32_t>(value)
            | (static_cast<std::uint32_t>(value * 73U) << 24U);
    runtimeApplyLightingLookupRow(reference.data(), reference.size(), live.red, live.green, live.blue, live.raster, false);
    runtimeApplyLightingLookupRow(optimized.data(), optimized.size(), live.red, live.green, live.blue, live.raster, true);
    if (reference != optimized) { std::cerr << "exhaustive RGB pixels differ\n"; return 1; }
    std::cout << "Lighting lookup: " << cases << " rows, " << exact << " exact / " << fallback
        << " fallback tints, all 16777216 RGB colors + alpha exact; padding/unaligned/tails pass.\n";
    if (argc > 1 && std::string_view(argv[1]) == "--benchmark") {
        benchmark(live, "live-map");
        benchmark(Lookup({0.5, 0.25, 0.75}), "mixed-channels");
        benchmark(Lookup({0.7, 0.9, 1.15}), "rounding-fallback");
    }
    return 0;
}
