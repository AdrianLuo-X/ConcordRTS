// Independent complete original TMP producer/query family; no new query/codec
// function is called to calculate the old expected result.
namespace frozenTmpQuery {
using RuntimeMapRenderCache = RuntimeTerrainTmpCache;
using RuntimeMapRenderContext = RuntimeTerrainTmpContext;
inline RuntimeTerrainTmpByteSource* activeSource = nullptr;
inline RuntimeTerrainTmpMemoPort* activeMemo = nullptr;
using frozenTmpCodec::loadRuntimeTmpTileBytes;
struct PortBorrow {
 RuntimeTerrainTmpByteSource* previousSource;
 RuntimeTerrainTmpMemoPort* previousMemo;
 PortBorrow(RuntimeTerrainTmpByteSource& s,RuntimeTerrainTmpMemoPort& m)
  :previousSource(activeSource),previousMemo(activeMemo){activeSource=&s;activeMemo=&m;}
 ~PortBorrow(){activeSource=previousSource;activeMemo=previousMemo;}
 PortBorrow(const PortBorrow&)=delete;
 PortBorrow& operator=(const PortBorrow&)=delete;
};
bool loadRuntimeTmpTileFile(const std::string& p,RuntimeTmpTileFile& t,std::string& e){
 std::vector<unsigned char> bytes;
 return activeSource->readLooseBytes(p,bytes,e)&&frozenTmpCodec::loadRuntimeTmpTileBytes(bytes,p,t,e);
}
bool loadRuntimeResourceBytesByName(const std::vector<std::string>& roots,const std::string& asset,
 const std::string& name,RuntimeMapRenderCache&,std::vector<unsigned char>& bytes,std::string& origin,std::string& error){
 return activeSource->readResourceBytesByName(roots,asset,name,bytes,origin,error);
}
std::string cachedRuntimeMixExportsRootForContext(const RuntimeMapRenderContext&,RuntimeMapRenderCache&){
 return activeSource->cachedMixExportsRoot();
}
bool runtimeAssetRootHasMixArchives(const std::string& asset,RuntimeMapRenderCache&){
 return activeSource->assetRootHasMixArchives(asset);
}
bool findRuntimeGeometryTerrainTile(int tile,const RuntimeMapRenderContext&,RuntimeMapRenderCache&,
 RuntimeTmpTileFile*& file,bool& updates){return activeMemo->find(tile,file,updates);}
void rememberRuntimeGeometryTerrainTile(int tile,const RuntimeMapRenderContext&,RuntimeMapRenderCache&,
 RuntimeTmpTileFile* file,bool updates){activeMemo->remember(tile,file,updates);}

constexpr int RuntimeLandTypeUnknown = -1;
constexpr const char* runtimeTerrainMixResourcePrefix = "@mix-resource:";
constexpr std::array<int, 16> RuntimeLandTypeByTmpTerrainType{{
    0, 8, 8, 8, 8, 10, 9, 3, 3, 2, 6, 1, 1, 0, 7, 3
}};
std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}


std::string normalizedLowerPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return toLowerAscii(std::move(path));
}


bool runtimePathUsesDefaultAssetRoot(
    const std::string& resourcePath,
    const std::string& assetRootPath
) {
    if (resourcePath.empty() || assetRootPath.empty()) {
        return false;
    }

    const auto normalizedAbsolute = [](const std::string& path) {
        std::error_code errorCode;
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(path, errorCode);
        return normalizedLowerPath((errorCode
            ? std::filesystem::path(path)
            : absolutePath).lexically_normal().generic_string());
    };
    std::string root = normalizedAbsolute(assetRootPath);
    while (!root.empty() && root.back() == '/') {
        root.pop_back();
    }
    const std::string path = normalizedAbsolute(resourcePath);
    return path == root
        || (path.size() > root.size()
            && path.compare(0, root.size(), root) == 0
            && path[root.size()] == '/');
}


int runtimeLandTypeForTmpImage(const RuntimeTmpSubTileImage* terrainImage) {
    if (terrainImage == nullptr
        || !terrainImage->hasTerrainType
        || terrainImage->terrainType
            >= RuntimeLandTypeByTmpTerrainType.size()) {
        return RuntimeLandTypeUnknown;
    }
    return RuntimeLandTypeByTmpTerrainType[terrainImage->terrainType];
}


RuntimeTmpTileFile* loadRuntimeTmpTileFromCache(
    const std::string& path,
    RuntimeMapRenderCache& cache
) {
    auto [it, inserted] = cache.tmpTilesByPath.try_emplace(path);
    RuntimeTmpTileFile& tileFile = it->second;
    if (!tileFile.loadAttempted) {
        tileFile.loadAttempted = true;
        std::string errorMessage;
        tileFile.loaded = loadRuntimeTmpTileFile(path, tileFile, errorMessage);
        tileFile.errorMessage = errorMessage;
    }

    return tileFile.loaded ? &tileFile : nullptr;
}


