// Test-only presentation probe. No simulation tick or object exit is advanced.
// Captures use copied wreck timelines and restore all detached effects on every exit.
bool runRuntimeWaterSinkingRenderProbe(RuntimeWindowState& state, int objectId,
    std::string& error, const std::string& label, bool requireEntrySplash = true) {
    const auto fail = [&](const std::string& why) {
        error = "Water sinking native render: " + label + ": " + why;
        return false;
    };
    const auto found = std::find_if(state.wrecks.begin(), state.wrecks.end(),
        [&](const auto& wreck) { return wreck.object.instanceId == objectId && wreck.sinking; });
    if (found == state.wrecks.end()) return fail("retained sinking body absent");
    const RuntimeWreckState original = *found;
    const auto* object = findRuntimeObjectByIdIncludingInactive(state, objectId);
    if (!object || object->alive || object->health != 0)
        return fail("fixture body still has live gameplay authority");
    const int tickBefore = state.tick;
    const std::string recordBefore = serializeRuntimeWreck(original);
    const char* output = std::getenv("CONCORD_WATER_SINKING_OUTPUT");
    std::filesystem::path directory;
    if (output && *output) {
        directory = std::filesystem::path(output) / "native";
        std::filesystem::create_directories(directory);
    }
    std::string safeLabel = label;
    for (char& c : safeLabel) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    const auto path = [&](const std::string& stage) {
        return directory.empty() ? std::string{} : (directory / (safeLabel + "-" + stage + ".bmp")).string();
    };
    constexpr int width = 640, height = 480;
    const RuntimeCellPoint center{static_cast<int>(std::lround(original.x)),
        static_cast<int>(std::lround(original.y))};
    // Share the exact camera used for the native capture with the anchor contract.
    RuntimeMapCamera probeCamera; probeCamera.initialized = true;
    const RECT probeViewport{0, 0, width, height};
    const auto projectedCenter = runtimeProjectCellXY(center.x, center.y,
        terrainLevelAtRuntimeCell(state, center.x, center.y), probeCamera);
    probeCamera.worldOffsetX = projectedCenter.x - width / 2;
    probeCamera.worldOffsetY = projectedCenter.y - height / 2;
    RECT splashExpected{}; POINT splashAnchor{}, waterContactAnchor{}; int splashFrameIndex = -1;
    double splashWorldX = 0.0, splashWorldY = 0.0, splashWorldLevel = 0.0;
    struct Sample { std::vector<std::uint32_t> pixels; RuntimeMapRenderStats stats;
        int count = 0, minX = 640, minY = 480, maxX = -1, maxY = -1; };
    std::array<Sample, 3> phases;
    Sample baseline, splash, splashOnly, submerged, finished;
    const auto render = [&](Sample& sample, const std::string& stage) {
        const auto stats = drawRuntimeStateOffscreenAtCell(state, center, width, height,
            error, path(stage), false, false, -1, &sample.pixels, false, &probeCamera);
        if (!stats || sample.pixels.size() != static_cast<std::size_t>(width * height)) return false;
        sample.stats = *stats;
        return true;
    };
    const auto measure = [&](Sample& sample, const Sample& background) {
        // Tight enough to reject unrelated map changes, large enough for the real turret/hull.
        for (int y = height / 2 - 110; y < height / 2 + 70; ++y)
            for (int x = width / 2 - 100; x < width / 2 + 100; ++x) {
                const auto i = static_cast<std::size_t>(y * width + x);
                if ((sample.pixels[i] & 0xffffffU) == (background.pixels[i] & 0xffffffU)) continue;
                ++sample.count; sample.minX = std::min(sample.minX, x); sample.maxX = std::max(sample.maxX, x);
                sample.minY = std::min(sample.minY, y); sample.maxY = std::max(sample.maxY, y);
            }
    };
    {
        struct RestorePresentation {
            RuntimeWindowState& state;
            decltype(state.wrecks) wrecks;
            decltype(state.impactEffects) effects;
            ~RestorePresentation() { state.wrecks = std::move(wrecks); state.impactEffects = std::move(effects); }
        } restore{state, state.wrecks, state.impactEffects};
        state.wrecks.clear(); state.impactEffects.clear();
        if (!render(baseline, "water-background")) return false;
        constexpr std::array<int, 3> ruleFrames{0, 4, 8};
        for (std::size_t phase = 0; phase < phases.size(); ++phase) {
            RuntimeWreckState presentation = original;
            presentation.startTick = state.tick - ruleFrames[phase] * std::max(1, state.config.fixedTickHz) / 15;
            state.wrecks.assign(1, presentation);
            if (!render(phases[phase], std::string("body-") + std::to_string(ruleFrames[phase]))) return false;
            measure(phases[phase], baseline);
            const auto& stats = phases[phase].stats;
            const bool realBody = stats.visibleVoxelBodies > baseline.stats.visibleVoxelBodies
                || stats.visibleObjectShpSprites > baseline.stats.visibleObjectShpSprites;
            if (!realBody || stats.missingVoxelProxies > baseline.stats.missingVoxelProxies
                || stats.missingObjectShpSprites > baseline.stats.missingObjectShpSprites
                || phases[phase].count <= 0)
                return fail("frame " + std::to_string(ruleFrames[phase]) + " did not draw original VXL/SHP body pixels");
        }
        if (phases[0].pixels == phases[1].pixels || phases[1].pixels == phases[2].pixels
            || phases[2].count >= phases[0].count || phases[1].count > phases[0].count)
            return fail("three body samples do not show progressive immersion (pixels "
                + std::to_string(phases[0].count) + "/" + std::to_string(phases[1].count)
                + "/" + std::to_string(phases[2].count) + ")");
        // Submerged bottom faces must be clipped at the waterline, not translated below it.
        if (phases[1].maxY > phases[0].maxY + 2 || phases[2].maxY > phases[0].maxY + 2
            || phases[2].minY < phases[0].minY)
            return fail("descending body leaked below its original waterline");
        // At the last retained frame a small stock tank may be completely below water.
        // Its transparent native raster must suppress the fallback marker as well.
        RuntimeWreckState atSubmerged = original;
        atSubmerged.startTick = state.tick - 80 * std::max(1, state.config.fixedTickHz) / 15;
        state.wrecks.assign(1, atSubmerged);
        if (!render(submerged, "body-80-submerged")) return false;
        measure(submerged, baseline);
        if (submerged.count != 0 || submerged.stats.missingVoxelProxies > baseline.stats.missingVoxelProxies
            || submerged.stats.missingObjectShpSprites > baseline.stats.missingObjectShpSprites)
            return fail("fully submerged body left visible pixels or a fallback marker");
        state.wrecks.clear();
        if (!render(finished, "body-removed")) return false;
        measure(finished, baseline);
        if (finished.count != 0 || finished.stats.wreckBodySprites != baseline.stats.wreckBodySprites)
            return fail("removed sinking body still rendered");
        if (requireEntrySplash) {
            RuntimeWreckState atEntry = original; atEntry.startTick = state.tick;
            state.wrecks.assign(1, atEntry);
            for (const auto& effect : restore.effects) {
                if (effect.animId == "H2O_EXP3"
                    && (effect.targetObjectId == objectId
                        || (std::abs(effect.worldX - original.x) < 0.01 && std::abs(effect.worldY - original.y) < 0.01)))
                    state.impactEffects.push_back(effect);
            }
            if (state.impactEffects.empty()) return fail("original H2O_EXP3 entry splash absent");
            if (state.impactEffects.size() != 1) return fail("entry splash does not identify exactly one water-contact actor");
            const auto& entry = state.impactEffects.front();
            const auto actor = makeRuntimeImpactRenderActor(entry);
            splashWorldX = actor.x; splashWorldY = actor.y; splashWorldLevel = actor.level;
            if (std::abs(actor.x - original.x) > 0.001 || std::abs(actor.y - original.y) > 0.001
                || std::abs(actor.level - original.groundLevel) > 0.001)
                return fail("entry splash inherited an airborne or different water-contact position");
            splashFrameIndex = actor.frameIndex;
            splashAnchor = runtimeScreenPoint(runtimeProjectCellXY(actor.x, actor.y, actor.level,
                probeCamera), probeViewport, probeCamera);
            splashAnchor.x += runtimeMuzzlePixelX(actor.screenOffsetX, probeCamera, actor.nativeFixedX);
            splashAnchor.y += runtimeMuzzlePixelY(actor.screenOffsetY, probeCamera, actor.nativeFixedY);
            waterContactAnchor = runtimeScreenPoint(runtimeProjectCellXY(original.x, original.y, original.groundLevel,
                probeCamera), probeViewport, probeCamera);
            if (std::abs(splashAnchor.x - waterContactAnchor.x) > 1
                || std::abs(splashAnchor.y - waterContactAnchor.y) > 1)
                return fail("entry splash pixel anchor does not meet the projected body water-contact point");
            if (!render(splash, "entry-with-splash")) return false;
            measure(splash, phases[0]);
            if (splash.stats.impactAnimDrawn <= phases[0].stats.impactAnimDrawn || splash.count <= 0)
                return fail("original H2O_EXP3 did not draw native pixels");
            // Isolate the actual water animation from the retained body. A body pixel
            // in the difference image must not conceal a misplaced white water column.
            state.wrecks.clear();
            if (!render(splashOnly, "splash-only")) return false;
            measure(splashOnly, baseline);
            if (splashOnly.count <= 0 || splashOnly.stats.impactAnimDrawn <= baseline.stats.impactAnimDrawn)
                return fail("isolated original water splash did not draw native pixels");
            const RuntimeObjectShpSpriteCacheEntry* decoded = nullptr;
            for (const auto& [key, sprite] : state.renderCache.auxiliaryShpSpritesByImage) {
                if (key.rfind("h2o_exp3|", 0) == 0 && sprite.loaded && sprite.frameIndex == splashFrameIndex) {
                    decoded = &sprite; break;
                }
            }
            if (!decoded) return fail("actual decoded entry-splash frame unavailable");
            const auto& image = decoded->image;
            int localMinX = image.width, localMinY = image.height, localMaxX = -1, localMaxY = -1;
            for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
                if (image.pixels[static_cast<std::size_t>(y) * image.width + x] == 0) continue;
                localMinX = std::min(localMinX, x); localMinY = std::min(localMinY, y);
                localMaxX = std::max(localMaxX, x); localMaxY = std::max(localMaxY, y);
            }
            if (localMaxX < 0 || localMaxY < 0) return fail("entry-splash frame is transparent");
            // The original indexed animation is authored about its global canvas
            // center. Use the native projection and the actual decoded opaque crop.
            const int left = splashAnchor.x - image.globalWidth / 2 + image.offsetX;
            const int top = splashAnchor.y - image.globalHeight / 2 + image.offsetY;
            splashExpected = RECT{left + localMinX, top + localMinY, left + localMaxX, top + localMaxY};
            constexpr int rasterTolerance = 2;
            if (std::abs(splashOnly.minX - splashExpected.left) > rasterTolerance
                || std::abs(splashOnly.minY - splashExpected.top) > rasterTolerance
                || std::abs(splashOnly.maxX - splashExpected.right) > rasterTolerance
                || std::abs(splashOnly.maxY - splashExpected.bottom) > rasterTolerance)
                return fail("original splash canvas is detached from its projected water-contact anchor (actual "
                    + std::to_string(splashOnly.minX) + "," + std::to_string(splashOnly.minY) + ","
                    + std::to_string(splashOnly.maxX) + "," + std::to_string(splashOnly.maxY)
                    + " expected " + std::to_string(splashExpected.left) + "," + std::to_string(splashExpected.top)
                    + "," + std::to_string(splashExpected.right) + "," + std::to_string(splashExpected.bottom) + ")");
        }
    }
    const auto restored = std::find_if(state.wrecks.begin(), state.wrecks.end(),
        [&](const auto& wreck) { return wreck.object.instanceId == objectId; });
    if (state.tick != tickBefore || restored == state.wrecks.end()
        || serializeRuntimeWreck(*restored) != recordBefore)
        return fail("render instrumentation changed the authoritative sinking timeline");
    if (!directory.empty()) {
        std::ofstream report(directory / (safeLabel + "-pixels.csv"));
        report << "stage,rule_frame,body_pixels,min_x,min_y,max_x,max_y,voxel_bodies,shp_bodies,impact_drawn\n";
        for (std::size_t i = 0; i < phases.size(); ++i) {
            const auto& s = phases[i];
            report << "body," << (i == 0 ? 0 : i == 1 ? 4 : 8) << ',' << s.count << ',' << s.minX << ',' << s.minY
                << ',' << s.maxX << ',' << s.maxY << ',' << s.stats.visibleVoxelBodies << ','
                << s.stats.visibleObjectShpSprites << ',' << s.stats.impactAnimDrawn << '\n';
        }
        report << "submerged,80," << submerged.count << ',' << submerged.minX << ',' << submerged.minY
            << ',' << submerged.maxX << ',' << submerged.maxY << ',' << submerged.stats.visibleVoxelBodies
            << ',' << submerged.stats.visibleObjectShpSprites << ',' << submerged.stats.impactAnimDrawn << '\n';
        report << "removed,81," << finished.count << ',' << finished.minX << ',' << finished.minY
            << ',' << finished.maxX << ',' << finished.maxY << ',' << finished.stats.visibleVoxelBodies
            << ',' << finished.stats.visibleObjectShpSprites << ',' << finished.stats.impactAnimDrawn << '\n';
        if (requireEntrySplash) {
            report << "splash,0," << splash.count << ',' << splash.minX << ',' << splash.minY << ',' << splash.maxX
                << ',' << splash.maxY << ',' << splash.stats.visibleVoxelBodies << ','
                << splash.stats.visibleObjectShpSprites << ',' << splash.stats.impactAnimDrawn << '\n';
            report << "splash-only," << splashFrameIndex << ',' << splashOnly.count << ',' << splashOnly.minX << ',' << splashOnly.minY
                << ',' << splashOnly.maxX << ',' << splashOnly.maxY << ',' << splashOnly.stats.visibleVoxelBodies
                << ',' << splashOnly.stats.visibleObjectShpSprites << ',' << splashOnly.stats.impactAnimDrawn << '\n';
            std::ofstream anchors(directory / (safeLabel + "-splash-anchor.csv"));
            anchors << "frame,world_x,world_y,world_level,anchor_x,anchor_y,water_contact_x,water_contact_y,expected_min_x,expected_min_y,expected_max_x,expected_max_y,actual_min_x,actual_min_y,actual_max_x,actual_max_y,pixels\n";
            anchors << std::setprecision(17) << splashFrameIndex << ',' << splashWorldX << ',' << splashWorldY << ',' << splashWorldLevel
                << ',' << splashAnchor.x << ',' << splashAnchor.y << ',' << waterContactAnchor.x << ',' << waterContactAnchor.y
                << ',' << splashExpected.left << ',' << splashExpected.top
                << ',' << splashExpected.right << ',' << splashExpected.bottom << ',' << splashOnly.minX << ',' << splashOnly.minY
                << ',' << splashOnly.maxX << ',' << splashOnly.maxY << ',' << splashOnly.count << '\n';
        }
    }
    std::cout << "Water sinking native render: " << label << " body pixels " << phases[0].count << '/'
        << phases[1].count << '/' << phases[2].count;
    if (requireEntrySplash) std::cout << ", original splash " << splash.count << " pixels, isolated "
        << splashOnly.count << " at projected water contact";
    std::cout << ", waterline clipping and unchanged timeline passed\n";
    return true;
}
