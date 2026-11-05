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

using namespace concord::runtime::resource_detail;

namespace {
// A headless host has no Native geometry frame. Every cache miss still
// invokes the same byte source, decoder and terrain query.
class RuntimeUnframedTmpMemo final : public RuntimeTerrainTmpMemoPort {
public:
    bool find(int, RuntimeTmpTileFile*&, bool&) override { return false; }
    void remember(int, RuntimeTmpTileFile*, bool) override {}
};

const RuntimeTmpSubTileImage* resolveRuntimeTerrainMetadata(
    int tileIndex,
    int subTileIndex,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    RuntimeTerrainTmpResourceByteSource source(cache, &context);
    RuntimeUnframedTmpMemo memo;
    return ::resolveRuntimeTerrainMetadata(
        tileIndex, subTileIndex, runtimeTerrainTmpContextForRender(context),
        cache, source, memo
    );
}
}

bool resolveRuntimeTerrainTileGridShape(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& gridWidth,
    int& gridHeight) {
    RuntimeTerrainTmpResourceByteSource source(cache, &context);
    RuntimeUnframedTmpMemo memo;
    return ::resolveRuntimeTerrainTmpTileGridShape(
        cell.tileIndex, cell.subTileIndex,
        runtimeTerrainTmpContextForRender(context), cache, source, memo,
        gridWidth, gridHeight
    );
}

unsigned char resolveRuntimeTerrainSubTileRampType(
    const TerrainCell& cell, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) {
    RuntimeTerrainTmpResourceByteSource source(cache, &context);
    RuntimeUnframedTmpMemo memo;
    return ::resolveRuntimeTerrainSubTileRampType(
        cell.tileIndex, cell.subTileIndex,
        runtimeTerrainTmpContextForRender(context), cache, source, memo
    );
}

RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    RuntimeTerrainTmpResourceByteSource source(cache, &context);
    RuntimeUnframedTmpMemo memo;
    return ::resolveRuntimeTerrainSubTileLandType(
        cell.tileIndex, cell.subTileIndex,
        runtimeTerrainTmpContextForRender(context), cache, source, memo
    );
}

bool resolveRuntimeInfantrySwimming(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    return runtimeInfantrySwimmingWithMetadata(object, context, &cache, resolveRuntimeTerrainMetadata);
}

bool runtimeObjectUsesAircraftSpriteForRender(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context) {
    return runtimeObjectAircraftRuleClassification(object, context.rules);
}
