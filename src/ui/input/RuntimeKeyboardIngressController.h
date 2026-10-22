#pragma once

#include "ui/input/RuntimeSelectionKeyController.h"

#include <cstdint>

enum class RuntimeCampaignInputEvent : std::uint8_t {
    Other,
    LeftButtonDown,
    LeftButtonUp,
    LeftButtonDoubleClick,
    RightButtonDown,
    RightButtonUp,
    RightButtonDoubleClick,
    MiddleButtonDown,
    MiddleButtonUp,
    MiddleButtonDoubleClick,
    AuxiliaryButtonDown,
    AuxiliaryButtonUp,
    AuxiliaryButtonDoubleClick,
    PointerMove,
    VerticalWheel,
    HorizontalWheel,
    KeyDown,
    KeyUp,
    Character,
    SystemKeyDown,
    SystemKeyUp
};

// The native owner checks for a live root before lending this synchronous host.
class RuntimeCampaignInputAdmissionHost {
public:
    virtual ~RuntimeCampaignInputAdmissionHost() = default;
    virtual bool campaignMode() = 0;
    virtual bool campaignInputLocked() = 0;
    virtual bool finished() = 0;
    virtual bool menuOpen() = 0;
};

// The adapter classifies messages; this controller owns the finite admission policy.
bool rejectRuntimeCampaignInput(
    RuntimeCampaignInputEvent event,
    RuntimeCampaignInputAdmissionHost& host
);

struct RuntimeKeyboardIngressInput {
    std::uint64_t rawKey;
    bool repeated;
    RuntimeKeyDownSource source;
};

// Child calls stay in their existing owners. No host/reference escapes the event.
class RuntimeKeyboardIngressHost {
public:
    virtual ~RuntimeKeyboardIngressHost() = default;
    virtual bool diplomacyOpen() = 0;
    virtual bool menuOpen() = 0;
    virtual bool finished() = 0;
    virtual bool captureOwned() = 0;
    virtual bool controlDown() = 0;
    virtual void releaseCapture() = 0;
    virtual void invalidate() = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;

    virtual bool handleModalKey(std::int32_t rawKey, bool repeated) = 0;
    virtual bool handleMenuCaptureKey(const RuntimeKeyboardIngressInput& input) = 0;
    virtual bool handleResultSessionKey(const RuntimeKeyboardIngressInput& input) = 0;
    virtual bool handleOptionsSessionKey(const RuntimeKeyboardIngressInput& input) = 0;
    virtual bool handleSaveLoadSessionKey(
        const RuntimeKeyboardIngressInput& input,
        bool controlDown
    ) = 0;
    virtual bool handleMenuNavigationKey(const RuntimeKeyboardIngressInput& input) = 0;
    virtual bool handleCommandKey(const RuntimeKeyboardIngressInput& input) = 0;
    virtual RuntimeSelectionKeyResult handleSelectionKey(
        const RuntimeKeyboardIngressInput& input
    ) = 0;
    virtual bool handleCameraKey(const RuntimeKeyboardIngressInput& input) = 0;
};

bool handleRuntimeKeyboardIngressKey(
    const RuntimeKeyboardIngressInput& input,
    RuntimeKeyboardIngressHost& host
);
