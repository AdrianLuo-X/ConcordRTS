// Compare the complete deferred high-bridge draw with original MIX SHP pixels.
// The oracle reads the SHP header/crop and index-zero mask, independently of
// the bridge TMP adapter and its visibility bounds. No bridgeheads or terrain
// extensions are present in this flat fixture.
bool runRuntimeBridgeConnectivityContract(const RuntimeMapRenderContext& assets, std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "Bridge connectivity: " + why;
        return false;
    };
    const std::string root = assets.assetRootPath;
    IniFile rulesIni, artIni, theaterIni;
    RulesDatabase rules;
    ArtDatabase art;
    TheaterDatabase theater;
    const std::string theaterPath = root + "/ra2md/localmd/temperatmd.ini";
    if (!rulesIni.loadFromFile(root + "/ra2md/localmd/rulesmd.ini")
        || !artIni.loadFromFile(root + "/ra2md/localmd/artmd.ini")
        || !theaterIni.loadFromFile(theaterPath)
        || !rules.loadFromIni(rulesIni) || !art.loadFromIni(artIni)
        || !theater.loadFromIni(theaterIni, theaterPath)) return fail("original rules/art/theater");

    constexpr int width = 320, height = 240, tileWidth = 60, tileHeight = 30;
    const RECT viewport{0, 0, width, height};
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("DIB creation");
    }
    const HGDIOBJ previous = SelectObject(dc, bitmap);
    struct Cleanup {
        HDC dc; HBITMAP bitmap; HGDIOBJ previous;
        ~Cleanup() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, previous};
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    const auto snapshot = [&]() {
        GdiFlush();
        std::vector<std::uint32_t> result(pixels, pixels + width * height);
        for (auto& pixel : result) pixel &= 0xffffffU;
        return result;
    };
    RuntimeMapRenderContext context;
    context.assetRootPath = root;
    context.rules = &rules; context.art = &art; context.theater = &theater;
    context.palettePath = root + "/palettes/isotem.pal";
    context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
    RuntimeMapRenderCache oracleCache;
    const auto* palette = runtimeOverlayPaletteForRender(context, oracleCache);
    if (!palette || palette->size() < 256) return fail("original overlay palette");
    std::array<std::uint32_t, 256> packedPalette{};
    for (std::size_t i = 0; i < packedPalette.size(); ++i)
        packedPalette[i] = packDibColor((*palette)[i]) & 0xffffffU;

    GameWorld bareWorld;
    MapDatabase emptyMap;
    if (!bareWorld.loadFromMap(emptyMap)) return fail("fixture world");
    for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = 0; cell.subTileIndex = 0; cell.level = 0;
        bareWorld.accessTerrainCells().push_back(cell);
    }
    constexpr std::size_t targetIndex = 9 * 20 + 9; // Native cell (10,10), world projection (0,300).
    const char* output = std::getenv("CONCORD_BRIDGE_CONNECTIVITY_OUTPUT");
    const std::filesystem::path outputRoot = output && *output ? output : "build/issue22_03_bridge_connectivity";
    struct NativeBridgeCase { const char* type; const char* image; int frame; };
    int frames = 0, poses = 0, draws = 0;
    std::uint64_t checkedOpaquePixels = 0, checkedTransparentPixels = 0;
    for (const auto& bridgeCase : {NativeBridgeCase{"BRIDGE1", "BRIDGE", 0},
            NativeBridgeCase{"BRIDGE2", "BRIDGE", 9},
            NativeBridgeCase{"BRIDGEB1", "BRIDGB", 0},
            NativeBridgeCase{"BRIDGEB2", "BRIDGB", 9}}) {
        const std::string stem = std::string(bridgeCase.image) + "-" + std::to_string(bridgeCase.frame);
        const auto& overlays = rules.getOverlayTypes();
        const auto nativeType = std::find_if(overlays.begin(), overlays.end(), [&](const OverlayType& type) {
            return type.id == bridgeCase.type;
        });
        if (nativeType == overlays.end() || nativeType->image != bridgeCase.image || nativeType->listIndex < 0)
            return fail(stem + " original OverlayTypes identity/image unavailable");
        std::vector<unsigned char> bytes;
        std::string source, why;
        if (!loadRuntimeResourceBytesFromMixArchives(root, toLowerAscii(bridgeCase.image) + ".tem",
                oracleCache, bytes, source, why)) return fail(stem + " original MIX SHP: " + why);
        RuntimeShpFrameImage original;
        if (!decodeRuntimeShpFrameFromBytes(bytes, source, bridgeCase.frame, original, why))
            return fail(stem + " original SHP decode: " + why);
        if (original.width <= 0 || original.height <= 0 || original.globalWidth <= 0 || original.globalHeight <= 0
            || original.pixels.size() != static_cast<std::size_t>(original.width) * original.height)
            return fail(stem + " invalid original frame dimensions");
        // Original high-bridge screen anchor: global SHP centre, crop offset,
        // then the original half-tile X/Y shift and one additional Y pixel.
        const int originX = -tileWidth / 2 - original.globalWidth / 2 + original.offsetX;
        const int originY = -tileHeight / 2 - 1 - original.globalHeight / 2 + original.offsetY;
        int left = width, top = height, right = -width, bottom = -height;
        std::size_t nativeOpaque = 0;
        for (int y = 0; y < original.height; ++y) for (int x = 0; x < original.width; ++x) {
            if (!original.pixels[static_cast<std::size_t>(y) * original.width + x]) continue;
            left = std::min(left, originX + x); right = std::max(right, originX + x);
            top = std::min(top, originY + y); bottom = std::max(bottom, originY + y);
            ++nativeOpaque;
        }
        if (!nativeOpaque || right - left + 1 > width || bottom - top + 1 > height
            || left >= -tileWidth - 1) return fail(stem + " native frame does not exercise high-bridge overhang");
        GameWorld bridgeWorld = bareWorld;
        auto& cell = bridgeWorld.accessTerrainCells()[targetIndex];
        cell.hasOverlay = true; cell.overlayIndex = nativeType->listIndex;
        cell.overlayType = nativeType->id; cell.overlayImage = nativeType->image;
        cell.overlayFrame = bridgeCase.frame;
        const int middleX = (width - (right - left + 1)) / 2 - left;
        const int middleY = (height - (bottom - top + 1)) / 2 - top;
        std::vector<std::pair<std::string, POINT>> positions{{"center", {middleX, middleY}}};
        // Every pixel of the native overhang beyond the old +/-one-tile cull.
        for (int x = width + tileWidth + 1; x <= width - left - 1; ++x)
            positions.push_back({"right+" + std::to_string(x - width), {x, middleY}});
        positions.push_back({"right-partial", {width - left - (right - left + 1) / 2, middleY}});
        positions.push_back({"right-outside", {width - left, middleY}});
        positions.push_back({"left-partial", {-right + (right - left + 1) / 2, middleY}});
        positions.push_back({"left-last", {-right, middleY}});
        positions.push_back({"left-outside", {-right - 1, middleY}});
        positions.push_back({"top-partial", {middleX, -bottom + (bottom - top + 1) / 2}});
        positions.push_back({"top-last", {middleX, -bottom}});
        positions.push_back({"top-outside", {middleX, -bottom - 1}});
        positions.push_back({"bottom-partial", {middleX, height - top - (bottom - top + 1) / 2}});
        positions.push_back({"bottom-last", {middleX, height - top - 1}});
        positions.push_back({"bottom-outside", {middleX, height - top}});
        RuntimeMapRenderCache pannedCache, bareCache;
        for (const auto& position : positions) {
            RuntimeMapCamera camera;
            camera.initialized = true; camera.tileWidth = tileWidth; camera.tileHeight = tileHeight;
            camera.worldOffsetX = -position.second.x; camera.worldOffsetY = 300 - position.second.y;
            std::fill(pixels, pixels + width * height, 0x203040U);
            const auto bareStats = drawRuntimeMap(dc, viewport, bareWorld, context, camera, bareCache);
            if (!bareStats.rendered || !bareStats.texturedTerrainCells)
                return fail(stem + "/" + position.first + " bare original terrain not drawn");
            const auto bare = snapshot();
            auto expected = bare;
            std::vector<unsigned char> mask(width * height, 0);
            std::size_t visibleOpaque = 0;
            for (int y = 0; y < original.height; ++y) for (int x = 0; x < original.width; ++x) {
                const auto index = original.pixels[static_cast<std::size_t>(y) * original.width + x];
                if (!index) continue; // Raw index zero alone is transparent, including native black colors.
                const int px = position.second.x + originX + x, py = position.second.y + originY + y;
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                const auto target = static_cast<std::size_t>(py) * width + px;
                expected[target] = packedPalette[index]; mask[target] = 1;
                ++visibleOpaque;
            }
            const bool outside = position.first.find("outside") != std::string::npos;
            if ((outside && visibleOpaque) || (!outside && !visibleOpaque))
                return fail(stem + "/" + position.first + " invalid independent boundary fixture");
            RuntimeMapRenderCache coldCache;
            for (const char* phase : {"cold", "warm", "panned"}) {
                RuntimeMapRenderCache& cache = std::string(phase) == "panned" ? pannedCache : coldCache;
                const auto hitsBefore = cache.terrainLayerCacheHits;
                std::fill(pixels, pixels + width * height, 0x203040U);
                const auto stats = drawRuntimeMap(dc, viewport, bridgeWorld, context, camera, cache);
                const auto actual = snapshot();
                ++draws;
                checkedOpaquePixels += visibleOpaque;
                checkedTransparentPixels += width * height - visibleOpaque;
                const std::string label = stem + "/" + position.first + "/" + phase;
                if (!stats.rendered || (std::string(phase) == "warm" && cache.terrainLayerCacheHits <= hitsBefore))
                    return fail(label + " complete map/warm terrain cache not exercised");
                if (actual != expected || (visibleOpaque && (stats.bridgeOverlayDeferredDrawn != 1 || stats.bridgeOverlayMissing))) {
                    std::size_t different = 0, omitted = 0, alteredTransparent = 0, first = actual.size();
                    for (std::size_t i = 0; i < actual.size(); ++i) if (actual[i] != expected[i]) {
                        ++different; if (first == actual.size()) first = i;
                        if (mask[i] && actual[i] == bare[i]) ++omitted;
                        if (!mask[i]) ++alteredTransparent;
                    }
                    const std::string fileStem = stem + "-" + position.first + "-" + phase;
                    writeRuntimeBgraBmp(outputRoot / (fileStem + "-actual.bmp"), width, height, actual);
                    writeRuntimeBgraBmp(outputRoot / (fileStem + "-expected.bmp"), width, height, expected);
                    std::vector<std::uint32_t> difference = actual;
                    for (std::size_t i = 0; i < difference.size(); ++i) if (actual[i] != expected[i]) difference[i] = 0xff00ffU;
                    writeRuntimeBgraBmp(outputRoot / (fileStem + "-difference.bmp"), width, height, difference);
                    return fail(label + " full deferred draw differs from original SHP; center=("
                        + std::to_string(position.second.x) + "," + std::to_string(position.second.y)
                        + ") rawOpaque=[" + std::to_string(left) + "," + std::to_string(top) + ".."
                        + std::to_string(right) + "," + std::to_string(bottom) + "] visible=" + std::to_string(visibleOpaque)
                        + " mismatches=" + std::to_string(different) + " omitted=" + std::to_string(omitted)
                        + " transparentChanged=" + std::to_string(alteredTransparent)
                        + " first=(" + std::to_string(first % width) + "," + std::to_string(first / width)
                        + ") deferred=" + std::to_string(stats.bridgeOverlayDeferredDrawn)
                        + " missing=" + std::to_string(stats.bridgeOverlayMissing));
                }
                if (std::string(phase) == "panned"
                    && (position.first == "center" || position.first == "right+61")) {
                    const std::string fileStem = stem + "-" + position.first;
                    writeRuntimeBgraBmp(outputRoot / (fileStem + "-actual.bmp"), width, height, actual);
                    writeRuntimeBgraBmp(outputRoot / (fileStem + "-expected.bmp"), width, height, expected);
                }
            }
            ++poses;
        }
        ++frames;
        std::cout << "Native bridge connectivity PASS " << stem << " poses=" << positions.size()
            << " rawOpaque=" << nativeOpaque << " bounds=[" << left << "," << top << ".." << right << "," << bottom
            << "] cold/warm/panned/fullMap=exact\n";
    }
    std::cout << "Native bridge connectivity: frames=" << frames << " poses=" << poses << " draws=" << draws
        << " opaquePixels=" << checkedOpaquePixels << " transparentPixels=" << checkedTransparentPixels
        << " originalMIX/SHP/clip/deferred=exact\n";
    return true;
}
