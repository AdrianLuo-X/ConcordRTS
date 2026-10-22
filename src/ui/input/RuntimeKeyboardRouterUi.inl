// Private synchronous assembly: decisions and priority belong to the standard router.
// The client/viewport are owned values; each component reborrows the current root.
class RuntimeKeyboardRouterUiHost final : public RuntimeKeyboardRouterHost {
public:
    RuntimeKeyboardRouterUiHost(RuntimeWindowState& state, const RECT& client)
        : state_(state), client_(client) {}
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool finished() override { return state_.skirmishMatchFinished; }
    void rejectObserverSelection() override { (void)rejectRuntimeObserverSelection(state_); }
    bool handleReferenceKey(std::int32_t code, bool repeated) override {
        return handleRuntimePlayerReferenceKey(state_, client_, code, repeated);
    }
    void prepareNavigation() override { viewport_ = runtimeMapViewport(client_); }
    bool handleObjectCycleKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimeObjectCycleUiHost host(state_, client_, viewport_);
        return handleRuntimeObjectCycleKey({key.code, key.repeated, key.nowMs},
            state_.playerLocalCommands, state_.selectedObjectIds, host);
    }
    bool handleTeamKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimeTeamKeyUiHost host(state_, client_);
        return handleRuntimeTeamKey({key.code, key.repeated, key.nowMs},
            state_.playerLocalCommands, state_.selectedObjectIds, host);
    }
    bool handleViewNavigationKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimeViewNavigationUiHost host(state_, client_, viewport_);
        return handleRuntimeViewNavigationKey({key.code, key.repeated, key.nowMs},
            state_.playerLocalCommands, host);
    }
    bool handleQuickSelectionKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimeQuickSelectionUiHost host(state_, client_, viewport_);
        return handleRuntimeQuickSelectionKey({key.code, key.repeated, key.nowMs},
            state_.playerLocalCommands, state_.selectedObjectIds, host);
    }
    bool handlePlanningFactoryKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimePlanningFactoryKeyUiHost host(state_);
        return handleRuntimePlanningFactoryKey({key.code, key.repeated},
            state_.playerLocalCommands, state_.selectedObjectIds, host);
    }
    bool handleRadarNavigationKey(const RuntimeKeyboardRouterInput& key) override {
        RuntimeViewNavigationUiHost host(state_, client_, viewport_);
        return handleRuntimeRadarNavigationKey({key.code, key.repeated, key.nowMs}, host);
    }
private:
    RuntimeWindowState& state_;
    RECT client_;
    RECT viewport_{};
};