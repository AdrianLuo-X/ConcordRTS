#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <memory>

class GameWorld;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing immutable geometry lease. World/context/camera/source snapshots must
// remain unchanged until exit; original cache/borrow/nesting rules still apply.
// The original implementation owns its frame and stores viewport values.
class RuntimeGeometryQueryScope {
public:
    RuntimeGeometryQueryScope(const GameWorld& world, const RuntimeInputPixelRect& viewport,
        const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
        RuntimeMapRenderCache& cache, bool viewportStructureGeometryOnly = false);
    ~RuntimeGeometryQueryScope();
    RuntimeGeometryQueryScope(const RuntimeGeometryQueryScope&) = delete;
    RuntimeGeometryQueryScope& operator=(const RuntimeGeometryQueryScope&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
