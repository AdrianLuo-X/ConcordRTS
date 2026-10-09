#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeParatrooperTargetSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Paratrooper targeting: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("native resources");
    int cases = 0, frames = 0, saves = 0, aaHits = 0, groundHits = 0, lowAirFrames = 0;
    for (int hz : {15, 30, 60}) for (const char* type : {"E1", "HTNK", "MTNK", "LTNK", "SHK", "DESO", "FV", "GGI", "FLAKT"}) {
        const bool antiAir = std::string(type) == "FV" || std::string(type) == "GGI" || std::string(type) == "FLAKT";
        const std::string scene = std::string(type) + "/" + std::to_string(hz) + "Hz";
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, *assets, false); s.config = config; s.config.fixedTickHz = hz;
        s.config.playerSlots.clear(); s.config.playerCountry = "Russians";
        s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.superweapons = true; s.config.noCooldown = s.config.crates = s.config.shortGame = false;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Russians";
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 48; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);
        if (!addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Building, "GAAIRC", {5, 5})
            || !addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Building, "GAPOWR", {10, 5}))
            return fail(scene + " support hosts");
        for (auto& object : s.world.accessObjects()) object.buildAnimTicksRemaining = object.buildAnimTicksTotal = 0;
        ensureRuntimeSuperWeaponsInitialized(s);
        auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
        s.superWeapons.tick(context);
        if (!s.superWeapons.forceReady("Americans", "AmericanParaDropSpecial", &context)
            || !s.superWeapons.beginTargetSelection("Americans", "AmericanParaDropSpecial", context, &error)
            || !s.superWeapons.submitTarget("Americans", "AmericanParaDropSpecial", {{24, 24}, -1}, context, &error))
            return fail(scene + " native paradrop: " + error);
        RuntimeSnapshotActiveStateScope scope(s);
        int targetId = -1;
        for (int tick = 0; tick < hz * 10 && targetId < 0; ++tick) {
            ++s.tick; advanceRuntimeEffects(s);
            for (const auto& o : s.world.getObjects()) if (o.alive && o.mission == "ParadropDrop") { targetId = o.instanceId; break; }
        }
        auto* target = findMutableObjectById(s, targetId);
        if (!target || !target->airborne || target->kind != WorldObjectKind::Infantry) return fail(scene + " native airborne infantry");
        // Keep the native infantry alive long enough to observe AA hits and landing.
        target->health = 10000; const int initialHealth = target->health;
        const RuntimeCellPoint at = runtimeCellPointForObject(*target);
        const auto* native = s.rules.findUnitType("InfantryType", type);
        auto* actor = addRuntimeObjectInstanceForOwner(s, "Russians", native ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit,
            type, {at.x - 2, at.y});
        if (!actor) return fail(scene + " attacker"); const int actorId = actor->instanceId;
        actor->facing = actor->turretFacing = runtimeFacingFromDelta(2, 0); actor->mission = "Guard";
        actor->health = 10000;
        target = findMutableObjectById(s, targetId);
        s.selectedObjectIds = {actorId}; issueRuntimeAttackCommand(s, *target);
        if (!runtimeObjectIsAirTarget(s, *target)) return fail(scene + " native paratrooper classified ground; accepted="
            + std::to_string(s.lastCommand.acceptedCount));
        if ((s.lastCommand.acceptedCount > 0) != antiAir) return fail(scene + " manual attack gate");
        if (!antiAir) {
            issueRuntimeAttackCommand(s, *target, true);
            if (s.lastCommand.acceptedCount != 0) return fail(scene + " force attack bypassed air gate");
        }
        const auto checkDomain = [&](RuntimeWindowState& state, const WorldObjectInstance& victim) {
            RuntimeProjectileEffect ground; ground.ag = true; ground.aa = false;
            RuntimeProjectileEffect air; air.ag = false; air.aa = true;
            if (runtimeProjectileCanHitTarget(state, ground, victim) == victim.airborne
                || runtimeProjectileCanHitTarget(state, air, victim) != victim.airborne) return false;
            const auto* attacker = findObjectById(state, actorId);
            return attacker && (runtimeWeaponProfileForObject(state, *attacker, &victim).weapon != nullptr)
                == (antiAir || !victim.airborne);
        };
        const auto saveCheck = [&](const WorldObjectInstance& victim) {
            std::ostringstream out; if (!writeRuntimeStateToStream(s, out, error)) return false;
            IniFile ini; if (!ini.loadFromString(out.str())) return false;
            auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, s, false);
            if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return false;
            const auto* restored = findObjectById(*loaded, targetId);
            if (!restored || restored->airborne != victim.airborne || !checkDomain(*loaded, *restored)) return false;
            ++saves; return true;
        };
        if ((std::string(type) == "E1" || std::string(type) == "FV") && !saveCheck(*target)) return fail(scene + " airborne save: " + error);
        bool landed = false, sawHit = false;
        for (int tick = 0; tick < hz * 45; ++tick) {
            target = findMutableObjectById(s, targetId);
            if (!target || !checkDomain(s, *target)) return fail(scene + " continuous weapon/projectile domain");
            if (target->airborne && target->renderLevel > 0.0 && target->level == 0) ++lowAirFrames;
            ++s.tick; advanceRuntimeSimulation(s); ++frames;
            target = findMutableObjectById(s, targetId);
            if (!target) return fail(scene + " target died before landing");
            if (!target->airborne) { landed = true; break; }
            if (antiAir && !sawHit && target->health < initialHealth) {
                sawHit = true;
                s.selectedObjectIds = {actorId}; issueRuntimeStopCommand(s);
                actor = findMutableObjectById(s, actorId);
                if (!actor) return fail(scene + " AA attacker disappeared");
                actor->mission = "Sleep";
                actor->weaponCooldownTicksRemaining = hz * 1000;
            }
            if (!antiAir && (target->health != initialHealth
                || (s.behaviors.contains(actorId) && s.behaviors.at(actorId).targetObjectId == targetId)))
                return fail(scene + " ground weapon fired/acquired during descent");
        }
        if (!landed || (antiAir && !sawHit)) return fail(scene + " landing/real AA damage");
        if (antiAir) ++aaHits;
        if (!checkDomain(s, *target)) return fail(scene + " landing did not change target domain");
        if ((std::string(type) == "E1" || std::string(type) == "FV") && !saveCheck(*target)) return fail(scene + " landed save: " + error);
        if (!antiAir) {
            const int groundHealth = target->health;
            s.selectedObjectIds = {actorId}; issueRuntimeAttackCommand(s, *target);
            if (s.lastCommand.acceptedCount != 1) return fail(scene + " landed infantry rejected attack");
            // Guard can fire on the exact frame the support system lands the
            // soldier, before this explicit post-landing command is issued.
            bool hit = groundHealth < initialHealth;
            for (int tick = 0; tick < hz * 5 && !hit; ++tick) {
                ++s.tick; advanceRuntimeSimulation(s);
                const auto* victim = findRuntimeObjectByIdIncludingInactive(s, targetId);
                hit = victim && victim->health < groundHealth;
            }
            if (!hit) {
                const auto* victim = findRuntimeObjectByIdIncludingInactive(s, targetId);
                const auto* attacker = findRuntimeObjectByIdIncludingInactive(s, actorId);
                return fail(scene + " ground weapon did not damage landed infantry; landingHealth="
                    + std::to_string(groundHealth) + " finalHealth=" + std::to_string(victim ? victim->health : -1)
                    + " attacker=" + (attacker ? attacker->mission : "missing"));
            }
            ++groundHits;
        }
        ++cases;
        std::cout << "Paratrooper target " << scene << " manual/force/auto/projectile/landing PASS\n";
    }
    if (lowAirFrames == 0) return fail("no above-ground descent frames at rounded level zero");
    std::cout << "Paratrooper targeting passed: " << cases << " native drops, " << frames << " descent frames, "
        << groundHits << " landed ground hits, " << aaHits << " airborne AA hits, " << saves << " saves, " << lowAirFrames << " low airborne frames\n";
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
