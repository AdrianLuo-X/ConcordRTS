// A frame-local receiver buffer follows the real TMP/SHP/VXL silhouettes.
// Ground cliffs retain their own cell and tall objects take light at their base.
struct RuntimeMapLightFrame {
    RECT bounds{};
    int width = 0, height = 0;
    int drawingReceiver = -1;
    std::vector<RuntimeMapLightSource> sources;
    std::vector<int> pixels;
    std::vector<std::array<std::uint32_t, 256>> lookups;
    RuntimeLightingTint global;
    const MapLightingProfile* profile = nullptr;
    bool referenceMode = false;
    bool profileEnabled = false;
    bool terrainCacheHit = false;
    bool firstTintAvailable = false;
    std::array<std::uint64_t, 3> firstTintBits{};
    std::size_t reusedTables = 0;

    std::string terrainCacheKey(const RECT& viewport, const GameWorld& world,
        const RuntimeMapCamera& camera, const RuntimeMapRenderCache& cache) const {
        // Store exact scalar bytes, not a lossy hash or rounded light value.
        // Serialize fields separately so struct padding never enters the key.
        std::string key;
        const auto append = [&](const auto& value) {
            key.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        append(viewport.left); append(viewport.top);
        append(viewport.right); append(viewport.bottom);
        append(camera.worldOffsetX); append(camera.worldOffsetY);
        append(global.red); append(global.green); append(global.blue);
        append(profile != nullptr);
        if (profile) {
            append(profile->ambient); append(profile->ground);
            append(profile->red); append(profile->green); append(profile->blue);
        }
        append(sources.size());
        for (const auto& light : sources) {
            append(light.objectId); append(light.x); append(light.y);
            append(light.visibility); append(light.intensity);
            append(light.red); append(light.green); append(light.blue);
        }
        const auto& layer = cache.terrainLayer;
        append(layer.built); append(layer.width); append(layer.height);
        append(layer.originX); append(layer.originY);
        append(layer.terrainCellIndices.data()); append(layer.terrainCellIndices.size());
        append(layer.pixels.size());
        append(cache.terrainLayerCacheMisses);
        append(&world); append(world.getTerrainCells().data()); append(world.getTerrainCells().size());
        append(cache.terrainByCellIndex.data()); append(cache.terrainByCellIndex.size());
        key.append(layer.key);
        return key;
    }

    int receiver(double x, double y) {
        const auto tint = runtimeMapLightTintAt(sources, global, profile, x, y);
        const int index = static_cast<int>(lookups.size());
        std::array<std::uint64_t, 3> tintBits{};
        if (!referenceMode) {
            static_assert(sizeof(double) == sizeof(std::uint64_t));
            std::memcpy(&tintBits[0], &tint.red, sizeof(double));
            std::memcpy(&tintBits[1], &tint.green, sizeof(double));
            std::memcpy(&tintBits[2], &tint.blue, sizeof(double));
            if (firstTintAvailable && tintBits == firstTintBits) {
                // Keep the original receiver IDs and diagnostic count. Only
                // reuse the exact table computation, without quantizing tint.
                // A local copy remains valid if push_back grows the vector.
                const auto table = lookups.front();
                lookups.push_back(table);
                if (profileEnabled) ++reusedTables;
                return index;
            }
        }
        auto& table = lookups.emplace_back();
        for (std::uint32_t value = 0; value < 256; ++value)
            table[value] = runtimeApplyLightingTintToPackedDibColor(value * 0x010101U, tint);
        if (!referenceMode && !firstTintAvailable) {
            firstTintBits = tintBits;
            firstTintAvailable = true;
        }
        return index;
    }

    RuntimeMapLightFrame(const RECT& viewport, const GameWorld& world,
        const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache)
        : bounds(viewport),
          global(context.lightingTint ? *context.lightingTint : RuntimeLightingTint{}), profile(context.lightingProfile) {
        const char* reference = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_REFERENCE");
        referenceMode = reference && reference[0] == '1' && reference[1] == '\0';
        const char* profileSetting = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_PROFILE");
        profileEnabled = profileSetting && profileSetting[0] == '1' && profileSetting[1] == '\0';
        const auto started = runtimeMaybeTimingStart(profileEnabled);
        const auto finishProfile = [&] {
            if (!profileEnabled) return;
            const double constructionMs = runtimeMaybeElapsedMs(true, started);
            std::cout << "Dense building map-light profile: reference=" << (referenceMode ? "yes" : "no")
                << " sources=" << sources.size() << " viewportPixels=" << pixels.size()
                << " receiverRequests=" << lookups.size() << " reusedTables=" << reusedTables
                << " builtTables=" << lookups.size() - reusedTables
                << " terrainCacheHit=" << (terrainCacheHit ? "yes" : "no")
                << " constructionMs=" << constructionMs << '\n';
        };
        sources = runtimeMapLightSources(world, context.rules);
        if (sources.empty() || context.effectOnlyProbe) { sources.clear(); finishProfile(); return; }
        width = bounds.right - bounds.left; height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) { sources.clear(); finishProfile(); return; }
        const char* cacheReference = runtimeRendererEnvironmentValue("CONCORD_MAP_LIGHT_CACHE_REFERENCE");
        const bool cacheEnabled = !referenceMode
            && !(cacheReference && cacheReference[0] == '1' && cacheReference[1] == '\0');
        auto& terrainCache = cache.mapLightTerrain;
        const std::string key = cacheEnabled ? terrainCacheKey(viewport, world, camera, cache) : std::string{};
        if (cacheEnabled && terrainCache.valid && terrainCache.key == key) {
            pixels = terrainCache.pixels;
            lookups = terrainCache.lookups;
            firstTintAvailable = terrainCache.firstTintAvailable;
            firstTintBits = terrainCache.firstTintBits;
            reusedTables = terrainCache.reusedTables;
            terrainCacheHit = true;
            finishProfile();
            return;
        }
        const auto retainTerrain = [&] {
            if (cacheEnabled) {
                terrainCache.valid = false;
                terrainCache.key = key;
                terrainCache.pixels = pixels;
                terrainCache.lookups = lookups;
                terrainCache.firstTintAvailable = firstTintAvailable;
                terrainCache.firstTintBits = firstTintBits;
                terrainCache.reusedTables = reusedTables;
                terrainCache.valid = true;
            }
            finishProfile();
        };
        receiver(-1000000, -1000000); // Pixels without a receiver retain the global transform.
        pixels.assign(static_cast<std::size_t>(width) * height, 0);
        const auto& layer = cache.terrainLayer;
        if (!layer.built || layer.terrainCellIndices.size() != layer.pixels.size()) { retainTerrain(); return; }
        const int originX = viewport.left + layer.originX - camera.worldOffsetX;
        const int originY = viewport.top + layer.originY - camera.worldOffsetY;
        std::vector<int> cellReceivers(cache.terrainByCellIndex.size(), -1);
        for (int y = std::max(static_cast<int>(bounds.top), originY);
            y < std::min(static_cast<int>(bounds.bottom), originY + layer.height); ++y) {
            for (int x = std::max(static_cast<int>(bounds.left), originX);
                x < std::min(static_cast<int>(bounds.right), originX + layer.width); ++x) {
                const int cellIndex = layer.terrainCellIndices[static_cast<std::size_t>(y - originY) * layer.width + x - originX];
                if (cellIndex < 0 || static_cast<std::size_t>(cellIndex) >= cellReceivers.size()) continue;
                auto& entry = cellReceivers[cellIndex];
                if (entry < 0) {
                    const auto* cell = cache.terrainByCellIndex[cellIndex];
                    entry = cell ? receiver(cell->x, cell->y) : 0;
                }
                pixels[static_cast<std::size_t>(y - bounds.top) * width + x - bounds.left] = entry;
            }
        }
        retainTerrain();
    }

