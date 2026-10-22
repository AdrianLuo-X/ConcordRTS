#include "ui/input/RuntimeMapRightPointerController.h"

#include <algorithm>
#include <cstdlib>

bool handleRuntimeMapRightPointerDown(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host) {
    const RuntimeMapRightPointerInput owned = input;
    if (!host.worldLoaded()) return false;
    if (host.menuOpen()) return true;

    const auto client = host.clientRect();
    const auto point = owned.point;
    host.setMouseClientPointValid(true);
    host.setMouseClientPoint(point);
    const auto sidebar = host.sidebarRect(client);
    const auto viewport = host.mapViewport(client);
    if (!runtimeInputPixelPointInRectInclusive(viewport, point)
        && host.cancelActiveSuperWeaponTargetSelection("right-click")) {
        host.requestRepaint();
        host.redrawWindow();
        return true;
    }
    if (runtimeInputPixelPointInRectInclusive(sidebar, point)) {
        const auto buttonIndex = host.buildButtonAtPoint(sidebar, point);
        if (buttonIndex.has_value()) {
            host.dispatchBuildButtonSecondary(*buttonIndex);
            host.requestRepaint();
            host.redrawWindow();
            return true;
        }
        if (host.sidebarToolActive()) {
            host.clearSidebarTool();
            host.setInteractionStatus("sidebar tool canceled");
            host.requestRepaint();
            host.redrawWindow();
            return true;
        }
        return true;
    }
    if (!runtimeInputPixelPointInRectInclusive(viewport, point)) return true;

    host.focusWindow();
    host.captureWindow();
    host.gestureState().active = true;
    host.gestureState().moved = false;
    host.gestureState().start = point;
    host.gestureState().last = point;
    host.gestureState().cursorDelta = {0, 0};
    host.setInteractionStatus("right drag pan");
    host.requestRepaint();
    return true;
}

bool panRuntimeMapRightPointerByDelta(
    std::int32_t dx, std::int32_t dy, RuntimeMapRightPointerGestureHost& host) {
    // Keep the original signed arithmetic domain; no new overflow policy.
    if ((dx * dx) + (dy * dy) <= 16) return false;
    const auto panX = std::clamp(dx, -96, 96);
    const auto panY = std::clamp(dy, -96, 96);
    if (panX == 0 && panY == 0) return false;
    if (!host.scrollCameraClamped(panX, panY)) {
        host.setInteractionStatus("map edge");
        return false;
    }
    host.invalidateProjection();
    host.setInteractionStatus("right drag pan");
    return true;
}

std::int32_t runtimeMapRightPointerPanStep(std::int32_t delta) {
    const auto magnitude = std::abs(delta);
    if (magnitude <= 4) return 0;
    const auto step = std::clamp(magnitude / 4, 1, 32);
    return delta < 0 ? -step : step;
}

bool handleRuntimeMapRightPointerMove(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host) {
    const RuntimeMapRightPointerInput owned = input;
    if (!host.gestureState().active) return false;
    const auto point = owned.point;
    const auto previousPoint = host.gestureState().last;
    const auto totalDx = point.x - host.gestureState().start.x;
    const auto totalDy = point.y - host.gestureState().start.y;
    if ((totalDx * totalDx) + (totalDy * totalDy) > 16)
        host.gestureState().moved = true;
    if (point.x != previousPoint.x || point.y != previousPoint.y)
        host.gestureState().cursorDelta = {
            point.x - previousPoint.x, point.y - previousPoint.y};
    host.gestureState().last = point;
    if (!host.scrollCoastingEnabled() && host.gestureState().moved)
        (void)panRuntimeMapRightPointerByDelta(
            point.x - previousPoint.x, point.y - previousPoint.y, host);
    host.requestRepaint();
    return true;
}

bool advanceRuntimeMapRightPointerTick(RuntimeMapRightPointerGestureHost& host) {
    if (!host.scrollCoastingEnabled()
        || !host.gestureState().active
        || !host.gestureState().moved) return false;
    const auto dx = host.gestureState().last.x - host.gestureState().start.x;
    const auto dy = host.gestureState().last.y - host.gestureState().start.y;
    const auto rateScale = std::clamp(host.scrollRate(), 0, 6) + 3;
    const auto panX = runtimeMapRightPointerPanStep(dx) * rateScale / 6;
    const auto panY = runtimeMapRightPointerPanStep(dy) * rateScale / 6;
    if (!panRuntimeMapRightPointerByDelta(panX, panY, host)) return false;
    return true;
}

