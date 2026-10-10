bool runRuntimeVerticalBombSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "vertical bomb: " + message;
        return false;
    };
    for (int rank : {0, 2}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.report = live.report;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.theater = live.theater;
        probe.theaterLoaded = live.theaterLoaded;
        probe.world = live.world;
        probe.world.accessTerrainCells().clear();
        probe.world.accessTerrainObjects().clear();
        probe.world.accessSmudges().clear();
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = "Russians";
        probe.triggers.bootstrapComplete = true;
        probe.economy.skirmishStartApplied = true;
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0;
            cell.subTileIndex = 0;
            cell.level = 2;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance bomber;
        bomber.instanceId = 1;
        bomber.kind = WorldObjectKind::Unit;
        bomber.type = "ZEP";
        bomber.owner = probe.economy.owner;
        bomber.health = 2000;
        bomber.x = 8;
        bomber.y = 8;
        bomber.cell = runtimeCellKey(8, 8);
        bomber.level = 5;
        bomber.flightLevel = 5;
        bomber.airborne = true;
        bomber.veterancy = rank;
        bomber.mission = "Guard";
        bomber.facing = 64;
        auto target = bomber;
        target.instanceId = 2;
        target.type = "MTNK";
        target.owner = "Americans";
        target.health = 600;
        target.x = 9;
        target.level = 2;
        target.flightLevel = 0;
        target.airborne = false;
        target.cell = runtimeCellKey(9, 8);
        probe.world.accessObjects() = {bomber, target};
        initializeRuntimeNavigation(probe);
        probe.selectedObjectIds = {1};
        issueRuntimeAttackCommand(probe, *findObjectById(probe, 2));
        for (int step = 0; step < 600 && probe.projectiles.empty(); ++step) {
            ++probe.tick;
            advanceRuntimeSimulation(probe);
        }
        if (probe.projectiles.empty()) return fail("Kirov did not approach and bomb: " + probe.simulationStatus);
        const auto shot = probe.projectiles.front();
        const auto* releasedBy = findObjectById(probe, 1);
        const auto profile = runtimeWeaponProfileForObject(probe, *releasedBy, findObjectById(probe, 2));
        if (!profile.weapon) return fail("Kirov lost its original weapon profile");
        const double releaseDistance = runtimeWeaponDistanceToTarget(probe, *releasedBy,
            *findObjectById(probe, 2), *profile.weapon);
        // Original PrimaryFireFLH=-50,0,-140 is relative to the actual firing
        // pose. Turning and vertical flight continue while this order waits
        // to fire; their initial facing and integer core height are not the
        // muzzle origin. Compute the world vector independently from facing.
        const double firingAngle = (shot.attackerFacing - 96)
            * (2.0 * 3.14159265358979323846 / 256.0);
        const double originalMuzzleX = runtimeObjectRenderX(*releasedBy)
            - 50.0 * std::cos(firingAngle) / 256.0;
        const double originalMuzzleY = runtimeObjectRenderY(*releasedBy)
            - 50.0 * std::sin(firingAngle) / 256.0;
        const double originalMuzzleZ = runtimeDrawLevelForObject(*releasedBy)
            - 140.0 * std::sqrt(6.0) / 256.0;
        if (!shot.vertical || shot.projectileImageId != "ZBOMB" || shot.targetObjectId >= 0
            || shot.sourceZ <= shot.targetZ || shot.sourceZ >= runtimeDrawLevelForObject(*releasedBy)
            || shot.sourceX != shot.targetX || shot.sourceY != shot.targetY
            || shot.attackerFacing != releasedBy->facing
            || std::abs(shot.sourceX - originalMuzzleX) > 0.000001
            || std::abs(shot.sourceY - originalMuzzleY) > 0.000001
            || std::abs(shot.sourceZ - originalMuzzleZ) > 0.000001
            || std::abs(shot.sourceScreenOffsetX) > 4096
            || std::abs(shot.sourceScreenOffsetY) > 4096
            || std::abs(static_cast<long long>(shot.sourceNativeFixedX)) > 65536
            || std::abs(static_cast<long long>(shot.sourceNativeFixedY)) > 65536
            || releaseDistance <= 0.25
            || releaseDistance > runtimeEffectiveWeaponRange(probe, *releasedBy,
                *findObjectById(probe, 2), *profile.weapon) + 0.001)
        {
            std::ostringstream details;
            details << "Kirov lost its original early bombing range or vertical release column"
                << " rank=" << rank << " tick=" << probe.tick << " actor=" << shot.actorId
                << " weapon=" << shot.weaponId << " distance=" << releaseDistance
                << " allowed=" << runtimeEffectiveWeaponRange(probe, *releasedBy,
                    *findObjectById(probe, 2), *profile.weapon)
                << " vertical=" << shot.vertical << " image=" << shot.projectileImageId
                << " boundTarget=" << shot.targetObjectId
                << " source=" << shot.sourceX << ',' << shot.sourceY << ',' << shot.sourceZ
                << " target=" << shot.targetX << ',' << shot.targetY << ',' << shot.targetZ
                << " bodyZ=" << runtimeDrawLevelForObject(*releasedBy)
                << " facing=" << releasedBy->facing << '/' << shot.attackerFacing
                << " expected=" << originalMuzzleX << ',' << originalMuzzleY << ',' << originalMuzzleZ
                << " offsets=" << shot.sourceScreenOffsetX << ',' << shot.sourceScreenOffsetY
                << ',' << shot.sourceNativeFixedX << ',' << shot.sourceNativeFixedY;
            return fail(details.str());
        }
        const std::string outputRoot = std::getenv("CONCORD_ELECTRIC_ATTACK_OUTPUT")
            ? std::getenv("CONCORD_ELECTRIC_ATTACK_OUTPUT") : "";
        if (!outputRoot.empty() && !drawRuntimeStateOffscreenAtCell(probe, {9, 8}, 480, 400, error,
                outputRoot + "/ZEP-" + std::to_string(rank) + "-release.bmp", false)) return false;
        eraseRuntimeBehavior(probe, 1);
        auto* movedBomber = findMutableObjectById(probe, 1);
        movedBomber->mission = "Guard";
        setRuntimeObjectCell(probe, *movedBomber, {3, 3});
        clearRuntimeRenderPosition(*movedBomber);
        auto* escapedTarget = findMutableObjectById(probe, 2);
        setRuntimeObjectCell(probe, *escapedTarget, {17, 17});
        clearRuntimeRenderPosition(*escapedTarget);
        auto bystander = target;
        bystander.instanceId = 3;
        bystander.x = shot.toCell.x;
        bystander.y = shot.toCell.y;
        bystander.cell = runtimeCellKey(bystander.x, bystander.y);
        probe.world.accessObjects().push_back(bystander);
        double previousZ = shot.sourceZ;
        int samples = 0;
        while (!probe.projectiles.empty() && samples <= shot.totalTicks + 2) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
            ++samples;
            if (probe.projectiles.empty()) break;
            const auto& bomb = probe.projectiles.front();
            if (bomb.currentX != shot.sourceX || bomb.currentY != shot.sourceY
                || bomb.targetX != shot.sourceX || bomb.targetY != shot.sourceY
                || bomb.sourceScreenOffsetX != shot.sourceScreenOffsetX
                || bomb.sourceScreenOffsetY != shot.sourceScreenOffsetY
                || bomb.sourceNativeFixedX != shot.sourceNativeFixedX
                || bomb.sourceNativeFixedY != shot.sourceNativeFixedY
                || bomb.currentZ > previousZ + 0.000001 || bomb.currentZ < shot.targetZ - 0.000001)
                return fail("a released bomb drifted, rose, or followed a moving unit");
            previousZ = bomb.currentZ;
            if (!outputRoot.empty() && samples == shot.totalTicks / 2
                && !drawRuntimeStateOffscreenAtCell(probe, {9, 8}, 480, 400, error,
                    outputRoot + "/ZEP-" + std::to_string(rank) + "-falling.bmp", false)) return false;
            if (samples == 5) {
                RuntimeProjectileEffect restored;
                if (!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(bomb), restored)
                    || restored.targetObjectId >= 0 || restored.sourceX != shot.sourceX
                    || restored.sourceY != shot.sourceY || restored.currentX != bomb.currentX
                    || restored.currentY != bomb.currentY || restored.currentZ != bomb.currentZ
                    || restored.sourceScreenOffsetX != shot.sourceScreenOffsetX
                    || restored.sourceScreenOffsetY != shot.sourceScreenOffsetY
                    || restored.sourceNativeFixedX != shot.sourceNativeFixedX
                    || restored.sourceNativeFixedY != shot.sourceNativeFixedY)
                    return fail("saving a falling bomb changed its trajectory");
                probe.projectiles.front() = std::move(restored);
            }
        }
        if (!probe.projectiles.empty() || findObjectById(probe, 2)->health != 600
            || findRuntimeObjectByIdIncludingInactive(probe, 3)->health >= 600)
            return fail("bomb impact followed the escaped target instead of its release column");
        if (probe.impactEffects.empty()) return fail("vertical bomb had no impact effect");
        const auto& impact = probe.impactEffects.back();
        if (std::abs(impact.worldX - shot.sourceX) > 0.000001
            || std::abs(impact.worldY - shot.sourceY) > 0.000001)
            return fail("bomb impact effect moved away from its vertical trajectory");
        std::cout << "Kirov vertical bomb: rank=" << rank << " samples=" << samples
            << " earlyReleaseRange=" << releaseDistance
            << " originalFLH=" << shot.sourceX << ',' << shot.sourceY << ',' << shot.sourceZ
            << " frozenOffsets=" << shot.sourceScreenOffsetX << ',' << shot.sourceScreenOffsetY
            << ',' << shot.sourceNativeFixedX << ',' << shot.sourceNativeFixedY
            << " fixedXY=ok descending=ok movingTargetMiss=ok groundDamage=ok saveLoad=ok\n";
    }
    return true;
}
