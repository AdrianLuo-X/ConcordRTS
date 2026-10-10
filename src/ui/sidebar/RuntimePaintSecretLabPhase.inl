// Included after makeRuntimeSpecialBuildingContext. The sidebar phase only
// changes UI/derived caches. No world/rules writer or message pump is reachable.
class RuntimePaintSecretLabPhaseScope {
public:
    inline static thread_local RuntimePaintSecretLabPhaseScope* active = nullptr;
    explicit RuntimePaintSecretLabPhaseScope(const RuntimeWindowState& state,
        bool readonlyInputs = false) noexcept
        : previous_(active), state_(&state), reference_(runtimeSecretLabMemoReference()),
          enabled_(readonlyInputs && !reference_ && !previous_) {
        if (previous_) previous_->invalidate();
        active = this;
    }
    ~RuntimePaintSecretLabPhaseScope() { active = previous_; }
    RuntimePaintSecretLabPhaseScope(const RuntimePaintSecretLabPhaseScope&) = delete;
    RuntimePaintSecretLabPhaseScope& operator=(const RuntimePaintSecretLabPhaseScope&) = delete;
    void invalidate() noexcept {
        enabled_ = false;
        if (memo_) memo_->invalidate();
    }
    const RuntimeSpecialBuildingContext* contextFor(const RuntimeWindowState& state) {
        if (!enabled_ || active != this || state_ != &state) return nullptr;
        if (!context_) {
            context_.emplace(makeRuntimeSpecialBuildingContext(state));
            memo_.emplace(*context_, true, reference_);
        }
        return &*context_;
    }
private:
    RuntimePaintSecretLabPhaseScope* previous_;
    const RuntimeWindowState* state_;
    bool reference_, enabled_;
    // Memo dies first; the context and its callback closures outlive its use.
    std::optional<RuntimeSpecialBuildingContext> context_;
    std::optional<RuntimeSecretLabReadOnlyMemoScope> memo_;
};
