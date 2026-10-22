#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/input/RuntimeMenuValues.h"
#include "ui/input/RuntimePlayerCommands.h"
#include "ui/input/RuntimePlayerCommunication.h"
#include "ui/input/RuntimeReferencePresentationController.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Each port is called at its original query/effect position.
// Construction of the private adapter performs no query.
struct RuntimeMapLeftPointerInput {
    RuntimeInputPixelPoint point;
    std::uint64_t rawButtons = 0;
};

struct RuntimeMapLeftPointerState {
    bool active = false;
    bool moved = false;
    RuntimeInputPixelPoint start;
    RuntimeInputPixelPoint current;
};

enum class RuntimeMapLeftSound : std::uint8_t {
    Button, Tab, Blocked, InsufficientFunds, Scold, CannotDeployHere
};
enum class RuntimeMapLeftModifier : std::uint8_t { Control, Alt, Shift };
enum class RuntimeMapLeftSuperWeaponState : std::uint8_t {
    Other, Ready, Charging, Suspended, AwaitingSecondTarget
};
enum class RuntimeMapLeftTargetSource : std::uint8_t { None, PickedObject, TerrainWall };

struct RuntimeMapLeftMenuControl {
    bool slider = false;
    RuntimeInputPixelRect valueRect;
};
struct RuntimeMapLeftSuperWeaponChoice {
    std::string owner;
    std::string id;
    std::string name;
};
struct RuntimeMapLeftSuperWeaponSnapshot {
    std::string owner;
    std::string id;
    bool targetSelectionActive = false;
};
struct RuntimeMapLeftSuperWeaponRemaining {
    std::string id;
    std::size_t expectedTargetCount = 0;
    std::size_t targetCount = 0;
    RuntimeMultiplayerGameplayCell firstTargetCell;
};
struct RuntimeMapLeftBoxItem {
    std::optional<std::int32_t> objectId;
    RuntimeInputPixelRect bounds;
};
struct RuntimeMapLeftWaypointRoute {
    std::int32_t actorId = 0;
    std::string owner;
    std::uint64_t loopStart = 0;
    std::vector<RuntimePlayerWaypoint> points;
};

