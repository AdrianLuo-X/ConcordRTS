// Include beside RuntimePathSnapshotMetadataQueries.inl inside the original
// snapshot-builder translation unit. No global/persistent path data is added.
#pragma once





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

