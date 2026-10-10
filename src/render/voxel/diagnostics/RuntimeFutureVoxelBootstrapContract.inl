// Include after RuntimeVoxelLoadingPrewarmContract.inl: its complete native
// producer entry/image/source equality functions remain the independent oracle.
namespace {
bool runtimeFutureVoxelBootstrapContractSeedPreserved(const RuntimeMapRenderCache& seed,
    const RuntimeMapRenderCache& current) {
    auto filtered = current;
    for (auto it = filtered.voxelSourcesByImage.begin(); it != filtered.voxelSourcesByImage.end(); )
        if (seed.voxelSourcesByImage.find(it->first) == seed.voxelSourcesByImage.end()) it = filtered.voxelSourcesByImage.erase(it);
        else ++it;
    for (auto it = filtered.voxelFixedScaleByKey.begin(); it != filtered.voxelFixedScaleByKey.end(); )
        if (seed.voxelFixedScaleByKey.find(it->first) == seed.voxelFixedScaleByKey.end()) it = filtered.voxelFixedScaleByKey.erase(it);
        else ++it;
    return runtimeVoxelLoadingPrewarmContractSeedPreserved(seed, filtered)
        && seed.looseResourceIndexBuilt == current.looseResourceIndexBuilt
        && seed.looseResourceIndexRootPath == current.looseResourceIndexRootPath
        && seed.looseResourcePathsByName == current.looseResourcePathsByName
        && seed.mixArchiveRootPath == current.mixArchiveRootPath
        && seed.mixArchivePathsScanned == current.mixArchivePathsScanned
        && seed.mixArchivePaths == current.mixArchivePaths
        && seed.mixArchivesByPath.size() == current.mixArchivesByPath.size();
}

bool runtimeFutureVoxelBootstrapContractStatsEqual(const RuntimeFutureVoxelBootstrapStats& a,
    const RuntimeFutureVoxelBootstrapStats& b) {
    return a.referenceSkipped == b.referenceSkipped && a.nestedSkipped == b.nestedSkipped
        && a.cacheDisabledSkipped == b.cacheDisabledSkipped && a.unsupportedProfileSkipped == b.unsupportedProfileSkipped
        && a.paletteNotReadySkipped == b.paletteNotReadySkipped && a.indexNotReadySkipped == b.indexNotReadySkipped
        && a.budgetStopped == b.budgetStopped && a.timeStopped == b.timeStopped
        && a.allocationStopped == b.allocationStopped && a.sourceChangedStopped == b.sourceChangedStopped
        && a.families == b.families && a.duplicateFamilies == b.duplicateFamilies
        && a.unsupportedTypes == b.unsupportedTypes && a.failedFamilies == b.failedFamilies
        && a.unknownEarlierArchives == b.unknownEarlierArchives && a.refusedSourceReads == b.refusedSourceReads
        && a.sourceReads == b.sourceReads && a.sourceReadBytes == b.sourceReadBytes
        && a.maximumTransientReadBytes == b.maximumTransientReadBytes
        && a.existingSources == b.existingSources && a.existingNegativeSources == b.existingNegativeSources
        && a.attempts == b.attempts && a.decodedEntries == b.decodedEntries && a.failedEntries == b.failedEntries
        && a.existingEntries == b.existingEntries && a.newEntries == b.newEntries
        && a.newSources == b.newSources && a.newFixedScales == b.newFixedScales
        && a.addedPayloadBytes == b.addedPayloadBytes && a.progressCallbacks == b.progressCallbacks
        && runtimeVoxelFaceContractDoubleEqual(a.elapsedMilliseconds, b.elapsedMilliseconds);
}

void runtimeFutureVoxelBootstrapContractSnapshot(RuntimeFutureVoxelBootstrapState& state) {
    state.mixPaths = state.live.mixArchivePaths;
    state.vplPalette = state.live.voxelPalette; state.vplLookup = state.live.voxelPaletteLookup;
}

struct RuntimeFutureVoxelBootstrapContractProgress {
    const std::vector<std::string>* types = nullptr;
    const RuntimeMapRenderContext* context = nullptr;
    RuntimeMapRenderCache* cache = nullptr;
    RuntimeFutureVoxelBootstrapStats* stats = nullptr;
    std::size_t calls = 0;
    bool aliasPreserved = true;
    bool foreignSkipped = true;
};

void runtimeFutureVoxelBootstrapContractProgressCallback(void* opaque) {
    auto& data = *static_cast<RuntimeFutureVoxelBootstrapContractProgress*>(opaque);
    ++data.calls;
    const auto before = *data.stats;
    prewarmRuntimeFutureNormalVoxelTypes(*data.types, *data.context, *data.cache, {}, *data.stats);
    data.aliasPreserved = data.aliasPreserved && runtimeFutureVoxelBootstrapContractStatsEqual(before, *data.stats);
    RuntimeFutureVoxelBootstrapStats foreign;
    prewarmRuntimeFutureNormalVoxelTypes(*data.types, *data.context, *data.cache, {}, foreign);
    data.foreignSkipped = data.foreignSkipped && foreign.nestedSkipped && foreign.attempts == 0
        && foreign.newEntries == 0 && foreign.newSources == 0 && foreign.elapsedMilliseconds == 0.0;
}
} // namespace

