// Include immediately before handleRuntimeMapRightClick, after the UI dispatch
// helpers and camera/selection functions are defined.
int runtimePlayerCurrentKeyCode(WPARAM key) {
    const int code = runtimePlayerKeyCode(static_cast<int>(key),
        (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0);
    RuntimeKeyboardParentDiagnostics::key(static_cast<std::uint64_t>(key), code);
    return code;
}

// This is presentation authority only. The simulation temporarily borrows
// selectedObjectIds when applying AI and remote players' authoritative orders.
bool runtimePlayerSelectionAllowed(const RuntimeWindowState& state) {
    return !state.multiplayerLocalSpectator
        && (state.campaignMode || !runtimeLocalObserver(state.config));
}

bool rejectRuntimeObserverSelection(RuntimeWindowState& state) {
    if (runtimePlayerSelectionAllowed(state)) return false;
    state.selectedObjectIds.clear();
    state.leftPointer.active = state.leftPointer.moved = false;
    state.repaintRequested = true;
    return true;
}

bool runtimePlayerLocalSelectable(const RuntimeWindowState& state, int id) {
    if (!runtimePlayerSelectionAllowed(state)) return false;
    const auto* object = findObjectById(state, id);
    return object && object->alive && !runtimeGateObjectIsLimbo(*object)
        && runtimeObjectAcceptsPlayerSelection(state, *object)
        && runtimeCanonicalHumanHouseOwner(object->owner)
            == runtimeCanonicalHumanHouseOwner(state.economy.owner)
        && runtimeObjectVisibleToOwner(state, state.economy.owner, *object);
}

// Click, box and keyboard selection all use the same local ownership gate.
// Hit testing remains unfiltered for hover health and target commands.
void applyRuntimePlayerSelection(RuntimeWindowState& state, const std::vector<int>& ids,
    bool append, bool toggle) {
    std::erase_if(state.selectedObjectIds,
        [&](int id) { return !runtimePlayerLocalSelectable(state, id); });
    std::vector<int> eligible;
    for (int id : ids) if (runtimePlayerLocalSelectable(state, id)) eligible.push_back(id);
    applySelection(state, eligible, append, toggle);
}

bool runtimePlayerMobileSelectable(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return runtimePlayerLocalSelectable(state, object.instanceId) && runtimeObjectCanBeBoxSelected(object)
        && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)
        && !(type && (type->spawned || type->missileSpawn));
}

// Private synchronous assembly keeps native authority, validation and effects here.
class RuntimePlayerFactoryUiHost final : public RuntimePlayerFactoryHost {
public:
    explicit RuntimePlayerFactoryUiHost(RuntimeWindowState& state) : state_(state) {}
    bool usesIntentRouter() override { return runtimeUsesGameplayIntentRouter(state_); }
    void refreshAuthority() override { refreshRuntimeMultiplayerPlayerIntentAuthority(state_); }
    bool submitPrimary(std::int32_t id, std::string& error) override {
        return state_.multiplayerIntentRouter.setPrimaryFactory(id, error);
    }
    bool submitRally(std::int32_t id, RuntimeMultiplayerGameplayCell cell,
        std::string& error) override {
        return state_.multiplayerIntentRouter.setRallyPoint(id, cell, error);
    }
    bool applyPrimary(std::int32_t id) override {
        return applyRuntimePlayerPrimaryFactory(state_, state_.economy.owner, id);
    }
    bool applyRally(std::int32_t id, RuntimeMultiplayerGameplayCell cell) override {
        return applyRuntimePlayerRallyPoint(state_, state_.economy.owner, id, cell);
    }
    void setStatus(const std::string& status) override { state_.interactionStatus = status; }
    void primarySelectedSound() override { emitRuntimeSound(state_, "EVA_PrimaryBuildingSelected"); }
    void requestRepaint() override { state_.repaintRequested = true; }
private:
    RuntimeWindowState& state_;
};

bool dispatchRuntimePlayerFactoryFromUi(RuntimeWindowState& state,
    int id, std::optional<RuntimeMultiplayerGameplayCell> cell
) {
    RuntimePlayerFactoryUiHost host(state);
    return dispatchRuntimePlayerFactory({id, cell}, host);
}

