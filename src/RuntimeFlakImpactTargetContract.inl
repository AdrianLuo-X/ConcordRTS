// Read-only eligibility checks use original rules and a detached world. Real
// weapon flight, damage and native body pixels are checked by the flak scenes.
bool runRuntimeFlakImpactTargetContract(RuntimeWindowState& live, std::string& error) {
    const auto liveWorldHash = live.world.deterministicSimulationStateHash();
    const auto liveRulesHash = live.rules.getDetailRulesHash();
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& s = *storage;
    seedRuntimeLoadScratch(s, live, false);
    s.campaignMode = false;
    s.config.gameMode = "Battle";
    s.skirmishModeRulesLoaded = s.skirmishModeAlliesAllowed = true;
    s.skirmishTeamByOwner = {{"playera", "1"}, {"ally", "1"}, {"playerb", "2"}};
    s.playerCommands.alliances.clear();
    s.world.accessObjects().clear();
    RuntimeSnapshotActiveStateScope active(s);
    const auto fail = [&](const std::string& reason) {
        error = "Flak impact target contract: " + reason;
        return false;
    };
    const auto* trooper = findRuntimeUnitTypeById(s.rules, "FLAKT");
    const auto* airship = findRuntimeUnitTypeById(s.rules, "ZEP");
    const auto* weapon = trooper ? findRuntimeWeaponType(s.rules, trooper->secondaryWeapon) : nullptr;
    const auto* type = weapon ? findRuntimeProjectileType(s.rules, weapon->projectile) : nullptr;
    const auto* warhead = weapon ? findRuntimeWarheadType(s.rules, weapon->warhead) : nullptr;
    if (!trooper || !airship || !weapon || !type || !warhead || !type->flakScatter
        || !type->aa || type->ag || weapon->damage <= 0 || warhead->cellSpread <= 0.0)
        return fail("original FLAKT/ZEP anti-air rules are unavailable");

    WorldObjectInstance source;
    source.instanceId = 10; source.mapId = "FlakContractSource";
    source.kind = WorldObjectKind::Infantry; source.type = "FLAKT";
    source.owner = "PlayerA"; source.alive = true; source.health = trooper->strength;
    source.x = 76; source.y = 80; source.cell = runtimeCellKey(source.x, source.y);
    source.mission = "Sleep";
    WorldObjectInstance low;
    low.instanceId = 20; low.mapId = "FlakContractLowId";
    low.kind = WorldObjectKind::Unit; low.type = "ZEP";
    low.owner = "PlayerB"; low.alive = true; low.health = airship->strength;
    low.x = low.y = 80; low.cell = runtimeCellKey(low.x, low.y);
    low.airborne = true; low.level = low.flightLevel = 6;
    low.hasRenderPosition = true; low.renderX = 79.75; low.renderY = 80.0;
    low.renderLevel = 6.0; low.mission = "Sleep";
    auto high = low;
    high.instanceId = 30; high.mapId = "FlakContractHighId"; high.renderX = 80.25;
    const auto reset = [&] { s.world.accessObjects() = {source, high, low}; };
    reset();

    RuntimeProjectileTargetPosition hit;
    hit.cell = {80, 80}; hit.x = hit.y = 80.0; hit.level = 6;
    RuntimeProjectileEffect shot;
    shot.actorId = 100; shot.attackerObjectId = source.instanceId; shot.targetObjectId = -1;
    shot.owner = source.owner; shot.weaponId = weapon->id; shot.projectileId = type->id;
    shot.warheadId = warhead->id; shot.damage = weapon->damage;
    shot.cellSpread = warhead->cellSpread; shot.percentAtMax = warhead->percentAtMax;
    shot.aa = true; shot.ag = false; shot.affectsAllies = false; shot.affectsEnemies = true;
    shot.hasTargetPosition = true; shot.targetX = hit.x; shot.targetY = hit.y;
    shot.targetZ = hit.level; shot.toCell = hit.cell; shot.toLevel = hit.level;
    shot.veteranDamageMultiplier = 1.0;
    int checks = 0;
    const auto expect = [&](const char* label, const RuntimeProjectileEffect& probe,
        const RuntimeProjectileTargetPosition& point, int expectedId) {
        const auto worldHash = s.world.deterministicSimulationStateHash();
        const auto* target = runtimeFlakImpactBodyTarget(s, probe, point);
        const int actualId = target ? target->instanceId : -1;
        if (actualId != expectedId)
            return fail(std::string(label) + " selected=" + std::to_string(actualId)
                + " expected=" + std::to_string(expectedId));
        if (s.world.deterministicSimulationStateHash() != worldHash
            || s.rules.getDetailRulesHash() != liveRulesHash)
            return fail(std::string(label) + " changed world damage or original rules");
        ++checks;
        return true;
    };
    if (!expect("equal-distance low ID", shot, hit, 20)) return false;
    std::reverse(s.world.accessObjects().begin(), s.world.accessObjects().end());
    if (!expect("equal-distance reversed world order", shot, hit, 20)) return false;
    reset();
    s.world.accessObjects()[1].renderX = 80.125;
    if (!expect("nearest continuous body before low ID", shot, hit, 30)) return false;

    reset();
    auto missed = hit; missed.cell = {86, 86}; missed.x = missed.y = 86.0;
    if (!expect("outside original damage footprint", shot, missed, -1)) return false;
    for (auto& object : s.world.accessObjects())
        if (object.instanceId != source.instanceId) object.owner = "Ally";
    if (!runtimeOwnersAreAllied(s, source.owner, "Ally"))
        return fail("fixture distinct owners are not allied");
    if (!expect("allied bodies without force fire", shot, hit, -1)) return false;
    auto forced = shot; forced.affectsAllies = true;
    if (!expect("forced allied body", forced, hit, 20)) return false;

    reset();
    for (auto& object : s.world.accessObjects())
        if (object.instanceId != source.instanceId) { object.alive = false; object.health = 0; }
    if (!expect("dead bodies", shot, hit, -1)) return false;
    reset();
    for (auto& object : s.world.accessObjects())
        if (object.instanceId != source.instanceId) { object.airborne = false; object.level = object.flightLevel = 0; }
    if (!expect("ground bodies", shot, hit, -1)) return false;
    reset();
    auto zero = shot; zero.damage = 0;
    if (!expect("zero damage", zero, hit, -1)) return false;
    auto locked = shot; locked.targetObjectId = 20;
    if (!expect("ordinary locked hit keeps its own path", locked, hit, -1)) return false;
    auto ground = shot; ground.ag = true;
    if (!expect("surface-capable scatter excluded", ground, hit, -1)) return false;

    // Stock only defines Inaccurate on FlakProj and FlakTProj, both also
    // FlakScatter. Check the original non-flak AA projectile without changing
    // rules, and exercise Inaccurate-only if a supplied ruleset defines one.
    const auto* ordinary = findRuntimeProjectileType(s.rules, "MedusaProjectile");
    if (!ordinary || ordinary->flakScatter || !ordinary->aa)
        return fail("original non-flak anti-air projectile is unavailable");
    auto nonFlak = shot; nonFlak.projectileId = ordinary->id;
    if (!expect("non-flak anti-air projectile excluded", nonFlak, hit, -1)) return false;
    int inaccurateOnlyChecks = 0;
    for (const auto& projectile : s.rules.getProjectileTypes()) {
        if (!projectile.inaccurate || projectile.flakScatter) continue;
        nonFlak.projectileId = projectile.id;
        if (!expect("Inaccurate-only projectile excluded", nonFlak, hit, -1)) return false;
        ++inaccurateOnlyChecks;
    }
    if (live.world.deterministicSimulationStateHash() != liveWorldHash
        || live.rules.getDetailRulesHash() != liveRulesHash)
        return fail("detached fixture changed its source state");
    std::cout << "Flak impact target contract passed: checks=" << checks
        << " inaccurateOnlyRules=" << inaccurateOnlyChecks
        << " stockRules/readOnly/miss/allies/force/dead/ground/zero/nonFlak/stableTie=ok\n";
    error.clear();
    return true;
}
