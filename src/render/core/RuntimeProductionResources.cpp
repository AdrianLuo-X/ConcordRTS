#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include "render/core/RuntimeMapRenderer.h"
#include "assets/codecs/ShpCodec.h"
#include "data/maps/MapDatabase.h"
#include "render/core/RuntimeRendererEnvironment.h"

#include "render/core/detail/RuntimeProductionResources.h"

RuntimeMapRenderContext::RuntimeMapRenderContext() = default;
RuntimeMapRenderContext::~RuntimeMapRenderContext() = default;

namespace concord::runtime::resource_detail {

int positiveModulo(int value, int modulus) {
    if (modulus <= 0) {
        return 0;
    }

    int result = value % modulus;
    if (result < 0) {
        result += modulus;
    }
    return result;
}

bool loadBytes(
    const std::string& path,
    std::vector<unsigned char>& bytes,
    std::string& errorMessage) {
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
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }

    if (!input && !bytes.empty()) {
        errorMessage = "failed to read file: " + path;
        return false;
    }

    return true;
}

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

int runtimeShpShadowFrameIndex(
    int frameCount,
    int visibleFrameIndex,
    bool splitShadow) {
    if (!splitShadow || frameCount < 2 || (frameCount % 2) != 0) {
        return -1;
    }
    const int visibleFrameCount = frameCount / 2;
    if (visibleFrameIndex < 0 || visibleFrameIndex >= visibleFrameCount) {
        return -1;
    }
    return visibleFrameIndex + visibleFrameCount;
}

bool decodeRuntimeShpFrameFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& errorMessage) {
    // Preserve the original invalidation even when byte decoding fails.
    image.shadowMaskSpanCache.invalidate();
    ShpIndexedFrame decoded;
    if (!decodeShpIndexedFrame(bytes, sourceName, frameIndex, ShpDecodePolicy::Runtime, decoded, errorMessage)) {
        return false;
    }
    if (decoded.authoredPause) {
        image = RuntimeShpFrameImage{};
        image.globalWidth = decoded.globalWidth;
        image.globalHeight = decoded.globalHeight;
        image.sourcePath = sourceName;
        return true;
    }
    image.offsetX = decoded.offsetX;
    image.offsetY = decoded.offsetY;
    image.globalWidth = decoded.globalWidth;
    image.globalHeight = decoded.globalHeight;
    image.width = decoded.width;
    image.height = decoded.height;
    image.pixels = std::move(decoded.pixels);
    image.sourcePath = sourceName;
    return true;
}

std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

std::string trimAsciiForRender(const std::string& value) {
    const auto isNotSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    const auto begin = std::find_if(
        value.begin(),
        value.end(),
        isNotSpace
    );
    if (begin == value.end()) {
        return {};
    }
    const auto end = std::find_if(
        value.rbegin(),
        value.rend(),
        isNotSpace
    ).base();
    return std::string(begin, end);
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

bool runtimeFileNameHasExtension(
    const std::string& fileName,
    const std::string& extension) {
    if (fileName.empty() || extension.empty()) {
        return false;
    }

    return toLowerAscii(std::filesystem::path(fileName).extension().string())
        == toLowerAscii(extension);
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

bool findFileByNameCaseInsensitive(
    const std::string& rootPath,
    const std::string& fileName,
    std::string& resolvedPath) {
    if (rootPath.empty() || fileName.empty()) {
        return false;
    }

    const std::filesystem::path root(rootPath);
    std::error_code errorCode;
    if (!std::filesystem::exists(root, errorCode)) {
        return false;
    }

    const std::string targetFileName = toLowerAscii(fileName);
    if (std::filesystem::is_regular_file(root, errorCode)) {
        if (toLowerAscii(root.filename().string()) == targetFileName) {
            resolvedPath = root.generic_string();
            return true;
        }
        return false;
    }

    if (!std::filesystem::is_directory(root, errorCode)) {
        return false;
    }

    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!errorCode && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(errorCode)
            && toLowerAscii(entry.path().filename().string()) == targetFileName) {
            resolvedPath = entry.path().generic_string();
            return true;
        }
        iterator.increment(errorCode);
    }

    return false;
}

std::string normalizedLowerPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return toLowerAscii(std::move(path));
}

std::string runtimeMixExportsRootFromPath(const std::string& pathString) {
    if (pathString.empty()) {
        return "";
    }

    std::filesystem::path path(pathString);
    std::error_code errorCode;
    if (std::filesystem::is_regular_file(path, errorCode)) {
        path = path.parent_path();
    }

    for (std::filesystem::path current = path;
        !current.empty();
        current = current.parent_path()) {
        if (toLowerAscii(current.filename().string()) == "mix_exports") {
            return current.generic_string();
        }
        if (current == current.parent_path()) {
            break;
        }
    }
    return "";
}

std::string runtimeMixExportsRootForContext(
    const RuntimeMapRenderContext& context) {
    std::string root = runtimeMixExportsRootFromPath(context.tileRootPath);
    if (!root.empty()) {
        return root;
    }
    root = runtimeMixExportsRootFromPath(context.structureRootPath);
    if (!root.empty()) {
        return root;
    }
    return runtimeMixExportsRootFromPath(context.assetRootPath);
}