// Private Win32 assembly for the same synchronous, platform-free Team loop.
class RuntimeTeamKeyUiHost final : public RuntimeTeamKeyHost {
public:
    RuntimeTeamKeyUiHost(RuntimeWindowState& state, const RECT& client)
        : state_(state), client_(client) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    bool selectable(int id) override { return runtimePlayerLocalSelectable(state_, id); }
    void applySelection(const std::vector<int>& ownedResult) override {
        applyRuntimePlayerSelection(state_, ownedResult, false, false);
    }
    void clearSelectedBeacon() override { state_.playerCommunication.selectedBeacon.reset(); }
    std::optional<RuntimeTeamKeyCell> objectCell(int id) override {
        if (const auto* object = findObjectById(state_, id))
            return RuntimeTeamKeyCell{object->x, object->y};
        return std::nullopt;
    }
    void centerCamera(RuntimeTeamKeyCell cell) override {
        centerRuntimeCameraOnCell(state_, client_, {cell.x, cell.y});
    }
    void teamAssigned(std::int32_t number) override {
        state_.interactionStatus = "team " + std::to_string(number) + " assigned";
    }
private:
    RuntimeWindowState& state_;
    RECT client_;
};

// Native queries stay in the original synchronous input assembly. Each owned
// result is consumed before returning; no projection/world reference escapes.
class RuntimeObjectCycleUiHost final : public RuntimeObjectCycleKeyHost {
public:
    RuntimeObjectCycleUiHost(RuntimeWindowState& state, const RECT& client, const RECT&)
        : state_(state), client_(client) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    std::vector<int> eligibleMobileObjectIds() override {
        std::vector<int> ids;
        for (const auto& object : state_.world.getObjects())
            if (runtimePlayerMobileSelectable(state_, object)) ids.push_back(object.instanceId);
        return ids;
    }
    void applySelection(const std::vector<int>& ownedResult) override {
        applyRuntimePlayerSelection(state_, ownedResult, false, false);
    }
    void clearSelectedBeacon() override { state_.playerCommunication.selectedBeacon.reset(); }
    void centerCurrentSelection() override { runtimePlayerCenterSelection(state_, client_); }
private:
    RuntimeWindowState& state_;
    RECT client_;
};

class RuntimeViewNavigationUiHost final : public RuntimeViewNavigationKeyHost {
public:
    RuntimeViewNavigationUiHost(RuntimeWindowState& state, const RECT& client, const RECT& viewport)
        : state_(state), client_(client), viewport_(viewport) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    RuntimeTeamKeyCell currentViewportCenterCell() override {
        const RuntimeInputPixelPoint center = runtimeInputPixelBoundsMidpoint(
            {viewport_.left, viewport_.top}, {viewport_.right, viewport_.bottom});
        const POINT point = approximateCellFromScreenPoint(state_, viewport_, POINT{center.x, center.y});
        return {point.x, point.y};
    }
    void centerCamera(RuntimeTeamKeyCell cell) override {
        centerRuntimeCameraOnCell(state_, client_, {cell.x, cell.y});
    }
    void viewSaved(std::int32_t number) override {
        state_.interactionStatus = "view " + std::to_string(number) + " saved";
    }
    std::optional<RuntimeTeamKeyCell> latestRadarEventCell() override {
        if (state_.playerLastRadarCell)
            return RuntimeTeamKeyCell{state_.playerLastRadarCell->x, state_.playerLastRadarCell->y};
        if (!state_.triggers.campaignRadarEvents.empty()) {
            const auto& cell = state_.triggers.campaignRadarEvents.back().cell;
            return RuntimeTeamKeyCell{cell.x, cell.y};
        }
        return std::nullopt;
    }
private:
    RuntimeWindowState& state_;
    RECT client_;
    RECT viewport_;
};

