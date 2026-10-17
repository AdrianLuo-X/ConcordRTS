#include "data/art/ArtDatabase.h"
#include "assets/inspection/AssetInspector.h"
#include "assets/audio/AudioCatalog.h"
#include "assets/audio/AudioSampleResolver.h"
#include "analysis/sandbox/AiTriggerSandbox.h"
#include "data/campaign/CampaignDatabase.h"
#include "analysis/sandbox/CombatSandbox.h"
#include "analysis/cli/CommandLineOptions.h"
#include "analysis/sandbox/EconomySandbox.h"
#include "analysis/reports/EngineStatus.h"
#include "world/model/GameWorld.h"
#include "core/io/IniFile.h"
#include "analysis/reports/JsonExporter.h"
#include "data/maps/MapDatabase.h"
#include "assets/preview/MapPreviewExporter.h"
#include "assets/preview/MapPreviewResourceContext.h"
#include "assets/archive/MixArchive.h"
#include "analysis/reports/ModCompatibilityScanner.h"
#include "analysis/reports/ObjectReferenceValidator.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "analysis/reports/PerformancePlan.h"
#include "analysis/sandbox/ReplaySandbox.h"
#include "data/rules/RulesDatabase.h"
#include "data/rules/TechTreeAnalyzer.h"
#include "data/art/TheaterDatabase.h"
#include "analysis/sandbox/VictoryCondition.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#pragma comment(lib, "Dbghelp.lib")
#endif
#endif

static bool commandLineTraceEnabled() {
    const char* rawValue = std::getenv("CONCORD_RULES_TRACE");
    if (rawValue == nullptr || rawValue[0] == '\0') {
        return false;
    }

    const std::string value(rawValue);
    return value != "0"
        && value != "false"
        && value != "False"
        && value != "no"
        && value != "off";
}

