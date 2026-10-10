#pragma once

#include "world/model/GameWorld.h"
#include <cstddef>
#include <cstdlib>
#include <vector>

// Independent of terrain-coordinate leases and source-grid memoization. Each
// update reads this switch once; the list is never retained across calls.
inline thread_local int runtimeVisionLiveSourcesReferenceOverride=-1;
inline bool runtimeVisionLiveSourcesReference() noexcept {
    if(runtimeVisionLiveSourcesReferenceOverride>=0)
        return runtimeVisionLiveSourcesReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_VISION_LIVE_SOURCES_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeVisionLiveSourcesReferenceScope {
    int previous_;
public:
    explicit RuntimeVisionLiveSourcesReferenceScope(bool reference) noexcept
        : previous_(runtimeVisionLiveSourcesReferenceOverride) {
        runtimeVisionLiveSourcesReferenceOverride=reference?1:0;
    }
    ~RuntimeVisionLiveSourcesReferenceScope() {
        runtimeVisionLiveSourcesReferenceOverride=previous_;
    }
    RuntimeVisionLiveSourcesReferenceScope(const RuntimeVisionLiveSourcesReferenceScope&)=delete;
    RuntimeVisionLiveSourcesReferenceScope& operator=(const RuntimeVisionLiveSourcesReferenceScope&)=delete;
};

struct RuntimeVisionLiveSourcesStats {
    std::size_t cargoHostVisits=0,aliveAddresses=0,sourcePasses=0;
    std::size_t sourceRangeEntries=0,originalRangeEntries=0;
    bool reference=false;
};
using RuntimeVisionLiveSourcesObserver=void(*)(void*,const GameWorld&,int,
    const RuntimeVisionLiveSourcesStats&);
inline thread_local RuntimeVisionLiveSourcesObserver runtimeVisionLiveSourcesObserver=nullptr;
inline thread_local void* runtimeVisionLiveSourcesObserverContext=nullptr;
class RuntimeVisionLiveSourcesObserverScope {
    RuntimeVisionLiveSourcesObserver previous_;
    void* previousContext_;
public:
    RuntimeVisionLiveSourcesObserverScope(RuntimeVisionLiveSourcesObserver observer,void* context) noexcept
        : previous_(runtimeVisionLiveSourcesObserver),previousContext_(runtimeVisionLiveSourcesObserverContext) {
        runtimeVisionLiveSourcesObserver=observer; runtimeVisionLiveSourcesObserverContext=context;
    }
    ~RuntimeVisionLiveSourcesObserverScope() {
        runtimeVisionLiveSourcesObserver=previous_; runtimeVisionLiveSourcesObserverContext=previousContext_;
    }
    RuntimeVisionLiveSourcesObserverScope(const RuntimeVisionLiveSourcesObserverScope&)=delete;
    RuntimeVisionLiveSourcesObserverScope& operator=(const RuntimeVisionLiveSourcesObserverScope&)=delete;
};

// ONE update's lexical read-only range. The cargo loop must visit every host,
// including dead hosts, and append addresses in that same order. No health,
// instanceId, transport, mission or owner filtering occurs here. The original
// source predicate remains in each loop. The concrete update call graph has no
// external callbacks and does not edit World; callers must not mutate World
// concurrently. This is not an alive cache or a pointer/count revision scheme.
class RuntimeVisionLiveSources {
    const GameWorld& world_;
    const std::vector<WorldObjectInstance>& original_;
    std::vector<const WorldObjectInstance*> alive_;
    int tick_;
    RuntimeVisionLiveSourcesStats stats_;
public:
    explicit RuntimeVisionLiveSources(const GameWorld& world,int tick,
        bool reference=runtimeVisionLiveSourcesReference()) noexcept
        : world_(world),original_(world.getObjects()),tick_(tick) { stats_.reference=reference; }
    ~RuntimeVisionLiveSources() {
        // Test-only observer executes after ALL source accesses. It must not
        // throw, mutate or destroy the world, or retain addresses from it.
        if(runtimeVisionLiveSourcesObserver)
            runtimeVisionLiveSourcesObserver(runtimeVisionLiveSourcesObserverContext,world_,tick_,stats_);
    }
    RuntimeVisionLiveSources(const RuntimeVisionLiveSources&)=delete;
    RuntimeVisionLiveSources& operator=(const RuntimeVisionLiveSources&)=delete;
    RuntimeVisionLiveSources(RuntimeVisionLiveSources&&)=delete;
    RuntimeVisionLiveSources& operator=(RuntimeVisionLiveSources&&)=delete;
    void appendDuringCargoScan(const WorldObjectInstance& object) {
        ++stats_.cargoHostVisits;
        if(object.alive) {
            ++stats_.aliveAddresses;
            if(!stats_.reference) alive_.push_back(&object);
        }
    }
    std::size_t sourceCount() const noexcept {
        return stats_.reference?original_.size():alive_.size();
    }
    const RuntimeVisionLiveSourcesStats& stats() const noexcept { return stats_; }
    class Iterator {
        const RuntimeVisionLiveSources* owner_;
        std::size_t index_;
    public:
        Iterator(const RuntimeVisionLiveSources* owner,std::size_t index) noexcept : owner_(owner),index_(index) {}
        const WorldObjectInstance& operator*() const noexcept {
            return owner_->stats_.reference?owner_->original_[index_]:*owner_->alive_[index_];
        }
        Iterator& operator++() noexcept { ++index_; return *this; }
        bool operator!=(const Iterator& other) const noexcept { return index_!=other.index_; }
    };
    Iterator begin() const noexcept { return Iterator(this,0); }
    Iterator end() const noexcept { return Iterator(this,sourceCount()); }
    const RuntimeVisionLiveSources& pass() noexcept {
        ++stats_.sourcePasses;
        stats_.sourceRangeEntries+=sourceCount();
        stats_.originalRangeEntries+=original_.size();
        return *this;
    }
};
