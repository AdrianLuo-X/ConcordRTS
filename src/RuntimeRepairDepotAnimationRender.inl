struct RuntimeRepairDepotAnimationLayer {
    const ArtType::ActiveAnimation* layer = nullptr;
    int ageTicks = 0;
    // 0=idle, 1=extend, 2=work, 3=retract.
    int phase = 0;
};

RuntimeRepairDepotAnimationLayer runtimeRepairDepotAnimationLayer(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    const ArtType& art
) {
    RuntimeRepairDepotAnimationLayer result;
    if (!context.specialMechanismSnapshots) return result;
    for (const auto& snapshot : *context.specialMechanismSnapshots) {
        if (snapshot.kind != "repairDepot" || snapshot.objectId != object.instanceId)
            continue;
        for (const auto& layer : art.specialAnimations) {
            if (layer.key != snapshot.resourceId
                || !runtimeRepairDepotAnimationKey(layer.key)) continue;
            result.layer = &layer;
            result.ageTicks = std::max(0, snapshot.progress);
            result.phase = layer.key == "SpecialAnim" ? 1
                : layer.key == "SpecialAnimTwo" ? 2 : 3;
            return result;
        }
    }
    return result;
}
