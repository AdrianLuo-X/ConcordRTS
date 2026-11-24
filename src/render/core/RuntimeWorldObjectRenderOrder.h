#pragma once

namespace concord::render {

struct RuntimeWorldObjectRenderOrderLess {
    template <class DrawRecord>
    bool operator()(const DrawRecord& left, const DrawRecord& right) const {
        if (left.sortPoint.y != right.sortPoint.y) {
            return left.sortPoint.y < right.sortPoint.y;
        }
        if (left.drawLevel != right.drawLevel) {
            return left.drawLevel < right.drawLevel;
        }
        if (left.sortPoint.x != right.sortPoint.x) {
            return left.sortPoint.x < right.sortPoint.x;
        }
        if (left.cellSum != right.cellSum) {
            return left.cellSum < right.cellSum;
        }
        if (left.kindBias != right.kindBias) {
            return left.kindBias < right.kindBias;
        }
        return left.instanceId < right.instanceId;
    }
};

enum class RuntimeGroundBodyKind {
    WaterWake,
    GroundImpact,
    Particle,
    WorldObject,
    GateOverlay
};

template <class SortKey>
static inline bool runtimeGroundBodyRenderOrderLess(
    const SortKey& left,
    const SortKey& right
) {
    if (left.groundRubble != right.groundRubble) {
        return left.groundRubble;
    }
    if (left.ySortLeptons != right.ySortLeptons) {
        return left.ySortLeptons < right.ySortLeptons;
    }
    if (left.drawLevel != right.drawLevel) {
        return left.drawLevel < right.drawLevel;
    }
    if (left.sortPoint.x != right.sortPoint.x) {
        return left.sortPoint.x < right.sortPoint.x;
    }
    if (left.cellSum != right.cellSum) {
        return left.cellSum < right.cellSum;
    }
    // Flat ground effects at the exact same key must remain beneath the body
    // that created them, irrespective of the body's category tie-breaker.
    if (left.kind != right.kind) {
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    }
    if (left.kindBias != right.kindBias) {
        return left.kindBias < right.kindBias;
    }
    // Older independent AnimClass actors are ordered first. Body records use
    // the default -1 and therefore retain their existing instance ordering.
    if (left.spawnTick != right.spawnTick) {
        return left.spawnTick < right.spawnTick;
    }
    if (left.instanceId != right.instanceId) {
        return left.instanceId < right.instanceId;
    }
    return left.ordinal < right.ordinal;
}

} // namespace concord::render
