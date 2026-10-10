#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

inline thread_local int runtimePathDynamicPreparationReferenceOverride = -1;
inline bool runtimePathDynamicPreparationReference() {
    if (runtimePathDynamicPreparationReferenceOverride >= 0)
        return runtimePathDynamicPreparationReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_DYNAMIC_PREPARATION_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathDynamicPreparationReferenceScope {
    int previous_;
public:
    explicit RuntimePathDynamicPreparationReferenceScope(bool reference) noexcept
        : previous_(runtimePathDynamicPreparationReferenceOverride) {
        runtimePathDynamicPreparationReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathDynamicPreparationReferenceScope() {
        runtimePathDynamicPreparationReferenceOverride = previous_;
    }
    RuntimePathDynamicPreparationReferenceScope(const RuntimePathDynamicPreparationReferenceScope&) = delete;
    RuntimePathDynamicPreparationReferenceScope& operator=(const RuntimePathDynamicPreparationReferenceScope&) = delete;
};

// Exact reduction of EVERY original authored row, including duplicate rows.
// Published only after the owning Workset completes admission successfully.
struct RuntimePathDynamicPreparationBounds {
    int minimumX = 0, maximumX = 0, minimumY = 0, maximumY = 0;
};
struct RuntimePathDynamicPreparationStats {
    bool reference = false, earlyPrepared = false, admitted = false;
    std::size_t entries = 0, boundsPassesAvoided = 0, boundsRowsAvoided = 0;
    std::size_t infantryRangeEntries = 0, infantryOriginalRangeEntries = 0;
    std::size_t infantryDestinations = 0;
};
using RuntimePathDynamicPreparationObserver = void(*)(void*, const void*,
    const RuntimePathDynamicPreparationStats&);
inline thread_local RuntimePathDynamicPreparationObserver runtimePathDynamicPreparationObserver = nullptr;
inline thread_local void* runtimePathDynamicPreparationObserverContext = nullptr;
inline thread_local const void* runtimeActivePathDynamicPreparation = nullptr;
class RuntimePathDynamicPreparationObserverScope {
    RuntimePathDynamicPreparationObserver previous_;
    void* previousContext_;
public:
    RuntimePathDynamicPreparationObserverScope(RuntimePathDynamicPreparationObserver observer,
        void* context) noexcept
        : previous_(runtimePathDynamicPreparationObserver),
          previousContext_(runtimePathDynamicPreparationObserverContext) {
        runtimePathDynamicPreparationObserver = observer;
        runtimePathDynamicPreparationObserverContext = context;
    }
    ~RuntimePathDynamicPreparationObserverScope() {
        runtimePathDynamicPreparationObserver = previous_;
        runtimePathDynamicPreparationObserverContext = previousContext_;
    }
    RuntimePathDynamicPreparationObserverScope(const RuntimePathDynamicPreparationObserverScope&) = delete;
    RuntimePathDynamicPreparationObserverScope& operator=(const RuntimePathDynamicPreparationObserverScope&) = delete;
};
inline bool runtimePathDynamicPreparationTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("CONCORD_PATH_DYNAMIC_PREPARATION_TRACE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
inline void runtimeRecordPathDynamicPreparation(const RuntimePathDynamicPreparationStats& value) {
    if (!runtimePathDynamicPreparationTraceEnabled()) return;
    struct Totals {
        std::uint64_t calls=0,early=0,admitted=0,entries=0,boundsPasses=0,boundsRows=0;
        std::uint64_t infantryRange=0,infantryOriginalRange=0,destinations=0;
        ~Totals() {
            std::cerr << "Runtime path dynamic preparation diagnostic: calls=" << calls
                << " earlyPrepared=" << early << " admitted=" << admitted << " entries=" << entries
                << " boundsPassesAvoided=" << boundsPasses << " boundsRowsAvoided=" << boundsRows
                << " infantryRangeEntries=" << infantryRange << " infantryOriginalRangeEntries=" << infantryOriginalRange
                << " infantryDestinations=" << destinations << '\n';
        }
    };
    static thread_local Totals totals;
    ++totals.calls;totals.early+=value.earlyPrepared;totals.admitted+=value.admitted;
    totals.entries+=value.entries;totals.boundsPasses+=value.boundsPassesAvoided;
    totals.boundsRows+=value.boundsRowsAvoided;totals.infantryRange+=value.infantryRangeEntries;
    totals.infantryOriginalRange+=value.infantryOriginalRangeEntries;totals.destinations+=value.infantryDestinations;
}
