#include "assets/terrain/RuntimeTerrainTmpQuery.h"
#include "assets/codecs/RuntimeTmpCodec.h"
#include "data/art/TheaterDatabase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <utility>

namespace {
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

constexpr int RuntimeLandTypeUnknown = -1;

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
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
) {
    auto [it, inserted] = cache.tmpTilesByPath.try_emplace(path);
    RuntimeTmpTileFile& tileFile = it->second;
    if (!tileFile.loadAttempted) {
        tileFile.loadAttempted = true;
        std::string errorMessage;
        std::vector<unsigned char> bytes;
        tileFile.loaded = source.readLooseBytes(path, bytes, errorMessage)
            && loadRuntimeTmpTileBytes(bytes, path, tileFile, errorMessage);
        tileFile.errorMessage = errorMessage;
    }

    return tileFile.loaded ? &tileFile : nullptr;
}

RuntimeTmpTileFile* loadRuntimeTmpTileByNameFromCache(
    const std::vector<std::string>& rootPaths,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
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
    const bool bytesLoaded = source.readResourceBytesByName(
        rootPaths,
        assetRootPath,
        fileName,
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
    const RuntimeTerrainTmpContext& context
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
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
) {
    if (context.theater == nullptr
        || (context.tileRootPath.empty() && context.assetRootPath.empty())) {
        return nullptr;
    }

    const std::string lookupRoot =
        runtimeTerrainTileLookupCacheRoot(context);
    if (cache.terrainTilePathCacheRoot != lookupRoot) {
        cache.terrainTilePathCacheRoot = lookupRoot;
        cache.terrainTilePathByIndex.clear();
        cache.missingTerrainTilePathByIndex.clear();
    }

    std::string assetRootPath = context.assetRootPath;
    if (assetRootPath.empty()) {
        assetRootPath = source.cachedMixExportsRoot();
    }
    const std::vector<std::string> rootPaths = {
        context.tileRootPath,
        context.assetRootPath,
        context.structureRootPath
    };
    const auto loadLocator = [&](const std::string& locator) {
        if (runtimeTerrainTileLocatorIsMixResource(locator)) {
            return loadRuntimeTmpTileByNameFromCache(
                rootPaths, assetRootPath, runtimeTerrainTileResourceName(locator), cache, source
            );
        }
        return loadRuntimeTmpTileFromCache(locator, cache, source);
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
                loadRuntimeTmpTileFromCache(reference.resolvedPath, cache, source)) {
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
                rootPaths, assetRootPath, fileName, cache, source
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
        && !source.assetRootHasMixArchives(assetRootPath)) {
        if (RuntimeTmpTileFile* tileFile =
                loadRuntimeTmpTileFromCache(reference.resolvedPath, cache, source)) {
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
    int tileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
) {
    RuntimeTmpTileFile* file = nullptr;
    bool updatesLastLoadError = false;
    if (memo.find(tileIndex, file, updatesLastLoadError)) {
        // MIX cache hits update this diagnostic; loose-file and resolved-miss
        // hits preserve the previous error exactly like the original paths.
        if (updatesLastLoadError && file) cache.lastTmpTileLoadError = file->errorMessage;
        return file;
    }
    file = loadRuntimeTerrainTileForIndexUnmemoized(tileIndex, context, cache, source);
    const auto locator = cache.terrainTilePathByIndex.find(tileIndex);
    updatesLastLoadError = file && locator != cache.terrainTilePathByIndex.end()
        && runtimeTerrainTileLocatorIsMixResource(locator->second);
    memo.remember(tileIndex, file, updatesLastLoadError);
    return file;
}

const RuntimeTmpSubTileImage* resolveRuntimeTerrainMetadata(
    int tileIndex,
    int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
) {
    if (context.theater == nullptr
        || (context.tileRootPath.empty() && context.assetRootPath.empty())
        || tileIndex < 0
        || tileIndex == 65535
        || subTileIndex < 0) {
        return nullptr;
    }

    RuntimeTmpTileFile* tileFile = loadRuntimeTerrainTileForIndex(
        tileIndex, context, cache, source, memo
    );
    if (tileFile == nullptr
        || static_cast<std::size_t>(subTileIndex) >= tileFile->images.size()) {
        return nullptr;
    }
    const RuntimeTmpSubTileImage& image =
        tileFile->images[static_cast<std::size_t>(subTileIndex)];
    return image.present && image.hasTerrainType ? &image : nullptr;
}

bool resolveRuntimeTerrainTmpTileGridShape(
    int tileIndex, int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo,
    int& gridWidth,
    int& gridHeight
) {
    gridWidth = 0;
    gridHeight = 0;
    if (tileIndex < 0 || tileIndex == 65535 || subTileIndex < 0) {
        return false;
    }
    const RuntimeTmpTileFile* tileFile = loadRuntimeTerrainTileForIndex(
        tileIndex, context, cache, source, memo
    );
    if (tileFile == nullptr || tileFile->gridWidth <= 0 || tileFile->gridHeight <= 0
        || static_cast<std::size_t>(subTileIndex) >= tileFile->images.size()
        || static_cast<std::size_t>(tileFile->gridWidth) * tileFile->gridHeight
            != tileFile->images.size()) {
        return false;
    }
    gridWidth = tileFile->gridWidth;
    gridHeight = tileFile->gridHeight;
    return true;
}

unsigned char resolveRuntimeTerrainSubTileRampType(
    int tileIndex, int subTileIndex, const RuntimeTerrainTmpContext& context, RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo) {
    const auto* image = resolveRuntimeTerrainMetadata(
        tileIndex, subTileIndex, context, cache, source, memo
    );
    return image && image->present ? image->rampType : 0;
}

RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(
    int tileIndex, int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
) {
    const RuntimeTmpSubTileImage* image = resolveRuntimeTerrainMetadata(
        tileIndex, subTileIndex, context, cache, source, memo
    );
    const int landType = runtimeLandTypeForTmpImage(image);
    return landType == RuntimeLandTypeUnknown
        ? RuntimeTerrainLandType::Unknown
        : static_cast<RuntimeTerrainLandType>(landType);
}

