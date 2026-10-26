#include "assets/preview/MapPreviewExporter.h"
#include "assets/preview/MapPreviewResourceContext.h"
#include "assets/codecs/ShpCodec.h"
#include "assets/codecs/PalDecoder.h"

#include "data/art/ArtDatabase.h"
#include "assets/archive/MixArchive.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

struct ProjectedPoint {
    int x = 0;
    int y = 0;
};

struct PreviewBounds {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

struct MapRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

struct HtmlTileRecord {
    const MapIsoTile* tile = nullptr;
    const MapOverlayCell* overlay = nullptr;
};

struct TmpHtmlTileRecord {
    const MapIsoTile* tile = nullptr;
    const MapOverlayCell* overlay = nullptr;
    int imageId = -1;
    int underlayImageId = -1;
    int overlayImageId = -1;
    bool drawZeroBase = false;
    int resolvedTileIndex = -1;
    int resolvedSubTileIndex = -1;
};

struct TmpHtmlObjectRecord {
    int kind = 0;
    int x = -1;
    int y = -1;
    int level = 0;
    int health = -1;
    int facing = -1;
    int subCell = -1;
    std::string id;
    std::string owner;
    std::string type;
    std::string mission;
    std::string tag;
    int spriteId = -1;
    int footprintWidth = 1;
    int footprintHeight = 1;
    bool onBridge = false;
};

struct TerrainWorkTile {
    MapIsoTile tile;
    int baseTileSetIndex = -1;
};

struct StructureFootprint {
    int width = 1;
    int height = 1;
};

struct TmpSubTileImage {
    bool present = false;
    int offsetX = 0;
    int offsetY = 0;
    int width = 0;
    int height = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    std::vector<unsigned char> pixels;
    int baseOffsetX = 0;
    int baseOffsetY = 0;
    int baseWidth = 0;
    int baseHeight = 0;
    std::vector<unsigned char> basePixels;
    std::vector<unsigned char> baseZData;
    bool hasBaseZData = false;
    bool hasExtra = false;
    int extraOffsetX = 0;
    int extraOffsetY = 0;
    int extraWidth = 0;
    int extraHeight = 0;
    std::vector<unsigned char> extraPixels;
    std::vector<unsigned char> extraZData;
    bool hasExtraZData = false;
};

struct TmpHtmlImageRecord {
    int offsetX = 0;
    int offsetY = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    int baseOffsetX = 0;
    int baseOffsetY = 0;
    int baseWidth = 0;
    int baseHeight = 0;
    std::vector<unsigned char> basePixels;
    std::vector<unsigned char> baseZData;
    bool hasBaseZData = false;
    bool hasExtra = false;
    int extraOffsetX = 0;
    int extraOffsetY = 0;
    int extraWidth = 0;
    int extraHeight = 0;
    std::vector<unsigned char> extraPixels;
    std::vector<unsigned char> extraZData;
    bool hasExtraZData = false;
};

struct TmpTileFile {
    int blockWidth = 0;
    int blockHeight = 0;
    std::vector<TmpSubTileImage> images;
};

struct ShpFrameImage {
    int offsetX = 0;
    int offsetY = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    int width = 0;
    int height = 0;
    int frameIndex = 0;
    int format = 0;
    unsigned int dataOffset = 0;
    std::vector<unsigned char> pixels;
    std::string sourcePath;
    std::string structureType;
    std::string artId;
    std::string imageName;
    std::string foundation;
    std::string candidateFiles;
    std::string activeAnimations;
    std::string anchorAdvice;
};

struct IndexedMixArchive {
    std::string path;
    MixArchive archive;
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> entryIndicesById;
};

const MixEntry* findIndexedMixEntryByName(
    const IndexedMixArchive& indexedMix,
    const std::string& fileName
) {
    const auto iterator =
        indexedMix.entryIndicesById.find(mixFileIdFromName(fileName));
    if (iterator == indexedMix.entryIndicesById.end()) {
        return nullptr;
    }

    const std::vector<MixEntry>& entries = indexedMix.archive.getEntries();
    const MixEntry* firstEntry = nullptr;
    for (const std::size_t entryIndex : iterator->second) {
        if (entryIndex >= entries.size()) {
            continue;
        }
        const MixEntry& entry = entries[entryIndex];
        if (firstEntry == nullptr) {
            firstEntry = &entry;
        }
        if (entry.size > 0) {
            return &entry;
        }
    }
    return firstEntry;
}

bool loadTmpTileFileFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceLabel,
    TmpTileFile& tileFile,
    std::string& errorMessage
);

bool loadTmpTileFile(
    const std::string& path,
    TmpTileFile& tileFile,
    std::string& errorMessage
);

bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) {
    if (left > static_cast<std::size_t>(-1) - right) {
        return false;
    }

    result = left + right;
    return true;
}

bool checkedMul(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > static_cast<std::size_t>(-1) / left) {
        return false;
    }

    result = left * right;
    return true;
}

bool loadBytes(
    const std::string& path,
    std::vector<unsigned char>& bytes,
    std::string& errorMessage
) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "failed to open file: " + path;
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        errorMessage = "failed to get file size: " + path;
        return false;
    }

    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));

    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    if (!input && !bytes.empty()) {
        errorMessage = "failed to read file: " + path;
        return false;
    }

    return true;
}

unsigned int readUInt32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8)
        | (static_cast<unsigned int>(bytes[offset + 2]) << 16)
        | (static_cast<unsigned int>(bytes[offset + 3]) << 24);
}

int readInt32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<int>(static_cast<std::int32_t>(readUInt32(bytes, offset)));
}

unsigned int readUInt16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

int signExtendUInt16(unsigned int value) {
    value &= 0xFFFFU;
    if ((value & 0x8000U) == 0) {
        return static_cast<int>(value);
    }

    return static_cast<int>(value) - 0x10000;
}

bool loadPalette(
    const std::string& palettePath,
    std::vector<Color>& palette,
    std::string& errorMessage
) {
    std::vector<unsigned char> paletteBytes;
    if (!loadBytes(palettePath, paletteBytes, errorMessage)) {
        return false;
    }
    concord::assets::PalRawPalette raw;
    if (!concord::assets::decodePalBytes(
            paletteBytes, raw, concord::assets::PalLengthPolicy::Exact768)) {
        errorMessage = "PAL file must be exactly 768 bytes: " + palettePath;
        return false;
    }
    const auto colors = concord::assets::mapPalColors(
        raw, concord::assets::PalColorPolicy::RoundedSixBit);
    palette.clear();
    palette.reserve(256);
    for (const auto& color : colors) {
        palette.push_back({color.r, color.g, color.b});
    }
    return true;
}

std::string toLowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    return value;
}

std::string trimAscii(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::string withShpExtension(const std::string& imageName) {
    if (imageName.empty()) {
        return "";
    }

    const std::filesystem::path path(imageName);
    if (!path.extension().empty()) {
        return path.filename().string();
    }

    return imageName + ".shp";
}

bool parsePositiveIntToken(
    const std::string& value,
    std::size_t begin,
    std::size_t end,
    int& parsed
) {
    if (begin >= end) {
        return false;
    }

    int result = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const char ch = value[i];
        if (ch < '0' || ch > '9') {
            return false;
        }

        result = (result * 10) + (ch - '0');
        if (result > 16) {
            return false;
        }
    }

    if (result <= 0) {
        return false;
    }

    parsed = result;
    return true;
}

StructureFootprint parseFoundationSize(const std::string& foundation) {
    std::string compact;
    compact.reserve(foundation.size());

    for (const char ch : foundation) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            compact.push_back(ch);
        }
    }

    compact = toLowerAscii(std::move(compact));
    const std::size_t separator = compact.find('x');
    if (separator == std::string::npos) {
        return {};
    }

    int width = 1;
    int height = 1;
    if (!parsePositiveIntToken(compact, 0, separator, width)
        || !parsePositiveIntToken(compact, separator + 1, compact.size(), height)) {
        return {};
    }

    return {width, height};
}

std::unordered_map<std::string, std::vector<std::string>> buildFileIndex(
    const std::string& rootPath,
    std::vector<std::string>& warnings
) {
    std::unordered_map<std::string, std::vector<std::string>> filesByName;

    if (rootPath.empty()) {
        return filesByName;
    }

    std::error_code errorCode;
    const std::filesystem::path root(rootPath);
    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        warnings.push_back("asset root is not a readable directory: " + rootPath);
        return filesByName;
    }

    for (
        const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(root, errorCode)
    ) {
        if (errorCode) {
            warnings.push_back("stopped scanning asset root: " + errorCode.message());
            break;
        }

        if (!entry.is_regular_file(errorCode)) {
            continue;
        }

        filesByName[toLowerAscii(entry.path().filename().string())].push_back(entry.path().string());
    }

    return filesByName;
}

std::string canonicalPathString(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, errorCode);
    return errorCode ? path.lexically_normal().string() : canonical.string();
}

void mergeFileIndex(
    std::unordered_map<std::string, std::vector<std::string>>& destination,
    std::unordered_map<std::string, std::vector<std::string>> source
) {
    for (auto& [fileName, paths] : source) {
        std::vector<std::string>& targetPaths = destination[fileName];
        targetPaths.insert(
            targetPaths.end(),
            std::make_move_iterator(paths.begin()),
            std::make_move_iterator(paths.end())
        );
    }
}

void addSearchRoot(std::vector<std::string>& roots, const std::filesystem::path& root) {
    if (root.empty()) {
        return;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(root, errorCode) || !std::filesystem::is_directory(root, errorCode)) {
        return;
    }

    const std::string canonicalRoot = canonicalPathString(root);
    if (std::find(roots.begin(), roots.end(), canonicalRoot) == roots.end()) {
        roots.push_back(canonicalRoot);
    }
}

void addSiblingResourceRoot(
    std::vector<std::string>& roots,
    const std::string& rootPath,
    const std::string& siblingName
) {
    if (rootPath.empty()) {
        return;
    }

    const std::filesystem::path root(rootPath);
    const std::filesystem::path parent = root.parent_path();
    if (parent.empty()) {
        return;
    }

    const std::filesystem::path resourceRoot = parent.parent_path();
    if (resourceRoot.empty()) {
        return;
    }

    addSearchRoot(roots, resourceRoot / siblingName / root.filename());
    addSearchRoot(roots, resourceRoot);
}

std::vector<std::string> structureAssetSearchRoots(const std::string& assetRootPath) {
    std::vector<std::string> roots;

    if (!assetRootPath.empty()) {
        addSearchRoot(roots, std::filesystem::path(assetRootPath));
        addSiblingResourceRoot(roots, assetRootPath, "extracted_buildings");
    }

    return roots;
}

std::vector<std::string> overlayAssetSearchRoots(
    const std::string& tileRootPath,
    const std::string& assetRootPath
) {
    std::vector<std::string> roots;

    if (!tileRootPath.empty()) {
        addSearchRoot(roots, std::filesystem::path(tileRootPath));
        addSiblingResourceRoot(roots, tileRootPath, "extracted_overlays");
    }

    if (!assetRootPath.empty()) {
        addSearchRoot(roots, std::filesystem::path(assetRootPath));
        addSiblingResourceRoot(roots, assetRootPath, "extracted_overlays");
    }

    return roots;
}

std::unordered_map<std::string, std::vector<std::string>> buildOverlayFileIndex(
    const std::string& tileRootPath,
    const std::string& assetRootPath,
    std::vector<std::string>& warnings
) {
    std::unordered_map<std::string, std::vector<std::string>> filesByName;
    std::set<std::string> seenRoots;

    for (const std::string& root : overlayAssetSearchRoots(tileRootPath, assetRootPath)) {
        if (root.empty() || seenRoots.find(root) != seenRoots.end()) {
            continue;
        }

        seenRoots.insert(root);

        std::error_code errorCode;
        const std::filesystem::path rootPath(root);
        if (!std::filesystem::exists(rootPath, errorCode)
            || !std::filesystem::is_directory(rootPath, errorCode)
        ) {
            continue;
        }

        mergeFileIndex(filesByName, buildFileIndex(root, warnings));
    }

    return filesByName;
}

std::vector<std::string> mixArchiveSearchRoots(
    const std::string& tileRootPath,
    const std::string& assetRootPath
) {
    std::vector<std::string> roots;

    if (!tileRootPath.empty()) {
        const std::filesystem::path tileRoot(tileRootPath);
        addSearchRoot(roots, tileRoot);

        const std::filesystem::path parent = tileRoot.parent_path();
        if (!parent.empty()) {
            const std::filesystem::path resourceRoot = parent.parent_path();
            if (!resourceRoot.empty()) {
                addSearchRoot(roots, resourceRoot);
            }
        }
    }

    if (!assetRootPath.empty()) {
        addSearchRoot(roots, std::filesystem::path(assetRootPath));
    }

    return roots;
}

std::vector<IndexedMixArchive> buildMixArchiveIndex(
    const std::string& tileRootPath,
    const std::string& assetRootPath,
    std::vector<std::string>& warnings
) {
    std::vector<IndexedMixArchive> archives;
    std::set<std::string> seenRoots;
    std::set<std::string> seenMixPaths;

    for (const std::string& root : mixArchiveSearchRoots(tileRootPath, assetRootPath)) {
        if (root.empty() || seenRoots.find(root) != seenRoots.end()) {
            continue;
        }

        seenRoots.insert(root);

        std::error_code errorCode;
        const std::filesystem::path rootPath(root);
        if (!std::filesystem::exists(rootPath, errorCode)
            || !std::filesystem::is_directory(rootPath, errorCode)
        ) {
            continue;
        }

        for (
            const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(rootPath, errorCode)
        ) {
            if (errorCode) {
                warnings.push_back("stopped scanning MIX roots: " + errorCode.message());
                break;
            }

            if (!entry.is_regular_file(errorCode)
                || toLowerAscii(entry.path().extension().string()) != ".mix") {
                continue;
            }

            const std::string mixPath = canonicalPathString(entry.path());
            if (!seenMixPaths.insert(mixPath).second) {
                continue;
            }

            MixArchive archive;
            if (!archive.loadFromFile(mixPath)) {
                warnings.push_back(
                    "MIX index load failed: "
                    + entry.path().filename().string()
                    + " error="
                    + archive.getErrorMessage()
                );
                continue;
            }

            if (!archive.isIndexReadable()) {
                warnings.push_back(
                    "MIX index is not readable: "
                    + entry.path().filename().string()
                );
                continue;
            }

            IndexedMixArchive indexed;
            indexed.path = mixPath;
            indexed.archive = std::move(archive);
            const std::vector<MixEntry>& entries = indexed.archive.getEntries();
            for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
                indexed.entryIndicesById[entries[entryIndex].id].push_back(entryIndex);
            }
            archives.push_back(std::move(indexed));
        }
    }

    return archives;
}

char newTheaterCharForExtension(const std::string& tileExtension) {
    if (tileExtension == ".tem") {
        return 't';
    }
    if (tileExtension == ".urb") {
        return 'u';
    }
    if (tileExtension == ".ubn") {
        return 'n';
    }
    if (tileExtension == ".des") {
        return 'd';
    }
    if (tileExtension == ".lun") {
        return 'l';
    }
    if (tileExtension == ".sno") {
        return 'a';
    }

    return '\0';
}

bool artIdSupportsNewTheater(const std::string& artId) {
    if (artId.size() < 2) {
        return false;
    }

    const char first = static_cast<char>(std::toupper(static_cast<unsigned char>(artId[0])));
    const char second = static_cast<char>(std::toupper(static_cast<unsigned char>(artId[1])));
    return (first == 'G' || first == 'N' || first == 'C' || first == 'Y')
        && (
            second == 'A'
            || second == 'T'
            || second == 'U'
            || second == 'D'
            || second == 'L'
            || second == 'N'
        );
}

void addUniqueCandidate(std::vector<std::string>& candidates, const std::string& candidate) {
    if (!candidate.empty()
        && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()
    ) {
        candidates.push_back(candidate);
    }
}

std::string formatCandidateList(const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return "(none)";
    }

    std::string result;
    const std::size_t previewCount = std::min<std::size_t>(candidates.size(), 5);
    for (std::size_t i = 0; i < previewCount; ++i) {
        if (!result.empty()) {
            result += ",";
        }

        result += candidates[i];
    }

    if (candidates.size() > previewCount) {
        result += ",...";
    }

    return result;
}

std::string formatFullCandidateList(const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return "(none)";
    }

    std::string result;
    for (const std::string& candidate : candidates) {
        if (!result.empty()) {
            result += ",";
        }

        result += candidate;
    }

    return result;
}

std::string formatAnimationLayer(
    const ArtType::ActiveAnimation& animation,
    const std::string& label,
    std::size_t index
) {
    std::string result =
        label
        + std::to_string(index + 1)
        + "="
        + animation.image;

    if (!animation.damagedImage.empty()) {
        result += "/damaged=" + animation.damagedImage;
    }

    result += "@";
    result += std::to_string(animation.offsetX);
    result += ",";
    result += std::to_string(animation.offsetY);
    result += " ySort=";
    result += std::to_string(animation.ySort);
    result += " zAdjust=";
    result += std::to_string(animation.zAdjust);
    result += animation.powered ? " powered=yes" : " powered=no";
    return result;
}

std::string formatActiveAnimationSummary(const ArtType* artType) {
    if (artType == nullptr
        || (artType->activeAnimations.empty() && artType->specialAnimations.empty())) {
        return "-";
    }

    std::string summary;
    for (std::size_t i = 0; i < artType->activeAnimations.size(); ++i) {
        if (!summary.empty()) {
            summary += "; ";
        }

        summary += formatAnimationLayer(artType->activeAnimations[i], "active", i);
    }

    for (std::size_t i = 0; i < artType->specialAnimations.size(); ++i) {
        if (!summary.empty()) {
            summary += "; ";
        }

        summary += formatAnimationLayer(artType->specialAnimations[i], "special", i);
    }

    return summary;
}

std::string formatExpectedStructureRect(const ShpFrameImage& image) {
    const int tileHeight = 30;
    const int left = (-image.globalWidth / 2) + image.offsetX;
    const int top = (tileHeight / 2) - image.globalHeight + image.offsetY;
    return "expectedRectRelativeToRearCell="
        + std::to_string(left)
        + ","
        + std::to_string(top)
        + ","
        + std::to_string(image.globalWidth)
        + "x"
        + std::to_string(image.globalHeight)
        + " visibleFrame="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " tileH="
        + std::to_string(tileHeight);
}

std::string formatStructureAnchorWarning(
    const std::string& structureType,
    const ArtObjectLink& link,
    const ArtType* artType,
    const ShpFrameImage& image,
    const std::vector<std::string>& candidateFiles
) {
    const StructureFootprint footprint = parseFoundationSize(link.foundation);
    const int anchorDx = std::max(0, footprint.width - 1);
    const int anchorDy = std::max(0, footprint.height - 1);
    return "structure art anchor "
        + structureType
        + ": art="
        + (link.artId.empty() ? "-" : link.artId)
        + " image="
        + (link.image.empty() ? "-" : link.image)
        + " foundation="
        + (link.foundation.empty() ? "-" : link.foundation)
        + " shp="
        + std::filesystem::path(image.sourcePath).filename().string()
        + " frame="
        + std::to_string(image.frameIndex)
        + " size="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " global="
        + std::to_string(image.globalWidth)
        + "x"
        + std::to_string(image.globalHeight)
        + " offset="
        + std::to_string(image.offsetX)
        + ","
        + std::to_string(image.offsetY)
        + " format="
        + std::to_string(image.format)
        + " dataOffset="
        + std::to_string(image.dataOffset)
        + " activeAnim="
        + formatActiveAnimationSummary(artType)
        + " rendererAnchor=footprintRearCell(+"
        + std::to_string(anchorDx)
        + ",+"
        + std::to_string(anchorDy)
        + ")"
        + " spriteTopLeft=project(x+"
        + std::to_string(anchorDx)
        + ",y+"
        + std::to_string(anchorDy)
        + ")+(-globalWidth/2+frameOffsetX,+tileH/2-globalHeight+frameOffsetY)"
        + " "
        + formatExpectedStructureRect(image)
        + " candidates="
        + formatCandidateList(candidateFiles);
}

std::string formatStructureAnchorAdvice(
    const ArtObjectLink& link,
    const ShpFrameImage& image
) {
    const StructureFootprint footprint = parseFoundationSize(link.foundation);
    const int anchorDx = std::max(0, footprint.width - 1);
    const int anchorDy = std::max(0, footprint.height - 1);

    return "footprintRearCell(+"
        + std::to_string(anchorDx)
        + ",+"
        + std::to_string(anchorDy)
        + ") topLeft=project(anchor)+("
        + std::to_string((-image.globalWidth / 2) + image.offsetX)
        + ","
        + "tileH/2-"
        + std::to_string(image.globalHeight)
        + "+"
        + std::to_string(image.offsetY)
        + ") "
        + formatExpectedStructureRect(image);
}

std::vector<std::string> structureAssetFileCandidates(
    const ArtObjectLink& link,
    const TheaterDatabase& theaterDatabase
) {
    std::vector<std::string> candidates;
    const std::string baseName = link.image.empty() ? link.artId : link.image;
    if (baseName.empty()) {
        return candidates;
    }

    const std::filesystem::path basePath(baseName);
    if (!basePath.extension().empty()) {
        addUniqueCandidate(candidates, toLowerAscii(basePath.filename().string()));
        return candidates;
    }

    const std::string lowerBaseName = toLowerAscii(baseName);
    const char newTheaterChar = newTheaterCharForExtension(theaterDatabase.getTileExtension());
    if (link.newTheater
        && newTheaterChar != '\0'
        && artIdSupportsNewTheater(link.artId)
        && lowerBaseName.size() >= 2
    ) {
        std::string newTheaterName = lowerBaseName;
        newTheaterName[1] = newTheaterChar;
        addUniqueCandidate(candidates, newTheaterName + ".shp");

        std::string genericName = lowerBaseName;
        genericName[1] = 'g';
        addUniqueCandidate(candidates, genericName + ".shp");
    }

    addUniqueCandidate(candidates, lowerBaseName + ".shp");
    return candidates;
}

bool isKnownPlaceholderStructureSprite(
    const std::string& structureType,
    const std::string& filePath
) {
    const std::string lowerType = toLowerAscii(structureType);
    const std::string lowerFileName =
        toLowerAscii(std::filesystem::path(filePath).filename().string());
    return lowerType.rfind("camorr", 0) == 0
        && lowerFileName.rfind("camorr", 0) == 0;
}

bool isLowBridgeAggregateBaseName(const std::string& baseName) {
    const std::string lower = toLowerAscii(baseName);
    return lower == "lobrdg26" || lower == "lobrdb26";
}

std::string lowBridgeSegmentBaseNameForOverlay(const MapOverlayCell& overlay) {
    const std::string baseName = overlay.image.empty() ? overlay.type : overlay.image;
    if (!isLowBridgeAggregateBaseName(baseName)
        || overlay.frame < 0
        || overlay.frame >= 28) {
        return "";
    }

    const std::string lower = toLowerAscii(baseName);
    const std::string prefix =
        lower.rfind("lobrdb", 0) == 0 ? "lobrdb" : "lobrdg";
    std::ostringstream stream;
    stream << prefix;
    const int segment = overlay.frame + 1;
    if (segment < 10) {
        stream << '0';
    }
    stream << segment;
    return stream.str();
}

std::vector<std::string> overlayAssetFileCandidates(
    const MapOverlayCell& overlay,
    const TheaterDatabase& theaterDatabase
) {
    std::vector<std::string> candidates;
    const std::string baseName = overlay.image.empty() ? overlay.type : overlay.image;
    if (baseName.empty()) {
        return candidates;
    }

    const std::filesystem::path basePath(baseName);
    if (!basePath.extension().empty()) {
        addUniqueCandidate(candidates, toLowerAscii(basePath.filename().string()));
        return candidates;
    }

    const std::string lowerBaseName = toLowerAscii(baseName);
    const std::string tileExtension = theaterDatabase.getTileExtension();
    const auto addTheaterCandidates =
        [&candidates, &theaterDatabase](const std::string& lowerName) {
            if (lowerName.empty()) {
                return;
            }
            for (const std::string& candidate :
                theaterDatabase.getResourceFileNameCandidates(lowerName, true)
            ) {
                addUniqueCandidate(candidates, candidate);
            }
    };

    addTheaterCandidates(lowBridgeSegmentBaseNameForOverlay(overlay));
    addTheaterCandidates(lowerBaseName);

    if (lowerBaseName == "bridge") {
        addTheaterCandidates("bridgb");
    }

    const char newTheaterChar = newTheaterCharForExtension(tileExtension);
    if (newTheaterChar != '\0'
        && lowerBaseName.size() >= 2
        && artIdSupportsNewTheater(lowerBaseName)
    ) {
        std::string newTheaterName = lowerBaseName;
        newTheaterName[1] = newTheaterChar;
        addUniqueCandidate(candidates, newTheaterName + ".shp");

        std::string genericName = lowerBaseName;
        genericName[1] = 'g';
        addUniqueCandidate(candidates, genericName + ".shp");
    }
    return candidates;
}

bool isLowBridgeOverlayName(const std::string& value) {
    const std::string lower = toLowerAscii(value);
    return lower.rfind("lobrdg", 0) == 0
        || lower.rfind("lobrdb", 0) == 0;
}

bool isLowBridgeOverlay(const MapOverlayCell& overlay) {
    return isLowBridgeOverlayName(overlay.type)
        || isLowBridgeOverlayName(overlay.image);
}

bool isHighBridgeOverlayName(const std::string& value) {
    const std::string lower = toLowerAscii(value);
    return lower.rfind("bridge", 0) == 0
        || lower.rfind("bridg", 0) == 0
        || lower.rfind("bridgb", 0) == 0;
}

bool isHighBridgeOverlay(const MapOverlayCell& overlay) {
    return isHighBridgeOverlayName(overlay.type)
        || isHighBridgeOverlayName(overlay.image);
}

bool shouldDrawLowBridgeOverlaySprite(const MapOverlayCell& overlay) {
    if (!isLowBridgeOverlay(overlay)) {
        return true;
    }

    return false;
}

int firstPresentTmpImageIndex(const TmpTileFile& tileFile) {
    for (std::size_t i = 0; i < tileFile.images.size(); ++i) {
        if (tileFile.images[i].present) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int overlayResourceDecodeFrame(const MapOverlayCell& overlay) {
    if (!lowBridgeSegmentBaseNameForOverlay(overlay).empty()) {
        return 1;
    }
    if (isLowBridgeOverlay(overlay)) {
        return 1;
    }
    if ((overlay.isOre || overlay.isGem) && overlay.frame >= 0) {
        return overlay.frame;
    }
    return overlay.frameIndex;
}

int resolveOverlayTmpImageIndex(
    const MapOverlayCell& overlay,
    const TmpTileFile& tileFile
) {
    if (tileFile.images.empty()) {
        return -1;
    }

    const int frameIndex = overlayResourceDecodeFrame(overlay);
    if (frameIndex >= 0
        && static_cast<std::size_t>(frameIndex) < tileFile.images.size()
        && tileFile.images[static_cast<std::size_t>(frameIndex)].present
    ) {
        return frameIndex;
    }

    if (isLowBridgeOverlay(overlay) && frameIndex >= 0) {
        const int wrappedFrame =
            frameIndex % static_cast<int>(tileFile.images.size());
        if (tileFile.images[static_cast<std::size_t>(wrappedFrame)].present) {
            return wrappedFrame;
        }
    }

    return firstPresentTmpImageIndex(tileFile);
}

bool decodeShpFrameFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceLabel,
    int frameIndex,
    ShpFrameImage& image,
    std::string& errorMessage
) {
    ShpIndexedFrame decoded;
    if (!decodeShpIndexedFrame(bytes, sourceLabel, frameIndex, ShpDecodePolicy::Preview, decoded, errorMessage)) {
        return false;
    }
    image.offsetX = decoded.offsetX;
    image.offsetY = decoded.offsetY;
    image.globalWidth = decoded.globalWidth;
    image.globalHeight = decoded.globalHeight;
    image.width = decoded.width;
    image.height = decoded.height;
    image.frameIndex = decoded.frameIndex;
    image.format = decoded.format;
    image.dataOffset = decoded.dataOffset;
    image.pixels = std::move(decoded.pixels);
    image.sourcePath = sourceLabel;
    return true;
}

bool decodeShpFrame(
    const std::string& shpPath,
    int frameIndex,
    ShpFrameImage& image,
    std::string& errorMessage
) {
    std::vector<unsigned char> bytes;
    if (!loadBytes(shpPath, bytes, errorMessage)) {
        return false;
    }

    return decodeShpFrameFromBytes(bytes, shpPath, frameIndex, image, errorMessage);
}

bool decodeShpFirstFrame(
    const std::string& shpPath,
    ShpFrameImage& image,
    std::string& errorMessage
) {
    return decodeShpFrame(shpPath, 0, image, errorMessage);
}

Color makeTileColor(const MapIsoTile& tile) {
    const int seed = tile.tileIndex * 37 + tile.subTileIndex * 17;
    int r = 70 + (seed % 80);
    int g = 80 + ((seed / 3) % 90);
    int b = 55 + ((seed / 7) % 70);

    const int levelBoost = (tile.level - 4) * 8;
    r = std::clamp(r + levelBoost, 0, 255);
    g = std::clamp(g + levelBoost, 0, 255);
    b = std::clamp(b + levelBoost, 0, 255);

    return {
        static_cast<unsigned char>(r),
        static_cast<unsigned char>(g),
        static_cast<unsigned char>(b)
    };
}

Color blend(Color base, Color overlay) {
    return {
        static_cast<unsigned char>((static_cast<int>(base.r) + overlay.r) / 2),
        static_cast<unsigned char>((static_cast<int>(base.g) + overlay.g) / 2),
        static_cast<unsigned char>((static_cast<int>(base.b) + overlay.b) / 2)
    };
}

int mapCellFromXY(int x, int y);

ProjectedPoint projectIsoTile(const MapIsoTile& tile) {
    return {
        tile.x - tile.y,
        tile.x + tile.y - tile.level
    };
}

bool parseIntFieldToken(const std::string& value, std::size_t begin, std::size_t end, int& parsed) {
    while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
        --end;
    }
    if (begin >= end) {
        return false;
    }

    const char* first = value.data() + begin;
    const char* last = value.data() + end;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    return result.ec == std::errc() && result.ptr == last;
}

MapRect parseMapRect(const std::string& value) {
    MapRect rect;
    int parts[4] = {0, 0, 0, 0};
    std::size_t begin = 0;

    for (int i = 0; i < 4; ++i) {
        const std::size_t comma = value.find(',', begin);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        if (!parseIntFieldToken(value, begin, end, parts[i])) {
            return rect;
        }

        begin = end + 1;
        if (i < 3 && comma == std::string::npos) {
            return rect;
        }
    }

    if (begin < value.size()) {
        return rect;
    }

    rect.left = parts[0];
    rect.top = parts[1];
    rect.width = parts[2];
    rect.height = parts[3];
    rect.valid = rect.width > 0 && rect.height > 0 && rect.width <= 512 && rect.height <= 512;
    return rect;
}

