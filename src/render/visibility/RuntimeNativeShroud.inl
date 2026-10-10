// Retail YR 1.001: TacticalClass::GetShroudFrame at 0x6D8700 uses the
// eight-neighbour table at 0x7F4194. CellClass maps -1/-2 to SHROUD frames 0/15
// at 0x48020D. These are rendering neighbours, not extra explored cells.
static constexpr std::array<signed char, 256> runtimeNativeShroudFrames{{
    -1,33,2,2,34,37,2,2,4,26,6,6,4,26,6,6,
    35,45,17,17,38,41,17,17,4,26,6,6,4,26,6,6,
    8,21,10,10,27,31,10,10,12,23,14,14,12,23,14,14,
    8,21,10,10,27,31,10,10,12,23,14,14,12,23,14,14,
    32,36,25,25,44,40,25,25,19,30,20,20,19,30,20,20,
    39,43,29,29,42,46,29,29,19,30,20,20,19,30,20,20,
    8,21,10,10,27,31,10,10,12,23,14,14,12,23,14,14,
    8,21,10,10,27,31,10,10,12,23,14,14,12,23,14,14,
    1,1,3,3,16,16,3,3,5,5,7,7,5,5,7,7,
    24,24,18,18,28,28,18,18,5,5,7,7,5,5,7,7,
    9,9,11,11,22,22,11,11,13,13,-2,-2,13,13,-2,-2,
    9,9,11,11,22,22,11,11,13,13,-2,-2,13,13,-2,-2,
    1,1,3,3,16,16,3,3,5,5,7,7,5,5,7,7,
    24,24,18,18,28,28,18,18,5,5,7,7,5,5,7,7,
    9,9,11,11,22,22,11,11,13,13,-2,-2,13,13,-2,-2,
    9,9,11,11,22,22,11,11,13,13,-2,-2,13,13,-2,-2
}};

static constexpr std::array<std::pair<int, int>, 8> runtimeNativeShroudNeighbours{{
    {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}
}};

static int runtimeNativeShroudProjectedNeighbour(const RuntimeVisionIncrementalOverlayCache& grid,
    int index, int dx, int dy) {
    const int x = index % grid.nativeColumns + dx, y = index / grid.nativeColumns + dy;
    return x >= 0 && y >= 0 && x < grid.nativeColumns && y < grid.nativeRows
        ? y * grid.nativeColumns + x : -1;
}

static int runtimeNativeShroudFrame(const RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, int index) {
    const auto& grid = cache.incremental;
    const auto shrouded = [&](int projected) {
        if (projected < 0 || static_cast<std::size_t>(projected) >= grid.nativeSources.size()) return true;
        for (int source : grid.nativeSources[projected])
            if (!runtimeVisionTileCellShrouded(cache, view, source)) return false;
        return true;
    };
    if (shrouded(index)) return 15;
    unsigned int bits = 0;
    for (unsigned int n = 0; n < runtimeNativeShroudNeighbours.size(); ++n) {
        const auto [dx, dy] = runtimeNativeShroudNeighbours[n];
        if (shrouded(runtimeNativeShroudProjectedNeighbour(grid, index, dx, dy))) bits |= 1U << n;
    }
    const int frame = runtimeNativeShroudFrames[bits];
    return frame == -2 ? 15 : frame == -1 ? 0 : frame;
}

static bool runtimeLoadNativeShroud(const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    // Asset-free diagnostic worlds retain the conservative procedural fallback.
    if (context.assetRootPath.empty()) return false;
    auto& atlas = cache.nativeShroudAtlas;
    const std::string key = context.assetRootPath + "|" + context.structureRootPath;
    if (atlas.key == key && atlas.attempted) return atlas.loaded;
    atlas = {}; atlas.key = key; atlas.attempted = true;
    atlas.alpha.resize(48 * 60 * 30);
    for (int frame = 0; frame < 48; ++frame) {
        const auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache("SHROUD", frame,
            context, cache, false);
        if (!sprite || !sprite->loaded) return false;
        const auto& image = sprite->image;
        if (image.globalWidth != 60 || image.globalHeight != 30
            || image.offsetX != 0 || image.offsetY != 0
            || image.width != 60 || image.height != 30 || image.pixels.size() != 1800)
            return false;
        for (std::size_t p = 0; p < image.pixels.size(); ++p) {
            const unsigned int sample = image.pixels[p];
            // SHROUD is an intensity image, not a theatre palette sprite.
            // 254 skips the pixel (0x47F1B7); 2/127 are its black/clear ends.
            const unsigned int keep = sample >= 127 ? 255 : sample <= 2 ? 0
                : ((sample - 2) * 255 + 62) / 125;
            atlas.alpha[frame * 1800 + p] = static_cast<unsigned char>(255 - keep);
        }
        atlas.sourcePath = sprite->sourcePath;
        atlas.fingerprint = sprite->sourceFingerprint;
    }
    atlas.loaded = true;
    return true;
}

