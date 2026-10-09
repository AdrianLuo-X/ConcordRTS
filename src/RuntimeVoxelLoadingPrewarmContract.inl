namespace {
const char* runtimeVoxelLoadingPrewarmContractDisabledCacheEnvironment(const char* name) {
    if (std::strcmp(name, "CONCORD_VXL_DISABLE_CACHE") == 0) return "1";
    return runtimeVoxelFaceContractEnvironment(name);
}
const char* runtimeVoxelLoadingPrewarmContractAutoEnvironment(const char* name) {
    if (std::strcmp(name, "CONCORD_VXL_FIXED_SCALE_ENABLE") == 0) return "0";
    if (std::strcmp(name, "CONCORD_VXL_PART_SCALE_MODE") == 0) return "shared";
    return runtimeVoxelFaceContractEnvironment(name);
}

bool runtimeVoxelLoadingPrewarmContractEntryEqual(const RuntimeVoxelCacheEntry& a,
    const RuntimeVoxelCacheEntry& b) {
    auto aImage = a.image, bImage = b.image;
    // A cold original query and a prewarmed query intentionally differ only in
    // this hit diagnostic. The complete actual cache key is still compared.
    aImage.rasterCacheHit = bImage.rasterCacheHit = false;
    if (!runtimeVoxelFaceContractImageEqual(aImage, bImage)
        || a.loadAttempted != b.loadAttempted || a.loaded != b.loaded || a.hasSprite != b.hasSprite
        || a.vehicleBodyPoseUse != b.vehicleBodyPoseUse || a.shadowPixels != b.shadowPixels
        || a.shadowSectionRequested != b.shadowSectionRequested || a.shadowSectionApplied != b.shadowSectionApplied
        || a.facingBucket != b.facingBucket || a.animationFrameIndex != b.animationFrameIndex
        || a.errorMessage != b.errorMessage || a.modelBounds.valid != b.modelBounds.valid
        || !runtimeVoxelFaceContractDoubleEqual(a.rasterFitScale, b.rasterFitScale)
        || !runtimeVoxelFaceContractDoubleEqual(a.projectedRangeX, b.projectedRangeX)
        || !runtimeVoxelFaceContractDoubleEqual(a.projectedRangeY, b.projectedRangeY)
        || a.metadata.imageId != b.metadata.imageId || a.metadata.vxlPath != b.metadata.vxlPath
        || a.metadata.hvaPath != b.metadata.hvaPath || a.metadata.vxlSectionCount != b.metadata.vxlSectionCount
        || a.metadata.hvaSectionCount != b.metadata.hvaSectionCount || a.metadata.hvaFrameCount != b.metadata.hvaFrameCount
        || a.metadata.vxlFileSize != b.metadata.vxlFileSize || a.metadata.hvaFileSize != b.metadata.hvaFileSize
        || a.metadata.vxlSectionSample != b.metadata.vxlSectionSample || a.metadata.hvaSectionSample != b.metadata.hvaSectionSample
        || a.shadowSpanCache.hasNonzero() != b.shadowSpanCache.hasNonzero()
        || a.shadowSpanCache.usable() != b.shadowSpanCache.usable()
        || a.shadowSpanCache.rebuilds() != b.shadowSpanCache.rebuilds()
        || a.shadowSpanCache.spans().size() != b.shadowSpanCache.spans().size()) return false;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (!runtimeVoxelFaceContractDoubleEqual(a.modelBounds.minimum[axis], b.modelBounds.minimum[axis])
            || !runtimeVoxelFaceContractDoubleEqual(a.modelBounds.maximum[axis], b.modelBounds.maximum[axis])) return false;
    for (std::size_t index = 0; index < a.shadowSpanCache.spans().size(); ++index) {
        const auto& x = a.shadowSpanCache.spans()[index]; const auto& y = b.shadowSpanCache.spans()[index];
        if (x.y != y.y || x.left != y.left || x.right != y.right) return false;
    }
    return true;
}

bool runtimeVoxelLoadingPrewarmContractMetadataEqual(const RuntimeVoxelMetadata& a,
    const RuntimeVoxelMetadata& b) {
    return a.imageId == b.imageId && a.vxlPath == b.vxlPath && a.hvaPath == b.hvaPath
        && a.vxlSectionCount == b.vxlSectionCount && a.hvaSectionCount == b.hvaSectionCount
        && a.hvaFrameCount == b.hvaFrameCount && a.vxlFileSize == b.vxlFileSize
        && a.hvaFileSize == b.hvaFileSize && a.vxlSectionSample == b.vxlSectionSample
        && a.hvaSectionSample == b.hvaSectionSample;
}

bool runtimeVoxelLoadingPrewarmContractTransformsEqual(const std::vector<RuntimeHvaTransform>& a,
    const std::vector<RuntimeHvaTransform>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (a[index].valid != b[index].valid) return false;
        for (std::size_t component = 0; component < a[index].m.size(); ++component)
            if (!runtimeVoxelFaceContractDoubleEqual(a[index].m[component], b[index].m[component])) return false;
    }
    return true;
}

