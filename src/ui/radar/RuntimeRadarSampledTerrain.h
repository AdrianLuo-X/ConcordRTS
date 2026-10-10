#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <unordered_set>

inline thread_local int runtimeRadarSampledTerrainReferenceOverride = -1;
inline bool runtimeRadarSampledTerrainReference() {
    if (runtimeRadarSampledTerrainReferenceOverride >= 0)
        return runtimeRadarSampledTerrainReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_RADAR_SAMPLED_TERRAIN_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeRadarSampledTerrainReferenceScope {
    int previous_;
public:
    explicit RuntimeRadarSampledTerrainReferenceScope(bool reference)
        : previous_(runtimeRadarSampledTerrainReferenceOverride) {
        runtimeRadarSampledTerrainReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeRadarSampledTerrainReferenceScope() {
        runtimeRadarSampledTerrainReferenceOverride = previous_;
    }
    RuntimeRadarSampledTerrainReferenceScope(const RuntimeRadarSampledTerrainReferenceScope&) = delete;
    RuntimeRadarSampledTerrainReferenceScope& operator=(const RuntimeRadarSampledTerrainReferenceScope&) = delete;
};

// One radar rebuild only. Inputs are the original runtimeCellKey values, so
// signed/noncanonical aliases retain the exact original lookup semantics.
// Failure means ALL keys are wanted and the producer takes its old full loop.
class RuntimeRadarSampledTerrainKeys {
    static constexpr std::size_t side = 512;
    using Bits = std::array<std::uint64_t, side * side / 64>;
    std::unique_ptr<Bits> dense_;
    std::unique_ptr<std::unordered_set<int>> sparse_;
    bool active_ = false;
    std::size_t count_ = 0;
    static std::size_t denseIndex(int key) noexcept {
        if (key < 0) return side * side;
        const unsigned x = static_cast<unsigned>(key) % 1000U;
        const unsigned y = static_cast<unsigned>(key) / 1000U;
        return x < side && y < side ? static_cast<std::size_t>(y) * side + x : side * side;
    }
public:
    static constexpr std::uint64_t maximumRasterPixels = 65536;
    bool begin(std::int64_t width, std::int64_t height,
        double scaleX, double scaleY, bool reference = runtimeRadarSampledTerrainReference()) {
        disable();
        if (reference || width <= 0 || height <= 0
            || static_cast<std::uint64_t>(width) > maximumRasterPixels
            || static_cast<std::uint64_t>(height) > maximumRasterPixels
            || static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > maximumRasterPixels
            || !(scaleX > 0.0) || !(scaleY > 0.0)
            || !std::isfinite(scaleX) || !std::isfinite(scaleY)) return false;
        try { dense_ = std::make_unique<Bits>(); }
        catch (const std::bad_alloc&) { return false; }
        active_ = true;
        return true;
    }
    void disable() noexcept { active_ = false; dense_.reset(); sparse_.reset(); count_ = 0; }
    bool active() const noexcept { return active_; }
    std::size_t size() const noexcept { return count_; }
    bool add(int key) {
        if (!active_) return false;
        const auto index = denseIndex(key);
        if (index < side * side) {
            auto& word = (*dense_)[index / 64];
            const auto bit = std::uint64_t{1} << (index % 64);
            if (!(word & bit)) { word |= bit; ++count_; }
            return true;
        }
        try {
            if (!sparse_) sparse_ = std::make_unique<std::unordered_set<int>>();
            if (sparse_->insert(key).second) ++count_;
        }
        catch (const std::bad_alloc&) { disable(); return false; }
        return true;
    }
    bool contains(int key) const noexcept {
        if (!active_) return true;
        const auto index = denseIndex(key);
        return index < side * side ? ((*dense_)[index / 64] & (std::uint64_t{1} << (index % 64))) != 0
            : sparse_ && sparse_->find(key) != sparse_->end();
    }
    // Guard only the NEW prepass conversions. An unsupported old raster takes
    // its original code, without making new assumptions about malformed maps.
    static bool exactLegacyKey(double floorX, double floorY, int& key) noexcept {
        if (!std::isfinite(floorX) || !std::isfinite(floorY)
            || floorX < static_cast<double>(std::numeric_limits<int>::min())
            || floorX > static_cast<double>(std::numeric_limits<int>::max())
            || floorY < static_cast<double>(std::numeric_limits<int>::min())
            || floorY > static_cast<double>(std::numeric_limits<int>::max())) return false;
        const int x = static_cast<int>(floorX), y = static_cast<int>(floorY);
        if (x < 0 || y < 0) { key = -1; return true; }
        const std::int64_t packed = static_cast<std::int64_t>(y) * 1000 + x;
        if (packed > std::numeric_limits<int>::max()) return false;
        key = static_cast<int>(packed);
        return true;
    }
};

struct RuntimeRadarSampledTerrainSummary {
    bool active = false;
    std::size_t sampledKeys = 0;
    std::uint64_t colorVisits = 0, effectOnlyVisits = 0, visibilityQueries = 0;
};
using RuntimeRadarSampledTerrainObserver = void(*)(void*, const RuntimeRadarSampledTerrainSummary&);
inline thread_local RuntimeRadarSampledTerrainObserver runtimeRadarSampledTerrainObserver = nullptr;
inline thread_local void* runtimeRadarSampledTerrainObserverContext = nullptr;
class RuntimeRadarSampledTerrainObserverScope {
    RuntimeRadarSampledTerrainObserver previous_;
    void* previousContext_;
public:
    RuntimeRadarSampledTerrainObserverScope(RuntimeRadarSampledTerrainObserver observer, void* context)
        : previous_(runtimeRadarSampledTerrainObserver), previousContext_(runtimeRadarSampledTerrainObserverContext) {
        runtimeRadarSampledTerrainObserver = observer; runtimeRadarSampledTerrainObserverContext = context;
    }
    ~RuntimeRadarSampledTerrainObserverScope() {
        runtimeRadarSampledTerrainObserver = previous_; runtimeRadarSampledTerrainObserverContext = previousContext_;
    }
    RuntimeRadarSampledTerrainObserverScope(const RuntimeRadarSampledTerrainObserverScope&) = delete;
    RuntimeRadarSampledTerrainObserverScope& operator=(const RuntimeRadarSampledTerrainObserverScope&) = delete;
};
class RuntimeRadarSampledTerrainObservation {
public:
    RuntimeRadarSampledTerrainSummary summary;
    ~RuntimeRadarSampledTerrainObservation() {
        if (runtimeRadarSampledTerrainObserver)
            runtimeRadarSampledTerrainObserver(runtimeRadarSampledTerrainObserverContext, summary);
    }
};