static void runtimeRebuildNativeShroudTile(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const RuntimeNativeShroudAtlas& atlas, const RECT& viewport,
    const RuntimeMapCamera& camera, int stride, int tileIndex) {
    auto& incremental = cache.incremental;
    auto& tile = incremental.tiles[static_cast<std::size_t>(tileIndex)];
    for (int y = tile.top; y < tile.bottom; ++y) {
        auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
        std::fill(row + tile.left, row + tile.right, 0);
    }
    const int width = std::max(1, camera.tileWidth), height = std::max(1, camera.tileHeight);
    for (const auto& cell : tile.geometricCells) {
        const int frame = runtimeNativeShroudFrame(cache, view, cell.cellIndex);
        if (frame == 0) continue;
        const auto* alpha = atlas.alpha.data() + frame * 1800;
        const int originX = cell.centerX - width / 2, originY = cell.centerY - height / 2;
        const int left = std::max(tile.left, originX), right = std::min(tile.right, originX + width);
        const int top = std::max(tile.top, originY), bottom = std::min(tile.bottom, originY + height);
        for (int y = top; y < bottom; ++y) {
            auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
            const auto* source = alpha + ((y - originY) * 30 / height) * 60;
            for (int x = left; x < right; ++x)
                row[x] = std::max(row[x], source[(x - originX) * 60 / width]);
        }
    }
    // Cliff faces and TMP overhangs can extend outside their base diamonds.
    // Unknown terrain stays concealed; this does not replace the native fade
    // inside the adjoining explored cells.
    const bool mergeOwnershipEncoding = runtimeVisionTileHasOwnership(layer)
        && !runtimeShroudPerformanceReference()
        && !runtimeMovementReferenceForRender()
        && !runtimeFrameScalingReferenceForRender();
    if (mergeOwnershipEncoding) {
        incremental.encodedPixelCount -= tile.encodedPixels.size();
        tile.encodedPixels.clear();
        for (int y = tile.top; y < tile.bottom; ++y) {
            auto* row = incremental.mask.data() + static_cast<std::size_t>(y) * stride;
            for (int x = tile.left; x < tile.right; ++x) {
                const std::size_t offset = static_cast<std::size_t>(y) * stride + x;
                const int owner = runtimeVisionTileOwnerAt(layer, viewport, camera, x, y);
                if (owner >= 0 && static_cast<std::size_t>(owner) < view.cellCount
                    && runtimeVisionTileCellShrouded(cache, view, owner)) row[x] = 255;
                const unsigned int alpha = row[x];
                if (alpha) tile.encodedPixels.push_back((static_cast<std::uint64_t>(offset) << 32U)
                    | (static_cast<std::uint32_t>(alpha) << 24U));
            }
        }
    } else {
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
        incremental.encodedPixelCount -= tile.encodedPixels.size();
        tile.encodedPixels.clear();
        for (int y = tile.top; y < tile.bottom; ++y) for (int x = tile.left; x < tile.right; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * stride + x;
            const unsigned int alpha = incremental.mask[offset];
            if (alpha) tile.encodedPixels.push_back((static_cast<std::uint64_t>(offset) << 32U)
                | (static_cast<std::uint32_t>(alpha) << 24U));
        }
    }
    incremental.encodedPixelCount += tile.encodedPixels.size();
    if (!runtimeShroudPerformanceReference()) runtimeBuildVisionOpaqueRuns(tile);
    else { tile.opaqueRuns.clear(); tile.opaqueRunPixels = 0; }
    const auto area = static_cast<std::uint64_t>(tile.right - tile.left) * (tile.bottom - tile.top);
    incremental.rebuiltMaskPixels += area;
    incremental.rebuiltEffectPixels += area;
}

