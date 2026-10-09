#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

// One call owns all reused data. No world pointer, tick, revision or content
// answer survives a call. This reference restores the four original routes.
inline thread_local int runtimeSimulationStageAdmissionOverride = -1;
inline bool runtimeSimulationStageAdmissionReference() noexcept {
    if (runtimeSimulationStageAdmissionOverride >= 0)
        return runtimeSimulationStageAdmissionOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_SIMULATION_STAGE_ADMISSION_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return reference;
}
class RuntimeSimulationStageAdmissionReferenceScope {
public:
    explicit RuntimeSimulationStageAdmissionReferenceScope(bool reference) noexcept
        : previous_(runtimeSimulationStageAdmissionOverride) {
        runtimeSimulationStageAdmissionOverride = reference ? 1 : 0;
    }
    ~RuntimeSimulationStageAdmissionReferenceScope() noexcept {
        runtimeSimulationStageAdmissionOverride = previous_;
    }
    RuntimeSimulationStageAdmissionReferenceScope(const RuntimeSimulationStageAdmissionReferenceScope&) = delete;
    RuntimeSimulationStageAdmissionReferenceScope& operator=(const RuntimeSimulationStageAdmissionReferenceScope&) = delete;
private:
    int previous_;
};

enum class RuntimeSimulationStageAdmissionKind { Aircraft, Subcell, Cloak, TickOccupancy };
struct RuntimeSimulationStageAdmissionStats {
    RuntimeSimulationStageAdmissionKind kind{};
    bool reference = false;
    bool admitted = false;
    std::size_t sourceEntries = 0;
    // These are candidate range slots or requested reserve entries, not
    // measured visits, allocator calls, retained capacity, or elapsed time.
    std::size_t omittedRangeEntries = 0;
    std::size_t originalReserveEntries = 0;
    std::size_t selectedReserveEntries = 0;
};
using RuntimeSimulationStageAdmissionObserver = void (*)(
    const RuntimeSimulationStageAdmissionStats&, void*) noexcept;
inline thread_local RuntimeSimulationStageAdmissionObserver runtimeSimulationStageAdmissionObserver = nullptr;
inline thread_local void* runtimeSimulationStageAdmissionObserverContext = nullptr;
class RuntimeSimulationStageAdmissionObserverScope {
public:
    RuntimeSimulationStageAdmissionObserverScope(RuntimeSimulationStageAdmissionObserver observer, void* context) noexcept
        : previous_(runtimeSimulationStageAdmissionObserver), previousContext_(runtimeSimulationStageAdmissionObserverContext) {
        runtimeSimulationStageAdmissionObserver = observer;
        runtimeSimulationStageAdmissionObserverContext = context;
    }
    ~RuntimeSimulationStageAdmissionObserverScope() noexcept {
        runtimeSimulationStageAdmissionObserver = previous_;
        runtimeSimulationStageAdmissionObserverContext = previousContext_;
    }
    RuntimeSimulationStageAdmissionObserverScope(const RuntimeSimulationStageAdmissionObserverScope&) = delete;
    RuntimeSimulationStageAdmissionObserverScope& operator=(const RuntimeSimulationStageAdmissionObserverScope&) = delete;
private:
    RuntimeSimulationStageAdmissionObserver previous_;
    void* previousContext_;
};

// Construct before the stage work. Destruction occurs after every real stage
// callback/read/write, including early returns. The observer is diagnostic,
// noexcept, and must not destroy the caller's world. Normal calls do not count
// individual objects or read a clock.
class RuntimeSimulationStageAdmissionObservation {
public:
    RuntimeSimulationStageAdmissionObservation(RuntimeSimulationStageAdmissionKind kind, bool reference, std::size_t count) noexcept
        : stats{kind, reference, false, count}, observer_(runtimeSimulationStageAdmissionObserver),
          context_(runtimeSimulationStageAdmissionObserverContext) {}
    ~RuntimeSimulationStageAdmissionObservation() noexcept {
        if (observer_ != nullptr) observer_(stats, context_);
    }
    RuntimeSimulationStageAdmissionObservation(const RuntimeSimulationStageAdmissionObservation&) = delete;
    RuntimeSimulationStageAdmissionObservation& operator=(const RuntimeSimulationStageAdmissionObservation&) = delete;
    RuntimeSimulationStageAdmissionStats stats;
private:
    RuntimeSimulationStageAdmissionObserver observer_;
    void* context_;
};

inline std::size_t runtimeSimulationStageInitialReserve(std::size_t original, bool reference) noexcept {
    // Only eager reservation is capped. Ordinary container growth is unlimited.
    return reference ? original : std::min(original, std::size_t{256});
}
