// Include immediately after runtimeCellIsPassableUncached. This memo is opt-in
// through the complete-component planner; the general query retains its scalar default.
// Commit gate/subcell helpers only inspect existing state here. Invalidate/end
// this scope BEFORE any world, rules, behavior, gate, repair or reservation edit.


class RuntimeComponentPassabilityReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentPassabilityReferenceScope(bool value)
        : previous_(runtimeComponentPassabilityReferenceOverride) {
        runtimeComponentPassabilityReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeComponentPassabilityReferenceScope() {
        runtimeComponentPassabilityReferenceOverride = previous_;
    }
    RuntimeComponentPassabilityReferenceScope(const RuntimeComponentPassabilityReferenceScope&) = delete;
    RuntimeComponentPassabilityReferenceScope& operator=(const RuntimeComponentPassabilityReferenceScope&) = delete;
};

// This switch restores the C12 unordered memo while retaining the complete
// component scope. The older switch above still selects the original scalar.


class RuntimeComponentDenseReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentDenseReferenceScope(bool value)
        : previous_(runtimeComponentDenseReferenceOverride) {
        runtimeComponentDenseReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeComponentDenseReferenceScope() { runtimeComponentDenseReferenceOverride = previous_; }
    RuntimeComponentDenseReferenceScope(const RuntimeComponentDenseReferenceScope&) = delete;
    RuntimeComponentDenseReferenceScope& operator=(const RuntimeComponentDenseReferenceScope&) = delete;
};

// Independent C21 ablation: preserve the C20 dense/unordered memo and kernel,
// but validate the general memo boundary on every query again.


class RuntimeComponentQueryLeaseReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentQueryLeaseReferenceScope(bool reference)
        : previous_(runtimeComponentQueryLeaseReferenceOverride) {
        runtimeComponentQueryLeaseReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentQueryLeaseReferenceScope() {
        runtimeComponentQueryLeaseReferenceOverride = previous_;
    }
    RuntimeComponentQueryLeaseReferenceScope(const RuntimeComponentQueryLeaseReferenceScope&) = delete;
    RuntimeComponentQueryLeaseReferenceScope& operator=(const RuntimeComponentQueryLeaseReferenceScope&) = delete;
};

// The complete-component BFS still iterates the original terrain/neighbor
// order and stores original integer component keys. Only private visited
// membership changes; legacy coordinate aliases intentionally remain aliases.










// Test observer records the actual production query sequence, not a reconstructed
// neighbor loop. No observer is installed during normal gameplay/benchmarks.



class RuntimePlannerPassabilityObserverScope {
    RuntimePlannerPassabilityObserver previous_;
    void* context_;
public:
    RuntimePlannerPassabilityObserverScope(RuntimePlannerPassabilityObserver observer, void* context)
        : previous_(runtimePlannerPassabilityObserver), context_(runtimePlannerPassabilityObserverContext) {
        runtimePlannerPassabilityObserver = observer; runtimePlannerPassabilityObserverContext = context;
    }
    ~RuntimePlannerPassabilityObserverScope() {
        runtimePlannerPassabilityObserver = previous_; runtimePlannerPassabilityObserverContext = context_;
    }
    RuntimePlannerPassabilityObserverScope(const RuntimePlannerPassabilityObserverScope&) = delete;
    RuntimePlannerPassabilityObserverScope& operator=(const RuntimePlannerPassabilityObserverScope&) = delete;
};



// Independent C22 ablation. true retains the C21b exact crush keys and its
// infantry-only census; false enables the owned empty-cell crush-key alias.


class RuntimeComponentCrushAliasReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentCrushAliasReferenceScope(bool value)
        : previous_(runtimeComponentCrushAliasReferenceOverride) {
        runtimeComponentCrushAliasReferenceOverride=value?1:0;
    }
    ~RuntimeComponentCrushAliasReferenceScope() { runtimeComponentCrushAliasReferenceOverride=previous_; }
    RuntimeComponentCrushAliasReferenceScope(const RuntimeComponentCrushAliasReferenceScope&)=delete;
    RuntimeComponentCrushAliasReferenceScope& operator=(const RuntimeComponentCrushAliasReferenceScope&)=delete;
};




// Constructed and owned by ONE complete-component kernel, after its explicit
// scope has passed every original identity/shape check. No view is returned or
// borrowed from an unrelated kernel. The complete BFS is synchronously read
// only: invalidate/end BEFORE any source payload, vector, map, mover, rule,
// gate, repair, behavior or reservation edit, even if addresses do not change.
//
// The ordinary wrapper above retains all per-query shape validation. This
// lease only removes repeated shape reads inside that proven lexical lifetime;
// reference, soft inputs, nested/disabled scopes and foreign arguments use the
// ordinary wrapper, including its original scalar/key/observer behavior.