static int runtimeNativeShroudTerrainIndex(const TerrainCell& cell) {
    return cell.cellIndex512 >= 0 ? cell.cellIndex512
        : cell.x >= 0 && cell.y >= 0 && cell.x < 512 && cell.y < 512
            ? cell.y * 512 + cell.x : -1;
}

static const RuntimeNativeShroudGeometryCache& runtimeEnsureNativeShroudGeometry(
    RuntimeVisionOverlayCache& cache, const GameWorld& world, RuntimeMapRenderStats& stats) {
    auto& geometry = cache.nativeGeometry;
    const auto& cells = world.getTerrainCells();
    bool matches = geometry.valid && geometry.terrainGeometry.size() == cells.size();
    // Level/index edits can happen in place. Pointer identity alone is not a
    // sufficient proof. This exact comparison runs only on overlay rebuilds.
    if (matches) for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        if (geometry.terrainGeometry[i] != std::array<int, 4>{
                cell.x, cell.y, cell.level, runtimeNativeShroudTerrainIndex(cell)}) {
            matches = false; break;
        }
    }
    if (matches) { ++stats.visionGeometryCacheHits; return geometry; }
    ++stats.visionGeometryCacheMisses;
    geometry = {};
    geometry.terrainGeometry.reserve(cells.size());
    int maxX = -1, maxY = -1;
    for (const auto& cell : cells) {
        geometry.terrainGeometry.push_back({cell.x, cell.y, cell.level,
            runtimeNativeShroudTerrainIndex(cell)});
        const int lift = (std::max(0, cell.level) + 1) / 2;
        geometry.minX = std::min(geometry.minX, cell.x - lift);
        geometry.minY = std::min(geometry.minY, cell.y - lift);
        maxX = std::max(maxX, cell.x - lift); maxY = std::max(maxY, cell.y - lift);
    }
    geometry.columns = std::max(0, maxX - geometry.minX + 1);
    geometry.rows = std::max(0, maxY - geometry.minY + 1);
    const std::size_t count = static_cast<std::size_t>(geometry.columns) * geometry.rows;
    geometry.sourceOffsets.assign(count + 1U, 0);
    for (const auto& cell : cells) {
        const int lift = (std::max(0, cell.level) + 1) / 2;
        const std::size_t plane = static_cast<std::size_t>(cell.y - lift - geometry.minY)
            * geometry.columns + cell.x - lift - geometry.minX;
        ++geometry.sourceOffsets[plane + 1U];
    }
    for (std::size_t i = 1; i < geometry.sourceOffsets.size(); ++i)
        geometry.sourceOffsets[i] += geometry.sourceOffsets[i - 1U];
    geometry.sourceIndices.resize(cells.size());
    auto cursor = geometry.sourceOffsets;
    for (const auto& cell : cells) {
        const int lift = (std::max(0, cell.level) + 1) / 2;
        const std::size_t plane = static_cast<std::size_t>(cell.y - lift - geometry.minY)
            * geometry.columns + cell.x - lift - geometry.minX;
        geometry.sourceIndices[cursor[plane]++] = runtimeNativeShroudTerrainIndex(cell);
    }
    geometry.valid = true;
    return geometry;
}