MapIsoTile normalizeTerrainTile(MapIsoTile tile, int emptyTileIndex) {
    if (tile.tileIndex >= 65535) {
        tile.tileIndex = emptyTileIndex;
        tile.subTileIndex = 0;
    }

    return tile;
}

int displayXForTile(const MapIsoTile& tile, int mapWidth) {
    return tile.x - tile.y + mapWidth - 1;
}

int displayYForTile(const MapIsoTile& tile, int mapWidth) {
    return tile.x + tile.y - mapWidth - 1;
}

bool mapCoordinatesFromDisplay(
    int displayX,
    int displayY,
    int mapWidth,
    int& x,
    int& y
) {
    const int doubledX = displayX + displayY;
    if ((doubledX % 2) != 0) {
        return false;
    }

    x = (doubledX / 2) + 1;
    y = displayY - x + mapWidth + 1;
    return mapCellFromXY(x, y) >= 0;
}

void sortTerrainTilesForDrawing(std::vector<MapIsoTile>& tiles, int mapWidth) {
    if (mapWidth <= 0) {
        std::sort(
            tiles.begin(),
            tiles.end(),
            [](const MapIsoTile& left, const MapIsoTile& right) {
                const int leftDepth = left.x + left.y + left.level;
                const int rightDepth = right.x + right.y + right.level;

                if (leftDepth != rightDepth) {
                    return leftDepth < rightDepth;
                }

                if (left.y != right.y) {
                    return left.y < right.y;
                }

                return left.x < right.x;
            }
        );
        return;
    }

    std::sort(
        tiles.begin(),
        tiles.end(),
        [mapWidth](const MapIsoTile& left, const MapIsoTile& right) {
            const int leftDy = displayYForTile(left, mapWidth);
            const int rightDy = displayYForTile(right, mapWidth);
            const int leftRow = leftDy / 2;
            const int rightRow = rightDy / 2;
            if (leftRow != rightRow) {
                return leftRow < rightRow;
            }

            const int leftDx = displayXForTile(left, mapWidth);
            const int rightDx = displayXForTile(right, mapWidth);
            const bool leftEven = leftDx % 2 == 0;
            const bool rightEven = rightDx % 2 == 0;
            if (leftEven != rightEven) {
                return leftEven;
            }

            return leftDx > rightDx;
        }
    );
}

int mapCellFromXY(int x, int y) {
    if (x < 0 || y < 0 || x >= 512 || y >= 512) {
        return -1;
    }

    return y * 512 + x;
}

int overlayCellFromTile(const MapIsoTile& tile) {
    return mapCellFromXY(tile.x, tile.y);
}

