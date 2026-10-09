#include "RuntimeStructureMuzzleAnchors.inl"

// Pixel offsets are authored against the fixed-size building bitmap. DualOffset
// adds the direction-dependent FLH to that pixel pivot in the native 60x30
// projection; camera zoom moves the building, but does not resize its SHP.
bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, POINT& result) {
    if (object.kind != WorldObjectKind::Structure) return false;
    const auto* art = findArtTypeForObject(object, context);
    if (!art) return false;
    const auto* building = context.rules ? context.rules->findBuildingType(object.type) : nullptr;
    if (art->voxel || art->turret || (building && building->turret)) return false;
    const bool secondaryOffset = secondary && !art->secondaryFirePixelOffset.empty();
    const auto& text = secondaryOffset ? art->secondaryFirePixelOffset : art->primaryFirePixelOffset;
    if (!text.empty()) {
        std::istringstream stream(text);
        int x = 0, y = 0;
        char comma = 0;
        if (!(stream >> x >> comma >> y) || comma != ',' || (stream >> std::ws, !stream.eof())
            || x < -32768 || x > 32767 || y < -32768 || y > 32767) return false;
        double sx = x, sy = y;
        if (secondaryOffset ? art->secondaryFireDualOffset : art->primaryFireDualOffset) {
            constexpr double pi = 3.14159265358979323846;
            const double angle = (firingFacing & 255) * 2.0 * pi / 256.0;
            const double fx = (std::sin(angle) - std::cos(angle)) / std::sqrt(2.0);
            const double fy = (-std::cos(angle) - std::sin(angle)) / std::sqrt(2.0);
            const double dx = (fx * forwardLeptons - fy * lateralLeptons) / 256.0;
            const double dy = (fy * forwardLeptons + fx * lateralLeptons) / 256.0;
            sx += (dx - dy) * 30.0;
            sy += (dx + dy) * 15.0 - heightLeptons * std::sqrt(6.0) * 15.0 / 256.0;
        }
        result = {center.x + static_cast<LONG>(std::lround(sx)),
            center.y + static_cast<LONG>(std::lround(sy))};
        return true;
    }
    const auto attach = [&](RuntimeObjectShpSpriteCacheEntry* sprite, int offsetX, int offsetY) {
        if (!sprite || !sprite->loaded) return false;
        const int width = sprite->image.globalWidth > 0 ? sprite->image.globalWidth : sprite->image.width;
        const int height = sprite->image.globalHeight > 0 ? sprite->image.globalHeight : sprite->image.height;
        POINT point{};
        if (!runtimeNativeStructureMuzzleAnchor(sprite->resolvedImageId, sprite->sourceFingerprint,
                sprite->frameIndex, width, height, point)) return false;
        // Structure main and active SHPs are center-authored, unlike infantry.
        result = {center.x + offsetX - width / 2 + point.x,
            center.y + offsetY - height / 2 + point.y};
        return true;
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
