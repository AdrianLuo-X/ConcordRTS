// Included after RuntimeVoxelLoadingPrewarm.inl, outside the first renderer
// anonymous namespace. Normal source/raster producers are unchanged.
namespace {
thread_local int runtimeFutureVoxelBootstrapReferenceOverride = -1;

bool runtimeFutureVoxelBootstrapReference() {
    if (runtimeFutureVoxelBootstrapReferenceOverride >= 0)
        return runtimeFutureVoxelBootstrapReferenceOverride != 0;
    const char* raw = runtimeRendererEnvironmentValue("CONCORD_FUTURE_VOXEL_BOOTSTRAP_REFERENCE");
    return raw != nullptr && std::strcmp(raw, "1") == 0;
}

class RuntimeFutureVoxelBootstrapReferenceScope {
public:
    explicit RuntimeFutureVoxelBootstrapReferenceScope(bool value)
        : previous_(runtimeFutureVoxelBootstrapReferenceOverride) {
        runtimeFutureVoxelBootstrapReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeFutureVoxelBootstrapReferenceScope() { runtimeFutureVoxelBootstrapReferenceOverride = previous_; }
    RuntimeFutureVoxelBootstrapReferenceScope(const RuntimeFutureVoxelBootstrapReferenceScope&) = delete;
    RuntimeFutureVoxelBootstrapReferenceScope& operator=(const RuntimeFutureVoxelBootstrapReferenceScope&) = delete;
private:
    int previous_;
};

struct RuntimeFutureVoxelBootstrapOrigin {
    std::string path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
};

struct RuntimeFutureVoxelBootstrapLooseLookup { std::string name, path; bool found = false; };
struct RuntimeFutureVoxelBootstrapMixLookup {
    std::string path, name;
    bool attempted = false, loaded = false, found = false;
    std::uint64_t fileSize = 0, dataStart = 0;
    std::uint32_t dataSize = 0, flags = 0;
    MixEntry entry{};
};

struct RuntimeFutureVoxelBootstrapState;
thread_local RuntimeFutureVoxelBootstrapState* runtimeFutureVoxelBootstrapState = nullptr;

struct RuntimeFutureVoxelBootstrapState {
    const RuntimeMapRenderContext& sourceContext;
    RuntimeMapRenderContext context;
    RuntimeMapRenderCache& live;
    const RuntimeFutureVoxelBootstrapLimits& limits;
    RuntimeFutureVoxelBootstrapStats& stats;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastProgress = started;
    bool callbackActive = false;
    bool stopped = false;
    bool familyRefused = false;
    std::size_t familyReadBytes = 0;
    std::vector<RuntimeFutureVoxelBootstrapOrigin> origins;
    std::vector<RuntimeFutureVoxelBootstrapLooseLookup> looseLookups;
    std::vector<RuntimeFutureVoxelBootstrapMixLookup> mixLookups;
    std::size_t lookupBytes = 0;
    std::string looseRoot = live.looseResourceIndexRootPath;
    std::string mixRoot = live.mixArchiveRootPath;
    bool looseBuilt = live.looseResourceIndexBuilt;
    bool mixScanned = live.mixArchivePathsScanned;
    std::size_t looseCount = live.looseResourcePathsByName.size();
    std::vector<std::string> mixPaths;
    std::vector<RuntimePaletteColor> vplPalette;
    std::vector<unsigned char> vplLookup;
    bool vplAttempted = live.voxelPaletteLookupLoadAttempted;
    bool vplLoaded = live.voxelPaletteLookupLoaded;
    unsigned int vplFirst = live.voxelPaletteFirstRemap;
    unsigned int vplLast = live.voxelPaletteLastRemap;
    unsigned int vplSections = live.voxelPaletteSectionCount;
    unsigned int vplNeutral = live.voxelPaletteNeutralSection;
    std::string vplPath = live.voxelPaletteLookupSourcePath;
    std::string vplError = live.voxelPaletteLookupErrorMessage;

