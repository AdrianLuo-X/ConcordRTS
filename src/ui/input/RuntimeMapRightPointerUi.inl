// Private synchronous bridges to the original window, geometry and command owners.
// Only the stable root is borrowed; child values are reacquired per operation.
RuntimeInputPixelRect runtimeRightPointerInputRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

RECT runtimeRightPointerNativeRect(RuntimeInputPixelRect rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

POINT runtimeRightPointerNativePoint(RuntimeInputPixelPoint point) {
    return {point.x, point.y};
}

class RuntimeMapRightPointerGestureUiHost final : public RuntimeMapRightPointerGestureHost {
public:
    RuntimeMapRightPointerGestureUiHost(RuntimeWindowState& state, HWND window = nullptr)
        : state_(state), window_(window) {}
    RuntimeMapRightPointerState& gestureState() override { return state_.rightPointer; }
    bool worldLoaded() override { return state_.world.getSummary().loaded; }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    RuntimeInputPixelRect clientRect() override {
        RECT rect; GetClientRect(window_, &rect);
        return runtimeRightPointerInputRect(rect);
    }
    RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) override {
        return runtimeRightPointerInputRect(runtimeSidebarRect(runtimeRightPointerNativeRect(client)));
    }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) override {
        return runtimeRightPointerInputRect(runtimeMapViewport(runtimeRightPointerNativeRect(client)));
    }
    void setMouseClientPointValid(bool valid) override { state_.mouseClientPointValid = valid; }
    void setMouseClientPoint(RuntimeInputPixelPoint point) override {
        state_.mouseClientPoint = runtimeRightPointerNativePoint(point);
    }
    bool cancelActiveSuperWeaponTargetSelection(const std::string& reason) override {
        return cancelRuntimeActiveSuperWeaponTargetSelection(state_, reason);
    }
    std::optional<std::int32_t> buildButtonAtPoint(
        RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        return runtimeBuildButtonAtPoint(state_, runtimeRightPointerNativeRect(sidebar),
            runtimeRightPointerNativePoint(point));
    }
    void dispatchBuildButtonSecondary(std::int32_t index) override {
        dispatchRuntimeBuildButtonSecondaryActionFromUi(state_, state_.economy.catalog[index]);
    }
    bool sidebarToolActive() override { return state_.activeSidebarTool != RuntimeSidebarTool::None; }
    void clearSidebarTool() override { state_.activeSidebarTool = RuntimeSidebarTool::None; }
    void focusWindow() override { SetFocus(window_); }
    void captureWindow() override { SetCapture(window_); }
    bool ownsCapture() override { return GetCapture() == window_; }
    void releaseCapture() override { ReleaseCapture(); }
    bool scrollCoastingEnabled() override { return state_.runtimeScrollCoastingEnabled; }
    std::int32_t scrollRate() override { return state_.runtimeScrollRate; }
    bool scrollCameraClamped(std::int32_t dx, std::int32_t dy) override {
        return scrollRuntimeMapCameraClamped(state_, dx, dy);
    }
    void invalidateProjection() override { invalidateRuntimeObjectProjectionCache(state_); }
    void setInteractionStatus(const std::string& status) override { state_.interactionStatus = status; }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redrawWindow() override {
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
private:
    RuntimeWindowState& state_;
    HWND window_;
};