#ifdef _WIN32
static LONG WINAPI commandLineUnhandledExceptionFilter(
    EXCEPTION_POINTERS* exceptionPointers
) {
    if (exceptionPointers != nullptr
        && exceptionPointers->ExceptionRecord != nullptr) {
        std::cerr << "Fatal unhandled exception: code=0x"
                  << std::hex
                  << exceptionPointers->ExceptionRecord->ExceptionCode
                  << " address=0x"
                  << reinterpret_cast<std::uintptr_t>(
                        exceptionPointers->ExceptionRecord->ExceptionAddress
                     )
                  << std::dec
                  << "\n";
    } else {
        std::cerr << "Fatal unhandled exception: unknown\n";
    }
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (SymInitialize(process, nullptr, TRUE)) {
        void* stack[32]{};
        const USHORT frames = CaptureStackBackTrace(
            0,
            static_cast<DWORD>(std::size(stack)),
            stack,
            nullptr
        );
        std::cerr << "Fatal stack:\n";
        alignas(SYMBOL_INFO) unsigned char symbolBuffer[
            sizeof(SYMBOL_INFO) + MAX_SYM_NAME
        ]{};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        for (USHORT index = 0; index < frames; ++index) {
            const DWORD64 address =
                reinterpret_cast<DWORD64>(stack[index]);
            DWORD64 displacement = 0;
            std::cerr << "  #" << index << " 0x"
                      << std::hex << address << std::dec;
            if (SymFromAddr(process, address, &displacement, symbol)) {
                std::cerr << " " << symbol->Name
                          << "+0x" << std::hex << displacement << std::dec;
            }
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(
                    process,
                    address,
                    &lineDisplacement,
                    &line
                )) {
                std::cerr << " " << line.FileName
                          << ":" << line.LineNumber;
            }
            std::cerr << "\n";
        }
        SymCleanup(process);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void configureCommandLineErrorReporting() {
#ifdef _WIN32
    const UINT previousMode = SetErrorMode(0);
    SetErrorMode(
        previousMode
        | SEM_FAILCRITICALERRORS
        | SEM_NOGPFAULTERRORBOX
        | SEM_NOOPENFILEERRORBOX
    );
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    SetUnhandledExceptionFilter(commandLineUnhandledExceptionFilter);
#endif
}

static void printMixArchive(const MixArchive& archive) {
    std::cout << "Loaded MIX file: " << archive.getPath() << "\n";
    std::cout << "\n========== MIX Archive ==========\n";
    std::cout << "File size:        " << archive.getFileSize() << "\n";
    std::cout << "Flags:            " << formatMixId(archive.getFlags()) << "\n";
    std::cout << "Has checksum:     " << (archive.hasChecksum() ? "yes" : "no") << "\n";
    std::cout << "Encrypted index:  " << (archive.hasEncryptedIndex() ? "yes" : "no") << "\n";

    if (!archive.isIndexReadable()) {
        std::cout << "Index readable:   no\n";
        if (!archive.getErrorMessage().empty()) {
            std::cout << "Reason:           " << archive.getErrorMessage() << "\n";
        }
        return;
    }

    std::cout << "Index readable:   yes\n";
    std::cout << "File count:       " << archive.getFileCount() << "\n";
    std::cout << "Data size:        " << archive.getDataSize() << "\n";
    std::cout << "Data starts at:   " << archive.getDataStartOffset() << "\n";

    std::cout << "\nEntries:\n";
    std::cout << "ID         Offset       Size         Absolute\n";

    for (const MixEntry& entry : archive.getEntries()) {
        std::cout << formatMixId(entry.id)
                  << " " << entry.offset
                  << " " << entry.size
                  << " " << entry.absoluteOffset
                  << "\n";
    }
}

static void printAssetInspection(const AssetInspectionReport& report) {
    if (!report.success) {
        std::cerr << "Failed to inspect asset: " << report.errorMessage << "\n";
        return;
    }

    std::cout << "Loaded asset file: " << report.path << "\n";
    std::cout << "\n========== Asset Inspection ==========\n";
    std::cout << "Kind:      " << report.kind << "\n";
    std::cout << "Extension: " << report.extension << "\n";
    std::cout << "File size: " << report.fileSize << "\n";

    if (!report.fields.empty()) {
        std::cout << "\nFields:\n";
        for (const AssetField& field : report.fields) {
            std::cout << "- " << field.name << ": " << field.value << "\n";
        }
    }

    if (!report.warnings.empty()) {
        std::cout << "\nWarnings:\n";
        for (const std::string& warning : report.warnings) {
            std::cout << "- " << warning << "\n";
        }
    }
}

static void printSummary(const RulesDatabase& rules) {
    std::cout << "\n========== Summary ==========\n";
    std::cout << "Infantry types: " << rules.getInfantryTypes().size() << "\n";
    std::cout << "Vehicle types:  " << rules.getVehicleTypes().size() << "\n";
    std::cout << "Aircraft types: " << rules.getAircraftTypes().size() << "\n";
    std::cout << "Building types: " << rules.getBuildingTypes().size() << "\n";
    std::cout << "Weapon types:   " << rules.getWeaponTypes().size() << "\n";
    std::cout << "Projectile types: " << rules.getProjectileTypes().size() << "\n";
    std::cout << "Warhead types:  " << rules.getWarheadTypes().size() << "\n";
    std::cout << "Warnings:       " << rules.getWarnings().size() << "\n";
}

static std::string formatMaybeInt(int value) {
    if (value < 0) {
        return "-";
    }

    return std::to_string(value);
}

static std::string formatYesNo(bool value) {
    return value ? "yes" : "no";
}

static std::string formatPayloadDecodeStatus(const MapPackedSectionSummary& summary) {
    if (!summary.payloadDecodeAttempted) {
        return "skipped";
    }

    return summary.payloadDecoded ? "yes" : "no";
}

static std::string formatMapCell(const MapPosition& position) {
    if (!position.valid) {
        return "-";
    }

    return std::to_string(position.cell);
}

static std::string formatMapX(const MapPosition& position) {
    if (!position.valid) {
        return "-";
    }

    return std::to_string(position.x);
}

static std::string formatMapY(const MapPosition& position) {
    if (!position.valid) {
        return "-";
    }

    return std::to_string(position.y);
}

static void printPackedSectionSummary(const MapPackedSectionSummary& summary) {
    std::cout << "- " << summary.sectionName
              << ": lines=" << summary.lineCount;

    if (summary.lineCount == 0) {
        std::cout << " missing\n";
        return;
    }

    std::cout << " encodedChars=" << summary.encodedChars
              << " decodedBytes=" << summary.decodedBytes
              << " chunks=" << summary.chunkCount
              << " compressedBytes=" << summary.totalCompressedBytes
              << " uncompressedBytes=" << summary.totalUncompressedBytes
              << " base64=" << formatYesNo(summary.base64Decoded)
              << " validChunks=" << formatYesNo(summary.chunkTableValid)
              << " payloadCodec=" << summary.payloadCodec
              << " payloadDecoded=" << formatPayloadDecodeStatus(summary)
              << "\n";

    if (!summary.chunks.empty()) {
        const MapPackedChunkSummary& first = summary.chunks.front();
        const MapPackedChunkSummary& last = summary.chunks.back();

        std::cout << "  firstChunk=" << first.compressedBytes
                  << "/" << first.uncompressedBytes
                  << " lastChunk=" << last.compressedBytes
                  << "/" << last.uncompressedBytes
                  << "\n";
    }

    if (summary.payloadDecodeAttempted) {
        std::cout << "  payloadChunks=" << summary.payloadDecodedChunks
                  << "/" << summary.chunkCount
                  << " payloadBytes=" << summary.payloadDecodedBytes
                  << " failures=" << summary.payloadDecodeFailureCount
                  << "\n";
    }

    if (summary.payloadRecordSize > 0) {
        std::cout << "  records=" << summary.payloadRecordCount
                  << " recordSize=" << summary.payloadRecordSize
                  << " remainderBytes=" << summary.payloadRemainderBytes
                  << "\n";
    }
}

static void printOverlaySummary(const MapOverlaySummary& summary) {
    std::cout << "\nOverlay:\n";
    std::cout << "- Cells:        " << summary.cellCount << "\n";
    std::cout << "- Bytes/cell:   " << summary.bytesPerCell << "\n";
    std::cout << "- Non-empty:    " << summary.nonEmptyCount << "\n";
    std::cout << "- Ore cells:    " << summary.oreCount << "\n";
    std::cout << "- Gem cells:    " << summary.gemCount << "\n";
    std::cout << "- Unknown:      " << summary.unknownTypeCount << "\n";

    if (summary.typeCounts.empty()) {
        return;
    }

    std::cout << "- Top types:\n";

    std::size_t printed = 0;
    for (const MapOverlayTypeCount& typeCount : summary.typeCounts) {
        if (printed >= 8) {
            break;
        }

        std::cout << "  "
                  << typeCount.overlayIndex
                  << " "
                  << (typeCount.type.empty() ? "-" : typeCount.type)
                  << " count=" << typeCount.count;

        if (typeCount.isGem) {
            std::cout << " kind=gem";
        } else if (typeCount.isOre) {
            std::cout << " kind=ore";
        }

        std::cout << "\n";
        ++printed;
    }
}

static void printMapSummary(const MapDatabase& mapDatabase) {
    const MapSummary& summary = mapDatabase.getSummary();

    std::cout << "\n========== Map Summary ==========\n";
    std::cout << "Sections:       " << summary.sectionCount << "\n";
    std::cout << "Name:           " << (summary.name.empty() ? "-" : summary.name) << "\n";
    std::cout << "Briefing:       " << (summary.briefing.empty() ? "-" : summary.briefing) << "\n";
    std::cout << "Theater:        " << (summary.theater.empty() ? "-" : summary.theater) << "\n";
    std::cout << "Size:           " << (summary.size.empty() ? "-" : summary.size) << "\n";
    std::cout << "LocalSize:      " << (summary.localSize.empty() ? "-" : summary.localSize) << "\n";
    std::cout << "Header bounds:  "
              << formatMaybeInt(summary.startX) << ","
              << formatMaybeInt(summary.startY) << " "
              << formatMaybeInt(summary.width) << "x"
              << formatMaybeInt(summary.height) << "\n";
    std::cout << "Start points:   " << formatMaybeInt(summary.numberStartingPoints) << "\n";
    std::cout << "Coop starts:    " << formatMaybeInt(summary.coopHumanStartSpots) << "\n";
    std::cout << "Multiplayer:    "
              << (summary.multiplayerOnly.empty() ? "-" : summary.multiplayerOnly)
              << "\n";
    std::cout << "Next scenario:  "
              << (summary.nextScenario.empty() ? "-" : summary.nextScenario)
              << "\n";

    std::cout << "\nObjects:\n";
    std::cout << "- Houses:       " << summary.houseCount << "\n";
    std::cout << "- Waypoints:    " << summary.waypointCount << "\n";
    std::cout << "- Terrain:      " << summary.terrainCount << "\n";
    std::cout << "- Iso tiles:    " << mapDatabase.getIsoTiles().size() << "\n";
    std::cout << "- Overlay:      " << mapDatabase.getOverlayCells().size() << "\n";
    std::cout << "- Structures:   " << summary.structureCount << "\n";
    std::cout << "- Units:        " << summary.unitCount << "\n";
    std::cout << "- Infantry:     " << summary.infantryCount << "\n";
    std::cout << "- Aircraft:     " << summary.aircraftCount << "\n";
    std::cout << "- Smudges:      " << summary.smudgeCount << "\n";
    std::cout << "- Cell tags:    " << summary.cellTagCount << "\n";

    std::cout << "\nTriggers and AI:\n";
    std::cout << "- Triggers:     " << summary.triggerCount << "\n";
    std::cout << "- Events:       " << summary.eventCount << "\n";
    std::cout << "- Actions:      " << summary.actionCount << "\n";
    std::cout << "- Tags:         " << summary.tagCount << "\n";
    std::cout << "- ScriptTypes:  " << summary.scriptTypeCount << "\n";
    std::cout << "- TaskForces:   " << summary.taskForceCount << "\n";
    std::cout << "- TeamTypes:    " << summary.teamTypeCount << "\n";
    std::cout << "- AI enables:   " << summary.aiTriggerEnableCount << "\n";

    std::cout << "\nPacked data:\n";
    printPackedSectionSummary(mapDatabase.getIsoMapPackSummary());
    printPackedSectionSummary(mapDatabase.getOverlayPackSummary());
    printPackedSectionSummary(mapDatabase.getOverlayDataPackSummary());
    printOverlaySummary(mapDatabase.getOverlaySummary());

    if (!mapDatabase.getWarnings().empty()) {
        std::cout << "\nWarnings:\n";
        for (const std::string& warning : mapDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }
}

static void printMapObjects(const MapDatabase& mapDatabase) {
    std::cout << "\n========== Map Objects ==========\n";

    std::cout << "\nWaypoints (" << mapDatabase.getWaypoints().size() << "):\n";
    for (const MapWaypoint& waypoint : mapDatabase.getWaypoints()) {
        std::cout << waypoint.id
                  << " | Cell=" << formatMapCell(waypoint.position)
                  << " | X=" << formatMapX(waypoint.position)
                  << " | Y=" << formatMapY(waypoint.position)
                  << "\n";
    }

    std::cout << "\nTerrain (" << mapDatabase.getTerrainObjects().size() << "):\n";
    for (const MapTerrainObject& object : mapDatabase.getTerrainObjects()) {
        std::cout << object.cell
                  << " | Type=" << object.type
                  << " | X=" << formatMapX(object.position)
                  << " | Y=" << formatMapY(object.position)
                  << "\n";
    }

    std::cout << "\nStructures (" << mapDatabase.getStructures().size() << "):\n";
    for (const MapStructureObject& object : mapDatabase.getStructures()) {
        std::cout << object.id
                  << " | Owner=" << object.owner
                  << " | Type=" << object.type
                  << " | Health=" << formatMaybeInt(object.health)
                  << " | X=" << formatMaybeInt(object.x)
                  << " | Y=" << formatMaybeInt(object.y)
                  << " | Cell=" << formatMapCell(object.position)
                  << " | Facing=" << formatMaybeInt(object.facing)
                  << " | Tag=" << (object.tag.empty() ? "-" : object.tag)
                  << "\n";
    }

    std::cout << "\nUnits (" << mapDatabase.getUnits().size() << "):\n";
    for (const MapUnitObject& object : mapDatabase.getUnits()) {
        std::cout << object.id
                  << " | Owner=" << object.owner
                  << " | Type=" << object.type
                  << " | Health=" << formatMaybeInt(object.health)
                  << " | X=" << formatMaybeInt(object.x)
                  << " | Y=" << formatMaybeInt(object.y)
                  << " | Cell=" << formatMapCell(object.position)
                  << " | Facing=" << formatMaybeInt(object.facing)
                  << " | Mission=" << (object.mission.empty() ? "-" : object.mission)
                  << " | Tag=" << (object.tag.empty() ? "-" : object.tag)
                  << " | OnBridge=" << (object.onBridge ? "yes" : "no")
                  << "\n";
    }

    std::cout << "\nInfantry (" << mapDatabase.getInfantry().size() << "):\n";
    for (const MapInfantryObject& object : mapDatabase.getInfantry()) {
        std::cout << object.id
                  << " | Owner=" << object.owner
                  << " | Type=" << object.type
                  << " | Health=" << formatMaybeInt(object.health)
                  << " | X=" << formatMaybeInt(object.x)
                  << " | Y=" << formatMaybeInt(object.y)
                  << " | Cell=" << formatMapCell(object.position)
                  << " | SubCell=" << formatMaybeInt(object.subCell)
                  << " | Mission=" << (object.mission.empty() ? "-" : object.mission)
                  << " | Facing=" << formatMaybeInt(object.facing)
                  << " | Tag=" << (object.tag.empty() ? "-" : object.tag)
                  << " | OnBridge=" << (object.onBridge ? "yes" : "no")
                  << "\n";
    }

    if (!mapDatabase.getWarnings().empty()) {
        std::cout << "\nWarnings:\n";
        for (const std::string& warning : mapDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }
}

static std::string formatFileCheckStatus(
    const TheaterTileReference& reference,
    bool checkedFiles
) {
    if (!reference.validTileSet) {
        return "unresolved";
    }

    if (!checkedFiles) {
        return "not-checked";
    }

    return reference.fileExists ? "found" : "missing";
}

static void printMapTileUsageReport(
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const MapTileUsageReport& report,
    bool checkedFiles
) {
    const MapSummary& summary = mapDatabase.getSummary();

    std::cout << "\n========== Map Tile Usage ==========\n";
    std::cout << "Map theater:       "
              << (summary.theater.empty() ? "-" : summary.theater)
              << "\n";
    std::cout << "Theater extension: "
              << (report.theaterExtension.empty() ? "-" : report.theaterExtension)
              << "\n";
    std::cout << "TileSets:          " << report.tileSetCount << "\n";
    std::cout << "Tile slots:        " << report.totalTileSlots << "\n";
    std::cout << "Map iso tiles:     " << report.mapTileCount << "\n";
    std::cout << "Empty tileIndex:   " << report.emptyTileCount << "\n";
    std::cout << "Unique tileIndex:  " << report.uniqueTileCount << "\n";
    std::cout << "Unresolved tiles:  " << report.unresolvedTileCount << "\n";
    std::cout << "Missing files:     ";
    if (checkedFiles) {
        std::cout << report.missingFileCount << "\n";
    } else {
        std::cout << "not checked\n";
    }

    if (!theaterDatabase.getWarnings().empty()) {
        std::cout << "\nTheater warnings:\n";
        for (const std::string& warning : theaterDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }

    if (!mapDatabase.getWarnings().empty()) {
        std::cout << "\nMap warnings:\n";
        for (const std::string& warning : mapDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }

    std::cout << "\nUsed tile indexes:\n";
    std::cout << "TileIndex Count TileSet InSet File Status\n";

    for (const TheaterTileReference& reference : report.usedTiles) {
        std::cout << reference.tileIndex
                  << " " << reference.useCount
                  << " ";

        if (reference.validTileSet) {
            std::cout << reference.tileSetIndex
                      << ":" << (reference.tileSetName.empty() ? "-" : reference.tileSetName)
                      << " " << reference.tileIndexInSet
                      << " " << (reference.expectedFileName.empty() ? "-" : reference.expectedFileName)
                      << " " << formatFileCheckStatus(reference, checkedFiles);

            if (reference.fileExists && !reference.resolvedPath.empty()) {
                std::cout << " " << reference.resolvedPath;
            }
        } else {
            std::cout << "- - - "
                      << formatFileCheckStatus(reference, checkedFiles);
        }

        std::cout << "\n";
    }
}

static std::string resolveTheaterPathForMap(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
);

static std::string resolvePalettePathForMap(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
);

struct LoadedMixArchive {
    std::string path;
    MixArchive archive;
};

static std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    return value;
}

static std::vector<LoadedMixArchive> loadReadableMixArchives(
    const std::string& mixRootPath,
    std::vector<std::string>& warnings
) {
    std::vector<LoadedMixArchive> archives;
    std::error_code errorCode;
    const std::filesystem::path root(mixRootPath);

    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        warnings.push_back("MIX root is not a readable directory: " + mixRootPath);
        return archives;
    }

    for (
        const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(root, errorCode)
    ) {
        if (errorCode) {
            warnings.push_back("Stopped scanning MIX root: " + errorCode.message());
            break;
        }

        if (!entry.is_regular_file(errorCode)) {
            continue;
        }

        if (toLowerAscii(entry.path().extension().string()) != ".mix") {
            continue;
        }

        LoadedMixArchive loadedArchive;
        loadedArchive.path = entry.path().string();
        if (!loadedArchive.archive.loadFromFile(loadedArchive.path)) {
            warnings.push_back(
                "Failed to load MIX archive "
                + loadedArchive.path
                + ": "
                + loadedArchive.archive.getErrorMessage()
            );
            continue;
        }

        if (!loadedArchive.archive.isIndexReadable()) {
            warnings.push_back(
                "Skipped unreadable MIX archive "
                + loadedArchive.path
                + ": "
                + loadedArchive.archive.getErrorMessage()
            );
            continue;
        }

        archives.push_back(std::move(loadedArchive));
    }

    return archives;
}

static int archiveTheaterPreferenceScore(
    const std::string& archivePath,
    const std::string& mapTheater
) {
    const std::string fileName =
        toLowerAscii(std::filesystem::path(archivePath).filename().string());
    const std::string theater = toLowerAscii(mapTheater);

    auto containsAny = [&](const std::vector<std::string>& tokens) {
        for (const std::string& token : tokens) {
            if (fileName.find(token) != std::string::npos) {
                return true;
            }
        }

        return false;
    };

    if (theater == "newurban" && containsAny({"ubn", "urbann"})) {
        return 0;
    }

    if (theater == "urban" && containsAny({"urb", "urban"})) {
        return 0;
    }

    if (theater == "desert" && containsAny({"des", "desert"})) {
        return 0;
    }

    if (theater == "snow" && containsAny({"sno", "snow"})) {
        return 0;
    }

    if (theater == "temperate" && containsAny({"tem", "temperat"})) {
        return 0;
    }

    if (theater == "lunar" && containsAny({"lun", "lunar"})) {
        return 0;
    }

    if (containsAny({"conq", "conquer", "gener", "generic"})) {
        return 10;
    }

    return 20;
}

static void sortArchivesForMapTheater(
    std::vector<LoadedMixArchive>& archives,
    const std::string& mapTheater
) {
    std::stable_sort(
        archives.begin(),
        archives.end(),
        [&](const LoadedMixArchive& left, const LoadedMixArchive& right) {
            return archiveTheaterPreferenceScore(left.path, mapTheater)
                < archiveTheaterPreferenceScore(right.path, mapTheater);
        }
    );
}

static int extractMapTileFiles(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
) {
    const std::string resolvedTheaterPath =
        resolveTheaterPathForMap(options, mapDatabase);

    if (resolvedTheaterPath.empty()) {
        std::cerr << "Failed to choose theater INI for map Theater="
                  << (mapDatabase.getSummary().theater.empty()
                      ? "-"
                      : mapDatabase.getSummary().theater)
                  << ". Use --theater <path> explicitly.\n";
        return 1;
    }

    IniFile theaterIni;
    if (!theaterIni.loadFromFile(resolvedTheaterPath)) {
        return 1;
    }

    TheaterDatabase theaterDatabase;
    if (!theaterDatabase.loadFromIni(theaterIni, resolvedTheaterPath)) {
        std::cerr << "Failed to load theater database.\n";
        return 1;
    }

    std::vector<std::string> mixWarnings;
    std::vector<LoadedMixArchive> archives =
        loadReadableMixArchives(options.mixRootPath, mixWarnings);
    sortArchivesForMapTheater(archives, mapDatabase.getSummary().theater);

    const MapTileUsageReport report =
        theaterDatabase.buildMapTileUsageReport(
            mapDatabase,
            options.extractMapTilesPath
        );

    std::cout << "Loaded theater file: " << resolvedTheaterPath << "\n";
    std::cout << "\n========== Map Tile Extraction ==========\n";
    std::cout << "Map theater:        "
              << (mapDatabase.getSummary().theater.empty()
                  ? "-"
                  : mapDatabase.getSummary().theater)
              << "\n";
    std::cout << "Theater extension:  "
              << (report.theaterExtension.empty() ? "-" : report.theaterExtension)
              << "\n";
    std::cout << "MIX root:           " << options.mixRootPath << "\n";
    std::cout << "Output directory:   " << options.extractMapTilesPath << "\n";
    std::cout << "Readable archives:  " << archives.size() << "\n";
    std::cout << "Unique tile files:  " << report.usedTiles.size() << "\n";

    if (!theaterDatabase.getWarnings().empty()) {
        std::cout << "\nTheater warnings:\n";
        for (const std::string& warning : theaterDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }

    if (!mixWarnings.empty()) {
        std::cout << "\nMIX scan warnings:\n";
        for (const std::string& warning : mixWarnings) {
            std::cout << "- " << warning << "\n";
        }
    }

    std::size_t alreadyPresentCount = 0;
    std::size_t extractedCount = 0;
    std::size_t notFoundCount = 0;
    std::size_t failedCount = 0;
    std::set<std::string> processedFileNames;

    std::cout << "\nFiles:\n";
    for (const TheaterTileReference& reference : report.usedTiles) {
        if (!reference.validTileSet || reference.expectedFileName.empty()) {
            ++notFoundCount;
            continue;
        }

        const std::string lowerFileName = toLowerAscii(reference.expectedFileName);
        if (!processedFileNames.insert(lowerFileName).second) {
            continue;
        }

        if (reference.fileExists && !options.overwriteOutput) {
            ++alreadyPresentCount;
            std::cout << "present "
                      << reference.expectedFileName
                      << " "
                      << reference.resolvedPath
                      << "\n";
            continue;
        }

        bool extracted = false;
        for (const LoadedMixArchive& loadedArchive : archives) {
            const MixEntry* entry =
                loadedArchive.archive.findEntryByName(reference.expectedFileName);
            if (entry == nullptr) {
                continue;
            }

            const std::filesystem::path outputPath =
                std::filesystem::path(options.extractMapTilesPath)
                / reference.expectedFileName;

            std::string errorMessage;
            if (!loadedArchive.archive.extractEntryToFile(
                    *entry,
                    outputPath.string(),
                    errorMessage
                )) {
                ++failedCount;
                std::cout << "failed "
                          << reference.expectedFileName
                          << " "
                          << loadedArchive.path
                          << " | "
                          << errorMessage
                          << "\n";
                extracted = true;
                break;
            }

            ++extractedCount;
            std::cout << "extracted "
                      << reference.expectedFileName
                      << " "
                      << loadedArchive.path
                      << "\n";
            extracted = true;
            break;
        }

        if (!extracted) {
            ++notFoundCount;
            std::cout << "missing "
                      << reference.expectedFileName
                      << "\n";
        }
    }

    std::cout << "\nSummary:\n";
    std::cout << "- Already present: " << alreadyPresentCount << "\n";
    std::cout << "- Extracted:       " << extractedCount << "\n";
    std::cout << "- Not found:       " << notFoundCount << "\n";
    std::cout << "- Failed:          " << failedCount << "\n";

    return failedCount == 0 && notFoundCount == 0 ? 0 : 1;
}

static std::string theaterIniFileNameForMapTheater(const std::string& mapTheater) {
    if (mapTheater == "TEMPERATE") {
        return "temperatmd.ini";
    }

    if (mapTheater == "SNOW") {
        return "snowmd.ini";
    }

    if (mapTheater == "URBAN") {
        return "urbanmd.ini";
    }

    if (mapTheater == "NEWURBAN") {
        return "urbannmd.ini";
    }

    if (mapTheater == "DESERT") {
        return "desertmd.ini";
    }

    if (mapTheater == "LUNAR") {
        return "lunarmd.ini";
    }

    return "";
}

static std::string paletteFileNameForMapTheater(const std::string& mapTheater) {
    if (mapTheater == "TEMPERATE") {
        return "isotem.pal";
    }

    if (mapTheater == "SNOW") {
        return "isosno.pal";
    }

    if (mapTheater == "URBAN") {
        return "isourb.pal";
    }

    if (mapTheater == "NEWURBAN") {
        return "isoubn.pal";
    }

    if (mapTheater == "DESERT") {
        return "isodes.pal";
    }

    if (mapTheater == "LUNAR") {
        return "isolun.pal";
    }

    return "";
}

static std::vector<std::string> unitPaletteFileNameCandidatesForMapTheater(
    const std::string& mapTheater
) {
    if (mapTheater == "TEMPERATE") {
        return {"unittem.pal", "cachemd_unittem.pal", "cache_unittem.pal"};
    }

    if (mapTheater == "SNOW") {
        return {"unitsno.pal", "cachemd_unitsno.pal", "cache_unitsno.pal"};
    }

    if (mapTheater == "URBAN") {
        return {"uniturb.pal", "cachemd_uniturb.pal", "cache_uniturb.pal"};
    }

    if (mapTheater == "NEWURBAN") {
        return {"unitubn.pal", "cachemd_unitubn.pal", "cache_unitubn.pal"};
    }

    if (mapTheater == "DESERT") {
        return {"unitdes.pal", "cachemd_unitdes.pal", "cache_unitdes.pal"};
    }

    if (mapTheater == "LUNAR") {
        return {"unitlun.pal", "cachemd_unitlun.pal", "cache_unitlun.pal"};
    }

    return {};
}

static std::string resolveTheaterPathForMap(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
) {
    if (!options.theaterPath.empty()) {
        return options.theaterPath;
    }

    const std::string theaterFileName =
        theaterIniFileNameForMapTheater(mapDatabase.getSummary().theater);

    if (theaterFileName.empty()) {
        return "";
    }

    return (std::filesystem::path(options.theaterDirPath) / theaterFileName).string();
}

static std::string resolvePalettePathForMap(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
) {
    if (!options.palettePath.empty()) {
        return options.palettePath;
    }

    const std::string paletteFileName =
        paletteFileNameForMapTheater(mapDatabase.getSummary().theater);

    if (paletteFileName.empty()) {
        return "";
    }

    return (std::filesystem::path(options.paletteDirPath) / paletteFileName).string();
}

static std::string resolveStructurePalettePathForMap(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase
) {
    const std::vector<std::string> candidates =
        unitPaletteFileNameCandidatesForMapTheater(mapDatabase.getSummary().theater);
    if (candidates.empty()) {
        return "";
    }

    std::vector<std::filesystem::path> searchDirs;
    if (!options.paletteDirPath.empty()) {
        const std::filesystem::path paletteDir(options.paletteDirPath);
        searchDirs.push_back(paletteDir);

        if (!paletteDir.parent_path().empty()) {
            searchDirs.push_back(paletteDir.parent_path() / "extracted_palettes");
        }
    }

    if (!options.palettePath.empty()) {
        const std::filesystem::path palettePath(options.palettePath);
        const std::filesystem::path paletteDir = palettePath.parent_path();
        if (!paletteDir.empty()) {
            searchDirs.push_back(paletteDir);
            if (!paletteDir.parent_path().empty()) {
                searchDirs.push_back(paletteDir.parent_path() / "extracted_palettes");
            }
        }
    }

    std::set<std::string> visitedDirs;
    for (const std::filesystem::path& directory : searchDirs) {
        const std::string directoryKey = directory.lexically_normal().string();
        if (!visitedDirs.insert(directoryKey).second) {
            continue;
        }

        for (const std::string& candidate : candidates) {
            const std::filesystem::path path = directory / candidate;
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) {
                return path.string();
            }
        }
    }

    return "";
}

static void printUnitList(const std::string& title, const std::vector<UnitType>& units) {
    std::cout << "\n========== " << title << " ==========\n";

    if (units.empty()) {
        std::cout << "No units loaded.\n";
        return;
    }

    for (const UnitType& unit : units) {
        std::cout << unit.id
                  << " | " << unit.name
                  << " | UIName=" << unit.uiName
                  << " | Image=" << unit.image
                  << " | Owner=" << unit.owner
                  << " | TechLevel=" << unit.techLevel
                  << " | HP=" << unit.strength
                  << " | Armor=" << unit.armor
                  << " | Primary=" << unit.primaryWeapon;

        if (!unit.secondaryWeapon.empty()) {
            std::cout << " | Secondary=" << unit.secondaryWeapon;
        }

        if (!unit.elitePrimaryWeapon.empty()) {
            std::cout << " | ElitePrimary=" << unit.elitePrimaryWeapon;
        }

        if (!unit.eliteSecondaryWeapon.empty()) {
            std::cout << " | EliteSecondary=" << unit.eliteSecondaryWeapon;
        }

        std::cout << " | Speed=" << unit.speed
                  << " | SpeedType=" << unit.speedType
                  << " | MovementZone=" << unit.movementZone
                  << " | Sight=" << unit.sight
                  << " | Category=" << unit.category
                  << " | Cost=" << unit.cost
                  << "\n";
    }
}

static void printAllUnits(const RulesDatabase& rules) {
    printUnitList("InfantryTypes", rules.getInfantryTypes());
    printUnitList("VehicleTypes", rules.getVehicleTypes());
    printUnitList("AircraftTypes", rules.getAircraftTypes());
}

static void printWeaponList(const std::vector<WeaponType>& weapons) {
    std::cout << "\n========== WeaponTypes ==========\n";

    if (weapons.empty()) {
        std::cout << "No weapons loaded.\n";
        return;
    }

    for (const WeaponType& weapon : weapons) {
        std::cout << weapon.id
                  << " | Damage=" << weapon.damage
                  << " | ROF=" << weapon.rof
                  << " | Range=" << weapon.range
                  << " | Projectile=" << weapon.projectile
                  << " | Speed=" << weapon.speed
                  << " | Warhead=" << weapon.warhead
                  << " | Burst=" << weapon.burst
                  << " | Report=" << weapon.report
                  << " | Anim=" << weapon.anim
                  << "\n";
    }
}

static int parsePositiveIntOrDefault(
    const std::string& value,
    int defaultValue
) {
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : defaultValue;
    } catch (...) {
        return defaultValue;
    }
}

static int parseNonNegativeIntOrDefault(
    const std::string& value,
    int defaultValue
) {
    if (value.empty()) {
        return defaultValue;
    }

    try {
        const int parsed = std::stoi(value);
        return parsed >= 0 ? parsed : defaultValue;
    } catch (...) {
        return defaultValue;
    }
}

static std::string formatCombatPoint(const CombatGridPoint& point) {
    return "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
}

static std::string formatEconomyPoint(const EconomyGridPoint& point) {
    return "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
}

static void printCombatSandboxReport(const CombatSandboxReport& report) {
    if (!report.success) {
        std::cerr << "Failed to run combat sandbox: "
                  << report.errorMessage
                  << "\n";
        return;
    }

    std::cout << "\n========== Combat Sandbox ==========\n";
    std::cout << "Selected: #"
              << report.selectedInstanceId
              << "\n";
    std::cout << "Attacker: #"
              << report.attackerInstanceId
              << " "
              << report.attackerId
              << " HP="
              << report.attackerStartingHp
              << " start="
              << formatCombatPoint(report.attackerStartPosition)
              << "\n";
    std::cout << "Defender: #"
              << report.defenderInstanceId
              << " "
              << report.defenderId
              << " HP="
              << report.defenderStartingHp
              << " start="
              << formatCombatPoint(report.defenderStartPosition)
              << "\n";
    std::cout << "Ticks simulated: " << report.ticksSimulated << "\n";

    std::cout << "\nEvents:\n";
    for (const CombatSandboxEvent& event : report.events) {
        std::cout << "tick " << event.tick << ": "
                  << event.text << "\n";
    }

    std::cout << "\nResult:\n";
    std::cout << "Winner: " << report.winner << "\n";
    std::cout << report.attackerId << " HP="
              << report.attackerFinalHp
              << " alive="
              << (report.attackerAlive ? "yes" : "no")
              << " final="
              << formatCombatPoint(report.attackerFinalPosition)
              << "\n";
    std::cout << report.defenderId << " HP="
              << report.defenderFinalHp
              << " alive="
              << (report.defenderAlive ? "yes" : "no")
              << " final="
              << formatCombatPoint(report.defenderFinalPosition)
              << "\n";
}

static void printEconomySandboxReport(const EconomySandboxReport& report) {
    if (!report.success) {
        std::cerr << "Failed to run economy sandbox: "
                  << report.errorMessage
                  << "\n";
        return;
    }

    std::cout << "\n========== Economy Sandbox ==========\n";
    std::cout << "House: " << report.houseId << "\n";
    std::cout << "MCV: " << report.mcvId
              << " -> " << report.constructionYardId << "\n";
    std::cout << "Credits: " << report.startingCredits
              << " -> " << report.finalCredits << "\n";
    std::cout << "Power: produced=" << report.powerProduced
              << " used=" << report.powerUsed
              << " balance=" << (report.powerProduced - report.powerUsed)
              << "\n";
    std::cout << "Ticks simulated: " << report.ticksSimulated << "\n";
    std::cout << "Ore loads delivered: " << report.oreLoadsDelivered << "\n";
    std::cout << "Infantry produced: " << report.infantryProduced << "\n";
    std::cout << "Combat vehicles produced: "
              << report.combatVehiclesProduced << "\n";
    std::cout << "Harvesters owned: " << report.harvestersOwned << "\n";

    std::cout << "\nBuildings:\n";
    for (const EconomyObjectSummary& building : report.buildings) {
        std::cout << "  #"
                  << building.instanceId
                  << " "
                  << building.typeId
                  << " at "
                  << formatEconomyPoint(building.position)
                  << "\n";
    }

    std::cout << "\nUnits:\n";
    for (const EconomyObjectSummary& unit : report.units) {
        std::cout << "  #"
                  << unit.instanceId
                  << " "
                  << unit.typeId
                  << " "
                  << unit.kind
                  << " at "
                  << formatEconomyPoint(unit.position)
                  << "\n";
    }

    std::cout << "\nEvents:\n";
    for (const EconomySandboxEvent& event : report.events) {
        std::cout << "tick " << event.tick << ": " << event.text << "\n";
    }

    std::cout << "\nResult:\n";
    std::cout << "Objective complete: "
              << (report.objectiveComplete ? "yes" : "no")
              << "\n";
    std::cout << "Winner: " << report.winner << "\n";
}

static void printAiTriggerSandboxReport(const AiTriggerSandboxReport& report) {
    if (!report.success) {
        std::cerr << "Failed to run AI trigger sandbox: "
                  << report.errorMessage
                  << "\n";
        return;
    }

    std::cout << "\n========== AI Trigger Sandbox ==========\n";
    std::cout << "AI House: " << report.aiHouseId << "\n";
    std::cout << "Enemy House: " << report.enemyHouseId << "\n";
    std::cout << "MCV: " << report.mcvId << "\n";
    std::cout << "Ticks simulated: " << report.ticksSimulated << "\n";

    std::cout << "\nParsed Map AI:\n";
    std::cout << "  map loaded: "
              << (report.parsed.mapLoaded ? "yes" : "no") << "\n";
    std::cout << "  triggers/events/actions: "
              << report.parsed.triggers
              << "/"
              << report.parsed.events
              << "/"
              << report.parsed.actions
              << "\n";
    std::cout << "  tags: " << report.parsed.tags << "\n";
    std::cout << "  teamTypes/taskForces/scriptTypes: "
              << report.parsed.teamTypes
              << "/"
              << report.parsed.taskForces
              << "/"
              << report.parsed.scriptTypes
              << "\n";
    std::cout << "  AITriggerTypes/enables: "
              << report.parsed.aiTriggerTypes
              << "/"
              << report.parsed.aiTriggerEnables
              << "\n";
    std::cout << "  create-team actions: "
              << report.parsed.createTeamActions
              << "\n";
    std::cout << "  linked trigger refs: "
              << report.parsed.linkedTriggerReferences
              << "\n";

    std::cout << "\nSelected Team:\n";
    std::cout << "  id: " << report.selectedTeam.id << "\n";
    std::cout << "  name: " << report.selectedTeam.name << "\n";
    std::cout << "  house: " << report.selectedTeam.house << "\n";
    std::cout << "  taskForce: " << report.selectedTeam.taskForceId
              << " " << report.selectedTeam.taskForceName
              << " units=" << report.selectedTeam.unitCount << "\n";
    std::cout << "  script: " << report.selectedTeam.scriptId
              << " " << report.selectedTeam.scriptName
              << " steps=" << report.selectedTeam.scriptStepCount << "\n";

    std::cout << "\nEvents:\n";
    for (const AiTriggerSandboxEvent& event : report.events) {
        std::cout << "tick " << event.tick << ": " << event.text << "\n";
    }

    std::cout << "\nResult:\n";
    std::cout << "Economy complete: "
              << (report.economyComplete ? "yes" : "no")
              << "\n";
    std::cout << "Attack wave created: "
              << (report.attackWaveCreated ? "yes" : "no")
              << "\n";
    std::cout << "Attack wave launched: "
              << (report.attackWaveLaunched ? "yes" : "no")
              << "\n";
    std::cout << "Victory trigger fired: "
              << (report.victoryTriggerFired ? "yes" : "no")
              << "\n";
    std::cout << "Winner: " << report.winner << "\n";
}

static void printModCompatibilityReport(const ModCompatibilityReport& report) {
    if (!report.success) {
        std::cerr << "Failed to run MOD compatibility report: "
                  << report.errorMessage
                  << "\n";
        return;
    }

    std::cout << "\n========== MOD Compatibility Report ==========\n";
    std::cout << "Achieved Level: " << report.achievedLevel << "\n";

    std::cout << "\nLevels:\n";
    for (const ModCompatibilityLevelResult& level : report.levels) {
        std::cout << "  Level "
                  << level.level
                  << " "
                  << level.name
                  << ": "
                  << (level.achieved ? "yes" : "no")
                  << " - "
                  << level.detail
                  << "\n";
    }

    std::cout << "\nCatalog:\n";
    std::cout << "  infantry/vehicles/aircraft: "
              << report.infantryTypes
              << "/"
              << report.vehicleTypes
              << "/"
              << report.aircraftTypes
              << "\n";
    std::cout << "  buildings/weapons/projectiles/warheads: "
              << report.buildingTypes
              << "/"
              << report.weaponTypes
              << "/"
              << report.projectileTypes
              << "/"
              << report.warheadTypes
              << "\n";
    std::cout << "  recognized object records: "
              << report.customObjectCount
              << "\n";
    std::cout << "  art sections: " << report.artSections << "\n";
    std::cout << "  map sections: " << report.mapSections << "\n";

    std::cout << "\nExtension Tags:\n";
    std::cout << "  Ares hits: " << report.aresTagCount << "\n";
    std::cout << "  Phobos hits: " << report.phobosTagCount << "\n";
    std::cout << "  unknown extension-like tags: "
              << report.unknownTagCount
              << "\n";

    if (!report.tagHits.empty()) {
        std::cout << "\nRecognized Extension Tag Samples:\n";
        const std::size_t sampleCount = std::min<std::size_t>(report.tagHits.size(), 20);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const ModCompatibilityTagHit& tag = report.tagHits[index];
            std::cout << "  ["
                      << tag.family
                      << "] "
                      << tag.section
                      << "."
                      << tag.key
                      << "="
                      << tag.value
                      << (tag.implemented ? " [implemented] " : " [unsupported] ")
                      << tag.supportDetail
                      << "\n";
        }
        if (report.tagHits.size() > sampleCount) {
            std::cout << "  ... "
                      << (report.tagHits.size() - sampleCount)
                      << " more recognized extension tags\n";
        }
    }

    if (!report.findings.empty()) {
        std::cout << "\nFindings:\n";
        for (const ModCompatibilityFinding& finding : report.findings) {
            std::cout << "  ["
                      << finding.severity
                      << "] "
                      << finding.source
                      << " "
                      << finding.section
                      << "."
                      << finding.key;
            if (!finding.value.empty()) {
                std::cout << "=" << finding.value;
            }
            std::cout << " - "
                      << finding.message
                      << "\n";
        }
    }
}

static void printIndentedList(const std::vector<std::string>& values) {
    for (const std::string& value : values) {
        std::cout << "    - " << value << "\n";
    }
}

static void printPerformancePlanReport(const PerformancePlanReport& report) {
    std::cout << "\n========== Performance and Multicore Plan ==========\n";
    std::cout << "Hardware threads:         " << report.hardwareThreads << "\n";
    std::cout << "Planned worker threads:   " << report.workerThreads << "\n";
    std::cout << "Deterministic simulation: "
              << (report.deterministicSimulationMainThread
                  ? "main-thread fixed tick"
                  : "not configured")
              << "\n";
    std::cout << "Render thread planned:    "
              << (report.renderThreadPlanned ? "yes" : "no")
              << "\n";
    std::cout << "Async loading planned:    "
              << (report.asyncLoadingPlanned ? "yes" : "no")
              << "\n";
    std::cout << "Job system planned:       "
              << (report.jobSystemPlanned ? "yes" : "no")
              << "\n";

    std::cout << "\nSubsystem strategy:\n";
    for (const PerformanceSubsystemPlan& subsystem : report.subsystems) {
        std::cout << "- " << subsystem.name
                  << " | parallel=" << subsystem.parallelism
                  << " | owner=" << subsystem.owner
                  << "\n";
        std::cout << "  strategy: " << subsystem.strategy << "\n";
        std::cout << "  boundary: " << subsystem.deterministicBoundary << "\n";
        std::cout << "  first step: " << subsystem.firstStep << "\n";
    }

    std::cout << "\nThread lanes:\n";
    for (const PerformanceThreadLane& lane : report.lanes) {
        std::cout << "- " << lane.name << ": " << lane.purpose << "\n";
        printIndentedList(lane.jobs);
    }

    std::cout << "\nRisks and mitigations:\n";
    for (const PerformanceRisk& risk : report.risks) {
        std::cout << "- [" << risk.severity << "] "
                  << risk.area
                  << ": "
                  << risk.mitigation
                  << "\n";
    }

    std::cout << "\nImplementation milestones:\n";
    for (const PerformanceMilestone& milestone : report.milestones) {
        std::cout << "- " << milestone.name << "\n";
        printIndentedList(milestone.checks);
    }
}

static void printReplaySandboxReport(const ReplaySandboxReport& report) {
    if (!report.success) {
        std::cerr << "Failed to run replay sandbox: "
                  << report.errorMessage
                  << "\n";
        return;
    }

    std::cout << "\n========== Deterministic Replay Sandbox ==========\n";
    std::cout << "Players:          " << report.playerCount << "\n";
    std::cout << "Seed:             " << report.randomSeed << "\n";
    std::cout << "Attacker:         " << report.attackerId << "\n";
    std::cout << "Defender:         " << report.defenderId << "\n";
    std::cout << "Ticks simulated:  " << report.ticksSimulated << "\n";
    std::cout << "Command records:  " << report.commandCount << "\n";
    std::cout << "Replay verified:  "
              << (report.replayVerified ? "yes" : "no")
              << "\n";
    std::cout << "Desync detected:  "
              << (report.desyncDetected ? "yes" : "no")
              << "\n";
    if (report.desyncDetected) {
        std::cout << "First desync tick: " << report.firstDesyncTick << "\n";
        std::cout << "Expected desync:   "
                  << (report.expectedDesync ? "yes" : "no")
                  << "\n";
    }
    std::cout << "Final state hash: " << report.finalStateHash << "\n";
    std::cout << "Winner:           "
              << (report.winner.empty() ? "-" : report.winner)
              << "\n";

    if (report.replayExported) {
        std::cout << "Replay file:      " << report.replayOutputPath << "\n";
    }

    std::cout << "\nLockstep rules:\n";
    printIndentedList(report.lockstepRules);

    std::cout << "\nCommands:\n";
    for (const ReplayCommandRecord& command : report.commands) {
        std::cout << "  tick="
                  << command.tick
                  << " player="
                  << command.playerId
                  << " seq="
                  << command.sequence
                  << " "
                  << command.type
                  << " #"
                  << command.subjectInstanceId
                  << " -> #"
                  << command.targetInstanceId
                  << "\n";
    }

    std::cout << "\nHash samples:\n";
    for (const ReplayHashFrame& frame : report.hashFrames) {
        std::cout << "  tick="
                  << frame.tick
                  << " commands="
                  << frame.commandsApplied;
        for (std::size_t index = 0; index < frame.peerHashes.size(); ++index) {
            std::cout << " peer"
                      << index
                      << "="
                      << frame.peerHashes[index];
        }
        std::cout << "\n";
    }

    if (!report.events.empty()) {
        std::cout << "\nEvents:\n";
        for (const ReplaySandboxEvent& event : report.events) {
            std::cout << "  ["
                      << event.tick
                      << "] "
                      << event.text
                      << "\n";
        }
    }
}

static void printDiagnostics(const DiagnosticReport& report) {
    std::cout << "\n========== Object Reference Diagnostics ==========\n";
    std::cout << "Errors:   " << report.errorCount() << "\n";
    std::cout << "Warnings: " << report.warningCount() << "\n";
    std::cout << "Info:     " << report.infoCount() << "\n";

    if (report.diagnostics.empty()) {
        std::cout << "\nNo diagnostics.\n";
        return;
    }

    std::cout << "\nDiagnostics:\n";

    for (const Diagnostic& diagnostic : report.diagnostics) {
        std::cout << "- [" << toString(diagnostic.severity) << "] "
                  << "source=" << diagnostic.source
                  << " field=" << diagnostic.field
                  << " target="
                  << (diagnostic.target.empty() ? "-" : diagnostic.target)
                  << " | " << diagnostic.message
                  << "\n";
    }
}

static void printArtLinks(const ArtDatabase& artDatabase, const RulesDatabase& rules) {
    const std::vector<ArtObjectLink> links = artDatabase.buildLinks(rules);
    int linkedCount = 0;
    int missingCount = 0;

    for (const ArtObjectLink& link : links) {
        if (link.hasArt) {
            ++linkedCount;
        } else {
            ++missingCount;
        }
    }

    std::cout << "\n========== Art Links ==========\n";
    std::cout << "Art sections: " << artDatabase.getArtTypes().size() << "\n";
    std::cout << "Linked objects: " << linkedCount << "\n";
    std::cout << "Missing art links: " << missingCount << "\n";
    std::cout << "Art parser warnings: " << artDatabase.getWarnings().size() << "\n";

    for (const std::string& warning : artDatabase.getWarnings()) {
        std::cout << "- warning: " << warning << "\n";
    }

    std::cout << "\nLinks:\n";

    for (const ArtObjectLink& link : links) {
        std::cout << link.objectKind
                  << " [" << link.objectId << "]"
                  << " -> [" << link.artId << "]"
                  << " | hasArt=" << (link.hasArt ? "yes" : "no")
                  << " | image=" << link.image
                  << " | voxel=" << (link.voxel ? "yes" : "no")
                  << " | cameo=" << link.cameo
                  << " | sequence=" << link.sequence
                  << " | foundation=" << link.foundation
                  << "\n";
    }
}

static bool loadRules(const std::string& rulesPath, IniFile& ini, RulesDatabase& rules) {
    const bool trace = commandLineTraceEnabled();
    if (trace) {
        std::cerr << "loadRules: loading INI " << rulesPath << "\n";
    }
    if (!ini.loadFromFile(rulesPath)) {
        return false;
    }

    if (trace) {
        std::cerr << "loadRules: sections="
                  << ini.getSectionNames().size()
                  << "\n";
        std::cerr << "loadRules: loading RulesDatabase\n";
    }
    if (!rules.loadFromIni(ini)) {
        std::cerr << "Failed to load rules database.\n";
        return false;
    }

    if (trace) {
        std::cerr << "loadRules: loaded RulesDatabase infantry="
                  << rules.getInfantryTypes().size()
                  << " vehicles="
                  << rules.getVehicleTypes().size()
                  << " buildings="
                  << rules.getBuildingTypes().size()
                  << " weapons="
                  << rules.getWeaponTypes().size()
                  << "\n";
    }
    return true;
}

static bool loadArt(const std::string& artPath, ArtDatabase& artDatabase) {
    IniFile artIni;

    if (!artIni.loadFromFile(artPath)) {
        return false;
    }

    if (!artDatabase.loadFromIni(artIni)) {
        std::cerr << "Failed to load art database.\n";
        return false;
    }

    return true;
}

static std::string firstExistingFile(const std::vector<std::filesystem::path>& candidates) {
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code errorCode;
        if (std::filesystem::is_regular_file(candidate, errorCode)) {
            return candidate.string();
        }
    }
    return "";
}

