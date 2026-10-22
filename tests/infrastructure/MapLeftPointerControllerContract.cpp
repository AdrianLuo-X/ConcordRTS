#include "ui/input/RuntimeMapLeftPointerController.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_WINUSER_)
#error Standard left-pointer contract must not include Windows headers.
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {
using Host = RuntimeMapLeftPointerHost;
using Input = RuntimeMapLeftPointerInput;
using Point = RuntimeInputPixelPoint;
using Rect = RuntimeInputPixelRect;
using Cell = RuntimeMultiplayerGameplayCell;
using Action = RuntimeMenuAction;
using Tool = RuntimeSidebarTool;
using SWState = RuntimeMapLeftSuperWeaponState;

std::string oldLower(std::string value) {
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}
std::string oldTrim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}
bool oldHalfOpen(Rect rect, Point point) {
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}
Rect oldNormalized(Point a, Point b) {
    return {std::min(a.x,b.x), std::min(a.y,b.y), std::max(a.x,b.x), std::max(a.y,b.y)};
}

template<class T> std::string show(const T& value);
template<class T> std::string show(const std::optional<T>& value);
template<class T> std::string show(const std::vector<T>& values);
std::string show(const std::string& value) { return std::to_string(value.size()) + ":" + value; }
std::string show(Point v) { return "(" + std::to_string(v.x) + "," + std::to_string(v.y) + ")"; }
std::string show(Rect v) { return "(" + std::to_string(v.left) + "," + std::to_string(v.top)
    + "," + std::to_string(v.right) + "," + std::to_string(v.bottom) + ")"; }
std::string show(Cell v) { return "(" + std::to_string(v.x) + "," + std::to_string(v.y) + ")"; }
std::string show(const RuntimeMapLeftPointerInput& v) { return show(v.point) + ":" + std::to_string(v.rawButtons); }
std::string show(const RuntimeMapLeftMenuControl& v) { return std::to_string(v.slider) + show(v.valueRect); }
std::string show(const RuntimeMapLeftSuperWeaponChoice& v) { return show(v.owner)+show(v.id)+show(v.name); }
std::string show(const RuntimeMapLeftSuperWeaponSnapshot& v) { return show(v.owner)+show(v.id)+std::to_string(v.targetSelectionActive); }
std::string show(const RuntimeMapLeftSuperWeaponRemaining& v) { return show(v.id)+":"
    +std::to_string(v.expectedTargetCount)+":"+std::to_string(v.targetCount)+show(v.firstTargetCell); }
std::string show(const RuntimeMapLeftBoxItem& v) { return show(v.objectId)+show(v.bounds); }
std::string show(const RuntimePlayerBeacon& v) { return show(v.owner)+show(v.cell)+":"
    +std::to_string(v.untilTick)+show(v.text)+":"+std::to_string(v.level); }
std::string show(const RuntimeMultiplayerGameplaySuperWeaponTarget& v) { return show(v.cell)+show(v.objectId); }
std::string show(const RuntimePlayerWaypoint& v) {
    std::string target = std::visit([](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T,RuntimeMultiplayerGameplayCellTarget>) return std::string("cell")+show(item.cell);
        else return std::string("object")+std::to_string(item.objectId);
    },v.order.target);
    return std::to_string(v.id)+":"+show(v.order.order)+show(v.order.objectIds)+target+show(v.order.append);
}
std::string show(const RuntimeMapLeftWaypointRoute& v) { return std::to_string(v.actorId)+":"
    +show(v.owner)+":"+std::to_string(v.loopStart)+show(v.points); }
template<class T> std::string show(const std::optional<T>& value) { return value ? "some("+show(*value)+")" : "none"; }
template<class T> std::string show(const std::vector<T>& values) {
    std::string out = "["+std::to_string(values.size())+":";
    for(const auto& value:values) out += show(value)+";";
    return out+"]";
}
template<class T> std::string show(const T& value) {
    if constexpr(std::is_enum_v<T>) return std::to_string(static_cast<std::underlying_type_t<T>>(value));
    else if constexpr(std::is_integral_v<T>) return std::to_string(value);
    else static_assert(std::is_integral_v<T>,"Every owned parameter/result needs full observation.");
}

struct Row {
    std::string method, parameters, result, before, after;
    bool exceptional = false;
    bool operator==(const Row&) const = default;
};
struct PrimitiveFailure final : std::exception {};
struct Model final : Host {
    RuntimeMapLeftPointerState gesture;
    std::vector<std::int32_t> selected{7};
    std::array<bool,3> modifiers{};
    std::set<std::int32_t> ineligibleBoxIds;
    std::map<std::uint64_t,std::optional<Point>> waypointPositions;
    std::vector<std::string> commands;
    std::vector<Row> trace;
    std::function<void(Model&,const std::string&)> hook;
    std::size_t throwAt = static_cast<std::size_t>(-1);
    std::int32_t rootEpoch = 1, worldTick = 21, repaintCount = 0, redrawCount = 0;
    std::int32_t projectionEpoch = 0, preparedProjectionEpoch = 0, callbackCount = 0;
    Input eventSource{{100,100},0};
    bool oldOracle = false, windowAlive = true, callbackBusy = false;
    std::uint64_t worldHash = 0x123456789abcdef0ULL;
    bool v_worldLoaded = true;
    RuntimeInputPixelRect v_clientRect = {0,0,640,480};
    RuntimeInputPixelRect v_sidebarRect = {500,0,640,480};
    RuntimeInputPixelRect v_mapViewport = {0,0,500,480};
    bool v_mouseValid = {};
    RuntimeInputPixelPoint v_mousePoint = {};
    bool v_menuOpen = {};
    bool v_rejectObserverSelection = {};
    bool v_ownsCapture = {};
    std::string v_interactionStatus = "initial";
    std::string v_economyStatus = "initial-economy";
    bool v_suppressPlayerAudio = {};
    RuntimeMenuAction v_menuActionAtPoint = {};
    std::vector<RuntimeMenuAction> v_menuFocusableActions = {};
    RuntimeMenuAction v_menuPressed = {};
    RuntimeMenuAction v_menuSliderDrag = {};
    RuntimeMenuAction v_menuHover = {};
    std::optional<RuntimeMapLeftMenuControl> v_menuControl = {};
    bool v_setMenuSliderFromX = true;
    RuntimeInputPixelRect v_radarRect = {510,10,630,100};
    std::optional<RuntimeMultiplayerGameplayCell> v_radarCellAtPoint = {};
    std::optional<RuntimeSidebarTool> v_sidebarToolAtPoint = {};
    RuntimeSidebarTool v_activeTool = {};
    std::string v_sidebarToolLabel = "repair";
    std::optional<RuntimeBuildKind> v_buildTabAtPoint = {};
    std::string v_buildTabLabel = "building";
    std::int32_t v_buildPageButtonAtPoint = {};
    std::int32_t v_buildPageCount = 3;
    std::int32_t v_activeBuildPage = 1;
    std::optional<std::int32_t> v_buildButtonAtPoint = {};
    bool v_prepareReadyProduction = {};
    bool v_preparedReadyCatalogAvailable = {};
    std::string v_catalogTypeId = "FACT";
    bool v_modifierDown = {};
    std::optional<RuntimeMapLeftSuperWeaponChoice> v_superWeaponAtPoint = {};
    RuntimeMapLeftSuperWeaponState v_chosenSuperWeaponState = SWState::Ready;
    bool v_usesIntentRouter = {};
    std::optional<std::string> v_superWeaponTargetMode = std::string("cell");
    bool v_routerBeginSuperWeapon = true;
    bool v_routerConfirmSuperWeapon = true;
    bool v_offlineBeginSuperWeapon = true;
    bool v_multiplayerBridgePresent = {};
    bool v_routerSuperWeaponPresent = {};
    std::string v_routerSuperWeaponId = "Chrono";
    std::optional<RuntimeMapLeftSuperWeaponRemaining> v_routerSuperWeaponRemaining = {};
    bool v_superWeaponTargetsGroundArea = {};
    bool v_routerTargetSuperWeapon = true;
    std::string v_economyOwner = " GDI ";
    std::vector<RuntimeMapLeftSuperWeaponSnapshot> v_superWeaponSnapshots = {};
    bool v_offlineSubmitSuperWeapon = true;
    RuntimeMapLeftSuperWeaponState v_superWeaponState = SWState::Other;
    RuntimeMultiplayerGameplayCell v_surfaceCell = {4,6};
    bool v_pickedObjectPresent = {};
    bool v_pickedObjectIsStructure = {};
    std::int32_t v_pickedObjectId = 7;
    std::vector<RuntimeMapLeftBoxItem> v_boxProjectionItems = {};
    bool v_objectCanBeBoxSelected = true;
    bool v_prepareTerrainWall = {};
    RuntimeMultiplayerGameplayCell v_preparedTerrainWallCell = {7,8};
    bool v_dispatchTargetOrder = true;
    bool v_selectionEmpty = {};
    std::size_t v_selectionSize = {};
    std::int32_t v_selectionFront = {};
    bool v_selectionContains = {};
    std::int32_t v_selectedMovableCount = {};
    bool v_beaconTargeting = {};
    bool v_dispatchBeacon = true;
    std::string v_communicationViewerOwner = "gdi";
    std::vector<RuntimePlayerBeacon> v_beacons = {};
    bool v_beaconVisible = true;
    bool v_followTargeting = {};
    bool v_rallyTargeting = {};
    bool v_planningWaypoints = {};
    std::string v_canonicalHumanOwner = "gdi";
    std::vector<RuntimeMapLeftWaypointRoute> v_waypointRoutes = {};
    std::optional<RuntimeInputPixelPoint> v_waypointScreenPoint = {};
    bool v_closeWaypointLoop = true;
    bool v_mouseFactoryCanRally = {};
    bool v_mouseFactoryCanBePrimary = {};
    bool v_dispatchFactory = true;
    bool v_dispatchSecondary = true;
    bool v_dispatchStructureTool = true;
    bool v_selectedPlacementPresent = {};
    bool v_placementValid = {};
    bool v_confirmPlacement = true;
    RuntimeMultiplayerGameplayCell v_placementCell = {9,10};
    bool v_handleExistingRightMove = {};
    RuntimeBuildKind currentBuildTab = RuntimeBuildKind::Building;
    std::int32_t menuFocus = -1, sliderValue = 17, persistedRevision = 0;
    std::int32_t preparedItem = -1, contextEpoch = 0, parasiteEpoch = 0, swInitCount = 0;
    bool forceHeld = false, synchronousApply = false, synchronousDestroyOnActivate = false;
    std::uint64_t selectedWaypoint = 0;
    std::optional<Cell> selectedBeacon = Cell{20,21};
    std::string menuStatus = "menu-status";

