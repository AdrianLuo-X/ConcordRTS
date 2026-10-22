#include "ui/input/RuntimeMapLeftPointerController.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
std::string leftPointerLower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}
std::string leftPointerTrim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}
void finishLeftGesture(RuntimeMapLeftPointerHost& host) {
    host.gestureState().active = false;
    host.gestureState().moved = false;
}
void repaintAndRedraw(RuntimeMapLeftPointerHost& host) {
    host.requestRepaint();
    host.redraw();
}
}

bool beginRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, RuntimeMenuAction action,
    RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host) {
    const auto control = host.menuControl(client, action);
    if (!control || !control->slider) return false;
    const auto valueRect = control->valueRect;
    // Original PtInRect has exclusive right/bottom edges.
    if (!(point.x >= valueRect.left && point.x < valueRect.right
        && point.y >= valueRect.top && point.y < valueRect.bottom)) return false;
    host.setMenuPressed(action);
    host.setMenuSliderDrag(action);
    (void)host.setMenuSliderFromX(client, action, point.x);
    return true;
}
bool updateRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, std::int32_t x,
    RuntimeMapLeftPointerHost& host) {
    if (host.menuSliderDrag() == RuntimeMenuAction::None) return false;
    return host.setMenuSliderFromX(client, host.menuSliderDrag(), x);
}
bool finishRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, std::int32_t x,
    RuntimeMapLeftPointerHost& host) {
    if (host.menuSliderDrag() == RuntimeMenuAction::None) return false;
    (void)updateRuntimeMapLeftMenuSlider(client, x, host);
    const auto completed = host.menuSliderDrag();
    host.setMenuSliderDrag(RuntimeMenuAction::None);
    host.setMenuPressed(RuntimeMenuAction::None);
    host.applyMenuSliderRuntimeEffects(completed);
    host.clearMenuStatus();
    host.persistMenuPreferences();
    return true;
}

bool handleRuntimeMapLeftMenuDown(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input,
    RuntimeMapLeftPointerHost& host) {
    const auto action = host.menuActionAtPoint(client, input.point);
    host.setMenuPressed(action);
    const auto buttons = host.menuFocusableActions(client);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i] == action) {
            host.setMenuFocus(static_cast<std::int32_t>(i));
            break;
        }
    }
    if (beginRuntimeMapLeftMenuSlider(client, action, input.point, host)) {
        host.captureWindow();
        repaintAndRedraw(host);
        return true;
    }
    if (action != RuntimeMenuAction::None) host.captureWindow();
    repaintAndRedraw(host);
    return true;
}
bool handleRuntimeMapLeftMenuMove(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input,
    RuntimeMapLeftPointerHost& host) {
    if (host.menuSliderDrag() != RuntimeMenuAction::None) {
        if (host.ownsCapture() && (input.rawButtons & 1U) != 0) {
            if (updateRuntimeMapLeftMenuSlider(client, input.point.x, host)) repaintAndRedraw(host);
        }
        return true;
    }
    const auto hover = host.menuActionAtPoint(client, input.point);
    if (hover != host.menuHover()) {
        host.setMenuHover(hover);
        repaintAndRedraw(host);
    }
    return true;
}
bool handleRuntimeMapLeftMenuUp(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input,
    RuntimeMapLeftPointerHost& host) {
    if (finishRuntimeMapLeftMenuSlider(client, input.point.x, host)) {
        if (host.ownsCapture()) host.releaseCapture();
        host.emitSound(RuntimeMapLeftSound::Button);
        repaintAndRedraw(host);
        return true;
    }
    const auto released = host.menuActionAtPoint(client, input.point);
    const auto pressed = host.menuPressed();
    host.setMenuPressed(RuntimeMenuAction::None);
    if (host.ownsCapture()) host.releaseCapture();
    if (released == pressed && pressed != RuntimeMenuAction::None) host.activateMenuAction(pressed);
    else repaintAndRedraw(host);
    return true;
}

