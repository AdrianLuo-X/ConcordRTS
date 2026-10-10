bool runRuntimeMoveToShroudSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    using Visibility = RuntimeCellVisibility;
    using Order = RuntimeMultiplayerGameplayTargetOrderKind;
    const auto fail = [&](const std::string& reason) { error = "MoveToShroud: " + reason; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    for (const char* id : {"CLEG", "CCOMAND", "CIVAN"}) {
        const auto* unit = stock->rules.findUnitType("InfantryType", id);
        if (!unit || !unit->teleporter || unit->moveToShroud) return fail(std::string("original rule: ") + id);
    }
    const std::string root = "build/smoke/feature-checks/05-move-to-shroud/checks";
    std::filesystem::create_directories(root);
    constexpr RuntimeCellPoint source{12,12}, destination{32,12};
    struct Fixture { std::unique_ptr<RuntimeWindowState> box; int actor; };
    const auto make = [&](const char* type, int hz, int visibility) {
        Fixture f; f.box = std::make_unique<RuntimeWindowState>(); auto& s = *f.box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = false; s.config.noCooldown = false;
        s.config.multiplayerActive = false; s.config.shortGame = false; s.config.superweapons = false;
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = true;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        RuntimeHumanHouseAuthority authority; authority.owner = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 1; y <= 40; ++y) for (int x = 1; x <= 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        f.actor = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, type, source)->instanceId;
        if (visibility >= 3) {
            addRuntimeObjectInstanceForOwner(s, "Russians", RuntimeBuildKind::Building, "GAGAP", {32,15});
            addRuntimeObjectInstanceForOwner(s, "Russians", RuntimeBuildKind::Building, "NAPOWR", {32,22});
        }
        if (visibility == 2 || visibility == 4)
            addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "E1", {32,10});
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s); updateRuntimeVisionState(s);
        if (visibility == 1 || visibility == 3) {
            s.vision.exploreArea("Americans", destination.x, destination.y, 2);
            updateRuntimeVisionState(s);
        }
        return f;
    };
    const auto orderFor = [](int id, Order kind, RuntimeCellPoint target, bool append = false) {
        RuntimeMultiplayerGameplayTargetOrderCommand command;
        command.objectIds = {id}; command.order = kind; command.append = append;
        command.target = RuntimeMultiplayerGameplayCellTarget{{target.x,target.y}};
        return command;
    };
    const auto arrive = [&](Fixture& f) {
        auto& s = *f.box; ++s.tick; (void)advanceRuntimeSpecialMechanisms(s);
        const auto* actor = findObjectById(s, f.actor);
        return actor && runtimeCellPointForObject(*actor) == destination
            && !s.specialMechanisms.isChronoInTransit(f.actor);
    };
    const auto cursorFrame = [&](RuntimeWindowState& s) {
        const RECT client{0,0,1280,720};
        const RECT viewport = runtimeMapViewport(client);
        // This flat fixture replaces the campaign terrain without retaining
        // its playable bounds. Place the camera explicitly for pointer checks.
        const auto projected = runtimeProjectCellXY(destination.x, destination.y, 0, s.camera);
        s.camera.worldOffsetX = projected.x - (viewport.right - viewport.left) / 2;
        s.camera.worldOffsetY = projected.y - (viewport.bottom - viewport.top) / 2;
        s.camera.initialized = true; s.mouseClientPointValid = true;
        const POINT point = runtimeScreenPointForCell(s, viewport, destination, 0);
        const auto cursor = runtimeMouseCursorFrameForPoint(s, client, point);
        return cursor ? cursor->frameIndex : -1;
    };
    int cases = 0;
    for (const char* type : {"CLEG", "CCOMAND", "CIVAN"}) for (int hz : {30,60})
        for (int condition = 0; condition < 5; ++condition) {
        auto f = make(type, hz, condition); auto& s = *f.box;
        const bool allowed = condition == 1 || condition == 2 || condition == 4;
        const Visibility expected = condition == 1 ? Visibility::Fogged
            : allowed ? Visibility::Visible : Visibility::Shrouded;
        if (s.vision.visibilityFor("Americans", destination.x, destination.y) != expected)
            return fail(std::string(type) + " invalid visibility fixture " + std::to_string(condition));
        applySelection(s, {f.actor}, false, false);
        const int cursor = cursorFrame(s);
        if (cursor != (allowed ? 31 : 41)) return fail(std::string(type) + " cursor=" + std::to_string(cursor)
            + " visibility=" + std::to_string(condition) + " Hz=" + std::to_string(hz));
        issueRuntimeMoveCommand(s, destination);
        if ((s.lastCommand.acceptedCount == 1) != allowed
            || s.lastCommand.rejectedCount != (allowed ? 0 : 1)
            || runtimeCellPointForObject(*findObjectById(s, f.actor)) != source)
            return fail(std::string(type) + " command acceptance " + std::to_string(condition));
        if (allowed) {
            if (!s.specialMechanisms.isChronoInTransit(f.actor) || !arrive(f))
                return fail(std::string(type) + " permitted destination did not arrive");
        } else if (s.specialMechanisms.isChronoInTransit(f.actor) || !s.impactEffects.empty()
            || s.behaviors.contains(f.actor)) return fail("rejected order mutated movement or warp effects");
        ++cases;
    }
    // The flag is authored data. Ordinary infantry and an explicit override
    // can enter black terrain; a mixed group still moves its permitted members.
    {
        auto f = make("CLEG", 30, 0); auto& s = *f.box;
        const int ordinary = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "E1", {13,12})->instanceId;
        initializeRuntimeNavigation(s); updateRuntimeVisionState(s);
        applySelection(s, {f.actor,ordinary}, false, false);
        if (cursorFrame(s) != 31) return fail("mixed group cursor rejected permitted members");
        issueRuntimeMoveCommand(s, destination);
        if (s.lastCommand.acceptedCount != 1 || s.lastCommand.rejectedCount != 1
            || !s.behaviors.contains(ordinary) || s.behaviors.contains(f.actor)
            || s.specialMechanisms.isChronoInTransit(f.actor)) return fail("mixed ordinary/chrono movement");
        const_cast<UnitType*>(s.rules.findUnitType("InfantryType", "CLEG"))->moveToShroud = true;
        applySelection(s, {f.actor}, false, false); issueRuntimeMoveCommand(s, destination);
        if (s.lastCommand.acceptedCount != 1 || !arrive(f)) return fail("explicit permission was ignored");
        ++cases;
    }
    // Map orders use this UI dispatcher; authoritative peer orders
    // use the same target-order executor below.
    for (Order kind : {Order::Move,Order::Primary,Order::AttackMove,Order::GuardDestination}) {
        for (bool known : {false,true}) {
            auto f = make("CLEG", 30, known ? 1 : 0); auto& s = *f.box;
            applySelection(s, {f.actor}, false, false);
            if (dispatchRuntimeTargetOrderFromUi(s, kind, destination) != known)
                return fail("UI travel order acceptance");
            if (known ? !arrive(f) : s.specialMechanisms.isChronoInTransit(f.actor))
                return fail("UI travel order movement");
            ++cases;
        }
    }
    {
        auto f = make("CLEG", 30, 0); auto& s = *f.box;
        const auto old = orderFor(f.actor, Order::Move, {14,12}, true);
        if (!applyRuntimePlayerTargetOrder(s, "Americans", old)) return fail("existing queued order");
        const auto before = s.playerCommands.serialize();
        for (Order kind : {Order::Move,Order::Primary,Order::AttackMove,Order::GuardDestination}) {
            if (applyRuntimePlayerTargetOrder(s, "Americans", orderFor(f.actor, kind, destination))
                || s.playerCommands.serialize() != before) return fail("rejected replacement erased previous queue");
            if (applyRuntimePlayerTargetOrder(s, "Americans", orderFor(f.actor, kind, destination, true))
                || s.playerCommands.serialize() != before) return fail("rejected append changed existing queue");
        }
        if (!applyRuntimePlayerWaypointCommand(s, "Americans", {RuntimePlayerWaypointOperation::Begin,0}))
            return fail("begin waypoint plan");
        const auto plan = s.playerCommands.serialize();
        if (applyRuntimePlayerTargetOrder(s, "Americans", orderFor(f.actor, Order::Move, destination, true))
            || s.playerCommands.serialize() != plan) return fail("planning bypassed shroud restriction");
        const int ordinary = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "E1", {13,12})->instanceId;
        auto mixed = orderFor(f.actor, Order::Move, destination, true); mixed.objectIds.push_back(ordinary);
        if (!applyRuntimePlayerTargetOrder(s, "Americans", mixed)
            || s.playerCommands.waypoints.routes.contains(f.actor)
            || !s.playerCommands.waypoints.routes.contains(ordinary)) return fail("mixed waypoint group was not filtered");
        ++cases;
    }
    {
        auto f = make("CLEG", 30, 1); auto& s = *f.box;
        if (!applyRuntimePlayerTargetOrder(s, "Americans", orderFor(f.actor, Order::Move, destination, true)))
            return fail("queue explored destination");
        s.vision.resetShroudArea("Americans", destination.x, destination.y, 3);
        (void)advanceRuntimePlayerCommands(s);
        if (s.specialMechanisms.isChronoInTransit(f.actor) || s.behaviors.contains(f.actor)
            || runtimeCellPointForObject(*findObjectById(s, f.actor)) != source
            || s.playerCommands.queues.contains(f.actor)) return fail("queued order did not recheck its destination");
        ++cases;
    }
    {
        auto f = make("CLEG", 30, 0); auto& s = *f.box;
        const auto before = s.specialMechanisms.stateHash(); std::string reason;
        if (requestRuntimeChronoMoveWithParasite(s, f.actor, {destination.x,destination.y}, 0,
                [](int,int,int) { return true; }, [](int,int,int) { return false; }, reason)
            || s.specialMechanisms.stateHash() != before || !s.impactEffects.empty())
            return fail("raw request bypassed shroud or mutated state on rejection");
        RuntimeObjectBehavior behavior; behavior.kind = RuntimeBehaviorKind::Attack;
        const auto result = advanceRuntimeTeleporterMove(s, *findMutableObjectById(s, f.actor), behavior, destination);
        if (!result || *result || behavior.state != RuntimeBehaviorState::Blocked
            || s.specialMechanisms.isChronoInTransit(f.actor)) return fail("attack approach walked or blinked into shroud");
        ++cases;
    }
    // A committed departure remains a displacement. Retail reads the flag in
    // action selection, not again in TeleportLocomotionClass's arrival phase.
    {
        auto f = make("CLEG", 30, 1); auto& s = *f.box;
        applySelection(s, {f.actor}, false, false); issueRuntimeMoveCommand(s, destination);
        if (!s.specialMechanisms.isChronoInTransit(f.actor)) return fail("save fixture did not depart");
        const std::string path = root + "/departed.ini";
        if (!saveRuntimeState(s, path, error)) return fail("departure save: " + error);
        auto restored = make("CLEG", 30, 0);
        if (!loadRuntimeState(*restored.box, path, error)) return fail("departure load: " + error);
        if (!restored.box->specialMechanisms.isChronoInTransit(f.actor)
            || restored.box->vision.visibilityFor("Americans", destination.x, destination.y) != Visibility::Fogged)
            return fail("departure or exploration was lost on load");
        for (Fixture* candidate : {&f,&restored}) {
            candidate->box->vision.resetShroudArea("Americans", destination.x, destination.y, 3);
            if (!arrive(*candidate)) return fail("committed displacement was revoked after reshrouding");
        }
        ++cases;
    }
    {
        auto f = make("CLEG", 30, 1); auto& s = *f.box;
        const int remote = addRuntimeObjectInstanceForOwner(s, "Russians", RuntimeBuildKind::Infantry,
            "CLEG", {12,20})->instanceId;
        initializeRuntimeNavigation(s); updateRuntimeVisionState(s);
        RuntimeHumanHouseAuthority authority; authority.owner = "Russians"; authority.canonicalOwner = "russians";
        authority.remoteEconomy.emplace(); authority.remoteEconomy->owner = "Russians";
        authority.remoteTechTree.emplace(); s.humanHouseAuthorities.push_back(std::move(authority));
        s.config.multiplayerActive = true;
        RuntimeMultiplayerGameplayCommand command; command.body = orderFor(remote, Order::Move, destination);
        if (s.vision.visibilityFor("Russians", destination.x, destination.y) != Visibility::Shrouded
            || applyRuntimeMultiplayerGameplayCommand(s, "Russians", command)
            || s.specialMechanisms.isChronoInTransit(remote) || s.economy.owner != "Americans")
            return fail("peer movement borrowed local observer vision");
        s.vision.resetShroudArea("Americans", destination.x, destination.y, 3);
        s.vision.exploreArea("Russians", destination.x, destination.y, 3);
        if (!applyRuntimeMultiplayerGameplayCommand(s, "Russians", command)
            || !s.specialMechanisms.isChronoInTransit(remote) || s.economy.owner != "Americans")
            return fail("peer owner vision was not used independently");
        ++s.tick; (void)advanceRuntimeSpecialMechanisms(s);
        if (runtimeCellPointForObject(*findObjectById(s, remote)) != destination) return fail("peer did not arrive");
        ++cases;
    }
    {
        const std::string base = "[InfantryTypes]\n0=I\n[VehicleTypes]\n0=V\n[AircraftTypes]\n0=A\n";
        std::uint64_t defaultHash = 0;
        for (bool explicitValues : {false,true}) {
            const std::string ground = explicitValues ? "MoveToShroud=no\n" : "";
            const std::string air = explicitValues ? "MoveToShroud=yes\n" : "";
            IniFile ini; RulesDatabase rules;
            if (!ini.loadFromString(base + "[I]\nSpeed=1\n" + ground + "[V]\nSpeed=1\n" + ground
                + "[A]\nSpeed=1\n" + air) || !rules.loadFromIni(ini)) return fail("defaults INI");
            for (const auto& [kind,id] : {std::pair{"InfantryType","I"},std::pair{"UnitType","V"},
                    std::pair{"AircraftType","A"}}) {
                const auto* unit = rules.findUnitType(kind,id);
                if (!unit || unit->moveToShroud != ((std::string(id) != "A") != explicitValues))
                    return fail("ground/air default or explicit override");
            }
            if (!explicitValues) defaultHash = rules.getDetailRulesHash();
            else if (rules.getDetailRulesHash() == defaultHash) return fail("restriction absent from compatibility hash");
        }
        ++cases;
    }
    std::cout << "MoveToShroud passed: " << cases << " cases; three chrono infantry, 30/60 Hz, black/fog/visible/gap/pierced gap, "
        "ordinary and mixed groups, UI travel orders, queue/planning, raw and attack approach, departure save/reshroud, "
        "peer owner vision, defaults and compatibility hash\n";
    return true;
}
