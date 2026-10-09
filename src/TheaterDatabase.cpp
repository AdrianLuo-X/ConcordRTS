#include "TheaterDatabase.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

std::string trimString(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);

    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

int parseIntOrDefault(const std::string& value, int defaultValue) {
    const std::string trimmed = trimString(value);
    if (trimmed.empty()) {
        return defaultValue;
    }

    int parsed = defaultValue;
    const char* begin = trimmed.data();
    const char* end = trimmed.data() + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc() || result.ptr != end) {
        return defaultValue;
    }

    return parsed;
}

bool parseInt(const std::string& value, int& parsed) {
    const std::string trimmed = trimString(value);
    if (trimmed.empty()) {
        return false;
    }

    const char* begin = trimmed.data();
    const char* end = trimmed.data() + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc() && result.ptr == end;
}

std::string formatTileSetSectionName(int index) {
    std::ostringstream stream;
    stream << "TileSet";
    if (index < 10) {
        stream << "000";
    } else if (index < 100) {
        stream << "00";
    } else if (index < 1000) {
        stream << "0";
    }
    stream << index;
    return stream.str();
}

std::string toLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            if (ch >= 'A' && ch <= 'Z') {
                return static_cast<char>(ch - 'A' + 'a');
            }

            return static_cast<char>(ch);
        }
    );

    return value;
}

bool parseBoolOrDefault(const std::string& value, bool defaultValue) {
    const std::string normalized = toLowerAscii(trimString(value));
    if (normalized.empty()) {
        return defaultValue;
    }
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        return false;
    }
    return defaultValue;
}

std::string inferTileExtensionFromPath(const std::string& theaterIniPath) {
    const std::filesystem::path path(theaterIniPath);
    const std::string stem = toLowerAscii(path.stem().string());

    if (stem.find("temperat") != std::string::npos) {
        return ".tem";
    }

    if (stem.find("snow") != std::string::npos) {
        return ".sno";
    }

    if (stem.find("urbann") != std::string::npos || stem.find("newurban") != std::string::npos) {
        return ".ubn";
    }

    if (stem.find("urban") != std::string::npos) {
        return ".urb";
    }

    if (stem.find("desert") != std::string::npos) {
        return ".des";
    }

    if (stem.find("lunar") != std::string::npos) {
        return ".lun";
    }

    return "";
}

std::string formatTwoDigitIndex(int value) {
    std::ostringstream stream;
    if (value < 10) {
        stream << '0';
    }
    stream << value;
    return stream.str();
}

void addUniqueCandidate(std::vector<std::string>& candidates, const std::string& candidate) {
    if (!candidate.empty()
        && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()
    ) {
        candidates.push_back(candidate);
    }
}

std::string normalizeExtension(std::string extension) {
    extension = trimString(extension);
    if (extension.empty()) {
        return extension;
    }

    if (extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }

    return toLowerAscii(extension);
}

std::vector<std::string> theaterExtensionFallbacks(const std::string& preferredExtension) {
    std::vector<std::string> extensions;
    addUniqueCandidate(extensions, normalizeExtension(preferredExtension));
    addUniqueCandidate(extensions, ".tem");
    addUniqueCandidate(extensions, ".sno");
    addUniqueCandidate(extensions, ".urb");
    addUniqueCandidate(extensions, ".ubn");
    addUniqueCandidate(extensions, ".des");
    addUniqueCandidate(extensions, ".lun");
    addUniqueCandidate(extensions, ".tmp");
    return extensions;
}

std::string tileFileStem(const std::string& fileName) {
    const std::filesystem::path filePath(trimString(fileName));
    std::string stem = filePath.stem().string();
    if (stem.empty()) {
        stem = filePath.filename().string();
    }

    return stem;
}

