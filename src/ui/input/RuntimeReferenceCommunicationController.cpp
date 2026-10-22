#include "ui/input/RuntimeReferenceCommunicationController.h"
#include "ui/input/RuntimePlayerCommands.h"
#include <algorithm>

void toggleRuntimeCommunicationAlliance(const std::string& target,
    RuntimeCommunicationAllianceHost& host) {
    if (target.empty()) return;
    const auto owner = host.canonicalOwner(host.currentOwner());
    const auto offer = host.allianceOffer(owner, target);
    const bool offered = !offer ? host.ownersAllied(owner, target) : *offer;
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind = RuntimePlayerCommunicationKind::Alliance;
    command.text = target; command.value = offered ? 0 : 1;
    host.dispatchCommunication(command);
}

bool handleRuntimeReferenceCommunicationKey(const RuntimeReferenceCommunicationInput& input,
    RuntimePlayerLocalCommands& local, RuntimePlayerCommunicationState& ui,
    const std::vector<int>& selection, RuntimeReferenceCommunicationHost& host) {
    const auto code = input.code;
    const bool repeated = input.repeated;
    const auto matches = [&](const std::string& id, std::int32_t fallback) {
        return code == host.binding(id, fallback);
    };
    if (matches("Diplomacy", 9)) {
        if (!repeated) host.openDiplomacy();
        return true;
    }
    if (matches("ToggleAlliance", 'A')) {
        if (repeated) return true;
        std::string target;
        if (!selection.empty())
            if (const auto object = host.objectOwner(selection.front()))
                if (host.canonicalOwner(*object) != host.canonicalOwner(host.currentOwner()))
                    target = host.canonicalOwner(*object);
        if (target.empty()) host.openDiplomacy();
        else toggleRuntimeCommunicationAlliance(target, host);
        return true;
    }
    if (matches("ChatToAll", 13) || matches("ChatToAllies", 36)) {
        if (!repeated) {
            ui.chatOpen = true; ui.chatAllies = matches("ChatToAllies", 36); ui.chatDraft.clear();
            ui.beaconEditing = false;
            if (!ui.chatAllies) if (const auto beacon = host.selectedBeacon()) {
                ui.beaconEditing = true; host.setChatDraftUtf8(beacon->text);
            }
            ui.beaconTargeting = local.followTargeting = local.rallyTargeting = false;
            host.setSelectionDragMoved(false);
            host.setSelectionDragActive(false);
        }
        return true;
    }
    if (matches("PlaceBeacon", 'B')) {
        if (!repeated) {
            if (!ui.beaconTargeting) {
                const auto owner = host.viewerOwner();
                const auto count = std::count_if(ui.beacons.begin(), ui.beacons.end(), [&](const auto& beacon) {
                    return beacon.owner == owner && host.beaconVisible(beacon);
                });
                if (count >= 3) {
                    host.beaconLimitStatus();
                    return true;
                }
                host.cancelSuperWeapon("beacon");
                host.dismissPlacement();
            }
            ui.beaconTargeting = !ui.beaconTargeting;
            ui.selectedBeacon.reset();
            local.followTargeting = local.rallyTargeting = false;
            host.setSidebarTool(RuntimeSidebarTool::None);
        }
        return true;
    }
    if (matches("DeleteBeacon", 46) || code == 110) {
        if (repeated) return true;
        if (local.planning && local.selectedWaypoint) {
            host.dispatchWaypoint(RuntimePlayerWaypointOperation::DeletePoint, local.selectedWaypoint);
            return true;
        }
        if (const auto beacon = host.selectedBeacon()) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::DeleteBeacon; command.cell = beacon->cell;
            host.dispatchCommunication(command);
        }
        return true;
    }
    if (matches("AllToCheer", 'C')) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Cheer;
            host.dispatchCommunication(command);
        }
        return true;
    }
    for (int i = 1; i <= 8; ++i) if (matches("Taunt_" + std::to_string(i), 116 + i - 1)) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Taunt;
            command.value = static_cast<std::uint8_t>(i);
            host.dispatchCommunication(command);
        }
        return true;
    }
    return false;
}

bool handleRuntimeDiplomacyNavigationKey(const RuntimeReferenceCommunicationInput& input,
    RuntimePlayerCommunicationState& ui, RuntimeDiplomacyNavigationHost& host) {
    const auto key = input.code;
    const bool repeated = input.repeated;
    if (!ui.diplomacyOpen) return false;
    const auto rows = host.rows();
    const int count = static_cast<int>(std::min<std::size_t>(rows.size(), 8));
    if (key == 27 || key == 9 || key == 13 || key == host.binding("Diplomacy", 9)) {
        if (!repeated) host.closePage();
    } else if (count > 1) {
        ui.diplomacyRow = std::clamp(ui.diplomacyRow, 1, count - 1);
        if (key == 38) ui.diplomacyRow = ui.diplomacyRow == 1 ? count - 1 : ui.diplomacyRow - 1;
        else if (key == 40) ui.diplomacyRow = ui.diplomacyRow == count - 1 ? 1 : ui.diplomacyRow + 1;
        else if ((key == 'A' || key == 32) && !repeated
            && host.allianceEnabled(rows[static_cast<std::size_t>(ui.diplomacyRow)]))
            toggleRuntimeCommunicationAlliance(
                rows[static_cast<std::size_t>(ui.diplomacyRow)].owner, host);
    }
    host.requestRepaint();
    return true;
}
