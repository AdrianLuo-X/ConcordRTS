#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class RuntimePointerIngressEvent : std::uint8_t {
    Other, PointerMove,
    LeftButtonDown, LeftButtonUp, LeftButtonDoubleClick,
    RightButtonDown, RightButtonUp, RightButtonDoubleClick,
    MiddleButtonDown, MiddleButtonUp, MiddleButtonDoubleClick,
    AuxiliaryButtonDown, AuxiliaryButtonUp, AuxiliaryButtonDoubleClick,
    VerticalWheel, HorizontalWheel,
    CaptureChanged, CancelMode, FocusLost, ApplicationDeactivated
};

struct RuntimePointerIngressInput {
    RuntimePointerIngressEvent event = RuntimePointerIngressEvent::Other;
    RuntimeInputPixelPoint point;
};

struct RuntimeDiplomacyPointerRow {
    std::string owner;
    bool self = false;
    bool defeated = false;
    bool human = false;
    std::optional<std::size_t> slot;
};

inline constexpr std::array<std::int32_t, 7> runtimeDiplomacyPointerChatControls{
    1750, 1751, 1752, 1753, 1762, 1763, 1764
};

// Each query reads its original synchronous root at the original call site.
// Rows and dispatch targets are owned values; no roster or world borrow crosses a sink.
class RuntimeDiplomacyPointerHost {
public:
    virtual ~RuntimeDiplomacyPointerHost() = default;
    virtual std::vector<RuntimeDiplomacyPointerRow> rows() = 0;
    virtual RuntimeInputPixelRect controlRect(RuntimeInputPixelRect client, std::int32_t id) = 0;
    virtual RuntimeInputPixelRect checkboxRect(RuntimeInputPixelRect client, std::int32_t id) = 0;
    virtual bool allianceEnabled(const RuntimeDiplomacyPointerRow& row) = 0;
    virtual void toggleAlliance(std::string target) = 0;
    virtual bool chatRecipientsAvailable() = 0;
    virtual std::uint8_t chatSlotsMask() = 0;
    virtual std::optional<std::size_t> ownChatSlot() = 0;
    virtual bool chatRecipientsExplicit() = 0;
    virtual void setChatRecipientsExplicit(bool value) = 0;
    virtual std::uint8_t chatRecipients() = 0;
    virtual void setChatRecipients(std::uint8_t value) = 0;
    virtual void setDiplomacyOpen(bool value) = 0;
    virtual void setDiplomacyRow(std::int32_t value) = 0;
    virtual std::int32_t diplomacyPressedControl() = 0;
    virtual void setDiplomacyPressedControl(std::int32_t value) = 0;
    virtual void setDiplomacyHoveredControl(std::int32_t value) = 0;
    virtual bool tauntsEnabled() = 0;
    virtual void setTauntsEnabled(bool value) = 0;
    virtual void setChatOpen(bool value) = 0;
    virtual void setBeaconEditing(bool value) = 0;
    virtual void clearChatDraft() = 0;
    virtual void setLeftActive(bool value) = 0;
    virtual void setLeftMoved(bool value) = 0;
    virtual void setRightActive(bool value) = 0;
    virtual void setRightMoved(bool value) = 0;
    virtual void requestRepaint() = 0;
};

class RuntimePointerIngressHost : public RuntimeDiplomacyPointerHost {
public:
    virtual bool finished() = 0;
    virtual bool campaignMode() = 0;
    virtual bool bannerVisible() = 0;
    virtual bool menuOpen() = 0;
    virtual bool chatOpen() = 0;
    virtual bool diplomacyOpen() = 0;
    virtual bool ownsCapture() = 0;
    virtual void releaseCapture() = 0;
    virtual void setCapture() = 0;
    virtual RuntimeInputPixelRect scoreClientRect() = 0;
    virtual RuntimeInputPixelRect diplomacyClientRect() = 0;
    virtual RuntimeInputPixelRect scoreContinueRect(RuntimeInputPixelRect client) = 0;
    virtual bool scoreContinuePressed() = 0;
    virtual void setScoreContinuePressed(bool value) = 0;
    virtual void setScoreContinueHovered(bool value) = 0;
    virtual void sendScoreReturn() = 0;
    virtual void invalidate() = 0;
};

void closeRuntimeDiplomacyPointer(RuntimeDiplomacyPointerHost& host);
void openRuntimeDiplomacyPointer(RuntimeDiplomacyPointerHost& host);
bool runtimeDiplomacyPointerChatEnabled(const RuntimeDiplomacyPointerRow& row,
    RuntimeDiplomacyPointerHost& host);
void toggleRuntimeDiplomacyPointerChat(const RuntimeDiplomacyPointerRow& row,
    RuntimeDiplomacyPointerHost& host);
std::int32_t hitRuntimeDiplomacyPointerControl(RuntimeInputPixelRect client,
    RuntimeInputPixelPoint point, RuntimeDiplomacyPointerHost& host);
void handleRuntimeDiplomacyPointer(const RuntimePointerIngressInput& input,
    RuntimeInputPixelRect client, RuntimeDiplomacyPointerHost& host);

// true preserves the original return-zero admission; false continues the native switch.
bool handleRuntimePointerIngress(const RuntimePointerIngressInput& input,
    RuntimePointerIngressHost& host);
