// Issue 17: finite real-command deployment anchor smoke. Candidate only.
// Integer engine anchor: floor((FoundationDimension - 1) / 2), not a half-cell centre.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeDeployCenterSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Deploy center " + label + " [" + stage + "]: " + why; return false;
    };
    const char* output = std::getenv("CONCORD_DEPLOY_CENTER_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue23_17_deploy_center";
    std::filesystem::create_directories(root);
    std::ofstream cases(root/"cases.csv"), actors(root/"actors.csv"), commands(root/"commands.csv"),
        checkpoints(root/"checkpoints.csv"), catalog(root/"catalog.csv");
    if (!cases || !actors || !commands || !checkpoints || !catalog) return fail("evidence streams");
    cases << "case,hz,type,mode,cycles,result\n";
    actors << std::setprecision(17) << "case,stage,tick,id,type,owner,kind,x,y,z,body_x,body_y,subcell,on_bridge,active,deployed,mission,hp,build_remaining,build_total,deploy_remaining,expected_anchor_x,expected_anchor_y,foundation_w,foundation_h\n";
    commands << "case,stage,tick,route,id,accepted\n";
    checkpoints << "case,stage,format,tick,file,base_file,disk_read,version,paired_live_ticks\n";
    catalog << "type,kind,loaded,transform,toggle,foundation,source_rules,source_art\n";
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original startup: " + assets->report.errorMessage);
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    const auto rulesHash = assets->rules.getDetailRulesHash();
    int landTile = -1;
    const auto context = runtimeTerrainMetadataContext(*assets);
    for (int tile=0; tile<static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex=tile; cell.subTileIndex=0;
        if (resolveRuntimeTerrainSubTileLandType(cell,context,assets->renderCache)==RuntimeTerrainLandType::Clear
            && resolveRuntimeTerrainSubTileRampType(cell,context,assets->renderCache)==0) { landTile=tile; break; }
    }
    if (landTile<0) return fail("actual original flat Clear TMP required");
    struct Family { const char* mobile; const char* structure; const char* country; int width, height; };
    const Family families[]={{"AMCV","GACNST","Americans",4,4},{"SMCV","NACNST","Russians",4,4},
        {"PCV","YACNST","YuriCountry",4,4},{"SMIN","YAREFN","YuriCountry",2,2}};
    // The expected rectangle comes from the original ART declaration, parsed
    // independently of the production placement/foundation helper.
    for (const auto& family:families) {
        const auto* unit=findRuntimeUnitTypeById(assets->rules,family.mobile);
        const auto* building=findRuntimeBuildingTypeById(assets->rules,family.structure);
        const auto* art=assets->art.findArtType(family.structure);
        int w=0,h=0; char separator=0; std::istringstream dimensions(art?art->foundation:"");
        dimensions >> w >> separator >> h; dimensions >> std::ws;
        if (!unit || !building || !art || unit->deploysInto!=family.structure
            || building->undeploysInto!=family.mobile || separator!='x' || !dimensions.eof()
            || w!=family.width || h!=family.height) return fail("stock transform/ART: "+std::string(family.mobile));
        catalog << family.mobile << ",vehicle,1," << unit->deploysInto << ",0," << art->foundation
            << ',' << assets->report.rulesPath << ',' << assets->report.artPath << '\n';
    }
    for (const char* type:{"E1","GGI","DESO","YURI","YURIPR","CAOS","SCHP","SCHD","TTNK"}) {
        const auto* unit=findRuntimeUnitTypeById(assets->rules,type);
        WorldObjectInstance probe; probe.type=type; probe.kind=(std::string(type)=="CAOS" || std::string(type)=="SCHP"
            || std::string(type)=="SCHD" || std::string(type)=="TTNK")?WorldObjectKind::Unit:WorldObjectKind::Infantry;
        const auto profile=runtimeDeployProfileForObject(*assets,probe);
        if (!unit || (std::string(type)=="TTNK" ? profile.hasProfile : !profile.hasProfile))
            return fail("stock deploy profile: "+std::string(type));
        catalog << type << ',' << (probe.kind==WorldObjectKind::Infantry?"infantry":"vehicle")
            << ",1," << unit->deploysInto << ',' << profile.togglesDeployedState << ",none,"
            << assets->report.rulesPath << ',' << assets->report.artPath << '\n';
    }
    const auto cornerFor=[](RuntimeCellPoint center,const Family& family) {
        return RuntimeCellPoint{center.x-(family.width-1)/2,center.y-(family.height-1)/2};
    };
    const auto step=[](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s,false);
    };
    const auto make=[&](int hz,const std::string& country) {
        auto q=std::make_unique<RuntimeWindowState>(); auto& s=*q;
        seedRuntimeLoadScratch(s,*assets,false); s.config=config; s.config.fixedTickHz=hz;
        s.config.playerSlots.clear(); s.config.playerCountry=country; s.config.gameMode="Battle";
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear(); s.config.mcvRepacks=true;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=false;
        s.config.crates=s.config.superweapons=false;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts={}; s.mapFactsLoaded=false; s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width=-1;
        for (int y=0;y<64;++y) for(int x=0;x<64;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=landTile; cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s);
        s.economy.owner=country; s.economy.skirmishStartApplied=true;
        s.triggers.loaded=s.triggers.bootstrapComplete=true;
        ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        const char* opponent=country=="Russians"?"Americans":"Russians";
        if (!addRuntimeObjectInstanceForOwner(s,opponent,RuntimeBuildKind::Building,"NAPOWR",{3,3})) q.reset();
        return q;
    };
    const auto home=[](const RuntimeWindowState& s,const char* type) -> const WorldObjectInstance* {
        const WorldObjectInstance* result=nullptr;
        for(const auto& o:s.world.getObjects()) if(o.alive && o.type==type) {
            if(result) return nullptr; result=&o;
        }
        return result;
    };
    const auto record=[&](const RuntimeWindowState& s,const WorldObjectInstance& o,RuntimeCellPoint center,int w,int h) {
        actors << label << ',' << stage << ',' << s.tick << ',' << o.instanceId << ',' << o.type << ',' << o.owner
            << ',' << int(o.kind) << ',' << o.x << ',' << o.y << ',' << o.level << ',' << runtimeObjectRenderX(o)
            << ',' << runtimeObjectRenderY(o) << ',' << o.subCell << ',' << o.onBridge << ',' << o.renderMoveActive
            << ',' << o.deployed << ',' << o.mission << ',' << o.health << ',' << o.buildAnimTicksRemaining
            << ',' << o.buildAnimTicksTotal << ',' << o.deployStateTicksRemaining << ',' << center.x << ','
            << center.y << ',' << w << ',' << h << '\n';
    };
    const auto slaveIdsForHost=[](const RuntimeWindowState& s,int hostId) {
        std::vector<int> ids;
        for(const auto& [id,task]:s.slaveMinerTasks) if(task.refineryId==hostId) ids.push_back(id);
        std::sort(ids.begin(),ids.end()); return ids;
    };
    const auto storedWorkers=[&](const RuntimeWindowState& s,int hostId,const std::vector<int>& expected,
        const char* phase,bool capture) {
        const auto ids=slaveIdsForHost(s,hostId); const auto* host=findObjectById(s,hostId);
        const auto* workerType=findRuntimeUnitTypeById(s.rules,"SLAV");
        bool valid=host && host->alive && host->health>0 && ids.size()==5
            && workerType && workerType->slaved && workerType->strength==125
            && (expected.empty() || ids==expected);
        if(capture) {
            stage=phase; std::cout << "DeployStoredWorkers case=" << label << " phase=" << phase << " tick=" << s.tick
                << " host=" << hostId << " workers=" << ids.size() << " buildRemaining="
                << (host?host->buildAnimTicksRemaining:-1) << '\n';
        }
        for(int id:ids) {
            const auto* worker=findRuntimeObjectByIdIncludingInactive(s,id); const auto& task=s.slaveMinerTasks.at(id);
            valid=valid && worker && worker->type=="SLAV" && worker->owner==host->owner && worker->health==125
                && runtimeSlaveMinerWorkerIsStored(s,*worker) && !worker->hasRenderPosition && !worker->renderMoveActive
                && worker->subCell==-1 && worker->x==host->x && worker->y==host->y
                && worker->level==host->level && worker->onBridge==host->onBridge && !s.behaviors.contains(id);
            if(capture && worker) {
                record(s,*worker,RuntimeCellPoint{host?host->x:worker->x,host?host->y:worker->y},families[3].width,families[3].height);
                std::cout << "DeployStoredWorker id=" << id << " home=" << task.refineryId << " alive=" << worker->alive
                    << " hp=" << worker->health << " phase=" << int(task.phase) << " mission=" << worker->mission
                    << " cell=" << worker->x << ',' << worker->y << " subcell=" << worker->subCell
                    << " pose=" << worker->hasRenderPosition << " active=" << worker->renderMoveActive << '\n';
            }
        }
        std::string invalid; if(valid && !validateRuntimeStoredSlaveMinerState(s,&invalid)) valid=false;
        return valid;
    };
    const auto input=[&](RuntimeWindowState& s,int id,bool key) {
        applySelection(s,{id},false,false);
        const bool accepted=key?dispatchRuntimeSecondaryActionForSelectionFromUi(s):dispatchRuntimeSecondaryActionFromUi(s,id);
        commands << label << ',' << stage << ',' << s.tick << ',' << (key?"D-selection":"secondary-click")
            << ',' << id << ',' << accepted << '\n'; return accepted;
    };
    const auto wait=[&](RuntimeWindowState& s,int limit,const auto& ready) {
        for(int n=0;n<=limit;++n) { if(ready()) return true; if(n<limit) step(s); } return false;
    };
    const auto checkBuilding=[&](const RuntimeWindowState& s,const Family& family,RuntimeCellPoint center) {
        const auto* o=home(s,family.structure); const auto corner=cornerFor(center,family);
        if(!o || o->kind!=WorldObjectKind::Structure || o->owner!=family.country || o->x!=corner.x || o->y!=corner.y
            || runtimeObjectRenderX(*o)!=double(corner.x) || runtimeObjectRenderY(*o)!=double(corner.y)
            || o->health!=runtimeMaxHealthForObject(s,*o)) return false;
        const auto item=runtimeBuildItemForBuilding(s,family.structure);
        if(!item) return false;
        const auto bounds=runtimeStructureFootprintForBuildItem(s,*item);
        return bounds.width==family.width && bounds.height==family.height;
    };
    const auto checkMobile=[&](const RuntimeWindowState& s,const Family& family,RuntimeCellPoint center) {
        const auto* o=home(s,family.mobile);
        return o && o->kind==WorldObjectKind::Unit && o->owner==family.country && o->x==center.x && o->y==center.y
            && runtimeObjectRenderX(*o)==double(center.x) && runtimeObjectRenderY(*o)==double(center.y)
            && !o->renderMoveActive && o->health==runtimeMaxHealthForObject(s,*o);
    };
    const auto subset=[](const RuntimeWindowState& s) {
        std::ostringstream text; text << std::setprecision(17) << s.tick << '|' << s.economy.credits;
        for(const auto& o:s.world.getObjects()) if(o.alive && (o.type=="AMCV" || o.type=="SMCV" || o.type=="PCV"
            || o.type=="SMIN" || o.type=="YAREFN" || o.type=="GACNST" || o.type=="NACNST" || o.type=="YACNST")) {
            text << '|' << o.instanceId << ':' << o.type << ':' << o.owner << ':' << o.health << ':' << o.x << ':' << o.y
                << ':' << o.level << ':' << o.subCell << ':' << o.onBridge << ':' << runtimeObjectRenderX(o)
                << ':' << runtimeObjectRenderY(o) << ':' << o.renderMoveActive << ':' << o.facing << ':' << o.deployed
                << ':' << o.mission << ':' << o.buildAnimTicksTotal << ':' << o.buildAnimTicksRemaining << ':' << o.deployStateTicksRemaining;
            const auto b=s.behaviors.find(o.instanceId); text << ':' << (b!=s.behaviors.end());
            if(b!=s.behaviors.end()) text << ':' << int(b->second.kind) << ':' << int(b->second.state)
                << ':' << b->second.moveTarget.x << ':' << b->second.moveTarget.y << ':' << b->second.moveCooldownTicks;
        }
        std::vector<int> taskIds; for(const auto& [id,t]:s.slaveMinerTasks) taskIds.push_back(id);
        std::sort(taskIds.begin(),taskIds.end());
        for(int id:taskIds) text << "|slave:" << id << ':' << serializeRuntimeSlaveMinerTask(s.slaveMinerTasks.at(id));
        return text.str();
    };
    int sceneCount=0, cycles=0, saves=0, paired=0, images=0;
    const auto checkpoint=[&](RuntimeWindowState& s,const std::string& name) {
        std::ostringstream baseText,combined,canonicalText; IniFile authority;
        if(!writeRuntimeStateToStream(s,baseText,error)) return fail("ordinary14 writer: "+error);
        combined << baseText.str();
        if(!writeRuntimeMultiplayerSnapshotExtension(s,combined,error) || !authority.loadFromString(combined.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(authority,canonicalText,error)) return fail("authority image writer: "+error);
        const auto basePath=root/(name+"-ordinary14.ini"), exactPath=root/(name+"-canonical-image.ini");
        const auto write=[](const std::filesystem::path& path,const std::string& text) {
            if(std::filesystem::exists(path)) return false;
            std::ofstream file(path,std::ios::binary); file << text; file.close(); return bool(file);
        };
        if(!write(basePath,baseText.str()) || !write(exactPath,canonicalText.str())) return fail("physical checkpoint write");
        IniFile base,exact;
        if(!base.loadFromFile(basePath.generic_string()) || !exact.loadFromFile(exactPath.generic_string())
            || base.getValue("RuntimeSave","Version","")!="14") return fail("physical checkpoint reread");
        auto ordinary=std::make_unique<RuntimeWindowState>(), precise=std::make_unique<RuntimeWindowState>();
        for(auto* q:{ordinary.get(),precise.get()}) {
            seedRuntimeLoadScratch(*q,s,false); RuntimeSnapshotActiveStateScope active(*q);
            if(!materializeRuntimeLoadStateFromIni(*q,base,error)
                || (q==precise.get() && !materializeRuntimeMultiplayerSnapshotExtension(*q,exact,error))) return fail("physical checkpoint restore: "+error);
            q->runtimeSoundsEnabled=q->runtimePreferencePersistenceEnabled=q->visionEnabled=false;
            q->runtimeSuppressPlayerAudioFeedback=true; initializeRuntimeOfflineGameplayRouting(*q);
        }
        for(int n=0;n<=12;++n) {
            if(subset(s)!=subset(*ordinary) || subset(s)!=subset(*precise)) return fail("checkpoint relevant authority differs step="+std::to_string(n));
            if(n<12) { step(s); step(*ordinary); step(*precise); ++paired; }
        }
        checkpoints << label << ',' << stage << ",ordinary14," << s.tick-12 << ',' << basePath.generic_string() << ','
            << basePath.generic_string() << ",1,14,12\n";
        checkpoints << label << ',' << stage << ",canonical-image-with-base," << s.tick-12 << ',' << exactPath.generic_string()
            << ',' << basePath.generic_string() << ",1,14,12\n"; saves+=2; return true;
    };
    const RuntimeCellPoint center{32,32};
    for(const auto& family:families) for(int hz:{30,60}) {
        label=std::string(family.mobile)+"-"+std::to_string(hz)+"-cycles"; auto box=make(hz,family.country);
        if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        auto* m=addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,family.mobile,center);
        if(!m) return fail("native mobile"); m->mission="Sleep"; const int credits=s.economy.credits;
        std::vector<int> originalSlaveIds;
        if(std::string(family.mobile)=="SMIN") {
            const int mobileId=m->instanceId;
            if(!wait(s,hz*5,[&](){return checkMobile(s,family,center)
                    && storedWorkers(s,mobileId,{},"stored-mobile-initial",false);}))
                return fail("original mobile did not naturally generate five Stored workers");
            originalSlaveIds=slaveIdsForHost(s,mobileId);
            if(!storedWorkers(s,mobileId,originalSlaveIds,"stored-mobile-initial",true))
                return fail("initial Stored worker authority");
        }
        for(int cycle=0;cycle<2;++cycle) {
            stage="mobile-before"; m=findMutableObjectById(s,home(s,family.mobile)?home(s,family.mobile)->instanceId:-1);
            if(!m || !checkMobile(s,family,center)) return fail("initial/returned mobile anchor");
            const int id=m->instanceId; record(s,*m,center,family.width,family.height);
            if(cycle==0 && hz==30 && std::string(family.mobile)=="AMCV") {
                if(!drawRuntimeStateOffscreenAtCell(s,center,800,600,error,(root/"before.bmp").string(),false)) return fail(error); ++images;
            }
            stage="deploy-input"; if(!input(s,id,cycle==1)) return fail("real deploy input");
            if(cycle==0 && hz==30 && std::string(family.mobile)=="AMCV") {
                if(!checkpoint(s,"AMCV-turn")) return false;
            }
            if(!wait(s,hz*15,[&](){return !home(s,family.mobile) && checkBuilding(s,family,center);})) return fail("centered deploy deadline");
            if(std::string(family.mobile)=="SMIN") {
                const auto* h=home(s,family.structure);
                if(!h || !storedWorkers(s,h->instanceId,originalSlaveIds,"stored-deployed-buildup",true))
                    return fail("deployed Stored worker transfer");
                if(cycle==0 && hz==30) {
                    stage="deployed-buildup-checkpoint";
                    if(h->buildAnimTicksRemaining<=12) return fail("physical Stored checkpoint must precede complete buildup");
                    if(!checkpoint(s,"SMIN-deployed")) return false;
                    h=home(s,family.structure);
                    if(!h || h->buildAnimTicksRemaining<=0
                        || !storedWorkers(s,h->instanceId,originalSlaveIds,"stored-after-checkpoint",true))
                        return fail("physical checkpoint released worker or completed buildup");
                }
            }
            if(!wait(s,hz*20,[&](){const auto* h=home(s,family.structure);return h && h->buildAnimTicksRemaining<=0;})) return fail("original buildup deadline");
            stage="deployed"; const auto* h=home(s,family.structure); record(s,*h,center,family.width,family.height);
            if(cycle==0 && hz==30 && std::string(family.mobile)=="AMCV") {
                if(!drawRuntimeStateOffscreenAtCell(s,center,800,600,error,(root/"deployed.bmp").string(),false)) return fail(error); ++images;
            }

            h=home(s,family.structure); if(!h) return fail("checkpoint lost deployed host"); const int hostId=h->instanceId;
            if(std::string(family.mobile)=="SMIN") {
                stage="stored-ready-repack";
                if(!storedWorkers(s,hostId,originalSlaveIds,"stored-ready-repack",true))
                    return fail("complete buildup released Stored worker before real repack");
                const auto* host=findObjectById(s,hostId);
                WorldObjectInstance probe; probe.instanceId=-1; probe.kind=WorldObjectKind::Unit;
                probe.type=family.mobile; probe.owner=host->owner; probe.alive=true;
                probe.level=host->level; probe.onBridge=host->onBridge;
                const auto exact=findRuntimePassableCellNear(s,probe,center,0,hostId);
                const bool clear=exact.has_value() && *exact==center;
                std::cout << "DeployStoredCenter case=" << label << " tick=" << s.tick << " host=" << hostId
                    << " centerPassable0=" << clear << " workers=" << originalSlaveIds.size() << '\n';
                if(!clear) return fail("Stored-worker original repack center is not physically passable");
            }
            stage="repack-input"; if(!input(s,hostId,cycle==0)) return fail("real repack input");
            if(cycle==0 && hz==30 && std::string(family.mobile)=="SMCV" && !checkpoint(s,"SMCV-repacking")) return false;
            if(!wait(s,hz*20,[&](){return !home(s,family.structure) && checkMobile(s,family,center);})) {
                std::cout << std::setprecision(17) << "DeployRepackDiagnostic case=" << label << " tick=" << s.tick
                    << " expected=" << center.x << ',' << center.y << " economy=" << s.economy.status
                    << " interaction=" << s.interactionStatus << " commandValid=" << s.lastCommand.valid
                    << " accepted=" << s.lastCommand.acceptedCount << " rejected=" << s.lastCommand.rejectedCount
                    << " commandTarget=" << s.lastCommand.targetObjectId << " slaveTasks=" << s.slaveMinerTasks.size() << '\n';
                for(const auto& o:s.world.getObjects()) if(o.alive && (o.type==family.mobile || o.type==family.structure)) {
                    record(s,o,center,family.width,family.height);
                    std::cout << "DeployRepackActor id=" << o.instanceId << " type=" << o.type << " owner=" << o.owner
                        << " hp=" << o.health << " maxHP=" << runtimeMaxHealthForObject(s,o) << " cell=" << o.x << ',' << o.y
                        << " body=" << runtimeObjectRenderX(o) << ',' << runtimeObjectRenderY(o) << ',' << runtimeObjectRenderLevel(o)
                        << " pose=" << o.hasRenderPosition << " active=" << o.renderMoveActive << " bridge=" << o.onBridge
                        << " renderBridge=" << o.renderOnBridge << " deployed=" << o.deployed << " mission=" << o.mission
                        << " buildRemaining=" << o.buildAnimTicksRemaining << " buildTotal=" << o.buildAnimTicksTotal;
                    const auto b=s.behaviors.find(o.instanceId);
                    if(b!=s.behaviors.end()) std::cout << " behavior=" << int(b->second.kind) << '/' << int(b->second.state)
                        << " moveTarget=" << b->second.moveTarget.x << ',' << b->second.moveTarget.y
                        << " pendingGoal=" << b->second.pendingPathGoal.x << ',' << b->second.pendingPathGoal.y
                        << " pending=" << b->second.pathRequestPending << " allowed=" << b->second.pendingAllowedOccupiedObjectId
                        << " path=" << b->second.path.size() << " cooldown=" << b->second.moveCooldownTicks;
                    else std::cout << " behavior=none";
                    if(const auto* unit=findRuntimeUnitType(s.rules,o)) std::cout << " gatherer=" << unit->resourceGatherer
                        << " destination=" << unit->resourceDestination << " storage=" << unit->storage << " slaves=" << unit->slavesNumber;
                    std::cout << '\n';
                }
                return fail("repack anchor drift/deadline");
            }
            stage="repacked"; record(s,*home(s,family.mobile),center,family.width,family.height);
            if(std::string(family.mobile)=="SMIN"
                && !storedWorkers(s,home(s,family.mobile)->instanceId,originalSlaveIds,"stored-mobile-repacked",true))
                return fail("repacked Stored worker identity/home transfer");
            if(s.economy.credits!=credits || s.rules.getDetailRulesHash()!=rulesHash) return fail("original credits/rules changed");
            if(cycle==0 && hz==30 && std::string(family.mobile)=="AMCV") {
                if(!drawRuntimeStateOffscreenAtCell(s,center,800,600,error,(root/"repacked.bmp").string(),false)) return fail(error); ++images;
            }
            ++cycles;
        }
        cases << label << ',' << hz << ',' << family.mobile << ",two-real-cycles,2,PASS\n"; ++sceneCount;
    }
    for(const auto& family:families) for(int cornerIndex=0;cornerIndex<4;++cornerIndex) {
        label=std::string(family.mobile)+"-corner-"+std::to_string(cornerIndex); stage="footprint-blocker";
        auto box=make(30,family.country); if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        auto* m=addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,family.mobile,center);
        if(!m) return fail("mobile"); const int id=m->instanceId; m->mission="Sleep";
        const auto top=cornerFor(center,family); const RuntimeCellPoint at{top.x+(cornerIndex%2)*(family.width-1),top.y+(cornerIndex/2)*(family.height-1)};
        const bool selfCorner=at==center;
        if(!selfCorner && !addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,"CAR",at)) return fail("real independent corner blocker");
        const auto item=runtimeBuildItemForBuilding(s,family.structure);
        if(!item || runtimeCellCanPlaceBuilding(s,*item,top,id,nullptr,family.country)!=selfCorner) return fail("independent centered corner placement gate");
        input(s,id,false);
        if(!wait(s,30*15,[&](){const auto* o=findObjectById(s,id); return selfCorner?checkBuilding(s,family,center):(!o || !runtimeMissionIsMcvDeployPending(*o));})) return fail("corner completion");
        if(selfCorner ? !checkBuilding(s,family,center) : (!home(s,family.mobile) || home(s,family.structure))) return fail("true corner collision/self-ignore");
        cases << label << ",30," << family.mobile << ',' << (selfCorner?"original-self-ignore":"true-corner-negative") << ",0,PASS\n"; ++sceneCount;
    }
    for(int n=0;n<3;++n) for(bool late:{false,true}) {
        const auto& family=families[n]; label=std::string(family.mobile)+(late?"-turn-recheck":"-obsolete-corner-positive");
        auto box=make(30,family.country); if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        auto* m=addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,family.mobile,center);
        if(!m) return fail("mobile"); const int id=m->instanceId; m->mission="Sleep";
        stage="request-turn";
        if(!late && !addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,"CAR",{center.x+family.width-1,center.y+family.height-1})) return fail("obsolete-only real blocker");
        if(!input(s,id,false)) return fail("accepted turn input");
        if(late) {
            const auto* turning=findObjectById(s,id);
            if(!turning || !runtimeMissionIsMcvDeployPending(*turning)) return fail("real pending-turn window");
            if(!addRuntimeObjectInstanceForOwner(s,family.country,RuntimeBuildKind::Unit,"CAR",cornerFor(center,family))) return fail("late blocker creation");
        }
        if(!wait(s,30*15,[&](){const auto* o=findObjectById(s,id);return !o || !runtimeMissionIsMcvDeployPending(*o);})) return fail("turn deadline");
        if(late ? (!home(s,family.mobile) || home(s,family.structure)) : !checkBuilding(s,family,center)) return fail("late true-corner recheck/old-corner exclusion");
        cases << label << ",30," << family.mobile << ',' << (late?"late-collision-negative":"obsolete-corner-positive") << ",0,PASS\n"; ++sceneCount;
    }
    for(const char* type:{"E1","GGI","DESO","YURI","YURIPR","CAOS","SCHP","TTNK"}) {
        label=std::string(type)+"-same-unit"; stage="native-profile"; auto box=make(30,"Americans");
        if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        const bool infantry=std::string(type)!="CAOS" && std::string(type)!="SCHP" && std::string(type)!="TTNK";
        auto* o=addRuntimeObjectInstanceForOwner(s,"Americans",infantry?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,type,center);
        if(!o) return fail("stock actor"); o->mission="Sleep"; const int id=o->instanceId;
        if(std::string(type)=="SCHP") {
            applySelection(s,{id},false,false);
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{34,32})
                || !wait(s,30*20,[&](){const auto* a=findObjectById(s,id);return a && a->airborne && !a->renderMoveActive && a->x==34 && a->y==32;})) return fail("natural flight setup");
        }
        const auto before=*findObjectById(s,id); const double x=runtimeObjectRenderX(before),y=runtimeObjectRenderY(before);
        stage="deploy-input"; const bool accepted=input(s,id,false); const auto* now=findObjectById(s,id);
        if(!now || runtimeObjectRenderX(*now)!=x || runtimeObjectRenderY(*now)!=y || now->subCell!=before.subCell) return fail("command changed body/subcell");
        if(std::string(type)=="TTNK") {
            if(accepted || runtimeDeployProfileForObject(s,*now).hasProfile) return fail("TTNK acquired fictitious deployment");
        } else {
            if(!accepted || !wait(s,30*30,[&](){const auto* a=findObjectById(s,id);return a && runtimeObjectHasDeployedState(s,*a)
                && !runtimeMissionIsDeploying(*a) && !runtimeMissionIsLandingToDeploy(*a);})) return fail("original deploy transition deadline");
            now=findObjectById(s,id);
            if(!now || now->health!=before.health || now->subCell!=before.subCell
                || runtimeObjectRenderX(*now)!=x || runtimeObjectRenderY(*now)!=y) return fail("stationary infantry/stat changed");
            stage="deployed"; record(s,*now,{now->x,now->y},0,0);
            if(std::string(type)=="SCHP") {
                RuntimeMapRenderContext visual; visual.rules=&s.rules; visual.art=&s.art;
                if(resolveRuntimeObjectVisualTypeId(*now,visual)!="SCHD") return fail("original deployed SCHD visual");
            }
            if(!wait(s,30*15,[&](){const auto* a=findObjectById(s,id);return a && a->deployStateTicksRemaining<=0;})) return fail("original undeploy delay");
            stage="undeploy-input"; if(!input(s,id,true)) return fail("real undeploy input");
            if(!wait(s,30*30,[&](){const auto* a=findObjectById(s,id);return a && !runtimeObjectHasDeployedState(s,*a)
                && !runtimeMissionIsUndeploying(*a) && !runtimeMissionIsTakingOffAfterUndeploy(*a);})) return fail("original undeploy transition deadline");
            now=findObjectById(s,id); if(!now || now->health!=before.health || now->subCell!=before.subCell
                || runtimeObjectRenderX(*now)!=x || runtimeObjectRenderY(*now)!=y) return fail("undeploy body/stat/subcell changed");
            stage="undeployed"; record(s,*now,{now->x,now->y},0,0);
        }
        cases << label << ",30," << type << ",original-state-profile,0,PASS\n"; ++sceneCount;
    }
    for(int hz:{30,60}) {
        label="AMCV-"+std::to_string(hz)+"-pending-foundation"; stage="future-centered-reservation";
        auto box=make(hz,"Americans"); if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        auto* m=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"AMCV",center); if(!m) return fail("MCV"); const int id=m->instanceId;
        auto* foot=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Infantry,"E1",{40,32}); if(!foot) return fail("Foot"); const int footId=foot->instanceId;
        auto* air=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"SHAD",{40,40}); if(!air) return fail("aircraft"); const int airId=air->instanceId;
        applySelection(s,{airId},false,false);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{44,40})
            || !wait(s,hz*15,[&](){const auto* a=findObjectById(s,airId);return a && a->airborne;})) return fail("actual air-domain setup");
        m=findMutableObjectById(s,id); if(!m) return fail("MCV lost");
        if(!input(s,id,false) || !runtimeMissionIsMcvDeployPending(*findObjectById(s,id))) return fail("pending turn authority");
        const auto occupancy=buildRuntimeOccupancyLookup(s); const auto top=cornerFor(center,families[0]);
        const auto pass=[&](RuntimeCellPoint cell,int moverId) {const auto* a=findObjectById(s,moverId);
            return a && runtimeCellIsPassable(s,s.terrainByCell,occupancy,cell,*a);};
        for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx) {
            const RuntimeCellPoint cell{top.x+dx,top.y+dy};
            if(pass(cell,footId) || !pass(cell,airId)) return fail("centered future foundation ground/air authority");
        }
        applySelection(s,{id},false,false);
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)) return fail("real Stop");
        const auto released=buildRuntimeOccupancyLookup(s);
        for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx) {
            const RuntimeCellPoint cell{top.x+dx,top.y+dy}; if(cell==center) continue;
            if(!runtimeCellIsPassable(s,s.terrainByCell,released,cell,*findObjectById(s,footId))) return fail("Stop retained future footprint");
        }
        if(home(s,"GACNST") || !home(s,"AMCV")) return fail("Stop transformed pending actor");
        cases << label << ',' << hz << ",AMCV,pending-ground-air-stop,0,PASS\n"; ++sceneCount;
    }
    for(int n=0;n<3;++n) {
        const auto& family=families[n]; label=std::string(family.mobile)+"-startup"; stage="original-starting-base";
        auto box=make(30,family.country); if(!box) return fail("fixture"); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint corner{28,28}, anchor{29,29};
        auto* base=spawnRuntimeSkirmishBase(s,family.country,family.structure,corner);
        if(!base || base->type!=family.mobile || runtimeCellPointForObject(*base)!=anchor) return fail("corner placement to actual mobile anchor");
        const int baseId=base->instanceId;
        const int count=spawnRuntimeSkirmishStartingUnits(s,family.country,family.country,anchor,6);
        if(count!=6) return fail("original starting troop count"); int actual=0;
        for(const auto& o:s.world.getObjects()) if(o.alive && o.owner==family.country && o.instanceId!=baseId) {
            ++actual; record(s,o,anchor,4,4);
            if(o.x>=corner.x && o.x<corner.x+4 && o.y>=corner.y && o.y<corner.y+4) return fail("starting troop inside actual future centered foundation");
        }
        base=findMutableObjectById(s,baseId); const auto item=runtimeBuildItemForBuilding(s,family.structure);
        if(actual!=6 || !base || !item || !runtimeCellCanPlaceBuilding(s,*item,corner,baseId,nullptr,family.country)) return fail("starting count/immediate footprint legality");
        if(!input(s,baseId,false) || !wait(s,30*15,[&](){return checkBuilding(s,family,anchor);})) return fail("actual opening deploy center");
        cases << label << ",30," << family.mobile << ",original-corner-start-six-troops,0,PASS\n"; ++sceneCount;
    }
    if(sceneCount!=43 || cycles!=16 || saves!=6 || paired!=36 || images!=3) return fail("finite matrix cardinality");
    std::cout << "Deploy center: passed cases=" << sceneCount << " realCycles=" << cycles
        << " physicalIni=" << saves << " pairedLiveTicks=" << paired << " bitmaps=" << images
        << " originalFoundation4x4/2x2 integerCenter=yes collision/lateTurn/air/Stop/startup=yes\n";
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