std::vector<std::string> tileFileNameCandidates(
    const TheaterTileSet& tileSet,
    int tileIndexInSet,
    const std::string& tileExtension
) {
    std::vector<std::string> candidates;
    const std::string rawFileName = trimString(tileSet.fileName);
    if (rawFileName.empty()) {
        return candidates;
    }

    const std::filesystem::path rawPath(rawFileName);
    const std::string rawFileOnly = rawPath.filename().string();
    const std::string rawExtension = normalizeExtension(rawPath.extension().string());
    const std::string stem = tileFileStem(rawFileName);
    if (stem.empty()) {
        return candidates;
    }

    const std::string baseName = stem + formatTwoDigitIndex(tileIndexInSet + 1);
    for (const std::string& extension : theaterExtensionFallbacks(tileExtension)) {
        addUniqueCandidate(candidates, toLowerAscii(baseName + extension));
    }

    if (!rawExtension.empty()) {
        addUniqueCandidate(candidates, toLowerAscii(baseName + rawExtension));

        if (tileIndexInSet == 0) {
            addUniqueCandidate(candidates, toLowerAscii(rawFileOnly));
        }
    }

    return candidates;
}

std::vector<std::string> resourceFileNameCandidates(
    const std::string& baseName,
    const std::string& tileExtension,
    bool includeShp
) {
    std::vector<std::string> candidates;
    const std::string rawName = trimString(baseName);
    if (rawName.empty()) {
        return candidates;
    }

    const std::filesystem::path rawPath(rawName);
    const std::string rawFileOnly = rawPath.filename().string();
    const std::string rawExtension = normalizeExtension(rawPath.extension().string());
    const std::string stem = tileFileStem(rawName);
    if (stem.empty()) {
        return candidates;
    }

    if (!rawExtension.empty()) {
        addUniqueCandidate(candidates, toLowerAscii(rawFileOnly));
    }

    const std::string lowerStem = toLowerAscii(stem);
    for (const std::string& extension : theaterExtensionFallbacks(tileExtension)) {
        addUniqueCandidate(candidates, lowerStem + extension);
    }

    if (includeShp) {
        addUniqueCandidate(candidates, lowerStem + ".shp");
    }

    return candidates;
}

bool findExistingFileCaseInsensitive(
    const std::filesystem::path& root,
    const std::vector<std::string>& fileNameCandidates,
    std::filesystem::path& resolvedPath
) {
    for (const std::string& fileName : fileNameCandidates) {
        const std::filesystem::path directPath = root / fileName;
        if (std::filesystem::exists(directPath)) {
            resolvedPath = directPath;
            return true;
        }
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        return false;
    }

    std::vector<std::string> lowerCandidates;
    lowerCandidates.reserve(fileNameCandidates.size());
    for (const std::string& fileName : fileNameCandidates) {
        lowerCandidates.push_back(toLowerAscii(fileName));
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, errorCode)) {
        if (errorCode) {
            return false;
        }

        if (!entry.is_regular_file(errorCode)) {
            continue;
        }

        const std::string lowerFileName = toLowerAscii(entry.path().filename().string());
        if (std::find(lowerCandidates.begin(), lowerCandidates.end(), lowerFileName)
            != lowerCandidates.end()
        ) {
            resolvedPath = entry.path();
            return true;
        }
    }

    return false;
}

std::unordered_map<std::string, std::string> buildFileNameIndex(
    const std::string& tileRootPath
) {
    std::unordered_map<std::string, std::string> filesByName;

    std::error_code errorCode;
    const std::filesystem::path root(tileRootPath);
    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        return filesByName;
    }

    for (
        const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(root, errorCode)
    ) {
        if (errorCode) {
            return filesByName;
        }

        if (!entry.is_regular_file(errorCode)) {
            continue;
        }

        const std::string lowerFileName = toLowerAscii(entry.path().filename().string());
        filesByName.emplace(lowerFileName, entry.path().string());
    }

    return filesByName;
}

void addGeneralAlias(
    std::unordered_map<std::string, int>& generalTileSetIndices,
    const std::string& alias,
    const std::string& source
) {
    const std::string aliasKey = toLowerAscii(alias);
    const auto aliasIt = generalTileSetIndices.find(aliasKey);
    if (aliasIt != generalTileSetIndices.end() && aliasIt->second >= 0) {
        return;
    }

    const auto sourceIt = generalTileSetIndices.find(toLowerAscii(source));
    if (sourceIt != generalTileSetIndices.end() && sourceIt->second >= 0) {
        generalTileSetIndices[aliasKey] = sourceIt->second;
    }
}

} // namespace

