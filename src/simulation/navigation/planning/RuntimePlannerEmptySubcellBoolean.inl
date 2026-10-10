// Include immediately before runtimeCellIsPassableUncached.
#include "simulation/navigation/occupancy/RuntimeEmptySubcellAvailability.h"

inline thread_local int runtimePlannerEmptySubcellBooleanReferenceOverride = -1;
bool runtimePlannerEmptySubcellBooleanReference() noexcept {
    if (runtimePlannerEmptySubcellBooleanReferenceOverride >= 0)
        return runtimePlannerEmptySubcellBooleanReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_EMPTY_SUBCELL_BOOLEAN_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
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
bool runtimePlannerEmptySubcellBooleanObserversAllowed() noexcept;

using RuntimePlannerEmptySubcellBooleanObserver = void(*)(void*, bool);
inline thread_local RuntimePlannerEmptySubcellBooleanObserver runtimePlannerEmptySubcellBooleanObserver = nullptr;
inline thread_local void* runtimePlannerEmptySubcellBooleanObserverContext = nullptr;
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
bool runtimePlannerEmptySubcellBooleanResult(const RuntimeSubcellLocation& destination,
    int ignoredMover, const std::map<int, RuntimeSubcellReservation>& reservations) {
    const bool result = runtimeEmptySubcellHasAvailability(destination, ignoredMover, reservations);
    if (runtimePlannerEmptySubcellBooleanObserver)
        runtimePlannerEmptySubcellBooleanObserver(runtimePlannerEmptySubcellBooleanObserverContext, result);
    static const bool trace = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
    if (trace) {
        struct Totals {
            std::size_t queries = 0, available = 0;
            ~Totals() {
                std::cerr << "Runtime planner empty subcell boolean: queries=" << queries
                          << " available=" << available << '\n';
            }
        };
        static thread_local Totals totals;
        ++totals.queries;
        totals.available += result;
    }
    return result;
}
