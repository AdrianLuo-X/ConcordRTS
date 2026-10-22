#pragma once
#include "ui/input/RuntimeCursorController.h"
#include <cctype>

// Independent complete old literals, mechanically adapted only to narrow ports.
// This file never calls the new controller/helper implementation.
namespace RuntimeCursorFrozenOld {
inline RuntimeCursorRequest stockRequest(int resource) {
    if (resource == 32649) return RuntimeCursorRequest::StockHand;
    if (resource == 32515) return RuntimeCursorRequest::StockAction;
    if (resource == 32648) return RuntimeCursorRequest::StockForbidden;
    return RuntimeCursorRequest::StockArrow;
}
inline std::string toLowerAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

inline std::string trimAsciiLocal(const std::string& value) {
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

inline RuntimeCursorRequest cursorForPoint(
    RuntimeCursorPointHost& host,
    const RuntimeInputPixelRect& clientRect,
    RuntimeInputPixelPoint point
) {
    const auto sidebar = host.sidebarRect(clientRect);
    if (runtimeInputPixelPointInRectInclusive(sidebar, point)) {
        if (host.superWeaponAtPoint(sidebar, point)
            || host.buildButtonAtPoint(sidebar, point)
            || host.buildPageButtonAtPoint(sidebar, point) != 0
            || host.buildTabAtPoint(sidebar, point)
            || host.sidebarToolAtPoint(sidebar, point)
            || host.radarCellAtPoint(host.radarRect(sidebar), point)) {
            return stockRequest(32649);
        }
        return stockRequest(32512);
    }

    const auto viewport = host.mapViewport(clientRect);
    if (!runtimeInputPixelPointInRectInclusive(viewport, point)) {
        return stockRequest(32512);
    }
    if (host.selectedPlacementPresent()) {
        return stockRequest(
            host.placementValid() ? 32515 : 32648
        );
    }
    if (host.activeSidebarToolPresent()) {
        return stockRequest(32515);
    }
    if (host.selectedMovableCount() > 0) {
        return stockRequest(32515);
    }
    return stockRequest(32512);
}

inline bool setCursor(bool clientArea, RuntimeCursorHost& host) {
        if (!clientArea) {
            return false;
        }
        if (!host.acquireWindowState() || !host.worldLoaded()) {
            return false;
        }
        if (host.menuOpen() || host.finished() || host.diplomacyOpen()) {
            host.setCursorRequest(RuntimeCursorRequest::SystemArrow);
            return true;
        }

        const auto point = host.cursorPoint();
        const auto clientRect = host.clientRect();
        if (runtimeInputPixelPointInRectInclusive(host.mapViewport(clientRect), point)) {
            host.ensureSuperWeaponsInitialized();
            if (host.activeLocalSuperWeaponTargeting()) {
                host.setCursorRequest(RuntimeCursorRequest::Hidden);
                return true;
            }
            const auto viewport = host.mapViewport(clientRect);
            const RuntimeCursorSuperWeaponRow* activeSuperWeapon = nullptr;
            host.beginRouterSuperWeaponObservation();
            const std::string owner = toLowerAsciiLocal(
                trimAsciiLocal(host.economyOwner())
            );
            const auto ownedSnapshots = host.superWeaponSnapshots();
            for (const RuntimeCursorSuperWeaponRow& snapshot : ownedSnapshots) {
                const bool localMultiplayerTargeting =
                    host.usesIntentRouter()
                    && host.routerSuperWeaponPresent()
                    && toLowerAsciiLocal(snapshot.id)
                        == toLowerAsciiLocal(
                            host.routerSuperWeaponId()
                        );
                if ((snapshot.targetSelectionActive
                        || localMultiplayerTargeting)
                    && (owner.empty()
                        || toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                            == owner)) {
                    activeSuperWeapon = &snapshot;
                    break;
                }
            }
            if (activeSuperWeapon != nullptr) {
                const auto targetPoint = host.surfaceCell(viewport, point);
                int objectId = -1;
                host.prepareAndPickProjection(viewport, point);
                if (host.pickedProjectionPresent()
                    && host.pickedObjectPresent()
                    && !host.superWeaponTargetsGroundArea(activeSuperWeapon->id)) {
                    objectId = host.pickedObjectId();
                }
                const bool valid = host.previewSuperWeaponTarget(
                    activeSuperWeapon->owner, activeSuperWeapon->id, targetPoint, objectId);
                host.setCursorRequest(valid ? RuntimeCursorRequest::StockAction : RuntimeCursorRequest::StockForbidden);
                return true;
            }
            host.setCursorRequest(RuntimeCursorRequest::Hidden);
            return true;
        }
        host.setCursorRequest(cursorForPoint(host, clientRect, point));
        return true;
    }

} // namespace RuntimeCursorFrozenOld
