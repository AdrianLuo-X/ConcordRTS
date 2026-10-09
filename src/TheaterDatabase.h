#pragma once

#include "IniFile.h"
#include "MapDatabase.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct TheaterTileSet {
    int index = -1;
    std::string setName;
    std::string fileName;
    int tilesInSet = 0;
    int startTileIndex = 0;
    bool morphable = false;
};

struct TheaterTileReference {
    int tileIndex = -1;
    int tileSetIndex = -1;
    int tileIndexInSet = -1;
    int useCount = 0;
    std::string tileSetName;
    std::string baseFileName;
    std::string expectedFileName;
    std::string resolvedPath;
    std::vector<std::string> candidateFileNames;
    bool morphable = false;
    bool validTileSet = false;
    bool fileExists = false;
};

struct MapTileUsageReport {
    std::string theaterExtension;
    std::size_t tileSetCount = 0;
    std::size_t totalTileSlots = 0;
    std::size_t mapTileCount = 0;
    std::size_t emptyTileCount = 0;
    std::size_t uniqueTileCount = 0;
    std::size_t unresolvedTileCount = 0;
    std::size_t missingFileCount = 0;
    std::vector<TheaterTileReference> usedTiles;
};

class TheaterDatabase {
public:
    bool loadFromIni(const IniFile& ini, const std::string& theaterIniPath);

    const std::vector<TheaterTileSet>& getTileSets() const;
    const std::string& getTileExtension() const;
    int getGeneralTileSetIndex(const std::string& key) const;
    std::vector<std::string> getResourceFileNameCandidates(
        const std::string& baseName,
        bool includeShp = true
    ) const;
    std::size_t getTotalTileSlots() const;
    TheaterTileReference resolveTileIndex(
        int tileIndex,
        const std::string& tileRootPath = ""
    ) const;
    MapTileUsageReport buildMapTileUsageReport(
        const MapDatabase& mapDatabase,
        const std::string& tileRootPath = ""
    ) const;
    const std::vector<std::string>& getWarnings() const;

private:
    std::vector<TheaterTileSet> tileSets_;
    std::string tileExtension_;
    std::unordered_map<std::string, int> generalTileSetIndices_;
    std::size_t totalTileSlots_ = 0;
    std::vector<std::string> warnings_;
};
