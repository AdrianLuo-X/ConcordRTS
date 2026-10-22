// The original save/load owner may swap scratch state synchronously.
class RuntimeSaveLoadSessionKeyUiHost final : public RuntimeSaveLoadSessionKeyHost {
public:
    RuntimeSaveLoadSessionKeyUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    std::int32_t sampleCurrentKeyCode(const RuntimeSessionKeyInput& input) override {
        return runtimePlayerCurrentKeyCode(static_cast<WPARAM>(input.rawKey));
    }
    std::vector<RuntimeSessionKeyBinding> bindings() override {
        std::vector<RuntimeSessionKeyBinding> result;
        result.reserve(state_.runtimeMenuKeyboardBindings.size());
        for (const auto& binding : state_.runtimeMenuKeyboardBindings)
            result.push_back({binding.first, binding.second});
        return result;
    }
    bool dispatch(bool save, std::string& error) override {
        return dispatchRuntimeSaveLoadFromUi(state_, save, error);
    }
    bool multiplayerBridgePresent() override { return state_.multiplayerBridge != nullptr; }
    std::string defaultSavePath() override { return runtimeDefaultSavePath(state_); }
    void setInteractionStatus(std::string status) override { state_.interactionStatus = std::move(status); }
    std::string interactionStatus() override { return state_.interactionStatus; }
    void setEconomyStatus(std::string status) override { state_.economy.status = std::move(status); }
    void fileSound(RuntimeSessionFileSound sound) override {
        emitRuntimeSound(state_, sound == RuntimeSessionFileSound::Save ? "save"
            : sound == RuntimeSessionFileSound::Load ? "load" : "blocked");
    }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override { RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); }
private:
    RuntimeWindowState& state_;
    HWND window_;
};