    bool inputsUnchanged() const {
        if (sourceContext.assetRootPath != context.assetRootPath
            || sourceContext.art != context.art || sourceContext.rules != context.rules
            || sourceContext.stableRenderConfiguration != context.stableRenderConfiguration
            || live.looseResourceIndexRootPath != looseRoot || live.mixArchiveRootPath != mixRoot
            || live.looseResourceIndexBuilt != looseBuilt || live.mixArchivePathsScanned != mixScanned
            || live.looseResourcePathsByName.size() != looseCount || live.mixArchivePaths != mixPaths
            || live.voxelPaletteLookupLoadAttempted != vplAttempted || live.voxelPaletteLookupLoaded != vplLoaded
            || live.voxelPaletteFirstRemap != vplFirst || live.voxelPaletteLastRemap != vplLast
            || live.voxelPaletteSectionCount != vplSections || live.voxelPaletteNeutralSection != vplNeutral
            || live.voxelPaletteLookupSourcePath != vplPath || live.voxelPaletteLookupErrorMessage != vplError
            || live.voxelPaletteLookup != vplLookup || live.voxelPalette.size() != vplPalette.size()) return false;
        for (std::size_t i = 0; i < vplPalette.size(); ++i)
            if (live.voxelPalette[i].r != vplPalette[i].r || live.voxelPalette[i].g != vplPalette[i].g
                || live.voxelPalette[i].b != vplPalette[i].b) return false;
        for (const auto& lookup : looseLookups) {
            const auto found = live.looseResourcePathsByName.find(lookup.name);
            if ((found != live.looseResourcePathsByName.end()) != lookup.found
                || (lookup.found && found->second != lookup.path)) return false;
        }
        for (const auto& lookup : mixLookups) {
            const auto found = live.mixArchivesByPath.find(lookup.path);
            if (found == live.mixArchivesByPath.end() || found->second.loadAttempted != lookup.attempted
                || found->second.loaded != lookup.loaded) return false;
            if (!lookup.loaded) continue;
            const auto& archive = found->second.archive;
            if (archive.getFileSize() != lookup.fileSize || archive.getDataStartOffset() != lookup.dataStart
                || archive.getDataSize() != lookup.dataSize || archive.getFlags() != lookup.flags) return false;
            const auto* entry = archive.findEntryByName(lookup.name);
            if ((entry != nullptr) != lookup.found) return false;
            if (entry != nullptr && (entry->id != lookup.entry.id || entry->offset != lookup.entry.offset
                    || entry->size != lookup.entry.size || entry->absoluteOffset != lookup.entry.absoluteOffset)) return false;
        }
        return true;
    }

    bool permit() {
        if (stopped || callbackActive) return false;
        if (!inputsUnchanged()) { stats.sourceChangedStopped = true; stopped = true; return false; }
        if (stats.attempts >= limits.maximumAttempts || stats.newEntries >= limits.maximumNewEntries
            || stats.addedPayloadBytes >= limits.maximumAddedPayloadBytes) {
            stats.budgetStopped = true; stopped = true; return false;
        }
        if (!(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
                < limits.maximumWallMilliseconds)) {
            stats.timeStopped = true; stopped = true; return false;
        }
        if (limits.loadingProgress != nullptr) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastProgress).count() >= 100.0) {
                lastProgress = now;
                struct CallbackScope { bool& value; explicit CallbackScope(bool& v) : value(v) { value = true; }
                    ~CallbackScope() { value = false; } } callback(callbackActive);
                ++stats.progressCallbacks;
                limits.loadingProgress(limits.loadingProgressContext);
            }
            // A pure loading HWND callback consumes this same soft deadline.
            if (!inputsUnchanged()) { stats.sourceChangedStopped = true; stopped = true; return false; }
            if (!(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
                    < limits.maximumWallMilliseconds)) {
                stats.timeStopped = true; stopped = true; return false;
            }
        }
        return true;
    }

    bool allowRead(std::size_t size, bool doubleCopy) {
        const std::size_t multiplier = doubleCopy ? 2U : 1U;
        if (size > limits.maximumSourceReadBytes || size > limits.maximumFamilySourceBytes - familyReadBytes
            || size > limits.maximumTransientReadBytes / multiplier) {
            ++stats.refusedSourceReads; familyRefused = true; return false;
        }
        familyReadBytes += size;
        stats.maximumTransientReadBytes = std::max(stats.maximumTransientReadBytes, size * multiplier);
        return true;
    }

    bool recordLoose(const std::string& name, const std::string* path) {
        if (std::any_of(looseLookups.begin(), looseLookups.end(), [&](const auto& value) { return value.name == name; })) return true;
        const auto size = sizeof(RuntimeFutureVoxelBootstrapLooseLookup) + name.size() + 1U
            + (path != nullptr ? path->size() + 1U : 0U);
        if (size > limits.maximumIndexPathBytes - lookupBytes) { familyRefused = true; return false; }
        looseLookups.push_back({name, path == nullptr ? std::string() : *path, path != nullptr}); lookupBytes += size;
        return true;
    }

    bool recordMix(const std::string& path, const std::string& name, const RuntimeMixArchiveCacheEntry& cache) {
        if (std::any_of(mixLookups.begin(), mixLookups.end(), [&](const auto& value) { return value.path == path && value.name == name; })) return true;
        const auto size = sizeof(RuntimeFutureVoxelBootstrapMixLookup) + path.size() + name.size() + 2U;
        if (size > limits.maximumIndexPathBytes - lookupBytes) { familyRefused = true; return false; }
        RuntimeFutureVoxelBootstrapMixLookup value;
        value.path = path; value.name = name; value.attempted = cache.loadAttempted; value.loaded = cache.loaded;
        if (cache.loaded) {
            value.fileSize = cache.archive.getFileSize(); value.dataStart = cache.archive.getDataStartOffset();
            value.dataSize = cache.archive.getDataSize(); value.flags = cache.archive.getFlags();
            const auto* entry = cache.archive.findEntryByName(name);
            value.found = entry != nullptr; if (entry != nullptr) value.entry = *entry;
        }
        mixLookups.push_back(std::move(value)); lookupBytes += size; return true;
    }

    bool origin(const std::string& path, RuntimeFutureVoxelBootstrapOrigin& out) {
        std::error_code error;
        out.path = path;
        out.size = std::filesystem::file_size(std::filesystem::path(path), error);
        if (error) { familyRefused = true; return false; }
        out.modified = std::filesystem::last_write_time(std::filesystem::path(path), error);
        if (error) { familyRefused = true; return false; }
        return true;
    }

    bool originUnchanged(const RuntimeFutureVoxelBootstrapOrigin& before) {
        RuntimeFutureVoxelBootstrapOrigin after;
        if (!origin(before.path, after) || before.size != after.size || before.modified != after.modified) {
            stats.sourceChangedStopped = true; familyRefused = true; stopped = true; return false;
        }
        return true;
    }
};

