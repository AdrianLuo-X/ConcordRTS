#include "render/geometry/RuntimeMarineMuzzleAnchors.inl"

// Resolve the visible sonar source from the very SHP frame the body renderer
// selected. Native-pixel coordinates need no FLH or zoom multiplier: mobile
// SHPs retain native pixel dimensions at every camera tile size.
bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& centerPixels, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) {
    if (object.kind != WorldObjectKind::Unit) return false;
    auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
    if (!sprite || !sprite->loaded) return false;
    const auto& image = sprite->image;
    const int width = image.globalWidth > 0 ? image.globalWidth : image.width;
    const int height = image.globalHeight > 0 ? image.globalHeight : image.height;
    RuntimeInputPixelPoint point{};
    if (!concord::render::geometry::resolveMarineMuzzleAttachment(
            {sprite->resolvedImageId, sprite->sourceFingerprint, sprite->frameIndex, width, height},
            centerPixels, camera.tileHeight, point)) return false;
    point.y += runtimeUnderwaterShpAlignmentOffsetY(object, context, camera, image);
    result = point;
    return true;
}
