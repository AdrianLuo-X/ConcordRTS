// Include after the camera bounds/reveal/clamp functions. This contract only
// changes a heap-owned private probe; the supplied loaded world stays intact.
bool runRuntimeCombinedCameraBoundsContract(
    RuntimeWindowState& live, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "combined camera bounds: " + message;
        return false;
    };
    if (!live.world.getSummary().loaded || live.world.getTerrainCells().empty())
        return fail("an actual loaded terrain map is required");

    const int entryReference = runtimeCameraBoundsPassReferenceOverride;
    {
        RuntimeCameraBoundsPassReferenceScope outer(true);
        if (!runtimeCameraBoundsPassReference()) return fail("reference scope not enabled");
        {
            RuntimeCameraBoundsPassReferenceScope inner(false);
            if (runtimeCameraBoundsPassReference()) return fail("nested optimized scope not enabled");
        }
        if (!runtimeCameraBoundsPassReference()) return fail("nested scope did not restore reference");
    }
    if (runtimeCameraBoundsPassReferenceOverride != entryReference)
        return fail("reference scope did not restore its original TLS value");

    const std::uint64_t liveHash = live.world.deterministicSimulationStateHash();
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& probe = *storage;
    probe.world = live.world;
    probe.report.map = live.report.map;
    probe.camera = live.camera;
    probe.rules = live.rules;
    probe.art = live.art;
    probe.rulesLoaded = live.rulesLoaded;
    probe.artLoaded = live.artLoaded;
    const auto sameBounds = [](const RuntimeCameraWorldBounds& a,
                               const RuntimeCameraWorldBounds& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right
            && a.bottom == b.bottom && a.valid == b.valid;
    };
    const auto sameReveal = [](const RuntimeCameraRevealMargins& a,
                               const RuntimeCameraRevealMargins& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right
            && a.bottom == b.bottom;
    };
    // Bounds arithmetic stays within the original int domain. Extreme offsets
    // only enter comparisons in clamp; extreme dimensions/coordinates that
    // would overflow the old projection are deliberately not synthesized.
    const std::array<std::pair<int, int>, 6> dimensions{{
        {60, 30}, {61, 29}, {31, 17}, {1, 1}, {0, 0}, {67, 59}
    }};
    const std::array<RECT, 7> viewports{{
        {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 0, 31, 17},
        {13, -7, 130, 91}, {0, 0, 1024, 640},
        {0, 0, 2256, 1504}, {31, 17, 0, 0}
    }};
    const int minimumOffset = std::numeric_limits<int>::min();
    const int maximumOffset = std::numeric_limits<int>::max();
    const std::array<std::pair<int, int>, 8> offsets{{
        {0, 0}, {minimumOffset, minimumOffset}, {maximumOffset, maximumOffset},
        {minimumOffset, maximumOffset}, {maximumOffset, minimumOffset},
        {-99999, 99999}, {17, -23}, {-23, 17}
    }};
    std::uint64_t boundsPairs = 0, revealPairs = 0, clampPairs = 0;
    int fixtures = 0, syntheticFixtures = 0;
    const auto compareFixture = [&](const char* label, bool dimensionMatrix) {
        ++fixtures;
        const auto entryCamera = probe.camera;
        const std::size_t count = dimensionMatrix ? dimensions.size() : 1U;
        for (std::size_t dimension = 0; dimension < count; ++dimension) {
            if (dimensionMatrix) {
                probe.camera.tileWidth = dimensions[dimension].first;
                probe.camera.tileHeight = dimensions[dimension].second;
            }
            const RuntimeCameraWorldBounds originalPlayable = runtimeCameraWorldBounds(probe);
            const RuntimeCameraWorldBounds originalFull = runtimeFullTerrainCameraWorldBounds(probe);
            const RuntimeCombinedCameraBounds combined = runtimeCombinedCameraWorldBounds(probe);
            if (!sameBounds(originalPlayable, combined.playable)
                || !sameBounds(originalFull, combined.full))
                return fail(std::string(label) + ": five-field playable/full bounds differ");
            ++boundsPairs;
            const RuntimeCameraRevealMargins originalReveal =
                runtimeCameraRevealMargins(probe, originalPlayable);
            const RuntimeCameraRevealMargins completedReveal =
                runtimeCameraRevealMargins(probe, combined.playable, &combined.full);
            if (!sameReveal(originalReveal, completedReveal))
                return fail(std::string(label) + ": four reveal margins differ");
            ++revealPairs;
            for (const RECT& viewport : viewports) {
                for (const auto& [offsetX, offsetY] : offsets) {
                    probe.camera.worldOffsetX = offsetX;
                    probe.camera.worldOffsetY = offsetY;
                    bool originalChanged = false, originalRepeated = false;
                    int originalX = 0, originalY = 0;
                    {
                        RuntimeCameraBoundsPassReferenceScope reference(true);
                        originalChanged = clampRuntimeMapCamera(probe, viewport);
                        originalX = probe.camera.worldOffsetX;
                        originalY = probe.camera.worldOffsetY;
                        originalRepeated = clampRuntimeMapCamera(probe, viewport);
                        if (originalRepeated || probe.camera.worldOffsetX != originalX
                            || probe.camera.worldOffsetY != originalY)
                            return fail(std::string(label) + ": original clamp is not idempotent");
                    }
                    probe.camera.worldOffsetX = offsetX;
                    probe.camera.worldOffsetY = offsetY;
                    {
                        RuntimeCameraBoundsPassReferenceScope optimized(false);
                        const bool optimizedChanged = clampRuntimeMapCamera(probe, viewport);
                        if (optimizedChanged != originalChanged
                            || probe.camera.worldOffsetX != originalX
                            || probe.camera.worldOffsetY != originalY)
                            return fail(std::string(label) + ": clamp return/offset pair differs");
                        if (clampRuntimeMapCamera(probe, viewport)
                            || probe.camera.worldOffsetX != originalX
                            || probe.camera.worldOffsetY != originalY)
                            return fail(std::string(label) + ": optimized clamp is not idempotent");
                    }
                    if (runtimeCameraBoundsPassReferenceOverride != entryReference)
                        return fail(std::string(label) + ": clamp leaked reference TLS");
                    ++clampPairs;
                }
            }
        }
        probe.camera = entryCamera;
        return true;
    };
    if (!compareFixture("actual loaded map", false)) return false;
    const auto cell = [](int x, int y, int level = 0) {
        TerrainCell value;
        value.x = x; value.y = y; value.level = level;
        return value;
    };
    const auto configure = [&](const char* size, const char* local, int width,
                               std::vector<TerrainCell> cells) {
        probe.report.map.size = size;
        probe.report.map.localSize = local;
        probe.report.map.width = width;
        probe.world.accessTerrainCells() = std::move(cells);
        probe.radarProjectionCacheValid = false;
        probe.camera.tileWidth = 60;
        probe.camera.tileHeight = 30;
    };
    const auto synthetic = [&](const char* label) {
        ++syntheticFixtures;
        return compareFixture(label, true);
    };
    configure("", "", 0, {});
    if (!synthetic("empty terrain")) return false;
    configure("invalid", "invalid", 0, {cell(-7, 4, 2), cell(16, 16, -3)});
    if (buildRuntimeRadarProjection(probe).valid)
        return fail("invalid-header fixture unexpectedly has a valid projection");
    if (!synthetic("invalid projection")) return false;
    configure("", "", 20, {cell(10, 10), cell(16, 16, 2), cell(19, 3, -1)});
    if (!synthetic("missing LocalSize")) return false;
    configure("0,0,20,20", "bad,local,size", 20,
        {cell(10, 10), cell(16, 16, 3), cell(3, 19, -2)});
    if (!synthetic("malformed LocalSize")) return false;
    configure("0,0,20,20", "2,2,0,4", 20,
        {cell(10, 10), cell(16, 16), cell(3, 19)});
    if (!synthetic("zero LocalSize extent")) return false;
    configure("0,0,20,20", "2,2,2,2", 20, {cell(10, 10), cell(11, 10, 2)});
    const auto disjoint = runtimeCombinedCameraWorldBounds(probe);
    if (!buildRuntimeRadarProjection(probe).valid || !disjoint.full.valid
        || !sameBounds(disjoint.playable, disjoint.full))
        return fail("disjoint LocalSize fixture did not use full-terrain fallback");
    if (!synthetic("disjoint LocalSize")) return false;
    configure("0,0,20,20", "2,2,8,4", 20,
        {cell(16, 16), cell(4, 3, -2), cell(30, 35, 4)});
    const auto partial = runtimeCombinedCameraWorldBounds(probe);
    if (!buildRuntimeRadarProjection(probe).valid || !partial.playable.valid
        || sameBounds(partial.playable, partial.full))
        return fail("partial LocalSize fixture does not exercise two different envelopes");
    if (!synthetic("partial LocalSize")) return false;
    configure("", "", 0,
        {cell(-30, 15, -4), cell(4, -10, 3), cell(16, 16), cell(30, 35, 4)});
    if (!synthetic("sparse negative coordinates and levels")) return false;
    auto& terrain = probe.world.accessTerrainCells();
    terrain.push_back(terrain.front());
    terrain.push_back(cell(16, 16, 9));
    if (!synthetic("duplicates and distinct elevation")) return false;
    const auto beforeReorder = runtimeCombinedCameraWorldBounds(probe);
    std::reverse(terrain.begin(), terrain.end());
    const auto afterReorder = runtimeCombinedCameraWorldBounds(probe);
    if (!sameBounds(beforeReorder.playable, afterReorder.playable)
        || !sameBounds(beforeReorder.full, afterReorder.full))
        return fail("reorder changed an envelope");
    if (!synthetic("reordered terrain")) return false;
    const auto beforeXMutation = runtimeFullTerrainCameraWorldBounds(probe);
    terrain.front().x = -80;
    if (sameBounds(beforeXMutation, runtimeFullTerrainCameraWorldBounds(probe)))
        return fail("same-count x mutation did not actually change the full envelope");
    if (!synthetic("in-place x change same count")) return false;
    const auto beforeYMutation = runtimeFullTerrainCameraWorldBounds(probe);
    terrain.front().y = 83;
    if (sameBounds(beforeYMutation, runtimeFullTerrainCameraWorldBounds(probe)))
        return fail("same-count y mutation did not actually change the full envelope");
    if (!synthetic("in-place y change same count")) return false;
    const auto beforeLevelMutation = runtimeFullTerrainCameraWorldBounds(probe);
    terrain.front().level = 47;
    if (sameBounds(beforeLevelMutation, runtimeFullTerrainCameraWorldBounds(probe)))
        return fail("same-count elevation mutation did not actually change the full envelope");
    if (!synthetic("in-place level change same count")) return false;
    const std::size_t sameCount = terrain.size();
    terrain.assign(sameCount, cell(-14, 23, -6));
    if (!synthetic("whole vector replace same count")) return false;
    terrain.clear();
    if (!synthetic("clear after populated bounds")) return false;
    terrain.assign(sameCount, cell(23, -14, 7));
    if (!synthetic("refill former size after clear")) return false;
    terrain = {cell(-3, -4, 2), cell(21, 8, -3)};
    if (!synthetic("replace different terrain size")) return false;

    probe.world = live.world;
    probe.report.map = live.report.map;
    probe.camera = live.camera;
    probe.radarProjectionCacheValid = false;
    if (!compareFixture("actual map restored after synthetic mutations", false)) return false;
    if (syntheticFixtures != 17 || fixtures != 19 || boundsPairs != 104
        || revealPairs != 104 || clampPairs != 5824)
        return fail("fixture matrix completion counts differ");
    if (runtimeCameraBoundsPassReferenceOverride != entryReference
        || live.world.deterministicSimulationStateHash() != liveHash)
        return fail("contract changed caller world or reference TLS");
    std::cout << "Camera combined bounds: actualLoadedMap=yes syntheticFixtures=17 "
        << "boundsPairs=104 boundsFields=5 revealPairs=104 revealFields=4 "
        << "clampPairs=5824 reference/TLS/mutations/oddZeroViewports=exact passed=yes\n";
    return true;
}
