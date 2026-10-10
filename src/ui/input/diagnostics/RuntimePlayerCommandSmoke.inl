// Runs the real dispatchers, path/movement executor, command scheduler and
// factory candidate/spawn routines on a small isolated terrain fixture.
bool runRuntimePlayerCommandSmokeProbe(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "player commands: " + why; return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    IniFile ini;
    if (!ini.loadFromString(R"PLAYER(
[General]
ChronoDelay=3
[VehicleTypes]
0=PCUNIT
1=PCMAGUNIT
[BuildingTypes]
0=PCFACTORY
1=PCGRINDER
[WeaponTypes]
0=PCGUN
[PCUNIT]
Strength=200
Cost=100
TechLevel=1
Speed=8
SpeedType=Track
MovementZone=Normal
Primary=PCGUN
Secondary=PCGUN
IsSimpleDeployer=yes
[PCMAGUNIT]
Strength=200
Cost=100
TechLevel=1
Speed=8
SpeedType=Track
MovementZone=Normal
Primary=PCGUN
Ammo=1
[PCGUN]
Damage=10
Range=10
ROF=30
Burst=3
BurstDelay=3
OmniFire=yes
Projectile=PCSHOT
Speed=100
Warhead=PCWH
[PCSHOT]
AG=yes
[PCWH]
AffectsEnemies=yes
AffectsAllies=no
[PCGRINDER]
Strength=1000
Cost=600
Grinding=yes
Foundation=1x1
[PCFACTORY]
Strength=1000
Cost=100
TechLevel=1
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
)PLAYER") || !state.rules.loadFromIni(ini)) return fail("fixture rules");
    state.rulesLoaded = true;
    state.config.fixedTickHz = 15;
    state.config.techLevel = 10;
    {
        // A single ordinary unit, with no map, nav state, prior activity or
        // special-capable companion, must keep the host recovery tick alive.
        auto recoveryOnly = std::make_unique<RuntimeWindowState>();
        recoveryOnly->rules = state.rules;
        recoveryOnly->rulesLoaded = true;
        recoveryOnly->config.fixedTickHz = 15;
        recoveryOnly->runtimeSuppressPlayerAudioFeedback = true;
        recoveryOnly->tick = 10;
        WorldObjectInstance ordinary;
        ordinary.instanceId = 101; ordinary.kind = WorldObjectKind::Unit;
        ordinary.type = "PCUNIT"; ordinary.owner = "Player";
        ordinary.alive = true; ordinary.health = 200; ordinary.mission = "Guard";
        recoveryOnly->world.accessObjects().push_back(ordinary);
        ensureRuntimeSpecialMechanismsInitialized(*recoveryOnly);
        recoveryOnly->specialMechanisms.beginChronosphereRecovery(
            recoveryOnly->world.accessObjects().front(), recoveryOnly->tick, 15);
        for (int frame = 0; frame < 3; ++frame) {
            if (!recoveryOnly->specialMechanisms.isChronoMovementLocked(101, recoveryOnly->tick))
                return fail("isolated ordinary unit recovery unlocked early");
            ++recoveryOnly->tick;
            (void)advanceRuntimeSpecialMechanisms(*recoveryOnly);
        }
        if (recoveryOnly->world.getObjects().front().mission != "Guard"
            || recoveryOnly->specialMechanisms.isChronoMovementLocked(101, recoveryOnly->tick)
            || recoveryOnly->specialMechanisms.hasActiveChronoTemporalStates())
            return fail("isolated ordinary unit recovery skipped host tick or retained its record");
    }
    state.economy.owner = "Player";
    state.economy.credits = 10000;
    state.economy.techLevel = 10;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.visionEnabled = false;
    state.structureFootprints["PCFACTORY"] = {1, 1, true};
    state.structureFootprints["PCGRINDER"] = {1, 1, true};
    RuntimeHumanHouseAuthority authority;
    authority.owner = "Player";
    authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
    authority.localFacade = true;
    state.humanHouseAuthorities.push_back(std::move(authority));
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y; terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const std::string& owner, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind;
        object.type = kind == WorldObjectKind::Structure ? "PCFACTORY" : "PCUNIT";
        object.mapId = "PlayerCommandSmoke" + std::to_string(id);
        object.owner = owner; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.health = kind == WorldObjectKind::Structure ? 1000 : 200;
        object.alive = true; object.poweredOperational = true; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Unit, "Player", 2, 2);
    add(2, WorldObjectKind::Unit, "Player", 12, 3);
    add(3, WorldObjectKind::Unit, "Enemy", 12, 5);
    add(4, WorldObjectKind::Unit, "Player", 4, 6);
    state.world.accessObjects().back().type = "PCMAGUNIT";
    state.world.accessObjects().back().currentAmmo = 1;
    add(10, WorldObjectKind::Structure, "Player", 10, 10);
    add(90, WorldObjectKind::Structure, "Player", 20, 10);
    add(100, WorldObjectKind::Structure, "Player", 28, 28);
    state.world.accessObjects().back().type = "PCGRINDER";
    {
        // Exercise the D-key dispatcher with deployers on both sides of an
        // ordinary unit and a stale selection ID.
        state.selectedObjectIds = {1, 4, 2, 404};
        const auto settleDeployment = [&]() {
            for (int frame = 0; frame < 120; ++frame) {
                ++state.tick;
                (void)advanceRuntimeBuildAnimations(state);
                (void)advanceRuntimeDeployTransitions(state);
                const auto* first = findObjectById(state, 1);
                const auto* second = findObjectById(state, 2);
                if (!runtimeMissionIsDeploying(*first) && !runtimeMissionIsUndeploying(*first)
                    && !runtimeMissionIsDeploying(*second) && !runtimeMissionIsUndeploying(*second))
                    return true;
            }
            return false;
        };
        const auto bothDeployed = [&]() {
            return runtimeObjectHasDeployedState(state, *findObjectById(state, 1))
                && runtimeObjectHasDeployedState(state, *findObjectById(state, 2));
        };
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(state) || !bothDeployed()
            || findObjectById(state, 4)->mission != "Guard"
            || state.selectedObjectIds != std::vector<int>{1, 4, 2})
            return fail("D did not deploy every capable actor in a mixed selection");
        if (!settleDeployment() || !dispatchRuntimeSecondaryActionForSelectionFromUi(state)
            || !runtimeMissionIsUndeploying(*findObjectById(state, 1))
            || !runtimeMissionIsUndeploying(*findObjectById(state, 2))
            || !settleDeployment())
            return fail("D did not undeploy the entire group");
        if (!runtimeObjectCanMove(state, *findObjectById(state, 1))
            || !runtimeObjectCanMove(state, *findObjectById(state, 2)))
            return fail("group undeployment did not restore movement");

        RuntimeMultiplayerRuntimeBridge bridge;
        state.multiplayerBridge = &bridge;
        const auto savedSlots = state.config.playerSlots;
        RuntimeConfig::PlayerSlot localSlot;
        localSlot.enabled = localSlot.human = localSlot.local = true;
        state.config.playerSlots = {localSlot};
        std::vector<RuntimeMultiplayerGameplayCommand> captured;
        bool rejectCapture = false;
        state.multiplayerIntentRouter.setCaptureCallback(
            [&](const RuntimeMultiplayerGameplayCommand& command, std::string& captureError) {
                const auto* secondary = std::get_if<RuntimeMultiplayerGameplaySecondaryActionCommand>(
                    &command.body);
                if (secondary == nullptr || secondary->objectIds.empty()) {
                    captureError = "expected a secondary action selection";
                    return false;
                }
                if (rejectCapture) {
                    captureError = "fixture capture rejection";
                    return false;
                }
                captured.push_back(command);
                captureError.clear();
                return true;
            });
        const bool networkAccepted = dispatchRuntimeSecondaryActionForSelectionFromUi(state);
        if (!networkAccepted || captured.size() != 1
            || std::get<RuntimeMultiplayerGameplaySecondaryActionCommand>(captured[0].body).objectIds
                != std::vector<std::int32_t>{1, 2}
            || runtimeObjectHasDeployedState(state, *findObjectById(state, 1))
            || runtimeObjectHasDeployedState(state, *findObjectById(state, 2)))
            return fail("multiplayer D did not queue the full group without local simulation changes");
        for (const auto& command : captured) {
            std::string validationError;
            if (!prevalidateRuntimeMultiplayerGameplayCommand(state, "Player", command, validationError)
                || !applyRuntimeMultiplayerGameplayCommand(state, "Player", command))
                return fail("authoritative group deployment rejected an actor");
        }
        if (!bothDeployed() || !settleDeployment())
            return fail("authoritative commands did not deploy the whole group");
        captured.clear();
        const std::size_t savedObjectCount = state.world.getObjects().size();
        std::vector<std::int32_t> largeSelection;
        state.selectedObjectIds.clear();
        for (int id = 200; id < 500; ++id) {
            add(id, WorldObjectKind::Unit, "Player", 15, 15);
            state.selectedObjectIds.push_back(id);
            largeSelection.push_back(id);
        }
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(state) || captured.size() != 2)
            return fail("large multiplayer deployment used one frame slot per actor or exceeded its payload");
        std::vector<std::int32_t> submittedIds;
        for (const auto& command : captured) {
            const auto& ids = std::get<RuntimeMultiplayerGameplaySecondaryActionCommand>(command.body).objectIds;
            if (ids.size() > runtimeMultiplayerGameplayMaximumObjectIds)
                return fail("group deployment exceeded the wire actor limit");
            submittedIds.insert(submittedIds.end(), ids.begin(), ids.end());
        }
        if (submittedIds != largeSelection)
            return fail("large multiplayer deployment omitted or duplicated selected actors");
        state.world.accessObjects().resize(savedObjectCount);
        state.selectedObjectIds = {1, 4, 2};
        captured.clear();
        rejectCapture = true;
        const bool rejectedAccepted = dispatchRuntimeSecondaryActionForSelectionFromUi(state);
        state.multiplayerIntentRouter.setCaptureCallback({});
        state.multiplayerIntentRouter.setAuthorityState({});
        state.multiplayerBridge = nullptr;
        state.config.playerSlots = savedSlots;
        if (rejectedAccepted || !captured.empty() || !bothDeployed())
            return fail("rejected multiplayer deployment changed simulation state");
        RuntimeMultiplayerGameplayCommand mixedToggle;
        mixedToggle.body = RuntimeMultiplayerGameplaySecondaryActionCommand{
            RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle, {1, 2, 4}};
        std::string toggleError;
        if (!prevalidateRuntimeMultiplayerGameplayCommand(state, "Player", mixedToggle, toggleError)
            || !applyRuntimeMultiplayerGameplayCommand(state, "Player", mixedToggle)
            || !runtimeMissionIsUndeploying(*findObjectById(state, 1))
            || !runtimeMissionIsUndeploying(*findObjectById(state, 2))
            || findObjectById(state, 4)->mission != "Guard" || !settleDeployment())
            return fail("one incapable actor prevented the group's authoritative undeployment");
        state.selectedObjectIds = {4};
        if (dispatchRuntimeSecondaryActionForSelectionFromUi(state))
            return fail("D accepted a selection with no secondary action");
        state.selectedObjectIds.clear();
        if (dispatchRuntimeSecondaryActionForSelectionFromUi(state))
            return fail("D accepted an empty selection");
    }
    state.selectedObjectIds = {1};
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Move, {5, 2}))
        return fail("initial move dispatcher rejected: " + state.interactionStatus);
    state.playerLocalCommands.planning = true;
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Move, {7, 2}))
        return fail("Z planning append rejected");
    if (!state.playerCommands.queues.contains(1)
        || state.behaviors.at(1).moveTarget != RuntimeCellPoint{5, 2})
        return fail("append replaced the active move");
    const auto driveMovement = [&]() {
        ++state.tick;
        (void)processRuntimePathRequests(state);
        auto* actor = findMutableObjectById(state, 1);
        auto active = state.behaviors.find(1);
        if (actor && active != state.behaviors.end() && active->second.kind == RuntimeBehaviorKind::Move)
            (void)advanceRuntimeMoveBehavior(state, *actor, active->second);
    };
    bool completedFirst = false;
    bool completedSecond = false;
    for (int frame = 0; frame < 600; ++frame) {
        (void)advanceRuntimePlayerCommands(state);
        auto active = state.behaviors.find(1);
        if (active != state.behaviors.end() && active->second.moveTarget == RuntimeCellPoint{7, 2}
            && !completedFirst) return fail("queued move executed before first arrival");
        driveMovement();
        active = state.behaviors.find(1);
        const auto* actor = findObjectById(state, 1);
        if (actor && active != state.behaviors.end()
            && active->second.state == RuntimeBehaviorState::Complete) {
            completedFirst = completedFirst || (actor->x == 5 && actor->y == 2);
            completedSecond = completedSecond || (actor->x == 7 && actor->y == 2);
        }
        if (completedSecond) break;
    }
    if (!completedFirst || !completedSecond || state.playerCommands.queues.contains(1))
        return fail("real movement did not visit both queued waypoints");
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Move, {9, 2}))
        return fail("stop test queue setup");
    RuntimeMultiplayerGameplayCommand stop;
    stop.body = RuntimeMultiplayerGameplayStopCommand{{1}};
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop)
        || state.playerCommands.queues.contains(1)) return fail("authoritative Stop left a queued route");
    state.playerLocalCommands.planning = false;
    const auto appendMove = [&](RuntimeCellPoint cell) {
        RuntimeMultiplayerGameplayTargetOrderCommand order;
        order.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
        order.objectIds = {1};
        order.target = RuntimeMultiplayerGameplayCellTarget{{cell.x, cell.y}};
        order.append = true;
        return applyRuntimePlayerTargetOrder(state, "Player", order);
    };
    const auto tickChronoRecovery = [&]() {
        ++state.tick;
        // Use the real host entry, including its idle fast path. Recovery of
        // an ordinary (non-teleporter) unit must keep that host tick alive.
        (void)advanceRuntimeSpecialMechanisms(state);
    };
    RuntimeMultiplayerGameplayTargetOrderCommand expiredTarget;
    expiredTarget.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    expiredTarget.objectIds = {1};
    expiredTarget.target = RuntimeMultiplayerGameplayObjectTarget{999};
    expiredTarget.append = true;
    if (!applyRuntimePlayerTargetOrder(state, "Player", expiredTarget)
        || !appendMove({9, 2}) || !appendMove({11, 2}))
        return fail("Chronosphere route setup");
    auto* recovering = findMutableObjectById(state, 1);
    state.specialMechanisms.beginChronosphereRecovery(*recovering, state.tick, state.config.fixedTickHz);
    if (!state.specialMechanisms.isChronoMovementLocked(1, state.tick))
        return fail("Chronosphere fixture did not enter recovery");
    // A stale target is still discarded, then both real moves must survive
    // every recovery tick even though the active behavior has been cleared.
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.at(1).orders.size() != 2)
        return fail("expired queued target was not skipped during recovery");
    for (int frame = 0; frame < 30 && state.specialMechanisms.isChronoMovementLocked(1, state.tick); ++frame) {
        (void)advanceRuntimePlayerCommands(state);
        if (!state.playerCommands.queues.contains(1)
            || state.playerCommands.queues.at(1).orders.size() != 2 || state.behaviors.contains(1))
            return fail("Chronosphere recovery consumed a valid queued move");
        tickChronoRecovery();
    }
    if (state.specialMechanisms.isChronoMovementLocked(1, state.tick))
        return fail("Chronosphere recovery did not unlock");
    (void)advanceRuntimePlayerCommands(state);
    if (!state.behaviors.contains(1) || state.behaviors.at(1).moveTarget != RuntimeCellPoint{9, 2}
        || state.playerCommands.queues.at(1).orders.size() != 1)
        return fail("Chronosphere route did not resume at the first retained move");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop))
        return fail("Chronosphere cleanup Stop");

    if (!appendMove({9, 2})) return fail("landing route setup");
    for (const char* transition : {"LandingToDeploy", "TakingOffAfterUndeploy"}) {
        findMutableObjectById(state, 1)->mission = transition;
        (void)advanceRuntimePlayerCommands(state);
        if (!state.playerCommands.queues.contains(1) || state.behaviors.contains(1))
            return fail("landing/takeoff transition consumed a queued move");
    }
    findMutableObjectById(state, 1)->mission = "Guard";
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.contains(1) || !state.behaviors.contains(1)
        || state.behaviors.at(1).moveTarget != RuntimeCellPoint{9, 2})
        return fail("landing/takeoff route did not resume");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop))
        return fail("landing cleanup Stop");

    auto* deployed = findMutableObjectById(state, 1);
    deployed->deployed = true; deployed->mission = "Deployed";
    RuntimeMultiplayerGameplayTargetOrderCommand deployedAttack;
    deployedAttack.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    deployedAttack.objectIds = {1};
    deployedAttack.target = RuntimeMultiplayerGameplayObjectTarget{3};
    deployedAttack.append = true;
    if (runtimeObjectCanMove(state, *deployed) || !applyRuntimePlayerTargetOrder(state, "Player", deployedAttack))
        return fail("deployed attack fixture");
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.contains(1) || !state.behaviors.contains(1)
        || state.behaviors.at(1).kind != RuntimeBehaviorKind::Attack)
        return fail("immobile deployed actor did not execute its valid queued attack");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop))
        return fail("deployed cleanup Stop");
    // Stop preserves deployment while the attack's returnToDeployed flag is
    // still active. Restore this fixture only after Stop has erased that flag.
    deployed = findMutableObjectById(state, 1);
    deployed->deployed = false; deployed->mission = "Guard";
    if (!runtimeObjectCanMove(state, *deployed)) return fail("deployed fixture did not restore mobility");

    if (dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow, {12, 5}, findObjectById(state, 3)))
        return fail("Follow accepted an enemy");
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow, {12, 3}, findObjectById(state, 2)))
        return fail("Follow rejected allied target");
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.following.contains(1)
        || state.playerCommands.following.at(1).lastTargetCell != RuntimeMultiplayerGameplayCell{12, 3})
        return fail("Follow did not issue a path toward its allied target");
    auto* ally = findMutableObjectById(state, 2);
    ally->x = 15; ally->cell = runtimeCellKey(15, ally->y);
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.following.at(1).lastTargetCell != RuntimeMultiplayerGameplayCell{15, 3})
        return fail("Follow did not refresh a moving target");
    eraseRuntimeBehavior(state, 1);
    state.specialMechanisms.beginChronosphereRecovery(*findMutableObjectById(state, 1),
        state.tick, state.config.fixedTickHz);
    ally->x = 16; ally->cell = runtimeCellKey(16, ally->y);
    for (int frame = 0; frame < 30 && state.specialMechanisms.isChronoMovementLocked(1, state.tick); ++frame) {
        (void)advanceRuntimePlayerCommands(state);
        if (!state.playerCommands.following.contains(1) || state.behaviors.contains(1)
            || state.playerCommands.following.at(1).lastTargetCell != RuntimeMultiplayerGameplayCell{15, 3})
            return fail("Chronosphere recovery lost or advanced the active Follow");
        tickChronoRecovery();
    }
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.following.contains(1)
        || state.playerCommands.following.at(1).lastTargetCell != RuntimeMultiplayerGameplayCell{16, 3})
        return fail("Follow did not resume after Chronosphere recovery");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop)
        || state.playerCommands.following.contains(1)) return fail("Stop did not cancel Follow");

    const auto* follower = findObjectById(state, 1);
    ally->x = follower->x + 1; ally->y = follower->y;
    ally->cell = runtimeCellKey(ally->x, ally->y);
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Move, {25, 2})
        || !dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow,
            runtimeCellPointForObject(*ally), ally)) return fail("nearby Follow move setup");
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.following.contains(1) || state.behaviors.contains(1))
        return fail("nearby Follow left the previous far move active");
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Attack,
            runtimeCellPointForObject(*findObjectById(state, 3)), findObjectById(state, 3)))
        return fail("nearby Follow attack setup");
    auto* firing = findMutableObjectById(state, 1);
    auto* enemy = findMutableObjectById(state, 3);
    const auto weapon = runtimeWeaponProfileForObject(state, *firing, enemy);
    if (!weapon.weapon || !fireRuntimeIndependentCombatWeapon(state, *firing, nullptr, *enemy, weapon)
        || !state.combatDetails.hasPendingBurst(1)) return fail("nearby Follow burst setup");
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow, runtimeCellPointForObject(*ally), ally))
        return fail("nearby Follow attack replacement rejected");
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.following.contains(1) || state.behaviors.contains(1)
        || state.combatDetails.hasPendingBurst(1))
        return fail("nearby Follow left the previous attack or weapon burst active");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop)) return fail("nearby Follow cleanup");
    state.playerLocalCommands.planning = true;
    if (!dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow, runtimeCellPointForObject(*ally), ally)
        || !appendMove({19, 2})) return fail("queued Follow tail setup");
    state.playerLocalCommands.planning = false;
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.following.contains(1) || !state.playerCommands.queues.contains(1)
        || state.playerCommands.queues.at(1).orders.size() != 1)
        return fail("activating queued Follow discarded its subsequent route");
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.at(1).orders.size() != 1)
        return fail("active Follow consumed its waiting route");
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop)
        || state.playerCommands.queues.contains(1) || state.playerCommands.following.contains(1))
        return fail("Stop did not clear queued Follow and its tail");

    const auto selectionBeforeMagazineQueue = state.selectedObjectIds;
    if (std::find(selectionBeforeMagazineQueue.begin(), selectionBeforeMagazineQueue.end(), 4)
        != selectionBeforeMagazineQueue.end()) return fail("queued actor must be outside the UI selection");
    RuntimeMultiplayerGameplayTargetOrderCommand magazineAttack;
    magazineAttack.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    magazineAttack.objectIds = {4};
    magazineAttack.target = RuntimeMultiplayerGameplayObjectTarget{3};
    RuntimeMultiplayerGameplayTargetOrderCommand afterBurstMove;
    afterBurstMove.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    afterBurstMove.objectIds = {4};
    afterBurstMove.target = RuntimeMultiplayerGameplayCellTarget{{9, 6}};
    afterBurstMove.append = true;
    if (!applyRuntimePlayerTargetOrder(state, "Player", magazineAttack)
        || !applyRuntimePlayerTargetOrder(state, "Player", afterBurstMove))
        return fail("paid burst route setup");
    auto* magazineUnit = findMutableObjectById(state, 4);
    (void)advanceRuntimeAttackBehavior(state, *magazineUnit, state.behaviors.at(4));
    if (magazineUnit->currentAmmo != 0 || !state.combatDetails.hasPendingBurst(4)
        || state.behaviors.at(4).state != RuntimeBehaviorState::Complete)
        return fail("real attack did not leave paid rounds after emptying its magazine");
    const auto countMagazineProjectiles = [&]() {
        return std::count_if(state.projectiles.begin(), state.projectiles.end(),
            [](const auto& projectile) { return projectile.attackerObjectId == 4; });
    };
    if (countMagazineProjectiles() != 1) return fail("magazine burst first shot count");
    for (int frame = 0; frame < 30 && state.combatDetails.hasPendingBurst(4); ++frame) {
        (void)advanceRuntimePlayerCommands(state);
        if (!state.playerCommands.queues.contains(4)
            || state.behaviors.at(4).kind != RuntimeBehaviorKind::Attack)
            return fail("queued move interrupted the paid burst after magazine depletion");
        ++state.tick;
        (void)advanceRuntimeCombatBursts(state);
    }
    if (state.combatDetails.hasPendingBurst(4) || countMagazineProjectiles() != 3
        || findObjectById(state, 4)->currentAmmo != 0)
        return fail("paid burst did not complete all three rounds without extra ammo");
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.contains(4) || state.behaviors.at(4).kind != RuntimeBehaviorKind::Move
        || state.behaviors.at(4).moveTarget != RuntimeCellPoint{9, 6})
        return fail("queued move did not start after the paid burst finished");
    if (state.selectedObjectIds != selectionBeforeMagazineQueue)
        return fail("queued command for an unselected actor changed the UI selection");
    RuntimeMultiplayerGameplayCommand magazineStop;
    magazineStop.body = RuntimeMultiplayerGameplayStopCommand{{4}};
    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", magazineStop))
        return fail("paid burst fixture cleanup");

    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Unit; item.typeId = "PCUNIT"; item.requiredFactory = "UnitType";
    item.cost = 100; item.techLevel = 1; item.techTreeAllowed = true;
    if (!applyRuntimePlayerPrimaryFactory(state, "Player", 90)) return fail("primary factory rejected");
    auto candidates = findRuntimeFactoryAnchorCandidatesForOwner(state, item, "Player", false);
    if (candidates.size() != 2 || candidates.front()->instanceId != 90)
        return fail("primary factory is not first among compatible factories");
    RuntimeFactoryDeliveryAcquireRequest delivery;
    delivery.lease.canonicalOwner = runtimeCanonicalHumanHouseOwner("Player");
    delivery.lease.hostId = 90; delivery.lease.producedObjectId = 1;
    delivery.lease.orderSerial = 1; delivery.lease.acquiredTick = state.tick;
    delivery.lease.rallyCell = {22, 10, false};
    if (!state.factoryDeliveries.acquire(delivery).acquired) return fail("factory door lease setup");
    const auto fallback = findRuntimeProductionSpawnForOwner(state, item, "Player", false);
    if (!fallback || !fallback->anchor || fallback->anchor->instanceId != 10)
        return fail("busy primary factory did not fall back to the other producer");
    if (!applyRuntimePlayerRallyPoint(state, "Player", 90, {25, 12})) return fail("factory rally rejected");
    auto* produced = findMutableObjectById(state, 1);
    produced->x = 21; produced->y = 10; produced->cell = runtimeCellKey(21, 10);
    clearRuntimeRenderPosition(*produced);
    queueRuntimeFactoryDeliveryExitMove(state, *produced, delivery.lease);
    queueRuntimePlayerFactoryRally(state, 90, 1);
    (void)advanceRuntimePlayerCommands(state);
    if (!state.playerCommands.queues.contains(1)
        || state.behaviors.at(1).moveTarget != RuntimeCellPoint{22, 10})
        return fail("player rally replaced the active factory exit");
    bool exitComplete = false;
    for (int frame = 0; frame < 300; ++frame) {
        (void)advanceRuntimePlayerCommands(state);
        driveMovement();
        const auto* actor = findObjectById(state, 1);
        if (actor && actor->x == 22 && actor->y == 10
            && state.behaviors.at(1).state == RuntimeBehaviorState::Complete) {
            exitComplete = true; break;
        }
    }
    if (!exitComplete || !state.playerCommands.queues.contains(1))
        return fail("rally did not remain queued until factory egress completion");
    (void)state.factoryDeliveries.releaseByObject(1);
    (void)advanceRuntimePlayerCommands(state);
    if (state.playerCommands.queues.contains(1)
        || state.behaviors.at(1).moveTarget != RuntimeCellPoint{25, 12})
        return fail("rally was not dispatched after factory door release");

    if (!applyRuntimeMultiplayerGameplayCommand(state, "Player", stop))
        return fail("transaction route cleanup");
    // Keep the ordinary unit's provisional fire-activity record: the real
    // simulation tick must clean it before dispatching the grinder order.
    state.specialMechanisms.notifyActivity(1, state.tick, "fire");
    if (!state.specialMechanisms.camouflageStates().contains(1))
        return fail("grinder activity ordering fixture did not retain its record");
    RuntimeMultiplayerGameplayTargetOrderCommand enterGrinder;
    enterGrinder.order = RuntimeMultiplayerGameplayTargetOrderKind::Primary;
    enterGrinder.objectIds = {1};
    enterGrinder.target = RuntimeMultiplayerGameplayObjectTarget{100};
    enterGrinder.append = true;
    RuntimeMultiplayerGameplayTargetOrderCommand otherActorMove;
    otherActorMove.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    otherActorMove.objectIds = {2};
    otherActorMove.target = RuntimeMultiplayerGameplayCellTarget{{20, 3}};
    otherActorMove.append = true;
    if (!applyRuntimePlayerTargetOrder(state, "Player", enterGrinder)
        || !applyRuntimePlayerTargetOrder(state, "Player", otherActorMove))
        return fail("transaction route setup");
    // The first Primary executes the real grinding scratch-state commit,
    // replacing the entire command map while the second actor is still due.
    const int grindingQueuedTick = ++state.tick;
    (void)advanceRuntimeSimulation(state, false);
    const auto* queuedGrinding = runtimeGrindingRelationForSource(state.grinding, 1);
    if (queuedGrinding == nullptr) {
        std::ostringstream diagnostic;
        diagnostic << "queued Primary did not commit the grinding transaction"
            << " tick=" << state.tick
            << " sourceQueue=" << state.playerCommands.queues.contains(1)
            << " otherQueue=" << state.playerCommands.queues.contains(2)
            << " camouflage=" << state.specialMechanisms.camouflageStates().contains(1)
            << " follow=" << state.playerCommands.following.contains(1)
            << " burst=" << state.combatDetails.hasPendingBurst(1)
            << " egress=" << runtimeFactoryDeliveryOwnsEgress(state, 1)
            << " grindingConflict=" << runtimeObjectConflictsWithGrinding(state, 1);
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, 1);
        const auto* host = findRuntimeObjectByIdIncludingInactive(state, 100);
        if (source != nullptr) {
            diagnostic << " source=" << source->type << '/' << source->owner << '/' << source->mission
                << " alive=" << source->alive << " health=" << source->health
                << " cell=" << source->x << ',' << source->y
                << " deployed=" << runtimeObjectHasDeployedState(state, *source)
                << " locked=" << runtimePlayerActorTemporarilyCommandLocked(state, *source);
        } else diagnostic << " source=missing";
        if (host != nullptr) {
            diagnostic << " host=" << host->type << '/' << host->owner << '/' << host->mission
                << " alive=" << host->alive << " power=" << host->poweredOperational;
        } else diagnostic << " host=missing";
        for (const int actor : {1, 2}) {
            const auto behavior = state.behaviors.find(actor);
            diagnostic << " behavior" << actor << '=';
            if (behavior == state.behaviors.end()) diagnostic << "none";
            else diagnostic << static_cast<int>(behavior->second.kind) << '/'
                << static_cast<int>(behavior->second.state) << "/target="
                << behavior->second.moveTarget.x << ',' << behavior->second.moveTarget.y;
        }
        auto candidate = state.grinding;
        const auto context = makeRuntimeGrindingContext(state, &candidate, false);
        const auto preflight = candidate.queueEntry(context, 100, 1, state.tick);
        diagnostic << " pairAccepted=" << preflight.accepted << " pairReason=" << preflight.reason;
        if (source != nullptr && host != nullptr)
            diagnostic << " entryPath=" << runtimeGrindingEntryCell(state, *source, *host).has_value();
        diagnostic << " lastCommand=" << state.lastCommand.verb
            << " valid=" << state.lastCommand.valid << " accepted=" << state.lastCommand.acceptedCount
            << " rejected=" << state.lastCommand.rejectedCount
            << " interaction=" << state.interactionStatus << " simulation=" << state.simulationStatus;
        return fail(diagnostic.str());
    }
    if (queuedGrinding->queuedTick != grindingQueuedTick || queuedGrinding->enteringTick != -1)
        return fail("grinding transaction lost the live queue tick");
    if (state.playerCommands.queues.contains(1) || state.playerCommands.queues.contains(2)
        || !state.behaviors.contains(2) || state.behaviors.at(2).kind != RuntimeBehaviorKind::Move
        || state.behaviors.at(2).moveTarget != RuntimeCellPoint{20, 3})
        return fail("grinding transaction invalidated the next actor's queued command");
    // Exercise the production entry commit at a later arrival boundary.
    state.tick += 5;
    auto* arriving = findMutableObjectById(state, 1);
    arriving->x = 28; arriving->y = 28; arriving->cell = runtimeCellKey(28, 28);
    clearRuntimeRenderPosition(*arriving);
    if (!completeRuntimeGrindingEntry(state, 1)) return fail("grinding entry commit rejected");
    const auto* enteredGrinding = runtimeGrindingRelationForSource(state.grinding, 1);
    if (enteredGrinding == nullptr || enteredGrinding->phase != RuntimeGrindingRelationPhase::Entering
        || enteredGrinding->queuedTick != grindingQueuedTick || enteredGrinding->enteringTick != state.tick)
        return fail("grinding entry transaction lost the live arrival tick");
    error.clear();
    return true;
}
