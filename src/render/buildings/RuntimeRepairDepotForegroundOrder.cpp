#include "render/buildings/RuntimeRepairDepotForegroundOrder.h"

#include <algorithm>

namespace concord::render {

RuntimeRepairDepotPlatform makeRuntimeRepairDepotPlatform(std::size_t recordIndex, int instanceId,
    double x, double y, int footprintWidth, int footprintHeight, int impassableRows) {
    const int blockedRows = std::clamp(impassableRows, 0, footprintWidth);
    return {recordIndex, instanceId, x + blockedRows - 0.5, y - 0.5,
        x + footprintWidth - 0.5, y + footprintHeight - 0.5};
}

RuntimeRepairDepotForegroundOrder::RuntimeRepairDepotForegroundOrder(
    std::span<const RuntimeRepairDepotPlatform> platforms)
    : platforms_(platforms), useSpatialIndex_(platforms.size() > 16) {
    if (!useSpatialIndex_) return;
    for (std::size_t i = 0; i < platforms_.size(); ++i) {
        const auto& platform = platforms_[i];
        spatialIndex_.add(platform.left, platform.top, platform.right, platform.bottom);
        platformsById_[platform.instanceId].push_back(i);
    }
}

std::optional<std::size_t> RuntimeRepairDepotForegroundOrder::firstMatchingHost(
    double x, double y, int dockedToObjectId) {
    if (useSpatialIndex_) {
        spatialIndex_.query(x, y, candidates_);
        // An interpolated vehicle can be outside its dock; preserve scene order
        // when including every host record with the matching object ID.
        const auto docked = platformsById_.find(dockedToObjectId);
        if (docked != platformsById_.end()) {
            candidates_.insert(candidates_.end(), docked->second.begin(), docked->second.end());
            std::sort(candidates_.begin(), candidates_.end());
            candidates_.erase(std::unique(candidates_.begin(), candidates_.end()), candidates_.end());
        }
    }
    const std::size_t count = useSpatialIndex_ ? candidates_.size() : platforms_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& platform = platforms_[useSpatialIndex_ ? candidates_[i] : i];
        const bool onPlatform = x >= platform.left && x < platform.right
            && y >= platform.top && y < platform.bottom;
        if (onPlatform || dockedToObjectId == platform.instanceId) return platform.recordIndex;
    }
    return std::nullopt;
}

} // namespace concord::render
