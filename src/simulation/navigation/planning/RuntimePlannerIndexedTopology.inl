// Include after runtimeCachedTerrainInfoForCell and RuntimePlannerTerrainQueries.
// Owned only by ONE complete-component BFS. Every dynamic predicate and the
// original seed/FIFO/output-map order remain outside this geometry lease.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>



class RuntimeComponentIndexedTopologyReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentIndexedTopologyReferenceScope(bool reference) noexcept
        :previous_(runtimeComponentIndexedTopologyReferenceOverride) {
        runtimeComponentIndexedTopologyReferenceOverride=reference?1:0;
    }
    ~RuntimeComponentIndexedTopologyReferenceScope() {
        runtimeComponentIndexedTopologyReferenceOverride=previous_;
    }
    RuntimeComponentIndexedTopologyReferenceScope(const RuntimeComponentIndexedTopologyReferenceScope&)=delete;
    RuntimeComponentIndexedTopologyReferenceScope& operator=(const RuntimeComponentIndexedTopologyReferenceScope&)=delete;
};






class RuntimePlannerIndexedTopologyObserverScope {
    RuntimePlannerIndexedTopologyObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerIndexedTopologyObserverScope(RuntimePlannerIndexedTopologyObserver observer,void* context)
        :previous_(runtimePlannerIndexedTopologyObserver),previousContext_(runtimePlannerIndexedTopologyObserverContext) {
        runtimePlannerIndexedTopologyObserver=observer; runtimePlannerIndexedTopologyObserverContext=context;
    }
    ~RuntimePlannerIndexedTopologyObserverScope() {
        runtimePlannerIndexedTopologyObserver=previous_; runtimePlannerIndexedTopologyObserverContext=previousContext_;
    }
    RuntimePlannerIndexedTopologyObserverScope(const RuntimePlannerIndexedTopologyObserverScope&)=delete;
    RuntimePlannerIndexedTopologyObserverScope& operator=(const RuntimePlannerIndexedTopologyObserverScope&)=delete;
};


