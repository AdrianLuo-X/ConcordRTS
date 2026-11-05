#pragma once
#include "render/core/RuntimeMapRenderer.h"

struct OriginalNativeRenderPlacement {
    POINT center{};
    POINT sortPoint{};
    RECT bodyViewport{};
    bool airborneForeground = false;
    int factoryForegroundHostId = -1;
};
