#include "ui/input/RuntimeCameraKeyController.h"

#include "ui/input/RuntimeTeamKeyController.h"

#include <algorithm>

bool handleRuntimeCameraKey(const RuntimeCameraKeyInput& input,
    RuntimeCameraKeyHost& host) {
    const RuntimeCameraKeyInput keyInput = input;
    const std::int32_t scrollStep =
        24 + (std::clamp(host.runtimeScrollRate(), std::int32_t{0}, std::int32_t{6}) * 8);
    const std::int32_t centerViewKey = host.binding("CenterView", 0x0c);
    bool handled = true;
    if (keyInput.rawKey == 0x25 || keyInput.rawKey == 0x27
        || keyInput.rawKey == 0x26 || keyInput.rawKey == 0x28)
        host.clearCameraFollow();
    if (host.sampleCurrentKeyCode(keyInput) == centerViewKey) {
        if (host.visionEnabled()
            && host.skirmishStartApplied()
            && host.skirmishStartCellX() >= 0
            && host.skirmishStartCellY() >= 0) {
            host.prepareCenterClient();
            const RuntimeTeamKeyCell cell = host.skirmishStartCell();
            host.centerCamera(cell);
        } else {
            host.resetCameraInitialized();
        }
        host.invalidateProjection();
    } else {
        switch (keyInput.rawKey) {
        case 0x25:
            host.scrollCamera(-scrollStep, 0);
            host.invalidateProjection();
            break;
        case 0x27:
            host.scrollCamera(scrollStep, 0);
            host.invalidateProjection();
            break;
        case 0x26:
            host.scrollCamera(0, -scrollStep);
            host.invalidateProjection();
            break;
        case 0x28:
            host.scrollCamera(0, scrollStep);
            host.invalidateProjection();
            break;
        default:
            handled = false;
            break;
        }
    }
    return handled;
}
