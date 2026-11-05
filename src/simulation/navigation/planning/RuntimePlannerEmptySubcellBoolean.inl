// Include immediately before runtimeCellIsPassableUncached.
#include "simulation/navigation/occupancy/RuntimeEmptySubcellAvailability.h"



class RuntimePlannerEmptySubcellBooleanReferenceScope {
    int previous_;
public:
    explicit RuntimePlannerEmptySubcellBooleanReferenceScope(bool reference) noexcept
        : previous_(runtimePlannerEmptySubcellBooleanReferenceOverride) {
        runtimePlannerEmptySubcellBooleanReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePlannerEmptySubcellBooleanReferenceScope() {
        runtimePlannerEmptySubcellBooleanReferenceOverride = previous_;
    }
    RuntimePlannerEmptySubcellBooleanReferenceScope(const RuntimePlannerEmptySubcellBooleanReferenceScope&) = delete;
    RuntimePlannerEmptySubcellBooleanReferenceScope& operator=(const RuntimePlannerEmptySubcellBooleanReferenceScope&) = delete;
};
// Defined after the original passability observer declaration. Original
// observers retain every original allocation/query/census route and count.





class RuntimePlannerEmptySubcellBooleanObserverScope {
    RuntimePlannerEmptySubcellBooleanObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerEmptySubcellBooleanObserverScope(RuntimePlannerEmptySubcellBooleanObserver observer, void* context) noexcept
        : previous_(runtimePlannerEmptySubcellBooleanObserver), previousContext_(runtimePlannerEmptySubcellBooleanObserverContext) {
        runtimePlannerEmptySubcellBooleanObserver = observer;
        runtimePlannerEmptySubcellBooleanObserverContext = context;
    }
    ~RuntimePlannerEmptySubcellBooleanObserverScope() {
        runtimePlannerEmptySubcellBooleanObserver = previous_;
        runtimePlannerEmptySubcellBooleanObserverContext = previousContext_;
    }
    RuntimePlannerEmptySubcellBooleanObserverScope(const RuntimePlannerEmptySubcellBooleanObserverScope&) = delete;
    RuntimePlannerEmptySubcellBooleanObserverScope& operator=(const RuntimePlannerEmptySubcellBooleanObserverScope&) = delete;
};

