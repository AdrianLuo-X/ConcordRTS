#include "ui/sidebar/RuntimeSidebarPageController.h"
#include "ui/input/RuntimeReferencePresentationController.h"

#include <algorithm>

std::int32_t runtimeSidebarPageTabIndex(RuntimeBuildKind kind) {
    switch (kind) {
    case RuntimeBuildKind::Building:
        return 0;
    case RuntimeBuildKind::Defense:
        return 1;
    case RuntimeBuildKind::Infantry:
        return 2;
    case RuntimeBuildKind::Unit:
        return 3;
    }
    return 0;
}

std::int32_t runtimeSidebarPageActive(RuntimeSidebarPageQueryHost& host) {
    const std::int32_t tab = runtimeSidebarPageTabIndex(host.activeTab());
    if (tab < 0 || tab >= host.pageSlotCount()) {
        return 0;
    }
    return std::max<std::int32_t>(0, host.pageAt(tab));
}

void runtimeSidebarPageSet(RuntimeSidebarPageHost& host, std::int32_t page) {
    const std::int32_t tab = runtimeSidebarPageTabIndex(host.activeTab());
    if (tab < 0 || tab >= host.pageSlotCount()) {
        return;
    }
    host.setPageAt(tab, std::max<std::int32_t>(0, page));
}

std::int32_t runtimeSidebarPageCount(
    RuntimeSidebarPageQueryHost& host, RuntimeInputPixelRect sidebar) {
    const std::int32_t filteredCount = host.filteredCatalogCount();
    const std::int32_t superWeaponCount = host.activeTab() == RuntimeBuildKind::Defense
        ? host.visibleSuperWeaponCount()
        : 0;
    const std::int32_t perPage = host.buttonsPerPage(sidebar);
    return std::max<std::int32_t>(
        1,
        (filteredCount + superWeaponCount + perPage - 1) / perPage
    );
}

void runtimeSidebarPageClamp(
    RuntimeSidebarPageHost& host, RuntimeInputPixelRect sidebar) {
    runtimeSidebarPageSet(
        host,
        std::min(
            runtimeSidebarPageActive(host),
            runtimeSidebarPageCount(host, sidebar) - 1
        )
    );
}

bool runtimeSidebarPageScroll(
    RuntimeSidebarPageHost& host, RuntimeInputPixelRect sidebar, std::int32_t delta) {
    const std::int32_t tab = runtimeSidebarPageTabIndex(host.activeTab());
    if (tab < 0 || tab >= host.wheelSlotCount()) return false;
    const std::int32_t before = runtimeSidebarPageActive(host);
    runtimeSidebarPageClamp(host, sidebar);
    // The original reference identifies this tab before the fresh page-count
    // query; its value is not read until afterwards. Keep that fresh read.
    const std::int32_t pages = runtimeSidebarPageCount(host, sidebar);
    if (pages <= 1) {
        host.setWheelAt(tab, 0);
        return before != runtimeSidebarPageActive(host);
    }
    const std::int32_t accumulated = host.wheelAt(tab) + delta;
    constexpr std::int32_t wheelDelta = 120;
    const std::int32_t steps = accumulated / wheelDelta;
    host.setWheelAt(tab, accumulated % wheelDelta);
    const std::int32_t previous = runtimeSidebarPageActive(host);
    const std::int32_t next = std::clamp<std::int32_t>(previous - steps, 0, pages - 1);
    if (steps != 0 && next == previous) host.setWheelAt(tab, 0);
    runtimeSidebarPageSet(host, next);
    if (next == before) return false;
    host.status("build page " + std::to_string(next + 1) + "/" + std::to_string(pages));
    return true;
}

void handleRuntimeSidebarWheel(RuntimeSidebarWheelHost& host) {
    if (host.menuOpen()) return;
    const std::int32_t delta = host.wheelDelta();
    const RuntimeInputPixelRect sidebar = host.sidebarRect();
    if (!runtimeSidebarPageScroll(host, sidebar, delta)) return;
    host.requestRepaint();
    host.redraw();
}