class RuntimeFutureVoxelBootstrapStateScope {
public:
    explicit RuntimeFutureVoxelBootstrapStateScope(RuntimeFutureVoxelBootstrapState& state)
        : previous_(runtimeFutureVoxelBootstrapState) { runtimeFutureVoxelBootstrapState = &state; }
    ~RuntimeFutureVoxelBootstrapStateScope() { runtimeFutureVoxelBootstrapState = previous_; }
    RuntimeFutureVoxelBootstrapStateScope(const RuntimeFutureVoxelBootstrapStateScope&) = delete;
    RuntimeFutureVoxelBootstrapStateScope& operator=(const RuntimeFutureVoxelBootstrapStateScope&) = delete;
private:
    RuntimeFutureVoxelBootstrapState* previous_;
};

std::string runtimeFutureVoxelBootstrapSourceKey(const std::string& image,
    const RuntimeMapRenderContext& context) {
    return normalizedLowerPath(context.assetRootPath) + "|vxl=" + toLowerAscii(fileNameForVxlImageId(image))
        + "|hva=" + toLowerAscii(fileNameForHvaImageId(image));
}

bool runtimeFutureVoxelBootstrapRead(RuntimeFutureVoxelBootstrapState& state,
    const std::string& fileName, std::vector<unsigned char>& bytes, std::string& source, std::string& error) {
    bytes.clear(); source.clear(); error.clear();
    if (!state.permit() || state.familyRefused) return false;
    const auto lowerName = toLowerAscii(fileName);
    const auto loose = state.live.looseResourcePathsByName.find(lowerName);
    if (!state.recordLoose(lowerName, loose == state.live.looseResourcePathsByName.end() ? nullptr : &loose->second)) return false;
    if (loose != state.live.looseResourcePathsByName.end()) {
        RuntimeFutureVoxelBootstrapOrigin before;
        if (!state.origin(loose->second, before)) return false;
        // The original binary reader's same opened stream determines the size
        // before any resize. No separate stat is trusted as an allocation cap.
        std::ifstream input(loose->second, std::ios::binary);
        if (!input.is_open()) { error = "failed to open file: " + loose->second; return false; }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        if (size < 0 || static_cast<std::uintmax_t>(size) > std::numeric_limits<std::size_t>::max()) {
            error = "failed to determine file size: " + loose->second; return false;
        }
        if (!state.allowRead(static_cast<std::size_t>(size), false)) return false;
        input.seekg(0, std::ios::beg);
        bytes.resize(static_cast<std::size_t>(size));
        if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty()) { error = "failed to read file: " + loose->second; return false; }
        if (!state.originUnchanged(before)) return false;
        state.origins.push_back(std::move(before)); source = loose->second;
    } else {
        if (!state.mixScanned || normalizedLowerPath(state.mixRoot) != normalizedLowerPath(state.context.assetRootPath)) {
            state.stats.indexNotReadySkipped = true; state.familyRefused = true; return false;
        }
        auto searchPaths = state.mixPaths;
        std::stable_sort(searchPaths.begin(), searchPaths.end(), [&](const auto& left, const auto& right) {
            const int a = runtimeMixSearchPriorityForFile(left, fileName), b = runtimeMixSearchPriorityForFile(right, fileName);
            return a != b ? a < b : normalizedLowerPath(left) < normalizedLowerPath(right);
        });
        bool found = false;
        for (const auto& path : searchPaths) {
            const auto cached = state.live.mixArchivesByPath.find(path);
            if (cached == state.live.mixArchivesByPath.end() || !cached->second.loadAttempted) {
                ++state.stats.unknownEarlierArchives; state.familyRefused = true; return false;
            }
            if (!state.recordMix(path, fileName, cached->second)) return false;
            if (!cached->second.loaded) continue;
            const auto* entry = cached->second.archive.findEntryByName(fileName);
            if (entry == nullptr) continue;
            RuntimeFutureVoxelBootstrapOrigin before;
            if (!state.origin(path, before)) return false;
            // Original readEntryData keeps its offset/data-area/file-size and
            // complete-read checks. Account both original vectors before it
            // can allocate; no unknown archive/index is loaded speculatively.
            if (!state.allowRead(entry->size, true)) return false;
            std::vector<std::uint8_t> raw;
            if (!cached->second.archive.readEntryData(*entry, raw, error)) return false;
            bytes.assign(raw.begin(), raw.end());
            if (!state.originUnchanged(before)) return false;
            state.origins.push_back(std::move(before)); source = path + "!" + fileName;
            found = true; break;
        }
        if (!found) { error = "resource was not found in MIX archives: " + fileName; return false; }
    }
    ++state.stats.sourceReads; state.stats.sourceReadBytes += bytes.size(); return true;
}