    std::string stateText() const {
        std::string out = show(gesture.active)+show(gesture.moved)+show(gesture.start)+show(gesture.current)
            +show(selected)+show(commands)+show(rootEpoch)+show(worldTick)+show(worldHash)
            +show(repaintCount)+show(redrawCount)+show(windowAlive)+show(projectionEpoch)+show(preparedProjectionEpoch)
            +show(currentBuildTab)+show(menuFocus)+show(sliderValue)+show(persistedRevision)
            +show(preparedItem)+show(contextEpoch)+show(parasiteEpoch)+show(swInitCount)
            +show(forceHeld)+show(synchronousApply)+show(synchronousDestroyOnActivate)
            +show(selectedWaypoint)+show(selectedBeacon)+show(menuStatus)+show(callbackCount)+show(eventSource);
        for(bool modifier:modifiers) out += show(modifier);
        for(auto id:ineligibleBoxIds) out += show(id);
        for(const auto& [id,point]:waypointPositions) out += show(id)+show(point);
        out += show(v_worldLoaded)+"|"+show(v_clientRect)+"|"+show(v_sidebarRect)+"|"+show(v_mapViewport)+"|"+show(v_mouseValid)+"|"+show(v_mousePoint)+"|"+show(v_menuOpen)+"|"+show(v_rejectObserverSelection)+"|"+show(v_ownsCapture)+"|"+show(v_interactionStatus)+"|"+show(v_economyStatus)+"|"+show(v_suppressPlayerAudio)+"|"+show(v_menuActionAtPoint)+"|"+show(v_menuFocusableActions)+"|"+show(v_menuPressed)+"|"+show(v_menuSliderDrag)+"|"+show(v_menuHover)+"|"+show(v_menuControl)+"|"+show(v_setMenuSliderFromX)+"|"+show(v_radarRect)+"|"+show(v_radarCellAtPoint)+"|"+show(v_sidebarToolAtPoint)+"|"+show(v_activeTool)+"|"+show(v_sidebarToolLabel)+"|"+show(v_buildTabAtPoint)+"|"+show(v_buildTabLabel)+"|"+show(v_buildPageButtonAtPoint)+"|"+show(v_buildPageCount)+"|"+show(v_activeBuildPage)+"|"+show(v_buildButtonAtPoint)+"|"+show(v_prepareReadyProduction)+"|"+show(v_preparedReadyCatalogAvailable)+"|"+show(v_catalogTypeId)+"|"+show(v_modifierDown)+"|"+show(v_superWeaponAtPoint)+"|"+show(v_chosenSuperWeaponState)+"|"+show(v_usesIntentRouter)+"|"+show(v_superWeaponTargetMode)+"|"+show(v_routerBeginSuperWeapon)+"|"+show(v_routerConfirmSuperWeapon)+"|"+show(v_offlineBeginSuperWeapon)+"|"+show(v_multiplayerBridgePresent)+"|"+show(v_routerSuperWeaponPresent)+"|"+show(v_routerSuperWeaponId)+"|"+show(v_routerSuperWeaponRemaining)+"|"+show(v_superWeaponTargetsGroundArea)+"|"+show(v_routerTargetSuperWeapon)+"|"+show(v_economyOwner)+"|"+show(v_superWeaponSnapshots)+"|"+show(v_offlineSubmitSuperWeapon)+"|"+show(v_superWeaponState)+"|"+show(v_surfaceCell)+"|"+show(v_pickedObjectPresent)+"|"+show(v_pickedObjectIsStructure)+"|"+show(v_pickedObjectId)+"|"+show(v_boxProjectionItems)+"|"+show(v_objectCanBeBoxSelected)+"|"+show(v_prepareTerrainWall)+"|"+show(v_preparedTerrainWallCell)+"|"+show(v_dispatchTargetOrder)+"|"+show(v_selectionEmpty)+"|"+show(v_selectionSize)+"|"+show(v_selectionFront)+"|"+show(v_selectionContains)+"|"+show(v_selectedMovableCount)+"|"+show(v_beaconTargeting)+"|"+show(v_dispatchBeacon)+"|"+show(v_communicationViewerOwner)+"|"+show(v_beacons)+"|"+show(v_beaconVisible)+"|"+show(v_followTargeting)+"|"+show(v_rallyTargeting)+"|"+show(v_planningWaypoints)+"|"+show(v_canonicalHumanOwner)+"|"+show(v_waypointRoutes)+"|"+show(v_waypointScreenPoint)+"|"+show(v_closeWaypointLoop)+"|"+show(v_mouseFactoryCanRally)+"|"+show(v_mouseFactoryCanBePrimary)+"|"+show(v_dispatchFactory)+"|"+show(v_dispatchSecondary)+"|"+show(v_dispatchStructureTool)+"|"+show(v_selectedPlacementPresent)+"|"+show(v_placementValid)+"|"+show(v_confirmPlacement)+"|"+show(v_placementCell)+"|"+show(v_handleExistingRightMove);
        return out;
    }
    template<class... Args> std::size_t enter(const std::string& name, const Args&... args) {
        std::string values;
        ((values += show(args)+"|"),...);
        const auto index = trace.size();
        trace.push_back({name,std::move(values),{},stateText(),{},false});
        if(index==throwAt) {
            trace[index].result="<primitive-exception>";
            trace[index].after=stateText();
            trace[index].exceptional=true;
            throw PrimitiveFailure{};
        }
        return index;
    }
    void complete(std::size_t index, std::string result) {
        trace[index].result=std::move(result);
        try {
            const auto method=trace[index].method;
            if(hook) hook(*this,method);
        } catch(...) {
            trace[index].after=stateText();
            trace[index].exceptional=true;
            throw;
        }
        trace[index].after=stateText();
    }
    template<class T> T leave(std::size_t index, T value) {
        complete(index,show(value));
        return value;
    }
    void leave(std::size_t index) { complete(index,"void"); }
    void command(std::string text, bool accepted) {
        commands.push_back(std::move(text)+":"+show(accepted));
        if(accepted && synchronousApply) ++worldHash;
    }
    RuntimeMapLeftPointerState& gestureState() override { return gesture; }
    bool worldLoaded() override {
        const auto index=enter("worldLoaded");
        auto result=v_worldLoaded;
        return leave(index,result);
    }
    RuntimeInputPixelRect clientRect() override {
        const auto index=enter("clientRect");
        auto result=v_clientRect;
        return leave(index,result);
    }
    RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) override {
        const auto index=enter("sidebarRect",client);
        auto result=v_sidebarRect;
        return leave(index,result);
    }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) override {
        const auto index=enter("mapViewport",client);
        auto result=v_mapViewport;
        return leave(index,result);
    }
    void setMouseValid(bool value) override {
        const auto index=enter("setMouseValid",value);
        v_mouseValid=value;
        leave(index);
    }
    void setMousePoint(RuntimeInputPixelPoint point) override {
        const auto index=enter("setMousePoint",point);
        v_mousePoint=point;
        leave(index);
    }
    bool mouseValid() override {
        const auto index=enter("mouseValid");
        auto result=v_mouseValid;
        return leave(index,result);
    }
    RuntimeInputPixelPoint mousePoint() override {
        const auto index=enter("mousePoint");
        auto result=v_mousePoint;
        return leave(index,result);
    }
    bool menuOpen() override {
        const auto index=enter("menuOpen");
        auto result=v_menuOpen;
        return leave(index,result);
    }
    bool rejectObserverSelection() override {
        const auto index=enter("rejectObserverSelection");
        auto result=v_rejectObserverSelection;
        return leave(index,result);
    }
    void focusWindow() override {
        const auto index=enter("focusWindow");
        leave(index);
    }
    void captureWindow() override {
        const auto index=enter("captureWindow");
        v_ownsCapture=true;
        leave(index);
    }
    bool ownsCapture() override {
        const auto index=enter("ownsCapture");
        auto result=v_ownsCapture;
        return leave(index,result);
    }
    void releaseCapture() override {
        const auto index=enter("releaseCapture");
        v_ownsCapture=false;
        leave(index);
    }
    void restoreForceAttackAfterCapture(std::uint64_t rawButtons) override {
        const auto index=enter("restoreForceAttackAfterCapture",rawButtons);
        forceHeld=(rawButtons&8U)!=0;
        leave(index);
    }
    void requestRepaint() override {
        const auto index=enter("requestRepaint");
        ++repaintCount;
        leave(index);
    }
    void redraw() override {
        const auto index=enter("redraw");
        ++redrawCount;
        leave(index);
    }
    void invalidateProjection() override {
        const auto index=enter("invalidateProjection");
        ++projectionEpoch;
        leave(index);
    }
    void setStatus(std::string text) override {
        const auto index=enter("setStatus",text);
        v_interactionStatus=text;
        leave(index);
    }
    std::string interactionStatus() override {
        const auto index=enter("interactionStatus");
        auto result=v_interactionStatus;
        return leave(index,result);
    }
    std::string economyStatus() override {
        const auto index=enter("economyStatus");
        auto result=v_economyStatus;
        return leave(index,result);
    }
    void setEconomyStatus(std::string text) override {
        const auto index=enter("setEconomyStatus",text);
        v_economyStatus=text;
        leave(index);
    }
    void emitSound(RuntimeMapLeftSound sound) override {
        const auto index=enter("emitSound",sound);
        commands.push_back("sound:"+show(sound));
        leave(index);
    }
    bool suppressPlayerAudio() override {
        const auto index=enter("suppressPlayerAudio");
        auto result=v_suppressPlayerAudio;
        return leave(index,result);
    }
    RuntimeMenuAction menuActionAtPoint(RuntimeInputPixelRect client, RuntimeInputPixelPoint point) override {
        const auto index=enter("menuActionAtPoint",client,point);
        auto result=v_menuActionAtPoint;
        return leave(index,result);
    }
    std::vector<RuntimeMenuAction> menuFocusableActions(RuntimeInputPixelRect client) override {
        const auto index=enter("menuFocusableActions",client);
        auto result=v_menuFocusableActions;
        return leave(index,result);
    }
    void setMenuFocus(std::int32_t index) override {
        const auto rowIndex=enter("setMenuFocus",index);
        menuFocus=index;
        leave(rowIndex);
    }
    RuntimeMenuAction menuPressed() override {
        const auto index=enter("menuPressed");
        auto result=v_menuPressed;
        return leave(index,result);
    }
    void setMenuPressed(RuntimeMenuAction action) override {
        const auto index=enter("setMenuPressed",action);
        v_menuPressed=action;
        leave(index);
    }
    RuntimeMenuAction menuSliderDrag() override {
        const auto index=enter("menuSliderDrag");
        auto result=v_menuSliderDrag;
        return leave(index,result);
    }
    void setMenuSliderDrag(RuntimeMenuAction action) override {
        const auto index=enter("setMenuSliderDrag",action);
        v_menuSliderDrag=action;
        leave(index);
    }
    RuntimeMenuAction menuHover() override {
        const auto index=enter("menuHover");
        auto result=v_menuHover;
        return leave(index,result);
    }
    void setMenuHover(RuntimeMenuAction action) override {
        const auto index=enter("setMenuHover",action);
        v_menuHover=action;
        leave(index);
    }
    std::optional<RuntimeMapLeftMenuControl> menuControl(RuntimeInputPixelRect client, RuntimeMenuAction action) override {
        const auto index=enter("menuControl",client,action);
        auto result=v_menuControl;
        return leave(index,result);
    }
    bool setMenuSliderFromX(RuntimeInputPixelRect client, RuntimeMenuAction action, std::int32_t x) override {
        const auto index=enter("setMenuSliderFromX",client,action,x);
        auto result=v_setMenuSliderFromX;
        if(result)sliderValue=x;
        return leave(index,result);
    }
    void applyMenuSliderRuntimeEffects(RuntimeMenuAction action) override {
        const auto index=enter("applyMenuSliderRuntimeEffects",action);
        commands.push_back("slider-effects:"+show(action));
        leave(index);
    }
    void clearMenuStatus() override {
        const auto index=enter("clearMenuStatus");
        menuStatus.clear();
        leave(index);
    }
    void persistMenuPreferences() override {
        const auto index=enter("persistMenuPreferences");
        ++persistedRevision;
        leave(index);
    }
    void activateMenuAction(RuntimeMenuAction action) override {
        const auto index=enter("activateMenuAction",action);
        v_menuOpen=false;if(synchronousDestroyOnActivate)windowAlive=false;
        leave(index);
    }
    RuntimeInputPixelRect radarRect(RuntimeInputPixelRect sidebar) override {
        const auto index=enter("radarRect",sidebar);
        auto result=v_radarRect;
        return leave(index,result);
    }
    std::optional<RuntimeMultiplayerGameplayCell> radarCellAtPoint(RuntimeInputPixelRect radar, RuntimeInputPixelPoint point) override {
        const auto index=enter("radarCellAtPoint",radar,point);
        auto result=v_radarCellAtPoint;
        return leave(index,result);
    }
    void centerCamera(RuntimeInputPixelRect client, RuntimeMultiplayerGameplayCell cell) override {
        const auto index=enter("centerCamera",client,cell);
        commands.push_back("camera:"+show(client)+show(cell));
        leave(index);
    }
    std::optional<RuntimeSidebarTool> sidebarToolAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto index=enter("sidebarToolAtPoint",sidebar,point);
        auto result=v_sidebarToolAtPoint;
        return leave(index,result);
    }
    RuntimeSidebarTool activeTool() override {
        const auto index=enter("activeTool");
        auto result=v_activeTool;
        return leave(index,result);
    }
    void setActiveTool(RuntimeSidebarTool tool) override {
        const auto index=enter("setActiveTool",tool);
        v_activeTool=tool;
        leave(index);
    }
    std::string sidebarToolLabel(RuntimeSidebarTool tool) override {
        const auto index=enter("sidebarToolLabel",tool);
        auto result=v_sidebarToolLabel;
        return leave(index,result);
    }
    std::optional<RuntimeBuildKind> buildTabAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto index=enter("buildTabAtPoint",sidebar,point);
        auto result=v_buildTabAtPoint;
        return leave(index,result);
    }
    void setBuildTab(RuntimeBuildKind kind) override {
        const auto index=enter("setBuildTab",kind);
        currentBuildTab=kind;
        leave(index);
    }
    void clampBuildPage(RuntimeInputPixelRect sidebar) override {
        const auto index=enter("clampBuildPage",sidebar);
        v_activeBuildPage=std::clamp(v_activeBuildPage,0,v_buildPageCount-1);
        leave(index);
    }
    std::string buildTabLabel(RuntimeBuildKind kind) override {
        const auto index=enter("buildTabLabel",kind);
        auto result=v_buildTabLabel;
        return leave(index,result);
    }
    std::int32_t buildPageButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto index=enter("buildPageButtonAtPoint",sidebar,point);
        auto result=v_buildPageButtonAtPoint;
        return leave(index,result);
    }
    std::int32_t buildPageCount(RuntimeInputPixelRect sidebar) override {
        const auto index=enter("buildPageCount",sidebar);
        auto result=v_buildPageCount;
        return leave(index,result);
    }
    std::int32_t activeBuildPage() override {
        const auto index=enter("activeBuildPage");
        auto result=v_activeBuildPage;
        return leave(index,result);
    }
    void setActiveBuildPage(std::int32_t page) override {
        const auto index=enter("setActiveBuildPage",page);
        v_activeBuildPage=page;
        leave(index);
    }
    std::optional<std::int32_t> buildButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto index=enter("buildButtonAtPoint",sidebar,point);
        auto result=v_buildButtonAtPoint;
        return leave(index,result);
    }
    bool prepareReadyProduction(std::int32_t catalogIndex) override {
        const auto index=enter("prepareReadyProduction",catalogIndex);
        auto result=v_prepareReadyProduction;
        preparedItem=result?catalogIndex:-1;
        return leave(index,result);
    }
    bool preparedReadyCatalogAvailable() override {
        const auto index=enter("preparedReadyCatalogAvailable");
        auto result=v_preparedReadyCatalogAvailable;
        return leave(index,result);
    }
    void beginPreparedReadyPlacement() override {
        const auto index=enter("beginPreparedReadyPlacement");
        v_selectedPlacementPresent=true;commands.push_back("ready-place:"+show(preparedItem));
        leave(index);
    }
    std::string catalogTypeId(std::int32_t catalogIndex) override {
        const auto index=enter("catalogTypeId",catalogIndex);
        auto result=v_catalogTypeId;
        return leave(index,result);
    }
    bool modifierDown(RuntimeMapLeftModifier modifier) override {
        const auto index=enter("modifierDown",modifier);
        auto result=modifiers[static_cast<std::size_t>(modifier)];
        return leave(index,result);
    }
    void queueProduction(std::int32_t catalogIndex, bool afterActive) override {
        const auto index=enter("queueProduction",catalogIndex,afterActive);
        commands.push_back("queue:"+show(catalogIndex)+show(afterActive));
        leave(index);
    }
    void ensureSuperWeaponsInitialized() override {
        const auto index=enter("ensureSuperWeaponsInitialized");
        ++swInitCount;
        leave(index);
    }
    std::optional<RuntimeMapLeftSuperWeaponChoice> superWeaponAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto index=enter("superWeaponAtPoint",sidebar,point);
        auto result=v_superWeaponAtPoint;
        return leave(index,result);
    }
    RuntimeMapLeftSuperWeaponState chosenSuperWeaponState() override {
        const auto index=enter("chosenSuperWeaponState");
        auto result=v_chosenSuperWeaponState;
        return leave(index,result);
    }
    void prepareSuperWeaponContext() override {
        const auto index=enter("prepareSuperWeaponContext");
        contextEpoch=rootEpoch;
        leave(index);
    }
    bool usesIntentRouter() override {
        const auto index=enter("usesIntentRouter");
        auto result=v_usesIntentRouter;
        return leave(index,result);
    }
    void refreshAuthority() override {
        const auto index=enter("refreshAuthority");
        commands.push_back("authority:"+show(rootEpoch));
        leave(index);
    }
    std::optional<std::string> superWeaponTargetMode(const std::string& id) override {
        const auto index=enter("superWeaponTargetMode",id);
        auto result=v_superWeaponTargetMode;
        return leave(index,result);
    }
    bool routerBeginSuperWeapon(const std::string& id, std::size_t count, std::string& error) override {
        const auto index=enter("routerBeginSuperWeapon",id,count,error);
        auto result=v_routerBeginSuperWeapon;
        if(result){v_routerSuperWeaponPresent=true;v_routerSuperWeaponId=id;v_routerSuperWeaponRemaining=RuntimeMapLeftSuperWeaponRemaining{id,count,0,{}};}else error="rejected";
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    bool routerConfirmSuperWeapon(std::string& error) override {
        const auto index=enter("routerConfirmSuperWeapon",error);
        auto result=v_routerConfirmSuperWeapon;
        if(result){v_routerSuperWeaponPresent=false;v_routerSuperWeaponRemaining.reset();}else error="rejected";
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    bool offlineBeginSuperWeapon(const std::string& owner, const std::string& id, std::string& error) override {
        const auto index=enter("offlineBeginSuperWeapon",owner,id,error);
        auto result=v_offlineBeginSuperWeapon;
        if(!result)error="rejected";command("offline-begin:"+show(owner)+show(id)+show(contextEpoch),result);contextEpoch=0;
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    bool multiplayerBridgePresent() override {
        const auto index=enter("multiplayerBridgePresent");
        auto result=v_multiplayerBridgePresent;
        return leave(index,result);
    }
    bool routerSuperWeaponPresent() override {
        const auto index=enter("routerSuperWeaponPresent");
        auto result=v_routerSuperWeaponPresent;
        return leave(index,result);
    }
    std::string routerSuperWeaponId() override {
        const auto index=enter("routerSuperWeaponId");
        auto result=v_routerSuperWeaponId;
        return leave(index,result);
    }
    std::optional<RuntimeMapLeftSuperWeaponRemaining> routerSuperWeaponRemaining() override {
        const auto index=enter("routerSuperWeaponRemaining");
        auto result=v_routerSuperWeaponRemaining;
        return leave(index,result);
    }
    bool superWeaponTargetsGroundArea(const std::string& id) override {
        const auto index=enter("superWeaponTargetsGroundArea",id);
        auto result=v_superWeaponTargetsGroundArea;
        return leave(index,result);
    }
    bool routerTargetSuperWeapon(RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) override {
        const auto index=enter("routerTargetSuperWeapon",target,error);
        auto result=v_routerTargetSuperWeapon;
        if(result){if(v_routerSuperWeaponRemaining){++v_routerSuperWeaponRemaining->targetCount;if(v_routerSuperWeaponRemaining->targetCount==1)v_routerSuperWeaponRemaining->firstTargetCell=target.cell;if(v_routerSuperWeaponRemaining->targetCount>=v_routerSuperWeaponRemaining->expectedTargetCount){v_routerSuperWeaponPresent=false;v_routerSuperWeaponRemaining.reset();}}}else error="rejected";command("router-target:"+show(target),result);
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    void beginChronosphereMarker(const std::string& id, RuntimeMultiplayerGameplayCell cell) override {
        const auto index=enter("beginChronosphereMarker",id,cell);
        commands.push_back("marker:"+show(id)+show(cell));
        leave(index);
    }
    void clearChronosphereMarker() override {
        const auto index=enter("clearChronosphereMarker");
        commands.push_back("clear-marker");
        leave(index);
    }
    std::string economyOwner() override {
        const auto index=enter("economyOwner");
        auto result=v_economyOwner;
        return leave(index,result);
    }
    std::vector<RuntimeMapLeftSuperWeaponSnapshot> superWeaponSnapshots() override {
        const auto index=enter("superWeaponSnapshots");
        auto result=v_superWeaponSnapshots;
        return leave(index,result);
    }
    void prepareParasiteHostPoses() override {
        const auto index=enter("prepareParasiteHostPoses");
        parasiteEpoch=rootEpoch;
        leave(index);
    }
    bool offlineSubmitSuperWeapon(const std::string& owner, const std::string& id, RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) override {
        const auto index=enter("offlineSubmitSuperWeapon",owner,id,target,error);
        auto result=v_offlineSubmitSuperWeapon;
        if(!result)error="rejected";command("offline-target:"+show(owner)+show(id)+show(target)+show(contextEpoch),result);contextEpoch=0;
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    void notifyParasiteHostsRelocated() override {
        const auto index=enter("notifyParasiteHostsRelocated");
        commands.push_back("parasite:"+show(parasiteEpoch));
        leave(index);
    }
    void drainObjectExitQueue() override {
        const auto index=enter("drainObjectExitQueue");
        commands.push_back("exit-drain");
        leave(index);
    }
    void flushSuperWeaponHostEvents() override {
        const auto index=enter("flushSuperWeaponHostEvents");
        commands.push_back("flush-SW");
        leave(index);
    }
    RuntimeMapLeftSuperWeaponState superWeaponState(const std::string& owner, const std::string& id) override {
        const auto index=enter("superWeaponState",owner,id);
        auto result=v_superWeaponState;
        return leave(index,result);
    }
    RuntimeMultiplayerGameplayCell surfaceCell(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto index=enter("surfaceCell",viewport,point);
        auto result=v_surfaceCell;
        return leave(index,result);
    }
    void prepareProjection(RuntimeInputPixelRect viewport) override {
        const auto index=enter("prepareProjection",viewport);
        preparedProjectionEpoch=projectionEpoch;
        leave(index);
    }
    void pickProjection(RuntimeInputPixelPoint point) override {
        const auto index=enter("pickProjection",point);
        leave(index);
    }
    bool pickedObjectPresent() override {
        const auto index=enter("pickedObjectPresent");
        auto result=v_pickedObjectPresent;
        return leave(index,result);
    }
    bool pickedObjectIsStructure() override {
        const auto index=enter("pickedObjectIsStructure");
        auto result=v_pickedObjectIsStructure;
        return leave(index,result);
    }
    std::int32_t pickedObjectId() override {
        const auto index=enter("pickedObjectId");
        auto result=v_pickedObjectId;
        return leave(index,result);
    }
    std::vector<RuntimeMapLeftBoxItem> boxProjectionItems() override {
        const auto index=enter("boxProjectionItems");
        auto result=v_boxProjectionItems;
        return leave(index,result);
    }
    bool objectCanBeBoxSelected(std::int32_t id) override {
        const auto index=enter("objectCanBeBoxSelected",id);
        auto result=v_objectCanBeBoxSelected&&!ineligibleBoxIds.contains(id);
        return leave(index,result);
    }
    bool prepareTerrainWall(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto index=enter("prepareTerrainWall",viewport,point);
        auto result=v_prepareTerrainWall;
        return leave(index,result);
    }
    RuntimeMultiplayerGameplayCell preparedTerrainWallCell() override {
        const auto index=enter("preparedTerrainWallCell");
        auto result=v_preparedTerrainWallCell;
        return leave(index,result);
    }
    bool dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind order, RuntimeMultiplayerGameplayCell cell, RuntimeMapLeftTargetSource target) override {
        const auto index=enter("dispatchTargetOrder",order,cell,target);
        auto result=v_dispatchTargetOrder;
        command("target:"+show(order)+show(cell)+show(target)+show(v_pickedObjectId),result);
        return leave(index,result);
    }
    bool selectionEmpty() override {
        const auto index=enter("selectionEmpty");
        auto result=selected.empty();
        return leave(index,result);
    }
    std::size_t selectionSize() override {
        const auto index=enter("selectionSize");
        auto result=selected.size();
        return leave(index,result);
    }
    std::int32_t selectionFront() override {
        const auto index=enter("selectionFront");
        auto result=selected.front();
        return leave(index,result);
    }
    bool selectionContains(std::int32_t id) override {
        const auto index=enter("selectionContains",id);
        auto result=std::find(selected.begin(),selected.end(),id)!=selected.end();
        return leave(index,result);
    }
    std::int32_t selectedMovableCount() override {
        const auto index=enter("selectedMovableCount");
        auto result=v_selectedMovableCount;
        return leave(index,result);
    }
    void applySelection(std::vector<std::int32_t> ids, bool append, bool toggle) override {
        const auto index=enter("applySelection",ids,append,toggle);
        if(!append&&!toggle)selected.clear();for(auto id:ids){auto at=std::find(selected.begin(),selected.end(),id);if(toggle&&at!=selected.end())selected.erase(at);else if(at==selected.end())selected.push_back(id);}
        leave(index);
    }
    void clearSelectedBeacon() override {
        const auto index=enter("clearSelectedBeacon");
        selectedBeacon.reset();
        leave(index);
    }
    void selectBeacon(RuntimeMultiplayerGameplayCell cell) override {
        const auto index=enter("selectBeacon",cell);
        selectedBeacon=cell;
        leave(index);
    }
    bool beaconTargeting() override {
        const auto index=enter("beaconTargeting");
        auto result=v_beaconTargeting;
        return leave(index,result);
    }
    void setBeaconTargeting(bool value) override {
        const auto index=enter("setBeaconTargeting",value);
        v_beaconTargeting=value;
        leave(index);
    }
    bool dispatchBeacon(RuntimeMultiplayerGameplayCell cell) override {
        const auto index=enter("dispatchBeacon",cell);
        auto result=v_dispatchBeacon;
        command("beacon:"+show(cell),result);
        return leave(index,result);
    }
    std::string communicationViewerOwner() override {
        const auto index=enter("communicationViewerOwner");
        auto result=v_communicationViewerOwner;
        return leave(index,result);
    }
    std::vector<RuntimePlayerBeacon> beacons() override {
        const auto index=enter("beacons");
        auto result=v_beacons;
        return leave(index,result);
    }
    bool beaconVisible(const RuntimePlayerBeacon& beacon) override {
        const auto index=enter("beaconVisible",beacon);
        auto result=v_beaconVisible;
        return leave(index,result);
    }
    bool followTargeting() override {
        const auto index=enter("followTargeting");
        auto result=v_followTargeting;
        return leave(index,result);
    }
    void setFollowTargeting(bool value) override {
        const auto index=enter("setFollowTargeting",value);
        v_followTargeting=value;
        leave(index);
    }
    bool rallyTargeting() override {
        const auto index=enter("rallyTargeting");
        auto result=v_rallyTargeting;
        return leave(index,result);
    }
    void setRallyTargeting(bool value) override {
        const auto index=enter("setRallyTargeting",value);
        v_rallyTargeting=value;
        leave(index);
    }
    bool planningWaypoints() override {
        const auto index=enter("planningWaypoints");
        auto result=v_planningWaypoints;
        return leave(index,result);
    }
    std::string canonicalHumanOwner() override {
        const auto index=enter("canonicalHumanOwner");
        auto result=v_canonicalHumanOwner;
        return leave(index,result);
    }
    std::vector<RuntimeMapLeftWaypointRoute> waypointRoutes() override {
        const auto index=enter("waypointRoutes");
        auto result=v_waypointRoutes;
        return leave(index,result);
    }
    std::optional<RuntimeInputPixelPoint> waypointScreenPoint(RuntimeInputPixelRect viewport, const RuntimePlayerWaypoint& waypoint) override {
        const auto index=enter("waypointScreenPoint",viewport,waypoint);
        auto result=waypointPositions.contains(waypoint.id)?waypointPositions.at(waypoint.id):std::optional<Point>{};
        return leave(index,result);
    }
    void setSelectedWaypoint(std::uint64_t id) override {
        const auto index=enter("setSelectedWaypoint",id);
        selectedWaypoint=id;
        leave(index);
    }
    bool closeWaypointLoop(std::uint64_t id) override {
        const auto index=enter("closeWaypointLoop",id);
        auto result=v_closeWaypointLoop;
        command("close-loop:"+show(id),result);
        return leave(index,result);
    }
    bool mouseFactoryCanRally() override {
        const auto index=enter("mouseFactoryCanRally");
        auto result=v_mouseFactoryCanRally && selected.size()==1;
        return leave(index,result);
    }
    bool mouseFactoryCanBePrimary(std::int32_t id) override {
        const auto index=enter("mouseFactoryCanBePrimary",id);
        auto result=v_mouseFactoryCanBePrimary;
        return leave(index,result);
    }
    bool dispatchFactory(std::int32_t id, std::optional<RuntimeMultiplayerGameplayCell> cell) override {
        const auto index=enter("dispatchFactory",id,cell);
        auto result=v_dispatchFactory;
        command("factory:"+show(id)+show(cell),result);
        return leave(index,result);
    }
    bool dispatchSecondary(std::int32_t id) override {
        const auto index=enter("dispatchSecondary",id);
        auto result=v_dispatchSecondary;
        command("secondary:"+show(id),result);
        return leave(index,result);
    }
    bool dispatchStructureTool(RuntimeSidebarTool tool, std::int32_t id) override {
        const auto index=enter("dispatchStructureTool",tool,id);
        auto result=v_dispatchStructureTool;
        command("tool:"+show(tool)+show(id),result);
        return leave(index,result);
    }
    bool selectedPlacementPresent() override {
        const auto index=enter("selectedPlacementPresent");
        auto result=v_selectedPlacementPresent;
        return leave(index,result);
    }
    void updatePlacementPreview(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto index=enter("updatePlacementPreview",viewport,point);
        commands.push_back("preview:"+show(viewport)+show(point));
        leave(index);
    }
    bool placementValid() override {
        const auto index=enter("placementValid");
        auto result=v_placementValid;
        return leave(index,result);
    }
    bool confirmPlacement(std::string& error) override {
        const auto index=enter("confirmPlacement",error);
        auto result=v_confirmPlacement;
        if(!result)error="rejected";command("confirm-placement",result);
        trace[index].parameters += "out:"+show(error);
        return leave(index,result);
    }
    void notePlacementResult(bool accepted, const std::string& error, RuntimeMapLeftSound rejectedSound) override {
        const auto index=enter("notePlacementResult",accepted,error,rejectedSound);
        commands.push_back("placement-note:"+show(accepted)+show(error)+show(rejectedSound));v_interactionStatus=accepted?"placement accepted":"placement rejected";
        leave(index);
    }
    RuntimeMultiplayerGameplayCell placementCell() override {
        const auto index=enter("placementCell");
        auto result=v_placementCell;
        return leave(index,result);
    }
    void placeReadyBuilding(RuntimeMultiplayerGameplayCell cell) override {
        const auto index=enter("placeReadyBuilding",cell);
        command("place:"+show(cell),true);
        leave(index);
    }
    bool handleExistingRightMove(const RuntimeMapLeftPointerInput& input) override {
        const auto index=enter("handleExistingRightMove",input);
        auto result=v_handleExistingRightMove;
        return leave(index,result);
    }
};

// BEGIN INDEPENDENT FROZEN OLD: slider-begin
bool oldSliderBegin(RuntimeInputPixelRect clientRect, RuntimeMenuAction action, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host) {
    const std::optional<RuntimeMapLeftMenuControl> control =
        host.menuControl(clientRect, action);
    if (!control.has_value()
        || !control->slider) {
        return false;
    }

    const RuntimeInputPixelRect valueRect = control->valueRect;
    if (!oldHalfOpen(valueRect, point)) {
        return false;
    }

    host.setMenuPressed(action);
    host.setMenuSliderDrag(action);
    (void)host.setMenuSliderFromX(clientRect, action, point.x);
    return true;
}
// END INDEPENDENT FROZEN OLD: slider-begin

// BEGIN INDEPENDENT FROZEN OLD: slider-update
bool oldSliderUpdate(RuntimeInputPixelRect clientRect, int clientX, RuntimeMapLeftPointerHost& host) {
    if (host.menuSliderDrag() == RuntimeMenuAction::None) {
        return false;
    }
    return host.setMenuSliderFromX(clientRect, host.menuSliderDrag(), clientX);
}
// END INDEPENDENT FROZEN OLD: slider-update

// BEGIN INDEPENDENT FROZEN OLD: slider-finish
bool oldSliderFinish(RuntimeInputPixelRect clientRect, int clientX, RuntimeMapLeftPointerHost& host) {
    if (host.menuSliderDrag() == RuntimeMenuAction::None) {
        return false;
    }
    (void)oldSliderUpdate(clientRect, clientX, host);
    const RuntimeMenuAction completedAction = host.menuSliderDrag();
    host.setMenuSliderDrag(RuntimeMenuAction::None);
    host.setMenuPressed(RuntimeMenuAction::None);
    host.applyMenuSliderRuntimeEffects(completedAction);
    host.clearMenuStatus();
    (void)host.persistMenuPreferences();
    return true;
}
// END INDEPENDENT FROZEN OLD: slider-finish

// BEGIN INDEPENDENT FROZEN OLD: waypoint-click
bool oldWaypointClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host) {
    if (!host.planningWaypoints() || host.followTargeting() || host.rallyTargeting()
        || host.beaconTargeting() || host.activeTool() != RuntimeSidebarTool::None) return false;
    const auto owner = host.canonicalHumanOwner();
    std::uint64_t nearestId = 0;
    long long nearestDistance = 145;
    bool canClose = false;
    for (const auto& route : host.waypointRoutes()) { const auto id = route.actorId;
        if (route.owner != owner) continue;
        for (std::size_t i = 0; i < route.points.size(); ++i) {
            const auto position = host.waypointScreenPoint(viewport, route.points[i]);
            if (!position) continue;
            const long long dx = point.x - position->x, dy = point.y - position->y;
            const long long distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestId = route.points[i].id; nearestDistance = distance;
                canClose = !route.loopStart && i + 1 < route.points.size() && host.selectionContains(id);
            }
        }
    }
    host.setSelectedWaypoint(nearestId);
    if (!nearestId) return false;
    if (canClose) host.closeWaypointLoop(nearestId);
    host.gestureState().active = host.gestureState().moved = false;
    host.requestRepaint();
    return true;
}
// END INDEPENDENT FROZEN OLD: waypoint-click

// BEGIN INDEPENDENT FROZEN OLD: target-click
bool oldTargetClick(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host) {
    if (!runtimeInputPixelPointInRectInclusive(viewport, point)) return false;
    const RuntimeMultiplayerGameplayCell targetCell = host.surfaceCell(viewport, point);
    if (host.beaconTargeting()) {
        
        if (host.dispatchBeacon(targetCell)) {
            host.setBeaconTargeting(false);
            host.applySelection({}, false, false);
            host.clearSelectedBeacon();
        }
        host.gestureState().active = host.gestureState().moved = false;
        host.requestRepaint();
        return true;
    }
    if (oldWaypointClick(viewport, point, host)) return true;
    if (!host.followTargeting() && !host.rallyTargeting() && host.activeTool()==RuntimeSidebarTool::None
        && !((host.modifierDown(RuntimeMapLeftModifier::Control) ? 0x8000 : 0)&0x8000) && !((host.modifierDown(RuntimeMapLeftModifier::Alt) ? 0x8000 : 0)&0x8000) && !((host.modifierDown(RuntimeMapLeftModifier::Shift) ? 0x8000 : 0)&0x8000)) {
        const auto owner=host.communicationViewerOwner();
        for (const auto& beacon:host.beacons()) {
            if (beacon.owner!=owner || !host.beaconVisible(beacon)) continue;
            // The input path resolves ground cell centers. Native selection is
            // within half a cell of the beacon's stored ground point.
            const double dx=static_cast<double>(targetCell.x)-beacon.cell.x;
            const double dy=static_cast<double>(targetCell.y)-beacon.cell.y;
            if (dx*dx+dy*dy<0.25) {
                host.applySelection({}, false, false);
                host.selectBeacon(beacon.cell);
                host.gestureState().active=host.gestureState().moved=false;
                host.requestRepaint();
                return true;
            }
        }
        host.clearSelectedBeacon();
    }
    if (!host.followTargeting() && !host.rallyTargeting()) return false;
    if (host.rallyTargeting()) {
        if (host.selectionSize() == 1)
            host.dispatchFactory(host.selectionFront(), targetCell);
        host.setRallyTargeting(false);
    } else {
        host.prepareProjection(viewport);
        host.pickProjection(point);
        if (host.pickedObjectPresent()) host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Follow, targetCell, RuntimeMapLeftTargetSource::PickedObject);
        else host.setStatus("follow requires an allied unit");
        host.setFollowTargeting(false);
    }
    host.gestureState().active = false;
    host.gestureState().moved = false;
    host.requestRepaint();
    return true;
}
// END INDEPENDENT FROZEN OLD: target-click

