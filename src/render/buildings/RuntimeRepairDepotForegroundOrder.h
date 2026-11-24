#pragma once

#include "render/core/RuntimeRenderSpatialIndex.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace concord::render {

struct RuntimeRepairDepotPlatform {
    std::size_t recordIndex;
    int instanceId;
    double left;
    double top;
    double right;
    double bottom;
};

RuntimeRepairDepotPlatform makeRuntimeRepairDepotPlatform(std::size_t recordIndex, int instanceId,
    double x, double y, int footprintWidth, int footprintHeight, int impassableRows);

// Borrows the scene's ordered platforms for one paint or geometry query.
class RuntimeRepairDepotForegroundOrder {
public:
    explicit RuntimeRepairDepotForegroundOrder(std::span<const RuntimeRepairDepotPlatform> platforms);
    std::optional<std::size_t> firstMatchingHost(double x, double y, int dockedToObjectId);

private:
    std::span<const RuntimeRepairDepotPlatform> platforms_;
    bool useSpatialIndex_;
    RuntimeRenderSpatialIndex spatialIndex_;
    std::unordered_map<int, std::vector<std::size_t>> platformsById_;
    std::vector<std::size_t> candidates_;
};

} // namespace concord::render
