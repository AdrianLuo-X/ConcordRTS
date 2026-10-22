#include "ui/input/RuntimeInputCancellationController.h"

// This private observer only records original values and effects. With no
// diagnostic attached the ordinary production path performs the original APIs.
class RuntimeInputCancellationDiagnostics {
public:
    virtual ~RuntimeInputCancellationDiagnostics() = default;
    virtual void observe(const char* operation, RuntimeMenuAction value) = 0;
};
thread_local RuntimeInputCancellationDiagnostics* runtimeInputCancellationDiagnostics = nullptr;
void runtimeObserveInputCancellation(const char* operation,
    RuntimeMenuAction value = RuntimeMenuAction::None) {
    if (runtimeInputCancellationDiagnostics)
        runtimeInputCancellationDiagnostics->observe(operation, value);
}
RuntimeMenuAction runtimeObserveCancellationSlider(RuntimeWindowState& state) {
    const auto value = state.runtimeMenuSliderDrag;
    runtimeObserveInputCancellation("slider", value);
    return value;
}
RuntimeMenuAction runtimeObserveCancellationPressed(RuntimeWindowState& state) {
    const auto value = state.runtimeMenuPressed;
    runtimeObserveInputCancellation("pressed", value);
    return value;
}
class RuntimeInputCaptureUiHost final : public RuntimeInputCaptureHost {
public:
    RuntimeInputCaptureUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    RuntimeMenuAction sliderDrag() override { return runtimeObserveCancellationSlider(state_); }
    RuntimeMenuAction pressed() override { return runtimeObserveCancellationPressed(state_); }
    void setSliderDrag(RuntimeMenuAction value) override {
        runtimeObserveInputCancellation("setSlider", value);
        state_.runtimeMenuSliderDrag = value;
    }
    void setPressed(RuntimeMenuAction value) override {
        runtimeObserveInputCancellation("setPressed", value);
        state_.runtimeMenuPressed = value;
    }
    void applySliderRuntimeEffects(RuntimeMenuAction action) override {
        runtimeObserveInputCancellation("effects", action);
        applyRuntimeMenuSliderRuntimeEffects(state_, action);
    }
    void persistPreferences() override {
        runtimeObserveInputCancellation("preferences");
        (void)persistRuntimeMenuPreferences(state_);
    }
    void requestRepaint() override {
        runtimeObserveInputCancellation("repaint");
        state_.repaintRequested = true;
    }
    void redraw() override {
        runtimeObserveInputCancellation("redraw");
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
private:
    RuntimeWindowState& state_;
    HWND window_;
};