PreviewBounds calculatePreviewBounds(const std::vector<MapIsoTile>& tiles) {
    PreviewBounds bounds;

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (const MapIsoTile& tile : tiles) {
        const ProjectedPoint point = projectIsoTile(tile);
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    bounds.minX = minX;
    bounds.minY = minY;
    bounds.maxX = maxX;
    bounds.maxY = maxY;
    return bounds;
}

std::unordered_map<int, const MapOverlayCell*> buildOverlayCellLookup(
    const std::vector<MapOverlayCell>& overlayCells
) {
    std::unordered_map<int, const MapOverlayCell*> overlayByCell;
    overlayByCell.reserve(overlayCells.size());

    for (const MapOverlayCell& overlay : overlayCells) {
        overlayByCell[overlay.cellIndex] = &overlay;
    }

    return overlayByCell;
}

std::unordered_map<int, int> buildTileLevelLookup(const std::vector<MapIsoTile>& tiles) {
    std::unordered_map<int, int> levelByCell;
    levelByCell.reserve(tiles.size());

    for (const MapIsoTile& tile : tiles) {
        const int cell = mapCellFromXY(tile.x, tile.y);
        if (cell < 0) {
            continue;
        }

        const auto [it, inserted] = levelByCell.emplace(cell, tile.level);
        if (!inserted) {
            it->second = std::max(it->second, tile.level);
        }
    }

    return levelByCell;
}

int absInt(int value) {
    return value < 0 ? -value : value;
}

bool containsAscii(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

bool isWaterLikeTerrain(const TheaterTileReference& reference) {
    const std::string name = toLowerAscii(reference.tileSetName + " " + reference.baseFileName);
    return containsAscii(name, "water")
        || containsAscii(name, "shore")
        || containsAscii(name, "wclif");
}

bool isPlainWaterTerrain(const TheaterTileReference& reference) {
    const std::string setName = toLowerAscii(reference.tileSetName);
    const std::string fileName = toLowerAscii(reference.baseFileName);
    return containsAscii(setName, "water")
        && !containsAscii(setName, "shore")
        && !containsAscii(setName, "cliff")
        && !containsAscii(setName, "fall")
        && !containsAscii(setName, "slope")
        && !containsAscii(setName, "cave")
        && !containsAscii(setName, "bridge")
        && containsAscii(fileName, "water");
}

bool isPavementLikeTerrain(const TheaterTileReference& reference) {
    if (isWaterLikeTerrain(reference)) {
        return false;
    }

    const std::string fileName = toLowerAscii(reference.baseFileName);
    if (containsAscii(fileName, "clear") || containsAscii(fileName, "cler")) {
        return false;
    }

    return containsAscii(fileName, "pave")
        || containsAscii(fileName, "pav")
        || containsAscii(fileName, "proad")
        || containsAscii(fileName, "plat")
        || containsAscii(fileName, "pvclr");
}

bool mapUsesWaterLikeTerrain(const MapTileUsageReport& usageReport) {
    return std::any_of(
        usageReport.usedTiles.begin(),
        usageReport.usedTiles.end(),
        [](const TheaterTileReference& reference) {
            return isWaterLikeTerrain(reference);
        }
    );
}

bool shouldDrawPlainWaterTerrainSprites(
    const std::vector<MapIsoTile>& renderTiles,
    const std::unordered_map<int, TheaterTileReference>& referencesByTileIndex
) {
    std::size_t plainWaterTiles = 0;
    for (const MapIsoTile& tile : renderTiles) {
        const auto referenceIt = referencesByTileIndex.find(tile.tileIndex);
        if (referenceIt != referencesByTileIndex.end()
            && isPlainWaterTerrain(referenceIt->second)
        ) {
            ++plainWaterTiles;
        }
    }

    if (plainWaterTiles == 0) {
        return false;
    }

    const double ratio =
        static_cast<double>(plainWaterTiles)
        / static_cast<double>(std::max<std::size_t>(renderTiles.size(), 1));
    return plainWaterTiles <= 512 || ratio <= 0.20;
}

int findFirstExistingTileFromSet(
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::vector<std::string>& requiredTokens
) {
    for (const TheaterTileSet& tileSet : theaterDatabase.getTileSets()) {
        if (tileSet.tilesInSet <= 0) {
            continue;
        }

        const std::string name = toLowerAscii(tileSet.setName + " " + tileSet.fileName);
        const bool matches = std::all_of(
            requiredTokens.begin(),
            requiredTokens.end(),
            [&name](const std::string& token) {
                return containsAscii(name, token);
            }
        );
        if (!matches) {
            continue;
        }

        for (int offset = 0; offset < tileSet.tilesInSet; ++offset) {
            const TheaterTileReference reference =
                theaterDatabase.resolveTileIndex(tileSet.startTileIndex + offset, tileRootPath);
            if (reference.validTileSet && reference.fileExists) {
                return reference.tileIndex;
            }
        }
    }

    return -1;
}

int findPavementUnderlayTileIndex(
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath
) {
    const int latPavementTileIndex =
        findFirstExistingTileFromSet(theaterDatabase, tileRootPath, {"lat", "pavement"});
    if (latPavementTileIndex >= 0) {
        return latPavementTileIndex;
    }

    return findFirstExistingTileFromSet(theaterDatabase, tileRootPath, {"pavement"});
}

int findWaterFillTileIndex(
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath
) {
    for (const TheaterTileSet& tileSet : theaterDatabase.getTileSets()) {
        if (tileSet.tilesInSet <= 0) {
            continue;
        }

        const std::string setName = toLowerAscii(tileSet.setName);
        const std::string fileName = toLowerAscii(tileSet.fileName);
        if (!containsAscii(setName, "water")
            || containsAscii(setName, "shore")
            || containsAscii(setName, "cliff")
            || containsAscii(setName, "fall")
            || containsAscii(setName, "slope")
            || containsAscii(setName, "cave")
            || containsAscii(setName, "bridge")
            || !containsAscii(fileName, "water")
        ) {
            continue;
        }

        for (int offset = 0; offset < tileSet.tilesInSet; ++offset) {
            const TheaterTileReference reference =
                theaterDatabase.resolveTileIndex(tileSet.startTileIndex + offset, tileRootPath);
            if (reference.validTileSet && reference.fileExists) {
                return reference.tileIndex;
            }
        }
    }

    return -1;
}

int chooseTerrainUnderlayTileIndex(
    const TheaterTileReference& reference,
    int clearTileIndex,
    int pavementTileIndex
) {
    if (reference.tileIndex == clearTileIndex || isWaterLikeTerrain(reference)) {
        return -1;
    }

    if (isPavementLikeTerrain(reference) && pavementTileIndex >= 0) {
        return pavementTileIndex;
    }

    return clearTileIndex;
}

bool ensureTileReference(
    std::unordered_map<int, TheaterTileReference>& referencesByTileIndex,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    int tileIndex
) {
    if (tileIndex < 0) {
        return false;
    }

    if (referencesByTileIndex.find(tileIndex) != referencesByTileIndex.end()) {
        return true;
    }

    TheaterTileReference reference = theaterDatabase.resolveTileIndex(tileIndex, tileRootPath);
    if (!reference.validTileSet || !reference.fileExists) {
        return false;
    }

    referencesByTileIndex.emplace(tileIndex, std::move(reference));
    return true;
}

int findTileSetStartTileIndex(const TheaterDatabase& theaterDatabase, int tileSetIndex) {
    if (tileSetIndex < 0) {
        return -1;
    }

    const std::vector<TheaterTileSet>& tileSets = theaterDatabase.getTileSets();
    const auto it = std::find_if(
        tileSets.begin(),
        tileSets.end(),
        [tileSetIndex](const TheaterTileSet& tileSet) {
            return tileSet.index == tileSetIndex && tileSet.tilesInSet > 0;
        }
    );

    return it == tileSets.end() ? -1 : it->startTileIndex;
}

bool isLatTileSet(const TheaterDatabase& theaterDatabase, int tileSetIndex) {
    return tileSetIndex >= 0
        && (
            tileSetIndex == theaterDatabase.getGeneralTileSetIndex("RoughTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("SandTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("GreenTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("PaveTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("BlueMoldTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("CrystalTile")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("SwampTile")
        );
}

bool isClatTileSet(const TheaterDatabase& theaterDatabase, int tileSetIndex) {
    return tileSetIndex >= 0
        && (
            tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToRoughLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToSandLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToGreenLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToPaveLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToBlueMoldLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToCrystalLat")
            || tileSetIndex == theaterDatabase.getGeneralTileSetIndex("WaterToSwampLat")
        );
}

int latTileSetForClat(const TheaterDatabase& theaterDatabase, int tileSetIndex) {
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToRoughLat")) {
        return theaterDatabase.getGeneralTileSetIndex("RoughTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToSandLat")) {
        return theaterDatabase.getGeneralTileSetIndex("SandTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToGreenLat")) {
        return theaterDatabase.getGeneralTileSetIndex("GreenTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToPaveLat")) {
        return theaterDatabase.getGeneralTileSetIndex("PaveTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToBlueMoldLat")) {
        return theaterDatabase.getGeneralTileSetIndex("BlueMoldTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("ClearToCrystalLat")) {
        return theaterDatabase.getGeneralTileSetIndex("CrystalTile");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("WaterToSwampLat")) {
        return theaterDatabase.getGeneralTileSetIndex("SwampTile");
    }

    return -1;
}

int clatTileSetForLat(const TheaterDatabase& theaterDatabase, int tileSetIndex) {
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("RoughTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToRoughLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("SandTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToSandLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("GreenTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToGreenLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("PaveTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToPaveLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("BlueMoldTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToBlueMoldLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("CrystalTile")) {
        return theaterDatabase.getGeneralTileSetIndex("ClearToCrystalLat");
    }
    if (tileSetIndex == theaterDatabase.getGeneralTileSetIndex("SwampTile")) {
        return theaterDatabase.getGeneralTileSetIndex("WaterToSwampLat");
    }

    return -1;
}

bool latTileSetsConnect(
    const TheaterDatabase& theaterDatabase,
    int tileSetIndex,
    int neighborTileSetIndex
) {
    if (tileSetIndex < 0 || neighborTileSetIndex < 0 || tileSetIndex == neighborTileSetIndex) {
        return false;
    }

    const int greenTile = theaterDatabase.getGeneralTileSetIndex("GreenTile");
    const int shorePieces = theaterDatabase.getGeneralTileSetIndex("ShorePieces");
    const int waterBridge = theaterDatabase.getGeneralTileSetIndex("WaterBridge");
    const int paveTile = theaterDatabase.getGeneralTileSetIndex("PaveTile");
    const int pavedRoads = theaterDatabase.getGeneralTileSetIndex("PavedRoads");
    const int medians = theaterDatabase.getGeneralTileSetIndex("Medians");
    const int miscPaveTile = theaterDatabase.getGeneralTileSetIndex("MiscPaveTile");

    const auto pairMatches = [tileSetIndex, neighborTileSetIndex](int first, int second) {
        return first >= 0
            && second >= 0
            && (
                (tileSetIndex == first && neighborTileSetIndex == second)
                || (tileSetIndex == second && neighborTileSetIndex == first)
            );
    };

    if (pairMatches(greenTile, shorePieces) || pairMatches(greenTile, waterBridge)) {
        return false;
    }

    if (pairMatches(paveTile, pavedRoads)
        || pairMatches(paveTile, medians)
        || pairMatches(paveTile, miscPaveTile)
    ) {
        return false;
    }

    return true;
}

void applyAutoLat(
    std::vector<TerrainWorkTile>& workTiles,
    std::unordered_map<int, TheaterTileReference>& referencesByTileIndex,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    int mapWidth
) {
    if (mapWidth <= 0 || workTiles.empty()) {
        return;
    }

    std::unordered_map<int, std::size_t> workIndexByCell;
    workIndexByCell.reserve(workTiles.size());
    for (std::size_t i = 0; i < workTiles.size(); ++i) {
        const int cell = mapCellFromXY(workTiles[i].tile.x, workTiles[i].tile.y);
        if (cell >= 0) {
            workIndexByCell[cell] = i;
        }
    }

    for (TerrainWorkTile& workTile : workTiles) {
        if (isClatTileSet(theaterDatabase, workTile.baseTileSetIndex)) {
            const int latSet = latTileSetForClat(theaterDatabase, workTile.baseTileSetIndex);
            const int latTileIndex = findTileSetStartTileIndex(theaterDatabase, latSet);
            if (latTileIndex >= 0
                && ensureTileReference(referencesByTileIndex, theaterDatabase, tileRootPath, latTileIndex)
            ) {
                workTile.tile.tileIndex = latTileIndex;
                workTile.tile.subTileIndex = 0;
                workTile.baseTileSetIndex = latSet;
            }
        }
    }

    const int neighborOffsets[][3] = {
        {1, -1, 1},
        {1, 1, 2},
        {-1, 1, 4},
        {-1, -1, 8}
    };

    for (TerrainWorkTile& workTile : workTiles) {
        if (!isLatTileSet(theaterDatabase, workTile.baseTileSetIndex)) {
            continue;
        }

        const int clatSet = clatTileSetForLat(theaterDatabase, workTile.baseTileSetIndex);
        const int clatStartTileIndex = findTileSetStartTileIndex(theaterDatabase, clatSet);
        if (clatStartTileIndex < 0) {
            continue;
        }

        const int displayX = displayXForTile(workTile.tile, mapWidth);
        const int displayY = displayYForTile(workTile.tile, mapWidth);
        int transitionTile = 0;

        for (const auto& offset : neighborOffsets) {
            int neighborX = 0;
            int neighborY = 0;
            if (!mapCoordinatesFromDisplay(
                displayX + offset[0],
                displayY + offset[1],
                mapWidth,
                neighborX,
                neighborY
            )) {
                continue;
            }

            const int neighborCell = mapCellFromXY(neighborX, neighborY);
            const auto neighborIt = workIndexByCell.find(neighborCell);
            if (neighborIt == workIndexByCell.end()) {
                continue;
            }

            const TerrainWorkTile& neighborTile = workTiles[neighborIt->second];
            if (latTileSetsConnect(
                theaterDatabase,
                workTile.baseTileSetIndex,
                neighborTile.baseTileSetIndex
            )) {
                transitionTile += offset[2];
            }
        }

        if (transitionTile <= 0) {
            continue;
        }

        const int transitionTileIndex = clatStartTileIndex + transitionTile;
        if (!ensureTileReference(
            referencesByTileIndex,
            theaterDatabase,
            tileRootPath,
            transitionTileIndex
        )) {
            continue;
        }

        workTile.tile.tileIndex = transitionTileIndex;
        workTile.tile.subTileIndex = 0;
    }
}

std::vector<MapIsoTile> buildRenderableTerrainTiles(
    const MapDatabase& mapDatabase,
    std::unordered_map<int, TheaterTileReference>& referencesByTileIndex,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    int clearTileIndex,
    int waterFillTileIndex,
    int pavementTileIndex
) {
    const std::vector<MapIsoTile>& sourceTiles = mapDatabase.getIsoTiles();
    const MapRect fullSize = parseMapRect(mapDatabase.getSummary().size);
    const int mapWidth = fullSize.valid ? fullSize.width : 0;
    std::vector<TerrainWorkTile> workTiles;
    workTiles.reserve(sourceTiles.size());

    (void)waterFillTileIndex;
    (void)pavementTileIndex;

    const int emptyTileIndex = clearTileIndex;

    for (const MapIsoTile& sourceTile : sourceTiles) {
        MapIsoTile normalizedTile = normalizeTerrainTile(sourceTile, emptyTileIndex);
        const auto referenceIt = referencesByTileIndex.find(normalizedTile.tileIndex);
        workTiles.push_back({
            normalizedTile,
            referenceIt == referencesByTileIndex.end() ? -1 : referenceIt->second.tileSetIndex
        });
    }

    applyAutoLat(
        workTiles,
        referencesByTileIndex,
        theaterDatabase,
        tileRootPath,
        mapWidth
    );

    std::vector<MapIsoTile> renderTiles;
    renderTiles.reserve(workTiles.size());
    for (const TerrainWorkTile& workTile : workTiles) {
        renderTiles.push_back(workTile.tile);
    }

    return renderTiles;
}

int findTileLevelAt(
    const std::unordered_map<int, int>& levelByCell,
    int x,
    int y
) {
    const int cell = mapCellFromXY(x, y);
    if (cell < 0) {
        return 0;
    }

    const auto it = levelByCell.find(cell);
    return it == levelByCell.end() ? 0 : it->second;
}

std::unordered_map<std::string, int> buildStructureSpriteIds(
    const MapDatabase& mapDatabase,
    const MapStructureSpriteOptions& options,
    const TheaterDatabase& theaterDatabase,
    MapStructureSpriteStats& stats,
    std::vector<ShpFrameImage>& spriteImages
) {
    stats = {};
    std::unordered_map<std::string, int> spriteIdsByStructureType;

    if (options.rules == nullptr || options.art == nullptr || options.assetRootPath.empty()) {
        return spriteIdsByStructureType;
    }

    std::set<std::string> usedStructureTypes;
    for (const MapStructureObject& structure : mapDatabase.getStructures()) {
        usedStructureTypes.insert(structure.type);
    }

    stats.uniqueStructureTypes = static_cast<int>(usedStructureTypes.size());

    std::unordered_map<std::string, const BuildingType*> buildingsById;
    buildingsById.reserve(options.rules->getBuildingTypes().size());
    for (const BuildingType& building : options.rules->getBuildingTypes()) {
        buildingsById.emplace(building.id, &building);
    }

    std::unordered_map<std::string, ArtObjectLink> linksByBuildingId;
    for (const ArtObjectLink& link : options.art->buildLinks(*options.rules)) {
        if (link.objectKind == "building") {
            linksByBuildingId.emplace(link.objectId, link);
        }
    }

    std::vector<std::string> indexWarnings;
    std::unordered_map<std::string, std::vector<std::string>> filesByName;
    for (const std::string& root : structureAssetSearchRoots(options.assetRootPath)) {
        mergeFileIndex(filesByName, buildFileIndex(root, indexWarnings));
    }
    for (const std::string& warning : indexWarnings) {
        stats.warnings.push_back(warning);
    }

    std::unordered_map<std::string, int> spriteIdsByFileName;
    for (const std::string& structureType : usedStructureTypes) {
        const auto buildingIt = buildingsById.find(structureType);
        if (buildingIt == buildingsById.end()) {
            ++stats.missingSprites;
            stats.warnings.push_back("missing rules building for " + structureType);
            continue;
        }

        const auto linkIt = linksByBuildingId.find(structureType);
        if (linkIt == linksByBuildingId.end() || !linkIt->second.hasArt) {
            ++stats.missingSprites;
            stats.warnings.push_back("missing art section for " + structureType);
            continue;
        }

        if (linkIt->second.voxel) {
            ++stats.missingSprites;
            stats.warnings.push_back("structure uses voxel art, not SHP: " + structureType);
            continue;
        }

        const std::vector<std::string> candidateFiles =
            structureAssetFileCandidates(linkIt->second, theaterDatabase);
        if (candidateFiles.empty()) {
            ++stats.missingSprites;
            stats.warnings.push_back("missing SHP candidate for " + structureType);
            continue;
        }

        bool usedCachedSprite = false;
        for (const std::string& candidateFile : candidateFiles) {
            const auto cachedSpriteIt = spriteIdsByFileName.find(candidateFile);
            if (cachedSpriteIt != spriteIdsByFileName.end()) {
                spriteIdsByStructureType.emplace(structureType, cachedSpriteIt->second);
                usedCachedSprite = true;
                break;
            }
        }
        if (usedCachedSprite) {
            continue;
        }

        std::string selectedFileName;
        std::string selectedFilePath;
        for (const std::string& candidateFile : candidateFiles) {
            const auto fileIt = filesByName.find(candidateFile);
            if (fileIt != filesByName.end() && !fileIt->second.empty()) {
                selectedFileName = candidateFile;
                selectedFilePath = fileIt->second.front();
                break;
            }
        }

        if (selectedFilePath.empty()) {
            ++stats.missingSprites;
            stats.warnings.push_back(
                "missing SHP for "
                + structureType
                + " art="
                + (linkIt->second.artId.empty() ? "-" : linkIt->second.artId)
                + " image="
                + (linkIt->second.image.empty() ? "-" : linkIt->second.image)
                + " foundation="
                + (linkIt->second.foundation.empty() ? "-" : linkIt->second.foundation)
                + ": candidates="
                + formatCandidateList(candidateFiles)
            );
            continue;
        }

        if (isKnownPlaceholderStructureSprite(structureType, selectedFilePath)) {
            ++stats.missingSprites;
            stats.warnings.push_back(
                "placeholder SHP ignored for "
                + structureType
                + ": "
                + std::filesystem::path(selectedFilePath).filename().string()
            );
            continue;
        }

        ShpFrameImage image;
        std::string errorMessage;
        if (!decodeShpFirstFrame(selectedFilePath, image, errorMessage)) {
            ++stats.decodeFailures;
            stats.warnings.push_back(errorMessage);
            continue;
        }

        const ArtType* artType = options.art->findArtType(linkIt->second.artId);
        image.structureType = structureType;
        image.artId = linkIt->second.artId;
        image.imageName = linkIt->second.image;
        image.foundation = linkIt->second.foundation;
        image.candidateFiles = formatCandidateList(candidateFiles);
        image.activeAnimations = formatActiveAnimationSummary(artType);
        image.anchorAdvice = formatStructureAnchorAdvice(linkIt->second, image);

        const int spriteId = static_cast<int>(spriteImages.size());
        stats.warnings.push_back(formatStructureAnchorWarning(
            structureType,
            linkIt->second,
            artType,
            image,
            candidateFiles
        ));
        spriteImages.push_back(std::move(image));
        spriteIdsByFileName.emplace(selectedFileName, spriteId);
        spriteIdsByStructureType.emplace(structureType, spriteId);
        ++stats.embeddedSprites;
    }

    stats.warnings.push_back(
        "building art coverage: types="
        + std::to_string(stats.uniqueStructureTypes)
        + " embedded="
        + std::to_string(stats.embeddedSprites)
        + " missing="
        + std::to_string(stats.missingSprites)
        + " decodeFailures="
        + std::to_string(stats.decodeFailures)
    );

    return spriteIdsByStructureType;
}

std::unordered_map<std::string, StructureFootprint> buildStructureFootprints(
    const MapDatabase& mapDatabase,
    const MapStructureSpriteOptions& options
) {
    std::unordered_map<std::string, StructureFootprint> footprintsByStructureType;
    if (options.rules == nullptr || options.art == nullptr) {
        return footprintsByStructureType;
    }

    std::set<std::string> usedStructureTypes;
    for (const MapStructureObject& structure : mapDatabase.getStructures()) {
        usedStructureTypes.insert(structure.type);
    }

    std::unordered_map<std::string, ArtObjectLink> linksByBuildingId;
    for (const ArtObjectLink& link : options.art->buildLinks(*options.rules)) {
        if (link.objectKind == "building") {
            linksByBuildingId.emplace(link.objectId, link);
        }
    }

    for (const std::string& structureType : usedStructureTypes) {
        const auto linkIt = linksByBuildingId.find(structureType);
        if (linkIt != linksByBuildingId.end() && !linkIt->second.foundation.empty()) {
            footprintsByStructureType.emplace(
                structureType,
                parseFoundationSize(linkIt->second.foundation)
            );
            continue;
        }

        const ArtType* directArt = options.art->findArtType(structureType);
        if (directArt != nullptr && !directArt->foundation.empty()) {
            footprintsByStructureType.emplace(
                structureType,
                parseFoundationSize(directArt->foundation)
            );
        }
    }

    return footprintsByStructureType;
}

int objectDrawDepth(const TmpHtmlObjectRecord& object) {
    int anchorX = object.x;
    int anchorY = object.y;

    if (object.kind == 1) {
        anchorX += std::max(0, object.footprintWidth - 1);
        anchorY += std::max(0, object.footprintHeight - 1);
    }

    return anchorX + anchorY + object.level;
}

void sortTmpHtmlObjectRecords(std::vector<TmpHtmlObjectRecord>& objects) {
    std::sort(
        objects.begin(),
        objects.end(),
        [](const TmpHtmlObjectRecord& left, const TmpHtmlObjectRecord& right) {
            const int leftDepth = objectDrawDepth(left);
            const int rightDepth = objectDrawDepth(right);
            if (leftDepth != rightDepth) {
                return leftDepth < rightDepth;
            }

            if (left.y != right.y) {
                return left.y < right.y;
            }

            if (left.x != right.x) {
                return left.x < right.x;
            }

            return left.kind < right.kind;
        }
    );
}

std::vector<TmpHtmlObjectRecord> buildTmpHtmlObjectRecords(
    const MapDatabase& mapDatabase,
    const std::unordered_map<int, int>& levelByCell,
    const std::unordered_map<std::string, int>& structureSpriteIds,
    const std::unordered_map<std::string, StructureFootprint>& structureFootprints
) {
    std::vector<TmpHtmlObjectRecord> objects;
    objects.reserve(
        mapDatabase.getTerrainObjects().size()
        + mapDatabase.getStructures().size()
        + mapDatabase.getUnits().size()
        + mapDatabase.getInfantry().size()
    );

    for (const MapTerrainObject& terrain : mapDatabase.getTerrainObjects()) {
        if (!terrain.position.valid) {
            continue;
        }

        TmpHtmlObjectRecord record;
        record.kind = 0;
        record.x = terrain.position.x;
        record.y = terrain.position.y;
        record.level = findTileLevelAt(levelByCell, record.x, record.y);
        record.id = terrain.cell;
        record.type = terrain.type;
        objects.push_back(record);
    }

    for (const MapStructureObject& structure : mapDatabase.getStructures()) {
        if (!structure.position.valid) {
            continue;
        }

        TmpHtmlObjectRecord record;
        record.kind = 1;
        record.x = structure.position.x;
        record.y = structure.position.y;
        record.level = findTileLevelAt(levelByCell, record.x, record.y);
        record.health = structure.health;
        record.facing = structure.facing;
        record.id = structure.id;
        record.owner = structure.owner;
        record.type = structure.type;
        record.tag = structure.tag;
        const auto spriteIt = structureSpriteIds.find(structure.type);
        if (spriteIt != structureSpriteIds.end()) {
            record.spriteId = spriteIt->second;
        }
        const auto footprintIt = structureFootprints.find(structure.type);
        if (footprintIt != structureFootprints.end()) {
            record.footprintWidth = footprintIt->second.width;
            record.footprintHeight = footprintIt->second.height;
        }
        objects.push_back(record);
    }

    for (const MapUnitObject& unit : mapDatabase.getUnits()) {
        if (!unit.position.valid) {
            continue;
        }

        TmpHtmlObjectRecord record;
        record.kind = 2;
        record.x = unit.position.x;
        record.y = unit.position.y;
        record.level = findTileLevelAt(levelByCell, record.x, record.y);
        record.health = unit.health;
        record.facing = unit.facing;
        record.id = unit.id;
        record.owner = unit.owner;
        record.type = unit.type;
        record.mission = unit.mission;
        record.tag = unit.tag;
        record.onBridge = unit.onBridge;
        objects.push_back(record);
    }

    for (const MapInfantryObject& infantry : mapDatabase.getInfantry()) {
        if (!infantry.position.valid) {
            continue;
        }

        TmpHtmlObjectRecord record;
        record.kind = 3;
        record.x = infantry.position.x;
        record.y = infantry.position.y;
        record.level = findTileLevelAt(levelByCell, record.x, record.y);
        record.health = infantry.health;
        record.facing = infantry.facing;
        record.subCell = infantry.subCell;
        record.id = infantry.id;
        record.owner = infantry.owner;
        record.type = infantry.type;
        record.mission = infantry.mission;
        record.tag = infantry.tag;
        record.onBridge = infantry.onBridge;
        objects.push_back(record);
    }

    sortTmpHtmlObjectRecords(objects);
    return objects;
}

std::string escapeHtmlText(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

std::string escapeJavaScriptString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20) {
                escaped += "?";
            } else {
                escaped += static_cast<char>(ch);
            }
            break;
        }
    }

    return escaped;
}

std::string base64Encode(const std::vector<unsigned char>& bytes) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size();) {
        const std::uint32_t first = bytes[i++];
        const bool hasSecond = i < bytes.size();
        const std::uint32_t second = hasSecond ? bytes[i++] : 0;
        const bool hasThird = i < bytes.size();
        const std::uint32_t third = hasThird ? bytes[i++] : 0;
        const std::uint32_t packed = (first << 16) | (second << 8) | third;

        encoded += alphabet[(packed >> 18) & 0x3FU];
        encoded += alphabet[(packed >> 12) & 0x3FU];
        encoded += hasSecond ? alphabet[(packed >> 6) & 0x3FU] : '=';
        encoded += hasThird ? alphabet[packed & 0x3FU] : '=';
    }

    return encoded;
}

int overlayKindValue(const MapOverlayCell* overlay) {
    if (overlay == nullptr) {
        return 0;
    }

    if (overlay->isGem) {
        return 2;
    }

    if (overlay->isOre) {
        return 1;
    }

    if (isLowBridgeOverlay(*overlay) || isHighBridgeOverlay(*overlay)) {
        return 3;
    }

    const std::string lowerName =
        toLowerAscii(overlay->type + " " + overlay->image);
    if (lowerName.find("wall") != std::string::npos) {
        return 4;
    }

    return 5;
}

std::string overlayKindName(const MapOverlayCell& overlay) {
    if (overlay.isGem) {
        return "gem";
    }
    if (overlay.isOre) {
        return "ore";
    }
    if (isLowBridgeOverlay(overlay) || isHighBridgeOverlay(overlay)) {
        return "bridge";
    }

    const std::string lowerName =
        toLowerAscii(overlay.type + " " + overlay.image);
    if (lowerName.find("wall") != std::string::npos) {
        return "wall";
    }

    return "other";
}

std::string overlayPaletteHint(const MapOverlayCell& overlay) {
    const std::string kind = overlayKindName(overlay);
    if (kind == "ore" || kind == "gem" || kind == "bridge") {
        return "theater terrain palette";
    }
    if (kind == "wall") {
        return "theater wall/overlay palette";
    }
    return "theater TMP or overlay SHP palette";
}

std::string overlayRendererLayerHint(const MapOverlayCell& overlay) {
    const std::string kind = overlayKindName(overlay);
    if (kind == "ore" || kind == "gem") {
        return "resource-overlay not bridge";
    }
    if (kind == "bridge") {
        return "bridge-overlay not ore";
    }
    if (kind == "wall") {
        return "wall-overlay";
    }
    return "ordinary-overlay";
}

struct OverlayResourceCoverage {
    int overlayCells = 0;
    int oreCells = 0;
    int gemCells = 0;
    int bridgeCells = 0;
    int wallCells = 0;
    int otherCells = 0;
    int oreBridgeOverlapCells = 0;
    int candidateCells = 0;
    int resolvedCells = 0;
    int missingCells = 0;
    int looseResolvedCells = 0;
    int mixResolvedCells = 0;
    int decodedCells = 0;
    int decodeFailedCells = 0;
    int oreResolvedCells = 0;
    int gemResolvedCells = 0;
    int oreMissingCells = 0;
    int gemMissingCells = 0;
    int bridgeResolvedCells = 0;
    int bridgeMissingCells = 0;
    int bridgeDecodeFailedCells = 0;
    std::vector<std::string> details;
};

struct OverlayResourceGroup {
    MapOverlayCell overlay;
    int cellCount = 0;
};

struct OverlayResourceProbe {
    bool found = false;
    bool decoded = false;
    bool sourceIsMix = false;
    std::string candidate;
    std::string source;
    std::string decodeResult;
    std::string missSummary;
};

std::string overlayResourceGroupKey(const MapOverlayCell& overlay) {
    const std::string lowBridgeSegment =
        lowBridgeSegmentBaseNameForOverlay(overlay);
    return std::to_string(overlay.overlayIndex)
        + "|"
        + overlay.type
        + "|"
        + overlay.image
        + "|"
        + (lowBridgeSegment.empty()
            ? std::to_string(overlayResourceDecodeFrame(overlay))
            : ("segment:" + lowBridgeSegment))
        + "|raw="
        + std::to_string(overlay.frameIndex)
        + "|"
        + overlayKindName(overlay);
}

std::string overlayResourceLabel(const MapOverlayCell& overlay) {
    std::string label = overlay.type.empty() ? "-" : overlay.type;
    if (!overlay.image.empty() && overlay.image != overlay.type) {
        label += "/";
        label += overlay.image;
    }

    label += " frame ";
    label += std::to_string(overlayResourceDecodeFrame(overlay));
    return label;
}

bool findIndexedCandidateFile(
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<std::string>& candidates,
    std::string& resolvedPath
) {
    for (const std::string& candidate : candidates) {
        const auto fileIt = filesByName.find(toLowerAscii(candidate));
        if (fileIt != filesByName.end() && !fileIt->second.empty()) {
            resolvedPath = fileIt->second.front();
            return true;
        }
    }

    resolvedPath.clear();
    return false;
}

bool isTmpLikeExtension(const std::string& extension) {
    return extension == ".tem"
        || extension == ".urb"
        || extension == ".ubn"
        || extension == ".des"
        || extension == ".sno"
        || extension == ".lun"
        || extension == ".tmp";
}

std::string decodeOverlayTmpResource(
    const MapOverlayCell& overlay,
    const TmpTileFile& tileFile
) {
    const int resolvedFrame = resolveOverlayTmpImageIndex(overlay, tileFile);
    if (resolvedFrame < 0) {
        return "decode=fail tmp slots="
            + std::to_string(tileFile.images.size())
            + " requestedFrame="
            + std::to_string(overlayResourceDecodeFrame(overlay))
            + " no-present-frame";
    }

    const TmpSubTileImage& image = tileFile.images[static_cast<std::size_t>(resolvedFrame)];
    std::string result =
        "decode=ok tmp slots="
        + std::to_string(tileFile.images.size())
        + " requestedFrame="
        + std::to_string(overlayResourceDecodeFrame(overlay))
        + " resolvedFrame="
        + std::to_string(resolvedFrame)
        + " size="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " offset="
        + std::to_string(image.offsetX)
        + ","
        + std::to_string(image.offsetY);

    if (resolvedFrame != overlayResourceDecodeFrame(overlay)) {
        result += " fallback=yes";
    }

    return result;
}

std::string decodeOverlayResourceFromBytes(concord::assets::MapPreviewResourceSession& session,
    const MapOverlayCell& overlay,
    const std::string& candidate,
    const std::vector<unsigned char>& bytes,
    const std::string& sourceLabel,
    bool& decoded
) {
    decoded = false;
    const std::string extension =
        toLowerAscii(std::filesystem::path(candidate).extension().string());

    if (isTmpLikeExtension(extension)) {
        auto& tmpDecodeCache = session.cache<TmpTileFile>(concord::assets::MapPreviewCacheDomain::OverlayTmp);
        auto& tmpErrorCache = session.cache<std::string>(concord::assets::MapPreviewCacheDomain::OverlayTmpError);

        const std::string cacheKey =
            "tmp:"
            + sourceLabel
            + "#"
            + candidate
            + "#"
            + std::to_string(bytes.size());
        const auto cachedTile = tmpDecodeCache.find(cacheKey);
        if (cachedTile != tmpDecodeCache.end()) {
            const std::string result = decodeOverlayTmpResource(overlay, cachedTile->second);
            decoded = result.rfind("decode=ok", 0) == 0;
            return result + " cache=hit";
        }

        const auto cachedError = tmpErrorCache.find(cacheKey);
        if (cachedError != tmpErrorCache.end()) {
            return cachedError->second + " cache=hit";
        }

        TmpTileFile tileFile;
        std::string errorMessage;
        if (!loadTmpTileFileFromBytes(bytes, sourceLabel, tileFile, errorMessage)) {
            const std::string result = "decode=fail tmp error=" + errorMessage;
            tmpErrorCache.emplace(cacheKey, result);
            return result;
        }

        const std::string result = decodeOverlayTmpResource(overlay, tileFile);
        decoded = result.rfind("decode=ok", 0) == 0;
        tmpDecodeCache.emplace(cacheKey, std::move(tileFile));
        return result;
    }

    if (extension == ".shp") {
        auto& shpFrameCache = session.cache<ShpFrameImage>(concord::assets::MapPreviewCacheDomain::OverlayShpFrame);
        auto& shpErrorCache = session.cache<std::string>(concord::assets::MapPreviewCacheDomain::OverlayShpError);

        const std::string baseCacheKey =
            "shp:"
            + sourceLabel
            + "#"
            + candidate
            + "#"
            + std::to_string(bytes.size());
        const auto decodeCachedFrame =
            [&bytes, &sourceLabel, &baseCacheKey, &shpFrameCache, &shpErrorCache](
                int frameIndex,
                ShpFrameImage& image,
                std::string& errorMessage
            ) {
                const std::string frameCacheKey =
                    baseCacheKey + "#" + std::to_string(frameIndex);
                const auto cachedFrame = shpFrameCache.find(frameCacheKey);
                if (cachedFrame != shpFrameCache.end()) {
                    image = cachedFrame->second;
                    errorMessage.clear();
                    return true;
                }

                const auto cachedError = shpErrorCache.find(frameCacheKey);
                if (cachedError != shpErrorCache.end()) {
                    errorMessage = cachedError->second;
                    return false;
                }

                if (!decodeShpFrameFromBytes(bytes, sourceLabel, frameIndex, image, errorMessage)) {
                    shpErrorCache.emplace(frameCacheKey, errorMessage);
                    return false;
                }

                shpFrameCache.emplace(frameCacheKey, image);
                return true;
            };

        ShpFrameImage image;
        std::string errorMessage;
        const int requestedFrame = overlayResourceDecodeFrame(overlay);
        int resolvedFrame = requestedFrame;
        if (!decodeCachedFrame(requestedFrame, image, errorMessage)) {
            if (requestedFrame != 0
                && decodeCachedFrame(0, image, errorMessage)) {
                resolvedFrame = 0;
            } else {
                return "decode=fail shp error=" + errorMessage;
            }
        }

        decoded = true;
        std::string result =
            "decode=ok shp requestedFrame="
            + std::to_string(requestedFrame)
            + " resolvedFrame="
            + std::to_string(resolvedFrame)
            + " size="
            + std::to_string(image.width)
            + "x"
            + std::to_string(image.height)
            + " global="
            + std::to_string(image.globalWidth)
            + "x"
            + std::to_string(image.globalHeight)
            + " offset="
            + std::to_string(image.offsetX)
            + ","
            + std::to_string(image.offsetY)
            + " format="
            + std::to_string(image.format);

        if (resolvedFrame != requestedFrame) {
            result += " fallback=yes";
        }

        return result;
    }

    return "decode=skip unsupported-extension=" + (extension.empty() ? "(none)" : extension);
}

std::string decodeOverlayResourceFromPath(concord::assets::MapPreviewResourceSession& session,
    const MapOverlayCell& overlay,
    const std::string& candidate,
    const std::string& path,
    bool& decoded
) {
    decoded = false;
    std::vector<unsigned char> bytes;
    std::string errorMessage;
    if (!session.readLooseBytes(concord::assets::MapPreviewCacheDomain::OverlayLooseBytes,
            path, bytes, errorMessage)) {
        return "decode=fail read error=" + errorMessage;
    }
    return decodeOverlayResourceFromBytes(session,overlay, candidate, bytes, path, decoded);
}

OverlayResourceProbe probeOverlayResource(concord::assets::MapPreviewResourceSession& session,
    const MapOverlayCell& overlay,
    const std::vector<std::string>& candidates,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives
) {
    OverlayResourceProbe probe;
    std::vector<std::string> candidateIds;

    for (const std::string& candidate : candidates) {
        candidateIds.push_back(candidate + "=" + formatMixId(mixFileIdFromName(candidate)));

        const auto fileIt = filesByName.find(toLowerAscii(candidate));
        if (fileIt != filesByName.end() && !fileIt->second.empty()) {
            probe.found = true;
            probe.candidate = candidate;
            probe.source = "loose:" + fileIt->second.front();
            probe.decodeResult = decodeOverlayResourceFromPath(session,
                overlay,
                candidate,
                fileIt->second.front(),
                probe.decoded
            );
            return probe;
        }

        for (const IndexedMixArchive& indexedMix : mixArchives) {
            const MixEntry* entry = findIndexedMixEntryByName(indexedMix, candidate);
            if (entry == nullptr) {
                continue;
            }

            std::vector<unsigned char> bytes;
            std::string errorMessage;
            probe.found = true;
            probe.sourceIsMix = true;
            probe.candidate = candidate;
            probe.source =
                "mix:"
                + indexedMix.path
                + "!"
                + candidate
                + " id="
                + formatMixId(mixFileIdFromName(candidate))
                + " size="
                + std::to_string(entry->size);

            auto& mixBytesCache = session.cache<std::vector<unsigned char>>(concord::assets::MapPreviewCacheDomain::OverlayMixBytes);
            const std::string bytesCacheKey =
                indexedMix.path
                + "!"
                + formatMixId(entry->id)
                + "#"
                + std::to_string(entry->size);
            const auto cachedBytes = mixBytesCache.find(bytesCacheKey);
            if (cachedBytes != mixBytesCache.end()) {
                bytes = cachedBytes->second;
            } else {
                std::vector<std::uint8_t> data;
                if (!indexedMix.archive.readEntryData(*entry, data, errorMessage)) {
                    probe.decodeResult = "decode=fail mix-read error=" + errorMessage;
                    return probe;
                }

                bytes.assign(data.begin(), data.end());
                mixBytesCache.emplace(bytesCacheKey, bytes);
            }

            probe.decodeResult = decodeOverlayResourceFromBytes(session,
                overlay,
                candidate,
                bytes,
                indexedMix.path + "!" + candidate,
                probe.decoded
            );
            return probe;
        }
    }

    probe.missSummary =
        "exactHashMiss mixArchives="
        + std::to_string(mixArchives.size())
        + " ids="
        + formatFullCandidateList(candidateIds);
    return probe;
}

struct ResourceDiagnosticProbe {
    bool found = false;
    bool decoded = false;
    bool sourceIsMix = false;
    std::string candidate;
    std::string source;
    std::string decodeResult;
    std::string missSummary;
};

int diagnosticVisibleMainFrameCount(unsigned int frameCount) {
    if (frameCount == 0) {
        return 0;
    }

    if (frameCount >= 4 && (frameCount % 2U) == 0U) {
        return static_cast<int>(std::max(1U, frameCount / 2U));
    }

    return static_cast<int>(std::max(1U, frameCount));
}

std::vector<std::string> resourceFileCandidates(
    const std::string& baseName,
    const std::vector<std::string>& extensions
) {
    std::vector<std::string> candidates;
    const std::string rawName = trimAscii(baseName);
    if (rawName.empty()) {
        return candidates;
    }

    const std::filesystem::path rawPath(rawName);
    const std::string rawFileOnly = rawPath.filename().string();
    const std::string rawExtension =
        toLowerAscii(rawPath.extension().string());
    const std::string stem = rawPath.stem().empty()
        ? rawFileOnly
        : rawPath.stem().string();

    if (!rawExtension.empty()) {
        addUniqueCandidate(candidates, toLowerAscii(rawFileOnly));
    }

    const std::string lowerStem = toLowerAscii(stem);
    for (const std::string& extension : extensions) {
        addUniqueCandidate(candidates, lowerStem + extension);
    }

    return candidates;
}

std::vector<std::string> shpResourceCandidates(const std::string& baseName) {
    return resourceFileCandidates(baseName, {".shp"});
}

std::vector<std::string> graphicResourceCandidates(const std::string& baseName) {
    return resourceFileCandidates(baseName, {".shp", ".vxl", ".hva"});
}

std::vector<std::string> paletteResourceCandidates(const std::string& baseName) {
    return resourceFileCandidates(baseName, {".pal"});
}

std::string decodeDiagnosticTmpResource(
    const TmpTileFile& tileFile,
    int requestedFrame
) {
    int resolvedFrame = -1;
    if (requestedFrame >= 0
        && static_cast<std::size_t>(requestedFrame) < tileFile.images.size()
        && tileFile.images[static_cast<std::size_t>(requestedFrame)].present
    ) {
        resolvedFrame = requestedFrame;
    } else {
        resolvedFrame = firstPresentTmpImageIndex(tileFile);
    }

    if (resolvedFrame < 0) {
        return "decode=fail tmp slots="
            + std::to_string(tileFile.images.size())
            + " requestedFrame="
            + std::to_string(requestedFrame)
            + " no-present-frame";
    }

    const TmpSubTileImage& image = tileFile.images[static_cast<std::size_t>(resolvedFrame)];
    std::string result =
        "decode=ok tmp slots="
        + std::to_string(tileFile.images.size())
        + " requestedFrame="
        + std::to_string(requestedFrame)
        + " resolvedFrame="
        + std::to_string(resolvedFrame)
        + " size="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " offset="
        + std::to_string(image.offsetX)
        + ","
        + std::to_string(image.offsetY)
        + " base="
        + std::to_string(image.baseWidth)
        + "x"
        + std::to_string(image.baseHeight)
        + " extra="
        + (image.hasExtra ? "yes" : "no");

    if (resolvedFrame != requestedFrame) {
        result += " fallback=yes";
    }

    return result;
}

std::string decodeDiagnosticResourceFromBytes(concord::assets::MapPreviewResourceSession& session,
    const std::string& candidate,
    const std::vector<unsigned char>& bytes,
    const std::string& sourceLabel,
    int requestedFrame,
    bool& decoded
) {
    decoded = false;
    const std::string extension =
        toLowerAscii(std::filesystem::path(candidate).extension().string());
    auto& decodeResultCache = session.cache<std::pair<std::string, bool>>(concord::assets::MapPreviewCacheDomain::DiagnosticResult);
    const std::string cacheKey =
        sourceLabel
        + "#"
        + toLowerAscii(candidate)
        + "#"
        + std::to_string(requestedFrame)
        + "#"
        + std::to_string(bytes.size());
    const auto cachedResult = decodeResultCache.find(cacheKey);
    if (cachedResult != decodeResultCache.end()) {
        decoded = cachedResult->second.second;
        return cachedResult->second.first + " cache=hit";
    }

    if (isTmpLikeExtension(extension)) {
        TmpTileFile tileFile;
        std::string errorMessage;
        if (!loadTmpTileFileFromBytes(bytes, sourceLabel, tileFile, errorMessage)) {
            const std::string result = "decode=fail tmp error=" + errorMessage;
            decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
            return result;
        }

        const std::string result = decodeDiagnosticTmpResource(tileFile, requestedFrame);
        decoded = result.rfind("decode=ok", 0) == 0;
        const std::string summary = result + " palette=indexed-theater";
        decodeResultCache.emplace(cacheKey, std::make_pair(summary, decoded));
        return summary;
    }

    if (extension == ".shp") {
        const unsigned int frameCount =
            bytes.size() >= 8 ? readUInt16(bytes, 6) : 0;
        ShpFrameImage image;
        std::string errorMessage;
        const int requested = requestedFrame < 0 ? 0 : requestedFrame;
        int resolvedFrame = requested;
        if (!decodeShpFrameFromBytes(bytes, sourceLabel, requested, image, errorMessage)) {
            if (requested != 0
                && decodeShpFrameFromBytes(bytes, sourceLabel, 0, image, errorMessage)
            ) {
                resolvedFrame = 0;
            } else {
                const std::string result = "decode=fail shp error=" + errorMessage;
                decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
                return result;
            }
        }

        decoded = true;
        std::string result =
            "decode=ok shp requestedFrame="
            + std::to_string(requested)
            + " resolvedFrame="
            + std::to_string(resolvedFrame)
            + " frameCount="
            + std::to_string(frameCount)
            + " visibleMainFrames="
            + std::to_string(diagnosticVisibleMainFrameCount(frameCount))
            + " size="
            + std::to_string(image.width)
            + "x"
            + std::to_string(image.height)
            + " global="
            + std::to_string(image.globalWidth)
            + "x"
            + std::to_string(image.globalHeight)
            + " offset="
            + std::to_string(image.offsetX)
            + ","
            + std::to_string(image.offsetY)
            + " format="
            + std::to_string(image.format)
            + " dataOffset="
            + std::to_string(image.dataOffset)
            + " palette=indexed-external";

        if (resolvedFrame != requested) {
            result += " fallback=yes";
        }

        decodeResultCache.emplace(cacheKey, std::make_pair(result, decoded));
        return result;
    }

    if (extension == ".pal") {
        decoded = bytes.size() == 768;
        const std::string result =
            std::string(decoded ? "decode=ok pal colors=256" : "decode=fail pal size=")
            + (decoded ? "" : std::to_string(bytes.size()));
        decodeResultCache.emplace(cacheKey, std::make_pair(result, decoded));
        return result;
    }

    if (extension == ".vxl") {
        const std::string result = "decode=skip vxl size="
            + std::to_string(bytes.size())
            + " palette=embedded-vxl";
        decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
        return result;
    }

    if (extension == ".hva") {
        const std::string result = "decode=skip hva size=" + std::to_string(bytes.size());
        decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
        return result;
    }

    if (extension == ".pcx") {
        const std::string result = "decode=skip pcx size="
            + std::to_string(bytes.size())
            + " palette=pcx-or-ui";
        decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
        return result;
    }

    if (extension == ".mix") {
        const std::string result = "decode=skip nested-mix size=" + std::to_string(bytes.size());
        decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
        return result;
    }

    const std::string result =
        "decode=skip unsupported-extension=" + (extension.empty() ? "(none)" : extension);
    decodeResultCache.emplace(cacheKey, std::make_pair(result, false));
    return result;
}

std::string decodeDiagnosticResourceFromPath(concord::assets::MapPreviewResourceSession& session,
    const std::string& candidate,
    const std::string& path,
    int requestedFrame,
    bool& decoded
) {
    decoded = false;
    std::vector<unsigned char> bytes;
    std::string errorMessage;
    if (!session.readLooseBytes(concord::assets::MapPreviewCacheDomain::DiagnosticLooseBytes,
            path, bytes, errorMessage)) {
        return "decode=fail read error=" + errorMessage;
    }
    return decodeDiagnosticResourceFromBytes(session,candidate, bytes, path, requestedFrame, decoded);
}

ResourceDiagnosticProbe probeDiagnosticResource(concord::assets::MapPreviewResourceSession& session,
    const std::vector<std::string>& candidates,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    int requestedFrame
) {
    ResourceDiagnosticProbe probe;
    std::vector<std::string> candidateIds;

    for (const std::string& candidate : candidates) {
        candidateIds.push_back(candidate + "=" + formatMixId(mixFileIdFromName(candidate)));

        const auto fileIt = filesByName.find(toLowerAscii(candidate));
        if (fileIt != filesByName.end() && !fileIt->second.empty()) {
            probe.found = true;
            probe.candidate = candidate;
            probe.source = "loose:" + fileIt->second.front();
            probe.decodeResult = decodeDiagnosticResourceFromPath(session,
                candidate,
                fileIt->second.front(),
                requestedFrame,
                probe.decoded
            );
            return probe;
        }

        for (const IndexedMixArchive& indexedMix : mixArchives) {
            const MixEntry* entry = findIndexedMixEntryByName(indexedMix, candidate);
            if (entry == nullptr) {
                continue;
            }

            std::vector<unsigned char> bytes;
            std::string errorMessage;
            probe.found = true;
            probe.sourceIsMix = true;
            probe.candidate = candidate;
            probe.source =
                "mix:"
                + indexedMix.path
                + "!"
                + candidate
                + " id="
                + formatMixId(mixFileIdFromName(candidate))
                + " size="
                + std::to_string(entry->size);

            auto& mixBytesCache = session.cache<std::vector<unsigned char>>(concord::assets::MapPreviewCacheDomain::DiagnosticMixBytes);
            const std::string bytesCacheKey =
                indexedMix.path
                + "!"
                + formatMixId(entry->id)
                + "#"
                + std::to_string(entry->size);
            const auto cachedBytes = mixBytesCache.find(bytesCacheKey);
            if (cachedBytes != mixBytesCache.end()) {
                bytes = cachedBytes->second;
            } else {
                std::vector<std::uint8_t> data;
                if (!indexedMix.archive.readEntryData(*entry, data, errorMessage)) {
                    probe.decodeResult = "decode=fail mix-read error=" + errorMessage;
                    return probe;
                }
                bytes.assign(data.begin(), data.end());
                mixBytesCache.emplace(bytesCacheKey, bytes);
            }

            probe.decodeResult = decodeDiagnosticResourceFromBytes(session,
                candidate,
                bytes,
                indexedMix.path + "!" + candidate,
                requestedFrame,
                probe.decoded
            );
            return probe;
        }
    }

    probe.missSummary =
        "exactHashMiss mixArchives="
        + std::to_string(mixArchives.size())
        + " ids="
        + formatFullCandidateList(candidateIds);
    return probe;
}

std::string formatProbeForReport(
    const ResourceDiagnosticProbe& probe,
    const std::vector<std::string>& candidates
) {
    if (probe.found) {
        return "candidates="
            + formatFullCandidateList(candidates)
            + " selected="
            + probe.candidate
            + " source="
            + probe.source
            + " "
            + probe.decodeResult;
    }

    return "candidates="
        + formatFullCandidateList(candidates)
        + " source=missing "
        + probe.missSummary
        + " decode=not-attempted";
}

ResourceDiagnosticProbe appendResourceDiagnostic(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const std::string& label,
    const std::vector<std::string>& candidates,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    int requestedFrame,
    const std::string& paletteStatus
) {
    if (candidates.empty()) {
        stats.warnings.push_back(
            label
            + ": candidates=(none) source=missing decode=not-attempted palette="
            + paletteStatus
        );
        return {};
    }

    const ResourceDiagnosticProbe probe =
        probeDiagnosticResource(session,candidates, filesByName, mixArchives, requestedFrame);
    stats.warnings.push_back(
        label
        + ": "
        + formatProbeForReport(probe, candidates)
        + " palette="
        + paletteStatus
    );
    return probe;
}

const BuildingType* findBuildingTypeById(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLowerAscii(id);
    for (const BuildingType& building : rules.getBuildingTypes()) {
        if (toLowerAscii(building.id) == lowerId) {
            return &building;
        }
    }

    return nullptr;
}

const UnitType* findUnitTypeById(
    const std::vector<UnitType>& units,
    const std::string& id
) {
    const std::string lowerId = toLowerAscii(id);
    for (const UnitType& unit : units) {
        if (toLowerAscii(unit.id) == lowerId) {
            return &unit;
        }
    }

    return nullptr;
}

const WeaponType* findWeaponTypeById(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLowerAscii(id);
    for (const WeaponType& weapon : rules.getWeaponTypes()) {
        if (toLowerAscii(weapon.id) == lowerId) {
            return &weapon;
        }
    }

    return nullptr;
}

const ProjectileType* findProjectileTypeById(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLowerAscii(id);
    for (const ProjectileType& projectile : rules.getProjectileTypes()) {
        if (toLowerAscii(projectile.id) == lowerId) {
            return &projectile;
        }
    }

    return nullptr;
}

std::unordered_map<std::string, ArtObjectLink> buildObjectLinksById(
    const RulesDatabase& rules,
    const ArtDatabase& art,
    const std::string& objectKind
) {
    std::unordered_map<std::string, ArtObjectLink> linksById;
    for (const ArtObjectLink& link : art.buildLinks(rules)) {
        if (link.objectKind == objectKind) {
            linksById.emplace(toLowerAscii(link.objectId), link);
        }
    }

    return linksById;
}

std::string paletteStatusForArt(
    const ArtType* artType,
    const std::string& fallback
) {
    if (artType != nullptr) {
        if (!artType->palette.empty()) {
            return artType->palette;
        }
        if (!artType->altPalette.empty()) {
            return "alt=" + artType->altPalette;
        }
        if (artType->animPalette) {
            return "anim.pal";
        }
        if (artType->terrainPalette) {
            return "theater terrain palette";
        }
        if (artType->remapable) {
            return fallback.empty() ? "external remap palette" : fallback;
        }
    }

    return fallback.empty() ? "undetermined" : fallback;
}

int animationProbeFrame(const ArtType* artType) {
    if (artType != nullptr && artType->hasAnimationStart) {
        return std::max(0, artType->animationStart);
    }

    return 0;
}

std::string animationFrameWindow(const ArtType* artType) {
    if (artType == nullptr) {
        return "frameWindow=unknown";
    }

    std::string result = "frameWindow=";
    if (artType->hasAnimationStart) {
        result += "start=" + std::to_string(artType->animationStart);
    } else {
        result += "start=0(default)";
    }
    if (artType->hasAnimationLoopStart) {
        result += " loopStart=" + std::to_string(artType->animationLoopStart);
    }
    if (artType->hasAnimationLoopEnd) {
        result += " loopEnd=" + std::to_string(artType->animationLoopEnd);
    }
    if (artType->hasAnimationEnd) {
        result += " end=" + std::to_string(artType->animationEnd);
    }
    return result;
}

std::vector<std::string> artShpCandidates(
    const ArtDatabase& art,
    const std::string& artId
) {
    const ArtType* artType = art.findArtType(artId);
    const std::string imageName =
        artType != nullptr && !artType->image.empty() ? artType->image : artId;
    return shpResourceCandidates(imageName);
}

std::string fileNameOnlyLower(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    return toLowerAscii(std::filesystem::path(path).filename().string());
}

std::vector<std::string> unitPaletteCandidateNamesForTheater(
    const std::string& theater
) {
    const std::string lower = toLowerAscii(theater);
    if (lower == "temperate") {
        return {"cachemd_unittem.pal", "cache_unittem.pal", "unittem.pal"};
    }
    if (lower == "snow") {
        return {"cachemd_unitsno.pal", "cache_unitsno.pal", "unitsno.pal"};
    }
    if (lower == "urban") {
        return {"cachemd_uniturb.pal", "cache_uniturb.pal", "uniturb.pal"};
    }
    if (lower == "newurban") {
        return {"cachemd_unitubn.pal", "cache_unitubn.pal", "unitubn.pal"};
    }
    if (lower == "desert") {
        return {"cachemd_unitdes.pal", "cache_unitdes.pal", "unitdes.pal"};
    }
    if (lower == "lunar") {
        return {"cachemd_unitlun.pal", "cache_unitlun.pal", "unitlun.pal"};
    }
    return {};
}

std::vector<std::string> isoPaletteCandidateNamesForTheater(
    const std::string& theater
) {
    const std::string lower = toLowerAscii(theater);
    if (lower == "temperate") {
        return {"isotem.pal", "cachemd_isotem.pal", "cache_isotem.pal"};
    }
    if (lower == "snow") {
        return {"isosno.pal", "cachemd_isosno.pal", "cache_isosno.pal"};
    }
    if (lower == "urban") {
        return {"isourb.pal", "cachemd_isourb.pal", "cache_isourb.pal"};
    }
    if (lower == "newurban") {
        return {"isoubn.pal", "cachemd_isoubn.pal", "cache_isoubn.pal"};
    }
    if (lower == "desert") {
        return {"isodes.pal", "cachemd_isodes.pal", "cache_isodes.pal"};
    }
    if (lower == "lunar") {
        return {"isolun.pal", "cachemd_isolun.pal", "cache_isolun.pal"};
    }
    return {};
}

std::vector<std::string> projectilePaletteCandidateNames(
    const std::string& theater,
    const std::string& structurePalettePath,
    const std::string& terrainPalettePath
) {
    std::vector<std::string> candidates;
    addUniqueCandidate(candidates, fileNameOnlyLower(structurePalettePath));
    for (const std::string& candidate : unitPaletteCandidateNamesForTheater(theater)) {
        addUniqueCandidate(candidates, candidate);
    }
    addUniqueCandidate(candidates, "anim.pal");
    addUniqueCandidate(candidates, fileNameOnlyLower(terrainPalettePath));
    for (const std::string& candidate : isoPaletteCandidateNamesForTheater(theater)) {
        addUniqueCandidate(candidates, candidate);
    }
    return candidates;
}

std::string recommendedProjectilePaletteName(
    const std::string& theater,
    const std::string& structurePalettePath
) {
    const std::string explicitStructurePalette =
        fileNameOnlyLower(structurePalettePath);
    if (!explicitStructurePalette.empty()) {
        return explicitStructurePalette;
    }

    const std::vector<std::string> candidates =
        unitPaletteCandidateNamesForTheater(theater);
    if (!candidates.empty()) {
        return candidates.front();
    }

    return "current theater unit palette";
}

std::string formatSequenceFrameRange(
    const ArtType::SequenceFrame* frame
) {
    if (frame == nullptr || !frame->present) {
        return "missing";
    }

    const int frameCount = std::max(0, frame->frameCount);
    const int facingMultiplier = std::max(0, frame->facingMultiplier);
    const int totalFrames =
        frameCount * std::max(1, facingMultiplier);
    const int endFrame =
        totalFrames > 0 ? frame->startFrame + totalFrames - 1 : frame->startFrame;

    return "start="
        + std::to_string(frame->startFrame)
        + " count="
        + std::to_string(frameCount)
        + " facingMultiplier="
        + std::to_string(facingMultiplier)
        + " range="
        + std::to_string(frame->startFrame)
        + "-"
        + std::to_string(endFrame);
}

const ArtType::SequenceFrame* findSequenceFrame(
    const ArtType& artType,
    const std::string& name
) {
    const std::string lowerName = toLowerAscii(name);
    for (const ArtType::SequenceFrame& frame : artType.sequenceFrames) {
        if (toLowerAscii(frame.name) == lowerName) {
            return &frame;
        }
    }

    return nullptr;
}

int sequenceProbeFrame(
    const ArtType::SequenceFrame* frame
) {
    if (frame == nullptr || !frame->present) {
        return 0;
    }

    return std::max(0, frame->startFrame);
}

std::string formatLayerOffset(const ArtType::ActiveAnimation& animation) {
    return " offset="
        + std::to_string(animation.offsetX)
        + ","
        + std::to_string(animation.offsetY)
        + " ySort="
        + std::to_string(animation.ySort)
        + " zAdjust="
        + std::to_string(animation.zAdjust)
        + " powered="
        + (animation.powered ? "yes" : "no");
}

void appendAnimationLayerDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const std::string& buildingId,
    const std::string& group,
    const ArtType::ActiveAnimation& animation,
    const ArtDatabase& art,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& fallbackPalette
) {
    const ArtType* artType = art.findArtType(animation.image);
    appendResourceDiagnostic(session,
        stats,
        "building layer diagnostic "
            + buildingId
            + " "
            + group
            + " "
            + animation.key
            + " image="
            + animation.image
            + formatLayerOffset(animation)
            + " "
            + animationFrameWindow(artType)
            + " layerRect=project(footprintRearCell)+(-globalWidth/2+frameOffsetX+offsetX,tileH/2-globalHeight+frameOffsetY+offsetY)"
            + " clip=globalRect-not-local-frame",
        artShpCandidates(art, animation.image),
        filesByName,
        mixArchives,
        animationProbeFrame(artType),
        paletteStatusForArt(artType, fallbackPalette)
    );

    if (animation.damagedImage.empty()) {
        return;
    }

    const ArtType* damagedArtType = art.findArtType(animation.damagedImage);
    appendResourceDiagnostic(session,
        stats,
        "building layer diagnostic "
            + buildingId
            + " "
            + group
            + " "
            + animation.key
            + "Damaged image="
            + animation.damagedImage
            + formatLayerOffset(animation)
            + " "
            + animationFrameWindow(damagedArtType)
            + " layerRect=project(footprintRearCell)+(-globalWidth/2+frameOffsetX+offsetX,tileH/2-globalHeight+frameOffsetY+offsetY)"
            + " clip=globalRect-not-local-frame",
        artShpCandidates(art, animation.damagedImage),
        filesByName,
        mixArchives,
        animationProbeFrame(damagedArtType),
        paletteStatusForArt(damagedArtType, fallbackPalette)
    );
}

void appendBuildingLayerDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const RulesDatabase& rules,
    const ArtDatabase& art,
    const TheaterDatabase& theaterDatabase,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& structurePalettePath
) {
    const std::unordered_map<std::string, ArtObjectLink> linksByBuildingId =
        buildObjectLinksById(rules, art, "building");
    const std::vector<std::string> targetBuildings = {
        "GACNST",
        "GAPOWR",
        "GAPILE",
        "GAWEAP",
        "CAOILD"
    };

    for (const std::string& buildingId : targetBuildings) {
        const BuildingType* building = findBuildingTypeById(rules, buildingId);
        const auto linkIt = linksByBuildingId.find(toLowerAscii(buildingId));
        if (building == nullptr || linkIt == linksByBuildingId.end() || !linkIt->second.hasArt) {
            stats.warnings.push_back(
                "building layer diagnostic "
                + buildingId
                + ": missing rules/art link"
            );
            continue;
        }

        const ArtObjectLink& link = linkIt->second;
        const ArtType* artType = art.findArtType(link.artId);
        const StructureFootprint footprint = parseFoundationSize(link.foundation);
        const std::string basePalette = paletteStatusForArt(
            artType,
            structurePalettePath.empty()
                ? "structure.pal/external"
                : std::filesystem::path(structurePalettePath).filename().string()
        );

        stats.warnings.push_back(
            "building layer diagnostic "
            + buildingId
            + ": art="
            + link.artId
            + " image="
            + (link.image.empty() ? "-" : link.image)
            + " foundation="
            + (link.foundation.empty() ? "-" : link.foundation)
            + " footprint="
            + std::to_string(footprint.width)
            + "x"
            + std::to_string(footprint.height)
            + " buildup="
            + (link.buildup.empty() ? "none" : link.buildup)
            + " bib="
            + (link.bibShape.empty() ? "none" : link.bibShape)
            + " activeLayers="
            + std::to_string(link.activeAnimations.size())
            + " auxLayers="
            + std::to_string(link.auxiliaryAnimations.size())
            + " shadow="
            + (link.shadowSpecified
                ? (link.shadow ? "yes" : "no")
                : "default")
            + " shadowIndex="
            + std::to_string(std::max(0, link.shadowIndex))
            + " useTurretShadow="
            + (link.useTurretShadow ? "yes" : "no")
            + " remap="
            + (link.remapable ? "yes" : "no")
            + " paletteHint="
            + basePalette
            + " ownerRemapRule=mapOwnerHouseColor-if-remapable"
            + " zAdjust="
            + std::to_string(link.zAdjust)
            + " ySortAdjust="
            + std::to_string(link.ySortAdjust)
            + " anchor=footprintRearCell(+"
            + std::to_string(std::max(0, footprint.width - 1))
            + ",+"
            + std::to_string(std::max(0, footprint.height - 1))
            + ") anchorFormula=topLeft=project(footprintRearCell)+(-globalWidth/2+frameOffsetX,tileH/2-globalHeight+frameOffsetY)+layerOffset"
            + " expectedScreenRect=base/globalSize-relative-to-rear-cell; active/damaged/flag/aux use same anchor plus layer offset"
            + " clipRule=global-rect-not-local-frame"
            + " neutralColorRule=Neutral/white comes from house remap only when art remapable=yes"
            + " placementHitTest=use-same-MapFacts-footprint-and-approach-ring"
            + " rendererNeeds=base+buildup+active+bib/aux"
        );

        appendResourceDiagnostic(session,
            stats,
            "building layer diagnostic " + buildingId + " base",
            structureAssetFileCandidates(link, theaterDatabase),
            filesByName,
            mixArchives,
            0,
            basePalette
        );

        if (!link.buildup.empty()) {
            appendResourceDiagnostic(session,
                stats,
                "building layer diagnostic " + buildingId + " buildup image=" + link.buildup,
                shpResourceCandidates(link.buildup),
                filesByName,
                mixArchives,
                0,
                basePalette
            );
        } else {
            stats.warnings.push_back(
                "building layer diagnostic "
                + buildingId
                + " buildup: none"
            );
        }

        if (!link.bibShape.empty()) {
            appendResourceDiagnostic(session,
                stats,
                "building layer diagnostic " + buildingId + " bib image=" + link.bibShape,
                theaterDatabase.getResourceFileNameCandidates(link.bibShape, true),
                filesByName,
                mixArchives,
                0,
                "theater terrain palette"
            );
        } else {
            stats.warnings.push_back(
                "building layer diagnostic "
                + buildingId
                + " bib: none"
            );
        }

        for (const ArtType::ActiveAnimation& animation : link.activeAnimations) {
            appendAnimationLayerDiagnostics(session,
                stats,
                buildingId,
                "active",
                animation,
                art,
                filesByName,
                mixArchives,
                basePalette
            );
        }

        for (const ArtType::ActiveAnimation& animation : link.specialAnimations) {
            appendAnimationLayerDiagnostics(session,
                stats,
                buildingId,
                "special",
                animation,
                art,
                filesByName,
                mixArchives,
                basePalette
            );
        }

        for (const ArtType::ActiveAnimation& animation : link.auxiliaryAnimations) {
            appendAnimationLayerDiagnostics(session,
                stats,
                buildingId,
                "aux",
                animation,
                art,
                filesByName,
                mixArchives,
                basePalette
            );
        }

        const std::vector<std::string> cameoNames = {link.cameo, link.altCameo};
        for (const std::string& cameoName : cameoNames) {
            if (cameoName.empty()) {
                continue;
            }
            appendResourceDiagnostic(session,
                stats,
                "building layer diagnostic " + buildingId + " cameo image=" + cameoName,
                shpResourceCandidates(cameoName),
                filesByName,
                mixArchives,
                0,
                "sidebar/cameo palette"
            );
        }

        if (toLowerAscii(buildingId) == "caoild") {
            stats.warnings.push_back(
                "oil derrick half-render diagnostic CAOILD: rules/art layers are base CAOILD, active CAOILD_A, "
                "damaged active CAOILD_AD, and flare/flag CAOILD_F when present. If only the lower/body half is visible, "
                "resource evidence above distinguishes missing decode from renderer clipping: draw each resolved layer from "
                "project(footprintRearCell)+expectedRectRelativeToRearCell, clip against global rect, and do not crop to local frame bounds."
                " Neutral/white is a renderer house-remap consumption issue when remapable art is present, not a separate SHP."
            );
        }
    }

    stats.warnings.push_back(
        "renderer consumption diagnostic: building renderer should consume parsed layer evidence "
        "per building; the preview embeds only the resolved base SHP for placement comparison. Placement preview, hit-test, "
        "attack, engineer-enter, repair, and occupy should consume the same MapFacts footprint/edge/approach facts. "
        "HTML preview object hit-test uses structure footprint diamonds rather than sprite rects so it matches placement cells; "
        "final sprite drawing still uses rear-cell anchor plus SHP global size/frame offset/layer offset."
    );
    stats.warnings.push_back(
        "building active anchor diagnostic: if GACNST/GAPOWR/GAPILE/GAWEAP/CAOILD active layers appear above the building, "
        "the likely renderer bug is applying ActiveAnim offset to the already-drawn base top-left, or using ZAdjust as screen Y. "
        "Use the same footprint rear-cell anchor formula as base SHP, then apply frameOffset/globalSize/layer offset; use ZAdjust/YSort for ordering."
    );
}

