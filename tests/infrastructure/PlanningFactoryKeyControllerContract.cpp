#include "ui/input/RuntimePlanningFactoryKeyController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error Standard planning/factory header must not include Windows business headers.
#endif
#include "ui/input/RuntimePlayerCommands.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(sizeof(decltype(RuntimePlanningFactoryKeyInput::code)) == 4);
static_assert(std::is_signed_v<decltype(RuntimePlanningFactoryKeyInput::code)>);
static_assert(RuntimePlanningFactoryKeyInput{}.code == 0);
static_assert(!RuntimePlanningFactoryKeyInput{}.repeated);
static_assert(std::is_same_v<decltype(RuntimePlayerFactoryRequest::objectId), std::int32_t>);
static_assert(RuntimePlayerFactoryRequest{}.objectId == 0);
static_assert(std::has_virtual_destructor_v<RuntimePlanningFactoryKeyHost>);
static_assert(std::has_virtual_destructor_v<RuntimePlayerFactoryHost>);
static_assert(!noexcept(dispatchRuntimePlayerFactory({}, std::declval<RuntimePlayerFactoryHost&>())));

namespace {
using Cell = RuntimeMultiplayerGameplayCell;
using Request = RuntimePlayerFactoryRequest;
using Input = RuntimePlanningFactoryKeyInput;
using Operation = RuntimePlayerWaypointOperation;
struct Fixture;
struct Router {
    Fixture* state;
    bool setPrimaryFactory(int id, std::string& error);
    bool setRallyPoint(int id, Cell cell, std::string& error);
};
struct StatusSink {
    Fixture* state;
    StatusSink& operator=(const std::string& text);
};
struct RepaintSink {
    Fixture* state;
    RepaintSink& operator=(bool value);
};
struct Effects {
    int generation = 0;
    std::string status = "original-status";
    bool repaint = false;
    int sounds = 0;
};
struct Fixture {
    RuntimePlayerLocalCommands playerLocalCommands;
    std::vector<int> selectedObjectIds{17};
    struct Economy { std::string owner = "Player"; } economy;
    bool routed = false, accepted = true, production = false;
    int hook = 0, hookDepth = 0, callbacks = 0;
    bool hookTriggered = false;
    Request* externalRequest = nullptr;
    std::map<std::string, int> bindings;
    std::map<std::string, std::deque<int>> scriptedBindings;
    std::shared_ptr<Effects> effects = std::make_shared<Effects>();
    std::vector<std::shared_ptr<Effects>> retired;
    std::vector<Request> factories;
    std::vector<RuntimeMultiplayerGameplayWaypointCommand> waypoints;
    std::vector<std::string> trace;
    std::size_t bindingReads = 0, routeReads = 0, refreshCalls = 0;
    std::size_t submitCalls = 0, applyCalls = 0, statusCalls = 0, soundCalls = 0, repaintCalls = 0;
    std::uint64_t authoritySentinel = 0x1020304050607080ULL;
    Router multiplayerIntentRouter{this};
    StatusSink interactionStatus{this};
    RepaintSink repaintRequested{this};
    Fixture() = default;
    Fixture(const Fixture& other) : Fixture() { *this = other; }
    Fixture& operator=(const Fixture& other) {
        if (this == &other) return *this;
        playerLocalCommands = other.playerLocalCommands; selectedObjectIds = other.selectedObjectIds;
        economy = other.economy; routed = other.routed; accepted = other.accepted;
        production = other.production; hook = other.hook; hookDepth = other.hookDepth;
        callbacks = other.callbacks; hookTriggered = other.hookTriggered; externalRequest = nullptr;
        bindings = other.bindings; scriptedBindings = other.scriptedBindings;
        effects = std::make_shared<Effects>(*other.effects);
        retired.clear();
        for (const auto& old : other.retired) retired.push_back(std::make_shared<Effects>(*old));
        factories = other.factories; waypoints = other.waypoints; trace = other.trace;
        bindingReads = other.bindingReads; routeReads = other.routeReads; refreshCalls = other.refreshCalls;
        submitCalls = other.submitCalls; applyCalls = other.applyCalls; statusCalls = other.statusCalls;
        soundCalls = other.soundCalls; repaintCalls = other.repaintCalls;
        authoritySentinel = other.authoritySentinel;
        return *this;
    }
    void react(const std::string& event);
    int binding(const std::string& action, int fallback) {
        ++bindingReads;
        int value = bindings.contains(action) ? bindings.at(action) : fallback;
        auto found = scriptedBindings.find(action);
        if (found != scriptedBindings.end() && !found->second.empty()) {
            value = found->second.front(); found->second.pop_front();
        }
        trace.push_back("binding:" + action + ":" + std::to_string(fallback) + ":" + std::to_string(value));
        react("binding:" + action);
        return value;
    }
    bool route() {
        ++routeReads; const bool result = routed;
        trace.push_back("route:" + std::to_string(result)); react("route"); return result;
    }
    void refresh() {
        ++refreshCalls; trace.push_back("refresh"); react("refresh");
    }
    bool factory(bool submit, int id, std::optional<Cell> cell, std::string* error) {
        if (submit) ++submitCalls; else ++applyCalls;
        factories.push_back({id, cell});
        const bool result = accepted;
        trace.push_back(std::string(submit ? "submit:" : "apply:")
            + (cell ? "rally:" : "primary:") + std::to_string(id)
            + (cell ? ":" + std::to_string(cell->x) + "," + std::to_string(cell->y) : "")
            + ":owner=" + economy.owner + ":accepted=" + std::to_string(result)
            + ":error-in=" + (error ? *error : "<absent>"));
        if (error) *error = result ? "success-error-is-not-ui" : "controlled-submit-error";
        react(cell ? "rally" : "primary"); return result;
    }
    void status(const std::string& text) {
        ++statusCalls; trace.push_back("status:" + text);
        effects->status = text; react("status");
    }
    void sound() {
        ++soundCalls; trace.push_back("sound:EVA_PrimaryBuildingSelected");
        ++effects->sounds; react("sound");
    }
    void repaint(bool value) {
        ++repaintCalls; trace.push_back("repaint:" + std::to_string(value));
        effects->repaint = value; react("repaint");
    }
    void waypoint(Operation operation, std::uint64_t id) {
        waypoints.push_back({operation, id});
        trace.push_back("waypoint:" + std::to_string(static_cast<int>(operation)) + ":" + std::to_string(id));
        react("waypoint");
        // The existing Waypoint UI owner is outside this extraction. Its controlled
        // accepted/rejected effects let the key contract observe correct borrowing.
        if (accepted) {
            if (operation == Operation::Begin) playerLocalCommands.planning = true;
            if (operation == Operation::Commit) playerLocalCommands.planning = false;
            if (operation != Operation::CloseLoop) playerLocalCommands.selectedWaypoint = 0;
        }
    }
};
bool Router::setPrimaryFactory(int id, std::string& error) {
    return state->factory(true, id, std::nullopt, &error);
}
bool Router::setRallyPoint(int id, Cell cell, std::string& error) {
    return state->factory(true, id, cell, &error);
}
StatusSink& StatusSink::operator=(const std::string& text) { state->status(text); return *this; }
RepaintSink& RepaintSink::operator=(bool value) { state->repaint(value); return *this; }

// Old-owner primitives deliberately use neither controller nor its host interfaces.
int runtimeMenuKeyboardBindingCode(Fixture& state, const std::string& action, int fallback) {
    return state.binding(action, fallback);
}
bool runtimeUsesGameplayIntentRouter(Fixture& state) { return state.route(); }
void refreshRuntimeMultiplayerPlayerIntentAuthority(Fixture& state) { state.refresh(); }
bool applyRuntimePlayerPrimaryFactory(Fixture& state, const std::string& owner, int id) {
    if (owner != state.economy.owner) throw std::runtime_error("owner read changed");
    return state.factory(false, id, std::nullopt, nullptr);
}
bool applyRuntimePlayerRallyPoint(Fixture& state, const std::string& owner, int id, Cell cell) {
    if (owner != state.economy.owner) throw std::runtime_error("owner read changed");
    return state.factory(false, id, cell, nullptr);
}
void emitRuntimeSound(Fixture& state, const std::string& sound) {
    if (sound != "EVA_PrimaryBuildingSelected") throw std::runtime_error("unexpected sound");
    state.sound();
}
void dispatchRuntimePlayerWaypointFromUi(Fixture& state, Operation operation, std::uint64_t id = 0) {
    state.waypoint(operation, id);
}

// Frozen original shared body; only RuntimeWindowState's type is replaced.
// Original router member calls, fallback owner read, optional presence and UI
// assignment syntax are retained, independently of the production controller.
bool dispatchRuntimePlayerFactoryFromUi(Fixture& state,
    int id, std::optional<RuntimeMultiplayerGameplayCell> cell
) {
    bool accepted = false;
    std::string error;
    if (runtimeUsesGameplayIntentRouter(state)) {
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        accepted = cell ? state.multiplayerIntentRouter.setRallyPoint(id, *cell, error)
            : state.multiplayerIntentRouter.setPrimaryFactory(id, error);
    } else accepted = cell ? applyRuntimePlayerRallyPoint(state, state.economy.owner, id, *cell)
        : applyRuntimePlayerPrimaryFactory(state, state.economy.owner, id);
    state.interactionStatus = accepted ? (cell ? "factory rally point set" : "primary factory selected")
        : "select an owned production factory";
    if (accepted && !cell) emitRuntimeSound(state, "EVA_PrimaryBuildingSelected");
    state.repaintRequested = true;
    return accepted;
}
bool frozenOldKeys(Fixture& state, int code, bool repeated) {
    const auto matches = [&](const std::string& action, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, action, fallback);
    };
    auto& local = state.playerLocalCommands;
    if (matches("PlanningMode", 'Z')) {
        if (!repeated) dispatchRuntimePlayerWaypointFromUi(state,
            local.planning ? RuntimePlayerWaypointOperation::Commit : RuntimePlayerWaypointOperation::Begin);
        return true;
    }
    if (matches("SetPrimaryFactory", 'U' | runtimePlayerControlKey)) {
        if (!repeated && state.selectedObjectIds.size() == 1)
            dispatchRuntimePlayerFactoryFromUi(state, state.selectedObjectIds.front(), std::nullopt);
        return true;
    }
    if (matches("SetRallyPoint", 'J')) {
        local.rallyTargeting = state.selectedObjectIds.size() == 1;
        local.followTargeting = false;
        state.interactionStatus = "choose a factory rally point";
        return true;
    }
    return false;
}
class FactoryHost final : public RuntimePlayerFactoryHost {
public:
    explicit FactoryHost(Fixture& value) : state(value) {}
    bool usesIntentRouter() override { return state.route(); }
    void refreshAuthority() override { state.refresh(); }
    bool submitPrimary(std::int32_t id, std::string& error) override {
        return state.multiplayerIntentRouter.setPrimaryFactory(id, error);
    }
    bool submitRally(std::int32_t id, Cell cell, std::string& error) override {
        return state.multiplayerIntentRouter.setRallyPoint(id, cell, error);
    }
    bool applyPrimary(std::int32_t id) override {
        return applyRuntimePlayerPrimaryFactory(state, state.economy.owner, id);
    }
    bool applyRally(std::int32_t id, Cell cell) override {
        return applyRuntimePlayerRallyPoint(state, state.economy.owner, id, cell);
    }
    void setStatus(const std::string& status) override { state.status(status); }
    void primarySelectedSound() override { state.sound(); }
    void requestRepaint() override { state.repaint(true); }
private:
    Fixture& state;
};
class KeyHost final : public RuntimePlanningFactoryKeyHost {
public:
    explicit KeyHost(Fixture& value) : state(value) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        return state.binding(action, fallback);
    }
    void dispatchWaypoint(Operation operation, std::uint64_t id) override { state.waypoint(operation, id); }
    void dispatchFactory(Request request) override {
        FactoryHost host(state); dispatchRuntimePlayerFactory(request, host);
    }
    void setStatus(const std::string& status) override { state.status(status); }
