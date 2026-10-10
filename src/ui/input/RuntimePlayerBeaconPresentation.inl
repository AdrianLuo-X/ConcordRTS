// Retail Beacon.CPP loads PBEACON and RDRBEACN independently of Art[BEACON].
// The native global-frame clock is adapted to this engine's 15 authored frames/s.
int runtimePlayerBeaconNativeFrame(const RuntimeWindowState& state, bool selected) {
    const auto frame = static_cast<std::int64_t>(std::max(0, state.tick)) * 15
        / std::max(1, state.config.fixedTickHz);
    return static_cast<int>(frame % 10) + (selected ? 10 : 0);
}

int runtimePlayerBeaconRadarNativeFrame(const RuntimeWindowState& state) {
    const auto frame = static_cast<std::int64_t>(std::max(0, state.tick)) * 15
        / std::max(1, state.config.fixedTickHz);
    const int phase = static_cast<int>(frame % 32);
    return phase < 8 ? phase : -1;
}

POINT runtimePlayerBeaconScreenPoint(const RuntimeWindowState& state,
    const RECT& viewport, const RuntimePlayerBeacon& beacon) {
    return runtimeScreenPointForCell(state, viewport,
        {beacon.cell.x, beacon.cell.y}, beacon.level);
}

std::vector<RuntimePaletteColor> runtimePlayerBeaconPalette(RuntimeWindowState& state,
    const RuntimePlayerBeacon& beacon) {
    const auto candidates = unitPaletteCandidatesForRuntimeTheater(state.report.map.theater);
    if (candidates.empty()) return {};
    const auto paletteId = std::filesystem::path(candidates.back()).stem().string();
    const auto* palette = runtimeUiPalette(state, paletteId);
    return palette ? runtimeOwnerRemappedUiPalette(*palette,
        runtimeRadarColorForOwner(state, beacon.owner)) : std::vector<RuntimePaletteColor>{};
}

bool drawRuntimePlayerBeaconFrame(HDC dc, const RECT& clip, RuntimeWindowState& state,
    const RuntimeShpFrameImage& image, const std::vector<RuntimePaletteColor>& palette,
    POINT center) {
    if (!dc || image.width <= 0 || image.height <= 0
        || image.globalWidth <= 0 || image.globalHeight <= 0 || palette.empty()
        || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height) return false;
    const LONG left = center.x - image.globalWidth / 2 + image.offsetX;
    const LONG top = center.y - image.globalHeight / 2 + image.offsetY;
    const RECT rect{left, top, left + image.width, top + image.height};
    RECT visible{};
    if (!IntersectRect(&visible, &rect, &clip)) return false;

    // Direct DIB writes do not consult the DC clip. Crop actual indexed pixels
    // at the viewport boundary, retaining their original 1:1 placement.
    RuntimeShpFrameImage cropped;
    const RuntimeShpFrameImage* source = &image;
    if (!EqualRect(&visible, &rect)) {
        cropped.width = static_cast<int>(visible.right - visible.left);
        cropped.height = static_cast<int>(visible.bottom - visible.top);
        cropped.pixels.reserve(static_cast<std::size_t>(cropped.width) * cropped.height);
        const int sourceX = static_cast<int>(visible.left - rect.left);
        const int sourceY = static_cast<int>(visible.top - rect.top);
        for (int y = 0; y < cropped.height; ++y) {
            const auto begin = image.pixels.begin()
                + static_cast<std::size_t>(sourceY + y) * image.width + sourceX;
            cropped.pixels.insert(cropped.pixels.end(), begin, begin + cropped.width);
        }
        source = &cropped;
    }
    GdiFlush();
    const auto target = runtimePixelSurfaceTargetForDc(state, dc);
    return drawRuntimeIndexedImage(dc, visible, *source, palette, true, true, true,
        target.pixels, target.width, target.height);
}

