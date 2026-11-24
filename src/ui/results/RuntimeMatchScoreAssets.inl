template<class ResolveRoot, class FindMod, class Fallback>
struct RuntimeMatchScoreAssetSources {
    RuntimeMapRenderCache& cache;
    const std::string& modRootPath;
    const ResolveRoot& resolveRoot;
    const FindMod& findModFile;
    const Fallback& loadFallback;
    std::optional<RuntimeMapRenderContext> paletteContext{};

    std::filesystem::path assetRoot() const { return resolveRoot(); }
    const std::string& modRoot() const { return modRootPath; }
    std::string pathText(const std::filesystem::path& path) const { return pathString(path); }

    void preparePaletteContext(const std::filesystem::path& root) {
        auto& context = paletteContext.emplace();
        context.assetRootPath = pathString(root);
        context.tileRootPath = context.structureRootPath = context.assetRootPath;
    }

    auto findMod(const std::string& id, const std::string& extension) const {
        return findModFile(id, extension);
    }

    const RuntimeUiShpCacheEntry* fallback(const std::string& id, int frame) const {
        return loadFallback(id, frame);
    }

    bool readShpFile(const std::filesystem::path& path, int frame,
        RuntimeShpFrameImage& image, std::string& error) const {
        return decodeRuntimeShpFrameFile(pathString(path), frame, image, error);
    }

    bool readPcxFile(const std::filesystem::path& path, RuntimeShpFrameImage& image,
        std::vector<RuntimePaletteColor>& palette, std::string& error) const {
        return decodeRuntimePcxImageFile(pathString(path), image, palette, error);
    }

    bool readShpArchive(const std::filesystem::path& path, const std::string& id, int frame,
        RuntimeShpFrameImage& image, std::string& source, std::string& error) const {
        return loadRuntimeShpFrameFromMixArchive(pathString(path), cache, id, frame, image, source, error);
    }

    bool readPcxArchive(const std::filesystem::path& path, const std::string& id, RuntimeShpFrameImage& image,
        std::vector<RuntimePaletteColor>& palette, std::string& source, std::string& error) const {
        return loadRuntimePcxImageFromMixArchive(pathString(path), cache, id, image, palette, source, error);
    }

    bool readPaletteFile(const std::filesystem::path& path,
        std::vector<RuntimePaletteColor>& palette, std::string& error) const {
        return loadRuntimePaletteFile(pathString(path), palette, error);
    }

    bool readPaletteArchive(const std::filesystem::path& path, const std::string& id,
        std::vector<RuntimePaletteColor>& palette, std::string& source, std::string& error) const {
        return loadRuntimePaletteFromMixArchive(pathString(path), cache, id, palette, source, error);
    }

    bool readPaletteResource(const std::string& id, std::vector<RuntimePaletteColor>& palette,
        std::string& source, std::string& error) const {
        return loadRuntimePaletteResource(*paletteContext, cache, id, palette, source, error);
    }
};

RuntimeUiShpCacheEntry* runtimeMatchScoreFrame(RuntimeWindowState& state,
    const std::string& id, const std::string& paletteId, int frame = 0) {
    const auto resolveRoot = [&] { return std::filesystem::path(runtimeAssetRootPath(state.config)); };
    const auto findMod = [&](const std::string& image, const std::string& extension) {
        return findRuntimeLooseUiResourcePath(state, "", image, extension, true);
    };
    const auto fallback = [&](const std::string& image, int index) {
        return loadRuntimeUiShpFrame(state, "", image, index);
    };
    RuntimeMatchScoreAssetSources sources{state.renderCache, state.config.modRootPath, resolveRoot, findMod, fallback};
    return concord::ui::loadMatchScoreFrame(state.uiShpByKey, sources, id, paletteId, frame);
}