// BEGIN INDEPENDENT FROZEN OLD: superweapon-submit
bool oldSubmitSuperWeapon(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point, RuntimeMapLeftPointerHost& host) {
    host.ensureSuperWeaponsInitialized();
    if (host.usesIntentRouter()) {
        
        if (!host.routerSuperWeaponPresent()) {
            return false;
        }
        const RuntimeMultiplayerGameplayCell targetPoint = host.surfaceCell(viewport, point);
        RuntimeMultiplayerGameplaySuperWeaponTarget target;
        target.cell = {targetPoint.x, targetPoint.y};
        host.prepareProjection(viewport);
        host.pickProjection(point);
        if (host.pickedObjectPresent()
            && !host.superWeaponTargetsGroundArea(host.routerSuperWeaponId())) {
            target.objectId = host.pickedObjectId();
        }
        host.refreshAuthority();
        std::string errorMessage;
        const bool accepted =
            host.routerTargetSuperWeapon(std::move(target), errorMessage);
        if (accepted) {
            const auto remaining = host.routerSuperWeaponRemaining();
            if (remaining && remaining->expectedTargetCount == 2
                && remaining->targetCount == 1) {
                host.beginChronosphereMarker(remaining->id, remaining->firstTargetCell);
            } else if (!remaining) {
                host.clearChronosphereMarker();
            }
            host.setStatus(host.routerSuperWeaponPresent()
                ? "superweapon choose destination"
                : (host.multiplayerBridgePresent() ? "superweapon launch queued for multiplayer" : "superweapon launched"));
        } else {
            host.setStatus("superweapon target rejected: " + errorMessage);
            host.emitSound(RuntimeMapLeftSound::Blocked);
        }
        host.gestureState().active = false;
        host.gestureState().moved = false;
        return true;
    }
    const std::string owner = oldLower(
        oldTrim(host.economyOwner())
    );
    std::optional<RuntimeMapLeftSuperWeaponSnapshot> activeSuperWeapon;
    for (const auto& snapshot : host.superWeaponSnapshots()) {
        if (snapshot.targetSelectionActive
            && (owner.empty()
                || oldLower(oldTrim(snapshot.owner)) == owner)) {
            activeSuperWeapon = snapshot;
            break;
        }
    }
    if (!activeSuperWeapon) {
        return false;
    }
    const std::string activeSuperWeaponOwner = activeSuperWeapon->owner;
    const std::string activeSuperWeaponId = activeSuperWeapon->id;

    const RuntimeMultiplayerGameplayCell targetPoint = host.surfaceCell(viewport, point);
    int objectId = -1;
    host.prepareProjection(viewport);
    host.pickProjection(point);
    if (host.pickedObjectPresent()
        && !host.superWeaponTargetsGroundArea(activeSuperWeaponId)) {
        objectId = host.pickedObjectId();
    }

    host.prepareSuperWeaponContext();
    host.prepareParasiteHostPoses();
    std::string reason;
    const bool accepted = host.offlineSubmitSuperWeapon(activeSuperWeaponOwner, activeSuperWeaponId, {{targetPoint.x, targetPoint.y}, objectId}, reason);
    (void)host.notifyParasiteHostsRelocated();
    (void)host.drainObjectExitQueue();
    if (accepted) {
        (void)host.flushSuperWeaponHostEvents();
        const RuntimeMapLeftSuperWeaponState nextState = host.superWeaponState(activeSuperWeaponOwner, activeSuperWeaponId);
        host.setStatus(nextState == RuntimeMapLeftSuperWeaponState::AwaitingSecondTarget
                ? "superweapon choose destination"
                : "superweapon launch queued");
    } else {
        host.setStatus("superweapon target rejected: " + reason);
        host.setEconomyStatus(host.interactionStatus());
        if (!host.suppressPlayerAudio()) {
            host.emitSound(RuntimeMapLeftSound::Blocked);
        }
    }
    host.gestureState().active = false;
    host.gestureState().moved = false;
    return true;
}
// END INDEPENDENT FROZEN OLD: superweapon-submit

