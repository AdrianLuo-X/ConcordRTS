#pragma once

#include "ui/input/RuntimeTeamKeyController.h"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct RuntimePlayerLocalCommands;
struct RuntimePlayerSelectionCandidate;

struct RuntimeLocalNavigationInput {
    std::int32_t code;
    bool repeated;
    RuntimeUiTimeMs nowMs;
};

// Each host is borrowed for this synchronous call only. Queries return owned
// values and effects finish immediately; no scene/native state is retained.
class RuntimeObjectCycleKeyHost {
public:
    virtual ~RuntimeObjectCycleKeyHost() = default;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual std::vector<int> eligibleMobileObjectIds() = 0;
    virtual void applySelection(const std::vector<int>& ownedResult) = 0;
    virtual void clearSelectedBeacon() = 0;
    virtual void centerCurrentSelection() = 0;
};

class RuntimeViewNavigationKeyHost {
public:
    virtual ~RuntimeViewNavigationKeyHost() = default;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual RuntimeTeamKeyCell currentViewportCenterCell() = 0;
    virtual void centerCamera(RuntimeTeamKeyCell cell) = 0;
    virtual void viewSaved(std::int32_t number) = 0;
    virtual std::optional<RuntimeTeamKeyCell> latestRadarEventCell() = 0;
};

class RuntimeQuickSelectionKeyHost {
public:
    virtual ~RuntimeQuickSelectionKeyHost() = default;
    virtual std::int32_t binding(const std::string& action, std::int32_t fallback) = 0;
    virtual std::set<int> onScreenObjectIds() = 0;
    virtual std::vector<RuntimePlayerSelectionCandidate> mobileCandidates(
        const std::set<int>& onScreen) = 0;
    virtual void applySelection(const std::vector<int>& ownedResult) = 0;
    virtual void clearSelectedBeacon() = 0;
};

// Call each entry at its original separate position around Team and the
// remaining command consumers. No entry normalizes or samples input.
bool handleRuntimeObjectCycleKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& currentSelection,
    RuntimeObjectCycleKeyHost& host);
bool handleRuntimeViewNavigationKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, RuntimeViewNavigationKeyHost& host);
bool handleRuntimeQuickSelectionKey(const RuntimeLocalNavigationInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& currentSelection,
    RuntimeQuickSelectionKeyHost& host);
bool handleRuntimeRadarNavigationKey(const RuntimeLocalNavigationInput& input,
    RuntimeViewNavigationKeyHost& host);