bool runtimeVoxelLoadingPrewarmContractSeedPreserved(const RuntimeMapRenderCache& seed,
    const RuntimeMapRenderCache& current) {
    for (const auto& [key, entry] : seed.voxelObjectsByType) {
        const auto found = current.voxelObjectsByType.find(key);
        if (found == current.voxelObjectsByType.end()
            || !runtimeVoxelLoadingPrewarmContractEntryEqual(entry, found->second)) return false;
    }
    for (const auto& [key, rasterKey] : seed.stableVoxelCacheKeyByLookup) {
        const auto found = current.stableVoxelCacheKeyByLookup.find(key);
        if (found == current.stableVoxelCacheKeyByLookup.end() || found->second != rasterKey) return false;
    }
    if (seed.voxelSourcesByImage.size() != current.voxelSourcesByImage.size()
        || seed.voxelFixedScaleByKey.size() != current.voxelFixedScaleByKey.size()) return false;
    for (const auto& [key, source] : seed.voxelSourcesByImage) {
        const auto found = current.voxelSourcesByImage.find(key);
        if (found == current.voxelSourcesByImage.end()) return false;
        const auto& actual = found->second;
        if (source.loadAttempted != actual.loadAttempted || source.loaded != actual.loaded
            || !runtimeVoxelLoadingPrewarmContractMetadataEqual(source.metadata, actual.metadata)
            || source.vxlBytes != actual.vxlBytes || source.hvaBytes != actual.hvaBytes
            || source.errorMessage != actual.errorMessage
            || !runtimeVoxelLoadingPrewarmContractTransformsEqual(source.firstFrameTransforms, actual.firstFrameTransforms)
            || source.transformsByFrame.size() != actual.transformsByFrame.size()) return false;
        for (const auto& [frame, transforms] : source.transformsByFrame) {
            const auto transform = actual.transformsByFrame.find(frame);
            if (transform == actual.transformsByFrame.end()
                || !runtimeVoxelLoadingPrewarmContractTransformsEqual(transforms, transform->second)) return false;
        }
    }
    for (const auto& [key, fixed] : seed.voxelFixedScaleByKey) {
        const auto found = current.voxelFixedScaleByKey.find(key);
        if (found == current.voxelFixedScaleByKey.end()) return false;
        const auto& actual = found->second;
        if (fixed.loadAttempted != actual.loadAttempted || fixed.valid != actual.valid
            || fixed.category != actual.category || fixed.mode != actual.mode || fixed.errorMessage != actual.errorMessage
            || !runtimeVoxelFaceContractDoubleEqual(fixed.fitScale, actual.fitScale)
            || !runtimeVoxelFaceContractDoubleEqual(fixed.assetMaxRangeX, actual.assetMaxRangeX)
            || !runtimeVoxelFaceContractDoubleEqual(fixed.assetMaxRangeY, actual.assetMaxRangeY)
            || !runtimeVoxelFaceContractDoubleEqual(fixed.targetWidth, actual.targetWidth)
            || !runtimeVoxelFaceContractDoubleEqual(fixed.targetHeight, actual.targetHeight)) return false;
    }
    return seed.voxelPaletteLookupLoadAttempted == current.voxelPaletteLookupLoadAttempted
        && seed.voxelPaletteLookupLoaded == current.voxelPaletteLookupLoaded
        && seed.voxelPaletteLookup == current.voxelPaletteLookup
        && seed.voxelPaletteLookupSourcePath == current.voxelPaletteLookupSourcePath
        && seed.voxelPaletteLookupErrorMessage == current.voxelPaletteLookupErrorMessage
        && seed.voxelPaletteFirstRemap == current.voxelPaletteFirstRemap
        && seed.voxelPaletteLastRemap == current.voxelPaletteLastRemap
        && seed.voxelPaletteSectionCount == current.voxelPaletteSectionCount
        && seed.voxelPaletteNeutralSection == current.voxelPaletteNeutralSection;
}

std::vector<unsigned char> runtimeVoxelLoadingPrewarmContractHva(int frames) {
    // Independent native HVA encoding, one named CUBE limb with moving x/y
    // translation. The real parser supplies metadata and frame transforms.
    std::vector<unsigned char> bytes(40U + static_cast<std::size_t>(frames) * 48U, 0);
    std::memcpy(bytes.data(), "prewarm HVA", 11);
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset + shift / 8] = static_cast<unsigned char>(value >> shift);
    };
    put32(16, static_cast<std::uint32_t>(frames)); put32(20, 1);
    std::memcpy(bytes.data() + 24, "CUBE", 4);
    for (int frame = 0; frame < frames; ++frame) {
        const auto matrix = 40U + static_cast<std::size_t>(frame) * 48U;
        for (int axis : {0, 5, 10}) {
            const float value = 1.0F; std::memcpy(bytes.data() + matrix + axis * 4U, &value, 4);
        }
        const float x = static_cast<float>(frame) * 0.125F;
        const float y = static_cast<float>(frame % 3) * -0.25F;
        std::memcpy(bytes.data() + matrix + 3U * 4U, &x, 4);
        std::memcpy(bytes.data() + matrix + 7U * 4U, &y, 4);
    }
    return bytes;
}

bool runtimeVoxelLoadingPrewarmContractSeedBytes(const std::string& image,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    std::vector<unsigned char> vxlBytes, int frames, std::string& error) {
    const std::string key = normalizedLowerPath(context.assetRootPath)
        + "|vxl=" + toLowerAscii(fileNameForVxlImageId(image))
        + "|hva=" + toLowerAscii(fileNameForHvaImageId(image));
    RuntimeVoxelSourceCacheEntry source;
    source.loadAttempted = true;
    source.vxlBytes = std::move(vxlBytes);
    source.hvaBytes = runtimeVoxelLoadingPrewarmContractHva(frames);
    source.loaded = parseRuntimeVxlMetadataFromBytes(source.vxlBytes, "prewarm/" + image + ".vxl", source.metadata, error)
        && parseRuntimeHvaMetadataFromBytes(source.hvaBytes, "prewarm/" + image + ".hva", source.metadata, error)
        && parseRuntimeHvaFrameTransformsFromBytes(source.hvaBytes, source.metadata.hvaPath, source.firstFrameTransforms, error);
    source.metadata.imageId = image;
    if (!source.loaded) return false;
    cache.voxelSourcesByImage.emplace(key, std::move(source));
    return true;
}

bool runtimeVoxelLoadingPrewarmContractSeedSource(const std::string& image,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache, int frames, std::string& error) {
    return runtimeVoxelLoadingPrewarmContractSeedBytes(image, context, cache,
        runtimeVoxelFaceContractCube(false), frames, error);
}

struct RuntimeVoxelLoadingPrewarmContractProgressException {};

bool runtimeVoxelLoadingPrewarmContractStatsEqual(const RuntimeVoxelLoadingPrewarmStats& a,
    const RuntimeVoxelLoadingPrewarmStats& b) {
    return a.referenceSkipped == b.referenceSkipped && a.v1Reference == b.v1Reference
        && a.nestedSkipped == b.nestedSkipped && a.cacheDisabledSkipped == b.cacheDisabledSkipped
        && a.paletteNotReadySkipped == b.paletteNotReadySkipped && a.unsupportedProfileSkipped == b.unsupportedProfileSkipped
        && a.budgetStopped == b.budgetStopped && a.timeStopped == b.timeStopped && a.allocationStopped == b.allocationStopped
        && a.families == b.families && a.duplicateFamilies == b.duplicateFamilies
        && a.unavailableSources == b.unavailableSources && a.oversizedSources == b.oversizedSources
        && a.highHvaSources == b.highHvaSources && a.fixedScaleNotReady == b.fixedScaleNotReady
        && a.attempts == b.attempts && a.existingEntries == b.existingEntries && a.decodedEntries == b.decodedEntries
        && a.failedEntries == b.failedEntries && a.newEntries == b.newEntries && a.addedPayloadBytes == b.addedPayloadBytes
        && runtimeVoxelFaceContractDoubleEqual(a.elapsedMilliseconds, b.elapsedMilliseconds);
}