static std::string defaultAssetRootForRules(const std::string& rulesPath) {
    if (rulesPath.empty()) {
        return "";
    }

    const std::filesystem::path rules = std::filesystem::path(rulesPath);
    const std::filesystem::path localData = rules.parent_path();
    if (localData.empty()) {
        return "";
    }
    return (localData / "resources" / "mix_exports").string();
}

static std::string findSoundIniForEngineStatus(const std::string& assetRootPath) {
    if (assetRootPath.empty()) {
        return "";
    }

    const std::filesystem::path root(assetRootPath);
    return firstExistingFile({
        root / "ra2md" / "localmd" / "soundmd.ini",
        root / "expandmd01" / "soundmd.ini",
        root / "language" / "sound.ini",
        root / "langmd" / "soundmd.ini"
    });
}

static std::string findEvaIniForEngineStatus(const std::string& assetRootPath) {
    if (assetRootPath.empty()) {
        return "";
    }

    const std::filesystem::path root(assetRootPath);
    return firstExistingFile({
        root / "ra2md" / "localmd" / "evamd.ini",
        root / "expandmd01" / "evamd.ini",
        root / "language" / "eva.ini",
        root / "langmd" / "evamd.ini"
    });
}

static std::unordered_map<std::string, std::vector<std::string>> buildAssetFileIndex(
    const std::string& assetRootPath,
    std::vector<std::string>& warnings,
    std::size_t& indexedFileCount
) {
    std::unordered_map<std::string, std::vector<std::string>> filesByName;
    indexedFileCount = 0;

    std::error_code errorCode;
    const std::filesystem::path root(assetRootPath);
    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        warnings.push_back("Asset root is not a readable directory: " + assetRootPath);
        return filesByName;
    }

    for (
        const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(root, errorCode)
    ) {
        if (errorCode) {
            warnings.push_back("Stopped scanning asset root: " + errorCode.message());
            break;
        }

        if (!entry.is_regular_file(errorCode)) {
            continue;
        }

        ++indexedFileCount;
        const std::string fileName =
            toLowerAscii(entry.path().filename().string());
        filesByName[fileName].push_back(entry.path().string());
    }

    return filesByName;
}

