POINT runtimeNuclearMissileBodyAnchor(const std::string& imageId,
    const POINT& bottom, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache) {
    auto* body = loadRuntimeAuxiliaryShpSpriteFromCache(imageId, 0, context, cache, false);
    if (!body) return bottom;
    const POINT center = runtimeAuxiliaryShpAdjustedCenter(bottom, camera, body->image, 0, 0, true);
    const RECT bounds = runtimeShpFrameScreenRect(center, camera, body->image);
    // The physical point is the ascent's nozzle / descent's nose, not the
    // center of the asymmetric 68x288 canvas.
    return {bottom.x + bottom.x - (bounds.left + bounds.right) / 2,
        bottom.y + bottom.y - bounds.bottom};
}

bool runtimeNuclearMissileTailPoint(const std::string& imageId,
    const POINT& bodyAnchor, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& tail) {
    const bool up = equalAsciiNoCase(imageId, "NKMSLUP");
    if (!up && !equalAsciiNoCase(imageId, "NKMSLDN")) return false;
    auto* body = loadRuntimeAuxiliaryShpSpriteFromCache(imageId, 0, context, cache, false);
    if (!body || body->image.width <= 0 || body->image.height <= 0) return false;
    // The stock nuclear SHPs have large, asymmetric empty canvas margins.
    // Use their actual frame bounds, not the canvas center or a guessed length.
    const POINT anchor = runtimeNuclearMissileBodyAnchor(imageId, bodyAnchor, context, camera, cache);
    const POINT center = runtimeAuxiliaryShpAdjustedCenter(anchor, camera, body->image, 0, 0, true);
    const RECT bounds = runtimeShpFrameScreenRect(center, camera, body->image);
    tail = {(bounds.left + bounds.right) / 2, up ? bounds.bottom : bounds.top};
    return true;
}

bool drawRuntimeNuclearMissileBody(HDC dc, const RECT& viewport, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const std::string& imageId, const std::string& owner) {
    const auto* palette = runtimeUnitPaletteForRender(context, cache);
    if (!palette) return false;
    // NKMSL's authored 68x288 canvas intentionally exceeds the small-shell
    // sanity limit. Use its full original frame and matching centered anchor.
    const POINT anchor = runtimeNuclearMissileBodyAnchor(imageId, center, context, camera, cache);
    return drawRuntimeAuxiliaryShpSpriteWithPalette(dc, viewport, anchor, context, camera,
        cache, imageId, 0, *palette, 0, 0, false,
        runtimeOwnerHasRendererRemapColor(context, owner), runtimeOwnerRemapColor(context, owner),
        255, true);
}
