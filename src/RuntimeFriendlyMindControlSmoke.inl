bool runRuntimeFriendlyMindControlSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Friendly mind control: " + why; return false; };
    const char* output = std::getenv("CONCORD_FRIENDLY_MIND_CONTROL_OUTPUT");
    if (output == nullptr || *output == '\0') return fail("CONCORD_FRIENDLY_MIND_CONTROL_OUTPUT is required");
    const std::filesystem::path root(output);
    std::filesystem::create_directories(root);
    std::ofstream cases(pathString(root / "cases.csv"));
    std::ofstream actions(pathString(root / "actions.csv"));
    if (!cases || !actions) return fail("CSV output could not be opened");
    cases << "type,rank,hz,kind,source_id,target_id,weapon,warhead,capacity,infinite,begin_tick,end_tick,actor,status\n";
    actions << "type,rank,hz,phase,tick,source_id,target_id,weapon,warhead,accepted,rejected,owner,hp,links,actor,result\n";
    const auto quoted = [](const std::string& text) {
        std::string result = "\"";
        for (const char c : text) { if (c == '"') result += '"'; result += c; }
        return result + '"';
    };
    auto loaded = std::make_unique<RuntimeWindowState>();
    loaded->config = config;
    loaded->config.computerAiEnabled = false;
    loaded->config.multiplayerActive = false;
    loaded->config.playerSlots.clear();
    loaded->config.gameMode = "STANDARD";
    loaded->report = buildRuntimeStartupReport(loaded->config, &loaded->world, &loaded->theater,
        &loaded->art, &loaded->rules, &loaded->mapFacts);
    if (!loaded->report.success) return fail("original startup failed");
    std::cout << "Friendly mind control rules: " << loaded->report.rulesPath << '\n';
    loaded->rulesLoaded = loaded->report.rulesLoaded; loaded->artLoaded = loaded->report.artLoaded;
    loaded->theaterLoaded = loaded->report.theaterLoaded; loaded->mapFactsLoaded = loaded->report.map.loaded;
    if (!loaded->rulesLoaded || !loaded->artLoaded || !loaded->theaterLoaded || !loaded->mapFactsLoaded)
        return fail("original rules/art/theater/map not fully loaded");
    struct Stock { const char* type; RuntimeBuildKind kind; int rank; const char* weapon; int capacity; bool infinite; };
    const std::array<Stock, 6> stock{{
        {"YURI", RuntimeBuildKind::Infantry, 0, "MindControl", 1, false},
        {"YURIPR", RuntimeBuildKind::Infantry, 0, "SuperMindControl", 1, false},
        {"MIND", RuntimeBuildKind::Unit, 0, "MultipleMindControlTank", 3, true},
        {"YAPSYT", RuntimeBuildKind::Building, 0, "MultipleMindControlTower", 3, false},
        {"PTROOP", RuntimeBuildKind::Infantry, 0, "MindControl", 1, false},
        {"PTROOP", RuntimeBuildKind::Infantry, 2, "MindControlE", 10, false}
    }};
    struct Fixture { std::unique_ptr<RuntimeWindowState> storage; int source = -1; int own = -1; int allied = -1; };
    const auto make = [&](const Stock& spec, int hz) {
        Fixture f; f.storage = std::make_unique<RuntimeWindowState>(); auto& s = *f.storage;
        seedRuntimeLoadScratch(s, *loaded, false);
        s.config = loaded->config; s.config.fixedTickHz = hz; s.config.playerCountry = "YuriCountry";
        s.config.noCooldown = false; s.economy.owner = "YuriCountry";
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimeSoundsEnabled = false; s.visionEnabled = false;
        s.world = loaded->world; s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        // The stock cursor fixture's synthetic flat land; preserve original rules/art/theater.
        for (int y = 44; y <= 88; ++y) for (int x = 44; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](const std::string& owner, RuntimeBuildKind kind,
                const std::string& type, RuntimeCellPoint cell) {
            auto* object = addRuntimeObjectInstanceForOwner(s, owner, kind, type, cell);
            if (object == nullptr) return -1;
            object->mission = "Sleep"; object->weaponCooldownTicksRemaining = 100000;
            object->buildAnimTicksRemaining = object->buildAnimTicksTotal = 0;
            clearRuntimeRenderPosition(*object); return object->instanceId;
        };
        f.source = add("YuriCountry", spec.kind, spec.type, {60, 64});
        f.own = add("YuriCountry", RuntimeBuildKind::Unit, "MTNK", {64, 66});
        f.allied = add("Americans", RuntimeBuildKind::Unit, "MTNK", {66, 64});
        const int power = add("YuriCountry", RuntimeBuildKind::Building, "NANRCT", {54, 58});
        if (f.source >= 0) {
            auto* source = findMutableObjectById(s, f.source);
            source->veterancy = spec.rank; source->weaponCooldownTicksRemaining = 0;
        }
        s.skirmishTeamByOwner["yuricountry"] = "friendly";
        s.skirmishTeamByOwner["americans"] = "friendly";
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        if (power < 0) f.source = -1;
        return f;
    };
    int profiles = 0, friendlyForceControlBlocked = 0, forceRejected = 0, friendlySecondaryAccepted = 0;
    int guardWindows = 0, naturalControls = 0, released = 0;
    int directImpacts = 0, ownCoreRejected = 0, garrisonRejected = 0, garrisonEnemy = 0;
    int immuneRejected = 0, capacityRejected = 0, infiniteAccepted = 0, existingRejected = 0;
    int ordinaryHits = 0, fallbackProfiles = 0;
    const auto row = [&](const Stock& spec, int hz, const std::string& phase, RuntimeWindowState& s,
            int sourceId, int targetId, const RuntimeWeaponProfile& profile, int actor, const std::string& result) {
        const auto* target = findRuntimeObjectByIdIncludingInactive(s, targetId);
        actions << spec.type << ',' << spec.rank << ',' << hz << ',' << phase << ',' << s.tick << ','
            << sourceId << ',' << targetId << ',' << (profile.weapon ? profile.weapon->id : "") << ','
            << (profile.warhead ? profile.warhead->id : "") << ',' << s.lastCommand.acceptedCount << ','
            << s.lastCommand.rejectedCount << ',' << quoted(target ? target->owner : "missing") << ','
            << (target ? target->health : -1) << ',' << s.specialMechanisms.mindControlLinks().size() << ','
            << actor << ',' << quoted(result) << '\n';
    };
    const auto isControl = [](const RuntimeWeaponProfile& p) { return p.warhead && p.warhead->mindControl; };
    const auto step = [](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    const auto observe = [](const RuntimeWindowState& s, int source, int target, const std::string& weapon) {
        int actor = -1;
        const auto scan = [&](const auto& list) {
            for (const auto& p : list)
                if (p.attackerObjectId == source && p.targetObjectId == target && p.weaponId == weapon)
                    actor = std::max(actor, p.actorId);
        };
        scan(s.projectiles); scan(s.recentExpiredProjectiles);
        return actor;
    };
    for (const int hz : {30, 60}) for (const auto& spec : stock) {
        auto f = make(spec, hz); if (f.source < 0 || f.own < 0 || f.allied < 0) return fail("stock fixture " + std::string(spec.type));
        auto& s = *f.storage; RuntimeSnapshotActiveStateScope active(s);
        const int beginTick = s.tick;
        if (!runtimeOwnersAreAllied(s, "YuriCountry", "Americans")) return fail("actual allied relation missing");
        for (const int targetId : {f.own, f.allied}) {
            const auto* source = findObjectById(s, f.source); const auto* target = findObjectById(s, targetId);
            const auto normal = runtimeWeaponProfileForObject(s, *source, target, false);
            const auto forced = runtimeWeaponProfileForObject(s, *source, target, true);
            // Only MindControl is forbidden. A real non-control secondary is not globally banned.
            if (isControl(normal) || isControl(forced)) return fail("friendly MC profile " + std::string(spec.type));
            const int hp = target->health; const std::string owner = target->owner;
            if (!s.specialMechanisms.mindControlLinks().empty()) return fail("friendly fixture already has a control link");
            const auto links = s.specialMechanisms.stateHash(); const int actor = s.nextProjectileActorId;
            applySelection(s, {f.source}, false, false);
            const bool accepted = dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Attack,
                runtimeCellPointForObject(*target), target);
            row(spec, hz, targetId == f.own ? "own-force" : "allied-force", s, f.source, targetId, forced, -1, accepted ? "accepted" : "rejected");
            // The selected non-control secondary remains legal; a missing weapon must still reject.
            const bool secondaryAccepted = forced.weapon != nullptr;
            target = findObjectById(s, targetId);
            if (accepted != secondaryAccepted || s.lastCommand.acceptedCount != (secondaryAccepted ? 1 : 0)
                || s.lastCommand.rejectedCount != (secondaryAccepted ? 0 : 1)
                || !target || target->owner != owner || target->health != hp
                || !s.specialMechanisms.mindControlLinks().empty()
                || (!secondaryAccepted && s.specialMechanisms.stateHash() != links) || s.nextProjectileActorId != actor)
                return fail("friendly force result/authority differs selected non-control weapon " + std::string(spec.type));
            ++friendlyForceControlBlocked;
            if (secondaryAccepted) ++friendlySecondaryAccepted; else ++forceRejected;
        }
        applySelection(s, {f.source}, false, false); issueRuntimeStopCommand(s);
        const int beforeGuardActor = s.nextProjectileActorId;
        if (s.lastCommand.acceptedCount != 1) return fail("original Stop order rejected");
        for (int tick = 0; tick < runtimeRuleFramesToTicks(4, hz); ++tick) step(s);
        if (!s.specialMechanisms.mindControlLinks().empty() || s.nextProjectileActorId != beforeGuardActor)
            return fail("Guard acquired a friendly target");
        row(spec, hz, "friendly-guard-window", s, f.source, f.own, RuntimeWeaponProfile{}, -1, "not acquired");
        ++guardWindows;
        std::vector<WorldObjectInstance> coreObjects = s.world.getObjects();
        RuntimeSpecialMechanismState core; core.reset(s.rules.getSpecialMechanismRules());
        for (const bool upperOwner : {false, true}) {
            for (auto& object : coreObjects) if (object.instanceId == f.own) object.owner = upperOwner ? "YURICOUNTRY" : "YuriCountry";
            const auto before = core.stateHash(); std::string reason;
            if (core.tryMindControl(coreObjects, s.rules, f.source, f.own, spec.capacity, false, spec.infinite,
                    false, s.tick, nullptr, nullptr, reason) || core.stateHash() != before)
                return fail("core own target accepted without alliance callback");
            ++ownCoreRejected;
        }
        auto* enemy = addRuntimeObjectInstanceForOwner(s, "Russians", RuntimeBuildKind::Unit, "MTNK", {64, 64});
        if (!enemy) return fail("stock enemy spawn");
        const int enemyId = enemy->instanceId; const int enemyHp = enemy->health;
        enemy->mission = "Sleep"; enemy->weaponCooldownTicksRemaining = 100000; clearRuntimeRenderPosition(*enemy);
        auto* source = findMutableObjectById(s, f.source);
        std::string authoredWeapon;
        if (source->kind == WorldObjectKind::Structure) {
            const auto* type = s.rules.findBuildingType(source->type);
            if (type) authoredWeapon = type->primaryWeapon;
        } else {
            const auto* type = findRuntimeUnitType(s.rules, *source);
            if (type) authoredWeapon = spec.rank >= 2 && !type->elitePrimaryWeapon.empty()
                ? type->elitePrimaryWeapon : type->primaryWeapon;
        }
        const auto* authored = findRuntimeWeaponType(s.rules, authoredWeapon);
        if (!authored || authoredWeapon != spec.weapon || authored->damage != spec.capacity
            || authored->infiniteMindControl != spec.infinite) return fail("actual loaded stock weapon/rank authority differs");
        const auto original = runtimeWeaponProfileForObject(s, *source, enemy, false);
        if (!isControl(original) || !original.weapon || original.weapon->id != spec.weapon
            || original.weapon->damage != spec.capacity || original.weapon->infiniteMindControl != spec.infinite)
            return fail("original control weapon/rank changed");
        // Deliberately bypass only the new player profile: exercise stale/raw original projectile impact authority.
        source->mission = "Sleep";
        for (const int targetId : {f.own, f.allied}) {
            const auto* target = findObjectById(s, targetId); const int hp = target->health; const std::string owner = target->owner;
            const auto beforeLinks = s.specialMechanisms.stateHash(); const int expectedActor = s.nextProjectileActorId;
            spawnRuntimeProjectile(s, *findObjectById(s, f.source), *target, *original.weapon, original, original.weapon->damage);
            if (s.nextProjectileActorId == expectedActor) return fail("direct original control projectile not spawned");
            int ticks = 0; while (std::any_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) {
                    return p.attackerObjectId == f.source && p.targetObjectId == targetId;
                }) && ticks++ < 10 * hz) { ++s.tick; advanceRuntimeEffects(s); }
            target = findObjectById(s, targetId);
            row(spec, hz, "direct-friendly-impact", s, f.source, targetId, original, expectedActor, "rejected");
            if (ticks > 10 * hz || !target || target->owner != owner || target->health != hp
                || s.specialMechanisms.stateHash() != beforeLinks) return fail("friendly original impact changed authority");
            ++directImpacts;
        }
        applySelection(s, {f.source}, false, false);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*findObjectById(s, enemyId)), findObjectById(s, enemyId))
            || s.lastCommand.acceptedCount != 1) return fail("enemy Primary order rejected");
        int actualActor = observe(s, f.source, enemyId, spec.weapon), ticks = 0;
        while (!s.specialMechanisms.mindControlLinks().contains(enemyId) && ticks++ < 10 * hz) {
            step(s); actualActor = std::max(actualActor, observe(s, f.source, enemyId, spec.weapon));
        }
        enemy = findMutableObjectById(s, enemyId);
        const auto link = s.specialMechanisms.mindControlLinks().find(enemyId);
        if (ticks > 10 * hz || actualActor <= 0 || !enemy || !enemy->alive || enemy->health != enemyHp
            || enemy->owner != "YuriCountry" || link == s.specialMechanisms.mindControlLinks().end()
            || link->second.controllerObjectId != f.source || link->second.originalOwner != "Russians")
            return fail("natural enemy control missing original actor/link/undamaged ownership");
        row(spec, hz, "enemy-natural-control", s, f.source, enemyId, original, actualActor, "controlled"); ++naturalControls;
        if (!s.specialMechanisms.releaseMindControlByController(s.world.accessObjects(), f.source, s.tick, nullptr, "friendly control smoke release"))
            return fail("original release failed");
        enemy = findMutableObjectById(s, enemyId);
        if (!enemy || enemy->owner != "Russians" || enemy->health != enemyHp || !s.specialMechanisms.mindControlLinks().empty())
            return fail("release did not restore original enemy owner/health");
        ++released;
        cases << spec.type << ',' << spec.rank << ',' << hz << ",stock," << f.source << ',' << enemyId << ','
            << original.weapon->id << ',' << original.warhead->id << ',' << spec.capacity << ',' << spec.infinite << ','
            << beginTick << ',' << s.tick << ',' << actualActor << ",PASS\n"; ++profiles;
    }
    for (const int hz : {30, 60}) {
        for (const auto& spec : {stock[2], stock[3]}) {
            auto f = make(spec, hz); if (f.source < 0) return fail("capacity fixture"); auto& s = *f.storage;
            RuntimeSnapshotActiveStateScope active(s); RuntimeSpecialMechanismState core;
            core.reset(s.rules.getSpecialMechanismRules()); std::vector<WorldObjectInstance> objects = s.world.getObjects();
            std::vector<int> targets; int next = 1000;
            for (int i = 0; i < 4; ++i) { auto target = *findObjectById(s, f.own); target.instanceId = next++; target.owner = "Russians"; objects.push_back(target); targets.push_back(target.instanceId); }
            const auto allied = [&](const std::string& a, const std::string& b) { return runtimeOwnersAreAllied(s, a, b); };
            for (int i = 0; i < 4; ++i) { std::string reason;
                const bool accepted = core.tryMindControl(objects, s.rules, f.source, targets[i], spec.capacity,
                    false, spec.infinite, false, s.tick, allied, nullptr, reason);
                if (accepted != (spec.infinite || i < 3)) return fail("original finite/infinite capacity changed");
                if (spec.infinite) ++infiniteAccepted; else if (!accepted) ++capacityRejected;
            }
            const auto unchanged = core.stateHash(); std::string reason;
            if (core.tryMindControl(objects, s.rules, f.source, targets.front(), spec.capacity, false, spec.infinite,
                    false, s.tick, allied, nullptr, reason) || core.stateHash() != unchanged) return fail("existing controlled link accepted/mutated");
            ++existingRejected;
            auto immune = *findObjectById(s, f.own); immune.instanceId = next++; immune.kind = WorldObjectKind::Infantry;
            immune.type = "YURI"; immune.owner = "Russians"; objects.push_back(immune);
            const auto* immuneType = findRuntimeUnitType(s.rules, immune);
            if (!immuneType || !immuneType->immuneToPsionics || core.tryMindControl(objects, s.rules, f.source, immune.instanceId,
                    spec.capacity, false, spec.infinite, false, s.tick, allied, nullptr, reason) || core.stateHash() != unchanged)
                return fail("original psionic immunity changed");
            ++immuneRejected;
            cases << spec.type << ',' << spec.rank << ',' << hz << ",core-capacity," << f.source << ',' << targets.back()
                << ',' << spec.weapon << ",Controller," << spec.capacity << ',' << spec.infinite << ',' << s.tick << ',' << s.tick << ",-1,PASS\n";
        }
        auto gf = make(stock[0], hz); auto& g = *gf.storage; RuntimeSnapshotActiveStateScope active(g);
        IniFile originalIni, overlay;
        if (!loadRuntimeIniSource(config, RuntimeCampaignIniKind::Rules, originalIni)
            || !overlay.loadFromString("[E1]\nOccupier=yes\nOccupyWeapon=MindControl\nEliteOccupyWeapon=MindControl\n"))
            return fail("isolated garrison rules copy");
        originalIni.appendFrom(overlay); if (!g.rules.loadFromIni(originalIni)) return fail("isolated garrison rules parse");
        auto* occupant = addRuntimeObjectInstanceForOwner(g, "YuriCountry", RuntimeBuildKind::Infantry, "E1", {62, 62});
        if (!occupant) return fail("garrison stock occupant"); const int occupantId = occupant->instanceId;
        auto* enemy = addRuntimeObjectInstanceForOwner(g, "Russians", RuntimeBuildKind::Unit, "MTNK", {64, 64});
        if (!enemy) return fail("garrison stock target"); const int enemyId = enemy->instanceId;
        occupant = findMutableObjectById(g, occupantId);
        for (const int target : {gf.own, gf.allied}) for (const bool force : {false, true}) {
            if (runtimeGarrisonWeaponProfileForOccupant(g, *occupant, findObjectById(g, target), force).weapon != nullptr)
                return fail("garrison friendly MC fell through original slot order"); ++garrisonRejected;
        }
        const auto gp = runtimeGarrisonWeaponProfileForOccupant(g, *occupant, findObjectById(g, enemyId), true);
        if (!isControl(gp) || !gp.weapon || gp.weapon->id != "MindControl") return fail("garrison enemy MC profile changed"); ++garrisonEnemy;
        cases << "E1,0," << hz << ",memory-garrison-profile," << occupantId << ',' << enemyId << ",MindControl,Controller,1,0," << g.tick << ',' << g.tick << ",-1,PASS\n";
    }
    for (const int hz : {30, 60}) {
        const Stock tank{"MTNK", RuntimeBuildKind::Unit, 0, "105mm", 0, false}; auto f = make(tank, hz);
        if (f.source < 0) return fail("ordinary force fixture"); auto& s = *f.storage; RuntimeSnapshotActiveStateScope active(s);
        const auto* target = findObjectById(s, f.own); const int hp = target->health;
        const auto p = runtimeWeaponProfileForObject(s, *findObjectById(s, f.source), target, true);
        if (!p.weapon || isControl(p)) return fail("ordinary force weapon changed");
        applySelection(s, {f.source}, false, false);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Attack,
                runtimeCellPointForObject(*target), target) || s.lastCommand.acceptedCount != 1) return fail("ordinary own force rejected");
        int actor = -1, ticks = 0;
        while ((target = findObjectById(s, f.own)) != nullptr && target->health == hp && ticks++ < 10 * hz) {
            step(s); actor = std::max(actor, observe(s, f.source, f.own, p.weapon->id));
        }
        target = findObjectById(s, f.own);
        if (ticks > 10 * hz || actor <= 0 || !target || !target->alive || target->owner != "YuriCountry" || target->health >= hp
            || !s.specialMechanisms.mindControlLinks().empty()) return fail("ordinary own force caused no original damage");
        row(tank, hz, "ordinary-own-force", s, f.source, f.own, p, actor, "damaged"); ++ordinaryHits;
        cases << "MTNK,0," << hz << ",ordinary-own-force," << f.source << ',' << f.own << ',' << p.weapon->id << ','
            << p.warhead->id << ",0,0,0," << s.tick << ',' << actor << ",PASS\n";
        auto yf = make(stock[0], hz); auto& y = *yf.storage; RuntimeSnapshotActiveStateScope yActive(y);
        auto* organic = addRuntimeObjectInstanceForOwner(y, "YuriCountry", RuntimeBuildKind::Infantry, "E2", {60, 65});
        if (!organic) return fail("PsiWave organic actor");
        const auto fallback = runtimeWeaponProfileForObject(y, *findObjectById(y, yf.source), organic, true);
        if (!fallback.weapon || !fallback.warhead || fallback.weapon->id != "PsiWave" || fallback.warhead->mindControl
            || fallback.warhead->id != "PsiPulse") return fail("authored non-control PsiWave fallback removed"); ++fallbackProfiles;
        cases << "YURI,0," << hz << ",ordinary-secondary-profile," << yf.source << ',' << organic->instanceId
            << ",PsiWave,PsiPulse,0,0,0," << y.tick << ",-1,PASS\n";
    }
    cases.flush(); actions.flush(); if (!cases.good() || !actions.good()) return fail("CSV writing failed");
    if (profiles != 12 || friendlyForceControlBlocked != 24 || forceRejected != 20 || friendlySecondaryAccepted != 4
        || guardWindows != 12 || naturalControls != 12 || released != 12
        || directImpacts != 24 || ownCoreRejected != 24 || garrisonRejected != 8 || garrisonEnemy != 2
        || immuneRejected != 4 || capacityRejected != 2 || infiniteAccepted != 8 || existingRejected != 4
        || ordinaryHits != 2 || fallbackProfiles != 2) return fail("finite matrix cardinality");
    std::cout << "Friendly mind control passed: sourceRankClocks=" << profiles
        << " friendlyForceControlBlocked=" << friendlyForceControlBlocked << " friendlyForceRejected=" << forceRejected
        << " friendlySecondaryAccepted=" << friendlySecondaryAccepted
        << " guardWindows=" << guardWindows << " naturalEnemyControls=" << naturalControls << " releases=" << released
        << " directFriendlyImpactsRejected=" << directImpacts << " ownWithoutCallbackRejected=" << ownCoreRejected
        << " garrisonRejected=" << garrisonRejected << " garrisonEnemyProfiles=" << garrisonEnemy
        << " immuneRejected=" << immuneRejected << " finiteCapacityRejected=" << capacityRejected
        << " infiniteAccepted=" << infiniteAccepted << " existingRejected=" << existingRejected
        << " ordinaryOwnForceHits=" << ordinaryHits << " PsiWaveFallbackProfiles=" << fallbackProfiles << '\n';
    return true;
}