bool beginRuntimeMapLeftSuperWeapon(RuntimeMapLeftSuperWeaponChoice choice,
    RuntimeMapLeftPointerHost& host) {
    host.prepareSuperWeaponContext();
    std::string reason;
    bool beganTargeting = false;
    if (host.usesIntentRouter()) {
        host.refreshAuthority();
        const auto ruleMode = host.superWeaponTargetMode(choice.id);
        const std::string targetMode = !ruleMode ? std::string("cell")
            : leftPointerLower(leftPointerTrim(*ruleMode));
        const std::size_t targetCount =
            targetMode == "none" || targetMode == "notarget"
                || targetMode == "wholemap" || targetMode == "map"
            ? 0U : (targetMode == "twostage" || targetMode == "two-stage" ? 2U : 1U);
        if (host.chosenSuperWeaponState() != RuntimeMapLeftSuperWeaponState::Ready) reason = "not ready";
        else {
            beganTargeting = host.routerBeginSuperWeapon(choice.id, targetCount, reason);
            if (beganTargeting && targetCount == 0U) beganTargeting = host.routerConfirmSuperWeapon(reason);
        }
    } else beganTargeting = host.offlineBeginSuperWeapon(choice.owner, choice.id, reason);
    if (beganTargeting) {
        host.clearChronosphereMarker();
        host.setStatus(host.multiplayerBridgePresent() && !host.routerSuperWeaponPresent()
            ? "superweapon launch queued for multiplayer" : "superweapon target: " + choice.name);
        host.emitSound(RuntimeMapLeftSound::Button);
    } else {
        host.setStatus("superweapon unavailable: " + reason);
        const bool unfinished = host.chosenSuperWeaponState() == RuntimeMapLeftSuperWeaponState::Charging
            || host.chosenSuperWeaponState() == RuntimeMapLeftSuperWeaponState::Suspended;
        if (!unfinished && !host.suppressPlayerAudio()) host.emitSound(RuntimeMapLeftSound::Blocked);
    }
    repaintAndRedraw(host);
    return true;
}
bool handleRuntimeMapLeftSidebarDown(RuntimeInputPixelRect client, RuntimeInputPixelRect sidebar,
    RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {
    const auto radar = host.radarRect(sidebar);
    const auto cell = host.radarCellAtPoint(radar, input.point);
    if (cell) {
        host.centerCamera(client, *cell);
        host.invalidateProjection();
        host.setStatus("radar jump " + std::to_string(cell->x) + "," + std::to_string(cell->y));
        finishLeftGesture(host);
        repaintAndRedraw(host);
        return true;
    }
    host.ensureSuperWeaponsInitialized();
    if (auto choice = host.superWeaponAtPoint(sidebar, input.point))
        return beginRuntimeMapLeftSuperWeapon(std::move(*choice), host);
    const auto tool = host.sidebarToolAtPoint(sidebar, input.point);
    if (tool) {
        host.setActiveTool(host.activeTool() == *tool ? RuntimeSidebarTool::None : *tool);
        host.setStatus(host.activeTool() == RuntimeSidebarTool::None ? "sidebar tool canceled"
            : host.sidebarToolLabel(host.activeTool()));
        host.emitSound(RuntimeMapLeftSound::Tab);
        repaintAndRedraw(host);
        return true;
    }
    const auto tab = host.buildTabAtPoint(sidebar, input.point);
    if (tab) {
        host.setBuildTab(*tab);
        host.clampBuildPage(sidebar);
        host.setStatus("build tab " + host.buildTabLabel(*tab));
        host.emitSound(RuntimeMapLeftSound::Tab);
        repaintAndRedraw(host);
        return true;
    }
    const auto delta = host.buildPageButtonAtPoint(sidebar, input.point);
    if (delta != 0) {
        const auto count = host.buildPageCount(sidebar);
        const auto next = std::clamp(host.activeBuildPage() + delta, 0, count - 1);
        host.setActiveBuildPage(next);
        host.setStatus("build page " + std::to_string(next + 1) + "/" + std::to_string(count));
        host.emitSound(RuntimeMapLeftSound::Button);
        repaintAndRedraw(host);
        return true;
    }
    const auto index = host.buildButtonAtPoint(sidebar, input.point);
    if (index) {
        if (host.prepareReadyProduction(*index)) {
            if (host.preparedReadyCatalogAvailable()) host.beginPreparedReadyPlacement();
            host.setStatus("placing " + host.catalogTypeId(*index));
            host.emitSound(RuntimeMapLeftSound::Button);
        } else {
            const bool addNext = host.modifierDown(RuntimeMapLeftModifier::Shift);
            host.queueProduction(*index, addNext);
        }
        repaintAndRedraw(host);
    }
    return true;
}

bool submitRuntimeMapLeftSuperWeapon(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point,
    RuntimeMapLeftPointerHost& host) {
    host.ensureSuperWeaponsInitialized();
    if (host.usesIntentRouter()) {
        if (!host.routerSuperWeaponPresent()) return false;
        RuntimeMultiplayerGameplaySuperWeaponTarget target;
        target.cell = host.surfaceCell(viewport, point);
        host.prepareProjection(viewport);
        host.pickProjection(point);
        if (host.pickedObjectPresent() && !host.superWeaponTargetsGroundArea(host.routerSuperWeaponId()))
            target.objectId = host.pickedObjectId();
        host.refreshAuthority();
        std::string error;
        const bool accepted = host.routerTargetSuperWeapon(std::move(target), error);
        if (accepted) {
            const auto remaining = host.routerSuperWeaponRemaining();
            if (remaining && remaining->expectedTargetCount == 2 && remaining->targetCount == 1)
                host.beginChronosphereMarker(remaining->id, remaining->firstTargetCell);
            else if (!remaining) host.clearChronosphereMarker();
            host.setStatus(host.routerSuperWeaponPresent() ? "superweapon choose destination"
                : (host.multiplayerBridgePresent() ? "superweapon launch queued for multiplayer" : "superweapon launched"));
        } else {
            host.setStatus("superweapon target rejected: " + error);
            host.emitSound(RuntimeMapLeftSound::Blocked);
        }
        finishLeftGesture(host);
        return true;
    }
    const auto owner = leftPointerLower(leftPointerTrim(host.economyOwner()));
    std::optional<RuntimeMapLeftSuperWeaponSnapshot> active;
    for (const auto& snapshot : host.superWeaponSnapshots()) {
        if (snapshot.targetSelectionActive
            && (owner.empty() || leftPointerLower(leftPointerTrim(snapshot.owner)) == owner)) {
            active = snapshot;
            break;
        }
    }
    if (!active) return false;
    const auto activeOwner = active->owner;
    const auto activeId = active->id;
    RuntimeMultiplayerGameplaySuperWeaponTarget target;
    target.cell = host.surfaceCell(viewport, point);
    target.objectId = -1;
    host.prepareProjection(viewport);
    host.pickProjection(point);
    if (host.pickedObjectPresent() && !host.superWeaponTargetsGroundArea(activeId))
        target.objectId = host.pickedObjectId();
    host.prepareSuperWeaponContext();
    host.prepareParasiteHostPoses();
    std::string reason;
    const bool accepted = host.offlineSubmitSuperWeapon(activeOwner, activeId, std::move(target), reason);
    host.notifyParasiteHostsRelocated();
    host.drainObjectExitQueue();
    if (accepted) {
        host.flushSuperWeaponHostEvents();
        const auto next = host.superWeaponState(activeOwner, activeId);
        host.setStatus(next == RuntimeMapLeftSuperWeaponState::AwaitingSecondTarget
            ? "superweapon choose destination" : "superweapon launch queued");
    } else {
        host.setStatus("superweapon target rejected: " + reason);
        host.setEconomyStatus(host.interactionStatus());
        if (!host.suppressPlayerAudio()) host.emitSound(RuntimeMapLeftSound::Blocked);
    }
    finishLeftGesture(host);
    return true;
}

bool handleRuntimeMapLeftWaypointClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point,
    RuntimeMapLeftPointerHost& host) {
    if (!host.planningWaypoints() || host.followTargeting() || host.rallyTargeting()
        || host.beaconTargeting() || host.activeTool() != RuntimeSidebarTool::None) return false;
    const auto owner = host.canonicalHumanOwner();
    std::uint64_t nearestId = 0;
    long long nearestDistance = 145;
    bool canClose = false;
    for (const auto& route : host.waypointRoutes()) {
        if (route.owner != owner) continue;
        for (std::size_t i = 0; i < route.points.size(); ++i) {
            const auto position = host.waypointScreenPoint(viewport, route.points[i]);
            if (!position) continue;
            const long long dx = point.x - position->x, dy = point.y - position->y;
            const long long distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestId = route.points[i].id;
                nearestDistance = distance;
                canClose = !route.loopStart && i + 1 < route.points.size() && host.selectionContains(route.actorId);
            }
        }
    }
    host.setSelectedWaypoint(nearestId);
    if (!nearestId) return false;
    if (canClose) (void)host.closeWaypointLoop(nearestId);
    host.gestureState().moved = false;
    host.gestureState().active = false;
    host.requestRepaint();
    return true;
}
bool handleRuntimeMapLeftTargetClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point,
    RuntimeMapLeftPointerHost& host) {
    if (!runtimeInputPixelPointInRectInclusive(viewport, point)) return false;
    const auto targetCell = host.surfaceCell(viewport, point);
    if (host.beaconTargeting()) {
        if (host.dispatchBeacon(targetCell)) {
            host.setBeaconTargeting(false);
            host.applySelection({}, false, false);
            host.clearSelectedBeacon();
        }
        // Preserve the original chained assignment's right-to-left order.
        host.gestureState().moved = false;
        host.gestureState().active = false;
        host.requestRepaint();
        return true;
    }
    if (handleRuntimeMapLeftWaypointClick(viewport, point, host)) return true;
    if (!host.followTargeting() && !host.rallyTargeting() && host.activeTool() == RuntimeSidebarTool::None
        && !host.modifierDown(RuntimeMapLeftModifier::Control)
        && !host.modifierDown(RuntimeMapLeftModifier::Alt)
        && !host.modifierDown(RuntimeMapLeftModifier::Shift)) {
        const auto owner = host.communicationViewerOwner();
        for (const auto& beacon : host.beacons()) {
            if (beacon.owner != owner || !host.beaconVisible(beacon)) continue;
            const double dx = static_cast<double>(targetCell.x) - beacon.cell.x;
            const double dy = static_cast<double>(targetCell.y) - beacon.cell.y;
            if (dx * dx + dy * dy < 0.25) {
                host.applySelection({}, false, false);
                host.selectBeacon(beacon.cell);
                host.gestureState().moved = false;
                host.gestureState().active = false;
                host.requestRepaint();
                return true;
            }
        }
        host.clearSelectedBeacon();
    }
    if (!host.followTargeting() && !host.rallyTargeting()) return false;
    if (host.rallyTargeting()) {
        if (host.selectionSize() == 1) (void)host.dispatchFactory(host.selectionFront(), targetCell);
        host.setRallyTargeting(false);
    } else {
        host.prepareProjection(viewport);
        host.pickProjection(point);
        if (host.pickedObjectPresent())
            (void)host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Follow, targetCell,
                RuntimeMapLeftTargetSource::PickedObject);
        else host.setStatus("follow requires an allied unit");
        host.setFollowTargeting(false);
    }
    finishLeftGesture(host);
    host.requestRepaint();
    return true;
}

