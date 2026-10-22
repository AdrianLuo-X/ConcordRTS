#include "ui/input/RuntimeReferencePresentationController.h"
#include "ui/input/RuntimePlayerCommands.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
using Kind = RuntimeBuildKind;
using Tool = RuntimeSidebarTool;
using Lane = RuntimeProductionLane;
using Order = RuntimeProductionLaneOrder;
using Ready = RuntimeReferencePresentationReady;
using Preview = RuntimeReferencePresentationPreview;
using Rect = RuntimeInputPixelRect;
using Point = RuntimeInputPixelPoint;

std::string orderText(const Order& o) {
    std::ostringstream out;
    out << o.serial << ':' << o.itemKey.size() << ':' << std::quoted(o.itemKey)
        << ':' << o.cost << ':' << o.totalTicks << ':' << o.remainingTicks
        << ':' << o.creditsSpent << ':' << o.creditRemainderMicros
        << ':' << o.zeroCostProgressMicros << ':' << o.rateDivisionRemainder
        << ':' << o.rateDivisionDenominator << ':' << o.requiresPlacement;
    return out.str();
}
std::string rectText(const Rect& r) {
    return std::to_string(r.left) + ',' + std::to_string(r.top) + ','
        + std::to_string(r.right) + ',' + std::to_string(r.bottom);
}
std::string pointText(const Point& p) { return std::to_string(p.x) + ',' + std::to_string(p.y); }
std::string localText(const RuntimePlayerLocalCommands& local) {
    std::ostringstream out;
    for (const auto& team : local.teams) {
        out << '['; for (int id : team) out << id << ','; out << ']';
    }
    for (const auto& view : local.views) {
        if (view) out << '(' << view->x << ',' << view->y << ')'; else out << '-';
    }
    out << ';' << local.planning << ';' << local.selectedWaypoint
        << ';' << local.followTargeting << ';' << local.rallyTargeting
        << ';' << local.cameraFollow << ';' << local.showCoordinates
        << ';' << local.lastSelectionKey << ';' << local.lastSelectionAtMs
        << ';' << local.veterancyRank << ';' << local.healthRank;
    return out.str();
}
RuntimePlayerLocalCommands seededLocal() {
    RuntimePlayerLocalCommands local;
    for (std::size_t i = 0; i < local.teams.size(); ++i)
        local.teams[i] = {static_cast<int>(i) + 1, static_cast<int>(i) + 101};
    local.views[0] = RuntimeMultiplayerGameplayCell{-4, 7};
    local.views[2] = RuntimeMultiplayerGameplayCell{std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()};
    local.planning = true;
    local.selectedWaypoint = std::numeric_limits<std::uint64_t>::max();
    local.followTargeting = local.rallyTargeting = true;
    local.showCoordinates = true;
    local.lastSelectionKey = 0x7ff;
    local.lastSelectionAtMs = std::numeric_limits<std::uint64_t>::max() - 499;
    local.veterancyRank = 2; local.healthRank = 1;
    return local;
}
Order makeOrder(std::uint64_t serial, const std::string& key) {
    Order o;
    o.serial = serial; o.itemKey = key; o.cost = 43; o.totalTicks = 79;
    o.remainingTicks = 0; o.creditsSpent = 43; o.creditRemainderMicros = 123;
    o.zeroCostProgressMicros = 456; o.rateDivisionRemainder = 789;
    o.rateDivisionDenominator = 17; o.requiresPlacement = true;
    return o;
}
struct Item {
    std::size_t ordinal = 0;
    Kind kind = Kind::Building;
    std::string typeId;
    std::optional<Order> ready;
    Lane lane = Lane::Structures;
    bool beginAccepted = true;
};
std::vector<Item> seededCatalog() {
    return {{0,Kind::Unit,"MTNK",makeOrder(77,"MTNK"),Lane::Vehicles,true},
        {1,Kind::Building,"GAPOWR",std::nullopt,Lane::Structures,true},
        {2,Kind::Defense,"GAPILL",makeOrder(23,"GAPILL"),Lane::Armory,true},
        {3,Kind::Building,"GACNST",makeOrder(91,"GACNST"),Lane::Structures,true},
        {4,Kind::Building,"GAREFN",makeOrder(1,"GAREFN"),Lane::Structures,true},
        {5,Kind::Infantry,"E1",makeOrder(15,"E1"),Lane::Infantry,true}};
}
struct Scenario {
    RuntimePlayerLocalCommands local = seededLocal();
    Kind tab = Kind::Unit;
    Tool tool = Tool::Sell;
    bool beacon = true, dragMoved = true, dragActive = true, mouseValid = true;
    Point mouse{100,100};
    Rect client{0,0,800,600};
    int page = 19, clampPage = 2;
    std::optional<Kind> clampTab;
    std::map<std::size_t,Kind> kindQueryTabChanges;
    std::map<std::string,std::int32_t> bindings;
    std::map<std::string,std::vector<std::int32_t>> bindingScripts;
    std::vector<Item> catalog = seededCatalog();
    std::vector<int> selected{7,8};
    std::optional<std::string> renameOnBegin;
    bool centerDisablesFollow = false;
    Point centerBaseResult{-110,222}, centerSelectionResult{333,-444};
    std::string status = "retained previous placement";
    bool repaint = false;
};
struct Fixture;
struct Trace {
    Fixture* fixture = nullptr;
    std::vector<std::string> events;
    void record(const std::string& label);
};
template<class T> struct Tracked {
    T value;
    Trace* trace;
    const char* writeLabel;
    const char* readLabel;
    Tracked(T initial, Trace& sink, const char* write, const char* read = "")
        : value(std::move(initial)), trace(&sink), writeLabel(write), readLabel(read) {}
    Tracked& operator=(const T& next) { value = next; trace->record(writeLabel); return *this; }
    Tracked& operator=(const Tracked& next) { return *this = next.value; }
    operator T() const { if (*readLabel != '\0') trace->record(readLabel); return value; }
};
struct Catalog {
    std::vector<Item> items;
    Trace* trace;
    std::size_t sizeQueries = 0;
    Catalog(const std::vector<Item>& input, Trace& sink) : items(input), trace(&sink) {}
    auto begin() { return items.begin(); }
    auto end() { ++sizeQueries; trace->record("catalog-size:" + std::to_string(items.size())); return items.end(); }
    std::size_t querySize() { ++sizeQueries; trace->record("catalog-size:" + std::to_string(items.size())); return items.size(); }
};
struct Selection {
    std::vector<int> ids;
    Trace* trace;
    Selection(const std::vector<int>& input, Trace& sink) : ids(input), trace(&sink) {}
    bool empty() { trace->record("selection-empty"); return ids.empty(); }
};
struct Communication {
    Tracked<bool> beaconTargeting;
    Communication(bool beacon, Trace& sink) : beaconTargeting(beacon,sink,"set-beacon") {}
};
struct Economy { Catalog catalog; Economy(const std::vector<Item>& input, Trace& sink) : catalog(input,sink) {} };

