// Included after the renderer's first anonymous namespace. The public entry
// point runs only in loading; normal paints retain the original cold loader.
namespace {
struct RuntimeVoxelLoadingPrewarmFamily {
    const WorldObjectInstance* object = nullptr;
    std::string body, turret, barrel;
    int shadowSection = 0;
    int frames = 1;
    bool turretShadow = false;
    std::array<double, 32> bodyFitScales{};
};

bool runtimeVoxelLoadingPrewarmSourceSupported(const RuntimeVoxelSourceCacheEntry& source,
    const RuntimeVoxelLoadingPrewarmLimits& limits, RuntimeVoxelLoadingPrewarmStats& stats) {
    if (!source.loaded || !source.loadAttempted || source.vxlBytes.empty()
        || source.hvaBytes.empty() || source.firstFrameTransforms.empty()) {
        ++stats.unavailableSources; return false;
    }
    if (source.metadata.hvaFrameCount < 1 || source.metadata.hvaFrameCount > limits.maximumHvaFrames) {
        ++stats.highHvaSources; return false;
    }
    if (source.vxlBytes.size() > limits.maximumSourceBytes
        || source.hvaBytes.size() > limits.maximumSourceBytes - source.vxlBytes.size()
        || source.metadata.vxlSectionCount < 1 || source.metadata.vxlSectionCount > limits.maximumSections
        || source.metadata.hvaSectionCount < 1 || source.metadata.hvaSectionCount > limits.maximumSections
        || source.vxlBytes.size() < 802U) {
        ++stats.oversizedSources; return false;
    }
    const auto& bytes = source.vxlBytes;
    const std::size_t sections = readUInt32(bytes, 20);
    if (sections == 0 || sections > static_cast<std::size_t>(limits.maximumSections)) {
        ++stats.oversizedSources; return false;
    }
    const std::size_t footer = 802U + sections * 28U + readUInt32(bytes, 28);
    if (footer > bytes.size() || sections * 92U > bytes.size() - footer) {
        ++stats.oversizedSources; return false;
    }
    std::size_t totalGrid = 0;
    for (std::size_t section = 0; section < sections; ++section) {
        const auto offset = footer + section * 92U;
        const std::size_t grid = static_cast<std::size_t>(bytes[offset + 88])
            * bytes[offset + 89] * bytes[offset + 90];
        if (grid > limits.maximumSectionVoxelGrid
            || grid > limits.maximumTotalVoxelGrid - totalGrid) {
            ++stats.oversizedSources; return false;
        }
        totalGrid += grid;
    }
    return true;
}

const RuntimeVoxelSourceCacheEntry* runtimeVoxelLoadingPrewarmChooseSource(
    const std::vector<std::string>& candidates, const RuntimeMapRenderContext& context,
    const RuntimeMapRenderCache& cache, const RuntimeVoxelLoadingPrewarmLimits& limits,
    RuntimeVoxelLoadingPrewarmStats& stats, std::string& image) {
    for (const auto& candidate : candidates) {
        if (candidate.empty() || candidate.size() > 128U) { ++stats.unavailableSources; return nullptr; }
        const std::string key = normalizedLowerPath(context.assetRootPath)
            + "|vxl=" + toLowerAscii(fileNameForVxlImageId(candidate))
            + "|hva=" + toLowerAscii(fileNameForHvaImageId(candidate));
        const auto found = cache.voxelSourcesByImage.find(key);
        // An unknown earlier candidate may later load successfully. Do not
        // guess which fallback image production will select, or read its file.
        if (found == cache.voxelSourcesByImage.end() || !found->second.loadAttempted) {
            ++stats.unavailableSources; return nullptr;
        }
        if (!found->second.loaded) continue;
        if (!runtimeVoxelLoadingPrewarmSourceSupported(found->second, limits, stats)) return nullptr;
        image = candidate;
        return &found->second;
    }
    ++stats.unavailableSources;
    return nullptr;
}

RuntimeVoxelCacheEntry* runtimeVoxelLoadingPrewarmRequest(RuntimeVoxelLoadingPrewarmState& state,
    const std::string& image, const std::string& owner, int facing, double scale,
    int shadow, int frame) {
    if (!state.permitRequest() || !state.pumpProgressIfDue()) return nullptr;
    ++state.stats->attempts;
    const auto oldSize = state.cache->voxelObjectsByType.size();
    // Bypass the paint geometry memo: a rejected temporary must never become
    // a remembered null query in an outer batch. The actual producer/key is
    // otherwise exactly the one used by normal body/part/shadow drawing.
    auto* result = loadRuntimeVoxelImageFromCacheUnbatched(image, owner, facing,
        *state.context, *state.cache, scale, shadow, frame, 0, nullptr);
    if (state.cache->voxelObjectsByType.size() == oldSize && result != nullptr)
        ++state.stats->existingEntries;
    return result;
}
} // namespace