bool TheaterDatabase::loadFromIni(const IniFile& ini, const std::string& theaterIniPath) {
    tileSets_.clear();
    generalTileSetIndices_.clear();
    warnings_.clear();
    totalTileSlots_ = 0;
    tileExtension_ = inferTileExtensionFromPath(theaterIniPath);

    if (tileExtension_.empty()) {
        warnings_.push_back("Could not infer theater tile extension from theater INI path.");
    }

    int currentTileIndex = 0;
    const char* generalKeys[] = {
        "BlackTile",
        "BlueMoldTile",
        "ClearTile",
        "ClearToBlueMoldLat",
        "ClearToCrystalLat",
        "ClearToGreenLat",
        "ClearToPaveLat",
        "ClearToRoughLat",
        "ClearToSandLat",
        "CrystalCliff",
        "CrystalTile",
        "GreenTile",
        "Medians",
        "MiscPaveTile",
        "PaveTile",
        "PavedRoads",
        "RoughTile",
        "SandTile",
        "ShorePieces",
        "SwampTile",
        "WaterBridge",
        "WaterSet",
        "WaterToSwampLat"
    };

    for (const char* key : generalKeys) {
        generalTileSetIndices_.emplace(
            toLowerAscii(key),
            parseIntOrDefault(ini.getValue("General", key), -1)
        );
    }

    const IniFile::Section* generalSection = ini.getSection("General");
    if (generalSection != nullptr) {
        for (const IniFile::KeyValue& entry : *generalSection) {
            int parsed = -1;
            if (parseInt(entry.second, parsed)) {
                generalTileSetIndices_[toLowerAscii(entry.first)] = parsed;
            }
        }
    }

    addGeneralAlias(generalTileSetIndices_, "WaterBridge", "BridgeSet");
    addGeneralAlias(generalTileSetIndices_, "WaterBridge", "WoodBridgeSet");
    addGeneralAlias(generalTileSetIndices_, "TrainBridge", "TrainBridgeSet");
    addGeneralAlias(generalTileSetIndices_, "IceShore", "IceShoreSet");

    for (int index = 0; index < 10000; ++index) {
        const std::string sectionName = formatTileSetSectionName(index);
        if (ini.getSection(sectionName) == nullptr) {
            break;
        }

        TheaterTileSet tileSet;
        tileSet.index = index;
        tileSet.setName = trimString(ini.getValue(sectionName, "SetName"));
        tileSet.fileName = trimString(ini.getValue(sectionName, "FileName"));
        tileSet.tilesInSet = parseIntOrDefault(ini.getValue(sectionName, "TilesInSet"), 0);
        tileSet.startTileIndex = currentTileIndex;
        tileSet.morphable = parseBoolOrDefault(
            ini.getValue(sectionName, "Morphable"),
            false
        );

        if (tileSet.tilesInSet < 0) {
            warnings_.push_back(sectionName + " has a negative TilesInSet value.");
            tileSet.tilesInSet = 0;
        }

        if (tileSet.tilesInSet > 0 && tileSet.fileName.empty()) {
            warnings_.push_back(sectionName + " has tiles but no FileName value.");
            tileSet.tilesInSet = 0;
        }

        if (tileSet.tilesInSet > std::numeric_limits<int>::max() - currentTileIndex) {
            warnings_.push_back(sectionName + " TilesInSet would overflow the tile index range.");
            tileSet.tilesInSet = std::numeric_limits<int>::max() - currentTileIndex;
        }

        currentTileIndex += tileSet.tilesInSet;
        tileSets_.push_back(tileSet);
    }

    totalTileSlots_ = static_cast<std::size_t>(currentTileIndex);

    if (tileSets_.empty()) {
        warnings_.push_back("Theater INI has no contiguous TileSet0000 sections.");
        return false;
    }

    return true;
}

const std::vector<TheaterTileSet>& TheaterDatabase::getTileSets() const {
    return tileSets_;
}

const std::string& TheaterDatabase::getTileExtension() const {
    return tileExtension_;
}

int TheaterDatabase::getGeneralTileSetIndex(const std::string& key) const {
    const auto it = generalTileSetIndices_.find(toLowerAscii(key));
    return it == generalTileSetIndices_.end() ? -1 : it->second;
}

