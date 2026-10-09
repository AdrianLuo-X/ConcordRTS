bool runRuntimeCooperativeOutcomeSmoke(const RuntimeWindowState& baselineA,
    const RuntimeWindowState& baselineB, const RuntimeMultiplayerSnapshotMemoryImage& image,
    std::string& error) {
    const auto fail = [&](std::string detail) { error = "cooperative actual outcome: " + detail; return false; };
    const auto destroy = [&](RuntimeWindowState& state, int coalition, bool onlyA, bool buildingsOnly) {
        RuntimeSnapshotActiveStateScope active(state);
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<int> victims;
            for (const auto& object : state.world.getObjects()) {
                const auto slot = runtimeCooperativeSlotForOwner(state, object.owner);
                if (!slot || !runtimeSkirmishObjectSurvives(state, object)) continue;
                if (onlyA && *slot != 0) continue;
                if (coalition >= 0 && (*slot < 2 ? 0 : 1) != coalition) continue;
                if (buildingsOnly && object.kind != WorldObjectKind::Structure) continue;
                victims.push_back(object.instanceId);
            }
            if (victims.empty()) break;
            for (int id : victims) {
                RuntimeObjectExitRequest death;
                death.objectId = id; death.tick = state.tick;
                death.kind = RuntimeObjectExitKind::Destroyed;
                finalizeRuntimeObjectExit(state, death);
            }
            drainRuntimeObjectExitQueue(state);
        }
        completeRuntimeWorldRefresh(state);
    };
    for (int variant = 0; variant < 3; ++variant) {
        auto a = std::make_unique<RuntimeWindowState>(); auto b = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*a, baselineA, true); seedRuntimeLoadScratch(*b, baselineB, true);
        a->tick = baselineA.tick; b->tick = baselineB.tick;
        for (auto* state : {a.get(), b.get()}) {
            RuntimeMultiplayerSnapshotStagedState staged;
            if (!stageRuntimeMultiplayerSnapshot(*state, image.bytes, image.descriptor, staged, error)) return false;
            commitRuntimeMultiplayerSnapshotAuthority(*state, std::move(staged));
        }
        if (variant == 0) {
            for (auto* state : {a.get(), b.get()}) {
                destroy(*state, 0, true, false); // A eliminated, B still fights.
                destroy(*state, 0, false, true); // All human buildings gone.
            }
            for (int frame = 0; frame < 10; ++frame) {
                advanceRuntimeCooperativeSmokeWorld(*a); advanceRuntimeCooperativeSmokeWorld(*b);
                if (!compareRuntimeCooperativeSmokeWorlds(*a, *b, error)) return false;
            }
            if (a->skirmishMatchFinished || a->cooperativeSession.status() != RuntimeCooperativeSessionStatus::Running)
                return fail("one surviving partner's units did not keep the task running");
            for (auto* state : {a.get(), b.get()}) destroy(*state, 0, false, false);
        } else if (variant == 1) {
            for (auto* state : {a.get(), b.get()}) {
                destroy(*state, 0, true, false); destroy(*state, 1, false, false);
            }
        } else {
            for (auto* state : {a.get(), b.get()}) destroy(*state, -1, false, false);
        }
        for (int frame = 0; frame < 10 && !a->skirmishMatchFinished; ++frame) {
            advanceRuntimeCooperativeSmokeWorld(*a); advanceRuntimeCooperativeSmokeWorld(*b);
            if (!compareRuntimeCooperativeSmokeWorlds(*a, *b, error)) return false;
        }
        const auto expected = variant == 0 ? RuntimeCooperativeSessionStatus::EnemyCoalitionVictory
            : (variant == 1 ? RuntimeCooperativeSessionStatus::PlayerCoalitionVictory : RuntimeCooperativeSessionStatus::Draw);
        if (!a->skirmishMatchFinished || !b->skirmishMatchFinished || a->cooperativeSession.status() != expected
            || a->skirmishResult != b->skirmishResult) return fail("two players did not share the proper result");
        if (variant == 1 && (!a->multiplayerWinningOwners.contains("skirmishplayer1")
            || !a->multiplayerWinningOwners.contains("skirmishplayer2") || !a->multiplayerWinningTeams.contains("1")))
            return fail("the eliminated partner was not included in the common victory");
        RuntimeMultiplayerSnapshotIdentity identity = image.descriptor.identity;
        identity.proposalId += 10 + variant; identity.snapshotToken = "cooperative-result-" + std::to_string(variant);
        identity.barrierTick = a->tick; identity.snapshotTick = a->tick;
        identity.authoritativeStateHash = runtimeMultiplayerAuthoritativeStateHash(*a, error);
        RuntimeMultiplayerSnapshotMemoryImage terminal;
        RuntimeMultiplayerSnapshotStagedState staged;
        if (!exportRuntimeMultiplayerSnapshotV15(*a, identity, terminal, error)
            || !stageRuntimeMultiplayerSnapshot(*b, terminal.bytes, terminal.descriptor, staged, error)) return false;
        commitRuntimeMultiplayerSnapshotAuthority(*b, std::move(staged));
        if (!compareRuntimeCooperativeSmokeWorlds(*a, *b, error)) return false;
        std::cout << "Cooperative real outcome: " << runtimeCooperativeSessionStatusName(expected)
            << " both views=yes surviving partner preserved=yes result snapshot=yes\n";
    }
    return true;
}