static std::string normalizedPathForCompare(const std::filesystem::path& path) {
    std::error_code errorCode;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(path, errorCode);
    if (errorCode) {
        normalized = std::filesystem::absolute(path, errorCode);
    }
    if (errorCode) {
        normalized = path;
    }

    return toLowerAscii(normalized.lexically_normal().string());
}

static bool isSameOrChildPath(
    const std::filesystem::path& childPath,
    const std::filesystem::path& parentPath
) {
    const std::string child = normalizedPathForCompare(childPath);
    std::string parent = normalizedPathForCompare(parentPath);
    if (parent.empty()) {
        return false;
    }

    if (child == parent) {
        return true;
    }

    const char last = parent[parent.size() - 1];
    if (last != '\\' && last != '/') {
        parent.push_back('\\');
    }

    return child.size() > parent.size()
        && child.compare(0, parent.size(), parent) == 0;
}

static bool isGeneratedResourceOutputPath(const std::string& sourcePath) {
    const std::string lowerPath = toLowerAscii(sourcePath);
    return lowerPath.find("\\extracted_buildings\\") != std::string::npos
        || lowerPath.find("/extracted_buildings/") != std::string::npos
        || lowerPath.find("\\extracted_tiles\\") != std::string::npos
        || lowerPath.find("/extracted_tiles/") != std::string::npos
        || lowerPath.find("\\tmp_previews\\") != std::string::npos
        || lowerPath.find("/tmp_previews/") != std::string::npos;
}

