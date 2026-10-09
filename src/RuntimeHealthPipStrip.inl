// Include after the indexed-image UI primitives. This only combines the
// repeated opaque, transparent-index 2x2 pips within one unit's health bar.
thread_local int runtimeHealthPipStripReferenceOverride = -1;

bool runtimeHealthPipStripReference() {
    if (runtimeHealthPipStripReferenceOverride >= 0)
        return runtimeHealthPipStripReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value != nullptr && std::string(value) == "1";
    }();
    return reference;
}

class RuntimeHealthPipStripReferenceScope {
public:
    explicit RuntimeHealthPipStripReferenceScope(bool reference)
        : previous_(runtimeHealthPipStripReferenceOverride) {
        runtimeHealthPipStripReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeHealthPipStripReferenceScope() {
        runtimeHealthPipStripReferenceOverride = previous_;
    }
    RuntimeHealthPipStripReferenceScope(const RuntimeHealthPipStripReferenceScope&) = delete;
    RuntimeHealthPipStripReferenceScope& operator=(const RuntimeHealthPipStripReferenceScope&) = delete;
private:
    int previous_;
};

bool drawRuntimeHealthPipStripPixels(
    const RuntimeShpFrameImage& image,
    const std::vector<RuntimePaletteColor>& palette,
    bool useMagentaChroma,
    RuntimePixelSurfaceTarget target,
    int left,
    int top,
    int fillPixels
) {
    if (image.width != 2 || image.height != 2 || image.pixels.size() < 4
        || palette.empty() || target.pixels == nullptr
        || target.width <= 0 || target.height <= 0 || fillPixels < 0) {
        return false;
    }

    std::array<std::uint32_t, 4> colors{};
    std::array<bool, 4> writes{};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const unsigned char paletteIndex = image.pixels[index];
        if (paletteIndex == 0) continue;
        RuntimePaletteColor color{0, 0, 0};
        if (static_cast<std::size_t>(paletteIndex) < palette.size()) {
            color = palette[paletteIndex];
            if (useMagentaChroma && color.r >= 240 && color.b >= 240
                && color.g <= 32) continue;
        }
        // Match drawRuntimeIndexedImage's CPU path: a nonzero invalid index
        // writes black, and an opaque write clears the unused high byte.
        colors[index] = static_cast<std::uint32_t>(color.b)
            | (static_cast<std::uint32_t>(color.g) << 8)
            | (static_cast<std::uint32_t>(color.r) << 16);
        writes[index] = true;
    }

    const std::int64_t start = std::max<std::int64_t>(0, left);
    const std::int64_t end = std::min<std::int64_t>(target.width,
        static_cast<std::int64_t>(left) + fillPixels);
    for (int row = 0; row < 2; ++row) {
        const std::int64_t y = static_cast<std::int64_t>(top) + row;
        if (y < 0 || y >= target.height || start >= end) continue;
        std::uint32_t* destination = target.pixels
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width);
        for (std::int64_t x = start; x < end; ++x) {
            const std::size_t source = static_cast<std::size_t>(row * 2)
                + static_cast<std::size_t>((x - left) & 1);
            if (writes[source]) destination[static_cast<std::size_t>(x)] = colors[source];
        }
    }
    return true;
}

bool drawRuntimeHealthPipStrip(
    HDC dc,
    RuntimeWindowState& state,
    const RuntimeUiShpCacheEntry& image,
    const std::vector<RuntimePaletteColor>& palette,
    int left,
    int top,
    int fillPixels
) {
    if (runtimeHealthPipStripReference()) return false;
    return drawRuntimeHealthPipStripPixels(image.image, palette, image.pcxFallback,
        runtimePixelSurfaceTargetForDc(state, dc), left, top, fillPixels);
}
