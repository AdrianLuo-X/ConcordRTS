#include "render/geometry/RuntimeMarineMuzzleAnchors.inl"

// Resolve the visible sonar source from the very SHP frame the body renderer
// selected. Native-pixel coordinates need no FLH or zoom multiplier: mobile
// SHPs retain native pixel dimensions at every camera tile size.
bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& result) {
    if (object.kind != WorldObjectKind::Unit) return false;
    auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
    if (!sprite || !sprite->loaded
        || toLowerAscii(sprite->resolvedImageId) != "dlph"
        || sprite->sourceFingerprint != 8976802777928645145ULL) return false;
    const auto& image = sprite->image;
    const int width = image.globalWidth > 0 ? image.globalWidth : image.width;
    const int height = image.globalHeight > 0 ? image.globalHeight : image.height;
    POINT point{};
    if (!runtimeFindMarineMuzzleAnchor(sprite->resolvedImageId, sprite->frameIndex,
            width, height, point)) return false;
    result = {center.x - width / 2 + point.x,
        center.y + camera.tileHeight / 2 - height + point.y
            + runtimeUnderwaterShpAlignmentOffsetY(object, context, camera, image)};
    return true;
}
