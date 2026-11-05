// Include after RuntimePlannerTerrainQueries.inl, before map-query helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <vector>



class RuntimeComponentColdLookupReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentColdLookupReferenceScope(bool reference)
        : previous_(runtimeComponentColdLookupReferenceOverride) {
        runtimeComponentColdLookupReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentColdLookupReferenceScope() {
        runtimeComponentColdLookupReferenceOverride = previous_;
    }
    RuntimeComponentColdLookupReferenceScope(const RuntimeComponentColdLookupReferenceScope&) = delete;
    RuntimeComponentColdLookupReferenceScope& operator=(const RuntimeComponentColdLookupReferenceScope&) = delete;
};






class RuntimePlannerColdLookupObserverScope {
    RuntimePlannerColdLookupObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerColdLookupObserverScope(RuntimePlannerColdLookupObserver observer, void* context)
        : previous_(runtimePlannerColdLookupObserver), previousContext_(runtimePlannerColdLookupObserverContext) {
        runtimePlannerColdLookupObserver = observer; runtimePlannerColdLookupObserverContext = context;
    }
    ~RuntimePlannerColdLookupObserverScope() {
        runtimePlannerColdLookupObserver = previous_; runtimePlannerColdLookupObserverContext = previousContext_;
    }
    RuntimePlannerColdLookupObserverScope(const RuntimePlannerColdLookupObserverScope&) = delete;
    RuntimePlannerColdLookupObserverScope& operator=(const RuntimePlannerColdLookupObserverScope&) = delete;
};

// Owned by ONE complete BFS. Its existing terrain-query owner supplies the
// supplied-terrain and metadata tables: do not build a second copy of them.
// Only facts, and a DIFFERENT state terrain map, need extra pointer tables.
// These pointers expose the original nodes, including stale metadata and facts
// whose authored x/y disagree with cellIndexLookup. No classification or lazy
// derived cache is called during preparation.
//
// END/INVALIDATE before any in-place mover, world, map, facts, rules, occupancy,
// reservation or gate edit. Shape/address checks are NOT content revisions.
// Disabled/nested/foreign owners mask older owners; nothing escapes this BFS.

