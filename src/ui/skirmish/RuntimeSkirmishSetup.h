#pragma once
#include "runtime/config/RuntimeConfig.h"
#include "ui/skirmish/RuntimeSkirmishSelection.h"

// Called only after the existing caller accepts a selection. Fixed tick, seed,
// multiplayer identity, and all unrelated settings remain the caller's authority.
void applyRuntimeSkirmishSelection(RuntimeConfig& activeConfig,
    const RuntimeSkirmishSelection& selection);
