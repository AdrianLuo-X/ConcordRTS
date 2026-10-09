POINT runtimeChronoEffectAnchor(
    const WorldObjectInstance& object, POINT point, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, bool ground
) {
    RECT bounds{};
    bool resolved = false;
    if (object.kind == WorldObjectKind::Unit && objectUsesVoxelArt(object, context)) {
        if (const auto* voxel = loadRuntimeVoxelFromCache(object, context, cache)) {
            bounds = runtimeShpFrameScreenRect(runtimeVoxelModelDrawCenter(point, camera), camera, voxel->image);
            resolved = runtimeRectIsValid(bounds);
        }
    } else {
        resolved = resolveRuntimeObjectShpSpriteBounds(object, point, context, camera, cache, bounds);
    }
    if (resolved) point.y = ground ? bounds.bottom - 2 : (bounds.top + bounds.bottom) / 2;
    return point;
}

int runtimeChronoSparkleFrame(
    const SpecialMechanismRenderSnapshot& snapshot,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    const ArtType* art = context.art == nullptr ? nullptr : context.art->findArtType(snapshot.resourceId);
    const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(
        snapshot.resourceId, 0, context, cache, runtimeArtShadowEnabled(art, false));
    if (first == nullptr || first->frameCount <= 0) return 0;
    const int count = first->frameCount;
    const int start = std::clamp(art != nullptr && art->hasAnimationLoopStart
        ? art->animationLoopStart : 0, 0, count - 1);
    const int end = std::clamp(art != nullptr && art->hasAnimationLoopEnd
        ? art->animationLoopEnd : count, start + 1, count);
    const int step = std::max(0, snapshot.progress) / runtimeAnimationFrameDelayTicks(art, context.fixedTickHz);
    return start + step % (end - start);
}

void drawRuntimeChronoRecovery(
    HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats,
    const SpecialMechanismRenderSnapshot& snapshot, const WorldObjectInstance* object
) {
    if (object == nullptr || !object->alive || snapshot.resourceId.empty()
        || (snapshot.phase != "WarpIn" && snapshot.phase != "Cooldown")
        || snapshot.progress >= snapshot.requiredProgress
        || runtimeSpecialObjectIsHidden(context, *object)
        || !runtimeVisionObjectHasCurrentVisibility(context, *object)) return;
    const POINT center = screenPoint(runtimeObjectDrawPoint(
        *object, runtimeObjectFoundationDimensions(*object, context), camera), viewport, camera);
    const POINT anchor = runtimeChronoEffectAnchor(*object, center, context, camera, cache, false);
    const int frame = runtimeChronoSparkleFrame(snapshot, context, cache);
    const ArtType* art = context.art == nullptr ? nullptr : context.art->findArtType(snapshot.resourceId);
    (void)drawRuntimeClassifiedAuxiliaryEffect(dc, viewport, anchor, context, camera, cache,
        snapshot.resourceId, frame, 0, 0, runtimeArtShadowEnabled(art, false),
        RuntimeAuxiliaryEffectKind::Impact, true, true);
    recordRuntimeSpecialMechanismRenderIntent(stats, snapshot, frame);
}
