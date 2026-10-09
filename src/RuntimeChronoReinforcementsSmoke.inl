bool runRuntimeChronoReinforcementsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Chrono reinforcements: " + why; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.gameMode = "Battle";
    stock->config.defaultMapPath = pathString(std::filesystem::path(config.localDataRootPath)
        / "resources/mix_exports/mapsmd03/all07smd.map");
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original all07 resources unavailable");
    IniFile scenario;
    if (!scenario.loadFromFile(stock->config.defaultMapPath)) return fail("original all07 map unavailable");
    if (stock->rules.getSpecialMechanismRules().chronoReinforcementDelay != 180
        || stock->rules.getSpecialMechanismRules().chronoDelay != 60) return fail("stock delay fields");
    const auto makeWorld = [&]() {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& state = *result;
        state.config = stock->config; state.config.fixedTickHz = 30;
        state.config.computerAiEnabled = false; state.config.shortGame = false;
        state.rules = stock->rules; state.art = stock->art; state.theater = stock->theater;
        state.world = stock->world; state.world.accessObjects().clear();
        state.report = stock->report; state.mapFacts = stock->mapFacts;
        state.rulesLoaded = state.artLoaded = true;
        state.economy.owner = "Americans"; state.visionEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        state.triggers.loaded = state.triggers.bootstrapComplete = true; state.tick = 100;
        initializeRuntimeNavigation(state);
        for (const auto* id : {"0964C97C", "04E3AA8C"}) {
            const auto team = parseRuntimeTeamType(scenario, id);
            state.triggers.teams[team.id] = team;
            state.triggers.taskForces[team.taskForceId] = parseRuntimeTaskForce(scenario, team.taskForceId);
            state.triggers.scripts[team.scriptId] = parseRuntimeScriptType(scenario, team.scriptId);
        }
        return result;
    };
    const auto arrivalEffects = [](const RuntimeWindowState& state) {
        return std::count_if(state.impactEffects.begin(), state.impactEffects.end(), [](const auto& effect) {
            return effect.lifecycleState == "ChronoReinforcementIn";
        });
    };
    {
        auto storage = makeWorld(); auto& state = *storage;
        for (const auto& [id, waypoint] : std::array<std::pair<const char*, const char*>, 2>{
                {{"0964C97C", "AA"}, {"04E3AA8C", "AB"}}}) {
            RuntimeTriggerDefinition trigger; trigger.id = id;
            RuntimeTriggerAction action; action.type = 107; action.targetId = id;
            action.arguments.resize(8, "0"); action.arguments[7] = waypoint;
            if (!fireRuntimeCreateTeamTrigger(state, trigger, action, false) || !trigger.fired)
                return fail("original action 107 rejected");
        }
        if (state.world.getObjects().size() != 2 || state.triggers.reinforcements.pending.size() != 2
            || !state.triggers.teamMissions.empty() || state.triggers.scriptStepsIssued != 0
            || arrivalEffects(state) != 2) return fail("arrival/script/effect count: objects="
                + std::to_string(state.world.getObjects().size()) + " pending="
                + std::to_string(state.triggers.reinforcements.pending.size()) + " missions="
                + std::to_string(state.triggers.teamMissions.size()) + " scripts="
                + std::to_string(state.triggers.scriptStepsIssued) + " effects="
                + std::to_string(arrivalEffects(state)) + " status=" + state.triggers.status);
        for (const auto& [id, job] : state.triggers.reinforcements.pending) {
            const auto* object = findObjectById(state, job.objectIds.front());
            if (!object || object->mission != "ChronoFrozen"
                || runtimeCellDistance(runtimeCellPointForObject(*object), {job.originX, job.originY}) > 6)
                return fail("original waypoint arrival");
        }
        for (const auto& effect : state.impactEffects) {
            if (effect.lifecycleState == "ChronoReinforcementIn" && effect.animId != "WARPIN")
                return fail("missing original arrival animation");
        }
        const auto checkArrivalActors = [&](const RuntimeWindowState& current) {
            int count = 0;
            for (const auto& actor : *runtimeImpactRenderActorsForRender(current)) {
                if (actor.lifecycleState != "ChronoReinforcementIn") continue;
                ++count;
                if (actor.animId != "WARPIN" || !actor.centerOnAnchor)
                    return fail("reinforcement renderer: " + actor.lifecycleState + " " + actor.animId);
            }
            return count == 2 || fail("reinforcement renderer lost an arrival");
        };
        if (!checkArrivalActors(state)) return false;
        auto transferred = std::make_unique<RuntimeWindowState>();
        transferred->rules = state.rules;
        for (const auto& effect : state.impactEffects) {
            if (effect.lifecycleState != "ChronoReinforcementIn") continue;
            RuntimeImpactEffect copy;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect), copy)
                || !copy.animId.empty() || copy.remainingTicks != effect.remainingTicks)
                return fail("reinforcement snapshot did not preserve the animation phase");
            transferred->impactEffects.push_back(std::move(copy));
        }
        if (!checkArrivalActors(*transferred)) return false;
        const auto snapshots = state.specialMechanisms.renderSnapshots(state.tick, 30);
        const auto chronoCount = std::count_if(snapshots.begin(), snapshots.end(), [](const auto& snapshot) {
            return snapshot.kind == "chrono" && snapshot.expireTick == 460 && snapshot.progress == 0;
        });
        if (chronoCount != 2) return fail("360-tick arrival recovery/render duration");
        for (auto& object : state.world.accessObjects()) {
            const auto cell = runtimeCellPointForObject(object);
            RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move;
            move.moveTarget = {cell.x + 1, cell.y}; move.path.push_back(move.moveTarget);
            RuntimeObjectBehavior attack; attack.kind = RuntimeBehaviorKind::Attack;
            attack.attackGround = true; attack.attackBridgeCell = cell;
            if (advanceRuntimeMoveBehavior(state, object, move)
                || advanceRuntimeAttackBehavior(state, object, attack)
                || runtimeCellPointForObject(object) != cell || !state.projectiles.empty()
                || runtimeObjectCanMove(state, object)
                || (object.type == "AMCV" && runtimeSecondaryAbilityForObject(state, object) != RuntimeSecondaryAbility::None))
                return fail("movement, fire or deploy bypassed freeze");
        }
        state.tick = 240;
        advanceRuntimeSpecialMechanisms(state); advanceRuntimeTriggers(state);
        if (state.triggers.scriptStepsIssued != 0) return fail("script advanced during recovery");
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile save;
        if (!save.loadFromString(saved.str())) return fail("save parse");
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, state, false);
        if (!materializeRuntimeLoadStateFromIni(*restored, save, error)) return false;
        // Ordinary saves discard transient impact effects. Recovery and its
        // sparkle persist below; full network snapshots retain the arrival.
        if (serializeRuntimeTriggerReinforcements(restored->triggers.reinforcements)
                != serializeRuntimeTriggerReinforcements(state.triggers.reinforcements)
            || restored->specialMechanisms.stateHash() != state.specialMechanisms.stateHash())
            return fail("save changed arrival recovery or queued script");
        for (auto* resumed : {&state, restored.get()}) {
            const auto effectCount = arrivalEffects(*resumed);
            resumed->tick = 459;
            advanceRuntimeSpecialMechanisms(*resumed); advanceRuntimeTriggers(*resumed);
            if (resumed->triggers.scriptStepsIssued != 0 || resumed->triggers.reinforcements.pending.size() != 2)
                return fail("released before exact deadline");
            resumed->tick = 460;
            advanceRuntimeSpecialMechanisms(*resumed); advanceRuntimeTriggers(*resumed);
            if (!resumed->triggers.reinforcements.pending.empty() || resumed->triggers.scriptStepsIssued != 2
                || arrivalEffects(*resumed) != effectCount) return fail("script resume or duplicate arrival effect");
            for (const auto& object : resumed->world.getObjects()) {
                if (resumed->specialMechanisms.isChronoMovementLocked(object.instanceId, resumed->tick)
                    || object.mission != "Sleep") return fail("original sleep script not restored after recovery");
            }
            ++resumed->tick; advanceRuntimeTriggers(*resumed);
            if (resumed->triggers.scriptStepsIssued != 2 || resumed->world.getObjects().size() != 2)
                return fail("repeated script or duplicate object");
        }
        std::cout << "Original all07 action 107: MCV+Grizzly=yes authored waypoint=yes WARPIN=yes "
            "360-tick freeze=yes move/fire/deploy locked=yes script held=yes save/resume=yes\n";
    }
    {
        auto storage = makeWorld(); auto& state = *storage;
        RuntimeTriggerDefinition trigger;
        RuntimeTriggerAction action; action.type = 7; action.targetId = "04E3AA8C";
        if (!fireRuntimeCreateTeamTrigger(state, trigger, action, false)
            || !state.triggers.reinforcements.pending.empty() || state.triggers.scriptStepsIssued != 1
            || arrivalEffects(state) != 0) return fail("ordinary reinforcement delayed");
        auto settings = stock->rules.getSpecialMechanismRules();
        for (int hz : {15, 30, 60}) for (int delay : {0, 3, 180}) {
            settings.chronoReinforcementDelay = delay;
            RuntimeSpecialMechanismState mechanism; mechanism.reset(settings);
            auto object = state.world.getObjects().front(); object.mission = "Guard";
            mechanism.beginChronoReinforcementRecovery(object, 100, hz);
            const int deadline = 100 + delay * hz / 15;
            if ((delay > 0 && !mechanism.isChronoMovementLocked(object.instanceId, deadline - 1))
                || mechanism.isChronoMovementLocked(object.instanceId, deadline)) return fail("custom delay/clock conversion");
            mechanism.beginChronosphereRecovery(object, 100, hz);
            if (!mechanism.isChronoMovementLocked(object.instanceId, 100 + 60 * hz / 15 - 1)
                || mechanism.isChronoMovementLocked(object.instanceId, 100 + 60 * hz / 15))
                return fail("reinforcement delay changed ChronoDelay");
        }
        IniFile firstIni, secondIni;
        firstIni.loadFromString("[General]\nChronoReinfDelay=3\n");
        secondIni.loadFromString("[General]\nChronoReinfDelay=4\n");
        RulesDatabase first, second;
        first.loadFromIni(firstIni); second.loadFromIni(secondIni);
        if (first.getDetailRulesHash() == second.getDetailRulesHash()) return fail("delay absent from rules fingerprint");
        std::cout << "Chrono reinforcement rules: zero/custom delay=yes 15/30/60 Hz=yes ordinary immediate=yes "
            "ChronoDelay unchanged=yes rules fingerprint=yes\n";
    }
    std::cout << "Chrono reinforcements: passed\n";
    error.clear();
    return true;
}
