#pragma once
#include "simulation/triggers/RuntimeCampaignAiTriggerDefinition.h"
#include <cstdint>
#include <vector>

// Mix the original ordered definition fields into the existing enclosing hash.
void mixRuntimeComputerAiTriggerDefinitions(std::uint64_t& hash,
    const std::vector<RuntimeCampaignAiTriggerDefinition>& definitions);
