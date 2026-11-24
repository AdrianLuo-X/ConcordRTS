#include "render/geometry/RuntimeInfantryMuzzleAnchors.inl"
// Native anchors must never apply to a same-named replacement SHP.
bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& centerPixels, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) {
    auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
    if (!sprite || !sprite->loaded) return false;
    const auto& image = sprite->image;
    const int width = image.globalWidth > 0 ? image.globalWidth : image.width;
    const int height = image.globalHeight > 0 ? image.globalHeight : image.height;
    return concord::render::geometry::resolveInfantryMuzzleAttachment(
        {sprite->resolvedImageId, sprite->sequenceName, sprite->sourceFingerprint,
            sprite->frameIndex, width, height},
        centerPixels, camera.tileHeight, result);
}
