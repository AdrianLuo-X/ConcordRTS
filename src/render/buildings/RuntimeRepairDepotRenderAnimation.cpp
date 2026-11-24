#include "render/buildings/RuntimeRepairDepotRenderAnimation.h"

#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/buildings/RuntimeRepairDepotAnimation.h"

#include <algorithm>

namespace concord::render {

RuntimeRepairDepotAnimationLayer selectRuntimeRepairDepotAnimation(int instanceId,
    std::span<const SpecialMechanismRenderSnapshot> snapshots,
    std::span<const ArtType::ActiveAnimation> layers) {
    RuntimeRepairDepotAnimationLayer result;
    for (const auto& snapshot : snapshots) {
        if (snapshot.kind != "repairDepot" || snapshot.objectId != instanceId)
            continue;
        for (const auto& layer : layers) {
            if (layer.key != snapshot.resourceId
                || !runtimeRepairDepotAnimationKey(layer.key)) continue;
            result.layer = &layer;
            result.ageTicks = std::max(0, snapshot.progress);
            result.phase = layer.key == "SpecialAnim" ? 1
                : layer.key == "SpecialAnimTwo" ? 2 : 3;
            return result;
        }
    }
    return result;
}

} // namespace concord::render