void appendFlagDiagnostics(
    MapStructureSpriteStats& stats,
    const RulesDatabase& rules,
    const ArtDatabase& art
) {
    const std::unordered_map<std::string, ArtObjectLink> linksByBuildingId =
        buildObjectLinksById(rules, art, "building");

    const auto gapowrIt = linksByBuildingId.find("gapowr");
    const auto gapileIt = linksByBuildingId.find("gapile");
    const auto gaweapIt = linksByBuildingId.find("gaweap");

    stats.warnings.push_back(
        "flag diagnostic: no universal building flag resource key is parsed; renderer should not synthesize flags for every building."
    );
    if (gapowrIt != linksByBuildingId.end()) {
        stats.warnings.push_back(
            "flag diagnostic GAPOWR: activeLayers="
            + std::to_string(gapowrIt->second.activeAnimations.size())
            + " bib="
            + (gapowrIt->second.bibShape.empty() ? "none" : gapowrIt->second.bibShape)
            + " status=no flag-like barracks/factory layer"
        );
    }
    if (gapileIt != linksByBuildingId.end()) {
        stats.warnings.push_back(
            "flag diagnostic GAPILE: parsed flag-like evidence is ActiveAnim GAPILE_A/GAPILE_AD; "
            "resource/source/frame/offset are listed in building layer diagnostics."
        );
    }
    if (gaweapIt != linksByBuildingId.end()) {
        stats.warnings.push_back(
            "flag diagnostic GAWEAP: parsed factory flag/roof/door evidence is ActiveAnim GAWEAP_A/AD, "
            "ActiveAnimTwo GAWEAP_B/BD, and aux GAWEAP_1..4; resource/source/frame/offset are listed above."
        );
    }
}

void appendSidebarCandidateDiagnostic(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const std::string& label,
    const std::vector<std::string>& candidates,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& paletteStatus
) {
    appendResourceDiagnostic(session,
        stats,
        "sidebar resource diagnostic " + label,
        candidates,
        filesByName,
        mixArchives,
        0,
        paletteStatus
    );
}

void appendSidebarDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives
) {
    appendSidebarCandidateDiagnostic(session,
        stats,
        "mix containers",
        resourceFileCandidates(
            "sidec01",
            {".mix", ".shp"}
        ),
        filesByName,
        mixArchives,
        "not-applicable"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "mix containers md",
        resourceFileCandidates(
            "sidec02md",
            {".mix"}
        ),
        filesByName,
        mixArchives,
        "not-applicable"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "tabs",
        resourceFileCandidates(
            "tabs",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "repair button",
        resourceFileCandidates(
            "repair",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "sell button",
        resourceFileCandidates(
            "sell",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "main buttons",
        resourceFileCandidates(
            "mnbttn",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "start button",
        resourceFileCandidates(
            "startbut",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "power and health bars",
        resourceFileCandidates(
            "pips",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "power bar pips frames",
        resourceFileCandidates(
            "pips",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "health bar pips frames",
        resourceFileCandidates(
            "pips",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "standalone power image probe",
        resourceFileCandidates(
            "power",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette; expected-missing if pips.shp is used"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "standalone health image probe",
        resourceFileCandidates(
            "health",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette; expected-missing if pips.shp is used"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "radar panel",
        resourceFileCandidates(
            "radar",
            {".shp", ".pcx"}
        ),
        filesByName,
        mixArchives,
        "sidebar palette"
    );
    appendSidebarCandidateDiagnostic(session,
        stats,
        "sidebar grips top",
        resourceFileCandidates(
            "sbgript",
            {".pcx", ".shp"}
        ),
        filesByName,
        mixArchives,
        "pcx-or-ui"
    );

    const std::vector<std::string> cameoNames = {
        "POWRICON",
        "BRRKICON",
        "GWEPICON",
        "FVICON",
        "MTNKICON",
        "GIICON",
        "GDGIICON"
    };
    for (const std::string& cameoName : cameoNames) {
        appendSidebarCandidateDiagnostic(session,
            stats,
            "cameo " + cameoName,
            shpResourceCandidates(cameoName),
            filesByName,
            mixArchives,
            "cameo/sidebar palette"
        );
    }

    stats.warnings.push_back(
        "sidebar button frame diagnostic: repair/sell/tabs/radar/power probes above use real sidec01/sidec02md resource names. "
        "format 0 SHP rows are diagnostic-decodable when width*height raw indexed payload validates; AssetInspector validates indexed8 PCX RLE when present; renderer should place repair/sell from their SHP frame offsets "
        "with sidebar/cameo palette candidates and avoid synthesizing button art."
    );
    stats.warnings.push_back(
        "sidebar Java reference facts: read-only Java RA2 uses repair rect 20,8,64x31, sell rect 84,8,64x31, tabs start 26,38 step 29 with tab00..tab03 state frames, "
        "power strip width 17 from the left side2 slice, powerp 12x1 colored strips when present, radar frame animation at 50ms, and place.shp green/red diamonds cropped to 60x29. "
        "These are report facts for Runtime/Renderer reference, not copied Runtime parameters."
    );
    stats.warnings.push_back(
        "sidebar health/power diagnostic: pips.shp is the preferred power/health frame source; standalone health.shp/health.pcx is optional and should not trigger mix re-export."
    );
    stats.warnings.push_back(
        "sidebar frame purpose facts: tabs=frame0 repair=frames0-1 sell=frames0-1 power=frames0-1 radar=frames0-32 pips=health/power candidate frames; "
        "queue/progress should use resolved cameo frame plus progress overlay; verbose SHP probe above carries first frame size/offset/global and source MIX entry."
    );
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t comma = value.find(',', begin);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        const std::string token = trimAscii(value.substr(begin, end - begin));
        if (!token.empty()) {
            addUniqueCandidate(result, token);
        }
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }

    return result;
}

std::string projectilePaletteStatus(
    const ProjectileType& projectile,
    const ArtType* projectileArt,
    const std::string& recommendedPalette
) {
    if (projectile.imageIsNone || projectile.inviso) {
        return "not-applicable";
    }
    if (projectile.paletteIsSet && !projectile.palette.empty()) {
        return "projectile=" + projectile.palette;
    }
    if (!projectile.color.empty()) {
        return "projectileColor=" + projectile.color;
    }
    if (projectile.firersPalette) {
        return "firersPalette=yes";
    }
    if (projectile.animPalette) {
        return "anim.pal";
    }
    if (projectileArt != nullptr && !projectileArt->palette.empty()) {
        return "art=" + projectileArt->palette;
    }
    if (projectileArt != nullptr && projectileArt->animPalette) {
        return "anim.pal";
    }

    return "undetermined recommend="
        + recommendedPalette
        + " purple-risk=yes";
}

std::vector<std::string> collectWeaponIds(const UnitType& unit) {
    std::vector<std::string> weaponIds;
    addUniqueCandidate(weaponIds, unit.primaryWeapon);
    addUniqueCandidate(weaponIds, unit.secondaryWeapon);
    addUniqueCandidate(weaponIds, unit.elitePrimaryWeapon);
    addUniqueCandidate(weaponIds, unit.eliteSecondaryWeapon);
    addUniqueCandidate(weaponIds, unit.deployWeapon);
    addUniqueCandidate(weaponIds, unit.eliteDeployWeapon);
    addUniqueCandidate(weaponIds, unit.occupyWeapon);
    addUniqueCandidate(weaponIds, unit.eliteOccupyWeapon);
    return weaponIds;
}

void appendWeaponArtDiagnostic(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const std::string& objectId,
    const std::string& weaponId,
    const RulesDatabase& rules,
    const ArtDatabase& art,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& mapTheater,
    const std::string& structurePalettePath,
    const std::string& terrainPalettePath
) {
    const WeaponType* weapon = findWeaponTypeById(rules, weaponId);
    if (weapon == nullptr) {
        stats.warnings.push_back(
            "weapon art diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + ": missing weapon rules"
        );
        return;
    }

    const ProjectileType* projectile = findProjectileTypeById(rules, weapon->projectile);
    if (projectile == nullptr) {
        stats.warnings.push_back(
            "weapon art diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + ": projectile="
            + (weapon->projectile.empty() ? "-" : weapon->projectile)
            + " missing projectile rules"
        );
        return;
    }

    const std::string projectileArtId =
        !projectile->image.empty() && !projectile->imageIsNone
            ? projectile->image
            : projectile->id;
    const ArtType* projectileArt = art.findArtType(projectileArtId);
    const std::string recommendedPalette =
        recommendedProjectilePaletteName(mapTheater, structurePalettePath);
    const std::vector<std::string> paletteCandidates =
        projectilePaletteCandidateNames(
            mapTheater,
            structurePalettePath,
            terrainPalettePath
        );
    const std::string paletteStatus =
        projectilePaletteStatus(*projectile, projectileArt, recommendedPalette);

    stats.warnings.push_back(
        "weapon art diagnostic "
        + objectId
        + " weapon="
        + weaponId
        + " projectile="
        + projectile->id
        + " image="
        + (projectile->image.empty() ? "-" : projectile->image)
        + " inviso="
        + (projectile->inviso ? "yes" : "no")
        + " animates="
        + (projectile->animates ? "yes" : "no")
        + " frames="
        + std::to_string(projectile->frames)
        + " palette="
        + paletteStatus
        + " paletteCandidates="
        + formatFullCandidateList(paletteCandidates)
        + " rendererPaletteRecommendation="
        + (projectile->imageIsNone || projectile->inviso ? "none/logical projectile" : recommendedPalette)
        + " rendererPaletteFields=explicitProjectilePalette:"
        + (projectile->paletteIsSet ? (projectile->palette.empty() ? "(empty)" : projectile->palette) : "(not-set)")
        + ",firersPalette:"
        + (projectile->firersPalette ? "yes" : "no")
        + ",animPalette:"
        + (projectile->animPalette ? "yes" : "no")
        + ",artPalette:"
        + (projectileArt == nullptr || projectileArt->palette.empty() ? "(not-set)" : projectileArt->palette)
        + ",candidateList:"
        + formatFullCandidateList(paletteCandidates)
    );

    if (projectile->imageIsNone || projectile->inviso) {
        stats.warnings.push_back(
            "weapon art diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + " projectile art: none/logical projectile"
        );
        stats.warnings.push_back(
            "renderer projectile distinction diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + ": Image=none/Inviso means do not draw a projectile SHP; use Weapon Anim="
            + (weapon->anim.empty() ? "none" : weapon->anim)
            + " as muzzle/impact art when present, normally with anim.pal. rendererProjectileSprite=none logicalProjectile=yes"
        );
    } else {
        const ResourceDiagnosticProbe projectileProbe = appendResourceDiagnostic(session,
            stats,
            "weapon art diagnostic "
                + objectId
                + " weapon="
                + weaponId
                + " projectileArt="
                + projectileArtId,
            graphicResourceCandidates(projectileArtId),
            filesByName,
            mixArchives,
            animationProbeFrame(projectileArt),
            paletteStatus
        );
        stats.warnings.push_back(
            "MIX export diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + " projectileArt="
            + projectileArtId
            + ": resourcePresent="
            + (projectileProbe.found ? "yes" : "no")
            + " source="
            + (projectileProbe.found ? projectileProbe.source : "missing")
            + " noReexportNeeded="
            + (projectileProbe.found ? "yes" : "no")
        );
        appendResourceDiagnostic(session,
            stats,
            "projectile palette candidate diagnostic "
                + objectId
                + " weapon="
                + weaponId
                + " projectileArt="
                + projectileArtId,
            paletteCandidates,
            filesByName,
            mixArchives,
            0,
            "choose before rendering; likely " + recommendedPalette
        );
    }

    for (const std::string& animId : splitCommaList(weapon->anim)) {
        const ArtType* animArt = art.findArtType(animId);
        appendResourceDiagnostic(session,
            stats,
            "weapon art diagnostic "
                + objectId
                + " weapon="
                + weaponId
                + " weaponAnim="
                + animId
                + " "
                + animationFrameWindow(animArt),
            artShpCandidates(art, animId),
            filesByName,
            mixArchives,
            animationProbeFrame(animArt),
            paletteStatusForArt(animArt, "anim.pal/undetermined")
        );
        stats.warnings.push_back(
            "weapon anim palette diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + " weaponAnim="
            + animId
            + ": resource is an effect/muzzle SHP; renderer should prefer anim.pal when no explicit art Palette is set."
        );
    }

    for (const std::string& trailerId : splitCommaList(projectile->trailer)) {
        const ArtType* trailerArt = art.findArtType(trailerId);
        appendResourceDiagnostic(session,
            stats,
            "weapon art diagnostic "
                + objectId
                + " weapon="
                + weaponId
                + " trailer="
                + trailerId
                + " "
                + animationFrameWindow(trailerArt),
            artShpCandidates(art, trailerId),
            filesByName,
            mixArchives,
            animationProbeFrame(trailerArt),
            paletteStatusForArt(trailerArt, "anim.pal/undetermined")
        );
    }

    if (paletteStatus.find("undetermined") != std::string::npos) {
        stats.warnings.push_back(
            "palette diagnostic "
            + objectId
            + " weapon="
            + weaponId
            + ": rules/art do not set Projectile Palette/FirersPalette/AnimPalette for "
            + projectileArtId
            + "; purple artifacts are consistent with renderer using the ISO/theater terrain palette for a projectile SHP. "
            + "Most likely read projectile SHP with "
            + recommendedPalette
            + "; keep weapon Anim SHPs on anim.pal unless art overrides it."
        );
    }
}

void appendProjectileDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const RulesDatabase& rules,
    const ArtDatabase& art,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& mapTheater,
    const std::string& structurePalettePath,
    const std::string& terrainPalettePath
) {
    const std::vector<std::string> vehicleTargets = {"FV", "MTNK"};
    for (const std::string& objectId : vehicleTargets) {
        const UnitType* unit = findUnitTypeById(rules.getVehicleTypes(), objectId);
        if (unit == nullptr) {
            stats.warnings.push_back("weapon art diagnostic " + objectId + ": missing vehicle rules");
            continue;
        }
        for (const std::string& weaponId : collectWeaponIds(*unit)) {
            appendWeaponArtDiagnostic(session,
                stats,
                objectId,
                weaponId,
                rules,
                art,
                filesByName,
                mixArchives,
                mapTheater,
                structurePalettePath,
                terrainPalettePath
            );
        }
    }

    const std::vector<std::string> infantryTargets = {"E1", "GGI"};
    for (const std::string& objectId : infantryTargets) {
        const UnitType* unit = findUnitTypeById(rules.getInfantryTypes(), objectId);
        if (unit == nullptr) {
            stats.warnings.push_back("weapon art diagnostic " + objectId + ": missing infantry rules");
            continue;
        }
        for (const std::string& weaponId : collectWeaponIds(*unit)) {
            appendWeaponArtDiagnostic(session,
                stats,
                objectId,
                weaponId,
                rules,
                art,
                filesByName,
                mixArchives,
                mapTheater,
                structurePalettePath,
                terrainPalettePath
            );
        }
    }
}

void appendInfantrySequenceDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const RulesDatabase& rules,
    const ArtDatabase& art,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives,
    const std::string& mapTheater,
    const std::string& structurePalettePath
) {
    const std::unordered_map<std::string, ArtObjectLink> linksByInfantryId =
        buildObjectLinksById(rules, art, "infantry");
    const std::vector<std::string> infantryTargets = {"E1", "GGI"};

    for (const std::string& objectId : infantryTargets) {
        const UnitType* unit = findUnitTypeById(rules.getInfantryTypes(), objectId);
        const auto linkIt = linksByInfantryId.find(toLowerAscii(objectId));
        if (unit == nullptr || linkIt == linksByInfantryId.end() || !linkIt->second.hasArt) {
            stats.warnings.push_back(
                "infantry sequence diagnostic "
                + objectId
                + ": missing infantry rules/art link"
            );
            continue;
        }

        const ArtObjectLink& link = linkIt->second;
        const ArtType* artType = art.findArtType(link.artId);
        if (artType == nullptr) {
            stats.warnings.push_back(
                "infantry sequence diagnostic "
                + objectId
                + ": missing art section "
                + link.artId
            );
            continue;
        }

        const ArtType::SequenceFrame* ready = findSequenceFrame(*artType, "Ready");
        const ArtType::SequenceFrame* fireUp = findSequenceFrame(*artType, "FireUp");
        const ArtType::SequenceFrame* fireProne = findSequenceFrame(*artType, "FireProne");
        const ArtType::SequenceFrame* deploy = findSequenceFrame(*artType, "Deploy");
        const ArtType::SequenceFrame* deployed = findSequenceFrame(*artType, "Deployed");
        const ArtType::SequenceFrame* deployedFire = findSequenceFrame(*artType, "DeployedFire");
        const ArtType::SequenceFrame* undeploy = findSequenceFrame(*artType, "Undeploy");

        const bool deployedFireDiffers =
            deployedFire != nullptr
            && fireUp != nullptr
            && deployedFire->startFrame != fireUp->startFrame;
        const std::string unitPalette =
            recommendedProjectilePaletteName(mapTheater, structurePalettePath);
        const std::vector<std::string> candidates = artShpCandidates(art, link.artId);

        stats.warnings.push_back(
            "infantry sequence diagnostic "
            + objectId
            + ": art="
            + link.artId
            + " image="
            + (link.image.empty() ? link.artId : link.image)
            + " sequence="
            + (link.sequence.empty() ? "-" : link.sequence)
            + " deployer="
            + (unit->deployer ? "yes" : "no")
            + " deployFire="
            + (unit->deployFire ? "yes" : "no")
            + " primary="
            + (unit->primaryWeapon.empty() ? "-" : unit->primaryWeapon)
            + " secondary="
            + (unit->secondaryWeapon.empty() ? "-" : unit->secondaryWeapon)
            + " deployWeapon="
            + (unit->deployWeapon.empty() ? "-" : unit->deployWeapon)
            + " eliteDeployWeapon="
            + (unit->eliteDeployWeapon.empty() ? "-" : unit->eliteDeployWeapon)
            + " ready="
            + formatSequenceFrameRange(ready)
            + " fireUp="
            + formatSequenceFrameRange(fireUp)
            + " fireProne="
            + formatSequenceFrameRange(fireProne)
            + " deploy="
            + formatSequenceFrameRange(deploy)
            + " deployed="
            + formatSequenceFrameRange(deployed)
            + " deployedFire="
            + formatSequenceFrameRange(deployedFire)
            + " undeploy="
            + formatSequenceFrameRange(undeploy)
            + " deployedFireDiffers="
            + (deployedFireDiffers ? "yes" : "no")
            + " rendererNeeds=deployed mission uses Deployed/DeployedFire frames and deployWeapon, not normal FireUp."
        );

        appendResourceDiagnostic(session,
            stats,
            "infantry sequence diagnostic "
                + objectId
                + " SHP ready frame",
            candidates,
            filesByName,
            mixArchives,
            sequenceProbeFrame(ready),
            unitPalette
        );
        appendResourceDiagnostic(session,
            stats,
            "infantry sequence diagnostic "
                + objectId
                + " SHP normal fire frame",
            candidates,
            filesByName,
            mixArchives,
            sequenceProbeFrame(fireUp),
            unitPalette
        );
        appendResourceDiagnostic(session,
            stats,
            "infantry sequence diagnostic "
                + objectId
                + " SHP deploy frame",
            candidates,
            filesByName,
            mixArchives,
            sequenceProbeFrame(deploy),
            unitPalette
        );
        appendResourceDiagnostic(session,
            stats,
            "infantry sequence diagnostic "
                + objectId
                + " SHP deployed frame",
            candidates,
            filesByName,
            mixArchives,
            sequenceProbeFrame(deployed),
            unitPalette
        );
        appendResourceDiagnostic(session,
            stats,
            "infantry sequence diagnostic "
                + objectId
                + " SHP deployed fire frame",
            candidates,
            filesByName,
            mixArchives,
            sequenceProbeFrame(deployedFire),
            unitPalette
        );
    }
}

bool isBridgeTerrainReference(const TheaterTileReference& reference) {
    const std::string name = toLowerAscii(
        reference.tileSetName
        + " "
        + reference.baseFileName
        + " "
        + reference.expectedFileName
    );
    return containsAscii(name, "bridge")
        || containsAscii(name, "brdg")
        || containsAscii(name, "w-b")
        || containsAscii(name, "wbrdg");
}

std::string decodeBridgeTileReference(
    const TheaterTileReference& reference,
    int requestedSubTile
) {
    if (reference.resolvedPath.empty()) {
        return "source=missing decode=not-attempted";
    }

    TmpTileFile tileFile;
    std::string errorMessage;
    if (!loadTmpTileFile(reference.resolvedPath, tileFile, errorMessage)) {
        return "source=loose:"
            + reference.resolvedPath
            + " decode=fail tmp error="
            + errorMessage;
    }

    return "source=loose:"
        + reference.resolvedPath
        + " "
        + decodeDiagnosticTmpResource(tileFile, requestedSubTile)
        + " palette=theater terrain palette";
}

void appendBridgeDiagnostics(concord::assets::MapPreviewResourceSession& session,
    MapStructureSpriteStats& stats,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::unordered_map<int, TheaterTileReference>& referencesByTileIndex,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives
) {
    int bridgeCells = 0;
    int bridgeSamples = 0;
    std::set<std::string> sampledTiles;

    for (const MapIsoTile& tile : mapDatabase.getIsoTiles()) {
        const auto referenceIt = referencesByTileIndex.find(tile.tileIndex);
        if (referenceIt == referencesByTileIndex.end()
            || !isBridgeTerrainReference(referenceIt->second)
        ) {
            continue;
        }

        ++bridgeCells;
        const std::string sampleKey =
            std::to_string(tile.tileIndex)
            + ":"
            + std::to_string(tile.subTileIndex);
        if (bridgeSamples >= 32 || !sampledTiles.insert(sampleKey).second) {
            continue;
        }

        TheaterTileReference reference = referenceIt->second;
        if (reference.resolvedPath.empty() && !tileRootPath.empty()) {
            reference = theaterDatabase.resolveTileIndex(tile.tileIndex, tileRootPath);
        }

        stats.warnings.push_back(
            "bridge cell diagnostic: cell="
            + std::to_string(tile.x)
            + ","
            + std::to_string(tile.y)
            + " tile="
            + std::to_string(tile.tileIndex)
            + ":"
            + std::to_string(tile.subTileIndex)
            + " level="
            + std::to_string(tile.level)
            + " set="
            + (reference.tileSetName.empty() ? "-" : reference.tileSetName)
            + " base="
            + (reference.baseFileName.empty() ? "-" : reference.baseFileName)
            + " expected="
            + (reference.expectedFileName.empty() ? "-" : reference.expectedFileName)
            + " candidates="
            + formatFullCandidateList(reference.candidateFileNames)
            + " "
            + decodeBridgeTileReference(reference, tile.subTileIndex)
            + " anchor=project(tileCell,level)+tmpSlotOffset"
            + " expectedScreenRect=left=projectX-tileW/2+tmpOffsetX top=projectY-tileH/2+tmpOffsetY size=decodedTmpSlotSize tile=60x30"
            + " transitionHint=set/base names carry bridge/shore/cliff context; renderer must compose requested subTile at map level"
        );
        ++bridgeSamples;
    }

    stats.warnings.push_back(
        "bridge cell diagnostic summary: bridgeCells="
        + std::to_string(bridgeCells)
        + " sampledTileSlots="
        + std::to_string(bridgeSamples)
        + " rendererHint=respect requested subTile when present, otherwise use explicit TMP fallback evidence."
    );

    int lowBridgeSamples = 0;
    std::set<std::string> sampledOverlays;
    for (const MapOverlayCell& overlay : mapDatabase.getOverlayCells()) {
        if (!isLowBridgeOverlay(overlay)) {
            continue;
        }

        const std::string sampleKey =
            std::to_string(overlay.overlayIndex)
            + ":"
            + std::to_string(overlay.frameIndex);
        if (lowBridgeSamples >= 24 || !sampledOverlays.insert(sampleKey).second) {
            continue;
        }

        const std::vector<std::string> candidates =
            overlayAssetFileCandidates(overlay, theaterDatabase);
        const OverlayResourceProbe probe =
            probeOverlayResource(session,overlay, candidates, filesByName, mixArchives);
        stats.warnings.push_back(
            "bridge overlay diagnostic: cell="
            + std::to_string(overlay.x)
            + ","
            + std::to_string(overlay.y)
            + " index="
            + std::to_string(overlay.overlayIndex)
            + " kind="
            + overlayKindName(overlay)
            + " type="
            + (overlay.type.empty() ? "-" : overlay.type)
            + " image="
            + (overlay.image.empty() ? "-" : overlay.image)
            + " frame="
            + std::to_string(overlay.frameIndex)
            + " resourceFrame="
            + std::to_string(overlay.frame)
            + " fallbackFrame="
            + std::to_string(overlay.fallbackFrame)
            + " candidates="
            + formatFullCandidateList(candidates)
            + (probe.found
                ? std::string(" selected=") + probe.candidate + " source=" + probe.source + " " + probe.decodeResult
                : std::string(" source=missing ") + probe.missSummary + " decode=not-attempted")
            + " paletteHint="
            + overlayPaletteHint(overlay)
            + " expectedScreenRect=left=projectX-globalWidth/2+frameOffsetX top=projectY-globalHeight/2+frameOffsetY size=decodedOverlaySize tile=60x30"
            + " rendererHint=low-bridge overlay resource exists separately from terrain; do not use as ore/gem"
        );
        ++lowBridgeSamples;
    }
}

OverlayResourceCoverage buildOverlayResourceCoverage(concord::assets::MapPreviewResourceSession& session,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::unordered_map<std::string, std::vector<std::string>>& filesByName,
    const std::vector<IndexedMixArchive>& mixArchives
) {
    OverlayResourceCoverage coverage;
    std::vector<OverlayResourceGroup> groups;
    std::map<std::string, std::size_t> groupIndexByKey;
    std::map<std::string, std::vector<std::string>> candidatesByKey;
    std::map<std::string, OverlayResourceProbe> probeByKey;

    for (const MapOverlayCell& overlay : mapDatabase.getOverlayCells()) {
        ++coverage.overlayCells;
        const std::string overlayKind = overlayKindName(overlay);
        if (overlay.isOre) {
            ++coverage.oreCells;
        }
        if (overlay.isGem) {
            ++coverage.gemCells;
        }
        if (overlayKind == "bridge") {
            ++coverage.bridgeCells;
        } else if (overlayKind == "wall") {
            ++coverage.wallCells;
        } else if (overlayKind == "other") {
            ++coverage.otherCells;
        }
        if ((overlay.isOre || overlay.isGem) && overlayKind == "bridge") {
            ++coverage.oreBridgeOverlapCells;
        }

        const std::string key = overlayResourceGroupKey(overlay);

        const auto groupIt = groupIndexByKey.find(key);
        if (groupIt == groupIndexByKey.end()) {
            groupIndexByKey.emplace(key, groups.size());
            OverlayResourceGroup group;
            group.overlay = overlay;
            group.cellCount = 1;
            groups.push_back(std::move(group));
        } else {
            groups[groupIt->second].cellCount += 1;
        }
    }

    for (const OverlayResourceGroup& group : groups) {
        const MapOverlayCell& overlay = group.overlay;
        const std::string key = overlayResourceGroupKey(overlay);
        const std::vector<std::string> candidates =
            overlayAssetFileCandidates(overlay, theaterDatabase);
        candidatesByKey[key] = candidates;

        if (candidates.empty()) {
            coverage.missingCells += group.cellCount;
            if (overlay.isOre) {
                coverage.oreMissingCells += group.cellCount;
            }
            if (overlay.isGem) {
                coverage.gemMissingCells += group.cellCount;
            }
            coverage.details.push_back(
                "overlay resource trace: index="
                + std::to_string(overlay.overlayIndex)
                + " kind="
                + overlayKindName(overlay)
                + " type="
                + (overlay.type.empty() ? "-" : overlay.type)
                + " image="
                + (overlay.image.empty() ? "-" : overlay.image)
                + " frame="
                + std::to_string(overlay.frameIndex)
                + " resourceFrame="
                + std::to_string(overlay.frame)
                + " fallbackFrame="
                + std::to_string(overlay.fallbackFrame)
                + " cells="
                + std::to_string(group.cellCount)
                + " sampleCell="
                + std::to_string(overlay.x)
                + ","
                + std::to_string(overlay.y)
                + " candidates=(none) source=missing decode=not-attempted"
                + " paletteHint="
                + overlayPaletteHint(overlay)
                + " rendererLayer="
                + overlayRendererLayerHint(overlay)
            );
            continue;
        }

        coverage.candidateCells += group.cellCount;
        const OverlayResourceProbe probe =
            probeOverlayResource(session,overlay, candidates, filesByName, mixArchives);
        probeByKey[key] = probe;

        if (probe.found) {
            coverage.resolvedCells += group.cellCount;
            if (probe.sourceIsMix) {
                coverage.mixResolvedCells += group.cellCount;
            } else {
                coverage.looseResolvedCells += group.cellCount;
            }
            if (probe.decoded) {
                coverage.decodedCells += group.cellCount;
            } else {
                coverage.decodeFailedCells += group.cellCount;
            }
            if (overlay.isOre) {
                coverage.oreResolvedCells += group.cellCount;
            }
            if (overlay.isGem) {
                coverage.gemResolvedCells += group.cellCount;
            }
            const std::string overlayKind = overlayKindName(overlay);
            if (overlayKind == "bridge") {
                coverage.bridgeResolvedCells += group.cellCount;
                if (!probe.decoded) {
                    coverage.bridgeDecodeFailedCells += group.cellCount;
                }
            }

            coverage.details.push_back(
                "overlay resource trace: index="
                + std::to_string(overlay.overlayIndex)
                + " kind="
                + overlayKind
                + " type="
                + (overlay.type.empty() ? "-" : overlay.type)
                + " image="
                + (overlay.image.empty() ? "-" : overlay.image)
                + " frame="
                + std::to_string(overlay.frameIndex)
                + " resourceFrame="
                + std::to_string(overlay.frame)
                + " fallbackFrame="
                + std::to_string(overlay.fallbackFrame)
                + " cells="
                + std::to_string(group.cellCount)
                + " sampleCell="
                + std::to_string(overlay.x)
                + ","
                + std::to_string(overlay.y)
                + " candidates="
                + formatFullCandidateList(candidates)
                + " selected="
                + probe.candidate
                + " source="
                + probe.source
                + " "
                + probe.decodeResult
                + " paletteHint="
                + overlayPaletteHint(overlay)
                + " rendererLayer="
                + overlayRendererLayerHint(overlay)
            );
        } else {
            coverage.missingCells += group.cellCount;
            if (overlay.isOre) {
                coverage.oreMissingCells += group.cellCount;
            }
            if (overlay.isGem) {
                coverage.gemMissingCells += group.cellCount;
            }
            const std::string overlayKind = overlayKindName(overlay);
            if (overlayKind == "bridge") {
                coverage.bridgeMissingCells += group.cellCount;
            }

            coverage.details.push_back(
                "overlay resource trace: index="
                + std::to_string(overlay.overlayIndex)
                + " kind="
                + overlayKind
                + " type="
                + (overlay.type.empty() ? "-" : overlay.type)
                + " image="
                + (overlay.image.empty() ? "-" : overlay.image)
                + " frame="
                + std::to_string(overlay.frameIndex)
                + " resourceFrame="
                + std::to_string(overlay.frame)
                + " fallbackFrame="
                + std::to_string(overlay.fallbackFrame)
                + " cells="
                + std::to_string(group.cellCount)
                + " sampleCell="
                + std::to_string(overlay.x)
                + ","
                + std::to_string(overlay.y)
                + " candidates="
                + formatFullCandidateList(candidates)
                + " source=missing "
                + probe.missSummary
                + " decode=not-attempted"
                + " paletteHint="
                + overlayPaletteHint(overlay)
                + " rendererLayer="
                + overlayRendererLayerHint(overlay)
            );
        }
    }

    for (const MapOverlayCell& overlay : mapDatabase.getOverlayCells()) {
        const std::string key = overlayResourceGroupKey(overlay);
        auto candidatesIt = candidatesByKey.find(key);
        if (candidatesIt == candidatesByKey.end()) {
            candidatesIt = candidatesByKey.emplace(
                key,
                overlayAssetFileCandidates(overlay, theaterDatabase)
            ).first;
        }

        auto probeIt = probeByKey.find(key);
        if (probeIt == probeByKey.end() && !candidatesIt->second.empty()) {
            probeIt = probeByKey.emplace(
                key,
                probeOverlayResource(session,overlay, candidatesIt->second, filesByName, mixArchives)
            ).first;
        }

        const std::string overlayKind = overlayKindName(overlay);
        std::string sourceSummary;
        if (candidatesIt->second.empty()) {
            sourceSummary = "candidates=(none) source=missing decode=not-attempted";
        } else if (probeIt != probeByKey.end() && probeIt->second.found) {
            sourceSummary =
                "candidates="
                + formatFullCandidateList(candidatesIt->second)
                + " selected="
                + probeIt->second.candidate
                + " source="
                + probeIt->second.source
                + " "
                + probeIt->second.decodeResult;
        } else if (probeIt != probeByKey.end()) {
            sourceSummary =
                "candidates="
                + formatFullCandidateList(candidatesIt->second)
                + " source=missing "
                + probeIt->second.missSummary
                + " decode=not-attempted";
        } else {
            sourceSummary =
                "candidates="
                + formatFullCandidateList(candidatesIt->second)
                + " source=missing decode=not-attempted";
        }

        coverage.details.push_back(
            "overlay cell trace: cell="
            + std::to_string(overlay.x)
            + ","
            + std::to_string(overlay.y)
            + " cellIndex="
            + std::to_string(overlay.cellIndex)
            + " index="
            + std::to_string(overlay.overlayIndex)
            + " kind="
            + overlayKind
            + " type="
            + (overlay.type.empty() ? "-" : overlay.type)
            + " image="
            + (overlay.image.empty() ? "-" : overlay.image)
            + " frame="
            + std::to_string(overlay.frameIndex)
            + " resourceFrame="
            + std::to_string(overlay.frame)
            + " fallbackFrame="
            + std::to_string(overlay.fallbackFrame)
            + " "
            + sourceSummary
            + " paletteHint="
            + overlayPaletteHint(overlay)
            + " rendererLayer="
            + overlayRendererLayerHint(overlay)
        );
    }

    return coverage;
}

void appendOverlayResourceCoverageWarnings(
    const OverlayResourceCoverage& coverage,
    MapStructureSpriteStats& stats
) {
    stats.warnings.push_back(
        "overlay resource coverage: cells="
        + std::to_string(coverage.overlayCells)
        + " candidates="
        + std::to_string(coverage.candidateCells)
        + " resolved="
        + std::to_string(coverage.resolvedCells)
        + " missing="
        + std::to_string(coverage.missingCells)
        + " loose="
        + std::to_string(coverage.looseResolvedCells)
        + " mix="
        + std::to_string(coverage.mixResolvedCells)
        + " decoded="
        + std::to_string(coverage.decodedCells)
        + " decodeFailed="
        + std::to_string(coverage.decodeFailedCells)
    );

    stats.warnings.push_back(
        "overlay resource coverage ore/gem: ore="
        + std::to_string(coverage.oreResolvedCells)
        + "/"
        + std::to_string(coverage.oreCells)
        + " gem="
        + std::to_string(coverage.gemResolvedCells)
        + "/"
        + std::to_string(coverage.gemCells)
        + " oreMissing="
        + std::to_string(coverage.oreMissingCells)
        + " gemMissing="
        + std::to_string(coverage.gemMissingCells)
    );

    stats.warnings.push_back(
        "overlay resource disambiguation: ore="
        + std::to_string(coverage.oreCells)
        + " gem="
        + std::to_string(coverage.gemCells)
        + " bridge="
        + std::to_string(coverage.bridgeCells)
        + " wall="
        + std::to_string(coverage.wallCells)
        + " other="
        + std::to_string(coverage.otherCells)
        + " oreBridgeOverlap="
        + std::to_string(coverage.oreBridgeOverlapCells)
        + " bridgeResolved="
        + std::to_string(coverage.bridgeResolvedCells)
        + "/"
        + std::to_string(coverage.bridgeCells)
        + " bridgeMissing="
        + std::to_string(coverage.bridgeMissingCells)
        + " bridgeDecodeFailed="
        + std::to_string(coverage.bridgeDecodeFailedCells)
        + " rendererRule=consume overlayKind before candidate list; never use bridge candidates for ore/gem."
    );

    for (const std::string& detail : coverage.details) {
        stats.warnings.push_back(detail);
    }
}

void putPixel(
    std::vector<Color>& pixels,
    int width,
    int height,
    int x,
    int y,
    Color color
) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    pixels[static_cast<std::size_t>(y * width + x)] = color;
}

bool decodeTmpImagePixels(
    const std::vector<unsigned char>& bytes,
    std::size_t imageOffset,
    std::size_t imageRecordEnd,
    unsigned int blockWidth,
    unsigned int blockHeight,
    TmpSubTileImage& image,
    std::string& errorMessage
) {
    constexpr std::size_t imageHeaderBytes = 52;

    if (blockWidth == 0 || blockHeight == 0 || imageRecordEnd <= imageOffset) {
        errorMessage = "TMP tile has invalid image dimensions.";
        return false;
    }

    std::size_t baseColorBytes = 0;
    unsigned int rowWidth = 4;
    for (unsigned int y = 0; y < blockHeight; ++y) {
        if (rowWidth > blockWidth) {
            errorMessage = "TMP packed diamond row is wider than the tile image.";
            return false;
        }

        baseColorBytes += rowWidth;
        if (y < (blockHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (baseColorBytes == 0) {
        errorMessage = "TMP base color data is empty.";
        return false;
    }

    const std::size_t imageRecordBytes = imageRecordEnd - imageOffset;
    if (imageRecordBytes < imageHeaderBytes + baseColorBytes) {
        errorMessage = "TMP image record is too small for base pixels.";
        return false;
    }

    const int rawBaseOffsetX = readInt32(bytes, imageOffset);
    const int rawBaseOffsetY = readInt32(bytes, imageOffset + 4);
    const unsigned int extraDataOffset = readUInt32(bytes, imageOffset + 8);
    const unsigned int zDataOffset = readUInt32(bytes, imageOffset + 12);
    const unsigned int extraZDataOffset = readUInt32(bytes, imageOffset + 16);
    const int rawExtraOffsetX = readInt32(bytes, imageOffset + 20);
    const int rawExtraOffsetY = readInt32(bytes, imageOffset + 24);
    const unsigned int extraWidth = readUInt32(bytes, imageOffset + 28);
    const unsigned int extraHeight = readUInt32(bytes, imageOffset + 32);

    const std::size_t expectedZDataOffset = imageHeaderBytes + baseColorBytes;
    const bool hasBaseZData =
        zDataOffset != 0
        && zDataOffset != 0xCDCDCDCDU;
    if (hasBaseZData
        && (
            static_cast<std::size_t>(zDataOffset) < expectedZDataOffset
            || static_cast<std::size_t>(zDataOffset) + baseColorBytes > imageRecordBytes
        )
    ) {
        errorMessage = "TMP z-data offset is outside the image record.";
        return false;
    }

    std::size_t extraPixelCount = 0;
    const bool hasExtra =
        extraWidth > 0
        && extraHeight > 0
        && extraWidth <= 512
        && extraHeight <= 512
        && checkedMul(
            static_cast<std::size_t>(extraWidth),
            static_cast<std::size_t>(extraHeight),
            extraPixelCount
        )
        && extraDataOffset >= imageHeaderBytes
        && static_cast<std::size_t>(extraDataOffset) + extraPixelCount <= imageRecordBytes
        && (
            extraZDataOffset == 0
            || extraZDataOffset == 0xCDCDCDCDU
            || static_cast<std::size_t>(extraZDataOffset) + extraPixelCount <= imageRecordBytes
        );

    const int baseOffsetX = 0;
    const int baseOffsetY = 0;
    const int extraOffsetX = rawExtraOffsetX - rawBaseOffsetX;
    const int extraOffsetY = rawExtraOffsetY - rawBaseOffsetY;

    int minX = baseOffsetX;
    int minY = baseOffsetY;
    int maxX = baseOffsetX + static_cast<int>(blockWidth);
    int maxY = baseOffsetY + static_cast<int>(blockHeight);

    if (hasExtra) {
        minX = std::min(minX, extraOffsetX);
        minY = std::min(minY, extraOffsetY);
        maxX = std::max(maxX, extraOffsetX + static_cast<int>(extraWidth));
        maxY = std::max(maxY, extraOffsetY + static_cast<int>(extraHeight));
    }

    if (maxX <= minX || maxY <= minY) {
        errorMessage = "TMP image bounds are invalid.";
        return false;
    }

    image.present = true;
    image.offsetX = minX;
    image.offsetY = minY;
    image.width = maxX - minX;
    image.height = maxY - minY;
    image.baseOffsetX = baseOffsetX;
    image.baseOffsetY = baseOffsetY;
    image.baseWidth = static_cast<int>(blockWidth);
    image.baseHeight = static_cast<int>(blockHeight);
    image.basePixels.assign(baseColorBytes, 0);
    image.baseZData.clear();
    image.hasBaseZData = hasBaseZData;
    if (hasBaseZData) {
        const std::size_t zDataAbsoluteOffset =
            imageOffset + static_cast<std::size_t>(zDataOffset);
        image.baseZData.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(zDataAbsoluteOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(zDataAbsoluteOffset + baseColorBytes)
        );
    }
    image.hasExtra = hasExtra;
    image.extraOffsetX = extraOffsetX;
    image.extraOffsetY = extraOffsetY;
    image.extraWidth = hasExtra ? static_cast<int>(extraWidth) : 0;
    image.extraHeight = hasExtra ? static_cast<int>(extraHeight) : 0;
    image.extraPixels.clear();
    image.extraZData.clear();
    image.hasExtraZData = false;
    image.pixels.assign(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height),
        0
    );

    const std::size_t basePixelDataOffset = imageOffset + imageHeaderBytes;
    std::size_t sourceIndex = basePixelDataOffset;
    std::size_t packedBaseIndex = 0;
    rowWidth = 4;
    for (unsigned int y = 0; y < blockHeight; ++y) {
        const unsigned int xStart = (blockWidth - rowWidth) / 2;
        for (unsigned int x = 0; x < rowWidth; ++x) {
            const unsigned char paletteIndex = bytes[sourceIndex];
            ++sourceIndex;
            image.basePixels[packedBaseIndex] = paletteIndex;
            ++packedBaseIndex;
            if (paletteIndex == 0) {
                continue;
            }

            const int targetX = baseOffsetX - minX + static_cast<int>(xStart + x);
            const int targetY = baseOffsetY - minY + static_cast<int>(y);
            image.pixels[
                (static_cast<std::size_t>(targetY) * image.width)
                + static_cast<std::size_t>(targetX)
            ] = paletteIndex;
        }

        if (y < (blockHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (hasExtra) {
        image.extraPixels.assign(extraPixelCount, 0);
        image.hasExtraZData =
            extraZDataOffset != 0
            && extraZDataOffset != 0xCDCDCDCDU;
        if (image.hasExtraZData) {
            const std::size_t extraZDataAbsoluteOffset =
                imageOffset + static_cast<std::size_t>(extraZDataOffset);
            image.extraZData.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(extraZDataAbsoluteOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(extraZDataAbsoluteOffset + extraPixelCount)
            );
        }

        const std::size_t extraPixelDataOffset =
            imageOffset + static_cast<std::size_t>(extraDataOffset);
        for (unsigned int y = 0; y < extraHeight; ++y) {
            for (unsigned int x = 0; x < extraWidth; ++x) {
                const unsigned char paletteIndex =
                    bytes[
                        extraPixelDataOffset
                        + (static_cast<std::size_t>(y) * extraWidth)
                        + x
                    ];
                image.extraPixels[
                    (static_cast<std::size_t>(y) * extraWidth) + x
                ] = paletteIndex;
                if (paletteIndex == 0) {
                    continue;
                }

                const int targetX = extraOffsetX - minX + static_cast<int>(x);
                const int targetY = extraOffsetY - minY + static_cast<int>(y);
                if (targetX < 0
                    || targetY < 0
                    || targetX >= image.width
                    || targetY >= image.height) {
                    errorMessage = "TMP extra pixels fall outside computed bounds.";
                    return false;
                }

                image.pixels[
                    (static_cast<std::size_t>(targetY) * image.width)
                    + static_cast<std::size_t>(targetX)
                ] = paletteIndex;
            }
        }
    }

    return true;
}

std::size_t findNextTmpImageOffset(
    const std::vector<std::size_t>& imageOffsets,
    std::size_t currentOffset,
    std::size_t fileSize
) {
    std::size_t nextOffset = fileSize;

    for (const std::size_t candidate : imageOffsets) {
        if (candidate > currentOffset && candidate < nextOffset) {
            nextOffset = candidate;
        }
    }

    return nextOffset;
}

bool isValidTmpImageOffset(
    std::size_t imageOffset,
    std::size_t tableEnd,
    std::size_t fileSize
) {
    return imageOffset >= tableEnd && imageOffset < fileSize;
}

bool validateTmpImageOffsets(
    const std::vector<std::size_t>& imageOffsets,
    std::size_t tableEnd,
    std::size_t fileSize,
    const std::string& path,
    std::string& errorMessage
) {
    for (const std::size_t imageOffset : imageOffsets) {
        if (imageOffset == 0) {
            continue;
        }

        if (!isValidTmpImageOffset(imageOffset, tableEnd, fileSize)) {
            errorMessage = "TMP image offset is outside file: " + path;
            return false;
        }
    }

    return true;
}

bool decodeRawTmpFormat3Pixels(
    const std::vector<unsigned char>& bytes,
    std::size_t dataOffset,
    std::size_t dataEnd,
    unsigned int width,
    unsigned int height,
    std::vector<unsigned char>& pixels,
    std::string& errorMessage
) {
    if (dataOffset >= dataEnd || dataEnd > bytes.size()) {
        errorMessage = "TMP raw image data bounds are invalid.";
        return false;
    }

    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

    std::size_t cursor = dataOffset;
    for (unsigned int y = 0; y < height; ++y) {
        if (cursor + 2 > dataEnd) {
            errorMessage = "TMP raw format 3 row table ended early.";
            return false;
        }

        const std::size_t rowStart = cursor;
        const std::size_t rowLength = readUInt16(bytes, cursor);
        if (rowLength < 2) {
            errorMessage = "TMP raw format 3 row has an invalid length.";
            return false;
        }

        const std::size_t rowEnd = rowStart + rowLength;
        if (rowEnd < rowStart || rowEnd > dataEnd) {
            errorMessage = "TMP raw format 3 row extends past the image record.";
            return false;
        }

        cursor += 2;
        unsigned int x = 0;
        while (cursor < rowEnd) {
            const unsigned char value = bytes[cursor++];
            if (value != 0) {
                if (x >= width) {
                    errorMessage = "TMP raw format 3 row contains too many opaque pixels.";
                    return false;
                }

                pixels[(static_cast<std::size_t>(y) * width) + x] = value;
                ++x;
                continue;
            }

            if (cursor >= rowEnd) {
                errorMessage = "TMP raw format 3 transparent run is truncated.";
                return false;
            }

            unsigned int runLength = bytes[cursor++];
            if (x + runLength > width) {
                runLength = width - x;
            }

            x += runLength;
        }

        cursor = rowEnd;
    }

    return true;
}

bool loadRawTmpTileFile(
    const std::vector<unsigned char>& bytes,
    const std::string& path,
    TmpTileFile& tileFile,
    std::string& errorMessage
) {
    constexpr std::size_t preludeBytes = 8;
    constexpr std::size_t slotRecordBytes = 24;
    constexpr std::size_t dataOffsetOffset = 20;

    if (bytes.size() < preludeBytes + slotRecordBytes) {
        errorMessage = "TMP raw slot table is too small: " + path;
        return false;
    }

    const unsigned int headerWidth = readUInt16(bytes, 2);
    const unsigned int headerHeight = readUInt16(bytes, 4);
    const unsigned int headerSlotCount = readUInt16(bytes, 6);

    std::size_t firstDataOffset = 0;
    for (
        std::size_t offsetField = preludeBytes + dataOffsetOffset;
        offsetField + 4 <= bytes.size();
        offsetField += slotRecordBytes
    ) {
        const unsigned int candidate = readUInt32(bytes, offsetField);
        if (candidate > preludeBytes
            && candidate < bytes.size()
            && ((static_cast<std::size_t>(candidate) - preludeBytes) % slotRecordBytes) == 0
            && (firstDataOffset == 0 || candidate < firstDataOffset)
        ) {
            firstDataOffset = static_cast<std::size_t>(candidate);
        }
    }

    if (firstDataOffset == 0 || firstDataOffset <= preludeBytes) {
        errorMessage = "TMP file has unsupported block dimensions: " + path;
        return false;
    }

    const std::size_t tableBytes = firstDataOffset - preludeBytes;
    if (tableBytes % slotRecordBytes != 0) {
        errorMessage = "TMP raw slot table size is not aligned: " + path;
        return false;
    }

    const std::size_t slotCount = tableBytes / slotRecordBytes;
    if (slotCount == 0 || slotCount > 4096) {
        errorMessage = "TMP raw slot count is unsupported: " + path;
        return false;
    }
    if (headerSlotCount != 0 && headerSlotCount != slotCount) {
        errorMessage = "TMP raw slot count disagrees with header: " + path;
        return false;
    }

    std::vector<std::size_t> imageOffsets(slotCount, 0);
    std::vector<std::pair<unsigned int, unsigned int>> dimensions(slotCount, {0, 0});
    std::vector<std::pair<int, int>> imageAnchors(slotCount, {0, 0});
    unsigned int maxWidth = 0;
    unsigned int maxHeight = 0;

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t recordOffset = preludeBytes + (i * slotRecordBytes);
        const unsigned int packedAnchor = readUInt32(bytes, recordOffset);
        const unsigned int packedDimensions = readUInt32(bytes, recordOffset + 4);
        const unsigned int width = packedDimensions & 0xFFFFU;
        const unsigned int height = (packedDimensions >> 16) & 0xFFFFU;
        const unsigned int dataOffset = readUInt32(bytes, recordOffset + dataOffsetOffset);

        if (dataOffset == 0) {
            continue;
        }

        if (dataOffset < firstDataOffset || dataOffset >= bytes.size()) {
            errorMessage = "TMP raw image offset is outside file: " + path;
            return false;
        }

        if (width == 0 || height == 0 || width > 512 || height > 512) {
            errorMessage = "TMP raw image dimensions are unsupported: " + path;
            return false;
        }

        imageOffsets[i] = static_cast<std::size_t>(dataOffset);
        dimensions[i] = {width, height};
        imageAnchors[i] = {
            signExtendUInt16(packedAnchor & 0xFFFFU),
            signExtendUInt16((packedAnchor >> 16) & 0xFFFFU)
        };
        maxWidth = std::max(maxWidth, width);
        maxHeight = std::max(maxHeight, height);
    }

    tileFile.blockWidth = headerWidth > 0 ? static_cast<int>(headerWidth) : static_cast<int>(maxWidth);
    tileFile.blockHeight = headerHeight > 0 ? static_cast<int>(headerHeight) : static_cast<int>(maxHeight);
    tileFile.images.assign(slotCount, TmpSubTileImage{});

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t imageOffset = imageOffsets[i];
        if (imageOffset == 0) {
            continue;
        }

        const std::size_t imageRecordEnd =
            findNextTmpImageOffset(imageOffsets, imageOffset, bytes.size());
        std::vector<unsigned char> pixels;
        if (!decodeRawTmpFormat3Pixels(
                bytes,
                imageOffset,
                imageRecordEnd,
                dimensions[i].first,
                dimensions[i].second,
                pixels,
                errorMessage
            )
        ) {
            errorMessage += " path=" + path;
            return false;
        }

        TmpSubTileImage& image = tileFile.images[i];
        image.present = true;
        image.offsetX = imageAnchors[i].first;
        image.offsetY = imageAnchors[i].second;
        image.width = static_cast<int>(dimensions[i].first);
        image.height = static_cast<int>(dimensions[i].second);
        image.globalWidth = tileFile.blockWidth;
        image.globalHeight = tileFile.blockHeight;
        image.pixels = std::move(pixels);
        image.baseOffsetX = 0;
        image.baseOffsetY = 0;
        image.baseWidth = 0;
        image.baseHeight = 0;
        image.basePixels.clear();
        image.baseZData.clear();
        image.hasBaseZData = false;
        image.hasExtra = false;
        image.extraOffsetX = 0;
        image.extraOffsetY = 0;
        image.extraWidth = 0;
        image.extraHeight = 0;
        image.extraPixels.clear();
        image.extraZData.clear();
        image.hasExtraZData = false;
    }

    return true;
}

bool loadTmpTileFileFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceLabel,
    TmpTileFile& tileFile,
    std::string& errorMessage
) {
    constexpr std::size_t headerBytes = 16;
    constexpr std::size_t offsetBytes = 4;

    if (bytes.size() < headerBytes + offsetBytes) {
        errorMessage = "TMP file is too small: " + sourceLabel;
        return false;
    }

    const unsigned int xBlocks = readUInt32(bytes, 0);
    const unsigned int yBlocks = readUInt32(bytes, 4);
    const unsigned int blockWidth = readUInt32(bytes, 8);
    const unsigned int blockHeight = readUInt32(bytes, 12);

    if (
        xBlocks == 0
        || yBlocks == 0
        || xBlocks > 64
        || yBlocks > 64
        || blockWidth == 0
        || blockHeight == 0
        || blockWidth > 512
        || blockHeight > 512
    ) {
        return loadRawTmpTileFile(bytes, sourceLabel, tileFile, errorMessage);
    }

    std::size_t slotCount = 0;
    if (!checkedMul(static_cast<std::size_t>(xBlocks), static_cast<std::size_t>(yBlocks), slotCount)) {
        errorMessage = "TMP slot count overflows: " + sourceLabel;
        return false;
    }

    std::size_t tableBytes = 0;
    if (!checkedMul(slotCount, offsetBytes, tableBytes)) {
        errorMessage = "TMP offset table size overflows: " + sourceLabel;
        return false;
    }

    std::size_t tableEnd = 0;
    if (!checkedAdd(headerBytes, tableBytes, tableEnd) || tableEnd > bytes.size()) {
        errorMessage = "TMP offset table points beyond file: " + sourceLabel;
        return false;
    }

    tileFile.blockWidth = static_cast<int>(blockWidth);
    tileFile.blockHeight = static_cast<int>(blockHeight);
    tileFile.images.assign(slotCount, TmpSubTileImage{});

    std::vector<std::size_t> imageOffsets(slotCount, 0);
    for (std::size_t i = 0; i < slotCount; ++i) {
        const unsigned int rawOffset = readUInt32(bytes, headerBytes + (i * offsetBytes));
        if (rawOffset == 0) {
            continue;
        }

        imageOffsets[i] = static_cast<std::size_t>(rawOffset);
    }

    if (!validateTmpImageOffsets(imageOffsets, tableEnd, bytes.size(), sourceLabel, errorMessage)) {
        return false;
    }

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t imageOffset = imageOffsets[i];
        if (imageOffset == 0) {
            continue;
        }

        const std::size_t imageRecordEnd =
            findNextTmpImageOffset(imageOffsets, imageOffset, bytes.size());
        if (!decodeTmpImagePixels(
                bytes,
                imageOffset,
                imageRecordEnd,
                blockWidth,
                blockHeight,
                tileFile.images[i],
                errorMessage
            )
        ) {
            errorMessage += " path=" + sourceLabel;
            return false;
        }
    }

    return true;
}

bool loadTmpTileFile(
    const std::string& path,
    TmpTileFile& tileFile,
    std::string& errorMessage
) {
    std::vector<unsigned char> bytes;
    if (!loadBytes(path, bytes, errorMessage)) {
        return false;
    }

    return loadTmpTileFileFromBytes(bytes, path, tileFile, errorMessage);
}

Color averageTmpBaseColor(
    const TmpSubTileImage& image,
    const std::vector<Color>& palette,
    Color fallback
) {
    std::uint64_t red = 0;
    std::uint64_t green = 0;
    std::uint64_t blue = 0;
    std::uint64_t count = 0;

    for (const unsigned char paletteIndex : image.basePixels) {
        if (paletteIndex == 0
            || static_cast<std::size_t>(paletteIndex) >= palette.size()
        ) {
            continue;
        }

        const Color color = palette[paletteIndex];
        red += color.r;
        green += color.g;
        blue += color.b;
        ++count;
    }

    if (count == 0) {
        return fallback;
    }

    return Color{
        static_cast<unsigned char>(red / count),
        static_cast<unsigned char>(green / count),
        static_cast<unsigned char>(blue / count)
    };
}

Color estimateTmpFileBaseColor(
    const TheaterTileReference& reference,
    const std::vector<Color>& palette,
    Color fallback
) {
    TmpTileFile tileFile;
    std::string ignoredError;
    if (!loadTmpTileFile(reference.resolvedPath, tileFile, ignoredError)) {
        return fallback;
    }

    for (const TmpSubTileImage& image : tileFile.images) {
        if (image.present && !image.basePixels.empty()) {
            return averageTmpBaseColor(image, palette, fallback);
        }
    }

    return fallback;
}

void drawTmpImage(
    std::vector<Color>& pixels,
    int width,
    int height,
    int targetX,
    int targetY,
    const TmpSubTileImage& image,
    const std::vector<Color>& palette
) {
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const unsigned char paletteIndex =
                image.pixels[static_cast<std::size_t>(y * image.width + x)];
            if (paletteIndex == 0) {
                continue;
            }

            putPixel(
                pixels,
                width,
                height,
                targetX + image.offsetX + x,
                targetY + image.offsetY + y,
                palette[paletteIndex]
            );
        }
    }
}

void putZBufferedPixel(
    std::vector<Color>& pixels,
    std::vector<int>& zBuffer,
    int width,
    int height,
    int x,
    int y,
    int zValue,
    Color color
) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(y * width + x);
    if (zValue < zBuffer[index]) {
        return;
    }

    pixels[index] = color;
    zBuffer[index] = zValue;
}

void drawTmpImageZBuffered(
    std::vector<Color>& pixels,
    std::vector<int>& zBuffer,
    int width,
    int height,
    int targetX,
    int targetY,
    const MapIsoTile& tile,
    const TmpSubTileImage& image,
    const std::vector<Color>& palette,
    int cellHeight,
    bool drawZeroBase,
    int zBias
) {
    if (image.baseWidth <= 0 || image.baseHeight <= 0 || image.basePixels.empty()) {
        return;
    }

    const int baseZ = (tile.x + tile.y) * cellHeight / 2;
    const int halfHeight = image.baseHeight / 2;
    int rowWidth = 0;
    std::size_t sourceIndex = 0;

    for (int y = 0; y < image.baseHeight; ++y) {
        if (y < halfHeight) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }

        if (rowWidth <= 0) {
            continue;
        }

        const int xStart = (image.baseWidth - rowWidth) / 2;
        for (int x = 0; x < rowWidth; ++x) {
            if (sourceIndex >= image.basePixels.size()) {
                return;
            }

            const unsigned char paletteIndex = image.basePixels[sourceIndex];
            const int zAdjust =
                image.hasBaseZData && sourceIndex < image.baseZData.size()
                    ? image.baseZData[sourceIndex]
                    : 0;
            ++sourceIndex;

            if (paletteIndex == 0 && !drawZeroBase) {
                continue;
            }

            putZBufferedPixel(
                pixels,
                zBuffer,
                width,
                height,
                targetX + image.baseOffsetX + xStart + x,
                targetY + image.baseOffsetY + y,
                baseZ - zAdjust + zBias,
                palette[paletteIndex]
            );
        }
    }

    if (!image.hasExtra || image.extraPixels.empty()) {
        return;
    }

    const int extraTargetX = targetX + image.extraOffsetX - image.baseOffsetX;
    const int extraTargetY = targetY + image.extraOffsetY - image.baseOffsetY;
    for (int y = 0; y < image.extraHeight; ++y) {
        for (int x = 0; x < image.extraWidth; ++x) {
            const std::size_t source =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.extraWidth))
                + static_cast<std::size_t>(x);
            if (source >= image.extraPixels.size()) {
                return;
            }

            const unsigned char paletteIndex = image.extraPixels[source];
            if (paletteIndex == 0) {
                continue;
            }

            const int zAdjust =
                image.hasExtraZData && source < image.extraZData.size()
                    ? image.extraZData[source]
                    : 0;
            putZBufferedPixel(
                pixels,
                zBuffer,
                width,
                height,
                extraTargetX + x,
                extraTargetY + y,
                baseZ - zAdjust + zBias,
                palette[paletteIndex]
            );
        }
    }
}

void drawDiamondFillZBuffered(
    std::vector<Color>& pixels,
    std::vector<int>& zBuffer,
    int width,
    int height,
    int targetX,
    int targetY,
    int cellWidth,
    int cellHeight,
    const MapIsoTile& tile,
    Color color,
    int zBias
) {
    const int halfHeight = cellHeight / 2;
    int rowWidth = 0;
    const int baseZ = (tile.x + tile.y) * cellHeight / 2;

    for (int y = 0; y < cellHeight; ++y) {
        if (y < halfHeight) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }

        if (rowWidth <= 0) {
            continue;
        }

        const int xStart = (cellWidth - rowWidth) / 2;
        for (int x = 0; x < rowWidth; ++x) {
            putZBufferedPixel(
                pixels,
                zBuffer,
                width,
                height,
                targetX + xStart + x,
                targetY + y,
                baseZ + zBias,
                color
            );
        }
    }
}

