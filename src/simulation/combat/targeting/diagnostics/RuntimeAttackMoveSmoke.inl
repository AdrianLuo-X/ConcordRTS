bool runRuntimeAttackMoveSmokeProbe(std::string& error) {
    using Kind = RuntimeMultiplayerGameplayTargetOrderKind;
    const auto fail = [&](const std::string& reason) { error = "attack move: " + reason; return false; };
    const auto makeState = [&]() {
        auto state = std::make_unique<RuntimeWindowState>();
        IniFile ini;
        ini.loadFromString(R"TRAVEL(
[General]
ChronoDelay=3
[VehicleTypes]
0=AMTANK
1=AMTARGET
2=AMEXEMPT
3=AMCHRONO
[AMTANK]
Strength=200
Speed=8
Sight=6
GuardRange=6
SpeedType=Track
MovementZone=Normal
Primary=AMGUN
[AMTARGET]
Strength=120
Speed=1
Sight=1
SpeedType=Track
MovementZone=Normal
[AMEXEMPT]
Strength=200
Speed=8
SpeedType=Track
MovementZone=Normal
Primary=AMGUN
PreventAttackMove=yes
[AMCHRONO]
Strength=200
Speed=8
SpeedType=Track
MovementZone=Normal
Primary=AMGUN
Teleporter=yes
[AMGUN]
Damage=60
Range=4
ROF=6
Projectile=AMSHOT
Speed=100
OmniFire=yes
Warhead=AMWH
[AMSHOT]
AG=yes
Instant=yes
[AMWH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
AffectsAllies=no
AffectsEnemies=yes
)TRAVEL");
        state->rules.loadFromIni(ini);
        state->rulesLoaded = true;
        state->config.fixedTickHz = 15;
        state->runtimeSuppressPlayerAudioFeedback = true;
        state->visionEnabled = false;
        state->economy.owner = "Player";
        RuntimeHumanHouseAuthority authority;
        authority.owner = "Player";
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true;
        state->humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = 0;
            state->world.accessTerrainCells().push_back(cell);
            state->terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        state->terrainByCell = buildRuntimeTerrainLookup(state->world);
        return state;
    };
    const auto add = [&](RuntimeWindowState& state, int id, std::string owner,
        int x, int y, std::string type = "AMTARGET") {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = WorldObjectKind::Unit;
        object.owner = std::move(owner); object.type = std::move(type);
        object.mapId = "Travel" + std::to_string(id);
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.health = object.type == "AMTARGET" ? 120 : 200;
        object.alive = object.poweredOperational = true; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    const auto tick = [&](RuntimeWindowState& state) {
        ++state.tick;
        (void)advanceRuntimeSimulation(state, false);
    };
    const auto order = [&](RuntimeWindowState& state, Kind kind, int x, int y) {
        state.selectedObjectIds = {1};
        return dispatchRuntimeTargetOrderFromUi(state, kind, {x, y});
    };
    if (runtimePlayerModifiedTargetOrder(true, true, false, true) != Kind::AttackMove
        || runtimePlayerModifiedTargetOrder(true, false, true, false) != Kind::GuardDestination
        || runtimePlayerModifiedTargetOrder(true, false, true, true) != Kind::Follow
        || runtimePlayerModifiedTargetOrder(true, false, false, false) != Kind::ForceFireCell)
        return fail("mouse modifiers conflict");
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "Player", 2, 5, "AMTANK");
        add(state, 2, "Enemy", 11, 5);
        add(state, 3, "Player", 9, 7);
        add(state, 4, "Neutral", 9, 8);
        initializeRuntimeNavigation(state);
        if (!order(state, Kind::AttackMove, 30, 5) || !state.playerCommands.travelling.contains(1)) {
            const auto* actor = findObjectById(state, 1);
            const auto* unit = actor ? findRuntimeUnitType(state.rules, *actor) : nullptr;
            return fail("dispatcher did not retain destination; verb=" + state.lastCommand.verb
                + " accepted=" + std::to_string(state.lastCommand.acceptedCount)
                + " rejected=" + std::to_string(state.lastCommand.rejectedCount)
                + " unit=" + (unit ? unit->id : "missing")
                + " owner=" + state.economy.owner + " " + state.interactionStatus);
        }
        bool fought = false, resumed = false, arrived = false;
        for (int frame = 0; frame < 900; ++frame) {
            tick(state);
            const auto behavior = state.behaviors.find(1);
            const auto* enemy = findObjectById(state, 2);
            const auto* actor = findObjectById(state, 1);
            fought = fought || (behavior != state.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Attack
                && behavior->second.targetObjectId == 2 && !behavior->second.forceFire);
            if (fought && (!enemy || !enemy->alive)) {
                resumed = resumed || (behavior != state.behaviors.end()
                    && behavior->second.kind == RuntimeBehaviorKind::Move);
                arrived = actor && actor->x == 30 && actor->y == 5
                    && !state.playerCommands.travelling.contains(1);
            }
            if (arrived) break;
        }
        if (!fought || !resumed || !arrived) {
            const auto* actor = findObjectById(state, 1);
            return fail("fight/resume/arrival=" + std::to_string(fought) + "/"
                + std::to_string(resumed) + "/" + std::to_string(arrived)
                + " actor=" + std::to_string(actor ? actor->x : -1) + ","
                + std::to_string(actor ? actor->y : -1) + " " + state.simulationStatus);
        }
        if (findObjectById(state, 3)->health != 120 || findObjectById(state, 4)->health != 120)
            return fail("attack move damaged friendly or neutral targets");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "Player", 2, 5, "AMTANK");
        add(state, 2, "Enemy", 5, 8);
        add(state, 3, "Enemy", 24, 5);
        initializeRuntimeNavigation(state);
        if (!order(state, Kind::GuardDestination, 22, 5)) return fail("guard destination dispatcher");
        bool guarded = false, fought = false;
        for (int frame = 0; frame < 900; ++frame) {
            tick(state);
            if (findObjectById(state, 2)->health != 120) return fail("guard destination engaged en route");
            const auto travel = state.playerCommands.travelling.find(1);
            guarded = guarded || (travel != state.playerCommands.travelling.end() && travel->second.guarding);
            const auto* target = findObjectById(state, 3);
            fought = fought || !target || !target->alive || target->health < 120;
            if (guarded && fought) break;
        }
        if (!guarded || !fought) return fail("destination did not become an active guard area");
        // A retreating enemy cannot drag a destination guard across the map.
        if (auto* target = findMutableObjectById(state, 3)) {
            target->x = 38; target->y = 5; target->cell = runtimeCellKey(38, 5);
        }
        for (int frame = 0; frame < 100; ++frame) tick(state);
        if (findObjectById(state, 1)->x > 28) return fail("guard chased outside its area");
        RuntimePlayerCommandState saved;
        if (!saved.restore(state.playerCommands.serialize(), error)
            || saved.stateHash() != state.playerCommands.stateHash()) return fail("active guard save round trip");
        if (!dispatchRuntimeSelectionCommandFromUi(state, RuntimeMultiplayerGameplayCommandType::Stop)
            || state.playerCommands.travelling.contains(1)) return fail("Stop did not cancel destination guard");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "Player", 2, 5, "AMTANK");
        initializeRuntimeNavigation(state);
        if (!order(state, Kind::AttackMove, 20, 5)) return fail("replacement setup");
        const auto saved = state.playerCommands.serialize();
        if (!state.playerCommands.restore(saved, error) || !order(state, Kind::Move, 4, 5)
            || state.playerCommands.travelling.contains(1)) return fail("ordinary move did not replace travel intent");
        findMutableObjectById(state, 1)->type = "AMEXEMPT";
        if (!order(state, Kind::AttackMove, 20, 5) || state.playerCommands.travelling.contains(1))
            return fail("PreventAttackMove was ignored");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "Player", 2, 5, "AMCHRONO");
        initializeRuntimeNavigation(state);
        if (!order(state, Kind::GuardDestination, 12, 5)
            || !state.playerCommands.travelling.contains(1)) return fail("chrono guard lost its destination");
        for (int frame = 0; frame < 120; ++frame) tick(state);
        const auto travel = state.playerCommands.travelling.find(1);
        const auto* actor = findObjectById(state, 1);
        if (!actor || actor->x != 12 || actor->y != 5 || travel == state.playerCommands.travelling.end()
            || !travel->second.guarding) return fail("chrono recovery did not become a destination guard");
    }
    error.clear(); return true;
}
