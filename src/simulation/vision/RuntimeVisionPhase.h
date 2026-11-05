#pragma once
#include "simulation/vision/RuntimeVision.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include "runtime/config/RuntimeConfig.h"

// Borrowed live fields for one synchronous authority update. Presentation
// cache ownership stays with its typed notification consumer.
struct RuntimeVisionPhaseContext {
    const GameWorld& world;
    const RulesDatabase& rules;
    const bool& rulesLoaded;
    const bool& visionEnabled;
    RuntimeVisionSystem& vision;
    const RuntimeConfig& config;
    const std::string& localOwner;
    int& tick;
    std::map<int, RuntimePublicBuildingLocation>& publicBuildingLocations;
    double& lastSimVisionMs;
};

class RuntimeVisionPhaseOperations {
public:
    virtual ~RuntimeVisionPhaseOperations() = default;
    virtual std::string ownerForSlot(std::size_t slotIndex) = 0;
    virtual bool isLandRushMode() = 0;
    virtual bool ownersAreAllied(const std::string& left, const std::string& right) = 0;
    virtual std::string canonicalHumanOwner(const std::string& owner) = 0;
    virtual void notifyPublicBuildingLocation(const RuntimeCellPoint& cell) noexcept = 0;
    virtual void invalidateLocalProjection() noexcept = 0;
};

bool runtimeVisionOwnerIsNeutral(const std::string& owner);
bool updateRuntimePublicBuildingLocations(RuntimeVisionPhaseContext& state,
    RuntimeVisionPhaseOperations& operations,
    const std::vector<RuntimeVisionObserverDefinition>& observers);
bool updateRuntimeVisionState(RuntimeVisionPhaseContext& state, RuntimeVisionPhaseOperations& operations);
