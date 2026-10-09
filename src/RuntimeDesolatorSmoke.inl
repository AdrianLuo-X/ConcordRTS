bool runRuntimeDesolatorSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "desolator: " + message; return false; };
    const auto* beam = live.rules.findWeaponType("RadBeamWeapon");
    const auto* eruption = live.rules.findWeaponType("RadEruptionWeapon");
    const auto* eruptionWarhead = live.rules.findWarheadType("RadEruptionWarhead");
    if (beam == nullptr || beam->radLevel != 0 || !beam->radBeam
        || eruption == nullptr || eruption->radLevel != 500 || !eruption->areaFire
        || eruptionWarhead == nullptr || eruptionWarhead->cellSpread != 10.0)
        return fail("original beam / deployed radiation rules missing");
    const auto makeProbe = [&](int hz) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config; probe.config.fixedTickHz = hz;
        probe.config.computerAiEnabled = false;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.economy.owner = "Arabs";
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry, "DESO", {15, 15});
        initializeRuntimeNavigation(probe);
        ensureRuntimeSpecialMechanismsInitialized(probe);
        return storage;
    };
    const auto step = [](RuntimeWindowState& probe) { ++probe.tick; advanceRuntimeSimulation(probe); };
    for (int hz : {30, 60}) {
        for (const char* targetType : {"E1", "MTNK"}) {
            auto storage = makeProbe(hz);
            auto& probe = *storage;
            if (probe.world.getObjects().empty()) return fail("DESO fixture missing");
            const int desoId = probe.world.getObjects().front().instanceId;
            auto* victim = addRuntimeObjectInstanceForOwner(probe, "Americans",
                std::string(targetType) == "E1" ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit,
                targetType, {18, 15});
            if (victim == nullptr) return fail("ordinary attack target missing");
            const int targetId = victim->instanceId, health = victim->health;
            victim->mission = "Sleep"; victim->weaponCooldownTicksRemaining = 100000;
            applySelection(probe, {desoId}, false, false);
            issueRuntimeAttackCommand(probe, *victim);
            bool hit = false;
            for (int tick = 0; tick < hz * 4; ++tick) {
                step(probe);
                const auto* current = findRuntimeObjectByIdIncludingInactive(probe, targetId);
                hit = current == nullptr || current->health < health;
                if (!probe.specialMechanisms.radiationAreas().empty())
                    return fail("ordinary beam left a radiation site under its victim");
                if (hit) break;
            }
            if (!hit) return fail("ordinary beam stopped dealing direct damage");
            const auto* firingDeso = findObjectById(probe, desoId);
            if (!firingDeso || firingDeso->weaponCooldownTicksRemaining <= 0)
                return fail("ordinary attack cooldown fixture missing");
            if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("deploy while shooting rejected");
            const int transitionTicks = findObjectById(probe, desoId)->buildAnimTicksRemaining;
            for (int tick = 0; tick < transitionTicks + 1; ++tick) step(probe);
            if (probe.specialMechanisms.radiationAreas().size() != 1U)
                return fail("first deployment waited for ordinary fire cooldown or slow projectile");
            if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("combat undeploy rejected");
            const int foldTicks = findObjectById(probe, desoId)->buildAnimTicksRemaining;
            for (int tick = 0; tick < foldTicks; ++tick) {
                step(probe);
                if (tick + 1 < foldTicks && !runtimeMissionIsUndeploying(*findObjectById(probe, desoId)))
                    return fail("automatic enemy acquisition interrupted manual undeployment");
            }
            if (findObjectById(probe, desoId)->deployed) return fail("attacking Desolator remained deployed");
        }
        auto storage = makeProbe(hz);
        auto& probe = *storage;
        const int desoId = probe.world.getObjects().front().instanceId;
        if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("deploy command rejected");
        const int transitionTicks = findObjectById(probe, desoId)->buildAnimTicksRemaining;
        for (int tick = 0; tick < transitionTicks + 1; ++tick) step(probe);
        if (probe.specialMechanisms.radiationAreas().size() != 1U) return fail("deployment did not create one site");
        const int firstPulseTick = probe.tick;
        const int eruptionCooldown = findObjectById(probe, desoId)->radiationEruptionCooldownTicksRemaining;
        if (eruptionCooldown < hz * 3) return fail("eruption ROF did not start");
        const auto site = probe.specialMechanisms.radiationAreas().begin()->second;
        if (site.center.x != 15 || site.center.y != 15 || site.radius != 10 || site.strength != 500)
            return fail("deployed site did not use its own cell / RadLevel / CellSpread");
        auto damageState = probe.specialMechanisms;
        std::vector<WorldObjectInstance> targets;
        for (const auto& offset : std::vector<SpecialCell>{{0, 0}, {6, 0}, {8, 8}, {10, 0}}) {
            WorldObjectInstance target;
            target.instanceId = 201 + static_cast<int>(targets.size());
            target.kind = WorldObjectKind::Infantry; target.type = "E1";
            target.owner = "Americans"; target.alive = true; target.health = 125;
            target.x = 15 + offset.x; target.y = 15 + offset.y;
            targets.push_back(target);
        }
        const auto pulses = damageState.tickRadiation(targets, probe.rules, site.nextApplicationTick,
            [](const auto& a, const auto& b) { return a == b; }, [](int) { return true; }, nullptr);
        const auto findPulse = [&](int id) -> const RadiationDamageEvent* {
            for (const auto& pulse : pulses) if (pulse.targetObjectId == id) return &pulse;
            return nullptr;
        };
        const auto* center = findPulse(201); const auto* nearEdge = findPulse(202);
        if (center == nullptr || nearEdge == nullptr || center->intensity <= nearEdge->intensity
            || findPulse(203) != nullptr || findPulse(204) != nullptr)
            return fail("radiation damage retained a square footprint or lost radial attenuation");
        std::ostringstream saved;
        probe.specialMechanisms.save(saved);
        IniFile ini; ini.loadFromString(saved.str());
        RuntimeSpecialMechanismState restored;
        restored.reset(probe.rules.getSpecialMechanismRules());
        if (!restored.load(ini, error) || restored.stateHash() != probe.specialMechanisms.stateHash())
            return fail("deployed site save round trip failed: " + error);
        probe.specialMechanisms = std::move(restored);
        // Move to a fresh cell between deployments so refreshing an existing
        // site cannot conceal an extra pulse caused by resetting the timer.
        if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("cooldown undeploy rejected");
        const int foldTicks = findObjectById(probe, desoId)->buildAnimTicksRemaining;
        for (int i = 0; i < foldTicks; ++i) step(probe);
        auto* deso = findMutableObjectById(probe, desoId);
        deso->x = 16; deso->cell = runtimeCellKey(16, 15); clearRuntimeRenderPosition(*deso);
        if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("cooldown redeploy rejected");
        const int secondDeployTicks = findObjectById(probe, desoId)->buildAnimTicksRemaining;
        for (int i = 0; i < secondDeployTicks; ++i) step(probe);
        if (probe.specialMechanisms.radiationAreas().size() != 1U)
            return fail("redeployment bypassed eruption cooldown");
        std::ostringstream fullSave;
        if (!writeRuntimeStateToStream(probe, fullSave, error)) return fail("cooldown save: " + error);
        auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, probe, false);
        IniFile fullIni;
        if (!fullIni.loadFromString(fullSave.str()) || !materializeRuntimeLoadStateFromIni(*loaded, fullIni, error)
            || findObjectById(*loaded, desoId)->radiationEruptionCooldownTicksRemaining
                != findObjectById(probe, desoId)->radiationEruptionCooldownTicksRemaining)
            return fail("eruption cooldown save restore: " + error);
        const int minimumSecondPulseTick = firstPulseTick + eruptionCooldown;
        while (probe.tick < minimumSecondPulseTick) {
            step(probe); step(*loaded);
            if (probe.tick < minimumSecondPulseTick && probe.specialMechanisms.radiationAreas().size() != 1U)
                return fail("early second eruption");
        }
        for (int i = 0; i < 3; ++i) { step(probe); step(*loaded); }
        if (probe.specialMechanisms.radiationAreas().size() != 2U
            || loaded->specialMechanisms.stateHash() != probe.specialMechanisms.stateHash())
            return fail("second eruption or deterministic restored continuation failed");
        for (int tick = 0; tick < hz * 5; ++tick) step(probe);
        if (probe.specialMechanisms.radiationAreas().empty() || probe.specialMechanisms.radiationAreas().size() > 2U)
            return fail("continued deployment duplicated or lost its site");
        if (!executeRuntimeSecondaryAction(probe, desoId)) return fail("undeploy command rejected");
        for (int tick = 0; tick < 700; ++tick) step(probe);
        if (!probe.specialMechanisms.radiationAreas().empty()) return fail("undeployed radiation never dissipated");
        std::cout << "Desolator timing hz=" << hz << " first-pulse-after-animation=1 tick"
            << " redeploy-cooldown=" << eruptionCooldown << " combat-undeploy/save-resume=ok\n";
    }
    if (const char* output = std::getenv("CONCORD_DESOLATOR_TERRAIN_DIR"); output && *output) {
        auto storage = makeProbe(30); auto& probe = *storage;
        probe.report = live.report; probe.world = live.world;
        probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
        probe.mapFacts = live.mapFacts; probe.mapFactsLoaded = live.mapFactsLoaded;
        probe.world.accessObjects().clear();
        std::unordered_map<int, const TerrainCell*> cells;
        for (const auto& cell : probe.world.getTerrainCells()) cells[runtimeCellKey(cell.x, cell.y)] = &cell;
        RuntimeCellPoint center{15, 15}; int highestScore = -1;
        for (const auto& cell : probe.world.getTerrainCells()) {
            int score = 0, neighbors = 0;
            for (int dy : {-4, 0, 4}) for (int dx : {-4, 0, 4}) {
                const auto found = cells.find(runtimeCellKey(cell.x + dx, cell.y + dy));
                if (found != cells.end()) { ++neighbors; score += std::abs(cell.level - found->second->level); }
            }
            if (neighbors == 9 && score > highestScore) { highestScore = score; center = {cell.x, cell.y}; }
        }
        if (highestScore <= 0) return fail("native sloped terrain fixture missing");
        auto* deso = addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry, "DESO", center);
        if (!deso) return fail("native terrain Desolator missing");
        deso->deployed = true; deso->mission = "Deployed";
        initializeRuntimeNavigation(probe);
        std::vector<std::uint32_t> normal, irradiated;
        const std::string root(output);
        if (!drawRuntimeStateOffscreenAtCell(probe, center, 960, 660, error,
                root + "/terrain-normal.bmp", false, true, -1, &normal)) return fail(error);
        probe.specialMechanisms.spawnRadiationArea(deso->instanceId, deso->owner,
            {center.x, center.y}, 10, 500, 1, "RadSite", probe.tick, true, nullptr);
        const auto stats = drawRuntimeStateOffscreenAtCell(probe, center, 960, 660, error,
            root + "/terrain-radiation.bmp", false, true, -1, &irradiated);
        if (!stats || stats->texturedTerrainCells < 100 || stats->fallbackTerrainCells != 0
            || stats->radiationTintPixels < 10000 || normal == irradiated) return fail("native TMP terrain tint missing");
        std::cout << "Desolator native terrain: center=" << center.x << ',' << center.y
            << " height-variation=" << highestScore << " TMP-cells=" << stats->texturedTerrainCells
            << " illuminated-pixels=" << stats->radiationTintPixels << '\n';
    }
    RuntimeMapRenderContext context;
    context.rules = &live.rules; context.art = &live.art;
    context.assetRootPath = runtimeAssetRootPath(live.config);
    context.palettePath = runtimePalettePath(live.config, live.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    if (!runRuntimeDesolatorRenderContractCheck(context, error)) return false;
    std::cout << "Desolator: ordinary beam damage without residue; deployed radial field, 30/60Hz, save, upkeep, decay, original SHP and smooth tint=ok\n";
    return true;
}