struct Fixture {
    Trace trace;
    Scenario config;
    RuntimePlayerLocalCommands playerLocalCommands;
    Communication playerCommunication;
    Tracked<Kind> activeBuildTab;
    Tracked<Tool> activeSidebarTool;
    Tracked<bool> selectionDragMoved, selectionDragActive;
    Tracked<std::string> interactionStatus;
    Tracked<bool> repaintRequested, mouseClientPointValid;
    Point mouseClientPoint;
    Economy economy;
    Selection selectedObjectIds;
    std::map<std::string,std::size_t> bindingReads;
    std::vector<std::size_t> kindReads, readyReads, begins;
    std::optional<Ready> placement;
    std::vector<Preview> previews;
    int page;
    Point camera{17,18};
    int clampCalls = 0, viewportQueries = 0, soundCalls = 0, centerBaseCalls = 0, centerSelectionCalls = 0;
    // Opaque sentinels, not an invented implementation of authority or world.
    std::uint64_t tick = 21, currentTick = 21;
    std::uint64_t commandHash = 0xfedcba9876543210ULL, worldHash = 0x123456789abcdef0ULL;
    int inputDelay = 2, captures = 0, pending = 0, credits = 943;
    explicit Fixture(const Scenario& s)
        : config(s), playerLocalCommands(s.local), playerCommunication(s.beacon,trace),
          activeBuildTab(s.tab,trace,"set-tab","active-tab"), activeSidebarTool(s.tool,trace,"set-tool","sidebar-tool"),
          selectionDragMoved(s.dragMoved,trace,"set-drag-moved"), selectionDragActive(s.dragActive,trace,"set-drag-active"),
          interactionStatus(s.status,trace,"set-status"), repaintRequested(s.repaint,trace,"set-repaint"),
          mouseClientPointValid(s.mouseValid,trace,"unused-mouse-write","mouse-valid"),
          mouseClientPoint(s.mouse), economy(s.catalog,trace), selectedObjectIds(s.selected,trace), page(s.page) {
        trace.fixture = this;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    std::string stateText() const {
        std::ostringstream out;
        out << static_cast<int>(activeBuildTab.value) << ';' << static_cast<int>(activeSidebarTool.value)
            << ';' << playerCommunication.beaconTargeting.value << ';' << selectionDragMoved.value
            << ';' << selectionDragActive.value << ';' << interactionStatus.value.size() << ':' << std::quoted(interactionStatus.value)
            << ';' << repaintRequested.value << ';' << mouseClientPointValid.value
            << ';' << pointText(mouseClientPoint) << ';' << rectText(config.client)
            << ';' << page << ';' << pointText(camera) << ';' << localText(playerLocalCommands) << ";selection=";
        for (int id : selectedObjectIds.ids) out << id << ',';
        out << ";catalog=";
        for (const auto& item : economy.catalog.items) {
            out << '[' << item.ordinal << ',' << static_cast<int>(item.kind) << ',' << item.typeId.size()
                << ':' << std::quoted(item.typeId) << ',' << static_cast<int>(item.lane) << ',' << item.beginAccepted << ',';
            if (item.ready) out << orderText(*item.ready); else out << '-';
            out << ']';
        }
        out << ";placement=";
        if (placement) out << placement->catalogOrdinal << ',' << static_cast<int>(placement->lane) << ',' << orderText(placement->order);
        else out << '-';
        out << ";previews=";
        for (const auto& p : previews) out << '[' << rectText(p.viewport) << ':' << pointText(p.point) << ']';
        out << ";bindings=";
        for (const auto& read : bindingReads) out << read.first << ':' << read.second << ',';
        out << ";kind="; for (auto i : kindReads) out << i << ',';
        out << ";ready="; for (auto i : readyReads) out << i << ',';
        out << ";begin="; for (auto i : begins) out << i << ',';
        out << ";counts=" << economy.catalog.sizeQueries << ',' << clampCalls << ',' << viewportQueries
            << ',' << soundCalls << ',' << centerBaseCalls << ',' << centerSelectionCalls;
        out << ";authority=" << tick << ',' << currentTick << ',' << commandHash << ',' << worldHash
            << ',' << inputDelay << ',' << captures << ',' << pending << ',' << credits;
        return out.str();
    }
    std::int32_t binding(const std::string& id, std::int32_t fallback) {
        const auto read = bindingReads[id]++;
        auto result = fallback;
        const auto configured = config.bindings.find(id);
        if (configured != config.bindings.end()) result = configured->second;
        const auto scripted = config.bindingScripts.find(id);
        if (scripted != config.bindingScripts.end() && read < scripted->second.size()) result = scripted->second[read];
        trace.record("binding:" + id + ':' + std::to_string(fallback) + ':' + std::to_string(result));
        return result;
    }
    void clamp(const Rect& sidebar) {
        ++clampCalls; trace.record("clamp:" + rectText(sidebar));
        page = config.clampPage;
        if (config.clampTab) activeBuildTab = *config.clampTab;
        trace.record("clamp-result");
    }
    void noteKind(std::size_t ordinal) {
        kindReads.push_back(ordinal); trace.record("catalog-kind:" + std::to_string(ordinal));
        const auto change = config.kindQueryTabChanges.find(ordinal);
        if (change != config.kindQueryTabChanges.end()) activeBuildTab = change->second;
    }
    const Order* readyFor(const Item& item, Lane* lane) {
        readyReads.push_back(item.ordinal); *lane = item.lane;
        trace.record("ready:" + std::to_string(item.ordinal) + ':' + std::to_string(static_cast<int>(*lane)));
        return item.ready ? &*item.ready : nullptr;
    }
    bool begin(std::size_t ordinal, Lane lane, const Order& order, bool userAction) {
        begins.push_back(ordinal);
        trace.record("begin:" + std::to_string(ordinal) + ':' + std::to_string(static_cast<int>(lane))
            + ':' + orderText(order) + ':' + std::to_string(userAction));
        auto& item = economy.catalog.items[ordinal];
        if (item.beginAccepted) {
            placement = Ready{ordinal,lane,order};
            if (config.renameOnBegin) item.typeId = *config.renameOnBegin;
        }
        trace.record("begin-result:" + std::to_string(item.beginAccepted));
        return item.beginAccepted;
    }
    Rect viewport(const Rect& r) { ++viewportQueries; trace.record("viewport:" + rectText(r)); return r; }
    void preview(const Rect& r, const Point& p) {
        previews.push_back(Preview{r,p}); trace.record("preview:" + rectText(r) + ':' + pointText(p));
    }
    void sound() { ++soundCalls; trace.record("sound:sidebar-click"); }
    void centerBase() {
        ++centerBaseCalls; trace.record("center-base");
        playerLocalCommands.cameraFollow = false; camera = config.centerBaseResult; trace.record("center-base-result");
    }
    void centerSelection() {
        ++centerSelectionCalls; trace.record("center-selection");
        camera = config.centerSelectionResult;
        if (config.centerDisablesFollow) playerLocalCommands.cameraFollow = false;
        trace.record("center-selection-result");
    }
};
void Trace::record(const std::string& label) { events.push_back(label + "|" + fixture->stateText()); }

class Host final : public RuntimeReferencePresentationHost {
public:
    Fixture& state;
    explicit Host(Fixture& fixture) : state(fixture) {}
    std::int32_t binding(const std::string& id, std::int32_t fallback) override { return state.binding(id,fallback); }
    void setActiveBuildTab(Kind kind) override { state.activeBuildTab = kind; }
    Kind activeBuildTab() override { return state.activeBuildTab; }
    void clampActiveBuildPage() override { state.clamp(runtimeInputPixelSidebarRect(state.config.client,168)); }
    std::size_t catalogSize() override { return state.economy.catalog.querySize(); }
    Kind catalogKind(std::size_t ordinal) override { state.noteKind(ordinal); return state.economy.catalog.items[ordinal].kind; }
    std::optional<Ready> readyForItem(std::size_t ordinal) override {
        const auto& item = state.economy.catalog.items[ordinal];
        auto lane = Lane::Structures;
        const auto* order = state.readyFor(item,&lane);
        if (!order) return std::nullopt;
        return Ready{ordinal,lane,*order};
    }
    bool beginReadyPlacement(const Ready& ready) override { return state.begin(ready.catalogOrdinal,ready.lane,ready.order,true); }
    Tool sidebarTool() override { return state.activeSidebarTool; }
    void setSidebarTool(Tool tool) override { state.activeSidebarTool = tool; }
    void setBeaconTargeting(bool targeting) override { state.playerCommunication.beaconTargeting = targeting; }
    void setSelectionDragMoved(bool moved) override { state.selectionDragMoved = moved; }
    void setSelectionDragActive(bool active) override { state.selectionDragActive = active; }
    void placementStatus(std::size_t ordinal) override { state.interactionStatus = "placing " + state.economy.catalog.items[ordinal].typeId; }
    std::optional<Preview> stationaryPreviewInput() override {
        const auto viewport = state.viewport(runtimeInputPixelMapViewport(state.config.client,168));
        if (!state.mouseClientPointValid) return std::nullopt;
        return Preview{viewport,state.mouseClientPoint};
    }
    void previewPlacement(const Preview& p) override { state.preview(p.viewport,p.point); }
    void sidebarClickSound() override { state.sound(); }
    void requestRepaint() override { state.repaintRequested = true; }
    bool selectionEmpty() override { return state.selectedObjectIds.empty(); }
    void centerBase() override { state.centerBase(); }
    void centerSelection() override { state.centerSelection(); }
};

// Independent old adapters instrument only primitive owner boundaries. Tracked
// assignment/conversion observes the original expressions without replacing
// their decisions. Catalog::end observes the original range-for's single end.
std::int32_t runtimeMenuKeyboardBindingCode(Fixture& state, const std::string& id, int fallback) {
    return state.binding(id,fallback);
}
Rect runtimeSidebarRect(const Rect& client) {
    return {std::max(client.left,client.right - 168),client.top,client.right,client.bottom};
}
void runtimeClampActiveBuildPage(Fixture& state, const Rect& sidebar) { state.clamp(sidebar); }
bool frozenSingleStructureSlot(Kind kind) { return kind == Kind::Building || kind == Kind::Defense; }
const Order* runtimeReadyProductionForItem(Fixture& state, const Item& item, Lane* lane) { return state.readyFor(item,lane); }
struct OldPlacement { Lane lane; Order order; const Item* item; };
bool beginRuntimePlacementFromUi(Fixture& state, const OldPlacement& placement, bool userAction) {
    return state.begin(placement.item->ordinal,placement.lane,placement.order,userAction);
}
Rect frozenMapViewport(Fixture& state, const Rect& client) {
    return state.viewport({client.left,client.top,std::max(client.left,client.right - 168),client.bottom});
}
bool pointInRectInclusive(const Rect& rect, const Point& point) {
    return point.x >= rect.left && point.x <= rect.right
        && point.y >= rect.top && point.y <= rect.bottom;
}
void updateRuntimePlacementPreview(Fixture& state, const Rect& viewport, const Point& point) { state.preview(viewport,point); }
inline constexpr int runtimeSoundIntentSidebarButtonClick = 31;
void emitRuntimeSound(Fixture& state, int intent) { if (intent == runtimeSoundIntentSidebarButtonClick) state.sound(); }
void runtimePlayerCenterBase(Fixture& state, const Rect&) { state.centerBase(); }
void runtimePlayerCenterSelection(Fixture& state, const Rect&) { state.centerSelection(); }

// Frozen RuntimePlayerHotkeysUi.inl:129..183, before the production extraction.
// Entry/Rect types and two helper names are adapted; noteKind is trace-only and
// frozenMapViewport adds state solely to observe the original viewport query.
// The final false denotes fallthrough into the still-unmigrated reference tail.
bool frozenOldReferenceKey(Fixture& state, const Rect& client, int code, bool repeated) {
    auto& local = state.playerLocalCommands; auto& ui = state.playerCommunication;
    const auto matches = [&](const std::string& id, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, id, fallback);
    };
    const std::pair<const char*, RuntimeBuildKind> tabs[] = {
        {"StructureTab", RuntimeBuildKind::Building}, {"DefenseTab", RuntimeBuildKind::Defense},
        {"InfantryTab", RuntimeBuildKind::Infantry}, {"UnitTab", RuntimeBuildKind::Unit}};
    constexpr int tabKeys[] = {'Q','W','E','R'};
    for (int i = 0; i < 4; ++i) if (matches(tabs[i].first, tabKeys[i])) {
        state.activeBuildTab = tabs[i].second;
        runtimeClampActiveBuildPage(state, runtimeSidebarRect(client));
        if (!repeated && frozenSingleStructureSlot(state.activeBuildTab)) {
            for (const auto& item : state.economy.catalog) {
                state.noteKind(item.ordinal);
                if (item.kind != state.activeBuildTab) continue;
                RuntimeProductionLane lane = RuntimeProductionLane::Structures;
                const auto* ready = runtimeReadyProductionForItem(state, item, &lane);
                if (ready == nullptr) continue;
                if (beginRuntimePlacementFromUi(state, {lane, *ready, &item}, true)) {
                    state.activeSidebarTool = RuntimeSidebarTool::None;
                    local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
                    state.selectionDragActive = state.selectionDragMoved = false;
                    state.interactionStatus = "placing " + item.typeId;
                    // A keyboard selection must follow a stationary mouse too.
                    const Rect viewport = frozenMapViewport(state, client);
                    if (state.mouseClientPointValid
                        && pointInRectInclusive(viewport, state.mouseClientPoint)) {
                        updateRuntimePlacementPreview(state, viewport, state.mouseClientPoint);
                    }
                    emitRuntimeSound(state, runtimeSoundIntentSidebarButtonClick);
                    state.repaintRequested = true;
                }
                break;
            }
        }
        return true;
    }
    if (matches("ToggleRepair", 'K') || matches("ToggleSell", 'L')) {
        if (!repeated) {
            const auto tool = matches("ToggleRepair", 'K') ? RuntimeSidebarTool::Repair : RuntimeSidebarTool::Sell;
            state.activeSidebarTool = state.activeSidebarTool == tool ? RuntimeSidebarTool::None : tool;
            local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
        }
        return true;
    }
    if (matches("CenterBase", 'H')) { runtimePlayerCenterBase(state, client); return true; }
    if (matches("ShowCoordinates", 'O')) {
        if (!repeated) local.showCoordinates = !local.showCoordinates;
        return true;
    }
    if (matches("Follow", 'F')) {
        if (!repeated) local.cameraFollow = !local.cameraFollow && !state.selectedObjectIds.empty();
        if (local.cameraFollow) runtimePlayerCenterSelection(state, client);
        return true;
    }
    return false;
}

