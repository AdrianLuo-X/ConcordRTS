#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RuntimePlayerLocalCommands;

struct RuntimePlanningFactoryKeyInput {
    std::int32_t code = 0;
    bool repeated = false;
};

// An owned request remains stable across synchronous authority/dispatch callbacks.
struct RuntimePlayerFactoryRequest {
    std::int32_t objectId = 0;
    std::optional<RuntimeMultiplayerGameplayCell> cell;
};

class RuntimePlanningFactoryKeyHost {
public:
    virtual ~RuntimePlanningFactoryKeyHost() = default;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual void dispatchWaypoint(RuntimePlayerWaypointOperation operation,
        std::uint64_t waypointId) = 0;
    virtual void dispatchFactory(RuntimePlayerFactoryRequest request) = 0;
    virtual void setStatus(const std::string& status) = 0;
};

// Each query/sink borrows the current root synchronously. Concrete adapters keep
// validation, authoritative apply, native sound and repaint ownership in place.
class RuntimePlayerFactoryHost {
public:
    virtual ~RuntimePlayerFactoryHost() = default;
    virtual bool usesIntentRouter() = 0;
    virtual void refreshAuthority() = 0;
    virtual bool submitPrimary(std::int32_t objectId, std::string& error) = 0;
    virtual bool submitRally(std::int32_t objectId,
        RuntimeMultiplayerGameplayCell cell, std::string& error) = 0;
    virtual bool applyPrimary(std::int32_t objectId) = 0;
    virtual bool applyRally(std::int32_t objectId, RuntimeMultiplayerGameplayCell cell) = 0;
    virtual void setStatus(const std::string& status) = 0;
    virtual void primarySelectedSound() = 0;
    virtual void requestRepaint() = 0;
};

bool handleRuntimePlanningFactoryKey(const RuntimePlanningFactoryKeyInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& selection,
    RuntimePlanningFactoryKeyHost& host);
bool dispatchRuntimePlayerFactory(RuntimePlayerFactoryRequest request,
    RuntimePlayerFactoryHost& host);