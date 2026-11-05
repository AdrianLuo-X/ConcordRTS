// Include after runtimeCellTraversalCost, before the complete-component BFS.
// This explicit local kernel belongs only to a single read-only planner. End
// or invalidate it BEFORE editing its mover, rules, world, terrain, occupancy,
// reservations or gates. It never registers/reuses another caller's context.


class RuntimeComponentKernelReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentKernelReferenceScope(bool reference)
        : previous_(runtimeComponentKernelReferenceOverride) {
        runtimeComponentKernelReferenceOverride=reference ? 1 : 0;
    }
    ~RuntimeComponentKernelReferenceScope() {
        runtimeComponentKernelReferenceOverride=previous_;
    }
    RuntimeComponentKernelReferenceScope(const RuntimeComponentKernelReferenceScope&) = delete;
    RuntimeComponentKernelReferenceScope& operator=(const RuntimeComponentKernelReferenceScope&) = delete;
};




class RuntimePlannerKernelLeaseObserverScope {
    RuntimePlannerKernelLeaseObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerKernelLeaseObserverScope(RuntimePlannerKernelLeaseObserver observer,void* context)
        : previous_(runtimePlannerKernelLeaseObserver),previousContext_(runtimePlannerKernelLeaseObserverContext) {
        runtimePlannerKernelLeaseObserver=observer; runtimePlannerKernelLeaseObserverContext=context;
    }
    ~RuntimePlannerKernelLeaseObserverScope() {
        runtimePlannerKernelLeaseObserver=previous_; runtimePlannerKernelLeaseObserverContext=previousContext_;
    }
    RuntimePlannerKernelLeaseObserverScope(const RuntimePlannerKernelLeaseObserverScope&) = delete;
    RuntimePlannerKernelLeaseObserverScope& operator=(const RuntimePlannerKernelLeaseObserverScope&) = delete;
};




class RuntimePlannerCrushAliasObserverScope {
    RuntimePlannerCrushAliasObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerCrushAliasObserverScope(RuntimePlannerCrushAliasObserver observer,void* context)
        : previous_(runtimePlannerCrushAliasObserver),previousContext_(runtimePlannerCrushAliasObserverContext) {
        runtimePlannerCrushAliasObserver=observer; runtimePlannerCrushAliasObserverContext=context;
    }
    ~RuntimePlannerCrushAliasObserverScope() {
        runtimePlannerCrushAliasObserver=previous_; runtimePlannerCrushAliasObserverContext=previousContext_;
    }
    RuntimePlannerCrushAliasObserverScope(const RuntimePlannerCrushAliasObserverScope&)=delete;
    RuntimePlannerCrushAliasObserverScope& operator=(const RuntimePlannerCrushAliasObserverScope&)=delete;
};