void prewarmRuntimeCurrentWorldNormalVoxels(const GameWorld& world,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    const RuntimeVoxelLoadingPrewarmLimits& requestedLimits, RuntimeVoxelLoadingPrewarmStats& stats) {
    // A nested caller may alias the active budget counters. Do not reset or
    // annotate that outer object, even when total-reference/cache-disable is on.
    if (runtimeVoxelLoadingPrewarmState != nullptr && runtimeVoxelLoadingPrewarmState->stats == &stats) return;
    stats = {};
    if (runtimeVoxelLoadingPrewarmReference()) { stats.referenceSkipped = true; return; }
    // One loading-only dispatch. Normal paints never inspect this switch.
    const bool v1Reference = runtimeVoxelLoadingPrewarmV1Reference();
    stats.v1Reference = v1Reference;
    if (runtimeVoxelLoadingPrewarmState != nullptr) { stats.nestedSkipped = true; return; }
    if (runtimeVxlDisableCache()) { stats.cacheDisabledSkipped = true; return; }
    // The original raster key rounds forced part scale to1/10000, unlike
    // stable lookup's full bits. Automatic/image-dependent fitting can give
    // two exact inputs the same raster key; changing their first-load order
    // would then change which image wins. Production asset/manual fixed-scale
    // body fit is independent of facing/HVA/image for one ordinary owner.
    const auto fixedMode = runtimeVxlFixedScaleMode();
    if (!runtimeVxlFixedScaleRequested() || (fixedMode != "asset" && fixedMode != "manual")) {
        stats.unsupportedProfileSkipped = true; return;
    }
    // VPL must have been resolved by the original first-frame producer. A
    // negative already-attempted result is also exact and requires no new IO.
    if (runtimeVxlPaletteLookupEnabled() && !cache.voxelPaletteLookupLoadAttempted) {
        stats.paletteNotReadySkipped = true; return;
    }
    if (context.assetRootPath.size() > 4096U) { ++stats.unavailableSources; return; }
    RuntimeVoxelLoadingPrewarmLimits limits;
    if (v1Reference) limits.maximumWallMilliseconds = 5000.0;
    else {
        limits.loadingProgress = requestedLimits.loadingProgress;
        limits.loadingProgressContext = requestedLimits.loadingProgressContext;
    }
    limits.maximumNewEntries = std::min(limits.maximumNewEntries, requestedLimits.maximumNewEntries);
    limits.maximumAttempts = std::min(limits.maximumAttempts, requestedLimits.maximumAttempts);
    limits.maximumAddedPayloadBytes = std::min(limits.maximumAddedPayloadBytes, requestedLimits.maximumAddedPayloadBytes);
    limits.maximumFamilies = std::min(limits.maximumFamilies, requestedLimits.maximumFamilies);
    limits.maximumSourceBytes = std::min(limits.maximumSourceBytes, requestedLimits.maximumSourceBytes);
    limits.maximumSectionVoxelGrid = std::min(limits.maximumSectionVoxelGrid, requestedLimits.maximumSectionVoxelGrid);
    limits.maximumTotalVoxelGrid = std::min(limits.maximumTotalVoxelGrid, requestedLimits.maximumTotalVoxelGrid);
    limits.maximumHvaFrames = std::min(limits.maximumHvaFrames, requestedLimits.maximumHvaFrames);
    limits.maximumSections = std::min(limits.maximumSections, requestedLimits.maximumSections);
    limits.maximumWallMilliseconds = std::isfinite(requestedLimits.maximumWallMilliseconds)
        ? std::min(limits.maximumWallMilliseconds, requestedLimits.maximumWallMilliseconds) : 0.0;
    RuntimeVoxelLoadingPrewarmState state{&context, &cache, &limits, &stats,
        std::chrono::steady_clock::now()};
    RuntimeVoxelLoadingPrewarmStateScope admission(state);
    try {
        if (!state.permitRequest()) return;
        std::vector<RuntimeVoxelLoadingPrewarmFamily> families;
        families.reserve(std::min<std::size_t>(limits.maximumFamilies, 32U));
        for (const auto& object : world.getObjects()) {
            if (!object.alive || object.kind != WorldObjectKind::Unit || object.type.empty()
                || object.type.size() > 128U || object.type.find('|') != std::string::npos) continue;
            if (!state.permitRequest()) break;
            RuntimeVoxelLoadingPrewarmFamily family;
            family.object = &object;
            const auto* source = runtimeVoxelLoadingPrewarmChooseSource(
                resolveObjectVoxelImageIdCandidates(object, context), context, cache, limits, stats, family.body);
            if (source == nullptr) continue;
            family.frames = source->metadata.hvaFrameCount;
            family.shadowSection = runtimeObjectVoxelShadowSection(object, context);
            if (runtimeObjectHasTurretForRender(object, context)) {
                runtimeVoxelLoadingPrewarmChooseSource(runtimeUnitTurretImageCandidates(object, context),
                    context, cache, limits, stats, family.turret);
                if (runtimeDrawSeparateUnitVoxelBarrels())
                    runtimeVoxelLoadingPrewarmChooseSource(runtimeUnitBarrelImageCandidates(object, context),
                        context, cache, limits, stats, family.barrel);
                const auto* art = findArtTypeForObject(object, context);
                family.turretShadow = art != nullptr && art->useTurretShadow;
            }
            const bool duplicate = std::any_of(families.begin(), families.end(), [&](const auto& prior) {
                return prior.object->type == object.type && prior.body == family.body
                    && prior.turret == family.turret && prior.barrel == family.barrel
                    && prior.shadowSection == family.shadowSection && prior.frames == family.frames
                    && prior.turretShadow == family.turretShadow;
            });
            if (duplicate) { ++stats.duplicateFamilies; continue; }
            if (families.size() == limits.maximumFamilies) { stats.budgetStopped = true; break; }
            families.push_back(std::move(family));
        }
        stats.families = families.size();
        if (!v1Reference) {
            // The admitted asset/manual profile fixes body scale per ordinary
            // owner. Complete each normal body and its same-facing real parts
            // before additional HVA frames consume the loading budget.
            const auto partsAtFacing = [&](RuntimeVoxelLoadingPrewarmFamily& family, int facing, double fit) {
                if (!family.turret.empty() && !state.stopped) {
                    const double partFit = runtimeVxlSharedPartScaleEnabled()
                        ? runtimeVoxelPartFitScale(fit, runtimeVxlTurretRelativeScale()) : 0.0;
                    runtimeVoxelLoadingPrewarmRequest(state, family.turret, family.object->type + "|turret", facing, partFit, 0, 0);
                    if (family.turretShadow && !state.stopped)
                        runtimeVoxelLoadingPrewarmRequest(state, family.turret, family.object->type + "|turret-shadow", facing, partFit, 0, 0);
                }
                if (!family.barrel.empty() && !state.stopped) {
                    const double partFit = runtimeVxlSharedPartScaleEnabled()
                        ? runtimeVoxelPartFitScale(fit, runtimeVxlBarrelRelativeScale()) : 0.0;
                    runtimeVoxelLoadingPrewarmRequest(state, family.barrel, family.object->type + "|barrel", facing, partFit, 0, 0);
                    if (family.turretShadow && !state.stopped)
                        runtimeVoxelLoadingPrewarmRequest(state, family.barrel, family.object->type + "|barrel-shadow", facing, partFit, 0, 0);
                }
            };
            for (int facing = 0; facing < 32 && !state.stopped; ++facing)
                for (auto& family : families) {
                    if (state.stopped) break;
                    auto* body = runtimeVoxelLoadingPrewarmRequest(state, family.body, family.object->type,
                        facing, 0.0, family.shadowSection, 0);
                    if (body != nullptr) {
                        // Copy before another loader call: rejected/cache-disabled
                        // decoder results may use a reusable temporary entry.
                        const double fit = body->rasterFitScale;
                        family.bodyFitScales[facing] = fit;
                        if (fit > 0.0) partsAtFacing(family, facing, fit);
                    }
                }
            for (int frame = 1; frame < limits.maximumHvaFrames && !state.stopped; ++frame)
                for (int facing = 0; facing < 32 && !state.stopped; ++facing)
                    for (auto& family : families) {
                        if (state.stopped) break;
                        if (frame >= family.frames || !(family.bodyFitScales[facing] > 0.0)) continue;
                        runtimeVoxelLoadingPrewarmRequest(state, family.body, family.object->type,
                            facing, 0.0, family.shadowSection, frame);
                    }
        } else {
        // Round-robin frame zero first: every current family gets normal angles
        // before extra animation frames can consume the remaining budget.
        for (int frame = 0; frame < limits.maximumHvaFrames && !state.stopped; ++frame)
            for (int facing = 0; facing < 32 && !state.stopped; ++facing)
                for (auto& family : families) {
                    if (state.stopped) break;
                    if (frame >= family.frames || (frame != 0 && !(family.bodyFitScales[facing] > 0.0))) continue;
                    auto* body = runtimeVoxelLoadingPrewarmRequest(state, family.body, family.object->type,
                        facing, 0.0, family.shadowSection, frame);
                    if (frame == 0 && body != nullptr) family.bodyFitScales[facing] = body->rasterFitScale;
                }
        // Parts use all actual body fit-bit identities: automatic fitting can
        // vary with body facing. Never round/approximate a scale to gain hits.
        for (auto& family : families) {
            if (state.stopped) break;
            std::vector<double> scales;
            for (double fit : family.bodyFitScales) {
                if (!(fit > 0.0)) continue;
                if (!runtimeVxlSharedPartScaleEnabled()) fit = 0.0;
                const bool duplicate = std::any_of(scales.begin(), scales.end(), [&](double prior) {
                    return std::memcmp(&fit, &prior, sizeof(fit)) == 0;
                });
                if (!duplicate) scales.push_back(fit);
            }
            for (double fit : scales) for (int facing = 0; facing < 32 && !state.stopped; ++facing) {
                if (!family.turret.empty()) {
                    const double partFit = runtimeVxlSharedPartScaleEnabled()
                        ? runtimeVoxelPartFitScale(fit, runtimeVxlTurretRelativeScale()) : 0.0;
                    runtimeVoxelLoadingPrewarmRequest(state, family.turret, family.object->type + "|turret", facing, partFit, 0, 0);
                    if (family.turretShadow && !state.stopped)
                        runtimeVoxelLoadingPrewarmRequest(state, family.turret, family.object->type + "|turret-shadow", facing, partFit, 0, 0);
                }
                if (!family.barrel.empty() && !state.stopped) {
                    const double partFit = runtimeVxlSharedPartScaleEnabled()
                        ? runtimeVoxelPartFitScale(fit, runtimeVxlBarrelRelativeScale()) : 0.0;
                    runtimeVoxelLoadingPrewarmRequest(state, family.barrel, family.object->type + "|barrel", facing, partFit, 0, 0);
                    if (family.turretShadow && !state.stopped)
                        runtimeVoxelLoadingPrewarmRequest(state, family.barrel, family.object->type + "|barrel-shadow", facing, partFit, 0, 0);
                }
            }
        }
        } // Complete C23 ordering and original soft time ceiling.
    } catch (const std::bad_alloc&) {
        stats.allocationStopped = true; state.stopped = true;
    }
    stats.elapsedMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - state.started).count();
}
