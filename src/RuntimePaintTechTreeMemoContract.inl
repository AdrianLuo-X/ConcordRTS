bool runRuntimePaintTechTreeMemoContract(RuntimeWindowState& live, std::string& error) {
    const auto equal = [](const RuntimeTechTreeDecision& a, const RuntimeTechTreeDecision& b) {
        return a.staticAllowed == b.staticAllowed && a.ownerAllowed == b.ownerAllowed
            && a.techAllowed == b.techAllowed && a.factoryAvailable == b.factoryAvailable
            && a.prerequisitesMet == b.prerequisitesMet && a.enoughCredits == b.enoughCredits
            && a.canQueue == b.canQueue && a.canStart == b.canStart
            && a.lowPower == b.lowPower && a.lowPowerAfterBuild == b.lowPowerAfterBuild
            && a.powerNet == b.powerNet && a.powerNetAfterBuild == b.powerNetAfterBuild
            && a.techLevel == b.techLevel && a.maxTechLevel == b.maxTechLevel
            && a.missingFactory == b.missingFactory && a.missingPrerequisite == b.missingPrerequisite
            && a.reason == b.reason;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    seedRuntimeLoadScratch(probe, live, false);
    probe.economy = live.economy;
    const auto fail = [&] { error = "paint tech tree memo: decision changed"; return false; };
    for (int frame = 0; frame < 5; ++frame) {
        if (frame == 1) probe.economy.credits = 0;
        if (frame == 2) probe.economy.techLevel = 0;
        if (frame == 3) probe.economy.owner = "Russians";
        if (frame == 4) {
            probe.world.accessObjects().clear();
            probe.economy.catalog.push_back(RuntimeBuildCatalogItem{});
        }
        RuntimePaintTechTreeMemoScope scope;
        for (const auto& item : probe.economy.catalog) {
            const auto expected = evaluateRuntimeTechTreeDecisionUnmemoized(probe, item);
            if (!equal(expected, evaluateRuntimeTechTreeDecision(probe, item))
                || !equal(expected, evaluateRuntimeTechTreeDecision(probe, item))) return fail();
            const auto temporary = item;
            if (!equal(expected, evaluateRuntimeTechTreeDecision(probe, temporary))) return fail();
            {
                RuntimePaintTechTreeMemoScope nested;
                if (!equal(expected, evaluateRuntimeTechTreeDecision(probe, item))) return fail();
            }
            if (RuntimePaintTechTreeMemoScope::active != &scope) return fail();
        }
    }
    return true;
}
