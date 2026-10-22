#pragma once

#include "ui/input/RuntimeMenuValues.h"
#include "ui/time/RuntimeUiClock.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RuntimeSessionKeyInput {
    std::uint64_t rawKey = 0;
    bool repeated = false;
};

struct RuntimeSessionKeyBinding {
    std::string id;
    std::int32_t code = 0;
};

enum class RuntimeSessionCancelReason : std::uint8_t { Escape, Options };
enum class RuntimeSessionFileSound : std::uint8_t { Save, Load, Blocked };

class RuntimeResultSessionKeyHost {
public:
    virtual ~RuntimeResultSessionKeyHost() = default;
    virtual bool finished() = 0;
    virtual bool exitAfterPendingVideos() = 0;
    virtual bool bannerVisible() = 0;
    virtual bool campaignMode() = 0;
    virtual RuntimeUiTimeMs nowMs() = 0;
    virtual void beginScorePage(RuntimeUiTimeMs now) = 0;
    virtual bool queueCampaignPostScore() = 0;
    virtual void requestRepaint() = 0;
    virtual void setExitRequested(bool requested) = 0;
    virtual void destroyWindow() = 0;
    virtual std::size_t scoreRowCount() = 0;
    virtual std::int32_t scorePage() = 0;
    virtual void setScorePage(std::int32_t page) = 0;
};

class RuntimeOptionsSessionKeyHost {
public:
    virtual ~RuntimeOptionsSessionKeyHost() = default;
    virtual std::int32_t binding(const std::string& id, std::int32_t fallback) = 0;
    virtual std::int32_t sampleCurrentKeyCode(const RuntimeSessionKeyInput& input) = 0;
    virtual bool menuOpen() = 0;
    virtual bool beaconTargeting() = 0;
    virtual bool followTargeting() = 0;
    virtual bool rallyTargeting() = 0;
    virtual bool sidebarToolIsActive() = 0;
    virtual void setBeaconTargeting(bool active) = 0;
    virtual void setRallyTargeting(bool active) = 0;
    virtual void setFollowTargeting(bool active) = 0;
    virtual void clearSidebarTool() = 0;
    virtual void setSelectionDragMoved(bool moved) = 0;
    virtual void setSelectionDragActive(bool active) = 0;
    virtual bool ownsCapture() = 0;
    virtual void releaseCapture() = 0;
    virtual bool cancelSuperWeaponTargetSelection(RuntimeSessionCancelReason reason) = 0;
    virtual bool dismissPlacement() = 0;
    virtual RuntimeMenuPage page() = 0;
    virtual RuntimeMenuPage parentPage(RuntimeMenuPage page) = 0;
    virtual void resetPage(RuntimeMenuPage page) = 0;
    virtual void setMenuOpen(bool open) = 0;
    virtual void menuSound(bool opening) = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
    virtual void invalidate() = 0;
};

class RuntimeSaveLoadSessionKeyHost {
public:
    virtual ~RuntimeSaveLoadSessionKeyHost() = default;
    virtual std::int32_t sampleCurrentKeyCode(const RuntimeSessionKeyInput& input) = 0;
    virtual std::vector<RuntimeSessionKeyBinding> bindings() = 0;
    virtual bool dispatch(bool save, std::string& error) = 0;
    virtual bool multiplayerBridgePresent() = 0;
    virtual std::string defaultSavePath() = 0;
    virtual void setInteractionStatus(std::string status) = 0;
    virtual std::string interactionStatus() = 0;
    virtual void setEconomyStatus(std::string status) = 0;
    virtual void fileSound(RuntimeSessionFileSound sound) = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
};

bool handleRuntimeResultSessionKey(const RuntimeSessionKeyInput& input,
                                  RuntimeResultSessionKeyHost& host);
bool handleRuntimeOptionsSessionKey(const RuntimeSessionKeyInput& input,
                                   RuntimeOptionsSessionKeyHost& host);
bool handleRuntimeSaveLoadSessionKey(const RuntimeSessionKeyInput& input,
                                    bool controlDown,
                                    RuntimeSaveLoadSessionKeyHost& host);
