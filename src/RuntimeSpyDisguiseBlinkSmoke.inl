bool runRuntimeSpyDisguiseBlinkSmoke(
    RuntimeWindowState& state,
    int spyId,
    std::string& error
) {
    const auto fail = [&](const std::string& why) {
        error = "Spy owner disguise blink: " + why;
        return false;
    };
    const WorldObjectInstance* spy = findObjectById(state, spyId);
    const RuntimeSpyDisguiseState* current = state.spies.disguiseState(spyId);
    if (spy == nullptr || current == nullptr || !current->active) {
        return fail("an active disguise fixture is required");
    }
    const RuntimeSpyDisguiseState disguise = *current;
    std::string opposingOwner;
    for (const auto& object : state.world.getObjects()) {
        if (object.alive && !runtimeOwnersAreAllied(state, object.owner, spy->owner)) {
            opposingOwner = object.owner;
            break;
        }
    }
    if (opposingOwner.empty()) return fail("an opposing observer fixture is required");

    struct RestoreState {
        RuntimeWindowState& state;
        int tick;
        int fixedTickHz;
        std::string owner;
        RuntimeSpySystemState spies;
        ~RestoreState() {
            state.tick = tick;
            state.config.fixedTickHz = fixedTickHz;
            state.economy.owner = std::move(owner);
            state.spies = std::move(spies);
        }
    } restore{state, state.tick, state.config.fixedTickHz, state.economy.owner, state.spies};
    const auto sameProjection = [](const RuntimeSpyObserverProjection& left,
                                   const RuntimeSpyObserverProjection& right) {
        return left.active == right.active && left.identity == right.identity
            && left.type == right.type && left.owner == right.owner;
    };
    const auto originalSpyHash = state.spies.stateHash();
    const auto originalWorldHash = state.world.deterministicSimulationStateHash();
    const auto opposingProjection = runtimeSpyObserverProjectionForOwner(
        state, opposingOwner, *spy);
    state.economy.owner = spy->owner;

    // The expected intervals are authored frame boundaries, including a full
    // wrap. Intermediate simulation ticks must not move the phase early.
    const std::vector<std::pair<int, bool>> samples{
        {0, false}, {7, false}, {8, true}, {20, true}, {55, true},
        {56, false}, {255, false}, {256, false}, {264, true}
    };
    int checks = 0;
    int restoredChecks = 0;
    for (int hz : {15, 30, 60}) {
        state.config.fixedTickHz = hz;
        bool sawSpy = false;
        bool sawDisguise = false;
        for (const auto& [frame, expectedDisguise] : samples) {
            for (int subTick = 0; subTick < hz / 15; ++subTick) {
                state.tick = disguise.lastChangeTick + frame * hz / 15 + subTick;
                const auto observationHash = runtimePersistentTransientStateHash(state);
                const auto own = runtimeSpyObserverProjectionForOwner(
                    state, spy->owner, *spy, true);
                const auto repeated = runtimeSpyObserverProjectionForOwner(
                    state, spy->owner, *spy, true);
                const auto radar = runtimeSpyObserverProjectionForOwner(
                    state, spy->owner, *spy);
                const auto opposing = runtimeSpyObserverProjectionForOwner(
                    state, opposingOwner, *spy, true);
                const auto opposingLegacy = runtimeSpyObserverProjectionForOwner(
                    state, opposingOwner, *spy);
                const std::string sample = " at " + std::to_string(hz)
                    + " Hz/frame " + std::to_string(frame)
                    + "/subtick " + std::to_string(subTick);
                if (own.active != expectedDisguise) {
                    return fail("wrong owner phase" + sample);
                }
                if (own.active && (own.identity != disguise.identity
                        || own.type != disguise.disguiseType
                        || own.owner != disguise.disguiseOwner)) {
                    return fail("owner phase lost the copied/default identity" + sample);
                }
                if (!sameProjection(own, repeated)) {
                    return fail("repeated paint advanced a paused phase" + sample);
                }
                if (radar.active) return fail("owner radar identity changed" + sample);
                if (!sameProjection(opposing, opposingLegacy)
                    || !sameProjection(opposing, opposingProjection)) {
                    return fail("owner blink altered opposing detection/identity" + sample);
                }
                if (runtimePersistentTransientStateHash(state) != observationHash
                    || state.spies.stateHash() != originalSpyHash
                    || state.world.deterministicSimulationStateHash() != originalWorldHash) {
                    return fail("appearance queries mutated simulation authority" + sample);
                }
                sawDisguise = sawDisguise || own.active;
                sawSpy = sawSpy || !own.active;
                ++checks;

                if (subTick == 0 && (frame == 7 || frame == 8
                        || frame == 55 || frame == 56)) {
                    std::ostringstream saved;
                    state.spies.save(saved);
                    IniFile ini;
                    RuntimeSpySystemState reloaded;
                    std::string loadError;
                    if (!ini.loadFromString(saved.str())
                        || !reloaded.load(ini, makeRuntimeSpyContext(state),
                            state.tick, &loadError)
                        || reloaded.stateHash() != originalSpyHash) {
                        return fail("save/load changed disguise authority" + sample
                            + (loadError.empty() ? "" : ": " + loadError));
                    }
                    state.spies = std::move(reloaded);
                    const auto loadedAppearance = runtimeSpyObserverProjectionForOwner(
                        state, spy->owner, *spy, true);
                    if (!sameProjection(own, loadedAppearance)) {
                        return fail("save/load restarted the owner blink phase" + sample);
                    }
                    ++restoredChecks;
                }
            }
        }
        if (!sawSpy || !sawDisguise) return fail("cycle did not show both appearances");
    }

    // A real reveal takes precedence even when its saved origin would put
    // the owner's cosmetic cycle in the disguise interval.
    state.config.fixedTickHz = 30;
    state.tick = disguise.lastChangeTick;
    const auto damaged = state.spies.notifyDamaged(
        makeRuntimeSpyContext(state), spyId, state.tick);
    state.tick += 16;
    if (!damaged.accepted || !damaged.changed || state.spies.isDisguised(spyId)
        || runtimeSpyObserverProjectionForOwner(state, spy->owner, *spy, true).active
        || runtimeSpyObserverProjectionForOwner(state, opposingOwner, *spy, true).active) {
        return fail("cosmetic blink overrode a damage reveal");
    }
    std::cout << "Spy owner blink: "
        << (disguise.observerRelative ? "default" : "copied")
        << " cases=" << checks << " savePhases=" << restoredChecks
        << " radar=stable observer=stable authority=unchanged reveal=honored\n";
    return true;
}