struct RuntimeVoxelLoadingPrewarmContractProgressProbe {
    const GameWorld* world = nullptr;
    const RuntimeMapRenderContext* context = nullptr;
    const RuntimeVoxelLoadingPrewarmLimits* limits = nullptr;
    std::size_t calls = 0;
    bool invalid = false;
    bool exhaustDeadline = false;
    bool throwOnCall = false;
};

void runtimeVoxelLoadingPrewarmContractProgress(void* raw) {
    auto& probe = *static_cast<RuntimeVoxelLoadingPrewarmContractProgressProbe*>(raw);
    ++probe.calls;
    auto* outer = runtimeVoxelLoadingPrewarmState;
    if (outer == nullptr || !outer->progressCallbackActive || outer->context != probe.context) {
        probe.invalid = true; return;
    }
    const auto attempts = outer->stats->attempts;
    RuntimeVoxelLoadingPrewarmStats nested;
    prewarmRuntimeCurrentWorldNormalVoxels(*probe.world, *probe.context, *outer->cache, *probe.limits, nested);
    if (!nested.nestedSkipped || nested.attempts || nested.newEntries
        || runtimeVoxelLoadingPrewarmState != outer
        || runtimeVoxelLoadingPrewarmRequest(*outer, "GTNK", "__PW_TANK__", 0, 0.0, 0, 0) != nullptr
        || outer->stats->attempts != attempts) probe.invalid = true;
    const auto before = *outer->stats;
    const auto rasterCount = outer->cache->voxelObjectsByType.size();
    const auto lookupCount = outer->cache->stableVoxelCacheKeyByLookup.size();
    const auto aliasCall = [&] {
        prewarmRuntimeCurrentWorldNormalVoxels(*probe.world, *probe.context, *outer->cache, *probe.limits, *outer->stats);
        if (!runtimeVoxelLoadingPrewarmContractStatsEqual(before, *outer->stats)
            || runtimeVoxelLoadingPrewarmState != outer || !outer->progressCallbackActive
            || outer->cache->voxelObjectsByType.size() != rasterCount
            || outer->cache->stableVoxelCacheKeyByLookup.size() != lookupCount) probe.invalid = true;
    };
    aliasCall();
    { RuntimeVoxelLoadingPrewarmReferenceScope total(true); aliasCall(); }
    { RuntimeRendererEnvironmentScope disabled(runtimeVoxelLoadingPrewarmContractDisabledCacheEnvironment); aliasCall(); }
    if (probe.exhaustDeadline) outer->started = std::chrono::steady_clock::now() - std::chrono::seconds(20);
    if (probe.throwOnCall) throw RuntimeVoxelLoadingPrewarmContractProgressException{};
}
} // namespace

