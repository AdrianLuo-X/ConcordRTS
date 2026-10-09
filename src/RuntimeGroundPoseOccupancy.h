#pragma once

#include "GameWorld.h"
#include "GameWorldSubCellOffset.h"

#include <cmath>
#include <limits>
#include <optional>

// Navigation commits before interpolation completes. Keep the additional
// physical tile and one infantry slot derived from the existing saved pose.
// No second position or occupancy state is serialized. Same-tile occupancy
// stays with the authoritative slot, preserving the three-slot contract.
struct RuntimeGroundPoseOccupancyContext {
    const void* state = nullptr;
    bool (*holdsActivePose)(const void*,int) = nullptr;
};
inline thread_local RuntimeGroundPoseOccupancyContext runtimeGroundPoseOccupancyQueryContext;
class RuntimeGroundPoseOccupancyQueryScope {
    RuntimeGroundPoseOccupancyContext previous_;
public:
    explicit RuntimeGroundPoseOccupancyQueryScope(RuntimeGroundPoseOccupancyContext context)
        : previous_(runtimeGroundPoseOccupancyQueryContext) {
        runtimeGroundPoseOccupancyQueryContext=context;
    }
    ~RuntimeGroundPoseOccupancyQueryScope() { runtimeGroundPoseOccupancyQueryContext=previous_; }
    RuntimeGroundPoseOccupancyQueryScope(const RuntimeGroundPoseOccupancyQueryScope&)=delete;
    RuntimeGroundPoseOccupancyQueryScope& operator=(const RuntimeGroundPoseOccupancyQueryScope&)=delete;
};
inline bool runtimeGroundPoseUsesActiveOccupancy(const WorldObjectInstance& object,
    const RuntimeGroundPoseOccupancyContext& context) {
    return object.renderMoveActive && worldObjectHasGroundRenderPose(object)
        && context.holdsActivePose && context.holdsActivePose(context.state,object.instanceId);
}

struct RuntimeGroundPoseOccupancy {
    int x = -1, y = -1, subCell = -1;
    bool onBridge = false;
};

inline bool runtimeGroundPoseCanContributeOccupancy(const WorldObjectInstance& object,
    const RuntimeGroundPoseOccupancyContext* queryContext=nullptr) {
    if (!worldObjectHasGroundRenderPose(object) || !object.alive || object.health <= 0
        || (object.kind != WorldObjectKind::Unit && object.kind != WorldObjectKind::Infantry)
        || object.authoredAircraft || object.tankBunkerHostId >= 0
        || !std::isfinite(object.renderX) || !std::isfinite(object.renderY)) return false;
    const auto& context=queryContext ? *queryContext : runtimeGroundPoseOccupancyQueryContext;
    return !object.renderMoveActive || runtimeGroundPoseUsesActiveOccupancy(object,context);
}

inline std::optional<RuntimeGroundPoseOccupancy> runtimeAdditionalGroundPoseOccupancy(
    const WorldObjectInstance& object, const RuntimeGroundPoseOccupancyContext* queryContext=nullptr) {
    if (!runtimeGroundPoseCanContributeOccupancy(object,queryContext)) return std::nullopt;
    const auto offset = object.kind==WorldObjectKind::Infantry
        ? worldInfantrySubCellOffset(object.subCell) : WorldObjectCellOffset{};
    const double bodyX = object.renderX + offset.x, bodyY = object.renderY + offset.y;
    const double roundedX = std::floor(bodyX + 0.5), roundedY = std::floor(bodyY + 0.5);
    if (roundedX < 0.0 || roundedY < 0.0
        || roundedX > std::numeric_limits<int>::max()
        || roundedY > std::numeric_limits<int>::max()) return std::nullopt;
    RuntimeGroundPoseOccupancy pose;
    pose.x = static_cast<int>(roundedX); pose.y = static_cast<int>(roundedY);
    pose.onBridge = object.renderOnBridge;
    if (pose.x == object.x && pose.y == object.y && pose.onBridge == object.onBridge)
        return std::nullopt;
    if (object.kind == WorldObjectKind::Infantry) {
        if (object.subCell != 2 && object.subCell != 4 && object.subCell != 3) return std::nullopt;
        const double dx = bodyX - pose.x, dy = bodyY - pose.y;
        double best = std::numeric_limits<double>::max();
        for (int slot : {2, 4, 3}) {
            const auto candidate=worldInfantrySubCellOffset(slot);
            const double sx=candidate.x,sy=candidate.y;
            const double distance = (dx-sx)*(dx-sx) + (dy-sy)*(dy-sy);
            if (distance < best) { best=distance; pose.subCell=slot; }
        }
    }
    return pose;
}

// A stopped vehicle has a full-cell body between two navigation centers.
// Hold all touched neighboring cells, even after its center passes halfway.
// This fixed maximum-four visitor allocates nothing; infantry still hold one
// physical subcell and ordinary active steps keep their existing departure rule.
template<class Visitor>
inline void forEachRuntimeAdditionalGroundPoseOccupancy(const WorldObjectInstance& object,
    Visitor visit, const RuntimeGroundPoseOccupancyContext* queryContext=nullptr) {
    if (object.kind!=WorldObjectKind::Unit) {
        if (const auto held=runtimeAdditionalGroundPoseOccupancy(object,queryContext)) visit(*held);
        return;
    }
    if (!runtimeGroundPoseCanContributeOccupancy(object,queryContext)) return;
    const auto snapped=[](double value) {
        const double integer=std::round(value);
        return std::abs(value-integer)<=1e-9 ? integer : value;
    };
    const double x=snapped(object.renderX),y=snapped(object.renderY);
    const double left=std::floor(x),right=std::ceil(x),top=std::floor(y),bottom=std::ceil(y);
    if (left<0.0 || top<0.0 || right>std::numeric_limits<int>::max()
        || bottom>std::numeric_limits<int>::max()) return;
    const int x0=static_cast<int>(left),y0=static_cast<int>(top);
    const int width=static_cast<int>(right-left),height=static_cast<int>(bottom-top);
    for (int dy=0;dy<=height;++dy) for (int dx=0;dx<=width;++dx) {
        const int column=x0+dx,row=y0+dy;
        if (column==object.x && row==object.y && object.renderOnBridge==object.onBridge) continue;
        visit(RuntimeGroundPoseOccupancy{column,row,-1,object.renderOnBridge});
    }
}
