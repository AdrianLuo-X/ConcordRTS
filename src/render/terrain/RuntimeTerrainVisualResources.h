#pragma once

#include "assets/terrain/RuntimeTerrainTmpQuery.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "render/terrain/RuntimeTerrainVisualSelection.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace concord::render {

// Borrows the live cache and archive registry. Named reads may populate the
// registry before the returned source name is classified.
struct RuntimeTerrainVisualSources {
    const RuntimeTerrainTmpContext& context;
    RuntimeTerrainTmpCache& tiles;
    RuntimeTerrainTmpByteSource& bytes;
    const std::vector<std::string>& mixArchives;
};

RuntimeTmpTileFile* runtimeDiscoverTerrainVisualNamed(
    const std::string& fileName, RuntimeTerrainVisualSources resources,
    std::string& locator, std::string& sourceName);
const RuntimeTmpTileFile* runtimeFindTerrainVisualCachedFile(
    const std::string& locator, const RuntimeTerrainTmpCache& cache);
bool runtimeTerrainVisualSourceIsArchive(const std::vector<std::string>& archives,
    const std::string& fileName, const std::string& sourceName);

// Uses the renderer's existing family record without borrowing its frame,
// cache owner or resource adapter implementation.
template<class Family>
Family& runtimeTerrainVisualFamilyForCell(
    int tileIndex, const RuntimeTmpTileFile& canonical,
    const std::string& familyRoot, RuntimeTerrainVisualSources resources,
    std::string& cachedRoot, std::unordered_map<int, Family>& families
) {
    if (cachedRoot != familyRoot) {
        cachedRoot = familyRoot;
        families.clear();
    }
    auto [found, inserted] = families.try_emplace(tileIndex);
    auto& family = found->second;
    if (!inserted) return family;
    const auto source = resources.tiles.terrainTilePathByIndex.find(tileIndex);
    if (source == resources.tiles.terrainTilePathByIndex.end()) return family;
    family.canonicalLocator = source->second;
    const bool namedBase = runtimeTerrainTileLocatorIsMixResource(source->second);
    const std::string originalName = namedBase
        ? runtimeTerrainTileResourceName(source->second)
        : std::filesystem::path(source->second).filename().string();
    if (namedBase) {
        // @mix-resource is a named-loader locator, not proof of a MIX payload.
        // Resolve its real source once, using exactly the original precedence.
        std::string discoveredLocator;
        if (!runtimeDiscoverTerrainVisualNamed(originalName, resources,
                discoveredLocator, family.canonicalSourceName)) {
            family.canonicalSourceName.clear(); return family;
        }
        const bool actualMix = runtimeTerrainVisualSourceIsArchive(resources.mixArchives, originalName, family.canonicalSourceName);
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
                siblings.try_emplace(runtime_save_text::toLowerAsciiLocal(entry.path().filename().string()),
                    entry.path().generic_string());
            }
            if (!error) iterator.increment(error);
        }
    }
    const std::string canonicalError = resources.tiles.lastTmpTileLoadError;
    for (char suffix = 'a'; suffix <= 'z'; ++suffix) {
        const std::string name = stem + suffix + extension;
        typename decltype(family.variants)::value_type choice;
        choice.resourceName = name;
        RuntimeTmpTileFile* variant = nullptr;
        if (family.siblingOnly) {
            const auto sibling = siblings.find(runtime_save_text::toLowerAsciiLocal(name));
            if (sibling == siblings.end()) continue;
            choice.locator = sibling->second;
            choice.sourceName = sibling->second;
            variant = loadRuntimeTmpTileFromCache(choice.locator, resources.tiles, resources.bytes);
        } else {
            variant = runtimeDiscoverTerrainVisualNamed(name, resources,
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
    resources.tiles.lastTmpTileLoadError = canonicalError;
    return family;
}

} // namespace concord::render
