// A core receipt belongs to one aircraft sortie. Its weapon may launch several
// independent root missiles; each round must authorize exactly one impact.
bool finishRuntimeAirstrikeSalvo(RuntimeWindowState& state,
    RuntimeAirstrikeRootAuthority& authority) {
    if (authority.terminalTick >= 0 || authority.roundsRemaining > 0
        || std::any_of(authority.rounds.begin(), authority.rounds.end(),
            [](const auto& round) { return round.terminalTick < 0; })) return false;
    if (state.airstrikes.acknowledgeRootProjectile(authority.strikeId,
            authority.planeToken, state.tick) != RuntimeAirstrikeProjectileAckResult::Accepted)
        return false;
    authority.terminalTick = state.tick;
    return true;
}

void reconcileRuntimeAirstrikeSalvos(RuntimeWindowState& state) {
    std::set<int> liveActors;
    for (const auto& projectile : state.projectiles) liveActors.insert(projectile.actorId);
    for (auto& [rootId, authority] : state.airstrikeRootAuthoritiesByActor) {
        (void)rootId;
        if (authority.terminalTick >= 0) continue;
        for (auto& round : authority.rounds)
            if (round.terminalTick < 0 && !liveActors.contains(round.actorId))
                round.terminalTick = state.tick;
        const auto* plane = findObjectById(state, authority.planeObjectId);
        const auto* target = findObjectById(state, authority.targetObjectId);
        const auto* weapon = findRuntimeWeaponType(state.rules, authority.weaponId);
        if (!plane || plane->health <= 0 || plane->owner != authority.owner
            || plane->type != authority.aircraftTypeId || !target || target->health <= 0
            || !weapon || !runtimeWeaponCanTargetObject(state, *weapon, *target)
            || !runtimeAirstrikeBindingForObject(state, authority.planeObjectId)) {
            authority.roundsRemaining = 0;
            authority.nextFireTick = -1;
        }
        finishRuntimeAirstrikeSalvo(state, authority);
    }
}

void advanceRuntimeAirstrikeSalvos(RuntimeWindowState& state) {
    for (auto& [rootId, authority] : state.airstrikeRootAuthoritiesByActor) {
        (void)rootId;
        if (authority.terminalTick >= 0 || authority.roundsRemaining <= 0) continue;
        const auto* plane = findObjectById(state, authority.planeObjectId);
        const auto* target = findObjectById(state, authority.targetObjectId);
        const auto* weapon = findRuntimeWeaponType(state.rules, authority.weaponId);
        const auto* strike = state.airstrikes.findStrike(authority.strikeId);
        if (!plane || !target || !weapon || (strike && strike->cancelled)
            || state.nextProjectileActorId >= INT_MAX) {
            authority.roundsRemaining = 0; authority.nextFireTick = -1;
            finishRuntimeAirstrikeSalvo(state, authority);
            continue;
        }
        if (state.tick < authority.nextFireTick) continue;
        RuntimeAirstrikeSalvoRound round;
        round.actorId = state.nextProjectileActorId++;
        round.launchTick = state.tick;
        const auto source = runtimeCellPointForObject(*plane);
        round.sourceCell = {source.x, source.y};
        round.damage = runtimeAuthoredDamageAgainst(state, *target, weapon->damage,
            weapon->warhead, 0.0, authority.veteranDamageMultiplier);
        authority.rounds.push_back(round);
        --authority.roundsRemaining;
        authority.nextFireTick = authority.roundsRemaining > 0
            ? static_cast<int>(std::min<std::int64_t>(INT_MAX, static_cast<std::int64_t>(state.tick)
                + runtimeNextBurstDelayTicks(state, *plane, *weapon,
                    static_cast<int>(authority.rounds.size())))) : -1;
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile, state,
            authority.elite ? RuntimeWeaponSlot::ElitePrimary : RuntimeWeaponSlot::Primary,
            *weapon, "airstrikeManagedBurst");
        spawnRuntimeProjectile(state, *plane, *target, *weapon, profile, round.damage,
            -1, 0, false, -1, authority.veteranDamageMultiplier, 0, 0, 0, false,
            nullptr, round.actorId, authority.strikeId, authority.planeToken, strike && strike->forceFire);
    }
}
