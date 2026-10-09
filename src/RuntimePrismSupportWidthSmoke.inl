bool runRuntimePrismSupportWidthSmoke(RuntimeWindowState& live,
    const std::string& root, std::string& error) {
    std::cout << std::unitbuf;
    int requested = -1, cases = 0, linkSaves = 0;
    const auto fail = [&](const std::string& why) {
        error = "Prism support width N=" + std::to_string(requested) + ": " + why;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& s = *storage;
    seedRuntimeLoadScratch(s, live, false);
    s.visionEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true;
    s.config.noCooldown = false;
    s.economy.owner = "Americans";
    s.ownerRemapColors["americans"] = RGB(255, 220, 40);
    s.world.accessObjects().clear();
    s.world.accessTerrainObjects().clear();
    s.world.accessTerrainCells().clear();
    s.world.accessSmudges().clear();
    s.projectiles.clear();
    s.recentExpiredProjectiles.clear();
    s.impactEffects.clear();
    s.behaviors.clear();
    for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = 0;
        s.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(s);
    const auto add = [&](RuntimeBuildKind kind, const char* type, RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstance(s, kind, type, cell);
        if (!object) return -1;
        object->owner = "Americans";
        object->veterancy = 0;
        object->buildAnimTicksRemaining = object->buildAnimTicksTotal = 0;
        object->poweredOperational = true;
        object->mission = "Guard";
        return object->instanceId;
    };
    const int masterId = add(RuntimeBuildKind::Building, "ATESLA", {64, 64});
    const int targetId = add(RuntimeBuildKind::Unit, "MTNK", {70, 61});
    // Exclusions precede the eligible towers in instance order, so an erroneous
    // selection cannot be hidden by the stock eight-supporter cap.
    const int unpoweredId = add(RuntimeBuildKind::Building, "ATESLA", {66, 66});
    const int enemyId = add(RuntimeBuildKind::Building, "ATESLA", {67, 64});
    const int distantId = add(RuntimeBuildKind::Building, "ATESLA", {54, 64});
    const int inactiveId = add(RuntimeBuildKind::Building, "ATESLA", {67, 60});
    const int constructingId = add(RuntimeBuildKind::Building, "ATESLA", {68, 66});
    const std::array<RuntimeCellPoint, 9> supportCells{{
        {60, 64}, {60, 61}, {61, 58}, {64, 58}, {58, 65},
        {59, 68}, {62, 70}, {65, 71}, {64, 60}
    }};
    std::vector<int> supporterIds;
    for (const auto cell : supportCells)
        supporterIds.push_back(add(RuntimeBuildKind::Building, "ATESLA", cell));
    if (masterId <= 0 || targetId <= 0 || unpoweredId <= 0 || enemyId <= 0
        || distantId <= 0 || inactiveId <= 0 || constructingId <= 0
        || std::any_of(supporterIds.begin(), supporterIds.end(), [](int id) { return id <= 0; }))
        return fail("stock scene could not be created");
    const auto& supportRules = s.rules.getSpecialMechanismRules().prismSupport;
    if (supportRules.supportMax != 8 || toUpperAsciiLocal(supportRules.prismType) != "ATESLA")
        return fail("stock Prism support rules changed");
    std::ofstream report;
    if (!root.empty()) {
        report.open(std::filesystem::path(root) / "support-width.csv");
        if (!report) return fail("could not write width report");
        report << "hz,eligible,supports,main_thickness,link_thickness,damage_multiplier,damage,roundtrip\n";
    }
    for (int hz : {15,30,60}) for (requested = 0; requested <= 9; ++requested) {
        s.config.fixedTickHz = hz;
        s.tick = s.config.fixedTickHz * (11 + requested);
        s.projectiles.clear();
        s.recentExpiredProjectiles.clear();
        s.impactEffects.clear();
        s.behaviors.clear();
        for (auto& object : s.world.accessObjects()) {
            object.alive = true;
            object.health = runtimeMaxHealthForObject(s, object);
            object.owner = "Americans";
            object.weaponCooldownTicksRemaining = 0;
            object.chargeTurretStartTick = -1;
            object.buildAnimTicksRemaining = object.buildAnimTicksTotal = 0;
            object.poweredOperational = true;
            object.mission = "Guard";
        }
        findMutableObjectById(s, unpoweredId)->poweredOperational = false;
        findMutableObjectById(s, enemyId)->owner = "Russians";
        auto* inactive = findMutableObjectById(s, inactiveId);
        inactive->alive = false; inactive->health = 0;
        findMutableObjectById(s, constructingId)->buildAnimTicksRemaining = 100000;
        for (std::size_t n = 0; n < supporterIds.size(); ++n) {
            if (static_cast<int>(n) < requested) continue;
            auto* inactiveSupport = findMutableObjectById(s, supporterIds[n]);
            inactiveSupport->alive = false; inactiveSupport->health = 0;
        }
        auto* master = findMutableObjectById(s, masterId);
        auto* target = findMutableObjectById(s, targetId);
        target->owner = "Russians";
        master->facing = master->turretFacing = runtimeFacingFromDelta(
            target->x - master->x, target->y - master->y);
        const auto profile = runtimeWeaponProfileForObject(s, *master, target);
        if (!profile.weapon || profile.weapon->laserThickness != 1)
            return fail("stock primary beam missing");
        applySelection(s, {masterId}, false, false);
        issueRuntimeAttackCommand(s, *target);
        if (!s.behaviors.contains(masterId)) return fail("actual attack command rejected");
        bool fired = false;
        for (int tick = 0; tick < s.config.fixedTickHz * 10 && !fired; ++tick) {
            ++s.tick;
            advanceRuntimeWeaponCooldowns(s);
            advanceRuntimeAttackBehavior(s, *master, s.behaviors.at(masterId));
            fired = std::any_of(s.projectiles.begin(), s.projectiles.end(),
                [&](const RuntimeProjectileEffect& p) {
                    return p.attackerObjectId == masterId && p.weaponId == profile.weapon->id;
                });
        }
        if (!fired) return fail("charged primary attack did not fire");
        const int expectedCount = std::min(requested, 8);
        const int expectedThickness = 1 + 2 * expectedCount;
        const double expectedMultiplier = 1.0 + expectedCount * supportRules.supportModifier;
        std::vector<int> actualSupporters;
        const RuntimeProjectileEffect* mainBeam = nullptr;
        for (const auto& projectile : s.projectiles) {
            if (projectile.renderIntent == "prismSupportBeam") {
                actualSupporters.push_back(projectile.attackerObjectId);
                if (projectile.laserThickness != expectedThickness || projectile.damage != 0
                    || projectile.targetObjectId != -1
                    || makeRuntimeProjectileRenderActor(projectile).laserThickness != expectedThickness)
                    return fail("support link lost volley width or gained friendly damage");
                const auto encoded = serializeRuntimeProjectileEffect(projectile);
                RuntimeProjectileEffect resumed;
                if (!parseRuntimeProjectileEffect(encoded, resumed)
                    || serializeRuntimeProjectileEffect(resumed) != encoded
                    || makeRuntimeProjectileRenderActor(resumed).laserThickness != expectedThickness)
                    return fail("support link width changed after projectile save/load");
                ++linkSaves;
            } else if (projectile.attackerObjectId == masterId
                && projectile.weaponId == profile.weapon->id) {
                if (mainBeam) return fail("more than one primary beam fired");
                mainBeam = &projectile;
            }
        }
        std::sort(actualSupporters.begin(), actualSupporters.end());
        const std::vector<int> expectedSupporters(supporterIds.begin(),
            supporterIds.begin() + expectedCount);
        if (actualSupporters != expectedSupporters || !mainBeam
            || mainBeam->laserThickness != expectedThickness
            || makeRuntimeProjectileRenderActor(*mainBeam).laserThickness != expectedThickness
            || std::abs(mainBeam->veteranDamageMultiplier - expectedMultiplier) > 0.000001
            || mainBeam->damage != runtimeWeaponDamageAgainst(s, *target, *profile.weapon,
                0.0, expectedMultiplier))
            return fail("eligible support count, width, render projection or existing damage changed"
                " supports=" + std::to_string(actualSupporters.size())
                + " width=" + std::to_string(mainBeam ? mainBeam->laserThickness : -1)
                + " multiplier=" + std::to_string(mainBeam ? mainBeam->veteranDamageMultiplier : -1.0));
        const auto serialized = serializeRuntimeProjectileEffect(*mainBeam);
        RuntimeProjectileEffect restored;
        if (!parseRuntimeProjectileEffect(serialized, restored)
            || serializeRuntimeProjectileEffect(restored) != serialized
            || makeRuntimeProjectileRenderActor(restored).laserThickness != expectedThickness)
            return fail("projectile save roundtrip lost the main beam width");
        if (report) report << hz << ',' << requested << ',' << expectedCount << ',' << expectedThickness
            << ',' << (expectedCount ? expectedThickness : 0)
            << ',' << expectedMultiplier << ',' << mainBeam->damage << ",ok\n";
        if (hz == 30 && (requested == 0 || requested == 1 || requested == 2 || requested == 4 || requested == 8)) {
            const auto stats = drawRuntimeStateOffscreenAtCell(s, {65, 64}, 1024, 768, error,
                root.empty() ? "" : root + "/support-" + std::to_string(requested) + ".bmp",
                false, false);
            if (!stats || !stats->rendered || stats->projectileActorTravelDrawn != expectedCount + 1)
                return fail("actual emitted beams were missing from the rendered scene: " + error);
        }
        ++cases;
        std::cout << "Prism support width: Hz=" << hz << " eligible=" << requested << " supports=" << expectedCount
            << " thickness=" << expectedThickness << " multiplier=" << expectedMultiplier
            << " exclusions/render/save=ok\n";
    }
    std::cout << "Prism linked volley: " << cases << " cases, " << linkSaves << " support projectile reloads, 30 main reloads PASS\n";
    return true;
}
