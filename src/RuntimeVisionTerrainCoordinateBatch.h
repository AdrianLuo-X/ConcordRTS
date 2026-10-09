#pragma once

#include "RuntimeVisionTerrainCoordinateLease.h"
#include <optional>

// Only the local main-loop's bounded simulation while owns this scope. It ends
// before audio, movies, painting and message dispatch. It is never retained in
// WindowState, across messages or across service batches. The checked loop may
// mutate terrain contents, but not x/y/order; all world replacement commits and
// synchronous window reentry must invalidate BEFORE the first possible write.
inline thread_local int runtimeVisionTerrainCoordinateBatchReferenceOverride=-1;
inline bool runtimeVisionTerrainCoordinateBatchReference() noexcept {
    if(runtimeVisionTerrainCoordinateBatchReferenceOverride>=0)
        return runtimeVisionTerrainCoordinateBatchReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_VISION_TERRAIN_BATCH_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeVisionTerrainCoordinateBatchReferenceScope {
    int previous_;
public:
    explicit RuntimeVisionTerrainCoordinateBatchReferenceScope(bool reference) noexcept
        : previous_(runtimeVisionTerrainCoordinateBatchReferenceOverride) {
        runtimeVisionTerrainCoordinateBatchReferenceOverride=reference?1:0;
    }
    ~RuntimeVisionTerrainCoordinateBatchReferenceScope() {
        runtimeVisionTerrainCoordinateBatchReferenceOverride=previous_;
    }
    RuntimeVisionTerrainCoordinateBatchReferenceScope(const RuntimeVisionTerrainCoordinateBatchReferenceScope&)=delete;
    RuntimeVisionTerrainCoordinateBatchReferenceScope& operator=(const RuntimeVisionTerrainCoordinateBatchReferenceScope&)=delete;
};

class RuntimeVisionTerrainCoordinateBatchScope;
inline thread_local RuntimeVisionTerrainCoordinateBatchScope* runtimeActiveVisionTerrainCoordinateBatch=nullptr;

class RuntimeVisionTerrainCoordinateBatchScope {
    RuntimeVisionTerrainCoordinateBatchScope* previous_;
    const GameWorld* world_;
    RuntimeVisionTerrainCoordinateLeaseScope lease_;
public:
    explicit RuntimeVisionTerrainCoordinateBatchScope(const GameWorld& world,bool enabled=true) noexcept
        : previous_(runtimeActiveVisionTerrainCoordinateBatch),world_(&world),
          lease_(world,enabled && !runtimeVisionTerrainCoordinateBatchReference()) {
        // A disabled/foreign nested batch masks its ancestors.
        runtimeActiveVisionTerrainCoordinateBatch=this;
    }
    ~RuntimeVisionTerrainCoordinateBatchScope() {
        // The held lease subsequently restores the previous lease. Clearing the
        // batch pointer first also prevents its destruction observer borrowing it.
        runtimeActiveVisionTerrainCoordinateBatch=previous_;
    }
    RuntimeVisionTerrainCoordinateBatchScope(const RuntimeVisionTerrainCoordinateBatchScope&)=delete;
    RuntimeVisionTerrainCoordinateBatchScope& operator=(const RuntimeVisionTerrainCoordinateBatchScope&)=delete;
    RuntimeVisionTerrainCoordinateBatchScope(RuntimeVisionTerrainCoordinateBatchScope&&)=delete;
    RuntimeVisionTerrainCoordinateBatchScope& operator=(RuntimeVisionTerrainCoordinateBatchScope&&)=delete;
    bool mayBorrow(const GameWorld& world) const noexcept {
        return runtimeActiveVisionTerrainCoordinateBatch==this
            && world_==&world && runtimeActiveVisionTerrainCoordinateLease==&lease_
            && lease_.enabled() && !runtimeVisionTerrainCoordinateReference()
            && !runtimeVisionTerrainCoordinateBatchReference();
    }
    bool enabled() const noexcept { return lease_.enabled(); }
    std::size_t originalHashes() const noexcept { return lease_.originalHashes(); }
    std::size_t reuses() const noexcept { return lease_.reuses(); }
    std::size_t fallbacks() const noexcept { return lease_.fallbacks(); }
    void invalidate() noexcept { lease_.invalidate(); }
};

class RuntimeVisionTerrainCoordinateInvocationScope {
    std::optional<RuntimeVisionTerrainCoordinateLeaseScope> ordinary_;
    bool borrowed_=false;
public:
    explicit RuntimeVisionTerrainCoordinateInvocationScope(const GameWorld& world) {
        auto* batch=runtimeActiveVisionTerrainCoordinateBatch;
        borrowed_=batch && batch->mayBorrow(world);
        if(!borrowed_) ordinary_.emplace(world);
    }
    bool borrowed() const noexcept { return borrowed_; }
    bool ownsLease() const noexcept { return ordinary_.has_value(); }
    RuntimeVisionTerrainCoordinateInvocationScope(const RuntimeVisionTerrainCoordinateInvocationScope&)=delete;
    RuntimeVisionTerrainCoordinateInvocationScope& operator=(const RuntimeVisionTerrainCoordinateInvocationScope&)=delete;
    RuntimeVisionTerrainCoordinateInvocationScope(RuntimeVisionTerrainCoordinateInvocationScope&&)=delete;
    RuntimeVisionTerrainCoordinateInvocationScope& operator=(RuntimeVisionTerrainCoordinateInvocationScope&&)=delete;
};

// Called after the window procedure obtains its state pointer, before ANY
// handler writes. A foreign active batch must not hide same-world ancestors;
// the original invalidator walks the entire active held-lease chain.
inline void runtimeInvalidateVisionTerrainBatchForMessage(const GameWorld& world) noexcept {
    if(runtimeActiveVisionTerrainCoordinateBatch)
        invalidateRuntimeVisionTerrainCoordinateLeases(world);
}
