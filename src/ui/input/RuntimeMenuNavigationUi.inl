// Private synchronous adapter; menu layout/native rectangle never leaves this file.
class RuntimeMenuNavigationUiHost final : public RuntimeMenuNavigationHost {
public:
    RuntimeMenuNavigationUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    RuntimeMenuNavigationUiHost(RuntimeWindowState& state, const RECT& client)
        : state_(state), client_(client) {}
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    RuntimeMenuPage page() override { return state_.runtimeMenuPage; }
    void prepareClient() override {
        client_ = {};
        GetClientRect(window_, &client_);
    }
    std::vector<RuntimeMenuAction> focusableActions() override {
        const auto buttons = runtimeMenuFocusableButtons(state_, client_);
        std::vector<RuntimeMenuAction> actions;
        actions.reserve(buttons.size());
        for (const auto& button : buttons) actions.push_back(button.action);
        return actions;
    }
    std::int32_t focus() override { return state_.runtimeMenuFocus; }
    void setFocus(std::int32_t value) override { state_.runtimeMenuFocus = value; }
    void changeKeyboardPage(std::int32_t direction, bool byCategory) override {
        runtimeKeyboardChangePage(state_, direction, byCategory);
    }
    bool adjustSlider(RuntimeMenuAction action, std::int32_t direction) override {
        return adjustRuntimeMenuSliderPosition(state_, action, direction);
    }
    void clearHover() override { state_.runtimeMenuHover = RuntimeMenuAction::None; }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override {
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
    void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }
    void buttonSound() override { emitRuntimeSound(state_, runtimeSoundIntentSidebarButtonClick); }
    void activate(RuntimeMenuAction action) override {
        activateRuntimeMenuAction(window_, state_, action);
    }
private:
    RuntimeWindowState& state_;
    HWND window_ = nullptr;
    RECT client_{};
};
