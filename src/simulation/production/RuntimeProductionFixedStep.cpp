#include "simulation/production/RuntimeProductionFixedStep.h"
#include "simulation/economy/RuntimeEconomyFixedStep.h"

#include <algorithm>
#include <numeric>
#include <utility>

RuntimeProductionFixedStepResult advanceRuntimeProductionFixedStep(
    RuntimeProductionLanesState& lanes, HouseEconomyContext& house,
    const RuntimeProductionAdvanceHooks& hooks) {
    RuntimeProductionLanesState staged =
        lanes;
    const std::uint64_t previousHash =
        runtimeProductionLanesStateHash(staged);
    const int creditsBefore = std::max(0, house.credits);
    int stagedCredits = creditsBefore;

    std::vector<RuntimeProductionAdvanceEvent> events =
        runtimeAdvanceProductionLanes(staged, stagedCredits, hooks);
    const int creditsDebited = creditsBefore - stagedCredits;
    const int eventDebits = std::accumulate(
        events.begin(),
        events.end(),
        0,
        [](int total, const RuntimeProductionAdvanceEvent& event) {
            return total + std::max(0, event.creditsDebited);
        }
    );
    if (creditsDebited < 0
        || eventDebits != creditsDebited
        || (creditsDebited > 0
            && !spendRuntimeHouseCredits(house, creditsDebited))) {

        return {false, false, std::move(events)};
    }
    lanes = std::move(staged);
    bool changed = previousHash != runtimeProductionLanesStateHash(
        lanes
    );

    return {true, changed, std::move(events)};
}

