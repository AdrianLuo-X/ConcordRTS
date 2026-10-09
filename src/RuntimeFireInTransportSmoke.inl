bool runRuntimeFireInTransportSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& reason) { error = "FireInTransport: " + reason; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    for (const char* id : {"Punch", "Smash", "PunchE", "SmashE"}) {
        const auto* weapon = stock->rules.findWeaponType(id);
        if (!weapon || weapon->fireInTransport) return fail(std::string("original restriction: ") + id);
    }
    for (const char* id : {"M60", "MissileLauncher", "MissileLauncherE"}) {
        const auto* weapon = stock->rules.findWeaponType(id);
        if (!weapon || !weapon->fireInTransport) return fail(std::string("default permission: ") + id);
    }
    const std::string root = "build/smoke/feature-checks/04-fire-in-transport/checks";
    std::filesystem::create_directories(root);
    struct Fixture { std::unique_ptr<RuntimeWindowState> box; int host, brute, gunner, target; };
    const auto make = [&](int hz, bool elite, bool infantry) {
        Fixture f; f.box = std::make_unique<RuntimeWindowState>(); auto& s = *f.box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = false; s.config.noCooldown = false;
        s.config.multiplayerActive = false; s.config.shortGame = false; s.config.superweapons = false;
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 30; ++y) for (int x = 1; x <= 30; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        f.host = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "BFRT", {12,12})->instanceId;
        f.brute = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "BRUTE", {12,13})->instanceId;
        f.gunner = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "GGI", {13,13})->instanceId;
        f.target = addRuntimeObjectInstanceForOwner(s, "Russians",
            infantry ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit,
            infantry ? "E1" : "HTNK", {14,12})->instanceId;
        findMutableObjectById(s, f.target)->mission = "Sleep";
        findMutableObjectById(s, f.brute)->veterancy = elite ? 2 : 0;
        findMutableObjectById(s, f.gunner)->veterancy = elite ? 2 : 0;
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        return f;
    };
    const auto board = [&](Fixture& f) {
        auto& s = *f.box;
        for (int id : {f.brute, f.gunner}) {
            applySelection(s, {id}, false, false);
            if (!queueRuntimeTransportTarget(s, *findMutableObjectById(s, f.host))
                || !waitForRuntimeTransportSmokeMotion(s, f.host, id)) return false;
            eraseRuntimeBehavior(s, id);
        }
        return findObjectById(s, f.host)->transportPassengerIds.size() == 2;
    };
    const auto shot = [](RuntimeWindowState& s, int id) {
        return std::find_if(s.projectiles.begin(), s.projectiles.end(),
            [&](const auto& projectile) { return projectile.attackerObjectId == id; });
    };
    int cases = 0;
    for (int hz : {30,60}) for (bool elite : {false,true})
        for (bool infantry : {false,true}) for (bool manual : {false,true}) {
        auto f = make(hz, elite, infantry); auto& s = *f.box;
        if (!board(f)) return fail("real mixed boarding");
        if (manual) {
            applySelection(s, {f.host}, false, false);
            issueRuntimeAttackCommand(s, *findObjectById(s, f.target));
            // Force fire must not override this weapon restriction either.
            s.behaviors.at(f.host).forceFire = true;
        }
        auto* brute = findMutableRuntimeObjectByIdIncludingInactive(s, f.brute);
        const auto raw = runtimeWeaponProfileForObject(s, *brute, findObjectById(s, f.target));
        const std::string expected = std::string(infantry ? "Punch" : "Smash") + (elite ? "E" : "");
        if (!raw.weapon || raw.weapon->id != expected) return fail("ordinary Brute weapon selection");
        const auto pose = runtimeCombatFiringPose(*brute, findObjectById(s, f.host));
        if (runtimeCombatPassengerWeaponProfile(s, pose, findObjectById(s, f.target)).weapon)
            return fail("forbidden passenger profile survived");
        const int ammo = brute->currentAmmo;
        if (fireRuntimeIndependentCombatWeapon(s, *brute, findObjectById(s, f.host),
                *findObjectById(s, f.target), raw, true)
            || beginRuntimeCombatBurst(s, pose, *findObjectById(s, f.target), *raw.weapon,
                raw, 1.2, f.host, true, true) != 0)
            return fail("direct firing boundary accepted a forbidden weapon");
        (void)advanceRuntimeCombatIndependentFire(s);
        brute = findMutableRuntimeObjectByIdIncludingInactive(s, f.brute);
        if (shot(s, f.brute) != s.projectiles.end())
            return fail("forbidden Brute passenger fired");
        if (brute->alive || brute->weaponCooldownTicksRemaining != 0 || brute->currentAmmo != ammo
            || s.combatDetails.hasPendingBurst(f.brute)) return fail("forbidden fire changed passenger state");
        const auto legal = shot(s, f.gunner);
        if (legal == s.projectiles.end() || legal->weaponId != (elite ? "MissileLauncherE" : "MissileLauncher"))
            return fail("legal GGI passenger stopped firing");
        if (s.specialMechanisms.camouflageStates().contains(f.gunner))
            return fail("hidden passenger fire created an invalid camouflage entry");
        if (hz == 30 && elite && !infantry && manual) {
            const std::string path = root + "/passengers.ini";
            if (!saveRuntimeState(s, path, error)) return fail("passenger save: " + error);
            auto loaded = make(hz, elite, infantry);
            if (!loadRuntimeState(*loaded.box, path, error)) return fail("passenger load: " + error);
            auto& restored = *loaded.box;
            if (restored.combatDetails.deterministicHash() != s.combatDetails.deterministicHash())
                return fail("passenger combat state changed during load");
            const auto before = restored.projectiles.size();
            findMutableRuntimeObjectByIdIncludingInactive(restored, f.gunner)->weaponCooldownTicksRemaining = 0;
            (void)advanceRuntimeCombatIndependentFire(restored);
            if (restored.projectiles.size() <= before || shot(restored, f.brute) != restored.projectiles.end())
                return fail("passenger firing permissions after full load");
        }
        const int health = findObjectById(s, f.target)->health;
        const auto impact = *legal; (void)applyRuntimeProjectileImpact(s, impact);
        if (!findObjectById(s, f.target) || findObjectById(s, f.target)->health >= health)
            return fail("legal passenger projectile caused no damage");
        const auto resumeSnapshot = [&]() {
            std::vector<WorldObjectInstance> objects;
            for (int id : {f.host, f.brute, f.gunner, f.target})
                if (const auto* object = findMutableRuntimeObjectByIdIncludingInactive(s, id)) objects.push_back(*object);
            return objects;
        };
        const auto resumeBeforeUnload = resumeSnapshot();
        if (unloadRuntimeTransportSmokeNaturally(s, f.host, {f.brute, f.gunner}) != 2)
            return fail("real mixed unload");
        eraseRuntimeBehavior(s, f.host);
        findMutableObjectById(s, f.host)->mission = "Sleep";
        findMutableObjectById(s, f.gunner)->mission = "Sleep";
        const auto resumeAfterUnload = resumeSnapshot();
        const int resumeStartTick = s.tick, resumeNextActor = s.nextProjectileActorId;
        applySelection(s, {f.brute}, false, false);
        issueRuntimeAttackCommand(s, *findObjectById(s, f.target));
        const auto resumeCommand = s.lastCommand;
        for (int step = 0; step < hz * 10 && shot(s, f.brute) == s.projectiles.end(); ++step) {
            ++s.tick; advanceRuntimeSimulation(s);
        }
        if (shot(s, f.brute) == s.projectiles.end() || shot(s, f.brute)->weaponId != expected) {
            std::ostringstream detail; detail << std::setprecision(17);
            detail << "Brute did not resume ordinary attack after unloading"
                << " scene=" << cases << " hz=" << hz << " elite=" << elite
                << " infantry=" << infantry << " manual=" << manual << " expected=" << expected
                << " startTick=" << resumeStartTick << " endTick=" << s.tick
                << " command=" << resumeCommand.verb << " accepted=" << resumeCommand.acceptedCount
                << " rejected=" << resumeCommand.rejectedCount << " target=" << resumeCommand.targetObjectId
                << " nextActor=" << resumeNextActor << "->" << s.nextProjectileActorId
                << " finished=" << s.skirmishMatchFinished << " result=" << s.skirmishResult
                << " countryCount=" << s.skirmishCountryByOwner.size() << " status=" << s.simulationStatus;
            const auto describe = [&](const char* phase, const WorldObjectInstance& object, bool terrain) {
                const auto body = runtimeTransportMotionBody(object);
                detail << "\n " << phase << " id=" << object.instanceId << " type=" << object.type
                    << " owner=" << object.owner << " alive=" << object.alive << " hp=" << object.health
                    << " mission=" << object.mission << " cell=" << object.x << ',' << object.y
                    << " slot=" << object.subCell << " body=" << body.x << ',' << body.y << ',' << body.level
                    << " render=" << runtimeObjectRenderX(object) << ',' << runtimeObjectRenderY(object)
                    << ',' << runtimeObjectRenderLevel(object) << " pose=" << object.hasRenderPosition
                    << " active=" << object.renderMoveActive << " air=" << object.airborne
                    << " bridge=" << body.onBridge << " weaponClock=" << object.weaponCooldownTicksRemaining
                    << " cargo=";
                for (int id : object.transportPassengerIds) detail << id << ',';
                if (!terrain) return;
                const auto cell = std::find_if(s.world.getTerrainCells().begin(), s.world.getTerrainCells().end(),
                    [&](const TerrainCell& value) { return value.x == object.x && value.y == object.y; });
                if (cell == s.world.getTerrainCells().end()) { detail << " terrain=missing"; return; }
                const auto info = runtimeNavigationTerrainInfoForCell(s, *cell);
                detail << " tile=" << cell->tileIndex << '/' << cell->subTileIndex << " level=" << cell->level
                    << " land=" << static_cast<int>(info.movementLandType) << " ramp=" << info.movementRampType
                    << " passable=" << info.passable << " water=" << info.water << " shore=" << info.shore
                    << " cliff=" << info.cliff << " bridgeTerrain=" << info.bridge
                    << " mapPass=" << runtimeMapQueryPassabilityAllows(s, {object.x, object.y}, object);
            };
            for (const auto& object : resumeBeforeUnload) describe("beforeUnload", object, false);
            for (const auto& object : resumeAfterUnload) describe("afterUnload", object, false);
            for (int id : {f.host, f.brute, f.gunner, f.target}) {
                if (const auto* object = findMutableRuntimeObjectByIdIncludingInactive(s, id)) describe("failure", *object, true);
                else detail << "\n failure id=" << id << " missing";
            }
            const auto move = s.behaviors.find(f.brute);
            const auto* source = findObjectById(s, f.brute);
            const auto* target = findObjectById(s, f.target);
            if (move != s.behaviors.end()) {
                const auto& b = move->second;
                detail << "\n behavior kind=" << static_cast<int>(b.kind) << " state=" << static_cast<int>(b.state)
                    << " target=" << b.targetObjectId << " goal=" << b.moveTarget.x << ',' << b.moveTarget.y
                    << " force=" << b.forceFire << " auto=" << b.autoAcquired << " shared=" << b.pendingHasSharedGoal
                    << " allowedHost=" << b.pendingAllowedOccupiedObjectId << " transfer=" << static_cast<int>(b.transportTransferKind)
                    << " transferHost=" << b.transportTransferHostId << " pendingPath=" << b.pathRequestPending
                    << " blocked=" << b.blockedTicks << " replans=" << b.pathReplans << " nextRepath=" << b.nextRepathTick
                    << " moveClock=" << b.moveCooldownTicks << " weaponClock=" << b.weaponCooldownTicks
                    << " renderClock=" << b.renderMoveTicksRemaining << '/' << b.renderMoveTicksTotal << " path=";
                for (const auto& cell : b.path) detail << cell.x << ',' << cell.y << ';';
                if (source && !b.path.empty()) {
                    (void)runtimeTickOccupancyLookup(s);
                    const auto admission = runtimeGroundBodyTransitionAdmission(s, *source,
                        runtimeCellPointForObject(*source), b.path.front());
                    detail << " bodyBlocked=" << admission.bodyBlocked << " held=" << admission.held
                        << " destinationSlot=" << admission.destinationSubCell.value_or(-1);
                }
            } else detail << "\n behavior=missing";
            if (source && target) {
                const auto profile = runtimeWeaponProfileForObject(s, *source, target);
                detail << "\n ordinaryProfile=" << (profile.weapon ? profile.weapon->id : "none")
                    << " visible=" << runtimeObjectVisibleToOwner(s, source->owner, *target)
                    << " allied=" << runtimeOwnersAreAllied(s, source->owner, target->owner);
                if (profile.weapon) detail << " rawRange=" << profile.weapon->range
                    << " effectiveRange=" << runtimeEffectiveWeaponRange(s, *source, *target, *profile.weapon)
                    << " distance=" << runtimeWeaponDistanceToTarget(s, *source, *target, *profile.weapon)
                    << " canTarget=" << runtimeWeaponCanTargetObject(s, *profile.weapon, *target);
            }
            const auto describeProjectiles = [&](const char* phase, const auto& projectiles) {
                for (const auto& p : projectiles) if (p.attackerObjectId == f.brute || p.attackerObjectId == f.gunner)
                    detail << "\n " << phase << " actor=" << p.actorId << " source=" << p.attackerObjectId
                        << " target=" << p.targetObjectId << " weapon=" << p.weaponId << " projectile=" << p.projectileId
                        << " payload=" << p.damage << " spawn=" << p.spawnTick << " impact=" << p.impactTick
                        << " remaining=" << p.remainingTicks << " generation=" << p.generation << " parent=" << p.parentActorId;
            };
            describeProjectiles("activeProjectile", s.projectiles);
            describeProjectiles("expiredProjectile", s.recentExpiredProjectiles);
            return fail(detail.str());
        }
        ++cases;
    }
    // A modded old save can contain a round scheduled before the restriction existed.
    {
        auto f = make(30, false, false); auto& s = *f.box;
        if (!board(f)) return fail("legacy boarding");
        RuntimePendingWeaponBurst burst;
        burst.attackerObjectId = f.brute; burst.sourceAnchorObjectId = f.host;
        burst.targetObjectId = f.target; burst.targetX = 14; burst.targetY = 12;
        burst.targetKind = static_cast<int>(WorldObjectKind::Unit);
        burst.weaponSlot = static_cast<int>(RuntimeWeaponSlot::Secondary);
        burst.roundsRemaining = 1; burst.nextFireTick = 20; burst.independentFire = true;
        burst.owner = "Americans"; burst.sourceType = "BRUTE"; burst.weaponId = "Smash";
        // Stock Smash is single-shot. Only this fixture models a mod's burst.
        const_cast<WeaponType*>(s.rules.findWeaponType("Smash"))->burst = 2;
        s.combatDetails.bursts.emplace(f.brute, burst);
        std::ostringstream saved; s.combatDetails.save(saved);
        IniFile ini; RuntimeCombatDetailsState restored;
        if (!ini.loadFromString(saved.str()) || !restored.load(ini, error))
            return fail("legacy combat save roundtrip: " + error);
        if (!restored.validateWorld(s.rules, s.world.getObjects(), s.tick, error))
            return fail("legacy combat fixture is invalid: " + error);
        s.combatDetails = std::move(restored); s.tick = 1;
        (void)advanceRuntimeCombatBursts(s);
        if (s.combatDetails.hasPendingBurst(f.brute) || shot(s, f.brute) != s.projectiles.end())
            return fail("legacy forbidden burst was not cancelled before its next round");
        const_cast<WeaponType*>(s.rules.findWeaponType("Smash"))->fireInTransport = true;
        if (!s.combatDetails.load(ini, error)) return fail("permitted burst reload: " + error);
        s.tick = 20; (void)advanceRuntimeCombatBursts(s);
        if (shot(s, f.brute) == s.projectiles.end()) return fail("explicitly permitted burst did not continue");
        ++cases;
    }
    {
        auto f = make(30, false, false); auto& s = *f.box;
        if (!board(f)) return fail("fixed-slot boarding");
        // Only this in-memory fixture forces an explicit, forbidden secondary.
        auto* type = const_cast<UnitType*>(s.rules.findUnitType("InfantryType", "BRUTE"));
        type->openTransportWeapon = 1;
        (void)advanceRuntimeCombatIndependentFire(s);
        if (shot(s, f.brute) != s.projectiles.end() || shot(s, f.gunner) == s.projectiles.end())
            return fail("fixed forbidden slot fell back to an ordinary weapon");
        ++cases;
    }
    {
        const std::string base = "[InfantryTypes]\n0=SOLDIER\n[SOLDIER]\nPrimary=GUN\n[GUN]\nDamage=1\n";
        std::uint64_t defaultHash = 0;
        for (const std::string setting : {"", "FireInTransport=yes\n", "FireInTransport=no\n"}) {
            IniFile ini; RulesDatabase rules;
            if (!ini.loadFromString(base + setting) || !rules.loadFromIni(ini)) return fail("permission INI fixture");
            const bool allowed = setting != "FireInTransport=no\n";
            if (!rules.findWeaponType("GUN") || rules.findWeaponType("GUN")->fireInTransport != allowed)
                return fail("permission default or explicit parsing");
            if (setting.empty()) defaultHash = rules.getDetailRulesHash();
            else if ((rules.getDetailRulesHash() == defaultHash) != allowed)
                return fail("permission is missing from the rules compatibility hash");
        }
        ++cases;
    }
    std::string regressionError;
    if (!runRuntimeCombatDetailsSmokeProbe(regressionError)) return fail(regressionError);
    std::cout << "FireInTransport passed: " << cases << " cases; normal/elite, infantry/vehicle, 30/60 Hz, "
        "automatic/forced orders, real boarding/unload, legal damage, fixed slot, full save, old burst save, defaults/hash; "
        "combat scheduler regression passed\n";
    return true;
}
