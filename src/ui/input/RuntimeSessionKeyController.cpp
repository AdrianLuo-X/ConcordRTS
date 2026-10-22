#include "ui/input/RuntimeSessionKeyController.h"

#include <algorithm>
#include <utility>

bool handleRuntimeResultSessionKey(const RuntimeSessionKeyInput& input,
                                  RuntimeResultSessionKeyHost& host) {
    const RuntimeSessionKeyInput key = input;
    if (host.finished()
        && (key.rawKey == 0x0D || key.rawKey == 'R' || key.rawKey == 0x1B)) {
        if (host.exitAfterPendingVideos()) return true;
        if (key.repeated) return true;
        if (host.bannerVisible() && (!host.campaignMode() || key.rawKey != 0x1B)) {
            host.beginScorePage(host.nowMs());
            return true;
        }
        if (host.campaignMode() && key.rawKey != 0x1B
            && host.queueCampaignPostScore()) {
            host.requestRepaint();
            return true;
        }
        host.setExitRequested(true);
        host.destroyWindow();
        return true;
    }
    if (host.finished()) {
        if (!host.campaignMode() && !host.bannerVisible()
            && (key.rawKey == 0x25 || key.rawKey == 0x27
                || key.rawKey == 0x21 || key.rawKey == 0x22)) {
            // The original signed cast, subtraction, addition and clamp domain
            // is retained; no saturation or enlarged overflow domain is added.
            const std::int32_t lastPage = std::max(std::int32_t{0},
                (static_cast<std::int32_t>(host.scoreRowCount()) - 1) / 8);
            host.setScorePage(std::clamp(host.scorePage()
                + ((key.rawKey == 0x25 || key.rawKey == 0x21) ? -1 : 1),
                std::int32_t{0}, lastPage));
            host.requestRepaint();
        }
        return true;
    }
    return false;
}

bool handleRuntimeOptionsSessionKey(const RuntimeSessionKeyInput& input,
                                   RuntimeOptionsSessionKeyHost& host) {
    const RuntimeSessionKeyInput key = input;
    const std::uint64_t optionsKey = static_cast<std::uint64_t>(
        host.binding("Options", 0x1B));
    const bool menuNavigationKey =
        host.sampleCurrentKeyCode(key) == static_cast<std::int32_t>(optionsKey)
        || (host.menuOpen() && key.rawKey == 0x1B);
    if (!menuNavigationKey) return false;
    if (key.repeated) return true;
    if (!host.menuOpen() && (host.beaconTargeting()
        || host.followTargeting() || host.rallyTargeting()
        || host.sidebarToolIsActive())) {
        host.setBeaconTargeting(false);
        host.setRallyTargeting(false);
        host.setFollowTargeting(false);
        host.clearSidebarTool();
        host.setSelectionDragMoved(false);
        host.setSelectionDragActive(false);
        if (host.ownsCapture()) host.releaseCapture();
        host.requestRepaint();
        host.invalidate();
        return true;
    }
    if (host.ownsCapture()) host.releaseCapture();
    if (!host.menuOpen() && host.cancelSuperWeaponTargetSelection(
            key.rawKey == 0x1B ? RuntimeSessionCancelReason::Escape
                              : RuntimeSessionCancelReason::Options)) {
        host.requestRepaint();
        host.redraw();
        return true;
    }
    if (!host.menuOpen() && host.dismissPlacement()) {
        host.requestRepaint();
        host.invalidate();
        return true;
    }
    bool openingMenu = false;
    if (!host.menuOpen()) {
        host.setMenuOpen(true);
        openingMenu = true;
    } else if (host.page() != RuntimeMenuPage::Main) {
        host.resetPage(host.parentPage(host.page()));
    } else {
        host.setMenuOpen(false);
    }
    host.menuSound(openingMenu);
    host.requestRepaint();
    host.redraw();
    return true;
}

bool handleRuntimeSaveLoadSessionKey(const RuntimeSessionKeyInput& input,
                                    bool controlDown,
                                    RuntimeSaveLoadSessionKeyHost& host) {
    const RuntimeSessionKeyInput key = input;
    const std::int32_t actualBindingCode = host.sampleCurrentKeyCode(key);
    const std::vector<RuntimeSessionKeyBinding> bindings = host.bindings();
    const bool configuredGameplayBinding = std::any_of(
        bindings.begin(), bindings.end(), [&](const auto& binding) {
            return binding.id != "Options" && binding.code == actualBindingCode;
        });
    if (configuredGameplayBinding
        || !((controlDown && (key.rawKey == 'S' || key.rawKey == 'L'))
            || key.rawKey == 0x74 || key.rawKey == 0x78)) return false;
    std::string errorMessage;
    const bool save = (controlDown && key.rawKey == 'S') || key.rawKey == 0x74;
    const bool ok = host.dispatch(save, errorMessage);
    const bool multiplayer = host.multiplayerBridgePresent();
    std::string status = ok
        ? (multiplayer
            ? std::string(save ? "multiplayer save proposed" : "multiplayer load proposed")
            : std::string(save ? "saved " : "loaded ") + host.defaultSavePath())
        : std::string(save ? "save failed: " : "load failed: ") + errorMessage;
    host.setInteractionStatus(std::move(status));
    if (!multiplayer) host.setEconomyStatus(host.interactionStatus());
    host.fileSound(ok ? (save ? RuntimeSessionFileSound::Save
                             : RuntimeSessionFileSound::Load)
                      : RuntimeSessionFileSound::Blocked);
    host.requestRepaint();
    host.redraw();
    return true;
}
