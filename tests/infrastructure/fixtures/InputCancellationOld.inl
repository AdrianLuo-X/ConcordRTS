bool frozenCaptureChanged(RuntimeMapRightPointerGestureHost& suppliedRight,
    RuntimeInputCaptureHost& menu) {

        {
            auto& rightHost = suppliedRight;
            captureChangedRuntimeMapRightPointer(rightHost);
        }
        if ((menu.sliderDrag() != RuntimeMenuAction::None
                || menu.pressed() != RuntimeMenuAction::None)) {
            const RuntimeMenuAction interruptedSlider =
                menu.sliderDrag();
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
