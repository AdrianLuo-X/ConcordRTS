#pragma once

#include <filesystem>
#include <string>

namespace concord::ui {

// Each attempt belongs to the caller's existing UI asset cache. Reads and
// fallback go through the same source loaders as ordinary UI art.
template<class Cache, class Sources>
typename Cache::mapped_type* loadMatchScoreFrame(Cache& uiCache, Sources& sources,
    const std::string& id, const std::string& paletteId, int frame) {
    const std::string key = "match_score|" + id + "|" + std::to_string(frame);
    auto& entry = uiCache[key];
    if (entry.loadAttempted) return entry.loaded ? &entry : nullptr;
    entry.loadAttempted = true;
    const auto root = sources.assetRoot();
    sources.preparePaletteContext(root / "ra2");
    const bool bar = id.find("scrnlbar") != std::string::npos;
    const bool yuri = id.rfind("mpy", 0) == 0;
    std::string error, source;
    const auto loadShp = [&](const std::filesystem::path& path) {
        if (!sources.readShpFile(path, frame, entry.image, error)) return false;
        entry.sourcePath = sources.pathText(path);
        return true;
    };
    const auto loadPcx = [&](const std::filesystem::path& path) {
        if (!sources.readPcxFile(path, entry.image, entry.embeddedPalette, error)) return false;
        entry.sourcePath = sources.pathText(path);
        entry.pcxFallback = true;
        return true;
    };
    if (const auto mod = sources.findMod(id, bar ? ".pcx" : ".shp")) {
        entry.loaded = bar ? loadPcx(*mod) : loadShp(*mod);
    }
    // Expansion entries use their associated Yuri palette and strip family.
    bool expansionYuri = false;
    if (!entry.loaded && yuri) {
        const auto path = root / "expandmd01" / (id + (bar ? ".pcx" : ".shp"));
        entry.loaded = bar ? loadPcx(path) : loadShp(path);
        expansionYuri = entry.loaded;
    }
    if (!entry.loaded && bar) {
        entry.loaded = sources.readPcxArchive(root / "ra2md" / "ntrlmd.mix",
            id, entry.image, entry.embeddedPalette, entry.sourcePath, error);
        entry.pcxFallback = entry.loaded;
    }
    if (!entry.loaded && !bar) {
        const auto mix = id == "mpsscrnl" ? root / "ra2md" / "ntrlmd.mix"
            : yuri || id == "mnbttn" ? root / "ra2md" / "localmd.mix" : root / "ra2" / "neutral.mix";
        entry.loaded = sources.readShpArchive(mix, id, frame, entry.image, entry.sourcePath, error);
    }
    if (!entry.loaded) {
        if (const auto* fallback = sources.fallback(id, frame)) {
            entry = *fallback;
        }
    }
    if (!entry.loaded) {
        entry.errorMessage = error;
        return nullptr;
    }
    if (entry.embeddedPalette.empty()) {
        bool loaded = false;
        if (!sources.modRoot().empty()) {
            for (const auto& path : {std::filesystem::path(sources.modRoot()) / (paletteId + ".pal"),
                    std::filesystem::path(sources.modRoot()) / "pal" / (paletteId + ".pal")}) {
                if (sources.readPaletteFile(path, entry.embeddedPalette, error)) {
                    loaded = true;
                    break;
                }
            }
        }
        if (!loaded && expansionYuri) {
            loaded = sources.readPaletteFile(root / "expandmd01" / "mpyscrn.pal", entry.embeddedPalette, error);
        }
        if (!loaded) {
            const auto mix = paletteId == "shell" ? root / "ra2" / "cache.mix"
                : paletteId == "mpsscrn" || paletteId == "mpyscrn" ? root / "ra2md" / "localmd.mix"
                : paletteId == "mainbttn" ? root / "ra2" / "local.mix" : root / "ra2" / "neutral.mix";
            loaded = sources.readPaletteArchive(mix, paletteId, entry.embeddedPalette, source, error);
        }
        if (!loaded) {
            loaded = sources.readPaletteResource(paletteId, entry.embeddedPalette, source, error);
        }
        if (!loaded) {
            entry.loaded = false;
            entry.errorMessage = error;
            return nullptr;
        }
    }
    return &entry;
}

} // namespace concord::ui
