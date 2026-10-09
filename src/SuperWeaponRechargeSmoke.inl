namespace {
bool smokeSuperWeaponConcurrentRecharge() {
    const auto fail = [](const std::string& detail) {
        std::cerr << "superweapon concurrent recharge: " << detail << '\n'; return false;
    };
    for (int hz : {30, 60}) for (const char* type : {"MultiMissile", "IronCurtain", "ForceShield",
            "LightningStorm", "ChronoSphere", "PsychicDominator", "GeneticMutator",
            "ParaDrop", "SpyPlane", "PsychicReveal"}) {
        SuperWeaponRule rule;
        rule.id = "CLOCK"; rule.type = rule.action = type;
        rule.targetMode = std::string(type) == "ChronoSphere" ? "TwoStage" : "Area";
        rule.rechargeTimeMinutes = 0.5; rule.durationFrames = 15; rule.defermentFrames = 1;
        rule.isPowered = false; rule.targetFriendly = true;
        SuperWeaponSystem system; system.reset(std::vector<SuperWeaponRule>{rule});
        std::vector<WorldObjectInstance> objects;
        if (std::string(type) == "ForceShield") {
            WorldObjectInstance target; target.instanceId = 42;
            target.kind = WorldObjectKind::Structure; target.owner = "A";
            target.type = "TARGET"; target.x = target.y = 20;
            target.alive = true; target.health = 1000; objects.push_back(target);
        }
        SuperWeaponWorldContext context;
        context.fixedTickHz = hz; context.mapWidth = context.mapHeight = 64;
        context.objects = &objects;
        context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 64 && y < 64;
        };
        if (!system.grant("A", rule.id, -1, &context)) return fail("grant");
        system.tick(context);
        if (!system.forceReady("A", rule.id, &context)
            || !system.beginTargetSelection("A", rule.id, context)) return fail("arm");
        if (system.submitTarget("A", rule.id, {{-10,-10},-1}, context)
            || system.findSidebarSnapshot("A", rule.id)->progress != 1.0)
            return fail("invalid target consumed charge");
        if (rule.targetMode == "TwoStage") {
            if (!system.submitTarget("A", rule.id, {{20,20},-1}, context)
                || system.findSidebarSnapshot("A", rule.id)->progress != 1.0
                || !system.cancelTargetSelection("A", rule.id, context)
                || system.findSidebarSnapshot("A", rule.id)->progress != 1.0)
                return fail("first target/cancel consumed charge");
            if (hz == 60) {
                if (!system.submitTwoStageTarget("A", rule.id, {{20,20},-1}, {{24,24},-1}, context))
                    return fail("atomic two-target command");
            } else {
                if (!system.beginTargetSelection("A", rule.id, context)
                    || !system.submitTarget("A", rule.id, {{20,20},-1}, context)
                    || !system.submitTarget("A", rule.id, {{24,24},-1}, context)) return fail("second target");
            }
        } else if (!system.submitTarget("A", rule.id,
                {{20,20}, objects.empty() ? -1 : 42}, context)) return fail("target");
        const int recharge = 30 * hz;
        if (system.findSidebarSnapshot("A", rule.id)->remainingTicks != recharge
            || system.findSidebarSnapshot("A", rule.id)->progress != 0.0)
            return fail(std::string(type) + " did not reset at acceptance");
        for (int tick = 1; tick <= recharge; ++tick) {
            context.tick = tick; system.tick(context);
            const auto* snapshot = system.findSidebarSnapshot("A", rule.id);
            if (snapshot->remainingTicks != recharge - tick)
                return fail(std::string(type) + " stopped/reset charge at tick " + std::to_string(tick));
            if (tick == hz) {
                std::ostringstream saved; system.save(saved);
                IniFile ini; SuperWeaponSystem loaded; loaded.reset(std::vector<SuperWeaponRule>{rule});
                if (!ini.loadFromString(saved.str()) || !loaded.load(ini)
                    || loaded.stateHash() != system.stateHash()) return fail("active recharge save/hash");
                system = std::move(loaded);
            }
        }
        if (system.stateOf("A", rule.id) != SuperWeaponRuntimeState::Ready)
            return fail(std::string(type) + " added effect duration to recharge");
    }
    // Power can stop charging without cancelling an already applied effect.
    SuperWeaponRule rule; rule.id = "POWER"; rule.type = rule.action = "IronCurtain";
    rule.targetMode = "Area"; rule.isPowered = true;
    rule.durationFrames = 300; rule.rechargeTimeMinutes = 1.0;
    bool powered = true;
    SuperWeaponWorldContext context; context.fixedTickHz = 30;
    context.cellLegal = [](int, int, SuperWeaponTargetMode) { return true; };
    context.ownerPowered = [&](const std::string&) { return powered; };
    SuperWeaponSystem system; system.reset(std::vector<SuperWeaponRule>{rule});
    system.grant("A", rule.id, -1, &context); system.tick(context);
    if (!system.forceReady("A", rule.id, &context)
        || !system.beginTargetSelection("A", rule.id, context)
        || !system.submitTarget("A", rule.id, {{20,20},-1}, context)) return fail("power fixture");
    context.tick = 1; system.tick(context);
    const int before = system.findSidebarSnapshot("A", rule.id)->remainingTicks;
    powered = false;
    for (int tick = 2; tick <= 11; ++tick) { context.tick = tick; system.tick(context); }
    if (system.stateOf("A", rule.id) != SuperWeaponRuntimeState::Active
        || system.findSidebarSnapshot("A", rule.id)->remainingTicks != before
        || !system.findSidebarSnapshot("A", rule.id)->chargePaused) return fail("active power pause");
    std::ostringstream saved; system.save(saved);
    IniFile ini; SuperWeaponSystem loaded; loaded.reset(std::vector<SuperWeaponRule>{rule});
    if (!ini.loadFromString(saved.str()) || !loaded.load(ini)
        || system.stateHash() != loaded.stateHash()) return fail("paused save/hash");
    powered = true; context.tick = 12; loaded.tick(context);
    if (loaded.findSidebarSnapshot("A", rule.id)->remainingTicks != before - 1
        || loaded.findSidebarSnapshot("A", rule.id)->chargePaused) return fail("active power recovery");
    std::string legacy = saved.str();
    const auto version = legacy.find("Version=8");
    if (version == std::string::npos) return fail("save version");
    legacy.replace(version, 9, "Version=4");
    const auto charge = legacy.find("\nChargeTicks=");
    const auto end = legacy.find('\n', charge + 1);
    if (charge == std::string::npos || end == std::string::npos) return fail("legacy charge field");
    legacy.replace(charge + 1, end - charge - 1, "ChargeTicks=1800");
    IniFile old; SuperWeaponSystem migrated; migrated.reset(std::vector<SuperWeaponRule>{rule});
    if (!old.loadFromString(legacy) || !migrated.load(old)
        || migrated.stateOf("A", rule.id) != SuperWeaponRuntimeState::Active
        || migrated.findSidebarSnapshot("A", rule.id)->remainingTicks != 1800)
        return fail("legacy active save restored a consumed ready charge");
    std::cout << "superweapon concurrent recharge: 10 effect families x 30/60 Hz; exact ready tick, "
        "target/cancel, active/paused save, power recovery and v4 migration PASS\n";
    return true;
}
} // namespace
