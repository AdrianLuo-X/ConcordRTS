// This is an independent software oracle for the inspected retail TMP leaves,
// not an execution of gamemd.exe. Original instruction addresses and executable
// SHA are recorded alongside the pre-edit backups. It intentionally uses the
// original top/height operands, rather than runtimeTmpNativeDepthWord.
namespace RuntimeTerrainNativeDepthContractDetail {
struct Report {
    std::uint64_t cases = 0, checkedPixels = 0, legacyWrongPixels = 0;
    std::uint64_t stockNativeDraws = 0, stockExtraDraws = 0;
    std::ostringstream rows;
    std::vector<std::uint32_t> witnessOld, witnessExpected, witnessActual;
};
static RuntimeTerrainLayerCache emptyLayer(int width, int height) {
    RuntimeTerrainLayerCache layer;
    layer.width = width; layer.height = height;
    const auto count = static_cast<std::size_t>(width) * height;
    layer.pixels.assign(count, 0x000F161FU);
    layer.terrainDepthWords.assign(count, 0xffffU);
    layer.terrainCellIndices.assign(count, -1);
    layer.surfaceCellIndices.assign(count, -1);
    return layer;
}
static bool nativeEligible(const RuntimeTmpSubTileImage& image,
    const RuntimeMapCamera& camera) {
    const auto extraCount = static_cast<std::size_t>(std::max(0, image.extraWidth))
        * std::max(0, image.extraHeight);
    return camera.tileWidth == 60 && camera.tileHeight == 30
        && image.baseWidth == 60 && image.baseHeight == 30
        && image.hasBaseZData && image.basePixels.size() >= 900
        && image.baseZData.size() >= 900
        && (!image.hasExtra || (extraCount > 0 && image.hasExtraZData
            && image.extraPixels.size() >= extraCount && image.extraZData.size() >= extraCount));
}
static std::uint16_t retailWord(const TerrainCell& cell, unsigned char byte) {
    const int diamondTop = (cell.x + cell.y) * 15 - cell.level * 15 - 15;
    const auto truncated = static_cast<std::uint16_t>(0x8000 - diamondTop - 30);
    return static_cast<std::uint16_t>(static_cast<int>(truncated) - 30 * cell.level / 2 + byte);
}
static void oraclePixel(RuntimeTerrainLayerCache& layer, int x, int y,
    unsigned char color, unsigned char depth, const TerrainCell& cell,
    const std::array<std::uint32_t, 256>& palette, bool native) {
    if (!color || x < 0 || y < 0 || x >= layer.width || y >= layer.height) return;
    const auto offset = static_cast<std::size_t>(y) * layer.width + x;
    const auto word = native ? retailWord(cell, depth) : std::uint16_t(0xffffU);
    if (native && layer.terrainDepthWords[offset] < word) return;
    layer.pixels[offset] = palette[color];
    layer.terrainDepthWords[offset] = word;
    layer.terrainCellIndices[offset] = cell.cellIndex512;
    layer.surfaceCellIndices[offset] = cell.cellIndex512;
}
static void oracleDraw(RuntimeTerrainLayerCache& layer, const POINT& center,
    const TerrainCell& cell, const RuntimeMapCamera& camera,
    const RuntimeTmpSubTileImage& image, const std::array<std::uint32_t, 256>& palette) {
    const int left = center.x - camera.tileWidth / 2;
    const int top = center.y - camera.tileHeight / 2;
    if (!nativeEligible(image, camera)) {
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
            oraclePixel(layer, left + image.offsetX + x, top + image.offsetY + y,
                image.pixels[static_cast<std::size_t>(y) * image.width + x], 0, cell, palette, false);
        return;
    }
    std::size_t source = 0;
    for (int row = 0; row < 30; ++row) {
        const int width = 4 * (row < 15 ? row + 1 : 29 - row);
        const int leftPadding = 30 - width / 2;
        for (int column = 0; column < width; ++column, ++source)
            oraclePixel(layer, left + image.baseOffsetX + leftPadding + column,
                top + image.baseOffsetY + row, image.basePixels[source], image.baseZData[source],
                cell, palette, true);
    }
    if (image.hasExtra) for (int row = 0; row < image.extraHeight; ++row)
        for (int column = 0; column < image.extraWidth; ++column) {
            const auto offset = static_cast<std::size_t>(row) * image.extraWidth + column;
            oraclePixel(layer, left + image.extraOffsetX - image.baseOffsetX + column,
                top + image.extraOffsetY - image.baseOffsetY + row,
                image.extraPixels[offset], image.extraZData[offset], cell, palette, true);
        }
}
static RuntimeTmpSubTileImage fixture(unsigned char color, unsigned char baseZ,
    unsigned char extraZ, bool extra = true) {
    RuntimeTmpSubTileImage image;
    image.present = true; image.hasTerrainType = true;
    image.baseWidth = 60; image.baseHeight = 30;
    image.basePixels.assign(900, color); image.baseZData.assign(900, baseZ);
    image.hasBaseZData = true; image.hasExtra = extra;
    image.extraWidth = extra ? 16 : 0; image.extraHeight = extra ? 40 : 0;
    image.extraOffsetX = 22; image.extraOffsetY = -10;
    image.extraPixels.assign(extra ? 640 : 0, color);
    image.extraZData.assign(extra ? 640 : 0, extraZ); image.hasExtraZData = extra;
    image.offsetY = extra ? -10 : 0; image.width = 60; image.height = extra ? 40 : 30;
    image.pixels.assign(static_cast<std::size_t>(image.width) * image.height, 0);
    for (std::size_t i = 0; i < image.basePixels.size(); ++i)
        if (i % 11 == 0) image.basePixels[i] = 0;
    for (std::size_t i = 0; i < image.extraPixels.size(); ++i)
        if (i % 13 == 0) image.extraPixels[i] = 0;
    std::size_t source = 0;
    for (int row = 0; row < 30; ++row) {
        const int width = 4 * (row < 15 ? row + 1 : 29 - row);
        for (int column = 0; column < width; ++column, ++source)
            image.pixels[static_cast<std::size_t>(row - image.offsetY) * 60 + 30 - width / 2 + column]
                = image.basePixels[source];
    }
    if (extra) for (int row = 0; row < 40; ++row) for (int column = 0; column < 16; ++column) {
        const auto colorIndex = image.extraPixels[static_cast<std::size_t>(row) * 16 + column];
        if (colorIndex) image.pixels[static_cast<std::size_t>(row) * 60 + 22 + column] = colorIndex;
    }
    return image;
}
static bool compare(const char* label, const RuntimeTerrainLayerCache& expected,
    const RuntimeTerrainLayerCache& actual, std::string& error, Report& report) {
    if (expected.pixels != actual.pixels || expected.terrainCellIndices != actual.terrainCellIndices
        || expected.surfaceCellIndices != actual.surfaceCellIndices
        || expected.terrainDepthWords != actual.terrainDepthWords) {
        for (std::size_t i = 0; i < expected.pixels.size(); ++i)
            if (expected.pixels[i] != actual.pixels[i]
                || expected.terrainCellIndices[i] != actual.terrainCellIndices[i]
                || expected.surfaceCellIndices[i] != actual.surfaceCellIndices[i]
                || expected.terrainDepthWords[i] != actual.terrainDepthWords[i]) {
                error = std::string(label) + " pixel=" + std::to_string(i)
                    + " color=" + std::to_string(expected.pixels[i]) + "/" + std::to_string(actual.pixels[i])
                    + " z=" + std::to_string(expected.terrainDepthWords[i]) + "/" + std::to_string(actual.terrainDepthWords[i]);
                return false;
            }
    }
    ++report.cases; report.checkedPixels += expected.pixels.size();
    return true;
}
static bool primitives(std::string& error, Report& report) {
    std::array<std::uint32_t, 256> palette{};
    palette[1] = 0x00DE593BU; palette[2] = 0x004CBD7CU; palette[3] = 0;
    const auto nearImage = fixture(1, 12, 5), farImage = fixture(2, 220, 240);
    auto noZ = fixture(2, 8, 8); noZ.hasBaseZData = false; noZ.hasExtraZData = false;
    auto partial = fixture(2, 8, 8); partial.hasExtraZData = false;
    auto transparent = noZ;
    std::fill(transparent.pixels.begin(), transparent.pixels.end(), 0);
    auto blackEqual = fixture(3, 12, 5);
    auto overlap = fixture(2, 40, 230);
    struct Scene { const char* name; std::vector<RuntimeTmpSubTileImage> images; POINT center; int level; int tileWidth; };
    const std::vector<Scene> scenes{
        {"native_near_then_far", {nearImage, farImage}, {80, 65}, 0, 60},
        {"native_far_then_near", {farImage, nearImage}, {80, 65}, 0, 60},
        {"equal_depth_opaque_black", {nearImage, blackEqual}, {80, 65}, 0, 60},
        {"unsigned_high_byte", {fixture(1, 130, 135), farImage}, {80, 65}, 0, 60},
        {"same_tile_base_extra_overlap", {overlap}, {80, 65}, 0, 60},
        {"native_noz_native", {nearImage, noZ, farImage}, {80, 65}, 0, 60},
        {"native_partial_extra_z_native", {nearImage, partial, farImage}, {80, 65}, 0, 60},
        {"transparent_noz_preserves_z", {nearImage, transparent, farImage}, {80, 65}, 0, 60},
        {"top_left_clipping", {nearImage, farImage}, {-5, 10}, 7, 60},
        {"bottom_right_clipping", {nearImage, farImage}, {158, 111}, 14, 60},
        {"level_seed_cancellation", {nearImage, farImage}, {80, 65}, 14, 60},
        {"nonnative_grid_preserves_painter", {nearImage, farImage}, {80, 65}, 0, 120}
    };
    for (const auto& scene : scenes) {
        auto expected = emptyLayer(160, 120), actual = expected, legacy = expected;
        RuntimeMapCamera camera; camera.tileWidth = scene.tileWidth; camera.tileHeight = scene.tileWidth / 2;
        int index = 0;
        for (const auto& image : scene.images) {
            TerrainCell cell; cell.x = 10 + index; cell.y = 10 - index;
            cell.level = scene.level; cell.cellIndex512 = cell.y * 512 + cell.x;
            oracleDraw(expected, scene.center, cell, camera, image, palette);
            if (!drawTmpTileImageZBufferedToLayer(actual, actual.terrainDepthWords,
                    scene.center, cell, camera, image, palette, cell.cellIndex512)) {
                error = std::string(scene.name) + " drawing refused"; return false;
            }
            drawTmpTileImageToLayer(legacy, scene.center, camera, image, palette, cell.cellIndex512);
            ++index;
        }
        if (!compare(scene.name, expected, actual, error, report)) return false;
        std::uint64_t oldWrong = 0;
        for (std::size_t i = 0; i < expected.pixels.size(); ++i)
            oldWrong += expected.pixels[i] != legacy.pixels[i]
                || expected.terrainCellIndices[i] != legacy.terrainCellIndices[i]
                || expected.surfaceCellIndices[i] != legacy.surfaceCellIndices[i];
        report.legacyWrongPixels += oldWrong;
        report.rows << scene.name << ',' << expected.pixels.size() << ',' << oldWrong << ",0\n";
        if (std::string(scene.name) == "native_near_then_far") {
            if (!oldWrong) { error = "legacy painter failure witness absent"; return false; }
            report.witnessOld = legacy.pixels; report.witnessExpected = expected.pixels;
            report.witnessActual = actual.pixels;
        }
    }
    // Fallback diamonds and dedicated bridge painters intentionally have no
    // TMP candidate: their opaque writes must invalidate prior tile depth.
    auto layer = emptyLayer(160, 120); TerrainCell cell;
    cell.x = cell.y = 10; cell.cellIndex512 = 5130;
    RuntimeMapCamera camera; const POINT center{80, 65};
    drawTmpTileImageZBufferedToLayer(layer, layer.terrainDepthWords, center, cell, camera, nearImage, palette, 5130);
    drawDiamondToLayer(layer, center, 60, 30, RGB(1, 2, 3), 5131);
    const auto point = std::size_t(65) * 160 + 80;
    if (layer.terrainDepthWords[point] != 0xffffU || layer.terrainCellIndices[point] != 5131
        || layer.surfaceCellIndices[point] != 5131) { error = "diamond stale depth"; return false; }
    drawTmpTileImageZBufferedToLayer(layer, layer.terrainDepthWords, center, cell, camera, farImage, palette, 5132);
    if (layer.terrainCellIndices[point] != 5132 || layer.surfaceCellIndices[point] != 5132) {
        error = "native after diamond rejected by hidden tile"; return false;
    }
    putLayerPixel(layer, 80, 65, palette[1], -1, 5133);
    if (layer.terrainDepthWords[point] != 0xffffU || layer.terrainCellIndices[point] != 5132
        || layer.surfaceCellIndices[point] != 5133) { error = "bridge painter ownership/depth"; return false; }
    report.rows << "diamond_bridge_plain_overwrite,1,0,0\n"; ++report.cases;
    for (int x : {0, 1, 255, 511}) for (int y : {0, 1, 255, 511})
        for (int level : {0, 1, 7, 14}) for (int z : {0, 31, 32, 127, 128, 255}) {
            TerrainCell sample; sample.x = x; sample.y = y; sample.level = level;
            if (retailWord(sample, static_cast<unsigned char>(z))
                != runtimeTmpNativeDepthWord(x, y, static_cast<unsigned char>(z))) {
                error = "retail top/height seed mismatch"; return false;
            }
        }
    return true;
}
static bool stockProducer(const RuntimeMapRenderContext& source, std::string& error, Report& report) {
    if (!source.rules || !source.theater) { error = "stock rules/theater absent"; return false; }
    RuntimeMapRenderContext context = source;
    context.mapFacts = nullptr; context.useTileTextures = true;
    context.stableRenderConfiguration = true;
    RuntimeMapRenderCache cache;
    if (!ensureRuntimePalette(context, cache) || cache.palette.size() < 256) {
        error = "stock palette absent"; return false;
    }
    std::array<std::uint32_t, 256> palette{};
    for (int i = 0; i < 256; ++i) palette[i] = packDibColor(cache.palette[i]);
    RuntimeMapCamera camera; camera.initialized = true;
    int extraTile = -1, extraSubTile = -1;
    const int tileLimit = static_cast<int>(std::min<std::size_t>(512, source.theater->getTotalTileSlots()));
    for (int tile = 0; tile < tileLimit && extraTile < 0; ++tile) {
        auto* file = loadRuntimeTerrainTileForIndex(tile, context, cache);
        if (!file) continue;
        for (std::size_t sub = 0; sub < file->images.size(); ++sub)
            if (file->images[sub].present && file->images[sub].hasExtra
                && nativeEligible(file->images[sub], camera)) {
                extraTile = tile; extraSubTile = static_cast<int>(sub); break;
            }
    }
    if (extraTile < 0) { error = "stock native TMP extra not found in first 512 slots"; return false; }
    IniFile ini; MapDatabase map; GameWorld world;
    if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) {
        error = "stock producer fixture map"; return false;
    }
    world.accessTerrainCells().clear();
    for (int y = 4; y < 12; ++y) for (int x = 12; x < 20; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = ((x + y) % 3 == 0) ? extraTile : 0;
        cell.subTileIndex = cell.tileIndex == extraTile ? extraSubTile : 0;
        cell.level = (x + y) % 3; world.accessTerrainCells().push_back(cell);
    }
    std::string firstKey; std::uint64_t expectedHits = 0, expectedMisses = 0;
    for (int pass = 0; pass < 4; ++pass) {
        camera.worldOffsetX = pass < 2 ? 20 + pass * 20 : 380;
        camera.worldOffsetY = pass < 2 ? 20 + pass * 20 : 300;
        const int width = pass == 3 ? 540 : 360, height = pass == 3 ? 330 : 240;
        if (!buildTerrainLayer(world, context, camera, width, height, cache)) {
            error = "stock terrain producer refused"; return false;
        }
        if (pass == 1) ++expectedHits; else ++expectedMisses;
        if (cache.terrainLayerCacheHits != expectedHits || cache.terrainLayerCacheMisses != expectedMisses) {
            error = "terrain camera bucket/viewport cache lifecycle"; return false;
        }
        if (pass == 0) firstKey = cache.terrainLayer.key;
        if (pass > 0 && ((pass == 1) != (cache.terrainLayer.key == firstKey))) {
            error = "terrain movement cache key"; return false;
        }
        auto expected = emptyLayer(cache.terrainLayer.width, cache.terrainLayer.height);
        for (const auto& cell : world.getTerrainCells()) {
            bool empty = false;
            const auto* image = resolveTerrainVisualCellImage(cell, context, cache, &empty);
            if (empty) continue;
            if (!image) { error = "stock fixture image missing"; return false; }
            // Independent native projection; includes level visually, then the
            // original depth seed cancels that level exactly once.
            const POINT center{(cell.x - cell.y) * 30 - cache.terrainLayer.originX,
                (cell.x + cell.y - cell.level) * 15 - cache.terrainLayer.originY};
            oracleDraw(expected, center, cell, camera, *image, palette);
            report.stockNativeDraws += nativeEligible(*image, camera);
            report.stockExtraDraws += image->hasExtra && nativeEligible(*image, camera);
        }
        const std::string label = "stock_producer_pass_" + std::to_string(pass);
        if (!compare(label.c_str(), expected, cache.terrainLayer, error, report)) return false;
        report.rows << label << ',' << expected.pixels.size() << ",0,0\n";
    }
    return report.stockNativeDraws > 0 && report.stockExtraDraws > 0;
}
static bool writeReport(const Report& report, std::string& error) {
    const char* output = std::getenv("CONCORD_TERRAIN_NATIVE_DEPTH_OUTPUT");
    if (!output || !*output) return true;
    try {
        const std::filesystem::path root(output);
        std::filesystem::create_directories(root);
        std::ofstream csv(root / "terrain-native-depth.csv");
        csv << "case,checked_pixels,legacy_color_or_owner_mismatches,actual_mismatches\n" << report.rows.str();
        std::ofstream evidence(root / "terrain-native-depth-evidence.txt");
        evidence << "oracle=independent software model of statically inspected retail instructions; not retail execution\n"
            << "gamemd.exe sha256=7B8A068535D6AF06845EDF95AE829B113D00C02909330E16F197426CD7DB94B6\n"
            << "seed=0x547F63..0x547F97; unsigned-byte/unsigned-word compare/write=0x5487AB..0x5487B6\n"
            << "scope=tile-to-tile TMP base+extra; no object scene-depth seeding\n"
            << "unknown/missing Z and nonnative camera=opaque painter invalidates covered TMP depth\n"
            << "display zoom=viewport resize/native camera preserved; end-to-end shell zoom is a separate check\n"
            << "stockNativeDraws=" << report.stockNativeDraws << " stockExtraDraws=" << report.stockExtraDraws << '\n';
        if (!csv || !evidence || !writeRuntimeBgraBmp(root / "terrain-depth-legacy.bmp", 160, 120, report.witnessOld)
            || !writeRuntimeBgraBmp(root / "terrain-depth-oracle.bmp", 160, 120, report.witnessExpected)
            || !writeRuntimeBgraBmp(root / "terrain-depth-actual.bmp", 160, 120, report.witnessActual)) {
            error = "terrain depth evidence output failed"; return false;
        }
    } catch (const std::exception& e) { error = std::string("terrain depth output: ") + e.what(); return false; }
    return true;
}
} // namespace RuntimeTerrainNativeDepthContractDetail

bool runRuntimeTerrainNativeDepthContractCheck(const RuntimeMapRenderContext& context,
    std::string& errorMessage) {
    RuntimeRendererEnvironmentScope environment;
    using namespace RuntimeTerrainNativeDepthContractDetail;
    Report report;
    if (!primitives(errorMessage, report) || !stockProducer(context, errorMessage, report)
        || !writeReport(report, errorMessage)) return false;
    std::cout << "Terrain native depth contract passed: cases=" << report.cases
        << " checkedPixels=" << report.checkedPixels << " legacyWrongPixels=" << report.legacyWrongPixels
        << " stockNativeDraws=" << report.stockNativeDraws << " stockExtraDraws=" << report.stockExtraDraws
        << " color/terrainOwner/surfaceOwner/Z=matched retailEvidence=static-instructions\n";
    errorMessage.clear(); return true;
}