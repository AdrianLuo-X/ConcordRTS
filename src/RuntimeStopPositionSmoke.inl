bool runRuntimeStopPositionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Stop position: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original rules unavailable");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordStopPositionSmoke";
    WNDCLASSW windowClass{}; windowClass.lpfnWndProc = runtimeWindowProc;
    windowClass.hInstance = instance; windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return fail("input window class");
    HWND window = CreateWindowExW(0, className, L"Stop position check", WS_POPUP,
        0, 0, 1024, 768, nullptr, nullptr, instance, nullptr);
    struct Cleanup {
        HWND window; HINSTANCE instance; const wchar_t* name;
        ~Cleanup() {
            if (window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); DestroyWindow(window); }
            UnregisterClassW(name, instance);
        }
    } cleanup{window, instance, className};
    if (!window) return fail("input window");
    const auto pose = [](const RuntimeWindowState& s, int id) {
        const auto* p = findObjectById(s, id);
        const auto offset = worldObjectSubCellOffset(*p);
        return std::array<double, 3>{(p->hasRenderPosition ? p->renderX : double(p->x)) + offset.x,
            (p->hasRenderPosition ? p->renderY : double(p->y)) + offset.y,
            p->hasRenderPosition ? p->renderLevel : double(p->level)};
    };
    const auto same = [](const auto& a, const auto& b) {
        for (int i = 0; i < 3; ++i) if (std::abs(a[i] - b[i]) > 1e-9) return false;
        return true;
    };
    const auto step = [](RuntimeWindowState& s) {
        ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    int cases = 0, saves = 0, resumes = 0, snapshots = 0, subcellChanges = 0;
    std::vector<std::string> failures;
    for (const char* type : {"MTNK", "E1", "DEST", "DLPH", "JUMPJET", "ORCA"})
        for (int hz : {30, 60}) for (bool routed : {false, true})
        for (bool diagonal : {false, true}) for (int phase : {0, 1, 2}) {
        const std::string scene = std::string(type) + " hz=" + std::to_string(hz)
            + " routed=" + std::to_string(routed) + " diagonal=" + std::to_string(diagonal)
            + " phase=" + std::to_string(phase);
        const auto bad = [&](const std::string& why) { return fail(scene + ": " + why); };
        const auto checkScene = [&]() -> bool {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.computerAiEnabled = false;
        s.config.noCooldown = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater;
        s.report = assets->report; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.economy.owner = "Americans"; s.economy.credits = 10000;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false; s.visionEnabled = false;
        RuntimeHumanHouseAuthority authority; authority.owner = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 1; y <= 44; ++y) for (int x = 1; x <= 44; ++x) {
            TerrainCell terrain; terrain.x = x; terrain.y = y;
            terrain.cellIndex512 = runtimeCellKey(x, y); terrain.tileIndex = 0;
            s.world.accessTerrainCells().push_back(terrain);
        }
        auto* mover = addRuntimeObjectInstance(s, runtimeBuildKindForUnitTypeId(s.rules, type), type, {8, 12});
        if (!mover) return bad("actor creation");
        const int id = mover->instanceId;
        const auto* unit = findRuntimeUnitType(s.rules, *mover);
        if (!unit) return bad("unit rules");
        const bool flying = runtimeUnitUsesJumpjetMovement(*unit) || runtimeFixedWingProfile(s, *mover) != nullptr;
        const bool chase = !flying && !unit->naval && phase == 2;
        const RuntimeCellPoint destination{32, diagonal ? 36 : 12};
        int enemyId = -1;
        if (chase) {
            auto* enemy = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "NAPOWR", destination);
            if (!enemy) return bad("attack target creation");
            enemyId = enemy->instanceId; enemy->owner = "Russians";
            mover = findMutableObjectById(s, id);
        }
        mover->facing = runtimeFacingFromDelta(1, diagonal ? 1 : 0);
        if (flying) {
            mover->airborne = mover->hasRenderPosition = true;
            mover->renderX = mover->x; mover->renderY = mover->y;
            mover->renderLevel = runtimeUnitUsesJumpjetMovement(*unit) ? unit->jumpJetHeight / 256.0 : 6.0;
            mover->level = mover->flightLevel = static_cast<int>(std::ceil(mover->renderLevel));
        }
        initializeRuntimeNavigation(s);
        const auto restoreTerrainMedium = [&]() {
            for (auto& [key, info] : s.terrainInfoByCell) {
                (void)key; info.movementLandType = info.authoredLandType = unit->naval
                    ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
                info.water = unit->naval; info.shore = info.bridge = false;
                info.passable = true; info.cost = 10;
            }
        };
        restoreTerrainMedium();
        RuntimeSnapshotActiveStateScope activeState(s);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        struct Detach { HWND window; ~Detach() { SetWindowLongPtrW(window, GWLP_USERDATA, 0); } } detach{window};
        if (routed) initializeRuntimeOfflineGameplayRouting(s);
        s.selectedObjectIds = {id};
        if (!dispatchRuntimeTargetOrderFromUi(s, chase ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
                : RuntimeMultiplayerGameplayTargetOrderKind::Move, destination,
                chase ? findObjectById(s, enemyId) : nullptr))
            return bad("move/attack chase rejected: " + s.interactionStatus);
        // Use a legal cardinal/diagonal route with the real movement executor.
        if (!flying && !chase) {
            std::vector<RuntimeCellPoint> path;
            for (int n = 1; n <= 24; ++n) path.push_back({8 + n, 12 + (diagonal ? n : 0)});
            replaceRuntimeBehaviorPath(s.behaviors.at(id), path);
            s.behaviors.at(id).pathRequestPending = false;
        }
        bool located = false;
        for (int n = 0; n < hz * 10; ++n) {
            step(s); mover = findMutableObjectById(s, id);
            const auto behavior = s.behaviors.find(id);
            if (!mover || behavior == s.behaviors.end() || !mover->renderMoveActive) continue;
            if (flying) { if (n >= 3 + phase * 4) { located = true; break; } }
            else if (behavior->second.renderMoveTicksTotal > 0) {
                const auto& b = behavior->second;
                const double progress = 1.0 - double(b.renderMoveTicksRemaining) / b.renderMoveTicksTotal;
                if (progress >= (phase == 0 ? 0.0 : phase == 1 ? 0.25 : 0.75)) {
                    located = true; break;
                }
            }
        }
        if (!located) return bad("did not reach the requested part of a real movement segment");
        // Preserve the fractional height of a surface segment too. This is
        // the same interpolated pose used while crossing a slope/bridge edge.
        if (!flying && !unit->naval && diagonal && phase == 1) {
            auto& b = s.behaviors.at(id);
            b.renderFromLevel = 0.0; b.renderToLevel = 2.0;
            mover->level = 2; mover->renderLevel = 0.625;
        }
        const auto before = pose(s, id);
        const auto stays = [&](const auto& current) {
            return flying ? std::abs(before[0] - current[0]) < 1e-9
                && std::abs(before[1] - current[1]) < 1e-9 : same(before, current);
        };
        const RuntimeCellPoint occupied{mover->x, mover->y};
        const auto stop = [&]() { SendMessageW(window, WM_KEYDOWN, 'S', 0); SendMessageW(window, WM_KEYUP, 'S', 0); };
        stop(); mover = findMutableObjectById(s, id);
        if (!s.lastCommand.valid || s.lastCommand.verb != "stop" || s.lastCommand.acceptedCount != 1)
            return bad("S was not accepted by the actual window input path");
        if (!same(before, pose(s, id))) return bad("S moved the continuous XYZ pose");
        if (RuntimeCellPoint{mover->x, mover->y} != occupied || mover->renderMoveActive
            || s.behaviors.contains(id) || s.playerCommands.queues.contains(id))
            return bad("Stop left movement active or changed committed occupancy");
        stop();
        int retaliationTarget = -1;
        if ((std::string(type) == "JUMPJET" || std::string(type) == "MTNK") && phase == 1) {
            auto* enemy = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK",
                {occupied.x + 2, occupied.y});
            if (!enemy) return bad("nearby retaliation target creation");
            retaliationTarget = enemy->instanceId; enemy->owner = "Russians";
        }
        bool retaliated = false;
        for (int n = 0; n < (retaliationTarget >= 0 ? hz * 2 : 12); ++n) {
            step(s); if (!stays(pose(s, id))) return bad("stopped unit drifted, including automatic in-range retaliation");
            const auto behavior = s.behaviors.find(id);
            retaliated = retaliated || (behavior != s.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Attack);
        }
        if (retaliationTarget >= 0) {
            if (!retaliated) return bad("nearby enemy never exercised automatic retaliation");
            // Manual fixture removal must also end the enemy's retaliation.
            // Saved behavior records intentionally reject dead actors.
            cancelRuntimeCombatFire(s, retaliationTarget);
            eraseRuntimeBehavior(s, retaliationTarget);
            s.playerCommands.cancel({retaliationTarget});
            auto* enemy = findMutableObjectById(s, retaliationTarget);
            enemy->alive = false; enemy->health = 0;
            stop();
        }
        const auto resting = pose(s, id);
        initializeRuntimeNavigation(s);
        if (!same(resting, pose(s, id))) return bad("navigation rebuild moved stopped unit");
        restoreTerrainMedium();
        refreshRuntimeBridgeMutationState(s, {{40, 40}});
        if (!same(resting, pose(s, id))) return bad("a distant bridge mutation moved stopped XYZ");
        // Ordinary save/load must retain stopped fractional positions too.
        if (phase == 1) {
            std::ostringstream saved; IniFile ini;
            if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str()))
                return bad("save: " + error);
            auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
            if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)) {
                const std::string failedSave = "build/issue21_04_stop_restore_failed.ini";
                std::ofstream diagnostic(failedSave, std::ios::binary | std::ios::trunc);
                diagnostic << saved.str();
                std::cerr << "Stop restore fixture: " << scene << " saved=" << failedSave << '\n';
                return bad("restore: " + error);
            }
            initializeRuntimeNavigation(*restored); completeRuntimeWorldRefresh(*restored);
            if (!same(resting, pose(*restored, id))) return bad("save lost stopped XYZ");
            for (int n = 0; n < 3; ++n) { step(*restored); if (!stays(pose(*restored, id))) return bad("restored unit drifted"); }
            ++saves;
        }
        // Compare the exact object-pose network extension for a stopped tank.
        if (std::string(type) == "MTNK" && phase == 1 && !diagonal && routed) {
            std::ostringstream snapshot; IniFile ini;
            if (!writeRuntimeStateToStream(s, snapshot, error)
                || !writeRuntimeMultiplayerSnapshotExtension(s, snapshot, error)
                || !ini.loadFromString(snapshot.str())) return bad("snapshot write: " + error);
            auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
            if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)) return bad("snapshot restore: " + error);
            initializeRuntimeNavigation(*restored); completeRuntimeWorldRefresh(*restored);
            if (!materializeRuntimeMultiplayerSnapshotExtension(*restored, ini, error)
                || !same(resting, pose(*restored, id))) return bad("snapshot lost stopped XYZ: " + error);
            ++snapshots;
        }
        const int oldSubcell = findObjectById(s, id)->subCell;
        if (std::string(type) == "E1") {
            const auto* walker = findObjectById(s, id);
            auto* resident = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "E1",
                {walker->x + 1, walker->y + (diagonal ? 1 : 0)});
            if (!resident) return bad("subcell resident creation");
            resident->subCell = oldSubcell;
            initializeRuntimeNavigation(s); restoreTerrainMedium();
            if (!same(resting, pose(s, id))) return bad("adding an adjacent subcell resident moved stopped infantry");
        }
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, destination)
            || !same(resting, pose(s, id))) return bad("new move snapped stopped unit before its first tick");
        bool advanced = false;
        auto previous = resting;
        for (int n = 0; n < hz * 4; ++n) {
            step(s); const auto current = pose(s, id);
            if (std::hypot(current[0] - previous[0], current[1] - previous[1]) > 0.26)
                return bad("resume jumped more than a legal per-tick movement distance");
            advanced = advanced || std::hypot(current[0] - resting[0], current[1] - resting[1]) > 1e-9;
            previous = current;
            if (advanced) break;
        }
        if (!advanced) return bad("resume never advanced");
        if (std::string(type) == "E1" && findObjectById(s, id)->subCell != oldSubcell) ++subcellChanges;
        const auto resumedPose = pose(s, id);
        stop();
        if (!same(resumedPose, pose(s, id))) return bad("second Stop after resuming moved the body");
        for (int n = 0; n < 3; ++n) {
            step(s); const auto current = pose(s, id);
            if (flying ? std::hypot(current[0] - resumedPose[0], current[1] - resumedPose[1]) > 1e-9
                : !same(resumedPose, current)) return bad("second Stop after resuming drifted");
        }
        ++resumes; ++cases;
        std::cout << "Stop position: " << scene << " chase=" << chase << " S/freeze/idle/nav/save/resume=ok\n";
        return true;
        };
        try {
            if (!checkScene()) { failures.push_back(error); std::cerr << error << '\n'; }
        } catch (const std::exception& exception) {
            bad(std::string("scene exception: ") + exception.what());
            failures.push_back(error); std::cerr << error << '\n';
        }
    }
    std::cout << "Stop position: passed cases=" << cases << " saves=" << saves
        << " resumes=" << resumes << " snapshots=" << snapshots << " subcellChanges=" << subcellChanges << '\n';
    if (!failures.empty()) return fail("failed scenes=" + std::to_string(failures.size()) + "; " + failures.front());
    if (!subcellChanges) return fail("infantry resume never exercised a change of rendered subcell offset");
    return true;
}