const std::string& cachedRuntimeMixExportsRootForContext(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    if (!cache.mixExportsRootResolved
        || cache.mixExportsRootAssetPath != context.assetRootPath
        || cache.mixExportsRootStructurePath != context.structureRootPath
        || cache.mixExportsRootTilePath != context.tileRootPath) {
        cache.resolvedMixExportsRoot = runtimeMixExportsRootForContext(context);
        cache.mixExportsRootAssetPath = context.assetRootPath;
        cache.mixExportsRootStructurePath = context.structureRootPath;
        cache.mixExportsRootTilePath = context.tileRootPath;
        cache.mixExportsRootResolved = true;
    }
    return cache.resolvedMixExportsRoot;
}

int runtimeMixSearchPriority(const std::string& path) {
    const std::string lower = normalizedLowerPath(path);
    const std::string fileName =
        toLowerAscii(std::filesystem::path(lower).filename().string());
    if (fileName == "expandmd02.mix") {
        return -30;
    }
    if (fileName == "expandmd01.mix") {
        return -20;
    }
    if (fileName == "ra2md.mix") {
        return -10;
    }
    if (lower.find("/ra2md/localmd.mix") != std::string::npos) {
        return 0;
    }
    if (lower.find("/ra2md/conqmd.mix") != std::string::npos) {
        return 10;
    }
    if (lower.find("/ra2md/") != std::string::npos) {
        return 20;
    }
    if (lower.find("/ra2/local.mix") != std::string::npos) {
        return 30;
    }
    if (lower.find("/ra2/conquer.mix") != std::string::npos) {
        return 40;
    }
    if (fileName == "expand01.mix") {
        return 45;
    }
    if (fileName == "ra2.mix") {
        return 46;
    }
    if (lower.find("/ra2/") != std::string::npos) {
        return 50;
    }
    return 100;
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

int runtimeMixSearchPriorityForFile(
    const std::string& path,
    const std::string& fileName) {
    const std::string lowerPath = normalizedLowerPath(path);
    const std::string lowerFile = toLowerAscii(fileName);
    const std::string mixName =
        toLowerAscii(std::filesystem::path(lowerPath).filename().string());

    const bool loadingScreenResource =
        (lowerFile.rfind("ls800", 0) == 0
            && std::filesystem::path(lowerFile).extension().string() == ".shp")
        || lowerFile == "mplsu.pal"
        || lowerFile == "mplsuk.pal"
        || lowerFile == "mplsg.pal"
        || lowerFile == "mplsf.pal"
        || lowerFile == "mplsk.pal"
        || lowerFile == "mplsr.pal"
        || lowerFile == "mplsc.pal"
        || lowerFile == "mplsl.pal"
        || lowerFile == "mplsi.pal"
        || lowerFile == "mpyls.pal"
        || lowerFile == "mplsobs.pal";
    if (loadingScreenResource) {
        if (mixName == "loadmd.mix") {
            return -70;
        }
        if (mixName == "load.mix") {
            return -65;
        }
    }

    const bool sidebarResource =
        isRuntimeSidebarResourceFileName(lowerFile);
    if (sidebarResource) {
        if (mixName == "sidec02md.mix") {
            return -60;
        }
        if (mixName == "sidec01.mix" || mixName == "sidec02.mix") {
            return -55;
        }
        if (mixName == "conqmd.mix" || mixName == "conquer.mix") {
            return -20;
        }
    }

    const std::string extension =
        toLowerAscii(std::filesystem::path(lowerFile).extension().string());
    if (extension == ".shp" || extension == ".sha"
        || extension == ".vxl" || extension == ".hva") {
        if (mixName == "conqmd.mix") {
            return -50;
        }
        if (mixName == "conquer.mix") {
            return -45;
        }
        if (mixName == "cachemd.mix" || mixName == "cache.mix") {
            return -40;
        }
        if (mixName.rfind("iso", 0) == 0) {
            return -30;
        }
    }

    return runtimeMixSearchPriority(path);
}

bool isRuntimeObjectResourceMixCandidate(const std::string& path) {
    const std::string fileName =
        toLowerAscii(std::filesystem::path(path).filename().string());
    if (fileName.rfind("iso", 0) == 0
        && std::filesystem::path(fileName).extension().string() == ".mix") {
        return true;
    }
    static constexpr const char* ObjectMixNames[] = {
        "cachemd.mix",
        "conqmd.mix",
        "des.mix",
        "desert.mix",
        "expandmd02.mix",
        "expandmd01.mix",
        "genermd.mix",
        "localmd.mix",
        "loadmd.mix",
        "lun.mix",
        "lunar.mix",
        "ntrlmd.mix",
        "ra2md.mix",
        "snowmd.mix",
        "ubn.mix",
        "urbann.mix",
        "cameo.mix",
        "cameomd.mix",
        "sidec02md.mix",
        "cache.mix",
        "conquer.mix",
        "expand01.mix",
        "generic.mix",
        "local.mix",
        "load.mix",
        "neutral.mix",
        "ra2.mix",
        "sidec01.mix",
        "sidec02.mix",
        "sidenc01.mix",
        "sidenc02.mix",
        "sno.mix",
        "snow.mix",
        "tem.mix",
        "temperat.mix",
        "urb.mix",
        "urban.mix"
    };

    for (const char* candidate : ObjectMixNames) {
        if (fileName == candidate) {
            return true;
        }
    }
    return false;
}

int runtimeLooseResourceSearchPriority(const std::string& path) {
    const std::string lower = normalizedLowerPath(path);
    if (lower.find("/extracted_buildings/") != std::string::npos) {
        return 0;
    }
    if (lower.find("/ra2md/localmd/") != std::string::npos) {
        return 10;
    }
    if (lower.find("/ra2md/") != std::string::npos) {
        return 20;
    }
    if (lower.find("/ra2/") != std::string::npos) {
        return 30;
    }
    return 100;
}

bool isRuntimeObjectLooseExtension(const std::filesystem::path& path) {
    const std::string extension = toLowerAscii(path.extension().string());
    return extension == ".shp"
        || extension == ".vxl"
        || extension == ".hva"
        || extension == ".tem"
        || extension == ".urb"
        || extension == ".ubn"
        || extension == ".des"
        || extension == ".sno"
        || extension == ".lun";
}

void appendRuntimeLooseResourcePathsFromRoot(
    const std::filesystem::path& root,
    std::vector<std::string>& paths) {
    std::error_code errorCode;
    if (std::filesystem::is_regular_file(root, errorCode)) {
        if (isRuntimeObjectLooseExtension(root)) {
            paths.push_back(root.generic_string());
        }
        return;
    }

    if (!std::filesystem::is_directory(root, errorCode)) {
        return;
    }

    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!errorCode && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(errorCode)
            && isRuntimeObjectLooseExtension(entry.path())) {
            paths.push_back(entry.path().generic_string());
        }
        iterator.increment(errorCode);
    }
}

