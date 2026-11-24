#pragma once

#include "data/art/ArtDatabase.h"

#include <span>

struct SpecialMechanismRenderSnapshot;

namespace concord::render {

struct RuntimeRepairDepotAnimationLayer {
    const ArtType::ActiveAnimation* layer = nullptr;
    int ageTicks = 0;
    // 0=idle, 1=extend, 2=work, 3=retract.
    int phase = 0;
};

RuntimeRepairDepotAnimationLayer selectRuntimeRepairDepotAnimation(int instanceId,
    std::span<const SpecialMechanismRenderSnapshot> snapshots,
    std::span<const ArtType::ActiveAnimation> layers);

} // namespace concord::render
