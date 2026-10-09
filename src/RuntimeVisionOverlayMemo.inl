// This is a rendering snapshot, not a gameplay revision or visibility cache.
// Exact comparison includes invalid cells and Gap Generator overrides because
// the compositor also consults ownership of TMP pixels outside base diamonds.
static bool runtimeVisionOverlayShroudMatches(const RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view) {
    if (cache.shroudedCellMask.size() != view.cellCount) return false;
    for (std::size_t index = 0; index < view.cellCount; ++index) {
        const bool shrouded = view.visibilityAtIndex(static_cast<int>(index))
            == RuntimeCellVisibility::Shrouded;
        if (cache.shroudedCellMask[index] != static_cast<std::uint8_t>(shrouded)) return false;
    }
    return true;
}

static void captureRuntimeVisionOverlayShroud(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view) {
    cache.shroudedCellMask.resize(view.cellCount);
    for (std::size_t index = 0; index < view.cellCount; ++index)
        cache.shroudedCellMask[index] = static_cast<std::uint8_t>(
            view.visibilityAtIndex(static_cast<int>(index)) == RuntimeCellVisibility::Shrouded);
}

static void refreshRuntimeVisionOverlayCounts(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view) {
    cache.visibleCellCount = cache.foggedCellCount = cache.shroudedCellCount = 0;
    for (const int index : cache.viewportTerrainCellIndices) {
        switch (view.visibilityAtIndex(index)) {
        case RuntimeCellVisibility::Visible: ++cache.visibleCellCount; break;
        case RuntimeCellVisibility::Fogged: ++cache.foggedCellCount; break;
        case RuntimeCellVisibility::Shrouded: ++cache.shroudedCellCount; break;
        }
    }
}

struct RuntimeVisionShroudColor {
    std::uint32_t color = 0;
    int mottle = 0;
};

static const std::array<RuntimeVisionShroudColor, 64 * 128>& runtimeVisionShroudColors() {
    static const auto colors = [] {
        std::array<RuntimeVisionShroudColor, 64 * 128> result{};
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 64; ++x) {
            const int waveA = 16 - std::abs(((x + y * 2) & 63) - 32);
            const int waveB = 32 - std::abs(((x * 2 - y) & 127) - 64);
            const int mottle = waveA / 4 + waveB / 8;
            const int coolLift = std::clamp((mottle + 8) / 6, 0, 2);
            result[static_cast<std::size_t>(y) * 64 + x] = {
                static_cast<std::uint32_t>(((3 + coolLift) << 16) | ((6 + coolLift) << 8) | (10 + coolLift)),
                mottle};
        }
        return result;
    }();
    return colors;
}

static std::uint32_t runtimeBlendVisionShroud(std::uint32_t pixel,
    std::uint32_t color, int alpha) {
    const unsigned int opacity = static_cast<unsigned int>(alpha);
    const unsigned int keep = 255U - opacity;
    const auto channel = [&](int shift) {
        return (((pixel >> shift) & 255U) * keep
            + ((color >> shift) & 255U) * opacity + 127U) / 255U;
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}
