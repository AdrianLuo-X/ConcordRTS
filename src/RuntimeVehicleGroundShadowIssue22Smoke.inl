// Issue 22 finite renderer-TU diagnostic. No simulation, save, or resource edits.
// The expected receiver uses original TMP bytes and native VXL mask blitters;
// it never obtains its floor/centre from the terrain-pose or actual shadow helper.
#include <iterator>

namespace RuntimeVehicleGroundShadowIssue22 {
using Bytes = std::vector<unsigned char>;
constexpr int surfaceWidth = 640;
constexpr int surfaceHeight = 480;
constexpr std::uint32_t surfaceBackground = 0x000f161fU;

std::uint64_t bytesHash(const void* data, std::size_t size) {
    std::uint64_t value = 14695981039346656037ULL;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) { value ^= bytes[i]; value *= 1099511628211ULL; }
    return value;
}
std::uint64_t pixelHash(const std::vector<std::uint32_t>& pixels) {
    return bytesHash(pixels.data(), pixels.size() * sizeof(std::uint32_t));
}
std::size_t differences(const std::vector<std::uint32_t>& first,
    const std::vector<std::uint32_t>& second) {
    if (first.size() != second.size()) throw std::runtime_error("different surface sizes");
    std::size_t result = 0;
    for (std::size_t i = 0; i < first.size(); ++i) if (first[i] != second[i]) ++result;
    return result;
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
bool writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    if (std::filesystem::exists(path)) return false;
    { std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      if (!file) return false; }
    std::ifstream file(path, std::ios::binary);
    const Bytes reread{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return bool(file) && reread == bytes;
}
bool writeBitmap(const std::filesystem::path& path, int width, int height,
    const std::vector<std::uint32_t>& pixels) {
    if (std::filesystem::exists(path) || width <= 0 || height <= 0
        || pixels.size() != static_cast<std::size_t>(width) * height) return false;
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER imageHeader{};
    imageHeader.biSize = sizeof(imageHeader); imageHeader.biWidth = width;
    imageHeader.biHeight = -height; imageHeader.biPlanes = 1; imageHeader.biBitCount = 32;
    imageHeader.biCompression = BI_RGB;
    imageHeader.biSizeImage = static_cast<DWORD>(pixels.size() * sizeof(std::uint32_t));
    fileHeader.bfType = 0x4d42; fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(imageHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + imageHeader.biSizeImage;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&imageHeader), sizeof(imageHeader));
    file.write(reinterpret_cast<const char*>(pixels.data()), imageHeader.biSizeImage);
    file.close(); return !file.fail();
}
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* pixels = nullptr;
    Surface() {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = surfaceWidth; info.bmiHeader.biHeight = -surfaceHeight;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
            reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (dc && bitmap) previous = SelectObject(dc, bitmap);
    }
    ~Surface() {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    bool valid() const { return dc && bitmap && pixels && previous && previous != HGDI_ERROR; }
    void clear() { std::fill(pixels, pixels + surfaceWidth * surfaceHeight, surfaceBackground); }
    std::vector<std::uint32_t> capture() const {
        GdiFlush(); return {pixels, pixels + surfaceWidth * surfaceHeight};
    }
};
struct OriginalTile {
    int tileIndex = -1;
    int gridWidth = 0, gridHeight = 0;
    std::string fileName, source;
    Bytes bytes;
    struct SubTile { int height = 0, land = -1, ramp = -1; bool present = false; };
    std::vector<SubTile> subtiles;
};
std::uint32_t little(const Bytes& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("original TMP integer outside file");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8)
        | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
