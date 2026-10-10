bool runRuntimePaletteCacheHitContractCheck(std::string& error) {
    auto storage = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderCache& cache = *storage;
    RuntimeMapRenderContext context;
    context.assetRootPath = "palette-cache-contract-unused-root";
    context.palettePath = "palette-cache-contract-isotem.pal";
    context.structurePalettePath = "palette-cache-contract-structure.pal";
    cache.loadedPalettePath = context.palettePath;
    cache.paletteLoadAttempted = cache.paletteLoaded = true;
    cache.palette = {{11, 22, 33}};
    cache.loadedStructurePalettePath = context.structurePalettePath;
    cache.structurePaletteLoadAttempted = cache.structurePaletteLoaded = true;
    cache.structurePalette = {{44, 55, 66}};
    const auto key = [&](const char* suffix) {
        return context.assetRootPath + "|" + context.palettePath + suffix;
    };
    const auto seed = [](RuntimePaletteCacheEntry& entry, const std::string& entryKey) {
        entry.key = entryKey;
        entry.loadAttempted = entry.loaded = true;
        entry.palette = {{77, 88, 99}};
    };
    seed(cache.unitPalette, key("|unit"));
    RuntimePaletteCacheEntry& overlay = cache.namedPalettesByKey[key("|overlay")];
    RuntimePaletteCacheEntry& resource = cache.namedPalettesByKey[key("|resource-tmp")];
    seed(overlay, key("|overlay"));
    seed(resource, key("|resource-tmp"));
    if (runtimeUnitPaletteForRender(context, cache, false) != &cache.unitPalette.palette
        || runtimeUnitPaletteForRender(context, cache, true) != &cache.unitPalette.palette
        || runtimeOverlayPaletteForRender(context, cache) != &overlay.palette
        || runtimeResourceTmpPaletteForRender(context, cache) != &resource.palette) {
        error = "loaded palette cache did not return its exact palette storage";
        return false;
    }
    cache.unitPalette.loaded = false;
    overlay.loaded = false;
    resource.loaded = false;
    if (runtimeUnitPaletteForRender(context, cache, false) != nullptr
        || runtimeUnitPaletteForRender(context, cache, true) != &cache.structurePalette
        || runtimeOverlayPaletteForRender(context, cache) != &cache.palette
        || runtimeResourceTmpPaletteForRender(context, cache) != &cache.palette) {
        error = "palette cache shortcut changed an existing failed-load fallback";
        return false;
    }
    overlay.loaded = true;
    if (runtimeResourceTmpPaletteForRender(context, cache) != &overlay.palette) {
        error = "resource palette failure stopped using its overlay fallback";
        return false;
    }
    // A loaded marker without the corresponding attempted flag must still
    // enter the original loader. The lookup may succeed or fail on this host.
    seed(cache.unitPalette, key("|unit"));
    cache.unitPalette.loadAttempted = false;
    (void)runtimeUnitPaletteForRender(context, cache, false);
    if (!cache.unitPalette.loadAttempted) {
        error = "palette loaded flag bypassed the required initial load";
        return false;
    }
    seed(cache.unitPalette, "different-root|different-palette|unit");
    (void)runtimeUnitPaletteForRender(context, cache, false);
    if (cache.unitPalette.key != key("|unit")) {
        error = "unit palette shortcut ignored its complete context key";
        return false;
    }
    context.assetRootPath += "-changed";
    context.palettePath += "-changed";
    RuntimePaletteCacheEntry& changedOverlay =
        cache.namedPalettesByKey[key("|overlay")];
    RuntimePaletteCacheEntry& changedResource =
        cache.namedPalettesByKey[key("|resource-tmp")];
    seed(changedOverlay, key("|overlay"));
    seed(changedResource, key("|resource-tmp"));
    if (runtimeOverlayPaletteForRender(context, cache) != &changedOverlay.palette
        || runtimeResourceTmpPaletteForRender(context, cache) != &changedResource.palette) {
        error = "named palette shortcut reused a prior resource context";
        return false;
    }
    std::cout << "Palette cache: loadedStorage=preserved fallback=preserved "
              << "attemptedFlag=checked contextKey=checked\n";
    return true;
}