static const std::string* findPreferredLooseAssetSource(
    const std::vector<std::string>& sourcePaths,
    const std::filesystem::path& outputDirectory
) {
    const std::string* bestPath = nullptr;

    for (const std::string& sourcePath : sourcePaths) {
        if (
            isGeneratedResourceOutputPath(sourcePath)
            || isSameOrChildPath(sourcePath, outputDirectory)
        ) {
            continue;
        }

        bestPath = &sourcePath;
        break;
    }

    return bestPath;
}

static std::string withShpExtension(const std::string& imageName) {
    if (imageName.empty()) {
        return "";
    }

    const std::filesystem::path path(imageName);
    if (!path.extension().empty()) {
        return path.filename().string();
    }

    return imageName + ".shp";
}

struct MapStructureArtAsset {
    std::string structureType;
    std::size_t count = 0;
    std::string rulesImage;
    std::string artId;
    std::string artImage;
    std::string expectedShp;
    bool hasRules = false;
    bool hasArt = false;
    bool voxelArt = false;
};

static std::vector<MapStructureArtAsset> collectMapStructureArtAssets(
    const MapDatabase& mapDatabase,
    const RulesDatabase& rules,
    const ArtDatabase& artDatabase
) {
    std::unordered_map<std::string, const BuildingType*> buildingsById;
    buildingsById.reserve(rules.getBuildingTypes().size());
    for (const BuildingType& building : rules.getBuildingTypes()) {
        buildingsById.emplace(building.id, &building);
    }

    std::unordered_map<std::string, ArtObjectLink> buildingLinksById;
    for (const ArtObjectLink& link : artDatabase.buildLinks(rules)) {
        if (link.objectKind == "building") {
            buildingLinksById.emplace(link.objectId, link);
        }
    }

    std::map<std::string, std::size_t> usedStructureTypes;
    for (const MapStructureObject& structure : mapDatabase.getStructures()) {
        ++usedStructureTypes[structure.type];
    }

    std::vector<MapStructureArtAsset> assets;
    assets.reserve(usedStructureTypes.size());

    for (const auto& [structureType, count] : usedStructureTypes) {
        MapStructureArtAsset asset;
        asset.structureType = structureType;
        asset.count = count;

        const auto buildingIt = buildingsById.find(structureType);
        const BuildingType* building =
            buildingIt == buildingsById.end() ? nullptr : buildingIt->second;
        asset.hasRules = building != nullptr;
        asset.rulesImage = building == nullptr ? "" : building->image;

        const auto linkIt = buildingLinksById.find(structureType);
        const ArtObjectLink* link =
            linkIt == buildingLinksById.end() ? nullptr : &linkIt->second;

        asset.hasArt = link != nullptr && link->hasArt;
        asset.voxelArt = link != nullptr && link->voxel;
        asset.artId =
            link == nullptr
                ? (asset.rulesImage.empty() ? structureType : asset.rulesImage)
                : link->artId;
        asset.artImage =
            link == nullptr || link->image.empty()
                ? asset.artId
                : link->image;
        asset.expectedShp = withShpExtension(asset.artImage);

        assets.push_back(asset);
    }

    return assets;
}

