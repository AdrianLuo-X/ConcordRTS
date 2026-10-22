#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <cstdint>
#include <optional>
#include <string>

// The single gesture value stored by the runtime. No secondary shadow state.
struct RuntimeMapRightPointerState {
    bool active = false;
    bool moved = false;
    RuntimeInputPixelPoint start{};
    RuntimeInputPixelPoint last{};
    RuntimeInputPixelPoint cursorDelta{};
};

struct RuntimeMapRightPointerInput {
    RuntimeInputPixelPoint point{};
};

struct RuntimeMapRightPointerCell {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// Only fields read by the existing wall-target dispatch/revalidation boundary.
struct RuntimeMapRightPointerWallTarget {
    std::int32_t objectId = 0;
    RuntimeMapRightPointerCell cell{};
    std::string type;
};

enum class RuntimeMapRightPointerCancelEvent : std::uint8_t {
    Other,
    FocusLost,
    CancelMode,
    ApplicationDeactivated
};

class RuntimeMapRightPointerGestureHost {
public:
    virtual ~RuntimeMapRightPointerGestureHost() = default;
    // A borrow for the immediate expression only; reacquire after external calls.
    virtual RuntimeMapRightPointerState& gestureState() = 0;
    virtual bool worldLoaded() = 0;
    virtual bool menuOpen() = 0;
    virtual RuntimeInputPixelRect clientRect() = 0;
    virtual RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) = 0;
    virtual RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) = 0;
    virtual void setMouseClientPointValid(bool valid) = 0;
    virtual void setMouseClientPoint(RuntimeInputPixelPoint point) = 0;
    virtual bool cancelActiveSuperWeaponTargetSelection(const std::string& reason) = 0;
    virtual std::optional<std::int32_t> buildButtonAtPoint(
        RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual void dispatchBuildButtonSecondary(std::int32_t index) = 0;
    virtual bool sidebarToolActive() = 0;
    virtual void clearSidebarTool() = 0;
    virtual void focusWindow() = 0;
    virtual void captureWindow() = 0;
    virtual bool ownsCapture() = 0;
    virtual void releaseCapture() = 0;
    virtual bool scrollCoastingEnabled() = 0;
    virtual std::int32_t scrollRate() = 0;
    virtual bool scrollCameraClamped(std::int32_t dx, std::int32_t dy) = 0;
    virtual void invalidateProjection() = 0;
    virtual void setInteractionStatus(const std::string& status) = 0;
    virtual void requestRepaint() = 0;
    virtual void redrawWindow() = 0;
};

class RuntimeMapRightPointerClickHost {
public:
    virtual ~RuntimeMapRightPointerClickHost() = default;
    virtual bool hasSelectedBeacon() = 0;
    virtual void clearSelectedBeacon() = 0;
    virtual bool followTargeting() = 0;
    virtual bool rallyTargeting() = 0;
    virtual bool beaconTargeting() = 0;
    virtual void setBeaconTargeting(bool value) = 0;
    virtual void setFollowTargeting(bool value) = 0;
    virtual void setRallyTargeting(bool value) = 0;
    virtual bool cancelActiveSuperWeaponTargetSelection(const std::string& reason) = 0;
    virtual RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) = 0;
    virtual bool dismissPlacement() = 0;
    virtual bool planning() = 0;
    virtual void setSelectedWaypoint(std::uint64_t id) = 0;
    virtual bool sidebarToolActive() = 0;
    virtual void clearSidebarTool() = 0;
    virtual bool selectionEmpty() = 0;
    virtual std::optional<std::int32_t> projectedObjectIdAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual std::optional<RuntimeMapRightPointerWallTarget> visibleTerrainWallAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual bool selectionContains(std::int32_t objectId) = 0;
    virtual RuntimeMapRightPointerCell surfaceCellAtPoint(
        RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual bool dispatchPrimaryWall(const RuntimeMapRightPointerWallTarget& target) = 0;
    virtual bool dispatchPrimaryObject(
        RuntimeMapRightPointerCell cell, std::int32_t objectId) = 0;
    virtual void cancelInteraction() = 0;
    virtual void setInteractionStatus(const std::string& status) = 0;
    virtual void requestRepaint() = 0;
};

// Down/up false means preserve the native unloaded-world fallback. Move is only
// the original right branch, after the existing common pointer/menu prefix.
bool handleRuntimeMapRightPointerDown(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host);
bool handleRuntimeMapRightPointerMove(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host);
bool handleRuntimeMapRightPointerUp(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host,
    RuntimeMapRightPointerClickHost& clickHost);
bool handleRuntimeMapRightPointerCompletedClick(
    const RuntimeMapRightPointerInput& input, RuntimeInputPixelRect client,
    RuntimeMapRightPointerClickHost& host);
bool panRuntimeMapRightPointerByDelta(
    std::int32_t dx, std::int32_t dy, RuntimeMapRightPointerGestureHost& host);
std::int32_t runtimeMapRightPointerPanStep(std::int32_t delta);
bool advanceRuntimeMapRightPointerTick(RuntimeMapRightPointerGestureHost& host);
// These update only the right gesture; callers keep other cancel/menu owners.
void cancelRuntimeMapRightPointer(
    RuntimeMapRightPointerCancelEvent event, RuntimeMapRightPointerGestureHost& host);
void captureChangedRuntimeMapRightPointer(RuntimeMapRightPointerGestureHost& host);
