#include "ui/input/RuntimePointerIngressController.h"

bool runtimeDiplomacyAllianceEnabled(const RuntimeWindowState&, const RuntimeDiplomacyRow&);
void runtimePlayerToggleAlliance(RuntimeWindowState&, const std::string&);

static RuntimeInputPixelRect runtimePointerIngressPixelRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
static RECT runtimePointerIngressNativeRect(RuntimeInputPixelRect rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
static RuntimeDiplomacyPointerRow runtimePointerIngressOwnedRow(const RuntimeDiplomacyRow& row) {
    return {row.owner, row.self, row.defeated, row.human, row.slot};
}

template<class Port>
class RuntimeDiplomacyPointerUiHostBase : public Port {
public:
    explicit RuntimeDiplomacyPointerUiHostBase(RuntimeWindowState& state) : state_(state) {}
    std::vector<RuntimeDiplomacyPointerRow> rows() override {
        const auto privateRows = runtimeDiplomacyRows(state_);
        std::vector<RuntimeDiplomacyPointerRow> result;
        result.reserve(privateRows.size());
        for (const auto& row : privateRows) result.push_back(runtimePointerIngressOwnedRow(row));
        return result;
    }
    RuntimeInputPixelRect controlRect(RuntimeInputPixelRect client, std::int32_t id) override {
        return runtimePointerIngressPixelRect(
            runtimeDiplomacyControlRect(runtimePointerIngressNativeRect(client), id));
    }
    RuntimeInputPixelRect checkboxRect(RuntimeInputPixelRect client, std::int32_t id) override {
        return runtimePointerIngressPixelRect(
            runtimeDiplomacyCheckboxRect(runtimePointerIngressNativeRect(client), id));
    }
    bool allianceEnabled(const RuntimeDiplomacyPointerRow& row) override {
        RuntimeDiplomacyRow privateRow;
        privateRow.owner = row.owner; privateRow.self = row.self;
        privateRow.defeated = row.defeated; privateRow.human = row.human; privateRow.slot = row.slot;
        return runtimeDiplomacyAllianceEnabled(state_, privateRow);
    }
    void toggleAlliance(std::string target) override { runtimePlayerToggleAlliance(state_, target); }
    bool chatRecipientsAvailable() override { return runtimePlayerChatRecipientsAvailable(state_); }
    std::uint8_t chatSlotsMask() override { return runtimePlayerChatSlotsMask(state_); }
    std::optional<std::size_t> ownChatSlot() override {
        return runtimePlayerChatSlot(state_, state_.economy.owner);
    }
    bool chatRecipientsExplicit() override { return state_.playerCommunication.chatRecipientsExplicit; }
    void setChatRecipientsExplicit(bool value) override {
        state_.playerCommunication.chatRecipientsExplicit = value;
    }
    std::uint8_t chatRecipients() override { return state_.playerCommunication.chatRecipients; }
    void setChatRecipients(std::uint8_t value) override { state_.playerCommunication.chatRecipients = value; }
    void setDiplomacyOpen(bool value) override { state_.playerCommunication.diplomacyOpen = value; }
    void setDiplomacyRow(std::int32_t value) override { state_.playerCommunication.diplomacyRow = value; }
    std::int32_t diplomacyPressedControl() override {
        return state_.playerCommunication.diplomacyPressedControl;
    }
    void setDiplomacyPressedControl(std::int32_t value) override {
        state_.playerCommunication.diplomacyPressedControl = value;
    }
    void setDiplomacyHoveredControl(std::int32_t value) override {
        state_.playerCommunication.diplomacyHoveredControl = value;
    }
    bool tauntsEnabled() override { return state_.playerCommunication.tauntsEnabled; }
    void setTauntsEnabled(bool value) override { state_.playerCommunication.tauntsEnabled = value; }
    void setChatOpen(bool value) override { state_.playerCommunication.chatOpen = value; }
    void setBeaconEditing(bool value) override { state_.playerCommunication.beaconEditing = value; }
    void clearChatDraft() override { state_.playerCommunication.chatDraft.clear(); }
    void setLeftActive(bool value) override { state_.leftPointer.active = value; }
    void setLeftMoved(bool value) override { state_.leftPointer.moved = value; }
    void setRightActive(bool value) override { state_.rightPointer.active = value; }
    void setRightMoved(bool value) override { state_.rightPointer.moved = value; }
    void requestRepaint() override { state_.repaintRequested = true; }
protected:
    RuntimeWindowState& state_;
};
using RuntimeDiplomacyPointerUiHost = RuntimeDiplomacyPointerUiHostBase<RuntimeDiplomacyPointerHost>;

class RuntimePointerIngressUiHost final
    : public RuntimeDiplomacyPointerUiHostBase<RuntimePointerIngressHost> {
    using Base = RuntimeDiplomacyPointerUiHostBase<RuntimePointerIngressHost>;
public:
    RuntimePointerIngressUiHost(RuntimeWindowState& state, HWND window)
        : Base(state), window_(window) {}
    bool finished() override { return state_.skirmishMatchFinished; }
    bool campaignMode() override { return state_.campaignMode; }
    bool bannerVisible() override { return state_.resultPresentation.bannerVisible; }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool chatOpen() override { return state_.playerCommunication.chatOpen; }
    bool diplomacyOpen() override { return state_.playerCommunication.diplomacyOpen; }
    bool ownsCapture() override { return GetCapture() == window_; }
    void releaseCapture() override { ReleaseCapture(); }
    void setCapture() override { SetCapture(window_); }
    RuntimeInputPixelRect scoreClientRect() override {
        RECT client;
        GetClientRect(window_, &client);
        return runtimePointerIngressPixelRect(client);
    }
    RuntimeInputPixelRect diplomacyClientRect() override {
        RECT client{};
        GetClientRect(window_, &client);
        return runtimePointerIngressPixelRect(client);
    }
    RuntimeInputPixelRect scoreContinueRect(RuntimeInputPixelRect client) override {
        return runtimePointerIngressPixelRect(
            runtimeMatchScoreContinueRect(runtimePointerIngressNativeRect(client)));
    }
    bool scoreContinuePressed() override { return state_.resultPresentation.continuePressed; }
    void setScoreContinuePressed(bool value) override { state_.resultPresentation.continuePressed = value; }
    void setScoreContinueHovered(bool value) override { state_.resultPresentation.continueHovered = value; }
    void sendScoreReturn() override { SendMessageW(window_, WM_KEYDOWN, VK_RETURN, 0); }
    void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }
private:
    HWND window_;
};

