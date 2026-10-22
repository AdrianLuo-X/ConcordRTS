#include "ui/input/RuntimeInputCancellationController.h"

bool handleRuntimeInputCaptureChanged(RuntimeMapRightPointerGestureHost& right,
    RuntimeInputCaptureHost& menu) {
    captureChangedRuntimeMapRightPointer(right);
    if (menu.sliderDrag() != RuntimeMenuAction::None
        || menu.pressed() != RuntimeMenuAction::None) {
        const RuntimeMenuAction interruptedSlider = menu.sliderDrag();
        menu.setSliderDrag(RuntimeMenuAction::None);
        menu.setPressed(RuntimeMenuAction::None);
        if (interruptedSlider != RuntimeMenuAction::None) {
            menu.applySliderRuntimeEffects(interruptedSlider);
            menu.persistPreferences();
        }
        menu.requestRepaint();
        menu.redraw();
        return true;
    }
    return false;
}