struct Checks {
    std::size_t assertions = 0, differentialCases = 0, bindingQueries = 0, orderedTraceRows = 0, ownedChecks = 0, failures = 0;
    void expect(bool condition, const std::string& name) {
        ++assertions;
        if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
    }
};
void comparePair(Checks& checks, const std::string& name, const Fixture& oldState,
    const Fixture& actual, bool oldResult, bool result,
    std::size_t previouslyCountedBindings = 0, std::size_t previouslyCountedRows = 0) {
    ++checks.differentialCases;
    checks.expect(result == oldResult,name + " handled");
    checks.expect(actual.stateText() == oldState.stateText(),name + " full-state");
    checks.expect(actual.trace.events == oldState.trace.events,name + " full-ordered-trace");
    if (actual.trace.events != oldState.trace.events) {
        const auto count = std::min(actual.trace.events.size(),oldState.trace.events.size());
        for (std::size_t i = 0; i < count; ++i) if (actual.trace.events[i] != oldState.trace.events[i]) {
            std::cerr << "TRACE " << name << " row=" << i << " old=" << oldState.trace.events[i]
                << " new=" << actual.trace.events[i] << '\n';
            break;
        }
    }
    checks.expect(actual.tick == 21 && actual.currentTick == 21 && actual.inputDelay == 2
        && actual.commandHash == 0xfedcba9876543210ULL && actual.worldHash == 0x123456789abcdef0ULL
        && actual.captures == 0 && actual.pending == 0 && actual.credits == 943,name + " opaque authority sentinels unchanged");
    const auto& local = actual.playerLocalCommands;
    const auto& before = actual.config.local;
    checks.expect(local.teams == before.teams && local.views == before.views
        && local.planning == before.planning && local.selectedWaypoint == before.selectedWaypoint
        && local.lastSelectionKey == before.lastSelectionKey && local.lastSelectionAtMs == before.lastSelectionAtMs
        && local.veterancyRank == before.veterancyRank && local.healthRank == before.healthRank,
        name + " other LocalCommands fields unchanged");
    std::size_t bindingQueries = 0;
    for (const auto& read : actual.bindingReads) bindingQueries += read.second;
    checks.bindingQueries += bindingQueries - previouslyCountedBindings;
    checks.orderedTraceRows += actual.trace.events.size() - previouslyCountedRows;
}
template<class Verify> void runCase(Checks& checks, const std::string& name, const Scenario& scenario,
    const RuntimeReferencePresentationInput& input, Verify verify) {
    Fixture oldState(scenario), actual(scenario);
    Host host(actual);
    checks.expect(actual.trace.events.empty(),name + " host constructor has no query");
    const bool oldResult = frozenOldReferenceKey(oldState,scenario.client,input.code,input.repeated);
    const bool result = handleRuntimeReferencePresentationKey(input,actual.playerLocalCommands,host);
    comparePair(checks,name,oldState,actual,oldResult,result);
    verify(actual,result);
}
void runCase(Checks& checks, const std::string& name, const Scenario& scenario,
    const RuntimeReferencePresentationInput& input) {
    runCase(checks,name,scenario,input,[](const Fixture&,bool) {});
}
std::size_t readCount(const Fixture& state, const std::string& id) {
    const auto found = state.bindingReads.find(id);
    return found == state.bindingReads.end() ? 0 : found->second;
}

