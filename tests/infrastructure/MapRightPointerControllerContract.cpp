#include "ui/input/RuntimeMapRightPointerController.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_WINUSER_)
#error The right-pointer contract must not include a Windows header.
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Point = RuntimeInputPixelPoint;
using Rect = RuntimeInputPixelRect;
using Cell = RuntimeMapRightPointerCell;
using Wall = RuntimeMapRightPointerWallTarget;
using Gesture = RuntimeMapRightPointerState;
using Input = RuntimeMapRightPointerInput;
using Cancel = RuntimeMapRightPointerCancelEvent;

std::string value(bool v) { return v ? "1" : "0"; }
std::string value(std::int32_t v) { return std::to_string(v); }
std::string value(std::uint64_t v) { return std::to_string(v); }
std::string value(const std::string& v) {
    return std::to_string(v.size()) + ":" + v;
}
std::string value(Point v) { return std::to_string(v.x) + "," + std::to_string(v.y); }
std::string value(Rect v) {
    return std::to_string(v.left) + "," + std::to_string(v.top) + ","
        + std::to_string(v.right) + "," + std::to_string(v.bottom);
}
std::string value(Cell v) { return std::to_string(v.x) + "," + std::to_string(v.y); }
std::string value(const Wall& v) {
    return std::to_string(v.objectId) + "/" + value(v.cell) + "/" + value(v.type);
}
std::string value(const std::optional<std::int32_t>& v) {
    return v ? std::to_string(*v) : "null";
}
std::string value(const std::optional<Wall>& v) { return v ? value(*v) : "null"; }
std::string args(Rect r, Point p) { return value(r) + ";" + value(p); }

struct State {
    Gesture gesture{};
    bool loaded = true, menu = false, mouseValid = false;
    Point mouse{7, 9};
    Rect client{0, 0, 640, 480}, sidebar{500, 0, 639, 479}, viewport{0, 0, 499, 479};
    bool weaponTarget = false;
    std::optional<std::int32_t> button;
    bool tool = false, captured = false, coast = false, scrollBlocked = false;
    std::int32_t rate = 3, cameraX = 0, cameraY = 0;
    bool selectedBeacon = false, follow = false, rally = false, beacon = false;
    bool placement = false, planning = false;
    std::uint64_t waypoint = 71;
    std::vector<std::int32_t> selection{10};
    std::optional<std::int32_t> projected = 20;
    std::optional<Wall> wall;
    Cell surface{31, 47};
    bool wallAccepted = true, objectAccepted = true;
    std::string status = "unchanged";
    bool repaint = false;
    std::int32_t repaintRequests = 0, redraws = 0, focusCalls = 0, captureCalls = 0;
    std::int32_t releases = 0, invalidations = 0, cancels = 0, placementDismissals = 0;
    std::int32_t callbacks = 0;
    bool callbackSawActive = false;
    std::vector<std::string> attempts, commands;
    std::uint64_t tick = 21, worldHash = 0x1122334455667788ULL;

    std::string snapshot() const {
        std::ostringstream out;
        out << value(gesture.active) << value(gesture.moved) << "/" << value(gesture.start)
            << "/" << value(gesture.last) << "/" << value(gesture.cursorDelta)
            << "|" << loaded << menu << mouseValid << "/" << value(mouse)
            << "|" << value(client) << "/" << value(sidebar) << "/" << value(viewport)
            << "|" << weaponTarget << "/" << value(button) << "/" << tool << captured
            << coast << scrollBlocked << "/" << rate << "/" << cameraX << "," << cameraY
            << "|" << selectedBeacon << follow << rally << beacon << placement << planning
            << "/" << waypoint << "/[";
        for (const auto id : selection) out << id << ",";
        out << "]/" << value(projected) << "/" << value(wall) << "/" << value(surface)
            << "/" << wallAccepted << objectAccepted << "|" << value(status) << "/"
            << repaint << "/" << repaintRequests << "," << redraws << "," << focusCalls
            << "," << captureCalls << "," << releases << "," << invalidations << ","
            << cancels << "," << placementDismissals << "," << callbacks << ","
            << callbackSawActive << "|A[";
        for (const auto& a : attempts) out << value(a) << ";";
        out << "]C[";
        for (const auto& c : commands) out << value(c) << ";";
        out << "]|" << tick << "/" << worldHash;
        return out.str();
    }
};

struct Row {
    std::string operation, parameters, result, before, after;
    std::int32_t depth = 0;
    bool threw = false;
    bool operator==(const Row&) const = default;
};

struct Injected final : std::runtime_error {
    explicit Injected(const std::string& text) : std::runtime_error(text) {}
};

