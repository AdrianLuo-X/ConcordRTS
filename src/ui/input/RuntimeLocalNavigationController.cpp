#include "ui/input/RuntimeLocalNavigationController.h"
#include "ui/input/RuntimePlayerCommands.h"
#include <algorithm>
#include <limits>

bool handleRuntimeObjectCycleKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& currentSelection,
    RuntimeObjectCycleKeyHost& host) {
    const auto matches = [&](const std::string& action, int fallback) {
        return input.code == host.binding(action, fallback);
    };
    const bool previous=matches("PreviousObject",'M');
    if (previous || matches("NextObject",'N')) {
        if (input.repeated) return true;
        std::vector<int> ids=host.eligibleMobileObjectIds();
        if (ids.empty()) return true;
        std::sort(ids.begin(),ids.end());
        const int current=currentSelection.empty()?(previous?std::numeric_limits<int>::max():0):currentSelection.front();
        auto next=previous?std::lower_bound(ids.begin(),ids.end(),current):std::upper_bound(ids.begin(),ids.end(),current);
        if (previous) {if (next==ids.begin()) next=ids.end();--next;}
        else if (next==ids.end()) next=ids.begin();
        host.applySelection({*next});
        host.clearSelectedBeacon();
        local.cameraFollow=false;
        host.centerCurrentSelection();
        return true;
    }
    return false;
}

bool handleRuntimeViewNavigationKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, RuntimeViewNavigationKeyHost& host) {
    const auto matches = [&](const std::string& action, int fallback) {
        return input.code == host.binding(action, fallback);
    };
    constexpr int firstFunctionKey=112;
    for (int index = 0; index < 4; ++index) {
        const std::string suffix = std::to_string(index + 1);
        if (matches("SetView" + suffix, (firstFunctionKey + index) | runtimePlayerControlKey)) {
            const RuntimeTeamKeyCell point=host.currentViewportCenterCell();
            local.views[static_cast<std::size_t>(index)] = RuntimeMultiplayerGameplayCell{point.x, point.y};
            host.viewSaved(index + 1);
            return true;
        }
        if (matches("View" + suffix, firstFunctionKey + index)) {
            const auto& saved = local.views[static_cast<std::size_t>(index)];
            if (saved) host.centerCamera({saved->x, saved->y});
            return true;
        }
    }
    return false;
}

bool handleRuntimeQuickSelectionKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& currentSelection,
    RuntimeQuickSelectionKeyHost& host) {
    const auto matches = [&](const std::string& action, int fallback) {
        return input.code == host.binding(action, fallback);
    };
    const bool type = matches("TypeSelect", 'T');
    const bool combat = matches("CombatantSelect", 'P');
    const bool veteran = matches("VeterancyNav", 'Y');
    const bool health = matches("HealthNav", 'U');
    if (type || combat || veteran || health) {
        if (input.repeated) return true;
        const bool wholeMap = local.lastSelectionKey == input.code
            && runtimeUiWithinDoubleSelection(input.nowMs, local.lastSelectionAtMs);
        const std::set<int> onScreen=host.onScreenObjectIds();
        const std::vector<RuntimePlayerSelectionCandidate> candidates=host.mobileCandidates(onScreen);
        const auto selected = local.quickSelect(type ? RuntimePlayerQuickSelection::Type
            : combat ? RuntimePlayerQuickSelection::Combatant
            : health ? RuntimePlayerQuickSelection::Health : RuntimePlayerQuickSelection::Veterancy,
            currentSelection, candidates, wholeMap, false, wholeMap);
        host.applySelection(selected);
        host.clearSelectedBeacon();
        local.lastSelectionKey = input.code; local.lastSelectionAtMs = input.nowMs;
        return true;
    }
    return false;
}

bool handleRuntimeRadarNavigationKey(const RuntimeLocalNavigationInput& input,
    RuntimeViewNavigationKeyHost& host) {
    if (input.code == host.binding("CenterOnRadarEvent",32)) {
        if (const auto cell=host.latestRadarEventCell()) host.centerCamera(*cell);
        return true;
    }
    return false;
}
