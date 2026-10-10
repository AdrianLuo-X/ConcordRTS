// Include after runtimeCellTraversalCost, before the complete-component BFS.
// This explicit local kernel belongs only to a single read-only planner. End
// or invalidate it BEFORE editing its mover, rules, world, terrain, occupancy,
// reservations or gates. It never registers/reuses another caller's context.
inline thread_local int runtimeComponentKernelReferenceOverride = -1;
bool runtimeComponentKernelReference() {
    if (runtimeComponentKernelReferenceOverride >= 0)
        return runtimeComponentKernelReferenceOverride != 0;
    static const bool reference = [] {
        const char* value=std::getenv("CONCORD_COMPONENT_KERNEL_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
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

using RuntimePlannerKernelLeaseObserver = void(*)(void*,std::size_t,std::size_t,std::size_t);
inline thread_local RuntimePlannerKernelLeaseObserver runtimePlannerKernelLeaseObserver = nullptr;
inline thread_local void* runtimePlannerKernelLeaseObserverContext = nullptr;
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

using RuntimePlannerCrushAliasObserver=void(*)(void*,std::size_t,std::size_t,std::size_t,std::size_t);
inline thread_local RuntimePlannerCrushAliasObserver runtimePlannerCrushAliasObserver=nullptr;
inline thread_local void* runtimePlannerCrushAliasObserverContext=nullptr;
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

class RuntimePlannerConnectivityKernel {
    const RuntimeWindowState& state_;
    const WorldObjectInstance& mover_;
    const std::unordered_map<int,const TerrainCell*>& terrain_;
    const std::unordered_map<int,int>& occupancy_;
    bool reference_, enabled_ = true;
    std::optional<PathMovementMode> mode_;
    RuntimePlannerIndexedTopology indexedTopology_;
    RuntimePlannerTerrainQueries terrainQueries_;
    RuntimePlannerCellPassabilityReadOnlyLease passabilityLease_;
    RuntimePlannerColdCellLookups coldLookups_;
public:
    RuntimePlannerConnectivityKernel(const RuntimeWindowState& state,
        const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,
        bool reference=runtimeComponentKernelReference(),
        RuntimePlannerCellPassabilityScope* passabilityScope=nullptr)
        : state_(state), mover_(mover), terrain_(terrain), occupancy_(occupancy),
          reference_(reference),
          mode_(reference ? std::nullopt : std::optional<PathMovementMode>(runtimeObjectMovementMode(state_,mover_))),
          indexedTopology_(state,mover,terrain,occupancy,
              !reference && mode_!=PathMovementMode::Air && !runtimeComponentTerrainQueryReference()),
          terrainQueries_(state, terrain, !reference && mode_ != PathMovementMode::Air && !indexedTopology_.enabled()),
          passabilityLease_(passabilityScope,state,mover,terrain,occupancy,
              !reference && mode_ != PathMovementMode::Air),
          coldLookups_(state,mover,terrain,occupancy,terrainQueries_,
              !reference && mode_ != PathMovementMode::Air) {}
    ~RuntimePlannerConnectivityKernel() {
        if (runtimePlannerCrushAliasObserver)
            runtimePlannerCrushAliasObserver(runtimePlannerCrushAliasObserverContext,
                passabilityLease_.crushAliases(),passabilityLease_.aliasQualificationChecks(),
                passabilityLease_.aliasFoundationBorrows(),passabilityLease_.scalarQueries());
        if (runtimePlannerKernelLeaseObserver)
            runtimePlannerKernelLeaseObserver(runtimePlannerKernelLeaseObserverContext,
                leaseFullValidations(),leasedQueries(),leaseFallbacks());
    }
    RuntimePlannerConnectivityKernel(const RuntimePlannerConnectivityKernel&) = delete;
    RuntimePlannerConnectivityKernel& operator=(const RuntimePlannerConnectivityKernel&) = delete;
    RuntimePlannerConnectivityKernel(RuntimePlannerConnectivityKernel&&) = delete;
    RuntimePlannerConnectivityKernel& operator=(RuntimePlannerConnectivityKernel&&) = delete;
    void invalidate() noexcept {
        enabled_=false; coldLookups_.invalidate(); indexedTopology_.invalidate(); terrainQueries_.invalidate(); passabilityLease_.invalidate();
    }
    bool reference() const noexcept { return reference_; }
    std::optional<PathMovementMode> precomputedMode() const noexcept { return mode_; }
    bool usesOriginalCost() const noexcept {
        return reference_ || !enabled_ || mode_==PathMovementMode::Air;
    }
    std::size_t leaseFullValidations() const noexcept { return passabilityLease_.fullValidations(); }
    bool queryLeaseEnabled() const noexcept { return passabilityLease_.enabled(); }
    std::size_t leasedQueries() const noexcept { return passabilityLease_.queries(); }
    std::size_t leaseFallbacks() const noexcept { return passabilityLease_.fallbacks(); }
    bool seedIsPassable(const RuntimeCellPoint& seed) const {
        if (usesOriginalCost() || runtimeComponentQueryLeaseReference())
            return runtimeCellIsPassable(state_,terrain_,occupancy_,seed,mover_);
        // Keep the original implicit-mode, crush=false seed key. In particular
        // a failed seed still enters visited in the caller before later edges.
        return passabilityLease_.query(state_,terrain_,occupancy_,seed,mover_);
    }
    bool connects(const RuntimeCellPoint& current,const RuntimeCellPoint& next) const {
        // Air transition deliberately does not require terrain: the original
        // cost wrapper's subsequent terrain lookup is part of its eligibility.
        // Keep that exact wrapper for Air and disabled/reference kernels.
        if (usesOriginalCost())
            return runtimeCellTraversalCost(state_,terrain_,occupancy_,current,next,mover_).has_value();
        // For every non-Air mode, transition=true already proved current and
        // next terrain pointers exist. In this read-only lifetime the original
        // cost tail cannot return nullopt; its integer value is never consumed
        // by the component BFS. Keep repair, diagonal/layer/height, gate and
        // crush queries inside the original transition, with identical mode.
        return runtimeCellTransitionIsPassable(state_,terrain_,occupancy_,current,next,
            mover_,-1,nullptr,true,mode_,&terrainQueries_,
            runtimeComponentQueryLeaseReference() ? nullptr : &passabilityLease_,&indexedTopology_);
    }
};
