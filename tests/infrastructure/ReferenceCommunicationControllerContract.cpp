#include "ui/input/RuntimeReferenceCommunicationController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error Standard communication controller header must not include Windows business headers.
#endif
#include "ui/input/RuntimePlayerCommands.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(sizeof(decltype(RuntimeReferenceCommunicationInput::code)) == 4);
static_assert(std::is_signed_v<decltype(RuntimeReferenceCommunicationInput::code)>);
static_assert(std::is_same_v<decltype(RuntimeReferenceCommunicationInput::repeated), bool>);
static_assert(RuntimeReferenceCommunicationInput{}.code == 0);
static_assert(!RuntimeReferenceCommunicationInput{}.repeated);
static_assert(std::has_virtual_destructor_v<RuntimeCommunicationAllianceHost>);

namespace {
using Command = RuntimeMultiplayerGameplayPlayerCommunicationCommand;
using Cell = RuntimeMultiplayerGameplayCell;
using Row = RuntimeCommunicationDiplomacyRow;
constexpr int VK_TAB = 9, VK_RETURN = 13, VK_ESCAPE = 27, VK_SPACE = 32;
constexpr int VK_UP = 38, VK_DOWN = 40, VK_HOME = 36, VK_DELETE = 46, VK_DECIMAL = 110, VK_F5 = 116;

std::string bytes(const std::string& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : value) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string wide(const std::u16string& value) {
    std::ostringstream out;
    for (char16_t c : value) out << static_cast<std::uint32_t>(c) << ',';
    return out.str();
}
std::string cellText(const Cell& cell) {
    return std::to_string(cell.x) + "," + std::to_string(cell.y);
}
std::string commandText(const Command& command) {
    return std::to_string(static_cast<int>(command.kind)) + "/" + bytes(command.text)
        + "/" + cellText(command.cell) + "/" + std::to_string(command.value);
}
struct Fixture {
    RuntimePlayerLocalCommands playerLocalCommands;
    RuntimePlayerCommunicationState playerCommunication;
    std::vector<int> selectedObjectIds{1, 2};
    std::map<int, std::string> objects{{1, "Enemy"}, {2, "Player"}};
    std::string owner = "player-alias";
    std::string viewer = "Player";
    std::optional<bool> offer;
    bool allied = false, accepted = true, enabled = true;
    std::map<std::string, std::int32_t> bindings;
    std::map<std::string, std::deque<std::int32_t>> bindingScript;
    std::deque<std::string> canonicalScript;
    std::optional<RuntimePlayerBeacon> selected;
    std::map<std::pair<int, int>, bool> visible;
    std::vector<Row> roster{{"Player", true, false}, {"Enemy", false, false},
        {"Third", false, false}};
    RuntimeSidebarTool sidebarTool = RuntimeSidebarTool::Sell;
    bool dragMoved = true, dragActive = true, rightMoved = true, rightActive = true;
    bool superWeapon = true, placement = true, repaint = false;
    std::string status = "original-status";
    std::vector<Command> commands;
    std::vector<RuntimeMultiplayerGameplayWaypointCommand> waypoints;
    std::size_t bindingReads = 0, canonicalReads = 0, currentOwnerReads = 0;
    std::size_t objectReads = 0, selectedReads = 0, visibleReads = 0, viewerReads = 0;
    std::size_t rowReads = 0, eligibilityReads = 0, alliedReads = 0, offerReads = 0;
    std::size_t openCalls = 0, closeCalls = 0, cancelCalls = 0, dismissCalls = 0;
    std::size_t repaintCalls = 0, acceptedCalls = 0, rejectedCalls = 0;
    std::size_t alliesReads = 0, callbackCalls = 0;
    bool alliesQuerySawOpen = false, alliesQuerySawOldDraft = false;
    // Controlled owner callbacks, not a replacement for production validation/apply.
    int callback = 0, callbackDepth = 0;
    std::uint64_t opaqueAuthoritySentinel = 0x7171717171717171ULL;
};
Fixture seed() {
    Fixture state;
    auto& local = state.playerLocalCommands;
    local.teams[0] = {9, 1}; local.teams[7] = {7};
    local.views[0] = Cell{7, -3}; local.views[3] = Cell{-8, 19};
    local.followTargeting = local.rallyTargeting = local.cameraFollow = local.showCoordinates = true;
    local.lastSelectionKey = -79; local.lastSelectionAtMs = std::numeric_limits<std::uint64_t>::max();
    local.veterancyRank = 2; local.healthRank = 1;
    auto& ui = state.playerCommunication;
    ui.chatDraft = u"old-draft"; ui.chatAllies = true; ui.diplomacyRow = -17;
    ui.diplomacyPressedControl = 5; ui.diplomacyHoveredControl = 8;
    ui.chatRecipientsExplicit = true; ui.chatRecipients = 37; ui.tauntsEnabled = false;
    ui.diplomacyBackgroundSource = "background"; ui.diplomacyControlSources = {"one", "two"};
    ui.selectedBeacon = Cell{4, 9};
    ui.lines.push_back({"stable-owner", "stable-line", true, 71});
    ui.cheerUntilTick.emplace(7, 91);
    ui.nextFeedbackTick.emplace(std::make_pair("Player", RuntimePlayerCommunicationKind::Cheer), 99);
    ui.beacons.push_back({"Player", {4, 9}, 99, "owned-text", 2});
    ui.beacons.push_back({"Foreign", {6, 8}, -1, "foreign", -3});
    state.selected = ui.beacons.front();
    return state;
}
std::string snapshot(const Fixture& state) {
    const auto& local = state.playerLocalCommands;
    const auto& ui = state.playerCommunication;
    std::ostringstream out;
    out << "local=" << local.planning << ',' << local.selectedWaypoint << ','
        << local.followTargeting << local.rallyTargeting << local.cameraFollow << local.showCoordinates
        << ',' << local.lastSelectionKey << ',' << local.lastSelectionAtMs << ','
        << local.veterancyRank << ',' << local.healthRank << ';';
    for (const auto& team : local.teams) { out << '['; for (int id : team) out << id << ','; out << ']'; }
    for (const auto& view : local.views) out << (view ? cellText(*view) : "none") << ';';
    out << "ui=" << ui.chatOpen << ui.chatAllies << ':' << wide(ui.chatDraft) << ':'
        << ui.diplomacyOpen << ',' << ui.diplomacyRow << ',' << ui.diplomacyPressedControl << ','
        << ui.diplomacyHoveredControl << ',' << ui.chatRecipientsExplicit << ','
        << static_cast<int>(ui.chatRecipients) << ',' << ui.tauntsEnabled << ','
        << bytes(ui.diplomacyBackgroundSource) << ':';
    for (const auto& source : ui.diplomacyControlSources) out << bytes(source) << ',';
    out << ui.beaconTargeting << ',' << (ui.selectedBeacon ? cellText(*ui.selectedBeacon) : "none")
        << ',' << ui.beaconEditing << ';';
    for (const auto& beacon : ui.beacons) out << "b" << bytes(beacon.owner) << ':' << cellText(beacon.cell)
        << ':' << beacon.untilTick << ':' << bytes(beacon.text) << ':' << beacon.level << ';';
    for (const auto& line : ui.lines) out << "l" << bytes(line.owner) << ':' << bytes(line.text)
        << ':' << line.allies << ':' << line.untilTick << ';';
    std::vector<std::pair<int, int>> cheer(ui.cheerUntilTick.begin(), ui.cheerUntilTick.end());
    std::sort(cheer.begin(), cheer.end());
    for (const auto& value : cheer) out << "h" << value.first << ':' << value.second << ';';
    for (const auto& value : ui.nextFeedbackTick) out << "n" << bytes(value.first.first) << ':'
        << static_cast<int>(value.first.second) << ':' << value.second << ';';
    out << "outer=" << static_cast<int>(state.sidebarTool) << state.dragMoved << state.dragActive
        << state.rightMoved << state.rightActive << state.superWeapon << state.placement << state.repaint
        << ':' << bytes(state.status) << ';';
    for (int id : state.selectedObjectIds) out << "s" << id << ';';
    for (const auto& object : state.objects) out << "o" << object.first << ':' << bytes(object.second) << ';';
    out << "owners=" << bytes(state.owner) << ':' << bytes(state.viewer) << ':'
        << (state.offer ? (*state.offer ? "true" : "false") : "none")
        << state.allied << state.accepted << state.enabled << ';';
    for (const auto& binding : state.bindings) out << "kb" << bytes(binding.first) << ':' << binding.second << ';';
    for (const auto& script : state.bindingScript) {
        out << "ks" << bytes(script.first) << ':'; for (auto value : script.second) out << value << ','; out << ';';
    }
    for (const auto& value : state.canonicalScript) out << "cs" << bytes(value) << ';';
    if (state.selected) out << "selected=" << bytes(state.selected->owner) << ':' << cellText(state.selected->cell)
        << ':' << state.selected->untilTick << ':' << bytes(state.selected->text) << ':' << state.selected->level << ';';
    else out << "selected=none;";
    for (const auto& visible : state.visible) out << "v" << visible.first.first << ','
        << visible.first.second << ':' << visible.second << ';';
    for (const auto& row : state.roster) out << "r" << bytes(row.owner) << row.self << row.defeated << ';';
    for (const auto& command : state.commands) out << "c" << commandText(command) << ';';
    for (const auto& point : state.waypoints) out << "w" << static_cast<int>(point.operation) << ':' << point.waypointId << ';';
    out << "reads=" << state.bindingReads << ',' << state.canonicalReads << ',' << state.currentOwnerReads
        << ',' << state.objectReads << ',' << state.selectedReads << ',' << state.visibleReads << ','
        << state.viewerReads << ',' << state.rowReads << ',' << state.eligibilityReads << ','
        << state.alliedReads << ',' << state.offerReads << ";effects=" << state.openCalls << ','
        << state.closeCalls << ',' << state.cancelCalls << ',' << state.dismissCalls << ','
        << state.repaintCalls << ',' << state.acceptedCalls << ',' << state.rejectedCalls << ','
        << state.alliesReads << ',' << state.callbackCalls << ',' << state.alliesQuerySawOpen
        << state.alliesQuerySawOldDraft << ':' << state.callback << ':' << state.callbackDepth
        << ':' << state.opaqueAuthoritySentinel << ';';
    return out.str();
}
class TestHost;
void nestedDispatch(TestHost& host);

class TestHost final : public RuntimeReferenceCommunicationHost, public RuntimeDiplomacyNavigationHost {
public:
    Fixture& state;
    bool old;
    std::vector<std::string> trace;
    TestHost(Fixture& current, bool frozenOld) : state(current), old(frozenOld) {}
    void enter(const std::string& event) { trace.push_back(">" + event + "|" + snapshot(state)); }
    void leave(const std::string& event) { trace.push_back("<" + event + "|" + snapshot(state)); }
    std::string currentOwner() override {
        enter("currentOwner"); ++state.currentOwnerReads; auto value = state.owner;
        leave("currentOwner=" + bytes(value)); return value;
    }
    std::string canonicalOwner(const std::string& owner) override {
        enter("canonical=" + bytes(owner)); ++state.canonicalReads;
        auto value = owner == "player-alias" ? std::string("Player") : owner;
        if (!state.canonicalScript.empty()) { value = state.canonicalScript.front(); state.canonicalScript.pop_front(); }
        if (state.callback == 1) state.objects.clear();
        leave("canonical=" + bytes(value)); return value;
    }
    std::optional<bool> allianceOffer(const std::string& source, const std::string& target) override {
        enter("offer=" + bytes(source) + "/" + bytes(target)); ++state.offerReads;
        const auto result = state.offer; leave("offer"); return result;
    }
    bool ownersAllied(const std::string& source, const std::string& target) override {
        enter("allied=" + bytes(source) + "/" + bytes(target)); ++state.alliedReads;
        const bool result = state.allied; leave("allied"); return result;
    }
    void dispatchCommunication(Command command) override {
        enter("dispatch=" + commandText(command)); state.commands.push_back(command);
        if (state.accepted) ++state.acceptedCalls;
        else { ++state.rejectedCalls; state.playerCommunication.lines.push_back({"system", "rejected", false, 0}); }
        state.repaint = true;
        if (state.callback == 2) { state.objects.clear(); state.roster.clear(); state.playerCommunication.beacons.clear(); }
        if (state.callback == 3) { state.playerCommunication = {}; ++state.callbackCalls; }
        if (state.callback == 4 && state.callbackDepth == 0) {
            ++state.callbackDepth; ++state.callbackCalls; nestedDispatch(*this); --state.callbackDepth;
        }
        leave("dispatch=" + commandText(command));
    }
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        enter("binding=" + action + "/" + std::to_string(fallback)); ++state.bindingReads;
        auto value = state.bindings.contains(action) ? state.bindings.at(action) : fallback;
        auto found = state.bindingScript.find(action);
        if (found != state.bindingScript.end() && !found->second.empty()) {
            value = found->second.front(); found->second.pop_front();
        }
        if (action == "ChatToAllies") {
            ++state.alliesReads;
            if (state.playerCommunication.chatOpen) {
                state.alliesQuerySawOpen = true;
                state.alliesQuerySawOldDraft = state.playerCommunication.chatDraft == u"old-draft";
            }
        }
        leave("binding=" + action + "/" + std::to_string(value)); return value;
    }
    std::optional<std::string> objectOwner(std::int32_t id) override {
        enter("object=" + std::to_string(id)); ++state.objectReads;
        const auto found = state.objects.find(id);
        std::optional<std::string> value;
        if (found != state.objects.end()) value = found->second;
        leave("object"); return value;
    }
    void openDiplomacy() override {
        enter("open"); ++state.openCalls; auto& ui = state.playerCommunication;
        ui.diplomacyOpen = true; ui.diplomacyRow = 1; ui.diplomacyPressedControl = ui.diplomacyHoveredControl = 0;
        ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear();
        state.dragActive = state.dragMoved = false; state.rightActive = state.rightMoved = false; state.repaint = true;
        leave("open");
    }
    std::optional<RuntimePlayerBeacon> selectedBeacon() override {
        enter("selected"); ++state.selectedReads; auto result = state.selected;
        if (!result) state.playerCommunication.selectedBeacon.reset();
        if (state.callback == 5) { state.selected.reset(); state.playerCommunication.beacons.clear(); ++state.callbackCalls; }
        leave("selected"); return result;
    }
    void setChatDraftUtf8(const std::string& text) override {
        enter("draftUtf8=" + bytes(text));
        // A controlled conversion sink: UTF-8 codec correctness belongs to the unchanged owner.
        const auto countText = std::to_string(text.size());
        state.playerCommunication.chatDraft = u"converted-";
        state.playerCommunication.chatDraft.append(countText.begin(), countText.end());
        leave("draftUtf8");
    }
    std::string viewerOwner() override {
        enter("viewer"); ++state.viewerReads; auto result = state.viewer; leave("viewer=" + bytes(result)); return result;
    }
    bool beaconVisible(const RuntimePlayerBeacon& beacon) override {
        enter("visible=" + bytes(beacon.owner) + "/" + cellText(beacon.cell)); ++state.visibleReads;
        const auto found = state.visible.find({beacon.cell.x, beacon.cell.y});
        const bool result = found == state.visible.end() || found->second; leave("visible"); return result;
    }
    void cancelSuperWeapon(const std::string& reason) override {
        enter("cancel=" + reason); ++state.cancelCalls; state.superWeapon = false; leave("cancel");
    }
    void dismissPlacement() override {
        enter("dismiss"); ++state.dismissCalls; state.placement = false; state.dragMoved = false; leave("dismiss");
    }
    void setSidebarTool(RuntimeSidebarTool tool) override {
        enter("tool=" + std::to_string(static_cast<int>(tool))); state.sidebarTool = tool; leave("tool");
    }
    void setSelectionDragMoved(bool moved) override {
        enter("dragMoved"); state.dragMoved = moved; leave("dragMoved");
    }
    void setSelectionDragActive(bool active) override {
        enter("dragActive"); state.dragActive = active; leave("dragActive");
    }
    void beaconLimitStatus() override { enter("limit"); state.status = "beacon-limit"; leave("limit"); }
    void dispatchWaypoint(RuntimePlayerWaypointOperation operation, std::uint64_t id) override {
        enter("waypoint=" + std::to_string(static_cast<int>(operation)) + "/" + std::to_string(id));
        state.waypoints.push_back({operation, id}); leave("waypoint");
    }
    std::vector<Row> rows() override {
        enter("rows"); ++state.rowReads; auto result = state.roster;
        if (state.callback == 6) { state.roster.clear(); ++state.callbackCalls; }
        leave("rows"); return result;
    }
    void closePage() override {
        enter("close"); ++state.closeCalls; auto& ui = state.playerCommunication;
        ui.diplomacyOpen = false; ui.diplomacyPressedControl = ui.diplomacyHoveredControl = 0;
        state.rightActive = state.rightMoved = false; state.repaint = true; leave("close");
    }
    bool allianceEnabled(const Row& row) override {
        enter("enabled=" + bytes(row.owner) + "/" + std::to_string(row.self) + std::to_string(row.defeated));
        ++state.eligibilityReads; const bool result = state.enabled && !row.self && !row.defeated;
        if (state.callback == 7) { state.roster.clear(); ++state.callbackCalls; }
        if (state.callback == 8) { state.playerCommunication.diplomacyRow = 2; ++state.callbackCalls; }
        leave("enabled"); return result;
    }
    void requestRepaint() override { enter("repaint"); ++state.repaintCalls; state.repaint = true; leave("repaint"); }
};
struct OfferLookup {
    using Key = std::pair<std::string, std::string>;
    TestHost& host;
    std::optional<std::pair<Key, bool>> value;
    explicit OfferLookup(TestHost& current) : host(current) {}
    const std::pair<Key, bool>* find(const Key& key) {
        const auto result = host.allianceOffer(key.first, key.second);
        if (!result) { value.reset(); return nullptr; }
        value = std::make_pair(key, *result); return &*value;
    }
    const std::pair<Key, bool>* end() const { return nullptr; }
};
struct OwnedObject { std::string owner; };

