bool runRuntimeVirusSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "virus: " + message; return false; };
    const auto create = [&](int hz) {
        auto state = std::make_unique<RuntimeWindowState>();
        state->config = live.config;
        state->config.fixedTickHz = hz;
        state->rules = live.rules;
        state->art = live.art;
        state->rulesLoaded = state->artLoaded = true;
        state->runtimeSuppressPlayerAudioFeedback = true;
        state->triggers.bootstrapComplete = true;
        state->economy.skirmishStartApplied = true;
        state->economy.owner = "Americans";
        return state;
    };
    const auto addVictim = [](RuntimeWindowState& state, int id, int health) {
        WorldObjectInstance object;
        object.instanceId = id;
        object.kind = WorldObjectKind::Infantry;
        object.type = "E1";
        object.owner = "Russians";
        object.health = health;
        object.x = object.y = 8;
        object.cell = runtimeCellKey(8, 8);
        state.world.accessObjects().push_back(object);
    };
    const auto shoot = [](RuntimeWindowState& state, int damage) {
        RuntimeProjectileEffect shot;
        shot.actorId = damage == 1 ? 40 : 41;
        shot.attackerObjectId = 77;
        shot.targetObjectId = 20;
        shot.owner = "Americans";
        shot.toCell = shot.currentCell = {8, 8};
        shot.damage = damage;
        // Real weapons take damage from rules; omit the weapon only for the
        // controlled one-point nonlethal warhead probe.
        shot.weaponId = damage == 1 ? "" : "Virusgun";
        shot.warheadId = "Virus";
        shot.impactAnimId = "PIFF";
        shot.ag = shot.affectsEnemies = shot.affectsAllies = true;
        return applyRuntimeProjectileImpact(state, shot);
    };
    const auto step = [](RuntimeWindowState& state) {
        ++state.tick;
        advanceRuntimeEffects(state);
        advanceRuntimeParticles(state);
    };
    const auto death = [](RuntimeWindowState& state) -> RuntimeImpactEffect* {
        for (auto& impact : state.impactEffects)
            if (impact.lifecycleState == "VirusDeath") return &impact;
        return nullptr;
    };
    for (int hz : {30, 60}) {
        auto storage = create(hz);
        auto& state = *storage;
        addVictim(state, 20, 125);
        if (!shoot(state, 1) || death(state) != nullptr || !state.particles.particles().empty())
            return fail("nonlethal hit created virus death/gas");
        if (!shoot(state, 10000) || findObjectById(state, 20) != nullptr)
            return fail("lethal shot did not retire victim");
        auto* effect = death(state);
        if (effect == nullptr || effect->animationFrameCount != 35 || effect->frameIndex != 0
            || effect->deferredParticleTicks != hz * 7 / 5 || !state.particles.particles().empty()
            || effect->owner != "Russians" || effect->deferredParticleOwner != "Americans")
            return fail("swelling timeline, source ownership or deferred emission");
        const int burstTick = effect->deferredParticleTicks;
        for (int tick = 0; tick < burstTick - 1; ++tick) {
            step(state);
            if (!state.particles.particles().empty()) return fail("gas appeared before rupture");
        }
        effect = death(state);
        if (effect == nullptr || effect->frameIndex != 20) return fail("swelling skipped the last intact frame");
        // Normal saves and multiplayer snapshots retain the countdown, source
        // owner and frame. Restore one tick before the emission boundary.
        std::ostringstream saved;
        std::string saveError;
        if (!writeRuntimeStateToStream(state, saved, saveError)) return fail("save: " + saveError);
        IniFile ini;
        auto restored = create(hz);
        restored->tick = state.tick;
        restored->world = state.world;
        if (!ini.loadFromString(saved.str()) || !loadRuntimeVirusDeaths(*restored, ini, saveError))
            return fail("restore swelling: " + saveError);
        if (!restored->particles.load(ini, restored->rules, &saveError))
            return fail("restore particle clock: " + saveError);
        restored->particlesInitialized = true;
        auto* restoredDeath = death(*restored);
        if (restoredDeath == nullptr || runtimeMultiplayerSnapshotImpactRecord(*restoredDeath)
            != runtimeMultiplayerSnapshotImpactRecord(*effect)) return fail("saved death state diverged");
        const auto* actors = runtimeImpactRenderActorsForRender(*restored);
        if (actors == nullptr || actors->size() != 1 || actors->front().frameIndex != 20
            || actors->front().animId != "VIRUSD" || !actors->front().centerOnAnchor)
            return fail("restored swelling restarted or lost its image/anchor");
        step(state);
        step(*restored);
        if (state.particles.particles().size() != 3 || death(state)->frameIndex != 21
            || death(state)->deferredParticleTicks != -1
            || state.particles.stateHash() != restored->particles.stateHash())
            return fail("rupture did not emit exactly three identical restored clouds");
        for (const auto& cloud : state.particles.particles()) {
            if (cloud.owner != "Americans" || cloud.sourceObjectId != 77
                || cloud.speed < 1.9 || cloud.speed > 2.1)
                return fail("gas lost attribution or failed to spread");
        }
        // Re-saving after rupture must never replay the emission.
        std::ostringstream consumed;
        writeRuntimeVirusDeaths(state, consumed);
        if (!ini.loadFromString(consumed.str()) || !loadRuntimeVirusDeaths(*restored, ini, saveError))
            return fail("restore consumed burst");
        for (int tick = 0; tick < hz; ++tick) { step(state); step(*restored); }
        if (state.particles.particles().size() != 3
            || state.particles.stateHash() != restored->particles.stateHash())
            return fail("burst replayed or gas continuation diverged");
    }
    // Poison damage follows moving clouds and causes the same delayed death
    // in another infantry victim; it does not immediately multiply the gas.
    auto chain = create(30);
    addVictim(*chain, 20, 10);
    shoot(*chain, 10000);
    for (int tick = 0; tick < 42; ++tick) step(*chain);
    addVictim(*chain, 21, 1);
    for (int tick = 0; tick < 61 && findObjectById(*chain, 21) != nullptr; ++tick) step(*chain);
    if (findObjectById(*chain, 21) != nullptr || death(*chain) == nullptr
        || death(*chain)->targetObjectId != 21 || death(*chain)->deferredParticleTicks <= 0
        || chain->particles.particles().size() != 3) return fail("spreading poison did not cause delayed secondary death");
    for (int tick = 0; tick < 42; ++tick) step(*chain);
    if (chain->particles.particles().size() != 6) return fail("secondary rupture did not release gas");
    if (const char* output = std::getenv("CONCORD_VIRUS_DRIFT_OUTPUT")) {
        std::filesystem::create_directories(output);
        auto visual = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*visual, live, false);
        visual->world = live.world; visual->world.accessObjects().clear();
        visual->world.accessTerrainObjects().clear(); visual->world.accessSmudges().clear();
        visual->visionEnabled = false; visual->config.fixedTickHz = 30;
        visual->config.computerAiEnabled = false; visual->config.multiplayerActive = false;
        visual->config.playerSlots.clear(); visual->economy.owner = "Americans";
        visual->economy.skirmishStartApplied = true; visual->triggers.bootstrapComplete = true;
        visual->runtimeSoundsEnabled = false; visual->runtimeSuppressPlayerAudioFeedback = true;
        visual->runtimePreferencePersistenceEnabled = false;
        RuntimeSnapshotActiveStateScope scope(*visual);
        addVictim(*visual, 20, 1);
        auto& victim = visual->world.accessObjects().back();
        victim.x = 40; victim.y = 80; victim.cell = runtimeCellKey(40,80);
        initializeRuntimeNavigation(*visual);
        if (!shoot(*visual, 10000)) return fail("visual lethal hit");
        for (int tick = 1; tick <= 42 + 8*30; ++tick) {
            step(*visual);
            if (tick != 42 && tick != 42+2*30 && tick != 42+8*30) continue;
            const auto stats = drawRuntimeStateOffscreenAtCell(*visual, {40,80}, 640, 480, error,
                std::string(output)+"/gas-"+std::to_string((tick-42)/30)+"s.bmp", false);
            if (!stats || stats->particleActorShpDrawn != 3 || stats->particleActorFallbackDrawn != 0)
                return fail("three native gas sprites: " + error);
        }
    }
    std::cout << "Virus: nonlethal=ok swelling=1.4s rupture=frame21 gas=3 slowSpread=ok "
        "secondaryDeath=delayed save/snapshot=ok tickRate=30/60Hz\n";
    return true;
}