bool runRuntimeVoxelLoadingPrewarmAssetContractCheck(const RuntimeMapRenderContext& assets, std::string& error) {
    error.clear();
    const auto fail = [&](const std::string& detail) { error = "voxel loading prewarm: " + detail; return false; };
    RuntimeVoxelFaceContractEnvironmentScope profile("fixedscale", "0");
    RuntimeVoxelLoadingPrewarmReferenceScope optimized(false);
    RuntimeVoxelLoadingPrewarmV1ReferenceScope defaultOrder(false);
    IniFile rulesIni, artIni;
    RulesDatabase rules; ArtDatabase art;
    if (!rulesIni.loadFromString("[VehicleTypes]\n0=__PW_TANK__\n1=__PW_YURI__\n2=__PW_HIGH__\n3=__PW_AUTO__\n4=__PW_BAD__\n"
            "[AircraftTypes]\n0=__PW_AIR__\n[__PW_TANK__]\nImage=GTNK\nTurret=yes\nWalkRate=1\n"
            "[__PW_YURI__]\nImage=YAGGUN\nTurret=no\n[__PW_HIGH__]\nImage=__PW_HIGH_IMAGE__\nTurret=no\n"
            "[__PW_AIR__]\nImage=SCHP\nTurret=no\nWalkRate=1\n"
            "[__PW_AUTO__]\nImage=__PW_AUTO_IMAGE__\nTurret=yes\n"
            "[__PW_BAD__]\nImage=__PW_BAD_IMAGE__\nTurret=no\n")
        || !rules.loadFromIni(rulesIni)
        || !artIni.loadFromString("[GTNK]\nVoxel=yes\nUseTurretShadow=yes\n"
            "[YAGGUN]\nVoxel=yes\n[SCHP]\nVoxel=yes\n[__PW_HIGH_IMAGE__]\nVoxel=yes\n"
            "[__PW_AUTO_IMAGE__]\nVoxel=yes\nUseTurretShadow=yes\n[__PW_BAD_IMAGE__]\nVoxel=yes\n")
        || !art.loadFromIni(artIni)) return fail("stock family rules/art fixture did not load");
    auto context = assets;
    context.rules = &rules; context.art = &art; context.stableRenderConfiguration = true;
    context.animationTick = 0; context.wrecks = nullptr;
    GameWorld world;
    int id = 1;
    for (const char* type : {"__PW_TANK__", "__PW_AIR__", "__PW_YURI__", "__PW_TANK__"}) {
        WorldObjectInstance object;
        object.instanceId = id++; object.kind = WorldObjectKind::Unit;
        object.type = type; object.alive = true; object.health = 100;
        object.owner = "Americans"; object.x = id; object.y = 5; object.facing = 0;
        object.mission = "Move"; object.renderMoveActive = true;
        world.accessObjects().push_back(object);
    }
    auto ignored = world.getObjects().front(); ignored.instanceId = id++; ignored.alive = false;
    world.accessObjects().push_back(ignored);
    ignored.instanceId = id++; ignored.alive = true; ignored.kind = WorldObjectKind::Infantry;
    world.accessObjects().push_back(ignored);
    ignored.instanceId = id++; ignored.kind = WorldObjectKind::Structure;
    world.accessObjects().push_back(ignored);
    RuntimeMapRenderCache seed;
    // Seed through the original actual producer, like existing loading. Source,
    // VPL and fixed-scale keys are not fabricated by this contract.
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& object = world.getObjects()[index];
        auto* body = loadRuntimeVoxelFromCache(object, context, seed);
        if (body == nullptr || !body->hasSprite || body->image.pixels.empty()) return fail("actual stock body seed " + object.type);
        if (index == 0) for (const auto& pair : std::array<std::pair<std::vector<std::string>, std::string>, 4>{{
                {runtimeUnitTurretImageCandidates(object, context), object.type + "|turret"},
                {runtimeUnitBarrelImageCandidates(object, context), object.type + "|barrel"},
                {runtimeUnitTurretImageCandidates(object, context), object.type + "|turret-shadow"},
                {runtimeUnitBarrelImageCandidates(object, context), object.type + "|barrel-shadow"}}}) {
            const double relative = pair.second.find("barrel") != std::string::npos
                ? runtimeVxlBarrelRelativeScale() : runtimeVxlTurretRelativeScale();
            const double fit = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(body->rasterFitScale, relative) : 0.0;
            bool loaded = false;
            for (const auto& image : pair.first)
                if (auto* part = loadRuntimeVoxelImageFromCache(image, pair.second, 0, context, seed, fit)) {
                    loaded = part->hasSprite; if (loaded) break;
                }
            if (!loaded) return fail("actual stock part seed " + pair.second);
        }
    }
    const auto hash = world.deterministicSimulationStateHash();
    RuntimeVoxelLoadingPrewarmLimits limits;
    RuntimeVoxelLoadingPrewarmStats stats;
    if (limits.maximumWallMilliseconds != 15000.0 || limits.loadingProgress != nullptr
        || limits.loadingProgressContext != nullptr || limits.maximumNewEntries != 2048U
        || limits.maximumAttempts != 4096U || limits.maximumAddedPayloadBytes != 64U * 1024U * 1024U)
        return fail("default soft/hard budgets changed outside approved scope");
    const auto sourceCount = seed.voxelSourcesByImage.size();
    const auto fixedCount = seed.voxelFixedScaleByKey.size();
    std::size_t transformCount = 0;
    for (const auto& pair : seed.voxelSourcesByImage) transformCount += pair.second.transformsByFrame.size();
    std::size_t cases = 0, animated = 0, shadow = 0, warmHits = 0, coldFallbacks = 0, totalNew = 0;
    // Two forced-positive loading routes independently replay all published
    // images and unfinished cold fallbacks through the original real producer.
    for (int version = 0; version < 2; ++version) {
    RuntimeVoxelLoadingPrewarmV1ReferenceScope order(version != 0);
    RuntimeMapRenderCache original = seed, warmed = seed;
    const auto routeWarmHits = warmHits, routeAnimated = animated, routeShadow = shadow;
    prewarmRuntimeCurrentWorldNormalVoxels(world, context, warmed, limits, stats);
    std::size_t afterTransformCount = 0;
    for (const auto& pair : warmed.voxelSourcesByImage) afterTransformCount += pair.second.transformsByFrame.size();
    if (stats.families != 3 || stats.duplicateFamilies != 1 || stats.newEntries == 0
        || stats.referenceSkipped || stats.v1Reference != (version != 0)
        || stats.paletteNotReadySkipped || stats.cacheDisabledSkipped
        || stats.budgetStopped || stats.allocationStopped
        || stats.attempts > limits.maximumAttempts || stats.newEntries > limits.maximumNewEntries
        || stats.addedPayloadBytes == 0 || stats.addedPayloadBytes > limits.maximumAddedPayloadBytes
        || warmed.voxelObjectsByType.size() != seed.voxelObjectsByType.size() + stats.newEntries
        || warmed.voxelSourcesByImage.size() != sourceCount || warmed.voxelFixedScaleByKey.size() != fixedCount
        || afterTransformCount != transformCount
        || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, warmed)
        || world.deterministicSimulationStateHash() != hash)
        return fail("actual stock warm budget/source/authority coverage was not exercised");
    totalNew += stats.newEntries;
    std::vector<std::string> publishedKeys;
    for (const auto& pair : warmed.voxelObjectsByType)
        if (seed.voxelObjectsByType.find(pair.first) == seed.voxelObjectsByType.end()) publishedKeys.push_back(pair.first);
    if (publishedKeys.size() != stats.newEntries) return fail("published key census differs from actual admission count");
    // Replay the full domain even if the time budget stopped early. Published
    // poses must hit; unfinished poses must execute unchanged cold fallback.
    for (std::size_t objectIndex = 0; objectIndex < 3; ++objectIndex) {
        const auto& object = world.getObjects()[objectIndex];
        const auto images = resolveObjectVoxelImageIdCandidates(object, context);
        std::string image;
        RuntimeVoxelLoadingPrewarmStats selection;
        const auto* source = runtimeVoxelLoadingPrewarmChooseSource(images, context, warmed, limits, selection, image);
        if (source == nullptr) return fail("actual stock source vanished");
        for (int frame = 0; frame < source->metadata.hvaFrameCount; ++frame) for (int facing = 0; facing < 32; ++facing) {
            auto* expected = loadRuntimeVoxelImageFromCacheUnbatched(image, object.type, facing, context, original, 0.0,
                runtimeObjectVoxelShadowSection(object, context), frame, 0, nullptr);
            auto* actual = loadRuntimeVoxelImageFromCacheUnbatched(image, object.type, facing, context, warmed, 0.0,
                runtimeObjectVoxelShadowSection(object, context), frame, 0, nullptr);
            if (!expected || !actual
                || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expected, *actual))
                return fail("actual full body image/aux/key mismatch " + object.type + " f=" + std::to_string(facing) + " hva=" + std::to_string(frame));
            if (actual->image.rasterCacheHit) ++warmHits; else ++coldFallbacks;
            ++cases; if (frame != 0) ++animated;
            if (std::any_of(actual->shadowPixels.begin(), actual->shadowPixels.end(), [](unsigned char p) { return p != 0; })) ++shadow;
            if (objectIndex == 0 && frame == 0) {
                for (const auto& part : std::array<std::pair<const char*, double>, 4>{{
                        {"|turret", runtimeVxlTurretRelativeScale()}, {"|barrel", runtimeVxlBarrelRelativeScale()},
                        {"|turret-shadow", runtimeVxlTurretRelativeScale()}, {"|barrel-shadow", runtimeVxlBarrelRelativeScale()}}}) {
                    const bool barrel = std::strstr(part.first, "barrel") != nullptr;
                    const auto candidates = barrel ? runtimeUnitBarrelImageCandidates(object, context)
                        : runtimeUnitTurretImageCandidates(object, context);
                    std::string partImage; RuntimeVoxelLoadingPrewarmStats pick;
                    if (!runtimeVoxelLoadingPrewarmChooseSource(candidates, context, warmed, limits, pick, partImage)) return fail("actual part source missing");
                    const double fit = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(actual->rasterFitScale, part.second) : 0.0;
                    auto* oldPart = loadRuntimeVoxelImageFromCacheUnbatched(partImage, object.type + part.first,
                        facing, context, original, fit, 0, 0, 0, nullptr);
                    auto* newPart = loadRuntimeVoxelImageFromCacheUnbatched(partImage, object.type + part.first,
                        facing, context, warmed, fit, 0, 0, 0, nullptr);
                    if (!oldPart || !newPart
                        || !runtimeVoxelLoadingPrewarmContractEntryEqual(*oldPart, *newPart)) return fail("actual four suffix/fit-bit image mismatch");
                    if (newPart->image.rasterCacheHit) ++warmHits; else ++coldFallbacks;
                    ++cases;
                }
            }
        }
    }
    if (warmHits - routeWarmHits < stats.newEntries || animated == routeAnimated || shadow == routeShadow)
        return fail("published stock poses were not genuinely warm, or animated/shadow probes absent");
    for (const auto& key : publishedKeys) {
        const auto old = original.voxelObjectsByType.find(key), current = warmed.voxelObjectsByType.find(key);
        if (old == original.voxelObjectsByType.end() || current == warmed.voxelObjectsByType.end()
            || current->second.image.rasterCacheKey != key || !current->second.image.rasterCacheHit
            || !runtimeVoxelLoadingPrewarmContractEntryEqual(old->second, current->second))
            return fail("published full-key node was not replayed by independent original producer");
    }
    // Existing nodes are retained unchanged; a second pass may hit but never
    // creates duplicate image keys or new source/HVA/fixed-scale allocations.
    RuntimeVoxelLoadingPrewarmStats second;
    const auto residentCount = warmed.voxelObjectsByType.size();
    const auto beforeRepeat = warmed;
    prewarmRuntimeCurrentWorldNormalVoxels(world, context, warmed, limits, second);
    if (second.newEntries != 0 || second.decodedEntries != 0 || second.existingEntries == 0
        || second.v1Reference != (version != 0)
        || !runtimeVoxelLoadingPrewarmContractSeedPreserved(beforeRepeat, warmed)
        || warmed.voxelObjectsByType.size() != residentCount) return fail("duplicate repeat added a raster");
    for (int mode = 0; mode < 5; ++mode) {
        auto bounded = limits;
        if (mode == 0) bounded.maximumNewEntries = 0;
        if (mode == 1) bounded.maximumAttempts = 0;
        if (mode == 2) bounded.maximumAddedPayloadBytes = 1;
        if (mode == 3) bounded.maximumWallMilliseconds = 0;
        if (mode == 4) bounded.maximumNewEntries = 1;
        auto budgetCache = seed;
        RuntimeVoxelLoadingPrewarmStats limited;
        prewarmRuntimeCurrentWorldNormalVoxels(world, context, budgetCache, bounded, limited);
        if (limited.v1Reference != (version != 0)
            || limited.newEntries > bounded.maximumNewEntries || limited.attempts > bounded.maximumAttempts
            || limited.addedPayloadBytes > bounded.maximumAddedPayloadBytes
            || budgetCache.voxelObjectsByType.size() != seed.voxelObjectsByType.size() + limited.newEntries
            || budgetCache.voxelSourcesByImage.size() != sourceCount || budgetCache.voxelFixedScaleByKey.size() != fixedCount
            || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, budgetCache)
            || (mode == 3 ? !limited.timeStopped : !limited.budgetStopped)
            || (mode != 4 && limited.newEntries != 0) || (mode == 4 && limited.newEntries != 1)) return fail("hard budget fallback boundary " + std::to_string(mode));
        auto cold = seed;
        auto* oldImage = loadRuntimeVoxelImageFromCacheUnbatched("GTNK", "__PW_TANK__", 31, context, cold, 0.0, 0, 0, 0, nullptr);
        auto* fallback = loadRuntimeVoxelImageFromCacheUnbatched("GTNK", "__PW_TANK__", 31, context, budgetCache, 0.0, 0, 0, 0, nullptr);
        if (!oldImage || !fallback || !runtimeVoxelLoadingPrewarmContractEntryEqual(*oldImage, *fallback)) return fail("budget changed ordinary cold fallback image");
    }
    // A deterministic request budget proves actual ordering without relying on
    // host speed. Frame0/facing0 has 3 seeded bodies and4 actual tank parts.
    // The next5 requests must warm tank facing1+four parts only in C24.
    {
        auto bounded = limits; bounded.maximumAttempts = 12;
        auto ordered = seed;
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(world, context, ordered, bounded, result);
        if (!result.budgetStopped || result.attempts != 12 || result.v1Reference != (version != 0)
            || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, ordered))
            return fail("deterministic order budget did not run both actual routes");
        auto probe = seed;
        auto* body = loadRuntimeVoxelImageFromCacheUnbatched("GTNK", "__PW_TANK__", 1, context, probe, 0.0, 0, 0, 0, nullptr);
        if (!body || !body->hasSprite) return fail("ordered actual tank body witness absent");
        const double bodyFit = body->rasterFitScale;
        for (const auto& part : std::array<std::pair<const char*, double>, 4>{{
                {"|turret", runtimeVxlTurretRelativeScale()}, {"|turret-shadow", runtimeVxlTurretRelativeScale()},
                {"|barrel", runtimeVxlBarrelRelativeScale()}, {"|barrel-shadow", runtimeVxlBarrelRelativeScale()}}}) {
            const bool barrel = std::strstr(part.first, "barrel") != nullptr;
            const auto images = barrel ? runtimeUnitBarrelImageCandidates(world.getObjects()[0], context)
                : runtimeUnitTurretImageCandidates(world.getObjects()[0], context);
            std::string image; RuntimeVoxelLoadingPrewarmStats selection;
            if (!runtimeVoxelLoadingPrewarmChooseSource(images, context, probe, limits, selection, image))
                return fail("ordered actual part source absent");
            const double fit = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(bodyFit, part.second) : 0.0;
            auto* expected = loadRuntimeVoxelImageFromCacheUnbatched(image, std::string("__PW_TANK__") + part.first,
                1, context, probe, fit, 0, 0, 0, nullptr);
            if (!expected || !expected->hasSprite) return fail("ordered actual part decoder witness absent");
            const auto found = ordered.voxelObjectsByType.find(expected->image.rasterCacheKey);
            if (version == 0) {
                if (found == ordered.voxelObjectsByType.end()
                    || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expected, found->second))
                    return fail("frame0 body did not immediately warm same-facing actual part/shadow");
            } else if (found != ordered.voxelObjectsByType.end()) {
                return fail("V1 did not preserve original bodies-before-parts request order");
            }
        }
    }
    std::cout << "Voxel loading prewarm route: v1Reference=" << stats.v1Reference
        << " new=" << stats.newEntries << " attempts=" << stats.attempts
        << " elapsedMs=" << stats.elapsedMilliseconds << " timeStopped=" << stats.timeStopped
        << " full original producer/seed/source/fixed/HVA/budget comparison passed\n";
    } // Both C24 and unmodified C23 request ordering.
    {
        // No sleep or platform message loop: backdate only this private test
        // state's UI interval, then exercise genuine cached/cold producers.
        auto progressLimits = limits;
        RuntimeVoxelLoadingPrewarmContractProgressProbe probe{&world, &context, &progressLimits};
        progressLimits.loadingProgress = runtimeVoxelLoadingPrewarmContractProgress;
        progressLimits.loadingProgressContext = &probe;
        auto progressCache = seed;
        RuntimeVoxelLoadingPrewarmStats progressStats;
        RuntimeVoxelLoadingPrewarmState active{&context, &progressCache, &progressLimits, &progressStats,
            std::chrono::steady_clock::now() - std::chrono::milliseconds(200)};
        {
            RuntimeVoxelLoadingPrewarmStateScope scope(active);
            // A genuine admission makes alias clearing observable in the hard
            // entry/byte budget, rather than comparing an all-zero object.
            progressLimits.loadingProgress = nullptr;
            auto* admitted = runtimeVoxelLoadingPrewarmRequest(active, "GTNK", "__PW_TANK__", 1, 0.0, 0, 0);
            if (!admitted || progressStats.attempts != 1 || progressStats.newEntries != 1
                || progressStats.decodedEntries != 1 || progressStats.addedPayloadBytes == 0)
                return fail("same-stats progress alias fixture did not have a real active admission");
            progressLimits.loadingProgress = runtimeVoxelLoadingPrewarmContractProgress;
            active.lastProgress = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
            auto* first = runtimeVoxelLoadingPrewarmRequest(active, "GTNK", "__PW_TANK__", 0, 0.0, 0, 0);
            if (!first || probe.calls != 1 || probe.invalid || active.progressCallbackActive
                || runtimeVoxelLoadingPrewarmState != &active) return fail("loading UI pump/nested admission witness");
            // An independent future interval tag proves the throttled path
            // without a flaky assumption about host scheduling between calls.
            active.lastProgress = std::chrono::steady_clock::now() + std::chrono::hours(1);
            auto* next = runtimeVoxelLoadingPrewarmRequest(active, "GTNK", "__PW_TANK__", 2, 0.0, 0, 0);
            auto original = seed;
            auto* expected = loadRuntimeVoxelImageFromCacheUnbatched("GTNK", "__PW_TANK__", 2, context,
                original, 0.0, 0, 0, 0, nullptr);
            if (!next || !expected || probe.calls != 1 || probe.invalid || progressStats.attempts != 3
                || !runtimeVoxelLoadingPrewarmContractEntryEqual(*next, *expected)
                || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, progressCache)
                || world.deterministicSimulationStateHash() != hash) return fail("loading UI cadence changed original image/cache/authority");
        }
        if (runtimeVoxelLoadingPrewarmState != nullptr) return fail("UI pump leaked admission TLS");
        // Simulate a UI call that consumes the remaining soft time; no next
        // decoder request may start. An exception must also restore the guard.
        for (int mode = 0; mode < 2; ++mode) {
            auto unchanged = seed;
            RuntimeVoxelLoadingPrewarmStats result;
            probe.calls = 0; probe.invalid = false;
            probe.exhaustDeadline = mode == 0; probe.throwOnCall = mode == 1;
            RuntimeVoxelLoadingPrewarmState activeUi{&context, &unchanged, &progressLimits, &result,
                std::chrono::steady_clock::now() - std::chrono::milliseconds(200)};
            bool threw = false;
            {
                RuntimeVoxelLoadingPrewarmStateScope scope(activeUi);
                try {
                    if (runtimeVoxelLoadingPrewarmRequest(activeUi, "GTNK", "__PW_TANK__", 1, 0.0, 0, 0) != nullptr)
                        return fail("expired loading UI callback still decoded a pose");
                } catch (const RuntimeVoxelLoadingPrewarmContractProgressException&) { threw = true; }
                if (probe.calls != 1 || probe.invalid || activeUi.progressCallbackActive
                    || runtimeVoxelLoadingPrewarmState != &activeUi || result.attempts || result.newEntries
                    || (mode == 0 ? !result.timeStopped || threw : !threw)
                    || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, unchanged))
                    return fail("UI callback deadline/exception/reentry boundary");
            }
            if (runtimeVoxelLoadingPrewarmState != nullptr) return fail("UI callback exception leaked TLS");
        }
        probe.exhaustDeadline = probe.throwOnCall = false;
        for (int mode = 0; mode < 3; ++mode) {
            auto unchanged = seed;
            auto bounded = progressLimits;
            if (mode == 0) bounded.maximumAttempts = 12;
            if (mode == 2) bounded.maximumWallMilliseconds = 0;
            probe.calls = 0; probe.invalid = false;
            RuntimeVoxelLoadingPrewarmReferenceScope skip(mode == 1);
            RuntimeVoxelLoadingPrewarmV1ReferenceScope legacy(mode != 2);
            RuntimeVoxelLoadingPrewarmStats result;
            prewarmRuntimeCurrentWorldNormalVoxels(world, context, unchanged, bounded, result);
            if (probe.calls || probe.invalid || !runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, unchanged)
                || world.deterministicSimulationStateHash() != hash
                || (mode == 0 && (!result.v1Reference || result.attempts != 12))
                || (mode == 1 && (!result.referenceSkipped || result.v1Reference || result.attempts))
                || (mode == 2 && (!result.timeStopped || result.attempts)))
                return fail("legacy/skip/zero-time loading route dispatched UI callback");
        }
    }
    {
        auto bypass = seed;
        RuntimeVoxelLoadingPrewarmReferenceScope reference(true);
        RuntimeVoxelLoadingPrewarmV1ReferenceScope v1(true);
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(world, context, bypass, limits, result);
        if (!result.referenceSkipped || result.v1Reference || result.attempts || result.newEntries
            || bypass.voxelObjectsByType.size() != seed.voxelObjectsByType.size()) return fail("independent reference did not preserve old path");
    }
    {
        RuntimeRendererEnvironmentScope disabled(runtimeVoxelLoadingPrewarmContractDisabledCacheEnvironment);
        auto bypass = seed; RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(world, context, bypass, limits, result);
        if (!result.cacheDisabledSkipped || result.attempts || result.newEntries
            || bypass.voxelObjectsByType.size() != seed.voxelObjectsByType.size()) return fail("disabled cache was modified by prewarm");
    }
    {
        auto noFixed = seed;
        // A private test cache deliberately lacks the original fit profile.
        // Production never erases fit/raster nodes to make room for warmup.
        noFixed.voxelFixedScaleByKey.clear();
        GameWorld noFixedWorld; noFixedWorld.accessObjects().push_back(world.getObjects()[2]);
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(noFixedWorld, context, noFixed, limits, result);
        if (runtimeVxlFixedScaleRequested() && (!result.fixedScaleNotReady || result.newEntries
            || !noFixed.voxelFixedScaleByKey.empty()
            || noFixed.voxelObjectsByType.size() != seed.voxelObjectsByType.size())) return fail("missing exact fixed-scale profile grew an auxiliary cache");
    }
    {
        auto foreign = context; foreign.assetRootPath += "/other";
        auto unchanged = seed;
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(world, foreign, unchanged, limits, result);
        if (result.newEntries || result.attempts || !result.unavailableSources
            || unchanged.voxelSourcesByImage.size() != sourceCount) return fail("foreign source path triggered IO/borrow");
    }
    {
        RuntimeVoxelLoadingPrewarmState active{&context, &seed, &limits, &stats, std::chrono::steady_clock::now()};
        RuntimeVoxelLoadingPrewarmStateScope outer(active);
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(world, context, seed, limits, result);
        auto foreignContext = context; RuntimeMapRenderCache foreignCache;
        if (!result.nestedSkipped || result.attempts
            || runtimeVoxelLoadingPrewarmFor(context, foreignCache, 0, nullptr)
            || runtimeVoxelLoadingPrewarmFor(foreignContext, seed, 0, nullptr)
            || runtimeVoxelLoadingPrewarmFor(context, seed, 1, nullptr)) return fail("nested/foreign scoped admission mismatch");
    }
    {
        auto highContext = context; highContext.assetRootPath = "__prewarm_high_hva_contract__/root";
        RuntimeMapRenderCache high;
        if (!runtimeVoxelLoadingPrewarmContractSeedSource("__PW_HIGH_IMAGE__", highContext, high, 17, error)) return fail("genuine 17 HVA parser fixture: " + error);
        WorldObjectInstance object = world.getObjects().front(); object.type = "__PW_HIGH__";
        GameWorld highWorld; highWorld.accessObjects().push_back(object);
        auto* first = loadRuntimeVoxelImageFromCacheUnbatched("__PW_HIGH_IMAGE__", object.type, 0, highContext, high, 0.0, 0, 0, 0, nullptr);
        if (!first || !first->hasSprite || first->metadata.hvaFrameCount != 17) return fail("genuine 17 HVA ordinary seed decode");
        auto oldHigh = high;
        const auto count = high.voxelObjectsByType.size();
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(highWorld, highContext, high, limits, result);
        if (!result.highHvaSources || result.attempts || result.newEntries || high.voxelObjectsByType.size() != count)
            return fail("high HVA was not bounded without altering original source");
        auto* expected = loadRuntimeVoxelImageFromCacheUnbatched("__PW_HIGH_IMAGE__", object.type, 7, highContext, oldHigh, 0.0, 0, 16, 0, nullptr);
        auto* actual = loadRuntimeVoxelImageFromCacheUnbatched("__PW_HIGH_IMAGE__", object.type, 7, highContext, high, 0.0, 0, 16, 0, nullptr);
        if (!expected || !actual || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expected, *actual)) return fail("high HVA original lazy frame changed");
    }
    {
        RuntimeVoxelFaceContractEnvironmentScope automatic("westwood", "0");
        RuntimeRendererEnvironmentScope autoEnvironment(runtimeVoxelLoadingPrewarmContractAutoEnvironment);
        auto autoContext = context; autoContext.assetRootPath = "__prewarm_auto_fit_contract__/root";
        RuntimeMapRenderCache autoSeed;
        // Keep original already-resolved VPL state; only the source bytes below
        // are synthetic, and both metadata and all HVA matrices use real parsers.
        autoSeed.voxelPaletteLookupLoadAttempted = seed.voxelPaletteLookupLoadAttempted;
        autoSeed.voxelPaletteLookupLoaded = seed.voxelPaletteLookupLoaded;
        autoSeed.voxelPaletteLookup = seed.voxelPaletteLookup;
        autoSeed.voxelPaletteFirstRemap = seed.voxelPaletteFirstRemap;
        autoSeed.voxelPaletteLastRemap = seed.voxelPaletteLastRemap;
        autoSeed.voxelPaletteSectionCount = seed.voxelPaletteSectionCount;
        autoSeed.voxelPaletteNeutralSection = seed.voxelPaletteNeutralSection;
        auto wide = runtimeVoxelCornerContractWideSparseSource();
        std::fill(wide.begin() + 802, wide.begin() + 818, 0);
        std::memcpy(wide.data() + 802, "CUBE", 4);
        if (!runtimeVoxelLoadingPrewarmContractSeedBytes("__PW_AUTO_IMAGE__", autoContext, autoSeed, std::move(wide), 1, error)
            || !runtimeVoxelLoadingPrewarmContractSeedSource("__PW_AUTO_IMAGE__TUR", autoContext, autoSeed, 1, error)
            || !runtimeVoxelLoadingPrewarmContractSeedSource("__PW_AUTO_IMAGE__BARL", autoContext, autoSeed, 1, error))
            return fail("auto-fit real source parsing: " + error);
        WorldObjectInstance object = world.getObjects().front(); object.type = "__PW_AUTO__";
        GameWorld autoWorld; autoWorld.accessObjects().push_back(object);
        const auto authority = autoWorld.deterministicSimulationStateHash();
        auto* initial = loadRuntimeVoxelImageFromCacheUnbatched("__PW_AUTO_IMAGE__", object.type, 0,
            autoContext, autoSeed, 0.0, 0, 0, 0, nullptr);
        if (!initial || !initial->hasSprite || runtimeVxlFixedScaleRequested()) return fail("auto-fit original producer not selected");
        auto autoOriginal = autoSeed, autoWarm = autoSeed;
        std::vector<double> exactFits;
        for (int facing = 0; facing < 32; ++facing) {
            auto* body = loadRuntimeVoxelImageFromCacheUnbatched("__PW_AUTO_IMAGE__", object.type, facing,
                autoContext, autoOriginal, 0.0, 0, 0, 0, nullptr);
            if (!body) return fail("auto-fit original body decode");
            const double fit = body->rasterFitScale;
            if (std::none_of(exactFits.begin(), exactFits.end(), [&](double other) {
                    return std::memcmp(&fit, &other, sizeof(fit)) == 0;
                })) exactFits.push_back(fit);
        }
        if (exactFits.size() < 2 || !runtimeVxlSharedPartScaleEnabled())
            return fail("auto-fit independent fixture did not produce distinct forced part scales");
        RuntimeVoxelLoadingPrewarmStats result;
        prewarmRuntimeCurrentWorldNormalVoxels(autoWorld, autoContext, autoWarm, limits, result);
        if (!result.unsupportedProfileSkipped || result.attempts || result.newEntries || result.addedPayloadBytes
            || autoWarm.voxelObjectsByType.size() != autoSeed.voxelObjectsByType.size()
            || autoWorld.deterministicSimulationStateHash() != authority) return fail("auto-fit profile did not retain original first-writer cache semantics");
        for (int facing = 0; facing < 32; ++facing) {
            auto* oldBody = loadRuntimeVoxelImageFromCacheUnbatched("__PW_AUTO_IMAGE__", object.type, facing,
                autoContext, autoOriginal, 0.0, 0, 0, 0, nullptr);
            auto* warmBody = loadRuntimeVoxelImageFromCacheUnbatched("__PW_AUTO_IMAGE__", object.type, facing,
                autoContext, autoWarm, 0.0, 0, 0, 0, nullptr);
            if (!oldBody || !warmBody
                || !runtimeVoxelLoadingPrewarmContractEntryEqual(*oldBody, *warmBody)) return fail("auto-fit full body pixel/aux mismatch");
        }
        for (std::size_t fitIndex = 0; fitIndex < 2; ++fitIndex) for (int facing = 0; facing < 32; ++facing)
            for (const auto& part : std::array<std::pair<const char*, double>, 4>{{
                    {"|turret", runtimeVxlTurretRelativeScale()}, {"|barrel", runtimeVxlBarrelRelativeScale()},
                    {"|turret-shadow", runtimeVxlTurretRelativeScale()}, {"|barrel-shadow", runtimeVxlBarrelRelativeScale()}}}) {
                const bool barrel = std::strstr(part.first, "barrel") != nullptr;
                const std::string image = barrel ? "__PW_AUTO_IMAGE__BARL" : "__PW_AUTO_IMAGE__TUR";
                const double fit = runtimeVoxelPartFitScale(exactFits[fitIndex], part.second);
                auto* oldPart = loadRuntimeVoxelImageFromCacheUnbatched(image, object.type + part.first,
                    facing, autoContext, autoOriginal, fit, 0, 0, 0, nullptr);
                auto* warmPart = loadRuntimeVoxelImageFromCacheUnbatched(image, object.type + part.first,
                    facing, autoContext, autoWarm, fit, 0, 0, 0, nullptr);
                if (!oldPart || !warmPart
                    || !runtimeVoxelLoadingPrewarmContractEntryEqual(*oldPart, *warmPart)) return fail("auto-fit original Cartesian part scale/angle/aux fallback mismatch");
                ++cases;
            }
    }
    {
        auto badContext = context; badContext.assetRootPath = "__prewarm_bad_column_contract__/root";
        RuntimeMapRenderCache bad;
        auto bytes = runtimeVoxelFaceContractCube(false);
        // Metadata is genuinely valid, but the actual column points outside
        // its body. The original complete decoder must reject this source.
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes[830U + shift / 8U] = static_cast<unsigned char>(0x7fffffffU >> shift);
        if (!runtimeVoxelLoadingPrewarmContractSeedBytes("__PW_BAD_IMAGE__", badContext, bad, std::move(bytes), 1, error))
            return fail("genuine failed decoder fixture parser: " + error);
        if (loadRuntimeVoxelImageFromCacheUnbatched("__PW_BAD_IMAGE__", "__PW_BAD__", 0, badContext,
                bad, 0.5, 0, 0, 0, nullptr) != nullptr || bad.voxelObjectsByType.size() != 1)
            return fail("original decoder did not create genuine failed raster");
        const auto failedKey = bad.voxelObjectsByType.begin()->first;
        const auto failedEntry = bad.voxelObjectsByType.begin()->second;
        if (!failedEntry.loadAttempted || failedEntry.loaded || failedEntry.errorMessage.empty()) return fail("failed raster/error witness absent");
        auto oldBad = bad;
        RuntimeVoxelLoadingPrewarmStats failed;
        RuntimeVoxelLoadingPrewarmState active{&badContext, &bad, &limits, &failed, std::chrono::steady_clock::now()};
        {
            RuntimeVoxelLoadingPrewarmStateScope scope(active);
            if (runtimeVoxelLoadingPrewarmRequest(active, "__PW_BAD_IMAGE__", "__PW_BAD__", 1, 0.5, 0, 0) != nullptr
                || failed.decodedEntries != 1 || failed.failedEntries != 1 || failed.newEntries
                || bad.voxelObjectsByType.size() != 1
                || !runtimeVoxelLoadingPrewarmContractEntryEqual(failedEntry, bad.voxelObjectsByType.at(failedKey)))
                return fail("temporary failed decode published a negative node or changed old failure");
        }
        if (loadRuntimeVoxelImageFromCacheUnbatched("__PW_BAD_IMAGE__", "__PW_BAD__", 1, badContext, oldBad, 0.5, 0, 0, 0, nullptr)
            || loadRuntimeVoxelImageFromCacheUnbatched("__PW_BAD_IMAGE__", "__PW_BAD__", 1, badContext, bad, 0.5, 0, 0, 0, nullptr)
            || oldBad.voxelObjectsByType.size() != bad.voxelObjectsByType.size()) return fail("failed raster original lazy fallback changed");
        for (const auto& pair : oldBad.voxelObjectsByType) {
            const auto current = bad.voxelObjectsByType.find(pair.first);
            if (current == bad.voxelObjectsByType.end() || !runtimeVoxelLoadingPrewarmContractEntryEqual(pair.second, current->second))
                return fail("failed raster full original key/error/aux mismatch");
        }
    }
    if (world.deterministicSimulationStateHash() != hash || runtimeVoxelLoadingPrewarmState != nullptr)
        return fail("world authority or scope lifetime changed");
    std::cout << "Voxel loading prewarm: actual stock full image/aux/key probes=" << cases
        << " animated=" << animated << " shadow=" << shadow << " warmHits=" << warmHits
        << " coldFallbacks=" << coldFallbacks << " routes=2 totalNew=" << totalNew << " new=" << stats.newEntries
        << " addedPayload=" << stats.addedPayloadBytes << " budgets/nested/foreign/highHVA/loadingUI passed\n";
    return true;
}