// Frozen old algorithms below originate from the live pre-change Hotkeys file.
// Only owner-query/effect spelling is adapted to the same controlled primitive port;
// no new controller entry is called by these three oracle bodies.
void frozenOldToggleAlliance(Fixture& state, const std::string& target, TestHost& host) {
    OfferLookup offers(host);
    const auto runtimeCanonicalHumanHouseOwner = [&](const std::string& value) { return host.canonicalOwner(value); };
    const auto runtimeOwnersAreAllied = [&](Fixture&, const std::string& a, const std::string& b) { return host.ownersAllied(a, b); };
    const auto dispatchRuntimePlayerCommunicationFromUi = [&](Fixture&, Command command) { host.dispatchCommunication(std::move(command)); };
    if (target.empty()) return;
    const auto owner = runtimeCanonicalHumanHouseOwner(host.currentOwner());
    const auto offer = offers.find({owner, target});
    const bool offered = offer == offers.end()
        ? runtimeOwnersAreAllied(state, owner, target) : offer->second;
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind = RuntimePlayerCommunicationKind::Alliance;
    command.text = target; command.value = offered ? 0 : 1;
    dispatchRuntimePlayerCommunicationFromUi(state, command);
}
bool frozenOldReference(Fixture& state, int code, bool repeated, TestHost& host) {
    auto& local = state.playerLocalCommands; auto& ui = state.playerCommunication;
    const auto matches = [&](const std::string& id, int fallback) { return code == host.binding(id, fallback); };
    std::optional<OwnedObject> objectStorage;
    std::optional<RuntimePlayerBeacon> beaconStorage;
    const auto findObjectById = [&](Fixture&, int id) -> const OwnedObject* {
        const auto owner = host.objectOwner(id); if (!owner) return nullptr;
        objectStorage = OwnedObject{*owner}; return &*objectStorage;
    };
    const auto runtimeCanonicalHumanHouseOwner = [&](const std::string& value) { return host.canonicalOwner(value); };
    const auto runtimePlayerToggleAlliance = [&](Fixture& current, const std::string& target) { frozenOldToggleAlliance(current, target, host); };
    const auto openRuntimeDiplomacyPage = [&](Fixture&) { host.openDiplomacy(); };
    const auto runtimePlayerSelectedBeacon = [&](Fixture&) -> const RuntimePlayerBeacon* {
        beaconStorage = host.selectedBeacon(); return beaconStorage ? &*beaconStorage : nullptr;
    };
    const auto widenUtf8 = [&](const std::string& value) { host.setChatDraftUtf8(value); return ui.chatDraft; };
    const auto runtimePlayerCommunicationViewerOwner = [&](Fixture&) { return host.viewerOwner(); };
    const auto runtimePlayerBeaconVisibleToLocal = [&](Fixture&, const RuntimePlayerBeacon& beacon) { return host.beaconVisible(beacon); };
    const auto cancelRuntimeActiveSuperWeaponTargetSelection = [&](Fixture&, const std::string& reason) { host.cancelSuperWeapon(reason); };
    const auto dismissRuntimePlacementFromUi = [&](Fixture&) { host.dismissPlacement(); };
    const auto dispatchRuntimePlayerWaypointFromUi = [&](Fixture&, RuntimePlayerWaypointOperation operation, std::uint64_t id) { host.dispatchWaypoint(operation, id); };
    const auto dispatchRuntimePlayerCommunicationFromUi = [&](Fixture&, Command command) { host.dispatchCommunication(std::move(command)); };    if (matches("Diplomacy", VK_TAB)) {
        if (!repeated) openRuntimeDiplomacyPage(state);
        return true;
    }
    if (matches("ToggleAlliance", 'A')) {
        if (repeated) return true;
        std::string target;
        if (!state.selectedObjectIds.empty())
            if (const auto* object = findObjectById(state, state.selectedObjectIds.front()))
                if (runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(host.currentOwner()))
                    target = runtimeCanonicalHumanHouseOwner(object->owner);
        if (target.empty()) openRuntimeDiplomacyPage(state);
        else runtimePlayerToggleAlliance(state, target);
        return true;
    }
    if (matches("ChatToAll", VK_RETURN) || matches("ChatToAllies", VK_HOME)) {
        if (!repeated) {
            ui.chatOpen = true; ui.chatAllies = matches("ChatToAllies", VK_HOME); ui.chatDraft.clear();
            ui.beaconEditing=false;
            if (!ui.chatAllies) if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
                ui.beaconEditing=true;ui.chatDraft=widenUtf8(beacon->text);
            }
            ui.beaconTargeting = local.followTargeting = local.rallyTargeting = false;
            host.setSelectionDragMoved(false); host.setSelectionDragActive(false);
        }
        return true;
    }
    if (matches("PlaceBeacon", 'B')) {
        if (!repeated) {
            if (!ui.beaconTargeting) {
                const auto owner=runtimePlayerCommunicationViewerOwner(state);
                const auto count=std::count_if(ui.beacons.begin(),ui.beacons.end(),[&](const auto& beacon) {
                    return beacon.owner==owner && runtimePlayerBeaconVisibleToLocal(state,beacon);
                });
                if (count>=3) {
                    host.beaconLimitStatus();
                    return true;
                }
                cancelRuntimeActiveSuperWeaponTargetSelection(state,"beacon");
                dismissRuntimePlacementFromUi(state);
            }
            ui.beaconTargeting = !ui.beaconTargeting;
            ui.selectedBeacon.reset();
            local.followTargeting = local.rallyTargeting = false;
            host.setSidebarTool(RuntimeSidebarTool::None);
        }
        return true;
    }
    if (matches("DeleteBeacon", VK_DELETE) || code == VK_DECIMAL) {
        if (repeated) return true;
        if (local.planning && local.selectedWaypoint) {
            dispatchRuntimePlayerWaypointFromUi(state, RuntimePlayerWaypointOperation::DeletePoint, local.selectedWaypoint);
            return true;
        }
        if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::DeleteBeacon; command.cell = beacon->cell;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    if (matches("AllToCheer", 'C')) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Cheer;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    for (int i = 1; i <= 8; ++i) if (matches("Taunt_" + std::to_string(i), VK_F5 + i - 1)) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Taunt; command.value = static_cast<std::uint8_t>(i);
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    return false;
}
bool frozenOldDiplomacy(Fixture& state, int key, bool repeated, TestHost& host) {
    auto& ui = state.playerCommunication;
    const auto runtimeDiplomacyRows = [&](Fixture&) { return host.rows(); };
    const auto runtimeMenuKeyboardBindingCode = [&](Fixture&, const std::string& id, int fallback) { return host.binding(id, fallback); };
    const auto closeRuntimeDiplomacyPage = [&](Fixture&) { host.closePage(); };
    const auto runtimeDiplomacyAllianceEnabled = [&](Fixture&, const Row& row) { return host.allianceEnabled(row); };
    const auto runtimePlayerToggleAlliance = [&](Fixture& current, const std::string& target) { frozenOldToggleAlliance(current, target, host); };    if (!ui.diplomacyOpen) return false;
    const auto rows = runtimeDiplomacyRows(state);
    const int count=static_cast<int>(std::min<std::size_t>(rows.size(),8));
    if (key == VK_ESCAPE || key == VK_TAB || key == VK_RETURN
        || key==runtimeMenuKeyboardBindingCode(state,"Diplomacy",VK_TAB)) {
        if (!repeated) closeRuntimeDiplomacyPage(state);
    } else if (count>1) {
        ui.diplomacyRow = std::clamp(ui.diplomacyRow, 1, count-1);
        if (key == VK_UP) ui.diplomacyRow = ui.diplomacyRow==1?count-1:ui.diplomacyRow-1;
        else if (key == VK_DOWN) ui.diplomacyRow = ui.diplomacyRow==count-1?1:ui.diplomacyRow+1;
        else if ((key == 'A' || key == VK_SPACE) && !repeated
            && runtimeDiplomacyAllianceEnabled(state,rows[ui.diplomacyRow]))
            runtimePlayerToggleAlliance(state,rows[ui.diplomacyRow].owner);
    }
    host.requestRepaint();
    return true;
}
void nestedDispatch(TestHost& host) {
    if (host.old) (void)frozenOldReference(host.state, 'C', true, host);
    else (void)handleRuntimeReferenceCommunicationKey({'C', true}, host.state.playerLocalCommands,
        host.state.playerCommunication, host.state.selectedObjectIds,
        static_cast<RuntimeReferenceCommunicationHost&>(host));
}
enum class Entry { Reference, Diplomacy, Alliance };
struct Observed {
    Fixture state;
    std::vector<std::string> trace;
    bool handled;
};
struct Statistics {
    std::size_t checks = 0, failures = 0, differentialCases = 0;
    std::size_t referenceCases = 0, diplomacyCases = 0, allianceCases = 0;
    std::size_t bindingQueries = 0, orderedTraceRows = 0, ownedChecks = 0, callbackCases = 0;
    std::size_t fullInt32Samples = 0;
} stats;
void check(bool result, const std::string& label) {
    ++stats.checks;
    if (!result) {
        ++stats.failures;
        if (stats.failures <= 8) std::cerr << "Reference communication contract failed: " << label << '\n';
    }
}
Observed exercise(const Fixture& initial, Entry entry, std::int32_t code,
    bool repeated, const std::string& target, const std::string& label) {
    Fixture before = initial, after = initial;
    TestHost oldHost(before, true), newHost(after, false);
    bool oldHandled = true, newHandled = true;
    if (entry == Entry::Reference) {
        ++stats.referenceCases;
        oldHandled = frozenOldReference(before, code, repeated, oldHost);
        newHandled = handleRuntimeReferenceCommunicationKey({code, repeated}, after.playerLocalCommands,
            after.playerCommunication, after.selectedObjectIds,
            static_cast<RuntimeReferenceCommunicationHost&>(newHost));
    } else if (entry == Entry::Diplomacy) {
        ++stats.diplomacyCases;
        oldHandled = frozenOldDiplomacy(before, code, repeated, oldHost);
        newHandled = handleRuntimeDiplomacyNavigationKey({code, repeated}, after.playerCommunication,
            static_cast<RuntimeDiplomacyNavigationHost&>(newHost));
    } else {
        ++stats.allianceCases;
        frozenOldToggleAlliance(before, target, oldHost);
        toggleRuntimeCommunicationAlliance(target, static_cast<RuntimeReferenceCommunicationHost&>(newHost));
    }
    ++stats.differentialCases;
    stats.bindingQueries += after.bindingReads;
    stats.orderedTraceRows += newHost.trace.size();
    check(oldHandled == newHandled, label + ": consumed");
    check(snapshot(before) == snapshot(after), label + ": complete local/control fixture state");
    check(before.commands == after.commands, label + ": all communication command fields");
    check(before.waypoints == after.waypoints, label + ": complete waypoint operation/id");
    check(oldHost.trace == newHost.trace, label + ": complete ordered query/effect/state trace");
    check(before.opaqueAuthoritySentinel == initial.opaqueAuthoritySentinel
        && after.opaqueAuthoritySentinel == initial.opaqueAuthoritySentinel, label + ": opaque authority boundary");
    return {std::move(after), std::move(newHost.trace), newHandled};
}
Observed reference(const Fixture& initial, std::int32_t code, bool repeated,
    const std::string& label) {
    return exercise(initial, Entry::Reference, code, repeated, {}, label);
}
Observed modal(const Fixture& initial, std::int32_t key, bool repeated,
    const std::string& label) {
    return exercise(initial, Entry::Diplomacy, key, repeated, {}, label);
}
std::int32_t fromBits(std::uint32_t value) {
    const auto maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    return value <= maximum ? static_cast<std::int32_t>(value)
        : -1 - static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max() - value);
}
std::vector<std::pair<std::string, std::int32_t>> actions() {
    std::vector<std::pair<std::string, std::int32_t>> result{
        {"Diplomacy", 9}, {"ToggleAlliance", 'A'}, {"ChatToAll", 13}, {"ChatToAllies", 36},
        {"PlaceBeacon", 'B'}, {"DeleteBeacon", 46}, {"AllToCheer", 'C'}};
    for (int i = 1; i <= 8; ++i) result.emplace_back("Taunt_" + std::to_string(i), 116 + i - 1);
    return result;
}
void keyMatrices() {
    const auto all = actions();
    for (int modifiers = 0; modifiers < 8; ++modifiers) {
        for (const auto& action : all) for (bool repeated : {false, true}) {
            const int code = runtimePlayerKeyCode(action.second, (modifiers & 1) != 0,
                (modifiers & 2) != 0, (modifiers & 4) != 0);
            (void)reference(seed(), code, repeated, "retail/full-modifier exact code");
            auto rebound = seed(); rebound.bindings[action.first] = code;
            (void)reference(rebound, code, repeated, "rebinding/full-modifier exact code");
        }
        (void)reference(seed(), runtimePlayerKeyCode(0x7f, (modifiers & 1) != 0,
            (modifiers & 2) != 0, (modifiers & 4) != 0), false, "unknown modifier");
    }
    const std::vector<std::int32_t> limits{std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(), -1, 0, -32769, 32768, -2048, 2048};
    for (const auto code : limits) {
        for (const auto& action : all) for (bool repeated : {false, true}) {
            auto state = seed(); state.bindings[action.first] = code;
            (void)reference(state, code, repeated, "rebind full int32 anchors");
        }
        (void)reference(seed(), code, false, "unknown full int32 anchor");
    }
    std::uint32_t bits = 0x6e624eb7U;
    for (int i = 0; i < 512; ++i) {
        bits ^= bits << 13; bits ^= bits >> 17; bits ^= bits << 5;
        ++stats.fullInt32Samples;
        const auto code = fromBits(bits);
        (void)reference(seed(), code, (i & 1) != 0, "full signed32 generated unknown/exact collision");
        if ((i % 16) == 0) {
            auto state = seed();
            state.bindings[all[static_cast<std::size_t>(i / 16) % all.size()].first] = code;
            (void)reference(state, code, false, "full signed32 generated rebind");
        }
    }
    for (bool repeated : {false, true}) {
        auto collision = seed();
        for (const auto& action : all) collision.bindings[action.first] = -917;
        auto result = reference(collision, -917, repeated, "all bindings collide earliest Diplomacy");
        check(result.state.bindingReads == 1 && result.state.openCalls == (repeated ? 0U : 1U)
            && result.state.commands.empty(), "collision stops after first binding");
        collision = seed();
        for (std::size_t i = 2; i < all.size(); ++i) collision.bindings[all[i].first] = 501;
        result = reference(collision, 501, repeated, "chat beats B/Delete/C/Taunt");
        check(result.state.commands.empty(), "chat collision does not dispatch");
    }
}
void allianceCases() {
    for (const std::string& target : {std::string{}, std::string("Enemy"), std::string("player-alias"),
            std::string("raw\0target", 10)}) {
        for (int offer = -1; offer <= 1; ++offer)
            for (bool allied : {false, true}) for (bool accepted : {false, true}) {
                auto state = seed(); if (offer >= 0) state.offer = offer != 0;
                state.allied = allied; state.accepted = accepted;
                const auto result = exercise(state, Entry::Alliance, 0, false, target, "direct alliance owner");
                if (target.empty()) {
                    check(result.trace.empty() && result.state.currentOwnerReads == 0
                        && result.state.commands.empty(), "empty alliance target is a zero-query return");
                } else {
                    check(result.state.commands.size() == 1 && result.state.commands[0].kind
                        == RuntimePlayerCommunicationKind::Alliance
                        && result.state.commands[0].text == target
                        && result.state.commands[0].cell == Cell{}
                        && result.state.commands[0].value == ((offer < 0 ? allied : offer != 0) ? 0 : 1)
                        && result.state.canonicalReads == 1 && result.state.currentOwnerReads == 1,
                        "alliance raw target/full defaults/current canonical only");
                    check(result.state.alliedReads == (offer < 0 ? 1U : 0U),
                        "explicit offer false or true never queries allied fallback");
                }
            }
    }
    for (int mode = 0; mode < 5; ++mode) {
        auto state = seed();
        if (mode == 0) state.selectedObjectIds.clear();
        if (mode == 1) state.selectedObjectIds = {999, 1};
        if (mode == 2) state.objects[1] = "Player";
        if (mode == 3) state.objects[1] = "player-alias";
        if (mode == 4) state.objects[1] = "Foreign-Unfiltered";
        const auto result = reference(state, 'A', false, "first selection only/self/stale/foreign");
        check(result.state.objectReads == (mode == 0 ? 0U : 1U),
            "A does not search later selected object or add eligibility lookup");
        check(mode == 4 ? result.state.commands.size() == 1 : result.state.openCalls == 1,
            "A opens or dispatches at original first-owner boundary");
        const auto repeated = reference(state, 'A', true, "repeated A consumes without selection queries");
        check(repeated.state.objectReads == 0 && repeated.state.currentOwnerReads == 0,
            "repeat A is an early consumed result");
    }
    auto state = seed();
    state.canonicalScript = {"Foreign-Initial", "Player", "Foreign-Final", "Player"};
    state.callback = 1;
    auto result = reference(state, 'A', false, "dynamic canonical callbacks retain owned object owner");
    check(result.state.canonicalReads == 4 && result.state.objectReads == 1
        && result.state.commands.size() == 1 && result.state.commands.front().text == "Foreign-Final",
        "A repeats canonical object query at original third call");
    ++stats.ownedChecks; ++stats.callbackCases;
    for (const int callback : {2, 3, 4}) {
        state = seed(); state.callback = callback;
        result = reference(state, 'A', false, "synchronous dispatch clears sources/replaces UI/reenters");
        check(result.state.commands.size() == 1 && result.state.commands.front().text == "Enemy",
            "owned alliance payload survives synchronous callback");
        ++stats.ownedChecks; ++stats.callbackCases;
    }
}
void chatCases() {
    for (bool allied : {false, true}) for (bool hasBeacon : {false, true})
        for (bool repeated : {false, true}) {
            auto state = seed(); if (!hasBeacon) state.selected.reset();
            const auto result = reference(state, allied ? 36 : 13, repeated, "chat/selected/stale/repeat");
            check(result.state.selectedReads == ((!allied && !repeated) ? 1U : 0U),
                "allied or repeated chat does not query/clear stale selected beacon");
            if (!repeated) check(!result.state.playerLocalCommands.rallyTargeting
                && !result.state.playerLocalCommands.followTargeting && !result.state.playerCommunication.beaconTargeting
                && !result.state.dragMoved && !result.state.dragActive, "chat targeting chain and moved-before-active sinks");
        }
    auto state = seed();
    state.bindingScript["ChatToAllies"] = {36, 37};
    auto result = reference(state, 36, false, "second Allies dynamic result must not use first match");
    check(result.state.alliesReads == 2 && !result.state.playerCommunication.chatAllies
        && result.state.selectedReads == 1 && result.state.alliesQuerySawOpen
        && result.state.alliesQuerySawOldDraft, "second Allies query sees open=true and old draft before clear");
    state = seed(); state.bindingScript["ChatToAllies"] = {13};
    result = reference(state, 13, false, "ChatAll outer short circuit inner Allies freshly true");
    check(result.state.alliesReads == 1 && result.state.playerCommunication.chatAllies
        && result.state.selectedReads == 0, "ChatAll collision still performs fresh inner Allies query only");
    state = seed(); state.callback = 5;
    state.selected->text = std::string("\xe4\xbf\xa1\xe6\xa0\x87\0x", 8);
    result = reference(state, 13, false, "owned beacon UTF8 handed to unchanged conversion sink");
    check(result.state.playerCommunication.beaconEditing && result.state.playerCommunication.chatDraft == u"converted-8"
        && result.state.playerCommunication.beacons.empty(), "owned beacon text survives source clear callback");
    ++stats.ownedChecks; ++stats.callbackCases;
}
void beaconCases() {
    for (const int count : {0, 2, 3, 4}) for (bool targeting : {false, true}) {
        auto state = seed(); auto& ui = state.playerCommunication;
        ui.beaconTargeting = targeting; ui.beacons.clear();
        for (int i = 0; i < count; ++i) ui.beacons.push_back({"Player", {i, 7}, -1, "own", i});
        ui.beacons.push_back({"player-alias", {55, 7}, -1, "raw-owner-mismatch", 0});
        ui.beacons.push_back({"Foreign", {56, 7}, -1, "foreign", 0});
        const auto result = reference(state, 'B', false, "B count/raw owner/close/limit");
        check(result.state.viewerReads == (targeting ? 0U : 1U)
            && result.state.visibleReads == (targeting ? 0U : static_cast<std::size_t>(count)),
            "B visible queried only for raw equal owner and only on entering");
        if (!targeting && count >= 3) {
            check(result.state.status == "beacon-limit" && result.state.cancelCalls == 0
                && result.state.dismissCalls == 0 && !result.state.playerCommunication.beaconTargeting
                && result.state.playerCommunication.selectedBeacon == ui.selectedBeacon
                && result.state.superWeapon && result.state.placement && result.state.dragActive,
                "B limit changes only owner status sink and returns before cancellation");
        } else {
            check(result.state.cancelCalls == (targeting ? 0U : 1U)
                && result.state.dismissCalls == (targeting ? 0U : 1U)
                && result.state.playerCommunication.beaconTargeting != targeting
                && !result.state.playerCommunication.selectedBeacon
                && !result.state.playerLocalCommands.rallyTargeting
                && !result.state.playerLocalCommands.followTargeting
                && result.state.sidebarTool == RuntimeSidebarTool::None, "B flip/reset/clear original chain");
        }
    }
    auto state = seed(); state.playerCommunication.beacons.clear();
    for (int i = 0; i < 4; ++i) {
        state.playerCommunication.beacons.push_back({"Player", {i, 2}, -1, "visibility", 0});
        state.visible[{i, 2}] = i < 2;
    }
    auto result = reference(state, 'B', false, "B count original owner-defined visibility subset");
    check(result.state.visibleReads == 4 && result.state.cancelCalls == 1, "invisible owned beacon does not fill limit");
    result = reference(state, 'B', true, "B repeat has no count or effect");
    check(result.state.viewerReads == 0 && result.state.visibleReads == 0 && result.state.cancelCalls == 0,
        "B repeat consumes at branch boundary");
}
void deleteCheerTauntCases() {
    for (const bool planning : {false, true}) for (const std::uint64_t point : {std::uint64_t{0},
            std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()})
        for (const bool beacon : {false, true}) for (const int code : {46, 110, 110 | 0x100}) {
            auto state = seed(); state.playerLocalCommands.planning = planning;
            state.playerLocalCommands.selectedWaypoint = point;
            if (!beacon) state.selected.reset();
            const auto result = reference(state, code, false, "Delete/Decimal planning priority and full uint64 point");
            if (code == (110 | 0x100)) check(!result.handled && result.state.waypoints.empty()
                && result.state.selectedReads == 0, "modified Decimal does not become raw Delete");
            else if (planning && point) check(result.state.waypoints.size() == 1
                && result.state.waypoints[0].operation == RuntimePlayerWaypointOperation::DeletePoint
                && result.state.waypoints[0].waypointId == point && result.state.selectedReads == 0
                && result.state.commands.empty(), "planning point owns early priority before beacon query");
            else if (beacon) check(result.state.commands.size() == 1
                && result.state.commands[0].kind == RuntimePlayerCommunicationKind::DeleteBeacon
                && result.state.commands[0].cell == state.selected->cell, "Delete copies selected owned cell");
        }
    for (bool accepted : {false, true}) for (int i = 0; i <= 8; ++i) for (bool repeated : {false, true}) {
        auto state = seed(); state.accepted = accepted;
        const auto result = reference(state, i == 0 ? 'C' : 115 + i, repeated, "Cheer/Taunt complete original command");
        check(result.handled && result.state.commands.size() == (repeated ? 0U : 1U),
            "Cheer/Taunt repeated consumes without submission");
        if (!repeated) check(result.state.commands[0].kind == (i == 0
            ? RuntimePlayerCommunicationKind::Cheer : RuntimePlayerCommunicationKind::Taunt)
            && result.state.commands[0].value == i && result.state.commands[0].text.empty()
            && result.state.commands[0].cell == Cell{}, "Cheer/Taunt all four command fields");
    }
}
void diplomacyCases() {
    const std::vector<int> rowStarts{std::numeric_limits<int>::min(), -1, 0, 1,
        std::numeric_limits<int>::max()};
    const std::vector<int> keys{27, 9, 13, 38, 40, 'A', 32, 0, -1, std::numeric_limits<int>::max()};
    for (int count : {0, 1, 2, 8, 10}) for (int row : rowStarts)
        for (int key : keys) for (bool repeated : {false, true}) for (bool enabled : {false, true}) {
            auto state = seed(); state.playerCommunication.diplomacyOpen = true;
            state.playerCommunication.diplomacyRow = row; state.enabled = enabled; state.roster.clear();
            for (int i = 0; i < count; ++i) state.roster.push_back({"owner-" + std::to_string(i), i == 0, false});
            const auto result = modal(state, key, repeated, "modal row count/clamp/close/cycle/eligibility");
            check(result.handled && result.state.rowReads == 1 && result.state.repaintCalls == 1,
                "open diplomacy snapshots rows once then repaints even repeat/unknown");
            if (key == 27 || key == 9 || key == 13) check(result.state.bindingReads == 0
                && result.state.closeCalls == (repeated ? 0U : 1U), "raw close keys short circuit binding");
            else check(result.state.bindingReads == 1, "other modal key queries binding even empty rows");
            if (count > 1 && key != 27 && key != 9 && key != 13)
                check(result.state.playerCommunication.diplomacyRow >= 1
                    && result.state.playerCommunication.diplomacyRow < std::min(count, 8),
                    "modal row remains in capped eight-row domain");
        }
    auto state = seed(); state.playerCommunication.diplomacyOpen = false;
    auto result = modal(state, 'A', false, "closed diplomacy has no rows/query/effects");
    check(!result.handled && result.trace.empty(), "closed modal does not enter owner queries");
    for (const std::int32_t code : {std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max(), -198, 0, 0x741}) {
        state = seed(); state.playerCommunication.diplomacyOpen = true; state.bindings["Diplomacy"] = code;
        result = modal(state, code, false, "modal rebind full int32 close");
        check(result.state.closeCalls == 1, "modal exact full32 rebind");
    }
    for (int callback : {2, 3, 4, 6, 7, 8}) {
        state = seed(); state.playerCommunication.diplomacyOpen = true;
        state.playerCommunication.diplomacyRow = 1; state.callback = callback;
        result = modal(state, 'A', false, "owned row/canonical payload/root sink after synchronous callback");
        check(result.state.commands.size() == 1 && result.state.commands[0].text
            == (callback == 8 ? "Third" : "Enemy") && result.state.rowReads == 1
            && result.state.eligibilityReads == 1 && result.state.repaintCalls == 1,
            "owned rows survive source clear and original repeated row-index read");
        ++stats.ownedChecks; ++stats.callbackCases;
    }
    state = seed(); state.playerCommunication.diplomacyOpen = true;
    state.playerCommunication.diplomacyRow = 1;
    result = modal(state, 38, true, "Up repeat wraps from first eligible to capped last");
    check(result.state.playerCommunication.diplomacyRow == 2, "Up repeat still moves");
    state.playerCommunication.diplomacyRow = 2;
    result = modal(state, 40, true, "Down repeat wraps last to first");
    check(result.state.playerCommunication.diplomacyRow == 1, "Down repeat still moves");
}
void ownedPayloadBoundaries() {
    const std::vector<std::int32_t> bounds{std::numeric_limits<std::int32_t>::min(), 0,
        std::numeric_limits<std::int32_t>::max()};
    for (const auto x : bounds) for (const auto y : bounds) {
        auto state = seed(); state.selected->cell = {x, y};
        const auto result = reference(state, 46, false, "owned beacon complete signed32 cell payload");
        check(result.state.commands.size() == 1 && result.state.commands[0].kind
            == RuntimePlayerCommunicationKind::DeleteBeacon && result.state.commands[0].cell == Cell{x, y}
            && result.state.commands[0].text.empty() && result.state.commands[0].value == 0,
            "DeleteBeacon owns both full-width cell fields without arithmetic or narrowing");
    }
    for (int mask = 0; mask < 4; ++mask) {
        auto state = seed(); state.playerCommunication.diplomacyOpen = true;
        state.playerCommunication.diplomacyRow = 1;
        state.roster[1].self = (mask & 1) != 0; state.roster[1].defeated = (mask & 2) != 0;
        const auto result = modal(state, 'A', false, "owned diplomacy self/defeated fields reach eligibility owner");
        check(result.state.eligibilityReads == 1 && result.state.commands.size() == (mask ? 0U : 1U),
            "controller does not prefilter or discard row qualification fields");
    }
    auto state = seed(); state.playerCommunication.diplomacyOpen = true;
    state.playerCommunication.diplomacyRow = 1; state.roster[1].owner.clear();
    const auto result = modal(state, 'A', false, "controlled empty row target obeys shared toggle early return");
    check(result.state.commands.empty() && result.state.currentOwnerReads == 0
        && result.state.repaintCalls == 1, "empty target remains zero owner/offer query after opaque eligibility");
}
} // namespace

int main() {
    keyMatrices();
    allianceCases();
    chatCases();
    beaconCases();
    deleteCheerTauntCases();
    diplomacyCases();
    ownedPayloadBoundaries();
    std::cout << "Reference communication contract: checks=" << stats.checks
        << " differentialCases=" << stats.differentialCases << " referenceCases=" << stats.referenceCases
        << " diplomacyCases=" << stats.diplomacyCases << " allianceCases=" << stats.allianceCases
        << " bindingQueries=" << stats.bindingQueries << " orderedTraceRows=" << stats.orderedTraceRows
        << " ownedChecks=" << stats.ownedChecks << " callbackCases=" << stats.callbackCases
        << " fullInt32Samples=" << stats.fullInt32Samples << " modifierCombinations=8 failures=" << stats.failures
        << " sameProductionCpp=1 frozenOldOracle=1 fullLocalState=1 completeCommunicationFields=4"
        << " nativeIntegration=not-covered bridgeAuthority=not-covered codec=callback-only"
        << " physicalModifierMatrix=not-run rootSwapModel=controlled-only\n";
    return stats.failures == 0 ? 0 : 1;
}
