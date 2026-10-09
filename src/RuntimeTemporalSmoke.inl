bool runRuntimeTemporalAttackSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "temporal attack: " + message; return false; };
    for (const int rank : {0, 2}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules;
        probe.art = live.art;
        probe.rulesLoaded = true;
        probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = "Americans";
        for (int y = 1; y <= 18; ++y) for (int x = 1; x <= 18; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0;
            cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance source;
        source.instanceId = 1;
        source.kind = WorldObjectKind::Infantry;
        source.type = "CLEG";
        source.owner = "Americans";
        source.health = 125;
        source.x = 8;
        source.y = 8;
        source.cell = runtimeCellKey(8, 8);
        source.facing = 64;
        source.veterancy = rank;
        source.mission = "Guard";
        auto target = source;
        target.instanceId = 2;
        target.kind = WorldObjectKind::Unit;
        target.type = "MTNK";
        target.owner = "Russians";
        target.x = 10;
        target.cell = runtimeCellKey(10, 8);
        target.health = 400;
        auto assistant = source;
        assistant.instanceId = 3;
        assistant.y = 9;
        assistant.cell = runtimeCellKey(8, 9);
        probe.world.accessObjects() = {source, target, assistant};
        initializeRuntimeNavigation(probe);
        const auto rays = [&]() {
            refreshRuntimeSpecialMechanismRenderSnapshots(probe);
            std::vector<SpecialMechanismRenderSnapshot> result;
            for (const auto& snapshot : probe.specialMechanismRenderSnapshots) {
                if (snapshot.kind == "temporalBeam") result.push_back(snapshot);
            }
            return result;
        };
        probe.selectedObjectIds = {1};
        issueRuntimeAttackCommand(probe, *findObjectById(probe, 2));
        if (probe.lastCommand.acceptedCount != 1) return fail("stock CLEG attack rejected");
        for (int tick = 0; tick < 180 && !probe.specialMechanisms.isTemporalFrozen(2); ++tick) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
            auto* attacker = findMutableObjectById(probe, 1);
            auto behavior = probe.behaviors.find(1);
            if (behavior == probe.behaviors.end()) return fail("attack order disappeared");
            if (attacker->weaponCooldownTicksRemaining > 0) --attacker->weaponCooldownTicksRemaining;
            advanceRuntimeAttackBehavior(probe, *attacker, behavior->second);
        }
        const auto first = rays();
        if (!probe.specialMechanisms.isTemporalFrozen(2) || first.size() != 1
            || !first.front().hasBeamEndpoints || first.front().objectId != 1
            || first.front().targetObjectId != 2 || first.front().color != "128,200,255"
            || first.front().sourceLevel <= source.level
            || findObjectById(probe, 2)->health != target.health) {
            return fail("shot did not establish a blue FLH beam and freeze without HP damage");
        }
        for (int tick = 0; tick < 12; ++tick) { ++probe.tick; advanceRuntimeEffects(probe); }
        if (!probe.projectiles.empty() || rays().size() != 1) {
            return fail("ray expired with the invisible projectile during weapon cooldown");
        }
        auto* movingTarget = findMutableObjectById(probe, 2);
        movingTarget->hasRenderPosition = true;
        movingTarget->renderMoveActive = true;
        movingTarget->renderX = 9.75;
        movingTarget->renderY = 8;
        RuntimeObjectBehavior moving;
        moving.kind = RuntimeBehaviorKind::Move;
        moving.moveTarget = {12, 8};
        moving.moveCooldownTicks = 5;
        advanceRuntimeMoveBehavior(probe, *movingTarget, moving);
        if (movingTarget->renderX != 9.75 || moving.moveCooldownTicks != 5
            || movingTarget->x != 10 || movingTarget->mission != "Temporal") {
            return fail("a frozen target kept moving");
        }
        issueRuntimeStopCommand(probe);
        if (!rays().empty() || probe.specialMechanisms.isTemporalFrozen(2)
            || findObjectById(probe, 2)->mission == "Temporal") {
            return fail("stop left a beam or frozen victim behind");
        }
        clearRuntimeRenderPosition(*movingTarget);

        const auto shoot = [&](int attackerId) {
            const auto* attacker = findObjectById(probe, attackerId);
            const auto* victim = findObjectById(probe, 2);
            if (attacker == nullptr || victim == nullptr) return false;
            const auto profile = runtimeWeaponProfileForObject(probe, *attacker, victim);
            if (profile.weapon == nullptr || !profile.weapon->radBeam
                || profile.weapon->id != (rank == 0 ? "NeutronRifle" : "NeutronRifleE")) return false;
            spawnRuntimeProjectile(probe, *attacker, *victim, *profile.weapon, profile, profile.weapon->damage);
            if (probe.projectiles.empty()) return false;
            const auto projectile = probe.projectiles.back();
            probe.projectiles.pop_back();
            return applyRuntimeProjectileImpact(probe, projectile);
        };
        for (const int id : {1, 3}) {
            RuntimeObjectBehavior behavior;
            behavior.kind = RuntimeBehaviorKind::Attack;
            behavior.targetObjectId = 2;
            probe.behaviors[id] = behavior;
            if (!shoot(id)) return fail("cooperative erasure shot failed");
        }
        if (rays().size() != 2) return fail("two attackers did not retain separate rays");
        // Temporal stasis rejects ordinary damage, but must still admit the
        // erasure contributions of both the original firer and other firers.
        const auto frozenAuthority = probe.specialMechanisms.stateHash();
        const int frozenHealth = findObjectById(probe,2)->health;
        const auto ordinaryProfile = runtimeWeaponProfileForObject(probe,
            *findObjectById(probe,2), findObjectById(probe,1));
        if (!ordinaryProfile.weapon || !ordinaryProfile.warhead || ordinaryProfile.warhead->temporal)
            return fail("ordinary damage control weapon missing");
        spawnRuntimeProjectile(probe,*findObjectById(probe,1),*findObjectById(probe,2),
            *ordinaryProfile.weapon,ordinaryProfile,ordinaryProfile.weapon->damage);
        const auto ordinaryShot=probe.projectiles.back();probe.projectiles.pop_back();
        applyRuntimeProjectileImpact(probe,ordinaryShot);
        if (findObjectById(probe,2)->health!=frozenHealth
            || probe.specialMechanisms.stateHash()!=frozenAuthority || rays().size()!=2)
            return fail("ordinary damage changed temporal stasis");
        const auto savedHash = probe.specialMechanisms.stateHash();
        std::ostringstream saved;
        probe.specialMechanisms.save(saved);
        IniFile savedIni;
        RuntimeSpecialMechanismState restored;
        restored.reset(probe.rules.getSpecialMechanismRules());
        if (!savedIni.loadFromString(saved.str()) || !restored.load(savedIni, error)
            || restored.stateHash() != savedHash) return fail("active ray save/load changed authority");
        probe.specialMechanisms.swap(restored);
        if (rays().size() != 2) return fail("loaded erasure lost its rays");
        probe.selectedObjectIds = {1};
        issueRuntimeStopCommand(probe);
        const auto remaining = rays();
        if (remaining.size() != 1 || remaining.front().objectId != 3
            || !probe.specialMechanisms.isTemporalFrozen(2)) return fail("stopping one erased another attacker's ray");
        probe.selectedObjectIds = {3};
        issueRuntimeMoveCommand(probe, {7, 9});
        if (!rays().empty() || probe.specialMechanisms.isTemporalFrozen(2)) return fail("move did not interrupt the beam");

        RuntimeObjectBehavior rangedAttack;
        rangedAttack.kind = RuntimeBehaviorKind::Attack;
        rangedAttack.targetObjectId = 2;
        probe.behaviors[1] = rangedAttack;
        if (!shoot(1)) return fail("range interruption setup failed");
        setRuntimeObjectCell(probe, *findMutableObjectById(probe, 2), {17, 8});
        advanceRuntimeAttackBehavior(probe, *findMutableObjectById(probe, 1), probe.behaviors.at(1));
        if (!rays().empty() || probe.specialMechanisms.isTemporalFrozen(2)) return fail("out-of-range beam remained connected");
        eraseRuntimeBehavior(probe, 1);
        setRuntimeObjectCell(probe, *findMutableObjectById(probe, 2), {10, 8});
        if (!shoot(3)) return fail("attacker death setup failed");
        RuntimeObjectExitRequest death;
        death.objectId = 3;
        death.kind = RuntimeObjectExitKind::Destroyed;
        death.tick = probe.tick;
        death.reason = "temporal attacker death probe";
        finalizeRuntimeObjectExit(probe, death);
        if (findObjectById(probe, 3) != nullptr || !rays().empty()
            || probe.specialMechanisms.isTemporalFrozen(2)
            || findObjectById(probe, 2)->mission == "Temporal") return fail("attacker death left a frozen victim");

        probe.behaviors[1] = rangedAttack;
        if (!shoot(1)) return fail("continuous erasure setup failed");
        for (int tick = 0; tick < 1200 && findObjectById(probe, 2) != nullptr; ++tick) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        if (findObjectById(probe, 2) != nullptr || !rays().empty()) return fail("completed erasure retained target/ray");
        int eraseEffects = 0;
        int duration = 0;
        for (const auto& effect : probe.impactEffects) {
            if (effect.lifecycleState == "UnitDestruction" || effect.lifecycleState == "BuildingDestruction")
                return fail("erasure emitted an ordinary explosion");
            if (effect.lifecycleState != "TemporalErase") continue;
            ++eraseEffects;
            if (effect.animId != "WARPAWAY" || effect.animationFrameCount < 2
                || effect.totalTicks != effect.animationFrameCount * effect.animationFrameDelayTicks
                || !makeRuntimeImpactRenderActor(effect).centerOnAnchor) return fail("WarpAway animation was truncated/misplaced");
            duration = effect.totalTicks;
        }
        if (eraseEffects != 1) return fail("completion did not emit exactly one WarpAway");
        for (int tick = 0; tick <= duration; ++tick) { ++probe.tick; advanceRuntimeEffects(probe); }
        for (const auto& effect : probe.impactEffects) {
            if (effect.lifecycleState == "TemporalErase") return fail("WarpAway kept looping after erasure");
        }
    }
    std::cout << "Temporal attack: rookie+elite=ok persistentBeam=ok FLH=ok freeze=ok "
              << "stopMove=ok multiAttacker=ok saveLoad=ok eraseTimeline=ok\n";
    return true;
}
