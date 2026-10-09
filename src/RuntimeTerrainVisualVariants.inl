// Presentation-only TMP variants. Canonical tile metadata and geometry loading
// remain the authority for navigation, radar and authored map geometry.
static bool runtimeTerrainVisualMaskMatches(
    const std::vector<unsigned char>& first,
    const std::vector<unsigned char>& second
) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        if ((first[index] == 0) != (second[index] == 0)) return false;
    }
    return true;
}

static bool runtimeTerrainVisualSingleCell(const RuntimeTmpTileFile& file) {
    return file.loaded && file.gridWidth == 1 && file.gridHeight == 1
        && file.images.size() == 1 && file.images.front().present
        && file.images.front().hasTerrainType;
}

static bool runtimeTerrainVisualVariantCompatible(
    const RuntimeTmpTileFile& canonical, const RuntimeTmpTileFile& variant
) {
    if (!runtimeTerrainVisualSingleCell(canonical)
        || !runtimeTerrainVisualSingleCell(variant)
        || canonical.blockWidth != variant.blockWidth
        || canonical.blockHeight != variant.blockHeight) return false;
    const auto& original = canonical.images.front();
    const auto& alternate = variant.images.front();
    if (original.terrainType != alternate.terrainType
        || original.rampType != alternate.rampType
        || original.offsetX != alternate.offsetX
        || original.offsetY != alternate.offsetY
        || original.width != alternate.width || original.height != alternate.height
        || original.globalWidth != alternate.globalWidth
        || original.globalHeight != alternate.globalHeight
        || original.baseOffsetX != alternate.baseOffsetX
        || original.baseOffsetY != alternate.baseOffsetY
        || original.baseWidth != alternate.baseWidth
        || original.baseHeight != alternate.baseHeight
        || original.hasBaseZData != alternate.hasBaseZData
        || original.hasExtra != alternate.hasExtra
        || !runtimeTerrainVisualMaskMatches(original.pixels, alternate.pixels)
        || !runtimeTerrainVisualMaskMatches(original.basePixels, alternate.basePixels)) return false;
    if (original.hasBaseZData && original.baseZData != alternate.baseZData) return false;
    if (original.hasExtra
        && (original.extraOffsetX != alternate.extraOffsetX
            || original.extraOffsetY != alternate.extraOffsetY
            || original.extraWidth != alternate.extraWidth
            || original.extraHeight != alternate.extraHeight
            || original.hasExtraZData != alternate.hasExtraZData
            || !runtimeTerrainVisualMaskMatches(original.extraPixels, alternate.extraPixels)
            || (original.hasExtraZData && original.extraZData != alternate.extraZData))) return false;
    // Color pixels and radar RGB may vary. All radar queries retain canonical TMPs.
    return true;
}

static RuntimeTmpTileFile* runtimeDiscoverTerrainVisualNamed(
    const std::string& fileName, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, std::string& locator, std::string& sourceName
) {
    locator.clear(); sourceName.clear();
    const std::string assetRoot = context.assetRootPath.empty()
        ? cachedRuntimeMixExportsRootForContext(context, cache) : context.assetRootPath;
    const std::vector<std::string> roots{
        context.tileRootPath, context.assetRootPath, context.structureRootPath
    };
    std::vector<unsigned char> bytes;
    std::string error;
    if (!loadRuntimeResourceBytesByName(roots, assetRoot, fileName, cache,
            bytes, sourceName, error)) return nullptr;
    const bool actualMix = std::any_of(cache.mixArchivePaths.begin(), cache.mixArchivePaths.end(),
        [&](const std::string& archive) { return sourceName == archive + "!" + fileName; });
    std::error_code fileError;
    if (!actualMix && !std::filesystem::is_regular_file(sourceName, fileError)) {
        sourceName.clear(); return nullptr;
    }
    // The real source is part of this presentation-only key. This never aliases
    // the canonical named cache or adds a variant to its geometry memo.
    locator = "visual-source:" + sourceName;
    auto [entry, inserted] = cache.tmpTilesByPath.try_emplace(locator);
    auto& file = entry->second;
    if (!file.loadAttempted) {
        file.loadAttempted = true;
        file.loaded = loadRuntimeTmpTileBytes(bytes, sourceName, file, error);
        file.errorMessage = error;
    }
    return file.loaded ? &file : nullptr;
}

