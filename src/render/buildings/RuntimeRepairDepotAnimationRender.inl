using concord::render::RuntimeRepairDepotAnimationLayer;

RuntimeRepairDepotAnimationLayer runtimeRepairDepotAnimationLayer(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, const ArtType& art) {
    if (!context.specialMechanismSnapshots) return {};
    return concord::render::selectRuntimeRepairDepotAnimation(
        object.instanceId, *context.specialMechanismSnapshots, art.specialAnimations);
}
