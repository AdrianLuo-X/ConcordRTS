#pragma once

#include "GameWorld.h"
#include "RuntimeRendererEnvironment.h"

#include <algorithm>
#include <optional>

inline thread_local std::optional<bool> runtimeEmptyBuildingDepthReferenceOverride;

class RuntimeEmptyBuildingDepthReferenceScope {
public:
    explicit RuntimeEmptyBuildingDepthReferenceScope(bool enabled)
        : previous_(runtimeEmptyBuildingDepthReferenceOverride) {
        runtimeEmptyBuildingDepthReferenceOverride = enabled;
    }
    ~RuntimeEmptyBuildingDepthReferenceScope() { runtimeEmptyBuildingDepthReferenceOverride = previous_; }
    RuntimeEmptyBuildingDepthReferenceScope(const RuntimeEmptyBuildingDepthReferenceScope&) = delete;
    RuntimeEmptyBuildingDepthReferenceScope& operator=(const RuntimeEmptyBuildingDepthReferenceScope&) = delete;
private:
    std::optional<bool> previous_;
};

inline bool runtimeEmptyBuildingDepthReferenceForRender() {
    if (runtimeEmptyBuildingDepthReferenceOverride) return *runtimeEmptyBuildingDepthReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_EMPTY_BUILDING_DEPTH_REFERENCE");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

inline bool runtimeSkipEmptyBuildingDepthScene(const GameWorld& world) {
    if (runtimeEmptyBuildingDepthReferenceForRender()) return false;
    // This intentionally includes dead, invisible, offscreen, selling and
    // missing-resource structures. Their complete established route is kept;
    // only a world containing no structure of any kind is known to be empty.
    return std::none_of(world.getObjects().begin(), world.getObjects().end(),
        [](const WorldObjectInstance& object) { return object.kind == WorldObjectKind::Structure; });
}
