struct RuntimeModShieldBarLayout {
    RuntimeInputPixelRect frame;
    RuntimeInputPixelRect fill;
    bool online;
};

std::optional<RuntimeModShieldBarLayout> runtimeModShieldBarLayout(
    const RulesDatabase& rules,
    const WorldObjectInstance& object,
    const RuntimeInputPixelRect& selectionBounds
) {
    const auto* shieldType = rules.findTechnoModExtension(object.type);
    const auto& shield = object.modExtensions;
    if (!shieldType || shieldType->shieldType.empty() || shield.shieldCapacity <= 0) {
        return std::nullopt;
    }
    const int width = object.kind == WorldObjectKind::Infantry ? 18 : 36;
    const int center = (selectionBounds.left + selectionBounds.right) / 2;
    const int top = selectionBounds.top - 15;
    const RuntimeInputPixelRect frame{center - width / 2, top, center + width / 2, top + 4};
    const int fill = static_cast<int>(
        static_cast<long long>(width - 2) * shield.shieldHealth / shield.shieldCapacity);
    return RuntimeModShieldBarLayout{
        frame, {frame.left + 1, top + 1, frame.left + 1 + fill, top + 3}, shield.shieldOnline};
}