static int printMapObjectArtReport(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase,
    const IniFile& rulesIni
) {
    RulesDatabase rules;
    if (!rules.loadFromIni(rulesIni)) {
        std::cerr << "Failed to load rules database.\n";
        return 1;
    }

    ArtDatabase artDatabase;
    if (!loadArt(options.artPath, artDatabase)) {
        return 1;
    }

    std::error_code assetRootError;
    const std::filesystem::path assetRoot(options.assetRootPath);
    if (
        !std::filesystem::exists(assetRoot, assetRootError)
        || !std::filesystem::is_directory(assetRoot, assetRootError)
    ) {
        std::cerr << "Asset root is not a readable directory: "
                  << options.assetRootPath
                  << "\n";
        return 1;
    }

    std::vector<std::string> assetWarnings;
    std::size_t indexedFileCount = 0;
    const std::unordered_map<std::string, std::vector<std::string>> filesByName =
        buildAssetFileIndex(
            options.assetRootPath,
            assetWarnings,
            indexedFileCount
        );

    const std::vector<MapStructureArtAsset> assets =
        collectMapStructureArtAssets(mapDatabase, rules, artDatabase);

    std::size_t presentShpCount = 0;
    std::size_t missingShpCount = 0;
    std::size_t missingRulesCount = 0;
    std::size_t missingArtCount = 0;
    std::size_t voxelArtCount = 0;

    std::cout << "Loaded ART file: " << options.artPath << "\n";
    std::cout << "Scanned asset root: " << options.assetRootPath << "\n";
    std::cout << "\n========== Map Object Art Report ==========\n";
    std::cout << "Scope:                Structures\n";
    std::cout << "Map structures:       " << mapDatabase.getStructures().size() << "\n";
    std::cout << "Unique structure IDs: " << assets.size() << "\n";
    std::cout << "Rules buildings:      " << rules.getBuildingTypes().size() << "\n";
    std::cout << "Art sections:         " << artDatabase.getArtTypes().size() << "\n";
    std::cout << "Indexed local files:  " << indexedFileCount << "\n";

    if (!assetWarnings.empty()) {
        std::cout << "\nAsset scan warnings:\n";
        for (const std::string& warning : assetWarnings) {
            std::cout << "- " << warning << "\n";
        }
    }

    if (!artDatabase.getWarnings().empty()) {
        std::cout << "\nART warnings:\n";
        for (const std::string& warning : artDatabase.getWarnings()) {
            std::cout << "- " << warning << "\n";
        }
    }

    std::cout << "\nStructure art coverage:\n";
    std::cout << "Type Count RulesImage ArtId ArtImage ExpectedSHP Status Path\n";

    for (const MapStructureArtAsset& asset : assets) {
        std::string status;
        std::string firstPath = "-";

        if (!asset.hasRules) {
            status = "missing-rules";
            ++missingRulesCount;
        } else if (!asset.hasArt) {
            status = "missing-art";
            ++missingArtCount;
        } else if (asset.voxelArt) {
            status = "voxel-art";
            ++voxelArtCount;
        } else {
            const auto fileIt = filesByName.find(toLowerAscii(asset.expectedShp));
            if (fileIt == filesByName.end() || fileIt->second.empty()) {
                status = "missing-shp";
                ++missingShpCount;
            } else {
                status = "found";
                ++presentShpCount;
                firstPath = fileIt->second.front();
            }
        }

        std::cout << asset.structureType
                  << " " << asset.count
                  << " " << (asset.rulesImage.empty() ? "-" : asset.rulesImage)
                  << " " << (asset.artId.empty() ? "-" : asset.artId)
                  << " " << (asset.artImage.empty() ? "-" : asset.artImage)
                  << " " << (asset.expectedShp.empty() ? "-" : asset.expectedShp)
                  << " " << status
                  << " " << firstPath
                  << "\n";
    }

    std::cout << "\nSummary:\n";
    std::cout << "Found SHP:       " << presentShpCount << "\n";
    std::cout << "Missing SHP:     " << missingShpCount << "\n";
    std::cout << "Missing rules:   " << missingRulesCount << "\n";
    std::cout << "Missing art:     " << missingArtCount << "\n";
    std::cout << "Voxel art:       " << voxelArtCount << "\n";

    return 0;
}