class RuntimeQuickSelectionUiHost final : public RuntimeQuickSelectionKeyHost {
public:
    RuntimeQuickSelectionUiHost(RuntimeWindowState& state, const RECT&, const RECT& viewport)
        : state_(state), viewport_(viewport) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    std::set<int> onScreenObjectIds() override {
        std::set<int> onScreen;
        for (const auto& projected : runtimeProjectedObjectsForViewport(state_, viewport_))
            if (projected.object && rectsIntersect(viewport_, projected.bounds))
                onScreen.insert(projected.object->instanceId);
        return onScreen;
    }
    std::vector<RuntimePlayerSelectionCandidate> mobileCandidates(const std::set<int>& onScreen) override {
        std::vector<RuntimePlayerSelectionCandidate> candidates;
        for (const auto& object : state_.world.getObjects()) {
            if (!runtimePlayerMobileSelectable(state_, object)) continue;
            const auto* unit = findRuntimeUnitType(state_.rules, object);
            candidates.push_back({object.instanceId, toUpperAsciiLocal(object.type),
                object.veterancy, unit && unit->isSelectableCombatant, onScreen.contains(object.instanceId),
                object.health, unit ? std::max(1, unit->strength) : std::max(1, object.health)});
        }
        return candidates;
    }
    void applySelection(const std::vector<int>& ownedResult) override {
        applyRuntimePlayerSelection(state_, ownedResult, false, false);
    }
    void clearSelectedBeacon() override { state_.playerCommunication.selectedBeacon.reset(); }
private:
    RuntimeWindowState& state_;
    RECT viewport_;
};

class RuntimePlanningFactoryKeyUiHost final : public RuntimePlanningFactoryKeyHost {
public:
    explicit RuntimePlanningFactoryKeyUiHost(RuntimeWindowState& state) : state_(state) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    void dispatchWaypoint(RuntimePlayerWaypointOperation operation, std::uint64_t id) override {
        dispatchRuntimePlayerWaypointFromUi(state_, operation, id);
    }
    void dispatchFactory(RuntimePlayerFactoryRequest request) override {
        dispatchRuntimePlayerFactoryFromUi(state_, request.objectId, request.cell);
    }
    void setStatus(const std::string& status) override { state_.interactionStatus = status; }
private:
    RuntimeWindowState& state_;
};

#include "ui/input/RuntimeKeyboardRouterUi.inl"

bool handleRuntimePlayerCommandCode(RuntimeWindowState& state, const RECT& client,
    int code, bool repeated, std::uint64_t now) {
    RuntimeKeyboardRouterUiHost host(state, client);
    return handleRuntimeKeyboardRouterKey({code, repeated, now}, host);
}

bool handleRuntimePlayerCommandKey(RuntimeWindowState& state, HWND window, WPARAM key, LPARAM flags) {
    RECT client{}; GetClientRect(window, &client);
    return handleRuntimePlayerCommandCode(state, client, runtimePlayerCurrentKeyCode(key),
        (flags & (static_cast<LPARAM>(1) << 30)) != 0, runtimeUiClockNow(state));
}

bool handleRuntimePlayerTargetClick(RuntimeWindowState& state, const RECT& viewport, POINT point);

// Private synchronous adapter; each query reborrows the current state root.
class RuntimeCameraKeyWin32Host final : public RuntimeCameraKeyHost {
public:
    RuntimeCameraKeyWin32Host(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    std::int32_t runtimeScrollRate() override { return state_.runtimeScrollRate; }
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, action, fallback);
    }
    void clearCameraFollow() override { state_.playerLocalCommands.cameraFollow = false; }
    std::int32_t sampleCurrentKeyCode(const RuntimeCameraKeyInput& input) override {
        return runtimePlayerCurrentKeyCode(static_cast<WPARAM>(input.rawKey));
    }
    bool visionEnabled() override { return state_.visionEnabled; }
    bool skirmishStartApplied() override { return state_.economy.skirmishStartApplied; }
    std::int32_t skirmishStartCellX() override { return state_.economy.skirmishStartCell.x; }
    std::int32_t skirmishStartCellY() override { return state_.economy.skirmishStartCell.y; }
    void prepareCenterClient() override {
        centerClient_ = {};
        GetClientRect(window_, &centerClient_);
    }
    RuntimeTeamKeyCell skirmishStartCell() override {
        return {state_.economy.skirmishStartCell.x, state_.economy.skirmishStartCell.y};
    }
    void centerCamera(RuntimeTeamKeyCell cell) override {
        centerRuntimeCameraOnCell(state_, centerClient_, {cell.x, cell.y});
    }
    void resetCameraInitialized() override { state_.camera.initialized = false; }
    void scrollCamera(std::int32_t dx, std::int32_t dy) override {
        scrollRuntimeMapCameraClamped(state_, dx, dy);
    }
    void invalidateProjection() override { invalidateRuntimeObjectProjectionCache(state_); }
private:
    RuntimeWindowState& state_;
    HWND window_;
    RECT centerClient_{};
};
