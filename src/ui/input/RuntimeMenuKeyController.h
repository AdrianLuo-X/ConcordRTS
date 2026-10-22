#pragma once

#include "ui/input/RuntimeMenuValues.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct RuntimeMenuKeyInput {
    std::uint64_t rawKey = 0;
    bool repeated = false;
};

enum class RuntimeMenuCaptureStatus : std::uint8_t {
    Cancelled,
    ModifierNeedsKey
};

constexpr bool runtimeMenuKeyModifierOnly(std::uint64_t rawKey) noexcept {
    switch (rawKey) {
    case 0x10: // SHIFT
    case 0xA0: // LSHIFT
    case 0xA1: // RSHIFT
    case 0x11: // CONTROL
    case 0xA2: // LCONTROL
    case 0xA3: // RCONTROL
    case 0x12: // MENU
    case 0xA4: // LMENU
    case 0xA5: // RMENU
    case 0x5B: // LWIN
    case 0x5C: // RWIN
        return true;
    default:
        return false;
    }
}

class RuntimeMenuCaptureHost {
public:
    virtual ~RuntimeMenuCaptureHost() = default;
    virtual bool menuOpen() = 0;
    virtual RuntimeMenuPage page() = 0;
    virtual std::int32_t captureIndex() = 0;
    virtual std::int32_t sampleCurrentKeyCode(const RuntimeMenuKeyInput& input) = 0;
    virtual std::size_t bindingCount() = 0;
    virtual std::int32_t defaultCode(std::int32_t index) = 0;
    virtual void setCaptureIndex(std::int32_t index) = 0;
    virtual void setCaptureStatus(RuntimeMenuCaptureStatus status) = 0;
    virtual bool assignBinding(std::int32_t index, std::int32_t code,
                               bool restoredDefault) = 0;
};

class RuntimeMenuNavigationHost {
public:
    virtual ~RuntimeMenuNavigationHost() = default;
    virtual bool menuOpen() = 0;
    virtual RuntimeMenuPage page() = 0;
    virtual void prepareClient() = 0;
    virtual std::vector<RuntimeMenuAction> focusableActions() = 0;
    virtual std::int32_t focus() = 0;
    virtual void setFocus(std::int32_t focus) = 0;
    virtual void changeKeyboardPage(std::int32_t direction, bool byCategory) = 0;
    virtual bool adjustSlider(RuntimeMenuAction action, std::int32_t direction) = 0;
    virtual void clearHover() = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
    virtual void invalidate() = 0;
    virtual void buttonSound() = 0;
    virtual void activate(RuntimeMenuAction action) = 0;
};

bool handleRuntimeMenuCaptureKey(const RuntimeMenuKeyInput& input,
                                RuntimeMenuCaptureHost& host);
bool handleRuntimeMenuHorizontalKey(const RuntimeMenuKeyInput& input,
                                   RuntimeMenuNavigationHost& host);
bool handleRuntimeMenuNavigationKey(const RuntimeMenuKeyInput& input,
                                   RuntimeMenuNavigationHost& host);