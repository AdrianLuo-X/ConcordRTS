COLORREF runtimeMagBeamPixelColor(COLORREF background, int strength) {
    // Retail gamemd 0x760190: amplify the sampled red by k/2 and blue
    // by 4*k (both /256); green is untouched. This is transparent light,
    // not a solid MagnaBeamColor overlay. Native k is 110 + 8*[0..9].
    const int red=GetRValue(background),blue=GetBValue(background);
    return RGB(std::min(255,red+((red*(strength/2))>>8)),GetGValue(background),
        std::min(255,blue+((blue*strength*4)>>8)));
}

void drawRuntimeMagBeamStatus(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, POINT from, POINT to,
    int intensity, int seed, const std::string& /*color*/) {
    const bool direct = hasPixelSurface(context);
    if (!direct && !dc) return;
    if (direct && dc) GdiFlush();
    const int frame = static_cast<int>(static_cast<std::int64_t>(context.animationTick)
        * 15 / std::max(1, context.fixedTickHz));
    const double dx = static_cast<double>(to.x) - from.x, dy = static_cast<double>(to.y) - from.y;
    const double square = dx * dx + dy * dy, length = std::sqrt(square);
    constexpr double radius = 3.5;
    const int left = std::max<int>(viewport.left, direct ? 0 : viewport.left);
    const int top = std::max<int>(viewport.top, direct ? 0 : viewport.top);
    const int right = std::min<int>(viewport.right, direct ? context.surfaceWidth : viewport.right);
    const int bottom = std::min<int>(viewport.bottom, direct ? context.surfaceHeight : viewport.bottom);
    const auto pixel = [&](int x, int y) {
        if (x < left || x >= right || y < top || y >= bottom) return;
        const double t = square == 0.0 ? 0.0 : std::clamp(((x - from.x) * dx + (y - from.y) * dy) / square, 0.0, 1.0);
        const double distance = std::hypot(x - from.x - t * dx, y - from.y - t * dy);
        const double coverage = std::clamp(radius - distance, 0.0, 1.0);
        if (coverage == 0.0) return;
        const int strength=110+8*static_cast<int>(runtimeElectricBoltNoise(seed,frame,x,y)%10U);
        COLORREF background;
        if(direct) {
            const auto packed=context.surfacePixels[y*context.surfaceWidth+x];
            background=RGB((packed>>16)&255,(packed>>8)&255,packed&255);
        } else {
            background=concord::ui::win32::MapNativeOverlayWin32::readPixel(dc,x,y);
            if(background==CLR_INVALID)return;
        }
        const auto illuminated=runtimeMagBeamPixelColor(background,strength);
        const int alpha=std::clamp(static_cast<int>(coverage*255.0
            *std::clamp(intensity,0,255)/208.0),0,255);
        const auto result=runtimeBlendTint(background,illuminated,alpha);
        if(direct)context.surfacePixels[y*context.surfaceWidth+x]=packDibColor(result);
        else SetPixelV(dc,x,y,result);
    };
    // Keep the existing straight beam footprint; native channel amplification
    // leaves the terrain and unit details visible inside it.
    if (std::abs(dx) >= std::abs(dy)) {
        const double extent = radius * (dx == 0.0 ? 1.0 : length / std::abs(dx));
        for (int x = std::max(left, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
            x <= std::min(right - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + radius))); ++x) {
            const double t = dx == 0.0 ? 0.0 : std::clamp((x - from.x) / dx, 0.0, 1.0);
            const double y = from.y + t * dy;
            for (int iy = std::max(top, static_cast<int>(std::floor(y - extent)));
                iy <= std::min(bottom - 1, static_cast<int>(std::ceil(y + extent))); ++iy) pixel(x, iy);
        }
    } else {
        const double extent = radius * length / std::abs(dy);
        for (int y = std::max(top, static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
            y <= std::min(bottom - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + radius))); ++y) {
            const double x = from.x + std::clamp((y - from.y) / dy, 0.0, 1.0) * dx;
            for (int ix = std::max(left, static_cast<int>(std::floor(x - extent)));
                ix <= std::min(right - 1, static_cast<int>(std::ceil(x + extent))); ++ix) pixel(ix, y);
        }
    }
}
