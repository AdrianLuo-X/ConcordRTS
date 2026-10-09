// Include beside RuntimePathSnapshotMetadataQueries.inl inside the original
// snapshot-builder translation unit. No global/persistent path data is added.
#pragma once

struct RuntimePathSnapshotConditionalQueryStats {
    bool reference=false;
    std::size_t metadataLookups=0,metadataSkipped=0;
    std::size_t occupantLookups=0,occupantSkipped=0;
};
using RuntimePathSnapshotConditionalQueryObserver=void(*)(void*,const RuntimeWindowState&,
    const RuntimePathSnapshotConditionalQueryStats&);
inline thread_local RuntimePathSnapshotConditionalQueryObserver runtimePathSnapshotConditionalQueryObserver=nullptr;
inline thread_local void* runtimePathSnapshotConditionalQueryObserverContext=nullptr;
class RuntimePathSnapshotConditionalQueryObserverScope {
    RuntimePathSnapshotConditionalQueryObserver previous_;
    void* previousContext_;
public:
    RuntimePathSnapshotConditionalQueryObserverScope(RuntimePathSnapshotConditionalQueryObserver observer,
        void* context) noexcept
        : previous_(runtimePathSnapshotConditionalQueryObserver),
          previousContext_(runtimePathSnapshotConditionalQueryObserverContext) {
        runtimePathSnapshotConditionalQueryObserver=observer;
        runtimePathSnapshotConditionalQueryObserverContext=context;
    }
    ~RuntimePathSnapshotConditionalQueryObserverScope() {
        runtimePathSnapshotConditionalQueryObserver=previous_;
        runtimePathSnapshotConditionalQueryObserverContext=previousContext_;
    }
    RuntimePathSnapshotConditionalQueryObserverScope(const RuntimePathSnapshotConditionalQueryObserverScope&)=delete;
    RuntimePathSnapshotConditionalQueryObserverScope& operator=(const RuntimePathSnapshotConditionalQueryObserverScope&)=delete;
};

// A null result only bypasses a pure find whose ENTIRE original use is gated
// by the same `used` boolean. Metadata and occupancy get separate booleans:
// land permission or repair can block a cell between the two old finds.
// Reference=true keeps both old unconditional calls and their original order.
// The existing metadata reference flag controls this once at construction;
// terrain topology, occupant representation and all other flags stay separate.
class RuntimePathSnapshotConditionalQueries {
    const RuntimeWindowState& state_;
    RuntimePathSnapshotConditionalQueryObserver observer_;
    void* context_;
    RuntimePathSnapshotConditionalQueryStats stats_;
public:
    explicit RuntimePathSnapshotConditionalQueries(const RuntimeWindowState& state)
        : state_(state),observer_(runtimePathSnapshotConditionalQueryObserver),
          context_(runtimePathSnapshotConditionalQueryObserverContext) {
        stats_.reference=runtimePathSnapshotMetadataQueryReference();
    }
    ~RuntimePathSnapshotConditionalQueries() {
        // Test-only diagnostic, after all per-cell queries. It must not throw,
        // destroy/edit the state or retain its borrowed addresses.
        if(observer_) observer_(context_,state_,stats_);
    }
    RuntimePathSnapshotConditionalQueries(const RuntimePathSnapshotConditionalQueries&)=delete;
    RuntimePathSnapshotConditionalQueries& operator=(const RuntimePathSnapshotConditionalQueries&)=delete;
    template<class Find>
    auto metadata(bool used,Find&& find) -> decltype(find()) {
        if(!used && !stats_.reference) {
            if(observer_) ++stats_.metadataSkipped;
            return nullptr;
        }
        if(observer_) ++stats_.metadataLookups;
        return find();
    }
    template<class Find>
    auto occupant(bool used,Find&& find) -> decltype(find()) {
        if(!used && !stats_.reference) {
            if(observer_) ++stats_.occupantSkipped;
            return nullptr;
        }
        if(observer_) ++stats_.occupantLookups;
        return find();
    }
};
