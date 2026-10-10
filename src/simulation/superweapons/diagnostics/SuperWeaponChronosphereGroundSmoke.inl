namespace {
bool smokeSuperWeaponChronosphereGround() {
    const auto fail = [](const std::string& reason) {
        std::cerr << "chronosphere ground: " << reason << "\n";
        return false;
    };
    // Original Type/Action and pre/post declarations, with renamed IDs.
    // The object eligibility defaults are parsed, not replaced by the test.
    IniFile declarations;
    RulesDatabase parsed;
    if (!declarations.loadFromString(R"ini(
[SuperWeaponTypes]
0=GroundChrono
1=GroundChronoPost
[GroundChrono]
Type=ChronoSphere
Action=ChronoSphere
PreClick=yes
RechargeTime=7
Range=1.4
DisableableFromShell=yes
[GroundChronoPost]
Type=ChronoWarp
Action=ChronoWarp
PostClick=yes
PreDependent=ChronoSphere
Range=1.4
DisableableFromShell=yes
[VehicleTypes]
0=GroundProbe
[GroundProbe]
Strength=100
Organic=no
[InfantryTypes]
0=FootProbe
[FootProbe]
Strength=100
Organic=yes
)ini") || !parsed.loadFromIni(declarations)) return fail("original declarations");
    const auto* original = parsed.findSuperWeaponType("GroundChrono");
    const auto* originalPost = parsed.findSuperWeaponType("GroundChronoPost");
    if (!original || !originalPost || original->targetMode != "TwoStage"
        || !original->targetFriendly || !original->targetEnemy
        || !original->targetUnits || original->targetBuildings
        || !originalPost->postClick || originalPost->targetBuildings)
        return fail("parsed original recipient policy");
    std::vector<SuperWeaponRule> rules{*original, *originalPost};
    for (auto& rule : rules) rule.exploredOnly = true;
    int cases = 0, saves = 0, rejected = 0, emptyCasts = 0;
    for (const int hz : {30, 60}) for (const bool empty : {false, true})
        for (const int hint : {-1, 3, 5, 8, 999}) {
        const std::string scene = std::to_string(hz) + (empty ? "-empty-" : "-occupied-")
            + std::to_string(hint);
        const auto bad = [&](const std::string& why) { return fail(scene + ": " + why); };
        std::vector<WorldObjectInstance> objects;
        const auto add = [&](int id, WorldObjectKind kind, int x, int y,
                             const std::string& owner = "A", bool alive = true) {
            WorldObjectInstance object;
            object.instanceId = id; object.kind = kind;
            object.type = kind == WorldObjectKind::Infantry ? "FootProbe" : "GroundProbe";
            object.owner = owner; object.alive = alive; object.health = alive ? 100 : 0;
            object.x = x; object.y = y; object.cell = y * 1000 + x; object.level = 2;
            object.mission = "Guard"; objects.push_back(object);
        };
        add(1, WorldObjectKind::Unit, 24, 24);
        add(2, WorldObjectKind::Unit, 25, 24, "Ally");
        add(3, WorldObjectKind::Unit, 23, 24, "Enemy");
        add(4, WorldObjectKind::Unit, 24, 25, "Neutral");
        add(5, WorldObjectKind::Structure, 24, 24, "Enemy");
        add(6, WorldObjectKind::Infantry, 24, 24);
        add(7, WorldObjectKind::Unit, 24, 24); objects.back().airborne = true;
        add(8, WorldObjectKind::Unit, 24, 24, "A", false);
        add(9, WorldObjectKind::Unit, 28, 28);
        add(10, WorldObjectKind::Unit, 24, 24);
        add(11, WorldObjectKind::Unit, 28, 28); objects.back().transportPassengerIds = {10};
        const auto initialObjects = objects;
        SuperWeaponWorldContext context;
        context.tick = 20; context.fixedTickHz = hz;
        context.mapWidth = context.mapHeight = 64;
        context.objects = &objects; context.rules = &parsed;
        context.ownerPowered = [](const auto&) { return true; };
        context.areAllied = [](const auto& a, const auto& b) {
            return a == b || (a == "A" && b == "Ally") || (a == "Ally" && b == "A");
        };
        context.cellLegal = [](int x, int y, SuperWeaponTargetMode) { return x != 12 || y != 12; };
        context.explored = [](const auto&, int x, int y) { return x != 13 || y != 13; };
        context.canPlaceObject = [](const auto&, int, int) { return true; };
        context.terrainLevel = [](int, int) { return 3; };
        std::vector<SuperWeaponEvent> events, resumedEvents, launchedEvents;
        context.emit = [&](const auto& event) { events.push_back(event); };
        SuperWeaponSystem system;
        system.reset(rules); system.tick(context);
        if (!system.forceReady("A", original->id, &context)
            || !system.beginTargetSelection("A", original->id, context)) return bad("arming");
        const SuperWeaponTarget first{empty ? SuperWeaponCell{51,51} : SuperWeaponCell{24,24}, hint};
        const SuperWeaponTarget second{empty ? SuperWeaponCell{54,54} : SuperWeaponCell{40,40}, hint};
        const auto rejectCells = [&](const SuperWeaponTarget& valid) {
            const auto before = system.stateHash();
            for (const auto cell : std::vector<SuperWeaponCell>{{-1,24},{24,-1},{64,24},{24,64},{12,12},{13,13}}) {
                const SuperWeaponTarget invalid{cell, hint};
                if (system.previewTarget("A", original->id, invalid, context)
                    || system.submitTarget("A", original->id, invalid, context)
                    || system.stateHash() != before) return false;
                // This intentionally starts in Selecting/AwaitingSecondTarget.
                // It does not claim Ready-invalid atomicity for the old API.
                if (system.submitTwoStageTarget("A", original->id, invalid, valid, context)
                    || system.submitTwoStageTarget("A", original->id, valid, invalid, context)
                    || system.stateHash() != before) return false;
                ++rejected;
            }
            context.superWeaponsAllowed = false;
            const bool blocked = !system.previewTarget("A", original->id, valid, context)
                && !system.submitTarget("A", original->id, valid, context)
                && !system.submitTwoStageTarget("A", original->id, first, second, context)
                && system.stateHash() == before;
            context.superWeaponsAllowed = true; ++rejected;
            return blocked;
        };
        if (!rejectCells(first) || !system.previewTarget("A", original->id, first, context)
            || !system.submitTarget("A", original->id, first, context)) return bad("first area target");
        const auto restore = [&](const SuperWeaponSystem& source, SuperWeaponSystem& destination) {
            std::ostringstream saved; source.save(saved); IniFile ini;
            destination.reset(rules); std::string error;
            ++saves;
            return ini.loadFromString(saved.str()) && ini.getValue("SuperWeapons", "Version", "") == "8"
                && destination.load(ini, &error) && source.stateHash() == destination.stateHash();
        };
        SuperWeaponSystem resumed;
        if (!restore(system, resumed)
            || resumed.stateOf("A", original->id) != SuperWeaponRuntimeState::AwaitingSecondTarget)
            return bad("Version8 pending first target restore");
        SuperWeaponSystem cancelled = resumed;
        auto quietContext = context; quietContext.emit = {};
        if (!cancelled.cancelTargetSelection("A", original->id, quietContext)
            || cancelled.stateOf("A", original->id) != SuperWeaponRuntimeState::Ready)
            return bad("cancel pending area target");
        if (!rejectCells(second) || !system.previewTarget("A", original->id, second, context)
            || !system.submitTarget("A", original->id, second, context)) return bad("second area target");
        auto resumedObjects = initialObjects;
        auto resumedContext = context; resumedContext.objects = &resumedObjects;
        resumedContext.emit = [&](const auto& event) { resumedEvents.push_back(event); };
        if (!resumed.previewTarget("A", original->id, second, resumedContext)
            || !resumed.submitTarget("A", original->id, second, resumedContext)
            || resumed.stateHash() != system.stateHash()) return bad("pending continuation");
        SuperWeaponSystem launched;
        if (!restore(system, launched)
            || launched.stateOf("A", original->id) != SuperWeaponRuntimeState::Launching)
            return bad("Version8 launch restore");
        auto launchedObjects = initialObjects;
        auto launchedContext = context; launchedContext.objects = &launchedObjects;
        launchedContext.emit = [&](const auto& event) { launchedEvents.push_back(event); };
        const auto sameObjects = [](const auto& a, const auto& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].instanceId != b[i].instanceId || a[i].kind != b[i].kind
                    || a[i].type != b[i].type || a[i].owner != b[i].owner
                    || a[i].alive != b[i].alive || a[i].health != b[i].health
                    || a[i].x != b[i].x || a[i].y != b[i].y || a[i].cell != b[i].cell
                    || a[i].level != b[i].level || a[i].airborne != b[i].airborne) return false;
            return true;
        };
        ++context.tick; resumedContext.tick = launchedContext.tick = context.tick;
        system.tick(context); resumed.tick(resumedContext); launched.tick(launchedContext);
        if (system.stateOf("A", original->id) != SuperWeaponRuntimeState::Cooldown
            || system.stateHash() != resumed.stateHash() || system.stateHash() != launched.stateHash()
            || !sameObjects(objects, resumedObjects) || !sameObjects(objects, launchedObjects))
            return bad("first launch tick continuation");
        auto expectedObjects = initialObjects;
        if (!empty) for (auto& object : expectedObjects) {
            if (object.instanceId <= 4) {
                object.x += 16; object.y += 16; object.cell = object.y * 1000 + object.x; object.level = 3;
            } else if (object.instanceId == 6) { object.alive = false; object.health = 0; }
        }
        if (!sameObjects(objects, expectedObjects)) return bad("original recipients and unrelated objects");
        const auto appliedOnce = [&](const auto& trace) {
            int applied = 0;
            for (const auto& event : trace) if (event.kind == "ChronoSphereApplied") {
                if (event.targetCell != first.cell || event.targetCell2 != second.cell
                    || event.value != (empty ? 0 : 5)) return false;
                ++applied;
            }
            return applied == 1;
        };
        if (!appliedOnce(events) || !appliedOnce(resumedEvents) || !appliedOnce(launchedEvents))
            return bad("empty/occupied cast executed exactly once");
        SuperWeaponSystem cooldown;
        if (!restore(system, cooldown)
            || cooldown.stateOf("A", original->id) != SuperWeaponRuntimeState::Cooldown)
            return bad("Version8 cooldown restore");
        auto cooldownObjects = objects; auto cooldownContext = quietContext;
        cooldownContext.objects = &cooldownObjects;
        for (int n = 0; n < 3; ++n) {
            ++context.tick;
            resumedContext.tick = launchedContext.tick = cooldownContext.tick = context.tick;
            system.tick(context); resumed.tick(resumedContext); launched.tick(launchedContext); cooldown.tick(cooldownContext);
            if (system.stateHash() != resumed.stateHash() || system.stateHash() != launched.stateHash()
                || system.stateHash() != cooldown.stateHash() || !sameObjects(objects, expectedObjects)
                || !sameObjects(objects, resumedObjects) || !sameObjects(objects, launchedObjects)
                || !sameObjects(objects, cooldownObjects)) return bad("cooldown continuation");
        }
        if (!appliedOnce(events) || !appliedOnce(resumedEvents) || !appliedOnce(launchedEvents))
            return bad("cooldown repeated cast");
        ++cases; if (empty) ++emptyCasts;
    }
    std::cout << "Chronosphere ground: passed cases=" << cases << " emptyCasts=" << emptyCasts
        << " savesVersion8=" << saves << " rejectedCellsAndOptions=" << rejected
        << " twoStages/hoverHints/recipientPolicy/cancel/continuation=ok\n";
    return true;
}
}
