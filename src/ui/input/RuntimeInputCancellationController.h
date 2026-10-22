#pragma once
#include "ui/input/RuntimeMapRightPointerController.h"
#include "ui/input/RuntimeMenuValues.h"

// Queries/effects stay synchronous; private construction performs no query.
// Focus/cancel, force-held and left-up retain their existing policy owners.
class RuntimeInputCaptureHost {
public:
    virtual ~RuntimeInputCaptureHost() = default;
    virtual RuntimeMenuAction sliderDrag() = 0;
    virtual RuntimeMenuAction pressed() = 0;
    virtual void setSliderDrag(RuntimeMenuAction action) = 0;
    virtual void setPressed(RuntimeMenuAction action) = 0;
    virtual void applySliderRuntimeEffects(RuntimeMenuAction action) = 0;
    virtual void persistPreferences() = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
};

// false preserves original native default processing. Left gesture state is
// neither read nor cleared. The right owner runs before fresh menu queries.
bool handleRuntimeInputCaptureChanged(RuntimeMapRightPointerGestureHost& right,
    RuntimeInputCaptureHost& menu);
