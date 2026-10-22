#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <cstdint>
#include <string>

// The existing definition remains in RuntimeReferencePresentationController.h.
enum class RuntimeBuildKind;

class RuntimeSidebarPageQueryHost {
public:
    virtual ~RuntimeSidebarPageQueryHost() = default;
    virtual RuntimeBuildKind activeTab() = 0;
    virtual std::int32_t pageSlotCount() = 0;
    virtual std::int32_t pageAt(std::int32_t tab) = 0;
    virtual std::int32_t filteredCatalogCount() = 0;
    virtual std::int32_t visibleSuperWeaponCount() = 0;
    virtual std::int32_t buttonsPerPage(RuntimeInputPixelRect sidebar) = 0;
};

class RuntimeSidebarPageHost : public RuntimeSidebarPageQueryHost {
public:
    virtual void setPageAt(std::int32_t tab, std::int32_t page) = 0;
    virtual std::int32_t wheelSlotCount() = 0;
    virtual std::int32_t wheelAt(std::int32_t tab) = 0;
    virtual void setWheelAt(std::int32_t tab, std::int32_t remainder) = 0;
    virtual void status(const std::string& text) = 0;
};

class RuntimeSidebarWheelHost : public RuntimeSidebarPageHost {
public:
    virtual bool menuOpen() = 0;
    virtual std::int32_t wheelDelta() = 0;
    virtual RuntimeInputPixelRect sidebarRect() = 0;
    virtual void requestRepaint() = 0;
    virtual void redraw() = 0;
};

// The original signed 32-bit sums/differences and positive buttons-per-page
// domain must be representable. No widened or saturating arithmetic is added.
std::int32_t runtimeSidebarPageTabIndex(RuntimeBuildKind kind);
std::int32_t runtimeSidebarPageActive(RuntimeSidebarPageQueryHost& host);
void runtimeSidebarPageSet(RuntimeSidebarPageHost& host, std::int32_t page);
std::int32_t runtimeSidebarPageCount(
    RuntimeSidebarPageQueryHost& host, RuntimeInputPixelRect sidebar);
void runtimeSidebarPageClamp(
    RuntimeSidebarPageHost& host, RuntimeInputPixelRect sidebar);
bool runtimeSidebarPageScroll(
    RuntimeSidebarPageHost& host, RuntimeInputPixelRect sidebar, std::int32_t delta);

// A non-null native state consumes every wheel event. Menu admission precedes
// delta/client queries, and repaint/redraw occur only when Scroll returns true.
void handleRuntimeSidebarWheel(RuntimeSidebarWheelHost& host);