bool runtimeFutureVoxelBootstrapSupported(const RuntimeVoxelSourceCacheEntry& source,
    RuntimeFutureVoxelBootstrapState& state) {
    RuntimeVoxelLoadingPrewarmLimits bounds;
    bounds.maximumSourceBytes = state.limits.maximumFamilySourceBytes;
    RuntimeVoxelLoadingPrewarmStats checks;
    if (!runtimeVoxelLoadingPrewarmSourceSupported(source, bounds, checks)
        || source.firstFrameTransforms.size() > 128U) { state.familyRefused = true; return false; }
    return true;
}

const RuntimeVoxelSourceCacheEntry* runtimeFutureVoxelBootstrapChooseSource(
    const std::vector<std::string>& candidates, RuntimeFutureVoxelBootstrapState& state,
    RuntimeMapRenderCache& scratch, std::string& image) {
    for (const auto& candidate : candidates) {
        if (candidate.empty() || candidate.size() > 128U || candidate.find('|') != std::string::npos) {
            state.familyRefused = true; return nullptr;
        }
        const auto key = runtimeFutureVoxelBootstrapSourceKey(candidate, state.context);
        auto ready = scratch.voxelSourcesByImage.find(key);
        if (ready != scratch.voxelSourcesByImage.end()) { image = candidate; return &ready->second; }
        RuntimeVoxelSourceCacheEntry value;
        const auto existing = state.live.voxelSourcesByImage.find(key);
        if (existing != state.live.voxelSourcesByImage.end() && existing->second.loadAttempted) {
            if (!existing->second.loaded) { ++state.stats.existingNegativeSources; continue; }
            if (!runtimeFutureVoxelBootstrapSupported(existing->second, state)) return nullptr;
            const std::size_t rawSize = existing->second.vxlBytes.size() + existing->second.hvaBytes.size();
            if (rawSize > state.limits.maximumFamilySourceBytes - state.familyReadBytes) {
                ++state.stats.refusedSourceReads; state.familyRefused = true; return nullptr;
            }
            state.familyReadBytes += rawSize;
            value.loadAttempted = true; value.loaded = true; value.metadata = existing->second.metadata;
            value.vxlBytes = existing->second.vxlBytes; value.hvaBytes = existing->second.hvaBytes;
            value.firstFrameTransforms = existing->second.firstFrameTransforms; value.errorMessage = existing->second.errorMessage;
            ++state.stats.existingSources;
        } else {
            value.loadAttempted = true; value.metadata.imageId = candidate;
            std::string vxlName, hvaName, error;
            if (!runtimeFutureVoxelBootstrapRead(state, fileNameForVxlImageId(candidate), value.vxlBytes, vxlName, error)) {
                if (state.familyRefused || state.stopped) return nullptr;
                continue;
            }
            if (!runtimeFutureVoxelBootstrapRead(state, fileNameForHvaImageId(candidate), value.hvaBytes, hvaName, error)) {
                if (state.familyRefused || state.stopped) return nullptr;
                continue;
            }
            value.loaded = parseRuntimeVxlMetadataFromBytes(value.vxlBytes, vxlName, value.metadata, error);
            if (value.loaded) value.loaded = parseRuntimeHvaMetadataFromBytes(value.hvaBytes, hvaName, value.metadata, error);
            // Reject before the frame-transform parser can allocate a section
            // vector from untrusted counts. Normal fallback retains old IO.
            if (value.loaded && (value.metadata.hvaFrameCount < 1 || value.metadata.hvaFrameCount > 16
                    || value.metadata.vxlSectionCount < 1 || value.metadata.vxlSectionCount > 128
                    || value.metadata.hvaSectionCount < 1 || value.metadata.hvaSectionCount > 128)) {
                state.familyRefused = true; return nullptr;
            }
            if (value.loaded) value.loaded = parseRuntimeHvaFrameTransformsFromBytes(value.hvaBytes,
                hvaName, value.firstFrameTransforms, error);
            value.errorMessage = error;
            if (!value.loaded) continue;
            if (!runtimeFutureVoxelBootstrapSupported(value, state)) return nullptr;
        }
        image = candidate;
        return &scratch.voxelSourcesByImage.emplace(key, std::move(value)).first->second;
    }
    return nullptr;
}

