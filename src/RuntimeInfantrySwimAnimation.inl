bool runtimeInfantryIsSwimmingForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache* cache
) {
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || (object.hasRenderPosition ? object.renderOnBridge : object.onBridge)
        || context.mapFacts == nullptr) return false;

    // Logical movement reserves the destination before the body reaches it.
    // Cross the shoreline with the rendered body, including bridge transitions.
    const double x = object.hasRenderPosition ? object.renderX : object.x;
    const double y = object.hasRenderPosition ? object.renderY : object.y;
    if (!std::isfinite(x) || !std::isfinite(y)
        || x < 0.0 || x >= 512.0 || y < 0.0 || y >= 512.0) return false;
    const MapCellFacts* cell = MapQuery(*context.mapFacts).getCell(
        static_cast<int>(std::floor(x + 0.5)),
        static_cast<int>(std::floor(y + 0.5))
    );
    if (cell == nullptr) return false;

    if (cell->hasTerrain) {
        // Shore tile sets contain both land and water subtiles. Their names
        // cannot determine whether this particular cell needs a swimming pose.
        std::unique_ptr<RuntimeMapRenderCache> temporaryCache;
        if (cache == nullptr) {
            temporaryCache = std::make_unique<RuntimeMapRenderCache>();
            cache = temporaryCache.get();
        }
        const int landType = runtimeLandTypeForTmpImage(resolveRuntimeTerrainMetadata(
            cell->terrain.tileIndex, cell->terrain.subTileIndex, context, *cache
        ));
        if (landType != RuntimeLandTypeUnknown) return landType == RuntimeLandTypeWater;
    }
    return cell->isWater && !cell->isShore;
}
