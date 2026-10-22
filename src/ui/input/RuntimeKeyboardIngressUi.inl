// Private mechanical native translation and synchronous effects. The standard
// owners decide admission, priority and every conditional outer effect.
RuntimeCampaignInputEvent runtimeCampaignInputEventForMessage(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN: return RuntimeCampaignInputEvent::LeftButtonDown;
    case WM_LBUTTONUP: return RuntimeCampaignInputEvent::LeftButtonUp;
    case WM_LBUTTONDBLCLK: return RuntimeCampaignInputEvent::LeftButtonDoubleClick;
    case WM_RBUTTONDOWN: return RuntimeCampaignInputEvent::RightButtonDown;
    case WM_RBUTTONUP: return RuntimeCampaignInputEvent::RightButtonUp;
    case WM_RBUTTONDBLCLK: return RuntimeCampaignInputEvent::RightButtonDoubleClick;
    case WM_MBUTTONDOWN: return RuntimeCampaignInputEvent::MiddleButtonDown;
    case WM_MBUTTONUP: return RuntimeCampaignInputEvent::MiddleButtonUp;
    case WM_MBUTTONDBLCLK: return RuntimeCampaignInputEvent::MiddleButtonDoubleClick;
    case WM_XBUTTONDOWN: return RuntimeCampaignInputEvent::AuxiliaryButtonDown;
    case WM_XBUTTONUP: return RuntimeCampaignInputEvent::AuxiliaryButtonUp;
    case WM_XBUTTONDBLCLK: return RuntimeCampaignInputEvent::AuxiliaryButtonDoubleClick;
    case WM_MOUSEMOVE: return RuntimeCampaignInputEvent::PointerMove;
    case WM_MOUSEWHEEL: return RuntimeCampaignInputEvent::VerticalWheel;
    case WM_MOUSEHWHEEL: return RuntimeCampaignInputEvent::HorizontalWheel;
    case WM_KEYDOWN: return RuntimeCampaignInputEvent::KeyDown;
    case WM_KEYUP: return RuntimeCampaignInputEvent::KeyUp;
    case WM_CHAR: return RuntimeCampaignInputEvent::Character;
    case WM_SYSKEYDOWN: return RuntimeCampaignInputEvent::SystemKeyDown;
    case WM_SYSKEYUP: return RuntimeCampaignInputEvent::SystemKeyUp;
    default: return RuntimeCampaignInputEvent::Other;
    }
}

class RuntimeCampaignInputAdmissionUiHost final : public RuntimeCampaignInputAdmissionHost {
public:
    explicit RuntimeCampaignInputAdmissionUiHost(RuntimeWindowState& state) : state_(state) {}
    bool campaignMode() override { return state_.campaignMode; }
    bool campaignInputLocked() override { return state_.triggers.campaignInputLocked; }
    bool finished() override { return state_.skirmishMatchFinished; }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
private:
    RuntimeWindowState& state_;
};

RuntimeKeyboardIngressInput runtimeKeyboardIngressInputForMessage(
    UINT message, WPARAM key, LPARAM flags) {
    return {static_cast<std::uint64_t>(key),
        (flags & (static_cast<LPARAM>(1) << 30)) != 0,
        message == WM_SYSKEYDOWN ? RuntimeKeyDownSource::SystemKeyDown : RuntimeKeyDownSource::KeyDown};
}

class RuntimeKeyboardIngressUiHost final : public RuntimeKeyboardIngressHost {
public:
    RuntimeKeyboardIngressUiHost(RuntimeWindowState& state, HWND window, LPARAM nativeFlags)
        : state_(state), window_(window), nativeFlags_(nativeFlags) {}
    bool diplomacyOpen() override { return state_.playerCommunication.diplomacyOpen; }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool finished() override { return state_.skirmishMatchFinished; }
    bool captureOwned() override { return GetCapture() == window_; }
    bool controlDown() override { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
    void releaseCapture() override { ReleaseCapture(); }
    void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override { RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); }
    bool handleModalKey(std::int32_t rawKey, bool repeated) override {
        return handleRuntimePlayerModalKey(state_, rawKey, repeated);
    }
    bool handleMenuCaptureKey(const RuntimeKeyboardIngressInput& input) override {
        return handleRuntimeMenuKeyCapture(state_, static_cast<WPARAM>(input.rawKey), input.repeated);
    }
    bool handleResultSessionKey(const RuntimeKeyboardIngressInput& input) override {
        RuntimeResultSessionKeyUiHost host(state_, window_);
        return ::handleRuntimeResultSessionKey({input.rawKey, input.repeated}, host);
    }
    bool handleOptionsSessionKey(const RuntimeKeyboardIngressInput& input) override {
        RuntimeOptionsSessionKeyUiHost host(state_, window_);
        return ::handleRuntimeOptionsSessionKey({input.rawKey, input.repeated}, host);
    }
    bool handleSaveLoadSessionKey(const RuntimeKeyboardIngressInput& input, bool controlDown) override {
        RuntimeSaveLoadSessionKeyUiHost host(state_, window_);
        return ::handleRuntimeSaveLoadSessionKey({input.rawKey, input.repeated}, controlDown, host);
    }
    bool handleMenuNavigationKey(const RuntimeKeyboardIngressInput& input) override {
        RuntimeMenuNavigationUiHost host(state_, window_);
        return ::handleRuntimeMenuNavigationKey({input.rawKey, input.repeated}, host);
    }
    bool handleCommandKey(const RuntimeKeyboardIngressInput& input) override {
        // Preserve the complete original native argument and the original wrapper.
        // That wrapper still obtains client first and passes key/time in one call.
        return handleRuntimePlayerCommandKey(state_, window_, static_cast<WPARAM>(input.rawKey), nativeFlags_);
    }
    RuntimeSelectionKeyResult handleSelectionKey(const RuntimeKeyboardIngressInput& input) override {
        RuntimeSelectionKeyWin32Host host(state_, static_cast<WPARAM>(input.rawKey));
        return handleRuntimeSelectionKey({static_cast<std::int32_t>(input.rawKey), input.repeated, input.source}, host);
    }
    bool handleCameraKey(const RuntimeKeyboardIngressInput& input) override {
        RuntimeCameraKeyWin32Host host(state_, window_);
        return handleRuntimeCameraKey({input.rawKey, input.repeated}, host);
    }
private:
    RuntimeWindowState& state_;
    HWND window_;
    LPARAM nativeFlags_;
};