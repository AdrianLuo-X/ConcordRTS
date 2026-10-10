struct RuntimeLoadingCountryText {
    const char* imageToken;
    const char* countryKey;
    const char* unitKey;
    const char* flag;
};

RuntimeLoadingCountryText runtimeLoadingCountryText(const std::string& imageId) {
    static constexpr RuntimeLoadingCountryText countries[]{
        {"ustates", "Name:Americans", "Name:Para", "usai"},
        {"ukingdom", "Name:British", "Name:SNIPE", "gbri"},
        {"germany", "Name:Germans", "Name:TNKD", "geri"},
        {"france", "Name:French", "Name:GTGCAN", "frai"},
        {"korea", "Name:Alliance", "Name:BEAGLE", "japi"},
        {"russia", "Name:Russians", "Name:TTNK", "rusi"},
        {"libya", "Name:Africans", "Name:DTRUCK", "arbi"},
        {"iraq", "Name:Arabs", "Name:DESO", "djbi"},
        {"cuba", "Name:Confederation", "Name:TERROR", "lati"},
        {"yuri", "Name:YuriCountry", "Name:YURIPR", "yrii"}
    };
    const auto id = toLowerAsciiLocal(imageId);
    for (const auto& country : countries) if (id.find(country.imageToken) != std::string::npos) return country;
    return {"obs", "Name:Observer", "GUI:Observer", "rani"};
}

std::optional<std::pair<double, double>> runtimeLoadingPreviewStartPosition(
    const IniFile& map,
    const RuntimeCellPoint& cell,
    int waypointId
) {
    const int x = parseRuntimeIntOrDefault(map.getValue("Header", "StartX", "0"), 0);
    const int y = parseRuntimeIntOrDefault(map.getValue("Header", "StartY", "0"), 0);
    const int width = parseRuntimeIntOrDefault(map.getValue("Header", "Width", "0"), 0);
    const int height = parseRuntimeIntOrDefault(map.getValue("Header", "Height", "0"), 0);
    const auto normalized = [](double px, double py) -> std::optional<std::pair<double, double>> {
        if (px < 0 || px > 1 || py < 0 || py > 1) return std::nullopt;
        return std::make_pair(px, py);
    };
    // Header coordinates belong to numbered waypoints, not compacted preview
    // entries or lobby slots. Missing entries must never shift another player.
    if (waypointId >= 0 && waypointId < 8 && width > 0 && height > 0) {
        const auto point = splitRuntimeTokenList(map.getValue("Header", "Waypoint" + std::to_string(waypointId + 1), ""));
        int px = 0, py = 0;
        if (point.size() == 2 && parseRuntimeInt(point[0], px) && parseRuntimeInt(point[1], py)
            && (px != 0 || py != 0)) {
            if (const auto position = normalized((static_cast<double>(px) - x) / width,
                    (static_cast<double>(py) - y) / height)) return position;
        }
    }
    // Older maps omit Header metadata. Their stock preview is an isometric
    // projection cropped to Map/LocalSize, rather than raw world-cell x/y.
    const auto size = splitRuntimeTokenList(map.getValue("Map", "Size", ""));
    const auto local = splitRuntimeTokenList(map.getValue("Map", "LocalSize", ""));
    if (size.size() != 4 || local.size() != 4) return std::nullopt;
    const int mapWidth = parseRuntimeIntOrDefault(size[2], 0);
    const int localWidth = parseRuntimeIntOrDefault(local[2], 0);
    const int localHeight = parseRuntimeIntOrDefault(local[3], 0);
    if (mapWidth <= 0 || localWidth <= 0 || localHeight <= 0) return std::nullopt;
    return normalized(
        ((static_cast<double>(cell.x) - cell.y + mapWidth) * 0.5
            - parseRuntimeIntOrDefault(local[0], 0)) / localWidth,
        ((static_cast<double>(cell.x) + cell.y - mapWidth) * 0.5
            - parseRuntimeIntOrDefault(local[1], 0)) / localHeight);
}

void refreshRuntimeLoadingMapStarts(RuntimeLoadingWindowState& loading, const RuntimeWindowState& runtime) {
    loading.mapStarts.clear();
    if (runtime.campaignMode || loading.mapStartAssignments.empty()) return;
    const auto candidates = runtimeSkirmishStartCandidates(runtime);
    for (std::size_t slotIndex = 0; slotIndex < loading.mapStartAssignments.size(); ++slotIndex) {
        const bool legacyPlayer = runtime.config.playerSlots.empty() && slotIndex == 0;
        if (!legacyPlayer && (slotIndex >= runtime.config.playerSlots.size()
                || !runtime.config.playerSlots[slotIndex].enabled
                || runtimeObserverSlot(runtime.config.playerSlots[slotIndex]))) continue;
        if (legacyPlayer && runtimeLocalObserver(runtime.config)) continue;
        const int candidateIndex = loading.mapStartAssignments[slotIndex];
        if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= candidates.size()) continue;
        const auto& cell = candidates[static_cast<std::size_t>(candidateIndex)];
        int waypointId = -1;
        for (const auto& waypoint : runtime.world.getWaypoints()) {
            int id = -1;
            if (waypoint.x == cell.x && waypoint.y == cell.y && parseRuntimeInt(waypoint.id, id)
                && id >= 0 && id < 8 && (waypointId < 0 || id < waypointId)) waypointId = id;
        }
        const auto position = runtimeLoadingPreviewStartPosition(loading.mapIni, cell, waypointId);
        if (!position) continue;
        const auto owner = legacyPlayer ? runtime.economy.owner : runtimeSkirmishOwnerForSlot(slotIndex);
        const auto color = runtime.ownerRemapColors.find(toLowerAsciiLocal(trimAsciiLocal(owner)));
        loading.mapStarts.push_back({position->first, position->second,
            color != runtime.ownerRemapColors.end() ? color->second : runtimeOwnerRadarColor(owner), slotIndex});
    }
}