void drawDiamondFill(
    std::vector<Color>& pixels,
    int width,
    int height,
    int centerX,
    int centerY,
    int cellWidth,
    int cellHeight,
    Color color
) {
    const int halfWidth = cellWidth / 2;
    const int halfHeight = cellHeight / 2;
    if (halfWidth <= 0 || halfHeight <= 0) {
        return;
    }

    for (int localY = 0; localY <= cellHeight; ++localY) {
        const int y = centerY - halfHeight + localY;
        const int dy = absInt(localY - halfHeight);
        const int span = ((halfHeight - dy) * halfWidth) / halfHeight;

        for (int x = centerX - span; x <= centerX + span; ++x) {
            putPixel(pixels, width, height, x, y, color);
        }
    }
}

void drawTileMarker(
    std::vector<Color>& pixels,
    int width,
    int height,
    int x,
    int y,
    Color color
) {
    putPixel(pixels, width, height, x, y, color);
    putPixel(pixels, width, height, x - 1, y, color);
    putPixel(pixels, width, height, x + 1, y, color);
    putPixel(pixels, width, height, x, y - 1, color);
    putPixel(pixels, width, height, x, y + 1, color);
}

} // namespace

bool exportMapPreviewToPpm(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    std::string& errorMessage
) {
    errorMessage.clear();

    const std::vector<MapIsoTile>& tiles = mapDatabase.getIsoTiles();
    if (tiles.empty()) {
        errorMessage = "map has no decoded IsoMapPack5 tiles";
        return false;
    }

    const PreviewBounds bounds = calculatePreviewBounds(tiles);

    constexpr int margin = 8;
    const int width = std::clamp(bounds.maxX - bounds.minX + (margin * 2) + 1, 1, 4096);
    const int height = std::clamp(bounds.maxY - bounds.minY + (margin * 2) + 1, 1, 4096);

    std::vector<Color> pixels(
        static_cast<std::size_t>(width * height),
        Color{18, 22, 28}
    );

    const std::unordered_map<int, const MapOverlayCell*> overlayByCell =
        buildOverlayCellLookup(mapDatabase.getOverlayCells());

    for (const MapIsoTile& tile : tiles) {
        const ProjectedPoint point = projectIsoTile(tile);
        Color color = makeTileColor(tile);

        const int overlayCell = overlayCellFromTile(tile);
        const auto overlayIt = overlayByCell.find(overlayCell);
        if (overlayIt != overlayByCell.end()) {
            const MapOverlayCell& overlay = *overlayIt->second;
            if (overlay.isGem) {
                color = blend(color, Color{210, 64, 220});
            } else if (overlay.isOre) {
                color = blend(color, Color{220, 190, 32});
            } else {
                color = blend(color, Color{120, 130, 150});
            }
        }

        drawTileMarker(
            pixels,
            width,
            height,
            point.x - bounds.minX + margin,
            point.y - bounds.minY + margin,
            color
        );
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "failed to open output file: " + outputPath;
        return false;
    }

    output << "P6\n" << width << " " << height << "\n255\n";
    for (const Color& color : pixels) {
        output.write(reinterpret_cast<const char*>(&color.r), 1);
        output.write(reinterpret_cast<const char*>(&color.g), 1);
        output.write(reinterpret_cast<const char*>(&color.b), 1);
    }

    if (!output.good()) {
        errorMessage = "failed while writing output file: " + outputPath;
        return false;
    }

    return true;
}