std::size_t runtimeFutureVoxelBootstrapSourcePayload(const std::string& key, const RuntimeVoxelSourceCacheEntry& value) {
    std::size_t size = sizeof(std::pair<const std::string, RuntimeVoxelSourceCacheEntry>) + key.capacity() + 1U;
    for (const auto* s : {&value.metadata.imageId, &value.metadata.vxlPath, &value.metadata.hvaPath,
            &value.metadata.vxlSectionSample, &value.metadata.hvaSectionSample, &value.errorMessage}) size += s->capacity() + 1U;
    size += value.vxlBytes.capacity() + value.hvaBytes.capacity()
        + value.firstFrameTransforms.capacity() * sizeof(RuntimeHvaTransform);
    for (const auto& [frame, transforms] : value.transformsByFrame) {
        (void)frame; size += sizeof(std::pair<const int, std::vector<RuntimeHvaTransform>>)
            + transforms.capacity() * sizeof(RuntimeHvaTransform);
    }
    return size;
}

std::size_t runtimeFutureVoxelBootstrapFixedPayload(const std::string& key, const RuntimeVxlFixedScaleCacheEntry& value) {
    return sizeof(std::pair<const std::string, RuntimeVxlFixedScaleCacheEntry>) + key.capacity() + 1U
        + value.category.capacity() + value.mode.capacity() + value.errorMessage.capacity() + 3U;
}

void runtimeFutureVoxelBootstrapCopyPalette(RuntimeFutureVoxelBootstrapState& state, RuntimeMapRenderCache& scratch) {
    scratch.voxelPaletteLookupLoadAttempted = state.vplAttempted;
    scratch.voxelPaletteLookupLoaded = state.vplLoaded;
    scratch.voxelPaletteFirstRemap = state.vplFirst; scratch.voxelPaletteLastRemap = state.vplLast;
    scratch.voxelPaletteSectionCount = state.vplSections; scratch.voxelPaletteNeutralSection = state.vplNeutral;
    scratch.voxelPalette = state.vplPalette; scratch.voxelPaletteLookup = state.vplLookup;
    scratch.voxelPaletteLookupSourcePath = state.vplPath; scratch.voxelPaletteLookupErrorMessage = state.vplError;
}

bool runtimeFutureVoxelBootstrapFit(const std::string& image, const std::string& owner,
    RuntimeFutureVoxelBootstrapState& state, RuntimeMapRenderCache& scratch) {
    const auto source = scratch.voxelSourcesByImage.find(runtimeFutureVoxelBootstrapSourceKey(image, state.context));
    if (source == scratch.voxelSourcesByImage.end()) return false;
    const auto sizing = runtimeVxlRasterSizingForOwner(owner);
    const auto key = runtimeVxlFixedScaleAssetKey(image, owner, source->second.vxlBytes, source->second.metadata.vxlPath, sizing);
    const auto existing = state.live.voxelFixedScaleByKey.find(key);
    if (existing != state.live.voxelFixedScaleByKey.end()) scratch.voxelFixedScaleByKey.emplace(key, existing->second);
    return ensureRuntimeVxlFixedScaleForAsset(image, owner, source->second.vxlBytes,
        source->second.firstFrameTransforms, source->second.metadata.vxlPath, sizing, &scratch, scratch) != nullptr;
}

