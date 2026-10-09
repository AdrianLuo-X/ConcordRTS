// Included after RuntimeCombatDetails.inl so this checks the production helpers.
#include "RuntimeCombatOpportunityContract.inl"
#include "RuntimeCombatOpportunityCandidatesContract.inl"
bool runRuntimeCombatDetailsSmokeProbe(std::string& error) {
    const auto fail = [&error](const std::string& message) {
        error = "combat details: " + message; return false;
    };
    IniFile ini;
    if (!ini.loadFromString(R"COMBAT(
[General]
BallisticScatter=1.0
TurboBoost=1.5
OpenToppedRangeBonus=2
OpenToppedDamageMultiplier=1.2
[InfantryTypes]
0=SOLDIER
[VehicleTypes]
0=FORTRESS
1=AEGIS
2=AIRTARGET
3=FIXTUREWEAPONS
[BuildingTypes]
0=TARGET
[WeaponTypes]
0=BURST
1=RADIAL
2=SCATTER
3=BOOST
[SOLDIER]
Strength=100
Primary=BURST
OpenTransportWeapon=0
Ammo=2
[FORTRESS]
Strength=1000
OpenTopped=yes
Passengers=5
[AEGIS]
Strength=1000
Primary=RADIAL
OpportunityFire=yes
DistributedFire=yes
RadialFireSegments=10
Speed=5
MovementZone=Normal
[AIRTARGET]
Strength=1000
SpeedType=Winged
MovementZone=Fly
[FIXTUREWEAPONS]
Strength=100
Primary=SCATTER
Secondary=BOOST
[TARGET]
Strength=1000
[BURST]
Damage=10
Range=6
ROF=30
Burst=3
BurstDelay=3
OmniFire=yes
Projectile=PLAIN
Speed=100
Warhead=WH
[RADIAL]
Damage=10
Range=10
ROF=3
OmniFire=yes
Projectile=MISSILE
Speed=100
Warhead=WH
[SCATTER]
Damage=10
Range=10
Projectile=FLAK
Speed=100
Warhead=WH
[BOOST]
Damage=10
Range=10
TurboBoost=yes
Projectile=MISSILE
Speed=100
Warhead=WH
[PLAIN]
AA=yes
AG=yes
[MISSILE]
AA=yes
AG=yes
Ranged=yes
ROT=8
[FLAK]
AA=yes
AG=yes
Inaccurate=yes
FlakScatter=yes
[WH]
CellSpread=0
AffectsEnemies=yes
AffectsAllies=no
)COMBAT")) return fail("fixture INI parse");
    auto owned = std::make_unique<RuntimeWindowState>();
    auto& state = *owned;
    if (!state.rules.loadFromIni(ini)) return fail("fixture rules parse");
    for (const char* id : {"BURST", "RADIAL", "SCATTER", "BOOST"})
        if (state.rules.findWeaponType(id) == nullptr) return fail(std::string("fixture weapon missing: ") + id);
    state.rulesLoaded = true;
    state.config.fixedTickHz = 15;
    state.economy.owner = "Player";
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.structureFootprints["TARGET"] = {1, 1, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y; terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&state](int id, const std::string& type, const std::string& owner,
        WorldObjectKind kind, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.type = type; object.owner = owner;
        object.kind = kind; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.health = kind == WorldObjectKind::Infantry ? 100 : 1000;
        object.mission = "Guard"; object.facing = 0;
        state.world.accessObjects().push_back(object);
    };
    add(1, "SOLDIER", "Player", WorldObjectKind::Infantry, 4, 5);
    add(2, "TARGET", "Enemy", WorldObjectKind::Structure, 9, 5);
    auto* shooter = findMutableObjectById(state, 1);
    auto* target = findMutableObjectById(state, 2);
    if (shooter == nullptr || target == nullptr) return fail("fixture object lookup");
    shooter->currentAmmo = 2;
    const auto profile = runtimeWeaponProfileForObject(state, *shooter, target);
    if (profile.weapon == nullptr) return fail("fixture weapon profile");
    const int facing = shooter->facing;
    if (!updateRuntimeWeaponFacingToward(state, *shooter, *target) || shooter->facing != facing)
        return fail("OmniFire turned the shooter");
    if (!fireRuntimeIndependentCombatWeapon(state, *shooter, nullptr, *target, profile)
        || state.projectiles.size() != 1 || state.projectiles.front().damage != 10
        || shooter->currentAmmo != 1 || !state.combatDetails.hasPendingBurst(1))
        return fail("first burst round/debit");
    std::ostringstream saved;
    state.combatDetails.save(saved);
    IniFile savedIni;
    RuntimeCombatDetailsState restored;
    if (!savedIni.loadFromString(saved.str()) || !restored.load(savedIni, error)
        || restored.deterministicHash() != state.combatDetails.deterministicHash())
        return fail("pending burst save roundtrip");
    state.combatDetails = std::move(restored);
    for (state.tick = 1; state.tick <= 6; ++state.tick) (void)advanceRuntimeCombatBursts(state);
    if (state.projectiles.size() != 3 || state.projectiles[1].spawnTick != 3
        || state.projectiles[2].spawnTick != 6 || shooter->currentAmmo != 1)
        return fail("rounds were not emitted independently at the authored cadence");
    const auto fired = state.projectiles;
    state.projectiles.clear();
    WorldTerrainObject tree;
    tree.type = "TREE"; tree.x = 9; tree.y = 5; tree.cell = runtimeCellKey(9, 5);
    tree.strength = 1000; tree.health = 1000;
    state.world.accessTerrainObjects().push_back(tree);
    for (const auto& projectile : fired) (void)applyRuntimeProjectileImpact(state, projectile);
    target = findMutableObjectById(state, 2);
    if (target == nullptr || target->health != 970
        || state.world.getTerrainObjects().front().health != 970)
        return fail("burst object/terrain impact multiplied damage a second time");

    // Legacy ordinary actors represented all paid rounds. Already individual
    // garrison/missile actors and virtual child weapons must not be migrated.
    auto aggregate = fired.front(); aggregate.damage = 30;
    auto garrisonRound = fired.front(); garrisonRound.actorId = 900;
    garrisonRound.sourceAnchorObjectId = 1;
    auto missileRound = fired.front(); missileRound.actorId = 901;
    missileRound.spawnedMissileDamage = true; missileRound.spawnedMissileBaseDamage = 10;
    auto childRound = fired.front(); childRound.actorId = 902;
    childRound.parentActorId = 99; childRound.generation = 1; childRound.damage = 30;
    const std::vector<RuntimeProjectileEffect> oldFlying{aggregate, garrisonRound, missileRound, childRound};
    IniFile preFeature;
    preFeature.loadFromString("[Game]\nTick=7\n");
    if (!state.combatDetails.reconcileLegacyProjectiles(preFeature, state.rules, oldFlying, error)
        || state.combatDetails.legacyProjectileRounds.size() != 1
        || state.combatDetails.legacyProjectileRounds.at(aggregate.actorId) != 3)
        return fail("legacy aggregate migration altered independent projectiles");
    std::ostringstream migratedSave;
    state.combatDetails.save(migratedSave);
    IniFile migratedIni;
    RuntimeCombatDetailsState migrated;
    if (!migratedIni.loadFromString(migratedSave.str()) || !migrated.load(migratedIni, error)
        || !migrated.reconcileLegacyProjectiles(migratedIni, state.rules, oldFlying, error))
        return fail("legacy in-flight damage did not survive another save/load");
    auto invalidMigration = migrated;
    invalidMigration.legacyProjectileRounds.emplace(garrisonRound.actorId, 3);
    if (invalidMigration.reconcileLegacyProjectiles(migratedIni, state.rules, oldFlying, error))
        return fail("legacy migration accepted a forged aggregate garrison actor");
    state.combatDetails = std::move(migrated);
    for (const auto& projectile : oldFlying) (void)applyRuntimeProjectileImpact(state, projectile);
    target = findMutableObjectById(state, 2);
    if (target == nullptr || target->health != 890
        || state.world.getTerrainObjects().front().health != 890)
        return fail("legacy/garrison/spawned/virtual-child damage contract changed");
    (void)advanceRuntimeCombatBursts(state);
    if (!state.combatDetails.legacyProjectileRounds.empty())
        return fail("expired legacy actor retained its migration marker");

    add(10, "FORTRESS", "Player", WorldObjectKind::Unit, 2, 5);
    auto* host = findMutableObjectById(state, 10);
    shooter = findMutableObjectById(state, 1);
    shooter->alive = false; shooter->mission = "InsideTransport";
    shooter->x = 0; shooter->y = 0; shooter->cell = 0;
    shooter->weaponCooldownTicksRemaining = 0; shooter->currentAmmo = 2;
    host->transportPassengerIds = {1};
    if (!advanceRuntimeCombatIndependentFire(state) || state.projectiles.size() != 1
        || state.projectiles.front().attackerObjectId != 1
        || state.projectiles.front().fromCell.x != 2
        || state.projectiles.front().damage != 12 || shooter->alive || shooter->currentAmmo != 1)
        return fail("OpenTopped source/range bonus/damage/ammo/hidden-state");
    host->transportPassengerIds.clear();
    (void)advanceRuntimeCombatBursts(state);
    if (state.combatDetails.hasPendingBurst(1)) return fail("removed passenger retained its burst");
    state.projectiles.clear();

    add(20, "AEGIS", "Player", WorldObjectKind::Unit, 4, 6);
    add(3, "TARGET", "Enemy", WorldObjectKind::Structure, 9, 7);
    auto* ship = findMutableObjectById(state, 20);
    ship->mission = "Moving";
    RuntimeObjectBehavior move;
    move.kind = RuntimeBehaviorKind::Move;
    move.moveTarget = {12, 12};
    state.behaviors[20] = move;
    if (!advanceRuntimeCombatIndependentFire(state) || state.projectiles.size() != 1
        || ship->mission != "Moving" || state.behaviors.at(20).kind != RuntimeBehaviorKind::Move
        || state.projectiles.front().renderIntent != "radialProjectile"
        || state.projectiles.front().attackerFacing != 64)
        return fail("opportunity fire changed movement or omitted radial launch");
    const int firstTarget = state.projectiles.front().targetObjectId;
    ship->weaponCooldownTicksRemaining = 0;
    if (!advanceRuntimeCombatIndependentFire(state) || state.projectiles.size() != 2
        || state.projectiles.back().targetObjectId == firstTarget)
        return fail("DistributedFire did not move to another eligible target");
    auto radial = state.projectiles.front();
    ++radial.elapsedTicks;
    const int radialFacing = radial.attackerFacing;
    while (runtimeRuleFramesElapsed(state.tick, state.config.fixedTickHz) == 0) ++state.tick;
    (void)advanceRuntimeRadialProjectilePosition(state, radial);
    if (radial.attackerFacing == radialFacing || (radial.currentX == radial.sourceX && radial.currentY == radial.sourceY))
        return fail("radial missile did not advance/turn from its launch arc");

    add(30, "AIRTARGET", "Enemy", WorldObjectKind::Unit, 10, 6);
    ship = findMutableObjectById(state, 20);
    target = findMutableObjectById(state, 30);
    target->airborne = true;
    RuntimeWeaponProfile boostProfile;
    const WeaponType* boost = findRuntimeWeaponType(state.rules, "BOOST");
    const WeaponType* scatter = findRuntimeWeaponType(state.rules, "SCATTER");
    if (boost == nullptr || scatter == nullptr) return fail("fixture secondary weapons");
    finalizeRuntimeWeaponProfile(boostProfile, state, RuntimeWeaponSlot::Primary, *boost, "boost probe");
    spawnRuntimeProjectile(state, *ship, *target, *boost, boostProfile, 10);
    const auto& boosted = state.projectiles.back();
    const double normalSpeed = 6.0 / runtimeProjectileTravelTicks(state.rules, *boost, 6.0);
    if (boosted.speedCellsPerTick <= normalSpeed) return fail("TurboBoost did not accelerate against an air target");
    RuntimeWeaponProfile scatterProfile;
    finalizeRuntimeWeaponProfile(scatterProfile, state, RuntimeWeaponSlot::Primary, *scatter, "scatter probe");
    spawnRuntimeProjectile(state, *ship, *target, *scatter, scatterProfile, 10);
    const auto& scattered = state.projectiles.back();
    if (scattered.targetObjectId != -1 || (scattered.targetX == target->x && scattered.targetY == target->y))
        return fail("Inaccurate/FlakScatter retained guaranteed target tracking");
    if (!runRuntimeCombatOpportunityContract(state.rules, error)) return false;
    if (!runRuntimeCombatOpportunityCandidatesContract(state.rules, error)) return false;
    error.clear();
    return true;
}
