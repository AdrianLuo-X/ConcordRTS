// Read the original SHP frame table independently of the bridge TMP adapter.
// Side-lane records are intentionally empty; only their centre record paints
// the three-cell-wide bridge sprite. Both rendering paths must preserve that.
bool runRuntimeLowBridgeFrameContract(const RuntimeMapRenderContext& assets, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "Low bridge frames: " + message;
        return false;
    };
    const std::string root = assets.assetRootPath;
    IniFile rulesIni, artIni;
    RulesDatabase rules;
    ArtDatabase art;
    if (!rulesIni.loadFromFile(root + "/ra2md/localmd/rulesmd.ini")
        || !artIni.loadFromFile(root + "/ra2md/localmd/artmd.ini")
        || !rules.loadFromIni(rulesIni) || !art.loadFromIni(artIni)) return fail("original rules/art");

    constexpr int width = 320, height = 240;
    const RECT viewport{0, 0, width, height};
    const POINT center{width / 2, height / 2};
    constexpr std::uint32_t background = 0x203040U;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("DIB creation");
    }
    const auto oldBitmap = SelectObject(dc, bitmap);
    struct Cleanup {
        HDC dc; HBITMAP bitmap; HGDIOBJ previous;
        ~Cleanup() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, oldBitmap};
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    const auto snapshot = [&]() {
        GdiFlush();
        std::vector<std::uint32_t> result(pixels, pixels + width * height);
        for (auto& pixel : result) pixel &= 0xffffffU;
        return result;
    };
    const char* output = std::getenv("CONCORD_NATIVE_SHROUD_OUTPUT");
    const std::filesystem::path outputRoot = output && *output ? output : "build/native-shroud-smoke";
    int frames = 0, emptyFrames = 0, fullMapCases = 0;

    for (const auto& theaterCase : {std::pair{"temperatmd", "tem"}, std::pair{"snowmd", "sno"},
            std::pair{"urbanmd", "urb"}, std::pair{"urbannmd", "ubn"},
            std::pair{"desertmd", "des"}, std::pair{"lunarmd", "lun"}}) {
        IniFile theaterIni;
        TheaterDatabase theater;
        const auto theaterPath = root + "/ra2md/localmd/" + theaterCase.first + ".ini";
        if (!theaterIni.loadFromFile(theaterPath) || !theater.loadFromIni(theaterIni, theaterPath))
            return fail(theaterPath);
        RuntimeMapRenderContext context;
        context.assetRootPath = root;
        context.rules = &rules; context.art = &art; context.theater = &theater;
        context.palettePath = root + "/palettes/iso" + theaterCase.second + ".pal";
        context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
        RuntimeMapCamera camera;
        camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
        camera.worldOffsetX = -width / 2; camera.worldOffsetY = 300 - height / 2;
        RuntimeMapRenderCache cache;
        if (!ensureRuntimePalette(context, cache)) return fail("terrain palette");
        const auto* overlayPalette = runtimeOverlayPaletteForRender(context, cache);
        if (!overlayPalette) return fail("overlay palette");
        std::array<std::uint32_t, 256> packedPalette{};
        for (std::size_t i = 0; i < packedPalette.size(); ++i) packedPalette[i] = packDibColor(cache.palette[i]);

        GameWorld world;
        MapDatabase emptyMap;
        if (!world.loadFromMap(emptyMap)) return fail("fixture world");
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x; cell.tileIndex = 0;
            world.accessTerrainCells().push_back(cell);
        }
        auto& target = world.accessTerrainCells()[9 * 20 + 9];
        RuntimeMapRenderCache plainCache;
        const auto plainStats = drawRuntimeMap(dc, viewport, world, context, camera, plainCache);
        if (!plainStats.rendered || !plainStats.texturedTerrainCells) return fail("bare map not drawn");
        const auto bareTerrain = snapshot();

        for (const char* prefix : {"LOBRDG", "LOBRDB"}) for (int number = 1; number <= 26; ++number) {
            const std::string id = std::string(prefix) + (number < 10 ? "0" : "") + std::to_string(number);
            const std::string filename = toLowerAscii(id) + "." + theaterCase.second;
            std::vector<unsigned char> bytes;
            std::string source, why;
            if (!loadRuntimeResourceBytesFromMixArchives(root, filename, cache, bytes, source, why))
                return fail(filename + " missing original archive");
            for (int frame : {0, 1, 2}) {
                const std::string label = std::string(theaterCase.first) + "-" + id + "-" + std::to_string(frame);
                RuntimeShpFrameImage original;
                if (!decodeRuntimeShpFrameFromBytes(bytes, source, frame, original, why)) return fail(label + ": " + why);
                const auto reference = [&](std::vector<std::uint32_t> base, const std::vector<RuntimePaletteColor>& palette) {
                    for (int y = 0; y < original.height; ++y) for (int x = 0; x < original.width; ++x) {
                        const auto index = original.pixels[static_cast<std::size_t>(y) * original.width + x];
                        if (!index) continue;
                        const int px = center.x - original.globalWidth / 2 + original.offsetX + x;
                        const int py = center.y - original.globalHeight / 2 + original.offsetY + y;
                        if (px >= 0 && py >= 0 && px < width && py < height)
                            base[static_cast<std::size_t>(py) * width + px] = packDibColor(palette[index]) & 0xffffffU;
                    }
                    return base;
                };
                TerrainCell cell = target;
                cell.hasOverlay = true; cell.overlayType = cell.overlayImage = id; cell.overlayFrame = frame;
                const auto expected = reference(std::vector<std::uint32_t>(width * height, background), *overlayPalette);
                for (int warm = 0; warm < 2; ++warm) {
                    std::fill(pixels, pixels + width * height, background);
                    if (!drawRuntimeBridgeOverlaySprite(dc, viewport, center, cell, nullptr, context, camera, cache)
                        || snapshot() != expected) return fail(label + " direct draw does not match original frame");
                    RuntimeTerrainLayerCache layer;
                    layer.width = width; layer.height = height;
                    layer.pixels.assign(width * height, background);
                    layer.terrainCellIndices.assign(width * height, -1);
                    layer.surfaceCellIndices.assign(width * height, -1);
                    const bool layerDrawn = drawRuntimeLowBridgeTmpSpriteToLayer(layer, center, context, cache, packedPalette, id, frame, cell.cellIndex512);
                    const auto expectedLayer = reference(std::vector<std::uint32_t>(width * height, background), cache.palette);
                    if (!layerDrawn || layer.pixels != expectedLayer) {
                        writeRuntimeBgraBmp(outputRoot / ("low-bridge-" + label + "-layer.bmp"), width, height, layer.pixels);
                        writeRuntimeBgraBmp(outputRoot / ("low-bridge-" + label + "-reference.bmp"), width, height, expectedLayer);
                        return fail(label + " cached terrain draw does not match original frame, drawn=" + std::to_string(layerDrawn));
                    }
                    if (original.pixels.empty()
                        && std::any_of(layer.surfaceCellIndices.begin(), layer.surfaceCellIndices.end(), [](int value) { return value >= 0; }))
                        return fail(label + " empty frame claims visible surface pixels");
                }
                if (number == 1 || number == 26) {
                    target = cell;
                    RuntimeMapRenderCache fullCache;
                    const auto expectedMap = reference(bareTerrain, cache.palette);
                    for (int warm = 0; warm < 2; ++warm) {
                        const auto stats = drawRuntimeMap(dc, viewport, world, context, camera, fullCache);
                        const auto actual = snapshot();
                        if (stats.visibleOverlayCells != 1 || actual != expectedMap) {
                            writeRuntimeBgraBmp(outputRoot / ("low-bridge-" + label + "-actual.bmp"), width, height, actual);
                            writeRuntimeBgraBmp(outputRoot / ("low-bridge-" + label + "-expected.bmp"), width, height, expectedMap);
                            return fail(label + " full map substitutes another frame or image");
                        }
                    }
                    target.hasOverlay = false; target.overlayType.clear(); target.overlayImage.clear();
                    ++fullMapCases;
                }
                ++frames;
                if (original.pixels.empty()) ++emptyFrames;
            }
        }
    }
    std::cout << "Native low bridge frames: theaters=6 frames=" << frames << " empty=" << emptyFrames
        << " fullMap=" << fullMapCases << " direct/cached/warm/originalSHP=exact\n";
    return true;
}
