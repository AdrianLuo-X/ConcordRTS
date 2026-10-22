#include "ui/input/RuntimeCursorController.h"
#include <cctype>
#include <optional>

namespace {
std::string lowerCursorAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}
std::string trimCursorAscii(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}
} // namespace

RuntimeCursorRequest runtimeCursorForInputPoint(RuntimeCursorPointHost& host,
    RuntimeInputPixelRect client, RuntimeInputPixelPoint point) {
    const auto sidebar = host.sidebarRect(client);
    if (runtimeInputPixelPointInRectInclusive(sidebar, point)) {
        if (host.superWeaponAtPoint(sidebar, point)
            || host.buildButtonAtPoint(sidebar, point)
            || host.buildPageButtonAtPoint(sidebar, point) != 0
            || host.buildTabAtPoint(sidebar, point)
            || host.sidebarToolAtPoint(sidebar, point)
            || host.radarCellAtPoint(host.radarRect(sidebar), point)) {
            return RuntimeCursorRequest::StockHand;
        }
        return RuntimeCursorRequest::StockArrow;
    }
    const auto viewport = host.mapViewport(client);
    if (!runtimeInputPixelPointInRectInclusive(viewport, point))
        return RuntimeCursorRequest::StockArrow;
    if (host.selectedPlacementPresent())
        return host.placementValid() ? RuntimeCursorRequest::StockAction
            : RuntimeCursorRequest::StockForbidden;
    if (host.activeSidebarToolPresent()) return RuntimeCursorRequest::StockAction;
    if (host.selectedMovableCount() > 0) return RuntimeCursorRequest::StockAction;
    return RuntimeCursorRequest::StockArrow;
}

bool handleRuntimeCursor(bool clientArea, RuntimeCursorHost& host) {
    if (!clientArea) return false;
    if (!host.acquireWindowState() || !host.worldLoaded()) return false;
    if (host.menuOpen() || host.finished() || host.diplomacyOpen()) {
        host.setCursorRequest(RuntimeCursorRequest::SystemArrow);
        return true;
    }
    const auto point = host.cursorPoint();
    const auto client = host.clientRect();
    if (runtimeInputPixelPointInRectInclusive(host.mapViewport(client), point)) {
        host.ensureSuperWeaponsInitialized();
        if (host.activeLocalSuperWeaponTargeting()) {
            host.setCursorRequest(RuntimeCursorRequest::Hidden);
            return true;
        }
        const auto viewport = host.mapViewport(client);
        host.beginRouterSuperWeaponObservation();
        const std::string owner = lowerCursorAscii(trimCursorAscii(host.economyOwner()));
        const auto snapshots = host.superWeaponSnapshots();
        std::optional<RuntimeCursorSuperWeaponRow> active;
        for (const auto& snapshot : snapshots) {
            const bool localMultiplayerTargeting = host.usesIntentRouter()
                && host.routerSuperWeaponPresent()
                && lowerCursorAscii(snapshot.id) == lowerCursorAscii(host.routerSuperWeaponId());
            if ((snapshot.targetSelectionActive || localMultiplayerTargeting)
                && (owner.empty() || lowerCursorAscii(trimCursorAscii(snapshot.owner)) == owner)) {
                active = snapshot;
                break;
            }
        }
        if (active) {
            const auto cell = host.surfaceCell(viewport, point);
            std::int32_t objectId = -1;
            host.prepareAndPickProjection(viewport, point);
            if (host.pickedProjectionPresent() && host.pickedObjectPresent()
                && !host.superWeaponTargetsGroundArea(active->id))
                objectId = host.pickedObjectId();
            const bool valid = host.previewSuperWeaponTarget(active->owner, active->id, cell, objectId);
            host.setCursorRequest(valid ? RuntimeCursorRequest::StockAction
                : RuntimeCursorRequest::StockForbidden);
            return true;
        }
        host.setCursorRequest(RuntimeCursorRequest::Hidden);
        return true;
    }
    host.setCursorRequest(runtimeCursorForInputPoint(host, client, point));
    return true;
}
