// Borrow only the stable root; every query/effect reborrows its current child.
class RuntimeOptionsSessionKeyUiHost final : public RuntimeOptionsSessionKeyHost {
public:
    RuntimeOptionsSessionKeyUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    std::int32_t binding(const std::string& id, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, id, fallback);
    }
    std::int32_t sampleCurrentKeyCode(const RuntimeSessionKeyInput& input) override {
        return runtimePlayerCurrentKeyCode(static_cast<WPARAM>(input.rawKey));
    }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool beaconTargeting() override { return state_.playerCommunication.beaconTargeting; }
    bool followTargeting() override { return state_.playerLocalCommands.followTargeting; }
    bool rallyTargeting() override { return state_.playerLocalCommands.rallyTargeting; }
    bool sidebarToolIsActive() override { return state_.activeSidebarTool != RuntimeSidebarTool::None; }
    void setBeaconTargeting(bool active) override { state_.playerCommunication.beaconTargeting = active; }
    void setRallyTargeting(bool active) override { state_.playerLocalCommands.rallyTargeting = active; }
    void setFollowTargeting(bool active) override { state_.playerLocalCommands.followTargeting = active; }
    void clearSidebarTool() override { state_.activeSidebarTool = RuntimeSidebarTool::None; }
    void setSelectionDragMoved(bool moved) override { state_.leftPointer.moved = moved; }
    void setSelectionDragActive(bool active) override { state_.leftPointer.active = active; }
    bool ownsCapture() override { return GetCapture() == window_; }
    void releaseCapture() override { ReleaseCapture(); }
    bool cancelSuperWeaponTargetSelection(RuntimeSessionCancelReason reason) override {
        return cancelRuntimeActiveSuperWeaponTargetSelection(
            state_, reason == RuntimeSessionCancelReason::Escape ? "escape" : "options");
    }
    bool dismissPlacement() override { return dismissRuntimePlacementFromUi(state_); }
    RuntimeMenuPage page() override { return state_.runtimeMenuPage; }
    RuntimeMenuPage parentPage(RuntimeMenuPage page) override { return runtimeMenuParentPage(page); }
    void resetPage(RuntimeMenuPage page) override { resetRuntimeMenuPage(state_, page); }
    void setMenuOpen(bool open) override { setRuntimeMenuOpen(state_, open); }
    void menuSound(bool opening) override {
        emitRuntimeSound(state_, opening ? runtimeSoundIntentMenuOpen : runtimeSoundIntentMenuClose);
    }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override { RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); }
    void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }
private:
    RuntimeWindowState& state_;
    HWND window_;
};
