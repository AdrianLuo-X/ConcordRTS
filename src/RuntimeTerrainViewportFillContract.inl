// Native old FillRect + the unchanged actual layer-copy/map producer is the
// oracle. Compare every raw32 pixel (including high bytes and outside viewport),
// all existing 353 semantic stats, cache resources/errors and DC brush state.
namespace RuntimeTerrainViewportFillContractDetail {
struct DIB {
    HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ oldBitmap = nullptr;
    std::uint32_t* pixels = nullptr; int width = 0, height = 0;
    bool topDown = false;
    DIB(int w, int h, bool negativeHeight = true) : width(w), height(h), topDown(negativeHeight) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = negativeHeight ? -h : h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* raw = nullptr;
        if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        pixels = static_cast<std::uint32_t*>(raw);
        if (bitmap) oldBitmap = SelectObject(dc, bitmap);
    }
    ~DIB() { if (oldBitmap) SelectObject(dc, oldBitmap); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    bool valid() const { return dc && bitmap && pixels && oldBitmap && oldBitmap != HGDI_ERROR; }
    void initialize() {
        GdiFlush();
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i)
            pixels[i] = 0xA5000000U ^ static_cast<std::uint32_t>(i * 9173U + 31U);
    }
    std::vector<std::uint32_t> copy() const {
        GdiFlush(); return {pixels, pixels + static_cast<std::size_t>(width) * height};
    }
};
inline void setupDC(HDC dc, int scene) {
    if (scene == 4) IntersectClipRect(dc, 4, 5, 31, 30);
    if (scene == 5) SetViewportOrgEx(dc, 3, 4, nullptr);
    if (scene == 6) SetMapMode(dc, MM_ANISOTROPIC);
    if (scene == 7) SetGraphicsMode(dc, GM_ADVANCED);
    if (scene == 8) SetLayout(dc, LAYOUT_RTL);
}
static bool primitive(std::string& error, std::uint64_t& checks) {
    for (int scene = 0; scene != 14; ++scene) {
        DIB old(64, 48, scene != 9), actual(64, 48, scene != 9);
        if (!old.valid() || !actual.valid()) { error = "viewport fill primitive DIB allocation"; return false; }
        RuntimeTerrainLayerCache layer; layer.built = true; layer.width = 80; layer.height = 60;
        layer.originX = -8; layer.originY = -6;
        layer.pixels.resize(static_cast<std::size_t>(layer.width) * layer.height);
        for (std::size_t i = 0; i < layer.pixels.size(); ++i)
            layer.pixels[i] = 0xBC000000U ^ static_cast<std::uint32_t>(i * 3217U + 17U);
        RECT viewport = scene == 1 ? RECT{7, 9, 51, 37} : RECT{0, 0, 64, 48};
        if (scene == 2) layer.originX = 1;  // only partial coverage: preserve background
        if (scene == 3) viewport.left = -1; // surface boundary crossing
        if (scene == 12) layer.built = false;
        if (scene == 13) layer.pixels.clear(); // guard only; never invoke an invalid copy
        RuntimeMapCamera camera; camera.initialized = true;
        RuntimeMapRenderContext context; context.surfaceWidth = 64; context.surfaceHeight = 48;
        context.surfaceTopDownOwned = actual.topDown && scene != 10;
        RuntimeTerrainViewportFillObservation witness;
        const bool expectOmission = scene == 0 || scene == 1;
        bool copied[2]{}; COLORREF brushColors[2]{}; HGDIOBJ brushes[2]{};
        for (int branch = 0; branch != 2; ++branch) {
            auto& dib = branch ? actual : old;
            const int savedDC = SaveDC(dib.dc);
            if (!savedDC) { error = "viewport fill primitive SaveDC"; return false; }
            dib.initialize(); setupDC(dib.dc, scene);
            SelectObject(dib.dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dib.dc, RGB(15, 22, 31));
            // A real previously queued GDI write precedes both routes.
            RECT queued{0, 0, 64, 48}; HBRUSH queuedBrush = CreateSolidBrush(RGB(9, 8, 7));
            if (!queuedBrush) { RestoreDC(dib.dc, savedDC); error = "queued brush allocation"; return false; }
            FillRect(dib.dc, &queued, queuedBrush); DeleteObject(queuedBrush);
            context.surfacePixels = scene == 11 ? nullptr : dib.pixels;
            bool omit = false;
            if (branch) {
                RuntimeTerrainViewportFillObservationScope observe(witness);
                omit = runtimeTryOmitTerrainViewportFill(dib.dc, viewport, context, camera, layer);
            }
            if (!omit) FillRect(dib.dc, &viewport, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if (scene != 13) copied[branch] = drawTerrainLayerToViewport(dib.dc, viewport, context, camera, layer);
            brushColors[branch] = GetDCBrushColor(dib.dc); brushes[branch] = GetCurrentObject(dib.dc, OBJ_BRUSH);
            GdiFlush();
            if (branch && (omit != expectOmission || witness.flushCalls != (expectOmission ? 1U : 0U))) {
                RestoreDC(dib.dc, savedDC); error = "viewport fill primitive branch witness scene=" + std::to_string(scene); return false;
            }
            RestoreDC(dib.dc, savedDC);
        }
        if (old.copy() != actual.copy() || copied[0] != copied[1]
            || brushColors[0] != brushColors[1] || brushes[0] != brushes[1]) {
            error = "viewport fill primitive raw32/copy/DC state scene=" + std::to_string(scene); return false;
        }
        ++checks;
    }
    // Arithmetic/OOB guard checks are independent of the raster copy. Never
    // pass a corrupt vector to the old producer merely to test admission.
    DIB dib(64, 48); if (!dib.valid()) { error = "guard DIB allocation"; return false; }
    RuntimeMapRenderContext context; context.surfacePixels = dib.pixels;
    context.surfaceWidth = 64; context.surfaceHeight = 48; context.surfaceTopDownOwned = true;
    RuntimeTerrainLayerCache layer; layer.built = true; layer.width = 64; layer.height = 48;
    layer.pixels.resize(64U * 48U); RuntimeMapCamera camera; camera.initialized = true;
    const RECT viewport{0, 0, 64, 48};
    layer.pixels.pop_back();
    if (runtimeTryOmitTerrainViewportFill(dib.dc, viewport, context, camera, layer)) { error = "truncated layer admitted"; return false; }
    layer.pixels.push_back(0); layer.originX = (std::numeric_limits<int>::max)();
    if (runtimeTryOmitTerrainViewportFill(dib.dc, viewport, context, camera, layer)) { error = "overflow layer admitted"; return false; }
    const RECT nonzeroViewport{1, 1, 64, 48};
    camera.worldOffsetX = (std::numeric_limits<int>::max)();
    if (runtimeTryOmitTerrainViewportFill(dib.dc, nonzeroViewport, context, camera, layer)) { error = "cancelling intermediate X overflow admitted"; return false; }
    layer.originX = camera.worldOffsetX = 0;
    layer.originY = camera.worldOffsetY = (std::numeric_limits<int>::max)();
    if (runtimeTryOmitTerrainViewportFill(dib.dc, nonzeroViewport, context, camera, layer)) { error = "cancelling intermediate Y overflow admitted"; return false; }
    layer.originY = camera.worldOffsetY = 0;
    layer.originX = 0; context.surfacePixels = dib.pixels + 1;
    if (runtimeTryOmitTerrainViewportFill(dib.dc, viewport, context, camera, layer)) { error = "foreign bits admitted"; return false; }
    std::uint64_t saturated = (std::numeric_limits<std::uint64_t>::max)();
    runtimeCountTerrainViewportFill(saturated);
    if (saturated != (std::numeric_limits<std::uint64_t>::max)()) { error = "diagnostic counter wrapped"; return false; }
    checks += 6;
    return true;
}
} // namespace RuntimeTerrainViewportFillContractDetail

