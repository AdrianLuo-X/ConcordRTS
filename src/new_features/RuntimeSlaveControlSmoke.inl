// Issue 13: real stock slave management, player input and retained orders.
// Canonical checkpoints are authority images paired with their ordinary v14 base.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeSlaveControlSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string label = "startup", stage = "assets";
    const auto fail = [&](const std::string& why) {
        error = "Slave control " + label + " [" + stage + "]: " + why; return false;
    };
    const char* output = std::getenv("CONCORD_SLAVE_CONTROL_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue23_13_slave_control";
    std::filesystem::create_directories(root);
    std::ofstream cases(root / "cases.csv"), actions(root / "actions.csv"), workers(root / "workers.csv"), saves(root / "checkpoints.csv");
    if (!cases || !actions || !workers || !saves) return fail("output unavailable");
    cases << "case,hz,workers,denied_orders,selection_checks,legacy_orders,ordinary14,canonical_images,paired_ticks,liberated_moves,credits,result\n";
    actions << "case,stage,tick,kind,actor,accepted,managed,state_unchanged\n";
    workers << std::setprecision(17) << "case,stage,tick,id,home,type,owner,alive,hp,managed,phase,cargo,value,mission,x,y,body_x,body_y,body_z,subcell,active\n";
    saves << "case,stage,format,tick,file,base_file,disk_read,version,paired_ticks\n";
    const auto* slaveRule = live.rules.findUnitType("infantry", "SLAV");
    const auto* mobileRule = live.rules.findUnitType("vehicle", "SMIN");
    const auto* homeRule = live.rules.findBuildingType("YAREFN");
    const auto* normalRule = live.rules.findUnitType("infantry", "E1");
    if (!live.rulesLoaded || !live.artLoaded || !live.theaterLoaded || !slaveRule || !mobileRule || !homeRule || !normalRule
        || !slaveRule->slaved || slaveRule->strength != 125 || slaveRule->speed != 3 || slaveRule->storage != 4
        || slaveRule->harvestRate != 150 || homeRule->slavesNumber != 5 || mobileRule->slavesNumber != 5
        || homeRule->slaveRegenRate != 500 || homeRule->slaveReloadRate != 25
        || mobileRule->deploysInto != "YAREFN" || homeRule->undeploysInto != "SMIN" || homeRule->enslaves != "SLAV")
        return fail("original registered SLAV/SMIN/YAREFN declarations required");
    std::optional<TerrainCell> flat;
    auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (const auto& cell : live.world.getTerrainCells()) {
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.bridge && !info.cliff && !info.shore && !cell.hasOverlay
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) { flat = cell; break; }
    }
    if (!flat) return fail("a real flat land TMP subtile is required");
    const auto pose = [](const WorldObjectInstance& o) { return std::array<double,3>{
        o.hasRenderPosition ? o.renderX : double(o.x), o.hasRenderPosition ? o.renderY : double(o.y),
        o.hasRenderPosition ? o.renderLevel : double(o.level)}; };
    const auto facade = [](RuntimeWindowState& s) {
        s.economy.owner = s.config.playerCountry = "YuriCountry"; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority a; a.owner = a.country = "YuriCountry"; a.canonicalOwner = "yuricountry"; a.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(a)); initializeRuntimeOfflineGameplayRouting(s);
    };
    int finished = 0, totalDenied = 0, totalSelections = 0, totalLegacy = 0, totalPairs = 0, totalSaves = 0, totalFreed = 0;
    for (int hz : {15,30,60}) {
        label = "stock-slave-control-" + std::to_string(hz); stage = "native fixture";
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.gameMode = "Battle"; s.config.playerSlots.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.gameModeRulesOverride.clear(); s.config.multiplayerActive = s.config.computerAiEnabled = s.config.noCooldown = false;
        s.config.superweapons = s.config.crates = s.config.shortGame = false; s.campaignMode = false;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.multiplayerLocalSpectator = false;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        s.economy.skirmishStartApplied = true; s.economy.credits = 0; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.tick = 200; s.skirmishTeamByOwner = {{"yuricountry","1"},{"british","2"}}; s.skirmishModeAlliesAllowed = true;
        facade(s); RuntimeSnapshotActiveStateScope sceneActive(s);
        for (int y = 13; y <= 48; ++y) for (int x = 20; x <= 48; ++x) {
            TerrainCell c = *flat; c.x = x; c.y = y; c.cellIndex512 = y * 512 + x; c.level = 0;
            if (x >= 38 && x <= 40 && y >= 32 && y <= 34) setRuntimeResourceTerrainBails(c, "TIB", 12);
            s.world.accessTerrainCells().push_back(std::move(c));
        }
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s);
        s.particles.reset(hz); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick);
        if (!s.particlesInitialized) return fail("native particle clock initialization");
        auto* miner = addRuntimeObjectInstanceForOwner(s, "YuriCountry", RuntimeBuildKind::Unit, "SMIN", {34,32});
        if (!miner) return fail("stock miner unavailable"); miner->mission = "Guard"; const int firstHome = miner->instanceId;
        auto* normal = addRuntimeObjectInstanceForOwner(s, "YuriCountry", RuntimeBuildKind::Infantry, "E1", {24,20});
        if (!normal) return fail("ordinary infantry unavailable"); normal->mission = "Sleep"; const int normalId = normal->instanceId;
        auto* enemy = addRuntimeObjectInstanceForOwner(s, "British", RuntimeBuildKind::Infantry, "E1", {46,15});
        if (!enemy) return fail("visible enemy target unavailable"); enemy->mission = "Sleep"; const int enemyId = enemy->instanceId;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        const auto home = [&]() -> const WorldObjectInstance* {
            const WorldObjectInstance* found = nullptr;
            for (const auto& o : s.world.getObjects()) if (o.alive && o.health > 0 && o.owner == "YuriCountry" && (o.type == "SMIN" || o.type == "YAREFN")) {
                if (found) return nullptr; found = &o;
            }
            return found;
        };
        const auto ids = [&]() { std::set<int> result; const auto* h = home(); if (!h) return result;
            for (const auto& [id,t] : s.slaveMinerTasks) if (t.refineryId == h->instanceId) result.insert(id); return result; };
        const auto record = [&]() {
            for (const auto& o : s.world.getObjects()) if (o.type == "SLAV") {
                const auto p = pose(o); const auto t = s.slaveMinerTasks.find(o.instanceId);
                workers << label << ',' << stage << ',' << s.tick << ',' << o.instanceId << ',' << (t == s.slaveMinerTasks.end() ? -1 : t->second.refineryId)
                    << ',' << o.type << ',' << o.owner << ',' << o.alive << ',' << o.health << ',' << runtimeObjectIsEnslavedSlaveWorker(s,o)
                    << ',' << (t == s.slaveMinerTasks.end() ? "liberated" : runtimeSlaveMinerPhaseName(t->second.phase))
                    << ',' << (t == s.slaveMinerTasks.end() ? 0 : t->second.cargoUnits) << ',' << (t == s.slaveMinerTasks.end() ? 0 : t->second.cargoValue)
                    << ',' << o.mission << ',' << o.x << ',' << o.y << ',' << p[0] << ',' << p[1] << ',' << p[2] << ',' << o.subCell << ',' << o.renderMoveActive << '\n';
            }
        };
        const auto step = [&]() { ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); };
        const auto wait = [&](int budget, const auto& ready, const std::string& why) {
            for (int n = 0; n < budget; ++n) { if (ready()) return true; step(); }
            if (ready()) return true; record();
            const auto* diagnosticHome=home();
            if(diagnosticHome) std::cout<<"SlaveReturnDiagnostic home="<<diagnosticHome->instanceId<<" type="<<diagnosticHome->type<<" cell="<<diagnosticHome->x<<','<<diagnosticHome->y<<" active="<<diagnosticHome->renderMoveActive<<" tick="<<s.tick<<'\n';
            for(const auto& [id,task]:s.slaveMinerTasks) {
                const auto* worker=findRuntimeObjectByIdIncludingInactive(s,id); if(!worker)continue;
                const auto behavior=s.behaviors.find(id);
                const auto exactEntry=diagnosticHome?findRuntimePassableCellNear(s,*worker,runtimeCellPointForObject(*diagnosticHome),0,diagnosticHome->instanceId):std::optional<RuntimeCellPoint>{};
                std::cout<<"SlaveReturnDiagnostic worker="<<id<<" task="<<serializeRuntimeSlaveMinerTask(task)<<" suspended="<<worker->harvestAutoSearchSuspended<<" hasPose="<<worker->hasRenderPosition<<" atHome="<<(diagnosticHome&&runtimeSlaveMinerWorkerAtRefinery(s,*worker,*diagnosticHome))<<" exactEntry="<<exactEntry.has_value()<<" behavior="<<(behavior==s.behaviors.end()?std::string("none"):runtimeMultiplayerSnapshotBehaviorRecord(id,behavior->second))<<'\n';
            }
            return fail(why + " tick=" + std::to_string(s.tick) + " status=" + s.simulationStatus);
        };
        const auto toggle = [&]() { const auto* h = home(); if (!h) return false; s.selectedObjectIds = {h->instanceId};
            return dispatchRuntimeSecondaryActionFromUi(s,h->instanceId); };
        stage = "original home deploy and worker generation"; s.selectedObjectIds = {firstHome};
        if (!toggle() || !wait(hz*30,[&]() { const auto* h = home(); return h && h->type == "YAREFN" && h->buildAnimTicksRemaining == 0 && ids().size() == 5; }, "original deployment did not generate five workers")) return false;
        const auto managedIds = ids();
        stage = "autonomous mining";
        if (!wait(hz*90,[&]() { for (int id : managedIds) { const auto* w = findObjectById(s,id); const auto t = s.slaveMinerTasks.find(id);
                if (w && t != s.slaveMinerTasks.end() && t->second.cargoUnits > 0 && !runtimeSlaveMinerWorkerAtRefinery(s,*w,*home())) return true; } return false;
            }, "no genuine stock ore pickup")) return false;
        int actor = -1; for (int id : managedIds) { const auto* w = findObjectById(s,id); if (w && !runtimeSlaveMinerWorkerAtRefinery(s,*w,*home())) { actor=id; break; } }
        if (actor < 0) return fail("no live emerged worker for real input");
        const auto unchanged = [&]() { std::ostringstream out; out << std::setprecision(17) << s.world.deterministicSimulationStateHash();
            for (int id : managedIds) { const auto* o = findRuntimeObjectByIdIncludingInactive(s,id);
                const auto t = s.slaveMinerTasks.find(id); if (!o || t == s.slaveMinerTasks.end()) { out << "missing" << id; continue; }
                out << '|' << id << ':' << serializeRuntimeSlaveMinerTask(t->second) << ':' << o->harvestAutoSearchSuspended;
                if (const auto b=s.behaviors.find(id); b!=s.behaviors.end()) out << ':' << runtimeMultiplayerSnapshotBehaviorRecord(id,b->second);
            } return out.str(); };
        int denied=0,selections=0,legacy=0,caseSaves=0,casePairs=0,freedMoves=0;
        const auto noManagedSelection = [&]() { for (int id : s.selectedObjectIds) { const auto* o=findObjectById(s,id);
                if (o && runtimeObjectIsEnslavedSlaveWorker(s,*o)) return false; } return true; };
        stage = "real click box groups and shortcuts";
        const RECT client{0,0,1000,700}, viewport=runtimeMapViewport(client);
        centerRuntimeCameraOnCell(s,client,runtimeCellPointForObject(*findObjectById(s,actor)));
        const auto projected=runtimeProjectedObjectsForViewport(s,viewport);
        const auto view=std::find_if(projected.begin(),projected.end(),[&](const auto& v) { return v.object && v.object->instanceId==actor; });
        if (view==projected.end()) return fail("emerged slave was not projected");
        std::optional<POINT> hit; const RECT bounds=view->bounds;
        for(int y=std::max(viewport.top,bounds.top);y<std::min(viewport.bottom,bounds.bottom)&&!hit;++y)
            for(int x=std::max(viewport.left,bounds.left);x<std::min(viewport.right,bounds.right);++x) {
                const auto* p=findProjectedObjectAtPoint(projected,{x,y}); if(p&&p->object&&p->object->instanceId==actor) { hit=POINT{x,y}; break; }
            }
        if(!hit) return fail("emerged slave has no actual unobscured picking pixel");
        HWND window=CreateWindowExW(0,L"STATIC",L"Slave control input",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window) return fail("hidden input window unavailable");
        struct WindowCleanup { HWND value; ~WindowCleanup(){DestroyWindow(value);} } windowCleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const auto inputBefore=unchanged();
        s.selectedObjectIds.clear(); runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(hit->x,hit->y));
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(hit->x,hit->y));
        if(!noManagedSelection()) return fail("real click selected a managed worker"); ++selections;
        s.selectedObjectIds.clear(); runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(bounds.left-2,bounds.top-2));
        runtimeWindowProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(bounds.right+2,bounds.bottom+2));
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(bounds.right+2,bounds.bottom+2));
        if(!noManagedSelection()) return fail("real box selected a managed worker"); ++selections;
        s.config.keyboardReferenceLayout=true; s.config.keyBindings.clear(); ensureRuntimeMenuKeyboardLoaded(s);
        s.selectedObjectIds={actor}; handleRuntimePlayerCommandCode(s,client,'1'|runtimePlayerControlKey,false,1000);
        if(!s.playerLocalCommands.teams[0].empty()) return fail("control group accepted managed worker"); ++selections;
        s.playerLocalCommands.teamCreate(0,{actor},[](int){return true;}); // Obsolete saved UI group, not an accepted new selection.
        for(int key : {'1','T','M','N'}) { s.selectedObjectIds={actor}; handleRuntimePlayerCommandCode(s,client,key,false,2000+selections*700);
            if(!noManagedSelection()) return fail("shortcut retained managed worker"); ++selections; }
        if(unchanged()!=inputBefore) return fail("selection input changed autonomous worker state");
        const auto deniedUi = [&](const std::string& kind,const auto& apply) { const auto before=unchanged(); s.selectedObjectIds={actor};
            const bool accepted=apply(); const bool same=unchanged()==before;
            actions<<label<<','<<stage<<','<<s.tick<<','<<kind<<','<<actor<<','<<accepted<<",1,"<<same<<'\n';
            if(accepted||!same) return fail(kind+" accepted or changed a managed worker"); ++denied; return true; };
        using Kind=RuntimeMultiplayerGameplayTargetOrderKind;
        stage="instant primary normal force follow travel and append";
        for(Kind kind : {Kind::Move,Kind::Primary,Kind::ForceFireCell,Kind::AttackMove,Kind::GuardDestination})
            if(!deniedUi("cell-"+std::to_string(int(kind)),[&](){return dispatchRuntimeTargetOrderFromUi(s,kind,{25,25});}))return false;
        for(Kind kind : {Kind::Primary,Kind::Attack})
            if(!deniedUi("enemy-"+std::to_string(int(kind)),[&](){const auto* t=findObjectById(s,enemyId);return dispatchRuntimeTargetOrderFromUi(s,kind,runtimeCellPointForObject(*t),t);}))return false;
        if(!deniedUi("Follow",[&](){const auto* t=findObjectById(s,normalId);return dispatchRuntimeTargetOrderFromUi(s,Kind::Follow,runtimeCellPointForObject(*t),t);}))return false;
        if(!deniedUi("Primary-return-home",[&](){const auto* h=home();return dispatchRuntimeTargetOrderFromUi(s,Kind::Primary,runtimeCellPointForObject(*h),h);}))return false;
        for(auto type : {RuntimeMultiplayerGameplayCommandType::Stop,RuntimeMultiplayerGameplayCommandType::Guard,RuntimeMultiplayerGameplayCommandType::Scatter})
            if(!deniedUi("selection-"+std::to_string(int(type)),[&](){return dispatchRuntimeSelectionCommandFromUi(s,type);}))return false;
        s.playerLocalCommands.planning=true;
        if(!deniedUi("append",[&](){return dispatchRuntimeTargetOrderFromUi(s,Kind::Move,{25,25});}))return false;
        s.playerLocalCommands.planning=false;
        if(s.playerCommands.queues.contains(actor)||s.playerCommands.following.contains(actor)||s.playerCommands.travelling.contains(actor)||s.playerCommands.waypoints.routes.contains(actor))
            return fail("rejected input created player authority");
        stage="authenticated command codec and prevalidation";
        RuntimeMultiplayerGameplayTargetOrderCommand order; order.order=Kind::Move; order.objectIds={actor}; order.target=RuntimeMultiplayerGameplayCellTarget{{25,25}};
        RuntimeMultiplayerGameplayCommand command; command.body=order; std::vector<std::uint8_t> bytes; RuntimeMultiplayerGameplayCommand decoded; std::string reason;
        if(!encodeRuntimeMultiplayerGameplayCommand(command,bytes,reason)||!decodeRuntimeMultiplayerGameplayCommand(bytes,decoded,reason))return fail("original command codec: "+reason);
        if(prevalidateRuntimeMultiplayerGameplayCommand(s,"YuriCountry",decoded,reason))return fail("authenticated peer accepted managed actor");
        if(!deniedUi("direct-authoritative-apply",[&](){return applyRuntimeMultiplayerGameplayCommand(s,"YuriCountry",decoded);}))return false;
        stage="obsolete retained queue follow travel waypoint";
        const auto key=runtimeCanonicalHumanHouseOwner(s.economy.owner);
        for(int mode=0;mode<4;++mode) {
            // These are legacy retained intents, constructed with the existing
            // state serializers' representations, never new accepted player orders.
            if(mode==0&&!s.playerCommands.append(key,order))return fail("legacy queue construction");
            if(mode==1)s.playerCommands.following[actor]={key,normalId,{-1,-1}};
            if(mode==2)s.playerCommands.travelling[actor]={key,{25,25},Kind::GuardDestination,false,s.tick};
            if(mode==3&&!s.playerCommands.waypoints.append(key,order))return fail("legacy waypoint construction");
            const auto before=unchanged(); advanceRuntimePlayerCommands(s);
            if(unchanged()!=before||s.playerCommands.queues.contains(actor)||s.playerCommands.following.contains(actor)
                ||s.playerCommands.travelling.contains(actor)||s.playerCommands.waypoints.routes.contains(actor))return fail("obsolete intent changed mining or survived permanent rejection mode="+std::to_string(mode));
            ++legacy;
        }
        const auto pairState = [&](const RuntimeWindowState& q) { std::ostringstream out; out<<std::setprecision(17)<<q.tick<<'|'<<q.economy.credits;
            for(const auto& o:s.world.getObjects()) { const auto* a=findRuntimeObjectByIdIncludingInactive(q,o.instanceId); if(!a){out<<"missing"<<o.instanceId;continue;}const auto p=pose(*a);
                out<<'|'<<a->instanceId<<':'<<a->type<<':'<<a->owner<<':'<<a->alive<<':'<<a->health<<':'<<a->mission<<':'<<a->x<<':'<<a->y<<':'<<a->level<<':'<<a->subCell<<':'<<a->onBridge
                    <<':'<<p[0]<<':'<<p[1]<<':'<<p[2]<<':'<<a->renderMoveActive<<':'<<runtimeObjectIsEnslavedSlaveWorker(q,*a);
                if(auto t=q.slaveMinerTasks.find(a->instanceId);t!=q.slaveMinerTasks.end())out<<':'<<serializeRuntimeSlaveMinerTask(t->second);
            } return out.str(); };
        const auto checkpoint = [&]() {
            if(!wait(hz*5,[&](){for(const auto& [id,b]:s.behaviors)if(b.kind==RuntimeBehaviorKind::Move){const auto* o=findObjectById(s,id);
                    if(o&&(!o->renderMoveActive||!o->hasRenderPosition||b.moveCooldownTicks<=1))return false;}return true;},"no legal committed v14 checkpoint window"))return false;
            std::ostringstream baseText,combined,canonicalText; IniFile extension;
            if(!writeRuntimeStateToStream(s,baseText,error))return fail("ordinary writer: "+error);
            combined<<baseText.str();
            if(!writeRuntimeMultiplayerSnapshotExtension(s,combined,error)||!extension.loadFromString(combined.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(extension,canonicalText,error))return fail("canonical authority writer: "+error);
            const auto basePath=root/(label+"-"+stage+"-ordinary14.ini"), exactPath=root/(label+"-"+stage+"-canonical-image.ini");
            const auto write=[&](const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;file.close();return bool(file);};
            if(!write(basePath,baseText.str())||!write(exactPath,canonicalText.str()))return fail("actual checkpoint file write failed");
            IniFile base,exact;if(!base.loadFromFile(basePath.generic_string())||!exact.loadFromFile(exactPath.generic_string())||base.getValue("RuntimeSave","Version","")!="14")return fail("actual checkpoint file parse failed");
            auto ordinary=std::make_unique<RuntimeWindowState>(), precise=std::make_unique<RuntimeWindowState>();
            for(auto* q : {ordinary.get(),precise.get()}) { seedRuntimeLoadScratch(*q,s,false); RuntimeSnapshotActiveStateScope active(*q);
                if(!materializeRuntimeLoadStateFromIni(*q,base,error)||(q==precise.get()&&!materializeRuntimeMultiplayerSnapshotExtension(*q,exact,error)))return fail("actual checkpoint load: "+error);
                facade(*q);q->runtimeSuppressPlayerAudioFeedback=true;q->runtimeSoundsEnabled=q->runtimePreferencePersistenceEnabled=q->visionEnabled=false;
            }
            if(pairState(*ordinary)!=pairState(s)||pairState(*precise)!=pairState(s))return fail("checkpoint worker/home/body/task/management mismatch");
            for(int n=0;n<12;++n) { step(); for(auto* q : {ordinary.get(),precise.get()}) { RuntimeSnapshotActiveStateScope active(*q);++q->tick;advanceRuntimeSimulation(*q,false);completeRuntimeWorldRefresh(*q); }
                if(pairState(*ordinary)!=pairState(s)||pairState(*precise)!=pairState(s))return fail("actual ordinary/canonical paired worker continuation mismatch");++casePairs;
            }
            saves<<label<<','<<stage<<",ordinary14,"<<s.tick-12<<','<<basePath.generic_string()<<','<<basePath.generic_string()<<",1,14,12\n";
            saves<<label<<','<<stage<<",ordinary14+canonical-image,"<<s.tick-12<<','<<exactPath.generic_string()<<','<<basePath.generic_string()<<",1,14,12\n";
            caseSaves+=2;return true;
        };
        stage="autonomous mining continues after rejected commands";
        if(!wait(hz*90,[&](){return s.economy.credits>0;},"rejected player commands interrupted original mining/delivery"))return false;
        stage="ordinary infantry control";applyRuntimePlayerSelection(s,{normalId},false,false);
        if(!selectionContains(s,normalId)||!dispatchRuntimeTargetOrderFromUi(s,Kind::Move,{25,20}))return fail("ordinary E1 control regressed");
        if(!wait(hz*15,[&](){const auto* o=findObjectById(s,normalId);return o&&o->x==25&&o->y==20&&!o->renderMoveActive;},"ordinary E1 did not move"))return false;
        stage="home undeploy move recall redeploy";
        if(!toggle()||!wait(hz*20,[&](){const auto* h=home();return h&&h->type=="SMIN"&&!runtimeMissionIsUndeploying(*h);},"original repack failed"))return false;
        const auto* mobile=home();s.selectedObjectIds={mobile->instanceId};
        if(!dispatchRuntimeTargetOrderFromUi(s,Kind::Move,{30,32}))return fail("managed miner is not player controllable");
        if(!wait(hz*150,[&](){const auto* h=home();if(!h||h->x!=30||h->y!=32||h->renderMoveActive||ids()!=managedIds)return false;
                for(int id:managedIds){const auto* w=findRuntimeObjectByIdIncludingInactive(s,id);const auto t=s.slaveMinerTasks.find(id);
                    if(!w||t==s.slaveMinerTasks.end()||t->second.phase!=RuntimeSlaveMinerPhase::Stored||!runtimeObjectIsEnslavedSlaveWorker(s,*w)
                        ||runtimeObjectAcceptsPlayerSelection(s,*w)||w->alive||w->mission!="InsideSlaveMiner")return false;}return true;},"mobile home failed real move/recall with managed identities"))return false;
        record();
        stage="managed-stored-checkpoint";if(hz==30&&!checkpoint())return false;
        stage="home undeploy move recall redeploy";
        if(!toggle()||!wait(hz*30,[&](){const auto* h=home();if(!h||h->type!="YAREFN"||h->buildAnimTicksRemaining||ids()!=managedIds)return false;
                for(int id:managedIds){const auto* w=findObjectById(s,id);if(!w||!runtimeObjectIsEnslavedSlaveWorker(s,*w)||runtimeSlaveMinerWorkerAtRefinery(s,*w,*h))return false;}return true;},"redeployed workers did not emerge with original management"))return false;
        stage="genuine home destruction releases existing owners";
        const int deadHome=home()->instanceId;RuntimeObjectExitRequest exit;exit.objectId=deadHome;exit.kind=RuntimeObjectExitKind::Destroyed;
        exit.tick=s.tick;exit.suppressCrew=exit.suppressDeathWeapon=true;exit.reason="slave control native parent death";
        if(!finalizeRuntimeObjectExit(s,exit).accepted)return fail("native home death rejected");
        if(!s.slaveMinerTasks.empty()||findObjectById(s,deadHome))return fail("native parent death did not release management");
        for(int id:managedIds){const auto* w=findObjectById(s,id);if(!w||w->health!=125||w->owner!="YuriCountry"||runtimeObjectIsEnslavedSlaveWorker(s,*w)
                ||!runtimePlayerLocalSelectable(s,id))return fail("liberated worker ownership/health/control gate wrong");}
        record();stage="liberated-checkpoint";if(hz==30&&!checkpoint())return false;
        stage="liberated selection and genuine move";
        const int freed=*managedIds.begin();applyRuntimePlayerSelection(s,{freed},false,false);
        if(!selectionContains(s,freed)||!dispatchRuntimeTargetOrderFromUi(s,Kind::Move,{28,28}))return fail("liberated original slave Move rejected");
        if(!wait(hz*30,[&](){const auto* w=findObjectById(s,freed);return w&&w->x==28&&w->y==28&&!w->renderMoveActive;},"liberated worker did not execute actual Move"))return false;
        s.selectedObjectIds={freed};if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))return fail("liberated worker Stop rejected");
        ++freedMoves;record();
        const int expectedSaves=hz==30?4:0,expectedPairs=hz==30?24:0;
        if(denied!=14||selections!=7||legacy!=4||caseSaves!=expectedSaves||casePairs!=expectedPairs||freedMoves!=1)return fail("required finite control/save matrix incomplete");
        cases<<label<<','<<hz<<",5,"<<denied<<','<<selections<<','<<legacy<<','<<caseSaves/2<<','<<caseSaves/2<<','<<casePairs<<','<<freedMoves<<','<<s.economy.credits<<",PASS\n";
        cases.flush();actions.flush();workers.flush();saves.flush();++finished;totalDenied+=denied;totalSelections+=selections;totalLegacy+=legacy;totalPairs+=casePairs;totalSaves+=caseSaves;totalFreed+=freedMoves;
        std::cout<<"Slave control case "<<label<<": originalWorkers5 denied="<<denied<<" selection="<<selections<<" legacy="<<legacy<<" diskFiles="<<caseSaves<<" pairedTicks="<<casePairs<<" liberatedMove=1 PASS\n";
    }
    if(finished!=3||totalDenied!=42||totalSelections!=21||totalLegacy!=12||totalSaves!=4||totalPairs!=24||totalFreed!=3)return fail("full native coverage incomplete");
    std::cout<<"Slave control: cases="<<finished<<" denied="<<totalDenied<<" selection="<<totalSelections<<" legacy="<<totalLegacy
        <<" ordinary14=2 canonicalImages=2 actualDiskFiles="<<totalSaves<<" pairedTicks="<<totalPairs<<" liberatedMoves="<<totalFreed<<" PASS\n";
    error.clear();return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
