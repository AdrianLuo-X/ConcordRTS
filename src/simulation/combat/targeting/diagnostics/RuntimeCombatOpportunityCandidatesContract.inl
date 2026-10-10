// Compares the production shortlist with the original target walk. This is a
// correctness/work-count probe, not a timed game or a frame-rate benchmark.
bool runRuntimeCombatOpportunityCandidatesContract(const RulesDatabase& rules, std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "combat opportunity spatial candidates: " + why;
        return false;
    };
    auto owned = std::make_unique<RuntimeWindowState>();
    auto& state = *owned;
    RuntimeSnapshotActiveStateScope active(state);
    state.rules = rules;
    state.rulesLoaded = true;
    state.config.fixedTickHz = 15;
    state.economy.owner = "Player";
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.tick = 10;
    state.structureFootprints["TARGET"] = {1, 1, true};
    for (int y = -16; y < 32; ++y) for (int x = -16; x < 32; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(cell);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, const char* type, const char* owner,
        WorldObjectKind kind, int x, int y, bool alive = true) {
        WorldObjectInstance object;
        object.instanceId = id; object.type = type; object.owner = owner;
        object.kind = kind; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.alive = alive; object.health = 1000; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, "AEGIS", "Player", WorldObjectKind::Unit, 4, 6);
    // Equal-distance ids deliberately arrive out of id and spatial order.
    add(3, "TARGET", "Enemy", WorldObjectKind::Structure, 6, 6);
    add(2, "TARGET", "Enemy", WorldObjectKind::Structure, 4, 8);
    add(4, "TARGET", "Neutral", WorldObjectKind::Structure, 4, 7);
    add(5, "TARGET", "Player", WorldObjectKind::Structure, 4, 6);
    add(6, "TARGET", "Enemy", WorldObjectKind::Structure, -27, 6);
    add(7, "AIRTARGET", "Enemy", WorldObjectKind::Unit, 8, 6);
    findMutableObjectById(state, 7)->airborne = true;
    for (int i = 0; i < 256; ++i)
        add(1000 + i, "TARGET", "Enemy", WorldObjectKind::Structure, 128 + i % 16, 128 + i / 16);
    for (int i = 0; i < 256; ++i)
        add(2000 + i, "TARGET", "Enemy", WorldObjectKind::Structure, 4, 6, false);
    const auto pose = *findObjectById(state, 1);
    const auto* radial = state.rules.findWeaponType("RADIAL");
    if (!radial) return fail("RADIAL fixture missing");
    RuntimeCombatOpportunityCandidates index;
    int cases = 0;
    const auto check = [&](const WorldObjectInstance& source, const WeaponType* weapon,
        double bonus, bool distributed, int preferred, bool force, bool passenger,
        RuntimeCombatOpportunityCandidates& candidates) {
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto psychicHash = state.psychicStatuses.stateHash();
        const auto combatHash = state.combatDetails.deterministicHash();
        const auto* reference = selectRuntimeCombatOpportunityTarget(state, source, weapon,
            bonus, distributed, preferred, force, passenger, true);
        const int referenceId = reference ? reference->instanceId : -1;
        const auto* optimized = selectRuntimeCombatOpportunityTarget(state, source, weapon,
            bonus, distributed, preferred, force, passenger, false, nullptr, &candidates);
        ++cases;
        return referenceId == (optimized ? optimized->instanceId : -1)
            && worldHash == state.world.deterministicSimulationStateHash()
            && psychicHash == state.psychicStatuses.stateHash()
            && combatHash == state.combatDetails.deterministicHash();
    };
    for (int cursor : {-1, 2, 3, 9999}) {
        state.combatDetails.fireCursors[1].lastTargetObjectId = cursor;
        for (bool distributed : {false, true}) for (bool force : {false, true})
            for (int preferred : {-1, 3, 4, 5, 1000})
                if (!check(pose, nullptr, 0, distributed, preferred, force, false, index))
                    return fail("nearest/preferred/force/distributed order changed");
    }
    const bool expectsIndex = !runtimeCombatOpportunityCandidatesReference()
        && !runtimeFrameScalingReferenceMovement()
        && !(std::getenv("CONCORD_MOVEMENT_REFERENCE")
            && std::string(std::getenv("CONCORD_MOVEMENT_REFERENCE")) == "1");
    if (expectsIndex && (index.populationBuilds != 1 || index.indexedQueries != static_cast<std::uint64_t>(cases)
        || index.candidateVisits > static_cast<std::uint64_t>(cases) * 16))
        return fail("quiet pass did not reuse a bounded shortlist across historic/far objects");

    // Mathematical completeness at bucket edges and across extended foundations:
    // everything retained by the exact scalar bound must occur in the shortlist.
    int geometryCases = 0;
    for (int width : {1, 7, 32}) {
        state.structureFootprints["TARGET"] = {width, 3, true};
        index.invalidate();
        for (int sourceX : {-9, -8, -1, 0, 7, 8, 15, 16}) for (int level : {0, 4, 20}) {
            auto source = pose; source.x = sourceX; source.level = level;
            const RuntimeCombatOpportunityRangeBound bound(state, source, nullptr, 0, false);
            const auto* shortlist = index.query(state, source, bound);
            if (!shortlist) return fail("ordinary boundary fixture unexpectedly fell back");
            if (!std::is_sorted(shortlist->begin(), shortlist->end())) return fail("world order lost");
            for (std::size_t i = 0; i < state.world.getObjects().size(); ++i) {
                const auto& object = state.world.getObjects()[i];
                if (object.alive && object.health > 0 && !bound.excludes(state, source, object)
                    && !std::binary_search(shortlist->begin(), shortlist->end(), i))
                    return fail("foundation/elevation/air/negative bucket boundary omitted a candidate");
                ++geometryCases;
            }
            for (bool distributed : {false, true})
                if (!check(source, nullptr, 0, distributed, 6, true, false, index))
                    return fail("foundation/height/air selection changed");
        }
    }
    state.structureFootprints["TARGET"] = {1, 1, true};
    index.invalidate();
    // Berserk opens allied and neutral candidates; geometry never filters by
    // allegiance and the unchanged scalar target checks decide this policy.
    for (bool berserkFriendly : {false, true}) {
        state.psychicStatuses.reset();
        RuntimePsychicStatusHitRequest hit;
        hit.currentTick = 10; hit.hitEventId = berserkFriendly ? 7002 : 7001; hit.durationTicks = 20;
        hit.source = runtimePsychicObjectSnapshot(state, *findObjectById(state, 2));
        hit.target = runtimePsychicObjectSnapshot(state, pose);
        hit.target.immuneToPsionics = false; hit.target.berserkFriendly = berserkFriendly;
        hit.warhead = {"__SPATIAL_OPPORTUNITY_GAS__", true, true};
        if (!state.psychicStatuses.applyHit(hit).accepted) return fail("psychic fixture rejected");
        for (int tick : {10, 29, 30}) {
            state.tick = tick;
            for (bool distributed : {false, true}) for (bool force : {false, true})
                if (!check(pose, nullptr, 0, distributed, 5, force, false, index))
                    return fail("berserk allegiance/expiry changed");
        }
    }
    state.psychicStatuses.reset(); state.tick = 10;

    // Specialized weapon contexts keep their complete target population.
    const auto fallback = [&](const WorldObjectInstance& source, const WeaponType* weapon,
        double bonus, bool passenger) {
        const RuntimeCombatOpportunityRangeBound bound(state, source, weapon, bonus, passenger);
        return index.query(state, source, bound) == nullptr
            && check(source, weapon, bonus, true, 5, true, passenger, index);
    };
    if (!fallback(pose, radial, 0, false) || !fallback(pose, nullptr, 2, false)
        || !fallback(pose, nullptr, 0, true)) return fail("fixed weapon/passenger/bonus fallback changed");
    add(700, "FORTRESS", "Player", WorldObjectKind::Unit, 2, 5);
    add(701, "SOLDIER", "Player", WorldObjectKind::Infantry, 0, 0, false);
    auto* transported = findMutableRuntimeObjectByIdIncludingInactive(state, 701);
    transported->mission = "InsideTransport";
    findMutableObjectById(state, 700)->transportPassengerIds = {701};
    const auto passengerPose = runtimeCombatFiringPose(*transported, findObjectById(state, 700));
    if (!fallback(passengerPose, nullptr, 2, true)) return fail("real hidden transport passenger changed target");
    auto specialPose = pose; specialPose.airborne = true;
    if (!fallback(specialPose, nullptr, 0, false)) return fail("airborne source fallback changed");
    specialPose = pose; specialPose.tankBunkerHostId = 1;
    if (!fallback(specialPose, nullptr, 0, false)) return fail("bunker source fallback changed");
    specialPose = pose; specialPose.activeGunnerWeaponIndex = 0;
    if (!fallback(specialPose, nullptr, 0, false)) return fail("gunner fallback changed");
    const auto profileKey = runtimeCapabilityCacheKeyForObject(pose);
    auto& profile = state.capabilityCache.profilesByType.at(profileKey);
    const auto savedProfile = profile;
    profile.airRangeBonus = 4;
    const bool airBonusFallback = fallback(pose, nullptr, 0, false);
    profile = savedProfile;
    if (!airBonusFallback) return fail("AirRangeBonus fallback changed");
    auto* mutableRadial = const_cast<WeaponType*>(radial);
    const auto savedWeapon = *mutableRadial;
    mutableRadial->spawner = true; mutableRadial->range = -2;
    const bool infiniteFallback = fallback(pose, nullptr, 0, false);
    *mutableRadial = savedWeapon;
    mutableRadial->range = std::numeric_limits<double>::quiet_NaN();
    const RuntimeCombatOpportunityRangeBound nonFinite(state, pose, nullptr, 0, false);
    const bool nonFiniteFallback = index.query(state, pose, nonFinite) == nullptr;
    *mutableRadial = savedWeapon;
    if (!infiniteFallback || !nonFiniteFallback) return fail("infinite/nonfinite range fallback changed");

    // Same-size reorder/death/relocation uses the caller's world-action boundary.
    index.invalidate();
    std::swap(state.world.accessObjects()[1], state.world.accessObjects()[2]);
    findMutableObjectById(state, 3)->alive = false;
    findMutableObjectById(state, 7)->health = 0;
    findMutableObjectById(state, 2)->x = 64;
    if (!check(pose, nullptr, 0, true, -1, false, false, index))
        return fail("world reorder/death/relocation retained obsolete positions");
    index.invalidate();
    auto* revived = findMutableRuntimeObjectByIdIncludingInactive(state, 2000);
    revived->alive = true;
    if (!check(pose, nullptr, 0, true, 2000, true, false, index))
        return fail("world-action boundary omitted a newly active historic object");
    const auto buildsBeforeGrowth = index.populationBuilds;
    add(8, "TARGET", "Enemy", WorldObjectKind::Structure, 5, 6);
    if (!check(pose, nullptr, 0, true, -1, false, false, index)
        || (expectsIndex && index.populationBuilds != buildsBeforeGrowth + 1))
        return fail("population growth did not rebuild current world indices");

    // Isolate the actual-fire boundary from the deliberately mutated range,
    // weapon mode and psychic fixtures above. Author and read back every gate.
    {
        auto shotOwned = std::make_unique<RuntimeWindowState>();
        auto& shot = *shotOwned;
        RuntimeSnapshotActiveStateScope shotActive(shot);
        IniFile shotIni;
        if (!shotIni.loadFromString("[VehicleTypes]\n0=SHOT_SOURCE\n[BuildingTypes]\n0=SHOT_TARGET\n"
            "[WeaponTypes]\n0=SHOT_WEAPON\n[SHOT_SOURCE]\nStrength=1000\nPrimary=SHOT_WEAPON\n"
            "Ammo=-1\nInitialAmmo=-1\nTurret=no\nOpportunityFire=yes\nDistributedFire=yes\nSpeed=5\nMovementZone=Normal\n"
            "[SHOT_TARGET]\nStrength=1000\n[SHOT_WEAPON]\nDamage=10\nRange=10\nROF=3\nBurst=1\n"
            "OmniFire=yes\nSpawner=no\nLimboLaunch=no\nSuicide=no\nNeverUse=no\nAreaFire=no\nDrainWeapon=no\n"
            "Projectile=SHOT_PROJECTILE\nSpeed=100\nWarhead=SHOT_WH\n"
            "[SHOT_PROJECTILE]\nAA=yes\nAG=yes\nROT=8\nRanged=yes\n"
            "[SHOT_WH]\nCellSpread=0\nAffectsEnemies=yes\nAffectsAllies=no\nTemporal=no\nParasite=no\n")
            || !shot.rules.loadFromIni(shotIni)) return fail("independent fire fixture rules");
        shot.rulesLoaded = true; shot.tick = 10; shot.config.fixedTickHz = 15;
        shot.economy.owner = "Player"; shot.runtimeSoundsEnabled = false;
        shot.runtimeSuppressPlayerAudioFeedback = true;
        shot.world.accessTerrainCells() = state.world.getTerrainCells();
        shot.terrainByCell = buildRuntimeTerrainLookup(shot.world);
        shot.terrainInfoByCell = state.terrainInfoByCell;
        shot.structureFootprints["SHOT_TARGET"] = {1, 1, true};
        const auto addShot = [&](int id, bool source, int x, int y, bool alive = true) {
            WorldObjectInstance object;
            object.instanceId = id; object.kind = source ? WorldObjectKind::Unit : WorldObjectKind::Structure;
            object.type = source ? "SHOT_SOURCE" : "SHOT_TARGET";
            object.owner = source ? "Player" : "Enemy";
            object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
            object.alive = alive; object.health = alive ? 1000 : 0;
            object.mission = source ? "Moving" : "Guard";
            shot.world.accessObjects().push_back(std::move(object));
        };
        addShot(1, true, 4, 6); addShot(8, false, 5, 6);
        for (int i = 0; i < 64; ++i) addShot(1000 + i, false, 128 + i % 16, 128 + i / 16);
        for (int i = 0; i < 256; ++i) addShot(2000 + i, false, 4, 6, false);
        auto* shooter = findMutableObjectById(shot, 1);
        auto* target = findMutableObjectById(shot, 8);
        shooter->facing = runtimeFacingFromDelta(target->x - shooter->x, target->y - shooter->y);
        shooter->turretFacing = shooter->facing;
        const auto* ammo = runtimeAmmoRulesForObject(shot.rules, *shooter);
        const auto* sourceUnit = findRuntimeUnitType(shot.rules, *shooter);
        initializeRuntimeObjectAmmo(*shooter, ammo);
        const auto shotProfile = runtimeWeaponProfileForObject(shot, *shooter, target);
        const auto* weapon = shotProfile.weapon;
        const auto* lifecycle = weapon ? shot.rules.findWeaponLifecycleProfile(weapon->id) : nullptr;
        if (weapon == nullptr || weapon->id != "SHOT_WEAPON" || weapon->damage != 10 || weapon->burst != 1
            || lifecycle == nullptr || !lifecycle->omniFire || sourceUnit == nullptr || !sourceUnit->distributedFire
            || ammo == nullptr || ammo->ammo != -1
            || shooter->currentAmmo != -1 || weapon->spawner || weapon->limboLaunch || weapon->suicide
            || weapon->neverUse || weapon->areaFire || weapon->drainWeapon
            || shotProfile.projectile == nullptr || !shotProfile.projectile->ag
            || shotProfile.warhead == nullptr || shotProfile.warhead->temporal || shotProfile.warhead->parasite
            || shot.combatDetails.hasPendingBurst(1) || !runtimeWeaponCanTargetObject(shot, *weapon, *target))
            return fail("explicit fire-ready readback differs: weapon=" + (weapon ? weapon->id : "none")
                + " damage=" + std::to_string(weapon ? weapon->damage : -1)
                + " burst=" + std::to_string(weapon ? weapon->burst : -1)
                + " omni=" + std::to_string(lifecycle && lifecycle->omniFire)
                + " distributed=" + std::to_string(sourceUnit && sourceUnit->distributedFire)
                + " ammoLimit=" + std::to_string(ammo ? ammo->ammo : -2)
                + " currentAmmo=" + std::to_string(shooter->currentAmmo)
                + " projectileAG=" + std::to_string(shotProfile.projectile && shotProfile.projectile->ag)
                + " warhead=" + (shotProfile.warhead ? shotProfile.warhead->id : "none")
                + " pending=" + std::to_string(shot.combatDetails.hasPendingBurst(1))
                + " specialWeapon=" + std::to_string(weapon && (weapon->spawner || weapon->limboLaunch
                    || weapon->suicide || weapon->neverUse || weapon->areaFire || weapon->drainWeapon)));
        const auto shotPose = *shooter;
        const RuntimeCombatOpportunityRangeBound bound(shot, shotPose, nullptr, 0, false);
        RuntimeCombatOpportunityCandidates shotIndex;
        if (shotIndex.query(shot, shotPose, bound) == nullptr || !shotIndex.valid || shotIndex.disabledForPass)
            return fail("actual-fire fixture did not build its geometry before firing");
        shooter->weaponCooldownTicksRemaining = 1;
        if (fireRuntimeIndependentCombatWeapon(shot, *shooter, nullptr, *target, shotProfile, false, &shotIndex)
            || !shotIndex.valid || shotIndex.disabledForPass) return fail("cooldown-only attempt revoked geometry");
        shooter->weaponCooldownTicksRemaining = 0;

        auto oldShotOwned = std::make_unique<RuntimeWindowState>();
        auto& oldShot = *oldShotOwned;
        oldShot.rules = shot.rules; oldShot.rulesLoaded = true; oldShot.world = shot.world;
        oldShot.tick = shot.tick; oldShot.config = shot.config; oldShot.economy.owner = "Player";
        oldShot.runtimeSoundsEnabled = false; oldShot.runtimeSuppressPlayerAudioFeedback = true;
        oldShot.terrainByCell = buildRuntimeTerrainLookup(oldShot.world);
        oldShot.terrainInfoByCell = shot.terrainInfoByCell; oldShot.structureFootprints = shot.structureFootprints;
        {
            RuntimeSnapshotActiveStateScope oldActive(oldShot);
            auto* oldShooter = findMutableObjectById(oldShot, 1);
            const auto* oldTarget = findObjectById(oldShot, 8);
            const auto oldProfile = runtimeWeaponProfileForObject(oldShot, *oldShooter, oldTarget);
            if (!fireRuntimeIndependentCombatWeapon(oldShot, *oldShooter, nullptr, *oldTarget, oldProfile, false))
                return fail("explicit original actual-fire call returned false");
        }
        const auto attacksBefore = shot.actionSummary.attacksThisTick;
        if (!fireRuntimeIndependentCombatWeapon(shot, *shooter, nullptr, *target, shotProfile, false, &shotIndex))
            return fail("explicit indexed actual-fire call returned false: valid=" + std::to_string(shotIndex.valid)
                + " disabled=" + std::to_string(shotIndex.disabledForPass));
        if (shotIndex.valid || !shotIndex.disabledForPass) return fail("successful actual fire failed to revoke geometry");
        if (shot.actionSummary.attacksThisTick != attacksBefore + 1 || shot.projectiles.size() != 1
            || oldShot.projectiles.size() != 1 || shot.projectiles.back().attackerObjectId != 1
            || shot.projectiles.back().targetObjectId != 8 || shot.projectiles.back().damage != 10
            || shot.world.deterministicSimulationStateHash() != oldShot.world.deterministicSimulationStateHash()
            || shot.combatDetails.deterministicHash() != oldShot.combatDetails.deterministicHash())
            return fail("actual fire differs from original damage/projectile/world/combat state");
        const auto firedCursor = shot.combatDetails.fireCursors.find(1);
        if (firedCursor == shot.combatDetails.fireCursors.end() || firedCursor->second.lastTargetObjectId != 8)
            return fail("explicit distributed actual fire did not advance its target cursor");
        auto* relocated = findMutableObjectById(shot, 1000); relocated->x = 4; relocated->y = 6;
        relocated->cell = runtimeCellKey(relocated->x, relocated->y);
        const auto postFireCheck = [&](RuntimeCombatOpportunityCandidates& candidates) {
            const auto before = shot.world.deterministicSimulationStateHash();
            const auto* oldTarget = selectRuntimeCombatOpportunityTarget(shot, shotPose, nullptr, 0,
                true, 1000, true, false, true);
            const auto* newTarget = selectRuntimeCombatOpportunityTarget(shot, shotPose, nullptr, 0,
                true, 1000, true, false, false, nullptr, &candidates);
            ++cases;
            if (!oldTarget || oldTarget->instanceId != 1000 || !newTarget || newTarget->instanceId != 1000
                || before != shot.world.deterministicSimulationStateHash()) {
                error = "old=" + std::to_string(oldTarget ? oldTarget->instanceId : -1)
                    + " new=" + std::to_string(newTarget ? newTarget->instanceId : -1)
                    + " cursor=" + std::to_string(firedCursor->second.lastTargetObjectId)
                    + " worldUnchanged=" + std::to_string(before == shot.world.deterministicSimulationStateHash());
                return false;
            }
            return true;
        };
        if (shotIndex.query(shot, shotPose, bound) != nullptr || !postFireCheck(shotIndex))
            return fail("post-fire relocation used an obsolete shortlist: " + error);
        RuntimeCombatOpportunityCandidates nextShotPass;
        if (!postFireCheck(nextShotPass)) return fail("next pass failed to observe post-fire relocation: " + error);
    }

    // Retain the scalar fallback for unrepresentable integer geometry, without
    // calling its signed-int distance helper on deliberately extreme fixtures.
    auto* target = findMutableObjectById(state, 1000);
    const RuntimeCombatOpportunityRangeBound ordinary(state, pose, nullptr, 0, false);
    RuntimeCombatOpportunityCandidates nextPass;
    target->x = std::numeric_limits<int>::max() - 4;
    state.structureFootprints["TARGET"] = {32, 1, true};
    nextPass.invalidate();
    if (nextPass.query(state, pose, ordinary) != nullptr) return fail("overflowing foundation was truncated");
    target->x = 5; state.structureFootprints["TARGET"] = {1, 1, true};
    nextPass.invalidate();
    auto extremePose = pose; extremePose.x = std::numeric_limits<int>::min();
    if (nextPass.query(state, extremePose, ordinary) != nullptr) return fail("extreme coordinate was truncated");
    std::cout << "Combat opportunity spatial equivalence passed: cases=" << cases
        << " geometryCases=" << geometryCases << " population=519"
        << " worldOrder/buckets/foundation/height/air/psychic/force/distributed/passenger/infinite/worldMutation/fireBoundary=yes\n";
    error.clear();
    return true;
}
