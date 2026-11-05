bool runtimeInfantryIsSwimmingForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache* cache) {
    return runtimeInfantrySwimmingWithMetadata(object, context, cache,
        resolveRuntimeTerrainMetadata);
}
