#pragma once

#include "render/core/RuntimeMapRenderer.h"
#include <chrono>
#include <filesystem>
#include <optional>

namespace concord::runtime::resource_detail {

int positiveModulo(int value, int modulus) ;

bool loadBytes(
    const std::string& path,
    std::vector<unsigned char>& bytes,
    std::string& errorMessage) ;

unsigned int readUInt16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) ;

int runtimeShpFrameCount(const std::vector<unsigned char>& bytes) ;

int runtimeVisibleMainFrameCount(int frameCount, bool splitShadow) ;

int runtimeShpShadowFrameIndex(
    int frameCount,
    int visibleFrameIndex,
    bool splitShadow) ;

bool decodeRuntimeShpFrameFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& errorMessage) ;

std::string toLowerAscii(std::string value) ;

std::string trimAsciiForRender(const std::string& value) ;

std::string fileNameForShpImageId(const std::string& imageId) ;

bool runtimeFileNameHasExtension(
    const std::string& fileName,
    const std::string& extension) ;

bool runtimeAuxiliaryResourceUsesShpBytes(
    const std::string& fileName,
    const ArtType* artType) ;

bool findFileByNameCaseInsensitive(
    const std::string& rootPath,
    const std::string& fileName,
    std::string& resolvedPath) ;

std::string normalizedLowerPath(std::string path) ;

std::string runtimeMixExportsRootFromPath(const std::string& pathString) ;

std::string runtimeMixExportsRootForContext(
    const RuntimeMapRenderContext& context) ;

const std::string& cachedRuntimeMixExportsRootForContext(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) ;

int runtimeMixSearchPriority(const std::string& path) ;

bool isRuntimeSidebarResourceFileName(const std::string& fileName) ;

int runtimeMixSearchPriorityForFile(
    const std::string& path,
    const std::string& fileName) ;

bool isRuntimeObjectResourceMixCandidate(const std::string& path) ;

int runtimeLooseResourceSearchPriority(const std::string& path) ;

bool isRuntimeObjectLooseExtension(const std::filesystem::path& path) ;

void appendRuntimeLooseResourcePathsFromRoot(
    const std::filesystem::path& root,
    std::vector<std::string>& paths) ;

std::vector<std::filesystem::path> runtimeLooseObjectSearchRoots(
    const std::filesystem::path& assetRoot) ;

void ensureRuntimeLooseResourceIndex(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) ;

bool findRuntimeLooseResourceByName(
    const std::string& rootPath,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::string& resolvedPath) ;

void ensureRuntimeMixArchivePaths(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) ;

RuntimeMixArchiveCacheEntry* loadRuntimeMixArchiveFromCache(
    const std::string& mixPath,
    RuntimeMapRenderCache& cache) ;

bool loadRuntimeResourceBytesFromMixArchives(
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::vector<unsigned char>& bytes,
    std::string& sourceName,
    std::string& errorMessage) ;

bool runtimeAssetRootHasMixArchives(
    const std::string& assetRootPath,
    RuntimeMapRenderCache& cache) ;

bool loadRuntimeResourceBytesByName(
    const std::vector<std::string>& looseRootPaths,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeMapRenderCache& cache,
    std::vector<unsigned char>& bytes,
    std::string& sourceName,
    std::string& errorMessage) ;

bool equalAsciiNoCase(const std::string& left, const std::string& right) ;

void addUniqueCandidate(std::vector<std::string>& candidates, const std::string& candidate) ;

char newTheaterCharForExtension(const std::string& tileExtension) ;

bool artIdSupportsNewTheater(const std::string& artId) ;

std::vector<std::string> resolveAuxiliarySpriteImageIdCandidates(
    const std::string& imageId,
    const RuntimeMapRenderContext& context,
    const ArtType* fallbackArtType = nullptr
) ;

double runtimeElapsedMs(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) ;

bool runtimeRenderDetailDiagnosticsEnabled(
    const RuntimeMapRenderContext& context) ;

std::chrono::steady_clock::time_point runtimeMaybeTimingStart(bool enabled) ;

double runtimeMaybeElapsedMs(
    bool enabled,
    const std::chrono::steady_clock::time_point& start) ;

bool runtimePathUsesDefaultAssetRoot(
    const std::string& resourcePath,
    const std::string& assetRootPath) ;

RuntimeTerrainTmpContext runtimeTerrainTmpContextForRender(
    const RuntimeMapRenderContext& context) ;

RuntimeObjectShpSpriteCacheEntry* loadRuntimeAuxiliaryShpSpriteFromCache(
    const std::string& imageId,
    int requestedFrameIndex,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    bool mainFramesOnly = false,
    const ArtType* fallbackArtType = nullptr
) ;

using RuntimeTerrainMetadataQuery = const RuntimeTmpSubTileImage*(*)(
    int, int, const RuntimeMapRenderContext&, RuntimeMapRenderCache&);

bool runtimeInfantrySwimmingWithMetadata(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache* cache, RuntimeTerrainMetadataQuery queryMetadata) ;

std::size_t releaseRuntimeAuxiliaryShpImageFromCacheInternal(
    RuntimeMapRenderCache& cache,
    const std::string& imageId) ;

void resetRuntimeWaterWakeStateInternal(RuntimeMapRenderCache& cache) ;

bool containsText(const std::string& haystack, const std::string& needle) ;

inline constexpr int RuntimeLandTypeUnknown = -1;

inline constexpr int RuntimeLandTypeWater = 2;

class RuntimeTerrainTmpResourceByteSource final : public RuntimeTerrainTmpByteSource {
public:
    explicit RuntimeTerrainTmpResourceByteSource(
        RuntimeMapRenderCache& cache,
        const RuntimeMapRenderContext* context = nullptr
    ) : cache_(cache), context_(context) {}
    bool readLooseBytes(
        const std::string& path, std::vector<unsigned char>& bytes,
        std::string& errorMessage
    ) override {
        return loadBytes(path, bytes, errorMessage);
    }
    bool readResourceBytesByName(
        const std::vector<std::string>& rootPaths,
        const std::string& assetRootPath, const std::string& fileName,
        std::vector<unsigned char>& bytes, std::string& sourceName,
        std::string& errorMessage
    ) override {
        return loadRuntimeResourceBytesByName(
            rootPaths, assetRootPath, fileName, cache_, bytes,
            sourceName, errorMessage
        );
    }
    std::string cachedMixExportsRoot() override {
        // Only index/metadata wrappers call this method; each binds context.
        return cachedRuntimeMixExportsRootForContext(*context_, cache_);
    }
    bool assetRootHasMixArchives(const std::string& assetRootPath) override {
        return runtimeAssetRootHasMixArchives(assetRootPath, cache_);
    }
private:
    RuntimeMapRenderCache& cache_;
    const RuntimeMapRenderContext* context_;
};

bool runtimeObjectAircraftRuleClassification(
    const WorldObjectInstance& object,
    const RulesDatabase* rules
) ;

}
