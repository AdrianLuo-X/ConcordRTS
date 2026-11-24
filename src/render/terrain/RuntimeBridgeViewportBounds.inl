// Bridge frames overhang their map cell. Only consult their cached resource
// geometry after the ordinary cell rectangle misses the paint area.
bool runtimeBridgeSpriteIntersectsPaintArea(
    const TerrainCell& sourceCell,
    RuntimeInputPixelPoint center,
    RuntimeInputPixelRect paintArea,
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
    const auto bridgeCenter = concord::render::runtimeBridgeViewportCenter(
        center, {camera.tileWidth, camera.tileHeight}, runtimeOverlayLooksLikeHighBridge(cell));
    const std::string segment = runtimeLowBridgeSegmentImageId(cell);
    const std::string imageId = cell.overlayImage.empty()
        ? cell.overlayType : cell.overlayImage;
    const std::string preferred = segment.empty() ? imageId : segment;
    const std::vector<std::string> roots{
        context.assetRootPath, context.tileRootPath, context.structureRootPath};
    const auto tmpBounds = [&](const std::string& name, bool candidate)
        -> std::optional<RuntimeInputPixelRect> {
        if (candidate && (!runtimeResourceCandidateLooksTmp(name)
            || !runtimeBridgeCandidateMatchesSegment(name, segment))) {
            return std::nullopt;
        }
        if (name.empty()) return std::nullopt;
        for (const auto& fileName : resolveAuxiliaryTmpFileNameCandidates(name, context)) {
            auto* file = loadRuntimeTmpTileByNameFromCache(
                roots, context.assetRootPath, fileName, cache);
            if (file == nullptr) continue;
            // Authored empty side frames must retain the painter's no-op.
            if (runtimeTerrainSubTileIsAuthoredEmpty(*file, frame))
                return RuntimeInputPixelRect{};
            const auto* image = findRuntimeTerrainSubTileImage(*file, frame);
            if (image == nullptr || image->width <= 0 || image->height <= 0
                || image->pixels.size() < static_cast<std::size_t>(image->width)
                    * static_cast<std::size_t>(image->height)) continue;
            return concord::render::runtimeBridgeViewportTmpBounds(bridgeCenter, *image);
        }
        return std::nullopt;
    };
    const auto shpBounds = [&](const std::string& name, bool candidate)
        -> std::optional<RuntimeInputPixelRect> {
        if (candidate && (!runtimeResourceCandidateLooksShp(name)
            || !runtimeBridgeCandidateMatchesSegment(name, segment))) {
            return std::nullopt;
        }
        auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(
            name, frame, context, cache, false);
        if (sprite == nullptr || !runtimeResourceShpLooksSane(sprite->image, camera)) {
            return std::nullopt;
        }
        return runtimeRenderPlacementPixelRect(runtimeShpFrameScreenRect(
            runtimeRenderPlacementNativePoint(bridgeCenter), camera, sprite->image));
    };
    const std::span<const std::string> candidates = runtimeBridgeMapFactHasCandidates(mapFact)
        ? std::span<const std::string>{mapFact->overlayResource.candidateFileNames}
        : std::span<const std::string>{};
    return concord::render::runtimeBridgeViewportResourceIntersects(
        preferred, imageId, candidates, paintArea, tmpBounds, shpBounds);
}
