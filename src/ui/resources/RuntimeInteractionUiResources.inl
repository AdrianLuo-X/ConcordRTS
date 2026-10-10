// A paint does not change resource roots, house identity or UI palettes. Keep
// the existing owning caches authoritative, while avoiding faction discovery
// and resource-key construction for every two-pixel health pip. No result
// survives this call; unordered_map insertion preserves element addresses.
struct RuntimeInteractionUiResourceScope {
    static inline thread_local RuntimeInteractionUiResourceScope* active = nullptr;
    RuntimeInteractionUiResourceScope* previous = active;
    RuntimeWindowState* state = nullptr;
    using FrameKey = std::tuple<std::string, std::string, int>;
    std::map<FrameKey, RuntimeUiShpCacheEntry*> frames;
    std::unordered_map<std::string, const std::vector<RuntimePaletteColor>*> palettes;
    bool veterancyResolved = false;
    std::optional<std::pair<int, int>> veterancyFrames;

    explicit RuntimeInteractionUiResourceScope(RuntimeWindowState& owner, bool enabled = true) {
        static const bool reference = [] {
            const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
            return value && value[0] == '1' && value[1] == '\0';
        }();
        if (enabled && !reference) state = &owner;
        active = this;
    }
    ~RuntimeInteractionUiResourceScope() { active = previous; }
    static RuntimeInteractionUiResourceScope* forState(const RuntimeWindowState& owner) {
        return active && active->state == &owner ? active : nullptr;
    }
};
