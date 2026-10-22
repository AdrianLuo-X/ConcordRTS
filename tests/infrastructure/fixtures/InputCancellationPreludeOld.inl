void frozenForceAttackHeld(const RuntimeForceAttackInput& input,
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

void frozenCancelRightPointer(
    RuntimeMapRightPointerCancelEvent event, RuntimeMapRightPointerGestureHost& host) {
    const auto ownedEvent = event;
    if (!host.gestureState().active
        || !(ownedEvent == RuntimeMapRightPointerCancelEvent::FocusLost
            || ownedEvent == RuntimeMapRightPointerCancelEvent::CancelMode
            || ownedEvent == RuntimeMapRightPointerCancelEvent::ApplicationDeactivated)) return;
    host.gestureState().active = false;
    host.gestureState().moved = false;
    host.requestRepaint();
    if (host.ownsCapture()) host.releaseCapture();
}

