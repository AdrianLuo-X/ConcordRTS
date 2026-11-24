#include "simulation/ai/RuntimeTeamGather.h"

#include "core/io/RuntimeSaveTextFields.h"
#include "world/model/GameWorld.h"

#include <cmath>

namespace concord::simulation::ai {

RuntimeCellPoint runtimeTeamHouseCenter(
    std::span<const WorldObjectInstance> objects,
    const std::string& owner,
    RuntimeCellPoint fallback) {
    long long x = 0;
    long long y = 0;
    int count = 0;
    for (const auto& object : objects) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || runtime_save_text::toLowerAsciiLocal(object.owner)
                != runtime_save_text::toLowerAsciiLocal(owner)) {
            continue;
        }
        x += object.x;
        y += object.y;
        ++count;
    }
    return count ? RuntimeCellPoint{static_cast<int>(x / count), static_cast<int>(y / count)} : fallback;
}

RuntimeCellPoint runtimeTeamGatherPoint(
    RuntimeCellPoint center,
    RuntimeCellPoint ownBase,
    RuntimeCellPoint enemyBase,
    bool outsideEnemyBase,
    bool hasOwnBase,
    int distance) {
    const auto origin = outsideEnemyBase ? enemyBase : ownBase;
    const auto toward = outsideEnemyBase ? ownBase : enemyBase;
    double dx = toward.x - origin.x;
    double dy = toward.y - origin.y;
    const double length = std::hypot(dx, dy);
    if (length > 0.0) {
        dx /= length;
        dy /= length;
    } else {
        dx = 1.0;
        dy = 0.0;
    }
    const RuntimeCellPoint destination{
        origin.x + static_cast<int>(std::lround(dx * distance)),
        origin.y + static_cast<int>(std::lround(dy * distance))};
    return !outsideEnemyBase && !hasOwnBase ? center : destination;
}

} // namespace concord::simulation::ai
