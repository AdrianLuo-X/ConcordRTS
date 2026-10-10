bool runtimeHasInfiniteMindControl(const RulesDatabase& rules, const WorldObjectInstance& object) {
    const WeaponSet* weapons = nullptr;
    if (const auto* building = buildingTypeFor(rules, object)) weapons = &building->weaponSet;
    else if (const auto* unit = unitTypeFor(rules, object)) weapons = &unit->weaponSet;
    if (weapons == nullptr) return false;
    const auto infinite = [&](const std::string& id) {
        const auto* weapon = rules.findWeaponType(id);
        const auto* warhead = weapon == nullptr ? nullptr : rules.findWarheadType(weapon->warhead);
        return weapon != nullptr && weapon->infiniteMindControl
            && warhead != nullptr && warhead->mindControl;
    };
    return infinite(weapons->primary) || infinite(weapons->secondary)
        || infinite(weapons->elitePrimary) || infinite(weapons->eliteSecondary)
        || std::any_of(weapons->indexedWeapons.begin(), weapons->indexedWeapons.end(), infinite)
        || std::any_of(weapons->eliteIndexedWeapons.begin(), weapons->eliteIndexedWeapons.end(), infinite);
}

std::vector<MindControlOverloadDamageEvent> RuntimeSpecialMechanismState::tickMindControlOverload(
    const std::vector<WorldObjectInstance>& objects, const RulesDatabase& rules, int tick, int fixedTickHz
) {
    std::vector<MindControlOverloadDamageEvent> damage;
    std::set<int> active;
    const int frame = static_cast<int>(std::min<std::int64_t>(
        static_cast<std::int64_t>(std::max(0, tick)) * 15 / std::max(1, fixedTickHz),
        std::numeric_limits<int>::max() - 1000002));
    const std::size_t levels = std::min({rules_.overloadCount.size(), rules_.overloadDamage.size(),
        rules_.overloadFrames.size()});
    for (const auto& controller : objects) {
        if (!controller.alive || controller.health <= 0 || !runtimeHasInfiniteMindControl(rules, controller)) continue;
        active.insert(controller.instanceId);
        auto& state = mindControlOverloadByController_[controller.instanceId];
        if (levels == 0 || frame < state.nextCheckFrame) continue;
        int count = 0;
        for (const auto& [targetId, link] : mindControlByTarget_) {
            if (link.controllerObjectId != controller.instanceId || link.permanent) continue;
            const auto* target = findObject(objects, targetId);
            if (target != nullptr && target->alive && target->owner == controller.owner) ++count;
        }
        std::size_t level = 0;
        while (level + 1 < levels && count > rules_.overloadCount[level]) ++level;
        // Retail CaptureManager::HandleOverload (0x471A50) decrements and
        // returns while the delay is positive, then samples the CURRENT count.
        // A configured delay of 60 therefore gives 61 authored-frame intervals.
        state.nextCheckFrame = frame + std::clamp(rules_.overloadFrames[level], 0, 1000000) + 1;
        const int amount = std::max(0, rules_.overloadDamage[level]);
        if (amount > 0) {
            damage.push_back({controller.instanceId, count, amount, !state.soundPlayed});
            state.soundPlayed = true;
        } else state.soundPlayed = false;
    }
    std::erase_if(mindControlOverloadByController_, [&](const auto& entry) { return !active.contains(entry.first); });
    return damage;
}
