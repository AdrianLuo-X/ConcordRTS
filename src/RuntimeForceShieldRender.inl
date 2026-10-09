// Tint only pixels painted by this object, at its normal depth in the
// scene. Ground, shadows and later foreground objects retain their colours.
class RuntimeForceShieldTintScope {
public:
    RuntimeForceShieldTintScope(HDC dc, const RECT& viewport,
        const RuntimeMapRenderContext& context, RuntimeMapRenderStats& stats,
        POINT center, int margin, bool enabled, int ironCurtainExpireTick = -1)
        : dc_(dc), context_(context), stats_(stats) {
        iron_ = !enabled && ironCurtainExpireTick > context.animationTick;
        if (!enabled && !iron_) return;
        if (iron_) {
            // Retail 0x70E3FA/0x70E40F and 0x70E6E1..0x70E7CE:
            // the sustained pulse rises for 8 native frames and falls for 16,
            // with Q8 intensity 51..128. Anchor to the saved expiration so a
            // load or paused redraw never restarts or advances the pulse.
            const auto remaining = static_cast<std::int64_t>(ironCurtainExpireTick - context.animationTick)
                * 15 / std::max(1, context.fixedTickHz);
            const int phase = static_cast<int>((24 - remaining % 24) % 24);
            ironIntensity_ = phase < 8 ? (1024 - 77 * (8 - phase)) / 8
                : (816 + 77 * (24 - phase)) / 16;
        }
        direct_ = hasPixelSurface(context);
        if (!direct_ && dc == nullptr) return;
        bounds_ = {std::max(viewport.left, center.x - margin),
            std::max(viewport.top, center.y - margin),
            std::min(viewport.right, center.x + margin + 1),
            std::min(viewport.bottom, center.y + margin + 1)};
        if (direct_) {
            bounds_.left = std::max<LONG>(0, bounds_.left);
            bounds_.top = std::max<LONG>(0, bounds_.top);
            bounds_.right = std::min<LONG>(context.surfaceWidth, bounds_.right);
            bounds_.bottom = std::min<LONG>(context.surfaceHeight, bounds_.bottom);
        }
        if (bounds_.left >= bounds_.right || bounds_.top >= bounds_.bottom) return;
        GdiFlush();
        before_.reserve(static_cast<std::size_t>(bounds_.right - bounds_.left)
            * (bounds_.bottom - bounds_.top));
        for (int y = bounds_.top; y < bounds_.bottom; ++y)
            for (int x = bounds_.left; x < bounds_.right; ++x) before_.push_back(read(x,y));
    }

    ~RuntimeForceShieldTintScope() {
        if (before_.empty()) return;
        GdiFlush();
        std::size_t index = 0;
        bool changed = false;
        for (int y = bounds_.top; y < bounds_.bottom; ++y) {
            for (int x = bounds_.left; x < bounds_.right; ++x, ++index) {
                const auto pixel = read(x,y);
                if (pixel == before_[index] || (!direct_ && pixel == CLR_INVALID)) continue;
                const auto color = direct_ ? colorRefFromRuntimeDibColor(pixel) : pixel;
                const int light = (GetRValue(color)*77 + GetGValue(color)*150 + GetBValue(color)*29) / 256;
                const COLORREF tint = iron_
                    ? RGB(GetRValue(color)*ironIntensity_/256, GetGValue(color)*ironIntensity_/256,
                        GetBValue(color)*ironIntensity_/256)
                    : RGB(24 + light/9, 12 + light/7, 64 + light*3/4);
                if (direct_) context_.surfacePixels[y*context_.surfaceWidth+x] = packRuntimeDibColor(tint);
                else SetPixelV(dc_,x,y,tint);
                changed = true;
            }
        }
        if (changed) {
            ++stats_.specialMechanismRenderActors;
            runtimeMixRenderIntentHash(stats_.renderIntentHash,iron_ ? "ironCurtainTint" : "forceShieldTint");
        }
    }

    RuntimeForceShieldTintScope(const RuntimeForceShieldTintScope&) = delete;
    RuntimeForceShieldTintScope& operator=(const RuntimeForceShieldTintScope&) = delete;

private:
    std::uint32_t read(int x, int y) const {
        return direct_ ? context_.surfacePixels[y*context_.surfaceWidth+x] : GetPixel(dc_,x,y);
    }
    HDC dc_;
    const RuntimeMapRenderContext& context_;
    RuntimeMapRenderStats& stats_;
    RECT bounds_{};
    bool direct_ = false;
    bool iron_ = false;
    int ironIntensity_ = 256;
    std::vector<std::uint32_t> before_;
};
