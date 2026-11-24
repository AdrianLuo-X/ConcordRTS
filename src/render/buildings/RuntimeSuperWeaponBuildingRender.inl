bool runtimeSuperWeaponBuildingLayerKey(const std::string& key) {
    return concord::render::isRuntimeSuperWeaponBuildingLayerKey(key);
}

int runtimeSuperWeaponBuildingFrameDelay(const ArtType* art, int tickHz) {
    return runtimeSuperWeaponBuildingAnimationDelay(art, tickHz);
}

int runtimeSuperWeaponBuildingFrame(const ArtType* art, int ageTicks, int tickHz) {
    return concord::render::selectRuntimeSuperWeaponBuildingFrame(art, ageTicks, tickHz,
        runtimeSuperWeaponBuildingFrameDelay);
}

using concord::render::RuntimeSuperWeaponBuildingLayer;

RuntimeSuperWeaponBuildingLayer runtimeSuperWeaponBuildingLayer(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, const ArtType& art) {
    const auto poses = context.specialMechanismSnapshots
        ? std::span<const SpecialMechanismRenderSnapshot>{*context.specialMechanismSnapshots}
        : std::span<const SpecialMechanismRenderSnapshot>{};
    return concord::render::selectRuntimeSuperWeaponBuildingLayer(object.instanceId,
        art.auxiliaryAnimations, poses, context.fixedTickHz, context.art != nullptr,
        [&](const ArtType::ActiveAnimation& layer) {
            return context.art->findArtType(runtimeObjectIsDamaged(object, context)
                && !layer.damagedImage.empty() ? layer.damagedImage : layer.image);
        }, runtimeSuperWeaponBuildingFrameDelay);
}
