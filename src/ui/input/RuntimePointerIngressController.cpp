#include "ui/input/RuntimePointerIngressController.h"
#include <algorithm>

namespace {
bool pointerMessage(RuntimePointerIngressEvent event) {
    switch (event) {
    case RuntimePointerIngressEvent::PointerMove:
    case RuntimePointerIngressEvent::LeftButtonDown:
    case RuntimePointerIngressEvent::LeftButtonUp:
    case RuntimePointerIngressEvent::LeftButtonDoubleClick:
    case RuntimePointerIngressEvent::RightButtonDown:
    case RuntimePointerIngressEvent::RightButtonUp:
    case RuntimePointerIngressEvent::RightButtonDoubleClick:
    case RuntimePointerIngressEvent::MiddleButtonDown:
    case RuntimePointerIngressEvent::MiddleButtonUp:
    case RuntimePointerIngressEvent::MiddleButtonDoubleClick:
    case RuntimePointerIngressEvent::AuxiliaryButtonDown:
    case RuntimePointerIngressEvent::AuxiliaryButtonUp:
    case RuntimePointerIngressEvent::AuxiliaryButtonDoubleClick:
    case RuntimePointerIngressEvent::VerticalWheel:
    case RuntimePointerIngressEvent::HorizontalWheel:
        return true;
    default:
        return false;
    }
}
bool terminalPointerMessage(RuntimePointerIngressEvent event) {
    return event != RuntimePointerIngressEvent::PointerMove && pointerMessage(event);
}
bool diplomacyCancelMessage(RuntimePointerIngressEvent event) {
    return event == RuntimePointerIngressEvent::CaptureChanged
        || event == RuntimePointerIngressEvent::CancelMode
        || event == RuntimePointerIngressEvent::FocusLost
        || event == RuntimePointerIngressEvent::ApplicationDeactivated;
}
bool chatPointerMessage(RuntimePointerIngressEvent event) {
    return event == RuntimePointerIngressEvent::LeftButtonDown
        || event == RuntimePointerIngressEvent::LeftButtonUp
        || event == RuntimePointerIngressEvent::LeftButtonDoubleClick
        || event == RuntimePointerIngressEvent::RightButtonDown
        || event == RuntimePointerIngressEvent::RightButtonUp
        || event == RuntimePointerIngressEvent::VerticalWheel;
}
bool halfOpenContains(RuntimeInputPixelRect rect, RuntimeInputPixelPoint point) {
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}
} // namespace

void closeRuntimeDiplomacyPointer(RuntimeDiplomacyPointerHost& host) {
    host.setDiplomacyOpen(false);
    host.setDiplomacyHoveredControl(0);
    host.setDiplomacyPressedControl(0);
    host.setRightMoved(false);
    host.setRightActive(false);
    host.requestRepaint();
}

void openRuntimeDiplomacyPointer(RuntimeDiplomacyPointerHost& host) {
    host.setDiplomacyOpen(true);
    host.setDiplomacyRow(1);
    host.setDiplomacyHoveredControl(0);
    host.setDiplomacyPressedControl(0);
    host.setBeaconEditing(false);
    host.setChatOpen(false);
    host.clearChatDraft();
    host.setLeftMoved(false);
    host.setLeftActive(false);
    host.setRightMoved(false);
    host.setRightActive(false);
    host.requestRepaint();
}

bool runtimeDiplomacyPointerChatEnabled(const RuntimeDiplomacyPointerRow& row,
    RuntimeDiplomacyPointerHost& host) {
    const auto ownedRow = row;
    return !ownedRow.self && ownedRow.human && ownedRow.slot && *ownedRow.slot < 8
        && host.chatRecipientsAvailable();
}

void toggleRuntimeDiplomacyPointerChat(const RuntimeDiplomacyPointerRow& row,
    RuntimeDiplomacyPointerHost& host) {
    const auto ownedRow = row;
    if (!runtimeDiplomacyPointerChatEnabled(ownedRow, host)) return;
    if (!host.chatRecipientsExplicit()) host.setChatRecipients(host.chatSlotsMask());
    host.setChatRecipientsExplicit(true);
    host.setChatRecipients(static_cast<std::uint8_t>(
        host.chatRecipients() ^ static_cast<std::uint8_t>(1U << *ownedRow.slot)));
    const auto ownSlot = host.ownChatSlot();
    host.setChatRecipients(static_cast<std::uint8_t>(
        host.chatRecipients() | static_cast<std::uint8_t>(1U << *ownSlot)));
    host.requestRepaint();
}

std::int32_t hitRuntimeDiplomacyPointerControl(RuntimeInputPixelRect client,
    RuntimeInputPixelPoint point, RuntimeDiplomacyPointerHost& host) {
    if (runtimeInputPixelPointInRectInclusive(host.controlRect(client, 1059), point))
        return 1059;
    const auto rows = host.rows();
    for (std::size_t i = 1; i < rows.size() && i < 8; ++i) {
        if (host.allianceEnabled(rows[i])
            && runtimeInputPixelPointInRectInclusive(
                host.checkboxRect(client, 1742 + static_cast<std::int32_t>(i)), point))
            return 1742 + static_cast<std::int32_t>(i);
        if (runtimeDiplomacyPointerChatEnabled(rows[i], host)
            && runtimeInputPixelPointInRectInclusive(
                host.checkboxRect(client, runtimeDiplomacyPointerChatControls[i - 1]), point))
            return runtimeDiplomacyPointerChatControls[i - 1];
    }
    if (runtimeInputPixelPointInRectInclusive(host.controlRect(client, 1840), point))
        return 1840;
    return 0;
}

