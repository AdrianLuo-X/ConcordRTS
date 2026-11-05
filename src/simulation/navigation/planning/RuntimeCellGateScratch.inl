// Include before runtimeCellIsPassableUncached. This is an exact miss proof,
// not a gate/world cache. Positive/stale/empty indexed entries use the old path.
#pragma once
#include <cstddef>
#include <cstdlib>


class RuntimeCellGateScratchReferenceScope {
    int previous_;
public:
    explicit RuntimeCellGateScratchReferenceScope(bool value)
        : previous_(runtimeCellGateScratchReferenceOverride) {
        runtimeCellGateScratchReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeCellGateScratchReferenceScope() { runtimeCellGateScratchReferenceOverride = previous_; }
    RuntimeCellGateScratchReferenceScope(const RuntimeCellGateScratchReferenceScope&) = delete;
    RuntimeCellGateScratchReferenceScope& operator=(const RuntimeCellGateScratchReferenceScope&) = delete;
};

// Contract-only POD collection: no callbacks, clocks, allocation or stdout.

class RuntimeCellGateScratchStatsScope {
    RuntimeCellGateScratchStats* previous_;
public:
    explicit RuntimeCellGateScratchStatsScope(RuntimeCellGateScratchStats& stats)
        : previous_(runtimeCellGateScratchStats) { runtimeCellGateScratchStats = &stats; }
    ~RuntimeCellGateScratchStatsScope() { runtimeCellGateScratchStats = previous_; }
    RuntimeCellGateScratchStatsScope(const RuntimeCellGateScratchStatsScope&) = delete;
    RuntimeCellGateScratchStatsScope& operator=(const RuntimeCellGateScratchStatsScope&) = delete;
};