static const RuntimeTmpTileFile* runtimeFindTerrainVisualCachedFile(
    const std::string& locator, const RuntimeMapRenderCache& cache
) {
    const auto entry = cache.tmpTilesByPath.find(locator);
    return entry != cache.tmpTilesByPath.end() && entry->second.loaded ? &entry->second : nullptr;
}

static RuntimeTerrainVisualFamily& runtimeTerrainVisualFamilyForCell(
    int tileIndex, const RuntimeTmpTileFile& canonical,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    const std::string familyRoot = runtimeTerrainTileLookupCacheRoot(context)
        + "|structures=" + normalizedLowerPath(context.structureRootPath);
    if (cache.terrainVisualFamilyCacheRoot != familyRoot) {
        cache.terrainVisualFamilyCacheRoot = familyRoot;
        cache.terrainVisualFamiliesByTileIndex.clear();
    }
    auto [found, inserted] = cache.terrainVisualFamiliesByTileIndex.try_emplace(tileIndex);
    auto& family = found->second;
    if (!inserted) return family;
    const auto source = cache.terrainTilePathByIndex.find(tileIndex);
    if (source == cache.terrainTilePathByIndex.end()) return family;
    family.canonicalLocator = source->second;
    const bool namedBase = runtimeTerrainTileLocatorIsMixResource(source->second);
    const std::string originalName = namedBase
        ? runtimeTerrainTileResourceName(source->second)
        : std::filesystem::path(source->second).filename().string();
    if (namedBase) {
        // @mix-resource is a named-loader locator, not proof of a MIX payload.
        // Resolve its real source once, using exactly the original precedence.
        std::string discoveredLocator;
        if (!runtimeDiscoverTerrainVisualNamed(originalName, context, cache,
                discoveredLocator, family.canonicalSourceName)) {
            family.canonicalSourceName.clear(); return family;
        }
        const bool actualMix = std::any_of(cache.mixArchivePaths.begin(), cache.mixArchivePaths.end(),
            [&](const std::string& archive) { return family.canonicalSourceName == archive + "!" + originalName; });
        family.siblingOnly = !actualMix;
    } else {
        family.canonicalSourceName = source->second;
        family.siblingOnly = true;
    }
    if (!runtimeTerrainVisualSingleCell(canonical)) return family;
    const std::filesystem::path originalPath(originalName);
    const std::string stem = originalPath.stem().string();
    const std::string extension = originalPath.extension().string();
    if (stem.empty() || extension.empty()) return family;

    std::unordered_map<std::string, std::string> siblings;
    if (family.siblingOnly) {
        // Every actual loose base, including a named no-MIX fallback, only uses
        // siblings of its real directory. Never import another root's family.
        std::error_code error;
        std::filesystem::directory_iterator iterator(
            std::filesystem::path(family.canonicalSourceName).parent_path(),
            std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto& entry = *iterator;
            if (entry.is_regular_file(error)) {
                siblings.try_emplace(toLowerAscii(entry.path().filename().string()),
                    entry.path().generic_string());
            }
            if (!error) iterator.increment(error);
        }
    }
    const std::string canonicalError = cache.lastTmpTileLoadError;
    for (char suffix = 'a'; suffix <= 'z'; ++suffix) {
        const std::string name = stem + suffix + extension;
        RuntimeTerrainVisualVariant choice;
        choice.resourceName = name;
        RuntimeTmpTileFile* variant = nullptr;
        if (family.siblingOnly) {
            const auto sibling = siblings.find(toLowerAscii(name));
            if (sibling == siblings.end()) continue;
            choice.locator = sibling->second;
            choice.sourceName = sibling->second;
            variant = loadRuntimeTmpTileFromCache(choice.locator, cache);
        } else {
            variant = runtimeDiscoverTerrainVisualNamed(name, context, cache,
                choice.locator, choice.sourceName);
        }
        if (!variant) {
            if (!choice.sourceName.empty()) ++family.rejectedVariantCount;
            continue;
        }
        if (runtimeTerrainVisualVariantCompatible(canonical, *variant)) {
            family.variants.push_back(std::move(choice));
        } else {
            ++family.rejectedVariantCount;
        }
    }
    // Missing optional siblings must not replace the real base-load diagnostic.
    cache.lastTmpTileLoadError = canonicalError;
    return family;
}