void handleRuntimeDiplomacyPointer(const RuntimePointerIngressInput& input,
    RuntimeInputPixelRect client, RuntimeDiplomacyPointerHost& host) {
    const auto key = input;
    const auto hit = hitRuntimeDiplomacyPointerControl(client, key.point, host);
    host.setDiplomacyHoveredControl(hit);
    if (key.event == RuntimePointerIngressEvent::LeftButtonDown) {
        host.setDiplomacyPressedControl(hit);
    } else if (key.event == RuntimePointerIngressEvent::LeftButtonUp) {
        const bool activate = hit != 0 && hit == host.diplomacyPressedControl();
        host.setDiplomacyPressedControl(0);
        if (activate) {
            if (hit == 1059) closeRuntimeDiplomacyPointer(host);
            else if (hit == 1840) host.setTauntsEnabled(!host.tauntsEnabled());
            else {
                const auto rows = host.rows();
                if (hit >= 1743 && hit <= 1749) {
                    const auto row = static_cast<std::size_t>(hit - 1742);
                    if (row < rows.size()) {
                        host.setDiplomacyRow(static_cast<std::int32_t>(row));
                        host.toggleAlliance(rows[row].owner);
                    }
                } else {
                    const auto found = std::find(runtimeDiplomacyPointerChatControls.begin(),
                        runtimeDiplomacyPointerChatControls.end(), hit);
                    const auto row = static_cast<std::size_t>(
                        found - runtimeDiplomacyPointerChatControls.begin() + 1);
                    if (row < rows.size()) {
                        host.setDiplomacyRow(static_cast<std::int32_t>(row));
                        toggleRuntimeDiplomacyPointerChat(rows[row], host);
                    }
                }
            }
        }
    } else if (key.event == RuntimePointerIngressEvent::RightButtonUp) {
        closeRuntimeDiplomacyPointer(host);
    }
    host.requestRepaint();
}

bool handleRuntimePointerIngress(const RuntimePointerIngressInput& input,
    RuntimePointerIngressHost& host) {
    const auto key = input;
    if (host.finished()) {
        if (host.ownsCapture()) host.releaseCapture();
        if (!host.campaignMode() && !host.bannerVisible()
            && (key.event == RuntimePointerIngressEvent::PointerMove
                || key.event == RuntimePointerIngressEvent::LeftButtonDown
                || key.event == RuntimePointerIngressEvent::LeftButtonUp)) {
            const auto client = host.scoreClientRect();
            const auto button = host.scoreContinueRect(client);
            const bool inside = halfOpenContains(button, key.point);
            if (key.event == RuntimePointerIngressEvent::LeftButtonUp) {
                const bool activate = host.scoreContinuePressed() && inside;
                host.setScoreContinuePressed(false);
                host.requestRepaint();
                if (activate) host.sendScoreReturn();
            } else {
                if (key.event == RuntimePointerIngressEvent::LeftButtonDown)
                    host.setScoreContinuePressed(inside);
                host.setScoreContinueHovered(inside);
                host.requestRepaint();
            }
            return true;
        }
        if (terminalPointerMessage(key.event)) return true;
    }
    if (!host.menuOpen() && !host.finished()
        && (host.chatOpen() || host.diplomacyOpen())) {
        if (host.diplomacyOpen()) {
            if (diplomacyCancelMessage(key.event)) {
                host.setDiplomacyPressedControl(0);
                if (key.event != RuntimePointerIngressEvent::CaptureChanged && host.ownsCapture())
                    host.releaseCapture();
                return true;
            }
            if (pointerMessage(key.event)) {
                const auto client = host.diplomacyClientRect();
                handleRuntimeDiplomacyPointer(key, client, host);
                if (key.event == RuntimePointerIngressEvent::LeftButtonDown) host.setCapture();
                if (key.event == RuntimePointerIngressEvent::LeftButtonUp
                    || key.event == RuntimePointerIngressEvent::RightButtonUp) {
                    if (host.ownsCapture()) host.releaseCapture();
                }
                host.setLeftMoved(false);
                host.setLeftActive(false);
                host.invalidate();
                return true;
            }
        }
        if (chatPointerMessage(key.event)) {
            if (key.event == RuntimePointerIngressEvent::RightButtonUp) {
                host.setBeaconEditing(false);
                host.setDiplomacyOpen(false);
                host.setChatOpen(false);
                host.clearChatDraft();
            }
            host.setLeftMoved(false);
            host.setLeftActive(false);
            host.requestRepaint();
            host.invalidate();
            return true;
        }
    }
    return false;
}
