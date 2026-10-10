#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class GameWorld;
struct RuntimeMapRenderContext;
struct RuntimeMapRenderCache;

struct RuntimeVoxelLoadingPrewarmLimits {
    std::size_t maximumNewEntries = 2048;
    std::size_t maximumAttempts = 4096;
    std::size_t maximumAddedPayloadBytes = 64U * 1024U * 1024U;
    std::size_t maximumFamilies = 128;
    std::size_t maximumSourceBytes = 8U * 1024U * 1024U;
    std::size_t maximumSectionVoxelGrid = 1024U * 1024U;
    std::size_t maximumTotalVoxelGrid = 4U * 1024U * 1024U;
    int maximumHvaFrames = 16;
    int maximumSections = 128;
    double maximumWallMilliseconds = 15000.0;
    // Optional loading-window-only pump, never a simulation/world callback.
    // Invoked between C24 requests at most once per100ms; V1 never invokes it.
    void (*loadingProgress)(void*) = nullptr;
    void* loadingProgressContext = nullptr;
};

struct RuntimeVoxelLoadingPrewarmStats {
    bool referenceSkipped = false;
    // C23 ordering and 5-second soft ceiling selected at loading dispatch.
    bool v1Reference = false;
    bool nestedSkipped = false;
    bool cacheDisabledSkipped = false;
    bool paletteNotReadySkipped = false;
    bool unsupportedProfileSkipped = false;
    bool budgetStopped = false;
    bool timeStopped = false;
    bool allocationStopped = false;
    std::size_t families = 0;
    std::size_t duplicateFamilies = 0;
    std::size_t unavailableSources = 0;
    std::size_t oversizedSources = 0;
    std::size_t highHvaSources = 0;
    std::size_t fixedScaleNotReady = 0;
    std::size_t attempts = 0;
    std::size_t existingEntries = 0;
    std::size_t decodedEntries = 0;
    std::size_t failedEntries = 0;
    std::size_t newEntries = 0;
    // Conservative owned payload: entry/key objects, vector capacities and
    // string capacities including both stable-lookup key/value copies. It
    // excludes allocator bookkeeping and unordered-map bucket arrays.
    std::size_t addedPayloadBytes = 0;
    double elapsedMilliseconds = 0.0;
};

// Loading-thread only, after ordinary first-frame prewarm has populated the
// actual current world's sources and fixed-scale/VPL configuration. This call
// never mutates the world. It does not read unknown VXL/HVA files or warm rules
// types absent from the current world. A single exact decoder is indivisible;
// the time limit is checked between requests, not inside rasterization.
void prewarmRuntimeCurrentWorldNormalVoxels(
    const GameWorld& world, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, const RuntimeVoxelLoadingPrewarmLimits& limits,
    RuntimeVoxelLoadingPrewarmStats& stats);

bool runRuntimeVoxelLoadingPrewarmAssetContractCheck(
    const RuntimeMapRenderContext& context, std::string& error);
