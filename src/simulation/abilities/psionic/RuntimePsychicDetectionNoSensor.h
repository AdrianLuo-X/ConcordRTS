#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>

// A call-local policy, never a world cache. Only the explicitly read-only
// production caller opts in; arbitrary public callbacks keep the old path.
inline thread_local int runtimePsychicNoSensorReferenceOverride = -1;
inline bool runtimePsychicNoSensorReference() {
    if (runtimePsychicNoSensorReferenceOverride >= 0)
        return runtimePsychicNoSensorReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PSYCHIC_DETECTION_NO_SENSOR_REFERENCE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return reference;
}
struct RuntimePsychicNoSensorReferenceScope {
    int previous = runtimePsychicNoSensorReferenceOverride;
    explicit RuntimePsychicNoSensorReferenceScope(bool value) {
        runtimePsychicNoSensorReferenceOverride = value ? 1 : 0;
    }
    ~RuntimePsychicNoSensorReferenceScope() { runtimePsychicNoSensorReferenceOverride = previous; }
    RuntimePsychicNoSensorReferenceScope(const RuntimePsychicNoSensorReferenceScope&) = delete;
    RuntimePsychicNoSensorReferenceScope& operator=(const RuntimePsychicNoSensorReferenceScope&) = delete;
};
struct RuntimePsychicNoSensorStats {
    std::size_t originalCalls = 0, readOnlyCalls = 0;
    std::size_t sensorScanEntries = 0, sensors = 0;
    std::size_t indexConstructions = 0, indexScanEntries = 0;
    std::size_t indexInsertionAttempts = 0, uniqueIndexEntries = 0;
    std::size_t noSensorOmissions = 0;
};
using RuntimePsychicNoSensorObserver = void(*)(const RuntimePsychicNoSensorStats&, void*) noexcept;
inline thread_local RuntimePsychicNoSensorObserver runtimePsychicNoSensorObserver = nullptr;
inline thread_local void* runtimePsychicNoSensorObserverContext = nullptr;
struct RuntimePsychicNoSensorObserverScope {
    RuntimePsychicNoSensorObserver previous = runtimePsychicNoSensorObserver;
    void* previousContext = runtimePsychicNoSensorObserverContext;
    RuntimePsychicNoSensorObserverScope(RuntimePsychicNoSensorObserver observer, void* context) {
        runtimePsychicNoSensorObserver = observer; runtimePsychicNoSensorObserverContext = context;
    }
    ~RuntimePsychicNoSensorObserverScope() {
        runtimePsychicNoSensorObserver = previous; runtimePsychicNoSensorObserverContext = previousContext;
    }
    RuntimePsychicNoSensorObserverScope(const RuntimePsychicNoSensorObserverScope&) = delete;
    RuntimePsychicNoSensorObserverScope& operator=(const RuntimePsychicNoSensorObserverScope&) = delete;
};
inline void runtimeObservePsychicNoSensor(bool deferred, std::size_t sourceCount,
    std::size_t sensorCount, std::size_t uniqueIndexEntries) noexcept {
    if (!runtimePsychicNoSensorObserver) return;
    RuntimePsychicNoSensorStats stats;
    stats.originalCalls = !deferred; stats.readOnlyCalls = deferred;
    stats.sensorScanEntries = sourceCount; stats.sensors = sensorCount;
    const bool builtIndex = !deferred || sensorCount != 0;
    stats.indexConstructions = builtIndex;
    stats.indexScanEntries = builtIndex ? sourceCount : 0;
    stats.indexInsertionAttempts = stats.indexScanEntries;
    stats.uniqueIndexEntries = uniqueIndexEntries;
    stats.noSensorOmissions = deferred && sensorCount == 0;
    runtimePsychicNoSensorObserver(stats, runtimePsychicNoSensorObserverContext);
}