bool handleRuntimeMapRightPointerCompletedClick(
    const RuntimeMapRightPointerInput& input, RuntimeInputPixelRect client,
    RuntimeMapRightPointerClickHost& host) {
    const RuntimeMapRightPointerInput owned = input;
    if (host.hasSelectedBeacon()) {
        host.clearSelectedBeacon();
        host.requestRepaint();
        return true;
    }
    if (host.followTargeting() || host.rallyTargeting() || host.beaconTargeting()) {
        host.setBeaconTargeting(false);
        host.setFollowTargeting(false);
        host.setRallyTargeting(false);
        return true;
    }
    if (host.cancelActiveSuperWeaponTargetSelection("right-click")) return true;
    const auto viewport = host.mapViewport(client);
    if (!runtimeInputPixelPointInRectInclusive(viewport, owned.point)) return false;
    if (host.dismissPlacement()) return true;
    if (host.planning()) {
        host.setSelectedWaypoint(0);
        host.cancelInteraction();
        return true;
    }
    if (host.sidebarToolActive()) {
        host.clearSidebarTool();
        host.setInteractionStatus("sidebar tool canceled");
        return true;
    }
    if (host.selectionEmpty()) {
        host.cancelInteraction();
        return true;
    }
    const auto projectedObjectId = host.projectedObjectIdAtPoint(viewport, owned.point);
    // This original helper performs its own second projected-object query.
    const auto wallTarget = host.visibleTerrainWallAtPoint(viewport, owned.point);
    if (wallTarget) {
        if (host.dispatchPrimaryWall(*wallTarget)) return true;
    }
    if (projectedObjectId && !host.selectionContains(*projectedObjectId)) {
        const auto cell = host.surfaceCellAtPoint(viewport, owned.point);
        if (host.dispatchPrimaryObject(cell, *projectedObjectId)) return true;
    }
    host.cancelInteraction();
    return true;
}

bool handleRuntimeMapRightPointerUp(
    const RuntimeMapRightPointerInput& input, RuntimeMapRightPointerGestureHost& host,
    RuntimeMapRightPointerClickHost& clickHost) {
    const RuntimeMapRightPointerInput owned = input;
    if (!host.worldLoaded()) return false;
    if (host.menuOpen()) return true;

    const bool wasRightDrag = host.gestureState().active;
    const bool wasRightDragMove = host.gestureState().moved;
    host.gestureState().active = false;
    host.gestureState().moved = false;
    if (host.ownsCapture()) host.releaseCapture();

    host.setMouseClientPointValid(true);
    host.setMouseClientPoint(owned.point);
    if (!wasRightDrag) return true;
    if (wasRightDragMove) {
        host.setInteractionStatus("camera panned");
        host.requestRepaint();
        host.redrawWindow();
        return true;
    }
    const auto client = host.clientRect();
    (void)handleRuntimeMapRightPointerCompletedClick(owned, client, clickHost);
    host.requestRepaint();
    host.redrawWindow();
    return true;
}

void cancelRuntimeMapRightPointer(
    RuntimeMapRightPointerCancelEvent event, RuntimeMapRightPointerGestureHost& host) {
    const auto ownedEvent = event;
    if (!host.gestureState().active
        || !(ownedEvent == RuntimeMapRightPointerCancelEvent::FocusLost
            || ownedEvent == RuntimeMapRightPointerCancelEvent::CancelMode
            || ownedEvent == RuntimeMapRightPointerCancelEvent::ApplicationDeactivated)) return;
    host.gestureState().active = false;
    host.gestureState().moved = false;
    host.requestRepaint();
    if (host.ownsCapture()) host.releaseCapture();
}

void captureChangedRuntimeMapRightPointer(RuntimeMapRightPointerGestureHost& host) {
    if (host.gestureState().active) {
        host.gestureState().active = false;
        host.gestureState().moved = false;
        host.requestRepaint();
    }
}
