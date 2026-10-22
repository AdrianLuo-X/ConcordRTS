// Private native message translation and effects for the standard force-held policy.
RuntimeForceAttackInput runtimeForceAttackInputForMessage(UINT message, WPARAM raw) {
    RuntimeForceAttackEvent event = RuntimeForceAttackEvent::Other;
    switch (message) {
    case WM_KEYDOWN: event = RuntimeForceAttackEvent::KeyDown; break;
    case WM_SYSKEYDOWN: event = RuntimeForceAttackEvent::SystemKeyDown; break;
    case WM_KEYUP: event = RuntimeForceAttackEvent::KeyUp; break;
    case WM_SYSKEYUP: event = RuntimeForceAttackEvent::SystemKeyUp; break;
    case WM_KILLFOCUS: event = RuntimeForceAttackEvent::KillFocus; break;
    case WM_ACTIVATEAPP: event = RuntimeForceAttackEvent::ActivateApplication; break;
    case WM_MOUSEMOVE: event = RuntimeForceAttackEvent::MouseMove; break;
    case WM_LBUTTONDOWN: event = RuntimeForceAttackEvent::LeftButtonDown; break;
    case WM_LBUTTONUP: event = RuntimeForceAttackEvent::LeftButtonUp; break;
    default: break;
    }
    return {event, static_cast<std::uint64_t>(raw)};
}

class RuntimeForceAttackHeldUiHost final : public RuntimeForceAttackHeldHost {
public:
    RuntimeForceAttackHeldUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    bool held() override { return state_.forceAttackHeld; }
    bool controlDown() override { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
    void setHeld(bool held) override { state_.forceAttackHeld = held; }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override {
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
private:
    RuntimeWindowState& state_;
    HWND window_;
};