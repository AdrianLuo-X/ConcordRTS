bool runRuntimeBuildupSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Buildup: " + why; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    rebuildRuntimeTerrainInfoCache(*stock);
    const TerrainCell* land = nullptr;
    for (const auto& cell : stock->world.getTerrainCells()) {
        const auto& info = stock->terrainInfoByCell.at(runtimeCellKey(cell.x, cell.y));
        if (!info.water && !info.shore && !info.ramp && !info.bridge && info.passable) { land = &cell; break; }
    }
    if (!land) return fail("no original flat terrain");
    const char* outputRoot = std::getenv("CONCORD_BUILDUP_TIME_DIR");
    const std::string root = outputRoot && *outputRoot ? outputRoot
        : "build/smoke/feature-checks/09-buildup-time/checks";
    std::filesystem::create_directories(root);
    std::ofstream records(root + "/buildup-frames.csv");
    records << "type,hz,source_frames,legacy_delay,total_ticks,elapsed_ticks,visible_frame,health,mission\n";
    // Only GAGATE_A is registered in the shipped YR rules. The other
    // three retained ART gate entries need explicit test-only types.
    IniFile gateRules, gateTypes;
    if (!gateRules.loadFromFile("local_data/rulesmd.ini")
        || !gateTypes.loadFromString("[BuildingTypes]\nQ13GateB=GAGATE_B\nQ13NodGateA=NAGATE_A\nQ13NodGateB=NAGATE_B\n"
            "[GAGATE_B]\nImage=GAGATE_B\nStrength=100\nGate=yes\n"
            "[NAGATE_A]\nImage=NAGATE_A\nStrength=100\nGate=yes\n"
            "[NAGATE_B]\nImage=NAGATE_B\nStrength=100\nGate=yes\n")) return fail("gate rule fixture");
    gateRules.appendFrom(gateTypes);
    RulesDatabase gateFixtureRules;
    if (!gateFixtureRules.loadFromIni(gateRules)) return fail("gate type registration");
    const auto make = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.config.playerCountry = "Americans"; s.config.computerAiEnabled = false;
        s.config.noCooldown = s.config.crates = s.config.multiplayerActive = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater; s.report = stock->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "BuildupProbe"; s.economy.credits = 100000; s.economy.techLevel = 10;
        s.triggers.bootstrapComplete = true; s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        for (int y=1; y<=48; ++y) for (int x=1; x<=48; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=land->tileIndex; cell.subTileIndex=land->subTileIndex;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        return box;
    };
    const auto overrideRules = [&](RuntimeWindowState& s, const std::string& value) {
        IniFile ini, extra;
        if (!ini.loadFromFile("local_data/rulesmd.ini")
                || !extra.loadFromString("[General]\nBuildupTime=" + value + "\n")) return false;
        ini.appendFrom(extra); return s.rules.loadFromIni(ini);
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, RuntimeCellPoint cell) {
        return addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Building,type,cell);
    };
    int cases=0;
    const auto runTiming = [&](const char* type, int hz, const char* overrideValue = nullptr, int gateStagesOverride = -1) {
        auto box=make(hz); auto& s=*box;
        if (std::string(type)=="GAGATE_B" || std::string(type).rfind("NAGATE",0)==0) s.rules=gateFixtureRules;
        if (overrideValue && !overrideRules(s,overrideValue)) return fail("rule override failed");
        if (gateStagesOverride >= 0) {
            IniFile ini,extra;
            if(!ini.loadFromFile("local_data/artmd.ini")
                ||!extra.loadFromString(std::string("[")+type+"]\nGateStages="+std::to_string(gateStagesOverride)+"\n"))
                return fail("gate stage override fixture");
            ini.appendFrom(extra);if(!s.art.loadFromIni(ini))return fail("gate stage override");
        }
        auto* object=add(s,type,{20,20}); if (!object) return fail(std::string(type)+" creation failed");
        const int id=object->instanceId;
        const auto* building=findRuntimeBuildingType(s.rules,*object);
        if (building && building->wall) {
            if (object->buildAnimTicksTotal || object->buildAnimTicksRemaining) return fail("wall gained buildup");
            ++cases; return true;
        }
        std::string source;
        const auto timing=runtimeStructureBuildupTiming(s,*object,&source);
        // YR retains NAGATE ART declarations but the configured original
        // resource set has no matching SHP. Verify the explicit missing-art
        // boundary, without claiming that absent art has been rendered.
        if(std::string(type).rfind("NAGATE",0)==0 && timing.frames==0 && source.empty()) {
            if(object->buildAnimTicksTotal!=hz/15)return fail("unavailable retained gate art boundary");
            for(int i=0;i<hz/15;++i){++s.tick;advanceRuntimeBuildAnimations(s);}
            object=findMutableObjectById(s,id);
            if(!object||object->buildAnimTicksRemaining||object->mission!="Guard")return fail("retained gate stuck without art");
            std::cout<<"Buildup "<<type<<" hz="<<hz<<" missing retained stock SHP; fallback boundary="<<hz/15<<'\n';
            ++cases;return true;
        }
        if (timing.frames<2 || source.empty()) return fail(std::string(type)+" original buildup was not resolved");
        if ((std::string(type).rfind("GAGATE",0)==0 && timing.frames!=(gateStagesOverride>=0?gateStagesOverride+1:10))
                || (std::string(type).rfind("NAGATE",0)==0 && timing.frames!=7))
            return fail(std::string(type)+" gate stage range changed: "+std::to_string(timing.frames)+" source="+source);
        const int delay=static_cast<int>(s.rules.getProductionRules().buildupTime*900.0/timing.frames);
        const int expected=std::max(1,timing.frames*delay)*(hz/15);
        if (object->buildAnimTicksTotal!=expected || expected<=0) return fail(std::string(type)+" wrong total");
        const int initialHealth=object->health;
        for (int elapsed=0; elapsed<=expected; ++elapsed) {
            object=findMutableObjectById(s,id);
            if (!object || object->buildAnimTicksRemaining!=expected-elapsed) return fail("timer skipped or ended early");
            if (elapsed<expected) {
                if (object->mission!="Construction") return fail("construction mission ended early");
                const int frame=runtimeBuildupFrameAtProgress(timing.frames,object->buildAnimTicksTotal,
                    object->buildAnimTicksRemaining,hz,building && building->gate);
                const int expectedStage=delay>0 ? elapsed/(delay*(hz/15)) : 0;
                const int expectedFrame=building && building->gate ? timing.frames-1-expectedStage : expectedStage;
                if (frame!=expectedFrame) return fail("frame cadence differs from the original stage timer");
                if (elapsed==expected/2) object->health=std::max(1,initialHealth/3);
                if (elapsed==0 || elapsed==expected/2 || elapsed==expected-1 || elapsed%(hz/15)==0)
                    records<<type<<','<<hz<<','<<timing.frames<<','<<delay<<','<<expected<<','<<elapsed<<','
                        <<frame<<','<<object->health<<','<<object->mission<<'\n';
            } else if (object->buildAnimTicksTotal!=0 || object->mission!="Guard") return fail("completion did not enter Guard");
            if (elapsed<expected) { ++s.tick; advanceRuntimeBuildAnimations(s); }
        }
        std::cout<<"Buildup "<<type<<" hz="<<hz<<" frames="<<timing.frames<<" delay="<<delay
            <<" ticks="<<expected<<" damageKeepsPhase=yes source="<<source<<'\n';
        ++cases; return true;
    };
    for (int hz : {30,60}) for (const char* type : {"GAPOWR","NAPOWR","YAPOWR",
            "GAPILE","NAHAND","YABRCK","GAREFN","NAREFN","YAREFN","GAWEAP","NAWEAP","YAWEAP",
            "GACNST","NACNST","YACNST","GTGCAN","NANRCT","GAGATE_A","GAGATE_B","NAGATE_A","NAGATE_B",
            "GAWALL","NAWALL"}) if (!runTiming(type,hz)) return false;
    for (int hz : {30,60}) for (const char* value : {"0.12","0.001","0"})
        if (!runTiming("GAPOWR",hz,value)) return false;
    for (int hz : {30,60}) if (!runTiming("GAGATE_A",hz,nullptr,6)) return false;
    {
        auto box=make(30); auto& s=*box;
        const auto stockHash=s.rules.getDetailRulesHash();
        const double speed=s.rules.getProductionRules().buildSpeed;
        if (!overrideRules(s,"0.12") || s.rules.getDetailRulesHash()==stockHash
                || s.rules.getProductionRules().buildSpeed!=speed) return fail("rule hash or sidebar timing isolation");
        for (const char* value : {"-1","nan","101"}) {
            if (!overrideRules(s,value) || s.rules.getProductionRules().buildupTime!=0.05) return fail("invalid fallback");
        }
        IniFile empty; empty.loadFromString("[General]\n"); RulesDatabase defaults;
        if (!defaults.loadFromIni(empty) || defaults.getProductionRules().buildupTime!=0.05) return fail("retail constructor default");
        ++cases;
    }
    for (int hz : {30,60}) {
        auto box=make(hz); auto& s=*box;
        auto* first=add(s,"GAPOWR",{20,20}); if (!first) return fail("concurrent setup");
        const int firstId=first->instanceId, firstTotal=first->buildAnimTicksTotal;
        for (int i=0;i<hz/3;++i) {++s.tick;advanceRuntimeSimulation(s);}
        s.config.noCooldown=true;
        auto* second=add(s,"GAPOWR",{28,20}); if (!second) return fail("second concurrent buildup");
        const int secondId=second->instanceId;
        if (second->buildAnimTicksTotal!=firstTotal) return fail("no-cooldown altered buildup");
        s.config.noCooldown=false;
        ++s.tick;advanceRuntimeSimulation(s);
        const std::string save=root+"/concurrent-"+std::to_string(hz)+".ini";
        if (!saveRuntimeState(s,save,error)) return fail("save "+error);
        auto restored=make(hz);
        if (!loadRuntimeState(*restored,save,error)) return fail("load "+error);
        for (int i=0;i<firstTotal+2;++i) {
            ++s.tick;advanceRuntimeSimulation(s); ++restored->tick;advanceRuntimeSimulation(*restored);
            for (int id : {firstId,secondId}) {
                const auto* a=findObjectById(s,id);const auto* b=findObjectById(*restored,id);
                if (!a || !b || a->buildAnimTicksTotal!=b->buildAnimTicksTotal
                        || a->buildAnimTicksRemaining!=b->buildAnimTicksRemaining || a->mission!=b->mission)
                    return fail("saved phase or completion diverged");
            }
        }
        const auto* art=runtimeArtTypeForObject(s,*findObjectById(s,firstId));
        if (art && art->freeBuildup) for (const auto& [key,entry] : s.renderCache.auxiliaryShpSpritesByImage)
            if (key.rfind(toLowerAsciiLocal(art->buildup)+"|",0)==0) return fail("completed concurrent buildup stayed cached");
        ++cases;
        auto* sale=add(s,"GAPOWR",{30,30});if(!sale)return fail("sale fixture");
        const int saleId=sale->instanceId, money=s.economy.credits;
        if (!sellRuntimeStructure(s,saleId) || findObjectById(s,saleId) || s.economy.credits<=money)
            return fail("existing sell cleanup/refund regressed");
        // Logical removal/refund is immediate; reverse playback is covered
        // separately by RuntimeBuildingSaleSmoke, including unfinished builds.
        ++cases;
    }
    for (const char* unitId : {"AMCV","SMCV","PCV"}) {
        auto box=make(30);auto& s=*box;
        s.config.mcvRepacks=true;
        auto* unit=addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Unit,unitId,{20,20});
        if(!unit)return fail("MCV fixture");const int oldId=unit->instanceId;
        if(!deployRuntimeMcv(s,oldId,false))return fail(std::string(unitId)+" deploy failed: "+s.economy.status);
        int yardId=-1;
        for(const auto& object:s.world.getObjects())if(object.alive&&object.kind==WorldObjectKind::Structure)yardId=object.instanceId;
        const auto* yard=findObjectById(s,yardId);
        if(!yard||findObjectById(s,oldId)||runtimeStructureCanRepack(s,*yard))return fail("MCV transition/repack gate");
        const int total=yard->buildAnimTicksTotal;
        for(int i=0;i<total;++i){++s.tick;advanceRuntimeBuildAnimations(s);}
        yard=findObjectById(s,yardId);
        if(!yard||!runtimeStructureCanRepack(s,*yard))return fail("completed MCV cannot repack");
        ++cases;
    }
    // Probe the real drawing path and its decoded original frame, rather
    // than inferring rendering solely from the simulation timer.
    std::ofstream renderRecords(root+"/render-frames.csv");
    renderRecords<<"type,hz,elapsed,total,expected_frame,decoded,drawn,colored_pixels\n";
    for(int hz : {30,60}) for(const char* type : {"GAPOWR","GAWEAP","GAGATE_A","GAGATE_B"}) {
        auto box=make(hz);auto& s=*box;
        if(std::string(type)=="GAGATE_B")s.rules=gateFixtureRules;
        auto* placed=add(s,type,{20,20});if(!placed)return fail("render fixture");
        auto object=*placed;
        object.x=object.y=object.cell=0;clearRuntimeRenderPosition(object);
        const auto timing=runtimeStructureBuildupTiming(s,object);
        const auto* building=findRuntimeBuildingType(s.rules,object);
        const auto* art=runtimeArtTypeForObject(s,object);
        if(!building||!art)return fail("render type metadata");
        RuntimeMapRenderContext context;
        context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.fixedTickHz=hz;
        constexpr int width=256,height=224;
        constexpr std::uint32_t background=0xff243039U;
        std::vector<std::uint32_t> sheet(width*3*height,background);
        RuntimeMapCamera camera;camera.worldOffsetX=-width/2;camera.worldOffsetY=-128;
        int column=0;
        for(int elapsed : {0,timing.fixedTicks/2,timing.fixedTicks-1}) {
            std::vector<std::uint32_t> pixels(width*height,background);
            context.surfacePixels=pixels.data();context.surfaceWidth=width;context.surfaceHeight=height;
            object.buildAnimTicksRemaining=timing.fixedTicks-elapsed;
            s.renderCache.auxiliaryShpSpritesByImage.clear();
            const bool drawn=drawRuntimeObjectPreviewSprite(nullptr,RECT{0,0,width,height},object,
                context,camera,s.renderCache);
            const int stage=elapsed/(timing.legacyFrameDelay*(hz/15));
            const int expectedFrame=building->gate?timing.frames-1-stage:stage;
            const std::string prefix=toLowerAsciiLocal(art->buildup)+"|";
            bool decoded=false;
            for(const auto& [key,frame]:s.renderCache.auxiliaryShpSpritesByImage)
                if(key.rfind(prefix,0)==0&&frame.loaded&&frame.frameIndex==expectedFrame)decoded=true;
            const auto colored=std::count_if(pixels.begin(),pixels.end(),[&](auto p){return p!=background;});
            renderRecords<<type<<','<<hz<<','<<elapsed<<','<<timing.fixedTicks<<','<<expectedFrame
                <<','<<decoded<<','<<drawn<<','<<colored<<'\n';
            if(!decoded)return fail(std::string(type)+" renderer did not decode the expected buildup frame");
            if(column==1&&(!drawn||colored==0))return fail(std::string(type)+" middle buildup frame did not draw");
            for(int y=0;y<height;++y)std::copy_n(pixels.data()+y*width,width,
                sheet.data()+y*width*3+column*width);
            ++column;
        }
        if(!writeRuntimeSmokeBmp(root+"/render-"+type+"-"+std::to_string(hz)+".bmp",
            sheet.data(),width*3,height,error))return fail(error);
        ++cases;
    }
    for(int hz : {30,60}) for(const char* image : {"", "MISSING_BUILDUP_PROBE_13"}) {
        auto box=make(hz);auto& s=*box;
        IniFile ini,extra;
        if(!ini.loadFromFile("local_data/artmd.ini")
            ||!extra.loadFromString(std::string("[GAPOWR]\nBuildup=")+image+"\n"))return fail("missing art fixture");
        ini.appendFrom(extra);if(!s.art.loadFromIni(ini))return fail("missing art override");
        auto* object=add(s,"GAPOWR",{20,20});if(!object)return fail("missing art building");
        const int id=object->instanceId;
        if(object->buildAnimTicksTotal!=hz/15)return fail("missing art boundary");
        for(int i=0;i<hz/15;++i){++s.tick;advanceRuntimeBuildAnimations(s);}
        object=findMutableObjectById(s,id);
        if(!object||object->buildAnimTicksRemaining||object->mission!="Guard")return fail("missing art stuck in construction");
        ++cases;
    }
    // Record current facility registration during construction. This is a
    // characterization check; it does not assert retail Place() parity.
    std::ofstream facilities(root+"/facility-registration.csv");
    facilities<<"hz,phase,power,infantry_factory,vehicle_factory,radar,refineries,owned_tech\n";
    for(int hz : {30,60}) {
        auto box=make(hz);auto& s=*box;int x=4,maxTicks=0;
        for(const char* type:{"GAPOWR","GAPILE","GAREFN","GAWEAP","GAAIRC","GATECH"}) {
            auto* object=add(s,type,{x,20});if(!object)return fail("facility fixture");
            maxTicks=std::max(maxTicks,object->buildAnimTicksTotal);x+=7;
        }
        for(int phase=0;phase<2;++phase) {
            refreshRuntimeEconomyMetricsForCurrentOwner(s);
            facilities<<hz<<','<<(phase?"completed":"construction")<<','<<s.economy.powerProduced<<','
                <<runtimeHasOwnedFactory(s,"InfantryType")<<','<<runtimeHasOwnedFactory(s,"UnitType")<<','
                <<runtimeHasOwnedRadar(s)<<','<<s.economy.refineries<<','<<runtimeHasOwnedBuilding(s,"GATECH")<<'\n';
            if(s.economy.powerProduced<=0||!runtimeHasOwnedFactory(s,"InfantryType")
                ||!runtimeHasOwnedFactory(s,"UnitType")||!runtimeHasOwnedRadar(s)
                ||s.economy.refineries!=1||!runtimeHasOwnedBuilding(s,"GATECH"))return fail("facility registration changed");
            if(!phase)for(int i=0;i<maxTicks;++i){++s.tick;advanceRuntimeBuildAnimations(s);}
        }
        ++cases;
    }
    // Full world rendering must not add a separate head over the buildup.
    // Also exercise the placement/preview path, which shares the turret pass.
    int turretScenes=0,turretSaves=0;
    std::vector<std::string> turretTypes;
    for(const auto& type:stock->rules.getBuildingTypes())
        if(!type.turretAnim.empty())turretTypes.push_back(type.id);
    turretTypes.push_back("ATESLA");turretTypes.push_back("TESLA");
    std::ofstream turretRecords(root+"/defense-buildup.csv");
    turretRecords<<"type,hz,elapsed,total,turret_attempts,turret_drawn,active_layers\n";
    const auto makeDefense=[&](int hz) {
        auto box=make(hz);auto& s=*box;
        auto cells=std::move(s.world.accessTerrainCells());
        // Keep the real loaded map summary required by drawRuntimeMap,
        // then replace its live contents with the isolated flat fixture.
        s.world=stock->world;s.world.accessObjects().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells()=std::move(cells);
        initializeRuntimeNavigation(s);
        return box;
    };
    const auto expectedBuildupFacing=[](const std::string& type) {
        return type=="GTGCAN"||type=="NALASR"||type=="NASAM" ? 16 : 32;
    };
    for(int hz:{15,30,60})for(const auto& type:turretTypes) {
        auto box=makeDefense(hz);auto& s=*box;
        auto* placed=add(s,type,{20,20});if(!placed)return fail("defense creation "+type);
        const int id=placed->instanceId,total=placed->buildAnimTicksRemaining;
        if(total<=0)return fail("defense missing construction timer "+type);
        const auto* building=findRuntimeBuildingType(s.rules,*placed);
        const bool separateTurret=building&&!building->turretAnim.empty();
        const auto timing=runtimeStructureBuildupTiming(s,*placed);
        if(timing.frames>1 && timing.legacyFrameDelay>0
            && runtimeBuildupFrameAtProgress(timing.frames,total,1,hz)!=timing.frames-1)
            return fail("terminal construction frame was skipped: "+type);
        if(separateTurret && (placed->facing!=expectedBuildupFacing(type) || placed->turretFacing!=-1))
            return fail("new turret does not share authored buildup facing: "+type);
        RuntimeBuildCatalogItem previewItem;previewItem.kind=RuntimeBuildKind::Defense;previewItem.typeId=type;
        const auto placementPreview=makeRuntimePlacementPreviewObject(s,previewItem,{20,20});
        if(placementPreview.facing!=placed->facing)return fail("placement pose differs: "+type);
        RuntimeMapRenderContext context;
        context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.fixedTickHz=hz;
        constexpr int width=256,height=224;constexpr std::uint32_t background=0xff243039U;
        std::vector<std::uint32_t> sheet(width*4*height,background),pixels(width*height,background);
        RuntimeMapCamera camera;camera.worldOffsetX=-width/2;camera.worldOffsetY=-128;
        int column=0;
        for(int elapsed:{0,total/2,total-1,total}) {
            while(s.tick<elapsed){++s.tick;advanceRuntimeBuildAnimations(s);}
            const auto* object=findObjectById(s,id);
            if(!object||object->buildAnimTicksRemaining!=total-elapsed)return fail("defense timer "+type);
            const auto result=drawRuntimeStateOffscreenAtCell(s,{21,21},640,480,error,"",false,false);
            if(!result||!result->rendered)return fail("defense world render "+type+": "+error);
            turretRecords<<type<<','<<hz<<','<<elapsed<<','<<total<<','<<result->structureTurretAnimAttempts
                <<','<<result->structureTurretAnimDrawn<<','<<result->buildingArtAnimationLayers<<'\n';
            const int expected=elapsed==total&&separateTurret?1:0;
            if(result->structureTurretAnimAttempts!=expected||result->structureTurretAnimDrawn!=expected
                ||(elapsed<total&&result->buildingArtAnimationLayers!=0))
                return fail("defense head overlapped buildup or did not appear at completion: "+type
                    +" hz="+std::to_string(hz)+" elapsed="+std::to_string(elapsed)
                    +" attempts="+std::to_string(result->structureTurretAnimAttempts)
                    +" drawn="+std::to_string(result->structureTurretAnimDrawn));
            auto preview=*object;preview.x=preview.y=preview.cell=0;clearRuntimeRenderPosition(preview);
            std::fill(pixels.begin(),pixels.end(),background);
            context.surfacePixels=pixels.data();context.surfaceWidth=width;context.surfaceHeight=height;
            (void)drawRuntimeObjectPreviewSprite(nullptr,RECT{0,0,width,height},preview,context,camera,s.renderCache);
            for(int y=0;y<height;++y)std::copy_n(pixels.data()+y*width,width,
                sheet.data()+y*width*4+column*width);
            if(separateTurret&&(column==1||column==2)) {
                std::ostringstream text;if(!writeRuntimeStateToStream(s,text,error))return fail("defense save "+error);
                IniFile saved;if(!saved.loadFromString(text.str()))return fail("defense saved data");
                auto restored=makeDefense(hz);
                if(!materializeRuntimeLoadStateFromIni(*restored,saved,error))return fail("defense restore "+error);
                const auto* loaded=findObjectById(*restored,id);
                const auto hidden=drawRuntimeStateOffscreenAtCell(*restored,{21,21},640,480,error,"",false,false);
                if(!loaded||loaded->buildAnimTicksRemaining!=total-elapsed||!hidden
                    ||loaded->facing!=expectedBuildupFacing(type)||loaded->turretFacing!=-1
                    ||hidden->structureTurretAnimAttempts!=0||hidden->structureTurretAnimDrawn!=0)
                    return fail("restore exposed unfinished cannon");
                while(restored->tick<total){++restored->tick;advanceRuntimeBuildAnimations(*restored);}
                const auto complete=drawRuntimeStateOffscreenAtCell(*restored,{21,21},640,480,error,"",false,false);
                if(!complete||complete->structureTurretAnimDrawn!=1)return fail("restored cannon completion");
                ++turretSaves;
            }
            ++column;
        }
        if(hz==30&&!writeRuntimeSmokeBmp(root+"/defense-"+type+".bmp",sheet.data(),width*4,height,error))return fail(error);
        ++turretScenes;
        std::cout<<"Defense buildup: "<<type<<" hz="<<hz<<" total="<<total
            <<" head hidden until exact completion, world/preview PASS\n";
    }
    // A manual order during construction cannot pre-rotate the hidden turret.
    // Run the real combat path on both sides of the completion boundary.
    int constructionOrders=0;
    for(int hz:{15,30,60})for(const char* type:{"GTGCAN","NALASR","NASAM","NAFLAK","YAGGUN"}) {
        auto box=makeDefense(hz);auto& s=*box;
        auto* source=add(s,type,{20,20});if(!source)return fail("order source");
        const int id=source->instanceId,total=source->buildAnimTicksRemaining;
        const bool air=std::string(type)=="NASAM"||std::string(type)=="NAFLAK";
        auto* target=addRuntimeObjectInstanceForOwner(s,"Other",RuntimeBuildKind::Unit,air?"DISK":"MTNK",{16,20});
        if(!target)return fail("order target");
        const int targetId=target->instanceId;
        target->health=10000;
        if(air){target->airborne=true;target->flightLevel=target->level=6;}
        applySelection(s,{id},false,false);issueRuntimeAttackCommand(s,*target);
        if(s.lastCommand.acceptedCount!=1)return fail("construction order was discarded");
        for(int tick=0;tick<total;++tick) {
            source=findMutableObjectById(s,id);source->poweredOperational=true;
            advanceRuntimeAttackBehavior(s,*source,s.behaviors.at(id));
            if(source->facing!=expectedBuildupFacing(type)||source->turretFacing!=-1||source->turretFacingClock.active()
                ||!s.projectiles.empty())return fail(std::string("hidden turret moved/fired: ")+type);
            ++s.tick;advanceRuntimeBuildAnimations(s);
        }
        for(int tick=0;tick<hz*30&&s.projectiles.empty();++tick) {
            ++s.tick;advanceRuntimeWeaponCooldowns(s);
            source=findMutableObjectById(s,id);source->poweredOperational=true;
            advanceRuntimeAttackBehavior(s,*source,s.behaviors.at(id));
        }
        source=findMutableObjectById(s,id);
        if(s.projectiles.empty()||source->turretFacing==expectedBuildupFacing(type)||!findObjectById(s,targetId))
            return fail(std::string("queued order did not turn/fire after completion: ")+type);
        ++constructionOrders;
    }
    std::cout<<"Defense buildup passed: "<<turretScenes<<" scenes, "<<turretSaves
        <<" restored phases, "<<constructionOrders<<" deferred construction orders\n";
    records.flush();
    std::cout<<"Buildup passed: "<<cases<<" cases; original assets, frame cadence, 30/60 Hz, damage phase, walls/gates, "
        <<"rule overrides/defaults/hash, concurrent completion, full save/load, no-cooldown isolation, MCV, sell cleanup, "
        <<"actual renderer, reverse gates, missing art and facility registration characterization\n";
    return true;
}