bool runtimeFutureVoxelBootstrapPublish(RuntimeFutureVoxelBootstrapState& state, RuntimeMapRenderCache& scratch) {
    if (!state.inputsUnchanged()) { state.stats.sourceChangedStopped = true; state.stopped = true; return false; }
    for (const auto& origin : state.origins) if (!state.originUnchanged(origin)) return false;
    for (const auto& [key, value] : scratch.voxelSourcesByImage) {
        const auto live = state.live.voxelSourcesByImage.find(key);
        if (live == state.live.voxelSourcesByImage.end() || !live->second.loadAttempted) continue;
        const auto& actual = live->second;
        const auto& a = actual.metadata; const auto& b = value.metadata;
        bool same = actual.loaded == value.loaded && actual.errorMessage == value.errorMessage
            && actual.vxlBytes == value.vxlBytes && actual.hvaBytes == value.hvaBytes
            && a.imageId == b.imageId && a.vxlPath == b.vxlPath && a.hvaPath == b.hvaPath
            && a.vxlSectionCount == b.vxlSectionCount && a.hvaSectionCount == b.hvaSectionCount
            && a.hvaFrameCount == b.hvaFrameCount && a.vxlFileSize == b.vxlFileSize && a.hvaFileSize == b.hvaFileSize
            && a.vxlSectionSample == b.vxlSectionSample && a.hvaSectionSample == b.hvaSectionSample
            && actual.firstFrameTransforms.size() == value.firstFrameTransforms.size();
        if (same) for (std::size_t i = 0; i < actual.firstFrameTransforms.size(); ++i)
            if (actual.firstFrameTransforms[i].valid != value.firstFrameTransforms[i].valid
                || std::memcmp(actual.firstFrameTransforms[i].m.data(), value.firstFrameTransforms[i].m.data(),
                    sizeof(double) * actual.firstFrameTransforms[i].m.size()) != 0) { same = false; break; }
        if (!same) { state.stats.sourceChangedStopped = true; state.stopped = true; return false; }
    }
    // A stopped soft deadline may still admit the already completed positive
    // requests. It never starts a new decode. All payload limits remain hard
    // bounds on retained values, excluding allocator/buckets/decoder scratch.
    for (auto& [key, value] : scratch.voxelSourcesByImage) {
        if (!value.loadAttempted || !value.loaded || state.live.voxelSourcesByImage.find(key) != state.live.voxelSourcesByImage.end()) continue;
        const auto size = runtimeFutureVoxelBootstrapSourcePayload(key, value);
        if (size > state.limits.maximumAddedPayloadBytes - state.stats.addedPayloadBytes) { state.stats.budgetStopped = true; state.stopped = true; return false; }
        if (state.live.voxelSourcesByImage.emplace(key, std::move(value)).second) {
            ++state.stats.newSources; state.stats.addedPayloadBytes += size;
        }
    }
    for (auto& [key, value] : scratch.voxelFixedScaleByKey) {
        if (!value.loadAttempted || !value.valid || state.live.voxelFixedScaleByKey.find(key) != state.live.voxelFixedScaleByKey.end()) continue;
        const auto size = runtimeFutureVoxelBootstrapFixedPayload(key, value);
        if (size > state.limits.maximumAddedPayloadBytes - state.stats.addedPayloadBytes) { state.stats.budgetStopped = true; state.stopped = true; return false; }
        if (state.live.voxelFixedScaleByKey.emplace(key, std::move(value)).second) {
            ++state.stats.newFixedScales; state.stats.addedPayloadBytes += size;
        }
    }
    for (auto& [key, value] : scratch.voxelObjectsByType) {
        if (!value.loadAttempted || !value.loaded || !value.hasSprite) continue;
        if (state.live.voxelObjectsByType.find(key) != state.live.voxelObjectsByType.end()) { ++state.stats.existingEntries; continue; }
        std::string alias;
        if (state.context.stableRenderConfiguration)
            for (const auto& [lookup, raster] : scratch.stableVoxelCacheKeyByLookup)
                if (raster == key) { alias = lookup; break; }
        // Do not replace any existing first-writer lookup, even one mapping to
        // a failed raster. Ordinary cold queries keep their original outcome.
        if (!alias.empty() && state.live.stableVoxelCacheKeyByLookup.find(alias) != state.live.stableVoxelCacheKeyByLookup.end()) {
            ++state.stats.existingEntries; continue;
        }
        const auto size = runtimeVoxelLoadingPrewarmEntryPayload(value, key, alias, !alias.empty());
        if (state.stats.newEntries >= state.limits.maximumNewEntries
            || size > state.limits.maximumAddedPayloadBytes - state.stats.addedPayloadBytes) {
            state.stats.budgetStopped = true; state.stopped = true; return false;
        }
        if (state.live.voxelObjectsByType.emplace(key, std::move(value)).second) {
            ++state.stats.newEntries; state.stats.addedPayloadBytes += size;
            ++state.live.voxelCacheMisses;
            if (!alias.empty()) state.live.stableVoxelCacheKeyByLookup.emplace(alias, key);
        }
    }
    return true;
}
} // namespace

