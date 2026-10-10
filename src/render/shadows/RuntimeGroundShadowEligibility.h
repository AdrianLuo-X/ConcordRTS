#pragma once

#include "render/core/RuntimeRendererEnvironment.h"

#include <cstdint>
#include <optional>

// Diagnostic override only. Normal rendering resolves the environment once at
// drawRuntimeMap entry; this neither memoizes objects nor retains world pointers.
inline thread_local std::optional<bool> runtimeGroundShadowEligibilityReferenceOverride;
class RuntimeGroundShadowEligibilityReferenceScope {
public:
    explicit RuntimeGroundShadowEligibilityReferenceScope(bool enabled)
        : previous_(runtimeGroundShadowEligibilityReferenceOverride) {
        runtimeGroundShadowEligibilityReferenceOverride = enabled;
    }
    ~RuntimeGroundShadowEligibilityReferenceScope() {
        runtimeGroundShadowEligibilityReferenceOverride = previous_;
    }
    RuntimeGroundShadowEligibilityReferenceScope(const RuntimeGroundShadowEligibilityReferenceScope&) = delete;
    RuntimeGroundShadowEligibilityReferenceScope& operator=(const RuntimeGroundShadowEligibilityReferenceScope&) = delete;
private:
    std::optional<bool> previous_;
};

inline bool runtimeGroundShadowEligibilityReferenceForRender() {
    if (runtimeGroundShadowEligibilityReferenceOverride)
        return *runtimeGroundShadowEligibilityReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_GROUND_SHADOW_ELIGIBILITY_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}

// Opt-in observation for the actual complete-map pixel contract. No per-object
// clock or allocation; the default null pointer never changes render statistics.
struct RuntimeGroundShadowEligibilityObservation {
    std::uint64_t visibilityQueries = 0;
    std::uint64_t retainedWreckQueries = 0;
    std::uint64_t deadRejected = 0;
    std::uint64_t eligible = 0;
    std::uint64_t crashingEligible = 0;
    std::uint64_t sellingEligible = 0;
    std::uint64_t collapseEligible = 0;
};
inline thread_local RuntimeGroundShadowEligibilityObservation* runtimeGroundShadowEligibilityObservation = nullptr;
class RuntimeGroundShadowEligibilityObservationScope {
public:
    explicit RuntimeGroundShadowEligibilityObservationScope(RuntimeGroundShadowEligibilityObservation& value)
        : previous_(runtimeGroundShadowEligibilityObservation) {
        runtimeGroundShadowEligibilityObservation = &value;
    }
    ~RuntimeGroundShadowEligibilityObservationScope() {
        runtimeGroundShadowEligibilityObservation = previous_;
    }
    RuntimeGroundShadowEligibilityObservationScope(const RuntimeGroundShadowEligibilityObservationScope&) = delete;
    RuntimeGroundShadowEligibilityObservationScope& operator=(const RuntimeGroundShadowEligibilityObservationScope&) = delete;
private:
    RuntimeGroundShadowEligibilityObservation* previous_;
};
