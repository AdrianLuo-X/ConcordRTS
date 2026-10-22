#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <string>
#include <vector>

// Semantic requests; only the private adapter owns native cursor resources.
enum class RuntimeCursorRequest : std::uint8_t {
    Hidden, SystemArrow, StockArrow, StockAction, StockForbidden, StockHand
};

struct RuntimeCursorSuperWeaponRow {
    std::string owner;
    std::string id;
    bool targetSelectionActive = false;
};

class RuntimeCursorPointHost {
public:
    virtual ~RuntimeCursorPointHost() = default;
    virtual RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) = 0;
    virtual RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) = 0;
    virtual bool superWeaponAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual bool buildButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual std::int32_t buildPageButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual bool buildTabAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual bool sidebarToolAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) = 0;
    virtual RuntimeInputPixelRect radarRect(RuntimeInputPixelRect sidebar) = 0;
    virtual bool radarCellAtPoint(RuntimeInputPixelRect radar, RuntimeInputPixelPoint point) = 0;
    virtual bool selectedPlacementPresent() = 0;
    virtual bool placementValid() = 0;
    virtual bool activeSidebarToolPresent() = 0;
    virtual std::int32_t selectedMovableCount() = 0;
};

// Each method runs at the corresponding original query/effect position.
// Native construction performs no query. Values crossing this interface are owned.
class RuntimeCursorHost : public RuntimeCursorPointHost {
public:
    virtual bool acquireWindowState() = 0;
    virtual bool worldLoaded() = 0;
    virtual bool menuOpen() = 0;
    virtual bool finished() = 0;
    virtual bool diplomacyOpen() = 0;
    virtual RuntimeInputPixelPoint cursorPoint() = 0;
    virtual RuntimeInputPixelRect clientRect() = 0;
    virtual void ensureSuperWeaponsInitialized() = 0;
    virtual bool activeLocalSuperWeaponTargeting() = 0;
    // The first call preserves the original accessor position. The later reads
    // observe the same stable root member freshly, never a borrowed public value.
    virtual void beginRouterSuperWeaponObservation() = 0;
    virtual bool routerSuperWeaponPresent() = 0;
    virtual std::string routerSuperWeaponId() = 0;
    virtual std::string economyOwner() = 0;
    virtual std::vector<RuntimeCursorSuperWeaponRow> superWeaponSnapshots() = 0;
    virtual bool usesIntentRouter() = 0;
    virtual RuntimeInputPixelPoint surfaceCell(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual void prepareAndPickProjection(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) = 0;
    virtual bool pickedProjectionPresent() = 0;
    virtual bool pickedObjectPresent() = 0;
    virtual bool superWeaponTargetsGroundArea(const std::string& id) = 0;
    virtual std::int32_t pickedObjectId() = 0;
    // The native implementation builds the actual context immediately and calls
    // the actual system synchronously. No context or world borrow escapes.
    virtual bool previewSuperWeaponTarget(const std::string& owner, const std::string& id,
        RuntimeInputPixelPoint cell, std::int32_t objectId) = 0;
    virtual void setCursorRequest(RuntimeCursorRequest request) = 0;
};

RuntimeCursorRequest runtimeCursorForInputPoint(RuntimeCursorPointHost& host,
    RuntimeInputPixelRect client, RuntimeInputPixelPoint point);
// false continues the original DefWindowProc; true preserves native TRUE.
bool handleRuntimeCursor(bool clientArea, RuntimeCursorHost& host);