static std::uint64_t runtimeTerrainVisualSelectionHash(
    const TerrainCell& cell, int tileIndex, const RuntimeMapRenderContext& context
) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&](unsigned char byte) {
        hash ^= byte; hash *= 1099511628211ULL;
    };
    for (std::uint32_t value : {static_cast<std::uint32_t>(cell.x),
            static_cast<std::uint32_t>(cell.y), static_cast<std::uint32_t>(tileIndex)}) {
        for (int shift = 0; shift != 32; shift += 8) append(static_cast<unsigned char>(value >> shift));
    }
    if (context.theater) {
        for (unsigned char byte : toLowerAscii(context.theater->getTileExtension())) append(byte);
    }
    // Mix every coordinate bit into low output bits before modulo selection.
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    return hash ^ (hash >> 31);
}

const RuntimeTmpSubTileImage* resolveTerrainVisualCellImage(
    const TerrainCell& cell, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, bool* authoredEmptySubTile,
    RuntimeTerrainVisualCellInfo* info
) {
    const int tileIndex = cell.tileIndex == 65535 ? 0 : cell.tileIndex;
    const int subTileIndex = cell.tileIndex == 65535 ? 0 : cell.subTileIndex;
    if (info) {
        *info = {};
        info->tileIndex = tileIndex; info->subTileIndex = subTileIndex;
        info->mappedClear = cell.tileIndex == 65535;
        info->selectionHash = runtimeTerrainVisualSelectionHash(cell, tileIndex, context);
    }
    bool authoredEmpty = false;
    const RuntimeTmpSubTileImage* original = resolveTerrainCellImage(
        tileIndex, subTileIndex, context, cache, &authoredEmpty);
    if (authoredEmptySubTile) *authoredEmptySubTile = authoredEmpty;
    if (info) info->authoredEmptySubTile = authoredEmpty;
    if (!original) return nullptr;
    const RuntimeTmpTileFile* canonical = loadRuntimeTerrainTileForIndex(tileIndex, context, cache);
    if (!canonical) return original;
    auto& family = runtimeTerrainVisualFamilyForCell(tileIndex, *canonical, context, cache);
    const std::uint64_t hash = info ? info->selectionHash
        : runtimeTerrainVisualSelectionHash(cell, tileIndex, context);
    const std::size_t selected = subTileIndex == 0
        ? static_cast<std::size_t>(hash % (family.variants.size() + 1)) : 0;
    const RuntimeTerrainVisualVariant* choice = selected == 0 ? nullptr : &family.variants[selected - 1];
    if (info) {
        const std::string canonicalName = runtimeTerrainTileLocatorIsMixResource(family.canonicalLocator)
            ? runtimeTerrainTileResourceName(family.canonicalLocator)
            : std::filesystem::path(family.canonicalLocator).filename().string();
        info->canonicalLocator = family.canonicalLocator;
        info->selectedLocator = choice ? choice->locator : family.canonicalLocator;
        info->canonicalResourceName = canonicalName;
        info->selectedResourceName = choice ? choice->resourceName : canonicalName;
        info->canonicalSourceName = family.canonicalSourceName;
        info->selectedSourceName = choice ? choice->sourceName : family.canonicalSourceName;
        info->eligibleVariantCount = static_cast<std::uint32_t>(family.variants.size());
        info->rejectedVariantCount = family.rejectedVariantCount;
        info->siblingOnly = family.siblingOnly;
    }
    if (!choice) return original;
    const RuntimeTmpTileFile* variant = runtimeFindTerrainVisualCachedFile(choice->locator, cache);
    if (!variant) {
        if (info) {
            info->selectedVariantFallback = true;
            info->selectedLocator = family.canonicalLocator;
            info->selectedResourceName = info->canonicalResourceName;
            info->selectedSourceName = family.canonicalSourceName;
        }
        return original;
    }
    return &variant->images.front();
}