static void runtimeSelectNativeShroudViewportGeometry(RuntimeVisionIncrementalOverlayCache& grid,
    const RuntimeNativeShroudGeometryCache& geometry, const RECT& viewport,
    const RuntimeMapCamera& camera) {
    if (geometry.columns == 0 || geometry.rows == 0) return;
    // Native frames consult one adjacent diamond. Projection holes also sample
    // the established two-diamond viewport halo. Include that halo plus a
    // conservative three-cell inverse-projection/rounding reserve.
    const double left = static_cast<double>(grid.left) - viewport.left
        + camera.worldOffsetX - 2.0 * camera.tileWidth;
    const double right = static_cast<double>(grid.right) - viewport.left
        + camera.worldOffsetX + 2.0 * camera.tileWidth;
    const double top = static_cast<double>(grid.top) - viewport.top
        + camera.worldOffsetY - 2.0 * camera.tileHeight;
    const double bottom = static_cast<double>(grid.bottom) - viewport.top
        + camera.worldOffsetY + 2.0 * camera.tileHeight;
    const int maxX = geometry.minX + geometry.columns - 1;
    const int maxY = geometry.minY + geometry.rows - 1;
    const auto bounded = [](double value, int minimum, int maximum) {
        return static_cast<int>(std::clamp(value, static_cast<double>(minimum),
            static_cast<double>(maximum)));
    };
    grid.nativeMinX = bounded(std::floor(left / camera.tileWidth + top / camera.tileHeight) - 3.0,
        geometry.minX, maxX);
    grid.nativeMinY = bounded(std::floor(top / camera.tileHeight - right / camera.tileWidth) - 3.0,
        geometry.minY, maxY);
    const int endX = bounded(std::ceil(right / camera.tileWidth + bottom / camera.tileHeight) + 3.0,
        geometry.minX, maxX);
    const int endY = bounded(std::ceil(bottom / camera.tileHeight - left / camera.tileWidth) + 3.0,
        geometry.minY, maxY);
    grid.nativeColumns = std::max(0, endX - grid.nativeMinX + 1);
    grid.nativeRows = std::max(0, endY - grid.nativeMinY + 1);
    grid.nativeSources.resize(static_cast<std::size_t>(grid.nativeColumns) * grid.nativeRows);
    for (int y = 0; y < grid.nativeRows; ++y) for (int x = 0; x < grid.nativeColumns; ++x) {
        const std::size_t source = static_cast<std::size_t>(y + grid.nativeMinY - geometry.minY)
            * geometry.columns + x + grid.nativeMinX - geometry.minX;
        auto& sources = grid.nativeSources[static_cast<std::size_t>(y) * grid.nativeColumns + x];
        sources.assign(geometry.sourceIndices.begin() + geometry.sourceOffsets[source],
            geometry.sourceIndices.begin() + geometry.sourceOffsets[source + 1U]);
    }
}