private:
    Fixture& state;
};
void Fixture::react(const std::string& event) {
    if (hookTriggered || hookDepth != 0) return;
    const bool trigger = (hook == 1 && event == "binding:PlanningMode")
        || (hook == 2 && event == "binding:SetPrimaryFactory")
        || (hook == 3 && event == "binding:SetRallyPoint")
        || (hook == 4 && event == "route") || (hook == 5 && event == "refresh")
        || (hook == 6 && event == "primary") || (hook == 7 && event == "status")
        || (hook == 8 && event == "rally") || (hook == 9 && event == "repaint")
        || (hook == 10 && event == "binding:PlanningMode") || (hook == 11 && event == "waypoint");
    if (!trigger) return;
    hookTriggered = true; ++callbacks; ++hookDepth;
    trace.push_back("callback:" + std::to_string(hook) + ":begin");
    if (hook == 1) playerLocalCommands.planning = !playerLocalCommands.planning;
    if (hook == 2) selectedObjectIds = {std::numeric_limits<int>::min()};
    if (hook == 3) { selectedObjectIds = {31, 31}; playerLocalCommands.followTargeting = true; }
    if (hook == 4) {
        routed = !routed;
        if (externalRequest) *externalRequest = {999, Cell{998, 997}};
    }
    if (hook == 5 || hook == 7) {
        retired.push_back(effects);
        effects = std::make_shared<Effects>(); effects->generation = 23;
        economy.owner = "owner-after-root-swap";
        if (externalRequest) *externalRequest = {-999, std::nullopt};
    }
    if (hook == 6) {
        retired.push_back(effects);
        effects = std::make_shared<Effects>(); effects->generation = 42;
        trace.push_back("nested:factory:begin");
        if (production) { FactoryHost host(*this); dispatchRuntimePlayerFactory({-7, Cell{0, 0}}, host); }
        else dispatchRuntimePlayerFactoryFromUi(*this, -7, Cell{0, 0});
        trace.push_back("nested:factory:end");
    }
    if (hook == 8) { --hookDepth; throw std::runtime_error("controlled callback exception"); }
    if (hook == 9) {
        trace.push_back("nested:key:begin");
        if (production) {
            KeyHost host(*this);
            handleRuntimePlanningFactoryKey({'J', true}, playerLocalCommands, selectedObjectIds, host);
        } else frozenOldKeys(*this, 'J', true);
        trace.push_back("nested:key:end");
    }
    if (hook == 10) bindings["SetPrimaryFactory"] = 0x71234567;
    if (hook == 11) { playerLocalCommands.selectedWaypoint = 999; accepted = false; }
    trace.push_back("callback:" + std::to_string(hook) + ":end"); --hookDepth;
}