bool exportMapTmpPreviewToPpm(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    std::string& errorMessage
) {
    errorMessage.clear();

    constexpr int cellWidth = 60;
    constexpr int cellHeight = 30;
    constexpr int margin = 96;
    constexpr std::uint64_t maxPixels = 80000000ULL;

    const std::vector<MapIsoTile>& tiles = mapDatabase.getIsoTiles();
    if (tiles.empty()) {
        errorMessage = "map has no decoded IsoMapPack5 tiles";
        return false;
    }

    if (tileRootPath.empty()) {
        errorMessage = "tile root path is required for TMP map preview";
        return false;
    }

    std::vector<Color> palette;
    if (!loadPalette(palettePath, palette, errorMessage)) {
        return false;
    }

    const MapTileUsageReport usageReport =
        theaterDatabase.buildMapTileUsageReport(mapDatabase, tileRootPath);

    if (usageReport.unresolvedTileCount > 0 || usageReport.missingFileCount > 0) {
        errorMessage =
            "map TMP preview requires complete tile files; unresolved="
            + std::to_string(usageReport.unresolvedTileCount)
            + " missing="
            + std::to_string(usageReport.missingFileCount);
        return false;
    }

    std::unordered_map<int, TheaterTileReference> referencesByTileIndex;
    referencesByTileIndex.reserve(usageReport.usedTiles.size());
    for (const TheaterTileReference& reference : usageReport.usedTiles) {
        referencesByTileIndex.emplace(reference.tileIndex, reference);
    }
    const bool hasWaterTerrain = mapUsesWaterLikeTerrain(usageReport);
    int waterFillTileIndex = -1;
    if (hasWaterTerrain) {
        waterFillTileIndex = findWaterFillTileIndex(theaterDatabase, tileRootPath);
    }
    if (waterFillTileIndex >= 0 && referencesByTileIndex.find(waterFillTileIndex) == referencesByTileIndex.end()) {
        TheaterTileReference waterReference = theaterDatabase.resolveTileIndex(waterFillTileIndex, tileRootPath);
        if (waterReference.validTileSet && waterReference.fileExists) {
            referencesByTileIndex.emplace(waterFillTileIndex, waterReference);
        }
    }
    if (referencesByTileIndex.find(0) == referencesByTileIndex.end()) {
        TheaterTileReference clearReference = theaterDatabase.resolveTileIndex(0, tileRootPath);
        if (clearReference.validTileSet && clearReference.fileExists) {
            referencesByTileIndex.emplace(0, clearReference);
        }
    }
    const int pavementUnderlayTileIndex =
        findPavementUnderlayTileIndex(theaterDatabase, tileRootPath);
    if (pavementUnderlayTileIndex >= 0
        && referencesByTileIndex.find(pavementUnderlayTileIndex) == referencesByTileIndex.end()
    ) {
        TheaterTileReference pavementReference =
            theaterDatabase.resolveTileIndex(pavementUnderlayTileIndex, tileRootPath);
        if (pavementReference.validTileSet && pavementReference.fileExists) {
            referencesByTileIndex.emplace(pavementUnderlayTileIndex, pavementReference);
        }
    }

    std::vector<MapIsoTile> renderTiles = buildRenderableTerrainTiles(
        mapDatabase,
        referencesByTileIndex,
        theaterDatabase,
        tileRootPath,
        0,
        waterFillTileIndex,
        pavementUnderlayTileIndex
    );
    if (renderTiles.empty()) {
        errorMessage = "map has no renderable terrain tiles";
        return false;
    }

    const PreviewBounds bounds = calculatePreviewBounds(renderTiles);
    const int width = ((bounds.maxX - bounds.minX) * (cellWidth / 2))
        + (margin * 2)
        + cellWidth;
    const int height = ((bounds.maxY - bounds.minY) * (cellHeight / 2))
        + (margin * 2)
        + cellHeight;

    if (width <= 0 || height <= 0) {
        errorMessage = "computed TMP preview dimensions are invalid";
        return false;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount > maxPixels) {
        errorMessage = "computed TMP preview is too large";
        return false;
    }

    std::unordered_map<std::string, TmpTileFile> tileCache;
    const Color defaultBackgroundColor{18, 22, 28};
    Color waterBackgroundColor{32, 39, 59};
    if (hasWaterTerrain && waterFillTileIndex >= 0) {
        const auto waterReferenceIt = referencesByTileIndex.find(waterFillTileIndex);
        if (waterReferenceIt != referencesByTileIndex.end()) {
            waterBackgroundColor =
                estimateTmpFileBaseColor(waterReferenceIt->second, palette, waterBackgroundColor);
        }
    }
    const Color backgroundColor = hasWaterTerrain ? waterBackgroundColor : defaultBackgroundColor;
    constexpr int underlayZBias = -512;

    std::vector<Color> pixels(
        static_cast<std::size_t>(pixelCount),
        backgroundColor
    );
    std::vector<int> zBuffer(
        static_cast<std::size_t>(pixelCount),
        std::numeric_limits<int>::min()
    );

    const MapRect fullSize = parseMapRect(mapDatabase.getSummary().size);
    sortTerrainTilesForDrawing(renderTiles, fullSize.valid ? fullSize.width : 0);
    const bool drawPlainWaterSprites =
        shouldDrawPlainWaterTerrainSprites(renderTiles, referencesByTileIndex);

    for (const MapIsoTile& tile : renderTiles) {
        const int resolvedTileIndex = tile.tileIndex;
        const int resolvedSubTileIndex = tile.subTileIndex;
        const auto referenceIt = referencesByTileIndex.find(resolvedTileIndex);
        if (referenceIt == referencesByTileIndex.end()) {
            errorMessage = "missing tile reference for tileIndex " + std::to_string(resolvedTileIndex);
            return false;
        }

        const TheaterTileReference& reference = referenceIt->second;
        if (isPlainWaterTerrain(reference) && !drawPlainWaterSprites) {
            continue;
        }

        const int underlayTileIndex =
            chooseTerrainUnderlayTileIndex(reference, 0, pavementUnderlayTileIndex);
        if (underlayTileIndex >= 0) {
            const auto underlayReferenceIt = referencesByTileIndex.find(underlayTileIndex);
            if (underlayReferenceIt != referencesByTileIndex.end()) {
                const TheaterTileReference& underlayReference = underlayReferenceIt->second;
                const auto [underlayCacheIt, underlayInserted] =
                    tileCache.emplace(underlayReference.resolvedPath, TmpTileFile{});
                if (underlayInserted) {
                    if (!loadTmpTileFile(underlayReference.resolvedPath, underlayCacheIt->second, errorMessage)) {
                        return false;
                    }
                }

                const TmpTileFile& underlayTileFile = underlayCacheIt->second;
                if (!underlayTileFile.images.empty() && underlayTileFile.images.front().present) {
                    const ProjectedPoint point = projectIsoTile(tile);
                    const int targetX = ((point.x - bounds.minX) * (cellWidth / 2)) + margin;
                    const int targetY = ((point.y - bounds.minY) * (cellHeight / 2)) + margin;
                    drawTmpImageZBuffered(
                        pixels,
                        zBuffer,
                        width,
                        height,
                        targetX,
                        targetY,
                        tile,
                        underlayTileFile.images.front(),
                        palette,
                        cellHeight,
                        false,
                        underlayZBias
                    );
                }
            }
        }

        const auto [cacheIt, inserted] = tileCache.emplace(reference.resolvedPath, TmpTileFile{});
        if (inserted) {
            if (!loadTmpTileFile(reference.resolvedPath, cacheIt->second, errorMessage)) {
                return false;
            }
        }

        const TmpTileFile& tileFile = cacheIt->second;
        if (resolvedSubTileIndex < 0
            || static_cast<std::size_t>(resolvedSubTileIndex) >= tileFile.images.size()
        ) {
            errorMessage =
                reference.expectedFileName
                + " does not contain subTileIndex "
                + std::to_string(resolvedSubTileIndex);
            return false;
        }

        const TmpSubTileImage& image = tileFile.images[static_cast<std::size_t>(resolvedSubTileIndex)];
        if (!image.present) {
            continue;
        }

        const ProjectedPoint point = projectIsoTile(tile);
        const int targetX = ((point.x - bounds.minX) * (cellWidth / 2)) + margin;
        const int targetY = ((point.y - bounds.minY) * (cellHeight / 2)) + margin;

        drawTmpImageZBuffered(
            pixels,
            zBuffer,
            width,
            height,
            targetX,
            targetY,
            tile,
            image,
            palette,
            cellHeight,
            false,
            0
        );
    }

    const std::filesystem::path outputFile(outputPath);
    const std::filesystem::path parent = outputFile.parent_path();
    if (!parent.empty()) {
        try {
            std::filesystem::create_directories(parent);
        } catch (const std::exception& exception) {
            errorMessage = "failed to create output directory: ";
            errorMessage += exception.what();
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "failed to open output file: " + outputPath;
        return false;
    }

    output << "P6\n" << width << " " << height << "\n255\n";
    for (const Color& color : pixels) {
        output.write(reinterpret_cast<const char*>(&color.r), 1);
        output.write(reinterpret_cast<const char*>(&color.g), 1);
        output.write(reinterpret_cast<const char*>(&color.b), 1);
    }

    if (!output.good()) {
        errorMessage = "failed while writing output file: " + outputPath;
        return false;
    }

    return true;
}

bool exportMapTmpPreviewToHtml(
    concord::assets::MapPreviewResourceSession& session,
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    const MapStructureSpriteOptions& structureSpriteOptions,
    MapStructureSpriteStats& structureSpriteStats,
    std::string& errorMessage
) {
    const auto& bound = session.context();
    if (bound.tileRootPath != tileRootPath
        || bound.assetRootPath != structureSpriteOptions.assetRootPath
        || bound.palettePath != palettePath
        || bound.structurePalettePath != structureSpriteOptions.structurePalettePath) {
        errorMessage = "preview resource session context differs from configured resource paths";
        return false;
    }

    errorMessage.clear();

    constexpr int cellWidth = 60;
    constexpr int cellHeight = 30;
    constexpr int margin = 96;
    constexpr std::uint64_t maxPixels = 80000000ULL;

    const std::vector<MapIsoTile>& tiles = mapDatabase.getIsoTiles();
    if (tiles.empty()) {
        errorMessage = "map has no decoded IsoMapPack5 tiles";
        return false;
    }

    if (tileRootPath.empty()) {
        errorMessage = "tile root path is required for TMP map HTML preview";
        return false;
    }

    std::vector<Color> palette;
    if (!loadPalette(palettePath, palette, errorMessage)) {
        return false;
    }

    std::vector<Color> structurePalette = palette;
    if (!structureSpriteOptions.structurePalettePath.empty()
        && !loadPalette(structureSpriteOptions.structurePalettePath, structurePalette, errorMessage)
    ) {
        return false;
    }

    const MapTileUsageReport usageReport =
        theaterDatabase.buildMapTileUsageReport(mapDatabase, tileRootPath);

    if (usageReport.unresolvedTileCount > 0 || usageReport.missingFileCount > 0) {
        errorMessage =
            "map TMP HTML preview requires complete tile files; unresolved="
            + std::to_string(usageReport.unresolvedTileCount)
            + " missing="
            + std::to_string(usageReport.missingFileCount);
        return false;
    }

    std::unordered_map<int, TheaterTileReference> referencesByTileIndex;
    referencesByTileIndex.reserve(usageReport.usedTiles.size());
    for (const TheaterTileReference& reference : usageReport.usedTiles) {
        referencesByTileIndex.emplace(reference.tileIndex, reference);
    }
    const bool hasWaterTerrain = mapUsesWaterLikeTerrain(usageReport);
    int waterFillTileIndex = -1;
    if (hasWaterTerrain) {
        waterFillTileIndex = findWaterFillTileIndex(theaterDatabase, tileRootPath);
    }
    if (waterFillTileIndex >= 0 && referencesByTileIndex.find(waterFillTileIndex) == referencesByTileIndex.end()) {
        TheaterTileReference waterReference = theaterDatabase.resolveTileIndex(waterFillTileIndex, tileRootPath);
        if (waterReference.validTileSet && waterReference.fileExists) {
            referencesByTileIndex.emplace(waterFillTileIndex, waterReference);
        }
    }
    if (referencesByTileIndex.find(0) == referencesByTileIndex.end()) {
        TheaterTileReference clearReference = theaterDatabase.resolveTileIndex(0, tileRootPath);
        if (clearReference.validTileSet && clearReference.fileExists) {
            referencesByTileIndex.emplace(0, clearReference);
        }
    }
    const int pavementUnderlayTileIndex =
        findPavementUnderlayTileIndex(theaterDatabase, tileRootPath);
    if (pavementUnderlayTileIndex >= 0
        && referencesByTileIndex.find(pavementUnderlayTileIndex) == referencesByTileIndex.end()
    ) {
        TheaterTileReference pavementReference =
            theaterDatabase.resolveTileIndex(pavementUnderlayTileIndex, tileRootPath);
        if (pavementReference.validTileSet && pavementReference.fileExists) {
            referencesByTileIndex.emplace(pavementUnderlayTileIndex, pavementReference);
        }
    }

    const Color defaultBackgroundColor{16, 21, 27};
    Color waterBackgroundColor{32, 39, 59};
    if (hasWaterTerrain && waterFillTileIndex >= 0) {
        const auto waterReferenceIt = referencesByTileIndex.find(waterFillTileIndex);
        if (waterReferenceIt != referencesByTileIndex.end()) {
            waterBackgroundColor =
                estimateTmpFileBaseColor(waterReferenceIt->second, palette, waterBackgroundColor);
        }
    }
    const Color terrainBackgroundColor =
        hasWaterTerrain ? waterBackgroundColor : defaultBackgroundColor;

    std::vector<MapIsoTile> renderTiles = buildRenderableTerrainTiles(
        mapDatabase,
        referencesByTileIndex,
        theaterDatabase,
        tileRootPath,
        0,
        waterFillTileIndex,
        pavementUnderlayTileIndex
    );
    if (renderTiles.empty()) {
        errorMessage = "map has no renderable terrain tiles";
        return false;
    }

    const PreviewBounds bounds = calculatePreviewBounds(renderTiles);
    const int canvasWidth = ((bounds.maxX - bounds.minX) * (cellWidth / 2))
        + (margin * 2)
        + cellWidth;
    const int canvasHeight = ((bounds.maxY - bounds.minY) * (cellHeight / 2))
        + (margin * 2)
        + cellHeight;

    if (canvasWidth <= 0 || canvasHeight <= 0) {
        errorMessage = "computed TMP HTML preview dimensions are invalid";
        return false;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(canvasWidth) * static_cast<std::uint64_t>(canvasHeight);
    if (pixelCount > maxPixels) {
        errorMessage = "computed TMP HTML preview is too large";
        return false;
    }

    const std::unordered_map<int, const MapOverlayCell*> overlayByCell =
        buildOverlayCellLookup(mapDatabase.getOverlayCells());
    const std::unordered_map<int, int> levelByCell = buildTileLevelLookup(renderTiles);
    std::vector<ShpFrameImage> structureSpriteImages;
    const std::unordered_map<std::string, int> structureSpriteIds =
        buildStructureSpriteIds(
            mapDatabase,
            structureSpriteOptions,
            theaterDatabase,
            structureSpriteStats,
            structureSpriteImages
        );
    const std::unordered_map<std::string, StructureFootprint> structureFootprints =
        buildStructureFootprints(mapDatabase, structureSpriteOptions);
    const std::vector<TmpHtmlObjectRecord> objectRecords =
        buildTmpHtmlObjectRecords(
            mapDatabase,
            levelByCell,
            structureSpriteIds,
            structureFootprints
        );

    const MapRect fullSize = parseMapRect(mapDatabase.getSummary().size);
    sortTerrainTilesForDrawing(renderTiles, fullSize.valid ? fullSize.width : 0);
    const bool drawPlainWaterSprites =
        shouldDrawPlainWaterTerrainSprites(renderTiles, referencesByTileIndex);

    std::unordered_map<std::string, TmpTileFile> tileCache;
    std::unordered_map<std::string, int> imageIdsByKey;
    std::vector<TmpHtmlImageRecord> encodedImages;
    std::vector<TmpHtmlTileRecord> records;
    records.reserve(renderTiles.size());

    auto addEncodedImage =
        [&imageIdsByKey, &encodedImages](const std::string& imageKey, const TmpSubTileImage& image) {
            const auto imageIdIt = imageIdsByKey.find(imageKey);
            if (imageIdIt != imageIdsByKey.end()) {
                return imageIdIt->second;
            }

            const int imageId = static_cast<int>(encodedImages.size());
            imageIdsByKey.emplace(imageKey, imageId);
            encodedImages.push_back({
                image.offsetX,
                image.offsetY,
                image.globalWidth,
                image.globalHeight,
                image.width,
                image.height,
                image.pixels,
                image.baseOffsetX,
                image.baseOffsetY,
                image.baseWidth,
                image.baseHeight,
                image.basePixels,
                image.baseZData,
                image.hasBaseZData,
                image.hasExtra,
                image.extraOffsetX,
                image.extraOffsetY,
                image.extraWidth,
                image.extraHeight,
                image.extraPixels,
                image.extraZData,
                image.hasExtraZData
            });
            return imageId;
        };

    auto addEncodedShpImage =
        [&imageIdsByKey, &encodedImages](const std::string& imageKey, const ShpFrameImage& image) {
            const auto imageIdIt = imageIdsByKey.find(imageKey);
            if (imageIdIt != imageIdsByKey.end()) {
                return imageIdIt->second;
            }

            const int imageId = static_cast<int>(encodedImages.size());
            imageIdsByKey.emplace(imageKey, imageId);
            encodedImages.push_back({
                image.offsetX,
                image.offsetY,
                image.globalWidth,
                image.globalHeight,
                image.width,
                image.height,
                image.pixels,
                0,
                0,
                0,
                0,
                {},
                {},
                false,
                false,
                0,
                0,
                0,
                0,
                {},
                {},
                false
            });
            return imageId;
        };

    auto addFirstSubTileImageForTileIndex =
        [&](int tileIndex) {
            const auto referenceIt = referencesByTileIndex.find(tileIndex);
            if (referenceIt == referencesByTileIndex.end()) {
                return -1;
            }

            const TheaterTileReference& reference = referenceIt->second;
            const auto [cacheIt, inserted] =
                tileCache.emplace(reference.resolvedPath, TmpTileFile{});
            if (inserted) {
                if (!loadTmpTileFile(reference.resolvedPath, cacheIt->second, errorMessage)) {
                    return -1;
                }
            }

            const TmpTileFile& tileFile = cacheIt->second;
            if (tileFile.images.empty() || !tileFile.images.front().present) {
                return -1;
            }

            return addEncodedImage(reference.resolvedPath + "#0", tileFile.images.front());
        };

    const int clearImageId = addFirstSubTileImageForTileIndex(0);
    const int pavementUnderlayImageId =
        pavementUnderlayTileIndex >= 0
            ? addFirstSubTileImageForTileIndex(pavementUnderlayTileIndex)
            : -1;

    std::vector<std::string> overlayIndexWarnings;
    const std::unordered_map<std::string, std::vector<std::string>> overlayFilesByName =
        buildOverlayFileIndex(
            tileRootPath,
            structureSpriteOptions.assetRootPath,
            overlayIndexWarnings
        );
    for (const std::string& warning : overlayIndexWarnings) {
        structureSpriteStats.warnings.push_back("overlay asset index: " + warning);
    }

    std::vector<std::string> mixIndexWarnings;
    const std::vector<IndexedMixArchive> mixArchives =
        buildMixArchiveIndex(
            tileRootPath,
            structureSpriteOptions.assetRootPath,
            mixIndexWarnings
        );
    std::size_t mixEntryIndexBuckets = 0;
    for (const IndexedMixArchive& indexedMix : mixArchives) {
        mixEntryIndexBuckets += indexedMix.entryIndicesById.size();
    }
    structureSpriteStats.warnings.push_back(
        "overlay MIX scan: archives="
        + std::to_string(mixArchives.size())
        + " entryIndexBuckets="
        + std::to_string(mixEntryIndexBuckets)
    );
    for (const std::string& warning : mixIndexWarnings) {
        structureSpriteStats.warnings.push_back("overlay MIX scan: " + warning);
    }

    appendOverlayResourceCoverageWarnings(
        buildOverlayResourceCoverage(session,
            mapDatabase,
            theaterDatabase,
            overlayFilesByName,
            mixArchives
        ),
        structureSpriteStats
    );

    if (structureSpriteOptions.rules != nullptr
        && structureSpriteOptions.art != nullptr
    ) {
        appendBuildingLayerDiagnostics(session,
            structureSpriteStats,
            *structureSpriteOptions.rules,
            *structureSpriteOptions.art,
            theaterDatabase,
            overlayFilesByName,
            mixArchives,
            structureSpriteOptions.structurePalettePath
        );
        appendFlagDiagnostics(
            structureSpriteStats,
            *structureSpriteOptions.rules,
            *structureSpriteOptions.art
        );
        appendSidebarDiagnostics(session,
            structureSpriteStats,
            overlayFilesByName,
            mixArchives
        );
        appendProjectileDiagnostics(session,
            structureSpriteStats,
            *structureSpriteOptions.rules,
            *structureSpriteOptions.art,
            overlayFilesByName,
            mixArchives,
            mapDatabase.getSummary().theater,
            structureSpriteOptions.structurePalettePath,
            palettePath
        );
        appendInfantrySequenceDiagnostics(session,
            structureSpriteStats,
            *structureSpriteOptions.rules,
            *structureSpriteOptions.art,
            overlayFilesByName,
            mixArchives,
            mapDatabase.getSummary().theater,
            structureSpriteOptions.structurePalettePath
        );
    }

    appendBridgeDiagnostics(session,
        structureSpriteStats,
        mapDatabase,
        theaterDatabase,
        tileRootPath,
        referencesByTileIndex,
        overlayFilesByName,
        mixArchives
    );

    std::set<std::string> missingOverlayWarnings;
    std::set<std::string> overlayDecodeWarnings;
    std::set<std::string> overlayFallbackWarnings;

    auto addOverlayImage =
        [&](const MapOverlayCell* overlay) {
            if (overlay == nullptr || overlay->isOre || overlay->isGem) {
                return -1;
            }
            if (!shouldDrawLowBridgeOverlaySprite(*overlay)) {
                return -1;
            }

            const std::vector<std::string> candidates =
                overlayAssetFileCandidates(*overlay, theaterDatabase);
            const std::string warningKey =
                overlay->type
                + "|"
                + overlay->image
                + "|"
                + std::to_string(overlay->frameIndex);

            if (candidates.empty()) {
                if (missingOverlayWarnings.insert(warningKey).second) {
                    structureSpriteStats.warnings.push_back(
                        "missing overlay asset candidate for "
                        + overlay->type
                        + " frame "
                        + std::to_string(overlayResourceDecodeFrame(*overlay))
                    );
                }
                return -1;
            }

            bool foundCandidateFile = false;
            bool sawDecodeFailure = false;
            std::string firstDecodeFailure;

            for (const std::string& candidate : candidates) {
                const auto fileIt = overlayFilesByName.find(candidate);
                if (fileIt == overlayFilesByName.end() || fileIt->second.empty()) {
                    continue;
                }

                foundCandidateFile = true;
                const std::string& path = fileIt->second.front();
                const std::string extension =
                    toLowerAscii(std::filesystem::path(path).extension().string());
                if (extension == ".tem"
                    || extension == ".urb"
                    || extension == ".ubn"
                    || extension == ".des"
                    || extension == ".sno"
                    || extension == ".lun"
                    || extension == ".tmp"
                ) {
                    const auto [cacheIt, inserted] = tileCache.emplace(path, TmpTileFile{});
                    if (inserted) {
                        std::string overlayError;
                        if (!loadTmpTileFile(path, cacheIt->second, overlayError)) {
                            sawDecodeFailure = true;
                            if (firstDecodeFailure.empty()) {
                                firstDecodeFailure = overlayError;
                            }
                            tileCache.erase(cacheIt);
                            continue;
                        }
                    }

                    const TmpTileFile& tileFile = cacheIt->second;
                    const int requestedFrameIndex =
                        overlayResourceDecodeFrame(*overlay);
                    const int resolvedFrameIndex =
                        resolveOverlayTmpImageIndex(*overlay, tileFile);
                    if (resolvedFrameIndex >= 0) {
                        if (resolvedFrameIndex != requestedFrameIndex
                            && overlayFallbackWarnings.insert(warningKey + "|" + path).second
                        ) {
                            structureSpriteStats.warnings.push_back(
                                "overlay TMP frame fallback for "
                                + overlay->type
                                + " frame "
                                + std::to_string(requestedFrameIndex)
                                + " -> "
                                + std::to_string(resolvedFrameIndex)
                                + ": "
                                + std::filesystem::path(path).filename().string()
                            );
                        }

                        const std::string imageKey =
                            "overlay-tmp:" + path + "#" + std::to_string(resolvedFrameIndex);
                        const auto cachedIt = imageIdsByKey.find(imageKey);
                        if (cachedIt != imageIdsByKey.end()) {
                            return cachedIt->second;
                        }

                        return addEncodedImage(
                            imageKey,
                            tileFile.images[static_cast<std::size_t>(resolvedFrameIndex)]
                        );
                    }

                    sawDecodeFailure = true;
                    if (firstDecodeFailure.empty()) {
                        firstDecodeFailure =
                            "TMP file has no present image for overlay frame "
                            + std::to_string(requestedFrameIndex)
                            + ": "
                            + path;
                    }
                    continue;
                }

                const int requestedFrameIndex =
                    overlayResourceDecodeFrame(*overlay);
                const std::string imageKey =
                    "overlay-shp:" + path + "#" + std::to_string(requestedFrameIndex);
                const auto cachedIt = imageIdsByKey.find(imageKey);
                if (cachedIt != imageIdsByKey.end()) {
                    return cachedIt->second;
                }

                ShpFrameImage image;
                std::string overlayError;
                if (!decodeShpFrame(path, requestedFrameIndex, image, overlayError)) {
                    if (requestedFrameIndex != 0
                        && decodeShpFrame(path, 0, image, overlayError)
                    ) {
                        if (overlayFallbackWarnings.insert(warningKey + "|" + path).second) {
                            structureSpriteStats.warnings.push_back(
                                "overlay SHP frame fallback for "
                                + overlay->type
                                + " frame "
                                + std::to_string(requestedFrameIndex)
                                + " -> 0: "
                                + std::filesystem::path(path).filename().string()
                            );
                        }

                        const std::string fallbackKey = "overlay-shp:" + path + "#0";
                        return addEncodedShpImage(fallbackKey, image);
                    }

                    sawDecodeFailure = true;
                    if (firstDecodeFailure.empty()) {
                        firstDecodeFailure = overlayError;
                    }
                    continue;
                }

                return addEncodedShpImage(imageKey, image);
            }

            if (!foundCandidateFile) {
                const OverlayResourceProbe probe =
                    probeOverlayResource(session,*overlay, candidates, overlayFilesByName, mixArchives);
                if (probe.found) {
                    if (missingOverlayWarnings.insert(warningKey + "|mix").second) {
                        structureSpriteStats.warnings.push_back(
                            "overlay asset resolved outside loose preview cache for "
                            + overlay->type
                            + " frame "
                            + std::to_string(overlayResourceDecodeFrame(*overlay))
                            + ": selected="
                            + probe.candidate
                            + " source="
                            + probe.source
                            + " "
                            + probe.decodeResult
                        );
                    }
                } else if (missingOverlayWarnings.insert(warningKey).second) {
                    structureSpriteStats.warnings.push_back(
                        "missing overlay asset for "
                        + overlay->type
                        + " frame "
                        + std::to_string(overlayResourceDecodeFrame(*overlay))
                        + ": candidates="
                        + formatFullCandidateList(candidates)
                        + " "
                        + probe.missSummary
                    );
                }
            } else if (sawDecodeFailure
                && overlayDecodeWarnings.insert(warningKey).second
            ) {
                structureSpriteStats.warnings.push_back(
                    "overlay asset decode failed for "
                    + overlay->type
                    + " frame "
                    + std::to_string(overlayResourceDecodeFrame(*overlay))
                    + ": "
                    + firstDecodeFailure
                );
            }

            return -1;
        };

    for (const MapIsoTile& tile : renderTiles) {
        const int resolvedTileIndex = tile.tileIndex;
        const int resolvedSubTileIndex = tile.subTileIndex;
        const auto referenceIt = referencesByTileIndex.find(resolvedTileIndex);
        if (referenceIt == referencesByTileIndex.end()) {
            errorMessage = "missing tile reference for tileIndex " + std::to_string(resolvedTileIndex);
            return false;
        }

        const TheaterTileReference& reference = referenceIt->second;
        const int overlayCell = overlayCellFromTile(tile);
        const auto overlayIt = overlayByCell.find(overlayCell);
        const bool plainWater = isPlainWaterTerrain(reference);

        if (plainWater && !drawPlainWaterSprites) {
            TmpHtmlTileRecord record;
            record.tile = &tile;
            record.overlay = overlayIt == overlayByCell.end() ? nullptr : overlayIt->second;
            record.imageId = -1;
            record.overlayImageId = addOverlayImage(record.overlay);
            record.resolvedTileIndex = resolvedTileIndex;
            record.resolvedSubTileIndex = resolvedSubTileIndex;
            records.push_back(record);
            continue;
        }

        int imageId = -1;
        const auto [cacheIt, inserted] = tileCache.emplace(reference.resolvedPath, TmpTileFile{});
        if (inserted) {
            if (!loadTmpTileFile(reference.resolvedPath, cacheIt->second, errorMessage)) {
                return false;
            }
        }

        const TmpTileFile& tileFile = cacheIt->second;
        if (resolvedSubTileIndex < 0
            || static_cast<std::size_t>(resolvedSubTileIndex) >= tileFile.images.size()
        ) {
            errorMessage =
                reference.expectedFileName
                + " does not contain subTileIndex "
                + std::to_string(resolvedSubTileIndex);
            return false;
        }

        const TmpSubTileImage& image = tileFile.images[static_cast<std::size_t>(resolvedSubTileIndex)];
        if (!image.present) {
            continue;
        }

        const std::string imageKey =
            reference.resolvedPath + "#" + std::to_string(resolvedSubTileIndex);
        imageId = addEncodedImage(imageKey, image);

        TmpHtmlTileRecord record;
        record.tile = &tile;
        record.overlay = overlayIt == overlayByCell.end() ? nullptr : overlayIt->second;
        record.imageId = imageId;
        record.overlayImageId = addOverlayImage(record.overlay);
        record.drawZeroBase = false;
        record.resolvedTileIndex = resolvedTileIndex;
        record.resolvedSubTileIndex = resolvedSubTileIndex;
        const int underlayTileIndex =
            chooseTerrainUnderlayTileIndex(reference, 0, pavementUnderlayTileIndex);
        if (underlayTileIndex == 0) {
            record.underlayImageId = clearImageId;
        } else if (underlayTileIndex == pavementUnderlayTileIndex) {
            record.underlayImageId = pavementUnderlayImageId;
        }
        records.push_back(record);
    }

    if (records.empty()) {
        errorMessage = "map has no drawable TMP tiles";
        return false;
    }

    const std::filesystem::path outputFile(outputPath);
    const std::filesystem::path parent = outputFile.parent_path();
    if (!parent.empty()) {
        try {
            std::filesystem::create_directories(parent);
        } catch (const std::exception& exception) {
            errorMessage = "failed to create output directory: ";
            errorMessage += exception.what();
            return false;
        }
    }

    const MapSummary& summary = mapDatabase.getSummary();
    const std::string title = summary.name.empty()
        ? "ConcordRTS TMP map preview"
        : summary.name + " TMP preview";
    const MapRect localSize = parseMapRect(summary.localSize);

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "failed to open output file: " + outputPath;
        return false;
    }

    output
        << "<!doctype html>\n"
        << "<html lang=\"en\">\n"
        << "<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << escapeHtmlText(title) << "</title>\n"
        << "<style>\n"
        << ":root{color-scheme:dark;font-family:Segoe UI,Arial,sans-serif;}\n"
        << "body{margin:0;background:#11151a;color:#e6edf3;overflow:hidden;}\n"
        << ".bar{position:fixed;inset:0 0 auto 0;height:104px;display:flex;align-items:center;align-content:center;flex-wrap:wrap;gap:8px 14px;padding:0 14px;background:#171b20;border-bottom:1px solid #2b333d;z-index:2;box-sizing:border-box;}\n"
        << ".title{font-weight:650;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}\n"
        << ".meta{color:#9ba7b4;font-size:12px;white-space:nowrap;}\n"
        << ".toggles{display:flex;align-items:center;gap:8px;font-size:12px;}\n"
        << ".toggle{display:flex;align-items:center;gap:4px;color:#cbd5e1;white-space:nowrap;}\n"
        << ".toggle input{width:13px;height:13px;margin:0;accent-color:#60a5fa;}\n"
        << ".finder{display:flex;align-items:center;gap:6px;font-size:12px;min-width:0;}\n"
        << ".finder input,.finder select{height:24px;background:#0f1318;border:1px solid #334155;color:#e2e8f0;border-radius:4px;padding:0 6px;font:inherit;box-sizing:border-box;}\n"
        << ".finder input{width:140px;}\n"
        << ".finder select{width:240px;}\n"
        << ".finder button{height:24px;border:1px solid #3b82f6;background:#1d4ed8;color:#f8fafc;border-radius:4px;padding:0 8px;font:inherit;cursor:pointer;}\n"
        << ".finder button:disabled{opacity:.45;cursor:default;}\n"
        << ".status{margin-left:auto;color:#dce7f2;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:42vw;}\n"
        << ".viewport{position:absolute;inset:104px 0 0 0;overflow:auto;background:#0f1318;}\n"
        << ".diag{position:fixed;right:12px;bottom:12px;z-index:3;max-width:min(760px,calc(100vw - 24px));max-height:42vh;background:#111820;border:1px solid #334155;color:#d8e2ec;font-size:11px;box-shadow:0 8px 28px rgba(0,0,0,.35);}\n"
        << ".diag summary{cursor:pointer;padding:8px 10px;color:#f8fafc;}\n"
        << ".diag pre{margin:0;padding:0 10px 10px 10px;max-height:34vh;overflow:auto;white-space:pre-wrap;line-height:1.35;}\n"
        << "canvas{display:block;background:#10151b;image-rendering:pixelated;}\n"
        << "@media(max-width:900px){.bar{height:128px}.viewport{inset:128px 0 0 0}.finder select{width:180px}.status{max-width:90vw;}}\n"
        << "</style>\n"
        << "</head>\n"
        << "<body>\n"
        << "<div class=\"bar\"><div class=\"title\">" << escapeHtmlText(title) << "</div>"
        << "<div class=\"meta\">theater "
        << escapeHtmlText(summary.theater.empty() ? "-" : summary.theater)
        << "</div><div class=\"meta\">tiles " << records.size()
        << "</div><div class=\"meta\">sprites " << encodedImages.size()
        << "</div><div class=\"meta\">objects " << objectRecords.size()
        << "</div><div class=\"meta\">structure sprites "
        << structureSpriteStats.embeddedSprites
        << "/" << structureSpriteStats.uniqueStructureTypes
        << " missing " << structureSpriteStats.missingSprites
        << " failed " << structureSpriteStats.decodeFailures
        << "</div><div class=\"toggles\">"
        << "<label class=\"toggle\"><input data-layer=\"overlay\" type=\"checkbox\" checked>Overlay</label>"
        << "<label class=\"toggle\"><input data-layer=\"terrain\" type=\"checkbox\" checked>Terrain</label>"
        << "<label class=\"toggle\"><input data-layer=\"structure\" type=\"checkbox\" checked>Structures</label>"
        << "<label class=\"toggle\"><input data-layer=\"unit\" type=\"checkbox\" checked>Units</label>"
        << "<label class=\"toggle\"><input data-layer=\"infantry\" type=\"checkbox\" checked>Infantry</label>"
        << "</div><div class=\"finder\"><input id=\"objectFilter\" type=\"search\" placeholder=\"filter object\" autocomplete=\"off\">"
        << "<select id=\"objectSelect\"></select><button id=\"focusObject\" type=\"button\">Focus</button></div>"
        << "<div id=\"status\" class=\"status\">hover a tile</div></div>\n"
        << "<div id=\"viewport\" class=\"viewport\"><canvas id=\"map\" width=\"" << canvasWidth
        << "\" height=\"" << canvasHeight << "\"></canvas></div>\n"
        << "<details id=\"diagnostics\" class=\"diag\"><summary>Diagnostics "
        << structureSpriteStats.warnings.size()
        << "</summary><pre id=\"diagText\"></pre></details>\n"
        << "<script>\n"
        << "const tileW=" << cellWidth << ",tileH=" << cellHeight
        << ",margin=" << margin << ";\n"
        << "const minX=" << bounds.minX << ",minY=" << bounds.minY << ";\n"
        << "const terrainBackground='rgb("
        << static_cast<int>(terrainBackgroundColor.r) << ","
        << static_cast<int>(terrainBackgroundColor.g) << ","
        << static_cast<int>(terrainBackgroundColor.b) << ")';\n"
        << "const terrainBackgroundRgb=["
        << static_cast<int>(terrainBackgroundColor.r) << ","
        << static_cast<int>(terrainBackgroundColor.g) << ","
        << static_cast<int>(terrainBackgroundColor.b) << "];\n"
        << "const localSize={valid:" << (localSize.valid ? "true" : "false")
        << ",left:" << localSize.left
        << ",top:" << localSize.top
        << ",width:" << localSize.width
        << ",height:" << localSize.height
        << "};\n"
        << "const terrainPalette=[";

    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (i > 0) {
            output << ",";
        }

        output << static_cast<int>(palette[i].r)
               << "," << static_cast<int>(palette[i].g)
               << "," << static_cast<int>(palette[i].b);
    }

    output << "];\n"
           << "const structurePalette=[";

    for (std::size_t i = 0; i < structurePalette.size(); ++i) {
        if (i > 0) {
            output << ",";
        }

        output << static_cast<int>(structurePalette[i].r)
               << "," << static_cast<int>(structurePalette[i].g)
               << "," << static_cast<int>(structurePalette[i].b);
    }

    output << "];\n"
           << "const images=[\n";

    for (std::size_t i = 0; i < encodedImages.size(); ++i) {
        const TmpHtmlImageRecord& image = encodedImages[i];
        output << "{w:" << image.width
               << ",h:" << image.height
               << ",gw:" << image.globalWidth
               << ",gh:" << image.globalHeight
               << ",x:" << image.offsetX
               << ",y:" << image.offsetY
               << ",bw:" << image.baseWidth
               << ",bh:" << image.baseHeight
               << ",bx:" << image.baseOffsetX
               << ",by:" << image.baseOffsetY
               << ",pixels:\""
               << escapeJavaScriptString(base64Encode(image.pixels))
               << "\",basePixels:\""
               << escapeJavaScriptString(base64Encode(image.basePixels))
               << "\",baseZ:\""
               << escapeJavaScriptString(base64Encode(image.baseZData))
               << "\",hasBaseZ:" << (image.hasBaseZData ? "true" : "false")
               << ",hasExtra:" << (image.hasExtra ? "true" : "false")
               << ",ex:" << image.extraOffsetX
               << ",ey:" << image.extraOffsetY
               << ",ew:" << image.extraWidth
               << ",eh:" << image.extraHeight
               << ",extraPixels:\""
               << escapeJavaScriptString(base64Encode(image.extraPixels))
               << "\",extraZ:\""
               << escapeJavaScriptString(base64Encode(image.extraZData))
               << "\",hasExtraZ:" << (image.hasExtraZData ? "true" : "false")
               << "}";

        if (i + 1 < encodedImages.size()) {
            output << ",";
        }

        output << "\n";
    }

    output << "];\n"
           << "const tiles=[\n";

    for (std::size_t i = 0; i < records.size(); ++i) {
        const MapIsoTile& tile = *records[i].tile;
        const MapOverlayCell* overlay = records[i].overlay;

        output << "["
               << tile.x << ","
               << tile.y << ","
               << tile.tileIndex << ","
               << tile.subTileIndex << ","
               << tile.level << ","
               << tile.iceGrowth << ","
               << (overlay == nullptr ? -1 : overlay->overlayIndex) << ","
               << (overlay == nullptr ? 0 : overlay->frameIndex) << ","
               << overlayKindValue(overlay) << ",\""
               << escapeJavaScriptString(overlay == nullptr ? "" : overlay->type)
               << "\"," << records[i].imageId << ","
               << records[i].underlayImageId << ","
               << records[i].overlayImageId << ","
               << (records[i].drawZeroBase ? 1 : 0) << ",\""
               << escapeJavaScriptString(overlay == nullptr ? "" : overlay->image)
               << "\"]";

        if (i + 1 < records.size()) {
            output << ",";
        }

        output << "\n";
    }

    output << "];\n"
           << "const structureImages=[\n";

    for (std::size_t i = 0; i < structureSpriteImages.size(); ++i) {
        const ShpFrameImage& image = structureSpriteImages[i];
        output << "{w:" << image.width
               << ",h:" << image.height
               << ",gw:" << image.globalWidth
               << ",gh:" << image.globalHeight
               << ",x:" << image.offsetX
               << ",y:" << image.offsetY
               << ",pixels:\""
               << escapeJavaScriptString(base64Encode(image.pixels))
               << "\",path:\""
               << escapeJavaScriptString(image.sourcePath)
               << "\",structure:\""
               << escapeJavaScriptString(image.structureType)
               << "\",art:\""
               << escapeJavaScriptString(image.artId)
               << "\",image:\""
               << escapeJavaScriptString(image.imageName)
               << "\",foundation:\""
               << escapeJavaScriptString(image.foundation)
               << "\",candidates:\""
               << escapeJavaScriptString(image.candidateFiles)
               << "\",active:\""
               << escapeJavaScriptString(image.activeAnimations)
               << "\",anchor:\""
               << escapeJavaScriptString(image.anchorAdvice)
               << "\",frame:" << image.frameIndex
               << ",fmt:" << image.format
               << ",dataOffset:" << image.dataOffset
               << "}";

        if (i + 1 < structureSpriteImages.size()) {
            output << ",";
        }

        output << "\n";
    }

    output << "];\n"
           << "const structureWarnings=[\n";

    for (std::size_t i = 0; i < structureSpriteStats.warnings.size(); ++i) {
        output << "\""
               << escapeJavaScriptString(structureSpriteStats.warnings[i])
               << "\"";

        if (i + 1 < structureSpriteStats.warnings.size()) {
            output << ",";
        }

        output << "\n";
    }

    output << "];\n"
           << "const diagText=document.getElementById('diagText');if(diagText)diagText.textContent=structureWarnings.join('\\n');\n"
           << "const objects=[\n";

    for (std::size_t i = 0; i < objectRecords.size(); ++i) {
        const TmpHtmlObjectRecord& object = objectRecords[i];

        output << "["
               << object.kind << ","
               << object.x << ","
               << object.y << ","
               << object.level << ","
               << object.health << ","
               << object.facing << ","
               << object.subCell << ",\""
               << escapeJavaScriptString(object.owner) << "\",\""
               << escapeJavaScriptString(object.type) << "\",\""
               << escapeJavaScriptString(object.id) << "\",\""
               << escapeJavaScriptString(object.mission) << "\",\""
               << escapeJavaScriptString(object.tag) << "\","
               << object.spriteId << ","
               << object.footprintWidth << ","
               << object.footprintHeight << ","
               << (object.onBridge ? 1 : 0) << "]";

        if (i + 1 < objectRecords.size()) {
            output << ",";
        }

        output << "\n";
    }

    output
        << "];\n"
        << "const canvas=document.getElementById('map');\n"
        << "const viewport=document.getElementById('viewport');\n"
        << "const ctx=canvas.getContext('2d');\n"
        << "ctx.imageSmoothingEnabled=false;\n"
        << "const status=document.getElementById('status');\n"
        << "const objectFilter=document.getElementById('objectFilter');\n"
        << "const objectSelect=document.getElementById('objectSelect');\n"
        << "const focusObject=document.getElementById('focusObject');\n"
        << "const layerState={overlay:true,terrain:true,structure:true,unit:true,infantry:true};\n"
        << "function decodeBase64Bytes(text){const raw=atob(text);const bytes=new Uint8Array(raw.length);for(let i=0;i<raw.length;++i)bytes[i]=raw.charCodeAt(i);return bytes;}\n"
        << "function makeIndexedSprite(data,palette){const source=decodeBase64Bytes(data.pixels);const rgba=new Uint8ClampedArray(data.w*data.h*4),shadowRgba=new Uint8ClampedArray(data.w*data.h*4);for(let i=0,j=0;i<source.length&&j<rgba.length;++i,j+=4){const index=source[i];if(index!==0){const p=index*3;rgba[j]=palette[p];rgba[j+1]=palette[p+1];rgba[j+2]=palette[p+2];rgba[j+3]=255;shadowRgba[j+3]=96;}}const canvas=document.createElement('canvas');canvas.width=data.w;canvas.height=data.h;const sctx=canvas.getContext('2d');sctx.imageSmoothingEnabled=false;sctx.putImageData(new ImageData(rgba,data.w,data.h),0,0);const shadowCanvas=document.createElement('canvas');shadowCanvas.width=data.w;shadowCanvas.height=data.h;const shctx=shadowCanvas.getContext('2d');shctx.imageSmoothingEnabled=false;shctx.putImageData(new ImageData(shadowRgba,data.w,data.h),0,0);return {canvas,shadowCanvas,w:data.w,h:data.h,gw:data.gw||data.w,gh:data.gh||data.h,x:data.x||0,y:data.y||0,path:data.path||'',structure:data.structure||'',art:data.art||'',image:data.image||'',foundation:data.foundation||'',candidates:data.candidates||'',active:data.active||'',anchor:data.anchor||'',frame:data.frame||0,fmt:data.fmt||0,dataOffset:data.dataOffset||0,alpha:source,bw:data.bw||0,bh:data.bh||0,bx:data.bx||0,by:data.by||0,basePixels:data.basePixels?decodeBase64Bytes(data.basePixels):new Uint8Array(0),baseZ:data.baseZ?decodeBase64Bytes(data.baseZ):new Uint8Array(0),hasBaseZ:!!data.hasBaseZ,hasExtra:!!data.hasExtra,ex:data.ex||0,ey:data.ey||0,ew:data.ew||0,eh:data.eh||0,extraPixels:data.extraPixels?decodeBase64Bytes(data.extraPixels):new Uint8Array(0),extraZ:data.extraZ?decodeBase64Bytes(data.extraZ):new Uint8Array(0),hasExtraZ:!!data.hasExtraZ};}\n"
        << "const sprites=images.map(image=>makeIndexedSprite(image,terrainPalette));\n"
        << "const structureSprites=structureImages.map(image=>makeIndexedSprite(image,structurePalette));if(structureWarnings.length)console.warn('Structure sprite warnings',structureWarnings);\n"
        << "function projectCoord(x,y,level){const px=x-y,py=x+y-level;return {x:(px-minX)*(tileW/2)+margin+(tileW/2),y:(py-minY)*(tileH/2)+margin+(tileH/2)};}\n"
        << "function project(t){return projectCoord(t[0],t[1],t[4]);}\n"
        << "function projectObject(o){let p;if(o[0]===1){const fw=Math.max(1,o[13]||1),fh=Math.max(1,o[14]||1);p=projectCoord(o[1]+fw-1,o[2]+fh-1,o[3]);}else{p=projectCoord(o[1],o[2],o[3]);}if(objectOnBridge(o))p.y-=tileH*2;if(o[0]===3&&o[6]>=0){const offsets=[[0,0],[0,-5],[-5,0],[5,0],[0,5],[-4,-4],[4,-4],[-4,4],[4,4]];const offset=offsets[o[6]%offsets.length];p.x+=offset[0];p.y+=offset[1];}return p;}\n"
        << "function objectKindName(kind){return kind===1?'structure':kind===2?'unit':kind===3?'infantry':'terrain';}\n"
        << "function objectOnBridge(o){return !!o[15];}\n"
        << "function isObjectVisible(o){return layerState[objectKindName(o[0])]!==false;}\n"
        << "function isHighBridgeTile(t){const type=t[9]||'';return t[6]===25||t[6]===26||t[6]===241||t[6]===242||type.startsWith('BRIDGE')||type.startsWith('BRIDG');}\n"
        << "function isLowBridgeTile(t){const type=t[9]||'';return type.startsWith('LOBRDG')||type.startsWith('LOBRDB');}\n"
        << "function overlayKindName(k){return k===1?'ore':k===2?'gem':k===3?'bridge':k===4?'wall':k===5?'other':'none';}\n"
        << "function drawOverlayMarker(t){if(t[8]===0||t[12]>=0||isLowBridgeTile(t))return;const p=project(t);ctx.fillStyle=t[8]===2?'rgba(226,74,230,.9)':t[8]===1?'rgba(230,198,52,.9)':'rgba(160,172,188,.85)';ctx.fillRect(Math.round(p.x)-2,Math.round(p.y)-2,4,4);}\n"
        << "function drawSelection(t){const p=project(t);ctx.beginPath();ctx.moveTo(p.x,p.y-tileH/2);ctx.lineTo(p.x+tileW/2,p.y);ctx.lineTo(p.x,p.y+tileH/2);ctx.lineTo(p.x-tileW/2,p.y);ctx.closePath();ctx.lineWidth=2;ctx.strokeStyle='#f0f6fc';ctx.stroke();}\n"
        << "function structureSpriteBounds(o){if(o[0]!==1||o[12]<0)return null;const s=structureSprites[o[12]];if(!s)return null;const p=projectObject(o);const x=Math.round(p.x-s.gw/2+s.x),y=Math.round(p.y+tileH/2-s.gh+s.y);return {x,y,w:s.w,h:s.h,s};}\n"
        << "function drawObjectMarker(o,isSelected){const p=projectObject(o);ctx.save();ctx.lineWidth=isSelected?3:1.5;ctx.strokeStyle=isSelected?'#f8fafc':'rgba(8,12,18,.86)';const spriteBounds=structureSpriteBounds(o);if(spriteBounds){ctx.drawImage(spriteBounds.s.canvas,spriteBounds.x,spriteBounds.y);if(isSelected){ctx.strokeStyle='#f8fafc';ctx.strokeRect(spriteBounds.x-.5,spriteBounds.y-.5,spriteBounds.w+1,spriteBounds.h+1);}ctx.restore();return;}if(o[0]===1){ctx.fillStyle='rgba(96,165,250,.92)';ctx.fillRect(Math.round(p.x)-6,Math.round(p.y)-12,12,12);ctx.strokeRect(Math.round(p.x)-6,Math.round(p.y)-12,12,12);}else if(o[0]===2){ctx.fillStyle='rgba(245,158,11,.94)';ctx.beginPath();ctx.arc(p.x,p.y-7,isSelected?6:5,0,Math.PI*2);ctx.fill();ctx.stroke();}else if(o[0]===3){ctx.fillStyle='rgba(244,114,182,.95)';ctx.beginPath();ctx.arc(p.x,p.y-5,isSelected?4:3,0,Math.PI*2);ctx.fill();ctx.stroke();}else{ctx.fillStyle='rgba(45,212,191,.9)';ctx.beginPath();ctx.moveTo(p.x,p.y-12);ctx.lineTo(p.x+5,p.y-3);ctx.lineTo(p.x-5,p.y-3);ctx.closePath();ctx.fill();ctx.stroke();}ctx.restore();}\n"
        << "function overlaySpriteBounds(t){if(t[12]<0)return null;const s=sprites[t[12]];if(!s)return null;const p=project(t),highBridge=isHighBridgeTile(t),lowBridge=isLowBridgeTile(t);if(highBridge||lowBridge){const x=Math.round(p.x-s.gw/2+s.x),y=Math.round(p.y-s.gh/2+s.y);return {x,y,w:s.w,h:s.h,s};}if(s.basePixels&&s.basePixels.length){const x=Math.round(p.x-tileW/2+s.x),y=Math.round(p.y-tileH/2+s.y);return {x,y,w:s.w,h:s.h,s};}const x=Math.round(p.x-s.gw/2+s.x),y=Math.round(p.y+tileH/2-s.gh+s.y);return {x,y,w:s.w,h:s.h,s};}\n"
        << "function drawOverlaySprite(t){const b=overlaySpriteBounds(t);if(b)ctx.drawImage(b.s.canvas,b.x,b.y);}\n"
        << "function drawHighBridgeShadow(t){if(!isHighBridgeTile(t))return;const b=overlaySpriteBounds(t);if(!b||!b.s.shadowCanvas)return;ctx.drawImage(b.s.shadowCanvas,b.x,b.y);}\n"
        << "function drawOverlaySprites(highOnly){for(const t of tiles)if(isHighBridgeTile(t)===highOnly)drawOverlaySprite(t);}\n"
        << "function drawOverlayMarkers(highOnly){for(const t of tiles)if(isHighBridgeTile(t)===highOnly)drawOverlayMarker(t);}\n"
        << "function drawObjects(onBridgeOnly){for(const o of objects)if(isObjectVisible(o)&&objectOnBridge(o)===onBridgeOnly)drawObjectMarker(o,false);}\n"
        << "function terrainSpriteBounds(t){const s=sprites[t[10]];if(!s)return null;const p=project(t);return {x:Math.round(p.x-tileW/2+s.x),y:Math.round(p.y-tileH/2+s.y),w:s.w,h:s.h,s};}\n"
        << "function spritePixelHit(b,mx,my){const sx=Math.floor(mx-b.x),sy=Math.floor(my-b.y);return sx>=0&&sy>=0&&sx<b.w&&sy<b.h&&b.s.alpha[sy*b.w+sx]!==0;}\n"
        << "function tileDiamondHit(t,mx,my){const p=project(t),dx=Math.abs(mx-p.x),dy=Math.abs(my-p.y);return (dx/(tileW/2)+dy/(tileH/2))<=1;}\n"
        << "function putTerrainPixel(rgba,zBuffer,x,y,z,index,skipZero,alpha){if((skipZero&&index===0)||x<0||y<0||x>=canvas.width||y>=canvas.height)return;const p=(y*canvas.width+x);if(z<zBuffer[p])return;const c=index*3,o=p*4;if(alpha<1){const inv=1-alpha;rgba[o]=Math.round(rgba[o]*inv+terrainPalette[c]*alpha);rgba[o+1]=Math.round(rgba[o+1]*inv+terrainPalette[c+1]*alpha);rgba[o+2]=Math.round(rgba[o+2]*inv+terrainPalette[c+2]*alpha);rgba[o+3]=255;}else{rgba[o]=terrainPalette[c];rgba[o+1]=terrainPalette[c+1];rgba[o+2]=terrainPalette[c+2];rgba[o+3]=255;}zBuffer[p]=z;}\n"
        << "function drawTerrainSpriteZ(rgba,zBuffer,t,s,zBias,drawZeroBase){if(!s||!s.basePixels.length)return;const p=project(t),left=Math.round(p.x-tileW/2),top=Math.round(p.y-tileH/2),baseZ=(t[0]+t[1])*(tileH/2),baseAlpha=drawZeroBase?.42:1;let rowWidth=0,source=0;const half=Math.floor(s.bh/2);for(let y=0;y<s.bh;++y){if(y<half)rowWidth+=4;else rowWidth-=4;if(rowWidth<=0)continue;const xStart=Math.floor((s.bw-rowWidth)/2);for(let x=0;x<rowWidth;++x){if(source>=s.basePixels.length)return;const index=s.basePixels[source],z=baseZ-(s.hasBaseZ&&source<s.baseZ.length?s.baseZ[source]:0)+zBias;putTerrainPixel(rgba,zBuffer,left+s.bx+xStart+x,top+s.by+y,z,index,!drawZeroBase,baseAlpha);++source;}}if(!s.hasExtra||!s.extraPixels.length)return;const extraLeft=left+s.ex-s.bx,extraTop=top+s.ey-s.by;for(let y=0;y<s.eh;++y){for(let x=0;x<s.ew;++x){const source=y*s.ew+x;if(source>=s.extraPixels.length)return;const index=s.extraPixels[source],z=baseZ-(s.hasExtraZ&&source<s.extraZ.length?s.extraZ[source]:0)+zBias;putTerrainPixel(rgba,zBuffer,extraLeft+x,extraTop+y,z,index,true,1);}}}\n"
        << "const terrainLayer=document.createElement('canvas');terrainLayer.width=canvas.width;terrainLayer.height=canvas.height;const terrainCtx=terrainLayer.getContext('2d');terrainCtx.imageSmoothingEnabled=false;function rebuildTerrainLayer(){const imageData=terrainCtx.createImageData(terrainLayer.width,terrainLayer.height),rgba=imageData.data,zBuffer=new Int32Array(terrainLayer.width*terrainLayer.height);zBuffer.fill(-2147483648);for(let i=0;i<rgba.length;i+=4){rgba[i]=terrainBackgroundRgb[0];rgba[i+1]=terrainBackgroundRgb[1];rgba[i+2]=terrainBackgroundRgb[2];rgba[i+3]=255;}for(const t of tiles){if(t[11]>=0)drawTerrainSpriteZ(rgba,zBuffer,t,sprites[t[11]],-512,false);drawTerrainSpriteZ(rgba,zBuffer,t,sprites[t[10]],0,!!t[13]);}terrainCtx.putImageData(imageData,0,0);}\n"
        << "function draw(){ctx.clearRect(0,0,canvas.width,canvas.height);ctx.fillStyle=terrainBackground;ctx.fillRect(0,0,canvas.width,canvas.height);if(layerState.terrain)ctx.drawImage(terrainLayer,0,0);if(layerState.overlay){drawOverlaySprites(false);drawOverlayMarkers(false);}drawObjects(false);if(layerState.overlay){for(const t of tiles)drawHighBridgeShadow(t);drawOverlaySprites(true);drawOverlayMarkers(true);}drawObjects(true);if(selectedTile)drawSelection(selectedTile);if(selectedObject&&isObjectVisible(selectedObject))drawObjectMarker(selectedObject,true);}\n"
        << "function expandRect(r,pad){return r?{x:r.x-pad,y:r.y-pad,w:r.w+pad*2,h:r.h+pad*2}:null;}\n"
        << "function unionRect(a,b){if(!a)return b;if(!b)return a;const x=Math.min(a.x,b.x),y=Math.min(a.y,b.y),right=Math.max(a.x+a.w,b.x+b.w),bottom=Math.max(a.y+a.h,b.y+b.h);return {x,y,w:right-x,h:bottom-y};}\n"
        << "function tileHitBounds(t,pad){const p=project(t);return {x:Math.round(p.x-tileW/2-pad),y:Math.round(p.y-tileH/2-pad),w:tileW+pad*2,h:tileH+pad*2};}\n"
        << "function bridgeHitBounds(t){if(!isHighBridgeTile(t)&&!isLowBridgeTile(t))return null;const spriteRect=expandRect(overlaySpriteBounds(t),12);if(isLowBridgeTile(t)&&!spriteRect)return null;const tileRect=tileHitBounds(t,isHighBridgeTile(t)?18:12);return unionRect(spriteRect,tileRect);}\n"
        << "function hitTest(mx,my){if(layerState.overlay){for(let i=tiles.length-1;i>=0;--i){const t=tiles[i],ob=overlaySpriteBounds(t),bb=bridgeHitBounds(t);if(ob&&spritePixelHit(ob,mx,my))return t;if(bb&&mx>=bb.x&&mx<=bb.x+bb.w&&my>=bb.y&&my<=bb.y+bb.h)return t;}}for(let i=tiles.length-1;i>=0;--i){const t=tiles[i],b=terrainSpriteBounds(t);if(b&&spritePixelHit(b,mx,my))return t;if(tileDiamondHit(t,mx,my))return t;}return null;}\n"
        << "function pointInDiamond(cx,cy,mx,my){return (Math.abs(mx-cx)/(tileW/2)+Math.abs(my-cy)/(tileH/2))<=1;}\n"
        << "function structureFootprintHit(o,mx,my){if(o[0]!==1)return false;const fw=Math.max(1,o[13]||1),fh=Math.max(1,o[14]||1);for(let dy=0;dy<fh;++dy){for(let dx=0;dx<fw;++dx){const p=projectCoord(o[1]+dx,o[2]+dy,o[3]);if(pointInDiamond(p.x,p.y,mx,my))return true;}}return false;}\n"
        << "function hitTestObject(mx,my){for(let i=objects.length-1;i>=0;--i){const o=objects[i];if(!isObjectVisible(o))continue;if(o[0]===1){if(structureFootprintHit(o,mx,my))return o;continue;}const p=projectObject(o);const dx=mx-p.x,dy=my-(p.y-7);if((dx*dx+dy*dy)<=100)return o;}return null;}\n"
        << "function describe(t){if(!t)return 'hover a tile';let text=`x ${t[0]} y ${t[1]} | tile ${t[2]}:${t[3]} | level ${t[4]} | ice ${t[5]}`;if(layerState.overlay&&t[6]>=0){text+=` | overlay ${t[6]} ${t[9]||'-'} kind ${overlayKindName(t[8])} frame ${t[7]}`;if(t[14])text+=` image ${t[14]}`;if(t[12]>=0)text+=` sprite ${t[12]}`;if(isHighBridgeTile(t))text+=` | high bridge damage/state frame ${t[7]}`;}return text;}\n"
        << "function describeObject(o){if(!o)return 'hover a tile';let text=`${objectKindName(o[0])} ${o[8]||'-'} | x ${o[1]} y ${o[2]} | level ${o[3]}`;if(objectOnBridge(o))text+=` | on bridge`;if(o[7])text+=` | owner ${o[7]}`;if(o[4]>=0)text+=` | health ${o[4]}`;if(o[5]>=0)text+=` | facing ${o[5]}`;if(o[6]>=0)text+=` | subcell ${o[6]}`;if(o[0]===1)text+=` | foundation ${o[13]||1}x${o[14]||1} | hit=footprint diamonds | final=rear-cell SHP global rect`;if(o[10])text+=` | mission ${o[10]}`;if(o[11])text+=` | tag ${o[11]}`;if(o[12]>=0){const s=structureSprites[o[12]];if(s&&s.path){text+=` | shp ${s.path.split(/[\\\\/]/).pop()} frame ${s.frame} ${s.w}x${s.h} offset ${s.x},${s.y} global ${s.gw}x${s.gh} fmt ${s.fmt} data ${s.dataOffset}`;if(s.art)text+=` | art ${s.art}`;if(s.image)text+=` image ${s.image}`;if(s.anchor)text+=` | anchor ${s.anchor}`;if(s.active&&s.active!=='-')text+=` | active ${s.active}`;if(s.candidates)text+=` | candidates ${s.candidates}`;}}return text;}\n"
        << "function objectLabel(o){let text=`${objectKindName(o[0])} ${o[8]||'-'} @ ${o[1]},${o[2]}`;if(objectOnBridge(o))text+=` bridge`;if(o[7])text+=` ${o[7]}`;if(o[9])text+=` #${o[9]}`;return text;}\n"
        << "function objectSearchText(o){return `${objectLabel(o)} ${o[10]||''} ${o[11]||''}`.toLowerCase();}\n"
        << "function refreshObjectList(){const filter=objectFilter.value.trim().toLowerCase();objectSelect.textContent='';let firstValue='';for(let i=0;i<objects.length;++i){const o=objects[i];if(!isObjectVisible(o))continue;if(filter&&!objectSearchText(o).includes(filter))continue;const option=document.createElement('option');option.value=String(i);option.textContent=objectLabel(o);objectSelect.appendChild(option);if(!firstValue)firstValue=option.value;}if(!firstValue){const option=document.createElement('option');option.value='';option.textContent='no objects';objectSelect.appendChild(option);focusObject.disabled=true;return;}focusObject.disabled=false;objectSelect.value=firstValue;}\n"
        << "function centerObject(o){const p=projectObject(o);viewport.scrollTo({left:Math.max(0,p.x-viewport.clientWidth/2),top:Math.max(0,p.y-viewport.clientHeight/2),behavior:'smooth'});}\n"
        << "function scrollToLocalSize(){if(!localSize.valid)return;viewport.scrollLeft=Math.max(0,localSize.left*tileW+margin-24);viewport.scrollTop=Math.max(0,Math.max(localSize.top-3,0)*tileH+tileH/2+margin-24);}\n"
        << "let selectedTile=null,selectedObject=null,drawPending=false,lastMouseX=-1,lastMouseY=-1;\n"
        << "function requestDraw(){if(drawPending)return;drawPending=true;requestAnimationFrame(()=>{drawPending=false;draw();});}\n"
        << "function setSelection(object,tile){if(selectedObject===object&&selectedTile===tile)return false;selectedObject=object;selectedTile=tile;return true;}\n"
        << "function updateStatus(){status.textContent=selectedObject?describeObject(selectedObject):describe(selectedTile);}\n"
        << "function selectObjectByIndex(index,shouldCenter){const o=objects[index];if(!o||!isObjectVisible(o))return;if(setSelection(o,null))requestDraw();if(shouldCenter)centerObject(o);status.textContent=describeObject(o);}\n"
        << "canvas.addEventListener('mousemove',event=>{const r=canvas.getBoundingClientRect();const mx=event.clientX-r.left,my=event.clientY-r.top;if(Math.abs(mx-lastMouseX)<2&&Math.abs(my-lastMouseY)<2)return;lastMouseX=mx;lastMouseY=my;const object=hitTestObject(mx,my),tile=object?null:hitTest(mx,my);if(setSelection(object,tile)){requestDraw();updateStatus();}});\n"
        << "canvas.addEventListener('mouseleave',()=>{lastMouseX=-1;lastMouseY=-1;if(setSelection(null,null))requestDraw();status.textContent='hover a tile';});\n"
        << "objectFilter.addEventListener('input',()=>{refreshObjectList();setSelection(null,null);requestDraw();status.textContent='hover a tile';});\n"
        << "objectSelect.addEventListener('change',()=>{if(objectSelect.value!=='')selectObjectByIndex(Number(objectSelect.value),false);});\n"
        << "focusObject.addEventListener('click',()=>{if(objectSelect.value!=='')selectObjectByIndex(Number(objectSelect.value),true);});\n"
        << "document.querySelectorAll('[data-layer]').forEach(control=>{control.addEventListener('change',()=>{layerState[control.dataset.layer]=control.checked;setSelection(null,null);refreshObjectList();requestDraw();status.textContent='hover a tile';});});\n"
        << "rebuildTerrainLayer();refreshObjectList();draw();scrollToLocalSize();\n"
        << "</script>\n"
        << "</body>\n"
        << "</html>\n";

    if (!output.good()) {
        errorMessage = "failed while writing output file: " + outputPath;
        return false;
    }

    return true;
}