    std::uint32_t transform(int x, int y, std::uint32_t pixel) const {
        const auto& table = lookups[pixels[static_cast<std::size_t>(y - bounds.top) * width + x - bounds.left]];
        return (pixel & 0xff000000U) | (table[(pixel >> 16U) & 255U] & 0xff0000U)
            | (table[(pixel >> 8U) & 255U] & 0xff00U) | (table[pixel & 255U] & 255U);
    }
};

thread_local RuntimeMapLightFrame* runtimeMapLightFrame = nullptr;
struct RuntimeMapLightFrameScope {
    RuntimeMapLightFrame* previous = runtimeMapLightFrame;
    explicit RuntimeMapLightFrameScope(RuntimeMapLightFrame& frame) {
        runtimeMapLightFrame = frame.sources.empty() ? nullptr : &frame;
    }
    ~RuntimeMapLightFrameScope() { runtimeMapLightFrame = previous; }
};
struct RuntimeMapLightObjectScope {
    RuntimeMapLightFrame* frame = runtimeMapLightFrame;
    int previous = -1;
    RuntimeMapLightObjectScope(double x, double y) {
        if (frame) { previous = frame->drawingReceiver; frame->drawingReceiver = frame->receiver(x, y); }
    }
    ~RuntimeMapLightObjectScope() { if (frame) frame->drawingReceiver = previous; }
};

void recordRuntimeMapLightShpFrame(const RECT& rect, const RECT& viewport,
    const RuntimeShpFrameImage& image, int opacity) {
    auto* frame = runtimeMapLightFrame;
    if (!frame || frame->drawingReceiver < 0 || opacity < 128) return;
    RECT clipped{};
    if (!IntersectRect(&clipped, &rect, &viewport) || !IntersectRect(&clipped, &clipped, &frame->bounds)) return;
    for (int y = clipped.top; y < clipped.bottom; ++y) for (int x = clipped.left; x < clipped.right; ++x)
        if (image.pixels[static_cast<std::size_t>(y - rect.top) * image.width + x - rect.left] != 0)
            frame->pixels[static_cast<std::size_t>(y - frame->bounds.top) * frame->width + x - frame->bounds.left]
                = frame->drawingReceiver;
}

int applyRuntimeMapLights(HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapLightFrame& frame) {
    if (frame.sources.empty()) return applyRuntimeLightingTintToViewport(dc, viewport, context);
    int changed = 0;
    for (int y = std::max(0L, viewport.top); y < viewport.bottom; ++y) {
        if (hasPixelSurface(context) && y >= context.surfaceHeight) break;
        for (int x = std::max(0L, viewport.left); x < viewport.right; ++x) {
            if (hasPixelSurface(context)) {
                if (x >= context.surfaceWidth) break;
                auto& pixel = context.surfacePixels[static_cast<std::size_t>(y) * context.surfaceWidth + x];
                pixel = frame.transform(x, y, pixel); ++changed;
            } else if (dc) {
                const COLORREF color = GetPixel(dc, x, y);
                if (color == CLR_INVALID) continue;
                const auto packed = (static_cast<std::uint32_t>(GetRValue(color)) << 16U)
                    | (static_cast<std::uint32_t>(GetGValue(color)) << 8U) | GetBValue(color);
                const auto lit = frame.transform(x, y, packed);
                SetPixelV(dc, x, y, RGB((lit >> 16U) & 255U, (lit >> 8U) & 255U, lit & 255U)); ++changed;
            }
        }
    }
    return changed;
}