void prepareRuntimeLoadingDetails(RuntimeLoadingWindowState& loading, RuntimeWindowState& runtime) {
    const auto text = runtimeLoadingCountryText(loading.imageId);
    loading.countryName = runtime.config.loadingCountryDisplayName.empty()
        ? runtimeMenuLabel(runtime, text.countryKey, L"") : runtime.config.loadingCountryDisplayName;
    loading.unitName = runtimeMenuLabel(runtime, text.unitKey, L"");
    loading.countryBrief = runtime.config.loadingCountryBrief.empty()
        ? runtimeMenuLabel(runtime, runtimeLoadingBriefKeyForCountry(runtime.config.playerCountry), L"")
        : runtime.config.loadingCountryBrief;
    loading.loadingLabel = runtimeMenuLabel(runtime, "GUI:LoadingEx", L"载入中 ..");
    loading.textColor = runtimeCountryIsYuri(runtime.config.playerCountry) ? RGB(200, 70, 255)
        : runtimeCountryIsSoviet(runtime.config.playerCountry) ? RGB(255, 0, 0) : RGB(75, 105, 255);
    const auto slotIndex = runtimeLocalSkirmishSlotIndex(runtime.config);
    const RuntimeConfig::PlayerSlot* slot = slotIndex && *slotIndex < runtime.config.playerSlots.size()
        ? &runtime.config.playerSlots[*slotIndex] : nullptr;
    loading.playerName = slot && !slot->name.empty() ? widenUtf8(slot->name)
        : runtimeMenuLabel(runtime, "GUI:Player", L"玩家");
    loading.playerColor = runtimeRemapColorFromToken(runtime.config,
        slot ? slot->color : runtime.config.playerColor, RGB(64, 128, 255));
    const std::filesystem::path root(runtimeAssetRootPath(runtime.config));
    std::string error, source;
    loading.flag = {};
    loading.flagPalette.clear();
    loading.flagLoaded = false;
    loading.flagPcx = false;
    for (const auto& path : {std::filesystem::path(runtime.config.modRootPath) / (std::string(text.flag) + ".pcx"),
            root / "ra2md" / "localmd" / (std::string(text.flag) + ".pcx")}) {
        if (decodeRuntimePcxImageFile(pathString(path), loading.flag, loading.flagPalette, error)) {
            loading.flagLoaded = loading.flagPcx = true; break;
        }
    }
    if (!loading.flagLoaded) {
        loading.flagLoaded = loadRuntimePcxImageFromMixArchive(pathString(root / "ra2" / "local.mix"),
            runtime.renderCache, text.flag, loading.flag, loading.flagPalette, source, error);
        loading.flagPcx = loading.flagLoaded;
    }
    if (!loading.flagLoaded) {
        loading.flagLoaded = decodeRuntimeShpFrameFile(pathString(root / "ra2md" / "localmd"
            / (std::string(text.flag) + ".shp")), 0, loading.flag, error)
            && loadRuntimePaletteFile(pathString(root / "ra2md" / "localmd"
                / (std::string(text.flag) == "yrii" ? "yrii.pal" : "mpyscrn.pal")), loading.flagPalette, error);
    }
    loading.mapPreview = {};
    loading.mapStarts.clear();
    loading.mapStartAssignments.clear();
    loading.mapIni = {};
    if (!runtime.config.defaultMapPath.empty() && loading.mapIni.loadFromFile(runtime.config.defaultMapPath)) {
        (void)decodeMapPreviewImage(loading.mapIni, loading.mapPreview, error);
    }
}

