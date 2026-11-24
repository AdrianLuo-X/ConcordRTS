#include "render/terrain/RuntimeTerrainVisualResources.h"

#include "assets/codecs/RuntimeTmpCodec.h"

#include <algorithm>
#include <system_error>

namespace concord::render {

bool runtimeTerrainVisualSourceIsArchive(const std::vector<std::string>& archives,
    const std::string& fileName, const std::string& sourceName) {
    return std::any_of(archives.begin(), archives.end(),
        [&](const std::string& archive) { return sourceName == archive + "!" + fileName; });
}

RuntimeTmpTileFile* runtimeDiscoverTerrainVisualNamed(
    const std::string& fileName, RuntimeTerrainVisualSources resources,
    std::string& locator, std::string& sourceName
) {
    locator.clear(); sourceName.clear();
    const std::string assetRoot = resources.context.assetRootPath.empty()
        ? resources.bytes.cachedMixExportsRoot() : resources.context.assetRootPath;
    const std::vector<std::string> roots{
        resources.context.tileRootPath, resources.context.assetRootPath, resources.context.structureRootPath
    };
    std::vector<unsigned char> bytes;
    std::string error;
    if (!resources.bytes.readResourceBytesByName(roots, assetRoot, fileName,
            bytes, sourceName, error)) return nullptr;
    const bool actualMix = runtimeTerrainVisualSourceIsArchive(resources.mixArchives, fileName, sourceName);
    std::error_code fileError;
    if (!actualMix && !std::filesystem::is_regular_file(sourceName, fileError)) {
        sourceName.clear(); return nullptr;
    }
    // The real source is part of this presentation-only key. This never aliases
    // the canonical named cache or adds a variant to its geometry memo.
    locator = "visual-source:" + sourceName;
    auto [entry, inserted] = resources.tiles.tmpTilesByPath.try_emplace(locator);
    auto& file = entry->second;
    if (!file.loadAttempted) {
        file.loadAttempted = true;
        file.loaded = loadRuntimeTmpTileBytes(bytes, sourceName, file, error);
        file.errorMessage = error;
    }
    return file.loaded ? &file : nullptr;
}

const RuntimeTmpTileFile* runtimeFindTerrainVisualCachedFile(
    const std::string& locator, const RuntimeTerrainTmpCache& cache
) {
    const auto entry = cache.tmpTilesByPath.find(locator);
    return entry != cache.tmpTilesByPath.end() && entry->second.loaded ? &entry->second : nullptr;
}

} // namespace concord::render
