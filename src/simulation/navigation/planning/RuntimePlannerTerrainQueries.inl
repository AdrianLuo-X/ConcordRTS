// Include after RuntimeWindowState/RuntimeTerrainInfo and
// runtimeCachedTerrainInfoForCell, before runtimeCellTransitionIsPassable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Independent reference restores the previous per-BFS owned metadata table.


class RuntimeComponentMetadataBorrowReferenceScope {
public:
    explicit RuntimeComponentMetadataBorrowReferenceScope(bool reference)
        : previous_(runtimeComponentMetadataBorrowReferenceOverride) {
        runtimeComponentMetadataBorrowReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentMetadataBorrowReferenceScope() {
        runtimeComponentMetadataBorrowReferenceOverride = previous_;
    }
    RuntimeComponentMetadataBorrowReferenceScope(const RuntimeComponentMetadataBorrowReferenceScope&) = delete;
    RuntimeComponentMetadataBorrowReferenceScope& operator=(const RuntimeComponentMetadataBorrowReferenceScope&) = delete;
private:
    int previous_;
};




class RuntimeComponentTerrainQueryReferenceScope {
public:
    explicit RuntimeComponentTerrainQueryReferenceScope(bool reference)
        : previous_(runtimeComponentTerrainQueryReferenceOverride) {
        runtimeComponentTerrainQueryReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentTerrainQueryReferenceScope() {
        runtimeComponentTerrainQueryReferenceOverride = previous_;
    }
    RuntimeComponentTerrainQueryReferenceScope(const RuntimeComponentTerrainQueryReferenceScope&) = delete;
    RuntimeComponentTerrainQueryReferenceScope& operator=(const RuntimeComponentTerrainQueryReferenceScope&) = delete;
private:
    int previous_;
};

// ONE complete-component BFS/kernel owns the terrain pointer table. Metadata
// is either the former independent owned table or its typed container's exact
// address table borrowed within this owner. No terrain/info values are cached.
// Both tables use original integer map keys, NOT pointed-to raw coordinates.
// Legacy alias (1001,2)/(1,3) keeps its old key=3001 identity.
//
// End/invalidate before ANY map node/payload, terrain cell, rule, world,
// occupancy, gate or reservation edit, even if addresses/counts do not change.
// Shape checks are defensive; they are not a content-version or mutation proof.
// No lease/view escapes this owner or enters serialized authority. The existing
// metadata container may retain only its own bounded live-address table.




class RuntimePlannerMetadataBorrowObserverScope {
public:
    RuntimePlannerMetadataBorrowObserverScope(RuntimePlannerMetadataBorrowObserver observer, void* context)
        : previous_(runtimePlannerMetadataBorrowObserver),
          previousContext_(runtimePlannerMetadataBorrowObserverContext) {
        runtimePlannerMetadataBorrowObserver = observer;
        runtimePlannerMetadataBorrowObserverContext = context;
    }
    ~RuntimePlannerMetadataBorrowObserverScope() {
        runtimePlannerMetadataBorrowObserver = previous_;
        runtimePlannerMetadataBorrowObserverContext = previousContext_;
    }
    RuntimePlannerMetadataBorrowObserverScope(const RuntimePlannerMetadataBorrowObserverScope&) = delete;
    RuntimePlannerMetadataBorrowObserverScope& operator=(const RuntimePlannerMetadataBorrowObserverScope&) = delete;
private:
    RuntimePlannerMetadataBorrowObserver previous_;
    void* previousContext_;
};


