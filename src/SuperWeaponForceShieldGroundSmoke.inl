namespace {
bool smokeSuperWeaponForceShieldGround() {
    const auto fail = [](const std::string& reason) {
        std::cerr << "force shield ground: " << reason << "\n";
        return false;
    };
    SuperWeaponRule rule;
    rule.id = "GroundShield";
    rule.type = rule.action = "ForceShield";
    rule.targetMode = "Area";
    rule.targetFriendly = rule.targetBuildings = true;
    rule.targetEnemy = rule.targetInfantry = rule.targetUnits = false;
    rule.exploredOnly = rule.disableableFromShell = true;
    rule.isPowered = false;
    rule.radius = 4;
    rule.durationFrames = 500;
    rule.blackoutDurationFrames = 1000;
    rule.rechargeTimeMinutes = 5.0;
    int cases = 0, saves = 0, rejections = 0;
    for (const int hz : {15, 30, 60}) {
        for (const bool empty : {false, true}) {
            for (const int hint : {-1, 1, 3, 4, 8, 999}) {
                std::vector<WorldObjectInstance> objects;
                const auto add = [&](int id, WorldObjectKind kind, int x, int y,
                                     const std::string& owner = "A", bool alive = true) {
                    WorldObjectInstance object;
                    object.instanceId = id; object.kind = kind; object.type = "Probe";
                    object.x = x; object.y = y; object.owner = owner;
                    object.alive = alive; object.health = alive ? 500 : 0;
                    objects.push_back(object);
                };
                add(1, WorldObjectKind::Structure, 26, 24);
                add(2, WorldObjectKind::Structure, 20, 24, "Ally");
                add(3, WorldObjectKind::Unit, 24, 24);
                add(4, WorldObjectKind::Structure, 24, 25, "Enemy");
                add(5, WorldObjectKind::Structure, 25, 25, "Neutral");
                add(6, WorldObjectKind::Structure, 28, 25); // (4,1): inside.
                add(7, WorldObjectKind::Structure, 28, 26); // (4,2): outside.
                add(8, WorldObjectKind::Structure, 23, 24, "A", false);
                add(9, WorldObjectKind::Structure, 27, 27); // (3,3): inside.
                add(10, WorldObjectKind::Structure, 27, 28); // (3,4): outside.
                add(11, WorldObjectKind::Infantry, 24, 23);
                add(12, WorldObjectKind::Structure, 25, 24);
                objects.back().airborne = true;
                SuperWeaponWorldContext context;
                context.tick = 20; context.fixedTickHz = hz;
                context.mapWidth = context.mapHeight = 64;
                context.objects = &objects;
                context.areAllied = [](const auto& a, const auto& b) {
                    return a == b || (a == "A" && b == "Ally") || (a == "Ally" && b == "A");
                };
                context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
                    return x != 12 || y != 12;
                };
                context.explored = [](const auto&, int x, int y) { return x != 13 || y != 13; };
                std::vector<SuperWeaponEvent> events;
                context.emit = [&](const auto& event) { events.push_back(event); };
                SuperWeaponSystem system;
                system.reset(std::vector<SuperWeaponRule>{rule});
                if (!system.forceReady("A", rule.id, &context)
                    || !system.beginTargetSelection("A", rule.id, context)) return fail("arming");
                const auto before = system.stateHash();
                for (const auto cell : std::vector<SuperWeaponCell>{{-1,24},{24,-1},{64,24},{24,64},{12,12},{13,13}}) {
                    const SuperWeaponTarget invalid{cell, hint};
                    if (system.previewTarget("A", rule.id, invalid, context)
                        || system.submitTarget("A", rule.id, invalid, context)
                        || system.stateHash() != before) return fail("invalid cell consumed charge or was accepted");
                    ++rejections;
                }
                const SuperWeaponTarget target{empty ? SuperWeaponCell{52,52} : SuperWeaponCell{24,24}, hint};
                context.superWeaponsAllowed = false;
                if (system.previewTarget("A", rule.id, target, context)
                    || system.submitTarget("A", rule.id, target, context)
                    || system.stateHash() != before) return fail("disabled shield accepted");
                ++rejections;
                context.superWeaponsAllowed = true;
                if (!system.previewTarget("A", rule.id, target, context)
                    || !system.submitTarget("A", rule.id, target, context)) return fail("valid ground rejected");
                const auto* launched = system.findSidebarSnapshot("A", rule.id);
                if (!launched || launched->targetSelectionActive
                    || launched->state != SuperWeaponRuntimeState::Launching
                    || launched->firstTargetCell != target.cell
                    || launched->remainingTicks != launched->rechargeTicks) return fail("ground command lifecycle");
                const auto restore = [&](const SuperWeaponSystem& source, SuperWeaponSystem& destination) {
                    std::ostringstream saved; source.save(saved); IniFile ini;
                    destination.reset(std::vector<SuperWeaponRule>{rule});
                    ++saves;
                    return ini.loadFromString(saved.str()) && destination.load(ini)
                        && source.stateHash() == destination.stateHash();
                };
                SuperWeaponSystem restored;
                if (!restore(system, restored)) return fail("pending command save");
                events.clear(); ++context.tick;
                system.tick(context);
                auto restoredContext = context; restoredContext.emit = {};
                restored.tick(restoredContext);
                if (system.stateHash() != restored.stateHash()) return fail("restored command diverged");
                const std::set<int> expected = empty ? std::set<int>{} : std::set<int>{1,2,6,9};
                std::set<int> protectedIds;
                int blackouts = 0;
                for (const auto& event : events) {
                    if (event.kind == "ForceShieldProtected") {
                        protectedIds.insert(event.objectId);
                        if (event.targetCell != target.cell) return fail("hover hint changed center");
                    }
                    if (event.kind == "ForceShieldBlackoutStarted") ++blackouts;
                }
                if (protectedIds != expected || blackouts != 1
                    || !system.isOwnerBlackout("A", context.tick)
                    || system.isOwnerBlackout("Enemy", context.tick)) return fail("recipients or blackout");
                const int protectEnd = context.tick + 500 * hz / 15;
                const int blackoutEnd = context.tick + 1000 * hz / 15;
                for (const auto& object : objects) {
                    if (system.isObjectProtected(object.instanceId, protectEnd-1) != expected.contains(object.instanceId)
                        || system.isObjectProtected(object.instanceId, protectEnd)) return fail("protection or duration");
                }
                if (!restore(system, restored)
                    || !restored.isOwnerBlackout("A", blackoutEnd-1)
                    || restored.isOwnerBlackout("A", blackoutEnd)) return fail("active save or blackout duration");
                context.tick = blackoutEnd; restoredContext.tick = context.tick;
                system.tick(context); restored.tick(restoredContext);
                if (system.stateHash() != restored.stateHash()) return fail("expiry continuation");
                ++cases;
            }
        }
    }
    std::cout << "Force shield ground: passed cases=" << cases << " saves=" << saves
              << " rejectedCellsAndOptions=" << rejections
              << " empty/own/ally/enemy/neutral/vehicle/infantry/dead/airborne/radius=ok\n";
    return true;
}
}
