// Bridge frames overhang their map cell. Only consult their cached resource
// geometry after the ordinary cell rectangle misses the paint area.
bool runtimeBridgeSpriteIntersectsPaintArea(
    const TerrainCell& sourceCell,
    const POINT& center,
    const RECT& paintArea,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
) {
    if (!sourceCell.hasOverlay
        || (!runtimeOverlayLooksLikeBridge(sourceCell)
            && !runtimeOverlayLooksLikeLowBridge(sourceCell))) {
        return false;
    }

    const MapCellFacts* mapFact = nullptr;
    if (context.mapFacts != nullptr) {
        const int cellIndex = sourceCell.cellIndex512 >= 0
            ? sourceCell.cellIndex512 : sourceCell.y * 512 + sourceCell.x;
        const auto found = context.mapFacts->cellIndexLookup.find(cellIndex);
        if (found != context.mapFacts->cellIndexLookup.end()
            && found->second < context.mapFacts->cells.size()) {
            mapFact = &context.mapFacts->cells[found->second];
        }
    }
    TerrainCell cell = sourceCell;
    if (mapFact != nullptr && mapFact->hasOverlay
        && cell.overlayIndex == mapFact->overlay.overlayIndex) {
        cell.overlayFrame = cell.overlayFrame >= 0 ? cell.overlayFrame
            : (mapFact->overlay.frameIndex != 0 ? mapFact->overlay.frameIndex
                : (mapFact->overlay.frame != 0 ? mapFact->overlay.frame
                    : mapFact->overlay.fallbackFrame));
        cell.overlayType = mapFact->overlay.type;
        cell.overlayImage = mapFact->overlay.image;
    }
    if (!runtimeBridgeOverlaySupportedByTerrainContext(cell, mapFact)) {
        return false;
    }
    const int frame = runtimeResourceOverlayFrame(cell, mapFact);
    POINT bridgeCenter = center;
    if (runtimeOverlayLooksLikeHighBridge(cell)) {
        bridgeCenter.x -= camera.tileWidth / 2;
        bridgeCenter.y -= camera.tileHeight / 2 + 1;
    }
    const std::string segment = runtimeLowBridgeSegmentImageId(cell);
    const std::string imageId = cell.overlayImage.empty()
        ? cell.overlayType : cell.overlayImage;
    const std::string preferred = segment.empty() ? imageId : segment;
    const std::vector<std::string> roots{
        context.assetRootPath, context.tileRootPath, context.structureRootPath};
    const auto intersectsPaint = [&](const RECT& bounds) {
        return bounds.right > bounds.left && bounds.bottom > bounds.top
            && rectIntersects(bounds, paintArea);
    };
    const auto tmpBounds = [&](const std::string& name) -> std::optional<RECT> {
        if (name.empty()) return std::nullopt;
        for (const auto& fileName : resolveAuxiliaryTmpFileNameCandidates(name, context)) {
            auto* file = loadRuntimeTmpTileByNameFromCache(
                roots, context.assetRootPath, fileName, cache);
            if (file == nullptr) continue;
            // Authored empty side frames must retain the painter's no-op.
            if (runtimeTerrainSubTileIsAuthoredEmpty(*file, frame))
                return RECT{0, 0, 0, 0};
            const auto* image = findRuntimeTerrainSubTileImage(*file, frame);
            if (image == nullptr || image->width <= 0 || image->height <= 0
                || image->pixels.size() < static_cast<std::size_t>(image->width)
                    * static_cast<std::size_t>(image->height)) continue;
            const int globalWidth = image->globalWidth > 0
                ? image->globalWidth : image->width;
            const int globalHeight = image->globalHeight > 0
                ? image->globalHeight : image->height;
            const LONG left = bridgeCenter.x - globalWidth / 2 + image->offsetX;
            const LONG top = bridgeCenter.y - globalHeight / 2 + image->offsetY;
            return RECT{left, top, left + image->width, top + image->height};
        }
        return std::nullopt;
    };
    if (const auto bounds = tmpBounds(preferred))
        return intersectsPaint(*bounds);
    if (runtimeBridgeMapFactHasCandidates(mapFact)) {
        for (const auto& fileName : mapFact->overlayResource.candidateFileNames) {
            if (!runtimeResourceCandidateLooksTmp(fileName)
                || !runtimeBridgeCandidateMatchesSegment(fileName, segment)) continue;
            if (const auto bounds = tmpBounds(fileName))
                return intersectsPaint(*bounds);
        }
    }
    const auto shpIntersects = [&](const std::string& name) {
        auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(
            name, frame, context, cache, false);
        return sprite != nullptr && runtimeResourceShpLooksSane(sprite->image, camera)
            && intersectsPaint(runtimeShpFrameScreenRect(
                bridgeCenter, camera, sprite->image));
    };
    if (runtimeBridgeMapFactHasCandidates(mapFact)) {
        for (const auto& fileName : mapFact->overlayResource.candidateFileNames) {
            if (runtimeResourceCandidateLooksShp(fileName)
                && runtimeBridgeCandidateMatchesSegment(fileName, segment)
                && shpIntersects(fileName)) return true;
        }
    }
    return !imageId.empty() && shpIntersects(imageId);
}
