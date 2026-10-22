#pragma once

#include "simulation/economy/HouseEconomyContext.h"
#include "simulation/production/RuntimeProductionLanes.h"

// The existing, typed lane queries retain their original lazy call order.
// No generic stage callback or host/world ownership is introduced here.
struct RuntimeProductionFixedStepResult {
    bool accepted = false;
    bool changed = false;
    std::vector<RuntimeProductionAdvanceEvent> events;
};

// Candidate lanes are committed only after the full event debit agrees and
// the same house's credit/storage ledger accepts payment. Presentation and
// world delivery remain ordered host commits after this synchronous result.
RuntimeProductionFixedStepResult advanceRuntimeProductionFixedStep(
    RuntimeProductionLanesState& lanes, HouseEconomyContext& house,
    const RuntimeProductionAdvanceHooks& hooks);
