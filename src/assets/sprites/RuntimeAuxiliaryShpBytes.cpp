#include "assets/sprites/RuntimeAuxiliaryShpBytes.h"
#include "data/art/ArtDatabase.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <utility>

namespace concord::runtime::resource_detail {
unsigned int readUInt16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

int runtimeShpFrameCount(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 8) {
        return 0;
    }

    return static_cast<int>(readUInt16(bytes, 6));
}

int runtimeVisibleMainFrameCount(int frameCount, bool splitShadow) {
    if (splitShadow && frameCount >= 2 && frameCount % 2 == 0) {
        return std::max(1, frameCount / 2);
    }

    return std::max(1, frameCount);
}

std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

bool runtimeFileNameHasExtension(
    const std::string& fileName,
    const std::string& extension) {
    if (fileName.empty() || extension.empty()) {
        return false;
    }

    return toLowerAscii(std::filesystem::path(fileName).extension().string())
        == toLowerAscii(extension);
}

std::string fileNameForShpImageId(const std::string& imageId) {
    if (imageId.empty()) {
        return "";
    }

    const std::string lower = toLowerAscii(imageId);
    if (!std::filesystem::path(lower).extension().empty()) {
        return imageId;
    }

    return imageId + ".shp";
}

bool runtimeAuxiliaryResourceUsesShpBytes(
    const std::string& fileName,
    const ArtType* artType) {
    if (runtimeFileNameHasExtension(fileName, ".shp")
        || runtimeFileNameHasExtension(fileName, ".sha")) {
        return true;
    }
    if (artType == nullptr || !artType->theater) {
        return false;
    }

    static constexpr std::array<const char*, 6> theaterExtensions{
        ".tem", ".sno", ".urb", ".des", ".lun", ".ubn"
    };
    return std::any_of(
        theaterExtensions.begin(),
        theaterExtensions.end(),
        [&fileName](const char* extension) {
            return runtimeFileNameHasExtension(fileName, extension);
        }
    );
}

bool isRuntimeSidebarResourceFileName(const std::string& fileName) {
    const std::string lowerFile = toLowerAscii(fileName);
    return lowerFile == "credits.shp"
        || lowerFile == "top.shp"
        || lowerFile == "side1.shp"
        || lowerFile == "side2.shp"
        || lowerFile == "side2b.shp"
        || lowerFile == "side3.shp"
        || lowerFile == "addon.shp"
        || lowerFile == "tabs.shp"
        || lowerFile == "tab00.shp"
        || lowerFile == "tab01.shp"
        || lowerFile == "tab02.shp"
        || lowerFile == "tab03.shp"
        || lowerFile == "diplobtn.shp"
        || lowerFile == "optbtn.shp"
        || lowerFile == "repair.shp"
        || lowerFile == "sell.shp"
        || lowerFile == "r-dn.shp"
        || lowerFile == "r-up.shp"
        || lowerFile == "gclock2.shp"
        || lowerFile == "power.shp"
        || lowerFile == "powerp.shp"
        || lowerFile == "radar.shp"
        || lowerFile == "pips.shp"
        || lowerFile == "sidebar.pal";
}

double runtimeElapsedMs(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::chrono::steady_clock::time_point runtimeMaybeTimingStart(bool enabled) {
    return enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
}

double runtimeMaybeElapsedMs(
    bool enabled,
    const std::chrono::steady_clock::time_point& start) {
    return enabled
        ? runtimeElapsedMs(start, std::chrono::steady_clock::now())
        : 0.0;
}
}

