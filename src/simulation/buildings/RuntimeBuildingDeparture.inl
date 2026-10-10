// A unit already in an authored service lane must be able to leave after a
// Stop/Move/Attack cancels its docking relation. This permission is derived
// from its current cell, so it disappears as soon as the unit is outside.
int runtimeStructureDepartureHostIdReference(const RuntimeWindowState& state,
    const WorldObjectInstance& mover) {
    if (!runtimeObjectMayUseStructureDriveRows(state, mover)) return -1;
    const int key = runtimeCellKey(mover.x, mover.y);
    if (state.tickOccupancyCacheTick == state.tick) {
        const auto count = state.tickGroundOccupantCountByCell.find(key);
        const auto occupant = state.tickOccupiedByCell.find(key);
        if ((count == state.tickGroundOccupantCountByCell.end() || count->second <= 1)
            && (occupant == state.tickOccupiedByCell.end() || occupant->second == mover.instanceId))
            return -1;
    }
    int hostId = -1;
    for (const auto& structure : state.world.getObjects()) {
        if (structure.alive && structure.kind == WorldObjectKind::Structure
            && runtimeStructureCellIsVehicleDriveRow(state, structure,
                runtimeCellPointForObject(mover), mover)
            && (hostId < 0 || structure.instanceId < hostId)) hostId = structure.instanceId;
    }
    return hostId;
}

class RuntimeBuildingDepartureReadOnlyQueryScope;
inline thread_local RuntimeBuildingDepartureReadOnlyQueryScope*
    runtimeActiveBuildingDepartureReadOnlyQueries = nullptr;

// A destination planner must end this scope before changing objects, rules,
// cargo, or occupancy hints. Only the first matching query evaluates the exact
// original predicate; -1 is a cached value too. Nothing survives the scope.
class RuntimeBuildingDepartureReadOnlyQueryScope {
    RuntimeBuildingDepartureReadOnlyQueryScope* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const WorldObjectInstance* objectData_;
    std::size_t objectCount_;
    int tick_;
    bool enabled_;
    std::optional<int> cachedHostId_;
    std::size_t materializations_ = 0;
public:
    RuntimeBuildingDepartureReadOnlyQueryScope(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, bool enabled = true)
        : previous_(runtimeActiveBuildingDepartureReadOnlyQueries), state_(&state),
          mover_(&mover), objectData_(state.world.getObjects().data()),
          objectCount_(state.world.getObjects().size()), tick_(state.tick),
          enabled_(enabled && !runtimeHistoryQueryReference()) {
        runtimeActiveBuildingDepartureReadOnlyQueries = this;
    }
    ~RuntimeBuildingDepartureReadOnlyQueryScope() {
        runtimeActiveBuildingDepartureReadOnlyQueries = previous_;
    }
    RuntimeBuildingDepartureReadOnlyQueryScope(const RuntimeBuildingDepartureReadOnlyQueryScope&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope& operator=(const RuntimeBuildingDepartureReadOnlyQueryScope&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope(RuntimeBuildingDepartureReadOnlyQueryScope&&) = delete;
    RuntimeBuildingDepartureReadOnlyQueryScope& operator=(RuntimeBuildingDepartureReadOnlyQueryScope&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    std::size_t materializations() const noexcept { return materializations_; }
    std::optional<int> find(const RuntimeWindowState& state, const WorldObjectInstance& mover) {
        if (!enabled_ || runtimeHistoryQueryReference() || state_ != &state || mover_ != &mover
            || tick_ != state.tick || objectData_ != state.world.getObjects().data()
            || objectCount_ != state.world.getObjects().size()) return std::nullopt;
        if (!cachedHostId_) {
            cachedHostId_ = runtimeStructureDepartureHostIdReference(state, mover);
            ++materializations_;
        }
        return cachedHostId_;
    }
};

int runtimeStructureDepartureHostId(const RuntimeWindowState& state,
    const WorldObjectInstance& mover) {
    if (!runtimeHistoryQueryReference() && runtimeActiveBuildingDepartureReadOnlyQueries != nullptr) {
        if (const auto cached = runtimeActiveBuildingDepartureReadOnlyQueries->find(state, mover))
            return *cached;
    }
    return runtimeStructureDepartureHostIdReference(state, mover);
}
