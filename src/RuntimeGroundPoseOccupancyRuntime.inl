#include "RuntimeGroundPoseOccupancy.h"

RuntimeGroundPoseOccupancyContext runtimeGroundPoseContext(const RuntimeWindowState& state) {
    return {&state, [](const void* opaque,int id) {
        const auto& state=*static_cast<const RuntimeWindowState*>(opaque);
        const auto found=state.behaviors.find(id);
        if (found==state.behaviors.end()) return false;
        const auto& behavior=found->second;
        return behavior.renderFromOffsetX!=0.0 || behavior.renderFromOffsetY!=0.0
            || behavior.renderToOffsetX!=0.0 || behavior.renderToOffsetY!=0.0;
    }};
}
