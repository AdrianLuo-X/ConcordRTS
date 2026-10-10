// Test-only candidate. Install through the issue-9 reviewed dispatcher proposal.
bool runRuntimeInfantrySubcellContinuitySmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Infantry continuity: " + why; return false; };
    const char* phaseValue = std::getenv("CONCORD_INFANTRY_CONTINUITY_PHASE");
    const std::string phase = phaseValue && *phaseValue ? phaseValue : "after";
    if (phase != "before" && phase != "after") return fail("phase must be exactly before or after");
    const bool before = phase == "before";
    const char* requested = std::getenv("CONCORD_INFANTRY_SUBCELL_CONTINUITY_OUTPUT");
    const std::filesystem::path root = requested && *requested ? std::filesystem::path(requested)
        : std::filesystem::path(config.localDataRootPath) / "issue23-infantry-continuity" / phase;
    std::filesystem::create_directories(root);
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original startup/resources");
    IniFile authored; if (!authored.loadFromFile(assets->report.rulesPath)) return fail("original rule file");
    std::ofstream inventory(root / "catalogue.csv"), rows(root / "motion.csv"), cases(root / "cases.csv");
    if (!inventory || !rows || !cases) return fail("evidence files");
    inventory << "type,speed,speed_type,movement_zone,locomotor,jumpjet,slaved,deployer,mode,chrono,walking,fixture_control_state\n";
    rows << "scene,tick,event,logical_x,logical_y,slot,base_x,base_y,body_x,body_y,active,remaining,total,facing,animation_distance,anchor_x,anchor_y,projected_x,projected_y,body_delta,from_x,from_y,to_x,to_y,from_offset_x,from_offset_y,to_offset_x,to_offset_y,actual_path\n";
    cases << "scene,type,hz,initial_slot,final_slot,changed_slots,retained,maximum_body_delta,arrival,save_pairs,paired_ticks,pixel_frames\n";
    rows << std::setprecision(17); cases << std::setprecision(17);
    const auto offset = [](int slot) -> std::array<double,2> {
        if (slot == 2) return {0.25,-0.25};
        if (slot == 4) return {0.25,0.25};
        if (slot == 3) return {-0.25,0.25};
        return {0.0,0.0};
    };
    const auto body = [&](const WorldObjectInstance& o) {
        const auto d = offset(o.subCell);
        return std::array<double,2>{(o.hasRenderPosition ? o.renderX : double(o.x))+d[0],
            (o.hasRenderPosition ? o.renderY : double(o.y))+d[1]};
    };
    const auto equalPoint = [](const auto& a, const auto& b) {
        return std::abs(a[0]-b[0]) <= 1e-9 && std::abs(a[1]-b[1]) <= 1e-9;
    };
    int catalogue=0, walking=0, air=0, naval=0, chronoCount=0, amphibious=0;
    for (const auto& type : assets->rules.getInfantryTypes()) {
        ++catalogue; const auto mode=runtimeUnitMovementModeProfile(type);
        const bool chrono=toLowerAsciiLocal(type.locomotor)=="{4a582747-9839-11d1-b709-00a024ddafd1}";
        const bool ordinary=mode!=PathMovementMode::Air && mode!=PathMovementMode::Naval && !chrono;
        walking+=ordinary; air+=mode==PathMovementMode::Air; naval+=mode==PathMovementMode::Naval;
        chronoCount+=chrono; amphibious+=mode==PathMovementMode::Amphibious;
        inventory<<type.id<<','<<type.speed<<','<<type.speedType<<','<<type.movementZone<<','<<type.locomotor
            <<','<<type.jumpJet<<','<<authored.getValue(type.id,"Slaved","")<<','<<type.deployer<<','<<int(mode)
            <<','<<chrono<<','<<ordinary<<','<<(type.id=="SLAV"?"liberated-no-host":"original-mode")<<'\n';
    }
    if(catalogue!=65 || walking!=59 || air!=2 || naval!=1 || chronoCount!=3 || amphibious!=3)
        return fail("actual complete catalogue classification changed: "+std::to_string(catalogue)+"/"+std::to_string(walking));
    int completed=0, slotViolations=0, ordinarySaves=0, canonicalSaves=0, paired=0, frames=0, stops=0;
    std::set<std::string> completedLabels;
    for(const auto& type:assets->rules.getInfantryTypes()) {
        const auto mode=runtimeUnitMovementModeProfile(type);
        if(mode==PathMovementMode::Air || mode==PathMovementMode::Naval
            || toLowerAsciiLocal(type.locomotor)=="{4a582747-9839-11d1-b709-00a024ddafd1}") continue;
        for(int hz:{15,30,60}) for(int initial:{2,4,3}) {
            const std::string scene=type.id+"-"+std::to_string(hz)+"-slot"+std::to_string(initial);
            auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
            s.config=config; s.config.fixedTickHz=hz; s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
            s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
            s.config.crates=s.config.superweapons=s.config.shortGame=false;
            s.rules=assets->rules; s.art=assets->art; s.theater=assets->theater; s.report=assets->report;
            s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=s.runtimeSoundsEnabled=false;
            s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
            s.economy.owner="Americans"; s.economy.credits=10000; s.economy.skirmishStartApplied=true;
            s.triggers.bootstrapComplete=true; initializeRuntimeLocalProductionLanes(s.economy);
            RuntimeHumanHouseAuthority authority; authority.owner="Americans"; authority.canonicalOwner="americans";
            authority.localFacade=true; s.humanHouseAuthorities.push_back(std::move(authority));
            for(int y=1;y<=24;++y) for(int x=1;x<=24;++x) {
                TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=cell.subTileIndex=0;
                s.world.accessTerrainCells().push_back(cell);
            }
            auto* current=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type.id,{5,12});
            if(!current)return fail(scene+" actual stock actor creation");
            const int id=current->instanceId; current->subCell=initial;
            current->facing=runtimeFacingFromDelta(1,0); current->harvestAutoSearchSuspended=true;
            initializeRuntimeNavigation(s);
            current=findMutableObjectById(s,id);
            if(!current || current->subCell!=initial)return fail(scene+" initial legal canonical slot not retained");
            // Stock SLAV stays Slaved=yes. This instance has no registered host;
            // its player Move is the already-liberated/unattached control case.
            if(type.id=="SLAV" && (runtimeObjectIsAttachedSlaveWorker(s,*current)||s.slaveMinerTasks.contains(id)))
                return fail(scene+" SLAV is attached instead of liberated/no-host");
            const auto origin=body(*current);
            const int speedLeptons=std::min(255,std::clamp(type.speed,0,100)*256/100);
            if(speedLeptons<=0)return fail(scene+" nonpositive original walking speed");
            // Independent retail quantum with integer segment-clock rounding.
            // A legal slot fallback can add lateral distance; ordinary after
            // straight walking is checked against the tighter one-quantum bound.
            const double normalTickBound=1.0/std::max(1.0,std::floor(256.0*hz/(15.0*speedLeptons)));
            RuntimeSnapshotActiveStateScope scope(s);
            auto context=runtimeTerrainMetadataContext(s); context.rules=&s.rules; context.art=&s.art;
            context.palettePath=runtimePalettePath(config,s.report.map);
            context.structurePalettePath=runtimeStructurePalettePath(config,s.report.map);
            context.structureRootPath=runtimeStructureRootPath(config,s.report.map); context.shadowsEnabled=false;
            const RECT viewport{0,0,640,480}; s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
            s.camera={};s.camera.initialized=true;s.camera.tileWidth=60;s.camera.tileHeight=30;
            s.camera.worldOffsetX=int(std::llround((5.0-12.0)*30))-320;
            s.camera.worldOffsetY=int(std::llround((5.0+12.0)*15))-240;
            ensureRuntimeCameraForViewport(s,viewport);
            RuntimeCellPoint goal{8,12};
            int changes=0, localPairs=0, localFrames=0, moved=0; double maximum=0;
            bool stopped=false, saved=false, arrived=false;
            std::unique_ptr<RuntimeWindowState> normal, exact;
            const auto checkAnchor=[&](const WorldObjectInstance& o,const std::string& event,double delta) {
                const auto p=body(o);const double level=(o.renderMoveActive||worldObjectHasStoppedGroundPose(o))?o.renderLevel:double(o.level);
                const POINT expected{viewport.left+int(std::llround((p[0]-p[1])*s.camera.tileWidth/2.0))-s.camera.worldOffsetX,
                    viewport.top+int(std::llround((p[0]+p[1]-level)*s.camera.tileHeight/2.0))-s.camera.worldOffsetY};
                context.animationTick=s.tick;
                const auto actual=runtimeObjectWorldRenderPlacement(o,s.world,viewport,context,s.camera,s.renderCache).center;
                const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
                const auto it=std::find_if(projected.begin(),projected.end(),[&](const auto& p){return p.object&&p.object->instanceId==id;});
                if(it==projected.end() || expected.x!=actual.x || expected.y!=actual.y
                    || it->renderCenter.x!=expected.x || it->renderCenter.y!=expected.y)return false;
                const auto bi=s.behaviors.find(id); const int rem=bi==s.behaviors.end()?0:bi->second.renderMoveTicksRemaining;
                const int total=bi==s.behaviors.end()?0:bi->second.renderMoveTicksTotal;
                const auto* segment=bi==s.behaviors.end()?nullptr:&bi->second;
                std::string actualPath;if(segment)for(const auto& cell:segment->path)
                    actualPath+=std::to_string(cell.x)+":"+std::to_string(cell.y)+"|";
                rows<<scene<<','<<s.tick<<','<<event<<','<<o.x<<','<<o.y<<','<<o.subCell<<','
                    <<(o.hasRenderPosition?o.renderX:double(o.x))<<','<<(o.hasRenderPosition?o.renderY:double(o.y))<<','
                    <<p[0]<<','<<p[1]<<','<<o.renderMoveActive<<','<<rem<<','<<total<<','<<o.facing<<','
                    <<o.movementAnimationDistance<<','<<actual.x<<','<<actual.y<<','<<it->renderCenter.x<<','<<it->renderCenter.y<<','<<delta
                    <<','<<(segment?segment->renderFrom.x:-1)<<','<<(segment?segment->renderFrom.y:-1)
                    <<','<<(segment?segment->renderTo.x:-1)<<','<<(segment?segment->renderTo.y:-1)
                    <<','<<(segment?segment->renderFromOffsetX:0.0)<<','<<(segment?segment->renderFromOffsetY:0.0)
                    <<','<<(segment?segment->renderToOffsetX:0.0)<<','<<(segment?segment->renderToOffsetY:0.0)<<','<<actualPath<<'\n';
                return true;
            };
            const auto command=[&](RuntimeCellPoint target) {
                const auto old=*findObjectById(s,id); applySelection(s,{id},false,false); issueRuntimeMoveCommand(s,target);
                current=findMutableObjectById(s,id);
                return s.lastCommand.acceptedCount==1 && current && equalPoint(body(old),body(*current))
                    && old.facing==current->facing && old.movementAnimationDistance==current->movementAnimationDistance;
            };
            if(!command(goal)||!checkAnchor(*current,"command",0))return fail(scene+" real Move/anchor changed body before a tick");
            for(int n=0;n<hz*40 && !arrived;++n) {
                const auto old=*findObjectById(s,id); const auto previous=body(old);
                const auto oldIt=s.behaviors.find(id); const bool active=oldIt!=s.behaviors.end()&&oldIt->second.renderMoveTicksRemaining>0;
                RuntimeObjectBehavior oldBehavior; if(active)oldBehavior=oldIt->second;
                ++s.tick; advanceRuntimeSimulation(s); current=findMutableObjectById(s,id);
                if(!current||!current->alive)return fail(scene+" actual actor vanished");
                const auto p=body(*current);const double delta=std::hypot(p[0]-previous[0],p[1]-previous[1]);maximum=std::max(maximum,delta);
                if(delta>2.0*normalTickBound+1e-9)return fail(scene+" body exceeded original speed/slot-transition envelope "+std::to_string(delta));
                if(current->movementAnimationDistance+1e-9<old.movementAnimationDistance)
                    return fail(scene+" walking animation distance reset at a cell/slot boundary");
                if(active) {
                    const auto now=s.behaviors.find(id);
                    const bool sameSegment=now!=s.behaviors.end()
                        &&now->second.renderFrom==oldBehavior.renderFrom&&now->second.renderTo==oldBehavior.renderTo
                        &&now->second.renderFromOffsetX==oldBehavior.renderFromOffsetX
                        &&now->second.renderFromOffsetY==oldBehavior.renderFromOffsetY
                        &&now->second.renderToOffsetX==oldBehavior.renderToOffsetX
                        &&now->second.renderToOffsetY==oldBehavior.renderToOffsetY
                        &&now->second.renderMoveTicksTotal==oldBehavior.renderMoveTicksTotal;
                    const auto d=offset(old.subCell);
                    const std::array<double,2> endpoint{oldBehavior.renderTo.x+oldBehavior.renderToOffsetX+d[0],
                        oldBehavior.renderTo.y+oldBehavior.renderToOffsetY+d[1]};
                    if(sameSegment) {
                        const int remaining=now->second.renderMoveTicksRemaining;
                        if(remaining!=oldBehavior.renderMoveTicksRemaining&&remaining!=oldBehavior.renderMoveTicksRemaining-1)
                            return fail(scene+" active clock advanced more than one actual tick");
                        if(remaining==oldBehavior.renderMoveTicksRemaining&&!equalPoint(previous,p))
                            return fail(scene+" paused active clock moved the body");
                        if(remaining==0&&!equalPoint(endpoint,p))return fail(scene+" accepted segment missed its true endpoint");
                    } else if(oldBehavior.renderMoveTicksRemaining!=1||!equalPoint(endpoint,p))
                        return fail(scene+" new segment committed before the preceding true endpoint");
                } else if(!equalPoint(previous,p))return fail(scene+" logical commit moved body before render clock");
                if(!before&&!stopped) {
                    if(std::abs(p[1]-origin[1])>1e-9||p[0]<previous[0]-1e-9
                        ||p[0]<origin[0]-1e-9||p[0]>origin[0]+3.0+1e-9||delta>normalTickBound+1e-9)
                        return fail(scene+" straight empty-cell walk left its fixed station line/original speed");
                }
                if(current->subCell!=old.subCell)++changes;
                if(current->subCell!=initial && !before)return fail(scene+" legal empty destination changed preferred slot");
                if(!checkAnchor(*current,"tick",delta))return fail(scene+" same-frame renderer/interaction disagreed with true body");
                if(delta>1e-9)++moved;
                if(type.id=="E1"&&initial==3 && !stopped && moved>=3 && current->renderMoveActive) {
                    const auto resting=*current;const auto here=body(resting);
                    applySelection(s,{id},false,false);issueRuntimeStopCommand(s);current=findMutableObjectById(s,id);
                    if(!current||!equalPoint(here,body(*current))||current->renderMoveActive)return fail(scene+" real Stop changed body");
                    initializeRuntimeNavigation(s);current=findMutableObjectById(s,id);
                    if(!current||!equalPoint(here,body(*current)))return fail(scene+" nav rebuild changed stopped body");
                    if(!checkAnchor(*current,"stop-nav",0))return fail(scene+" stopped/nav same-frame anchor disagreed");
                    goal={8,14};if(!command(goal)||!checkAnchor(*current,"turn-command",0))
                        return fail(scene+" turn/resume command changed stopped body/anchor");
                    stopped=true;++stops;
                }
                if(type.id=="E1"&&hz==30&&!saved&&moved>=4&&current->renderMoveActive) {
                    std::ostringstream ordinary,extension,canonical;IniFile baseIni,exactIni;
                    if(!writeRuntimeStateToStream(s,ordinary,error)||!writeRuntimeStateToStream(s,extension,error)
                        ||!writeRuntimeMultiplayerSnapshotExtension(s,extension,error)||!exactIni.loadFromString(extension.str())
                        ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni,canonical,error))return fail(scene+" writer: "+error);
                    for(int f=0;f<2;++f){std::ofstream out(root/(scene+(f?"-canonical.ini":"-ordinary14.ini")),std::ios::binary);
                        out<<(f?canonical.str():ordinary.str());out.close();if(!out.good())return fail(scene+" actual checkpoint write");}
                    if(!baseIni.loadFromFile((root/(scene+"-ordinary14.ini")).string())
                        ||!exactIni.loadFromFile((root/(scene+"-canonical.ini")).string()))return fail(scene+" actual checkpoint read");
                    const auto restore=[&](bool precise){auto q=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*q,s,false);
                        if(!materializeRuntimeLoadStateFromIni(*q,baseIni,error)
                            ||(precise&&!materializeRuntimeMultiplayerSnapshotExtension(*q,exactIni,error)))return std::unique_ptr<RuntimeWindowState>{};
                        // seedRuntimeLoadScratch preserves the immutable actual
                        // fixture world. The ordinary loader must not replace it
                        // with the map path recorded by the startup report.
                        const auto& sourceCells=s.world.getTerrainCells();const auto& restoredCells=q->world.getTerrainCells();
                        if(sourceCells.size()!=restoredCells.size()){error="restored terrain cell count";return std::unique_ptr<RuntimeWindowState>{};}
                        for(std::size_t ci=0;ci<sourceCells.size();++ci){const auto& a=sourceCells[ci];const auto& b=restoredCells[ci];
                            if(a.x!=b.x||a.y!=b.y||a.cellIndex512!=b.cellIndex512||a.tileIndex!=b.tileIndex
                                ||a.subTileIndex!=b.subTileIndex||a.level!=b.level){error="restored immutable fixture terrain";return std::unique_ptr<RuntimeWindowState>{};}}
                        return q;};
                    normal=restore(false);exact=restore(true);if(!normal||!exact)return fail(scene+" reader: "+error);
                    for(auto* q:{normal.get(),exact.get()}) {const auto* clone=findObjectById(*q,id);
                        if(!clone||clone->subCell!=current->subCell||!equalPoint(body(*clone),p))return fail(scene+" initial file restore changed body/slot");}
                    saved=true;++ordinarySaves;++canonicalSaves;
                } else if(normal && localPairs<12) {
                    for(auto* q:{normal.get(),exact.get()}) {
                        {RuntimeSnapshotActiveStateScope cloneScope(*q);++q->tick;advanceRuntimeSimulation(*q);}
                        const auto* clone=findObjectById(*q,id);
                        if(!clone||clone->subCell!=current->subCell||!equalPoint(body(*clone),body(*current))
                            ||clone->facing!=current->facing||std::abs(clone->movementAnimationDistance-current->movementAnimationDistance)>1e-9)
                            return fail(scene+" actual saved-vs-live continuation diverged");
                    }
                    ++localPairs;++paired;
                }
                if(type.id=="E1"&&initial==3&&(moved==2||stopped&&localFrames==1)&&localFrames<2) {
                    RuntimeMobileSpriteBoundsProbe probe;
                    const auto center=runtimeObjectBodyAnchorForRender(*current,viewport,context,s.camera);
                    if(!verifyRuntimeMobileObjectSpriteBounds(*current,center,context,s.camera,s.renderCache,probe)
                        ||probe.renderedPixels<=0||probe.pixels.empty())return fail(scene+" actual original SHP unavailable");
                    if(!writeRuntimeSmokeBmp((root/(scene+"-frame"+std::to_string(localFrames)+".bmp")).string(),
                        probe.pixels.data(),probe.imageWidth,probe.imageHeight,error))return fail(scene+" actual SHP BMP: "+error);
                    ++localFrames;++frames;
                }
                arrived=current->x==goal.x&&current->y==goal.y&&!current->renderMoveActive&&s.behaviors.find(id)==s.behaviors.end();
            }
            if(!arrived||moved<3)return fail(scene+" arrival/actual movement timed out");
            const auto settled=offset(current->subCell);
            if(!equalPoint(body(*current),std::array<double,2>{goal.x+settled[0],goal.y+settled[1]}))
                return fail(scene+" actual arrival body did not settle at its assigned slot endpoint");
            if(type.id=="E1"&&hz==30&&(!saved||localPairs!=12))return fail(scene+" real save/12paired ticks incomplete");
            const bool retained=changes==0&&current->subCell==initial;slotViolations+=!retained;
            cases<<scene<<','<<type.id<<','<<hz<<','<<initial<<','<<current->subCell<<','<<changes<<','<<retained
                <<','<<maximum<<",1,"<<saved<<','<<localPairs<<','<<localFrames<<'\n';
            if(!completedLabels.insert(scene).second)return fail("duplicate expected scene "+scene);++completed;
        }
    }
    if(completed!=531||completedLabels.size()!=531||ordinarySaves!=3||canonicalSaves!=3||paired!=36||stops!=3||frames!=6)
        return fail("incomplete 531 scenes or 3+3 actual saves/36pairs/3Stop/6SHP frames");
    std::cout<<"Infantry continuity: phase="<<phase<<" cases="<<completed<<" catalogue="<<catalogue
        <<" walking="<<walking<<" slotViolations="<<slotViolations<<" ordinary="<<ordinarySaves<<" canonical="<<canonicalSaves
        <<" paired="<<paired<<" frames="<<frames<<'\n';
    if(slotViolations)return fail("complete 531-case evidence retained; legal slot violations="+std::to_string(slotViolations));
    return true;
}
