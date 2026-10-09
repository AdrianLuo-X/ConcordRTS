#pragma once

#include "MapDatabase.h"
#include "TheaterDatabase.h"

#include <string>
#include <vector>

class ArtDatabase;
class RulesDatabase;

struct MapStructureSpriteOptions {
    const RulesDatabase* rules = nullptr;
    const ArtDatabase* art = nullptr;
    std::string assetRootPath;
    std::string structurePalettePath;
};

struct MapStructureSpriteStats {
    int uniqueStructureTypes = 0;
    int embeddedSprites = 0;
    int missingSprites = 0;
    int decodeFailures = 0;
    std::vector<std::string> warnings;
};

bool exportMapPreviewToPpm(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    std::string& errorMessage
);

bool exportMapPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    std::string& errorMessage
);

bool exportMapTmpPreviewToPpm(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    std::string& errorMessage
);

bool exportMapTmpPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    std::string& errorMessage
);

bool exportMapTmpPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    const MapStructureSpriteOptions& structureSpriteOptions,
    MapStructureSpriteStats& structureSpriteStats,
    std::string& errorMessage
);