std::vector<std::filesystem::path> runtimeLooseObjectSearchRoots(
    const std::filesystem::path& assetRoot) {
    return {
        assetRoot,
        assetRoot / "ra2md" / "localmd",
        assetRoot / "expandmd02",
        assetRoot / "expandmd01",
        assetRoot / "cameomd",
        assetRoot / "langmd",
        assetRoot / "language",
        assetRoot / "extracted_overlays"
    };
}

void ensureRuntimeLooseResourceIndex(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) {
    if (cache.looseResourceIndexRootPath != assetRootPath) {
        cache.looseResourceIndexRootPath = assetRootPath;
        cache.looseResourceIndexBuilt = false;
        cache.looseResourcePathsByName.clear();
    }

    if (cache.looseResourceIndexBuilt || assetRootPath.empty()) {
        return;
    }

    cache.looseResourceIndexBuilt = true;
    const std::filesystem::path root(assetRootPath);
    std::error_code errorCode;
    if (!std::filesystem::is_directory(root, errorCode)) {
        return;
    }

    std::vector<std::string> paths;
    for (const std::filesystem::path& searchRoot :
        runtimeLooseObjectSearchRoots(root)) {
        appendRuntimeLooseResourcePathsFromRoot(searchRoot, paths);
    }

    // A custom asset root may not use the extracted RA2/YR folder layout.
    if (paths.empty()) {
        appendRuntimeLooseResourcePathsFromRoot(root, paths);
    }

    std::sort(
        paths.begin(),
        paths.end(),
        [](const std::string& left, const std::string& right) {
            const int leftPriority = runtimeLooseResourceSearchPriority(left);
            const int rightPriority = runtimeLooseResourceSearchPriority(right);
            if (leftPriority != rightPriority) {
                return leftPriority < rightPriority;
            }
            return normalizedLowerPath(left) < normalizedLowerPath(right);
        }
    );

    for (const std::string& path : paths) {
        const std::string key =
            toLowerAscii(std::filesystem::path(path).filename().string());
        cache.looseResourcePathsByName.try_emplace(key, path);
    }
}

bool findRuntimeLooseResourceByName(
    const std::string& rootPath,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::string& resolvedPath) {
    if (rootPath.empty() || fileName.empty()) {
        return false;
    }

    if (normalizedLowerPath(rootPath) == normalizedLowerPath(assetRootPath)) {
        ensureRuntimeLooseResourceIndex(assetRootPath, cache);
        const auto it =
            cache.looseResourcePathsByName.find(toLowerAscii(fileName));
        if (it == cache.looseResourcePathsByName.end()) {
            return false;
        }

        resolvedPath = it->second;
        return true;
    }

    return findFileByNameCaseInsensitive(rootPath, fileName, resolvedPath);
}