// BEGIN INDEPENDENT FROZEN OLD: left-down
bool oldDown(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {
        if (!host.worldLoaded()) return false;
        const auto clientRect = host.clientRect();
        const auto point = input.point;
        host.setMouseValid(true);
        host.setMousePoint(point);
        if (!runtimeInputPixelPointInRectInclusive(clientRect, point)) {
            return false;
        }
        if (host.menuOpen()) {
            const RuntimeMenuAction action = host.menuActionAtPoint(clientRect, point);
            host.setMenuPressed(action);
            const auto buttons = host.menuFocusableActions(clientRect);
            for (std::size_t index = 0; index < buttons.size(); ++index) {
                if (buttons[index] == action) {
                    host.setMenuFocus(static_cast<int>(index));
                    break;
                }
            }
            if (oldSliderBegin(clientRect, action, point, host)) {
                host.captureWindow();
                host.requestRepaint();
                host.redraw();
                return true;
            }
            if (action != RuntimeMenuAction::None) {
                host.captureWindow();
            }
            host.requestRepaint();
            host.redraw();
            return true;
        }
        const RuntimeInputPixelRect sidebar = host.sidebarRect(clientRect);
        if (runtimeInputPixelPointInRectInclusive(sidebar, point)) {
            const RuntimeInputPixelRect radarRect = host.radarRect(sidebar);
            const std::optional<RuntimeMultiplayerGameplayCell> radarCell =
                host.radarCellAtPoint(radarRect, point);
            if (radarCell.has_value()) {
                host.centerCamera(clientRect, *radarCell);
                host.invalidateProjection();
                host.setStatus("radar jump "
                    + std::to_string(radarCell->x)
                    + ","
                    + std::to_string(radarCell->y));
                host.gestureState().active = false;
                host.gestureState().moved = false;
                host.requestRepaint();
                host.redraw();
                return true;
            }

            host.ensureSuperWeaponsInitialized();
            if (const auto superWeapon = host.superWeaponAtPoint(sidebar, point)) {
                const std::string superWeaponOwner = superWeapon->owner;
                const std::string superWeaponId = superWeapon->id;
                const std::string superWeaponName = superWeapon->name;
                host.prepareSuperWeaponContext();
                std::string reason;
                bool beganTargeting = false;
                if (host.usesIntentRouter()) {
                    host.refreshAuthority();
                    const auto rule = host.superWeaponTargetMode(superWeaponId);
                    const std::string targetMode = !rule
                        ? std::string("cell")
                        : oldLower(
                            oldTrim(*rule)
                        );
                    const std::size_t targetCount =
                        targetMode == "none"
                            || targetMode == "notarget"
                            || targetMode == "wholemap"
                            || targetMode == "map"
                        ? 0U
                        : (targetMode == "twostage"
                            || targetMode == "two-stage"
                            ? 2U
                            : 1U);
                    if (host.chosenSuperWeaponState()
                            != RuntimeMapLeftSuperWeaponState::Ready) {
                        reason = "not ready";
                    } else {
                        beganTargeting =
                            host.routerBeginSuperWeapon(superWeaponId, targetCount, reason);
                        if (beganTargeting && targetCount == 0U) {
                            beganTargeting = host.routerConfirmSuperWeapon(reason);
                        }
                    }
                } else {
                    beganTargeting = host.offlineBeginSuperWeapon(superWeaponOwner, superWeaponId, reason);
                }
                if (beganTargeting) {
                    host.clearChronosphereMarker();
                    host.setStatus(host.multiplayerBridgePresent()
                            && !host.routerSuperWeaponPresent()
                        ? "superweapon launch queued for multiplayer"
                        : "superweapon target: " + superWeaponName);
                    host.emitSound(RuntimeMapLeftSound::Button);
                } else {
                    host.setStatus("superweapon unavailable: " + reason);
                    const bool unfinished =
                        host.chosenSuperWeaponState()
                            == RuntimeMapLeftSuperWeaponState::Charging
                        || host.chosenSuperWeaponState()
                            == RuntimeMapLeftSuperWeaponState::Suspended;
                    if (!unfinished
                        && !host.suppressPlayerAudio()) {
                        host.emitSound(RuntimeMapLeftSound::Blocked);
                    }
                }
                host.requestRepaint();
                host.redraw();
                return true;
            }

            const std::optional<RuntimeSidebarTool> sidebarTool =
                host.sidebarToolAtPoint(sidebar, point);
            if (sidebarTool.has_value()) {
                host.setActiveTool(host.activeTool() == *sidebarTool
                        ? RuntimeSidebarTool::None
                        : *sidebarTool);
                host.setStatus(host.activeTool() == RuntimeSidebarTool::None
                        ? "sidebar tool canceled"
                        : host.sidebarToolLabel(host.activeTool()));
                host.emitSound(RuntimeMapLeftSound::Tab);
                host.requestRepaint();
                host.redraw();
                return true;
            }

            const std::optional<RuntimeBuildKind> tab =
                host.buildTabAtPoint(sidebar, point);
            if (tab.has_value()) {
                host.setBuildTab(*tab);
                host.clampBuildPage(sidebar);
                host.setStatus("build tab " + host.buildTabLabel(*tab));
                host.emitSound(RuntimeMapLeftSound::Tab);
                host.requestRepaint();
                host.redraw();
                return true;
            }

            const int pageDelta =
                host.buildPageButtonAtPoint(sidebar, point);
            if (pageDelta != 0) {
                const int pageCount = host.buildPageCount(sidebar);
                const int nextPage = std::clamp(
                    host.activeBuildPage() + pageDelta,
                    0,
                    pageCount - 1
                );
                host.setActiveBuildPage(nextPage);
                host.setStatus("build page " + std::to_string(nextPage + 1)
                    + "/" + std::to_string(pageCount));
                host.emitSound(RuntimeMapLeftSound::Button);
                host.requestRepaint();
                host.redraw();
                return true;
            }

            const std::optional<int> buttonIndex =
                host.buildButtonAtPoint(sidebar, point);
            if (buttonIndex.has_value()) {
                const auto item = *buttonIndex;
                if (host.prepareReadyProduction(item)) {
                    const bool catalog = host.preparedReadyCatalogAvailable();
                    if (catalog) {
                        host.beginPreparedReadyPlacement();
                    }
                    host.setStatus("placing " + host.catalogTypeId(item));
                    host.emitSound(RuntimeMapLeftSound::Button);
                } else {
                    const bool addNext =
                        ((host.modifierDown(RuntimeMapLeftModifier::Shift) ? 0x8000 : 0) & 0x8000) != 0;
                    host.queueProduction(item, addNext);
                }
                host.requestRepaint();
                host.redraw();
            }
            return true;
        }

        if (host.rejectObserverSelection()) return true;
        host.focusWindow();
        host.captureWindow();
        // Focus/capture can deliver nested messages. Restore this click's
        // modifiers and position before exposing the pressed cursor.
        host.restoreForceAttackAfterCapture(input.rawButtons);
        host.setMousePoint(point);
        host.requestRepaint();
        host.gestureState().active = true;
        host.gestureState().moved = false;
        host.gestureState().start = point;
        host.gestureState().current = point;
        host.setStatus("selecting");
        return true;
}
// END INDEPENDENT FROZEN OLD: left-down

// BEGIN INDEPENDENT FROZEN OLD: shared-move
bool oldMove(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {


        const auto point = input.point;
        const bool mousePointChanged =
            !host.mouseValid()
            || host.mousePoint().x != point.x
            || host.mousePoint().y != point.y;
        host.setMouseValid(true);
        host.setMousePoint(point);
        if (mousePointChanged) {
            host.requestRepaint();
        }
        if (host.menuOpen()) {
            const auto clientRect = host.clientRect();
            if (host.menuSliderDrag() != RuntimeMenuAction::None) {
                if (host.ownsCapture() && (input.rawButtons & 1U) != 0) {
                    if (oldSliderUpdate(clientRect, point.x, host)) {
                        host.requestRepaint();
                        host.redraw();
                    }
                }
                return true;
            }
            const RuntimeMenuAction hover = host.menuActionAtPoint(clientRect, point);
            if (hover != host.menuHover()) {
                host.setMenuHover(hover);
                host.requestRepaint();
                host.redraw();
            }
            return true;
        }
        if (host.handleExistingRightMove(input)) return true;

        if (host.selectedPlacementPresent()) {
            const auto clientRect = host.clientRect();
            const RuntimeInputPixelRect viewport = host.mapViewport(clientRect);
            if (runtimeInputPixelPointInRectInclusive(viewport, point)) {
                host.updatePlacementPreview(viewport, point);
                host.requestRepaint();
            }
        }

        if (!host.gestureState().active) {
            return false;
        }

        host.gestureState().current = point;
        const int deltaX = point.x - host.gestureState().start.x;
        const int deltaY = point.y - host.gestureState().start.y;
        if ((deltaX * deltaX) + (deltaY * deltaY) > 16) {
            host.gestureState().moved = true;
        }
        host.requestRepaint();
        return true;
}
// END INDEPENDENT FROZEN OLD: shared-move

// BEGIN INDEPENDENT FROZEN OLD: left-up
bool oldUp(RuntimeMapLeftPointerInput input, RuntimeMapLeftPointerHost& host) {


        const auto clientRect = host.clientRect();
        const auto point = input.point;
        host.setMouseValid(true);
        host.setMousePoint(point);
        if (host.menuOpen()) {
            if (oldSliderFinish(clientRect, point.x, host)) {
                if (host.ownsCapture()) {
                    host.releaseCapture();
                }
                host.emitSound(RuntimeMapLeftSound::Button);
                host.requestRepaint();
                host.redraw();
                return true;
            }
            const RuntimeMenuAction released = host.menuActionAtPoint(clientRect, point);
            const RuntimeMenuAction pressed = host.menuPressed();
            host.setMenuPressed(RuntimeMenuAction::None);
            if (host.ownsCapture()) {
                host.releaseCapture();
            }
            if (released == pressed && pressed != RuntimeMenuAction::None) {
                host.activateMenuAction(pressed);
            } else {
                host.requestRepaint();
                host.redraw();
            }
            return true;
        }
        if (host.ownsCapture()) {
            host.releaseCapture();
        }
        if (host.rejectObserverSelection()) return true;
        const RuntimeInputPixelRect viewport = host.mapViewport(clientRect);
        if (runtimeInputPixelPointInRectInclusive(viewport, point)
            && oldSubmitSuperWeapon(viewport, point, host)) {
            host.requestRepaint();
            host.redraw();
            return true;
        }
        if (!host.gestureState().active) {
            return false;
        }
        host.gestureState().current = point;

        if (host.activeTool() != RuntimeSidebarTool::None
            && runtimeInputPixelPointInRectInclusive(viewport, point)) {
            host.prepareProjection(viewport);
            host.pickProjection(point);
            bool toolHandled = false;
            if (host.pickedObjectPresent()
                && host.pickedObjectIsStructure()) {
                if (host.activeTool() == RuntimeSidebarTool::Sell) {
                    toolHandled = host.dispatchStructureTool(RuntimeSidebarTool::Sell, host.pickedObjectId());
                } else if (host.activeTool() == RuntimeSidebarTool::Repair) {
                    toolHandled = host.dispatchStructureTool(RuntimeSidebarTool::Repair, host.pickedObjectId());
                }
            } else {
                host.setEconomyStatus(host.sidebarToolLabel(host.activeTool())
                    + " needs a structure");
            }
            if (toolHandled) {
                if (host.activeTool() != RuntimeSidebarTool::Sell) {
                    host.setActiveTool(RuntimeSidebarTool::None);
                }
                host.setStatus(host.economyStatus());
            } else {
                host.emitSound(oldLower(host.economyStatus()).find("no credits") != std::string::npos ? RuntimeMapLeftSound::InsufficientFunds : RuntimeMapLeftSound::Scold);
            }
            host.gestureState().active = false;
            host.gestureState().moved = false;
            host.requestRepaint();
            host.redraw();
            return true;
        }

        if (host.selectedPlacementPresent()) {
            if (runtimeInputPixelPointInRectInclusive(viewport, point)) {
                host.updatePlacementPreview(viewport, point);
                if (host.usesIntentRouter()) {
                    const bool previewWasValid = host.placementValid();
                    host.refreshAuthority();
                    std::string errorMessage;
                    const bool accepted = host.confirmPlacement(errorMessage);
                    host.notePlacementResult(accepted, errorMessage, previewWasValid ? RuntimeMapLeftSound::Scold : RuntimeMapLeftSound::CannotDeployHere);
                } else {
                    host.placeReadyBuilding(host.placementCell());
                }
            }
            host.gestureState().active = false;
            host.gestureState().moved = false;
            host.requestRepaint();
            host.redraw();
            return true;
        }

        host.prepareProjection(viewport);
        std::vector<int> selectedIds;
        if (!host.gestureState().moved && oldTargetClick(viewport, point, host)) return true;
        const bool append = (input.rawButtons & 4U) != 0;
        const bool toggle = (input.rawButtons & 8U) != 0;
        const bool forceFireClick = toggle && !host.selectionEmpty()
            && runtimeInputPixelPointInRectInclusive(viewport, point);
        const bool altClick = ((host.modifierDown(RuntimeMapLeftModifier::Alt) ? 0x8000 : 0) & 0x8000) != 0
            && !host.selectionEmpty() && runtimeInputPixelPointInRectInclusive(viewport, point);
        const bool attackMoveClick = toggle && append && !altClick
            && !host.selectionEmpty() && runtimeInputPixelPointInRectInclusive(viewport, point);
        if ((altClick || attackMoveClick) && !host.gestureState().moved) {
            host.pickProjection(point);
            const RuntimeMultiplayerGameplayCell cell = host.surfaceCell(viewport, point);
            const auto order = runtimePlayerModifiedTargetOrder(toggle, append, altClick,
                host.pickedObjectPresent());
            host.dispatchTargetOrder(order, cell, order == RuntimeMultiplayerGameplayTargetOrderKind::Follow ? RuntimeMapLeftTargetSource::PickedObject : RuntimeMapLeftTargetSource::None);
        } else if (host.gestureState().moved) {
            const RuntimeInputPixelRect dragRect =
                oldNormalized(host.gestureState().start, host.gestureState().current);
            for (const auto& projectedObject : host.boxProjectionItems()) {
                if (projectedObject.objectId.has_value()
                    && host.objectCanBeBoxSelected(*projectedObject.objectId)
                    && runtimeInputPixelRectsIntersect(dragRect, projectedObject.bounds)) {
                    selectedIds.push_back(*projectedObject.objectId);
                }
            }
            host.applySelection(selectedIds, append, toggle);
            host.clearSelectedBeacon();
        } else {
            host.pickProjection(point);
            const bool hasMovableSelection =
                host.selectedMovableCount() > 0;
            const bool terrainWallTarget = (forceFireClick || (!append && !toggle)) ? host.prepareTerrainWall(viewport, point) : false;
            if (forceFireClick) {
                const auto target = terrainWallTarget ? RuntimeMapLeftTargetSource::TerrainWall : RuntimeMapLeftTargetSource::PickedObject; const bool targetPresent = terrainWallTarget || host.pickedObjectPresent();
                const RuntimeMultiplayerGameplayCell targetPoint =
                    host.surfaceCell(viewport, point);
                host.dispatchTargetOrder(targetPresent ? RuntimeMultiplayerGameplayTargetOrderKind::Attack : RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell, targetPoint, target);
                // Consume rejected force-fire too, preserving the selection.
            } else if (!append && !toggle && terrainWallTarget) {
                host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Primary, host.preparedTerrainWallCell(), RuntimeMapLeftTargetSource::TerrainWall);
            } else if (!append && !toggle
                && runtimeInputPixelPointInRectInclusive(viewport, point)
                && (!host.pickedObjectPresent())
                && host.mouseFactoryCanRally()) {
                const RuntimeMultiplayerGameplayCell cell = host.surfaceCell(viewport, point);
                host.dispatchFactory(host.selectionFront(), cell);
            } else if (!append && !toggle && host.pickedObjectPresent()
                && host.mouseFactoryCanBePrimary(host.pickedObjectId())) {
                host.dispatchFactory(host.pickedObjectId(), std::nullopt);
            } else if (!append
                && !toggle
                && host.pickedObjectPresent()
                && host.selectionContains(host.pickedObjectId())
                && host.dispatchSecondary(host.pickedObjectId())) {
                // handled secondary deploy/action
            } else if (!append
                && !toggle
                && hasMovableSelection
                && (!host.pickedObjectPresent()
                    || !host.selectionContains(host.pickedObjectId()))) {
                const RuntimeMultiplayerGameplayCell targetPoint =
                    host.surfaceCell(viewport, point);
                const RuntimeMultiplayerGameplayCell targetCell{targetPoint.x, targetPoint.y};
                host.dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Primary, targetCell, RuntimeMapLeftTargetSource::PickedObject);
            } else if (host.pickedObjectPresent()) {
                selectedIds.push_back(host.pickedObjectId());
                host.applySelection(selectedIds, append, toggle);
            host.clearSelectedBeacon();
            } else {
                host.applySelection(selectedIds, append, toggle);
            host.clearSelectedBeacon();
            }
        }

        host.gestureState().active = false;
        host.gestureState().moved = false;
        host.requestRepaint();
        host.redraw();
        return true;
}
// END INDEPENDENT FROZEN OLD: left-up


