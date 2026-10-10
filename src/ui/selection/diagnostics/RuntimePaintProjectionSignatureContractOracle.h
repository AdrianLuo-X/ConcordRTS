#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// Contract-only wrapper around the renderer's existing complete 353 non-timing
// fields and deep resource/cache oracle. No production drawing calls this.
struct RuntimeMapRenderStats;
struct RuntimeMapRenderCache;
bool runtimePaintProjectionSignatureContractRenderEqual(
    const RuntimeMapRenderStats& original, const RuntimeMapRenderStats& memoized,
    const RuntimeMapRenderCache& originalCache, const RuntimeMapRenderCache& memoizedCache,
    std::string& field);