RuntimeTmpTileFile* loadRuntimeTmpTileByNameFromCache(
    const std::vector<std::string>& rootPaths,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache
) {
    const std::string cacheKey = "resource:"
        + normalizedLowerPath(assetRootPath)
        + "|"
        + toLowerAscii(fileName);
    auto found = cache.tmpTilesByPath.find(cacheKey);
    if (found != cache.tmpTilesByPath.end()
        && found->second.loadAttempted) {
        cache.lastTmpTileLoadError = found->second.errorMessage;
        return found->second.loaded ? &found->second : nullptr;
    }

    std::vector<unsigned char> bytes;
    std::string sourceName;
    std::string errorMessage;
    const bool bytesLoaded = loadRuntimeResourceBytesByName(
        rootPaths,
        assetRootPath,
        fileName,
        cache,
        bytes,
        sourceName,
        errorMessage
    );

    auto [it, inserted] = cache.tmpTilesByPath.try_emplace(cacheKey);
    RuntimeTmpTileFile& tileFile = it->second;
    if (!tileFile.loadAttempted) {
        tileFile.loadAttempted = true;
        if (bytesLoaded) {
            tileFile.loaded = loadRuntimeTmpTileBytes(
                bytes,
                sourceName.empty() ? fileName : sourceName,
                tileFile,
                errorMessage
            );
        } else {
            tileFile.loaded = false;
        }
        tileFile.errorMessage = errorMessage;
    }
    cache.lastTmpTileLoadError = tileFile.errorMessage;

    return tileFile.loaded ? &tileFile : nullptr;
}


std::string runtimeTerrainTileLookupCacheRoot(
    const RuntimeMapRenderContext& context
) {
    return normalizedLowerPath(context.tileRootPath)
        + "|asset="
        + normalizedLowerPath(context.assetRootPath)
        + "|theater="
        + (
            context.theater == nullptr
                ? std::string{}
                : toLowerAscii(context.theater->getTileExtension())
        );
}


bool runtimeTerrainTileLocatorIsMixResource(const std::string& locator) {
    return locator.rfind(runtimeTerrainMixResourcePrefix, 0) == 0;
}


std::string runtimeTerrainTileResourceName(const std::string& locator) {
    return runtimeTerrainTileLocatorIsMixResource(locator)
        ? locator.substr(std::char_traits<char>::length(
            runtimeTerrainMixResourcePrefix
        ))
        : std::string{};
}


RuntimeTmpTileFile* loadRuntimeTerrainTileForIndexUnmemoized(
    int tileIndex,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
) {
    if (context.theater == nullptr
        || (context.tileRootPath.empty() && context.assetRootPath.empty())) {
        return nullptr;
    }

    const std::string lookupRoot =
        frozenTmpQuery::runtimeTerrainTileLookupCacheRoot(context);
    if (cache.terrainTilePathCacheRoot != lookupRoot) {
        cache.terrainTilePathCacheRoot = lookupRoot;
        cache.terrainTilePathByIndex.clear();
        cache.missingTerrainTilePathByIndex.clear();
    }

    std::string assetRootPath = context.assetRootPath;
    if (assetRootPath.empty()) {
        assetRootPath = cachedRuntimeMixExportsRootForContext(context, cache);
    }
    const std::vector<std::string> rootPaths = {
        context.tileRootPath,
        context.assetRootPath,
        context.structureRootPath
    };
    const auto loadLocator = [&](const std::string& locator) {
        if (runtimeTerrainTileLocatorIsMixResource(locator)) {
            return loadRuntimeTmpTileByNameFromCache(
                rootPaths,
                assetRootPath,
                runtimeTerrainTileResourceName(locator),
                cache
            );
        }
        return loadRuntimeTmpTileFromCache(locator, cache);
    };

    const auto cachedPath = cache.terrainTilePathByIndex.find(tileIndex);
    if (cachedPath != cache.terrainTilePathByIndex.end()) {
        return loadLocator(cachedPath->second);
    }
    if (cache.missingTerrainTilePathByIndex.find(tileIndex)
        != cache.missingTerrainTilePathByIndex.end()) {
        return nullptr;
    }

    // Preserve an explicitly configured tile outside the default export root.
    // For normal exports, original theater MIX payloads take precedence over
    // loose copies; loose-only custom installations remain usable.
    const TheaterTileReference reference =
        context.theater->resolveTileIndex(tileIndex, context.tileRootPath);
    if (!reference.validTileSet) {
        cache.missingTerrainTilePathByIndex.insert(tileIndex);
        return nullptr;
    }

    if (reference.fileExists && !reference.resolvedPath.empty()
        && !runtimePathUsesDefaultAssetRoot(reference.resolvedPath, assetRootPath)) {
        if (RuntimeTmpTileFile* tileFile =
                loadRuntimeTmpTileFromCache(reference.resolvedPath, cache)) {
            cache.terrainTilePathByIndex.emplace(
                tileIndex,
                reference.resolvedPath
            );
            return tileFile;
        }
    }

    for (const std::string& fileName : reference.candidateFileNames) {
        RuntimeTmpTileFile* tileFile =
            loadRuntimeTmpTileByNameFromCache(
                rootPaths,
                assetRootPath,
                fileName,
                cache
            );
        if (tileFile == nullptr) {
            continue;
        }

        cache.terrainTilePathByIndex.emplace(
            tileIndex,
            std::string(runtimeTerrainMixResourcePrefix) + fileName
        );
        return tileFile;
    }

    if (reference.fileExists && !reference.resolvedPath.empty()
        && !runtimeAssetRootHasMixArchives(assetRootPath, cache)) {
        if (RuntimeTmpTileFile* tileFile =
                loadRuntimeTmpTileFromCache(reference.resolvedPath, cache)) {
            cache.terrainTilePathByIndex.emplace(
                tileIndex,
                reference.resolvedPath
            );
            return tileFile;
        }
    }

    cache.missingTerrainTilePathByIndex.insert(tileIndex);
    return nullptr;
}


