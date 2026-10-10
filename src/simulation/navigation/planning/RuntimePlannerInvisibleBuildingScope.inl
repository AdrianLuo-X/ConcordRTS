// Only a read-only destination search owns this snapshot. Gate phases and
// mover authorization still use their original predicates on every query.
class RuntimePlannerInvisibleBuildingScope;
inline thread_local const RuntimePlannerInvisibleBuildingScope*
    runtimeActivePlannerInvisibleBuildings = nullptr;

class RuntimePlannerInvisibleBuildingScope {
    const RuntimePlannerInvisibleBuildingScope* previous_;
    const RuntimeWindowState* state_;
    const void* objectData_;
    std::size_t objectCount_;
    std::unordered_set<int> ids_;
    bool enabled_;
public:
    explicit RuntimePlannerInvisibleBuildingScope(const RuntimeWindowState& state,
        bool enabled = true)
        : previous_(runtimeActivePlannerInvisibleBuildings), state_(&state),
          objectData_(state.world.getObjects().data()),
          objectCount_(state.world.getObjects().size()),
          enabled_(enabled && !runtimeHistoryQueryReference()
              && !runtimeFrameScalingReferenceMovement()) {
        if (enabled_) {
            // Dead invisible structures remain in the original context too.
            for (const auto& object : state.world.getObjects())
                if (runtimeInvisibleMapBuilding(&state.rules, object))
                    ids_.insert(object.instanceId);
            runtimeActivePlannerInvisibleBuildings = this;
        }
    }
    ~RuntimePlannerInvisibleBuildingScope() {
        if (enabled_) runtimeActivePlannerInvisibleBuildings = previous_;
    }
    RuntimePlannerInvisibleBuildingScope(const RuntimePlannerInvisibleBuildingScope&) = delete;
    RuntimePlannerInvisibleBuildingScope& operator=(const RuntimePlannerInvisibleBuildingScope&) = delete;
    const std::unordered_set<int>* find(const RuntimeWindowState& state) const {
        return enabled_ && state_ == &state
            && objectData_ == state.world.getObjects().data()
            && objectCount_ == state.world.getObjects().size() ? &ids_ : nullptr;
    }
};