void drawRuntimeCountryLoading(HDC dc, const RECT& client, const RuntimeLoadingWindowState& loading) {
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const RECT canvas = runtimeMatchScoreScaledRect(client, RECT{0, 0, 800, 600});
    if (loading.originalImageLoaded) drawRuntimeIndexedImage(dc, canvas, loading.image, loading.palette,
        true, true, false, nullptr, 0, 0, false, runtimeIndexedOpaqueAlpha,
        loading.originalImagePcxFallback, loading.originalImagePcxFallback);
    if (!loading.runtime) return;
    const auto& font = runtimeResultBitFont(*loading.runtime);
    const auto text = [&](const std::wstring& value, RECT rect, COLORREF color, bool centered = false) {
        if (!font.bytes.empty()) { font.draw(dc, client, rect, value, color, centered); return; }
        rect = runtimeMatchScoreScaledRect(client, rect);
        const int saved = SaveDC(dc);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
        DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (centered ? DT_CENTER : DT_LEFT));
        RestoreDC(dc, saved);
    };
    // Native 800x600 locations correspond to the supplied 640x480 retail page.
    text(loading.unitName, RECT{20, 87, 260, 119}, RGB(0, 0, 0));
    std::wstring line;
    // Yuri Prime's taller illustration extends below the other unit portraits.
    int y = runtimeCountryIsYuri(loading.runtime->config.playerCountry) ? 196 : 153;
    for (const auto c : loading.countryBrief) {
        const auto candidate = line + c;
        if (c == L'\n' || (!line.empty() && (font.bytes.empty() ? candidate.size() * 16 : font.measure(candidate)) > 410)) {
            text(line, RECT{20, y, 433, y + 24}, loading.textColor);
            line.clear(); y += 24;
        }
        if (y > 249) break;
        if (c != L'\n' && c != L'\r') line += c;
    }
    if (!line.empty()) text(line, RECT{20, y, 433, y + 24}, loading.textColor);
    text(loading.countryName, RECT{661, 309, 754, 336}, loading.textColor, true);
    text(loading.loadingLabel, RECT{20, 298, 430, 323}, loading.textColor);
    const auto fill = [&](RECT base, COLORREF color, bool outline = false) {
        const RECT rect = runtimeMatchScoreScaledRect(client, base);
        HBRUSH brush = CreateSolidBrush(color);
        if (outline) FrameRect(dc, &rect, brush); else FillRect(dc, &rect, brush);
        DeleteObject(brush);
    };
    fill(RECT{20, 329, 107, 341}, loading.playerColor, true);
    fill(RECT{22, 331, 105, 339}, RGB(0, 0, 0));
    const int progress = 83 * std::clamp(loading.completed, 0, std::max(1, loading.total)) / std::max(1, loading.total);
    fill(RECT{22, 332, 22 + progress, 338}, loading.playerColor);
    if (loading.flagLoaded) drawRuntimeIndexedImage(dc, runtimeMatchScoreScaledRect(client, RECT{118, 326, 164, 349}),
        loading.flag, loading.flagPalette, true, true, true, nullptr, 0, 0, false,
        runtimeIndexedOpaqueAlpha, loading.flagPcx, loading.flagPcx);
    text(loading.playerName, RECT{172, 324, 432, 349}, loading.playerColor);

    const auto& preview = loading.mapPreview;
    const RECT previewFrame{500, 380, 716, 546};
    if (preview.width > 0 && preview.height > 0 && !preview.pixels.empty()) {
        const double scale = std::min(216.0 / preview.width, 166.0 / preview.height);
        const int width = std::max(1, static_cast<int>(std::lround(preview.width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(preview.height * scale)));
        const RECT base{500 + (216 - width) / 2, 380 + (166 - height) / 2,
            500 + (216 - width) / 2 + width, 380 + (166 - height) / 2 + height};
        const RECT destination = runtimeMatchScoreScaledRect(client, base);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = preview.width; info.bmiHeader.biHeight = -preview.height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, destination.left, destination.top, destination.right - destination.left, destination.bottom - destination.top,
            0, 0, preview.width, preview.height, preview.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, destination.left, destination.top, destination.right, destination.bottom);
        for (const auto& marker : loading.mapStarts) {
            const int x = base.left + static_cast<int>(std::lround(marker.x * (width - 1)));
            const int markerY = base.top + static_cast<int>(std::lround(marker.y * (height - 1)));
            fill(RECT{x - 4, markerY - 4, x + 5, markerY + 5}, RGB(0, 0, 0));
            fill(RECT{x - 3, markerY - 3, x + 4, markerY + 4}, marker.color, true);
        }
        RestoreDC(dc, saved);
    } else {
        text(runtimeMenuLabel(*loading.runtime, "GUI:NoPreview", L"无地图预览"), previewFrame, loading.textColor, true);
    }
}

bool presentRuntimeCountryLoading(
    HDC dc,
    const RECT& client,
    RuntimeLoadingWindowState& loading
) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (!ensureRuntimeBackBuffer(loading.backBuffer, dc, width, height)) {
        return false;
    }
    // Compose the complete page away from the window. In particular, the
    // black clear and individual layers must never be presented separately.
    const RECT bufferClient{0, 0, width, height};
    drawRuntimeCountryLoading(loading.backBuffer.memoryDc, bufferClient, loading);
    return BitBlt(dc, client.left, client.top, width, height,
        loading.backBuffer.memoryDc, 0, 0, SRCCOPY) != FALSE;
}
