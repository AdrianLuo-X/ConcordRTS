#include "render/geometry/RuntimeStructureMuzzleAnchors.inl"

// Pixel offsets are authored against the fixed-size building bitmap. DualOffset
// adds the direction-dependent FLH to that pixel pivot in the native 60x30
// projection; camera zoom moves the building, but does not resize its SHP.
bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& centerPixels, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, RuntimeInputPixelPoint& result) {
    if (object.kind != WorldObjectKind::Structure) return false;
    const auto* art = findArtTypeForObject(object, context);
    if (!art) return false;
    const auto* building = context.rules ? context.rules->findBuildingType(object.type) : nullptr;
    if (art->voxel || art->turret || (building && building->turret)) return false;
    const bool secondaryOffset = secondary && !art->secondaryFirePixelOffset.empty();
    const auto& text = secondaryOffset ? art->secondaryFirePixelOffset : art->primaryFirePixelOffset;
    if (!text.empty()) {
        return concord::render::geometry::resolveAuthoredStructureMuzzle(text,
            secondaryOffset ? art->secondaryFireDualOffset : art->primaryFireDualOffset,
            {forwardLeptons, lateralLeptons, heightLeptons}, firingFacing, centerPixels, result);
    }
    const auto attach = [&](RuntimeObjectShpSpriteCacheEntry* sprite, int offsetX, int offsetY) {
        if (!sprite || !sprite->loaded) return false;
        const int width = sprite->image.globalWidth > 0 ? sprite->image.globalWidth : sprite->image.width;
        const int height = sprite->image.globalHeight > 0 ? sprite->image.globalHeight : sprite->image.height;
        return concord::render::geometry::resolveStructureFrameMuzzle(
            {sprite->resolvedImageId, sprite->sourceFingerprint, sprite->frameIndex, width, height},
            centerPixels, {offsetX, offsetY}, result);
    };
    if (toLowerAscii(art->id) == "caeast01")
        return attach(loadRuntimeObjectShpSpriteFromCache(object, context, cache), 0, 0);
    if (toLowerAscii(art->id) != "yapsyt") return false;
    for (const auto& animation : art->activeAnimations) {
        if (toLowerAscii(animation.key) != "activeanim") continue;
        const bool damaged = runtimeObjectIsDamaged(object, context);
        const auto& imageId = damaged && !animation.damagedImage.empty() ? animation.damagedImage : animation.image;
        const auto* animationArt = context.art ? context.art->findArtType(imageId) : nullptr;
        if (!animationArt || !runtimeArtVisibleAtDetail(context, animationArt)) return false;
        const bool shadow = runtimeArtShadowEnabled(animationArt, false);
        auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(imageId, 0, context, cache, shadow);
        if (!first || first->frameCount <= 0) return false;
        auto animationContext = context;
        if (!object.poweredOperational && (animation.powered || animation.poweredEffect))
            animationContext.animationTick = runtimeStructurePoweredAnimationTick(object, context);
        const int frame = selectRuntimeAuxiliaryAnimationFrame(animationContext, animationArt, first->frameCount, shadow);
        auto* selected = frame == 0 ? first : loadRuntimeAuxiliaryShpSpriteFromCache(imageId, frame, context, cache, shadow);
        return attach(selected, animation.offsetX, animation.offsetY);
    }
    return false;
}
