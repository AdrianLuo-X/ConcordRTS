#include "ui/input/RuntimeKeyboardRouterController.h"

void handleRuntimeForceAttackHeld(const RuntimeForceAttackInput& input,
                                 RuntimeForceAttackHeldHost& host) {
    const RuntimeForceAttackInput key = input;
    if (key.event == RuntimeForceAttackEvent::PointerPressRestore) {
        host.setHeld((key.raw & 0x0008) != 0);
        return;
    }

    bool forceAttackHeld = host.held();
    if ((key.event == RuntimeForceAttackEvent::KeyDown
            || key.event == RuntimeForceAttackEvent::SystemKeyDown)
        && (key.raw == 0x11 || key.raw == 0xA2 || key.raw == 0xA3)) {
        forceAttackHeld = true;
    } else if ((key.event == RuntimeForceAttackEvent::KeyUp
            || key.event == RuntimeForceAttackEvent::SystemKeyUp)
        && (key.raw == 0x11 || key.raw == 0xA2 || key.raw == 0xA3)) {
        forceAttackHeld = host.controlDown();
    } else if (key.event == RuntimeForceAttackEvent::KillFocus
        || (key.event == RuntimeForceAttackEvent::ActivateApplication && key.raw == 0)) {
        forceAttackHeld = false;
    } else if (key.event == RuntimeForceAttackEvent::MouseMove
        || key.event == RuntimeForceAttackEvent::LeftButtonDown
        || key.event == RuntimeForceAttackEvent::LeftButtonUp) {
        forceAttackHeld = (key.raw & 0x0008) != 0;
    }
    if (forceAttackHeld != host.held()) {
        host.setHeld(forceAttackHeld);
        host.requestRepaint();
        host.redraw();
    }
}

bool handleRuntimeKeyboardRouterKey(const RuntimeKeyboardRouterInput& input,
                                   RuntimeKeyboardRouterHost& host) {
    const RuntimeKeyboardRouterInput key = input;
    if (host.menuOpen() || host.finished()) return false;
    host.rejectObserverSelection();
    if (host.handleReferenceKey(key.code, key.repeated)) return true;

    RuntimeKeyboardRouterInput navigation = key;
    const std::int32_t low = navigation.code & 0xFF;
    if (low >= 0x60 && low <= 0x69)
        navigation.code = (navigation.code & ~0xFF) | ('0' + low - 0x60);
    host.prepareNavigation();
    if (host.handleObjectCycleKey(navigation)) return true;
    if (host.handleTeamKey(navigation)) return true;
    if (host.handleViewNavigationKey(navigation)) return true;
    if (host.handleQuickSelectionKey(navigation)) return true;
    if (host.handlePlanningFactoryKey(navigation)) return true;
    if (host.handleRadarNavigationKey(navigation)) return true;
    return false;
}