void ensureRuntimeMixArchivePaths(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) {
    if (cache.mixArchiveRootPath != assetRootPath) {
        cache.mixArchiveRootPath = assetRootPath;
        cache.mixArchivePathsScanned = false;
        cache.mixArchivePaths.clear();
        cache.mixArchivesByPath.clear();
    }

    if (cache.mixArchivePathsScanned || assetRootPath.empty()) {
        return;
    }

    cache.mixArchivePathsScanned = true;
    const std::filesystem::path root(assetRootPath);
    std::error_code errorCode;
    if (!std::filesystem::is_directory(root, errorCode)) {
        return;
    }

    const auto addKnownMix = [&cache](const std::filesystem::path& path) {
        std::error_code existsError;
        if (!std::filesystem::is_regular_file(path, existsError)) {
            return;
        }
        const std::string pathString = path.generic_string();
        if (!isRuntimeObjectResourceMixCandidate(pathString)) {
            return;
        }
        if (std::find(
                cache.mixArchivePaths.begin(),
                cache.mixArchivePaths.end(),
                pathString
            ) == cache.mixArchivePaths.end()) {
            cache.mixArchivePaths.push_back(pathString);
        }
    };

    const std::filesystem::path ra2 = root / "ra2";
    const std::filesystem::path ra2md = root / "ra2md";
    const char* knownRa2mdMixes[] = {
        "conqmd.mix",
        "cachemd.mix",
        "localmd.mix",
        "loadmd.mix",
        "sidec02md.mix",
        "cameomd.mix",
        // Retail Yuri's Revenge calls this genermd.mix. Omitting it silently
        // falls back to RA2's generic.mix (including its unused fortress wall).
        "genermd.mix",
        "des.mix",
        "desert.mix",
        "lun.mix",
        "lunar.mix",
        "snowmd.mix",
        "ubn.mix",
        "urbann.mix",
        "isodes.mix",
        "isodesmd.mix",
        "isogenmd.mix",
        "isolun.mix",
        "isolunmd.mix",
        "isosnomd.mix",
        "isotemmd.mix",
        "isoubn.mix",
        "isoubnmd.mix",
        "isourbmd.mix"
    };
    const char* knownRa2Mixes[] = {
        "conquer.mix",
        "cache.mix",
        "local.mix",
        "load.mix",
        "sidec01.mix",
        "sidec02.mix",
        "sidenc01.mix",
        "sidenc02.mix",
        "cameo.mix",
        "generic.mix",
        "neutral.mix",
        "sno.mix",
        "snow.mix",
        "tem.mix",
        "temperat.mix",
        "urb.mix",
        "urban.mix",
        "isogen.mix",
        "isosnow.mix",
        "isotemp.mix",
        "isourb.mix"
    };
    for (const char* mixName : knownRa2mdMixes) {
        addKnownMix(ra2md / mixName);
    }
    for (const char* mixName : knownRa2Mixes) {
        addKnownMix(ra2 / mixName);
    }
    addKnownMix(root / "langmd" / "cameomd.mix");
    addKnownMix(root / "language" / "cameo.mix");
    addKnownMix(root / "expandmd02.mix");
    addKnownMix(root / "expandmd01.mix");
    addKnownMix(root / "ra2md.mix");
    addKnownMix(root / "expand01.mix");
    addKnownMix(root / "ra2.mix");

    if (!cache.mixArchivePaths.empty()) {
        std::sort(
            cache.mixArchivePaths.begin(),
            cache.mixArchivePaths.end(),
            [](const std::string& left, const std::string& right) {
                const int leftPriority = runtimeMixSearchPriority(left);
                const int rightPriority = runtimeMixSearchPriority(right);
                if (leftPriority != rightPriority) {
                    return leftPriority < rightPriority;
                }
                return normalizedLowerPath(left) < normalizedLowerPath(right);
            }
        );
        return;
    }

    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!errorCode && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(errorCode)
            && toLowerAscii(entry.path().extension().string()) == ".mix") {
            const std::string path = entry.path().generic_string();
            if (isRuntimeObjectResourceMixCandidate(path)) {
                cache.mixArchivePaths.push_back(path);
            }
        }
        iterator.increment(errorCode);
    }

    std::sort(
        cache.mixArchivePaths.begin(),
        cache.mixArchivePaths.end(),
        [](const std::string& left, const std::string& right) {
            const int leftPriority = runtimeMixSearchPriority(left);
            const int rightPriority = runtimeMixSearchPriority(right);
            if (leftPriority != rightPriority) {
                return leftPriority < rightPriority;
            }
            return normalizedLowerPath(left) < normalizedLowerPath(right);
        }
    );
}

RuntimeMixArchiveCacheEntry* loadRuntimeMixArchiveFromCache(
    const std::string& mixPath,
    RuntimeMapRenderCache& cache) {
    auto [it, inserted] = cache.mixArchivesByPath.try_emplace(mixPath);
    RuntimeMixArchiveCacheEntry& entry = it->second;
    if (!entry.loadAttempted) {
        entry.loadAttempted = true;
        entry.loaded = entry.archive.loadFromFile(mixPath)
            && entry.archive.isIndexReadable();
        entry.errorMessage = entry.archive.getErrorMessage();
        if (!entry.loaded && entry.errorMessage.empty()) {
            entry.errorMessage = "MIX archive index is not readable: " + mixPath;
        }
    }

    return entry.loaded ? &entry : nullptr;
}

