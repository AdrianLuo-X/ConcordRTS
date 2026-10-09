// A separate diagnostic switch keeps all earlier movement/rendering optimizations
// enabled when comparing this change. Tests can select either path in one process.
static thread_local int runtimeFrameScalingReferenceOverride = -1;

static bool runtimeFrameScalingReferenceForRender() {
    if (runtimeFrameScalingReferenceOverride >= 0)
        return runtimeFrameScalingReferenceOverride != 0;
    static const bool enabled = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

struct RuntimeFrameScalingReferenceScope {
    int previous = runtimeFrameScalingReferenceOverride;
    explicit RuntimeFrameScalingReferenceScope(bool reference) {
        runtimeFrameScalingReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeFrameScalingReferenceScope() { runtimeFrameScalingReferenceOverride = previous; }
};

static thread_local int runtimeShroudPerformanceReferenceOverride = -1;

static bool runtimeShroudPerformanceReference() {
    if (runtimeShroudPerformanceReferenceOverride >= 0)
        return runtimeShroudPerformanceReferenceOverride != 0;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_SHROUD_PERFORMANCE_REFERENCE");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

struct RuntimeShroudPerformanceReferenceScope {
    int previous = runtimeShroudPerformanceReferenceOverride;
    explicit RuntimeShroudPerformanceReferenceScope(bool reference) {
        runtimeShroudPerformanceReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeShroudPerformanceReferenceScope() {
        runtimeShroudPerformanceReferenceOverride = previous;
    }
};

static void runtimeApplyVisionEncodedPixels(const std::vector<std::uint64_t>& encoded,
    std::uint32_t* pixels) {
    for (const std::uint64_t value : encoded) {
        const std::uint32_t offset = static_cast<std::uint32_t>(value >> 32U);
        const std::uint32_t effect = static_cast<std::uint32_t>(value);
        const int opacity = static_cast<int>(effect >> 24U);
        const std::uint32_t tint = effect & 0x00ffffffU;
        pixels[offset] = opacity == 255 ? tint
            : runtimeBlendVisionShroud(pixels[offset], tint, opacity);
    }
}

static void runtimeBuildVisionOpaqueRuns(RuntimeVisionOverlayTile& tile) {
    tile.opaqueRuns.clear();
    tile.opaqueRunPixels = 0;
    // Release excessive allocation slack after heavy exploration. A 64x64
    // tile can retain at most 512 runs; no per-frame allocation occurs here.
    const std::size_t maximumRuns = tile.encodedPixels.size() / 8U;
    if (tile.opaqueRuns.capacity() > maximumRuns * 2U + 1U) {
        std::vector<RuntimeVisionOpaqueRun> bounded;
        bounded.reserve(maximumRuns);
        tile.opaqueRuns.swap(bounded);
    }
    const auto& encoded = tile.encodedPixels;
    for (std::size_t begin = 0; begin < encoded.size();) {
        const std::uint32_t effect = static_cast<std::uint32_t>(encoded[begin]);
        if ((effect >> 24U) != 255U) { ++begin; continue; }
        const std::uint32_t offset = static_cast<std::uint32_t>(encoded[begin] >> 32U);
        std::size_t end = begin + 1;
        while (end < encoded.size() && static_cast<std::uint32_t>(encoded[end]) == effect
            && static_cast<std::uint32_t>(encoded[end] >> 32U)
                == static_cast<std::uint64_t>(offset) + end - begin) ++end;
        if (end - begin >= 8U) {
            tile.opaqueRuns.push_back({static_cast<std::uint32_t>(begin), offset,
                static_cast<std::uint32_t>(end - begin), effect & 0x00ffffffU});
            tile.opaqueRunPixels += end - begin;
        }
        begin = end;
    }
}

static void runtimeApplyVisionTilePixels(const RuntimeVisionOverlayTile& tile,
    std::uint32_t* pixels, RuntimeMapRenderStats& stats, bool reference) {
    stats.visionOpaqueRunCacheBytes += tile.opaqueRuns.capacity() * sizeof(RuntimeVisionOpaqueRun);
    if (reference || tile.opaqueRuns.empty()) {
        runtimeApplyVisionEncodedPixels(tile.encodedPixels, pixels);
        return;
    }
    stats.visionOpaqueRunPixels += tile.opaqueRunPixels;
    std::size_t cursor = 0;
    const auto applyUntil = [&](std::size_t end) {
        while (cursor < end) {
            const std::uint64_t value = tile.encodedPixels[cursor++];
            const std::uint32_t effect = static_cast<std::uint32_t>(value);
            auto& pixel = pixels[static_cast<std::uint32_t>(value >> 32U)];
            const int opacity = static_cast<int>(effect >> 24U);
            const std::uint32_t tint = effect & 0x00ffffffU;
            pixel = opacity == 255 ? tint : runtimeBlendVisionShroud(pixel, tint, opacity);
        }
    };
    for (const auto& run : tile.opaqueRuns) {
        applyUntil(run.encodedBegin);
        std::fill_n(pixels + run.surfaceOffset, run.length, run.tint);
        cursor = static_cast<std::size_t>(run.encodedBegin) + run.length;
    }
    applyUntil(tile.encodedPixels.size());
}

static bool runtimeVisionTileCellShrouded(const RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < cache.shroudedCellMask.size()
        ? cache.shroudedCellMask[static_cast<std::size_t>(index)] != 0
        : view.visibilityAtIndex(index) == RuntimeCellVisibility::Shrouded;
}

static bool runtimeVisionTileHasOwnership(const RuntimeTerrainLayerCache& layer) {
    return layer.built && layer.width > 0 && layer.height > 0
        && layer.terrainCellIndices.size() == layer.pixels.size()
        && layer.terrainCellIndices.size()
            >= static_cast<std::size_t>(layer.width) * layer.height;
}

static int runtimeVisionTileOwnerAt(const RuntimeTerrainLayerCache& layer,
    const RECT& viewport, const RuntimeMapCamera& camera, int x, int y) {
    const std::int64_t layerX = static_cast<std::int64_t>(x) - viewport.left
        - layer.originX + camera.worldOffsetX;
    const std::int64_t layerY = static_cast<std::int64_t>(y) - viewport.top
        - layer.originY + camera.worldOffsetY;
    if (layerX < 0 || layerY < 0 || layerX >= layer.width || layerY >= layer.height)
        return -1;
    return layer.terrainCellIndices[static_cast<std::size_t>(layerY) * layer.width
        + static_cast<std::size_t>(layerX)];
}

static void runtimeRebuildVisionTileMask(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const RECT& viewport, const RuntimeMapCamera& camera, int stride, int tileIndex) {
    auto& incremental = cache.incremental;
    const auto& tile = incremental.tiles[static_cast<std::size_t>(tileIndex)];
    for (int y = tile.top; y < tile.bottom; ++y) {
        auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
        std::fill(row + tile.left, row + tile.right, 0);
    }
    const int halfWidth = std::max(1, camera.tileWidth / 2);
    const int halfHeight = std::max(1, camera.tileHeight / 2);
    for (const auto& cell : tile.geometricCells) {
        if (!runtimeVisionTileCellShrouded(cache, view, cell.cellIndex)) continue;
        const int top = std::max(tile.top, cell.centerY - halfHeight);
        const int bottom = std::min(tile.bottom, cell.centerY + halfHeight + 1);
        for (int y = top; y < bottom; ++y) {
            const int remaining = halfHeight - std::abs(y - cell.centerY);
            const int extent = (halfWidth * remaining + halfHeight - 1) / halfHeight;
            const int left = std::max(tile.left, cell.centerX - extent);
            const int right = std::min(tile.right, cell.centerX + extent + 1);
            if (left < right) {
                auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
                std::fill(row + left, row + right, 254);
            }
        }
    }
    if (runtimeVisionTileHasOwnership(layer)) {
        for (int y = tile.top; y < tile.bottom; ++y) {
            auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
            for (int x = tile.left; x < tile.right; ++x) {
                const int owner = runtimeVisionTileOwnerAt(layer, viewport, camera, x, y);
                if (owner >= 0 && static_cast<std::size_t>(owner) < view.cellCount
                    && runtimeVisionTileCellShrouded(cache, view, owner)) row[x] = 255;
            }
        }
    }
    incremental.rebuiltMaskPixels += static_cast<std::uint64_t>(tile.right - tile.left)
        * (tile.bottom - tile.top);
}

// Recompute exact nine-by-nine feathering for one output tile. Its input halo
// is clamped to the original viewport, never to the tile. Both divisions retain
// the original +4 integer rounding, including at viewport corners.
static void runtimeRebuildVisionTileEffects(RuntimeVisionOverlayCache& cache,
    const RECT& viewport, const RuntimeMapCamera& camera, int stride, int tileIndex) {
    auto& incremental = cache.incremental;
    auto& tile = incremental.tiles[static_cast<std::size_t>(tileIndex)];
    constexpr int side = RuntimeVisionIncrementalOverlayCache::tileSize;
    std::array<unsigned char, side * (side + 8)> horizontal{};
    std::array<unsigned char, side * side> softened{};
    bool needsBlur = false;
    for (int y = tile.top; y < tile.bottom && !needsBlur; ++y) {
        const auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
        needsBlur = std::find(row + tile.left, row + tile.right, 254) != row + tile.right;
    }
    const int width = tile.right - tile.left;
    const int height = tile.bottom - tile.top;
    if (needsBlur) {
        for (int localY = 0; localY < height + 8; ++localY) {
            const int y = std::clamp(tile.top + localY - 4, incremental.top, incremental.bottom - 1);
            const auto* input = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
            auto* output = horizontal.data() + localY * side;
            int sum = 0;
            for (int dx = -4; dx <= 4; ++dx)
                sum += input[std::clamp(tile.left + dx, incremental.left, incremental.right - 1)];
            for (int localX = 0; localX < width; ++localX) {
                const int x = tile.left + localX;
                output[localX] = static_cast<unsigned char>((sum + 4) / 9);
                sum += static_cast<int>(input[std::min(x + 5, incremental.right - 1)])
                    - input[std::max(x - 4, incremental.left)];
            }
        }
        std::array<int, side> sums{};
        for (int row = 0; row < 9; ++row) for (int x = 0; x < width; ++x)
            sums[x] += horizontal[row * side + x];
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            softened[y * side + x] = static_cast<unsigned char>((sums[x] + 4) / 9);
            if (y + 1 < height) sums[x] += static_cast<int>(horizontal[(y + 9) * side + x])
                - horizontal[y * side + x];
        }
    }
    incremental.encodedPixelCount -= tile.encodedPixels.size();
    tile.encodedPixels.clear();
    const auto& colors = runtimeVisionShroudColors();
    for (int y = tile.top; y < tile.bottom; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * stride;
        const int worldY = static_cast<int>((static_cast<std::int64_t>(y)
            - viewport.top + camera.worldOffsetY) & 127);
        const auto* colorRow = colors.data() + static_cast<std::size_t>(worldY) * 64;
        for (int x = tile.left; x < tile.right; ++x) {
            const std::size_t offset = row + x;
            const unsigned char marker = incremental.mask[offset];
            if (marker == 0) continue;
            const int blur = softened[(y - tile.top) * side + x - tile.left];
            const bool solid = marker == 255 || blur >= 220;
            const int worldX = static_cast<int>((static_cast<std::int64_t>(x)
                - viewport.left + camera.worldOffsetX) & 63);
            const auto& color = colorRow[worldX];
            const int opacity = solid ? 255 : std::clamp(blur + color.mottle, 0, 250);
            const std::uint32_t effect = (static_cast<std::uint32_t>(opacity) << 24U) | color.color;
            tile.encodedPixels.push_back((static_cast<std::uint64_t>(offset) << 32U) | effect);
        }
    }
    incremental.encodedPixelCount += tile.encodedPixels.size();
    if (!runtimeShroudPerformanceReference()) runtimeBuildVisionOpaqueRuns(tile);
    else { tile.opaqueRuns.clear(); tile.opaqueRunPixels = 0; }
    incremental.rebuiltEffectPixels += static_cast<std::uint64_t>(width) * height;
}

static bool runtimeBuildIncrementalVisionOverlay(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const GameWorld& world, const RECT& viewport, const RuntimeMapCamera& camera,
    int width, int height, const std::vector<unsigned char>& initialMask) {
    auto& incremental = cache.incremental;
    incremental = {};
    incremental.left = std::max(0, static_cast<int>(viewport.left));
    incremental.top = std::max(0, static_cast<int>(viewport.top));
    incremental.right = std::min(width, static_cast<int>(viewport.right));
    incremental.bottom = std::min(height, static_cast<int>(viewport.bottom));
    if (incremental.left >= incremental.right || incremental.top >= incremental.bottom) return false;
    constexpr int side = RuntimeVisionIncrementalOverlayCache::tileSize;
    incremental.columns = 1 + (incremental.right - incremental.left - 1) / side;
    incremental.rows = 1 + (incremental.bottom - incremental.top - 1) / side;
    incremental.mask = initialMask;
    incremental.tiles.resize(static_cast<std::size_t>(incremental.columns) * incremental.rows);
    for (int y = 0; y < incremental.rows; ++y) for (int x = 0; x < incremental.columns; ++x) {
        auto& tile = incremental.tiles[static_cast<std::size_t>(y) * incremental.columns + x];
        tile.left = incremental.left + x * side; tile.top = incremental.top + y * side;
        tile.right = std::min(incremental.right, tile.left + side);
        tile.bottom = std::min(incremental.bottom, tile.top + side);
    }
    const int halfWidth = std::max(1, camera.tileWidth / 2);
    const int halfHeight = std::max(1, camera.tileHeight / 2);
    for (const auto& cell : world.getTerrainCells()) {
        const POINT center = screenPoint(projectCell(cell, camera), viewport, camera);
        const int left = std::max(incremental.left, static_cast<int>(center.x) - halfWidth);
        const int right = std::min(incremental.right, static_cast<int>(center.x) + halfWidth + 1);
        const int top = std::max(incremental.top, static_cast<int>(center.y) - halfHeight);
        const int bottom = std::min(incremental.bottom, static_cast<int>(center.y) + halfHeight + 1);
        if (left >= right || top >= bottom) continue;
        const int index = cell.cellIndex512 >= 0 ? cell.cellIndex512
            : cell.x >= 0 && cell.y >= 0 && cell.x < 512 && cell.y < 512 ? cell.y * 512 + cell.x : -1;
        for (int ty = (top - incremental.top) / side; ty <= (bottom - 1 - incremental.top) / side; ++ty)
            for (int tx = (left - incremental.left) / side; tx <= (right - 1 - incremental.left) / side; ++tx)
                incremental.tiles[static_cast<std::size_t>(ty) * incremental.columns + tx]
                    .geometricCells.push_back({index, static_cast<int>(center.x), static_cast<int>(center.y)});
    }
    const bool hasOwnership = runtimeVisionTileHasOwnership(layer);
    std::vector<int> dependencies;
    for (std::size_t tileIndex = 0; tileIndex < incremental.tiles.size(); ++tileIndex) {
        const auto& tile = incremental.tiles[tileIndex];
        dependencies.clear();
        for (const auto& cell : tile.geometricCells)
            if (cell.cellIndex >= 0 && static_cast<std::size_t>(cell.cellIndex) < view.cellCount)
                dependencies.push_back(cell.cellIndex);
        if (hasOwnership) for (int y = tile.top; y < tile.bottom; ++y) {
            int previous = -1;
            for (int x = tile.left; x < tile.right; ++x) {
                const int owner = runtimeVisionTileOwnerAt(layer, viewport, camera, x, y);
                if (owner != previous && owner >= 0 && static_cast<std::size_t>(owner) < view.cellCount)
                    dependencies.push_back(owner);
                previous = owner;
            }
        }
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        for (const int index : dependencies)
            incremental.tilesByCell[index].push_back(static_cast<int>(tileIndex));
        runtimeRebuildVisionTileEffects(cache, viewport, camera, width, static_cast<int>(tileIndex));
    }
    incremental.valid = true;
    return true;
}

// All changed mask tiles are rebuilt before any output tile reads its halo.
// Expanding by one tile is conservative for the four-pixel feather radius.
static bool runtimeUpdateIncrementalVisionOverlay(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const RECT& viewport, const RuntimeMapCamera& camera, int stride) {
    auto& incremental = cache.incremental;
    std::vector<unsigned char> dirty(incremental.tiles.size(), 0);
    bool changed = false;
    incremental.rebuiltMaskPixels = incremental.rebuiltEffectPixels = 0;
    for (std::size_t index = 0; index < view.cellCount; ++index) {
        const auto shrouded = static_cast<std::uint8_t>(
            view.visibilityAtIndex(static_cast<int>(index)) == RuntimeCellVisibility::Shrouded);
        if (shrouded == cache.shroudedCellMask[index]) continue;
        cache.shroudedCellMask[index] = shrouded;
        changed = true;
        const auto found = incremental.tilesByCell.find(static_cast<int>(index));
        if (found != incremental.tilesByCell.end()) for (const int tile : found->second)
            dirty[static_cast<std::size_t>(tile)] = 1;
    }
    std::vector<unsigned char> effects(incremental.tiles.size(), 0);
    for (std::size_t index = 0; index < dirty.size(); ++index) {
        if (!dirty[index]) continue;
        runtimeRebuildVisionTileMask(cache, view, layer, viewport, camera, stride, static_cast<int>(index));
        const int x = static_cast<int>(index) % incremental.columns;
        const int y = static_cast<int>(index) / incremental.columns;
        for (int yy = std::max(0, y - 1); yy <= std::min(incremental.rows - 1, y + 1); ++yy)
            for (int xx = std::max(0, x - 1); xx <= std::min(incremental.columns - 1, x + 1); ++xx)
                effects[static_cast<std::size_t>(yy) * incremental.columns + xx] = 1;
    }
    for (std::size_t index = 0; index < effects.size(); ++index)
        if (effects[index]) runtimeRebuildVisionTileEffects(cache, viewport, camera, stride, static_cast<int>(index));
    return changed;
}
