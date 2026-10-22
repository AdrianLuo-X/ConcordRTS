#pragma once

#include "ui/time/RuntimeUiClock.h"
#include <cstdint>

// These values describe the original finite message policy, not native messages.
enum class RuntimeForceAttackEvent : std::uint8_t {
    Other,
    KeyDown,
    SystemKeyDown,
    KeyUp,
    SystemKeyUp,
    KillFocus,
    ActivateApplication,
    MouseMove,
    LeftButtonDown,
    LeftButtonUp,
    PointerPressRestore
};

struct RuntimeForceAttackInput {
    RuntimeForceAttackEvent event = RuntimeForceAttackEvent::Other;
    std::uint64_t raw = 0;
};

class RuntimeForceAttackHeldHost {
public:
    virtual ~RuntimeForceAttackHeldHost() = default;
    virtual bool held() = 0;
    virtual bool controlDown() = 0;
    virtual void setHeld(bool held) = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
};

// Ordinary events read held twice, at the original initial/comparison points.
// PointerPressRestore is the separate post-focus/capture unconditional store.
void handleRuntimeForceAttackHeld(const RuntimeForceAttackInput& input,
    RuntimeForceAttackHeldHost& host);

struct RuntimeKeyboardRouterInput {
    std::int32_t code = 0;
    bool repeated = false;
    RuntimeUiTimeMs nowMs = 0;
};

// Synchronous assembly of specifically named, already-owned components.
// Construction performs no queries. No owner/selection/native reference escapes.
class RuntimeKeyboardRouterHost {
public:
    virtual ~RuntimeKeyboardRouterHost() = default;
    virtual bool menuOpen() = 0;
    virtual bool finished() = 0;
    virtual void rejectObserverSelection() = 0;
    virtual bool handleReferenceKey(std::int32_t originalCode, bool repeated) = 0;
    virtual void prepareNavigation() = 0;
    virtual bool handleObjectCycleKey(const RuntimeKeyboardRouterInput& input) = 0;
    virtual bool handleTeamKey(const RuntimeKeyboardRouterInput& input) = 0;
    virtual bool handleViewNavigationKey(const RuntimeKeyboardRouterInput& input) = 0;
    virtual bool handleQuickSelectionKey(const RuntimeKeyboardRouterInput& input) = 0;
    virtual bool handlePlanningFactoryKey(const RuntimeKeyboardRouterInput& input) = 0;
    virtual bool handleRadarNavigationKey(const RuntimeKeyboardRouterInput& input) = 0;
};

bool handleRuntimeKeyboardRouterKey(const RuntimeKeyboardRouterInput& input,
    RuntimeKeyboardRouterHost& host);