void tabMatrix(Checks& checks) {
    constexpr int keys[] = {'Q','W','E','R'};
    const std::string ids[] = {"StructureTab","DefenseTab","InfantryTab","UnitTab"};
    for (int tab = 0; tab < 4; ++tab) for (int modifier = 0; modifier < 8; ++modifier)
        for (int repeated = 0; repeated < 2; ++repeated) {
            Scenario s;
            const int code = keys[tab] | (modifier << 8);
            s.bindings[ids[tab]] = code;
            runCase(checks,"tab/" + std::to_string(tab) + '/' + std::to_string(modifier) + '/' + std::to_string(repeated),
                s,{code,repeated != 0},[&](const Fixture& state,bool handled) {
                    checks.expect(handled && state.activeBuildTab.value == static_cast<Kind>(tab)
                        && state.clampCalls == 1 && state.page == s.clampPage,"tab writes then clamp");
                    for (int later = tab + 1; later < 4; ++later)
                        checks.expect(readCount(state,ids[later]) == 0,"later tab binding is lazy");
                    checks.expect(readCount(state,"ToggleRepair") == 0,"tab consumes before tools");
                    checks.expect(state.economy.catalog.sizeQueries == ((tab < 2 && !repeated) ? 1U : 0U),
                        "only nonrepeat structural tab queries one catalog end");
                    if (tab < 2 && !repeated) checks.expect(state.begins.size() == 1 && state.viewportQueries == 1
                        && state.soundCalls == 1 && state.previews.size() == 1,"tab placement success");
                    else checks.expect(state.begins.empty() && state.viewportQueries == 0
                        && state.soundCalls == 0,"repeat or nonstructural has no ready effects");
                });
        }
    Scenario collision;
    for (const auto& id : ids) collision.bindings[id] = 0x7ff;
    collision.bindings["ToggleRepair"] = collision.bindings["CenterBase"] = 0x7ff;
    runCase(checks,"all-binding-collision",collision,{0x7ff,false},[&](const Fixture& state,bool handled) {
        checks.expect(handled && state.activeBuildTab.value == Kind::Building
            && state.bindingReads.size() == 1,"first tab collision wins");
    });
    for (int code : std::vector<int>{'X',0,-1,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()})
        runCase(checks,"unknown/" + std::to_string(code),Scenario{},{code,false},[&](const Fixture& state,bool handled) {
            checks.expect(!handled && state.bindingReads.size() == 9 && state.clampCalls == 0
                && state.begins.empty(),"unmatched falls through without a whole-tail wrapper");
        });
    for (int code : {0,-1,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
        Scenario s; s.bindings["StructureTab"] = code;
        runCase(checks,"full-int32-code/" + std::to_string(code),s,{code,false});
    }
}
void catalogCases(Checks& checks) {
    for (int mode = 0; mode < 7; ++mode) {
        Scenario s;
        if (mode == 0) s.catalog.clear();
        if (mode == 1) for (auto& item : s.catalog) item.kind = Kind::Unit;
        if (mode == 2) for (auto& item : s.catalog) item.ready.reset();
        if (mode == 3) s.catalog[3].beginAccepted = false;
        if (mode == 4) s.renameOnBegin = "GACNST-renamed-after-begin";
        if (mode == 5) s.clampTab = Kind::Infantry;
        if (mode == 6) s.kindQueryTabChanges[0] = Kind::Defense;
        runCase(checks,"catalog/" + std::to_string(mode),s,{'Q',false},[&](const Fixture& state,bool) {
            if (mode <= 2) checks.expect(state.begins.empty() && state.soundCalls == 0
                && state.activeSidebarTool.value == s.tool && state.playerCommunication.beaconTargeting.value,
                "no ready has no success effects");
            if (mode == 3) checks.expect(state.begins == std::vector<std::size_t>{3}
                && state.readyReads == std::vector<std::size_t>({1,3}) && !state.placement
                && state.activeSidebarTool.value == s.tool && state.viewportQueries == 0
                && state.selectionDragMoved.value && state.selectionDragActive.value
                && state.playerLocalCommands.followTargeting && state.playerLocalCommands.rallyTargeting,
                "first rejected ready still breaks without clearing state or trying serial 1");
            if (mode == 4) checks.expect(state.interactionStatus.value == "placing GACNST-renamed-after-begin"
                && state.placement && state.placement->catalogOrdinal == 3 && state.placement->order.serial == 91,
                "status reads original item at its original time");
            if (mode == 5) checks.expect(state.economy.catalog.sizeQueries == 0
                && state.activeBuildTab.value == Kind::Infantry,"post-clamp tab is read fresh");
            if (mode == 6) checks.expect(state.placement && state.placement->catalogOrdinal == 2
                && state.placement->lane == Lane::Armory,"per-item active tab is not a scene snapshot");
        });
    }
    for (int lane = 0; lane < 6; ++lane) for (int extreme = 0; extreme < 3; ++extreme) {
        Scenario s;
        auto order = makeOrder(extreme == 0 ? 0 : std::numeric_limits<std::uint64_t>::max(),
            std::string("quoted\\\";key\0tail",17));
        if (extreme == 1) {
            order.cost = std::numeric_limits<int>::min(); order.totalTicks = std::numeric_limits<int>::max();
            order.remainingTicks = std::numeric_limits<int>::min(); order.creditsSpent = std::numeric_limits<int>::max();
            order.creditRemainderMicros = order.zeroCostProgressMicros = std::numeric_limits<std::uint32_t>::max();
            order.rateDivisionRemainder = std::numeric_limits<std::uint64_t>::max();
            order.rateDivisionDenominator = 0; order.requiresPlacement = false;
        }
        if (extreme == 2) {
            order.cost = 0; order.totalTicks = 0; order.remainingTicks = -1; order.creditsSpent = -1;
            order.creditRemainderMicros = order.zeroCostProgressMicros = 0;
            order.rateDivisionRemainder = 0; order.rateDivisionDenominator = std::numeric_limits<std::uint32_t>::max();
        }
        s.catalog = {{0,Kind::Building,"raw-owned-order",order,static_cast<Lane>(lane),true}};
        runCase(checks,"order/" + std::to_string(lane) + '/' + std::to_string(extreme),s,{'Q',false},
            [&](const Fixture& state,bool) {
                ++checks.ownedChecks;
                checks.expect(state.placement && state.placement->catalogOrdinal == 0
                    && state.placement->lane == static_cast<Lane>(lane)
                    && orderText(state.placement->order) == orderText(order),"complete owned order forwarded without new validation");
            });
    }
    Fixture fixture(Scenario{});
    Host host(fixture);
    const auto ready = host.readyForItem(3);
    checks.expect(ready.has_value(),"owned lifetime obtains real complete value");
    const auto text = ready ? orderText(ready->order) : std::string{};
    fixture.economy.catalog.items.clear();
    ++checks.ownedChecks;
    checks.expect(ready && ready->catalogOrdinal == 3 && ready->lane == Lane::Structures
        && orderText(ready->order) == text,"ready result owns all data after source-container lifetime");
}
void previewCases(Checks& checks) {
    const Rect clients[] = {{0,0,800,600},{-500,-400,300,200},{10,20,178,20},{10,20,100,100},
        {std::numeric_limits<std::int32_t>::min(),-1,-200,1},
        {std::numeric_limits<std::int32_t>::max() - 200,0,std::numeric_limits<std::int32_t>::max(),32},
        {-10,30,300,20}};
    for (std::size_t ci = 0; ci < std::size(clients); ++ci) {
        const auto& client = clients[ci];
        const auto right = std::max(client.left,client.right - 168);
        const Point points[] = {{client.left,client.top},{right,client.bottom},{client.left,client.bottom},
            {right,client.top},{right + 1,client.top},{client.left,client.bottom + 1},{right - 1,client.top}};
        for (std::size_t pi = 0; pi < std::size(points); ++pi) for (int valid = 0; valid < 2; ++valid) {
            Scenario s;
            s.client = client; s.mouse = points[pi]; s.mouseValid = valid != 0;
            const bool inside = valid && s.mouse.x >= client.left && s.mouse.x <= right
                && s.mouse.y >= client.top && s.mouse.y <= client.bottom;
            runCase(checks,"preview/" + std::to_string(ci) + '/' + std::to_string(pi) + '/' + std::to_string(valid),
                s,{'Q',false},[&](const Fixture& state,bool) {
                    checks.expect(state.viewportQueries == 1 && state.previews.size() == (inside ? 1U : 0U),
                        "viewport always computed and inclusive predicate owned by controller");
                    checks.expect(state.soundCalls == 1 && state.repaintRequested.value,
                        "pointer rejection does not skip successful placement sound and repaint");
                });
        }
    }
    Fixture fixture(Scenario{});
    Host host(fixture);
    const auto preview = host.stationaryPreviewInput();
    fixture.mouseClientPoint = {901,902}; fixture.config.client = {-3,-4,10,20};
    ++checks.ownedChecks;
    checks.expect(preview && rectText(preview->viewport) == "0,0,632,600" && pointText(preview->point) == "100,100",
        "preview owns rect and point rather than host references");
}

void toolCases(Checks& checks) {
    for (int tool = 0; tool < 3; ++tool) for (int key : {'K','L'})
        for (int repeated = 0; repeated < 2; ++repeated) for (int flags = 0; flags < 8; ++flags) {
            Scenario s; s.tool = static_cast<Tool>(tool);
            s.local.followTargeting = (flags & 1) != 0; s.local.rallyTargeting = (flags & 2) != 0;
            s.beacon = (flags & 4) != 0;
            runCase(checks,"tools/" + std::to_string(tool) + '/' + std::to_string(key)
                + '/' + std::to_string(repeated) + '/' + std::to_string(flags),s,{key,repeated != 0},
                [&](const Fixture& state,bool handled) {
                    const auto wanted = key == 'K' ? Tool::Repair : Tool::Sell;
                    const auto expected = repeated ? s.tool : (s.tool == wanted ? Tool::None : wanted);
                    checks.expect(handled && state.activeSidebarTool.value == expected
                        && readCount(state,"ToggleRepair") == (repeated ? 1U : 2U)
                        && readCount(state,"ToggleSell") == (key == 'K' ? 0U : 1U),
                        "tool exact short circuit and second Repair query");
                    checks.expect(state.selectionDragMoved.value == s.dragMoved
                        && state.selectionDragActive.value == s.dragActive && state.soundCalls == 0
                        && state.viewportQueries == 0 && state.interactionStatus.value == s.status
                        && state.repaintRequested.value == s.repaint && !state.placement,
                        "tools retain drag placement presentation and no extra sound/repaint");
                });
        }
    const std::vector<std::vector<std::int32_t>> scripts{{'K','L'},{'X','L'},{'X','K'},{'K','K'}};
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        Scenario s; s.bindingScripts["ToggleRepair"] = scripts[i]; s.bindings["ToggleSell"] = 'K';
        runCase(checks,"tool-script/" + std::to_string(i),s,{'K',false},[&](const Fixture& state,bool handled) {
            const auto chosen = scripts[i][1] == 'K' ? Tool::Repair : Tool::Sell;
            checks.expect(handled && state.activeSidebarTool.value == (s.tool == chosen ? Tool::None : chosen)
                && readCount(state,"ToggleRepair") == 2,"inner Repair query cannot be cached from outer result");
        });
    }
    for (int modifier = 0; modifier < 8; ++modifier) {
        Scenario s; const int code = 'K' | (modifier << 8);
        s.bindings["ToggleRepair"] = s.bindings["ToggleSell"] = code;
        runCase(checks,"tool-rebind-collision/" + std::to_string(modifier),s,{code,false});
    }
}
void cameraAndCoordinateCases(Checks& checks) {
    for (int following = 0; following < 2; ++following) for (int empty = 0; empty < 2; ++empty)
        for (int repeated = 0; repeated < 2; ++repeated) for (int disables = 0; disables < 2; ++disables) {
            Scenario s; s.local.cameraFollow = following != 0;
            if (empty) s.selected.clear();
            s.centerDisablesFollow = disables != 0;
            runCase(checks,"follow/" + std::to_string(following) + '/' + std::to_string(empty)
                + '/' + std::to_string(repeated) + '/' + std::to_string(disables),s,{'F',repeated != 0},
                [&](const Fixture& state,bool handled) {
                    const bool shouldCenter = repeated ? following != 0 : !following && !empty;
                    const auto emptyQueries = std::count_if(state.trace.events.begin(),state.trace.events.end(),
                        [](const std::string& event) { return event.starts_with("selection-empty|"); });
                    checks.expect(handled && state.centerSelectionCalls == (shouldCenter ? 1 : 0)
                        && state.playerLocalCommands.cameraFollow == (shouldCenter && !disables),
                        "Follow nonrepeat toggle then repeated/ordinary live follow centering");
                    checks.expect(emptyQueries == ((!repeated && !following) ? 1 : 0),
                        "Follow short circuit does not prequery selection");
                });
        }
    for (int modifier = 0; modifier < 8; ++modifier) for (int repeated = 0; repeated < 2; ++repeated)
        for (int mode = 0; mode < 4; ++mode) {
            Scenario s; s.local.cameraFollow = mode % 2 != 0;
            s.centerBaseResult = {mode == 0 ? 0 : -mode * 100,mode == 3 ? -4 : mode * 200};
            s.selected = mode == 2 ? std::vector<int>{} : std::vector<int>{9,-7};
            const int code = 'H' | (modifier << 8); s.bindings["CenterBase"] = code;
            runCase(checks,"base-owner-sink/" + std::to_string(modifier) + '/' + std::to_string(repeated)
                + '/' + std::to_string(mode),s,{code,repeated != 0},[&](const Fixture& state,bool handled) {
                    checks.expect(handled && state.centerBaseCalls == 1 && !state.playerLocalCommands.cameraFollow
                        && pointText(state.camera) == pointText(s.centerBaseResult),"H forwards original camera owner even repeat");
                    checks.expect(readCount(state,"ShowCoordinates") == 0,"H consumes before O and F");
                });
        }
    for (int shown = 0; shown < 2; ++shown) for (int repeated = 0; repeated < 2; ++repeated) {
        Scenario s; s.local.showCoordinates = shown != 0;
        runCase(checks,"coordinates/" + std::to_string(shown) + '/' + std::to_string(repeated),
            s,{'O',repeated != 0},[&](const Fixture& state,bool handled) {
                checks.expect(handled && state.playerLocalCommands.showCoordinates == (repeated ? shown != 0 : shown == 0)
                    && state.centerBaseCalls == 0 && state.centerSelectionCalls == 0,"O toggles only nonrepeat with no camera effect");
            });
    }
    for (const auto& winner : {std::string("CenterBase"),std::string("ShowCoordinates"),std::string("Follow")}) {
        Scenario s; s.bindings[winner] = 0x7fe;
        if (winner == "CenterBase") { s.bindings["ShowCoordinates"] = 0x7fe; s.bindings["Follow"] = 0x7fe; }
        if (winner == "ShowCoordinates") s.bindings["Follow"] = 0x7fe;
        runCase(checks,"HOF-priority/" + winner,s,{0x7fe,false});
    }
}
void additionalRebindings(Checks& checks) {
    for (int key : {'L','O','F'}) for (int modifier = 0; modifier < 8; ++modifier)
        for (int repeated = 0; repeated < 2; ++repeated) {
            Scenario s;
            const std::string id = key == 'L' ? "ToggleSell" : key == 'O' ? "ShowCoordinates" : "Follow";
            const int code = key | (modifier << 8);
            s.bindings[id] = code;
            runCase(checks,"additional-rebind/" + std::to_string(key) + '/' + std::to_string(modifier)
                + '/' + std::to_string(repeated),s,{code,repeated != 0});
        }
}
void sequences(Checks& checks) {
    const std::vector<std::vector<RuntimeReferencePresentationInput>> inputs{
        {{'O',false},{'O',false},{'O',true}},
        {{'F',false},{'F',true},{'F',false},{'F',true}},
        {{'K',false},{'K',true},{'K',false},{'L',false},{'L',false}},
        {{'Q',false},{'Q',true},{'W',false},{'E',false},{'R',true},{'H',true}},
        {{'F',false},{'Q',false},{'F',true},{'L',false},{'H',false}}};
    for (std::size_t sequence = 0; sequence < inputs.size(); ++sequence) {
        Scenario s; Fixture oldState(s), actual(s); Host host(actual);
        std::size_t countedBindings = 0, countedRows = 0;
        for (std::size_t step = 0; step < inputs[sequence].size(); ++step) {
            const auto& input = inputs[sequence][step];
            const bool oldResult = frozenOldReferenceKey(oldState,s.client,input.code,input.repeated);
            const bool result = handleRuntimeReferencePresentationKey(input,actual.playerLocalCommands,host);
            comparePair(checks,"sequence/" + std::to_string(sequence) + '/' + std::to_string(step),
                oldState,actual,oldResult,result,countedBindings,countedRows);
            countedBindings = 0;
            for (const auto& read : actual.bindingReads) countedBindings += read.second;
            countedRows = actual.trace.events.size();
        }
        if (sequence == 0) checks.expect(actual.playerLocalCommands.showCoordinates == s.local.showCoordinates,
            "two O toggles restore shared value and repeat retains it");
    }
}
} // namespace

