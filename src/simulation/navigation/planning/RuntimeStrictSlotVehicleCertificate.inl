// Command-local proof only. The complete BFS and assignment implementation stay
// unchanged. This is not a partial component map or a persistent value cache.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>



class RuntimeStrictSlotVehicleReferenceScope {
    int previous_;
public:
    explicit RuntimeStrictSlotVehicleReferenceScope(bool reference) noexcept
        : previous_(runtimeStrictSlotVehicleReferenceOverride) {
        runtimeStrictSlotVehicleReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeStrictSlotVehicleReferenceScope() { runtimeStrictSlotVehicleReferenceOverride = previous_; }
    RuntimeStrictSlotVehicleReferenceScope(const RuntimeStrictSlotVehicleReferenceScope&) = delete;
    RuntimeStrictSlotVehicleReferenceScope& operator=(const RuntimeStrictSlotVehicleReferenceScope&) = delete;
};



class RuntimeStrictSlotVehicleStatsScope {
    RuntimeStrictSlotVehicleCertificateStats* previous_;
public:
    explicit RuntimeStrictSlotVehicleStatsScope(RuntimeStrictSlotVehicleCertificateStats& stats) noexcept
        : previous_(runtimeStrictSlotVehicleStats) { runtimeStrictSlotVehicleStats=&stats; }
    ~RuntimeStrictSlotVehicleStatsScope() { runtimeStrictSlotVehicleStats=previous_; }
    RuntimeStrictSlotVehicleStatsScope(const RuntimeStrictSlotVehicleStatsScope&)=delete;
    RuntimeStrictSlotVehicleStatsScope& operator=(const RuntimeStrictSlotVehicleStatsScope&)=delete;
};



// Every successful proof is completed before any command assignment. The only
// diagnostic sink is POD aggregation; there is no callback/reentry in the proof.
// provenPoints is contract-only and receives an owned copy AFTER proof completion.