class Host final : public RuntimeMapRightPointerGestureHost,
                   public RuntimeMapRightPointerClickHost {
public:
    State state;
    std::vector<Row> trace;
    std::function<void(const std::string&)> afterCall;
    std::int32_t throwAt = 0, ordinal = 0, depth = 0;

    explicit Host(State initial) : state(std::move(initial)) {}

    template<class Function>
    auto call(const std::string& name, const std::string& parameters, Function effect) {
        const auto index = trace.size();
        trace.push_back({name, parameters, "", state.snapshot(), "", depth, false});
        ++depth;
        try {
            ++ordinal;
            if (throwAt != 0 && ordinal == throwAt) throw Injected("primitive:" + name);
            auto result = effect();
            if (afterCall) afterCall(name);
            trace[index].result = value(result);
            trace[index].after = state.snapshot();
            --depth;
            return result;
        } catch (...) {
            trace[index].threw = true;
            trace[index].after = state.snapshot();
            --depth;
            throw;
        }
    }
    template<class Function>
    void effect(const std::string& name, const std::string& parameters, Function action) {
        (void)call(name, parameters, [&] { action(); return std::string("void"); });
    }
    Gesture& gestureState() override {
        effect("state", "", [] {});
        return state.gesture;
    }
    bool worldLoaded() override { return call("loaded", "", [&] { return state.loaded; }); }
    bool menuOpen() override { return call("menu", "", [&] { return state.menu; }); }
    Rect clientRect() override { return call("client", "", [&] { return state.client; }); }
    Rect sidebarRect(Rect client) override {
        return call("sidebar", value(client), [&] { return state.sidebar; });
    }
    Rect mapViewport(Rect client) override {
        return call("viewport", value(client), [&] { return state.viewport; });
    }
    void setMouseClientPointValid(bool v) override {
        effect("mouse-valid", value(v), [&] { state.mouseValid = v; });
    }
    void setMouseClientPoint(Point p) override {
        effect("mouse", value(p), [&] { state.mouse = p; });
    }
    bool cancelActiveSuperWeaponTargetSelection(const std::string& reason) override {
        return call("weapon-cancel", value(reason), [&] {
            const auto active = state.weaponTarget;
            state.weaponTarget = false;
            return active;
        });
    }
    std::optional<std::int32_t> buildButtonAtPoint(Rect sidebar, Point point) override {
        return call("button", args(sidebar, point), [&] { return state.button; });
    }
    void dispatchBuildButtonSecondary(std::int32_t index) override {
        effect("secondary", value(index), [&] {
            state.attempts.push_back("secondary:" + value(index));
            state.commands.push_back("secondary:" + value(index));
        });
    }
    bool sidebarToolActive() override { return call("tool", "", [&] { return state.tool; }); }
    void clearSidebarTool() override { effect("clear-tool", "", [&] { state.tool = false; }); }
    void focusWindow() override { effect("focus", "", [&] { ++state.focusCalls; }); }
    void captureWindow() override {
        effect("capture", "", [&] { state.captured = true; ++state.captureCalls; });
    }
    bool ownsCapture() override { return call("owns-capture", "", [&] { return state.captured; }); }
    void releaseCapture() override {
        effect("release", "", [&] { state.captured = false; ++state.releases; });
    }
    bool scrollCoastingEnabled() override { return call("coast", "", [&] { return state.coast; }); }
    std::int32_t scrollRate() override { return call("rate", "", [&] { return state.rate; }); }
    bool scrollCameraClamped(std::int32_t dx, std::int32_t dy) override {
        return call("scroll", value(dx) + "," + value(dy), [&] {
            if (state.scrollBlocked) return false;
            const auto x = std::clamp(state.cameraX + dx, -64, 64);
            const auto y = std::clamp(state.cameraY + dy, -64, 64);
            const bool changed = x != state.cameraX || y != state.cameraY;
            state.cameraX = x; state.cameraY = y;
            return changed;
        });
    }
    void invalidateProjection() override {
        effect("invalidate", "", [&] { ++state.invalidations; });
    }
    void setInteractionStatus(const std::string& text) override {
        effect("status", value(text), [&] { state.status = text; });
    }
    void requestRepaint() override {
        effect("repaint", "", [&] { state.repaint = true; ++state.repaintRequests; });
    }
    void redrawWindow() override { effect("redraw", "", [&] { ++state.redraws; }); }
    bool hasSelectedBeacon() override {
        return call("selected-beacon", "", [&] { return state.selectedBeacon; });
    }
    void clearSelectedBeacon() override {
        effect("clear-beacon", "", [&] { state.selectedBeacon = false; });
    }
    bool followTargeting() override { return call("follow", "", [&] { return state.follow; }); }
    bool rallyTargeting() override { return call("rally", "", [&] { return state.rally; }); }
    bool beaconTargeting() override { return call("beacon", "", [&] { return state.beacon; }); }
    void setBeaconTargeting(bool v) override { effect("set-beacon", value(v), [&] { state.beacon = v; }); }
    void setFollowTargeting(bool v) override { effect("set-follow", value(v), [&] { state.follow = v; }); }
    void setRallyTargeting(bool v) override { effect("set-rally", value(v), [&] { state.rally = v; }); }
    bool dismissPlacement() override {
        return call("placement", "", [&] {
            const auto active = state.placement;
            if (active) { state.placement = false; ++state.placementDismissals; }
            return active;
        });
    }
    bool planning() override { return call("planning", "", [&] { return state.planning; }); }
    void setSelectedWaypoint(std::uint64_t id) override {
        effect("waypoint", value(id), [&] { state.waypoint = id; });
    }
    bool selectionEmpty() override { return call("selection-empty", "", [&] { return state.selection.empty(); }); }
    std::optional<std::int32_t> projectedObjectIdAtPoint(Rect viewport, Point point) override {
        return call("projected", args(viewport, point), [&] { return state.projected; });
    }
    std::optional<Wall> visibleTerrainWallAtPoint(Rect viewport, Point point) override {
        return call("wall", args(viewport, point), [&] { return state.wall; });
    }
    bool selectionContains(std::int32_t id) override {
        return call("selected", value(id), [&] {
            return std::find(state.selection.begin(), state.selection.end(), id) != state.selection.end();
        });
    }
    Cell surfaceCellAtPoint(Rect viewport, Point point) override {
        return call("surface", args(viewport, point), [&] { return state.surface; });
    }
    bool dispatchPrimaryWall(const Wall& target) override {
        return call("dispatch-wall", value(target), [&] {
            const auto command = "wall:" + value(target);
            state.attempts.push_back(command);
            if (state.wallAccepted) state.commands.push_back(command);
            return state.wallAccepted;
        });
    }
    bool dispatchPrimaryObject(Cell cell, std::int32_t id) override {
        return call("dispatch-object", value(cell) + ";" + value(id), [&] {
            const auto command = "object:" + value(cell) + ":" + value(id);
            state.attempts.push_back(command);
            if (state.objectAccepted) state.commands.push_back(command);
            return state.objectAccepted;
        });
    }
    void cancelInteraction() override {
        effect("cancel", "", [&] {
            ++state.cancels;
            state.selection.clear();
            state.status = "selection canceled";
        });
    }
};

// The following seven complete old blocks come from the frozen pre-change
// RuntimeShell. The source map records each finite type/field/primitive-port
// substitution and reverses every compiled block to its independent old bytes.
// These adapters contain no migrated branch, priority or pan policy.
class OldOracle {
    using POINT = Point;
    using RECT = Rect;
    using RuntimeCellPoint = Cell;
    using WPARAM = std::uint64_t;
    using LPARAM = Point;
    static constexpr std::uint32_t WM_RBUTTONDOWN = 1, WM_RBUTTONUP = 2;
    static constexpr std::uint32_t WM_KILLFOCUS = 3, WM_CANCELMODE = 4, WM_ACTIVATEAPP = 5;
    enum { GWLP_USERDATA = 0, RDW_INVALIDATE = 1, RDW_NOERASE = 2, FALSE = 0 };
    enum class RuntimeMultiplayerGameplayTargetOrderKind { Primary };
    struct WorldObjectInstance {
        std::int32_t instanceId = 0, x = 0, y = 0;
        std::string type;
        bool wall = false;
    };
    struct RuntimeProjectedObject { const WorldObjectInstance* object = nullptr; };
    Host& host;
    Rect borrowedViewport{};
    WorldObjectInstance borrowedObject{};
    RuntimeProjectedObject borrowedProjection{&borrowedObject};
    std::vector<RuntimeProjectedObject> borrowedVector;

