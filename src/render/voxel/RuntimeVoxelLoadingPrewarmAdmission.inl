// Included inside the renderer's anonymous namespace, after source/HVA helpers.
thread_local int runtimeVoxelLoadingPrewarmReferenceOverride = -1;
thread_local int runtimeVoxelLoadingPrewarmV1ReferenceOverride = -1;

bool runtimeVoxelLoadingPrewarmV1Reference() {
    if (runtimeVoxelLoadingPrewarmV1ReferenceOverride >= 0)
        return runtimeVoxelLoadingPrewarmV1ReferenceOverride != 0;
    const char* raw = runtimeRendererEnvironmentValue("CONCORD_VOXEL_LOADING_PREWARM_V1_REFERENCE");
    return raw != nullptr && std::strcmp(raw, "1") == 0;
}

class RuntimeVoxelLoadingPrewarmV1ReferenceScope {
public:
    explicit RuntimeVoxelLoadingPrewarmV1ReferenceScope(bool reference)
        : previous_(runtimeVoxelLoadingPrewarmV1ReferenceOverride) {
        runtimeVoxelLoadingPrewarmV1ReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeVoxelLoadingPrewarmV1ReferenceScope() {
        runtimeVoxelLoadingPrewarmV1ReferenceOverride = previous_;
    }
    RuntimeVoxelLoadingPrewarmV1ReferenceScope(const RuntimeVoxelLoadingPrewarmV1ReferenceScope&) = delete;
    RuntimeVoxelLoadingPrewarmV1ReferenceScope& operator=(const RuntimeVoxelLoadingPrewarmV1ReferenceScope&) = delete;
private:
    int previous_;
};

bool runtimeVoxelLoadingPrewarmReference() {
    if (runtimeVoxelLoadingPrewarmReferenceOverride >= 0)
        return runtimeVoxelLoadingPrewarmReferenceOverride != 0;
    const char* raw = runtimeRendererEnvironmentValue("CONCORD_VOXEL_LOADING_PREWARM_REFERENCE");
    return raw != nullptr && std::strcmp(raw, "1") == 0;
}

class RuntimeVoxelLoadingPrewarmReferenceScope {
public:
    explicit RuntimeVoxelLoadingPrewarmReferenceScope(bool reference)
        : previous_(runtimeVoxelLoadingPrewarmReferenceOverride) {
        runtimeVoxelLoadingPrewarmReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeVoxelLoadingPrewarmReferenceScope() {
        runtimeVoxelLoadingPrewarmReferenceOverride = previous_;
    }
    RuntimeVoxelLoadingPrewarmReferenceScope(const RuntimeVoxelLoadingPrewarmReferenceScope&) = delete;
    RuntimeVoxelLoadingPrewarmReferenceScope& operator=(const RuntimeVoxelLoadingPrewarmReferenceScope&) = delete;
private:
    int previous_;
};

struct RuntimeVoxelLoadingPrewarmState {
    const RuntimeMapRenderContext* context = nullptr;
    RuntimeMapRenderCache* cache = nullptr;
    const RuntimeVoxelLoadingPrewarmLimits* limits = nullptr;
    RuntimeVoxelLoadingPrewarmStats* stats = nullptr;
    std::chrono::steady_clock::time_point started;
    bool stopped = false;
    std::chrono::steady_clock::time_point lastProgress = started;
    bool progressCallbackActive = false;

