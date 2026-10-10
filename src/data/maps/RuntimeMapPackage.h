#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct RuntimeMapPackageMap {
    std::filesystem::path sourcePackagePath;
    std::filesystem::path mapPath;
    std::string id;
    std::string descriptionKey;
    int minPlayers = 2;
    int maxPlayers = 8;
    std::vector<std::string> filters;
};

struct RuntimeMapPackageScanResult {
    std::size_t packageFileCount = 0;
    std::size_t readablePackageCount = 0;
    std::size_t duplicateMapCount = 0;
    std::vector<RuntimeMapPackageMap> maps;
    std::vector<std::string> warnings;
};

bool isRuntimeMapPackageExtension(const std::filesystem::path& path);
// Returns an advertised package start limit only when the selected local map
// is byte-identical to that package entry. Does not create or change caches.
int resolveRuntimeMapPackageStartingPointLimit(
    const std::vector<std::filesystem::path>& directories,
    const std::filesystem::path& actualMapPath,
    const std::string& canonicalMapId
);


// Scans the supplied directories in priority order. Package maps are
// materialized under cacheDirectory because the rest of the runtime consumes
// ordinary map INI paths. The first package advertising a map id wins.
RuntimeMapPackageScanResult scanRuntimeMapPackages(
    const std::vector<std::filesystem::path>& directories,
    const std::filesystem::path& cacheDirectory
);