bool loadRuntimeResourceBytesFromMixArchives(
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::vector<unsigned char>& bytes,
    std::string& sourceName,
    std::string& errorMessage) {
    ensureRuntimeMixArchivePaths(assetRootPath, cache);

    std::vector<std::string> searchPaths = cache.mixArchivePaths;
    std::stable_sort(
        searchPaths.begin(),
        searchPaths.end(),
        [&fileName](const std::string& left, const std::string& right) {
            const int leftPriority =
                runtimeMixSearchPriorityForFile(left, fileName);
            const int rightPriority =
                runtimeMixSearchPriorityForFile(right, fileName);
            if (leftPriority != rightPriority) {
                return leftPriority < rightPriority;
            }
            return normalizedLowerPath(left) < normalizedLowerPath(right);
        }
    );

    for (const std::string& mixPath : searchPaths) {
        RuntimeMixArchiveCacheEntry* archiveEntry =
            loadRuntimeMixArchiveFromCache(mixPath, cache);
        if (archiveEntry == nullptr) {
            continue;
        }

        const MixEntry* mixEntry = archiveEntry->archive.findEntryByName(fileName);
        if (mixEntry == nullptr) {
            continue;
        }

        std::vector<std::uint8_t> entryBytes;
        if (!archiveEntry->archive.readEntryData(
                *mixEntry,
                entryBytes,
                errorMessage
            )) {
            return false;
        }

        bytes.assign(entryBytes.begin(), entryBytes.end());
        sourceName = mixPath + "!" + fileName;
        return true;
    }

    errorMessage = "resource was not found in loose files or MIX archives: "
        + fileName;
    return false;
}

bool runtimeAssetRootHasMixArchives(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) {
    ensureRuntimeMixArchivePaths(assetRootPath, cache);
    return !cache.mixArchivePaths.empty();
}

bool loadRuntimeResourceBytesByName(
    const std::vector<std::string>& looseRootPaths,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::vector<unsigned char>& bytes,
    std::string& sourceName,
    std::string& errorMessage) {
    bytes.clear();
    sourceName.clear();
    errorMessage.clear();

    const auto tryLooseRoot = [&](const std::string& rootPath)
        -> std::optional<bool> {
        std::string resolvedPath;
        if (!findRuntimeLooseResourceByName(
                rootPath, assetRootPath, fileName, cache, resolvedPath)) {
            return std::nullopt;
        }
        if (!loadBytes(resolvedPath, bytes, errorMessage)) {
            return false;
        }
        sourceName = resolvedPath;
        errorMessage.clear();
        return true;
    };

    // Explicit roots outside the default exports retain caller overrides.
    for (const std::string& rootPath : looseRootPaths) {
        if (!runtimePathUsesDefaultAssetRoot(rootPath, assetRootPath)) {
            if (const auto loaded = tryLooseRoot(rootPath); loaded.has_value()) {
                return *loaded;
            }
        }
    }

    // Stock archives are authoritative over default exported loose copies.
    if (loadRuntimeResourceBytesFromMixArchives(
            assetRootPath,
            fileName,
            cache,
            bytes,
            sourceName,
            errorMessage
        )) {
        return true;
    }
    bytes.clear();
    sourceName.clear();
    if (runtimeAssetRootHasMixArchives(assetRootPath, cache)) {
        return false;
    }

    // A root without archives is a standalone loose/custom installation.
    for (const std::string& rootPath : looseRootPaths) {
        if (runtimePathUsesDefaultAssetRoot(rootPath, assetRootPath)) {
            if (const auto loaded = tryLooseRoot(rootPath); loaded.has_value()) {
                return *loaded;
            }
        }
    }

    return false;
}

bool equalAsciiNoCase(const std::string& left, const std::string& right) {
    return toLowerAscii(left) == toLowerAscii(right);
}

void addUniqueCandidate(std::vector<std::string>& candidates, const std::string& candidate) {
    if (!candidate.empty()
        && std::find(candidates.begin(), candidates.end(), candidate)
            == candidates.end()) {
        candidates.push_back(candidate);
    }
}

char newTheaterCharForExtension(const std::string& tileExtension) {
    const std::string extension = toLowerAscii(tileExtension);
    if (extension == ".tem") {
        return 't';
    }
    if (extension == ".urb") {
        return 'u';
    }
    if (extension == ".ubn") {
        return 'n';
    }
    if (extension == ".des") {
        return 'd';
    }
    if (extension == ".lun") {
        return 'l';
    }
    if (extension == ".sno") {
        return 'a';
    }
    return '\0';
}

bool artIdSupportsNewTheater(const std::string& artId) {
    if (artId.size() < 2) {
        return false;
    }

    const char first =
        static_cast<char>(std::toupper(static_cast<unsigned char>(artId[0])));
    const char second =
        static_cast<char>(std::toupper(static_cast<unsigned char>(artId[1])));
    return (first == 'G' || first == 'N' || first == 'C' || first == 'Y')
        && (second == 'A'
            || second == 'T'
            || second == 'U'
            || second == 'D'
            || second == 'L'
            || second == 'N');
}

