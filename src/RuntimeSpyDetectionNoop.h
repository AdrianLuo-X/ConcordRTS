#pragma once

#include "RuntimeSpySystem.h"
#include <algorithm>
#include <cstdlib>
#include <type_traits>

inline thread_local int runtimeSpyDetectionNoopReferenceOverride = -1;
inline bool runtimeSpyDetectionNoopReference() {
    if (runtimeSpyDetectionNoopReferenceOverride >= 0)
        return runtimeSpyDetectionNoopReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_SPY_DETECTION_NOOP_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeSpyDetectionNoopReferenceScope {
    int previous_;
public:
    explicit RuntimeSpyDetectionNoopReferenceScope(bool reference)
        : previous_(runtimeSpyDetectionNoopReferenceOverride) {
        runtimeSpyDetectionNoopReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSpyDetectionNoopReferenceScope() {
        runtimeSpyDetectionNoopReferenceOverride = previous_;
    }
    RuntimeSpyDetectionNoopReferenceScope(const RuntimeSpyDetectionNoopReferenceScope&) = delete;
    RuntimeSpyDetectionNoopReferenceScope& operator=(const RuntimeSpyDetectionNoopReferenceScope&) = delete;
};
using RuntimeSpyDetectionNoopObserver = void(*)(void*, const RuntimeSpyDetectionUpdate&, bool);
inline thread_local RuntimeSpyDetectionNoopObserver runtimeSpyDetectionNoopObserver = nullptr;
inline thread_local void* runtimeSpyDetectionNoopObserverContext = nullptr;
class RuntimeSpyDetectionNoopObserverScope {
    RuntimeSpyDetectionNoopObserver previous_;
    void* previousContext_;
public:
    RuntimeSpyDetectionNoopObserverScope(RuntimeSpyDetectionNoopObserver observer, void* context)
        : previous_(runtimeSpyDetectionNoopObserver), previousContext_(runtimeSpyDetectionNoopObserverContext) {
        runtimeSpyDetectionNoopObserver = observer;
        runtimeSpyDetectionNoopObserverContext = context;
    }
    ~RuntimeSpyDetectionNoopObserverScope() {
        runtimeSpyDetectionNoopObserver = previous_;
        runtimeSpyDetectionNoopObserverContext = previousContext_;
    }
    RuntimeSpyDetectionNoopObserverScope(const RuntimeSpyDetectionNoopObserverScope&) = delete;
    RuntimeSpyDetectionNoopObserverScope& operator=(const RuntimeSpyDetectionNoopObserverScope&) = delete;
};

// Call AFTER ensure/default initialization, advance/rearm, and applying its
// actions. Only active disguise records can contribute a detection pair.
// An empty context makes the ORIGINAL updateDetection produce next={}, then
// retain its old-pair Conceal actions, sortState and detectedPairs assignment.
// No world/owner view is cached, and no service clock or cleanup is skipped.
template<class MakeContext>
RuntimeSpyDetectionUpdate runtimeSpyDetectionUpdateWithNoopContext(
    RuntimeSpySystemState& spies, int tick, MakeContext makeContext) {
    const bool skipContext = !runtimeSpyDetectionNoopReference()
        && std::none_of(spies.disguiseStates().begin(), spies.disguiseStates().end(),
            [](const RuntimeSpyDisguiseState& disguise) { return disguise.active; });
    using Context = std::invoke_result_t<MakeContext>;
    auto result = skipContext ? spies.updateDetection(Context{}, tick)
        : spies.updateDetection(makeContext(), tick);
    if (runtimeSpyDetectionNoopObserver)
        runtimeSpyDetectionNoopObserver(runtimeSpyDetectionNoopObserverContext, result, skipContext);
    return result;
}
