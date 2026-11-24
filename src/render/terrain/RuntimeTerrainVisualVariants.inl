// Presentation-only TMP variants. Canonical tile metadata and geometry loading
// remain the authority for navigation, radar and authored map geometry.
// Variant eligibility and selection values are calculated by RuntimeTerrainVisualSelection.

static std::uint64_t runtimeTerrainVisualSelectionHash(
    const TerrainCell& cell, int tileIndex, const RuntimeMapRenderContext& context
) {
    const auto extension = context.theater
        ? toLowerAscii(context.theater->getTileExtension()) : std::string{};
    return concord::render::runtimeTerrainVisualFamilyHash(cell.x, cell.y, tileIndex, extension);
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
    RuntimeTerrainTmpResourceByteSource source(cache, &context);
    const auto tmpContext = runtimeTerrainTmpContextForRender(context);
    const std::string familyRoot = runtimeTerrainTileLookupCacheRoot(context)
        + "|structures=" + normalizedLowerPath(context.structureRootPath);
    auto& family = concord::render::runtimeTerrainVisualFamilyForCell(
        tileIndex, *canonical, familyRoot, {tmpContext, cache, source, cache.mixArchivePaths},
        cache.terrainVisualFamilyCacheRoot, cache.terrainVisualFamiliesByTileIndex);
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
    const RuntimeTmpTileFile* variant = concord::render::runtimeFindTerrainVisualCachedFile(choice->locator, cache);
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
