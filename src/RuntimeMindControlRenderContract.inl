bool runRuntimeMindControlRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) { error = message; return false; };
    if (context.rules == nullptr || context.art == nullptr) return fail("mind-control resources missing");
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int width = 480, height = 224;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * height * 3, background);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -width / 2;
    camera.worldOffsetY = -100;
    camera.initialized = true;
    const RECT viewport{0, 0, width, height};
    GameWorld world;
    WorldObjectInstance source;
    source.instanceId = 1;
    source.kind = WorldObjectKind::Infantry;
    source.type = "YURI";
    source.owner = "Russians";
    source.health = 100;
    source.x = 0;
    source.y = 4;
    source.facing = 64;
    WorldObjectInstance target = source;
    target.instanceId = 2;
    target.type = "E2";
    target.x = 4;
    target.y = 0;
    target.facing = 192;
    world.accessObjects() = {source, target};
    std::vector<int> selection;
    probe.selectedObjectIds = &selection;
    SpecialMechanismRenderSnapshot snapshot;
    snapshot.kind = "mindControl";
    snapshot.objectId = 1;
    snapshot.targetObjectId = 2;
    snapshot.owner = source.owner;
    snapshot.resourceId = context.rules->getSpecialMechanismRules().controlledAnimationType;
    std::vector<SpecialMechanismRenderSnapshot> snapshots{snapshot};
    probe.specialMechanismSnapshots = &snapshots;
    const auto draw = [&]() {
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
        return stats.specialMechanismRenderActors;
    };
    const auto* palette = runtimeAnimPaletteForRender(probe, *cache);
    if (palette == nullptr) return fail("mind-control animation palette missing");
    int checkedFrames = 0;
    for (const auto& id : {snapshot.resourceId,
            context.rules->getSpecialMechanismRules().permaControlledAnimationType}) {
        snapshots.front().resourceId = id;
        const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(id, 0, probe, *cache, false);
        if (first == nullptr) return fail("mind-control ring missing: " + id);
        const int frames = first->frameCount;
        const int delay = runtimeAnimationFrameDelayTicks(context.art->findArtType(id), probe.fixedTickHz);
        for (int frame = 0; frame < frames; ++frame) {
            const auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(id, frame, probe, *cache, false);
            if (sprite == nullptr || sprite->frameIndex != frame) return fail(id + " ring frame missing");
            std::unordered_map<std::uint32_t, int> expected;
            for (unsigned char index : sprite->image.pixels) {
                if (index != 0) ++expected[packRuntimeDibColor((*palette)[index])];
            }
            // Cover the complete animation and its wrap, independent of the
            // global tick at which the control link was established.
            snapshots.front().progress = (frame + frames) * delay;
            probe.animationTick = 817;
            if (draw() != 1) return fail(id + " ring not drawn");
            std::unordered_map<std::uint32_t, int> actual;
            for (std::uint32_t pixel : pixels) if (pixel != background) ++actual[pixel];
            if (actual != expected || expected.empty()) return fail(id + " full frame/palette mismatch at " + std::to_string(frame));
            ++checkedFrames;
        }
        std::cout << "Mind-control ring: " << id << " frames=" << frames
            << " size=" << first->image.globalWidth << 'x' << first->image.globalHeight << '\n';
    }
    snapshots.front() = snapshot;
    snapshots.front().requiredProgress = 40;
    snapshots.front().progress = 0;
    draw();
    const auto casting = pixels;
    probe.animationTick += 1;
    draw();
    if (pixels != casting) return fail("mind-control arc jitters with the global tick");
    snapshots.front().requiredProgress = 0;
    draw();
    const auto ringOnly = pixels;
    RECT targetBounds{};
    const POINT targetCenter = screenPoint(runtimeObjectDrawPoint(target, {}, camera), viewport, camera);
    if (!resolveRuntimeObjectShpSpriteBounds(target, targetCenter, probe, camera, *cache, targetBounds))
        return fail("control target bounds missing");
    for (int y = targetBounds.top; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (ringOnly[y * width + x] != background)
            return fail("control ring is below the target's head");
    }
    int arcPixels = 0;
    const auto arcColor = packRuntimeDibColor(runtimeOwnerRemapColor(probe, source.owner));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        if (casting[i] != ringOnly[i]) {
            ++arcPixels;
            if (casting[i] != arcColor) return fail("mind-control arc used an electric/white-core palette");
        }
    }
    if (arcPixels < 100) return fail("mind-control attack arc missing");
    selection = {2};
    draw();
    if (pixels != casting) return fail("selected victim did not restore its control arc");
    selection = {1};
    draw();
    if (pixels != casting) return fail("selected controller did not restore its control arc");
    selection.clear();

    auto& moved = world.accessObjects()[1];
    moved.hasRenderPosition = true;
    moved.renderX = 4.5;
    moved.renderY = 0.5;
    draw();
    for (int y = 0; y < height - 15; ++y) for (int x = 0; x < width; ++x) {
        if (ringOnly[y * width + x] != pixels[(y + 15) * width + x])
            return fail("control ring detached from the interpolated unit");
    }
    moved = target;
    world.accessObjects()[0].alive = false;
    draw();
    if (pixels != ringOnly) return fail("permanent control ring depended on a living controller");
    world.accessObjects()[0].alive = true;
    moved.alive = false;
    if (draw() != 0 || std::any_of(pixels.begin(), pixels.end(),
            [](auto pixel) { return pixel != background; })) return fail("dead victim retained a control ring");
    moved = target;
    snapshots.clear();
    if (draw() != 0 || std::any_of(pixels.begin(), pixels.end(),
            [](auto pixel) { return pixel != background; })) return fail("released link retained its overlay");

    // A reviewable preview uses the same object and overlay drawing paths.
    snapshots = {snapshot};
    for (int row = 0; row < 3; ++row) {
        snapshots.front().resourceId = row == 2
            ? context.rules->getSpecialMechanismRules().permaControlledAnimationType
            : snapshot.resourceId;
        snapshots.front().requiredProgress = row == 0 ? 40 : 0;
        snapshots.front().progress = 0;
        std::fill(pixels.begin(), pixels.end(), background);
        for (const auto& object : world.getObjects()) {
            const POINT anchor = screenPoint(runtimeObjectDrawPoint(object, {}, camera), viewport, camera);
            if (!tryDrawObjectShpSprite(nullptr, viewport, object, anchor, probe, camera, *cache))
                return fail("mind-control preview unit could not be drawn: " + object.type);
        }
        if (row == 0) {
            const auto* cast = loadRuntimeAuxiliaryShpSpriteFromCache("YURICNTL", 0, probe, *cache, false);
            if (cast == nullptr) return fail("mind-control cast sprite missing");
            for (const auto& object : world.getObjects()) {
                RuntimeImpactRenderActor actor;
                actor.animId = "YURICNTL";
                actor.x = object.x;
                actor.y = object.y;
                actor.owner = object.owner;
                actor.attackerObjectId = object.instanceId;
                actor.targetObjectId = object.instanceId;
                actor.warheadId = "Controller";
                actor.frameIndex = std::min(4, cast->frameCount - 1);
                actor.centerOnAnchor = true;
                actor.renderIntent = "warheadImpact";
                RuntimeMapRenderStats stats;
                drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            }
            std::cout << "Mind-control cast: YURICNTL frames=" << cast->frameCount << '\n';
        }
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
        std::copy(pixels.begin(), pixels.end(), sheet.begin() + row * width * height);
    }
    if (const char* path = std::getenv("CONCORD_MIND_CONTROL_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height * 3, sheet))
            return fail("could not write mind-control preview");
    }
    std::cout << "Mind-control rendering: frames=" << checkedFrames
        << " arcPixels=" << arcPixels << " houseColor=ok selection=ok pose=ok release=ok\n";
    return true;
}