static void runtimeBuildNativeShroudOverlay(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const RuntimeNativeShroudAtlas& atlas, const GameWorld& world, const RECT& viewport,
    const RuntimeMapCamera& camera, int width, int height, RuntimeMapRenderStats& stats) {
    auto& incremental = cache.incremental;
    incremental = {};
    incremental.left = std::max(0, static_cast<int>(viewport.left));
    incremental.top = std::max(0, static_cast<int>(viewport.top));
    incremental.right = std::min(width, static_cast<int>(viewport.right));
    incremental.bottom = std::min(height, static_cast<int>(viewport.bottom));
    if (incremental.left >= incremental.right || incremental.top >= incremental.bottom) return;
    constexpr int side = RuntimeVisionIncrementalOverlayCache::tileSize;
    incremental.columns = 1 + (incremental.right - incremental.left - 1) / side;
    incremental.rows = 1 + (incremental.bottom - incremental.top - 1) / side;
    incremental.mask.assign(static_cast<std::size_t>(width) * height, 0);
    incremental.tiles.resize(static_cast<std::size_t>(incremental.columns) * incremental.rows);
    captureRuntimeVisionOverlayShroud(cache, view);
    for (int y = 0; y < incremental.rows; ++y) for (int x = 0; x < incremental.columns; ++x) {
        auto& tile = incremental.tiles[static_cast<std::size_t>(y) * incremental.columns + x];
        tile.left = incremental.left + x * side; tile.top = incremental.top + y * side;
        tile.right = std::min(incremental.right, tile.left + side);
        tile.bottom = std::min(incremental.bottom, tile.top + side);
    }
    const bool performanceReference = runtimeShroudPerformanceReference();
    // Invalid public diagnostic scales retain the exact pre-optimization path.
    const bool boundedGeometry = !performanceReference && camera.tileWidth > 0 && camera.tileHeight > 0;
    if (boundedGeometry) {
        const auto& geometry = runtimeEnsureNativeShroudGeometry(cache, world, stats);
        runtimeSelectNativeShroudViewportGeometry(incremental, geometry, viewport, camera);
    } else {
        int maxX = -1, maxY = -1;
        incremental.nativeMinX = incremental.nativeMinY = 512;
        for (const auto& cell : world.getTerrainCells()) {
            const int lift = (std::max(0, cell.level) + 1) / 2;
            const int x = cell.x - lift, y = cell.y - lift;
            incremental.nativeMinX = std::min(incremental.nativeMinX, x);
            incremental.nativeMinY = std::min(incremental.nativeMinY, y);
            maxX = std::max(maxX, x); maxY = std::max(maxY, y);
        }
        incremental.nativeColumns = std::max(0, maxX - incremental.nativeMinX + 1);
        incremental.nativeRows = std::max(0, maxY - incremental.nativeMinY + 1);
        incremental.nativeSources.resize(static_cast<std::size_t>(incremental.nativeColumns) * incremental.nativeRows);
    }
    for (const auto& cell : world.getTerrainCells()) {
        const int index = cell.cellIndex512 >= 0 ? cell.cellIndex512
            : cell.x >= 0 && cell.y >= 0 && cell.x < 512 && cell.y < 512 ? cell.y * 512 + cell.x : -1;
        if (!boundedGeometry) {
            const int lift = (std::max(0, cell.level) + 1) / 2;
            const int plane = (cell.y - lift - incremental.nativeMinY) * incremental.nativeColumns
                + cell.x - lift - incremental.nativeMinX;
            incremental.nativeSources[plane].push_back(index);
        }
        const POINT center = screenPoint(projectCell(cell, camera), viewport, camera);
        const RECT bounds{center.x-camera.tileWidth/2, center.y-camera.tileHeight/2,
            center.x+(camera.tileWidth+1)/2, center.y+(camera.tileHeight+1)/2};
        if (!rectIntersects(bounds, viewport)) continue;
        cache.viewportTerrainCellIndices.push_back(index);
        switch (view.visibilityAtIndex(index)) {
        case RuntimeCellVisibility::Visible: ++stats.visionVisibleCells; break;
        case RuntimeCellVisibility::Fogged: ++stats.visionFoggedCells; break;
        case RuntimeCellVisibility::Shrouded: ++stats.visionShroudedCells; break;
        }
    }
    const bool ownership = runtimeVisionTileHasOwnership(layer);
    for (int y = 0; y < incremental.nativeRows; ++y) for (int x = 0; x < incremental.nativeColumns; ++x) {
        TerrainCell planeCell; planeCell.x = x + incremental.nativeMinX; planeCell.y = y + incremental.nativeMinY;
        const POINT center = screenPoint(projectCell(planeCell, camera), viewport, camera);
        const int originX = center.x - camera.tileWidth / 2, originY = center.y - camera.tileHeight / 2;
        const int index = y * incremental.nativeColumns + x;
        if (ownership && incremental.nativeSources[index].empty()) {
            // Only fill projection holes at height drops. Supplementing an
            // existing flat cell can pull an explored neighbour's border TMP
            // pixels into an unknown cell, suppressing its native fade frame.
            // The TMP cliff face still occupies otherwise unmapped pixels:
            // associate its actual owners with the covering shroud diamond.
            // Include a neighbour halo so panning/cropping cannot change an
            // edge frame. Frame 15 supplies the exact scaled diamond coverage.
            const int sampleLeft = std::max(originX, incremental.left - 2 * camera.tileWidth);
            const int sampleRight = std::min(originX + camera.tileWidth, incremental.right + 2 * camera.tileWidth);
            const int sampleTop = std::max(originY, incremental.top - 2 * camera.tileHeight);
            const int sampleBottom = std::min(originY + camera.tileHeight, incremental.bottom + 2 * camera.tileHeight);
            auto& sources = incremental.nativeSources[index];
            const auto* diamond = atlas.alpha.data() + 15 * 1800;
            int previousOwner = -1;
            for (int py = sampleTop; py < sampleBottom; ++py) {
                const auto* row = diamond + ((py - originY) * 30 / camera.tileHeight) * 60;
                for (int px = sampleLeft; px < sampleRight; ++px) {
                    if (!row[(px - originX) * 60 / camera.tileWidth]) continue;
                    const int owner = runtimeVisionTileOwnerAt(layer, viewport, camera, px, py);
                    if (owner >= 0 && static_cast<std::size_t>(owner) < view.cellCount && owner != previousOwner)
                        sources.push_back(owner);
                    previousOwner = owner;
                }
            }
            std::sort(sources.begin(), sources.end());
            sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
        }
        const int left = std::max(incremental.left, originX);
        const int right = std::min(incremental.right, originX + camera.tileWidth);
        const int top = std::max(incremental.top, originY);
        const int bottom = std::min(incremental.bottom, originY + camera.tileHeight);
        if (left >= right || top >= bottom) continue;
        for (int ty = (top - incremental.top) / side; ty <= (bottom - 1 - incremental.top) / side; ++ty)
            for (int tx = (left - incremental.left) / side; tx <= (right - 1 - incremental.left) / side; ++tx)
                incremental.tiles[static_cast<std::size_t>(ty) * incremental.columns + tx]
                    .geometricCells.push_back({index, static_cast<int>(center.x), static_cast<int>(center.y)});
    }
    std::vector<int> dependencies;
    for (std::size_t t = 0; t < incremental.tiles.size(); ++t) {
        const auto& tile = incremental.tiles[t]; dependencies.clear();
        for (const auto& cell : tile.geometricCells) {
            const auto addSources = [&](int projected) {
                if (projected < 0) return;
                const auto& sources = incremental.nativeSources[projected];
                dependencies.insert(dependencies.end(), sources.begin(), sources.end());
            };
            addSources(cell.cellIndex);
            for (const auto [dx, dy] : runtimeNativeShroudNeighbours)
                addSources(runtimeNativeShroudProjectedNeighbour(incremental, cell.cellIndex, dx, dy));
        }
        if (ownership) for (int y = tile.top; y < tile.bottom; ++y) {
            int previous = -1;
            for (int x = tile.left; x < tile.right; ++x) {
                const int owner = runtimeVisionTileOwnerAt(layer, viewport, camera, x, y);
                if (owner != previous) dependencies.push_back(owner);
                previous = owner;
            }
        }
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        for (int index : dependencies) if (index >= 0 && static_cast<std::size_t>(index) < view.cellCount)
            incremental.tilesByCell[index].push_back(static_cast<int>(t));
        runtimeRebuildNativeShroudTile(cache, view, layer, atlas, viewport, camera, width, static_cast<int>(t));
    }
    incremental.valid = true;
}