static bool runRuntimeTerrainViewportFillContractCheck(const RuntimeMapRenderContext& source, std::string& error) {
    using namespace RuntimeTerrainViewportFillContractDetail;
    const auto fail = [&](const std::string& why) { error = "terrain viewport fill: " + why; return false; };
    std::uint64_t primitiveChecks = 0, draws = 0, elisions = 0;
    if (!primitive(error, primitiveChecks)) return false;
    if (!source.rules || !source.art || !source.theater) return fail("stock assets absent");
    IniFile ini; MapDatabase map; GameWorld world;
    if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("actual fixture map");
    if (world.getTerrainCells().empty()) for (int y = 0; y != 32; ++y) for (int x = 0; x != 32; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance object; object.instanceId = 1; object.type = "MTNK"; object.kind = WorldObjectKind::Unit;
    object.owner = "Observer"; object.alive = true; object.health = 1000;
    object.mission = "Guard"; object.x = object.y = 8; object.facing = 64;
    world.accessObjects().push_back(object);
    RuntimeVisionSystem vision;
    (void)vision.update(world, source.rules, {{"Observer", {"Observer"}}}, 42);
    for (int scene = 0; scene != 9; ++scene) {
        DIB old(800, 600), actual(800, 600);
        if (!old.valid() || !actual.valid()) return fail("actual map DIB allocation");
        RECT viewport = scene == 1 ? RECT{17, 23, 781, 577} : RECT{0, 0, 800, 600};
        if (scene == 2) viewport.left = -7;
        if (scene == 3) viewport.right = 807;
        RuntimeMapRenderContext context = source;
        context.animationTick = 42; context.fixedTickHz = 30; context.stableRenderConfiguration = true;
        context.vision = &vision; context.visionObserverOwner = "Observer"; context.mapFacts = nullptr;
        context.projectileActors = nullptr; context.impactActors = nullptr; context.wrecks = nullptr;
        context.buildingCollapses = nullptr; context.specialMechanismSnapshots = nullptr; context.gateSnapshots = nullptr;
        context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
        context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr; context.uiResources = nullptr;
        context.lightingTint = nullptr; context.lightingProfile = nullptr;
        context.detailStatsEnabled = context.coarseStageTimingEnabled = context.effectOnlyProbe = false;
        context.captureBuildingDepthHitSpans = false; context.useTileTextures = true;
        context.surfaceWidth = 800; context.surfaceHeight = 600;
        context.surfaceTopDownOwned = scene != 4;
        if (scene == 6) context.effectOnlyProbe = true;
        if (scene == 7) context.palettePath.clear(); // actual buildTerrainLayer failure
        RuntimeMapCamera camera; camera.initialized = true;
        camera.worldOffsetX = -400; camera.worldOffsetY = -80;
        RuntimeMapRenderCache oldCache, actualCache;
        for (int pass = 0; pass != 3; ++pass) {
            std::vector<std::uint32_t> expected; RuntimeMapRenderStats expectedStats;
            COLORREF expectedBrushColor = 0; HGDIOBJ expectedBrush = nullptr, expectedPen = nullptr;
            const auto revision = vision.revisionFor("Observer");
            for (int branch = 0; branch != 2; ++branch) {
                auto& dib = branch ? actual : old; auto& cache = branch ? actualCache : oldCache;
                dib.initialize(); const int savedDC = SaveDC(dib.dc);
                if (!savedDC) return fail("actual map SaveDC");
                if (scene == 8) IntersectClipRect(dib.dc, 7, 11, 789, 587);
                context.surfacePixels = scene == 5 ? nullptr : dib.pixels;
                RuntimeTerrainViewportFillReferenceScope reference(branch == 0);
                RuntimeTerrainViewportFillObservation observed;
                RuntimeTerrainViewportFillObservationScope observe(observed);
                auto drawCamera = camera;
                const auto stats = drawRuntimeMap(dib.dc, viewport, world, context, drawCamera, cache);
                const auto pixels = dib.copy();
                const COLORREF brushColor = GetDCBrushColor(dib.dc);
                const HGDIOBJ brush = GetCurrentObject(dib.dc, OBJ_BRUSH), pen = GetCurrentObject(dib.dc, OBJ_PEN);
                RestoreDC(dib.dc, savedDC);
                if (vision.revisionFor("Observer") != revision) return fail("drawing mutated vision");
                if (!branch) {
                    expected = pixels; expectedStats = stats; expectedBrushColor = brushColor;
                    expectedBrush = brush; expectedPen = pen;
                    if (observed.fills != 1 || observed.omitted || oldCache.terrainViewportFillCalls != static_cast<std::uint64_t>(pass + 1))
                        return fail("original FillRect producer witness");
                } else {
                    const bool omit = scene == 0 || scene == 1;
                    if (observed.omitted != (omit ? 1U : 0U) || observed.fills != (omit ? 0U : 1U)
                        || observed.flushCalls != (omit ? 1U : 0U)) return fail("actual branch witness scene=" + std::to_string(scene));
                    if ((scene == 0 || scene == 1) && (!stats.rendered || !cache.terrainLayer.built || !stats.visibleVoxelBodies))
                        return fail("stock full terrain and VXL producer absent");
                    if (scene == 7 && cache.terrainLayer.built) return fail("palette failure witness missing");
                    std::string field;
                    if (pixels != expected || !runtimeGroundShadowContractStatsEqual(expectedStats, stats, field)
                        || !runtimeGroundShadowContractCacheEqual(oldCache, actualCache)
                        || brushColor != expectedBrushColor || brush != expectedBrush || pen != expectedPen)
                        return fail("actual pixels/stats/cache/DC scene=" + std::to_string(scene) + " pass=" + std::to_string(pass) + " " + field);
                    elisions += observed.omitted; ++draws;
                }
            }
        }
    }
    if (primitiveChecks != 20 || draws != 27 || elisions != 6) return fail("incomplete matrix");
    std::cout << "Terrain viewport fill contract passed: primitive=" << primitiveChecks
        << " fullMapPairs=" << draws << " positiveElisions=" << elisions << " raw32=yes stats353=yes cache=yes brush=yes\n";
    error.clear(); return true;
}
