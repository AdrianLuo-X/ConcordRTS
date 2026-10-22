#pragma once

#include <cstdint>
#include <string>

struct RuntimeTeamKeyCell;

struct RuntimeCameraKeyInput {
    std::uint64_t rawKey = 0;
    bool repeated = false;
};

// Queries and effects complete synchronously against the current host root.
// prepareCenterClient owns the native viewport value privately. The final
// start cell is copied only after that query and is never retained by this API.
class RuntimeCameraKeyHost {
public:
    virtual ~RuntimeCameraKeyHost() = default;
    virtual std::int32_t runtimeScrollRate() = 0;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual void clearCameraFollow() = 0;
    virtual std::int32_t sampleCurrentKeyCode(const RuntimeCameraKeyInput& input) = 0;
    virtual bool visionEnabled() = 0;
    virtual bool skirmishStartApplied() = 0;
    virtual std::int32_t skirmishStartCellX() = 0;
    virtual std::int32_t skirmishStartCellY() = 0;
    virtual void prepareCenterClient() = 0;
    virtual RuntimeTeamKeyCell skirmishStartCell() = 0;
    virtual void centerCamera(RuntimeTeamKeyCell cell) = 0;
    virtual void resetCameraInitialized() = 0;
    virtual void scrollCamera(std::int32_t dx, std::int32_t dy) = 0;
    virtual void invalidateProjection() = 0;
};

// The original scroll policy clamps the complete int32 rate domain before
// multiplying. Existing native camera/geometry overflow domains stay unchanged.
// Repeated input takes the same path. False preserves the caller's DefWindowProc;
// true preserves its existing repaint/RedrawWindow epilogue at the same position.
bool handleRuntimeCameraKey(const RuntimeCameraKeyInput& input,
    RuntimeCameraKeyHost& host);
