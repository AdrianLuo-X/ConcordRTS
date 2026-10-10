// Locate the nozzle on the same opaque raster and pose used to draw the body.
// Model-space BodyLength need not equal the displayed SHP/VXL silhouette size.
POINT runtimeMissileRasterTail(const RuntimeShpFrameImage& image, const RECT& bounds,
    int facing, double pitch, const RuntimeMapCamera& camera) {
    const double angle = facing * 6.28318530717958647692 / 256.0;
    const double dx = (std::sin(angle) - std::cos(angle)) * 0.70710678118654752440;
    const double dy = (-std::cos(angle) - std::sin(angle)) * 0.70710678118654752440;
    const auto projected = projectCellXY(dx * std::cos(pitch) * 16.0,
        dy * std::cos(pitch) * 16.0,
        std::sin(pitch) * runtimeMissileLevelsPerCell * 16.0, camera);
    const double length = std::hypot(double(projected.x), double(projected.y));
    const double rx = length > 0.001 ? -projected.x / length : 0.0;
    const double ry = length > 0.001 ? -projected.y / length : 1.0;
    double rear = -1e9;
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        if (image.pixels[static_cast<std::size_t>(y) * image.width + x] != 0)
            rear = std::max(rear, x * rx + y * ry);
    double sumX = 0.0, sumY = 0.0; int count = 0;
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        if (image.pixels[static_cast<std::size_t>(y) * image.width + x] != 0
            && rear - (x * rx + y * ry) <= 1.25) {
            sumX += x; sumY += y; ++count;
        }
    return count ? POINT{bounds.left + static_cast<LONG>(std::lround(sumX / count + rx)),
        bounds.top + static_cast<LONG>(std::lround(sumY / count + ry))}
        : POINT{(bounds.left + bounds.right) / 2, bounds.bottom};
}

bool runtimeVoxelMissileTail(const POINT& modelCenter, const std::string& imageId,
    int facing, double pitch, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& tail,
    const std::string& cacheSuffix = "|missile") {
    const auto* body = loadRuntimeVoxelImageFromCache(imageId, imageId + cacheSuffix,
        facingBucketForFacingValue(facing, runtimeVoxelFacingBucketCount), context, cache,
        0.0, 0, 0, static_cast<int>(std::lround(pitch * 90.0 / 3.141592653589793)));
    if (!body || !body->hasSprite) return false;
    tail = runtimeMissileRasterTail(body->image,
        runtimeShpFrameScreenRect(modelCenter, camera, body->image), facing, pitch, camera);
    return true;
}

void drawRuntimeAttachedMissileExhaust(HDC dc, const RECT& viewport, const POINT& nozzle,
    const std::string& animation, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats) {
    const auto* art = context.art ? context.art->findArtType(animation) : nullptr;
    const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(animation, 0, context, cache, false);
    if (!first || !art) { ++stats.missileExhaustMissing; return; }
    const POINT firstCenter = runtimeAuxiliaryShpAdjustedCenter(nozzle, camera, first->image, 0, 0, true);
    const RECT firstBounds = runtimeShpFrameScreenRect(firstCenter, camera, first->image);
    const POINT anchor{nozzle.x + nozzle.x - (firstBounds.left + firstBounds.right) / 2,
        nozzle.y + nozzle.y - (firstBounds.top + firstBounds.bottom) / 2};
    const int frame = (std::max(0, context.animationTick)
        / runtimeMissileExhaustFrameDelay(art->rate, context.fixedTickHz)) % runtimeMissileExhaustFlameFrames;
    if (drawRuntimeClassifiedAuxiliaryEffect(dc, viewport, anchor, context, camera, cache,
            animation, frame, 0, 0, false, RuntimeAuxiliaryEffectKind::Trailer, true, true)) {
        ++stats.missileExhaustDrawn;
        ++stats.missileExhaustHeadDrawn;
    } else ++stats.missileExhaustMissing;
}
