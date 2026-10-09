#pragma once

#include "MapDatabase.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct RuntimeConfig;

// The six choices exposed by the retail random-map dialog (resource 261).
// The seed is retained with the map so a LAN peer can recreate the same file.
struct RuntimeRandomMapOptions {
    int environment = 3; // archipelago, continent, team continents, inland, mountains
    int theater = 0;     // temperate, snow
    int size = 1;        // small, medium, large, very large
    int resources = 1;   // low, moderate, high, extreme
    int timeOfDay = 1;   // morning, afternoon, dusk, night
    int players = 4;
    std::uint32_t seed = 1;
    std::string name;
};

struct RuntimeRandomMapResult {
    RuntimeRandomMapOptions options;
    std::string fileName;
    std::string iniText;
    MapPreviewImage preview;
    std::vector<MapPosition> starts;
    int width = 0;
    int height = 0;
    int landCells = 0;
    int waterCells = 0;
    int shoreCells = 0;
    int raisedCells = 0;
    int oreCells = 0;
    int trees = 0;
    int buildings = 0;
};

bool runtimeValidateRandomMapOptions(const RuntimeRandomMapOptions&, std::string& error);
int runtimeRandomMapDimension(const RuntimeRandomMapOptions&);
std::string runtimeRandomMapFileName(const RuntimeRandomMapOptions&);
bool runtimeRandomMapOptionsFromFileName(const std::string&, RuntimeRandomMapOptions&);
std::filesystem::path runtimeSavedRandomMapDirectory(const RuntimeConfig&);
std::filesystem::path runtimeRandomMapCacheDirectory(const RuntimeConfig&);
bool runtimeGenerateRandomMap(const RuntimeConfig&, const RuntimeRandomMapOptions&,
    RuntimeRandomMapResult&, std::string& error);
bool runtimeStoreRandomMap(const RuntimeRandomMapResult&, const std::filesystem::path& directory,
    std::filesystem::path& mapPath, std::string& error);
bool runtimeLoadRandomMap(const std::filesystem::path&, RuntimeRandomMapResult&, std::string& error);
bool runtimeRenameRandomMap(RuntimeRandomMapResult&, const std::string& name, std::string& error);