std::string requestText(const Request& request) {
    return std::to_string(request.objectId) + (request.cell
        ? ":" + std::to_string(request.cell->x) + "," + std::to_string(request.cell->y) : ":none");
}
std::string snapshot(const Fixture& state) {
    const auto& local = state.playerLocalCommands;
    std::ostringstream out;
    out << local.planning << ':' << local.selectedWaypoint << ':' << local.followTargeting
        << local.rallyTargeting << local.cameraFollow << local.showCoordinates << ':'
        << local.lastSelectionKey << ':' << local.lastSelectionAtMs << ':'
        << local.veterancyRank << ':' << local.healthRank << ';';
    for (const auto& team : local.teams) { for (int id : team) out << id << ','; out << ';'; }
    for (const auto& view : local.views)
        out << (view ? std::to_string(view->x) + "," + std::to_string(view->y) : "none") << ';';
    for (int id : state.selectedObjectIds) out << id << ',';
    out << ';' << state.economy.owner << ';' << state.routed << state.accepted << ':'
        << state.hook << ':' << state.hookDepth << ':' << state.hookTriggered << ':' << state.callbacks << ';';
    const auto writeEffects = [&](const Effects& effects) {
        out << effects.generation << ':' << effects.status << ':' << effects.repaint << ':' << effects.sounds << ';';
    };
    writeEffects(*state.effects);
    for (const auto& retired : state.retired) writeEffects(*retired);
    for (const auto& request : state.factories) out << requestText(request) << ';';
    for (const auto& command : state.waypoints)
        out << static_cast<int>(command.operation) << ':' << command.waypointId << ';';
    for (const auto& binding : state.bindings) out << binding.first << ':' << binding.second << ';';
    for (const auto& script : state.scriptedBindings) {
        out << script.first << '=';
        for (int code : script.second) out << code << ',';
        out << ';';
    }
    out << state.bindingReads << ':' << state.routeReads << ':' << state.refreshCalls << ':'
        << state.submitCalls << ':' << state.applyCalls << ':' << state.statusCalls << ':'
        << state.soundCalls << ':' << state.repaintCalls << ':' << state.authoritySentinel;
    return out.str();
}
Fixture seed() {
    Fixture state;
    auto& local = state.playerLocalCommands;
    local.teams[0] = {7, 2}; local.teams[9] = {99};
    local.views[0] = Cell{-3, 9}; local.views[3] = Cell{21, -8};
    local.selectedWaypoint = std::numeric_limits<std::uint64_t>::max();
    local.followTargeting = local.rallyTargeting = local.cameraFollow = local.showCoordinates = true;
    local.lastSelectionKey = -123; local.lastSelectionAtMs = std::numeric_limits<std::uint64_t>::max();
    local.veterancyRank = 2; local.healthRank = 1;
    return state;
}
struct Totals {
    std::size_t checks = 0, failures = 0, cases = 0, keyCases = 0, factoryCases = 0;
    std::size_t bindingQueries = 0, orderedTraceRows = 0, ownedChecks = 0, callbackCases = 0;
} totals;
void expect(bool condition, const std::string& label) {
    ++totals.checks;
    if (!condition) { ++totals.failures; std::cerr << "planning/factory contract: " << label << '\n'; }
}
void compare(const Fixture& oldState, const Fixture& actual, const std::string& label) {
    expect(snapshot(oldState) == snapshot(actual), label + " full state");
    expect(oldState.trace == actual.trace, label + " ordered query/dispatch/effects trace");
    expect(actual.authoritySentinel == 0x1020304050607080ULL, label + " unrelated authority sentinel");
    totals.bindingQueries += actual.bindingReads;
    totals.orderedTraceRows += actual.trace.size();
    if (actual.callbacks) ++totals.callbackCases;
}
Fixture keyCase(const Fixture& initial, Input input, const std::string& label) {
    Fixture oldState(initial), actual(initial); actual.production = true;
    bool oldResult = false, actualResult = false;
    std::string oldError, actualError;
    try { oldResult = frozenOldKeys(oldState, input.code, input.repeated); }
    catch (const std::exception& error) { oldError = error.what(); }
    try {
        KeyHost host(actual);
        actualResult = handleRuntimePlanningFactoryKey(input, actual.playerLocalCommands, actual.selectedObjectIds, host);
    } catch (const std::exception& error) { actualError = error.what(); }
    expect(oldResult == actualResult && oldError == actualError, label + " handled/exception");
    compare(oldState, actual, label); ++totals.cases; ++totals.keyCases;
    return actual;
}
Fixture factoryCase(const Fixture& initial, Request request, const std::string& label) {
    Fixture oldState(initial), actual(initial); actual.production = true;
    Request oldExternal = request, actualExternal = request;
    oldState.externalRequest = &oldExternal; actual.externalRequest = &actualExternal;
    bool oldResult = false, actualResult = false;
    std::string oldError, actualError;
    try { oldResult = dispatchRuntimePlayerFactoryFromUi(oldState, oldExternal.objectId, oldExternal.cell); }
    catch (const std::exception& error) { oldError = error.what(); }
    try { FactoryHost host(actual); actualResult = dispatchRuntimePlayerFactory(actualExternal, host); }
    catch (const std::exception& error) { actualError = error.what(); }
    expect(oldResult == actualResult && oldError == actualError, label + " accepted/exception");
    expect(requestText(oldExternal) == requestText(actualExternal), label + " external request callback");
    compare(oldState, actual, label); ++totals.cases; ++totals.factoryCases;
    actual.externalRequest = nullptr;
    return actual;
}
void fixedExpectations() {
    auto state = seed();
    auto result = keyCase(state, {'Z', false}, "Begin with nonzero selected waypoint");
    expect(result.waypoints.size() == 1 && result.waypoints.front().operation == Operation::Begin
        && result.waypoints.front().waypointId == 0, "Z authors Begin id zero");
    expect(result.playerLocalCommands.planning && result.playerLocalCommands.selectedWaypoint == 0,
        "existing accepted waypoint owner effects observed");
    state.playerLocalCommands.planning = true;
    result = keyCase(state, {'Z', false}, "Commit");
    expect(result.waypoints.size() == 1 && result.waypoints.front().operation == Operation::Commit
        && !result.playerLocalCommands.planning, "Z authors Commit");
    result = keyCase(state, {'Z', true}, "repeat Z");
    expect(result.waypoints.empty() && result.bindingReads == 1 && result.statusCalls == 0
        && result.repaintCalls == 0, "repeat Z consumes with no submit/effects");
    state.accepted = false;
    result = keyCase(state, {'Z', false}, "rejected Commit");
    expect(result.playerLocalCommands.planning && result.playerLocalCommands.selectedWaypoint
        == std::numeric_limits<std::uint64_t>::max(), "rejected old waypoint sink keeps local fields");
    state = seed();
    result = keyCase(state, {'U' | runtimePlayerControlKey, false}, "primary");
    expect(result.factories.size() == 1 && result.factories.front().objectId == 17
        && !result.factories.front().cell && result.routeReads == 1 && result.applyCalls == 1
        && result.submitCalls == 0 && result.refreshCalls == 0, "primary raw singleton/nullopt and offline route");
    expect(result.effects->status == "primary factory selected" && result.effects->sounds == 1
        && result.effects->repaint, "accepted primary status/sound/repaint");
    expect(result.trace.size() == 7 && result.trace[4] == "status:primary factory selected"
        && result.trace[5] == "sound:EVA_PrimaryBuildingSelected" && result.trace[6] == "repaint:1",
        "primary effect order");
    state.selectedObjectIds = {17, 17};
    result = keyCase(state, {'U' | runtimePlayerControlKey, false}, "duplicate selection");
    expect(result.factories.empty() && result.routeReads == 0 && result.statusCalls == 0,
        "duplicate ids remain size two, not normalized");
    state.selectedObjectIds = {17};
    result = keyCase(state, {'U' | runtimePlayerControlKey, true}, "repeat primary");
    expect(result.factories.empty() && result.routeReads == 0, "repeat primary consumes without dispatch");
    result = keyCase(state, {'J', true}, "repeat rally targeting");
    expect(result.playerLocalCommands.rallyTargeting && !result.playerLocalCommands.followTargeting
        && result.effects->status == "choose a factory rally point" && result.repaintCalls == 0
        && result.soundCalls == 0 && result.routeReads == 0, "J repeat local writes only, outer repaint not invented");
    state.selectedObjectIds.clear();
    result = keyCase(state, {'J', false}, "empty rally selection");
    expect(!result.playerLocalCommands.rallyTargeting && !result.playerLocalCommands.followTargeting
        && result.effects->status == "choose a factory rally point", "J always writes original status");
    result = keyCase(state, {std::numeric_limits<int>::min(), true}, "unknown full-width key");
    expect(result.bindingReads == 3 && result.trace.size() == 3 && result.statusCalls == 0,
        "unknown queries all three in lazy order");
    state = seed(); state.routed = true; state.accepted = false;
    result = factoryCase(state, {9, std::nullopt}, "router rejects");
    expect(result.routeReads == 1 && result.refreshCalls == 1 && result.submitCalls == 1
        && result.applyCalls == 0 && result.effects->status == "select an owned production factory"
        && result.soundCalls == 0 && result.repaintCalls == 1, "router rejection retains generic status/repaint");
    state.accepted = true;
    result = factoryCase(state, {9, Cell{0, 0}}, "engaged zero cell");
    expect(result.factories.front().cell == std::optional<Cell>(Cell{0, 0})
        && result.effects->status == "factory rally point set" && result.soundCalls == 0,
        "engaged zero cell is rally, not primary");
    state.hook = 4;
    result = factoryCase(state, {29, Cell{-3, 7}}, "owned request and one route query");
    expect(result.submitCalls == 1 && result.applyCalls == 0 && result.routeReads == 1
        && requestText(result.factories.front()) == "29:-3,7", "route callback cannot mutate owned request");
    ++totals.ownedChecks;
    state.hook = 5;
    result = factoryCase(state, {31, Cell{8, 2}}, "authority refresh swaps presentation child");
    expect(requestText(result.factories.front()) == "31:8,2" && result.effects->generation == 23
        && result.effects->status == "factory rally point set" && result.effects->repaint,
        "post-refresh current root and original owned request");
    expect(result.retired.size() == 1 && result.retired.front()->status == "original-status"
        && !result.retired.front()->repaint, "retired child receives no later UI writes");
    ++totals.ownedChecks;
    state.hook = 6;
    result = factoryCase(state, {37, std::nullopt}, "synchronous nested factory");
    expect(result.factories.size() == 2 && requestText(result.factories[0]) == "37:none"
        && requestText(result.factories[1]) == "-7:0,0" && result.effects->generation == 42
        && result.effects->status == "primary factory selected" && result.effects->sounds == 1,
        "nested rally completes before outer primary effects on current root");
    ++totals.ownedChecks;
    state.hook = 8;
    result = factoryCase(state, {41, Cell{1, 2}}, "callback exception");
    expect(result.statusCalls == 0 && result.soundCalls == 0 && result.repaintCalls == 0,
        "callback exception propagates before trailing effects");
    state = seed(); state.hook = 2; state.selectedObjectIds.clear();
    result = keyCase(state, {'U' | runtimePlayerControlKey, false}, "binding changes selection");
    expect(result.factories.size() == 1 && result.factories[0].objectId == std::numeric_limits<int>::min(),
        "selection is read after the fresh binding callback");
    state = seed(); state.hook = 10;
    result = keyCase(state, {0x71234567, false}, "binding changes later binding");
    expect(result.bindingReads == 2 && result.factories.size() == 1,
        "later binding stays lazy and current");
}
void matrices() {
    const std::vector<std::vector<int>> selections{{}, {17}, {17, 17}, {-1, 23}};
    const int baseCodes[]{'Z', 'U' | runtimePlayerControlKey, 'J', 'X'};
    for (int modifier = 0; modifier < 8; ++modifier)
        for (int rebound = 0; rebound < 2; ++rebound)
            for (int key = 0; key < 4; ++key)
                for (int repeat = 0; repeat < 2; ++repeat)
                    for (std::size_t selection = 0; selection < selections.size(); ++selection)
                        for (int planning = 0; planning < 2; ++planning) {
                            auto state = seed();
                            state.selectedObjectIds = selections[selection];
                            state.playerLocalCommands.planning = planning != 0;
                            state.routed = (selection & 1U) != 0; state.accepted = (modifier & 1) == 0;
                            const int bits = modifier << 8;
                            if (rebound) {
                                state.bindings["PlanningMode"] = 'Z' | bits;
                                state.bindings["SetPrimaryFactory"] = 'U' | runtimePlayerControlKey | bits;
                                state.bindings["SetRallyPoint"] = 'J' | bits;
                            }
                            keyCase(state, {baseCodes[key] | bits, repeat != 0}, "retail/rebound modifiers");
                        }
    const int codes[]{std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), -1, -257,
        0, 1, 0xff, 0x100, 0x7ff, 0x800, 0x10000, 0x71234567};
    const char* actions[]{"PlanningMode", "SetPrimaryFactory", "SetRallyPoint"};
    for (int code : codes) for (const char* action : actions)
        for (int repeat = 0; repeat < 2; ++repeat)
            for (const auto& selection : selections) {
                auto state = seed(); state.bindings[action] = code; state.selectedObjectIds = selection;
                keyCase(state, {code, repeat != 0}, "signed full-width rebound key");
            }
    for (int mask = 1; mask < 8; ++mask) for (int modifier = 0; modifier < 8; ++modifier)
        for (int repeat = 0; repeat < 2; ++repeat) {
            auto state = seed(); const int code = 0x62000000 | (modifier << 8) | 0x31;
            for (int action = 0; action < 3; ++action)
                if ((mask & (1 << action)) != 0) state.bindings[actions[action]] = code;
            auto result = keyCase(state, {code, repeat != 0}, "binding collision priority");
            const std::size_t expectedQueries = (mask & 1) != 0 ? 1U : (mask & 2) != 0 ? 2U : 3U;
            expect(result.bindingReads == expectedQueries, "first matching Z/U/J wins without later queries");
        }
    const std::vector<std::optional<Cell>> cells{std::nullopt, Cell{0, 0}, Cell{-1, 1},
        Cell{999, 999}, Cell{1000, -1000}, Cell{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}};
    for (int route = 0; route < 2; ++route) for (int accept = 0; accept < 2; ++accept)
        for (int id : codes) for (const auto& cell : cells) {
            auto state = seed(); state.routed = route != 0; state.accepted = accept != 0;
            factoryCase(state, {id, cell}, "factory value forwarding");
        }
    for (int hook = 1; hook <= 11; ++hook) for (int route = 0; route < 2; ++route)
        for (int accept = 0; accept < 2; ++accept) {
            auto state = seed(); state.hook = hook; state.routed = route != 0; state.accepted = accept != 0;
            factoryCase(state, {-53, std::nullopt}, "factory callback primary");
            factoryCase(state, {-59, Cell{7, -4}}, "factory callback rally");
            for (int key : baseCodes) keyCase(state, {key, false}, "key callback");
        }
    // Multiple keys consume independent scripted queries from the same root.
    Fixture oldState(seed()), actual(seed()); actual.production = true;
    for (auto* state : {&oldState, &actual}) {
        state->scriptedBindings["PlanningMode"] = {'X', 'Z', 'X', 'X'};
        state->scriptedBindings["SetPrimaryFactory"] = {'X', 'U' | runtimePlayerControlKey, 'X'};
        state->scriptedBindings["SetRallyPoint"] = {'J', 'J'};
    }
    const Input sequence[]{{'Z', false}, {'Z', true}, {'U' | runtimePlayerControlKey, false}, {'J', true}};
    for (const auto& input : sequence) {
        const bool oldResult = frozenOldKeys(oldState, input.code, input.repeated);
        KeyHost host(actual);
        const bool result = handleRuntimePlanningFactoryKey(input, actual.playerLocalCommands,
            actual.selectedObjectIds, host);
        expect(oldResult == result, "scripted sequence handled");
        compare(oldState, actual, "scripted sequence root state");
        ++totals.cases; ++totals.keyCases;
    }
}
} // namespace

int main() {
    expect(!RuntimePlayerFactoryRequest{}.cell, "default request optional is disengaged");
    fixedExpectations();
    matrices();
    std::cout << "planning-factory-key contract checks=" << totals.checks
        << " differentialCases=" << totals.cases << " keyCases=" << totals.keyCases
        << " factoryCases=" << totals.factoryCases << " bindingQueries=" << totals.bindingQueries
        << " orderedTraceRows=" << totals.orderedTraceRows << " ownedChecks=" << totals.ownedChecks
        << " callbackCases=" << totals.callbackCases << " modifierCombinations=8"
        << " oldAdapterTrace=all-fields-equal windowsHeader=absent"
        << " shellIntegration=not-covered realValidation=not-covered nativeKeyAdapter=not-run"
        << " authorityTick=not-covered rootSwapModel=only failures=" << totals.failures << '\n';
    return totals.failures ? 1 : 0;
}