int main() {
    Checks checks;
    checks.expect(static_cast<int>(Kind::Building) == 0 && static_cast<int>(Kind::Defense) == 1
        && static_cast<int>(Kind::Infantry) == 2 && static_cast<int>(Kind::Unit) == 3
        && static_cast<int>(Tool::None) == 0 && static_cast<int>(Tool::Repair) == 1
        && static_cast<int>(Tool::Sell) == 2,"moved enum original integer values");
    for (const auto kind : {Kind::Building,Kind::Defense,Kind::Infantry,Kind::Unit})
        checks.expect(runtimeBuildKindUsesSingleStructureSlot(kind) == (kind == Kind::Building || kind == Kind::Defense),
            "original single-slot formula on the original enum domain");
    tabMatrix(checks);
    catalogCases(checks);
    previewCases(checks);
    toolCases(checks);
    cameraAndCoordinateCases(checks);
    additionalRebindings(checks);
    sequences(checks);
    std::cout << "Reference presentation contract: checks=" << checks.assertions
        << " differentialCases=" << checks.differentialCases << " bindingQueries=" << checks.bindingQueries
        << " orderedTraceRows=" << checks.orderedTraceRows << " ownedChecks=" << checks.ownedChecks
        << " failures=" << checks.failures
        << " sameProductionCpp=1 frozenOldPrefix=1 fullLocalState=1 completeOrderFields=11"
        << " nativeIntegration=not-covered authority=opaque-sentinels cameraOwner=callback-only"
        << " wholeReferenceTail=not-covered geometryOverflow=not-covered\n";
    return checks.failures == 0 ? 0 : 1;
}