std::vector<std::string> resolveAuxiliarySpriteImageIdCandidates(
    const std::string& imageId,
    const RuntimeMapRenderContext& context,
    const ArtType* fallbackArtType) {
    std::vector<std::string> candidates;
    if (imageId.empty()) {
        return candidates;
    }

    const ArtType* artType = context.art == nullptr
        ? nullptr
        : context.art->findArtType(imageId);
    const std::string baseName =
        artType == nullptr || artType->image.empty()
            ? imageId
            : artType->image;
    if (baseName.empty()) {
        return candidates;
    }

    const std::filesystem::path basePath(baseName);
    if (!basePath.extension().empty()) {
        addUniqueCandidate(candidates, basePath.filename().string());
        return candidates;
    }

    const std::string lowerBaseName = toLowerAscii(baseName);
    if (artType != nullptr
        && artType->theater
        && context.theater != nullptr) {
        const std::string theaterExtension =
            context.theater->getTileExtension();
        if (!theaterExtension.empty()) {
            addUniqueCandidate(
                candidates,
                lowerBaseName + theaterExtension
            );
            addUniqueCandidate(candidates, baseName + theaterExtension);
        }
    }
    // Stock grinder B/D layers omit NewTheater, but their normal and snow
    // canvases are separate G/A resources just like their building body.
    const bool grinderLayer=lowerBaseName.rfind("yagrnd_",0)==0;
    // Buildup filenames belong to the building's NewTheater policy even
    // when the MK image has no separate ART entry. Other auxiliary layers
    // keep their own policy; a palette fallback alone does not opt them in.
    const bool buildupNewTheater = fallbackArtType != nullptr
        && fallbackArtType->newTheater
        && equalAsciiNoCase(trimAsciiForRender(imageId),
            trimAsciiForRender(fallbackArtType->buildup));
    if (((artType != nullptr && (artType->newTheater || grinderLayer))
            || buildupNewTheater)
        && context.theater != nullptr
        && artIdSupportsNewTheater(baseName)
        && lowerBaseName.size() >= 2) {
        const char newTheaterChar =
            newTheaterCharForExtension(context.theater->getTileExtension());
        if (newTheaterChar != '\0') {
            std::string newTheaterName = lowerBaseName;
            newTheaterName[1] = newTheaterChar;
            addUniqueCandidate(candidates, newTheaterName);

            std::string genericName = lowerBaseName;
            genericName[1] = 'g';
            addUniqueCandidate(candidates, genericName);
        }
    }

    addUniqueCandidate(candidates, baseName);
    if (!equalAsciiNoCase(baseName, imageId)) {
        addUniqueCandidate(candidates, imageId);
    }
    return candidates;
}

