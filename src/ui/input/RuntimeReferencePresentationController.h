#pragma once

#include "simulation/production/RuntimeProductionLanes.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct RuntimePlayerLocalCommands;

enum class RuntimeBuildKind {
    Building,
    Defense,
    Infantry,
    Unit
};

enum class RuntimeSidebarTool {
    None,
    Repair,
    Sell
};

bool runtimeBuildKindUsesSingleStructureSlot(RuntimeBuildKind kind);

struct RuntimeReferencePresentationInput {
    std::int32_t code;
    bool repeated;
};

struct RuntimeReferencePresentationReady {
    std::size_t catalogOrdinal;
    RuntimeProductionLane lane;
    RuntimeProductionLaneOrder order;
};

struct RuntimeReferencePresentationPreview {
    RuntimeInputPixelRect viewport;
    RuntimeInputPixelPoint point;
};

class RuntimeReferencePresentationHost {
public:
    virtual ~RuntimeReferencePresentationHost() = default;

    virtual std::int32_t binding(const std::string& id, std::int32_t fallback) = 0;
    virtual void setActiveBuildTab(RuntimeBuildKind kind) = 0;
    virtual RuntimeBuildKind activeBuildTab() = 0;
    virtual void clampActiveBuildPage() = 0;
    virtual std::size_t catalogSize() = 0;
    virtual RuntimeBuildKind catalogKind(std::size_t ordinal) = 0;
    virtual std::optional<RuntimeReferencePresentationReady> readyForItem(std::size_t ordinal) = 0;
    virtual bool beginReadyPlacement(const RuntimeReferencePresentationReady& ready) = 0;
    virtual RuntimeSidebarTool sidebarTool() = 0;
    virtual void setSidebarTool(RuntimeSidebarTool tool) = 0;
    virtual void setBeaconTargeting(bool targeting) = 0;
    virtual void setSelectionDragMoved(bool moved) = 0;
    virtual void setSelectionDragActive(bool active) = 0;
    virtual void placementStatus(std::size_t ordinal) = 0;
    // Compute the viewport even for an invalid mouse point. A value means the
    // original mouse-valid flag was true; the controller owns the inclusive hit.
    virtual std::optional<RuntimeReferencePresentationPreview> stationaryPreviewInput() = 0;
    virtual void previewPlacement(const RuntimeReferencePresentationPreview& preview) = 0;
    virtual void sidebarClickSound() = 0;
    virtual void requestRepaint() = 0;
    virtual bool selectionEmpty() = 0;
    virtual void centerBase() = 0;
    virtual void centerSelection() = 0;
};

bool handleRuntimeReferencePresentationKey(const RuntimeReferencePresentationInput& input,
    RuntimePlayerLocalCommands& local, RuntimeReferencePresentationHost& host);
