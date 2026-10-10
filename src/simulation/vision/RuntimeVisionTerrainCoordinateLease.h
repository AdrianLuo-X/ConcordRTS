#pragma once

#include "world/model/GameWorld.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// A derived scalar value belongs to ONE lexical simulation invocation. Never
// retain this scope in WindowState or across ticks. Invalidate BEFORE editing
// terrain x/y, size/order/storage or replacing either world at a commit.
inline thread_local int runtimeVisionTerrainCoordinateReferenceOverride=-1;
inline bool runtimeVisionTerrainCoordinateReference() noexcept {
    if(runtimeVisionTerrainCoordinateReferenceOverride>=0)
        return runtimeVisionTerrainCoordinateReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_VISION_TERRAIN_COORDINATE_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeVisionTerrainCoordinateReferenceScope {
    int previous_;
public:
    explicit RuntimeVisionTerrainCoordinateReferenceScope(bool value) noexcept
        : previous_(runtimeVisionTerrainCoordinateReferenceOverride) {
        runtimeVisionTerrainCoordinateReferenceOverride=value?1:0;
    }
    ~RuntimeVisionTerrainCoordinateReferenceScope() {
        runtimeVisionTerrainCoordinateReferenceOverride=previous_;
    }
    RuntimeVisionTerrainCoordinateReferenceScope(const RuntimeVisionTerrainCoordinateReferenceScope&)=delete;
    RuntimeVisionTerrainCoordinateReferenceScope& operator=(const RuntimeVisionTerrainCoordinateReferenceScope&)=delete;
};

class RuntimeVisionTerrainCoordinateLeaseScope;
inline thread_local RuntimeVisionTerrainCoordinateLeaseScope* runtimeActiveVisionTerrainCoordinateLease=nullptr;
inline void invalidateRuntimeVisionTerrainCoordinateLeases(const GameWorld& world) noexcept;

using RuntimeVisionTerrainCoordinateLeaseObserver=void(*)(void*,const GameWorld&,
    bool,std::size_t,std::size_t,std::size_t);
inline thread_local RuntimeVisionTerrainCoordinateLeaseObserver runtimeVisionTerrainCoordinateLeaseObserver=nullptr;
inline thread_local void* runtimeVisionTerrainCoordinateLeaseObserverContext=nullptr;
class RuntimeVisionTerrainCoordinateLeaseObserverScope {
    RuntimeVisionTerrainCoordinateLeaseObserver previous_;
    void* previousContext_;
public:
    RuntimeVisionTerrainCoordinateLeaseObserverScope(
        RuntimeVisionTerrainCoordinateLeaseObserver observer,void* context) noexcept
        : previous_(runtimeVisionTerrainCoordinateLeaseObserver),
          previousContext_(runtimeVisionTerrainCoordinateLeaseObserverContext) {
        runtimeVisionTerrainCoordinateLeaseObserver=observer;
        runtimeVisionTerrainCoordinateLeaseObserverContext=context;
    }
    ~RuntimeVisionTerrainCoordinateLeaseObserverScope() {
        runtimeVisionTerrainCoordinateLeaseObserver=previous_;
        runtimeVisionTerrainCoordinateLeaseObserverContext=previousContext_;
    }
    RuntimeVisionTerrainCoordinateLeaseObserverScope(const RuntimeVisionTerrainCoordinateLeaseObserverScope&)=delete;
    RuntimeVisionTerrainCoordinateLeaseObserverScope& operator=(const RuntimeVisionTerrainCoordinateLeaseObserverScope&)=delete;
};

// Pointer/count are defensive identity guards, not content revisions. The
// checked call graph makes coordinates read-only; commit hooks close ALL
// same-world ancestors before vector replacement, including rollback swaps.
class RuntimeVisionTerrainCoordinateLeaseScope {
    friend void invalidateRuntimeVisionTerrainCoordinateLeases(const GameWorld&) noexcept;
    RuntimeVisionTerrainCoordinateLeaseScope* previous_;
    const GameWorld* world_;
    const TerrainCell* cells_;
    std::size_t count_;
    bool enabled_,valid_=false;
    std::uint64_t signature_=0;
    std::size_t originalHashes_=0,reuses_=0,fallbacks_=0;
public:
    explicit RuntimeVisionTerrainCoordinateLeaseScope(const GameWorld& world,bool enabled=true) noexcept
        : previous_(runtimeActiveVisionTerrainCoordinateLease),world_(&world),
          cells_(world.getTerrainCells().data()),count_(world.getTerrainCells().size()),
          enabled_(enabled && !runtimeVisionTerrainCoordinateReference()) {
        // Disabled and reference-created nested scopes mask their parents.
        runtimeActiveVisionTerrainCoordinateLease=this;
    }
    ~RuntimeVisionTerrainCoordinateLeaseScope() {
        if(runtimeVisionTerrainCoordinateLeaseObserver)
            runtimeVisionTerrainCoordinateLeaseObserver(runtimeVisionTerrainCoordinateLeaseObserverContext,
                *world_,enabled_,originalHashes_,reuses_,fallbacks_);
        runtimeActiveVisionTerrainCoordinateLease=previous_;
    }
    RuntimeVisionTerrainCoordinateLeaseScope(const RuntimeVisionTerrainCoordinateLeaseScope&)=delete;
    RuntimeVisionTerrainCoordinateLeaseScope& operator=(const RuntimeVisionTerrainCoordinateLeaseScope&)=delete;
    RuntimeVisionTerrainCoordinateLeaseScope(RuntimeVisionTerrainCoordinateLeaseScope&&)=delete;
    RuntimeVisionTerrainCoordinateLeaseScope& operator=(RuntimeVisionTerrainCoordinateLeaseScope&&)=delete;
    void invalidate() noexcept { invalidateRuntimeVisionTerrainCoordinateLeases(*world_); }
    bool enabled() const noexcept { return enabled_; }
    std::size_t originalHashes() const noexcept { return originalHashes_; }
    std::size_t reuses() const noexcept { return reuses_; }
    std::size_t fallbacks() const noexcept { return fallbacks_; }
    template<class OriginalHash>
    std::uint64_t signatureFor(const GameWorld& world,OriginalHash&& original) {
        if(!enabled_ || runtimeActiveVisionTerrainCoordinateLease!=this
            || world_!=&world || runtimeVisionTerrainCoordinateReference()) {
            ++fallbacks_; return original();
        }
        const auto& cells=world.getTerrainCells();
        if(cells_!=cells.data() || count_!=cells.size()) {
            // If a missing hook changes storage/count, do not allow an outer
            // cache to resurrect when a nested scope returns. Same-size edits
            // still require an explicit pre-mutation hook.
            invalidateRuntimeVisionTerrainCoordinateLeases(world);
            ++fallbacks_; return original();
        }
        if(valid_) { ++reuses_; return signature_; }
        ++originalHashes_;
        const auto value=original();
        signature_=value; valid_=true;
        return value;
    }
};

inline void invalidateRuntimeVisionTerrainCoordinateLeases(const GameWorld& world) noexcept {
    for(auto* scope=runtimeActiveVisionTerrainCoordinateLease;scope;scope=scope->previous_)
        if(scope->world_==&world) { scope->enabled_=false; scope->valid_=false; }
}

template<class OriginalHash>
inline std::uint64_t runtimeVisionTerrainCoordinateSignature(const GameWorld& world,OriginalHash&& original) {
    auto* scope=runtimeActiveVisionTerrainCoordinateLease;
    // Ordinary UI/load/standalone refresh keeps the original scalar and does
    // not read the reference environment or build any auxiliary hash/table.
    if(!scope) return original();
    return scope->signatureFor(world,static_cast<OriginalHash&&>(original));
}
