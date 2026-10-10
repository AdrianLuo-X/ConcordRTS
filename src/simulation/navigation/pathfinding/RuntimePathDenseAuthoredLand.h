#pragma once
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

inline thread_local int runtimePathDenseAuthoredLandReferenceOverride = -1;
inline bool runtimePathDenseAuthoredLandReference() {
    if (runtimePathDenseAuthoredLandReferenceOverride >= 0)
        return runtimePathDenseAuthoredLandReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_DENSE_AUTHORED_LAND_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathDenseAuthoredLandReferenceScope {
    int previous_;
public:
    explicit RuntimePathDenseAuthoredLandReferenceScope(bool reference) noexcept
        : previous_(runtimePathDenseAuthoredLandReferenceOverride) {
        runtimePathDenseAuthoredLandReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathDenseAuthoredLandReferenceScope() { runtimePathDenseAuthoredLandReferenceOverride = previous_; }
    RuntimePathDenseAuthoredLandReferenceScope(const RuntimePathDenseAuthoredLandReferenceScope&) = delete;
    RuntimePathDenseAuthoredLandReferenceScope& operator=(const RuntimePathDenseAuthoredLandReferenceScope&) = delete;
};

struct RuntimePathOriginalAuthoredLandPermission {
    static constexpr bool exactDenseAuthoredLand = false;
};

// Private production policy: one immutable builder borrows the owning map's
// current writer-maintained ordinal array. Canonical-domain proof is performed
// by the unchanged Workset loop before allows() is called. The permission
// callable accepts an ordinal, never a fabricated RuntimeTerrainInfo.
template<class AllowsOrdinal>
class RuntimePathDenseAuthoredLandPermission {
    const std::vector<std::int64_t>* values_;
    AllowsOrdinal allows_;
public:
    static constexpr bool exactDenseAuthoredLand = true;
    static constexpr std::int64_t missing = std::numeric_limits<std::int64_t>::min();
    RuntimePathDenseAuthoredLandPermission(const std::vector<std::int64_t>* values, AllowsOrdinal allows)
        : values_(values), allows_(allows) {}
    bool available() const noexcept { return values_ && values_->size() == 512U * 512U; }
    bool allows(int x, int y) const {
        const auto value = (*values_)[static_cast<std::size_t>(y) * 512U + static_cast<std::size_t>(x)];
        return value == missing || allows_(value);
    }
};

// Test-only once-per-builder POD observer. This is deliberately separate from
// the old Workset/Preparation observers, whose presence forces the old route.
// The callback must not throw, mutate any source or retain borrowed addresses.
struct RuntimePathDenseAuthoredLandPreparationStats {
    bool eligible=false,arrayAcquired=false,policyDispatched=false;
    bool proofAdmitted=false,denseAdmitted=false;
    std::size_t entries=0,candidates=0;
};
using RuntimePathDenseAuthoredLandPreparationObserver=void(*)(void*,const void*,const void*,int,
    const RuntimePathDenseAuthoredLandPreparationStats&) noexcept;
inline thread_local RuntimePathDenseAuthoredLandPreparationObserver runtimePathDenseAuthoredLandPreparationObserver=nullptr;
inline thread_local void* runtimePathDenseAuthoredLandPreparationObserverContext=nullptr;
class RuntimePathDenseAuthoredLandPreparationObserverScope {
    RuntimePathDenseAuthoredLandPreparationObserver previous_;
    void* previousContext_;
public:
    RuntimePathDenseAuthoredLandPreparationObserverScope(RuntimePathDenseAuthoredLandPreparationObserver observer,void* context) noexcept
        :previous_(runtimePathDenseAuthoredLandPreparationObserver),previousContext_(runtimePathDenseAuthoredLandPreparationObserverContext) {
        runtimePathDenseAuthoredLandPreparationObserver=observer;
        runtimePathDenseAuthoredLandPreparationObserverContext=context;
    }
    ~RuntimePathDenseAuthoredLandPreparationObserverScope() {
        runtimePathDenseAuthoredLandPreparationObserver=previous_;
        runtimePathDenseAuthoredLandPreparationObserverContext=previousContext_;
    }
    RuntimePathDenseAuthoredLandPreparationObserverScope(const RuntimePathDenseAuthoredLandPreparationObserverScope&)=delete;
    RuntimePathDenseAuthoredLandPreparationObserverScope& operator=(const RuntimePathDenseAuthoredLandPreparationObserverScope&)=delete;
};