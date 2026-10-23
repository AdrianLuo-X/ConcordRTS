#pragma once

#include <iosfwd>
#include <string>

enum class CommandMode {
    None,
    Summary,
    Validate,
    DumpUnits,
    DumpWeapons,
    CombatSandbox,
    EconomySandbox,
    AiTriggerSandbox,
    ModCompatReport,
    EngineStatus,
    TechTreeReport,
    VictorySandbox,
    PathfindingSandbox,
    AudioReport,
    AudioDecodeSmoke,
    PerformancePlan,
    ReplaySandbox,
    DumpArt,
    MapSummary,
    DumpMapObjects,
    ExportMapPreview,
    ExportMapHtml,
    ExportMapTmpPreview,
    ExportMapTmpHtml,
    MapTileReport,
    MapObjectArtReport,
    ListMix,
    ExtractMixFile,
    ExtractMapTiles,
    ExtractMapObjectArt,
    InspectAsset,
    InspectVoxelPair,
    ExportFrame,
    ExportTmpTile,
    ExportJson
};

struct CommandLineOptions {
    std::string programName;
    std::string rulesPath;
    std::string artPath;
    std::string mapPath;
    std::string theaterPath;
    std::string theaterDirPath;
    std::string tileRootPath;
    std::string mixPath;
    std::string mixRootPath;
    std::string mixFileName;
    std::string assetPath;
    std::string assetRootPath;
    std::string hvaPath;
    std::string palettePath;
    std::string paletteDirPath;
    std::string exportFramePath;
    std::string exportTmpTilePath;
    std::string exportMapPreviewPath;
    std::string exportMapHtmlPath;
    std::string exportMapTmpPreviewPath;
    std::string exportMapTmpHtmlPath;
    std::string extractMapTilesPath;
    std::string extractMapObjectArtPath;
    std::string exportJsonPath;
    std::string combatAttackerId = "MTNK";
    std::string combatDefenderId = "HTNK";
    std::string combatTicks = "600";
    std::string combatDistance = "8";
    std::string economyHouseId = "Americans";
    std::string economyMcvId = "AMCV";
    std::string economyInfantryId;
    std::string economyVehicleId;
    std::string economyCredits = "10000";
    std::string aiHouseId = "Russians";
    std::string enemyHouseId = "Americans";
    std::string attackWaveSize = "3";
    std::string workerCount = "0";
    std::string randomSeed = "12345";
    std::string desyncTick;
    std::string runtimeReplayEventPath;
    std::string outputPath;
    CommandMode mode = CommandMode::None;
    bool showHelp = false;
    bool overwriteOutput = false;
    std::string errorMessage;
    std::string reportJsonPath;
};

CommandLineOptions parseCommandLine(int argc, char** argv);
void printHelp(std::ostream& output, const std::string& programName);
