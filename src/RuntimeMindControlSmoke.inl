bool runRuntimeMindControlVisualSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = message; return false; };
    IniFile customIni;
    customIni.loadFromString("[CombatDamage]\nControlledAnimationType=MODCONTROL\n"
        "PermaControlledAnimationType=MODPERMANENT\nMindControlAttackLineFrames=7\n"
        "[InfantryTypes]\n0=TEST\n[TEST]\nMindControlRingOffset=210\n");
    RulesDatabase customRules;
    customRules.loadFromIni(customIni);
    const auto& custom = customRules.getSpecialMechanismRules();
    const auto* customUnit = customRules.findUnitType("infantry", "TEST");
    if (custom.controlledAnimationType != "MODCONTROL"
        || custom.permaControlledAnimationType != "MODPERMANENT"
        || custom.mindControlAttackLineFrames != 7
        || customUnit == nullptr || customUnit->mindControlRingOffset != 210)
        return fail("custom mind-control animation rules were ignored");
    int cases = 0;
    for (const auto& [type, weaponId] : std::vector<std::pair<std::string, std::string>>{
            {"YURI", "MindControl"}, {"YURIPR", "SuperMindControl"},
            {"MIND", "MultipleMindControlTank"}, {"YAPSYT", "MultipleMindControlTower"}}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules;
        probe.art = live.art;
        probe.rulesLoaded = true;
        probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.tick = 137;
        probe.economy.owner = "Russians";
        WorldObjectInstance controller;
        controller.instanceId = 1;
        controller.type = type;
        controller.kind = type == "YAPSYT" ? WorldObjectKind::Structure
            : type == "MIND" ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
        controller.owner = "Russians";
        controller.health = 200;
        controller.x = 8;
        controller.y = 8;
        controller.cell = runtimeCellKey(8, 8);
        controller.facing = 64;
        WorldObjectInstance target = controller;
        target.instanceId = 2;
        target.type = "E2";
        target.kind = WorldObjectKind::Infantry;
        target.owner = "Americans";
        target.x = 9;
        target.cell = runtimeCellKey(9, 8);
        probe.world.accessObjects() = {controller, target};
        const WeaponType* weapon = probe.rules.findWeaponType(weaponId);
        if (weapon == nullptr) return fail("missing mind-control weapon: " + weaponId);
        RuntimeWeaponProfile profile;
        profile.slot = RuntimeWeaponSlot::Primary;
        spawnRuntimeProjectile(probe, controller, target, *weapon, profile, weapon->damage);
        if (probe.projectiles.size() != 1) return fail(weaponId + " did not fire");
        const auto projectile = probe.projectiles.front();
        probe.projectiles.clear();
        if (!applyRuntimeProjectileImpact(probe, projectile)) return fail(weaponId + " did not hit");
        const auto* controlled = findObjectById(probe, 2);
        if (controlled == nullptr || controlled->owner != controller.owner || controlled->health != target.health)
            return fail(weaponId + " failed to control the target without damage");
        if (probe.impactEffects.size() != 2) return fail(weaponId + " duplicated/missed its casting effects");
        int duration = 0;
        for (const auto& effect : probe.impactEffects) {
            if (effect.animId != "YURICNTL" || effect.animationFrameCount <= 4
                || effect.totalTicks != effect.animationFrameCount * effect.animationFrameDelayTicks
                || effect.frameIndex != 0 || !makeRuntimeImpactRenderActor(effect).centerOnAnchor)
                return fail(weaponId + " truncated or misplaced its cast animation");
            duration = std::max(duration, effect.totalTicks);
        }
        const auto initialSnapshots = probe.specialMechanisms.renderSnapshots(probe.tick, 30);
        if (initialSnapshots.size() != 1 || initialSnapshots.front().resourceId != "MINDANIM"
            || initialSnapshots.front().progress != 0 || initialSnapshots.front().requiredProgress != 40)
            return fail(weaponId + " used a casting SHP as the control ring or wrong attack duration");
        int lastFrame = -1;
        for (int elapsed = 1; elapsed <= duration; ++elapsed) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
            for (const auto& effect : probe.impactEffects) {
                if (effect.frameIndex < lastFrame) return fail(weaponId + " cast animation restarted");
                lastFrame = effect.frameIndex;
            }
        }
        if (!probe.impactEffects.empty() || lastFrame < 4) return fail(weaponId + " cast animation did not expire");
        const auto lateSnapshots = probe.specialMechanisms.renderSnapshots(probe.tick + 100, 30);
        if (lateSnapshots.size() != 1 || lateSnapshots.front().resourceId != "MINDANIM")
            return fail(weaponId + " lost its control ring when casting expired");
        std::ostringstream saved;
        probe.specialMechanisms.save(saved);
        IniFile saveIni;
        saveIni.loadFromString(saved.str());
        RuntimeSpecialMechanismState restored;
        restored.reset(probe.rules.getSpecialMechanismRules());
        if (!restored.load(saveIni, error)) return false;
        const auto restoredSnapshots = restored.renderSnapshots(probe.tick + 100, 30);
        if (restoredSnapshots.size() != 1 || restoredSnapshots.front().progress != lateSnapshots.front().progress
            || restoredSnapshots.front().expireTick != lateSnapshots.front().expireTick)
            return fail("loading a control link restarted its animation/attack deadline");
        probe.specialMechanisms.releaseMindControlByController(
            probe.world.accessObjects(), 1, probe.tick, nullptr, "visual smoke release");
        if (!probe.specialMechanisms.renderSnapshots(probe.tick).empty()) return fail("released control left a visual link");
        std::string reason;
        if (!probe.specialMechanisms.tryMindControl(probe.world.accessObjects(), probe.rules,
                1, 2, 1, true, false, false, probe.tick, nullptr, nullptr, reason)) return fail(reason);
        const auto permanent = probe.specialMechanisms.renderSnapshots(probe.tick + 100, 60);
        if (permanent.size() != 1 || permanent.front().resourceId != "MINDANIMR"
            || permanent.front().requiredProgress != 80 || permanent.front().progress != 100)
            return fail("permanent control used the wrong ring or an infinite attack line");
        ++cases;
    }
    std::cout << "Mind-control effects: weapons=" << cases
        << " castTimeline=ok controlRings=ok fixedTickHz=ok saveLoad=ok release=ok\n";
    return true;
}