std::vector<std::string> TheaterDatabase::getResourceFileNameCandidates(
    const std::string& baseName,
    bool includeShp
) const {
    return resourceFileNameCandidates(baseName, tileExtension_, includeShp);
}

std::size_t TheaterDatabase::getTotalTileSlots() const {
    return totalTileSlots_;
}

TheaterTileReference TheaterDatabase::resolveTileIndex(
    int tileIndex,
    const std::string& tileRootPath
) const {
    TheaterTileReference reference;
    reference.tileIndex = tileIndex;

    if (tileIndex < 0) {
        return reference;
    }

    const auto it = std::find_if(
        tileSets_.begin(),
        tileSets_.end(),
        [tileIndex](const TheaterTileSet& tileSet) {
            return tileIndex >= tileSet.startTileIndex
                && tileIndex < tileSet.startTileIndex + tileSet.tilesInSet;
        }
    );

    if (it == tileSets_.end()) {
        return reference;
    }

    reference.validTileSet = true;
    reference.tileSetIndex = it->index;
    reference.tileSetName = it->setName;
    reference.morphable = it->morphable;
    reference.tileIndexInSet = tileIndex - it->startTileIndex;
    reference.baseFileName = tileFileStem(it->fileName)
        + formatTwoDigitIndex(reference.tileIndexInSet + 1);
    reference.expectedFileName = reference.baseFileName + normalizeExtension(tileExtension_);
    reference.candidateFileNames =
        tileFileNameCandidates(*it, reference.tileIndexInSet, tileExtension_);

    if (!tileRootPath.empty() && !reference.expectedFileName.empty()) {
        std::filesystem::path resolvedPath;
        reference.fileExists = findExistingFileCaseInsensitive(
            std::filesystem::path(tileRootPath),
            reference.candidateFileNames,
            resolvedPath
        );

        if (reference.fileExists) {
            reference.resolvedPath = resolvedPath.string();
        }
    }

    return reference;
}

MapTileUsageReport TheaterDatabase::buildMapTileUsageReport(
    const MapDatabase& mapDatabase,
    const std::string& tileRootPath
) const {
    MapTileUsageReport report;
    report.theaterExtension = normalizeExtension(tileExtension_);
    report.tileSetCount = tileSets_.size();
    report.totalTileSlots = totalTileSlots_;
    report.mapTileCount = mapDatabase.getIsoTiles().size();

    std::map<int, int> tileUseCounts;
    for (const MapIsoTile& tile : mapDatabase.getIsoTiles()) {
        if (tile.tileIndex == 65535) {
            ++report.emptyTileCount;
            continue;
        }

        tileUseCounts[tile.tileIndex] += 1;
    }

    report.uniqueTileCount = tileUseCounts.size();
    report.usedTiles.reserve(tileUseCounts.size());

    const std::unordered_map<std::string, std::string> filesByName =
        tileRootPath.empty()
            ? std::unordered_map<std::string, std::string>{}
            : buildFileNameIndex(tileRootPath);

    for (const auto& [tileIndex, useCount] : tileUseCounts) {
        TheaterTileReference reference = resolveTileIndex(tileIndex);
        reference.useCount = useCount;

        if (!reference.validTileSet) {
            ++report.unresolvedTileCount;
        } else if (!tileRootPath.empty()) {
            const TheaterTileSet& tileSet = tileSets_[static_cast<std::size_t>(reference.tileSetIndex)];
            reference.candidateFileNames =
                tileFileNameCandidates(tileSet, reference.tileIndexInSet, tileExtension_);

            for (const std::string& candidate : reference.candidateFileNames) {
                const auto fileIt = filesByName.find(toLowerAscii(candidate));
                if (fileIt != filesByName.end()) {
                    reference.fileExists = true;
                    reference.resolvedPath = fileIt->second;
                    break;
                }
            }

            if (!reference.fileExists) {
                ++report.missingFileCount;
            }
        }

        report.usedTiles.push_back(reference);
    }

    return report;
}

const std::vector<std::string>& TheaterDatabase::getWarnings() const {
    return warnings_;
}
