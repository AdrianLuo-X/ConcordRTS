#include "analysis/cli/CommandLineOptions.h"

#include <iostream>
#include <string>

namespace {

bool setMode(
    CommandLineOptions& options,
    CommandMode mode,
    const std::string& optionName
) {
    if (options.mode != CommandMode::None) {
        options.errorMessage =
            "Only one action can be used at a time. Already selected another action before "
            + optionName + ".";
        return false;
    }

    options.mode = mode;
    return true;
}

bool readRequiredValue(
    int argc,
    char** argv,
    int& index,
    const std::string& optionName,
    std::string& value,
    std::string& errorMessage
) {
    if (index + 1 >= argc) {
        errorMessage = optionName + " requires a value.";
        return false;
    }

    const std::string nextValue = argv[index + 1];
    if (nextValue.rfind("--", 0) == 0) {
        errorMessage = optionName + " requires a value, but got another option.";
        return false;
    }

    value = nextValue;
    ++index;
    return true;
}

} // namespace

CommandLineOptions parseCommandLine(int argc, char** argv) {
    CommandLineOptions options;

    if (argc >= 1 && argv[0] != nullptr) {
        options.programName = argv[0];
    } else {
        options.programName = "concord_rules_dump";
    }

    if (argc <= 1) {
        options.showHelp = true;
        return options;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
            return options;
        }

        if (argument == "--overwrite") {
            options.overwriteOutput = true;
            continue;
        }

        if (argument == "--rules") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--rules",
                    options.rulesPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--art") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--art",
                    options.artPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--map") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--map",
                    options.mapPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--theater") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--theater",
                    options.theaterPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--theater-dir") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--theater-dir",
                    options.theaterDirPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--tile-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--tile-root",
                    options.tileRootPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--mix") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--mix",
                    options.mixPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--mix-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--mix-root",
                    options.mixRootPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--report-json") {
            if (!readRequiredValue(argc, argv, i, "--report-json",
                    options.reportJsonPath, options.errorMessage)) {
                return options;
            }
            continue;
        }

        if (argument == "--output") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--output",
                    options.outputPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--asset") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--asset",
                    options.assetPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--asset-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--asset-root",
                    options.assetRootPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--hva") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--hva",
                    options.hvaPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--palette") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--palette",
                    options.palettePath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--palette-dir") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--palette-dir",
                    options.paletteDirPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--summary") {
            if (!setMode(options, CommandMode::Summary, "--summary")) {
                return options;
            }
            continue;
        }

        if (argument == "--validate") {
            if (!setMode(options, CommandMode::Validate, "--validate")) {
                return options;
            }
            continue;
        }

        if (argument == "--dump-units") {
            if (!setMode(options, CommandMode::DumpUnits, "--dump-units")) {
                return options;
            }
            continue;
        }

        if (argument == "--dump-weapons") {
            if (!setMode(options, CommandMode::DumpWeapons, "--dump-weapons")) {
                return options;
            }
            continue;
        }

        if (argument == "--combat-sandbox") {
            if (!setMode(options, CommandMode::CombatSandbox, "--combat-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--economy-sandbox") {
            if (!setMode(options, CommandMode::EconomySandbox, "--economy-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--ai-trigger-sandbox") {
            if (!setMode(options, CommandMode::AiTriggerSandbox, "--ai-trigger-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--mod-compat-report") {
            if (!setMode(options, CommandMode::ModCompatReport, "--mod-compat-report")) {
                return options;
            }
            continue;
        }

        if (argument == "--engine-status") {
            if (!setMode(options, CommandMode::EngineStatus, "--engine-status")) {
                return options;
            }
            continue;
        }

        if (argument == "--tech-tree-report") {
            if (!setMode(options, CommandMode::TechTreeReport, "--tech-tree-report")) {
                return options;
            }
            continue;
        }

        if (argument == "--victory-sandbox") {
            if (!setMode(options, CommandMode::VictorySandbox, "--victory-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--pathfinding-sandbox") {
            if (!setMode(options, CommandMode::PathfindingSandbox, "--pathfinding-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--audio-report") {
            if (!setMode(options, CommandMode::AudioReport, "--audio-report")) {
                return options;
            }
            continue;
        }

        if (argument == "--audio-decode-smoke") {
            if (!setMode(options, CommandMode::AudioDecodeSmoke, "--audio-decode-smoke")) {
                return options;
            }
            continue;
        }

        if (argument == "--performance-plan") {
            if (!setMode(options, CommandMode::PerformancePlan, "--performance-plan")) {
                return options;
            }
            continue;
        }

        if (argument == "--replay-sandbox") {
            if (!setMode(options, CommandMode::ReplaySandbox, "--replay-sandbox")) {
                return options;
            }
            continue;
        }

        if (argument == "--runtime-events") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--runtime-events",
                    options.runtimeReplayEventPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--attacker") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--attacker",
                    options.combatAttackerId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--defender") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--defender",
                    options.combatDefenderId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--ticks") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--ticks",
                    options.combatTicks,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--distance") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--distance",
                    options.combatDistance,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--house") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--house",
                    options.economyHouseId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--mcv") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--mcv",
                    options.economyMcvId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--economy-infantry") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--economy-infantry",
                    options.economyInfantryId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--economy-vehicle") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--economy-vehicle",
                    options.economyVehicleId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--credits") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--credits",
                    options.economyCredits,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--ai-house") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--ai-house",
                    options.aiHouseId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--enemy-house") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--enemy-house",
                    options.enemyHouseId,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--wave-size") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--wave-size",
                    options.attackWaveSize,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--workers") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--workers",
                    options.workerCount,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--seed") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--seed",
                    options.randomSeed,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--desync-at") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--desync-at",
                    options.desyncTick,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--dump-art") {
            if (!setMode(options, CommandMode::DumpArt, "--dump-art")) {
                return options;
            }
            continue;
        }

        if (argument == "--map-summary") {
            if (!setMode(options, CommandMode::MapSummary, "--map-summary")) {
                return options;
            }
            continue;
        }

        if (argument == "--dump-map-objects") {
            if (!setMode(options, CommandMode::DumpMapObjects, "--dump-map-objects")) {
                return options;
            }
            continue;
        }

        if (argument == "--export-map-preview") {
            if (!setMode(options, CommandMode::ExportMapPreview, "--export-map-preview")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-map-preview",
                    options.exportMapPreviewPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--export-map-html") {
            if (!setMode(options, CommandMode::ExportMapHtml, "--export-map-html")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-map-html",
                    options.exportMapHtmlPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--export-map-tmp-preview") {
            if (!setMode(options, CommandMode::ExportMapTmpPreview, "--export-map-tmp-preview")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-map-tmp-preview",
                    options.exportMapTmpPreviewPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--export-map-tmp-html") {
            if (!setMode(options, CommandMode::ExportMapTmpHtml, "--export-map-tmp-html")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-map-tmp-html",
                    options.exportMapTmpHtmlPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--map-tile-report") {
            if (!setMode(options, CommandMode::MapTileReport, "--map-tile-report")) {
                return options;
            }
            continue;
        }

        if (argument == "--map-object-art-report") {
            if (!setMode(options, CommandMode::MapObjectArtReport, "--map-object-art-report")) {
                return options;
            }
            continue;
        }

        if (argument == "--extract-map-tiles") {
            if (!setMode(options, CommandMode::ExtractMapTiles, "--extract-map-tiles")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--extract-map-tiles",
                    options.extractMapTilesPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--extract-map-object-art") {
            if (!setMode(options, CommandMode::ExtractMapObjectArt, "--extract-map-object-art")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--extract-map-object-art",
                    options.extractMapObjectArtPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--list-mix") {
            if (!setMode(options, CommandMode::ListMix, "--list-mix")) {
                return options;
            }
            continue;
        }

        if (argument == "--extract-mix-file") {
            if (!setMode(options, CommandMode::ExtractMixFile, "--extract-mix-file")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--extract-mix-file",
                    options.mixFileName,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--inspect-asset") {
            if (!setMode(options, CommandMode::InspectAsset, "--inspect-asset")) {
                return options;
            }
            continue;
        }

        if (argument == "--inspect-vxl-hva") {
            if (!setMode(options, CommandMode::InspectVoxelPair, "--inspect-vxl-hva")) {
                return options;
            }
            continue;
        }

        if (argument == "--export-frame") {
            if (!setMode(options, CommandMode::ExportFrame, "--export-frame")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-frame",
                    options.exportFramePath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--export-tmp-tile") {
            if (!setMode(options, CommandMode::ExportTmpTile, "--export-tmp-tile")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-tmp-tile",
                    options.exportTmpTilePath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        if (argument == "--export-json") {
            if (!setMode(options, CommandMode::ExportJson, "--export-json")) {
                return options;
            }

            if (!readRequiredValue(
                    argc,
                    argv,
                    i,
                    "--export-json",
                    options.exportJsonPath,
                    options.errorMessage
                )) {
                return options;
            }
            continue;
        }

        options.errorMessage =
            "Unknown argument: " + argument + ". Use --help to see supported options.";
        return options;
    }

    if (!options.reportJsonPath.empty()
        && options.mode != CommandMode::EngineStatus
        && options.mode != CommandMode::ReplaySandbox) {
        options.errorMessage = "--report-json requires --engine-status or --replay-sandbox.";
        return options;
    }

    if (options.mode == CommandMode::DumpMapObjects) {
        if (options.mapPath.empty()) {
            options.errorMessage = "Missing required option for --dump-map-objects: --map <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::MapSummary) {
        if (options.mapPath.empty()) {
            options.errorMessage = "Missing required option for --map-summary: --map <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExportMapPreview) {
        if (options.mapPath.empty()) {
            options.errorMessage = "Missing required option for --export-map-preview: --map <path>.";
            return options;
        }

        if (options.exportMapPreviewPath.empty()) {
            options.errorMessage =
                "Missing required output path for --export-map-preview <ppm>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExportMapHtml) {
        if (options.mapPath.empty()) {
            options.errorMessage = "Missing required option for --export-map-html: --map <path>.";
            return options;
        }

        if (options.exportMapHtmlPath.empty()) {
            options.errorMessage =
                "Missing required output path for --export-map-html <html>.";
            return options;
        }

        return options;
    }

    if (
        options.mode == CommandMode::ExportMapTmpPreview
        || options.mode == CommandMode::ExportMapTmpHtml
    ) {
        const char* commandName = options.mode == CommandMode::ExportMapTmpPreview
            ? "--export-map-tmp-preview"
            : "--export-map-tmp-html";

        if (options.mapPath.empty()) {
            options.errorMessage =
                std::string("Missing required option for ") + commandName + ": --map <path>.";
            return options;
        }

        if (options.theaterPath.empty() && options.theaterDirPath.empty()) {
            options.errorMessage =
                std::string("Missing required option for ") + commandName + ": --theater <path> "
                "or --theater-dir <dir>.";
            return options;
        }

        if (options.tileRootPath.empty()) {
            options.errorMessage =
                std::string("Missing required option for ") + commandName + ": --tile-root <dir>.";
            return options;
        }

        if (options.palettePath.empty() && options.paletteDirPath.empty()) {
            options.errorMessage =
                std::string("Missing required option for ") + commandName + ": --palette <path> "
                "or --palette-dir <dir>.";
            return options;
        }

        if (
            options.mode == CommandMode::ExportMapTmpPreview
            && options.exportMapTmpPreviewPath.empty()
        ) {
            options.errorMessage =
                "Missing required output path for --export-map-tmp-preview <ppm>.";
            return options;
        }

        if (
            options.mode == CommandMode::ExportMapTmpHtml
            && options.exportMapTmpHtmlPath.empty()
        ) {
            options.errorMessage =
                "Missing required output path for --export-map-tmp-html <html>.";
            return options;
        }

        if (options.mode == CommandMode::ExportMapTmpHtml) {
            const bool wantsStructureSprites =
                !options.rulesPath.empty()
                || !options.artPath.empty()
                || !options.assetRootPath.empty();
            if (wantsStructureSprites) {
                if (options.rulesPath.empty()) {
                    options.errorMessage =
                        "Structure sprite rendering for --export-map-tmp-html requires --rules <path>.";
                    return options;
                }

                if (options.artPath.empty()) {
                    options.errorMessage =
                        "Structure sprite rendering for --export-map-tmp-html requires --art <path>.";
                    return options;
                }

                if (options.assetRootPath.empty()) {
                    options.errorMessage =
                        "Structure sprite rendering for --export-map-tmp-html requires --asset-root <dir>.";
                    return options;
                }
            }
        }

        return options;
    }

    if (options.mode == CommandMode::MapTileReport) {
        if (options.mapPath.empty()) {
            options.errorMessage = "Missing required option for --map-tile-report: --map <path>.";
            return options;
        }

        if (options.theaterPath.empty() && options.theaterDirPath.empty()) {
            options.errorMessage =
                "Missing required option for --map-tile-report: --theater <path> "
                "or --theater-dir <dir>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::MapObjectArtReport) {
        if (options.mapPath.empty()) {
            options.errorMessage =
                "Missing required option for --map-object-art-report: --map <path>.";
            return options;
        }

        if (options.rulesPath.empty()) {
            options.errorMessage =
                "Missing required option for --map-object-art-report: --rules <path>.";
            return options;
        }

        if (options.artPath.empty()) {
            options.errorMessage =
                "Missing required option for --map-object-art-report: --art <path>.";
            return options;
        }

        if (options.assetRootPath.empty()) {
            options.errorMessage =
                "Missing required option for --map-object-art-report: --asset-root <dir>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExtractMapObjectArt) {
        if (options.mapPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-object-art: --map <path>.";
            return options;
        }

        if (options.rulesPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-object-art: --rules <path>.";
            return options;
        }

        if (options.artPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-object-art: --art <path>.";
            return options;
        }

        if (options.mixRootPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-object-art: --mix-root <dir>.";
            return options;
        }

        if (options.extractMapObjectArtPath.empty()) {
            options.errorMessage =
                "Missing required output directory for --extract-map-object-art <dir>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExtractMapTiles) {
        if (options.mapPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-tiles: --map <path>.";
            return options;
        }

        if (options.theaterPath.empty() && options.theaterDirPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-tiles: --theater <path> "
                "or --theater-dir <dir>.";
            return options;
        }

        if (options.mixRootPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-map-tiles: --mix-root <dir>.";
            return options;
        }

        if (options.extractMapTilesPath.empty()) {
            options.errorMessage =
                "Missing required output directory for --extract-map-tiles <dir>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::InspectVoxelPair) {
        if (options.assetPath.empty()) {
            options.errorMessage = "Missing required option for --inspect-vxl-hva: --asset <vxl>.";
            return options;
        }

        if (options.hvaPath.empty()) {
            options.errorMessage = "Missing required option for --inspect-vxl-hva: --hva <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExportFrame) {
        if (options.assetPath.empty()) {
            options.errorMessage = "Missing required option for --export-frame: --asset <path>.";
            return options;
        }

        if (options.palettePath.empty()) {
            options.errorMessage = "Missing required option for --export-frame: --palette <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExportTmpTile) {
        if (options.assetPath.empty()) {
            options.errorMessage = "Missing required option for --export-tmp-tile: --asset <path>.";
            return options;
        }

        if (options.palettePath.empty()) {
            options.errorMessage = "Missing required option for --export-tmp-tile: --palette <path>.";
            return options;
        }

        if (options.exportTmpTilePath.empty()) {
            options.errorMessage =
                "Missing required output path for --export-tmp-tile <ppm>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::InspectAsset) {
        if (options.assetPath.empty()) {
            options.errorMessage = "Missing required option for --inspect-asset: --asset <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ListMix) {
        if (options.mixPath.empty()) {
            options.errorMessage = "Missing required option for --list-mix: --mix <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::ExtractMixFile) {
        if (options.mixPath.empty()) {
            options.errorMessage =
                "Missing required option for --extract-mix-file: --mix <path>.";
            return options;
        }

        if (options.mixFileName.empty()) {
            options.errorMessage =
                "Missing required filename for --extract-mix-file <name>.";
            return options;
        }

        if (options.outputPath.empty()) {
            options.errorMessage =
                "Missing required output path for --extract-mix-file: --output <path>.";
            return options;
        }

        return options;
    }

    if (options.mode == CommandMode::PerformancePlan) {
        return options;
    }

    if (options.rulesPath.empty()) {
        options.errorMessage = "Missing required option: --rules <path>.";
        return options;
    }

    if (options.mode == CommandMode::DumpArt && options.artPath.empty()) {
        options.errorMessage = "Missing required option for --dump-art: --art <path>.";
        return options;
    }

    if (options.mode == CommandMode::VictorySandbox && options.mapPath.empty()) {
        options.errorMessage =
            "Missing required option for --victory-sandbox: --map <path>.";
        return options;
    }

    if (options.mode == CommandMode::PathfindingSandbox && options.mapPath.empty()) {
        options.errorMessage =
            "Missing required option for --pathfinding-sandbox: --map <path>.";
        return options;
    }

    if (options.mode == CommandMode::None) {
        options.errorMessage =
            "Missing action. Choose --summary, --validate, --dump-units, --dump-weapons, "
            "--combat-sandbox, --economy-sandbox, --ai-trigger-sandbox, --mod-compat-report, --engine-status, --tech-tree-report, --victory-sandbox, --pathfinding-sandbox, --audio-report, --audio-decode-smoke, --performance-plan, --replay-sandbox, --dump-art, --map-summary, "
            "--dump-map-objects, --export-map-preview <ppm>, "
            "--export-map-html <html>, --export-map-tmp-preview <ppm>, "
            "--export-map-tmp-html <html>, --map-tile-report, --map-object-art-report, --list-mix, "
            "--extract-mix-file <name>, --extract-map-tiles <dir>, "
            "--extract-map-object-art <dir>, --inspect-asset, "
            "--inspect-vxl-hva, --export-frame <path>, --export-tmp-tile <path>, "
            "or --export-json <path>.";
        return options;
    }

    return options;
}

void printHelp(std::ostream& output, const std::string& programName) {
    output << "ConcordRTS\n";
    output << "RA2/YR rules command-line tool\n\n";

    output << "Usage:\n";
    output << "  " << programName << " --rules <path> --summary\n";
    output << "  " << programName << " --rules <path> --validate\n";
    output << "  " << programName << " --rules <path> --dump-units\n";
    output << "  " << programName << " --rules <path> --dump-weapons\n";
    output << "  " << programName << " --rules <path> --combat-sandbox "
           << "[--attacker MTNK] [--defender HTNK] [--ticks 600] [--distance 8]\n";
    output << "  " << programName << " --rules <path> --economy-sandbox "
           << "[--house Americans] [--mcv AMCV] [--credits 10000] "
           << "[--economy-infantry E1] [--economy-vehicle MTNK] [--ticks 600]\n";
    output << "  " << programName << " --rules <path> [--map <path>] --ai-trigger-sandbox "
           << "[--ai-house Russians] [--enemy-house Americans] [--mcv SMCV] "
           << "[--credits 10000] [--wave-size 3] [--ticks 900]\n";
    output << "  " << programName << " --rules <path> [--art <path>] [--map <path>] "
           << "--mod-compat-report\n";
    output << "  " << programName << " --rules <path> [--art <path>] [--map <path>] "
           << "[--asset-root <dir>] --engine-status\n";
    output << "  " << programName << " --rules <path> --tech-tree-report [--house Americans]\n";
    output << "  " << programName << " --rules <path> --map <path> --victory-sandbox "
           << "[--house Americans]\n";
    output << "  " << programName << " --rules <path> --map <path> --pathfinding-sandbox\n";
    output << "  " << programName << " --rules <path> [--asset-root <dir>] --audio-report\n";
    output << "  " << programName << " --rules <path> [--asset-root <dir>] "
           << "[--output <cache-dir>] --audio-decode-smoke\n";
    output << "  " << programName << " --performance-plan [--workers 4]\n";
    output << "  " << programName << " --rules <path> --replay-sandbox "
           << "[--attacker MTNK] [--defender HTNK] [--ticks 600] "
           << "[--distance 8] [--seed 12345] [--desync-at 120] "
           << "[--runtime-events events.txt] [--output replay.txt]\n";
    output << "  " << programName << " --rules <path> --art <path> --dump-art\n";
    output << "  " << programName << " --rules <path> [--art <path>] --export-json <path>\n\n";
    output << "  " << programName << " --map <path> [--rules <path>] --map-summary\n\n";
    output << "  " << programName << " --map <path> [--rules <path>] --dump-map-objects\n\n";
    output << "  " << programName << " --map <path> [--rules <path>] --export-map-preview <ppm>\n\n";
    output << "  " << programName << " --map <path> [--rules <path>] --export-map-html <html>\n\n";
    output << "  " << programName << " --map <path> --theater-dir <dir> "
           << "--tile-root <dir> --palette <pal> --export-map-tmp-preview <ppm>\n\n";
    output << "  " << programName << " --map <path> --theater-dir <dir> "
           << "--tile-root <dir> --palette-dir <dir> --export-map-tmp-preview <ppm>\n\n";
    output << "  " << programName << " --map <path> --theater-dir <dir> "
           << "--tile-root <dir> --palette-dir <dir> --export-map-tmp-html <html>\n\n";
    output << "  " << programName << " --map <path> --rules <path> --art <path> "
           << "--asset-root <dir> --theater-dir <dir> --tile-root <dir> "
           << "--palette-dir <dir> --export-map-tmp-html <html>\n\n";
    output << "  " << programName << " --map <path> --theater <path> "
           << "[--tile-root <dir>] --map-tile-report\n\n";
    output << "  " << programName << " --map <path> --theater-dir <dir> "
           << "[--tile-root <dir>] --map-tile-report\n\n";
    output << "  " << programName << " --map <path> --rules <path> --art <path> "
           << "--asset-root <dir> --map-object-art-report\n\n";
    output << "  " << programName << " --map <path> --theater-dir <dir> "
           << "--mix-root <dir> [--overwrite] --extract-map-tiles <dir>\n\n";
    output << "  " << programName << " --map <path> --rules <path> --art <path> "
           << "--mix-root <dir> [--overwrite] --extract-map-object-art <dir>\n\n";
    output << "  " << programName << " --mix <path> --list-mix\n\n";
    output << "  " << programName << " --mix <path> --extract-mix-file <name> --output <path>\n\n";
    output << "  " << programName << " --asset <path> --inspect-asset\n\n";
    output << "  " << programName << " --asset <vxl> --hva <hva> --inspect-vxl-hva\n\n";
    output << "  " << programName << " --asset <shp> --palette <pal> --export-frame <ppm>\n\n";
    output << "  " << programName << " --asset <tmp> --palette <pal> --export-tmp-tile <ppm>\n\n";

    output << "Options:\n";
    output << "  --help, -h            Show this help text.\n";
    output << "  --rules <path>        Path to rulesmd.ini or a test rules INI file.\n";
    output << "  --art <path>          Path to artmd.ini or a test art INI file.\n";
    output << "  --map <path>          Path to a RA2/YR map INI file (.map/.yrm/.mpr).\n";
    output << "  --theater <path>      Path to a theater INI file such as temperatmd.ini.\n";
    output << "  --theater-dir <dir>   Directory containing theater INIs; selected from map Theater.\n";
    output << "  --tile-root <dir>     Directory that contains extracted theater tile files.\n";
    output << "  --mix <path>          Path to a MIX archive.\n";
    output << "  --mix-root <dir>      Directory searched recursively for readable MIX archives.\n";
    output << "  --output <path>       Output path used by commands that extract or export data.\n";
    output << "  --overwrite           Replace existing files for extraction commands.\n";
    output << "  --asset <path>        Path to a PAL/SHP/VXL/HVA/TMP-like resource file.\n";
    output << "  --asset-root <dir>    Directory searched recursively for local art assets.\n";
    output << "  --hva <path>          Path to an HVA file paired with a VXL file.\n";
    output << "  --palette <path>      Path to a 256-color RA2/YR PAL file.\n";
    output << "  --palette-dir <dir>   Directory containing theater palettes such as isodes.pal.\n";
    output << "  --summary             Print object counts only.\n";
    output << "  --validate            Print the current validation report.\n";
    output << "  --dump-units          Print infantry, vehicle, and aircraft lists.\n";
    output << "  --dump-weapons        Print the discovered weapon list.\n";
    output << "  --combat-sandbox      Run a deterministic fixed-tick unit duel.\n";
    output << "  --attacker <id>       Attacking unit id for --combat-sandbox; default MTNK.\n";
    output << "  --defender <id>       Defending unit id for --combat-sandbox; default HTNK.\n";
    output << "  --ticks <count>       Max ticks for sandbox commands; default 600.\n";
    output << "  --distance <cells>    Initial unit separation for --combat-sandbox; default 8.\n";
    output << "  --economy-sandbox     Run a deterministic economy and production loop.\n";
    output << "  --house <id>          House name for --economy-sandbox; default Americans.\n";
    output << "  --mcv <id>            MCV unit id for --economy-sandbox; default AMCV.\n";
    output << "  --economy-infantry <id>\n";
    output << "                        Infantry to produce; default follows MCV faction.\n";
    output << "  --economy-vehicle <id>\n";
    output << "                        Vehicle to produce; default follows MCV faction.\n";
    output << "  --credits <amount>    Starting credits for --economy-sandbox; default 10000.\n";
    output << "  --ai-trigger-sandbox  Run a deterministic AI and trigger sandbox.\n";
    output << "  --ai-house <id>       AI house for --ai-trigger-sandbox; default Russians.\n";
    output << "  --enemy-house <id>    Enemy house for --ai-trigger-sandbox; default Americans.\n";
    output << "  --wave-size <count>   Fallback attack wave size; default 3.\n";
    output << "  --mod-compat-report   Scan rules/art/map INIs for MOD compatibility levels and extension tags.\n";
    output << "  --performance-plan    Print the deterministic multicore performance plan.\n";
    output << "  --workers <count>     Worker thread count for --performance-plan; default auto.\n";
    output << "  --replay-sandbox      Run a deterministic replay/lockstep/desync sandbox.\n";
    output << "  --report-json <path>  Also write scoped evidence JSON for --engine-status or --replay-sandbox.\n";
    output << "  --seed <value>        Deterministic random seed for --replay-sandbox; default 12345.\n";
    output << "  --desync-at <tick>    Intentionally mutate one peer at tick for desync detection tests.\n";
    output << "  --runtime-events <file>\n";
    output << "                        Runtime replay event snapshot file for --replay-sandbox.\n";
    output << "  --dump-art            Print rules object to art section links.\n";
    output << "  --map-summary         Print map bounds, theater, object counts, and overlay stats.\n";
    output << "  --dump-map-objects    Print terrain, structure, unit, and infantry placements.\n";
    output << "  --export-map-preview <ppm>\n";
    output << "                        Export a simple isometric map preview as binary PPM.\n";
    output << "  --export-map-html <html>\n";
    output << "                        Export an interactive isometric map preview as HTML.\n";
    output << "  --export-map-tmp-preview <ppm>\n";
    output << "                        Export an isometric map preview using real TMP tile pixels.\n";
    output << "  --export-map-tmp-html <html>\n";
    output << "                        Export an interactive HTML map preview using real TMP tile pixels.\n";
    output << "                        With --rules, --art, and --asset-root, embed structure SHP sprites.\n";
    output << "  --map-tile-report     Print map tileIndex to theater TileSet/file mapping.\n";
    output << "  --map-object-art-report\n";
    output << "                        Report map structure type to art image and SHP coverage.\n";
    output << "  --extract-map-tiles <dir>\n";
    output << "                        Extract used map tile files from MIX archives into a directory.\n";
    output << "  --extract-map-object-art <dir>\n";
    output << "                        Extract used map structure SHPs from MIX archives into a directory.\n";
    output << "  --list-mix            Print MIX archive header and directory entries.\n";
    output << "  --extract-mix-file <name>\n";
    output << "                        Extract one named file from a MIX archive using RA2/YR IDs.\n";
    output << "  --inspect-asset       Print basic resource file header information.\n";
    output << "  --inspect-vxl-hva     Print a paired VXL/HVA resource summary.\n";
    output << "  --export-frame <ppm>  Export the first raw SHP frame to a binary PPM image.\n";
    output << "  --export-tmp-tile <ppm>\n";
    output << "                        Export the first TMP sub-tile as a binary PPM image.\n";
    output << "  --export-json <path>  Export rules data, and art data when --art is provided.\n";
}