    bool permitRequest() {
        if (stopped || progressCallbackActive) return false;
        if (stats->attempts >= limits->maximumAttempts
            || stats->newEntries >= limits->maximumNewEntries
            || stats->addedPayloadBytes >= limits->maximumAddedPayloadBytes) {
            stats->budgetStopped = true; stopped = true; return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        if (!(elapsed < limits->maximumWallMilliseconds)) {
            stats->timeStopped = true; stopped = true; return false;
        }
        return true;
    }

    bool pumpProgressIfDue() {
        if (progressCallbackActive) return false;
        // The default-null and C23 routes add no clock read or UI dispatch.
        if (limits->loadingProgress == nullptr || stats->v1Reference) return true;
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double, std::milli>(now - lastProgress).count() < 100.0) return true;
        lastProgress = now;
        struct CallbackScope {
            bool& active;
            bool previous;
            explicit CallbackScope(bool& value) : active(value), previous(value) { active = true; }
            ~CallbackScope() { active = previous; }
        };
        {
            // Keep the outer admission TLS in place: recursively entering the
            // public loading function reports nestedSkipped, while a direct
            // recursive request is rejected by permitRequest above.
            CallbackScope scope(progressCallbackActive);
            limits->loadingProgress(limits->loadingProgressContext);
        }
        // UI time consumes the same soft budget. Never restart its deadline or
        // decode another pose after a callback exhausts any original limit.
        return permitRequest();
    }
};

thread_local RuntimeVoxelLoadingPrewarmState* runtimeVoxelLoadingPrewarmState = nullptr;

class RuntimeVoxelLoadingPrewarmStateScope {
public:
    explicit RuntimeVoxelLoadingPrewarmStateScope(RuntimeVoxelLoadingPrewarmState& state)
        : previous_(runtimeVoxelLoadingPrewarmState) { runtimeVoxelLoadingPrewarmState = &state; }
    ~RuntimeVoxelLoadingPrewarmStateScope() { runtimeVoxelLoadingPrewarmState = previous_; }
    RuntimeVoxelLoadingPrewarmStateScope(const RuntimeVoxelLoadingPrewarmStateScope&) = delete;
    RuntimeVoxelLoadingPrewarmStateScope& operator=(const RuntimeVoxelLoadingPrewarmStateScope&) = delete;
private:
    RuntimeVoxelLoadingPrewarmState* previous_;
};

RuntimeVoxelLoadingPrewarmState* runtimeVoxelLoadingPrewarmFor(
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    int pitch, const RuntimeVoxelBodyRotation* rotation) {
    auto* state = runtimeVoxelLoadingPrewarmState;
    return state != nullptr && state->context == &context && state->cache == &cache
        && pitch == 0 && (rotation == nullptr || !rotation->active()) ? state : nullptr;
}

std::size_t runtimeVoxelLoadingPrewarmEntryPayload(const RuntimeVoxelCacheEntry& entry,
    const std::string& key, const std::string& stableKey, bool stable) {
    // Fresh decoder entries have no lazily prepared opaque/shadow/color tables.
    // Reject an unexpected derived allocation rather than undercount its capacity.
    const auto& image = entry.image;
    if (!image.spriteOpaqueSpans.empty() || !image.shadowMaskSpanCache.spans().empty()
        || !entry.shadowSpanCache.spans().empty()) return std::numeric_limits<std::size_t>::max();
    std::size_t bytes = sizeof(std::pair<const std::string, RuntimeVoxelCacheEntry>);
    const auto stringBytes = [&](const std::string& value) { bytes += value.capacity() + 1U; };
    stringBytes(key); stringBytes(entry.errorMessage);
    for (const auto* value : {&entry.metadata.imageId, &entry.metadata.vxlPath,
            &entry.metadata.hvaPath, &entry.metadata.vxlSectionSample, &entry.metadata.hvaSectionSample,
            &image.rasterFunctionActuallyCalled, &image.rasterCacheKey, &image.sourcePath}) stringBytes(*value);
    for (const auto* value : {&image.pixels, &image.rawColorIndices, &image.ownerRemapMask,
            &image.lightLevels, &image.normalIndices, &image.normalBrightnessLevels,
            &image.vplSections, &image.depthLevels, &entry.shadowPixels}) bytes += value->capacity();
    bytes += image.pixelMaterials.capacity() * sizeof(unsigned int);
    bytes += (image.packedBaseColors.capacity() + image.packedRemapColors.capacity()) * sizeof(std::uint32_t);
    if (stable) {
        bytes += sizeof(std::pair<const std::string, std::string>);
        stringBytes(stableKey); stringBytes(key);
    }
    return bytes;
}

RuntimeVoxelCacheEntry* runtimeVoxelLoadingPrewarmPublish(RuntimeVoxelLoadingPrewarmState& state,
    RuntimeVoxelCacheEntry&& candidate, const std::string& key, const std::string& stableKey,
    bool stable) {
    ++state.stats->decodedEntries;
    if (!candidate.loaded || !candidate.hasSprite) { ++state.stats->failedEntries; return nullptr; }
    const std::size_t bytes = runtimeVoxelLoadingPrewarmEntryPayload(candidate, key, stableKey, stable);
    if (state.stats->newEntries >= state.limits->maximumNewEntries
        || bytes > state.limits->maximumAddedPayloadBytes - state.stats->addedPayloadBytes) {
        state.stats->budgetStopped = true; state.stopped = true; return nullptr;
    }
    // No live raster node exists before successful admission. Original entries
    // are never overwritten/erased, and failure leaves ordinary cold fallback.
    auto [position, inserted] = state.cache->voxelObjectsByType.emplace(key, std::move(candidate));
    if (!inserted) { ++state.stats->existingEntries; return position->second.loaded ? &position->second : nullptr; }
    ++state.stats->newEntries;
    state.stats->addedPayloadBytes += bytes;
    ++state.cache->voxelCacheMisses;
    if (stable) state.cache->stableVoxelCacheKeyByLookup.insert_or_assign(stableKey, key);
    return &position->second;
}
