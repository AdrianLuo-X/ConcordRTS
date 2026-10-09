// Included after runtimeWindowProc. The caller supplies a dedicated, initialized
// interaction-smoke state and exits afterwards; no simulation or input is run.
bool runRuntimeFullscreenRenderBenchmark(
    RuntimeWindowState& state,
    std::string& errorMessage
) {
    if (!state.world.getSummary().loaded || state.backBuffer.bitmap != nullptr
        || state.skirmishMatchFinished) {
        errorMessage = "fullscreen benchmark requires a fresh, loaded smoke state";
        return false;
    }
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeVoiceVolume = 0;
    state.runtimeSoundVolume = 0;
    state.runtimeMusicEnabled = false;
    state.visionEnabled = true;
    updateRuntimeVisionState(state);
    const auto worldHash = state.world.deterministicSimulationStateHash();
    int samples = 8;
    RuntimeCellPoint denseCell{-1, -1};
    const char* denseCellText = std::getenv("CONCORD_DENSE_BUILDING_BENCHMARK_CELL");
    const bool denseBenchmark = denseCellText != nullptr;
    if (denseBenchmark) {
        char* separator = nullptr;
        const long x = std::strtol(denseCellText, &separator, 10);
        char* end = nullptr;
        const long y = separator != denseCellText && *separator == ','
            ? std::strtol(separator + 1, &end, 10) : -1;
        if (separator == denseCellText || *separator != ',' || end == separator + 1
            || end == nullptr || *end != '\0' || x < 0 || x > 10000 || y < 0 || y > 10000) {
            errorMessage = "dense building benchmark cell must be x,y in [0,10000]";
            return false;
        }
        denseCell = {static_cast<int>(x), static_cast<int>(y)};
        if (const char* value = std::getenv("CONCORD_DENSE_BUILDING_BENCHMARK_SAMPLES")) {
            char* sampleEnd = nullptr;
            const long parsed = std::strtol(value, &sampleEnd, 10);
            if (sampleEnd == value || *sampleEnd != '\0' || parsed < 8 || parsed > 120) {
                errorMessage = "dense building benchmark samples must be in [8,120]";
                return false;
            }
            samples = static_cast<int>(parsed);
        }
        state.mouseClientPointValid = false;
        // This dedicated render probe does not run the skirmish opening or
        // move a scout to the requested location. Reveal its observer's map
        // explicitly so a successful timing cannot merely measure black
        // shroud. Keep the normal vision/shading/compositor path enabled.
        state.vision.revealMap(state.economy.owner);
        if (!state.vision.hasObserver(state.economy.owner)
            || !state.vision.isCellVisibleTo(state.economy.owner, denseCell.x, denseCell.y)) {
            errorMessage = "dense building benchmark requested cell was not revealed";
            return false;
        }
        invalidateRuntimeObjectProjectionCache(state);
        state.radarTerrainKey.clear();
    }
    constexpr int firstTick = 48;
    const std::vector<std::array<int, 2>> sizes = denseBenchmark
        ? std::vector<std::array<int, 2>>{{1024, 640}, {1920, 1080}, {2256, 1504}}
        : std::vector<std::array<int, 2>>{{1280, 720}, {1920, 1080}, {2256, 1504},
              {2560, 1440}, {3840, 2160}};

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordFullscreenPerformanceSmoke";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = runtimeWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    if (RegisterClassW(&windowClass) == 0) {
        errorMessage = "fullscreen benchmark window class registration failed";
        return false;
    }
    const HWND window = CreateWindowExW(0, className,
        L"Fullscreen rendering benchmark", WS_POPUP,
        0, 0, sizes[0][0], sizes[0][1], nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        UnregisterClassW(className, instance);
        errorMessage = "fullscreen benchmark hidden window creation failed";
        return false;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    struct WindowCleanup {
        HWND window;
        HINSTANCE instance;
        const wchar_t* className;
        RuntimeWindowState& state;
        ~WindowCleanup() {
            // WM_PAINT used the unmodified runtime procedure. Detach only at
            // teardown so its WM_DESTROY does not post WM_QUIT to the caller.
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            SetWindowLongPtrW(window, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(DefWindowProcW));
            DestroyWindow(window);
            UnregisterClassW(className, instance);
            releaseRuntimeBackBuffer(state.backBuffer);
            releaseRuntimeBackBuffer(state.sidebarStaticBuffer);
            releaseRuntimeBackBuffer(state.radarTerrainBuffer);
        }
    } cleanup{window, instance, className, state};

    // All times are CPU/GDI work in a hidden window, not display-present FPS.
    // Cached paints do not refresh map stats, so explicitly record zero work.
    constexpr std::size_t metricCount = 20;
    const std::array<const char*, metricCount> metricNames{{
        "paint", "dispatchFlush", "map", "terrain", "terrainBlit",
        "objects", "fog", "sidebar", "interaction", "terrainObjects",
        "animation", "lighting", "effects", "structures", "sort", "objectOverlay",
        "localLight", "shpBytesMiss", "shpFrameMiss", "voxels"
    }};
    struct Group {
        int count = 0;
        std::array<double, metricCount> total{};
        std::vector<double> paintTimes;
        int fogHits = 0;
        int fogMisses = 0;
        int terrainHits = 0;
        int terrainMisses = 0;
        int visibleTerrain = 0;
        int visibleObjects = 0;
        int visibleStructures = 0;
        std::uint64_t hash = 14695981039346656037ULL;
    };
    const auto appendHash = [](std::uint64_t hash, std::uint64_t value) {
        return (hash ^ value) * 1099511628211ULL;
    };
    const auto framebufferHash = [&]() {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto* pixels = static_cast<const std::uint32_t*>(state.backBuffer.pixels);
        const std::size_t count = static_cast<std::size_t>(state.backBuffer.width)
            * static_cast<std::size_t>(state.backBuffer.height);
        for (std::size_t index = 0; index < count; ++index) {
            // GDI does not define the unused high byte of a BI_RGB pixel.
            hash = appendHash(hash, pixels[index] & 0x00ffffffU);
        }
        return hash;
    };
    const auto checkBuffer = [&](int width, int height) {
        RECT actual{};
        DIBSECTION dib{};
        if (!GetClientRect(window, &actual) || IsWindowVisible(window)
            || actual.right != width || actual.bottom != height
            || state.backBuffer.width != width || state.backBuffer.height != height
            || state.backBuffer.memoryDc == nullptr || state.backBuffer.pixels == nullptr
            || GetObjectW(state.backBuffer.bitmap, sizeof(dib), &dib) != sizeof(dib)
            || dib.dsBm.bmWidth != width || dib.dsBm.bmHeight != height
            || dib.dsBm.bmWidthBytes != width * 4 || dib.dsBm.bmBitsPixel != 32
            || dib.dsBm.bmBits != state.backBuffer.pixels) {
            errorMessage = "hidden client/DIB dimensions or pixel storage are invalid";
            return false;
        }
        const auto& fog = state.renderCache.visionOverlay;
        if (fog.valid) {
            const std::size_t count = static_cast<std::size_t>(width) * height;
            if (fog.surfaceWidth != width || fog.surfaceHeight != height) {
                errorMessage = "fog cache retained a stale surface size";
                return false;
            }
            for (const std::uint64_t pixel : fog.encodedPixels) {
                if ((pixel >> 32U) >= count) {
                    errorMessage = "fog cache pixel offset exceeds the resized DIB";
                    return false;
                }
            }
        }
        return true;
    };
    const auto paint = [&](int width, int height, bool full, Group& group,
                           std::uint64_t& hash, const char* stage) {
        const int renderedBefore = state.renderedFramesThisSecond;
        const int paintedBefore = state.paintedFramesThisSecond;
        InvalidateRect(window, nullptr, FALSE);
        const auto start = std::chrono::steady_clock::now();
        SendMessageW(window, WM_PAINT, 0, 0);
        GdiFlush();
        const double dispatchMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (state.paintedFramesThisSecond != paintedBefore + 1
            || state.renderedFramesThisSecond != renderedBefore + (full ? 1 : 0)
            || (full && !state.renderStats.rendered)
            || !checkBuffer(width, height)) {
            if (errorMessage.empty()) {
                errorMessage = "production WM_PAINT did not take the expected full/cached path";
            }
            return false;
        }
        const auto& stats = state.renderStats;
        if (denseBenchmark && full
            && (stats.visibleWorldObjects <= 0 || stats.visibleStructureSprites <= 0)) {
            errorMessage = "dense building benchmark rendered no building sprites at "
                + std::to_string(width) + "x" + std::to_string(height)
                + " stage " + stage + " tick " + std::to_string(state.tick);
            return false;
        }
        const std::array<double, metricCount> values{{
            state.perf.lastPaintMs, dispatchMs,
            full ? state.perf.lastMapDrawMs : 0.0,
            full ? stats.terrainMs : 0.0,
            full ? stats.terrainBlitMs : 0.0,
            full ? stats.worldObjectsMs : 0.0,
            full ? stats.visionMs : 0.0,
            full ? state.perf.lastSidebarDrawMs : 0.0,
            full ? state.perf.lastOverlayDrawMs : 0.0,
            full ? stats.terrainObjectsMs : 0.0,
            full ? stats.animationMs : 0.0,
            full ? stats.lightingMs : 0.0,
            full ? stats.projectileEffectMs : 0.0,
            full ? stats.structureSpriteMs : 0.0,
            full ? stats.worldObjectSortMs : 0.0,
            full ? stats.objectOverlayMs : 0.0,
            full ? stats.localLightMs : 0.0,
            full ? stats.auxiliaryShpBytesCacheMissMs : 0.0,
            full ? stats.auxiliaryShpFrameCacheMissMs : 0.0,
            full ? stats.voxelSpriteMs : 0.0
        }};
        ++group.count;
        group.paintTimes.push_back(values[0]);
        for (std::size_t index = 0; index < metricCount; ++index) {
            group.total[index] += values[index];
        }
        if (full) {
            group.fogHits += stats.visionOverlayCacheHits;
            group.fogMisses += stats.visionOverlayCacheMisses;
            group.terrainHits += stats.terrainLayerCacheHits;
            group.terrainMisses += stats.terrainLayerCacheMisses;
            group.visibleTerrain = stats.visibleTerrainCells;
            group.visibleObjects = stats.visibleWorldObjects;
            group.visibleStructures = stats.visibleStructureSprites;
        }
        // Validation and hashing intentionally lie outside timed paint work.
        hash = framebufferHash();
        group.hash = appendHash(group.hash, hash);
        if (denseBenchmark) {
            std::cout << "Dense building frame: size=" << width << 'x' << height
                      << " stage=" << stage << " sample=" << group.count
                      << " tick=" << state.tick
                      << " camera=" << state.camera.worldOffsetX << ',' << state.camera.worldOffsetY
                      << " hover=" << (state.mouseClientPointValid ? "yes" : "no")
                      << " selected=" << state.selectedObjectIds.size()
                      << " visibleObjects=" << stats.visibleWorldObjects
                      << " visibleStructures=" << stats.visibleStructureSprites
                      << " framebufferHash=" << hash << '\n';
        }
        return true;
    };
    const auto print = [&](const char* name, int width, int height, Group& group) {
        std::sort(group.paintTimes.begin(), group.paintTimes.end());
        std::ostringstream line;
        line << std::fixed << std::setprecision(3)
             << "Fullscreen render benchmark: size=" << width << 'x' << height
             << " stage=" << name << " samples=" << group.count;
        for (std::size_t index = 0; index < metricCount; ++index) {
            line << ' ' << metricNames[index] << "MeanMs="
                 << group.total[index] / group.count;
        }
        line << " paintMedianMs=" << group.paintTimes[group.paintTimes.size() / 2]
             << " paintMaxMs=" << group.paintTimes.back()
             << " fogHits=" << group.fogHits << " fogMisses=" << group.fogMisses
             << " terrainHits=" << group.terrainHits << " terrainMisses=" << group.terrainMisses
             << " visibleTerrain=" << group.visibleTerrain
             << " visibleObjects=" << group.visibleObjects
             << " visibleStructures=" << group.visibleStructures
             << " framebufferSequenceHash=" << group.hash << '\n';
        std::cout << line.str();
    };

    std::cout << "Fullscreen render benchmark: hiddenWM_PAINT=yes displayFpsMeasured=no"
              << " simulationAdvanced=no hud=no vision=yes samples=" << samples
              << " worldObjects=" << state.world.getObjects().size()
              << " owner=" << state.economy.owner << '\n';
    if (denseBenchmark) {
        std::cout << "Dense building benchmark: cell=" << denseCell.x << ',' << denseCell.y
                  << " samples=" << samples << " vision=yes reveal=diagnostic-full-map"
                  << " seeHiddenObjects=" << (state.config.seeHiddenObjects ? "yes" : "no")
                  << " worldUnchangedRequired=yes\n";
    }
    RuntimeMapCamera originalCamera;
    std::uint64_t originalHash = 0;
    for (std::size_t sizeIndex = 0; sizeIndex < sizes.size(); ++sizeIndex) {
        const int width = sizes[sizeIndex][0];
        const int height = sizes[sizeIndex][1];
        if (!SetWindowPos(window, nullptr, 0, 0, width, height,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
            errorMessage = "hidden benchmark window resize failed";
            return false;
        }
        if (denseBenchmark) {
            centerRuntimeCameraOnCell(state, RECT{0, 0, width, height}, denseCell);
            invalidateRuntimeObjectProjectionCache(state);
        }
        // Deliberately leave repaintRequested false: a size mismatch must
        // invalidate cached-frame reuse without a simulation/input tick.
        state.tick = firstTick;
        state.repaintRequested = false;
        Group cold, warm, rebuild, cached;
        std::uint64_t hash = 0;
        if (!paint(width, height, true, cold, hash, "resize-cold")) return false;
        print("resize-cold", width, height, cold);
        for (int sample = 0; sample < samples; ++sample) {
            state.tick = firstTick + sample + 1;
            state.repaintRequested = true;
            if (!paint(width, height, true, warm, hash, "animation-warm")) return false;
            const std::uint64_t warmHash = hash;
            // Rebuild the same view at the same animation tick. This exercises
            // the uncached fog compositor without changing visibility/gameplay.
            state.renderCache.visionOverlay.valid = false;
            state.repaintRequested = true;
            if (!paint(width, height, true, rebuild, hash, "vision-rebuild")) return false;
            if (hash != warmHash) {
                errorMessage = "warm/rebuilt fog framebuffers differ at "
                    + std::to_string(width) + "x" + std::to_string(height)
                    + " tick " + std::to_string(state.tick);
                return false;
            }
        }
        print("animation-warm", width, height, warm);
        print("vision-rebuild", width, height, rebuild);
        const std::uint64_t fullHash = hash;
        for (int sample = 0; sample < samples; ++sample) {
            if (!paint(width, height, false, cached, hash, "cached-frame")) return false;
            if (hash != fullHash) {
                errorMessage = "cached WM_PAINT altered framebuffer pixels";
                return false;
            }
        }
        print("cached-frame", width, height, cached);
        if (denseBenchmark) {
            const RuntimeMapCamera stationaryCamera = state.camera;
            const RECT viewport = runtimeMapViewport(RECT{0, 0, width, height});
            Group panning, panReturn, hover, hoverReturn;
            for (int sample = 0; sample < samples; ++sample) {
                // Repeatable out-and-back camera movement; no simulation or
                // input command changes the scene during the measurement.
                const int phase = sample % 16;
                const int offset = phase < 8 ? phase + 1 : 16 - phase;
                state.camera = stationaryCamera;
                state.camera.worldOffsetX += offset * 24;
                state.camera.worldOffsetY += offset * 12;
                clampRuntimeMapCamera(state, viewport);
                invalidateRuntimeObjectProjectionCache(state);
                state.tick = firstTick + sample + 1;
                state.repaintRequested = true;
                if (!paint(width, height, true, panning, hash, "camera-pan")) return false;
            }
            print("camera-pan", width, height, panning);
            state.camera = stationaryCamera;
            invalidateRuntimeObjectProjectionCache(state);
            state.repaintRequested = true;
            if (!paint(width, height, true, panReturn, hash, "pan-return")) return false;
            if (hash != fullHash) {
                errorMessage = "dense building camera roundtrip changed stationary pixels";
                return false;
            }
            print("pan-return", width, height, panReturn);
            state.mouseClientPointValid = true;
            state.mouseClientPoint = {viewport.left + (viewport.right - viewport.left) / 2,
                viewport.top + (viewport.bottom - viewport.top) / 2};
            for (int sample = 0; sample < samples; ++sample) {
                state.tick = firstTick + sample + 1;
                state.repaintRequested = true;
                if (!paint(width, height, true, hover, hash, "hover-on")) return false;
            }
            print("hover-on", width, height, hover);
            state.mouseClientPointValid = false;
            state.repaintRequested = true;
            if (!paint(width, height, true, hoverReturn, hash, "hover-return")) return false;
            if (hash != fullHash) {
                errorMessage = "dense building hover roundtrip changed stationary pixels";
                return false;
            }
            print("hover-return", width, height, hoverReturn);
        }
        if (sizeIndex == 0) {
            originalCamera = state.camera;
            originalHash = fullHash;
        }
    }
    state.camera = originalCamera;
    invalidateRuntimeObjectProjectionCache(state);
    state.repaintRequested = false;
    if (!SetWindowPos(window, nullptr, 0, 0, sizes[0][0], sizes[0][1],
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
        errorMessage = "hidden benchmark resize back failed";
        return false;
    }
    Group resizedBack;
    std::uint64_t resizedHash = 0;
    if (!paint(sizes[0][0], sizes[0][1], true, resizedBack, resizedHash, "resize-back")) return false;
    if (resizedHash != originalHash
        || state.world.deterministicSimulationStateHash() != worldHash) {
        errorMessage = "resize back retained stale pixels or rendering changed world state";
        return false;
    }
    print("resize-back", sizes[0][0], sizes[0][1], resizedBack);
    if (const char* bitmap = std::getenv("CONCORD_FULLSCREEN_RENDER_BMP")) {
        if (!writeRuntimeSmokeBmp(bitmap, state.backBuffer.pixels,
                state.backBuffer.width, state.backBuffer.height, errorMessage)) return false;
    }
    std::cout << "Fullscreen render contract: warmEqualsRebuild=yes cachedEqualsFull=yes"
              << " resizeRoundtrip=yes dibDimensions=yes fogOffsetsInBounds=yes"
              << " worldUnchanged=yes framebufferHash=" << resizedHash << '\n';
    return true;
}