static bool runtimeUpdateNativeShroudOverlay(RuntimeVisionOverlayCache& cache,
    const RuntimeVisionGridView& view, const RuntimeTerrainLayerCache& layer,
    const RuntimeNativeShroudAtlas& atlas, const RECT& viewport,
    const RuntimeMapCamera& camera, int stride) {
    auto& incremental = cache.incremental;
    std::vector<unsigned char> dirty(incremental.tiles.size(), 0);
    bool changed = false;
    incremental.rebuiltMaskPixels = incremental.rebuiltEffectPixels = 0;
    for (std::size_t index = 0; index < view.cellCount; ++index) {
        const auto shrouded = static_cast<std::uint8_t>(
            view.visibilityAtIndex(static_cast<int>(index)) == RuntimeCellVisibility::Shrouded);
        if (shrouded == cache.shroudedCellMask[index]) continue;
        cache.shroudedCellMask[index] = shrouded; changed = true;
        const auto found = incremental.tilesByCell.find(static_cast<int>(index));
        if (found != incremental.tilesByCell.end()) for (int tile : found->second) dirty[tile] = 1;
    }
    // Neighbours are explicit dependencies: no screen-space blur or full-screen
    // rebuild is needed when exploration or a Gap Generator changes one cell.
    for (std::size_t t = 0; t < dirty.size(); ++t) if (dirty[t])
        runtimeRebuildNativeShroudTile(cache, view, layer, atlas, viewport, camera, stride, static_cast<int>(t));
    return changed;
}
