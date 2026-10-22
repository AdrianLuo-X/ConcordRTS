#pragma once

#include "ui/input/RuntimePlayerCommunication.h"
#include "ui/input/RuntimeReferencePresentationController.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RuntimePlayerLocalCommands;

struct RuntimeReferenceCommunicationInput {
    std::int32_t code = 0;
    bool repeated = false;
};

struct RuntimeCommunicationDiplomacyRow {
    std::string owner;
    bool self = false;
    bool defeated = false;
};

// Synchronous root queries and sinks. Returned owners, beacons and rows are owned
// values; no object/roster/beacon pointer is held across a dispatch or another key.
class RuntimeCommunicationAllianceHost {
public:
    virtual ~RuntimeCommunicationAllianceHost() = default;
    virtual std::string currentOwner() = 0;
    virtual std::string canonicalOwner(const std::string& owner) = 0;
    virtual std::optional<bool> allianceOffer(const std::string& source,
        const std::string& target) = 0;
    virtual bool ownersAllied(const std::string& source, const std::string& target) = 0;
    virtual void dispatchCommunication(
        RuntimeMultiplayerGameplayPlayerCommunicationCommand command) = 0;
};

class RuntimeReferenceCommunicationHost : public RuntimeCommunicationAllianceHost {
public:
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual std::optional<std::string> objectOwner(std::int32_t objectId) = 0;
    virtual void openDiplomacy() = 0;
    virtual std::optional<RuntimePlayerBeacon> selectedBeacon() = 0;
    virtual void setChatDraftUtf8(const std::string& text) = 0;
    virtual std::string viewerOwner() = 0;
    virtual bool beaconVisible(const RuntimePlayerBeacon& beacon) = 0;
    virtual void cancelSuperWeapon(const std::string& reason) = 0;
    virtual void dismissPlacement() = 0;
    virtual void setSidebarTool(RuntimeSidebarTool tool) = 0;
    virtual void setSelectionDragMoved(bool moved) = 0;
    virtual void setSelectionDragActive(bool active) = 0;
    virtual void beaconLimitStatus() = 0;
    virtual void dispatchWaypoint(RuntimePlayerWaypointOperation operation,
        std::uint64_t waypointId) = 0;
};

class RuntimeDiplomacyNavigationHost : public RuntimeCommunicationAllianceHost {
public:
    virtual std::vector<RuntimeCommunicationDiplomacyRow> rows() = 0;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual void closePage() = 0;
    virtual bool allianceEnabled(const RuntimeCommunicationDiplomacyRow& row) = 0;
    virtual void requestRepaint() = 0;
};

// The Reference presentation prefix and the original chat modal branch keep
// their existing outer priority. These entries replace only their frozen tails.
bool handleRuntimeReferenceCommunicationKey(const RuntimeReferenceCommunicationInput& input,
    RuntimePlayerLocalCommands& local, RuntimePlayerCommunicationState& ui,
    const std::vector<int>& selection, RuntimeReferenceCommunicationHost& host);
bool handleRuntimeDiplomacyNavigationKey(const RuntimeReferenceCommunicationInput& input,
    RuntimePlayerCommunicationState& ui, RuntimeDiplomacyNavigationHost& host);
void toggleRuntimeCommunicationAlliance(const std::string& target,
    RuntimeCommunicationAllianceHost& host);