double runtimeElapsedMs(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool runtimeRenderDetailDiagnosticsEnabled(
    const RuntimeMapRenderContext& context) {
    if (context.detailStatsEnabled) {
        return true;
    }

    static const bool environmentEnabled = []() {
        const char* rawValue = runtimeRendererEnvironmentValue("CONCORD_RENDER_DETAIL_STATS");
        if (rawValue == nullptr || rawValue[0] == '\0') {
            return false;
        }

        const std::string value = toLowerAscii(rawValue);
        return value != "0"
            && value != "false"
            && value != "no"
            && value != "off";
    }();
    return environmentEnabled;
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

bool runtimePathUsesDefaultAssetRoot(
    const std::string& resourcePath,
    const std::string& assetRootPath) {
    if (resourcePath.empty() || assetRootPath.empty()) {
        return false;
    }

    const auto normalizedAbsolute = [](const std::string& path) {
        std::error_code errorCode;
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(path, errorCode);
        return normalizedLowerPath((errorCode
            ? std::filesystem::path(path)
            : absolutePath).lexically_normal().generic_string());
    };
    std::string root = normalizedAbsolute(assetRootPath);
    while (!root.empty() && root.back() == '/') {
        root.pop_back();
    }
    const std::string path = normalizedAbsolute(resourcePath);
    return path == root
        || (path.size() > root.size()
            && path.compare(0, root.size(), root) == 0
            && path[root.size()] == '/');
}

RuntimeTerrainTmpContext runtimeTerrainTmpContextForRender(
    const RuntimeMapRenderContext& context) {
    return {context.theater, context.tileRootPath, context.assetRootPath,
        context.structureRootPath};
}

RuntimeObjectShpSpriteCacheEntry* loadRuntimeAuxiliaryShpSpriteFromCache(
    const std::string& imageId,
    int requestedFrameIndex,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    bool mainFramesOnly,
    const ArtType* fallbackArtType) {
    if (imageId.empty()) {
        return nullptr;
    }

    std::vector<std::string> imageCandidates =
        resolveAuxiliarySpriteImageIdCandidates(imageId, context, fallbackArtType);
    if (imageCandidates.empty()) {
        imageCandidates.push_back(imageId);
    }
    const ArtType* auxiliaryArtType = context.art == nullptr
        ? nullptr
        : context.art->findArtType(imageId);
    if (auxiliaryArtType == nullptr) {
        auxiliaryArtType = fallbackArtType;
    }
    std::string candidateKey;
    for (const std::string& candidate : imageCandidates) {
        candidateKey += "|";
        candidateKey += toLowerAscii(candidate);
    }
    const std::string& mixExportsRoot =
        cachedRuntimeMixExportsRootForContext(context, cache);

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
            runtimeRenderDetailDiagnosticsEnabled(context);
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
            loaded = loadRuntimeResourceBytesFromMixArchives(
                context.assetRootPath,
                fileName,
                cache,
                bytesEntry.bytes,
                sourceName,
                errorMessage
            );
            if (!loaded
                && !mixExportsRoot.empty()
                && normalizedLowerPath(mixExportsRoot)
                    != normalizedLowerPath(context.assetRootPath)) {
                loaded = loadRuntimeResourceBytesFromMixArchives(
                    mixExportsRoot,
                    fileName,
                    cache,
                    bytesEntry.bytes,
                    sourceName,
                    errorMessage
                );
            }
            if (!sidebarResource) {
                if (!loaded) {
                    loaded = loadRuntimeResourceBytesByName(
                        {context.structureRootPath},
                        context.assetRootPath,
                        fileName,
                        cache,
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
        return nullptr;
    }

    const int availableFrameCount =
        mainFramesOnly
            ? runtimeVisibleMainFrameCount(bytesEntry.frameCount, true)
            : bytesEntry.frameCount;
    const int normalizedFrameIndex = availableFrameCount <= 0
        ? 0
        : positiveModulo(requestedFrameIndex, availableFrameCount);
    const std::string cacheKey =
        toLowerAscii(imageId)
        + candidateKey
        + "|frame"
        + std::to_string(normalizedFrameIndex)
        + (mainFramesOnly ? "|main" : "|all")
        + "|"
        + context.assetRootPath
        + "|"
        + context.structureRootPath;

    auto [it, inserted] =
        cache.auxiliaryShpSpritesByImage.try_emplace(cacheKey);
    RuntimeObjectShpSpriteCacheEntry& entry = it->second;
    if (inserted) {
        ++cache.auxiliaryShpFrameCacheMisses;
    } else {
        ++cache.auxiliaryShpFrameCacheHits;
    }
    if (!entry.loadAttempted) {
        entry.loadAttempted = true;
        const bool detailTiming =
            runtimeRenderDetailDiagnosticsEnabled(context);
        const auto frameMissStart =
            runtimeMaybeTimingStart(detailTiming);

        entry.resolvedImageId = bytesEntry.resolvedImageId;
        entry.sourceFingerprint = bytesEntry.sourceFingerprint;
        entry.sourcePath = bytesEntry.sourcePath;
        entry.frameCount = bytesEntry.frameCount;
        entry.frameIndex = normalizedFrameIndex;
        std::string errorMessage;
        cache.lastColdResourceType = "SHP";
        cache.lastColdResourceName = imageId.substr(0, 96);
        ++cache.lastColdResourceSequence;
        entry.loaded = decodeRuntimeShpFrameFromBytes(
            bytesEntry.bytes,
            bytesEntry.sourcePath,
            entry.frameIndex,
            entry.image,
            errorMessage
        );
        entry.shadowFrameIndex = runtimeShpShadowFrameIndex(
            entry.frameCount,
            entry.frameIndex,
            mainFramesOnly
        );
        if (entry.loaded && entry.shadowFrameIndex >= 0) {
            std::string shadowError;
            entry.hasShadowFrame = decodeRuntimeShpFrameFromBytes(
                bytesEntry.bytes,
                bytesEntry.sourcePath,
                entry.shadowFrameIndex,
                entry.shadowImage,
                shadowError
            );
        }
        entry.errorMessage = errorMessage;
        cache.auxiliaryShpFrameCacheMissMs +=
            runtimeMaybeElapsedMs(detailTiming, frameMissStart);
    }

    return entry.loaded ? &entry : nullptr;
}

bool runtimeInfantrySwimmingWithMetadata(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache* cache, RuntimeTerrainMetadataQuery queryMetadata) {
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || (object.hasRenderPosition ? object.renderOnBridge : object.onBridge)
        || context.mapFacts == nullptr) return false;

    // Logical movement reserves the destination before the body reaches it.
    // Cross the shoreline with the rendered body, including bridge transitions.
    const double x = object.hasRenderPosition ? object.renderX : object.x;
    const double y = object.hasRenderPosition ? object.renderY : object.y;
    if (!std::isfinite(x) || !std::isfinite(y)
        || x < 0.0 || x >= 512.0 || y < 0.0 || y >= 512.0) return false;
    const MapCellFacts* cell = MapQuery(*context.mapFacts).getCell(
        static_cast<int>(std::floor(x + 0.5)),
        static_cast<int>(std::floor(y + 0.5))
    );
    if (cell == nullptr) return false;

    if (cell->hasTerrain) {
        // Shore tile sets contain both land and water subtiles. Their names
        // cannot determine whether this particular cell needs a swimming pose.
        std::unique_ptr<RuntimeMapRenderCache> temporaryCache;
        if (cache == nullptr) {
            temporaryCache = std::make_unique<RuntimeMapRenderCache>();
            cache = temporaryCache.get();
        }
        const int landType = runtimeLandTypeForTmpImage(queryMetadata(
            cell->terrain.tileIndex, cell->terrain.subTileIndex, context, *cache
        ));
        if (landType != RuntimeLandTypeUnknown) return landType == RuntimeLandTypeWater;
    }
    return cell->isWater && !cell->isShore;
}

std::size_t releaseRuntimeAuxiliaryShpImageFromCacheInternal(
    RuntimeMapRenderCache& cache,
    const std::string& imageId) {
    const std::string normalized = toLowerAscii(trimAsciiForRender(imageId));
    if (normalized.empty()) {
        return 0U;
    }
    // Both cache-key schemas begin with the requested image followed by the
    // first resolved-candidate separator. Requiring that separator keeps an
    // image such as GACNST2 from being evicted together with GACNST.
    const std::string prefix = normalized + "|";
    const auto eraseMatches = [&prefix](auto& entries) {
        std::size_t removed = 0U;
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->first.rfind(prefix, 0U) == 0U) {
                it = entries.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    };
    return eraseMatches(cache.auxiliaryShpSpritesByImage)
        + eraseMatches(cache.auxiliaryShpBytesByImage);
}

void resetRuntimeWaterWakeStateInternal(RuntimeMapRenderCache& cache) {
    cache.waterWakeActors.clear();
    cache.waterWakeLastSpawnTickByObject.clear();
    cache.waterWakeLastAnimationTick = -1;
    cache.waterWakeLastUpdateTick = -1;
    cache.waterWakeLastUpdateEligibleObjects = 0;
    cache.waterWakeLastUpdateEmissions = 0;
    cache.waterWakeLastUpdateSuppressedNotMoving = 0;
    cache.waterWakeLastUpdateSuppressedNotWater = 0;
    cache.waterWakeLastUpdateSuppressedSubmerged = 0;
    cache.waterWakeConfigurationSignature.clear();
    cache.terrainLookupWorld = nullptr;
    cache.terrainLookupData = nullptr;
    cache.terrainLookupCount = 0;
    cache.terrainByCell.clear();
    cache.terrainByCellIndex.clear();
}

bool containsText(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool runtimeObjectAircraftRuleClassification(
    const WorldObjectInstance& object,
    const RulesDatabase* rules
) {
    if (rules == nullptr
        || (object.kind != WorldObjectKind::Unit
            && object.kind != WorldObjectKind::Infantry)) {
        return false;
    }

    const UnitType* aircraftType =
        rules->findUnitType("aircraft", object.type);
    if (aircraftType != nullptr) {
        return true;
    }

    const UnitType* flyingType = object.kind == WorldObjectKind::Infantry
        ? rules->findUnitType("infantry", object.type)
        : rules->findUnitType("vehicle", object.type);
    if (flyingType == nullptr) {
        return false;
    }

    const std::string speedType = toLowerAscii(flyingType->speedType);
    const std::string movementZone = toLowerAscii(flyingType->movementZone);
    const std::string category = toLowerAscii(flyingType->category);
    const std::string locomotor = toLowerAscii(flyingType->locomotor);
    return flyingType->jumpJet
        || flyingType->consideredAircraft
        || containsText(speedType, "air")
        || containsText(speedType, "fly")
        || containsText(speedType, "winged")
        || containsText(movementZone, "air")
        || containsText(movementZone, "fly")
        || containsText(category, "air")
        || containsText(locomotor, "air")
        || containsText(locomotor, "fly");
}
}

using namespace concord::runtime::resource_detail;

int runtimeAircraftSpriteLiftPixels(
    const WorldObjectInstance& object,
    bool aircraft) {
    if (!aircraft || !object.airborne || object.mission == "CrashingWreck") {
        return 0;
    }
    if (object.aircraftDockFlightProgress >= 0.0) {
        return static_cast<int>(std::lround(34.0 * std::clamp(
            object.aircraftDockFlightProgress, 0.0, 1.0)));
    }
    return 34;
}

bool loadRuntimeAuxiliaryShpFrameImage(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& resolvedImageId,
    std::string& sourcePath,
    std::string& errorMessage,
    bool mainFramesOnly,
    int* frameCount,
    const ArtType* fallbackArtType) {
    RuntimeObjectShpSpriteCacheEntry* entry =
        loadRuntimeAuxiliaryShpSpriteFromCache(
            imageId,
            frameIndex,
            context,
            cache,
            mainFramesOnly,
            fallbackArtType
        );
    if (entry == nullptr || !entry->loaded) {
        errorMessage = entry != nullptr
            ? entry->errorMessage
            : "auxiliary SHP was not found: " + imageId;
        // A decoded frame failure must retain its resource path and reason;
        // the cache loader returns null for both missing and malformed art.
        const std::string prefix = toLowerAscii(imageId) + "|";
        for (const auto& [key, failed] : cache.auxiliaryShpSpritesByImage) {
            const int available = mainFramesOnly
                ? runtimeVisibleMainFrameCount(failed.frameCount, true) : failed.frameCount;
            if (key.rfind(prefix, 0) == 0 && !failed.loaded && available > 0
                    && failed.frameIndex == positiveModulo(frameIndex, available)
                    && !failed.errorMessage.empty()) {
                errorMessage = imageId + " frame " + std::to_string(failed.frameIndex)
                    + ": " + failed.errorMessage + " source=" + failed.sourcePath;
                break;
            }
        }
        return false;
    }

    image = entry->image;
    if (frameCount != nullptr) {
        *frameCount = mainFramesOnly
            ? runtimeVisibleMainFrameCount(entry->frameCount, true)
            : entry->frameCount;
    }
    resolvedImageId = entry->resolvedImageId;
    sourcePath = entry->sourcePath;
    errorMessage.clear();
    return true;
}

std::size_t releaseRuntimeAuxiliaryShpImageFromCache(
    RuntimeMapRenderCache& cache,
    const std::string& imageId) {
    return releaseRuntimeAuxiliaryShpImageFromCacheInternal(cache, imageId);
}

void resetRuntimeWaterWakeState(RuntimeMapRenderCache& cache) {
    resetRuntimeWaterWakeStateInternal(cache);
}