RuntimeAuxiliaryShpByteLookup loadRuntimeAuxiliaryShpBytes(
    const std::string& imageId, const RuntimeAuxiliaryShpByteContext& context,
    RuntimeAuxiliaryShpByteCache& cache, RuntimeAuxiliaryShpByteSource& source,
    const ArtType* fallbackArtType) {
    using namespace concord::runtime::resource_detail;

    if (imageId.empty()) {
        return {};
    }

    std::vector<std::string> imageCandidates =
        source.imageCandidates(imageId, fallbackArtType);
    if (imageCandidates.empty()) {
        imageCandidates.push_back(imageId);
    }
    const ArtType* auxiliaryArtType = source.findArtType(imageId);
    if (auxiliaryArtType == nullptr) {
        auxiliaryArtType = fallbackArtType;
    }
    std::string candidateKey;
    for (const std::string& candidate : imageCandidates) {
        candidateKey += "|";
        candidateKey += toLowerAscii(candidate);
    }
    const std::string& mixExportsRoot =
        source.cachedMixExportsRoot();

    const std::string bytesCacheKey =
        toLowerAscii(imageId)
        + candidateKey
        + "|bytes|"
        + context.assetRootPath
        + "|"
        + context.structureRootPath
        + "|mix="
        + mixExportsRoot
        + "|theater="
        + (auxiliaryArtType != nullptr && auxiliaryArtType->theater
            ? "1"
            : "0");
    auto [bytesIt, bytesInserted] =
        cache.auxiliaryShpBytesByImage.try_emplace(bytesCacheKey);
    RuntimeAuxiliaryShpBytesCacheEntry& bytesEntry = bytesIt->second;
    if (bytesInserted) {
        ++cache.auxiliaryShpBytesCacheMisses;
    } else {
        ++cache.auxiliaryShpBytesCacheHits;
    }
    if (!bytesEntry.loadAttempted) {
        bytesEntry.loadAttempted = true;
        const bool detailTiming =
            source.detailTimingEnabled();
        const auto bytesMissStart =
            runtimeMaybeTimingStart(detailTiming);

        std::string sourceName;
        std::string errorMessage;

        for (const std::string& candidate : imageCandidates) {
            const std::string fileName = fileNameForShpImageId(candidate);
            if (!runtimeAuxiliaryResourceUsesShpBytes(
                    fileName,
                    auxiliaryArtType
                )) {
                continue;
            }
            const bool sidebarResource =
                isRuntimeSidebarResourceFileName(fileName);
            bool loaded = false;
            loaded = source.readMix(
                context.assetRootPath,
                fileName,
                bytesEntry.bytes,
                sourceName,
                errorMessage
            );
            if (!loaded
                && !mixExportsRoot.empty()
                && !source.sameNormalizedRoot(mixExportsRoot, context.assetRootPath)) {
                loaded = source.readMix(
                    mixExportsRoot,
                    fileName,
                    bytesEntry.bytes,
                    sourceName,
                    errorMessage
                );
            }
            if (!sidebarResource) {
                if (!loaded) {
                    loaded = source.readByName(
                        {context.structureRootPath},
                        context.assetRootPath,
                        fileName,
                        bytesEntry.bytes,
                        sourceName,
                        errorMessage
                    );
                }
            }
            if (loaded) {
                bytesEntry.resolvedImageId = candidate;
                break;
            }
        }

        if (bytesEntry.bytes.empty()) {
            bytesEntry.loaded = false;
            bytesEntry.errorMessage =
                "auxiliary SHP was not found: "
                + imageId
                + " candidates="
                + candidateKey
                + " "
                + errorMessage;
        } else {
            bytesEntry.loaded = true;
            bytesEntry.sourceFingerprint = 14695981039346656037ULL;
            for (unsigned char value : bytesEntry.bytes)
                bytesEntry.sourceFingerprint = (bytesEntry.sourceFingerprint ^ value) * 1099511628211ULL;
            bytesEntry.sourcePath = sourceName;
            bytesEntry.frameCount = runtimeShpFrameCount(bytesEntry.bytes);
            bytesEntry.errorMessage = errorMessage;
        }
        cache.auxiliaryShpBytesCacheMissMs +=
            runtimeMaybeElapsedMs(detailTiming, bytesMissStart);
    }
    if (!bytesEntry.loaded || bytesEntry.frameCount <= 0) {
        return {nullptr, std::move(candidateKey)};
    }

    return {&bytesEntry, std::move(candidateKey)};
}