class RuntimeMapRightPointerClickUiHost final : public RuntimeMapRightPointerClickHost {
public:
    explicit RuntimeMapRightPointerClickUiHost(RuntimeWindowState& state) : state_(state) {}
    bool hasSelectedBeacon() override { return state_.playerCommunication.selectedBeacon.has_value(); }
    void clearSelectedBeacon() override { state_.playerCommunication.selectedBeacon.reset(); }
    bool followTargeting() override { return state_.playerLocalCommands.followTargeting; }
    bool rallyTargeting() override { return state_.playerLocalCommands.rallyTargeting; }
    bool beaconTargeting() override { return state_.playerCommunication.beaconTargeting; }
    void setBeaconTargeting(bool value) override { state_.playerCommunication.beaconTargeting = value; }
    void setFollowTargeting(bool value) override { state_.playerLocalCommands.followTargeting = value; }
    void setRallyTargeting(bool value) override { state_.playerLocalCommands.rallyTargeting = value; }
    bool cancelActiveSuperWeaponTargetSelection(const std::string& reason) override {
        return cancelRuntimeActiveSuperWeaponTargetSelection(state_, reason);
    }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) override {
        return runtimeRightPointerInputRect(runtimeMapViewport(runtimeRightPointerNativeRect(client)));
    }
    bool dismissPlacement() override { return dismissRuntimePlacementFromUi(state_); }
    bool planning() override { return state_.playerLocalCommands.planning; }
    void setSelectedWaypoint(std::uint64_t id) override { state_.playerLocalCommands.selectedWaypoint = id; }
    bool sidebarToolActive() override { return state_.activeSidebarTool != RuntimeSidebarTool::None; }
    void clearSidebarTool() override { state_.activeSidebarTool = RuntimeSidebarTool::None; }
    bool selectionEmpty() override { return state_.selectedObjectIds.empty(); }
    std::optional<std::int32_t> projectedObjectIdAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto& projected = runtimeProjectedObjectsForViewport(state_, runtimeRightPointerNativeRect(viewport));
        const auto* picked = findProjectedObjectAtPoint(projected, runtimeRightPointerNativePoint(point));
        if (picked != nullptr && picked->object != nullptr) return picked->object->instanceId;
        return std::nullopt;
    }
    std::optional<RuntimeMapRightPointerWallTarget> visibleTerrainWallAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto target = visibleTerrainWallTargetAtPoint(state_, runtimeRightPointerNativeRect(viewport),
            runtimeRightPointerNativePoint(point));
        if (target) return RuntimeMapRightPointerWallTarget{
            target->instanceId, {target->x, target->y}, target->type};
        return std::nullopt;
    }
    bool selectionContains(std::int32_t objectId) override {
        return ::selectionContains(state_, objectId);
    }
    RuntimeMapRightPointerCell surfaceCellAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const POINT cell = approximateCellFromScreenPoint(state_, runtimeRightPointerNativeRect(viewport),
            runtimeRightPointerNativePoint(point));
        return {cell.x, cell.y};
    }
    bool dispatchPrimaryWall(const RuntimeMapRightPointerWallTarget& target) override {
        WorldObjectInstance identity;
        identity.instanceId = target.objectId;
        identity.x = target.cell.x;
        identity.y = target.cell.y;
        identity.type = target.type;
        return dispatchRuntimeTargetOrderFromUi(state_, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            {target.cell.x, target.cell.y}, &identity);
    }
    bool dispatchPrimaryObject(RuntimeMapRightPointerCell cell, std::int32_t objectId) override {
        // The original target dispatcher consumes only the owned identity. It
        // performs existing current-world authority/eligibility validation itself.
        WorldObjectInstance identity;
        identity.instanceId = objectId;
        return dispatchRuntimeTargetOrderFromUi(state_, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            {cell.x, cell.y}, &identity);
    }
    void cancelInteraction() override { cancelRuntimeInteraction(state_); }
    void setInteractionStatus(const std::string& status) override { state_.interactionStatus = status; }
    void requestRepaint() override { state_.repaintRequested = true; }
private:
    RuntimeWindowState& state_;
};

bool advanceRuntimeRightMousePan(RuntimeWindowState& state) {
    RuntimeMapRightPointerGestureUiHost host(state);
    return advanceRuntimeMapRightPointerTick(host);
}

bool handleRuntimeMapRightClick(RuntimeWindowState& state, const RECT& clientRect, POINT point) {
    RuntimeMapRightPointerClickUiHost host(state);
    return handleRuntimeMapRightPointerCompletedClick({{point.x, point.y}},
        runtimeRightPointerInputRect(clientRect), host);
}