enum class Entry { Down, Move, Up, SliderBegin, SliderUpdate, SliderFinish, Target, Waypoint, SuperWeapon };
struct Step { Entry entry; Input input; bool useRootEvent = false; };
struct Scenario { std::string name; Model seed; std::vector<Step> steps; };
struct Run { std::vector<bool> returned; bool exceptional=false; };
bool invoke(Model& model, const Step& step) {
    const auto ownedEvent = step.useRootEvent ? model.eventSource : step.input;
    if(model.oldOracle) {
        switch(step.entry) {
        case Entry::Down: return oldDown(ownedEvent,model);
        case Entry::Move: return oldMove(ownedEvent,model);
        case Entry::Up: return oldUp(ownedEvent,model);
        case Entry::SliderBegin: return oldSliderBegin(model.v_clientRect,model.v_menuActionAtPoint,ownedEvent.point,model);
        case Entry::SliderUpdate: return oldSliderUpdate(model.v_clientRect,ownedEvent.point.x,model);
        case Entry::SliderFinish: return oldSliderFinish(model.v_clientRect,ownedEvent.point.x,model);
        case Entry::Target: return oldTargetClick(model.v_mapViewport,ownedEvent.point,model);
        case Entry::Waypoint: return oldWaypointClick(model.v_mapViewport,ownedEvent.point,model);
        case Entry::SuperWeapon: return oldSubmitSuperWeapon(model.v_mapViewport,ownedEvent.point,model);
        }
    } else {
        switch(step.entry) {
        case Entry::Down: return handleRuntimeMapLeftPointerDown(ownedEvent,model);
        case Entry::Move: return handleRuntimeMapLeftPointerMove(ownedEvent,model);
        case Entry::Up: return handleRuntimeMapLeftPointerUp(ownedEvent,model);
        case Entry::SliderBegin: return beginRuntimeMapLeftMenuSlider(model.v_clientRect,model.v_menuActionAtPoint,ownedEvent.point,model);
        case Entry::SliderUpdate: return updateRuntimeMapLeftMenuSlider(model.v_clientRect,ownedEvent.point.x,model);
        case Entry::SliderFinish: return finishRuntimeMapLeftMenuSlider(model.v_clientRect,ownedEvent.point.x,model);
        case Entry::Target: return handleRuntimeMapLeftTargetClick(model.v_mapViewport,ownedEvent.point,model);
        case Entry::Waypoint: return handleRuntimeMapLeftWaypointClick(model.v_mapViewport,ownedEvent.point,model);
        case Entry::SuperWeapon: return submitRuntimeMapLeftSuperWeapon(model.v_mapViewport,ownedEvent.point,model);
        }
    }
    throw std::runtime_error("unknown contract entry");
}
Run execute(Model& model,const std::vector<Step>& steps) {
    Run out;
    try { for(const auto& step:steps) out.returned.push_back(invoke(model,step)); }
    catch(const PrimitiveFailure&) { out.exceptional=true; }
    return out;
}
std::size_t checks=0,differences=0,orderedRows=0,exceptionCases=0,dynamicCases=0,nestedCases=0;
void require(bool condition,const std::string& label) {
    ++checks;
    if(!condition) throw std::runtime_error(label);
}
void compare(const Scenario& scenario, bool registerPrimitives,
    std::map<std::string,std::pair<Scenario,std::size_t>>& primitiveSeeds) {
    auto old=scenario.seed, now=scenario.seed;
    old.oldOracle=true;now.oldOracle=false;
    const auto a=execute(old,scenario.steps),b=execute(now,scenario.steps);
    require(a.returned==b.returned,scenario.name+": return sequence");
    require(a.exceptional==b.exceptional,scenario.name+": exception boundary");
    require(old.stateText()==now.stateText(),scenario.name+": full final state/commands");
    if(old.trace!=now.trace) {
        std::size_t first=0;
        while(first<old.trace.size()&&first<now.trace.size()&&old.trace[first]==now.trace[first])++first;
        const std::string left=first<old.trace.size()?old.trace[first].method:"<end>";
        const std::string right=first<now.trace.size()?now.trace[first].method:"<end>";
        require(false,scenario.name+": ordered query/effect/parameter/result/state trace at "
            +std::to_string(first)+" "+left+" / "+right);
    }
    require(true,scenario.name+": full ordered trace");
    ++differences;orderedRows+=old.trace.size();
    if(scenario.name.starts_with("fresh-"))dynamicCases+=old.callbackCount>0;
    if(scenario.name.starts_with("nested-"))nestedCases+=old.callbackCount>0;
    if(registerPrimitives) for(std::size_t i=0;i<old.trace.size();++i)
        primitiveSeeds.try_emplace(old.trace[i].method,scenario,i);
}
Model base() {
    Model m;
    m.gesture={true,false,{80,80},{100,100}};
    m.v_mousePoint={100,100};
    m.v_pickedObjectId=7;
    m.v_dispatchSecondary=false;
    m.v_waypointRoutes={};
    return m;
}
RuntimePlayerWaypoint waypoint(std::uint64_t id) {
    RuntimePlayerWaypoint p;
    p.id=id;
    p.order.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;
    p.order.objectIds={7};
    p.order.target=RuntimeMultiplayerGameplayCellTarget{{4,6}};
    p.order.append=false;
    return p;
}
std::vector<Scenario> scenarios() {
    std::vector<Scenario> out;
    auto add=[&](std::string name,Model model,Entry entry,Point point=Point{100,100},std::uint64_t bits=0) {
        out.push_back({std::move(name),std::move(model),{{entry,{point,bits}}}});
    };
    const std::vector<Point> points{{-100,-1},{0,0},{499,479},{500,480},{640,480},{641,481},{100,100},{550,50}};
    const std::vector<std::uint64_t> bits{0,1,4,8,12,0xfffffffffffffff0ULL,0x100000000ULL,0xffffffffffffffffULL};
    for(auto p:points) for(auto b:bits) for(bool loaded:{false,true}) {
        auto m=base();m.v_worldLoaded=loaded;
        add("down-domain-"+show(p)+show(b)+show(loaded),m,Entry::Down,p,b);
    }
    for(int menuMode=0;menuMode<5;++menuMode) for(auto p:points) for(auto b:bits) {
        auto m=base();m.v_menuOpen=true;
        m.v_menuActionAtPoint=menuMode==0?Action::None:Action::AdjustSoundVolume;
        m.v_menuFocusableActions={Action::None,Action::AdjustSoundVolume,Action::AdjustSoundVolume};
        if(menuMode>=2)m.v_menuControl=RuntimeMapLeftMenuControl{menuMode!=2,{80,80,120,120}};
        m.v_setMenuSliderFromX=menuMode!=4;
        add("menu-down-"+std::to_string(menuMode)+show(p)+show(b),m,Entry::Down,p,b);
        m.v_menuSliderDrag=menuMode>=2?Action::AdjustSoundVolume:Action::None;
        m.v_menuPressed=Action::AdjustSoundVolume;
        m.v_ownsCapture=(b&1U)!=0;
        m.v_menuHover=menuMode==1?Action::AdjustSoundVolume:Action::None;
        add("menu-move-"+std::to_string(menuMode)+show(p)+show(b),m,Entry::Move,p,b);
        add("menu-up-"+std::to_string(menuMode)+show(p)+show(b),m,Entry::Up,p,b);
    }
    for(int region=0;region<8;++region) for(bool accepted:{false,true}) for(bool shift:{false,true}) {
        auto m=base();m.modifiers[2]=shift;
        if(region==1)m.v_radarCellAtPoint=Cell{12,13};
        if(region==2){m.v_superWeaponAtPoint=RuntimeMapLeftSuperWeaponChoice{"GDI","Chrono","Chronosphere"};m.v_offlineBeginSuperWeapon=accepted;}
        if(region==3)m.v_sidebarToolAtPoint=Tool::Repair;
        if(region==4){m.v_sidebarToolAtPoint=Tool::Sell;m.v_activeTool=Tool::Sell;}
        if(region==5)m.v_buildTabAtPoint=RuntimeBuildKind::Unit;
        if(region==6){m.v_buildPageButtonAtPoint=accepted?1:-1;m.v_activeBuildPage=accepted?2:0;}
        if(region==7){m.v_buildButtonAtPoint=0;m.v_prepareReadyProduction=accepted;m.v_preparedReadyCatalogAvailable=shift;}
        add("sidebar-"+std::to_string(region)+show(accepted)+show(shift),m,Entry::Down,{550,50});
    }
    const std::vector<std::string> modes{"none","notarget","wholemap","map","twostage","two-stage","cell",""," CELL "," TwO-StAgE ","unknown"};
    for(const auto& mode:modes) for(bool rule:{false,true}) for(bool route:{false,true})
        for(bool accepted:{false,true}) for(auto state:{SWState::Ready,SWState::Charging,SWState::Suspended,SWState::Other}) {
        auto m=base();m.v_superWeaponAtPoint=RuntimeMapLeftSuperWeaponChoice{" GDI ","Chrono","Chronosphere"};
        m.v_superWeaponTargetMode=rule?std::optional<std::string>{mode}:std::nullopt;
        m.v_usesIntentRouter=route;m.v_chosenSuperWeaponState=state;
        m.v_routerBeginSuperWeapon=accepted;m.v_routerConfirmSuperWeapon=accepted;
        m.v_offlineBeginSuperWeapon=accepted;m.v_multiplayerBridgePresent=rule;
        m.v_suppressPlayerAudio=!rule;
        add("SW-begin-"+mode+show(rule)+show(route)+show(accepted)+show(state),m,Entry::Down,{550,50});
    }
    for(int dx:{0,4,5,-4,-5}) for(int dy:{0,4,5,-4,-5}) for(bool active:{false,true})
        for(bool right:{false,true}) for(bool placement:{false,true}) {
        auto m=base();m.gesture.active=active;m.gesture.start={100,100};
        m.v_handleExistingRightMove=right;m.v_selectedPlacementPresent=placement;
        out.push_back({"move-latch-"+std::to_string(dx)+","+std::to_string(dy)+show(active)+show(right)+show(placement),
            m,{{Entry::Move,{{100+dx,100+dy},0}},{Entry::Move,{{100,100},0}}}});
    }
    for(bool observer:{false,true}) for(bool captured:{false,true}) for(bool active:{false,true}) {
        auto m=base();m.v_rejectObserverSelection=observer;m.v_ownsCapture=captured;m.gesture.active=active;
        add("observer-down"+show(observer)+show(captured)+show(active),m,Entry::Down);
        add("observer-up"+show(observer)+show(captured)+show(active),m,Entry::Up);
    }
    for(int route=0;route<3;++route) for(bool picked:{false,true}) for(bool ground:{false,true})
        for(bool accepted:{false,true}) for(int remaining=0;remaining<3;++remaining) {
        auto m=base();m.v_usesIntentRouter=route!=0;m.v_routerSuperWeaponPresent=route==2;
        m.v_routerSuperWeaponRemaining=RuntimeMapLeftSuperWeaponRemaining{"Chrono",2,static_cast<std::size_t>(remaining),{3,4}};
        m.v_pickedObjectPresent=picked;m.v_superWeaponTargetsGroundArea=ground;
        m.v_routerTargetSuperWeapon=accepted;m.v_offlineSubmitSuperWeapon=accepted;
        m.v_multiplayerBridgePresent=ground;m.v_superWeaponState=remaining==1?SWState::AwaitingSecondTarget:SWState::Other;
        m.v_superWeaponSnapshots={{"Nod","wrong",true},{" GdI ","Chrono",true},{"gdi","later",true}};
        add("SW-submit-"+std::to_string(route)+show(picked)+show(ground)+show(accepted)+std::to_string(remaining),m,Entry::SuperWeapon);
        add("SW-up-priority-"+std::to_string(route)+show(picked)+show(ground)+show(accepted)+std::to_string(remaining),m,Entry::Up);
    }
    for(bool moved:{false,true}) for(bool picked:{false,true}) for(bool structure:{false,true})
        for(bool accepted:{false,true}) for(auto tool:{Tool::Repair,Tool::Sell}) {
        auto m=base();m.gesture.moved=moved;m.v_pickedObjectPresent=picked;
        m.v_pickedObjectIsStructure=structure;m.v_activeTool=tool;m.v_dispatchStructureTool=accepted;
        m.v_economyStatus=accepted?"tool done":"No CREDITS for tool";
        add("tool-"+show(moved)+show(picked)+show(structure)+show(accepted)+show(tool),m,Entry::Up);
    }
    for(bool route:{false,true}) for(bool valid:{false,true}) for(bool accepted:{false,true}) for(auto p:points) {
        auto m=base();m.v_selectedPlacementPresent=true;m.v_usesIntentRouter=route;
        m.v_placementValid=valid;m.v_confirmPlacement=accepted;
        add("placement-"+show(route)+show(valid)+show(accepted)+show(p),m,Entry::Up,p);
    }
    for(int flags=0;flags<8;++flags) for(int selection=0;selection<3;++selection)
        for(bool moved:{false,true}) for(bool picked:{false,true}) for(bool movable:{false,true})
        for(bool wall:{false,true}) for(bool accepted:{false,true}) {
        auto m=base();m.modifiers={bool(flags&1),bool(flags&2),bool(flags&4)};
        m.selected=selection==0?std::vector<std::int32_t>{}:selection==1?std::vector<std::int32_t>{7}:std::vector<std::int32_t>{7,8};
        m.gesture.moved=moved;m.v_pickedObjectPresent=picked;m.v_selectedMovableCount=movable?1:0;
        m.v_prepareTerrainWall=wall;m.v_dispatchTargetOrder=accepted;
        m.v_dispatchSecondary=accepted;m.v_mouseFactoryCanBePrimary=accepted;
        m.v_mouseFactoryCanRally=accepted;m.v_dispatchFactory=accepted;
        m.v_boxProjectionItems={{{},{60,60,110,110}},{7,{70,70,110,110}},{8,{0,0,1,1}},{9,{80,80,120,120}}};
        m.ineligibleBoxIds={9};
        const auto raw=std::uint64_t((flags&1?8:0)|(flags&4?4:0));
        add("selection-"+std::to_string(flags)+std::to_string(selection)+show(moved)+show(picked)+show(movable)+show(wall)+show(accepted),m,Entry::Up,{100,100},raw);
    }
    for(int mode=0;mode<8;++mode) for(int flags=0;flags<8;++flags) for(bool accepted:{false,true}) {
        auto m=base();m.modifiers={bool(flags&1),bool(flags&2),bool(flags&4)};
        m.v_beaconTargeting=mode==1;m.v_followTargeting=mode==2;m.v_rallyTargeting=mode==3;
        m.v_planningWaypoints=mode==4;m.v_activeTool=mode==5?Tool::Repair:Tool::None;
        m.v_beacons={{"gdi",{4,6},77,"owned text",2},{"Nod",{4,6},77,"foreign",1}};
        m.v_beaconVisible=mode!=6;m.v_pickedObjectPresent=mode!=7;m.v_dispatchBeacon=accepted;
        m.v_dispatchFactory=accepted;m.v_dispatchTargetOrder=accepted;
        add("target-"+std::to_string(mode)+std::to_string(flags)+show(accepted),m,Entry::Target);
    }
    for(int dx:{0,11,12,13}) for(int dy:{0,1}) for(bool loop:{false,true})
        for(bool selected:{false,true}) for(bool closeAccepted:{false,true}) {
        auto m=base();m.v_planningWaypoints=true;m.selected=selected?std::vector<std::int32_t>{7}:std::vector<std::int32_t>{};
        m.v_waypointRoutes={{7,"gdi",loop?1U:0U,{waypoint(10),waypoint(11)}},{8,"gdi",0,{waypoint(12),waypoint(13)}}};
        m.waypointPositions={{10,Point{100+dx,100+dy}},{11,std::nullopt},{12,Point{100+dx,100+dy}},{13,Point{100,300}}};
        m.v_closeWaypointLoop=closeAccepted;
        add("waypoint-strict-first-"+std::to_string(dx)+std::to_string(dy)+show(loop)+show(selected)+show(closeAccepted),m,Entry::Waypoint);
    }
    const auto prepareTrigger=[](Model& m,const std::string& trigger) {
        m.v_ownsCapture=true;m.v_selectedMovableCount=1;m.v_pickedObjectPresent=true;
        if(trigger=="dispatchTargetOrder")m.v_pickedObjectId=8;
        if(trigger=="dispatchFactory")m.v_mouseFactoryCanBePrimary=true;
        if(trigger=="refreshAuthority"){m.v_selectedPlacementPresent=true;m.v_usesIntentRouter=true;}
        if(trigger=="beginChronosphereMarker"){
            m.v_usesIntentRouter=true;m.v_routerSuperWeaponPresent=true;
            m.v_routerSuperWeaponRemaining=RuntimeMapLeftSuperWeaponRemaining{"Chrono",2,0,{3,4}};
        }
        if(trigger=="notifyParasiteHostsRelocated")m.v_superWeaponSnapshots={{"GDI","Chrono",true}};
        if(trigger=="activateMenuAction"){
            m.v_menuOpen=true;m.v_menuPressed=Action::AdjustSoundVolume;
            m.v_menuActionAtPoint=Action::AdjustSoundVolume;
        }
        if(trigger=="applyMenuSliderRuntimeEffects"||trigger=="persistMenuPreferences"){
            m.v_menuOpen=true;m.v_menuSliderDrag=Action::AdjustSoundVolume;
        }
    };
    for(const auto* triggerValue:{"focusWindow","captureWindow","releaseCapture","surfaceCell","prepareProjection",
        "pickProjection","refreshAuthority","dispatchTargetOrder","dispatchFactory","dispatchSecondary",
        "applySelection","beginChronosphereMarker","notifyParasiteHostsRelocated","activateMenuAction",
        "applyMenuSliderRuntimeEffects","persistMenuPreferences"}) {
        const std::string trigger=triggerValue;
        auto m=base();prepareTrigger(m,trigger);
        m.hook=[trigger](Model& model,const std::string& name) {
            if(name!=trigger||model.callbackBusy)return;
            model.callbackBusy=true;++model.callbackCount;++model.rootEpoch;++model.projectionEpoch;
            model.gesture.active=true;model.gesture.moved=false;
            model.v_pickedObjectId=99;model.v_interactionStatus="fresh-root";
            model.v_clientRect={0,0,320,240};model.v_selectedPlacementPresent=(name=="releaseCapture");
            model.selected={99};model.callbackBusy=false;
        };
        add("fresh-"+trigger,m,trigger=="focusWindow"||trigger=="captureWindow"?Entry::Down:Entry::Up);
    }
    for(const auto* triggerValue:{"releaseCapture","captureWindow","focusWindow","dispatchTargetOrder",
        "applySelection","prepareProjection","persistMenuPreferences","activateMenuAction"}) {
        const std::string trigger=triggerValue;
        auto m=base();prepareTrigger(m,trigger);
        m.hook=[trigger](Model& model,const std::string& name) {
            if(name!=trigger||model.callbackBusy)return;
            model.callbackBusy=true;++model.callbackCount;++model.rootEpoch;
            const Step nested{Entry::Move,{{44,45},0}};
            (void)invoke(model,nested);
            model.callbackBusy=false;
        };
        add("nested-"+trigger,m,trigger=="captureWindow"||trigger=="focusWindow"?Entry::Down:Entry::Up);
    }
    for(const auto* triggerValue:{"worldLoaded","focusWindow","captureWindow","releaseCapture","surfaceCell"}) {
        const std::string trigger=triggerValue;
        auto m=base();m.v_ownsCapture=true;
        m.hook=[trigger](Model& model,const std::string& name){
            if(name!=trigger||model.callbackBusy)return;
            model.callbackBusy=true;++model.callbackCount;
            model.eventSource={{600,470},0xffffffffffffffffULL};
            model.callbackBusy=false;
        };
        const auto entry=trigger=="releaseCapture"||trigger=="surfaceCell"?Entry::Up:Entry::Down;
        out.push_back({"fresh-owned-event-"+trigger,m,{{entry,{{100,100},0},true}}});
    }
    return out;
}
}

int main() {
    try {
        auto cases=scenarios();
        std::map<std::string,std::pair<Scenario,std::size_t>> primitiveSeeds;
        for(const auto& scenario:cases) compare(scenario,true,primitiveSeeds);
        for(const auto& [name,seed]:primitiveSeeds) {
            auto exceptional=seed.first;exceptional.name="exception-"+name;exceptional.seed.throwAt=seed.second;
            compare(exceptional,false,primitiveSeeds);++exceptionCases;
        }
        std::cout<<"MapRightPointer is an existing independent child; no pointer geometry or authority algorithm is modeled as migrated here.\n";
        std::cout<<"Map-left-pointer contract passed: checks="<<checks
            <<" differentialCases="<<differences<<" orderedRows="<<orderedRows
            <<" dynamicCases="<<dynamicCases<<" nestedCases="<<nestedCases
            <<" exceptionCases="<<exceptionCases<<" failures=0\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Map-left-pointer contract failed: "<<error.what()<<"\n";
        return 1;
    }
}
