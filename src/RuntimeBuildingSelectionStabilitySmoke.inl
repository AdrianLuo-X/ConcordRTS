// Runs after the independent sprite/hit oracle, with the same live projection.
// Compare production overlay pixels across animation/facing changes instead of
// requiring the cage to enclose every moving sprite pixel.
if (projectedObject && frameGeometry.valid) {
    const auto& f = frameGeometry;
    const LONG hw = projectedObject->footprintHalfWidth, hh = projectedObject->footprintHalfHeight;
    const bool regular = f.left.x + f.right.x == f.top.x + f.bottom.x
        && f.left.y + f.right.y == f.top.y + f.bottom.y
        && (f.right.x - f.top.x) * hh == (f.right.y - f.top.y) * hw
        && (f.top.x - f.left.x) * hh == (f.left.y - f.top.y) * hw
        && f.right.x > f.top.x && f.top.x > f.left.x;
    ++regularFrameChecks;
    if (!regular) recordFailure(id, scene.name, "selection foundation ceased to be an isometric rectangle");

    const POINT anchor = runtimeScreenPoint(runtimeProjectCellXY(object.x, object.y,
        scene.level, s.camera), viewport, s.camera);
    std::vector<LONG> selection;
    for (const POINT p : {f.left, f.top, f.right, f.bottom}) {
        selection.push_back(p.x - anchor.x); selection.push_back(p.y - anchor.y);
    }
    selection.push_back(f.verticalHeight);
    const auto health = runtimeStructureHealthBarLayout(f);
    if (!health.valid) recordFailure(id, scene.name, "health layout missing from valid foundation");
    if (health.valid) {
        const auto first = health.segmentAnchors.front();
        const LONG edgeRun = f.top.x - f.left.x, edgeRise = f.left.y - f.top.y;
        for (const auto p : health.segmentAnchors) {
            const long long cross = static_cast<long long>(p.y-first.y)*edgeRun
                + static_cast<long long>(p.x-first.x)*edgeRise;
            if (2*std::abs(cross) > edgeRun)
                recordFailure(id, scene.name, "health strip is not parallel to the crown edge");
            if (p.x-4 < health.bounds.left || p.x+5 >= health.bounds.right
                || p.y < health.bounds.top || p.y+6 >= health.bounds.bottom)
                recordFailure(id, scene.name, "health pip extends outside its reported bounds");
        }
    }
    for (const auto& p : health.segmentAnchors) {
        selection.push_back(p.x - anchor.x); selection.push_back(p.y - anchor.y);
    }
    const auto [reference, firstPose] = referenceSelection.emplace(scene.tile, selection);
    if (!firstPose) {
        ++stableGeometryChecks;
        if (selection != reference->second)
            recordFailure(id, scene.name, "animation/facing/health changed cage or health anchors");
    }

    GdiFlush();
    std::fill(overlay.pixels, overlay.pixels + overlay.size(), atlasBackground);
    // The interaction entry point subtracts the sidebar from its client.
    const RECT overlayClient{0, 0, probeWidth + runtimeSidebarWidth, probeHeight};
    drawRuntimeInteractionOverlay(overlay.dc, overlayClient, s, context);
    GdiFlush();
    if (s.lastStructureSelectionFrames != 1
        || !runtimeRectEquals(s.lastStructureSelectionFootprintBounds, f.footprintBounds)
        || s.lastStructureSelectionMaxHeight != f.verticalHeight)
        recordFailure(id, scene.name, "live selection overlay differs from the fixed foundation");
    if (runtimeObjectShouldDrawHealthBar(s, *projectedObject->object)) {
        ++healthLayoutChecks;
        if (!runtimeRectEquals(s.lastStructureHealthBarBounds, health.bounds)
            || s.lastStructureHealthBarSegments != static_cast<int>(health.segmentAnchors.size()))
            recordFailure(id, scene.name, "live health bar moved or resized with the animation");
        // Stock scenery with Strength=1 cannot have a living damaged state.
        if (scene.damaged && object.health < runtimeMaxHealthForObject(s, object)
            && s.lastStructureHealthBarSegments > 1
            && s.lastStructureHealthBarFilledSegments >= s.lastStructureHealthBarSegments)
            recordFailure(id, scene.name, "damage did not reduce the filled health pips");
    }
    if (!scene.damaged) {
        std::vector<std::array<LONG, 3>> pixels;
        for (int y = 0; y < probeHeight; ++y) for (int x = 0; x < probeWidth; ++x) {
            const auto pixel = overlay.pixels[static_cast<std::size_t>(y) * probeWidth + x] & 0x00ffffffU;
            if (pixel != atlasBackground) pixels.push_back({x - anchor.x, y - anchor.y, static_cast<LONG>(pixel)});
        }
        const auto [referencePixels, firstRaster] = referenceOverlay.emplace(scene.tile, pixels);
        if (!firstRaster) {
            ++stableRasterChecks;
            if (pixels != referencePixels->second)
                recordFailure(id, scene.name, "production selection/health pixels changed for an unchanged foundation and health");
        }
    }
    const bool pictureType = id == "GTGCAN" || id == "GACNST" || id == "GAWEAP"
        || id == "NANRCT" || id == "TESLA" || id == "NALASR" || id == "NASAM" || id == "NAFLAK";
    const bool picturePose = scene.name == "healthy" || scene.name == "working"
        || scene.name == "damaged" || scene.name == "buildup-last"
        || scene.name == "turret-0" || scene.name == "turret-128";
    if (pictureType && picturePose) {
        for (std::size_t index = 0; index < overlay.size(); ++index)
            if ((overlay.pixels[index] & 0x00ffffffU) != atlasBackground)
                surface.pixels[index] = overlay.pixels[index];
        if (!writeRuntimeSmokeBmp(root + "/stable-" + id + "-" + scene.name + ".bmp",
                surface.pixels, probeWidth, probeHeight, error)) return false;
    }
}
