#pragma once

struct WorldObjectInstance;

// This is the pose at one synchronous commit boundary, not a world borrow.
// Render coordinates remain owned by the object even when their flags clear.
struct RuntimeChronospherePose {
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    int subCell = -1;
    bool onBridge = false;
    bool airborne = false;
    bool hasRenderPosition = false;
    bool renderMoveActive = false;
    bool renderOnBridge = false;
    double renderX = 0.0;
    double renderY = 0.0;
    double renderLevel = 0.0;
    bool operator==(const RuntimeChronospherePose&) const = default;
};

enum class RuntimeChronosphereArrival { Formation, FatalWater };

struct RuntimeChronosphereRelocationRequest {
    int objectId = -1;
    // Preserve the source captured before the caller's admission queries.
    int oldCellX = -1;
    int oldCellY = -1;
    int targetX = -1;
    int targetY = -1;
    // The existing caller owns the y*1000+x world-cell encoding.
    int targetCell = -1;
    RuntimeChronosphereArrival arrival = RuntimeChronosphereArrival::Formation;
    bool operator==(const RuntimeChronosphereRelocationRequest&) const = default;
};

struct RuntimeChronosphereRelocationResult {
    bool applied = false;
    RuntimeChronosphereRelocationRequest request;
    RuntimeChronospherePose oldPose;
    RuntimeChronospherePose newPose;
    int healthAtNotification = 0;
    bool operator==(const RuntimeChronosphereRelocationResult&) const = default;
};

class RuntimeChronosphereRelocationPort {
public:
    virtual ~RuntimeChronosphereRelocationPort() = default;
    virtual bool hasTerrainLevel() const noexcept = 0;
    virtual int terrainLevel(int x, int y) = 0;
    virtual void onRelocated(const RuntimeChronosphereRelocationResult&) = 0;
};

// The caller retains the original legality/occupancy/alive/organic policy.
// Null is a no-op; same-cell arrivals still commit and notify exactly once.
// Borrow object and port only for this call. Callbacks may query or nest a
// commit, but must not replace/reallocate the borrowed object storage.
// Keep the original partial-effect order: x/y/cell, optional terrain query,
// bridge/render/mission, then notification. Exceptions do not roll back the
// original writes. The returned owned pose is captured before notification;
// it remains usable after a later storage rebuild, never as a live pointer.
RuntimeChronosphereRelocationResult commitRuntimeChronosphereRelocation(
    WorldObjectInstance* object,
    const RuntimeChronosphereRelocationRequest& request,
    RuntimeChronosphereRelocationPort& port);
