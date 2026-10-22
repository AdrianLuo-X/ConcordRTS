#include "ui/input/RuntimeReferencePresentationController.h"
#include "ui/input/RuntimePlayerCommands.h"

#include <utility>

bool runtimeBuildKindUsesSingleStructureSlot(RuntimeBuildKind kind) {
    return kind == RuntimeBuildKind::Building
        || kind == RuntimeBuildKind::Defense;
}

bool handleRuntimeReferencePresentationKey(const RuntimeReferencePresentationInput& input,
    RuntimePlayerLocalCommands& local, RuntimeReferencePresentationHost& host) {
    const auto matches = [&](const std::string& id, int fallback) {
        return input.code == host.binding(id, fallback);
    };
    const std::pair<const char*, RuntimeBuildKind> tabs[] = {
        {"StructureTab", RuntimeBuildKind::Building}, {"DefenseTab", RuntimeBuildKind::Defense},
        {"InfantryTab", RuntimeBuildKind::Infantry}, {"UnitTab", RuntimeBuildKind::Unit}};
    constexpr int tabKeys[] = {'Q','W','E','R'};
    for (int i = 0; i < 4; ++i) if (matches(tabs[i].first, tabKeys[i])) {
        host.setActiveBuildTab(tabs[i].second);
        host.clampActiveBuildPage();
        if (!input.repeated && runtimeBuildKindUsesSingleStructureSlot(host.activeBuildTab())) {
            const auto catalogSize = host.catalogSize();
            for (std::size_t ordinal = 0; ordinal < catalogSize; ++ordinal) {
                const auto kind = host.catalogKind(ordinal);
                if (kind != host.activeBuildTab()) continue;
                const auto ready = host.readyForItem(ordinal);
                if (!ready) continue;
                if (host.beginReadyPlacement(*ready)) {
                    host.setSidebarTool(RuntimeSidebarTool::None);
                    host.setBeaconTargeting(false);
                    local.rallyTargeting = false;
                    local.followTargeting = false;
                    host.setSelectionDragMoved(false);
                    host.setSelectionDragActive(false);
                    host.placementStatus(ordinal);
                    const auto preview = host.stationaryPreviewInput();
                    if (preview && runtimeInputPixelPointInRectInclusive(preview->viewport, preview->point)) {
                        host.previewPlacement(*preview);
                    }
                    host.sidebarClickSound();
                    host.requestRepaint();
                }
                break;
            }
        }
        return true;
    }
    if (matches("ToggleRepair", 'K') || matches("ToggleSell", 'L')) {
        if (!input.repeated) {
            const auto tool = matches("ToggleRepair", 'K') ? RuntimeSidebarTool::Repair : RuntimeSidebarTool::Sell;
            host.setSidebarTool(host.sidebarTool() == tool ? RuntimeSidebarTool::None : tool);
            host.setBeaconTargeting(false);
            local.rallyTargeting = false;
            local.followTargeting = false;
        }
        return true;
    }
    if (matches("CenterBase", 'H')) { host.centerBase(); return true; }
    if (matches("ShowCoordinates", 'O')) {
        if (!input.repeated) local.showCoordinates = !local.showCoordinates;
        return true;
    }
    if (matches("Follow", 'F')) {
        if (!input.repeated) local.cameraFollow = !local.cameraFollow && !host.selectionEmpty();
        if (local.cameraFollow) host.centerSelection();
        return true;
    }
    return false;
}
