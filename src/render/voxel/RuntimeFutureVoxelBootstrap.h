#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct RuntimeMapRenderContext;
struct RuntimeMapRenderCache;
struct RuntimeMapRenderStats;

// Loading-only, ordinary UnitType IDs already filtered by the shell's country
// and static tech rules. No object is created in GameWorld. Existing asset
// sources keep their first-use semantics: call only inside the fresh-match
// asset epoch, with immutable rules/art/indexes and a pure loading-HWND pump.
struct RuntimeFutureVoxelBootstrapLimits {
    std::size_t maximumFamilies = 32;
    std::size_t maximumNewEntries = 1024;
    std::size_t maximumAttempts = 2048;
    std::size_t maximumAddedPayloadBytes = 64U * 1024U * 1024U;
    std::size_t maximumFamilySourceBytes = 8U * 1024U * 1024U;
    std::size_t maximumSourceReadBytes = 8U * 1024U * 1024U;
    std::size_t maximumTransientReadBytes = 16U * 1024U * 1024U;
    std::size_t maximumIndexPaths = 256;
    std::size_t maximumIndexPathBytes = 1024U * 1024U;
    double maximumWallMilliseconds = 15000.0;
    void (*loadingProgress)(void*) = nullptr;
    void* loadingProgressContext = nullptr;
};

struct RuntimeFutureVoxelBootstrapStats {
    bool referenceSkipped = false;
    bool nestedSkipped = false;
    bool cacheDisabledSkipped = false;
    bool unsupportedProfileSkipped = false;
    bool paletteNotReadySkipped = false;
    bool indexNotReadySkipped = false;
    bool budgetStopped = false;
    bool timeStopped = false;
    bool allocationStopped = false;
    bool sourceChangedStopped = false;
    std::size_t families = 0;
    std::size_t duplicateFamilies = 0;
    std::size_t unsupportedTypes = 0;
    std::size_t failedFamilies = 0;
    std::size_t unknownEarlierArchives = 0;
    std::size_t refusedSourceReads = 0;
    std::size_t sourceReads = 0;
    std::size_t sourceReadBytes = 0;
    std::size_t maximumTransientReadBytes = 0;
    std::size_t existingSources = 0;
    std::size_t existingNegativeSources = 0;
    std::size_t attempts = 0;
    std::size_t decodedEntries = 0;
    std::size_t failedEntries = 0;
    std::size_t existingEntries = 0;
    std::size_t newEntries = 0;
    std::size_t newSources = 0;
    std::size_t newFixedScales = 0;
    std::size_t addedPayloadBytes = 0;
    std::size_t progressCallbacks = 0;
    double elapsedMilliseconds = 0.0;
};

void prewarmRuntimeFutureNormalVoxelTypes(const std::vector<std::string>& typeIds,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    const RuntimeFutureVoxelBootstrapLimits& limits, RuntimeFutureVoxelBootstrapStats& stats);

bool runRuntimeFutureVoxelBootstrapAssetContractCheck(
    const RuntimeMapRenderContext& context, std::string& error);

// Contract-only comparison: every existing non-timing render field is exact,
// except the two voxel cache hit/miss counters deliberately changed by loading.
bool runtimeFutureVoxelBootstrapRenderContractStatsEqual(
    const RuntimeMapRenderStats& original, const RuntimeMapRenderStats& prepared,
    std::string& mismatchedField);
