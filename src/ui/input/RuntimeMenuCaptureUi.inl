// All operations borrow the current root; existing assignment and persistence own effects.
class RuntimeMenuCaptureUiHost final : public RuntimeMenuCaptureHost {
public:
    explicit RuntimeMenuCaptureUiHost(RuntimeWindowState& state) : state_(state) {}
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    RuntimeMenuPage page() override { return state_.runtimeMenuPage; }
    std::int32_t captureIndex() override { return state_.runtimeMenuKeyCaptureIndex; }
    std::int32_t sampleCurrentKeyCode(const RuntimeMenuKeyInput& input) override {
        return runtimePlayerCurrentKeyCode(static_cast<WPARAM>(input.rawKey));
    }
    std::size_t bindingCount() override { return state_.runtimeMenuKeyboardBindings.size(); }
    std::int32_t defaultCode(std::int32_t index) override {
        return runtimeKeyboardDefaultAt(index);
    }
    void setCaptureIndex(std::int32_t index) override { state_.runtimeMenuKeyCaptureIndex = index; }
    void setCaptureStatus(RuntimeMenuCaptureStatus status) override {
        switch (status) {
        case RuntimeMenuCaptureStatus::Cancelled:
            state_.runtimeMenuStatus = L"已取消改键。";
            break;
        case RuntimeMenuCaptureStatus::ModifierNeedsKey:
            state_.runtimeMenuStatus = L"请同时按下组合键中的字母、数字或功能键。";
            break;
        }
    }
    bool assignBinding(std::int32_t index, std::int32_t code, bool restoredDefault) override {
        return assignRuntimeMenuKeyboardBinding(state_, index, code, restoredDefault);
    }
private:
    RuntimeWindowState& state_;
};
