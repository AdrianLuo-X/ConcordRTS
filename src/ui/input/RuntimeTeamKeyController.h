#pragma once

#include "ui/time/RuntimeUiClock.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RuntimePlayerLocalCommands;

struct RuntimeTeamKeyInput {
    std::int32_t code;
    bool repeated;
    RuntimeUiTimeMs nowMs;
};

struct RuntimeTeamKeyCell {
    std::int32_t x;
    std::int32_t y;
};

// Every query and effect completes synchronously. The controller retains no
// host, owner, selection, cell or callback beyond this invocation.
class RuntimeTeamKeyHost {
public:
    virtual ~RuntimeTeamKeyHost() = default;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual bool selectable(int id) = 0;
    virtual void applySelection(const std::vector<int>& ownedResult) = 0;
    virtual void clearSelectedBeacon() = 0;
    virtual std::optional<RuntimeTeamKeyCell> objectCell(int id) = 0;
    virtual void centerCamera(RuntimeTeamKeyCell cell) = 0;
    virtual void teamAssigned(std::int32_t number) = 0;
};

// code has already passed the original reference/NumPad/Previous-Next order.
// local and currentSelection are synchronous borrows of their existing owners;
// bindings and eligibility are queried at their original individual points.
bool handleRuntimeTeamKey(
    const RuntimeTeamKeyInput& input,
    RuntimePlayerLocalCommands& local,
    const std::vector<int>& currentSelection,
    RuntimeTeamKeyHost& host
);
