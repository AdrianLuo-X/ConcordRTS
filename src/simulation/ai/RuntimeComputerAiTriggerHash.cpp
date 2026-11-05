#include "simulation/ai/RuntimeComputerAiTriggerHash.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"

void mixRuntimeComputerAiTriggerDefinitions(std::uint64_t& hash,
    const std::vector<RuntimeCampaignAiTriggerDefinition>& definitions) {
    using namespace runtime_authority_hash;
    runtimeStableHashMix(hash, static_cast<long long>(definitions.size()));
    for (const auto& trigger : definitions) {
        for (const auto* identifier : {&trigger.id, &trigger.primaryTeamId, &trigger.secondaryTeamId,
                &trigger.owner, &trigger.conditionObject})
            runtimeStableHashMixString(hash, runtimeMultiplayerCanonicalIdentifier(*identifier));
        for (int value : {trigger.techLevel, trigger.conditionType, trigger.conditionNumber,
                trigger.conditionComparator, trigger.side, trigger.scope}) runtimeStableHashMix(hash, value);
        for (double value : {trigger.currentWeight, trigger.minimumWeight, trigger.maximumWeight})
            runtimeMultiplayerAuthoritativeMixDouble(hash, value);
        for (bool value : {trigger.availableInSkirmish, trigger.baseDefense, trigger.enabled, trigger.valid})
            runtimeMultiplayerAuthoritativeMixBool(hash, value);
        for (bool value : trigger.difficultyEnabled) runtimeMultiplayerAuthoritativeMixBool(hash, value);
    }

}
