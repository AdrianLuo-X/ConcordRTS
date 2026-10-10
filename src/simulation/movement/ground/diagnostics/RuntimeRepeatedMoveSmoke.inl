bool runRuntimeRepeatedMoveSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) {
        error = "Repeated move continuity: " + why; return false;
    };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original resources unavailable");
    const char* output = std::getenv("CONCORD_REPEATED_MOVE_DIR");
    const std::filesystem::path root = output && *output ? output
        : "build/smoke/feature-set-20/01-repeated-move/checks";
    std::filesystem::create_directories(root);
    std::ofstream csv(root / "click-continuity.csv");
    csv << "type,hz,slider,retarget,clicks,activeClicks,maxClickDisplacement,travel,arrived\n";
    int cases = 0, clicks = 0, activeClicks = 0;
    for (const char* type : {"E1", "E2", "INIT", "TANY", "YURI", "ENGINEER", "MTNK", "FV", "DLPH"})
    for (const int hz : {15, 30, 60}) for (int slider = 0; slider <= 6; ++slider)
    for (const bool retarget : {false, true}) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.gameSpeed = slider;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater; s.report = assets->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        initializeRuntimeLocalProductionLanes(s.economy);
        RuntimeHumanHouseAuthority authority; authority.owner = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* mover = addRuntimeObjectInstance(s, runtimeBuildKindForUnitTypeId(s.rules, type), type, {5, 12});
        if (!mover) return fail(std::string(type) + " creation");
        const int id = mover->instanceId;
        mover->facing = runtimeFacingFromDelta(1, 0); mover->harvestAutoSearchSuspended = true;
        initializeRuntimeNavigation(s);
        const bool naval = runtimeObjectMovementMode(s, *mover) == PathMovementMode::Naval;
        for (auto& [key, info] : s.terrainInfoByCell) {
            (void)key; info.movementLandType = info.authoredLandType = naval
                ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
            info.water = naval; info.shore = info.bridge = false; info.passable = true; info.cost = 10;
        }
        RuntimeSnapshotActiveStateScope activeState(s);
        applySelection(s, {id}, false, false);
        int localClicks = 0, localActive = 0; double travel = 0.0, maximumJump = 0.0;
        const auto click = [&](RuntimeCellPoint goal) {
            mover = findMutableObjectById(s, id);
            if (!mover) return false;
            const auto before = *mover;
            const double beforeX = runtimeObjectRenderX(before), beforeY = runtimeObjectRenderY(before);
            const auto behavior = s.behaviors.find(id);
            const bool active = behavior != s.behaviors.end() && behavior->second.moveCooldownTicks > 0;
            issueRuntimeMoveCommand(s, goal);
            ++localClicks; if (active) ++localActive;
            mover = findMutableObjectById(s, id);
            const double jump = std::hypot(runtimeObjectRenderX(*mover) - beforeX,
                runtimeObjectRenderY(*mover) - beforeY);
            maximumJump = std::max(maximumJump, jump);
            // A mouse command advances no simulation time. The rendered body,
            // authoritative cell, facing and walking phase must stay exactly put.
            return s.lastCommand.acceptedCount == 1 && jump < 1e-9
                && mover->x == before.x && mover->y == before.y && mover->subCell == before.subCell
                && mover->facing == before.facing
                && mover->movementAnimationDistance == before.movementAnimationDistance;
        };
        for (int n = 0; n < hz * 4; ++n) {
            const RuntimeCellPoint goal{18, retarget && (n / 7) % 2 ? 16 : 12};
            // Multiple clicks before a simulation tick also exercise paused
            // command processing; no click can finish a movement segment.
            for (int burst = 0; burst < 3; ++burst) if (!click(goal)) {
                csv << type << ',' << hz << ',' << slider << ',' << retarget << ',' << localClicks
                    << ',' << localActive << ',' << maximumJump << ',' << travel << ",0\n";
                return fail(std::string(type) + " hz=" + std::to_string(hz)
                    + " slider=" + std::to_string(slider) + " tick=" + std::to_string(n)
                    + " click displaced body=" + std::to_string(maximumJump));
            }
            const double x = runtimeObjectRenderX(*mover), y = runtimeObjectRenderY(*mover);
            ++s.tick; advanceRuntimeSimulation(s); mover = findMutableObjectById(s, id);
            if (!mover || !mover->alive) return fail(std::string(type) + " mover disappeared");
            const double delta = std::hypot(runtimeObjectRenderX(*mover) - x, runtimeObjectRenderY(*mover) - y);
            // Even the fastest fixture travels far less than one cell/tick.
            if (delta > 0.5) return fail(std::string(type) + " movement tick jumped=" + std::to_string(delta));
            travel += delta;
        }
        if (!click({18, 12})) return fail(std::string(type) + " final retarget displaced body");
        bool arrived = false;
        for (int n = 0; n < hz * 180 && !arrived; ++n) {
            ++s.tick; advanceRuntimeSimulation(s); mover = findMutableObjectById(s, id);
            if (!mover || !mover->alive) return fail(std::string(type) + " arrival mover disappeared");
            arrived = mover->x == 18 && mover->y == 12 && !mover->renderMoveActive;
        }
        if (!arrived || localActive == 0 || travel < 0.1) return fail(std::string(type)
            + " movement/arrival stalled active=" + std::to_string(localActive)
            + " travel=" + std::to_string(travel) + " cell=" + std::to_string(mover->x)
            + "," + std::to_string(mover->y));
        csv << type << ',' << hz << ',' << slider << ',' << retarget << ',' << localClicks << ','
            << localActive << ',' << maximumJump << ',' << travel << ',' << arrived << '\n';
        ++cases; clicks += localClicks; activeClicks += localActive;
    }
    std::cout << "Repeated move continuity: passed cases=" << cases << " clicks=" << clicks
        << " activeClicks=" << activeClicks << " hz=15/30/60 sliders=0..6 zero click displacement\n";
    return true;
}