bool handleRuntimeMapLeftPointerDown(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {
    if (!host.worldLoaded()) return false;
    const auto client = host.clientRect();
    host.setMouseValid(true);
    host.setMousePoint(input.point);
    if (!runtimeInputPixelPointInRectInclusive(client, input.point)) return false;
    if (host.menuOpen()) return handleRuntimeMapLeftMenuDown(client, input, host);
    const auto sidebar = host.sidebarRect(client);
    if (runtimeInputPixelPointInRectInclusive(sidebar, input.point))
        return handleRuntimeMapLeftSidebarDown(client, sidebar, input, host);
    if (host.rejectObserverSelection()) return true;
    host.focusWindow();
    host.captureWindow();
    host.restoreForceAttackAfterCapture(input.rawButtons);
    host.setMousePoint(input.point);
    host.requestRepaint();
    host.gestureState().active = true;
    host.gestureState().moved = false;
    host.gestureState().start = input.point;
    host.gestureState().current = input.point;
    host.setStatus("selecting");
    return true;
}
bool handleRuntimeMapLeftPointerMove(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {
    const bool changed = !host.mouseValid()
        || host.mousePoint().x != input.point.x || host.mousePoint().y != input.point.y;
    host.setMouseValid(true);
    host.setMousePoint(input.point);
    if (changed) host.requestRepaint();
    if (host.menuOpen()) return handleRuntimeMapLeftMenuMove(host.clientRect(), input, host);
    if (host.handleExistingRightMove(input)) return true;
    if (host.selectedPlacementPresent()) {
        const auto client = host.clientRect();
        const auto viewport = host.mapViewport(client);
        if (runtimeInputPixelPointInRectInclusive(viewport, input.point)) {
            host.updatePlacementPreview(viewport, input.point);
            host.requestRepaint();
        }
    }
    if (!host.gestureState().active) return false;
    host.gestureState().current = input.point;
    const int dx = input.point.x - host.gestureState().start.x;
    const int dy = input.point.y - host.gestureState().start.y;
    if (dx * dx + dy * dy > 16) host.gestureState().moved = true;
    host.requestRepaint();
    return true;
}
bool handleRuntimeMapLeftPointerUp(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {
    const auto client = host.clientRect();
    host.setMouseValid(true);
    host.setMousePoint(input.point);
    if (host.menuOpen()) return handleRuntimeMapLeftMenuUp(client, input, host);
    if (host.ownsCapture()) host.releaseCapture();
    if (host.rejectObserverSelection()) return true;
    const auto viewport = host.mapViewport(client);
    const bool inViewport = runtimeInputPixelPointInRectInclusive(viewport, input.point);
    if (inViewport && submitRuntimeMapLeftSuperWeapon(viewport, input.point, host)) {
        repaintAndRedraw(host);
        return true;
    }
    if (!host.gestureState().active) return false;
    host.gestureState().current = input.point;
    if (host.activeTool() != RuntimeSidebarTool::None && inViewport) {
        host.prepareProjection(viewport);
        host.pickProjection(input.point);
        bool handled = false;
        if (host.pickedObjectPresent() && host.pickedObjectIsStructure()) {
            if (host.activeTool() == RuntimeSidebarTool::Sell)
                handled = host.dispatchStructureTool(RuntimeSidebarTool::Sell, host.pickedObjectId());
            else if (host.activeTool() == RuntimeSidebarTool::Repair)
                handled = host.dispatchStructureTool(RuntimeSidebarTool::Repair, host.pickedObjectId());
        } else host.setEconomyStatus(host.sidebarToolLabel(host.activeTool()) + " needs a structure");
        if (handled) {
            if (host.activeTool() != RuntimeSidebarTool::Sell) host.setActiveTool(RuntimeSidebarTool::None);
            host.setStatus(host.economyStatus());
        } else {
            const bool noCredits = leftPointerLower(host.economyStatus()).find("no credits") != std::string::npos;
            host.emitSound(noCredits ? RuntimeMapLeftSound::InsufficientFunds : RuntimeMapLeftSound::Scold);
        }
        finishLeftGesture(host);
        repaintAndRedraw(host);
        return true;
    }
    if (host.selectedPlacementPresent()) {
        if (inViewport) {
            host.updatePlacementPreview(viewport, input.point);
            if (host.usesIntentRouter()) {
                const bool previewWasValid = host.placementValid();
                host.refreshAuthority();
                std::string error;
                const bool accepted = host.confirmPlacement(error);
                host.notePlacementResult(accepted, error,
                    previewWasValid ? RuntimeMapLeftSound::Scold : RuntimeMapLeftSound::CannotDeployHere);
            } else host.placeReadyBuilding(host.placementCell());
        }
        finishLeftGesture(host);
        repaintAndRedraw(host);
        return true;
    }
    host.prepareProjection(viewport);
    std::vector<std::int32_t> selected;
    if (!host.gestureState().moved && handleRuntimeMapLeftTargetClick(viewport, input.point, host)) return true;
    const bool append = (input.rawButtons & 4U) != 0;
    const bool toggle = (input.rawButtons & 8U) != 0;
    const bool forceFire = toggle && !host.selectionEmpty() && inViewport;
    const bool alt = host.modifierDown(RuntimeMapLeftModifier::Alt) && !host.selectionEmpty() && inViewport;
    const bool attackMove = toggle && append && !alt && !host.selectionEmpty() && inViewport;
    if ((alt || attackMove) && !host.gestureState().moved) {
        host.pickProjection(input.point);
        const auto cell = host.surfaceCell(viewport, input.point);
        const auto order = runtimePlayerModifiedTargetOrder(toggle, append, alt, host.pickedObjectPresent());
        (void)host.dispatchTargetOrder(order, cell,
            order == RuntimeMultiplayerGameplayTargetOrderKind::Follow
                ? RuntimeMapLeftTargetSource::PickedObject : RuntimeMapLeftTargetSource::None);
    } else if (host.gestureState().moved) {
        const auto start = host.gestureState().start;
        const auto current = host.gestureState().current;
        const RuntimeInputPixelRect drag{std::min(start.x, current.x), std::min(start.y, current.y),
            std::max(start.x, current.x), std::max(start.y, current.y)};
        for (const auto& item : host.boxProjectionItems()) {
            if (item.objectId && host.objectCanBeBoxSelected(*item.objectId)
                && runtimeInputPixelRectsIntersect(drag, item.bounds)) selected.push_back(*item.objectId);
        }
        host.applySelection(std::move(selected), append, toggle);
        host.clearSelectedBeacon();
    } else {
        host.pickProjection(input.point);
        const bool hasMovable = host.selectedMovableCount() > 0;
        const bool wall = (forceFire || (!append && !toggle))
            ? host.prepareTerrainWall(viewport, input.point) : false;
        if (forceFire) {
            const bool targetPresent = wall || host.pickedObjectPresent();
            const auto cell = host.surfaceCell(viewport, input.point);
            (void)host.dispatchTargetOrder(targetPresent ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
                : RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell, cell,
                wall ? RuntimeMapLeftTargetSource::TerrainWall
                    : RuntimeMapLeftTargetSource::PickedObject);
        } else if (!append && !toggle && wall) {
            const auto cell = host.preparedTerrainWallCell();
            (void)host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Primary, cell,
                RuntimeMapLeftTargetSource::TerrainWall);
        } else if (!append && !toggle && inViewport && !host.pickedObjectPresent()
            && host.mouseFactoryCanRally()) {
            const auto cell = host.surfaceCell(viewport, input.point);
            (void)host.dispatchFactory(host.selectionFront(), cell);
        } else if (!append && !toggle && host.pickedObjectPresent()
            && host.mouseFactoryCanBePrimary(host.pickedObjectId())) {
            (void)host.dispatchFactory(host.pickedObjectId(), std::nullopt);
        } else if (!append && !toggle && host.pickedObjectPresent()
            && host.selectionContains(host.pickedObjectId()) && host.dispatchSecondary(host.pickedObjectId())) {
            // A successful original secondary action consumes the click.
        } else if (!append && !toggle && hasMovable
            && (!host.pickedObjectPresent() || !host.selectionContains(host.pickedObjectId()))) {
            const auto cell = host.surfaceCell(viewport, input.point);
            (void)host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Primary, cell,
                RuntimeMapLeftTargetSource::PickedObject);
        } else if (host.pickedObjectPresent()) {
            selected.push_back(host.pickedObjectId());
            host.applySelection(std::move(selected), append, toggle);
            host.clearSelectedBeacon();
        } else {
            host.applySelection(std::move(selected), append, toggle);
            host.clearSelectedBeacon();
        }
    }
    finishLeftGesture(host);
    repaintAndRedraw(host);
    return true;
}