void drawRuntimePlayerBeaconCaption(HDC dc, RuntimeWindowState& state,
    const RuntimePlayerBeacon& beacon, POINT center) {
    if (beacon.text.empty()) return;
    const auto& font = runtimeResultBitFont(state);
    if (font.bytes.empty()) return;

    constexpr int maxWidth = 200;
    std::vector<std::wstring> lines;
    std::wstring line;
    for (const wchar_t c : widenUtf8(beacon.text)) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            lines.push_back(std::move(line));
            line.clear();
            continue;
        }
        if (!line.empty() && font.measure(line + c) > maxWidth) {
            lines.push_back(std::move(line));
            line.clear();
        }
        line += c;
    }
    lines.push_back(std::move(line));
    int width = 0;
    for (const auto& value : lines) width = std::max(width, font.measure(value));
    const LONG lineHeight = static_cast<LONG>(font.height);
    const LONG left = center.x - (width + 8) / 2;
    const LONG top = center.y + 32;
    const RECT background{left, top, left + width + 8,
        top + static_cast<LONG>(lines.size()) * lineHeight + 8};
    HBRUSH brush = CreateSolidBrush(runtimeRadarColorForOwner(state, beacon.owner));
    if (brush) {
        FillRect(dc, &background, brush);
        DeleteObject(brush);
    }
    // The bitmap font normally scales an 800x600 menu canvas. Beacon labels
    // use native-sized glyphs at the already projected ground anchor instead.
    const RECT identityClient{0, 0, 800, 600};
    LONG y = top + 4;
    for (const auto& value : lines) {
        const RECT bounds{left + 4, y, left + width + 4, y + lineHeight};
        font.draw(dc, identityClient, bounds, value, RGB(255, 255, 255), false);
        y += lineHeight;
    }
}

bool drawRuntimePlayerBeaconOverlay(HDC dc, const RECT& viewport,
    RuntimeWindowState& state) {
    if (!dc || viewport.right <= viewport.left || viewport.bottom <= viewport.top) return false;
    const int saved = SaveDC(dc);
    if (!saved) return false;
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    const auto viewer = runtimePlayerCommunicationViewerOwner(state);
    bool drawn = false;
    for (const auto& beacon : state.playerCommunication.beacons) {
        if (!runtimePlayerBeaconVisibleToLocal(state, beacon)) continue;
        const bool selected = runtimeCanonicalHumanHouseOwner(beacon.owner) == viewer
            && state.playerCommunication.selectedBeacon == beacon.cell;
        const auto* image = loadRuntimeUiShpFrame(state, "sidebar", "PBEACON",
            runtimePlayerBeaconNativeFrame(state, selected));
        if (!image) continue;
        const auto palette = runtimePlayerBeaconPalette(state, beacon);
        if (palette.empty()) continue;
        const POINT center = runtimePlayerBeaconScreenPoint(state, viewport, beacon);
        drawn = drawRuntimePlayerBeaconFrame(dc, viewport, state, image->image, palette, center) || drawn;
        drawRuntimePlayerBeaconCaption(dc, state, beacon, center);
    }
    RestoreDC(dc, saved);
    return drawn;
}

bool drawRuntimePlayerRadarBeacon(HDC dc, const RuntimeRadarLayout& layout,
    const RuntimeRadarProjection& projection, RuntimeWindowState& state,
    const RuntimePlayerBeacon& beacon) {
    if (!dc || !projection.valid || layout.scaleX <= 0.0 || layout.scaleY <= 0.0
        || !runtimePlayerBeaconVisibleToLocal(state, beacon)) return false;
    const int frame = runtimePlayerBeaconRadarNativeFrame(state);
    if (frame < 0) return false;
    auto dxy = runtimeRadarDxyPoint(beacon.cell.x, beacon.cell.y, projection.fullMapWidth);
    dxy.y += 1.0;
    if (!runtimeRadarProjectionContains(projection, dxy)) return false;
    const auto* image = loadRuntimeUiShpFrame(state, "sidebar", "RDRBEACN", frame);
    if (!image) return false;
    const auto palette = runtimePlayerBeaconPalette(state, beacon);
    if (palette.empty()) return false;
    const int saved = SaveDC(dc);
    if (!saved) return false;
    IntersectClipRect(dc, layout.rect.left, layout.rect.top, layout.rect.right, layout.rect.bottom);
    const bool drawn = drawRuntimePlayerBeaconFrame(dc, layout.rect, state, image->image,
        palette, runtimeRadarPointFromDxy(layout, projection, dxy));
    RestoreDC(dc, saved);
    return drawn;
}
