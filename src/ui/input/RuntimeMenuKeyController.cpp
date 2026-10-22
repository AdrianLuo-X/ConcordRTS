#include "ui/input/RuntimeMenuKeyController.h"

#include <algorithm>

namespace {
RuntimeMenuAction focusedAction(RuntimeMenuNavigationHost& host) {
    const std::vector<RuntimeMenuAction> actions = host.focusableActions();
    if (actions.empty()) {
        return RuntimeMenuAction::None;
    }
    const std::int32_t index = std::clamp(
        host.focus(), std::int32_t{0},
        static_cast<std::int32_t>(actions.size()) - 1
    );
    return actions[static_cast<std::size_t>(index)];
}
} // namespace

bool handleRuntimeMenuCaptureKey(const RuntimeMenuKeyInput& input,
                                RuntimeMenuCaptureHost& host) {
    const RuntimeMenuKeyInput key = input;
    if (!host.menuOpen()
        || host.page() != RuntimeMenuPage::Keyboard
        || host.captureIndex() < 0) {
        return false;
    }
    if (key.rawKey == 0x1B) {
        host.setCaptureIndex(-1);
        host.setCaptureStatus(RuntimeMenuCaptureStatus::Cancelled);
        return true;
    }
    if (runtimeMenuKeyModifierOnly(key.rawKey)) {
        host.setCaptureStatus(RuntimeMenuCaptureStatus::ModifierNeedsKey);
        return true;
    }
    const std::int32_t bindingIndex = host.captureIndex();
    std::int32_t keyCode = host.sampleCurrentKeyCode(key);
    bool restoredDefault = false;
    if (key.rawKey == 0x08) {
        if (bindingIndex < 0
            || bindingIndex >= static_cast<std::int32_t>(host.bindingCount())) {
            host.setCaptureIndex(-1);
            host.setCaptureStatus(RuntimeMenuCaptureStatus::Cancelled);
            return true;
        }
        keyCode = host.defaultCode(bindingIndex);
        restoredDefault = true;
    }
    (void)host.assignBinding(bindingIndex, keyCode, restoredDefault);
    return true;
}

bool handleRuntimeMenuHorizontalKey(const RuntimeMenuKeyInput& input,
                                   RuntimeMenuNavigationHost& host) {
    const RuntimeMenuKeyInput key = input;
    if (key.rawKey != 0x25 && key.rawKey != 0x27) {
        return false;
    }
    if (host.page() == RuntimeMenuPage::Keyboard) {
        host.changeKeyboardPage(key.rawKey == 0x27 ? 1 : -1,
            focusedAction(host) == RuntimeMenuAction::KeyboardCategory);
        return true;
    }
    return host.adjustSlider(focusedAction(host),
                             key.rawKey == 0x27 ? 1 : -1);
}

bool handleRuntimeMenuNavigationKey(const RuntimeMenuKeyInput& input,
                                   RuntimeMenuNavigationHost& host) {
    const RuntimeMenuKeyInput key = input;
    if (!host.menuOpen()) {
        return false;
    }
    if (host.page() == RuntimeMenuPage::Keyboard
        && (key.rawKey == 0x21 || key.rawKey == 0x22)) {
        host.changeKeyboardPage(key.rawKey == 0x21 ? -1 : 1, false);
        host.invalidate();
        return true;
    }
    host.prepareClient();
    const std::vector<RuntimeMenuAction> actions = host.focusableActions();
    if (!actions.empty()
        && (key.rawKey == 0x26 || key.rawKey == 0x28
            || key.rawKey == 0x24 || key.rawKey == 0x23)) {
        const std::int32_t count = static_cast<std::int32_t>(actions.size());
        if (key.rawKey == 0x24) {
            host.setFocus(0);
        } else if (key.rawKey == 0x23) {
            host.setFocus(count - 1);
        } else {
            // Preserve the original signed sum/remainder domain and behavior.
            host.setFocus((host.focus()
                + (key.rawKey == 0x26 ? count - 1 : 1)) % count);
        }
        host.clearHover();
        host.requestRepaint();
        host.redraw();
        return true;
    }
    if (!actions.empty() && (key.rawKey == 0x25 || key.rawKey == 0x27)) {
        if (handleRuntimeMenuHorizontalKey(key, host)) {
            host.buttonSound();
            host.clearHover();
            host.requestRepaint();
            host.redraw();
            return true;
        }
    }
    if (!actions.empty() && (key.rawKey == 0x0D || key.rawKey == 0x20)) {
        const std::int32_t index = std::clamp(
            host.focus(), std::int32_t{0},
            static_cast<std::int32_t>(actions.size()) - 1
        );
        host.activate(actions[static_cast<std::size_t>(index)]);
        return true;
    }
    return true;
}