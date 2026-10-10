// Include after runtimeAuthoredLandTypeAllowsMoverUncached. The reference
// switch is defined by RuntimePlannerConnectivityKernel.inl later in the shell.
bool runtimeComponentKernelReference();
class RuntimePlannerLandPermissionScope;
inline thread_local RuntimePlannerLandPermissionScope* runtimeActivePlannerLandPermission=nullptr;

// The complete-component planner is read-only. End/invalidate BEFORE editing
// its mover, rules or authority. Only twelve exact authored-land enum values
// are memoized; other enum values and foreign inputs keep the scalar query.
class RuntimePlannerLandPermissionScope {
    RuntimePlannerLandPermissionScope* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const WorldObjectInstance* objects_;
    std::size_t objectCount_;
    int tick_;
    bool enabled_;
    std::array<unsigned char,12> permissions_{};
    std::size_t materializations_=0, hits_=0;
public:
    RuntimePlannerLandPermissionScope(const RuntimeWindowState& state,
        const WorldObjectInstance& mover,bool enabled=true)
        : previous_(runtimeActivePlannerLandPermission), state_(&state), mover_(&mover),
          objects_(state.world.getObjects().data()), objectCount_(state.world.getObjects().size()),
          tick_(state.tick), enabled_(enabled && !runtimeComponentKernelReference()) {
        // Disabled nested scopes mask their outer scope as well.
        runtimeActivePlannerLandPermission=this;
    }
    ~RuntimePlannerLandPermissionScope() { runtimeActivePlannerLandPermission=previous_; }
    RuntimePlannerLandPermissionScope(const RuntimePlannerLandPermissionScope&) = delete;
    RuntimePlannerLandPermissionScope& operator=(const RuntimePlannerLandPermissionScope&) = delete;
    RuntimePlannerLandPermissionScope(RuntimePlannerLandPermissionScope&&) = delete;
    RuntimePlannerLandPermissionScope& operator=(RuntimePlannerLandPermissionScope&&) = delete;
    void invalidate() noexcept { enabled_=false; }
    std::size_t materializations() const noexcept { return materializations_; }
    std::size_t hits() const noexcept { return hits_; }
    std::optional<bool> query(const RuntimeWindowState& state,
        const RuntimeTerrainInfo& terrain,const WorldObjectInstance& mover) {
        const int ordinal=static_cast<int>(terrain.authoredLandType);
        if (!enabled_ || runtimeComponentKernelReference() || state_!=&state || mover_!=&mover
            || tick_!=state.tick || objects_!=state.world.getObjects().data()
            || objectCount_!=state.world.getObjects().size() || ordinal < -1 || ordinal > 10)
            return std::nullopt;
        auto& permission=permissions_[static_cast<std::size_t>(ordinal+1)];
        if (permission) { ++hits_; return permission==2; }
        const bool allowed=runtimeAuthoredLandTypeAllowsMoverUncached(state,terrain,mover);
        permission=allowed ? 2 : 1; ++materializations_;
        return allowed;
    }
};

bool runtimeAuthoredLandTypeAllowsMover(const RuntimeWindowState& state,
    const RuntimeTerrainInfo& terrain,const WorldObjectInstance& mover) {
    auto* scope=runtimeActivePlannerLandPermission;
    if (scope) {
        if (const auto answer=scope->query(state,terrain,mover)) return *answer;
    }
    // No scope: no reference lookup, key construction, allocation or hash.
    return runtimeAuthoredLandTypeAllowsMoverUncached(state,terrain,mover);
}
