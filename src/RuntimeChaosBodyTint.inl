bool runtimeObjectHasChaosBodyTint(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context
) {
    if (!object.alive || object.kind == WorldObjectKind::Structure
        || context.specialMechanismSnapshots == nullptr) return false;
    for (const auto& status : *context.specialMechanismSnapshots) {
        if (status.kind == "psychicStatus" && status.targetObjectId == object.instanceId
            && (status.phase == "BerserkFriendly" || status.phase == "BerserkEnemy")
            && (status.expireTick <= 0 || context.animationTick < status.expireTick)) return true;
    }
    return false;
}

std::uint32_t runtimeChaosTintPackedColor(std::uint32_t color) {
    // Tint the entire body after palette/remap/VPL shading. Keep the cached
    // original colors intact so the normal appearance returns on expiry.
    const int r = (color >> 16U) & 255U;
    const int g = (color >> 8U) & 255U;
    const int b = color & 255U;
    const int light = (r * 3 + g * 6 + b) / 10;
    return (static_cast<std::uint32_t>(std::min(255, 72 + light * 3 / 4)) << 16U)
        | (static_cast<std::uint32_t>(g / 4) << 8U)
        | static_cast<std::uint32_t>(b / 4);
}
