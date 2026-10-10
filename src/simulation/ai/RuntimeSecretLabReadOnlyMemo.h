#pragma once
#include "simulation/buildings/RuntimeSpecialBuildingSystem.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

// Private opt-in lease. Public special-building signatures remain unchanged.
// Pointer/count checks are defensive, not a semantic mutation generation.
inline thread_local int runtimeSecretLabMemoReferenceOverride = -1;
inline bool runtimeSecretLabMemoReference() noexcept {
    if (runtimeSecretLabMemoReferenceOverride >= 0)
        return runtimeSecretLabMemoReferenceOverride != 0;
    static const bool reference = [] {
        const char* flag = std::getenv("CONCORD_PAINT_SECRET_LAB_LIST_REFERENCE");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    return reference;
}
struct RuntimeSecretLabMemoReferenceScope {
    int previous = runtimeSecretLabMemoReferenceOverride;
    explicit RuntimeSecretLabMemoReferenceScope(bool reference) noexcept {
        runtimeSecretLabMemoReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSecretLabMemoReferenceScope() { runtimeSecretLabMemoReferenceOverride = previous; }
    RuntimeSecretLabMemoReferenceScope(const RuntimeSecretLabMemoReferenceScope&) = delete;
    RuntimeSecretLabMemoReferenceScope& operator=(const RuntimeSecretLabMemoReferenceScope&) = delete;
};
struct RuntimeSecretLabMemoObservation {
    std::uint64_t queries = 0, fullBuilds = 0, hits = 0, publications = 0;
    std::uint64_t invalidations = 0, capFallbacks = 0;
};
inline thread_local RuntimeSecretLabMemoObservation* runtimeSecretLabMemoObservation = nullptr;
struct RuntimeSecretLabMemoObservationScope {
    RuntimeSecretLabMemoObservation* previous = runtimeSecretLabMemoObservation;
    explicit RuntimeSecretLabMemoObservationScope(RuntimeSecretLabMemoObservation& value) noexcept {
        runtimeSecretLabMemoObservation = &value;
    }
    ~RuntimeSecretLabMemoObservationScope() { runtimeSecretLabMemoObservation = previous; }
    RuntimeSecretLabMemoObservationScope(const RuntimeSecretLabMemoObservationScope&) = delete;
    RuntimeSecretLabMemoObservationScope& operator=(const RuntimeSecretLabMemoObservationScope&) = delete;
};

class RuntimeSecretLabReadOnlyMemoScope {
public:
    static_assert(std::is_nothrow_move_assignable_v<std::vector<RuntimeSpecialBuildingUnlock>>);
    static constexpr std::size_t maximumUnlocks = 4096;
    inline static thread_local RuntimeSecretLabReadOnlyMemoScope* active = nullptr;
    explicit RuntimeSecretLabReadOnlyMemoScope(const RuntimeSpecialBuildingContext& context,
        bool readonlyInputs = false, bool reference = runtimeSecretLabMemoReference()) noexcept
        : previous_(active), context_(&context), rules_(context.rules), objects_(context.objects),
          data_(context.objects ? context.objects->data() : nullptr),
          count_(context.objects ? context.objects->size() : 0),
          fixedTickHz_(context.fixedTickHz), enabled_(readonlyInputs && !reference && !previous_
            && context.rules && context.objects) {
        if (previous_) previous_->invalidate();
        active = this;
    }
    ~RuntimeSecretLabReadOnlyMemoScope() { active = previous_; }
    RuntimeSecretLabReadOnlyMemoScope(const RuntimeSecretLabReadOnlyMemoScope&) = delete;
    RuntimeSecretLabReadOnlyMemoScope& operator=(const RuntimeSecretLabReadOnlyMemoScope&) = delete;
    void invalidate() noexcept {
        if (enabled_ && runtimeSecretLabMemoObservation)
            ++runtimeSecretLabMemoObservation->invalidations;
        enabled_ = ready_ = false;
    }
    bool accepts(const RuntimeSpecialBuildingContext& context) noexcept {
        if (!enabled_ || active != this || context_ != &context) return false;
        if (context.rules != rules_ || context.objects != objects_
            || context.fixedTickHz != fixedTickHz_ || !objects_
            || objects_->data() != data_ || objects_->size() != count_) {
            invalidate(); return false;
        }
        return true;
    }
    const std::vector<RuntimeSpecialBuildingUnlock>* borrow(
        const RuntimeSpecialBuildingContext& context) noexcept {
        if (!accepts(context) || !ready_) return nullptr;
        if (runtimeSecretLabMemoObservation) ++runtimeSecretLabMemoObservation->hits;
        return &unlocks_;
    }
    const std::vector<RuntimeSpecialBuildingUnlock>* publish(
        const RuntimeSpecialBuildingContext& context,
        std::vector<RuntimeSpecialBuildingUnlock>& completed) noexcept {
        if (!accepts(context)) return nullptr;
        if (completed.size() > maximumUnlocks) {
            if (runtimeSecretLabMemoObservation) ++runtimeSecretLabMemoObservation->capFallbacks;
            invalidate(); return nullptr;
        }
        // The original producer has completed all callbacks, selection and
        // sorting. Default-allocator vector move allocates/copies nothing.
        unlocks_ = std::move(completed);
        ready_ = true;
        if (runtimeSecretLabMemoObservation) ++runtimeSecretLabMemoObservation->publications;
        return &unlocks_;
    }
private:
    RuntimeSecretLabReadOnlyMemoScope* previous_;
    const RuntimeSpecialBuildingContext* context_;
    const RulesDatabase* rules_;
    const std::vector<WorldObjectInstance>* objects_;
    const WorldObjectInstance* data_;
    std::size_t count_;
    int fixedTickHz_;
    bool enabled_, ready_ = false;
    std::vector<RuntimeSpecialBuildingUnlock> unlocks_;
};

inline const std::vector<RuntimeSpecialBuildingUnlock>& runtimeSecretLabUnlocksForQuery(
    const RuntimeSpecialBuildingContext& context,
    std::vector<RuntimeSpecialBuildingUnlock>& originalScratch) {
    auto* observation = runtimeSecretLabMemoObservation;
    if (observation) ++observation->queries;
    auto* lease = RuntimeSecretLabReadOnlyMemoScope::active;
    if (lease) if (const auto* result = lease->borrow(context)) return *result;
    if (observation) ++observation->fullBuilds;
    // Do not catch producer failures or alter general callback execution.
    originalScratch = RuntimeSpecialBuildingSystem::secretLabUnlocks(context);
    if (lease) if (const auto* result = lease->publish(context, originalScratch)) return *result;
    return originalScratch;
}