bool runtimeFutureVoxelBootstrapRenderContractStatsEqual(const RuntimeMapRenderStats& original,
    const RuntimeMapRenderStats& prepared, std::string& mismatchedField) {
    auto a = original, b = prepared;
    a.voxelCacheHits = b.voxelCacheHits = 0;
    a.voxelCacheMisses = b.voxelCacheMisses = 0;
    mismatchedField.clear();
    return runtimeGroundShadowContractStatsEqual(a, b, mismatchedField);
}

bool runRuntimeFutureVoxelBootstrapAssetContractCheck(const RuntimeMapRenderContext& assets, std::string& error) {
    error.clear();
    const auto fail = [&](const std::string& detail) { error = "future voxel bootstrap: " + detail; return false; };
    RuntimeVoxelFaceContractEnvironmentScope fixedProfile("fixedscale", "0");
    RuntimeVoxelLoadingPrewarmReferenceScope totalEnabled(false);
    RuntimeFutureVoxelBootstrapReferenceScope enabled(false);
    auto context = assets;
    context.stableRenderConfiguration = true; context.animationTick = 0;
    context.specialMechanismSnapshots = nullptr;
    if (context.rules == nullptr || context.art == nullptr || context.rules->findUnitType("UnitType", "SREF") == nullptr)
        return fail("real stock SREF rules/art are required");
    WorldObjectInstance sref;
    sref.instanceId = -1; sref.kind = WorldObjectKind::Unit; sref.type = "SREF"; sref.alive = true; sref.health = 100;
    const auto* srefArt = findArtTypeForObject(sref, context);
    if (srefArt == nullptr || !srefArt->voxel) return fail("real SREF is not an ordinary VXL asset");
    WorldObjectInstance mtnk = sref; mtnk.type = "MTNK";
    RuntimeMapRenderCache seed;
    const auto* initial = loadRuntimeVoxelFromCache(mtnk, context, seed);
    if (initial == nullptr || !initial->hasSprite) return fail("actual MTNK opening producer seed failed");
    // A separate original cold discovery finishes precisely the stock indexes
    // needed by SREF. Only its real index values are copied to the input seed;
    // neither SREF source bytes, fits nor raster entries are transferred.
    RuntimeMapRenderCache discovery = seed;
    const auto* witness = loadRuntimeVoxelFromCache(sref, context, discovery);
    if (witness == nullptr || !witness->hasSprite) return fail("actual original cold SREF failed");
    const std::string bodyImage = witness->metadata.imageId;
    const double firstFit = witness->rasterFitScale;
    struct Part { std::string image, suffix; double relative = 1.0; };
    std::vector<Part> parts;
    if (runtimeObjectHasTurretForRender(sref, context)) {
        for (const auto& suffix : {std::string("|turret"), std::string("|barrel"), std::string("|turret-shadow"), std::string("|barrel-shadow")}) {
            const bool barrel = suffix.find("barrel") != std::string::npos;
            if (barrel && !runtimeDrawSeparateUnitVoxelBarrels()) continue;
            if (suffix.find("shadow") != std::string::npos && !srefArt->useTurretShadow) continue;
            const double relative = barrel ? runtimeVxlBarrelRelativeScale() : runtimeVxlTurretRelativeScale();
            const double fit = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(firstFit, relative) : 0.0;
            const auto candidates = barrel ? runtimeUnitBarrelImageCandidates(sref, context) : runtimeUnitTurretImageCandidates(sref, context);
            for (const auto& image : candidates) {
                const auto* part = loadRuntimeVoxelImageFromCacheUnbatched(image, sref.type + suffix, 0,
                    context, discovery, fit, 0, 0, 0, nullptr);
                if (part != nullptr && part->hasSprite) { parts.push_back({image, suffix, relative}); break; }
            }
        }
    }
    seed.looseResourceIndexRootPath = discovery.looseResourceIndexRootPath;
    seed.looseResourceIndexBuilt = discovery.looseResourceIndexBuilt;
    seed.looseResourcePathsByName = discovery.looseResourcePathsByName;
    seed.mixArchiveRootPath = discovery.mixArchiveRootPath;
    seed.mixArchivePathsScanned = discovery.mixArchivePathsScanned;
    seed.mixArchivePaths = discovery.mixArchivePaths; seed.mixArchivesByPath = discovery.mixArchivesByPath;
    if (seed.voxelSourcesByImage.find(runtimeFutureVoxelBootstrapSourceKey(bodyImage, context)) != seed.voxelSourcesByImage.end())
        return fail("fixture already contained future SREF source before bootstrap");
    const std::vector<std::string> types{"SREF", "SREF"};
    RuntimeFutureVoxelBootstrapLimits limits;
    RuntimeFutureVoxelBootstrapStats stats;
    RuntimeMapRenderCache warmed = seed, original = seed;
    prewarmRuntimeFutureNormalVoxelTypes(types, context, warmed, limits, stats);
    if (stats.families != 1 || stats.duplicateFamilies != 1 || stats.newSources == 0 || stats.newFixedScales == 0
        || stats.newEntries < 32 || stats.sourceReads < 2 || stats.referenceSkipped || stats.indexNotReadySkipped
        || stats.sourceChangedStopped || stats.allocationStopped || stats.unknownEarlierArchives != 0
        || stats.newEntries > limits.maximumNewEntries || stats.attempts > limits.maximumAttempts
        || stats.addedPayloadBytes == 0 || stats.addedPayloadBytes > limits.maximumAddedPayloadBytes
        || stats.maximumTransientReadBytes > limits.maximumTransientReadBytes
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(seed, warmed))
        return fail("actual future SREF positive source/raster/admission coverage was not exercised");
    std::size_t cases = 0, bodyHits = 0, partHits = 0, shadows = 0;
    for (int facing = 0; facing < 32; ++facing) {
        auto* expected = loadRuntimeVoxelImageFromCacheUnbatched(bodyImage, "SREF", facing, context, original,
            0.0, runtimeObjectVoxelShadowSection(sref, context), 0, 0, nullptr);
        auto* actual = loadRuntimeVoxelImageFromCacheUnbatched(bodyImage, "SREF", facing, context, warmed,
            0.0, runtimeObjectVoxelShadowSection(sref, context), 0, 0, nullptr);
        if (expected == nullptr || actual == nullptr || !actual->image.rasterCacheHit
            || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expected, *actual))
            return fail("actual SREF complete body pixels/aux/material/depth/shadow/key/error mismatch f=" + std::to_string(facing));
        ++cases; ++bodyHits;
        if (std::any_of(actual->shadowPixels.begin(), actual->shadowPixels.end(), [](unsigned char value) { return value != 0; })) ++shadows;
        const double fit = actual->rasterFitScale;
        for (const auto& part : parts) {
            const double scale = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(fit, part.relative) : 0.0;
            expected = loadRuntimeVoxelImageFromCacheUnbatched(part.image, "SREF" + part.suffix, facing, context, original, scale, 0, 0, 0, nullptr);
            actual = loadRuntimeVoxelImageFromCacheUnbatched(part.image, "SREF" + part.suffix, facing, context, warmed, scale, 0, 0, 0, nullptr);
            if (expected == nullptr || actual == nullptr || !actual->image.rasterCacheHit
                || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expected, *actual))
                return fail("actual SREF complete part/owner/fit/shadow mismatch " + part.suffix + " f=" + std::to_string(facing));
            ++cases; ++partHits;
        }
    }
    if (bodyHits != 32 || shadows == 0 || partHits != parts.size() * 32U)
        return fail("actual full facing/body shadow/available part domain was incomplete");
    auto refused = seed;
    RuntimeFutureVoxelBootstrapLimits tiny = limits; tiny.maximumAddedPayloadBytes = 1;
    prewarmRuntimeFutureNormalVoxelTypes(types, context, refused, tiny, stats);
    if (!stats.budgetStopped || stats.newEntries != 0 || stats.newSources != 0 || stats.newFixedScales != 0
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(seed, refused)) return fail("tiny retained budget published data or damaged first-writers");
    auto* fallback = loadRuntimeVoxelImageFromCacheUnbatched(bodyImage, "SREF", 0, context, refused,
        0.0, runtimeObjectVoxelShadowSection(sref, context), 0, 0, nullptr);
    auto cold = seed;
    auto* expectedFallback = loadRuntimeVoxelImageFromCacheUnbatched(bodyImage, "SREF", 0, context, cold,
        0.0, runtimeObjectVoxelShadowSection(sref, context), 0, 0, nullptr);
    if (fallback == nullptr || expectedFallback == nullptr || !runtimeVoxelLoadingPrewarmContractEntryEqual(*expectedFallback, *fallback))
        return fail("tiny budget did not retain complete original cold success");
    auto readRefused = seed;
    tiny = limits; tiny.maximumSourceReadBytes = 1;
    prewarmRuntimeFutureNormalVoxelTypes(types, context, readRefused, tiny, stats);
    if (stats.refusedSourceReads == 0 || stats.sourceReads != 0 || stats.newSources != 0 || stats.newEntries != 0
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(seed, readRefused)) return fail("same-open source size was not refused before retention");
    auto reference = seed;
    { RuntimeFutureVoxelBootstrapReferenceScope off(true);
      prewarmRuntimeFutureNormalVoxelTypes(types, context, reference, limits, stats); }
    if (!stats.referenceSkipped || stats.attempts != 0 || stats.elapsedMilliseconds != 0.0
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(seed, reference)) return fail("independent reference did work");
    { RuntimeVoxelLoadingPrewarmReferenceScope off(true);
      prewarmRuntimeFutureNormalVoxelTypes(types, context, reference, limits, stats); }
    if (!stats.referenceSkipped || stats.sourceReads != 0 || stats.elapsedMilliseconds != 0.0)
        return fail("total prewarm reference did not skip before clock/IO");
    auto unknown = seed;
    for (const auto& image : resolveObjectVoxelImageIdCandidates(sref, context)) {
        unknown.looseResourcePathsByName.erase(toLowerAscii(fileNameForVxlImageId(image)));
        unknown.looseResourcePathsByName.erase(toLowerAscii(fileNameForHvaImageId(image)));
    }
    const auto unknownPath = (std::filesystem::path(context.assetRootPath) / "__FUTURE_UNKNOWN_EARLIER__.mix").string();
    unknown.mixArchivePaths = {unknownPath}; unknown.mixArchivesByPath.emplace(unknownPath, RuntimeMixArchiveCacheEntry{});
    const auto unknownSeed = unknown;
    prewarmRuntimeFutureNormalVoxelTypes(types, context, unknown, limits, stats);
    if (stats.unknownEarlierArchives != 1 || stats.sourceReads != 0 || stats.newEntries != 0 || stats.newSources != 0
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(unknownSeed, unknown)) return fail("unknown earlier archive guessed a lower-priority winner");
    // Native negative first-writer comes from the unchanged original source
    // loader, not a fabricated success/failure entry. The fixture changes only
    // candidate order; its fallback remains the actual stock SREF decoder.
    IniFile rulesIni, artIni; RulesDatabase fallbackRules; ArtDatabase fallbackArt;
    if (!rulesIni.loadFromString("[VehicleTypes]\n0=SREF\n[SREF]\nImage=__FUTURE_MISSING__\nTurret=no\n")
        || !fallbackRules.loadFromIni(rulesIni)
        || !artIni.loadFromString("[__FUTURE_MISSING__]\nVoxel=yes\n[SREF]\nVoxel=yes\n")
        || !fallbackArt.loadFromIni(artIni)) return fail("negative candidate-order fixture failed");
    auto negativeContext = context; negativeContext.rules = &fallbackRules; negativeContext.art = &fallbackArt;
    auto negative = seed;
    const auto& failure = loadRuntimeVoxelSourceFromCache("__FUTURE_MISSING__", negativeContext, negative, false);
    if (!failure.loadAttempted || failure.loaded || failure.errorMessage.empty()) return fail("actual negative first-writer was not produced");
    const auto negativeSeed = negative;
    prewarmRuntimeFutureNormalVoxelTypes({"SREF"}, negativeContext, negative, limits, stats);
    if (stats.existingNegativeSources == 0 || stats.newEntries < 32
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(negativeSeed, negative)) return fail("negative first-writer was replaced or fallback was skipped");
    // Explicit same-address guards use a local state without source IO. These
    // counterexamples demonstrate that count/pointer identity alone is not
    // the lexical proof. Rules/art must remain immutable in the loading epoch.
    auto mutationCache = seed; auto mutationContext = context;
    RuntimeFutureVoxelBootstrapStats guardStats;
    RuntimeFutureVoxelBootstrapState guard{mutationContext, mutationContext, mutationCache, limits, guardStats, std::chrono::steady_clock::now()};
    runtimeFutureVoxelBootstrapContractSnapshot(guard);
    if (!guard.inputsUnchanged()) return fail("input guard fixture initially mismatched");
    mutationContext.assetRootPath += ".changed";
    if (guard.permit() || !guardStats.sourceChangedStopped || guardStats.newEntries != 0) return fail("in-place root mutation retained staged output");
    mutationContext = context; mutationCache = seed; guardStats = {};
    RuntimeFutureVoxelBootstrapState paletteGuard{mutationContext, mutationContext, mutationCache, limits, guardStats, std::chrono::steady_clock::now()};
    runtimeFutureVoxelBootstrapContractSnapshot(paletteGuard);
    if (!mutationCache.voxelPalette.empty()) mutationCache.voxelPalette.front().r ^= 1U;
    else mutationCache.voxelPaletteLookup.push_back(1U);
    if (paletteGuard.inputsUnchanged()) return fail("same-address palette input mutation escaped guard");
    mutationCache = seed; guardStats = {};
    const std::string lookupName = "__FUTURE_LOOKUP_GUARD__";
    mutationCache.looseResourcePathsByName[lookupName] = "before";
    RuntimeFutureVoxelBootstrapState pathGuard{mutationContext, mutationContext, mutationCache, limits, guardStats, std::chrono::steady_clock::now()};
    runtimeFutureVoxelBootstrapContractSnapshot(pathGuard);
    if (!pathGuard.recordLoose(lookupName, &mutationCache.looseResourcePathsByName.find(lookupName)->second)) return fail("lookup guard did not admit test input");
    mutationCache.looseResourcePathsByName.find(lookupName)->second = "after";
    if (pathGuard.inputsUnchanged()) return fail("same-count in-place loose winner mutation escaped guard");
    mutationCache = seed; guardStats = {};
    RuntimeFutureVoxelBootstrapContractProgress progress{&types, &context, &mutationCache, &guardStats};
    auto progressLimits = limits; progressLimits.loadingProgress = runtimeFutureVoxelBootstrapContractProgressCallback;
    progressLimits.loadingProgressContext = &progress;
    const auto now = std::chrono::steady_clock::now();
    RuntimeFutureVoxelBootstrapState callbackState{context, context, mutationCache, progressLimits, guardStats, now};
    runtimeFutureVoxelBootstrapContractSnapshot(callbackState);
    callbackState.lastProgress = now - std::chrono::milliseconds(101);
    { RuntimeFutureVoxelBootstrapStateScope active(callbackState);
      if (!callbackState.permit()) return fail("loading progress guard unexpectedly refused");
      const auto before = guardStats;
      { RuntimeFutureVoxelBootstrapReferenceScope disabled(true);
        prewarmRuntimeFutureNormalVoxelTypes(types, context, mutationCache, {}, guardStats); }
      { RuntimeRendererEnvironmentScope disabledCache(runtimeVoxelLoadingPrewarmContractDisabledCacheEnvironment);
        prewarmRuntimeFutureNormalVoxelTypes(types, context, mutationCache, {}, guardStats); }
      { RuntimeVoxelLoadingPrewarmReferenceScope disabledTotal(true);
        prewarmRuntimeFutureNormalVoxelTypes(types, context, mutationCache, {}, guardStats); }
      if (!runtimeFutureVoxelBootstrapContractStatsEqual(before, guardStats)) return fail("nested same-Stats reset active budget before skip"); }
    if (progress.calls != 1 || guardStats.progressCallbacks != 1 || !progress.aliasPreserved || !progress.foreignSkipped
        || guardStats.sourceReads != 0 || guardStats.newSources != 0 || guardStats.newEntries != 0
        || !runtimeFutureVoxelBootstrapContractSeedPreserved(seed, mutationCache)) return fail("progress nesting/alias/no-request boundary failed");
    std::cout << "[RenderSmokeFutureVoxelBootstrap] bodyFacings=" << bodyHits << " completeEntries=" << cases
        << " actualParts=" << parts.size() << " partHits=" << partHits << " shadowFacings=" << shadows
        << " nativeSources=" << warmed.voxelSourcesByImage.size() - seed.voxelSourcesByImage.size()
        << " boundedProgress=" << progress.calls << " coldFallback=yes negativeFirstWriter=yes unknownEarlierRefused=yes\n";
    return true;
}
