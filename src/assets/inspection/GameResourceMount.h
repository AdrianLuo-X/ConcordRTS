#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class ResourceMountKind {
    OriginalGame,
    LocalData,
    Mod
};

struct ResourceMountRequest {
    ResourceMountKind kind = ResourceMountKind::OriginalGame;
    std::string name;
    std::string rootPath;
    bool recursive = true;
};

struct ResourceExtensionCount {
    std::string extension;
    int count = 0;
};

struct ResourceMountLayerReport {
    ResourceMountKind kind = ResourceMountKind::OriginalGame;
    std::string name;
    std::string rootPath;
    bool exists = false;
    bool indexed = false;
    int fileCount = 0;
    int directoryCount = 0;
    std::uintmax_t totalBytes = 0;
    int mixCount = 0;
    int iniCount = 0;
    int mapCount = 0;
    int palCount = 0;
    int shpCount = 0;
    int tmpCount = 0;
    int vxlCount = 0;
    int hvaCount = 0;
    bool hasRules = false;
    bool hasArt = false;
    bool hasMaps = false;
    bool hasTheaterIni = false;
    bool hasPalette = false;
    bool hasMix = false;
    std::vector<ResourceExtensionCount> topExtensions;
    std::vector<std::string> sampleFiles;
    std::vector<std::string> warnings;
};

struct ResourceMountReport {
    bool success = false;
    int mountedLayerCount = 0;
    int existingLayerCount = 0;
    int totalFiles = 0;
    int totalDirectories = 0;
    std::uintmax_t totalBytes = 0;
    int totalMixFiles = 0;
    int totalIniFiles = 0;
    int totalMapFiles = 0;
    int totalPalFiles = 0;
    int totalShpFiles = 0;
    int totalTmpFiles = 0;
    int totalVxlFiles = 0;
    int totalHvaFiles = 0;
    bool hasRules = false;
    bool hasArt = false;
    bool hasMaps = false;
    bool hasTheaterIni = false;
    bool hasPalette = false;
    bool hasMix = false;
    std::vector<ResourceMountLayerReport> layers;
    std::vector<std::string> warnings;
};

std::string toString(ResourceMountKind kind);

ResourceMountReport buildResourceMountReport(
    const std::vector<ResourceMountRequest>& requests
);
