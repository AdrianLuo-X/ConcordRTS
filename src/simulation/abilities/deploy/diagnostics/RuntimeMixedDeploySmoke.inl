// Issue 22/24: D enters through the real window procedure. Initial deployed
// actors are established by real individual D commands, never by flag edits.
// Ordinary14 saves authority; only the seeded canonical extension owns VFX.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeMixedDeploySmokeProbe(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_MIXED_DEPLOY_OUTPUT");
    const std::filesystem::path output = configured && *configured ? configured : "build/issue22_24_mixed_deploy";
    const char* selected = std::getenv("CONCORD_MIXED_DEPLOY_CASE");
    const std::string filter = selected ? selected : "";
    std::filesystem::create_directories(output);
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Mixed deploy " + label + " [" + stage + "]: " + why; return false;
    };
    const auto idText = [](const std::vector<int>& ids) {
        std::ostringstream text;
        for (std::size_t n = 0; n < ids.size(); ++n) { if (n) text << ','; text << ids[n]; }
        return text.str();
    };
    std::ofstream cases(output / "cases.csv"), commands(output / "commands.csv"),
        objects(output / "objects.csv"), checkpoints(output / "checkpoints.csv"), assetsCsv(output / "assets.csv");
    if (!cases || !commands || !objects || !checkpoints || !assetsCsv) return fail("evidence streams unavailable");
    cases << "case,hz,mode,selected,deployable,first_deployed,second_mobile,group_keys,wire_batches,ordinary14,canonical_extensions,paired_ticks,result\n";
    commands << "case,stage,route,press,tick,batch,action,ids_hex,wire_bytes,captured,applied,result\n";
    objects << std::setprecision(17) << "case,branch,stage,tick,id,type,owner,hp,alive,x,y,z,body_x,body_y,body_z,deployed,airborne,mission_hex,anim_remaining,anim_total,deploy_timer,weapon,weapon_slot,can_move,visual_type\n";
    checkpoints << "case,stage,format,tick,actors,file,result\n";
    assetsCsv << "type,strength,primary,secondary,deployer,simple_deployer,deploy_to_land,rules_path,detail_hash\n";
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config; assets->config.playerSlots.clear(); assets->config.computerAiEnabled = false;
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original assets: " + assets->report.errorMessage);
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) return fail("original rules");
    const auto detailHash = assets->rules.getDetailRulesHash();
    const auto loadOwnerRules = [&](RuntimeWindowState& s) {
        if (!s.rules.loadFromIni(stock) || s.rules.getDetailRulesHash() != detailHash) return false;
        s.rulesLoaded = true; return true;
    };
    const std::array<const char*, 5> types{{"E1", "GGI", "DESO", "SCHP", "MTNK"}};
    const std::array<int, 5> strengths{{125, 100, 150, 300, 300}};
    for (std::size_t n = 0; n < types.size(); ++n) {
        const auto* type = findRuntimeUnitTypeById(assets->rules, types[n]);
        if (!type || type->strength != strengths[n] || (n < 3 && (!type->deployer || !type->deployFire))
            || (n == 3 && (!type->isSimpleDeployer || !type->deployToLand || type->unloadingClass != "SCHD"))
            || (n == 4 && (type->deployer || type->isSimpleDeployer || !type->deploysInto.empty())))
            return fail("original stock deployment identity changed: " + std::string(types[n]));
        assetsCsv << type->id << ',' << type->strength << ',' << type->primaryWeapon << ',' << type->secondaryWeapon
            << ',' << type->deployer << ',' << type->isSimpleDeployer << ',' << type->deployToLand << ','
            << assets->report.rulesPath << ',' << detailHash << '\n';
    }
    int landTile = -1;
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (info.passable && !info.water && !info.shore && !info.bridge && !info.cliff) { landTile = tile; break; }
    }
    if (landTile < 0) return fail("original passable land TMP unavailable");
    struct Scene {
        std::unique_ptr<RuntimeWindowState> state;
        std::vector<int> capable, selection;
        std::map<int, WorldObjectInstance> initial;
        int ordinary = 0, canonical = 0, paired = 0;
    };
    const auto make = [&](int hz, bool large) {
        Scene scene; scene.state = std::make_unique<RuntimeWindowState>(); auto& s = *scene.state;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.playerSlots.clear(); s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.gameMode = "Battle";
        s.config.playerCountry = "Americans"; s.config.skirmishLoadSavePath.clear();
        s.report = assets->report; s.art = assets->art; s.theater = assets->theater;
        s.artLoaded = s.theaterLoaded = true;
        if (!loadOwnerRules(s)) { scene.state.reset(); return scene; }
        s.world = assets->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        for (int y = 64; y <= 134; ++y) for (int x = 64; x <= 134; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        const auto add = [&](const char* type, RuntimeBuildKind kind, RuntimeCellPoint cell, const char* owner) {
            auto* object = addRuntimeObjectInstanceForOwner(s, owner, kind, type, cell);
            if (!object) return -1;
            const int id = object->instanceId;
            object->mission = "Sleep"; // Authored fixture starts idle, with full original HP and ammo.
            return id;
        };
        if (large) {
            for (int n = 0; n < 300; ++n) {
                const int id = add("E1", RuntimeBuildKind::Infantry, {70 + n % 20, 70 + n / 20}, "Americans");
                if (id < 0) { scene.state.reset(); return scene; }
                scene.capable.push_back(id); scene.selection.push_back(id);
            }
        } else {
            const std::array<RuntimeCellPoint, 5> cells{{{75,75}, {81,75}, {99,99}, {75,85}, {82,85}}};
            for (std::size_t n = 0; n < types.size(); ++n) {
                const int id = add(types[n], n < 3 ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit,
                    cells[n], "Americans");
                if (id < 0) { scene.state.reset(); return scene; }
                scene.selection.push_back(id); if (n < 4) scene.capable.push_back(id);
            }
        }
        if (large) {
            const int tank = add("MTNK", RuntimeBuildKind::Unit, {93,87}, "Americans");
            if (tank < 0) { scene.state.reset(); return scene; } scene.selection.push_back(tank);
        }
        if (add("GAPOWR", RuntimeBuildKind::Building, {128,128}, "Russians") < 0) { scene.state.reset(); return scene; }
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        for (int id : scene.selection) if (const auto* object = findObjectById(s, id)) scene.initial.emplace(id, *object);
        return scene;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto record = [&](const Scene& scene, const RuntimeWindowState& s, const char* branch) {
        RuntimeMapRenderContext context; context.rules = &s.rules; context.art = &s.art;
        for (int id : scene.selection) if (const auto* o = findObjectById(s, id)) {
            const auto weapon = runtimeWeaponProfileForObject(s, *o);
            objects << label << ',' << branch << ',' << stage << ',' << s.tick << ',' << id << ',' << o->type
                << ',' << o->owner << ',' << o->health << ',' << o->alive << ',' << o->x << ',' << o->y << ','
                << o->level << ',' << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ','
                << runtimeObjectRenderLevel(*o) << ',' << o->deployed << ',' << o->airborne << ',' << iniHexEncode(o->mission)
                << ',' << o->buildAnimTicksRemaining << ',' << o->buildAnimTicksTotal << ',' << o->deployStateTicksRemaining
                << ',' << weapon.weaponId << ',' << runtimeWeaponSlotName(weapon.slot) << ',' << runtimeObjectCanMove(s,*o)
                << ',' << resolveRuntimeObjectVisualTypeId(*o, context) << '\n';
        }
    };
    const auto invariants = [&](const Scene& scene, const RuntimeWindowState& s) {
        for (const auto& [id, original] : scene.initial) {
            const auto* o = findObjectById(s, id);
            if (!o || !o->alive || o->health != original.health || o->type != original.type || o->owner != original.owner
                || o->x != original.x || o->y != original.y
                || std::abs(runtimeObjectRenderX(*o) - runtimeObjectRenderX(original)) > 1e-8
                || std::abs(runtimeObjectRenderY(*o) - runtimeObjectRenderY(original)) > 1e-8)
                return fail("original identity/HP/XY changed id=" + std::to_string(id));
            if (o->type == "MTNK" && (o->deployed || o->airborne || o->mission != original.mission
                || o->buildAnimTicksRemaining != original.buildAnimTicksRemaining))
                return fail("incapable MTNK changed by D");
        }
        return true;
    };
    const auto stable = [](const RuntimeWindowState& s, const std::vector<int>& ids, bool deployed) {
        return std::all_of(ids.begin(), ids.end(), [&](int id) {
            const auto* o = findObjectById(s, id);
            return o && o->alive && runtimeObjectHasDeployedState(s, *o) == deployed
                && !runtimeMissionIsDeploying(*o) && !runtimeMissionIsUndeploying(*o)
                && !runtimeMissionIsLandingToDeploy(*o) && !runtimeMissionIsTakingOffAfterUndeploy(*o)
                && o->buildAnimTicksRemaining == 0;
        });
    };
    const auto settle = [&](Scene& scene, const std::vector<int>& ids, bool deployed) {
        auto& s = *scene.state;
        for (int n = 0; n < s.config.fixedTickHz * 15; ++n) {
            if (stable(s, ids, deployed)) return invariants(scene, s);
            step(s); record(scene, s, "original"); if (!invariants(scene, s)) return false;
        }
        return fail("original deployment/landing animation did not reach the requested common state");
    };
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordMixedDeploySmoke";
    WNDCLASSW wc{}; wc.lpfnWndProc = runtimeWindowProc; wc.hInstance = instance; wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail("real input class");
    struct ClassCleanup { HINSTANCE instance; const wchar_t* name; ~ClassCleanup() { UnregisterClassW(name, instance); } } classCleanup{instance,className};
    HWND window = CreateWindowExW(0, className, L"Mixed deploy test", WS_POPUP, 0, 0, 1024, 768, nullptr, nullptr, instance, nullptr);
    if (!window) return fail("real input window");
    struct WindowCleanup { HWND window; ~WindowCleanup() {
        SetWindowLongPtrW(window,GWLP_USERDATA,0); SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if (GetCapture() == window) ReleaseCapture(); DestroyWindow(window);
    } } windowCleanup{window};
    const auto key = [&](RuntimeWindowState& s, const std::vector<int>& ids) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = ids;
        ensureRuntimeMenuKeyboardLoaded(s);
        if (runtimeMenuKeyboardBindingCode(s, "DeployObject", 'D') != 'D') return fail("fixture requires original D binding");
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        SendMessageW(window, WM_KEYDOWN, 'D', 0); SendMessageW(window, WM_KEYUP, 'D', 0);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); return true;
    };
    const auto same = [&](const Scene& scene, const RuntimeWindowState& a, const RuntimeWindowState& b) {
        if (a.tick != b.tick || a.rules.getDetailRulesHash() != b.rules.getDetailRulesHash()) return false;
        for (int id : scene.selection) {
            const auto* x = findObjectById(a,id); const auto* y = findObjectById(b,id);
            if (!x || !y || x->type != y->type || x->owner != y->owner || x->health != y->health || x->alive != y->alive
                || x->x != y->x || x->y != y->y || x->level != y->level || x->deployed != y->deployed
                || x->airborne != y->airborne || x->flightLevel != y->flightLevel || x->mission != y->mission
                || x->buildAnimTicksRemaining != y->buildAnimTicksRemaining || x->buildAnimTicksTotal != y->buildAnimTicksTotal
                || x->deployStateTicksRemaining != y->deployStateTicksRemaining || x->facing != y->facing || x->turretFacing != y->turretFacing
                || x->weaponCooldownTicksRemaining != y->weaponCooldownTicksRemaining
                || std::abs(runtimeObjectRenderX(*x)-runtimeObjectRenderX(*y)) > 1e-8
                || std::abs(runtimeObjectRenderY(*x)-runtimeObjectRenderY(*y)) > 1e-8
                || std::abs(runtimeObjectRenderLevel(*x)-runtimeObjectRenderLevel(*y)) > 1e-8) return false;
        }
        return true;
    };
    int successful = 0, groupKeys = 0, batches = 0, ordinarySaves = 0, canonicalSaves = 0, pairedTicks = 0, screenshots = 0;
    std::vector<std::string> failures;
    const auto save = [&](Scene& scene) {
        auto& source = *scene.state; std::ostringstream normalText, extensionText, canonical;
        IniFile normalIni, canonicalIni;
        if (!writeRuntimeStateToStream(source,normalText,error) || !writeRuntimeStateToStream(source,extensionText,error)
            || !writeRuntimeMultiplayerSnapshotExtension(source,extensionText,error)
            || !normalIni.loadFromString(normalText.str()) || !canonicalIni.loadFromString(extensionText.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(canonicalIni,canonical,error)
            || !canonicalIni.loadFromString(canonical.str())) return fail("checkpoint write: " + error);
        const auto restore = [&](bool exact) {
            auto q = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*q,source,false);
            if (!loadOwnerRules(*q)) return std::unique_ptr<RuntimeWindowState>{};
            q->impactEffects.clear(); q->projectiles.clear(); q->recentExpiredProjectiles.clear();
            if (!materializeRuntimeLoadStateFromIni(*q,normalIni,error)) return std::unique_ptr<RuntimeWindowState>{};
            if (exact) {
                q->impactEffects.clear(); q->projectiles.clear(); q->recentExpiredProjectiles.clear();
                if (!materializeRuntimeMultiplayerSnapshotExtension(*q,canonicalIni,error)) return std::unique_ptr<RuntimeWindowState>{};
            }
            return q;
        };
        auto ordinary = restore(false); auto exact = restore(true); auto reference = restore(true);
        if (!ordinary || !exact || !reference) return fail("checkpoint restore: " + error);
        if (!same(scene,source,*ordinary) || !same(scene,source,*exact)) return fail("save changed body/identity/deploy animation/clock");
        for (int format = 0; format < 2; ++format) {
            const std::string file = label + '-' + std::to_string(source.tick) + '-' + std::to_string(scene.ordinary)
                + (format ? "-canonical-extension.ini" : "-ordinary14.ini");
            if (std::filesystem::exists(output/file)) return fail("checkpoint artifact already exists");
            std::ofstream stream(output/file,std::ios::binary); stream << (format ? canonical.str() : normalText.str());
            if (!stream.good()) return fail("checkpoint artifact write");
            checkpoints << label << ',' << stage << ',' << (format ? "canonical-extension" : "ordinary14")
                << ',' << source.tick << ',' << scene.selection.size() << ',' << file << ",PASS\n";
        }
        ++scene.ordinary; ++scene.canonical; ++ordinarySaves; ++canonicalSaves;
        for (int n = 0; n < source.config.fixedTickHz * 2; ++n) {
            step(*ordinary); step(*exact); step(*reference); ++scene.paired; ++pairedTicks;
            if (!same(scene,*reference,*ordinary) || !same(scene,*reference,*exact)) return fail("restored animation/body/HP clocks diverged");
            if (!invariants(scene,*ordinary) || !invariants(scene,*exact)) return false;
            record(scene,*ordinary,"ordinary14"); record(scene,*exact,"canonical-extension"); record(scene,*reference,"reference");
        }
        return true;
    };
    const auto capture = [&](Scene& scene, const char* suffix) {
        const auto stats = drawRuntimeStateOffscreenAtCell(*scene.state,{86,86},1280,720,error,
            pathString(output/(label+'-'+suffix+".bmp")),false);
        if (!stats || !stats->rendered || stats->missingVoxelProxies != 0) return fail("original native drawing: " + error);
        ++screenshots; return true;
    };
    const auto checkEquipment = [&](Scene& scene, bool deployed) {
        auto& s = *scene.state; RuntimeMapRenderContext context; context.rules=&s.rules; context.art=&s.art;
        for (int id : scene.capable) {
            const auto* o = findObjectById(s,id); const auto* unit = o ? findRuntimeUnitType(s.rules,*o) : nullptr;
            const auto profile = o ? runtimeWeaponProfileForObject(s,*o) : RuntimeWeaponProfile{};
            if (!o || !unit || profile.weaponId != (deployed ? unit->secondaryWeapon : unit->primaryWeapon)
                || runtimeObjectCanMove(s,*o) == deployed || (o->type == "SCHP"
                    && (o->airborne == deployed || resolveRuntimeObjectVisualTypeId(*o,context) != (deployed ? "SCHD" : "SCHP"))))
                return fail("original weapon/movement/visual role did not follow common state id=" + std::to_string(id));
        }
        return invariants(scene,s);
    };
    const auto finish = [&](bool passed) {
        if (passed) { ++successful; std::cout << "Mixed deploy " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    const std::array<const char*,4> modes{{"all-mobile","all-deployed","mixed-forward","mixed-reverse"}};
    for (int hz : {30,60}) for (int mode = 0; mode < 4; ++mode) {
        label = std::string(modes[mode]) + '-' + std::to_string(hz);
        if (!filter.empty() && filter != label) continue;
        finish([&]() {
            stage="startup"; auto scene=make(hz,false); if (!scene.state) return fail("stock scene creation");
            auto& s=*scene.state; std::vector<int> startDeployed;
            for (std::size_t n=0;n<scene.capable.size();++n) if (mode==1 || (mode==2 && n%2==0) || (mode==3 && n%2==1)) {
                stage="individual initial deployment";
                if (!key(s,{scene.capable[n]}) || !settle(scene,{scene.capable[n]},true)) return false;
                startDeployed.push_back(scene.capable[n]);
            }
            if (mode==3) std::reverse(scene.selection.begin(),scene.selection.end());
            stage="initial-state"; record(scene,s,"original");
            if (hz==30 && mode==2 && !capture(scene,"mixed")) return false;
            if (mode==2 && !save(scene)) return false;
            // When all actors begin deployed, the first D legitimately folds;
            // mixed/all-mobile first D must put every bistable actor deployed.
            const bool firstExpected = mode!=1;
            stage="first-D"; if (!key(s,scene.selection)) return false; ++groupKeys;
            commands << label << ',' << stage << ",WndProc-D,1," << s.tick << ",0,0," << iniHexEncode(idText(scene.selection)) << ",0,0,0,DISPATCHED\n";
            step(s); record(scene,s,"original"); if (!invariants(scene,s)) return false;
            if (mode==2) { stage="first-transition"; if (!save(scene)) return false; stage="first-D"; }
            if (!settle(scene,scene.capable,firstExpected) || !checkEquipment(scene,firstExpected)) return false;
            stage="first-complete"; record(scene,s,"original");
            if (hz==30 && mode==2 && !capture(scene,firstExpected?"all-deployed":"all-mobile")) return false;
            stage="second-D"; if (!key(s,scene.selection)) return false; ++groupKeys;
            commands << label << ',' << stage << ",WndProc-D,2," << s.tick << ",0,0," << iniHexEncode(idText(scene.selection)) << ",0,0,0,DISPATCHED\n";
            step(s); record(scene,s,"original"); if (!invariants(scene,s)) return false;
            if (mode==2) { stage="second-transition"; if (!save(scene)) return false; stage="second-D"; }
            if (!settle(scene,scene.capable,!firstExpected) || !checkEquipment(scene,!firstExpected)) return false;
            stage="second-complete"; record(scene,s,"original");
            if (hz==30 && mode==2 && !capture(scene,!firstExpected?"all-deployed":"all-mobile")) return false;
            cases << label << ',' << hz << ',' << modes[mode] << ',' << scene.selection.size() << ',' << scene.capable.size()
                << ',' << firstExpected << ',' << firstExpected << ",2,0," << scene.ordinary << ',' << scene.canonical
                << ',' << scene.paired << ",PASS\n";
            return true;
        }());
    }
    for (int hz : {30,60}) {
        label="multiplayer-batches-"+std::to_string(hz); if (!filter.empty() && filter!=label) continue;
        finish([&]() {
            stage="startup"; auto scene=make(hz,true); if (!scene.state) return fail("stock large scene creation");
            auto& s=*scene.state;
            // Chunk two starts wholly deployed while chunk one starts wholly
            // mobile. Recomputing each chunk would incorrectly fold chunk two.
            const std::vector<int> deployedTail(scene.capable.begin()+256,scene.capable.end());
            stage="individual tail initial deployment";
            for (int id : deployedTail) if (!key(s,{id}) || !settle(scene,{id},true)) return false;
            stage="mixed-before-wire"; record(scene,s,"original");
            RuntimeMultiplayerRuntimeBridge bridge; s.multiplayerBridge=&bridge;
            const auto originalSlots=s.config.playerSlots;
            RuntimeConfig::PlayerSlot local; local.enabled=local.human=local.local=true; s.config.playerSlots={local};
            struct RouteCleanup { RuntimeWindowState& state; std::vector<RuntimeConfig::PlayerSlot> slots; ~RouteCleanup() {
                state.multiplayerIntentRouter.setCaptureCallback({}); state.multiplayerIntentRouter.setAuthorityState({});
                state.multiplayerBridge=nullptr; state.config.playerSlots=std::move(slots);
            } } routeCleanup{s,originalSlots};
            std::vector<RuntimeMultiplayerGameplayCommand> captured;
            s.multiplayerIntentRouter.setCaptureCallback([&](const RuntimeMultiplayerGameplayCommand& command,std::string& message) {
                if (!std::holds_alternative<RuntimeMultiplayerGameplaySecondaryActionCommand>(command.body)) {message="secondary command required";return false;}
                captured.push_back(command); message.clear(); return true;
            });
            int sceneBatches=0;
            for (int press=1;press<=2;++press) {
                stage=press==1?"first-D":"second-D";
                const auto before=[&]() { std::map<int,std::tuple<bool,std::string,int>> values;
                    for(int id:scene.capable) { const auto* o=findObjectById(s,id); values[id]={o->deployed,o->mission,o->buildAnimTicksRemaining}; }
                    return values; }();
                captured.clear(); if(!key(s,scene.selection)) return false; ++groupKeys;
                if(captured.size()!=2) return fail("whole selection was not split into exactly two wire payloads");
                std::vector<std::int32_t> sentIds;
                for(std::size_t n=0;n<captured.size();++n) {
                    const auto& action=std::get<RuntimeMultiplayerGameplaySecondaryActionCommand>(captured[n].body);
                    const auto expected=press==1?RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll:RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll;
                    if(action.action!=expected || action.objectIds.size()>runtimeMultiplayerGameplayMaximumObjectIds)
                        return fail("wire fragment lost the frozen whole-selection destination");
                    sentIds.insert(sentIds.end(),action.objectIds.begin(),action.objectIds.end());
                    for(int id:scene.capable) { const auto* o=findObjectById(s,id);
                        if(before.at(id)!=std::tuple{o->deployed,o->mission,o->buildAnimTicksRemaining}) return fail("network D changed local simulation before replay"); }
                }
                std::sort(sentIds.begin(),sentIds.end()); auto expectedIds=scene.capable; std::sort(expectedIds.begin(),expectedIds.end());
                if(sentIds!=expectedIds) return fail("wire omitted/duplicated capable actors or included MTNK");
                for(std::size_t n=0;n<captured.size();++n) {
                    std::vector<std::uint8_t> wire; RuntimeMultiplayerGameplayCommand decoded; std::string problem;
                    if(!encodeRuntimeMultiplayerGameplayCommand(captured[n],wire,problem)
                        || !decodeRuntimeMultiplayerGameplayCommand(wire,decoded,problem)
                        || !prevalidateRuntimeMultiplayerGameplayCommand(s,"Americans",decoded,problem)
                        || !applyRuntimeMultiplayerGameplayCommand(s,"Americans",decoded)) return fail("authoritative wire replay: "+problem);
                    const auto& action=std::get<RuntimeMultiplayerGameplaySecondaryActionCommand>(decoded.body);
                    std::vector<int> ids(action.objectIds.begin(),action.objectIds.end());
                    commands << label << ',' << stage << ",captured-wire-replay," << press << ',' << s.tick << ',' << n
                        << ',' << static_cast<int>(action.action) << ',' << iniHexEncode(idText(ids))
                        << ',' << wire.size() << ",1,1,PASS\n"; ++batches; ++sceneBatches;
                }
                if(!settle(scene,scene.capable,press==1) || !checkEquipment(scene,press==1)) return false;
                record(scene,s,"original");
            }
            cases << label << ',' << hz << ",multiplayer-batches," << scene.selection.size() << ',' << scene.capable.size()
                << ",1,1,2," << sceneBatches << ",0,0,0,PASS\n"; return true;
        }());
    }
    if(!failures.empty()) {
        std::ostringstream result; result<<failures.size()<<" mixed deployment scene(s) failed";
        for(const auto& failure:failures) result<<'\n'<<failure; error=result.str(); return false;
    }
    if(successful==0) return fail("case filter selected no scene");
    std::cout << "Mixed deploy passed: cases=" << successful << " groupKeys=" << groupKeys << " wireBatches=" << batches
        << " ordinary14=" << ordinarySaves << " canonicalExtensions=" << canonicalSaves << " pairedTicks=" << pairedTicks
        << " nativeCaptures=" << screenshots << " stock/real-WndProc-D/frozen-batches/identity-HP-weapons/body-animation/save=ok\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
