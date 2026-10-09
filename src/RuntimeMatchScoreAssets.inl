// Shell art must be resolved without the map's theater, Art.ini aliases or
// unit remap palette. Keep it in a separate UI cache, as the main menu does.
RuntimeUiShpCacheEntry* runtimeMatchScoreFrame(RuntimeWindowState& state,
    const std::string& id, const std::string& paletteId, int frame = 0) {
    const std::string key = "match_score|" + id + "|" + std::to_string(frame);
    auto& entry = state.uiShpByKey[key];
    if (entry.loadAttempted) return entry.loaded ? &entry : nullptr;
    entry.loadAttempted = true;
    const std::filesystem::path root(runtimeAssetRootPath(state.config));
    RuntimeMapRenderContext context;
    context.assetRootPath = pathString(root / "ra2");
    context.tileRootPath = context.structureRootPath = context.assetRootPath;
    const bool bar = id.find("scrnlbar") != std::string::npos;
    const bool yuri = id.rfind("mpy", 0) == 0;
    std::string error, source;
    const auto loadShp = [&](const std::filesystem::path& path) {
        if (!decodeRuntimeShpFrameFile(pathString(path), frame, entry.image, error)) return false;
        entry.sourcePath = pathString(path); return true;
    };
    const auto loadPcx = [&](const std::filesystem::path& path) {
        if (!decodeRuntimePcxImageFile(pathString(path), entry.image, entry.embeddedPalette, error)) return false;
        entry.sourcePath = pathString(path); entry.pcxFallback = true; return true;
    };
    if (const auto mod = findRuntimeLooseUiResourcePath(state, "", id, bar ? ".pcx" : ".shp", true))
        entry.loaded = bar ? loadPcx(*mod) : loadShp(*mod);
    // Expansion overrides supply a matching Yuri background/palette and PCX
    // strip family. Do not mix the older LOCALMD background into those strips.
    bool expansionYuri = false;
    if (!entry.loaded && yuri) {
        const auto path = root / "expandmd01" / (id + (bar ? ".pcx" : ".shp"));
        entry.loaded = bar ? loadPcx(path) : loadShp(path);
        expansionYuri = entry.loaded;
    }
    if (!entry.loaded && bar) {
        entry.loaded = loadRuntimePcxImageFromMixArchive(pathString(root / "ra2md" / "ntrlmd.mix"),
            state.renderCache, id, entry.image, entry.embeddedPalette, entry.sourcePath, error);
        entry.pcxFallback = entry.loaded;
    }
    if (!entry.loaded && !bar) {
        const auto mix = id == "mpsscrnl" ? root / "ra2md" / "ntrlmd.mix"
            : yuri || id == "mnbttn" ? root / "ra2md" / "localmd.mix" : root / "ra2" / "neutral.mix";
        entry.loaded = loadRuntimeShpFrameFromMixArchive(pathString(mix), state.renderCache,
            id, frame, entry.image, entry.sourcePath, error);
    }
    if (!entry.loaded) {
        if (const auto* fallback = loadRuntimeUiShpFrame(state, "", id, frame)) {
            entry = *fallback;
        }
    }
    if (!entry.loaded) { entry.errorMessage = error; return nullptr; }
    if (entry.embeddedPalette.empty()) {
        bool loaded = false;
        if (!state.config.modRootPath.empty()) {
            for (const auto& path : {std::filesystem::path(state.config.modRootPath) / (paletteId + ".pal"),
                    std::filesystem::path(state.config.modRootPath) / "pal" / (paletteId + ".pal")})
                if (loadRuntimePaletteFile(pathString(path), entry.embeddedPalette, error)) { loaded = true; break; }
        }
        if (!loaded && expansionYuri)
            loaded = loadRuntimePaletteFile(pathString(root / "expandmd01" / "mpyscrn.pal"), entry.embeddedPalette, error);
        if (!loaded) {
            const auto mix = paletteId == "shell" ? root / "ra2" / "cache.mix"
                : paletteId == "mpsscrn" || paletteId == "mpyscrn" ? root / "ra2md" / "localmd.mix"
                : paletteId == "mainbttn" ? root / "ra2" / "local.mix" : root / "ra2" / "neutral.mix";
            loaded = loadRuntimePaletteFromMixArchive(pathString(mix), state.renderCache,
                paletteId, entry.embeddedPalette, source, error);
        }
        if (!loaded) loaded = loadRuntimePaletteResource(context, state.renderCache,
            paletteId, entry.embeddedPalette, source, error);
        if (!loaded) { entry.loaded = false; entry.errorMessage = error; return nullptr; }
    }
    return &entry;
}
