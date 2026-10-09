// This overlay is drawn on the presentation DC, never into the cached scene.
RECT runtimeFpsDisplayRect(const RECT& clientRect) {
    const RECT viewport = runtimeMapViewport(clientRect);
    constexpr LONG width = 96;
    constexpr LONG height = 22;
    constexpr LONG margin = 8;
    constexpr LONG bottomInset = 64;
    if (viewport.right - viewport.left < width + margin * 2
        || viewport.bottom - viewport.top < bottomInset + height + margin) {
        return {};
    }
    const LONG left = viewport.left + margin;
    const LONG bottom = viewport.bottom - bottomInset;
    return {left, bottom - height, left + width, bottom};
}

std::wstring runtimeFpsDisplayText(const RuntimeFpsDisplayMeter& meter) {
    if (!meter.hasSample()) return L"FPS: --";
    const double rounded = std::round(meter.framesPerSecond());
    if (rounded > 9999.0) return L"FPS: 9999+";
    return L"FPS: " + std::to_wstring(static_cast<int>(rounded));
}

void drawRuntimeFpsDisplay(
    HDC dc,
    const RECT& clientRect,
    const RuntimeFpsDisplayMeter& meter
) {
    if (dc == nullptr) return;
    RECT bounds = runtimeFpsDisplayRect(clientRect);
    if (IsRectEmpty(&bounds)) return;
    const int saved = SaveDC(dc);
    if (saved == 0) return;
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SetDCBrushColor(dc, RGB(19, 29, 39));
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 240, 245));
    bounds.left += 6;
    bounds.right -= 4;
    const std::wstring label = runtimeFpsDisplayText(meter);
    DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &bounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}
