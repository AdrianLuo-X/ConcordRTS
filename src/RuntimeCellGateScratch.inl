// Include before runtimeCellIsPassableUncached. This is an exact miss proof,
// not a gate/world cache. Positive/stale/empty indexed entries use the old path.
#pragma once
#include <cstddef>
#include <cstdlib>
inline thread_local int runtimeCellGateScratchReferenceOverride = -1;
bool runtimeCellGateScratchReference() {
    if (runtimeCellGateScratchReferenceOverride >= 0)
        return runtimeCellGateScratchReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_CELL_GATE_SCRATCH_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}
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
struct RuntimeCellGateScratchStats {
    std::size_t tailQueries = 0, exactMisses = 0, referenceOriginal = 0, indexedOriginal = 0;
};
// Contract-only POD collection: no callbacks, clocks, allocation or stdout.
inline thread_local RuntimeCellGateScratchStats* runtimeCellGateScratchStats = nullptr;
class RuntimeCellGateScratchStatsScope {
    RuntimeCellGateScratchStats* previous_;
public:
    explicit RuntimeCellGateScratchStatsScope(RuntimeCellGateScratchStats& stats)
        : previous_(runtimeCellGateScratchStats) { runtimeCellGateScratchStats = &stats; }
    ~RuntimeCellGateScratchStatsScope() { runtimeCellGateScratchStats = previous_; }
    RuntimeCellGateScratchStatsScope(const RuntimeCellGateScratchStatsScope&) = delete;
    RuntimeCellGateScratchStatsScope& operator=(const RuntimeCellGateScratchStatsScope&) = delete;
};
bool runtimeCellGateScratchExactMiss(const RuntimeWindowState& state, int legacyKey) {
    const bool reference = runtimeCellGateScratchReference();
    // Reference does not add a new map lookup before the old strict helper.
    const bool missing = !reference && state.gateIdsByCell.find(legacyKey) == state.gateIdsByCell.end();
    if (auto* stats = runtimeCellGateScratchStats) {
        ++stats->tailQueries;
        if (reference) ++stats->referenceOriginal;
        else if (missing) ++stats->exactMisses;
        else ++stats->indexedOriginal;
    }
    return missing;
}