class RuntimeMapLeftPointerHost {
public:
    virtual ~RuntimeMapLeftPointerHost() = default;
    virtual RuntimeMapLeftPointerState& gestureState() = 0;
    virtual bool worldLoaded() = 0;
    virtual RuntimeInputPixelRect clientRect() = 0;
    virtual RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) = 0;
    virtual RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) = 0;
    virtual void setMouseValid(bool value) = 0;
    virtual void setMousePoint(RuntimeInputPixelPoint point) = 0;
    virtual bool mouseValid() = 0;
    virtual RuntimeInputPixelPoint mousePoint() = 0;
    virtual bool menuOpen() = 0;
    virtual bool rejectObserverSelection() = 0;
    virtual void focusWindow() = 0;
    virtual void captureWindow() = 0;
    virtual bool ownsCapture() = 0;
    virtual void releaseCapture() = 0;
    virtual void restoreForceAttackAfterCapture(std::uint64_t rawButtons) = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
    virtual void invalidateProjection() = 0;
    virtual void setStatus(std::string text) = 0;
    virtual std::string interactionStatus() = 0;
    virtual std::string economyStatus() = 0;
    virtual void setEconomyStatus(std::string text) = 0;
    virtual void emitSound(RuntimeMapLeftSound sound) = 0;
    virtual bool suppressPlayerAudio() = 0;

    virtual RuntimeMenuAction menuActionAtPoint(RuntimeInputPixelRect client, RuntimeInputPixelPoint point) = 0;
    virtual std::vector<RuntimeMenuAction> menuFocusableActions(RuntimeInputPixelRect client) = 0;
    virtual void setMenuFocus(std::int32_t index) = 0;
    virtual RuntimeMenuAction menuPressed() = 0;
    virtual void setMenuPressed(RuntimeMenuAction action) = 0;
    virtual RuntimeMenuAction menuSliderDrag() = 0;
    virtual void setMenuSliderDrag(RuntimeMenuAction action) = 0;
    virtual RuntimeMenuAction menuHover() = 0;
    virtual void setMenuHover(RuntimeMenuAction action) = 0;
    virtual std::optional<RuntimeMapLeftMenuControl> menuControl(RuntimeInputPixelRect client, RuntimeMenuAction action) = 0;
    virtual bool setMenuSliderFromX(RuntimeInputPixelRect client, RuntimeMenuAction action, std::int32_t x) = 0;
    virtual void applyMenuSliderRuntimeEffects(RuntimeMenuAction action) = 0;
    virtual void clearMenuStatus() = 0;
    virtual void persistMenuPreferences() = 0;
    virtual void activateMenuAction(RuntimeMenuAction action) = 0;

    virtual RuntimeInputPixelRect radarRect(RuntimeInputPixelRect sidebar) = 0;
    virtual std::optional<RuntimeMultiplayerGameplayCell> radarCellAtPoint(RuntimeInputPixelRect radar, RuntimeInputPixelPoint point) = 0;
    virtual void centerCamera(RuntimeInputPixelRect client, RuntimeMultiplayerGameplayCell cell) = 0;
    virtual std::optional<RuntimeSidebarTool> sidebarToolAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual RuntimeSidebarTool activeTool() = 0;
    virtual void setActiveTool(RuntimeSidebarTool tool) = 0;
    virtual std::string sidebarToolLabel(RuntimeSidebarTool tool) = 0;
    virtual std::optional<RuntimeBuildKind> buildTabAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual void setBuildTab(RuntimeBuildKind kind) = 0;
    virtual void clampBuildPage(RuntimeInputPixelRect sidebar) = 0;
    virtual std::string buildTabLabel(RuntimeBuildKind kind) = 0;
    virtual std::int32_t buildPageButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual std::int32_t buildPageCount(RuntimeInputPixelRect sidebar) = 0;
    virtual std::int32_t activeBuildPage() = 0;
    virtual void setActiveBuildPage(std::int32_t page) = 0;
    virtual std::optional<std::int32_t> buildButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    // The private adapter retains the original item/ready-order observation;
    // only the old catalog lookup and placement primitives remain private.
    virtual bool prepareReadyProduction(std::int32_t catalogIndex) = 0;
    virtual bool preparedReadyCatalogAvailable() = 0;
    virtual void beginPreparedReadyPlacement() = 0;
    virtual std::string catalogTypeId(std::int32_t catalogIndex) = 0;
    virtual bool modifierDown(RuntimeMapLeftModifier modifier) = 0;
    virtual void queueProduction(std::int32_t catalogIndex, bool afterActive) = 0;

    virtual void ensureSuperWeaponsInitialized() = 0;
    virtual std::optional<RuntimeMapLeftSuperWeaponChoice> superWeaponAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    // Retains the original chosen sidebar snapshot privately; this state read
    // stays fresh after authority refresh and again in the failure branch.
    virtual RuntimeMapLeftSuperWeaponState chosenSuperWeaponState() = 0;
    virtual void prepareSuperWeaponContext() = 0;
    virtual bool usesIntentRouter() = 0;
    virtual void refreshAuthority() = 0;
    virtual std::optional<std::string> superWeaponTargetMode(const std::string& id) = 0;
    virtual bool routerBeginSuperWeapon(const std::string& id, std::size_t count, std::string& error) = 0;
    virtual bool routerConfirmSuperWeapon(std::string& error) = 0;
    virtual bool offlineBeginSuperWeapon(const std::string& owner, const std::string& id, std::string& error) = 0;
    virtual bool multiplayerBridgePresent() = 0;
    virtual bool routerSuperWeaponPresent() = 0;
    virtual std::string routerSuperWeaponId() = 0;
    virtual std::optional<RuntimeMapLeftSuperWeaponRemaining> routerSuperWeaponRemaining() = 0;
    virtual bool superWeaponTargetsGroundArea(const std::string& id) = 0;
    virtual bool routerTargetSuperWeapon(RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) = 0;
    virtual void beginChronosphereMarker(const std::string& id, RuntimeMultiplayerGameplayCell cell) = 0;
    virtual void clearChronosphereMarker() = 0;
    virtual std::string economyOwner() = 0;
    virtual std::vector<RuntimeMapLeftSuperWeaponSnapshot> superWeaponSnapshots() = 0;
    virtual void prepareParasiteHostPoses() = 0;
    virtual bool offlineSubmitSuperWeapon(const std::string& owner, const std::string& id, RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) = 0;
    virtual void notifyParasiteHostsRelocated() = 0;
    virtual void drainObjectExitQueue() = 0;
    virtual void flushSuperWeaponHostEvents() = 0;
    virtual RuntimeMapLeftSuperWeaponState superWeaponState(const std::string& owner, const std::string& id) = 0;

    virtual RuntimeMultiplayerGameplayCell surfaceCell(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    // These retain owned scene geometry and IDs at the original query positions.
    // Pick uses the original picker with a transient fresh-root object view;
    // no borrow survives a load/dispatch and no renderer object escapes.
    virtual void prepareProjection(RuntimeInputPixelRect viewport) = 0;
    virtual void pickProjection(RuntimeInputPixelPoint point) = 0;
    virtual bool pickedObjectPresent() = 0;
    virtual bool pickedObjectIsStructure() = 0;
    virtual std::int32_t pickedObjectId() = 0;
    virtual std::vector<RuntimeMapLeftBoxItem> boxProjectionItems() = 0;
    virtual bool objectCanBeBoxSelected(std::int32_t id) = 0;
    virtual bool prepareTerrainWall(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual RuntimeMultiplayerGameplayCell preparedTerrainWallCell() = 0;
    virtual bool dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind order, RuntimeMultiplayerGameplayCell cell, RuntimeMapLeftTargetSource target) = 0;
    virtual bool selectionEmpty() = 0;
    virtual std::size_t selectionSize() = 0;
    virtual std::int32_t selectionFront() = 0;
    virtual bool selectionContains(std::int32_t id) = 0;
    virtual std::int32_t selectedMovableCount() = 0;
    virtual void applySelection(std::vector<std::int32_t> ids, bool append, bool toggle) = 0;
    virtual void clearSelectedBeacon() = 0;
    virtual void selectBeacon(RuntimeMultiplayerGameplayCell cell) = 0;
    virtual bool beaconTargeting() = 0;
    virtual void setBeaconTargeting(bool value) = 0;
    virtual bool dispatchBeacon(RuntimeMultiplayerGameplayCell cell) = 0;
    virtual std::string communicationViewerOwner() = 0;
    virtual std::vector<RuntimePlayerBeacon> beacons() = 0;
    virtual bool beaconVisible(const RuntimePlayerBeacon& beacon) = 0;
    virtual bool followTargeting() = 0;
    virtual void setFollowTargeting(bool value) = 0;
    virtual bool rallyTargeting() = 0;
    virtual void setRallyTargeting(bool value) = 0;
    virtual bool planningWaypoints() = 0;
    virtual std::string canonicalHumanOwner() = 0;
    virtual std::vector<RuntimeMapLeftWaypointRoute> waypointRoutes() = 0;
    virtual std::optional<RuntimeInputPixelPoint> waypointScreenPoint(RuntimeInputPixelRect viewport, const RuntimePlayerWaypoint& waypoint) = 0;
    virtual void setSelectedWaypoint(std::uint64_t id) = 0;
    virtual bool closeWaypointLoop(std::uint64_t id) = 0;
    virtual bool mouseFactoryCanRally() = 0;
    virtual bool mouseFactoryCanBePrimary(std::int32_t id) = 0;
    virtual bool dispatchFactory(std::int32_t id, std::optional<RuntimeMultiplayerGameplayCell> cell) = 0;
    virtual bool dispatchSecondary(std::int32_t id) = 0;
    virtual bool dispatchStructureTool(RuntimeSidebarTool tool, std::int32_t id) = 0;
    virtual bool selectedPlacementPresent() = 0;
    virtual void updatePlacementPreview(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual bool placementValid() = 0;
    virtual bool confirmPlacement(std::string& error) = 0;
    virtual void notePlacementResult(bool accepted, const std::string& error, RuntimeMapLeftSound rejectedSound) = 0;
    virtual RuntimeMultiplayerGameplayCell placementCell() = 0;
    virtual void placeReadyBuilding(RuntimeMultiplayerGameplayCell cell) = 0;
    virtual bool handleExistingRightMove(const RuntimeMapLeftPointerInput& input) = 0;
};

bool beginRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, RuntimeMenuAction action, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host);
bool updateRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, std::int32_t x, RuntimeMapLeftPointerHost& host);
bool finishRuntimeMapLeftMenuSlider(RuntimeInputPixelRect client, std::int32_t x, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftMenuDown(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftMenuMove(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftMenuUp(RuntimeInputPixelRect client, RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool beginRuntimeMapLeftSuperWeapon(RuntimeMapLeftSuperWeaponChoice choice, RuntimeMapLeftPointerHost& host);
bool submitRuntimeMapLeftSuperWeapon(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftSidebarDown(RuntimeInputPixelRect client, RuntimeInputPixelRect sidebar, RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftWaypointClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftTargetClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftPointerDown(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftPointerMove(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
bool handleRuntimeMapLeftPointerUp(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host);