    std::intptr_t GetWindowLongPtrW(int, int) { return reinterpret_cast<std::intptr_t>(&host); }
    std::intptr_t DefWindowProcW(int, std::uint32_t, WPARAM, LPARAM) { return 1; }
    void GetClientRect(int, RECT* out) { *out = host.clientRect(); }
    POINT pointFromMouseMessage(LPARAM point) { return point; }
    RECT runtimeSidebarRect(RECT client) { return host.sidebarRect(client); }
    RECT runtimeMapViewport(RECT client) { return host.mapViewport(client); }
    static bool pointInRectInclusive(RECT r, POINT p) {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    }
    static bool cancelRuntimeActiveSuperWeaponTargetSelection(Host& state, const std::string& reason) {
        return state.cancelActiveSuperWeaponTargetSelection(reason);
    }
    static std::optional<int> runtimeBuildButtonAtPoint(Host& state, RECT r, POINT p) {
        return state.buildButtonAtPoint(r, p);
    }
    static void dispatchRuntimeBuildButtonSecondaryActionFromUi(Host& state, int index) {
        state.dispatchBuildButtonSecondary(index);
    }
    void SetFocus(int) { host.focusWindow(); }
    void SetCapture(int) { host.captureWindow(); }
    int GetCapture() { return host.ownsCapture() ? 1 : 0; }
    void ReleaseCapture() { host.releaseCapture(); }
    void RedrawWindow(int, std::nullptr_t, std::nullptr_t, int) { host.redrawWindow(); }
    static bool scrollRuntimeMapCameraClamped(Host& state, int dx, int dy) {
        return state.scrollCameraClamped(dx, dy);
    }
    static void invalidateRuntimeObjectProjectionCache(Host& state) { state.invalidateProjection(); }
    static bool dismissRuntimePlacementFromUi(Host& state) { return state.dismissPlacement(); }
    static void cancelRuntimeInteraction(Host& state) { state.cancelInteraction(); }
    const std::vector<RuntimeProjectedObject>& runtimeProjectedObjectsForViewport(Host&, RECT viewport) {
        borrowedViewport = viewport;
        return borrowedVector;
    }
    const RuntimeProjectedObject* findProjectedObjectAtPoint(
        const std::vector<RuntimeProjectedObject>&, POINT point) {
        const auto id = host.projectedObjectIdAtPoint(borrowedViewport, point);
        if (!id) return nullptr;
        borrowedObject.instanceId = *id;
        return &borrowedProjection;
    }
    static std::optional<WorldObjectInstance> visibleTerrainWallTargetAtPoint(Host& state, RECT r, POINT p) {
        const auto wall = state.visibleTerrainWallAtPoint(r, p);
        if (!wall) return std::nullopt;
        return WorldObjectInstance{wall->objectId, wall->cell.x, wall->cell.y, wall->type, true};
    }
    static bool selectionContains(Host& state, int id) { return state.selectionContains(id); }
    static POINT approximateCellFromScreenPoint(Host& state, RECT r, POINT p) {
        const auto cell = state.surfaceCellAtPoint(r, p);
        return {cell.x, cell.y};
    }
    static bool dispatchRuntimeTargetOrderFromUi(Host& state,
        RuntimeMultiplayerGameplayTargetOrderKind, Cell cell, const WorldObjectInstance* object) {
        if (object->wall)
            return state.dispatchPrimaryWall({object->instanceId, cell, object->type});
        return state.dispatchPrimaryObject(cell, object->instanceId);
    }

public:
    explicit OldOracle(Host& h) : host(h) {}

// BEGIN FROZEN pan-policy
bool panRuntimeCameraByRightDragDelta(
    Host& state,
    int dx,
    int dy
) {
    if ((dx * dx) + (dy * dy) <= 16) {
        return false;
    }

    const int panX = std::clamp(dx, -96, 96);
    const int panY = std::clamp(dy, -96, 96);
    if (panX == 0 && panY == 0) {
        return false;
    }

    if (!scrollRuntimeMapCameraClamped(state, panX, panY)) {
        state.setInteractionStatus("map edge");
        return false;
    }
    invalidateRuntimeObjectProjectionCache(state);
    state.setInteractionStatus("right drag pan");
    return true;
}

int runtimeRightDragPanStep(int delta) {
    const int magnitude = std::abs(delta);
    if (magnitude <= 4) {
        return 0;
    }

    const int step = std::clamp(magnitude / 4, 1, 32);
    return delta < 0 ? -step : step;
}

bool advanceRuntimeRightMousePan(Host& state) {
    if (!state.scrollCoastingEnabled()
        || !state.gestureState().active
        || !state.gestureState().moved) {
        return false;
    }

    const int dx = state.gestureState().last.x - state.gestureState().start.x;
    const int dy = state.gestureState().last.y - state.gestureState().start.y;
    const int rateScale = std::clamp(state.scrollRate(), 0, 6) + 3;
    const int panX = runtimeRightDragPanStep(dx) * rateScale / 6;
    const int panY = runtimeRightDragPanStep(dy) * rateScale / 6;
    if (!panRuntimeCameraByRightDragDelta(state, panX, panY)) {
        return false;
    }
    return true;
}

// END FROZEN pan-policy

// BEGIN FROZEN completed-click
bool handleRuntimeMapRightClick(
    Host& state,
    const RECT& clientRect,
    POINT point
) {
    if (state.hasSelectedBeacon()) {
        state.clearSelectedBeacon();state.requestRepaint();return true;
    }
    if (state.followTargeting() || state.rallyTargeting()
        || state.beaconTargeting()) {
        state.setBeaconTargeting(false);
        state.setFollowTargeting(false);
        state.setRallyTargeting(false);
        return true;
    }
    if (cancelRuntimeActiveSuperWeaponTargetSelection(
            state,
            "right-click"
        )) {
        return true;
    }

    const RECT viewport = runtimeMapViewport(clientRect);
    if (!pointInRectInclusive(viewport, point)) {
        return false;
    }

    if (dismissRuntimePlacementFromUi(state)) return true;

    if (state.planning()) {
        state.setSelectedWaypoint(0);
        cancelRuntimeInteraction(state);
        return true;
    }

    if (state.sidebarToolActive()) {
        state.clearSidebarTool();
        state.setInteractionStatus("sidebar tool canceled");
        return true;
    }

    if (state.selectionEmpty()) {
        cancelRuntimeInteraction(state);
        return true;
    }

    const std::vector<RuntimeProjectedObject>& projectedObjects =
        runtimeProjectedObjectsForViewport(state, viewport);
    const RuntimeProjectedObject* projectedObject =
        findProjectedObjectAtPoint(projectedObjects, point);
    const auto terrainWallTarget = visibleTerrainWallTargetAtPoint(state, viewport, point);
    if (terrainWallTarget) {
        if (dispatchRuntimeTargetOrderFromUi(state,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {terrainWallTarget->x, terrainWallTarget->y}, &*terrainWallTarget)) return true;
    }
    if (projectedObject != nullptr
        && projectedObject->object != nullptr
        && !selectionContains(state, projectedObject->object->instanceId)) {
        const POINT targetPoint =
            approximateCellFromScreenPoint(state, viewport, point);
        const RuntimeCellPoint targetCell{targetPoint.x, targetPoint.y};
        if (dispatchRuntimeTargetOrderFromUi(
                state,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                targetCell,
                projectedObject->object
            )) {
            return true;
        }
    }

    cancelRuntimeInteraction(state);
    return true;
}

// END FROZEN completed-click