void prewarmRuntimeFutureNormalVoxelTypes(const std::vector<std::string>& typeIds,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    const RuntimeFutureVoxelBootstrapLimits& requested, RuntimeFutureVoxelBootstrapStats& stats) {
    // Alias guard precedes every reset/disabled/reference branch.
    if (runtimeFutureVoxelBootstrapState != nullptr && &runtimeFutureVoxelBootstrapState->stats == &stats) return;
    stats = {};
    if (runtimeVoxelLoadingPrewarmReference() || runtimeFutureVoxelBootstrapReference()) { stats.referenceSkipped = true; return; }
    if (runtimeFutureVoxelBootstrapState != nullptr || runtimeVoxelLoadingPrewarmState != nullptr) { stats.nestedSkipped = true; return; }
    if (runtimeVxlDisableCache()) { stats.cacheDisabledSkipped = true; return; }
    if (!runtimeVxlFixedScaleRequested() || (runtimeVxlFixedScaleMode() != "asset" && runtimeVxlFixedScaleMode() != "manual")) {
        stats.unsupportedProfileSkipped = true; return;
    }
    if (runtimeVxlPaletteLookupEnabled() && !cache.voxelPaletteLookupLoadAttempted) { stats.paletteNotReadySkipped = true; return; }
    if (context.rules == nullptr || context.art == nullptr || context.assetRootPath.empty() || context.assetRootPath.size() > 4096U
        || !cache.looseResourceIndexBuilt
        || normalizedLowerPath(cache.looseResourceIndexRootPath) != normalizedLowerPath(context.assetRootPath)
        || (cache.mixArchivePathsScanned && normalizedLowerPath(cache.mixArchiveRootPath) != normalizedLowerPath(context.assetRootPath))) {
        stats.indexNotReadySkipped = true; return;
    }
    RuntimeFutureVoxelBootstrapLimits limits;
    limits.maximumFamilies = std::min(limits.maximumFamilies, requested.maximumFamilies);
    limits.maximumNewEntries = std::min(limits.maximumNewEntries, requested.maximumNewEntries);
    limits.maximumAttempts = std::min(limits.maximumAttempts, requested.maximumAttempts);
    limits.maximumAddedPayloadBytes = std::min(limits.maximumAddedPayloadBytes, requested.maximumAddedPayloadBytes);
    limits.maximumFamilySourceBytes = std::min(limits.maximumFamilySourceBytes, requested.maximumFamilySourceBytes);
    limits.maximumSourceReadBytes = std::min(limits.maximumSourceReadBytes, requested.maximumSourceReadBytes);
    limits.maximumTransientReadBytes = std::min(limits.maximumTransientReadBytes, requested.maximumTransientReadBytes);
    limits.maximumIndexPaths = std::min(limits.maximumIndexPaths, requested.maximumIndexPaths);
    limits.maximumIndexPathBytes = std::min(limits.maximumIndexPathBytes, requested.maximumIndexPathBytes);
    limits.maximumWallMilliseconds = std::isfinite(requested.maximumWallMilliseconds)
        ? std::min(limits.maximumWallMilliseconds, requested.maximumWallMilliseconds) : 0.0;
    limits.loadingProgress = requested.loadingProgress; limits.loadingProgressContext = requested.loadingProgressContext;
    std::size_t pathBytes = 0;
    for (const auto& path : cache.mixArchivePaths) {
        if (path.size() > 4096U || path.size() + 1U > limits.maximumIndexPathBytes - pathBytes) { stats.indexNotReadySkipped = true; return; }
        pathBytes += path.size() + 1U;
    }
    if (cache.mixArchivePaths.size() > limits.maximumIndexPaths || cache.voxelPalette.size() > 256U
        || cache.voxelPaletteLookup.size() > 256U * 256U) { stats.indexNotReadySkipped = true; return; }
    const auto started = std::chrono::steady_clock::now();
    try {
        RuntimeFutureVoxelBootstrapState state{context, context, cache, limits, stats, started};
        state.context.specialMechanismSnapshots = nullptr; // ordinary future units have no live-instance snapshots
        state.mixPaths = cache.mixArchivePaths; state.vplPalette = cache.voxelPalette; state.vplLookup = cache.voxelPaletteLookup;
        RuntimeFutureVoxelBootstrapStateScope active(state);
        std::vector<std::string> seen;
        seen.reserve(std::min<std::size_t>(limits.maximumFamilies, 32U));
        std::size_t considered = 0;
        for (const auto& type : typeIds) {
            if (considered++ == 128U) { stats.budgetStopped = true; break; }
            if (!state.permit()) break;
            if (type.empty() || type.size() > 128U || type.find('|') != std::string::npos) { ++stats.unsupportedTypes; continue; }
            const auto identity = toLowerAscii(type);
            if (std::find(seen.begin(), seen.end(), identity) != seen.end()) { ++stats.duplicateFamilies; continue; }
            if (stats.families == limits.maximumFamilies) { stats.budgetStopped = true; break; }
            seen.push_back(identity);
            WorldObjectInstance descriptor;
            descriptor.kind = WorldObjectKind::Unit; descriptor.type = type; descriptor.alive = true;
            descriptor.instanceId = -1; descriptor.health = 100;
            const auto* unit = context.rules->findUnitType("UnitType", type);
            const auto* art = findArtTypeForObject(descriptor, state.context);
            if (unit == nullptr || art == nullptr || !art->voxel) { ++stats.unsupportedTypes; continue; }
            ++stats.families;
            state.familyReadBytes = 0; state.familyRefused = false; state.origins.clear();
            state.looseLookups.clear(); state.mixLookups.clear(); state.lookupBytes = 0;
            RuntimeMapRenderCache scratch;
            runtimeFutureVoxelBootstrapCopyPalette(state, scratch);
            std::string body, turret, barrel;
            if (runtimeFutureVoxelBootstrapChooseSource(resolveObjectVoxelImageIdCandidates(descriptor, state.context), state, scratch, body) == nullptr
                || !runtimeFutureVoxelBootstrapFit(body, type, state, scratch)) { ++stats.failedFamilies; continue; }
            if (runtimeObjectHasTurretForRender(descriptor, state.context)) {
                runtimeFutureVoxelBootstrapChooseSource(runtimeUnitTurretImageCandidates(descriptor, state.context), state, scratch, turret);
                if (!state.familyRefused && runtimeDrawSeparateUnitVoxelBarrels())
                    runtimeFutureVoxelBootstrapChooseSource(runtimeUnitBarrelImageCandidates(descriptor, state.context), state, scratch, barrel);
            }
            if (state.familyRefused || state.stopped) { ++stats.failedFamilies; continue; }
            const int shadow = runtimeObjectVoxelShadowSection(descriptor, state.context);
            const bool partShadows = art->useTurretShadow;
            if (!runtimeVxlSharedPartScaleEnabled()) {
                if ((!turret.empty() && !runtimeFutureVoxelBootstrapFit(turret, type + "|turret", state, scratch))
                    || (!barrel.empty() && !runtimeFutureVoxelBootstrapFit(barrel, type + "|barrel", state, scratch))
                    || (partShadows && !turret.empty() && !runtimeFutureVoxelBootstrapFit(turret, type + "|turret-shadow", state, scratch))
                    || (partShadows && !barrel.empty() && !runtimeFutureVoxelBootstrapFit(barrel, type + "|barrel-shadow", state, scratch))) {
                    ++stats.failedFamilies; continue;
                }
            }
            std::size_t auxiliaryBytes = 0;
            for (const auto& [key, value] : scratch.voxelSourcesByImage)
                if (cache.voxelSourcesByImage.find(key) == cache.voxelSourcesByImage.end()) auxiliaryBytes += runtimeFutureVoxelBootstrapSourcePayload(key, value);
            for (const auto& [key, value] : scratch.voxelFixedScaleByKey)
                if (cache.voxelFixedScaleByKey.find(key) == cache.voxelFixedScaleByKey.end()) auxiliaryBytes += runtimeFutureVoxelBootstrapFixedPayload(key, value);
            if (auxiliaryBytes >= limits.maximumAddedPayloadBytes - stats.addedPayloadBytes) { stats.budgetStopped = true; break; }
            RuntimeVoxelLoadingPrewarmLimits rasterLimits;
            rasterLimits.maximumNewEntries = limits.maximumNewEntries - stats.newEntries;
            rasterLimits.maximumAttempts = limits.maximumAttempts - stats.attempts;
            rasterLimits.maximumAddedPayloadBytes = limits.maximumAddedPayloadBytes - stats.addedPayloadBytes - auxiliaryBytes;
            rasterLimits.maximumWallMilliseconds = limits.maximumWallMilliseconds;
            RuntimeVoxelLoadingPrewarmStats rasterStats;
            RuntimeVoxelLoadingPrewarmState raster{&state.context, &scratch, &rasterLimits, &rasterStats, started};
            {
                RuntimeVoxelLoadingPrewarmStateScope admission(raster);
                const auto request = [&](const std::string& image, const std::string& owner, int facing, double fit, int section) {
                    if (!state.permit() || raster.stopped) return static_cast<RuntimeVoxelCacheEntry*>(nullptr);
                    auto* result = runtimeVoxelLoadingPrewarmRequest(raster, image, owner, facing, fit, section, 0);
                    return result;
                };
                for (int facing = 0; facing < 32 && !state.stopped && !raster.stopped; ++facing) {
                    const auto* result = request(body, type, facing, 0.0, shadow);
                    if (result == nullptr) break; // actual decode failure is already in rasterStats
                    const double fit = result->rasterFitScale; // never borrow across another producer call
                    if (!turret.empty()) {
                        const double scale = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(fit, runtimeVxlTurretRelativeScale()) : 0.0;
                        request(turret, type + "|turret", facing, scale, 0);
                        if (partShadows) request(turret, type + "|turret-shadow", facing, scale, 0);
                    }
                    if (!barrel.empty()) {
                        const double scale = runtimeVxlSharedPartScaleEnabled() ? runtimeVoxelPartFitScale(fit, runtimeVxlBarrelRelativeScale()) : 0.0;
                        request(barrel, type + "|barrel", facing, scale, 0);
                        if (partShadows) request(barrel, type + "|barrel-shadow", facing, scale, 0);
                    }
                }
            }
            stats.attempts += rasterStats.attempts; stats.decodedEntries += rasterStats.decodedEntries;
            stats.failedEntries += rasterStats.failedEntries;
            stats.budgetStopped = stats.budgetStopped || rasterStats.budgetStopped;
            stats.timeStopped = stats.timeStopped || rasterStats.timeStopped;
            if (!scratch.voxelObjectsByType.empty()) runtimeFutureVoxelBootstrapPublish(state, scratch);
            if (raster.stopped) state.stopped = true;
        }
    } catch (const std::bad_alloc&) { stats.allocationStopped = true; }
    stats.elapsedMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
