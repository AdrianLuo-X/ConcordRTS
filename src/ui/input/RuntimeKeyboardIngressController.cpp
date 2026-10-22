#include "ui/input/RuntimeKeyboardIngressController.h"

bool rejectRuntimeCampaignInput(
    RuntimeCampaignInputEvent event,
    RuntimeCampaignInputAdmissionHost& host
) {
    if (host.campaignMode()
        && host.campaignInputLocked()
        && !host.finished()
        && !host.menuOpen()) {
        switch (event) {
        case RuntimeCampaignInputEvent::LeftButtonDown:
        case RuntimeCampaignInputEvent::LeftButtonUp:
        case RuntimeCampaignInputEvent::LeftButtonDoubleClick:
        case RuntimeCampaignInputEvent::RightButtonDown:
        case RuntimeCampaignInputEvent::RightButtonUp:
        case RuntimeCampaignInputEvent::RightButtonDoubleClick:
        case RuntimeCampaignInputEvent::MiddleButtonDown:
        case RuntimeCampaignInputEvent::MiddleButtonUp:
        case RuntimeCampaignInputEvent::MiddleButtonDoubleClick:
        case RuntimeCampaignInputEvent::AuxiliaryButtonDown:
        case RuntimeCampaignInputEvent::AuxiliaryButtonUp:
        case RuntimeCampaignInputEvent::AuxiliaryButtonDoubleClick:
        case RuntimeCampaignInputEvent::PointerMove:
        case RuntimeCampaignInputEvent::VerticalWheel:
        case RuntimeCampaignInputEvent::HorizontalWheel:
        case RuntimeCampaignInputEvent::KeyDown:
        case RuntimeCampaignInputEvent::KeyUp:
        case RuntimeCampaignInputEvent::Character:
            return true;
        default:
            break;
        }
    }
    return false;
}

bool handleRuntimeKeyboardIngressKey(
    const RuntimeKeyboardIngressInput& borrowed,
    RuntimeKeyboardIngressHost& host
) {
    const RuntimeKeyboardIngressInput input = borrowed;
    const bool wasDiplomacyOpen = host.diplomacyOpen();
    if (!host.menuOpen() && !host.finished()
        && host.handleModalKey(static_cast<std::int32_t>(input.rawKey), input.repeated)) {
        if (wasDiplomacyOpen && !host.diplomacyOpen() && host.captureOwned())
            host.releaseCapture();
        host.invalidate();
        return true;
    }
    if (host.handleMenuCaptureKey(input)) {
        host.requestRepaint();
        host.redraw();
        return true;
    }
    if (host.handleResultSessionKey(input)) return true;
    const bool controlDown = host.controlDown();
    if (host.handleOptionsSessionKey(input)) return true;
    if (host.handleSaveLoadSessionKey(input, controlDown)) return true;
    if (host.handleMenuNavigationKey(input)) return true;
    if (host.handleCommandKey(input)) {
        if (host.diplomacyOpen() && host.captureOwned()) host.releaseCapture();
        host.requestRepaint();
        host.invalidate();
        return true;
    }
    const auto selection = host.handleSelectionKey(input);
    if (selection == RuntimeSelectionKeyResult::RepeatedConsumed) return true;
    if (selection == RuntimeSelectionKeyResult::DispatchedConsumed) {
        host.requestRepaint();
        host.redraw();
        return true;
    }
    if (host.handleCameraKey(input)) {
        host.requestRepaint();
        host.redraw();
        return true;
    }
    return false;
}
