    runtimeStableHashMix(hash, static_cast<long long>(triggers.computerAiTriggers.size()));
    for (const auto& trigger : triggers.computerAiTriggers) {
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
