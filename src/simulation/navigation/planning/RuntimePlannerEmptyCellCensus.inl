// Include after footprint/origin helpers, before hard blocker/subcell queries.


class RuntimeComponentEmptyCensusReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentEmptyCensusReferenceScope(bool reference)
        : previous_(runtimeComponentEmptyCensusReferenceOverride) {
        runtimeComponentEmptyCensusReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentEmptyCensusReferenceScope() {
        runtimeComponentEmptyCensusReferenceOverride = previous_;
    }
    RuntimeComponentEmptyCensusReferenceScope(const RuntimeComponentEmptyCensusReferenceScope&) = delete;
    RuntimeComponentEmptyCensusReferenceScope& operator=(const RuntimeComponentEmptyCensusReferenceScope&) = delete;
};







class RuntimePlannerEmptyCensusObserverScope {
    RuntimePlannerEmptyCensusObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerEmptyCensusObserverScope(RuntimePlannerEmptyCensusObserver observer,void* context)
        : previous_(runtimePlannerEmptyCensusObserver),previousContext_(runtimePlannerEmptyCensusObserverContext) {
        runtimePlannerEmptyCensusObserver=observer; runtimePlannerEmptyCensusObserverContext=context;
    }
    ~RuntimePlannerEmptyCensusObserverScope() {
        runtimePlannerEmptyCensusObserver=previous_; runtimePlannerEmptyCensusObserverContext=previousContext_;
    }
    RuntimePlannerEmptyCensusObserverScope(const RuntimePlannerEmptyCensusObserverScope&) = delete;
    RuntimePlannerEmptyCensusObserverScope& operator=(const RuntimePlannerEmptyCensusObserverScope&) = delete;
};

// A conservative census for ONE synchronous read-only complete BFS. Mark ALL
// alive objects regardless of health, ID, transport, air, magbeam, alliance or
// layer, and ALL alive terrain footprints. A zero bit proves absence for both
// the hard blocker and subcell resident predicates; a set bit proves nothing.
// Noncanonical/oversized origins or rectangles disable the entire proof.
//
// End/invalidate BEFORE any source object/terrain, rule/footprint, gate, mover,
// behavior or reservation edit. State/tick checks are defensive, not a content
// revision. Disabled inner scopes deliberately mask outer scopes.