    std::intptr_t message(std::uint32_t message, LPARAM lParam, WPARAM wParam = 0) {
        constexpr int window = 1;
        switch (message) {
// BEGIN FROZEN right-down-case
    case WM_RBUTTONDOWN:
    {
        Host* state = reinterpret_cast<Host*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr || !state->worldLoaded()) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (state->menuOpen()) {
            return 0;
        }

        RECT clientRect;
        GetClientRect(window, &clientRect);
        const POINT point = pointFromMouseMessage(lParam);
        state->setMouseClientPointValid(true);
        state->setMouseClientPoint(point);
        const RECT sidebar = runtimeSidebarRect(clientRect);
        const RECT viewport = runtimeMapViewport(clientRect);
        // A map press may become a drag. Only a completed click cancels
        // targeting; sidebar/outside-map presses retain immediate cancel.
        if (!pointInRectInclusive(viewport, point)
            && cancelRuntimeActiveSuperWeaponTargetSelection(
                *state,
                "right-click"
            )) {
            state->requestRepaint();
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        if (pointInRectInclusive(sidebar, point)) {
            const std::optional<int> buttonIndex =
                runtimeBuildButtonAtPoint(*state, sidebar, point);
            if (buttonIndex.has_value()) {
                dispatchRuntimeBuildButtonSecondaryActionFromUi(
                    *state,
                    *buttonIndex
                );
                state->requestRepaint();
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            if (state->sidebarToolActive()) {
                state->clearSidebarTool();
                state->setInteractionStatus("sidebar tool canceled");
                state->requestRepaint();
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            return 0;
        }
        if (!pointInRectInclusive(viewport, point)) {
            return 0;
        }

        SetFocus(window);
        SetCapture(window);
        state->gestureState().active = true;
        state->gestureState().moved = false;
        state->gestureState().start = point;
        state->gestureState().last = point;
        state->gestureState().cursorDelta = {0, 0};
        state->setInteractionStatus("right drag pan");
        state->requestRepaint();
        return 0;
    }
// END FROZEN right-down-case
// BEGIN FROZEN right-up-case
    case WM_RBUTTONUP:
    {
        Host* state = reinterpret_cast<Host*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr || !state->worldLoaded()) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (state->menuOpen()) {
            return 0;
        }

        // ReleaseCapture synchronously sends WM_CAPTURECHANGED. Commit
        // the completed gesture first so that callback cannot erase it.
        const bool wasRightDrag = state->gestureState().active;
        const bool wasRightDragMove = state->gestureState().moved;
        state->gestureState().active = false;
        state->gestureState().moved = false;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        const POINT point = pointFromMouseMessage(lParam);
        state->setMouseClientPointValid(true);
        state->setMouseClientPoint(point);
        // A release after losing capture, or a sidebar press already
        // handled on button-down, must not cancel a retained map ability.
        if (!wasRightDrag) {
            return 0;
        }
        if (wasRightDragMove) {
            state->setInteractionStatus("camera panned");
            state->requestRepaint();
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        RECT clientRect;
        GetClientRect(window, &clientRect);
        handleRuntimeMapRightClick(*state, clientRect, point);

        state->requestRepaint();
        RedrawWindow(
            window,
            nullptr,
            nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
        );
        return 0;
    }
// END FROZEN right-up-case
        default: return 1;
        }
    }
    bool down(Point p) { return message(WM_RBUTTONDOWN, p) == 0; }
    bool up(Point p) { return message(WM_RBUTTONUP, p) == 0; }
    bool move(Point point) {
        Host* state = &host;
// BEGIN FROZEN right-move-branch
        if (state->gestureState().active) {
            const POINT previousPoint = state->gestureState().last;
            const int totalDx = point.x - state->gestureState().start.x;
            const int totalDy = point.y - state->gestureState().start.y;
            if ((totalDx * totalDx) + (totalDy * totalDy) > 16) {
                state->gestureState().moved = true;
            }
            if (point.x != previousPoint.x || point.y != previousPoint.y) {
                state->gestureState().cursorDelta = {point.x - previousPoint.x, point.y - previousPoint.y};
            }
            state->gestureState().last = point;
            if (!state->scrollCoastingEnabled()
                && state->gestureState().moved) {
                panRuntimeCameraByRightDragDelta(
                    *state,
                    point.x - previousPoint.x,
                    point.y - previousPoint.y
                );
            }
            state->requestRepaint();
            return true;
        }

// END FROZEN right-move-branch
        return false;
    }
    void cancel(Cancel event) {
        Host* campaignState = &host;
        constexpr int window = 1;
        const auto message = event == Cancel::FocusLost ? WM_KILLFOCUS
            : event == Cancel::CancelMode ? WM_CANCELMODE
            : event == Cancel::ApplicationDeactivated ? WM_ACTIVATEAPP : 0U;
        constexpr WPARAM wParam = FALSE;
// BEGIN FROZEN right-cancel-prefix
    if (campaignState != nullptr && campaignState->gestureState().active
        && (message == WM_KILLFOCUS || message == WM_CANCELMODE
            || (message == WM_ACTIVATEAPP && wParam == FALSE))) {
        campaignState->gestureState().active = false;
        campaignState->gestureState().moved = false;
        campaignState->requestRepaint();
        if (GetCapture() == window) ReleaseCapture();
    }
// END FROZEN right-cancel-prefix
    }
    void captureChanged() {
        Host* state = &host;
// BEGIN FROZEN capture-changed-right-branch
        if (state != nullptr && state->gestureState().active) {
            state->gestureState().active = false;
            state->gestureState().moved = false;
            state->requestRepaint();
        }
// END FROZEN capture-changed-right-branch
    }
};

enum class Operation { Down, Move, Up, Click, Pan, Step, Tick, CancelEvent, CaptureChanged };
struct Action {
    Operation operation = Operation::Click;
    Input input{{100, 120}};
    Rect client{0, 0, 640, 480};
    std::int32_t dx = 0, dy = 0;
    Cancel cancel = Cancel::Other;
};
enum class Mutation {
    None, Input, GestureReplacement, LiveClickState, ProjectionResult,
    WallResult, SurfaceResult, CaptureChanged, FocusLost, NestedClick, ThrowAfterEffect
};
struct Hook {
    std::string at;
    Mutation mutation = Mutation::None;
};
struct Case {
    std::string name;
    State initial;
    std::vector<Action> actions;
    Hook hook;
    std::int32_t throwAt = 0;
};
struct Result {
    State state;
    std::vector<Row> trace;
    std::vector<std::int32_t> returns;
    std::string exception;
};

std::int32_t drive(bool original, OldOracle& old, Host& host, Action& action) {
    switch (action.operation) {
    case Operation::Down:
        return original ? old.down(action.input.point)
            : handleRuntimeMapRightPointerDown(action.input, host);
    case Operation::Move:
        return original ? old.move(action.input.point)
            : handleRuntimeMapRightPointerMove(action.input, host);
    case Operation::Up:
        return original ? old.up(action.input.point)
            : handleRuntimeMapRightPointerUp(action.input, host, host);
    case Operation::Click:
        return original ? old.handleRuntimeMapRightClick(host, action.client, action.input.point)
            : handleRuntimeMapRightPointerCompletedClick(action.input, action.client, host);
    case Operation::Pan:
        return original ? old.panRuntimeCameraByRightDragDelta(host, action.dx, action.dy)
            : panRuntimeMapRightPointerByDelta(action.dx, action.dy, host);
    case Operation::Step:
        return original ? old.runtimeRightDragPanStep(action.dx)
            : runtimeMapRightPointerPanStep(action.dx);
    case Operation::Tick:
        return original ? old.advanceRuntimeRightMousePan(host) : advanceRuntimeMapRightPointerTick(host);
    case Operation::CancelEvent:
        if (original) old.cancel(action.cancel); else cancelRuntimeMapRightPointer(action.cancel, host);
        return 0;
    case Operation::CaptureChanged:
        if (original) old.captureChanged(); else captureChangedRuntimeMapRightPointer(host);
        return 0;
    }
    throw std::runtime_error("unknown operation");
}

Result runRoute(const Case& test, bool original) {
    Host host(test.initial);
    OldOracle old(host);
    auto actions = test.actions;
    host.throwAt = test.throwAt;
    bool fired = false;
    std::size_t currentAction = 0;
    host.afterCall = [&](const std::string& operation) {
        if (fired || test.hook.mutation == Mutation::None || operation != test.hook.at) return;
        fired = true;
        ++host.state.callbacks;
        host.state.callbackSawActive = host.state.gesture.active;
        switch (test.hook.mutation) {
        case Mutation::None: break;
        case Mutation::Input:
            actions[currentAction].input.point = {-300, -200};
            break;
        case Mutation::GestureReplacement:
            // Same state object, freshly replaced members: the original native
            // driver pins that state address. No arbitrary dangling-root test.
            host.state.gesture = {true, true, {-9, -8}, {41, 37}, {-3, -4}};
            host.state.viewport = {0, 0, 350, 300};
            host.state.projected = 91;
            host.state.selection = {77};
            host.state.surface = {201, 203};
            break;
        case Mutation::LiveClickState:
            host.state.selectedBeacon = false;
            host.state.follow = false;
            host.state.rally = true;
            host.state.planning = true;
            host.state.surface = {401, 403};
            break;
        case Mutation::ProjectionResult:
            host.state.projected = 92;
            host.state.selection = {10, 92};
            break;
        case Mutation::WallResult:
            host.state.wall = Wall{999, {801, 803}, "CHANGED"};
            host.state.wallAccepted = false;
            break;
        case Mutation::SurfaceResult:
            host.state.surface = {601, 603};
            break;
        case Mutation::CaptureChanged:
            if (original) old.captureChanged(); else captureChangedRuntimeMapRightPointer(host);
            break;
        case Mutation::FocusLost:
            if (original) old.cancel(Cancel::FocusLost);
            else cancelRuntimeMapRightPointer(Cancel::FocusLost, host);
            break;
        case Mutation::NestedClick: {
            Action nested;
            nested.operation = Operation::Click;
            nested.input.point = {13, 17};
            (void)drive(original, old, host, nested);
            break;
        }
        case Mutation::ThrowAfterEffect:
            throw Injected("callback:" + operation);
        }
    };
    std::vector<std::int32_t> returns;
    std::string exception;
    try {
        for (currentAction = 0; currentAction < actions.size(); ++currentAction)
            returns.push_back(drive(original, old, host, actions[currentAction]));
    } catch (const Injected& failure) {
        exception = failure.what();
    }
    return {std::move(host.state), std::move(host.trace), std::move(returns), std::move(exception)};
}

std::uint64_t checks = 0, differentials = 0, rows = 0;
std::uint64_t dynamicCases = 0, nestedCases = 0, exceptionCases = 0, sequenceCases = 0;
void expect(bool predicate, const std::string& message) {
    ++checks;
    if (!predicate) throw std::runtime_error(message);
}
bool hasOperation(const Result& result, const std::string& name) {
    return std::any_of(result.trace.begin(), result.trace.end(),
        [&](const Row& row) { return row.operation == name; });
}
std::size_t countOperation(const Result& result, const std::string& name) {
    return static_cast<std::size_t>(std::count_if(result.trace.begin(), result.trace.end(),
        [&](const Row& row) { return row.operation == name; }));
}
Result compare(const Case& test) {
    const auto old = runRoute(test, true);
    auto current = runRoute(test, false);
    ++differentials;
    rows += current.trace.size();
    expect(old.exception == current.exception, test.name + ": exception differs");
    expect(old.returns == current.returns, test.name + ": return sequence differs");
    expect(old.state.snapshot() == current.state.snapshot(), test.name + ": complete state differs");
    expect(old.trace.size() == current.trace.size(), test.name + ": trace count differs");
    for (std::size_t index = 0; index < old.trace.size(); ++index)
        expect(old.trace[index] == current.trace[index],
            test.name + ": ordered query/effect/state/return trace differs at " + std::to_string(index)
            + " old=" + old.trace[index].operation + " new=" + current.trace[index].operation);
    expect(current.state.tick == test.initial.tick && current.state.worldHash == test.initial.worldHash,
        test.name + ": pointer policy changed model world or authoritative tick");
    return current;
}
Action action(Operation operation, Point point = {100, 120}) {
    Action result;
    result.operation = operation;
    result.input.point = point;
    return result;
}
Case one(const std::string& name, State initial, Action input) {
    Case result;
    result.name = name;
    result.initial = std::move(initial);
    result.actions.push_back(input);
    return result;
}
State dragging(bool moved = true) {
    State state;
    state.gesture = {true, moved, {100, 120}, {140, 150}, {7, -9}};
    state.captured = true;
    return state;
}

void ordinaryMatrices() {
    const std::array<Point, 9> points{{{100,120},{0,0},{499,479},{500,200},
        {639,479},{640,480},{-1,-1},{-32768,32767},{32767,-32768}}};
    for (int mask = 0; mask < 64; ++mask) {
        State s;
        s.loaded = (mask & 1) == 0;
        s.menu = (mask & 2) != 0;
        s.weaponTarget = (mask & 4) != 0;
        s.button = (mask & 8) != 0 ? std::optional<std::int32_t>{3} : std::nullopt;
        s.tool = (mask & 16) != 0;
        s.gesture = {(mask & 32) != 0, true, {-8,-9}, {17,19}, {-3,4}};
        for (const auto p : points)
            (void)compare(one("down-" + std::to_string(mask) + "-" + value(p), s, action(Operation::Down, p)));
    }
    const std::array<Point, 13> movePoints{{{0,0},{3,0},{4,0},{5,0},{0,4},{0,5},
        {3,3},{-3,-3},{-4,0},{-5,0},{97,-97},{512,511},{-511,-512}}};
    for (int mask = 0; mask < 16; ++mask) {
        State s;
        s.gesture = {(mask & 1) != 0, (mask & 2) != 0, {0,0},
            (mask & 8) != 0 ? Point{3,-3} : Point{0,0}, {9,11}};
        s.coast = (mask & 4) != 0;
        for (const auto point : movePoints)
            (void)compare(one("move-" + std::to_string(mask) + "-" + value(point), s, action(Operation::Move, point)));
    }
    for (int mask = 0; mask < 32; ++mask) {
        State s;
        s.loaded = (mask & 1) == 0;
        s.menu = (mask & 2) != 0;
        s.gesture = {(mask & 4) != 0, (mask & 8) != 0, {100,120}, {100,120}, {8,9}};
        s.captured = (mask & 16) != 0;
        for (const auto point : points)
            (void)compare(one("up-" + std::to_string(mask) + "-" + value(point), s, action(Operation::Up, point)));
    }
    for (int mask = 0; mask < 512; ++mask) {
        State s;
        s.selectedBeacon = (mask & 1) != 0;
        s.follow = (mask & 2) != 0;
        s.rally = (mask & 4) != 0;
        s.beacon = (mask & 8) != 0;
        s.weaponTarget = (mask & 16) != 0;
        s.placement = (mask & 32) != 0;
        s.planning = (mask & 64) != 0;
        s.tool = (mask & 128) != 0;
        if ((mask & 256) != 0) s.selection.clear();
        s.wall = Wall{44, {51,53}, "WALL"};
        for (const auto point : std::array<Point, 2>{{{100,120},{-1,-1}}})
            (void)compare(one("click-priority-" + std::to_string(mask) + "-" + value(point),
                s, action(Operation::Click, point)));
    }
    for (int mask = 0; mask < 64; ++mask) {
        State s;
        s.projected = (mask & 1) != 0 ? std::optional<std::int32_t>{20} : std::nullopt;
        s.wall = (mask & 2) != 0 ? std::optional<Wall>{Wall{44,{51,53},"WALL"}} : std::nullopt;
        s.wallAccepted = (mask & 4) != 0;
        s.objectAccepted = (mask & 8) != 0;
        s.selection = (mask & 16) != 0 ? std::vector<std::int32_t>{10,20} : std::vector<std::int32_t>{10};
        if ((mask & 32) != 0) s.selection.clear();
        (void)compare(one("click-dispatch-" + std::to_string(mask), s, action(Operation::Click)));
    }
    constexpr std::array<std::int32_t, 13> deltas{{-512,-97,-96,-5,-4,-3,0,3,4,5,96,97,512}};
    for (const auto dx : deltas) for (const auto dy : deltas) for (int edge = 0; edge < 3; ++edge) {
        State s;
        s.cameraX = edge == 1 ? 64 : 0;
        s.cameraY = edge == 1 ? -64 : 0;
        s.scrollBlocked = edge == 2;
        auto a = action(Operation::Pan); a.dx = dx; a.dy = dy;
        (void)compare(one("pan-" + std::to_string(dx) + "," + std::to_string(dy) + "-" + std::to_string(edge), s, a));
    }
    for (int delta = -1024; delta <= 1024; ++delta) {
        auto a = action(Operation::Step); a.dx = delta;
        const auto result = compare(one("step-" + std::to_string(delta), {}, a));
        const auto magnitude = delta < 0 ? -delta : delta;
        const auto expectedMagnitude = magnitude <= 4 ? 0 : std::min(32, std::max(1, magnitude / 4));
        const auto expected = delta < 0 ? -expectedMagnitude : expectedMagnitude;
        expect(result.returns.front() == expected, "independent step interval/sign/clamp");
    }
    constexpr std::array<Point, 11> displacements{{{0,0},{4,0},{5,0},{8,0},{16,-16},
        {32,32},{96,-96},{128,129},{-128,-129},{512,511},{-511,-512}}};
    for (int mask = 0; mask < 8; ++mask) for (const auto rate : {-9,0,1,3,6,11})
        for (const auto delta : displacements) {
            State s;
            s.coast = (mask & 1) != 0;
            s.gesture = {(mask & 2) != 0, (mask & 4) != 0, {-20,-30},
                {-20 + delta.x,-30 + delta.y}, {-7,9}};
            s.rate = rate;
            (void)compare(one("tick-" + std::to_string(mask) + "-" + std::to_string(rate) + "-" + value(delta),
                s, action(Operation::Tick)));
        }
    for (int mask = 0; mask < 8; ++mask)
        for (const auto event : {Cancel::Other,Cancel::FocusLost,Cancel::CancelMode,Cancel::ApplicationDeactivated}) {
            State s;
            s.gesture = {(mask & 1) != 0,(mask & 2) != 0,{1,2},{3,4},{5,6}};
            s.captured = (mask & 4) != 0;
            auto a = action(Operation::CancelEvent); a.cancel = event;
            (void)compare(one("cancel-" + std::to_string(mask) + "-" + std::to_string(static_cast<int>(event)), s, a));
        }
    for (int mask = 0; mask < 4; ++mask) {
        State s;
        s.gesture = {(mask & 1) != 0,(mask & 2) != 0,{1,2},{3,4},{5,6}};
        (void)compare(one("capture-" + std::to_string(mask), s, action(Operation::CaptureChanged)));
    }
    // Degenerate/reversed viewports retain comparison-only original semantics.
    for (const auto viewport : std::array<Rect, 4>{{{0,0,0,0},{10,10,5,5},
        {-200,-300,-10,-20},{-32768,-32768,32767,32767}}}) {
        State s; s.viewport = viewport;
        for (const auto point : points) {
            (void)compare(one("rect-down-" + value(viewport) + "-" + value(point), s, action(Operation::Down, point)));
            (void)compare(one("rect-click-" + value(viewport) + "-" + value(point), s, action(Operation::Click, point)));
        }
    }
}

void independentBoundaries() {
    {
        State s; s.selectedBeacon = s.follow = s.rally = s.beacon = s.weaponTarget = true;
        s.placement = s.planning = s.tool = true;
        const auto result = compare(one("beacon-priority", s, action(Operation::Click, {-9,-7})));
        expect(!result.state.selectedBeacon && result.state.follow && result.state.weaponTarget
            && result.state.placement && result.state.planning && result.state.tool
            && result.state.repaintRequests == 1 && result.trace.size() == 3,
            "selected beacon clears before targeting/viewport/command queries");
    }
    {
        State s; s.rally = s.beacon = s.weaponTarget = s.placement = true;
        const auto result = compare(one("target-mode-priority", s, action(Operation::Click)));
        expect(!result.state.follow && !result.state.rally && !result.state.beacon
            && result.state.weaponTarget && result.state.placement
            && !hasOperation(result,"weapon-cancel") && !hasOperation(result,"viewport")
            && !result.state.repaint, "target mode cancellation has original three writes and no repaint");
    }
    {
        State s; s.wall = Wall{44,{51,53},"WALL"}; s.wallAccepted = true;
        const auto result = compare(one("wall-priority", s, action(Operation::Click)));
        expect(result.state.commands == std::vector<std::string>{"wall:44/51,53/4:WALL"}
            && !hasOperation(result,"selected") && !hasOperation(result,"surface")
            && countOperation(result,"projected") == 1 && countOperation(result,"wall") == 1,
            "owned wall consumes before projected object and surface query");
    }
    {
        State s; s.wall = Wall{44,{51,53},"WALL"}; s.wallAccepted = false;
        const auto result = compare(one("wall-false-object-fresh", s, action(Operation::Click)));
        expect(result.state.attempts.size() == 2 && result.state.commands.size() == 1
            && result.state.commands.front() == "object:31,47:20" && result.state.cancels == 0,
            "wall rejection keeps original object dispatch fallback");
        s.objectAccepted = false;
        const auto rejected = compare(one("two-rejections-cancel", s, action(Operation::Click)));
        expect(rejected.state.attempts.size() == 2 && rejected.state.commands.empty()
            && rejected.state.cancels == 1 && rejected.state.selection.empty(),
            "both rejected commands preserve cancellation");
    }
    {
        State s; s.planning = true;
        const auto result = compare(one("planning-id-zero", s, action(Operation::Click)));
        expect(result.state.waypoint == 0 && result.state.cancels == 1
            && !hasOperation(result,"projected"), "planning cancels selected waypoint with literal id zero");
    }
    {
        State s; s.gesture = {false,true,{1,2},{3,4},{5,6}};
        s.captured = true; s.weaponTarget = true;
        const auto result = compare(one("up-without-down", s, action(Operation::Up)));
        expect(!result.state.gesture.active && !result.state.gesture.moved
            && result.state.releases == 1 && result.state.weaponTarget && !result.state.repaint
            && result.state.mouseValid && !hasOperation(result,"selected-beacon"),
            "stray up releases only owned capture and never cancels retained map ability");
    }
    {
        State s; s.weaponTarget = true;
        const auto inside = compare(one("down-retains-ability", s, action(Operation::Down)));
        expect(inside.state.weaponTarget && inside.state.gesture.active
            && !hasOperation(inside,"weapon-cancel"), "map down retains targeting until completed click");
        const auto sidebar = compare(one("sidebar-immediate-ability-cancel", s, action(Operation::Down,{550,120})));
        expect(!sidebar.state.weaponTarget && !sidebar.state.gesture.active
            && sidebar.state.redraws == 1 && !hasOperation(sidebar,"button"),
            "outside viewport ability cancels before sidebar secondary owner");
    }
    {
        State s; s.gesture = {true,false,{0,0},{0,0},{9,11}};
        const auto jitter = compare(one("exact-four-pixel", s, action(Operation::Move,{4,0})));
        expect(!jitter.state.gesture.moved && jitter.state.cameraX == 0
            && !hasOperation(jitter,"scroll"), "distance square exactly16 stays click");
        const auto moved = compare(one("exact-five-pixel", s, action(Operation::Move,{5,0})));
        expect(moved.state.gesture.moved && moved.state.cameraX == 5
            && moved.state.invalidations == 1, "distance square25 begins pan");
    }
    {
        State s = dragging(); s.coast = true; s.rate = 0;
        s.gesture.last = {108,120};
        const auto result = compare(one("coast-rounded-four", s, action(Operation::Tick)));
        expect(result.returns == std::vector<std::int32_t>{0} && result.trace.size() == 8
            && !hasOperation(result,"scroll"), "tick integer scaling preserves deadzone after division");
        auto pan = action(Operation::Pan); pan.dx = 400; pan.dy = -500;
        const auto clamped = compare(one("pan-clamps-96", {}, pan));
        const auto scroll = std::find_if(clamped.trace.begin(),clamped.trace.end(),
            [](const Row& row){return row.operation=="scroll";});
        expect(scroll != clamped.trace.end() && scroll->parameters == "96,-96",
            "each raw delta clamps before the camera leaf");
    }
}

void sequences() {
    constexpr std::array<Point,8> directions{{{0,-80},{80,-80},{80,0},{80,80},
        {0,80},{-80,80},{-80,0},{-80,-80}}};
    for (const bool coast : {false,true}) for (const auto rate : {0,3,6,9})
        for (const auto d : directions) {
            Case test;
            test.name = "gesture-roundtrip-" + value(coast) + "-" + std::to_string(rate) + "-" + value(d);
            test.initial.coast = coast; test.initial.rate = rate;
            test.initial.projected.reset();
            test.actions = {action(Operation::Down,{100,120}),
                action(Operation::Move,{103,120}), action(Operation::Move,{100+d.x,120+d.y}),
                action(Operation::Tick), action(Operation::Tick),
                action(Operation::Move,{100,120}), action(Operation::Move,{100-d.x,120-d.y}),
                action(Operation::Up,{100-d.x,120-d.y})};
            const auto result = compare(test); ++sequenceCases;
            expect(!result.state.gesture.active && !result.state.gesture.moved && !result.state.captured
                && result.state.status == "camera panned" && result.state.cancels == 0,
                "moved latch survives return to origin; up cannot become completed click");
        }
    for (const auto interruption : {Cancel::FocusLost,Cancel::CancelMode,Cancel::ApplicationDeactivated}) {
        Case test;
        test.name = "interruption-before-up-" + std::to_string(static_cast<int>(interruption));
        test.initial.weaponTarget = true;
        auto cancel = action(Operation::CancelEvent); cancel.cancel = interruption;
        test.actions = {action(Operation::Down),action(Operation::Move,{130,150}),cancel,
            action(Operation::Move,{140,160}),action(Operation::Up)};
        const auto result = compare(test); ++sequenceCases;
        expect(!result.state.gesture.active && !result.state.captured && result.state.weaponTarget
            && result.state.cancels == 0 && !hasOperation(result,"selected-beacon"),
            "focus/cancel/deactivation interrupts without routing later up as command");
    }
}

void callbacksAndExceptions() {
    std::vector<Case> dynamic;
    auto add = [&](const std::string& label, State initial, Action input, Hook hook) {
        auto test = one(label,std::move(initial),input); test.hook=std::move(hook);
        dynamic.push_back(std::move(test));
    };
    add("owned-down-input",{},action(Operation::Down),{"loaded",Mutation::Input});
    add("owned-up-input",dragging(false),action(Operation::Up),{"loaded",Mutation::Input});
    add("owned-move-input",dragging(),action(Operation::Move,{180,190}),{"state",Mutation::Input});
    add("owned-click-input",{},action(Operation::Click),{"selected-beacon",Mutation::Input});
    add("fresh-gesture-after-focus",dragging(),action(Operation::Down),{"focus",Mutation::GestureReplacement});
    add("fresh-gesture-after-capture",dragging(),action(Operation::Down),{"capture",Mutation::GestureReplacement});
    add("fresh-click-after-release",dragging(false),action(Operation::Up),{"release",Mutation::GestureReplacement});
    add("fresh-target-mode-after-beacon",{},action(Operation::Click),{"selected-beacon",Mutation::LiveClickState});
    add("fresh-object-selection-after-projected",{},action(Operation::Click),{"projected",Mutation::ProjectionResult});
    add("fresh-surface-after-wall",{},action(Operation::Click),{"wall",Mutation::SurfaceResult});
    State wall; wall.wall=Wall{44,{51,53},"WALL"};
    add("owned-wall-result",wall,action(Operation::Click),{"wall",Mutation::WallResult});
    add("fresh-state-after-borrow",dragging(),action(Operation::Move,{170,180}),{"state",Mutation::GestureReplacement});
    for (const auto& test : dynamic) {
        const auto result = compare(test); ++dynamicCases;
        expect(result.state.callbacks==1, test.name+": callback was not reached");
    }

    const std::array<Hook,7> hooks{{{"focus",Mutation::FocusLost},
        {"capture",Mutation::CaptureChanged},{"release",Mutation::CaptureChanged},
        {"release",Mutation::FocusLost},{"release",Mutation::NestedClick},
        {"dispatch-wall",Mutation::NestedClick},{"repaint",Mutation::CaptureChanged}}};
    for (std::size_t index=0;index<hooks.size();++index) {
        State seed=dragging(index!=2 && index!=3 && index!=4);
        seed.wall=Wall{44,{51,53},"WALL"};
        auto test=one("nested-"+std::to_string(index),seed,
            action(index<2?Operation::Down:index==5?Operation::Click:index==6?Operation::Move:Operation::Up));
        test.hook=hooks[index];
        const auto result=compare(test); ++nestedCases;
        expect(result.state.callbacks==1,test.name+": nested hook not reached");
        if(index==2 || index==3 || index==4)
            expect(!result.state.callbackSawActive, "release callback observes completed gesture already committed");
        if(index==0 || index==1)
            expect(result.state.gesture.active && !result.state.gesture.moved
                && result.state.status=="right drag pan", "outer down initializes after focus/capture callback");
    }

    std::vector<Case> exceptionSeeds;
    exceptionSeeds.push_back(one("throw-down-map",{},action(Operation::Down)));
    State sidebar; sidebar.button=3;
    exceptionSeeds.push_back(one("throw-down-sidebar",sidebar,action(Operation::Down,{550,120})));
    State sidebarTool; sidebarTool.tool=true;
    exceptionSeeds.push_back(one("throw-down-tool",sidebarTool,action(Operation::Down,{550,120})));
    exceptionSeeds.push_back(one("throw-move",dragging(),action(Operation::Move,{180,190})));
    exceptionSeeds.push_back(one("throw-up-click",dragging(false),action(Operation::Up)));
    exceptionSeeds.push_back(one("throw-up-pan",dragging(true),action(Operation::Up)));
    State targeting; targeting.rally=true;
    exceptionSeeds.push_back(one("throw-target-cancel",targeting,action(Operation::Click)));
    State rejected; rejected.wall=Wall{44,{51,53},"WALL"}; rejected.wallAccepted=false; rejected.objectAccepted=false;
    exceptionSeeds.push_back(one("throw-click-fallback",rejected,action(Operation::Click)));
    State selected; selected.selectedBeacon=true;
    exceptionSeeds.push_back(one("throw-selected-beacon",selected,action(Operation::Click)));
    State planning; planning.planning=true;
    exceptionSeeds.push_back(one("throw-planning",planning,action(Operation::Click)));
    State placement; placement.placement=true;
    exceptionSeeds.push_back(one("throw-placement",placement,action(Operation::Click)));
    State coast=dragging();coast.coast=true;coast.rate=6;
    exceptionSeeds.push_back(one("throw-coast-tick",coast,action(Operation::Tick)));
    auto cancel=action(Operation::CancelEvent);cancel.cancel=Cancel::FocusLost;
    exceptionSeeds.push_back(one("throw-cancel",dragging(),cancel));
    exceptionSeeds.push_back(one("throw-capture",dragging(),action(Operation::CaptureChanged)));
    for(const auto& seed:exceptionSeeds) {
        const auto normal=runRoute(seed,true);
        for(std::size_t index=0;index<normal.trace.size();++index) {
            auto test=seed;
            test.name+="-primitive-"+std::to_string(index+1);
            test.throwAt=static_cast<std::int32_t>(index+1);
            const auto result=compare(test);++exceptionCases;
            expect(!result.exception.empty() && result.trace.back().threw,
                test.name+": requested primitive exception was not reached");
        }
    }
    for(const auto& at:std::array<std::string,5>{{"focus","capture","release","dispatch-object","repaint"}}) {
        auto test=one("throw-after-effect-"+at,
            at=="release"?dragging(false):State{},
            action(at=="release"?Operation::Up:at=="dispatch-object"?Operation::Click:Operation::Down));
        test.hook={at,Mutation::ThrowAfterEffect};
        const auto result=compare(test);++exceptionCases;
        expect(result.exception=="callback:"+at && result.state.callbacks==1,
            "effect before callback exception must remain committed");
    }
}
} // namespace

int main() {
    try {
        ordinaryMatrices();
        independentBoundaries();
        sequences();
        callbacksAndExceptions();
        std::cout << "Map right-pointer controller contract passed: checks=" << checks
            << " differentials=" << differentials << " orderedTraceRows=" << rows
            << " dynamic=" << dynamicCases << " nested=" << nestedCases
            << " exceptions=" << exceptionCases << " sequences=" << sequenceCases
            << " originalBlocks=7 noWindowsHeaders=yes\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Map right-pointer controller contract failed: " << error.what() << '\n';
        return 1;
    }
}