static RuntimePointerIngressEvent runtimePointerIngressEventForMessage(UINT message, WPARAM wParam) {
    switch (message) {
    case WM_MOUSEMOVE: return RuntimePointerIngressEvent::PointerMove;
    case WM_LBUTTONDOWN: return RuntimePointerIngressEvent::LeftButtonDown;
    case WM_LBUTTONUP: return RuntimePointerIngressEvent::LeftButtonUp;
    case WM_LBUTTONDBLCLK: return RuntimePointerIngressEvent::LeftButtonDoubleClick;
    case WM_RBUTTONDOWN: return RuntimePointerIngressEvent::RightButtonDown;
    case WM_RBUTTONUP: return RuntimePointerIngressEvent::RightButtonUp;
    case WM_RBUTTONDBLCLK: return RuntimePointerIngressEvent::RightButtonDoubleClick;
    case WM_MBUTTONDOWN: return RuntimePointerIngressEvent::MiddleButtonDown;
    case WM_MBUTTONUP: return RuntimePointerIngressEvent::MiddleButtonUp;
    case WM_MBUTTONDBLCLK: return RuntimePointerIngressEvent::MiddleButtonDoubleClick;
    case WM_XBUTTONDOWN: return RuntimePointerIngressEvent::AuxiliaryButtonDown;
    case WM_XBUTTONUP: return RuntimePointerIngressEvent::AuxiliaryButtonUp;
    case WM_XBUTTONDBLCLK: return RuntimePointerIngressEvent::AuxiliaryButtonDoubleClick;
    case WM_MOUSEWHEEL: return RuntimePointerIngressEvent::VerticalWheel;
    case WM_MOUSEHWHEEL: return RuntimePointerIngressEvent::HorizontalWheel;
    case WM_CAPTURECHANGED: return RuntimePointerIngressEvent::CaptureChanged;
    case WM_CANCELMODE: return RuntimePointerIngressEvent::CancelMode;
    case WM_KILLFOCUS: return RuntimePointerIngressEvent::FocusLost;
    case WM_ACTIVATEAPP:
        return wParam == FALSE ? RuntimePointerIngressEvent::ApplicationDeactivated
                               : RuntimePointerIngressEvent::Other;
    default: return RuntimePointerIngressEvent::Other;
    }
}
static RuntimePointerIngressInput runtimePointerIngressInputForMessage(
    UINT message, WPARAM wParam, LPARAM lParam) {
    return {runtimePointerIngressEventForMessage(message, wParam),
        runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
}