bool exportMapTmpPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    const MapStructureSpriteOptions& structureSpriteOptions,
    MapStructureSpriteStats& structureSpriteStats,
    std::string& errorMessage
) {
    concord::assets::MapPreviewResourceContext context;
    context.packageRoot = std::filesystem::current_path().string();
    context.tileRootPath = tileRootPath;
    context.assetRootPath = structureSpriteOptions.assetRootPath;
    context.palettePath = palettePath;
    context.structurePalettePath = structureSpriteOptions.structurePalettePath;
    concord::assets::MapPreviewResourceSession session(std::move(context));
    return exportMapTmpPreviewToHtml(session, outputPath, mapDatabase, theaterDatabase,
        tileRootPath, palettePath, structureSpriteOptions, structureSpriteStats, errorMessage);
}

bool exportMapTmpPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    const TheaterDatabase& theaterDatabase,
    const std::string& tileRootPath,
    const std::string& palettePath,
    std::string& errorMessage
) {
    MapStructureSpriteOptions structureSpriteOptions;
    MapStructureSpriteStats structureSpriteStats;
    return exportMapTmpPreviewToHtml(
        outputPath,
        mapDatabase,
        theaterDatabase,
        tileRootPath,
        palettePath,
        structureSpriteOptions,
        structureSpriteStats,
        errorMessage
    );
}

bool exportMapPreviewToHtml(
    const std::string& outputPath,
    const MapDatabase& mapDatabase,
    std::string& errorMessage
) {
    errorMessage.clear();

    const std::vector<MapIsoTile>& tiles = mapDatabase.getIsoTiles();
    if (tiles.empty()) {
        errorMessage = "map has no decoded IsoMapPack5 tiles";
        return false;
    }

    const PreviewBounds bounds = calculatePreviewBounds(tiles);
    const std::unordered_map<int, const MapOverlayCell*> overlayByCell =
        buildOverlayCellLookup(mapDatabase.getOverlayCells());

    std::vector<HtmlTileRecord> records;
    records.reserve(tiles.size());

    for (const MapIsoTile& tile : tiles) {
        const int overlayCell = overlayCellFromTile(tile);
        const auto overlayIt = overlayByCell.find(overlayCell);

        HtmlTileRecord record;
        record.tile = &tile;
        record.overlay = overlayIt == overlayByCell.end() ? nullptr : overlayIt->second;
        records.push_back(record);
    }

    std::sort(
        records.begin(),
        records.end(),
        [](const HtmlTileRecord& left, const HtmlTileRecord& right) {
            const int leftDepth = left.tile->x + left.tile->y + left.tile->level;
            const int rightDepth = right.tile->x + right.tile->y + right.tile->level;

            if (leftDepth != rightDepth) {
                return leftDepth < rightDepth;
            }

            if (left.tile->y != right.tile->y) {
                return left.tile->y < right.tile->y;
            }

            return left.tile->x < right.tile->x;
        }
    );

    constexpr int htmlTileWidth = 18;
    constexpr int htmlTileHeight = 9;
    constexpr int htmlMargin = 48;
    const int canvasWidth = std::clamp(
        (bounds.maxX - bounds.minX) * htmlTileWidth / 2
            + (htmlMargin * 2)
            + htmlTileWidth,
        1,
        8192
    );
    const int canvasHeight = std::clamp(
        (bounds.maxY - bounds.minY) * htmlTileHeight / 2
            + (htmlMargin * 2)
            + htmlTileHeight,
        1,
        8192
    );

    const MapSummary& summary = mapDatabase.getSummary();
    const std::string title = summary.name.empty() ? "ConcordRTS map preview" : summary.name;

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "failed to open output file: " + outputPath;
        return false;
    }

    output
        << "<!doctype html>\n"
        << "<html lang=\"en\">\n"
        << "<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << escapeHtmlText(title) << "</title>\n"
        << "<style>\n"
        << ":root{color-scheme:dark;font-family:Segoe UI,Arial,sans-serif;}\n"
        << "body{margin:0;background:#11151a;color:#e6edf3;overflow:hidden;}\n"
        << ".bar{position:fixed;inset:0 0 auto 0;height:48px;display:flex;align-items:center;gap:16px;padding:0 14px;background:#171b20;border-bottom:1px solid #2b333d;z-index:2;box-sizing:border-box;}\n"
        << ".title{font-weight:650;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}\n"
        << ".meta{color:#9ba7b4;font-size:12px;white-space:nowrap;}\n"
        << ".status{margin-left:auto;color:#dce7f2;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:58vw;}\n"
        << ".viewport{position:absolute;inset:48px 0 0 0;overflow:auto;background:#0f1318;}\n"
        << "canvas{display:block;background:#10151b;image-rendering:pixelated;}\n"
        << "</style>\n"
        << "</head>\n"
        << "<body>\n"
        << "<div class=\"bar\"><div class=\"title\">" << escapeHtmlText(title) << "</div>"
        << "<div class=\"meta\">theater "
        << escapeHtmlText(summary.theater.empty() ? "-" : summary.theater)
        << "</div><div class=\"meta\">tiles " << records.size()
        << "</div><div id=\"status\" class=\"status\">hover a tile</div></div>\n"
        << "<div class=\"viewport\"><canvas id=\"map\" width=\"" << canvasWidth
        << "\" height=\"" << canvasHeight << "\"></canvas></div>\n"
        << "<script>\n"
        << "const tileW=" << htmlTileWidth << ",tileH=" << htmlTileHeight
        << ",margin=" << htmlMargin << ";\n"
        << "const minX=" << bounds.minX << ",minY=" << bounds.minY << ";\n"
        << "const tiles=[\n";

    for (std::size_t i = 0; i < records.size(); ++i) {
        const MapIsoTile& tile = *records[i].tile;
        const MapOverlayCell* overlay = records[i].overlay;

        output << "["
               << tile.x << ","
               << tile.y << ","
               << tile.tileIndex << ","
               << tile.subTileIndex << ","
               << tile.level << ","
               << tile.iceGrowth << ","
               << (overlay == nullptr ? -1 : overlay->overlayIndex) << ","
               << (overlay == nullptr ? 0 : overlay->frameIndex) << ","
               << overlayKindValue(overlay) << ",\""
               << escapeJavaScriptString(overlay == nullptr ? "" : overlay->type)
               << "\"]";

        if (i + 1 < records.size()) {
            output << ",";
        }

        output << "\n";
    }

    output
        << "];\n"
        << "const canvas=document.getElementById('map');\n"
        << "const ctx=canvas.getContext('2d');\n"
        << "const status=document.getElementById('status');\n"
        << "function project(t){const px=t[0]-t[1],py=t[0]+t[1]-t[4]*2;return {x:(px-minX)*tileW/2+margin,y:(py-minY)*tileH/2+margin};}\n"
        << "function baseColor(t){const seed=t[2]*37+t[3]*17;let r=70+seed%80,g=80+Math.floor(seed/3)%90,b=55+Math.floor(seed/7)%70;const boost=(t[4]-4)*8;r=Math.max(0,Math.min(255,r+boost));g=Math.max(0,Math.min(255,g+boost));b=Math.max(0,Math.min(255,b+boost));return [r,g,b];}\n"
        << "function blend(a,b){return [Math.floor((a[0]+b[0])/2),Math.floor((a[1]+b[1])/2),Math.floor((a[2]+b[2])/2)];}\n"
        << "function color(t){let c=baseColor(t);if(t[8]===2)c=blend(c,[210,64,220]);else if(t[8]===1)c=blend(c,[220,190,32]);else if(t[8]===3)c=blend(c,[120,130,150]);return `rgb(${c[0]},${c[1]},${c[2]})`;}\n"
        << "function drawTile(t,stroke){const p=project(t);ctx.beginPath();ctx.moveTo(p.x,p.y-tileH/2);ctx.lineTo(p.x+tileW/2,p.y);ctx.lineTo(p.x,p.y+tileH/2);ctx.lineTo(p.x-tileW/2,p.y);ctx.closePath();ctx.fillStyle=color(t);ctx.fill();ctx.strokeStyle=stroke||'rgba(12,17,22,.45)';ctx.stroke();}\n"
        << "function draw(){ctx.clearRect(0,0,canvas.width,canvas.height);for(const t of tiles)drawTile(t);}\n"
        << "function hitTest(mx,my){for(let i=tiles.length-1;i>=0;--i){const t=tiles[i],p=project(t);const dx=Math.abs(mx-p.x)/(tileW/2),dy=Math.abs(my-p.y)/(tileH/2);if(dx+dy<=1)return t;}return null;}\n"
        << "function describe(t){if(!t)return 'hover a tile';let text=`x ${t[0]} y ${t[1]} | tile ${t[2]}:${t[3]} | level ${t[4]} | ice ${t[5]}`;if(t[6]>=0)text+=` | overlay ${t[6]} ${t[9]||'-'} frame ${t[7]}`;return text;}\n"
        << "let selected=null,drawPending=false,lastMouseX=-1,lastMouseY=-1;\n"
        << "function drawSelected(){draw();if(selected)drawTile(selected,'#f0f6fc');}\n"
        << "function requestDraw(){if(drawPending)return;drawPending=true;requestAnimationFrame(()=>{drawPending=false;drawSelected();});}\n"
        << "canvas.addEventListener('mousemove',event=>{const r=canvas.getBoundingClientRect();const mx=event.clientX-r.left,my=event.clientY-r.top;if(Math.abs(mx-lastMouseX)<2&&Math.abs(my-lastMouseY)<2)return;lastMouseX=mx;lastMouseY=my;const next=hitTest(mx,my);if(next!==selected){selected=next;requestDraw();status.textContent=describe(selected);}});\n"
        << "canvas.addEventListener('mouseleave',()=>{lastMouseX=-1;lastMouseY=-1;if(selected){selected=null;requestDraw();}status.textContent='hover a tile';});\n"
        << "draw();\n"
        << "</script>\n"
        << "</body>\n"
        << "</html>\n";

    if (!output.good()) {
        errorMessage = "failed while writing output file: " + outputPath;
        return false;
    }

    return true;
}