RuntimeTmpTileFile* loadRuntimeTerrainTileForIndex(
    int tileIndex, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    RuntimeTmpTileFile* file = nullptr;
    bool updatesLastLoadError = false;
    if (findRuntimeGeometryTerrainTile(tileIndex, context, cache, file, updatesLastLoadError)) {
        // MIX cache hits update this diagnostic; loose-file and resolved-miss
        // hits preserve the previous error exactly like the original paths.
        if (updatesLastLoadError && file) cache.lastTmpTileLoadError = file->errorMessage;
        return file;
    }
    file = loadRuntimeTerrainTileForIndexUnmemoized(tileIndex, context, cache);
    const auto locator = cache.terrainTilePathByIndex.find(tileIndex);
    updatesLastLoadError = file && locator != cache.terrainTilePathByIndex.end()
        && runtimeTerrainTileLocatorIsMixResource(locator->second);
    rememberRuntimeGeometryTerrainTile(tileIndex, context, cache, file, updatesLastLoadError);
    return file;
}


const RuntimeTmpSubTileImage* resolveRuntimeTerrainMetadata(
    int tileIndex,
    int subTileIndex,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
) {
    if (context.theater == nullptr
        || (context.tileRootPath.empty() && context.assetRootPath.empty())
        || tileIndex < 0
        || tileIndex == 65535
        || subTileIndex < 0) {
        return nullptr;
    }

    RuntimeTmpTileFile* tileFile = loadRuntimeTerrainTileForIndex(
        tileIndex,
        context,
        cache
    );
    if (tileFile == nullptr
        || static_cast<std::size_t>(subTileIndex) >= tileFile->images.size()) {
        return nullptr;
    }
    const RuntimeTmpSubTileImage& image =
        tileFile->images[static_cast<std::size_t>(subTileIndex)];
    return image.present && image.hasTerrainType ? &image : nullptr;
}


bool resolveRuntimeTerrainTileGridShape(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& gridWidth,
    int& gridHeight
) {
    gridWidth = 0;
    gridHeight = 0;
    if (cell.tileIndex < 0 || cell.tileIndex == 65535 || cell.subTileIndex < 0) {
        return false;
    }
    const RuntimeTmpTileFile* tileFile = loadRuntimeTerrainTileForIndex(
        cell.tileIndex, context, cache);
    if (tileFile == nullptr || tileFile->gridWidth <= 0 || tileFile->gridHeight <= 0
        || static_cast<std::size_t>(cell.subTileIndex) >= tileFile->images.size()
        || static_cast<std::size_t>(tileFile->gridWidth) * tileFile->gridHeight
            != tileFile->images.size()) {
        return false;
    }
    gridWidth = tileFile->gridWidth;
    gridHeight = tileFile->gridHeight;
    return true;
}


unsigned char resolveRuntimeTerrainSubTileRampType(
    const TerrainCell& cell, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) {
    const auto* image = resolveRuntimeTerrainMetadata(cell.tileIndex, cell.subTileIndex, context, cache);
    return image && image->present ? image->rampType : 0;
}


RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
) {
    const RuntimeTmpSubTileImage* image = resolveRuntimeTerrainMetadata(
        cell.tileIndex,
        cell.subTileIndex,
        context,
        cache
    );
    const int landType = frozenTmpQuery::runtimeLandTypeForTmpImage(image);
    return landType == RuntimeLandTypeUnknown
        ? RuntimeTerrainLandType::Unknown
        : static_cast<RuntimeTerrainLandType>(landType);
}

} // namespace frozenTmpQuery