bool readTile(int tileIndex, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& assetCache, const std::filesystem::path& output,
    OriginalTile& result, std::string& error) {
    const auto reference = context.theater->resolveTileIndex(tileIndex, context.tileRootPath);
    if (!reference.validTileSet || reference.expectedFileName.empty()) {
        error = "original TMP reference absent"; return false;
    }
    result.tileIndex = tileIndex; result.fileName = reference.expectedFileName;
    if (!loadRuntimeResourceBytesByName({context.tileRootPath, context.assetRootPath},
        context.assetRootPath, result.fileName, assetCache, result.bytes, result.source, error)) return false;
    result.gridWidth = static_cast<int>(little(result.bytes, 0));
    result.gridHeight = static_cast<int>(little(result.bytes, 4));
    if (result.gridWidth <= 0 || result.gridHeight <= 0 || result.gridWidth > 16
        || result.gridHeight > 16 || result.bytes.size() < 16
            + static_cast<std::size_t>(result.gridWidth * result.gridHeight) * 4) {
        error = "invalid original TMP grid"; return false;
    }
    for (int i = 0; i < result.gridWidth * result.gridHeight; ++i) {
        OriginalTile::SubTile sub;
        const auto offset = little(result.bytes, 16 + static_cast<std::size_t>(i) * 4);
        if (offset) {
            if (static_cast<std::size_t>(offset) + 52 > result.bytes.size()) {
                error = "truncated original TMP record"; return false;
            }
            sub.present = true; sub.height = result.bytes[offset + 40];
            sub.land = result.bytes[offset + 41]; sub.ramp = result.bytes[offset + 42];
        }
        result.subtiles.push_back(sub);
    }
    if (!writeBytes(output / result.fileName, result.bytes)) {
        error = "original TMP export collision/write/readback"; return false;
    }
    return true;
}
struct Scene { const char* name; int ramp; bool stopped; };
GameWorld sceneWorld(const Scene& scene, int flatTile, int slopeTile,
    int strength, std::string& error) {
    IniFile ini; MapDatabase map; GameWorld world;
    if (!ini.loadFromString("[Basic]\nName=Issue22FiniteGround\n[Map]\nTheater=TEMPERATE\nSize=0,0,128,128\nLocalSize=54,54,29,29\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map)) {
        error = "isolated loaded map"; return world;
    }
    // Original tiles/subtiles, matching low/high banks; no authored pixels changed.
    for (int y = 54; y <= 82; ++y) for (int x = 54; x <= 82; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = flatTile; cell.subTileIndex = 0;
        if (scene.ramp) cell.level = scene.ramp == 1 ? int(x > 65) : int(x < 65);
        if (scene.ramp && x == 65 && y >= 64 && y <= 66) {
            cell.tileIndex = slopeTile; cell.subTileIndex = y - 64; cell.level = 0;
        }
        world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance tank; tank.instanceId = 1; tank.kind = WorldObjectKind::Unit;
    tank.type = "MTNK"; tank.owner = "Americans"; tank.alive = true; tank.health = strength;
    tank.x = tank.y = 65; tank.cell = 65 * 1000 + 65; tank.level = 0;
    tank.subCell = -1; tank.facing = 96; tank.turretFacing = 96; tank.mission = "Guard";
    tank.renderX = 65; tank.renderY = 65; tank.renderLevel = 0;
    if (scene.stopped) {
        // Native road-ramp1-MTNK-30 baseline tick317, not a newly simulated claim.
        tank.hasRenderPosition = true; tank.renderMoveActive = false;
        tank.renderX = 64.86666666666666; tank.renderY = 65; tank.renderLevel = 0;
    }
    world.accessObjects().push_back(tank);
    return world;
}
}

bool runRuntimeVehicleGroundShadowIssue22Smoke(const RuntimeMapRenderContext& sourceContext,
    std::string& error) {
    using namespace RuntimeVehicleGroundShadowIssue22;
    error.clear();
    try {
        const char* outputValue = std::getenv("CONCORD_VEHICLE_GROUND_SHADOW_ISSUE22_OUTPUT");
        const char* expectation = std::getenv("CONCORD_VEHICLE_GROUND_SHADOW_ISSUE22_EXPECT_CORRECT");
        if (!outputValue || !*outputValue || !expectation
            || (std::string(expectation) != "0" && std::string(expectation) != "1")) {
            error = "issue22 OUTPUT and exact EXPECT_CORRECT=0/1 required"; return false;
        }
        const bool expectCorrect = std::string(expectation) == "1";
        const std::filesystem::path output(outputValue);
        std::filesystem::create_directories(output / "native-original");
        if (std::filesystem::exists(output / "frames.csv")) {
            error = "issue22 output already contains frames.csv"; return false;
        }
        if (!sourceContext.theater || !sourceContext.rules || !sourceContext.art
            || sourceContext.palettePath.empty() || sourceContext.structurePalettePath.empty()
            || sourceContext.theater->getTileExtension() != ".tem") {
            error = "original TEM theater/rules/art required"; return false;
        }
        RuntimeMapRenderContext context = sourceContext;
        context.mapFacts = nullptr; context.projectileActors = nullptr; context.impactActors = nullptr;
        context.wrecks = nullptr; context.buildingCollapses = nullptr;
        context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr;
        context.localLightSnapshots = nullptr; context.specialMechanismSnapshots = nullptr;
        context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr;
        context.gateSnapshots = nullptr; context.vision = nullptr;
        context.lightingTint = nullptr; context.lightingProfile = nullptr;
        context.uiResources = nullptr; context.animationTick = 317; context.fixedTickHz = 30;
        context.detailStatsEnabled = true; context.shadowsEnabled = true;
        context.surfaceTopDownOwned = true;
        RuntimeMapRenderCache originalCache;
        OriginalTile flat, ramp1, ramp3;
        const int clearSet = context.theater->getGeneralTileSetIndex("ClearTile");
        const int pavedSet = context.theater->getGeneralTileSetIndex("PavedRoadSlopes");
        int clearIndex = -1, pavedIndex = -1;
        for (const auto& set : context.theater->getTileSets()) {
            if (set.index == clearSet) clearIndex = set.startTileIndex;
            if (set.index == pavedSet && set.tilesInSet >= 3) pavedIndex = set.startTileIndex;
        }
        if (clearIndex < 0 || pavedIndex < 0
            || !readTile(clearIndex, context, originalCache, output / "native-original", flat, error)
            || !readTile(pavedIndex, context, originalCache, output / "native-original", ramp1, error)
            || !readTile(pavedIndex + 2, context, originalCache, output / "native-original", ramp3, error)) return false;
        if (flat.subtiles.size() != 1 || !flat.subtiles[0].present || flat.subtiles[0].ramp != 0
            || flat.subtiles[0].height != 0 || ramp1.gridWidth != 1 || ramp1.gridHeight != 3
            || ramp3.gridWidth != 1 || ramp3.gridHeight != 3) {
            error = "original clear and paved-road TMP geometry differs"; return false;
        }
        for (int i = 0; i < 3; ++i) if (!ramp1.subtiles[i].present || !ramp3.subtiles[i].present
            || ramp1.subtiles[i].ramp != 1 || ramp3.subtiles[i].ramp != 3
            || ramp1.subtiles[i].land != 11 || ramp3.subtiles[i].land != 11
            || ramp1.subtiles[i].height != 0 || ramp3.subtiles[i].height != 0) {
            error = "original ramp1/ramp3 record metadata differs"; return false;
        }
        const auto* type = context.rules->findUnitType("vehicle", "MTNK");
        if (!type || type->strength <= 0) { error = "original MTNK absent"; return false; }
        Surface surface;
        if (!surface.valid()) { error = "owned topdown DIB unavailable"; return false; }
        context.surfacePixels = surface.pixels;
        context.surfaceWidth = surfaceWidth; context.surfaceHeight = surfaceHeight;
        const RECT viewport{0, 0, surfaceWidth, surfaceHeight};
        RuntimeMapCamera camera; camera.initialized = true;
        camera.tileWidth = 60; camera.tileHeight = 30;
        camera.worldOffsetX = -surfaceWidth / 2;
        camera.worldOffsetY = 65 * camera.tileHeight - surfaceHeight / 2;
        std::ofstream frames(output / "frames.csv"), cases(output / "cases.csv"), assets(output / "assets.csv");
        frames << "scene,phase,pixel_hash,terrain_hits,terrain_misses,voxel_bodies,voxel_turrets,shadow_pixels,generated_fallbacks,width,height,camera_x,camera_y\n";
        cases << "scene,ramp,tile,subtile,logical_x,logical_y,subcell,body_x,body_y,object_render_level,original_floor,receiver_delta,pixel_shift_y,native_center_x,native_center_y,expected_center_x,expected_center_y,warm_mismatches,shadow_mismatches,body_hash,authority_before,authority_after,expect_correct,fix_accepted\n";
        assets << "kind,scene,resource,source,raw_bytes,raw_hash,width,height,offset_x,offset_y,global_width,global_height,shadow_mask_bytes,shadow_mask_hash\n";
        frames << std::setprecision(17); cases << std::setprecision(17);
        for (const auto* tile : {&flat, &ramp1, &ramp3})
            assets << "TMP,all," << quoted(tile->fileName) << ',' << quoted(tile->source) << ','
                << tile->bytes.size() << ',' << bytesHash(tile->bytes.data(), tile->bytes.size()) << ','
                << tile->gridWidth << ',' << tile->gridHeight << ",0,0,0,0,0,0\n";
        const std::array<Scene, 4> scenes{{{"road-ramp1-center",1,false},
            {"road-ramp3-center",3,false},{"flat-control",0,false},{"road-ramp1-fractional-stop",1,true}}};
        const std::array<const char*, 5> phases{{"cold","warm","shadow-off-body","isolated-actual-shadow","isolated-expected-shadow"}};
        int slopeDifferences = 0;
        for (const auto& scene : scenes) {
            const auto& tile = scene.ramp == 3 ? ramp3 : scene.ramp == 1 ? ramp1 : flat;
            GameWorld world = sceneWorld(scene, flat.tileIndex, tile.tileIndex, type->strength, error);
            if (!error.empty() || !world.getSummary().loaded || world.getTerrainCells().size() != 841
                || world.getObjects().size() != 1) { if (error.empty()) error = "isolated world shape"; return false; }
            const auto& tank = world.getObjects().front();
            if (scene.stopped && !worldObjectHasStoppedGroundPose(tank)) {
                error = "real baseline fractional stopped pose not recognized"; return false;
            }
            const auto* art = findArtTypeForObject(tank, context);
            if (!art || !art->voxel || art->useTurretShadow || runtimeObjectLooksAircraftForRender(tank, context)) {
                error = "original MTNK ground VXL/no-attached-shadow classification differs"; return false;
            }
            const auto authorityBefore = world.deterministicSimulationStateHash();
            RuntimeMapRenderCache cache;
            std::array<std::vector<std::uint32_t>, 5> images;
            std::array<RuntimeMapRenderStats, 5> stats{};
            std::array<int, 5> shadowPixels{};
            for (int pass = 0; pass < 3; ++pass) {
                surface.clear(); context.shadowsEnabled = pass != 2;
                stats[pass] = drawRuntimeMap(surface.dc, viewport, world, context, camera, cache);
                images[pass] = surface.capture(); shadowPixels[pass] = stats[pass].shadowPixelsDarkened;
                if (!stats[pass].rendered || stats[pass].visibleVoxelBodies != 1
                    || stats[pass].visibleVoxelTurrets <= 0) {
                    error = std::string(scene.name) + " original body/turret not actually drawn"; return false;
                }
            }
            context.shadowsEnabled = true;
            if (stats[0].terrainLayerCacheMisses <= 0 || stats[1].terrainLayerCacheHits <= 0
                || stats[0].voxelShadowsDrawn != 1 || stats[0].generatedShadowFallbacks != 0
                || stats[2].shadowCandidates != 1 || stats[2].shadowsSuppressed != 1
                || stats[2].shadowsDrawn != 0 || stats[2].voxelShadowsDrawn != 0
                || stats[2].shadowPixelsDarkened != 0
                || !cache.terrainLayer.built || cache.terrainLayer.width <= 0 || cache.terrainLayer.height <= 0
                || cache.terrainLayer.pixels.size() != static_cast<std::size_t>(cache.terrainLayer.width) * cache.terrainLayer.height) {
                error = std::string(scene.name) + " cold/warm/disabled-shadow or terrain-cache contract"; return false;
            }
            const double bodyX = tank.hasRenderPosition ? tank.renderX : tank.x;
            const double bodyY = tank.hasRenderPosition ? tank.renderY : tank.y;
            const double renderZ = scene.stopped ? tank.renderLevel : tank.level;
            // Independent retail floor planes for original raw ramp0/1/3.
            const int rawRamp = tile.subtiles[scene.ramp ? 1 : 0].ramp;
            const double floor = tank.level + (rawRamp == 1 ? .5 + (bodyX - tank.x)
                : rawRamp == 3 ? .5 - (bodyX - tank.x) : 0.0);
            const double receiverDelta = floor - renderZ;
            const int shiftY = static_cast<int>(std::lround(receiverDelta * camera.tileHeight / 2.0));
            const POINT independentBase{viewport.left + static_cast<LONG>(std::llround((bodyX - bodyY) * camera.tileWidth / 2.0)) - camera.worldOffsetX,
                viewport.top + static_cast<LONG>(std::llround((bodyX + bodyY) * camera.tileHeight / 2.0
                    - renderZ * camera.tileHeight / 2.0)) - camera.worldOffsetY};
            const POINT expectedCenter{independentBase.x, independentBase.y - shiftY};
            const auto nativePoint = runtimeObjectShadowDrawPoint(tank, RuntimeFootprintDimensions{1,1},
                camera, false, drawLevelForObject(tank));
            const POINT nativeCenter = screenPoint(nativePoint, viewport, camera);
            if (nativeCenter.x != independentBase.x || nativeCenter.y != independentBase.y) {
                error = "independent native base projection differs"; return false;
            }
            auto* voxel = loadRuntimeVoxelFromCache(tank, context, cache);
            if (!voxel || !voxel->hasSprite || voxel->image.width <= 0 || voxel->image.height <= 0
                || voxel->shadowPixels.size() < static_cast<std::size_t>(voxel->image.width) * voxel->image.height
                || std::none_of(voxel->shadowPixels.begin(), voxel->shadowPixels.end(), [](unsigned char v){return v != 0;})) {
                error = "original VXL shadow template empty"; return false;
            }
            const Bytes shadowTemplate = voxel->shadowPixels;
            if (!writeBytes(output / "native-original" / (std::string(scene.name) + "-shadow.pal8"), shadowTemplate)) {
                error = "shadow template close/readback export"; return false;
            }
            assets << "VXL-shadow," << scene.name << ',' << quoted(voxel->metadata.imageId) << ','
                << quoted(voxel->metadata.vxlPath) << ',' << voxel->metadata.vxlFileSize << ",0,"
                << voxel->image.width << ',' << voxel->image.height << ',' << voxel->image.offsetX << ','
                << voxel->image.offsetY << ',' << voxel->image.globalWidth << ',' << voxel->image.globalHeight << ','
                << shadowTemplate.size() << ',' << bytesHash(shadowTemplate.data(), shadowTemplate.size()) << '\n';
            RuntimeWorldObjectShadowResult actualShadow;
            for (int pass = 3; pass < 5; ++pass) {
                surface.clear();
                if (!drawTerrainLayerToViewport(surface.dc, viewport, context, camera, cache.terrainLayer)) {
                    error = "actual native underlying ground blit"; return false;
                }
                RuntimeGeometryQueryScope geometry(world, viewport, context, camera, cache);
                if (pass == 3) {
                    actualShadow = drawRuntimeWorldObjectShadow(surface.dc, viewport, tank, nativeCenter,
                        context, camera, cache, true);
                    if (!actualShadow.drawn || !actualShadow.voxelMask || actualShadow.generatedFallback
                        || actualShadow.pixelsDarkened <= 0) {
                        error = "actual isolated native VXL shadow not drawn"; return false;
                    }
                    shadowPixels[pass] = actualShadow.pixelsDarkened;
                } else {
                    // Expected uses the SAME ORIGINAL mask blitter, a separately computed TMP receiver,
                    // and the native attached-part pass. The actual world-shadow helper is never used here.
                    const POINT expectedModelCenter = runtimeVoxelModelDrawCenter(expectedCenter, camera);
                    int expectedPixels = 0;
                    if (!drawRuntimeVoxelShadowMask(surface.dc, viewport, expectedModelCenter, context,
                        camera, voxel->image, shadowTemplate, false, &expectedPixels, nullptr)) {
                        error = "independent original VXL mask invalid"; return false;
                    }
                    expectedPixels += drawRuntimeAttachedVoxelShadows(surface.dc, viewport, tank,
                        expectedModelCenter, context, camera, cache, voxel->rasterFitScale);
                    if (expectedPixels <= 0) { error = "independent original mask has no actual ground pixels"; return false; }
                    shadowPixels[pass] = expectedPixels;
                }
                images[pass] = surface.capture();
            }
            const auto warmMismatches = differences(images[0], images[1]);
            const auto shadowMismatches = differences(images[3], images[4]);
            const auto authorityAfter = world.deterministicSimulationStateHash();
            for (int pass = 0; pass < 5; ++pass)
                frames << scene.name << ',' << phases[pass] << ',' << pixelHash(images[pass]) << ','
                    << stats[pass].terrainLayerCacheHits << ',' << stats[pass].terrainLayerCacheMisses << ','
                    << stats[pass].visibleVoxelBodies << ',' << stats[pass].visibleVoxelTurrets << ','
                    << shadowPixels[pass] << ',' << stats[pass].generatedShadowFallbacks << ','
                    << surfaceWidth << ',' << surfaceHeight << ',' << camera.worldOffsetX << ',' << camera.worldOffsetY << '\n';
            cases << scene.name << ',' << rawRamp << ',' << tile.tileIndex << ',' << (scene.ramp ? 1 : 0)
                << ',' << tank.x << ',' << tank.y << ',' << tank.subCell << ',' << bodyX << ',' << bodyY << ',' << renderZ
                << ',' << floor << ',' << receiverDelta << ',' << shiftY << ',' << nativeCenter.x << ',' << nativeCenter.y
                << ',' << expectedCenter.x << ',' << expectedCenter.y << ',' << warmMismatches << ',' << shadowMismatches
                << ',' << pixelHash(images[2]) << ',' << authorityBefore << ',' << authorityAfter
                << ',' << int(expectCorrect) << ',' << int(expectCorrect && shadowMismatches == 0) << '\n';
            std::vector<std::uint32_t> sheet(static_cast<std::size_t>(surfaceWidth * 5) * surfaceHeight);
            for (int pass = 0; pass < 5; ++pass) for (int y = 0; y < surfaceHeight; ++y)
                std::copy_n(images[pass].data() + static_cast<std::size_t>(y) * surfaceWidth, surfaceWidth,
                    sheet.data() + static_cast<std::size_t>(y) * surfaceWidth * 5 + pass * surfaceWidth);
            if (!writeBitmap(output / (std::string(scene.name) + "-five-pass.bmp"), surfaceWidth * 5, surfaceHeight, sheet)
                || !writeBitmap(output / (std::string(scene.name) + "-shadow-off-body.bmp"), surfaceWidth, surfaceHeight, images[2])) {
                error = "actual DIB BMP write"; return false;
            }
            std::cout << "Issue22 ground shadow: scene=" << scene.name << " floor=" << std::setprecision(17)
                << floor << " shiftY=" << shiftY << " warmMismatches=" << warmMismatches
                << " shadowMismatches=" << shadowMismatches << " bodyHash=" << pixelHash(images[2]) << '\n';
            if (warmMismatches != 0 || authorityBefore != authorityAfter || (!scene.ramp && shadowMismatches != 0)
                || (expectCorrect && shadowMismatches != 0) || (!expectCorrect && scene.ramp && shadowMismatches == 0)) {
                error = std::string(scene.name) + " exact warm/authority/receiver expectation failed"; return false;
            }
            if (scene.ramp && shadowMismatches != 0) ++slopeDifferences;
        }
        frames.close(); cases.close(); assets.close();
        if (frames.fail() || cases.fail() || assets.fail()) { error = "finite CSV write"; return false; }
        std::cout << "Issue22 ground shadow " << (expectCorrect ? "strict source-frame PASS" : "BEFORE DIAGNOSTIC ONLY")
            << ": scenes=4 captures=20 bmp=8 nativeTmp=3 shadowTemplates=4 slopeDifferences=" << slopeDifferences
            << " issue22FixAccepted=" << (expectCorrect ? "true" : "false") << '\n';
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Issue22 ground shadow: ") + exception.what(); return false;
    }
}
