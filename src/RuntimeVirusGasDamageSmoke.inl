bool runRuntimeVirusGasDamageSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "virus gas damage: " + why; return false; };
    const auto make = [&](int hz) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.config.multiplayerActive = false; s.config.playerSlots.clear(); s.config.crates = false;
        s.report = live.report; s.art = live.art; s.theater = live.theater;
        IniFile rules;
        if (!rules.loadFromFile(live.report.rulesPath) || !s.rules.loadFromIni(rules))
            return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 22; ++y) for (int x = 1; x <= 22; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        WorldObjectInstance source; source.instanceId = 77; source.type = "VIRUS";
        source.kind = WorldObjectKind::Infantry; source.owner = "YuriCountry";
        source.mission = "Sleep"; source.alive = true; source.currentAmmo = -1;
        source.health = runtimeMaxHealthForObject(s,source);
        source.x = source.y = 20; source.cell = runtimeCellKey(20,20);
        s.world.accessObjects().push_back(source);
        ensureRuntimeParticlesInitialized(s);
        return storage;
    };
    const auto add = [](RuntimeWindowState& s, int id, RuntimeCellPoint cell) {
        WorldObjectInstance actor; actor.instanceId = id; actor.type = "E1";
        actor.kind = WorldObjectKind::Infantry; actor.owner = s.economy.owner;
        actor.alive = true; actor.mission = "Guard"; actor.currentAmmo = -1;
        actor.health = runtimeMaxHealthForObject(s,actor);
        actor.x = cell.x; actor.y = cell.y; actor.cell = runtimeCellKey(cell.x,cell.y);
        s.world.accessObjects().push_back(actor);
    };
    const auto cloud = [](RuntimeWindowState& s, double x, double y) {
        RuntimeParticleSpawnContext context; context.owner = "YuriCountry";
        context.sourceObjectId = 77; context.sourceTag = "virus-gas-damage-smoke";
        context.position = {x * 256.0,y * 256.0,0.0};
        return s.particles.spawnParticle(s.rules,"VirusCloud1",context) != 0;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s);
    };
    int stationary = 0, passages = 0, hits = 0, saves = 0;
    for (int hz : {15,30,60}) for (double dx : {-0.24,0.24}) for (double dy : {-0.24,0.24}) {
        auto storage = make(hz); if (!storage) return fail("stock resources"); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s);
        add(s,1,{10,10});
        int nextId = 2;
        for (int y = 9; y <= 11; ++y) for (int x = 9; x <= 11; ++x)
            if (x != 10 || y != 10) add(s,nextId++,{x,y});
        initializeRuntimeNavigation(s);
        const int initial = findObjectById(s,1)->health;
        if (!cloud(s,10.0+dx,10.0+dy)) return fail("spawn");
        const auto& particle = s.particles.particles().front();
        if (particle.damageCountdown != hz*2 || particle.speed != 0.0)
            return fail("native MaxDC=30 / rested cloud setup");
        std::unique_ptr<RuntimeWindowState> restored;
        int previous = initial;
        for (int tick = 1; tick <= hz*6; ++tick) {
            step(s);
            const auto* victim = findObjectById(s,1);
            if (!victim) return fail("stationary victim died too quickly");
            const int damage = previous-victim->health;
            if ((tick % (hz*2) == 0 && (damage <= 0 || damage >= 40))
                || (tick % (hz*2) != 0 && damage != 0)) {
                return fail("stationary interval/position hz=" + std::to_string(hz)
                    + " offset=" + std::to_string(dx) + "," + std::to_string(dy)
                    + " tick=" + std::to_string(tick) + " damage=" + std::to_string(damage));
            }
            if (damage > 0) ++hits;
            for (int id = 2; id < nextId; ++id) {
                const auto* neighbor = findObjectById(s,id);
                if (!neighbor || neighbor->health != initial) return fail("cloud damaged neighboring cell");
            }
            previous = victim->health;
            if (restored) {
                step(*restored);
                const auto* copy = findObjectById(*restored,1);
                if (!copy || copy->health != victim->health || restored->particles.stateHash() != s.particles.stateHash())
                    return fail("mid-interval reload diverged");
            } else if (tick == hz*3) {
                std::ostringstream saved; IniFile ini; restored = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*restored,s,false);
                if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str())
                    || !materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("reload: " + error);
                if (restored->particles.stateHash() != s.particles.stateHash()) return fail("reload reset gas clock");
                ++saves;
            }
        }
        ++stationary;
    }
    // Walk straight through a settled cloud in both axes and directions. Four
    // entry phases cover MaxDC's timing window without forcing a hit on every
    // brief crossing. Check that damage stops after the unit walks out.
    const std::array<RuntimeCellPoint,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
    for (int hz : {15,30,60}) for (const auto direction : directions) {
        int directionHits = 0;
        int firstEntryTick = -1;
        for (int phase = 0; phase < 4; ++phase) {
            auto storage = make(hz); if (!storage) return fail("walk resources"); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const RuntimeCellPoint start{10-direction.x*3,10-direction.y*3};
            const RuntimeCellPoint goal{10+direction.x*3,10+direction.y*3};
            add(s,1,start); initializeRuntimeNavigation(s);
            const int initial = findObjectById(s,1)->health;
            if (!cloud(s,9.76,9.76)) return fail("walking cloud spawn");
            // Measure a real traversal before choosing its pulse phase. Four
            // arbitrary half-second offsets can all miss a short occupancy
            // window after subcell movement changes, without a damage defect.
            const int interval = hz*2;
            const int alignedDelay = firstEntryTick < 0 ? 0
                : (interval - (firstEntryTick + 1) % interval) % interval;
            const int delay = phase == 0 ? 0 : (alignedDelay + (phase-1)*hz/2) % interval;
            for (int tick = 0; tick < delay; ++tick) step(s);
            s.selectedObjectIds = {1}; issueRuntimeMoveCommand(s,goal);
            if (s.lastCommand.acceptedCount != 1) return fail("walk command rejected");
            bool crossed = false, arrived = false; int previous = initial, changes = 0;
            for (int tick = 0; tick < hz*25; ++tick) {
                const auto* before = findObjectById(s,1);
                if (!before) return fail("walking victim died");
                const bool inside = before->x == 10 && before->y == 10;
                if (phase == 0 && inside && firstEntryTick < 0) firstEntryTick = s.tick;
                const RuntimeCellPoint previousCell{before->x,before->y};
                crossed = crossed || inside;
                step(s); const auto* after = findObjectById(s,1);
                if (!after) return fail("walking victim died after pulse");
                const int damage = previous-after->health;
                if (damage < 0 || damage >= 40 || (damage > 0 && !inside))
                    return fail("walking damage outside cloud tick=" + std::to_string(s.tick)
                        + " cell=" + std::to_string(previousCell.x) + "," + std::to_string(previousCell.y));
                if (damage > 0) { ++changes; ++hits; }
                previous = after->health;
                if (after->x == goal.x && after->y == goal.y && !after->renderMoveActive) { arrived = true; break; }
            }
            if (!crossed || !arrived) return fail("walk never crossed/left cloud hz=" + std::to_string(hz)
                + " phase=" + std::to_string(phase) + " cell=" + std::to_string(findObjectById(s,1)->x)
                + "," + std::to_string(findObjectById(s,1)->y));
            const int leftHealth = findObjectById(s,1)->health;
            for (int tick = 0; tick < hz*5; ++tick) step(s);
            if (!findObjectById(s,1) || findObjectById(s,1)->health != leftHealth)
                return fail("poison kept damaging after exit");
            directionHits += changes; ++passages;
        }
        if (directionHits == 0) return fail("no damage across walking phases hz=" + std::to_string(hz)
            + " direction=" + std::to_string(direction.x) + "," + std::to_string(direction.y));
    }
    if (!runRuntimeVirusSmokeProbe(live,error)) return false;
    std::cout << "Virus gas damage passed: stationary=" << stationary << " passages=" << passages
        << " damagePulses=" << hits << " reloads=" << saves
        << " neighborCells=untouched exitDamage=none tickRate=15/30/60Hz\n";
    return true;
}
