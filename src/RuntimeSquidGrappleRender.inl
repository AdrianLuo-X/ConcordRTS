// Retail ParasiteClass::UpdateSquiddy selects SQDG, not WeaponType::Anim.
// The 240-frame sheet is three actions, each containing eight ten-frame strips.
int runtimeSquidGrappleFrame(int facing, int age, int fixedTickHz) {
    const auto pose = runtimeSquidGrapplePose(age, fixedTickHz);
    // This attachment follows the victim's native facing. The moving SQD
    // sprite's screen-facing convention is not the SQDG host convention.
    const int direction = worldObjectArtFacingBucket(facing, 8);
    return pose.action * 80 + direction * 10 + pose.frame;
}

int drawRuntimeSquidGrapple(HDC dc, const RECT& viewport,
    const WorldObjectInstance& source, const WorldObjectInstance& target,
    const SpecialMechanismRenderSnapshot& snapshot,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats) {
    if (!target.alive || target.health <= 0) return -1;
    const std::string image = "SQDG";
    const auto* art = context.art ? context.art->findArtType(image) : nullptr;
    const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(image, 0, context, cache, false);
    const auto* palette = runtimeUnitPaletteForRender(context, cache, true);
    if (!art || !first || !palette || first->frameCount < 240) {
        ++stats.squidGrappleMissing; return -1;
    }
    const int localFrame = runtimeSquidGrappleFrame(target.facing, snapshot.progress, context.fixedTickHz);
    const POINT center = screenPoint(runtimeObjectDrawPoint(target, {}, camera), viewport, camera);
    // Submarines carry the attached body below the water plane. Surface ship
    // tentacles remain opaque; submerged hosts use the same depth/opacity as
    // their hull. The squid's remap band belongs to the attacking player.
    if (drawRuntimeAuxiliaryShpSpriteWithPalette(dc, viewport, center, context, camera, cache,
            image, localFrame, *palette, 0, 0, false, true,
            runtimeOwnerRemapColor(context, source.owner), runtimeObjectSpriteOpacity(target), true)) {
        ++stats.squidGrappleDrawn;
        return localFrame;
    }
    ++stats.squidGrappleMissing;
    return -1;
}

void drawRuntimeSquidSplash(HDC dc, const RECT& viewport, const POINT& center,
    const RuntimeImpactRenderActor& actor, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats) {
    const auto* art = context.art ? context.art->findArtType(actor.animId) : nullptr;
    const auto* palette = runtimeAnimPaletteForRender(context, cache);
    const int opacity = art ? ((100 - art->translucency) * 255 + 50) / 100 : 255;
    if (art && palette && drawRuntimeAuxiliaryShpSpriteWithPalette(dc, viewport,
            center, context, camera, cache, actor.animId, actor.frameIndex, *palette,
            0, static_cast<int>(std::lround(art->yDrawOffset * camera.tileHeight / 30.0)),
            false, false, RGB(255,255,255), opacity, true)) {
        ++stats.squidSplashDrawn;
        ++stats.impactActorDrawn;
    } else {
        ++stats.squidSplashMissing;
    }
}