static int extractMapObjectArtFiles(
    const CommandLineOptions& options,
    const MapDatabase& mapDatabase,
    const IniFile& rulesIni
) {
    RulesDatabase rules;
    if (!rules.loadFromIni(rulesIni)) {
        std::cerr << "Failed to load rules database.\n";
        return 1;
    }

    ArtDatabase artDatabase;
    if (!loadArt(options.artPath, artDatabase)) {
        return 1;
    }

    const std::vector<MapStructureArtAsset> assets =
        collectMapStructureArtAssets(mapDatabase, rules, artDatabase);

    std::vector<std::string> mixWarnings;
    std::vector<LoadedMixArchive> archives =
        loadReadableMixArchives(options.mixRootPath, mixWarnings);
    sortArchivesForMapTheater(archives, mapDatabase.getSummary().theater);

    std::vector<std::string> looseWarnings;
    std::size_t indexedLooseFileCount = 0;
    const std::unordered_map<std::string, std::vector<std::string>> looseFilesByName =
        buildAssetFileIndex(
            options.mixRootPath,
            looseWarnings,
            indexedLooseFileCount
        );

    const std::filesystem::path outputDirectory(options.extractMapObjectArtPath);

    try {
        std::filesystem::create_directories(outputDirectory);
    } catch (const std::exception& exception) {
        std::cerr << "Failed to create output directory: "
                  << exception.what()
                  << "\n";
        return 1;
    }

    std::cout << "Loaded ART file: " << options.artPath << "\n";
    std::cout << "\n========== Map Object Art Extraction ==========\n";
    std::cout << "Scope:                Structures\n";
    std::cout << "Map theater:          " << mapDatabase.getSummary().theater << "\n";
    std::cout << "Map structures:       " << mapDatabase.getStructures().size() << "\n";
    std::cout << "Unique structure IDs: " << assets.size() << "\n";
    std::cout << "MIX root:             " << options.mixRootPath << "\n";
    std::cout << "Output directory:     " << options.extractMapObjectArtPath << "\n";
    std::cout << "Indexed loose files:  " << indexedLooseFileCount << "\n";
    std::cout << "Readable archives:    " << archives.size() << "\n";

    if (!looseWarnings.empty()) {
        std::cout << "\nLoose file scan warnings:\n";
        for (const std::string& warning : looseWarnings) {
            std::cout << "- " << warning << "\n";
        }
    }

    if (!mixWarnings.empty()) {
        std::cout << "\nMIX scan warnings:\n";
        for (const std::string& warning : mixWarnings) {
            std::cout << "- " << warning << "\n";
        }
    }

    std::size_t alreadyPresentCount = 0;
    std::size_t copiedLooseCount = 0;
    std::size_t extractedCount = 0;
    std::size_t notFoundCount = 0;
    std::size_t failedCount = 0;
    std::size_t skippedCount = 0;
    std::set<std::string> processedFileNames;

    std::cout << "\nFiles:\n";
    for (const MapStructureArtAsset& asset : assets) {
        if (!asset.hasRules) {
            ++skippedCount;
            std::cout << "skip missing-rules "
                      << asset.structureType
                      << "\n";
            continue;
        }

        if (!asset.hasArt) {
            ++skippedCount;
            std::cout << "skip missing-art "
                      << asset.structureType
                      << " "
                      << (asset.artId.empty() ? "-" : asset.artId)
                      << "\n";
            continue;
        }

        if (asset.voxelArt) {
            ++skippedCount;
            std::cout << "skip voxel-art "
                      << asset.structureType
                      << " "
                      << (asset.expectedShp.empty() ? "-" : asset.expectedShp)
                      << "\n";
            continue;
        }

        if (asset.expectedShp.empty()) {
            ++skippedCount;
            std::cout << "skip empty-shp "
                      << asset.structureType
                      << "\n";
            continue;
        }

        const std::string lowerFileName = toLowerAscii(asset.expectedShp);
        if (!processedFileNames.insert(lowerFileName).second) {
            continue;
        }

        const std::filesystem::path outputPath =
            outputDirectory / asset.expectedShp;
        std::error_code existsError;
        if (
            std::filesystem::exists(outputPath, existsError)
            && !options.overwriteOutput
        ) {
            ++alreadyPresentCount;
            std::cout << "present "
                      << asset.expectedShp
                      << " "
                      << outputPath.string()
                      << "\n";
            continue;
        }

        const auto looseFileIt = looseFilesByName.find(lowerFileName);
        if (looseFileIt != looseFilesByName.end()) {
            const std::string* sourcePath = findPreferredLooseAssetSource(
                looseFileIt->second,
                outputDirectory
            );
            if (sourcePath != nullptr) {
                std::error_code copyError;
                const bool copied = std::filesystem::copy_file(
                    *sourcePath,
                    outputPath,
                    options.overwriteOutput
                        ? std::filesystem::copy_options::overwrite_existing
                        : std::filesystem::copy_options::none,
                    copyError
                );
                if (!copied) {
                    ++failedCount;
                    std::cout << "failed-copy "
                              << asset.expectedShp
                              << " "
                              << copyError.message()
                              << "\n";
                    continue;
                }

                ++copiedLooseCount;
                std::cout << "copied-loose "
                          << asset.expectedShp
                          << " "
                          << *sourcePath
                          << " "
                          << outputPath.string()
                          << "\n";
                continue;
            }
        }

        const std::uint32_t fileId = mixFileIdFromName(asset.expectedShp);
        const LoadedMixArchive* sourceArchive = nullptr;
        const MixEntry* sourceEntry = nullptr;

        for (const LoadedMixArchive& loadedArchive : archives) {
            const MixEntry* entry = loadedArchive.archive.findEntryById(fileId);
            if (entry == nullptr) {
                continue;
            }

            sourceArchive = &loadedArchive;
            sourceEntry = entry;
            break;
        }

        if (sourceArchive == nullptr || sourceEntry == nullptr) {
            ++notFoundCount;
            std::cout << "not-found "
                      << asset.expectedShp
                      << " id="
                      << formatMixId(fileId)
                      << "\n";
            continue;
        }

        std::string errorMessage;
        if (!sourceArchive->archive.extractEntryToFile(
                *sourceEntry,
                outputPath.string(),
                errorMessage
            )) {
            ++failedCount;
            std::cout << "failed "
                      << asset.expectedShp
                      << " "
                      << errorMessage
                      << "\n";
            continue;
        }

        ++extractedCount;
        std::cout << "extracted "
                  << asset.expectedShp
                  << " "
                  << sourceArchive->path
                  << " "
                  << outputPath.string()
                  << "\n";
    }

    std::cout << "\nSummary:\n";
    std::cout << "Already present: " << alreadyPresentCount << "\n";
    std::cout << "Copied loose:    " << copiedLooseCount << "\n";
    std::cout << "Extracted:       " << extractedCount << "\n";
    std::cout << "Not found:       " << notFoundCount << "\n";
    std::cout << "Skipped:         " << skippedCount << "\n";
    std::cout << "Failed:          " << failedCount << "\n";

    return failedCount == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    configureCommandLineErrorReporting();

    const CommandLineOptions options = parseCommandLine(argc, argv);

    if (options.showHelp) {
        printHelp(std::cout, options.programName);
        return 0;
    }

    if (!options.errorMessage.empty()) {
        std::cerr << "Error: " << options.errorMessage << "\n\n";
        printHelp(std::cerr, options.programName);
        return 1;
    }

    if (options.mode == CommandMode::PerformancePlan) {
        PerformancePlanConfig config;
        config.requestedWorkerThreads =
            parsePositiveIntOrDefault(options.workerCount, 0);

        const PerformancePlanReport report = buildPerformancePlan(config);
        printPerformancePlanReport(report);
        return 0;
    }

    if (options.mode == CommandMode::ListMix) {
        MixArchive archive;

        if (!archive.loadFromFile(options.mixPath)) {
            std::cerr << "Failed to load MIX archive: "
                      << archive.getErrorMessage()
                      << "\n";
            return 1;
        }

        printMixArchive(archive);
        return 0;
    }

    if (options.mode == CommandMode::ExtractMixFile) {
        MixArchive archive;

        if (!archive.loadFromFile(options.mixPath)) {
            std::cerr << "Failed to load MIX archive: "
                      << archive.getErrorMessage()
                      << "\n";
            return 1;
        }

        const std::uint32_t fileId = mixFileIdFromName(options.mixFileName);
        const MixEntry* entry = archive.findEntryById(fileId);
        if (entry == nullptr) {
            std::cerr << "MIX file entry not found: "
                      << options.mixFileName
                      << " id=" << formatMixId(fileId)
                      << "\n";
            return 1;
        }

        std::string errorMessage;
        if (!archive.extractEntryToFile(*entry, options.outputPath, errorMessage)) {
            std::cerr << "Failed to extract MIX file: "
                      << errorMessage
                      << "\n";
            return 1;
        }

        std::cout << "Extracted MIX file: " << options.mixFileName << "\n";
        std::cout << "ID:                 " << formatMixId(fileId) << "\n";
        std::cout << "Source MIX:         " << options.mixPath << "\n";
        std::cout << "Offset:             " << entry->offset << "\n";
        std::cout << "Size:               " << entry->size << "\n";
        std::cout << "Output:             " << options.outputPath << "\n";
        return 0;
    }

    if (options.mode == CommandMode::InspectAsset) {
        const AssetInspectionReport report = inspectAssetFile(options.assetPath);
        printAssetInspection(report);
        return report.success ? 0 : 1;
    }

    if (
        options.mode == CommandMode::MapSummary
        || options.mode == CommandMode::DumpMapObjects
        || options.mode == CommandMode::ExportMapPreview
        || options.mode == CommandMode::ExportMapHtml
        || options.mode == CommandMode::ExportMapTmpPreview
        || options.mode == CommandMode::ExportMapTmpHtml
        || options.mode == CommandMode::MapTileReport
        || options.mode == CommandMode::MapObjectArtReport
        || options.mode == CommandMode::ExtractMapObjectArt
        || options.mode == CommandMode::ExtractMapTiles
    ) {
        IniFile mapIni;

        if (!mapIni.loadFromFile(options.mapPath)) {
            return 1;
        }

        IniFile rulesIni;
        const IniFile* rulesIniForMap = nullptr;
        if (!options.rulesPath.empty()) {
            if (!rulesIni.loadFromFile(options.rulesPath)) {
                return 1;
            }

            rulesIniForMap = &rulesIni;
        }

        MapDatabase mapDatabase;
        if (!mapDatabase.loadFromIni(mapIni, rulesIniForMap)) {
            std::cerr << "Failed to load map database.\n";
            return 1;
        }

        std::cout << "Loaded map file: " << options.mapPath << "\n";
        if (rulesIniForMap != nullptr) {
            std::cout << "Loaded rules file: " << options.rulesPath << "\n";
        }

        if (options.mode == CommandMode::MapSummary) {
            printMapSummary(mapDatabase);
        } else if (options.mode == CommandMode::DumpMapObjects) {
            printMapObjects(mapDatabase);
        } else if (options.mode == CommandMode::MapObjectArtReport) {
            return printMapObjectArtReport(
                options,
                mapDatabase,
                rulesIni
            );
        } else if (options.mode == CommandMode::ExtractMapObjectArt) {
            return extractMapObjectArtFiles(
                options,
                mapDatabase,
                rulesIni
            );
        } else if (options.mode == CommandMode::ExtractMapTiles) {
            return extractMapTileFiles(options, mapDatabase);
        } else if (options.mode == CommandMode::MapTileReport) {
            const std::string resolvedTheaterPath =
                resolveTheaterPathForMap(options, mapDatabase);

            if (resolvedTheaterPath.empty()) {
                std::cerr << "Failed to choose theater INI for map Theater="
                          << (mapDatabase.getSummary().theater.empty()
                              ? "-"
                              : mapDatabase.getSummary().theater)
                          << ". Use --theater <path> explicitly.\n";
                return 1;
            }

            IniFile theaterIni;
            if (!theaterIni.loadFromFile(resolvedTheaterPath)) {
                return 1;
            }

            TheaterDatabase theaterDatabase;
            if (!theaterDatabase.loadFromIni(theaterIni, resolvedTheaterPath)) {
                std::cerr << "Failed to load theater database.\n";
                return 1;
            }

            std::cout << "Loaded theater file: " << resolvedTheaterPath << "\n";

            const MapTileUsageReport report =
                theaterDatabase.buildMapTileUsageReport(
                    mapDatabase,
                    options.tileRootPath
                );

            printMapTileUsageReport(
                mapDatabase,
                theaterDatabase,
                report,
                !options.tileRootPath.empty()
            );
        } else if (
            options.mode == CommandMode::ExportMapTmpPreview
            || options.mode == CommandMode::ExportMapTmpHtml
        ) {
            const std::string resolvedTheaterPath =
                resolveTheaterPathForMap(options, mapDatabase);

            if (resolvedTheaterPath.empty()) {
                std::cerr << "Failed to choose theater INI for map Theater="
                          << (mapDatabase.getSummary().theater.empty()
                              ? "-"
                              : mapDatabase.getSummary().theater)
                          << ". Use --theater <path> explicitly.\n";
                return 1;
            }

            IniFile theaterIni;
            if (!theaterIni.loadFromFile(resolvedTheaterPath)) {
                return 1;
            }

            TheaterDatabase theaterDatabase;
            if (!theaterDatabase.loadFromIni(theaterIni, resolvedTheaterPath)) {
                std::cerr << "Failed to load theater database.\n";
                return 1;
            }

            const std::string resolvedPalettePath =
                resolvePalettePathForMap(options, mapDatabase);
            if (resolvedPalettePath.empty()) {
                std::cerr << "Failed to choose palette for map Theater="
                          << (mapDatabase.getSummary().theater.empty()
                              ? "-"
                              : mapDatabase.getSummary().theater)
                          << ". Use --palette <path> explicitly.\n";
                return 1;
            }

            std::cout << "Loaded theater file: " << resolvedTheaterPath << "\n";
            std::cout << "Loaded palette file: " << resolvedPalettePath << "\n";

            std::string errorMessage;
            if (options.mode == CommandMode::ExportMapTmpPreview) {
                if (!exportMapTmpPreviewToPpm(
                        options.exportMapTmpPreviewPath,
                        mapDatabase,
                        theaterDatabase,
                        options.tileRootPath,
                        resolvedPalettePath,
                        errorMessage
                    )) {
                    std::cerr << "Failed to export map TMP preview: "
                              << errorMessage
                              << "\n";
                    return 1;
                }

                std::cout << "Exported map TMP preview: "
                          << options.exportMapTmpPreviewPath
                          << "\n";
            } else {
                MapStructureSpriteOptions structureSpriteOptions;
                MapStructureSpriteStats structureSpriteStats;
                RulesDatabase structureRules;
                ArtDatabase structureArt;

                const bool exportStructureSprites =
                    !options.rulesPath.empty()
                    && !options.artPath.empty()
                    && !options.assetRootPath.empty();

                if (exportStructureSprites) {
                    if (!structureRules.loadFromIni(rulesIni)) {
                        std::cerr << "Failed to load rules database for structure sprites.\n";
                        return 1;
                    }

                    if (!loadArt(options.artPath, structureArt)) {
                        return 1;
                    }

                    structureSpriteOptions.rules = &structureRules;
                    structureSpriteOptions.art = &structureArt;
                    structureSpriteOptions.assetRootPath = options.assetRootPath;
                    structureSpriteOptions.structurePalettePath =
                        resolveStructurePalettePathForMap(options, mapDatabase);

                    std::cout << "Loaded ART file: " << options.artPath << "\n";
                    std::cout << "Scanned structure asset root: "
                              << options.assetRootPath
                              << "\n";
                    if (structureSpriteOptions.structurePalettePath.empty()) {
                        std::cout
                            << "Structure palette: fallback to map terrain palette\n";
                    } else {
                        std::cout << "Loaded structure palette file: "
                                  << structureSpriteOptions.structurePalettePath
                                  << "\n";
                    }
                }

                concord::assets::MapPreviewResourceContext previewContext;
                previewContext.packageRoot = std::filesystem::current_path().string();
                previewContext.tileRootPath = options.tileRootPath;
                previewContext.assetRootPath = structureSpriteOptions.assetRootPath;
                previewContext.palettePath = resolvedPalettePath;
                previewContext.structurePalettePath = structureSpriteOptions.structurePalettePath;
                concord::assets::MapPreviewResourceSession previewSession(std::move(previewContext));
                const bool exported = exportMapTmpPreviewToHtml(
                    previewSession,
                    options.exportMapTmpHtmlPath,
                    mapDatabase,
                    theaterDatabase,
                    options.tileRootPath,
                    resolvedPalettePath,
                    structureSpriteOptions,
                    structureSpriteStats,
                    errorMessage);

                if (!exported) {
                    std::cerr << "Failed to export map TMP HTML preview: "
                              << errorMessage
                              << "\n";
                    return 1;
                }

                std::cout << "Exported map TMP HTML preview: "
                          << options.exportMapTmpHtmlPath
                          << "\n";

                if (exportStructureSprites) {
                    std::cout << "Structure sprite types: "
                              << structureSpriteStats.uniqueStructureTypes
                              << "\n";
                    std::cout << "Embedded structure sprites: "
                              << structureSpriteStats.embeddedSprites
                              << "\n";
                    std::cout << "Missing structure sprites: "
                              << structureSpriteStats.missingSprites
                              << "\n";
                    std::cout << "Structure sprite decode failures: "
                              << structureSpriteStats.decodeFailures
                              << "\n";
                    if (!structureSpriteStats.warnings.empty()) {
                        std::cout << "Structure sprite warnings:\n";
                        for (const std::string& warning : structureSpriteStats.warnings) {
                            std::cout << "- " << warning << "\n";
                        }
                    }
                }
            }
        } else if (options.mode == CommandMode::ExportMapPreview) {
            std::string errorMessage;
            if (!exportMapPreviewToPpm(
                    options.exportMapPreviewPath,
                    mapDatabase,
                    errorMessage
                )) {
                std::cerr << "Failed to export map preview: " << errorMessage << "\n";
                return 1;
            }

            std::cout << "Exported map preview: "
                      << options.exportMapPreviewPath
                      << "\n";
        } else {
            std::string errorMessage;
            if (!exportMapPreviewToHtml(
                    options.exportMapHtmlPath,
                    mapDatabase,
                    errorMessage
                )) {
                std::cerr << "Failed to export map HTML preview: "
                          << errorMessage
                          << "\n";
                return 1;
            }

            std::cout << "Exported map HTML preview: "
                      << options.exportMapHtmlPath
                      << "\n";
        }

        return 0;
    }

    if (options.mode == CommandMode::InspectVoxelPair) {
        const AssetInspectionReport report = inspectVxlHvaPair(
            options.assetPath,
            options.hvaPath
        );
        printAssetInspection(report);
        return report.success ? 0 : 1;
    }

    if (options.mode == CommandMode::ExportFrame) {
        std::string errorMessage;

        if (!exportShpFirstFrameToPpm(
                options.assetPath,
                options.palettePath,
                options.exportFramePath,
                errorMessage
            )) {
            std::cerr << "Failed to export SHP frame: " << errorMessage << "\n";
            return 1;
        }

        std::cout << "Exported SHP frame: " << options.exportFramePath << "\n";
        return 0;
    }

    if (options.mode == CommandMode::ExportTmpTile) {
        std::string errorMessage;

        if (!exportTmpFirstTileToPpm(
                options.assetPath,
                options.palettePath,
                options.exportTmpTilePath,
                errorMessage
            )) {
            std::cerr << "Failed to export TMP tile: " << errorMessage << "\n";
            return 1;
        }

        std::cout << "Exported TMP tile: " << options.exportTmpTilePath << "\n";
        return 0;
    }

    IniFile ini;
    RulesDatabase rules;

    if (!loadRules(options.rulesPath, ini, rules)) {
        return 1;
    }

    std::cout << "Loaded INI file: " << options.rulesPath << "\n";

    switch (options.mode) {
    case CommandMode::Summary:
        printSummary(rules);
        break;

    case CommandMode::Validate:
        printDiagnostics(ObjectReferenceValidator().validate(ini, rules));
        break;

    case CommandMode::DumpUnits:
        printAllUnits(rules);
        break;

    case CommandMode::DumpWeapons:
        printWeaponList(rules.getWeaponTypes());
        break;

    case CommandMode::CombatSandbox:
    {
        CombatSandboxConfig config;
        config.attackerId = options.combatAttackerId;
        config.defenderId = options.combatDefenderId;
        config.maxTicks = parsePositiveIntOrDefault(options.combatTicks, 600);
        config.startDistanceCells =
            parsePositiveIntOrDefault(options.combatDistance, 8);

        const CombatSandboxReport report = runCombatSandbox(rules, config);
        printCombatSandboxReport(report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::EconomySandbox:
    {
        EconomySandboxConfig config;
        config.houseId = options.economyHouseId;
        config.mcvId = options.economyMcvId;
        config.infantryId = options.economyInfantryId;
        config.vehicleId = options.economyVehicleId;
        config.startingCredits =
            parsePositiveIntOrDefault(options.economyCredits, 10000);
        config.maxTicks = parsePositiveIntOrDefault(options.combatTicks, 600);

        const EconomySandboxReport report = runEconomySandbox(rules, config);
        printEconomySandboxReport(report);
        if (!report.success || !report.objectiveComplete) {
            return 1;
        }
        break;
    }

    case CommandMode::AiTriggerSandbox:
    {
        IniFile mapIni;
        const IniFile* mapIniForAi = nullptr;
        if (!options.mapPath.empty()) {
            if (!mapIni.loadFromFile(options.mapPath)) {
                return 1;
            }
            mapIniForAi = &mapIni;
            std::cout << "Loaded map file: " << options.mapPath << "\n";
        }

        AiTriggerSandboxConfig config;
        config.aiHouseId = options.aiHouseId;
        config.enemyHouseId = options.enemyHouseId;
        config.mcvId = options.economyMcvId;
        config.startingCredits =
            parsePositiveIntOrDefault(options.economyCredits, 10000);
        config.maxTicks = parsePositiveIntOrDefault(options.combatTicks, 900);
        config.attackWaveSize =
            parsePositiveIntOrDefault(options.attackWaveSize, 3);

        const AiTriggerSandboxReport report =
            runAiTriggerSandbox(rules, mapIniForAi, config);
        printAiTriggerSandboxReport(report);
        if (!report.success
            || !report.economyComplete
            || !report.attackWaveLaunched
            || !report.victoryTriggerFired) {
            return 1;
        }
        break;
    }

    case CommandMode::ModCompatReport:
    {
        IniFile artIni;
        IniFile mapIni;
        const IniFile* artIniForReport = nullptr;
        const IniFile* mapIniForReport = nullptr;

        if (!options.artPath.empty()) {
            if (!artIni.loadFromFile(options.artPath)) {
                return 1;
            }
            artIniForReport = &artIni;
            std::cout << "Loaded ART file: " << options.artPath << "\n";
        }

        if (!options.mapPath.empty()) {
            if (!mapIni.loadFromFile(options.mapPath)) {
                return 1;
            }
            mapIniForReport = &mapIni;
            std::cout << "Loaded map file: " << options.mapPath << "\n";
        }

        ModCompatibilityScanOptions scanOptions;
        scanOptions.hasArtIni = !options.artPath.empty();
        scanOptions.hasMapIni = !options.mapPath.empty();

        const ModCompatibilityReport report =
            ModCompatibilityScanner().scan(
                ini,
                rules,
                artIniForReport,
                mapIniForReport,
                scanOptions
            );
        printModCompatibilityReport(report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::EngineStatus:
    {
        ArtDatabase artDatabase;
        ArtDatabase* artForStatus = nullptr;
        if (!options.artPath.empty()) {
            if (!loadArt(options.artPath, artDatabase)) {
                return 1;
            }
            artForStatus = &artDatabase;
            std::cout << "Loaded ART file: " << options.artPath << "\n";
        }

        IniFile mapIni;
        MapDatabase mapDatabase;
        MapDatabase* mapForStatus = nullptr;
        if (!options.mapPath.empty()) {
            if (!mapIni.loadFromFile(options.mapPath)) {
                return 1;
            }
            if (!mapDatabase.loadFromIni(mapIni, &ini)) {
                std::cerr << "Failed to load map database.\n";
                return 1;
            }
            mapForStatus = &mapDatabase;
            std::cout << "Loaded map file: " << options.mapPath << "\n";
        }

        const std::string assetRoot =
            options.assetRootPath.empty()
                ? defaultAssetRootForRules(options.rulesPath)
                : options.assetRootPath;
        const std::string soundPath = findSoundIniForEngineStatus(assetRoot);
        IniFile soundIni;
        IniFile* soundForStatus = nullptr;
        if (!soundPath.empty() && soundIni.loadFromFile(soundPath)) {
            soundForStatus = &soundIni;
        }

        const std::string evaPath = findEvaIniForEngineStatus(assetRoot);
        IniFile evaIni;
        IniFile* evaForStatus = nullptr;
        if (!evaPath.empty() && evaIni.loadFromFile(evaPath)) {
            evaForStatus = &evaIni;
        }

        CampaignDatabaseOptions campaignOptions;
        campaignOptions.resourceRootPath = assetRoot;
        campaignOptions.localDataRootPath =
            std::filesystem::path(options.rulesPath).parent_path().string();
        const CampaignDatabase campaignDatabase =
            loadCampaignDatabase(campaignOptions);

        EngineStatusInput statusInput;
        statusInput.projectRootPath = std::filesystem::current_path().string();
        statusInput.assetRootPath = assetRoot;
        statusInput.rulesPath = options.rulesPath;
        statusInput.artPath = options.artPath;
        statusInput.mapPath = options.mapPath;
        statusInput.soundPath = soundPath;
        statusInput.evaPath = evaPath;
        statusInput.rules = &rules;
        statusInput.art = artForStatus;
        statusInput.map = mapForStatus;
        statusInput.soundIni = soundForStatus;
        statusInput.evaIni = evaForStatus;
        statusInput.campaign = &campaignDatabase;

        printEngineStatusReport(
            std::cout,
            buildEngineStatusReport(statusInput)
        );
        break;
    }

    case CommandMode::TechTreeReport:
    {
        printTechTreeReport(
            std::cout,
            analyzeTechTree(rules, options.economyHouseId)
        );
        break;
    }

    case CommandMode::VictorySandbox:
    {
        IniFile mapIni;
        if (!mapIni.loadFromFile(options.mapPath)) {
            return 1;
        }

        MapDatabase mapDatabase;
        if (!mapDatabase.loadFromIni(mapIni, &ini)) {
            std::cerr << "Failed to load map database.\n";
            return 1;
        }

        GameWorld world;
        if (!world.loadFromMap(mapDatabase)) {
            std::cerr << "Failed to load game world.\n";
            return 1;
        }

        std::cout << "Loaded map file: " << options.mapPath << "\n";
        const VictoryConditionReport report =
            buildVictoryConditionReport(
                world,
                mapDatabase,
                options.economyHouseId
            );
        printVictoryConditionReport(std::cout, report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::PathfindingSandbox:
    {
        IniFile mapIni;
        if (!mapIni.loadFromFile(options.mapPath)) {
            return 1;
        }

        MapDatabase mapDatabase;
        if (!mapDatabase.loadFromIni(mapIni, &ini)) {
            std::cerr << "Failed to load map database.\n";
            return 1;
        }

        GameWorld world;
        if (!world.loadFromMap(mapDatabase)) {
            std::cerr << "Failed to load game world.\n";
            return 1;
        }

        std::cout << "Loaded map file: " << options.mapPath << "\n";
        const PathfindingSandboxReport report = runPathfindingSandbox(world);
        printPathfindingSandboxReport(std::cout, report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::AudioReport:
    {
        const std::string assetRoot =
            options.assetRootPath.empty()
                ? defaultAssetRootForRules(options.rulesPath)
                : options.assetRootPath;

        const std::string soundPath = findSoundIniForEngineStatus(assetRoot);
        IniFile soundIni;
        IniFile* soundForReport = nullptr;
        if (!soundPath.empty() && soundIni.loadFromFile(soundPath)) {
            soundForReport = &soundIni;
        }

        const std::string evaPath = findEvaIniForEngineStatus(assetRoot);
        IniFile evaIni;
        IniFile* evaForReport = nullptr;
        if (!evaPath.empty() && evaIni.loadFromFile(evaPath)) {
            evaForReport = &evaIni;
        }

        const AudioCatalogReport report = buildAudioCatalogReport(
            soundForReport,
            soundPath,
            evaForReport,
            evaPath,
            assetRoot
        );
        printAudioCatalogReport(std::cout, report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::AudioDecodeSmoke:
    {
        const std::string assetRoot =
            options.assetRootPath.empty()
                ? defaultAssetRootForRules(options.rulesPath)
                : options.assetRootPath;
        const std::string cacheRoot =
            options.outputPath.empty()
                ? (std::filesystem::current_path()
                    / "build"
                    / "audio_decode_smoke_cache").generic_string()
                : options.outputPath;

        const std::string soundPath = findSoundIniForEngineStatus(assetRoot);
        IniFile soundIni;
        IniFile* soundForSmoke = nullptr;
        if (!soundPath.empty() && soundIni.loadFromFile(soundPath)) {
            soundForSmoke = &soundIni;
        }

        const AudioDecodeSmokeReport report =
            runAudioDecodeSmoke(assetRoot, cacheRoot, soundForSmoke);
        printAudioDecodeSmokeReport(std::cout, report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::ReplaySandbox:
    {
        ReplaySandboxConfig config;
        config.attackerId = options.combatAttackerId;
        config.defenderId = options.combatDefenderId;
        config.maxTicks = parsePositiveIntOrDefault(options.combatTicks, 600);
        config.startDistanceCells =
            parsePositiveIntOrDefault(options.combatDistance, 8);
        config.randomSeed =
            parseNonNegativeIntOrDefault(options.randomSeed, 12345);
        config.desyncAtTick =
            parseNonNegativeIntOrDefault(options.desyncTick, -1);
        config.outputPath = options.outputPath;

        const ReplaySandboxReport report =
            options.runtimeReplayEventPath.empty()
                ? runReplaySandbox(rules, config)
                : runReplaySandboxWithRuntimeEventFile(
                    rules,
                    config,
                    options.runtimeReplayEventPath
                );
        printReplaySandboxReport(report);
        if (!report.success) {
            return 1;
        }
        break;
    }

    case CommandMode::DumpArt:
    {
        ArtDatabase artDatabase;

        if (!loadArt(options.artPath, artDatabase)) {
            return 1;
        }

        std::cout << "Loaded ART file: " << options.artPath << "\n";
        printArtLinks(artDatabase, rules);
        break;
    }

    case CommandMode::ExportJson:
    {
        const DiagnosticReport diagnostics =
            ObjectReferenceValidator().validate(ini, rules);
        std::string errorMessage;
        ArtDatabase artDatabase;
        const ArtDatabase* artDatabaseForExport = nullptr;

        if (!options.artPath.empty()) {
            if (!loadArt(options.artPath, artDatabase)) {
                return 1;
            }

            std::cout << "Loaded ART file: " << options.artPath << "\n";
            artDatabaseForExport = &artDatabase;
        }

        if (!exportRulesToJson(
                options.exportJsonPath,
                rules,
                diagnostics,
                artDatabaseForExport,
                errorMessage
            )) {
            std::cerr << "Failed to export JSON: " << errorMessage << "\n";
            return 1;
        }

        std::cout << "Exported JSON: " << options.exportJsonPath << "\n";
        break;
    }

    case CommandMode::ListMix:
        std::cerr << "Unexpected MIX command dispatch.\n";
        return 1;

    case CommandMode::ExtractMixFile:
        std::cerr << "Unexpected MIX extract command dispatch.\n";
        return 1;

    case CommandMode::PerformancePlan:
        std::cerr << "Unexpected performance plan command dispatch.\n";
        return 1;

    case CommandMode::MapSummary:
        std::cerr << "Unexpected map command dispatch.\n";
        return 1;

    case CommandMode::DumpMapObjects:
        std::cerr << "Unexpected map object command dispatch.\n";
        return 1;

    case CommandMode::ExportMapPreview:
        std::cerr << "Unexpected map preview export command dispatch.\n";
        return 1;

    case CommandMode::ExportMapHtml:
        std::cerr << "Unexpected map HTML preview export command dispatch.\n";
        return 1;

    case CommandMode::ExportMapTmpPreview:
        std::cerr << "Unexpected map TMP preview export command dispatch.\n";
        return 1;

    case CommandMode::ExportMapTmpHtml:
        std::cerr << "Unexpected map TMP HTML preview export command dispatch.\n";
        return 1;

    case CommandMode::MapTileReport:
        std::cerr << "Unexpected map tile report command dispatch.\n";
        return 1;

    case CommandMode::MapObjectArtReport:
        std::cerr << "Unexpected map object art report command dispatch.\n";
        return 1;

    case CommandMode::ExtractMapTiles:
        std::cerr << "Unexpected map tile extract command dispatch.\n";
        return 1;

    case CommandMode::ExtractMapObjectArt:
        std::cerr << "Unexpected map object art extract command dispatch.\n";
        return 1;

    case CommandMode::InspectAsset:
        std::cerr << "Unexpected asset command dispatch.\n";
        return 1;

    case CommandMode::InspectVoxelPair:
        std::cerr << "Unexpected VXL/HVA command dispatch.\n";
        return 1;

    case CommandMode::ExportFrame:
        std::cerr << "Unexpected SHP frame export command dispatch.\n";
        return 1;

    case CommandMode::ExportTmpTile:
        std::cerr << "Unexpected TMP tile export command dispatch.\n";
        return 1;

    case CommandMode::None:
        std::cerr << "No command mode selected.\n";
        return 1;
    }

    return 0;
}
