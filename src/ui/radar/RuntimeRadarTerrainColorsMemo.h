#pragma once

#include "render/core/RuntimeMapRenderer.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

// The reference is independent of the map/geometry reference switches so a
// real radar rebuild can be compared in the same executable.
inline thread_local int runtimeRadarTerrainColorReferenceOverride = -1;

inline bool runtimeRadarTerrainColorReference() {
    if (runtimeRadarTerrainColorReferenceOverride >= 0)
        return runtimeRadarTerrainColorReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_RADAR_TERRAIN_COLOR_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeRadarTerrainColorReferenceScope {
public:
    explicit RuntimeRadarTerrainColorReferenceScope(bool reference)
        : previous_(runtimeRadarTerrainColorReferenceOverride) {
        runtimeRadarTerrainColorReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeRadarTerrainColorReferenceScope() {
        runtimeRadarTerrainColorReferenceOverride = previous_;
    }
    RuntimeRadarTerrainColorReferenceScope(const RuntimeRadarTerrainColorReferenceScope&) = delete;
    RuntimeRadarTerrainColorReferenceScope& operator=(const RuntimeRadarTerrainColorReferenceScope&) = delete;
private:
    int previous_;
};

struct RuntimeRadarTerrainColorMemoSummary {
    std::uint64_t queries = 0, hits = 0, scalarCalls = 0;
    std::size_t entries = 0;
    bool reference = false;
};
using RuntimeRadarTerrainColorMemoObserver = void(*)(void*, const RuntimeRadarTerrainColorMemoSummary&);
inline thread_local RuntimeRadarTerrainColorMemoObserver runtimeRadarTerrainColorMemoObserver = nullptr;
inline thread_local void* runtimeRadarTerrainColorMemoObserverContext = nullptr;

// Observe completed production rebuilds in contracts, with one destructor
// branch per memo and no additional per-cell hook during normal painting.
class RuntimeRadarTerrainColorMemoObserverScope {
public:
    RuntimeRadarTerrainColorMemoObserverScope(RuntimeRadarTerrainColorMemoObserver observer, void* context)
        : previous_(runtimeRadarTerrainColorMemoObserver),
          previousContext_(runtimeRadarTerrainColorMemoObserverContext) {
        runtimeRadarTerrainColorMemoObserver = observer;
        runtimeRadarTerrainColorMemoObserverContext = context;
    }
    ~RuntimeRadarTerrainColorMemoObserverScope() {
        runtimeRadarTerrainColorMemoObserver = previous_;
        runtimeRadarTerrainColorMemoObserverContext = previousContext_;
    }
    RuntimeRadarTerrainColorMemoObserverScope(const RuntimeRadarTerrainColorMemoObserverScope&) = delete;
    RuntimeRadarTerrainColorMemoObserverScope& operator=(const RuntimeRadarTerrainColorMemoObserverScope&) = delete;
private:
    RuntimeRadarTerrainColorMemoObserver previous_;
    void* previousContext_;
};

// Own this memo inside one radar terrain rebuild, after its context is final.
// The source context and loaded TMP/palette results must remain unchanged until
// exit; inserts into the resource cache are allowed. Explicit invalidation is
// required before resetting/replacing/reloading them in the same lifetime.
// No cell position, owner, explored/shrouded mask, ore state, radar projection,
// final COLORREF, or destination pixels are memoized here. Every cell continues
// through the original radar visibility/resource/fallback/drawing branches.
class RuntimeRadarTerrainColorsMemo {
    struct Entry {
        bool resolved = false;
        RuntimePaletteColor left;
        RuntimePaletteColor right;
        bool writesLastTmpError = false;
        std::string lastTmpError;
    };
public:
    static constexpr std::size_t maximumMemoizedColors = 2048;

    RuntimeRadarTerrainColorsMemo(const RuntimeMapRenderContext& context,
        RuntimeMapRenderCache& cache, bool reference = runtimeRadarTerrainColorReference())
        : context_(context), cache_(cache), reference_(reference) {}
    ~RuntimeRadarTerrainColorsMemo() {
        if (runtimeRadarTerrainColorMemoObserver)
            runtimeRadarTerrainColorMemoObserver(runtimeRadarTerrainColorMemoObserverContext,
                RuntimeRadarTerrainColorMemoSummary{queries_, hits_, scalarCalls_, entries_.size(), reference_});
    }

    bool resolve(const TerrainCell& cell, RuntimePaletteColor& left,
        RuntimePaletteColor& right) {
        return resolveInternal(cell, left, right, true);
    }

    // Preserve every original asset lookup/cache first-write/error replay and
    // memo counter in author order, while discarding unobserved RGB outputs.
    // The caller may omit visibility/fallback arithmetic only when no raster
    // pixel can read this legacy key and the cell is not a resource candidate.
    bool visitWithoutColors(const TerrainCell& cell) {
        RuntimePaletteColor left{}, right{};
        return resolveInternal(cell, left, right, false);
    }

private:
    bool resolveInternal(const TerrainCell& cell, RuntimePaletteColor& left,
        RuntimePaletteColor& right, bool copyColors) {
        ++queries_;
        if (reference_) return scalar(cell, left, right);
        const int tile = cell.tileIndex == 65535 ? 0 : cell.tileIndex;
        const int subTile = cell.tileIndex == 65535 ? 0 : cell.subTileIndex;
        // Keep complete signed int values: no tile/subtile truncation or alias.
        const std::uint64_t key = (static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(tile)) << 32)
            | static_cast<std::uint32_t>(subTile);
        if (lastEntry_ && lastKey_ == key) return replay(*lastEntry_, left, right, copyColors);
        const auto found = entries_.find(key);
        if (found != entries_.end()) {
            lastKey_ = key; lastEntry_ = &found->second;
            return replay(found->second, left, right, copyColors);
        }
        const bool resolved = scalar(cell, left, right);
        if (entries_.size() < maximumMemoizedColors) {
            Entry entry;
            entry.resolved = resolved;
            if (resolved) { entry.left = left; entry.right = right; }
            // Warm MIX resolutions assign their cached diagnostic on every
            // call. Loose-file and missing-index hits leave it untouched.
            // A rejected texture/context/palette never reaches tile loading.
            const auto locator = cache_.terrainTilePathByIndex.find(tile);
            entry.writesLastTmpError = context_.useTileTextures && context_.theater
                && (!context_.tileRootPath.empty() || !context_.assetRootPath.empty())
                && !context_.palettePath.empty() && cache_.paletteLoaded
                && cache_.loadedPalettePath == context_.palettePath
                && locator != cache_.terrainTilePathByIndex.end()
                && locator->second.rfind("@mix-resource:", 0) == 0;
            if (entry.writesLastTmpError) entry.lastTmpError = cache_.lastTmpTileLoadError;
            const auto inserted = entries_.emplace(key, std::move(entry));
            lastKey_ = key; lastEntry_ = &inserted.first->second;
        }
        return resolved;
    }

public:
    void invalidate() {
        lastEntry_ = nullptr;
        entries_.clear();
    }
    std::size_t size() const { return entries_.size(); }
    std::uint64_t queries() const { return queries_; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t scalarCalls() const { return scalarCalls_; }

    RuntimeRadarTerrainColorsMemo(const RuntimeRadarTerrainColorsMemo&) = delete;
    RuntimeRadarTerrainColorsMemo& operator=(const RuntimeRadarTerrainColorsMemo&) = delete;
private:
    bool scalar(const TerrainCell& cell, RuntimePaletteColor& left,
        RuntimePaletteColor& right) {
        ++scalarCalls_;
        return resolveRuntimeTerrainRadarColors(cell, context_, cache_, left, right);
    }
    bool replay(const Entry& entry, RuntimePaletteColor& left,
        RuntimePaletteColor& right, bool copyColors) {
        ++hits_;
        if (entry.writesLastTmpError) cache_.lastTmpTileLoadError = entry.lastTmpError;
        // The original false path does not assign either output.
        if (copyColors && entry.resolved) { left = entry.left; right = entry.right; }
        return entry.resolved;
    }
    const RuntimeMapRenderContext& context_;
    RuntimeMapRenderCache& cache_;
    bool reference_;
    std::unordered_map<std::uint64_t, Entry> entries_;
    const Entry* lastEntry_ = nullptr;
    std::uint64_t lastKey_ = 0;
    std::uint64_t queries_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t scalarCalls_ = 0;
};
