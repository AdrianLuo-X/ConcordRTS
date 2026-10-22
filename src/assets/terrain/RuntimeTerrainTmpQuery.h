#pragma once

#include "assets/codecs/RuntimeTmpTypes.h"
#include "simulation/navigation/terrain/RuntimeTerrainTypes.h"

#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class TheaterDatabase;

inline constexpr const char* runtimeTerrainMixResourcePrefix = "@mix-resource:";

inline constexpr std::array<int, 16> RuntimeLandTypeByTmpTerrainType{{
    0, 8, 8, 8, 8, 10, 9, 3, 3, 2, 6, 1, 1, 0, 7, 3
}};

// The renderer owns this component once. Node insertion does not invalidate
// borrowed image pointers; callers retain the original scope mutation rules.
struct RuntimeTerrainTmpCache {
    std::unordered_map<std::string, RuntimeTmpTileFile> tmpTilesByPath;
    std::string lastTmpTileLoadError;
    std::string terrainTilePathCacheRoot;
    std::unordered_map<int, std::string> terrainTilePathByIndex;
    std::unordered_set<int> missingTerrainTilePathByIndex;
};

// Synchronous read-only view, not an owning snapshot or renderer context.
struct RuntimeTerrainTmpContext {
    const TheaterDatabase* theater;
    const std::string& tileRootPath;
    const std::string& assetRootPath;
    const std::string& structureRootPath;
};

class RuntimeTerrainTmpByteSource {
public:
    virtual ~RuntimeTerrainTmpByteSource() = default;
    virtual bool readLooseBytes(
        const std::string& path,
        std::vector<unsigned char>& bytes,
        std::string& errorMessage
    ) = 0;
    virtual bool readResourceBytesByName(
        const std::vector<std::string>& rootPaths,
        const std::string& assetRootPath,
        const std::string& fileName,
        std::vector<unsigned char>& bytes,
        std::string& sourceName,
        std::string& errorMessage
    ) = 0;
    virtual std::string cachedMixExportsRoot() = 0;
    virtual bool assetRootHasMixArchives(const std::string& assetRootPath) = 0;
};

// Only a typed memo boundary. Native frame/context/cache matching and the
// existing cap/borrow lifetime remain in the private adapter.
class RuntimeTerrainTmpMemoPort {
public:
    virtual ~RuntimeTerrainTmpMemoPort() = default;
    virtual bool find(
        int tileIndex,
        RuntimeTmpTileFile*& file,
        bool& updatesLastLoadError
    ) = 0;
    virtual void remember(
        int tileIndex,
        RuntimeTmpTileFile* file,
        bool updatesLastLoadError
    ) = 0;
};

RuntimeTmpTileFile* loadRuntimeTmpTileFromCache(
    const std::string& path,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
);
RuntimeTmpTileFile* loadRuntimeTmpTileByNameFromCache(
    const std::vector<std::string>& rootPaths,
    const std::string& assetRootPath,
    const std::string& fileName,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
);
std::string runtimeTerrainTileLookupCacheRoot(
    const RuntimeTerrainTmpContext& context
);
bool runtimeTerrainTileLocatorIsMixResource(const std::string& locator);
std::string runtimeTerrainTileResourceName(const std::string& locator);
RuntimeTmpTileFile* loadRuntimeTerrainTileForIndexUnmemoized(
    int tileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source
);
RuntimeTmpTileFile* loadRuntimeTerrainTileForIndex(
    int tileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
);
const RuntimeTmpSubTileImage* resolveRuntimeTerrainMetadata(
    int tileIndex,
    int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
);
int runtimeLandTypeForTmpImage(const RuntimeTmpSubTileImage* terrainImage);
unsigned char resolveRuntimeTerrainSubTileRampType(
    int tileIndex,
    int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
);
RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(
    int tileIndex,
    int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo
);
bool resolveRuntimeTerrainTmpTileGridShape(
    int tileIndex,
    int subTileIndex,
    const RuntimeTerrainTmpContext& context,
    RuntimeTerrainTmpCache& cache,
    RuntimeTerrainTmpByteSource& source,
    RuntimeTerrainTmpMemoPort& memo,
    int& gridWidth,
    int& gridHeight
);
