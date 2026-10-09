bool runRuntimeGattlingSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& reason) { error = "Gattling: " + reason; return false; };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("original startup failed");
    int cases = 0;
    for (const std::string type : {"YAGGUN", "YTNK"}) for (int rank : {0, 2})
        for (bool air : {false, true}) for (int hz : {30, 60}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        s.config = config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.report = live->report; s.rules = live->rules; s.art = live->art; s.theater = live->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false; s.visionEnabled = false;
        s.economy.owner = "YuriCountry"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        WorldObjectInstance source;
        source.instanceId = 1; source.type = type; source.owner = "YuriCountry";
        source.kind = type == "YAGGUN" ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        source.alive = true; source.health = 810; source.poweredOperational = true;
        source.x = source.y = 8; source.cell = runtimeCellKey(8, 8);
        source.facing = source.turretFacing = runtimeFacingFromDelta(3, 0);
        source.veterancy = rank; source.mission = "Guard";
        auto target = source;
        target.instanceId = 2; target.type = air ? "DISK" : "HTNK"; target.kind = WorldObjectKind::Unit;
        target.owner = "Americans"; target.health = 1000000; target.veterancy = 0;
        target.x = 11; target.cell = runtimeCellKey(11, 8);
        target.airborne = air; target.level = target.flightLevel = air ? 5 : 0;
        target.mission = "Sleep"; target.weaponCooldownTicksRemaining = 1000000;
        // Real damage clamps HP to authored Strength. Increase both for the
        // long firing run; changing HP alone let the air target die in stage 2.
        auto* durableTarget = const_cast<UnitType*>(s.rules.findUnitType("UnitType", target.type));
        if (!durableTarget) return fail("target type missing");
        durableTarget->strength = target.health;
        auto second = target; second.instanceId = 3; second.x = 12; second.cell = runtimeCellKey(12, 8);
        s.world.accessObjects() = {source, target, second};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        s.selectedObjectIds = {1}; issueRuntimeAttackCommand(s, *findObjectById(s, 2));
        if (s.lastCommand.acceptedCount != 1) return fail(type + " attack order rejected");
        std::array<int, 3> counts{}, cooldowns{}, volleyMin{}, volleyMax{};
        std::array<int, 3> lastVolleyTick{-1, -1, -1}, intervalMin{1000000, 1000000, 1000000}, intervalMax{};
        std::array<int, 3> firstStageTick{-1, -1, -1};
        std::vector<RuntimeImpactRenderActor> muzzles(6);
        bool burstMuzzleMissing = false;
        const auto step = [&]() {
            ++s.tick;
            advanceRuntimeWeaponCooldowns(s);
            const std::size_t firstBurstEffect = s.impactEffects.size();
            advanceRuntimeCombatBursts(s);
            for (std::size_t i = firstBurstEffect; i < s.impactEffects.size(); ++i) {
                const auto& flash = s.impactEffects[i];
                if (flash.attackerObjectId != 1 || flash.renderIntent != "weaponAnim") continue;
                const int stage = flash.animId == "VTMUZZLE" ? 2 : flash.animId == "GUNFIRE" ? 1 : 0;
                auto& first = muzzles[stage];
                const auto second = makeRuntimeImpactRenderActor(flash);
                // Both rounds must come from distinct gun barrels, not the
                // foundation origin that a lost indexed FLH produced.
                if (std::hypot(second.x - source.x, second.y - source.y) < 0.1
                    || (!first.animId.empty() && std::hypot(second.x - first.x, second.y - first.y) < 0.1)) {
                    if (!burstMuzzleMissing) std::cout << "Gattling burst mismatch " << type
                        << " air=" << air << " weapon=" << flash.weaponId << " tick=" << s.tick
                        << " first=" << first.x << ',' << first.y << " second=" << second.x << ',' << second.y << '\n';
                    burstMuzzleMissing = true;
                }
                muzzles[stage + 3] = second;
            }
            advanceRuntimeGattlingWeaponTimers(s);
            const int before = s.actionSummary.attacksThisTick;
            const auto behavior = s.behaviors.find(1);
            if (behavior != s.behaviors.end())
                advanceRuntimeAttackBehavior(s, *findMutableObjectById(s, 1), behavior->second);
            return s.actionSummary.attacksThisTick > before;
        };
        for (int tick = 0; tick < hz * 50; ++tick) {
            const bool fired = step();
            const auto* attacker = findObjectById(s, 1);
            const auto profile = runtimeWeaponProfileForObject(s, *attacker, findObjectById(s, 2));
            if (!profile.weapon || profile.indexedWeaponIndex < 0) return fail("lost indexed weapon");
            const int stage = profile.indexedWeaponIndex / 2;
            if (fired) {
                if (lastVolleyTick[stage] >= 0) {
                    const int gap = s.tick - lastVolleyTick[stage];
                    intervalMin[stage] = std::min(intervalMin[stage], gap);
                    intervalMax[stage] = std::max(intervalMax[stage], gap);
                }
                lastVolleyTick[stage] = s.tick;
                if (firstStageTick[stage] < 0) firstStageTick[stage] = s.tick;
                ++counts[stage];
                const double rofBonus = runtimeVeterancyAbilityMultiplier(s, *attacker, VeteranAbility::Rof);
                const int baseRof = runtimeRofScaledFrames(profile.weapon->rof, rofBonus);
                const int maximumRof = runtimeRofScaledFrames(profile.weapon->rof + 2, rofBonus);
                cooldowns[stage] = runtimeRuleFramesToTicks(baseRof, hz);
                volleyMin[stage] = runtimeRuleFramesToTicks(baseRof + 3 * (profile.weapon->burst - 1), hz);
                volleyMax[stage] = runtimeRuleFramesToTicks(maximumRof + 5 * (profile.weapon->burst - 1), hz);
                const auto flash = std::find_if(s.impactEffects.rbegin(), s.impactEffects.rend(),
                    [&](const auto& effect) { return effect.attackerObjectId == 1
                        && effect.renderIntent == "weaponAnim" && effect.weaponId == profile.weapon->id; });
                if (flash == s.impactEffects.rend()) return fail("actual stage muzzle missing");
                const bool correctFlash = stage == 0 ? flash->animId.rfind("MGUN-", 0) == 0
                    : flash->animId == (stage == 1 ? "GUNFIRE" : "VTMUZZLE");
                if (!correctFlash) return fail("wrong stage muzzle resource");
                muzzles[stage] = makeRuntimeImpactRenderActor(*flash);
            }
            advanceRuntimeEffects(s);
            if (counts[2] >= 5) break;
        }
        const bool cadenceCorrect = air
            ? cooldowns[0] > cooldowns[1] && cooldowns[1] > cooldowns[2]
            : cooldowns[0] == cooldowns[1] && cooldowns[1] == cooldowns[2];
        for (int stage = 0; stage < 3; ++stage)
            if (intervalMin[stage] < volleyMin[stage] || intervalMax[stage] > volleyMax[stage])
                return fail(type + " real volley spacing differs from reload at stage " + std::to_string(stage + 1)
                    + (air ? " air" : " ground") + " rank=" + std::to_string(rank) + " hz=" + std::to_string(hz)
                    + " actual=" + std::to_string(intervalMin[stage]) + ".." + std::to_string(intervalMax[stage])
                    + " expected=" + std::to_string(volleyMin[stage]) + ".." + std::to_string(volleyMax[stage]));
        if (counts[0] < 2 || counts[1] < 2 || counts[2] < 5 || !cadenceCorrect
            || findObjectById(s, 2)->health >= target.health)
            return fail(type + " stages lost authored cadence or damage: "
                + std::to_string(counts[0]) + "/" + std::to_string(counts[1]) + "/" + std::to_string(counts[2])
                + " reload=" + std::to_string(cooldowns[0]) + "/" + std::to_string(cooldowns[1])
                + "/" + std::to_string(cooldowns[2]) + " status=" + s.simulationStatus);
        if (burstMuzzleMissing || std::any_of(muzzles.begin(), muzzles.end(),
                [](const auto& flash) { return flash.animId.empty(); })) {
            std::string names;
            for (const auto& muzzle : muzzles) names += "[" + muzzle.animId + "]";
            return fail(type + " burst lost its indexed or alternate muzzle: " + names);
        }
        const int threshold = rank == 2 ? 100 : 200;
        const int firstStageDelay = firstStageTick[1] - firstStageTick[0];
        if (std::abs(firstStageDelay - threshold * hz / 15) > 40 * hz / 15)
            return fail("stage timing depends on fixed simulation rate");
        const int savedTimer = s.gattlingWeaponTimers.at(1);
        IniFile saved;
        if (!saved.loadFromString("[GattlingWeaponTimers]\nSchema=1\nCount=1\n0=1|"
                + std::to_string(savedTimer) + "\n")) return fail("save fixture");
        s.gattlingWeaponTimers.clear();
        if (!restoreRuntimeGattlingTimersFromSave(s, saved, error, 14)
            || s.gattlingWeaponTimers.at(1) != savedTimer) return fail("stage save/resume");

        if (hz == 30 && rank == 0 && !air) {
            RuntimeMapRenderContext context;
            context.rules = &s.rules; context.art = &s.art; context.theater = &s.theater;
            context.assetRootPath = runtimeAssetRootPath(config);
            context.structureRootPath = runtimeStructureRootPath(config, s.report.map);
            context.palettePath = runtimePalettePath(config, s.report.map);
            context.structurePalettePath = runtimeStructurePalettePath(config, s.report.map);
            context.fixedTickHz = hz;
            if (!runRuntimeGattlingRenderContractCheck(context, *findObjectById(s, 1), muzzles, error)) return false;
        }
        const int reload = findObjectById(s, 1)->weaponCooldownTicksRemaining;
        issueRuntimeAttackCommand(s, *findObjectById(s, 3));
        if (s.gattlingWeaponTimers.contains(1)
            || findObjectById(s, 1)->weaponCooldownTicksRemaining != reload
            || runtimeWeaponProfileForObject(s, *findObjectById(s, 1), findObjectById(s, 3)).indexedWeaponIndex / 2 != 0)
            return fail("retarget kept its stage or bypassed reload");
        s.gattlingWeaponTimers[1] = savedTimer;
        issueRuntimeStopCommand(s);
        if (s.gattlingWeaponTimers.contains(1)) return fail("stop did not reset immediately");
        issueRuntimeAttackCommand(s, *findObjectById(s, 3));
        s.behaviors.at(1).state = RuntimeBehaviorState::Attacking;
        s.gattlingWeaponTimers[1] = savedTimer;
        findMutableObjectById(s, 3)->x = 31;
        ++s.tick; advanceRuntimeGattlingWeaponTimers(s);
        if (s.gattlingWeaponTimers.contains(1)) return fail("out-of-range did not reset");
        findMutableObjectById(s, 3)->x = 12;
        s.gattlingWeaponTimers[1] = savedTimer;
        auto* lostTarget = findMutableObjectById(s, 3);
        lostTarget->alive = false;
        ++s.tick; advanceRuntimeGattlingWeaponTimers(s);
        if (s.gattlingWeaponTimers.contains(1)) return fail("dead target did not reset");
        // The ordinary lookup intentionally excludes dead objects.
        lostTarget->alive = true;
        if (type == "YAGGUN") {
            s.gattlingWeaponTimers[1] = savedTimer;
            findMutableObjectById(s, 1)->poweredOperational = false;
            ++s.tick; advanceRuntimeGattlingWeaponTimers(s);
            if (s.gattlingWeaponTimers.contains(1)) return fail("power loss preserved charge");
        } else {
            s.gattlingWeaponTimers[1] = savedTimer;
            issueRuntimeMoveCommand(s, {8, 12});
            if (s.lastCommand.acceptedCount != 1 || s.gattlingWeaponTimers.contains(1))
                return fail("move did not reset immediately");
        }
        ++cases;
        std::cout << "Gattling " << type << " rank=" << rank << (air ? " air" : " ground") << " hz=" << hz
            << " stages=" << counts[0] << '/' << counts[1] << '/' << counts[2]
            << " reload=" << cooldowns[0] << '/' << cooldowns[1] << '/' << cooldowns[2]
            << " actual=" << intervalMin[0] << '/' << intervalMin[1] << '/' << intervalMin[2]
            << " reset/save=ok\n";
    }
    std::cout << "Gattling passed: " << cases << " live attack cases; stage flashes, authored ground/air cadence,"
        << " retarget/stop/move/power/range/target-loss resets, save timing and original pixels\n";
    return true;
}
