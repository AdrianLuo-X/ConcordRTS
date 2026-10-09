#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Only the placement of the complete ordered loop changes. Reference invokes
// the same body once per one-cell range; no predicate or game input is cached.
inline thread_local int runtimePathDynamicCellBatchReferenceOverride = -1;
inline bool runtimePathDynamicCellBatchReference() {
    if (runtimePathDynamicCellBatchReferenceOverride >= 0)
        return runtimePathDynamicCellBatchReferenceOverride != 0;
    static const bool value = [] {
        const char* text = std::getenv("CONCORD_PATH_DYNAMIC_CELL_BATCH_REFERENCE");
        return text && text[0] == '1' && text[1] == '\0';
    }();
    return value;
}
class RuntimePathDynamicCellBatchReferenceScope {
    int previous_;
public:
    explicit RuntimePathDynamicCellBatchReferenceScope(bool reference)
        : previous_(runtimePathDynamicCellBatchReferenceOverride) {
        runtimePathDynamicCellBatchReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathDynamicCellBatchReferenceScope() {
        runtimePathDynamicCellBatchReferenceOverride = previous_;
    }
    RuntimePathDynamicCellBatchReferenceScope(const RuntimePathDynamicCellBatchReferenceScope&) = delete;
    RuntimePathDynamicCellBatchReferenceScope& operator=(const RuntimePathDynamicCellBatchReferenceScope&) = delete;
};

// One clock-free POD update per cold builder, only when existing Trace is on.
// No observer is installed and per-cell statistics stay compiled out normally.
// Counts are admission inputs/results, not visited predicates or elapsed costs.
inline void runtimeRecordPathDynamicSparseAdmission(bool admitted,
    bool nonAir, bool oldReference, bool censusReference,
    std::size_t expectedQueries, std::size_t occupants,
    std::size_t threatAreas, bool activeRadiation, std::size_t retainedBytes,
    bool perCellBatchReference) {
    static const bool trace = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
    if (!trace) return;
    struct Counts {
        std::uint64_t calls=0, enabled=0, airRejected=0, oldReferenceRejected=0;
        std::uint64_t hazardRejected=0, minimumRejected=0, densityRejected=0;
        std::uint64_t censusReferenceRejected=0, allocationFallback=0;
        std::uint64_t terrainEntries=0, enabledTerrainEntries=0;
        std::uint64_t occupiedEntries=0, threatBuilders=0, radiationBuilders=0;
        std::uint64_t referenceLoopBuilders=0, retainedBytesTotal=0;
        ~Counts() {
            std::cerr << "Runtime path sparse admission diagnostic: calls=" << calls
                << " enabled=" << enabled << " airRejected=" << airRejected
                << " oldReferenceRejected=" << oldReferenceRejected
                << " hazardRejected=" << hazardRejected << " minimumRejected=" << minimumRejected
                << " densityRejected=" << densityRejected
                << " censusReferenceRejected=" << censusReferenceRejected
                << " allocationFallback=" << allocationFallback
                << " terrainEntries=" << terrainEntries << " enabledTerrainEntries=" << enabledTerrainEntries
                << " occupiedEntries=" << occupiedEntries << " threatBuilders=" << threatBuilders
                << " radiationBuilders=" << radiationBuilders << " retainedBytesTotal=" << retainedBytesTotal
                << " referenceLoopBuilders=" << referenceLoopBuilders << '\n';
        }
    };
    static thread_local Counts counts;
    ++counts.calls;
    counts.terrainEntries += expectedQueries;
    counts.occupiedEntries += occupants;
    counts.threatBuilders += threatAreas != 0;
    counts.radiationBuilders += activeRadiation;
    counts.referenceLoopBuilders += perCellBatchReference;
    counts.retainedBytesTotal += retainedBytes;
    if (admitted) { ++counts.enabled; counts.enabledTerrainEntries += expectedQueries; }
    else if (!nonAir) ++counts.airRejected;
    else if (oldReference) ++counts.oldReferenceRejected;
    else if (threatAreas || activeRadiation) ++counts.hazardRejected;
    else if (expectedQueries < 4096) ++counts.minimumRejected;
    else if (censusReference) ++counts.censusReferenceRejected;
    else if (occupants > 4096 || occupants > expectedQueries / 8) ++counts.densityRejected;
    else ++counts.allocationFallback;
}
