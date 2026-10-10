bool runRuntimeAirDamageSmokeSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const bool diagnostic=std::getenv("CONCORD_AIR_DAMAGE_SMOKE_DIAGNOSTIC")!=nullptr;
    const char* output=std::getenv("CONCORD_AIR_DAMAGE_SMOKE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-19/02-air-damage-smoke/checks";
    std::filesystem::create_directories(root);
    auto live=std::make_unique<RuntimeWindowState>(); live->config=config;
    live->config.playerSlots.clear(); live->config.gameModeRulesOverride.clear();
    live->report=buildRuntimeStartupReport(live->config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success){error="Air damage smoke: original resources unavailable";return false;}
    std::ofstream inventory(root/"inventory.csv"),rows(root/"source-checks.csv");
    if(!inventory||!rows){error="Air damage smoke: cannot open evidence";return false;}
    inventory<<"id,kind,strength,authored_smoke,reason\n";
    rows<<"id,hz,pose,origin_x,origin_y,origin_z,screen_offset_y,render_x,render_y,render_level,logical_level,result\n";
    int listed=0,eligible=0,excluded=0,cases=0,failures=0,saves=0,oldPuffs=0;
    std::string firstFailure;
    const auto fail=[&](const std::string& detail){++failures;if(firstFailure.empty())firstFailure=detail;
        std::cout<<"Air damage smoke FAIL "<<detail<<'\n';};
    std::vector<WorldObjectInstance> poses;
    std::vector<RuntimeParticlePoint> origins;
    std::vector<std::vector<RuntimeParticleRenderSnapshot>> previews;
    std::vector<std::string> labels;
    const auto make=[&](const UnitType& type,int kind,int hz){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=config;s.config.fixedTickHz=hz;s.config.playerSlots.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
        s.rules=live->rules;s.art=live->art;s.theater=live->theater;s.report=live->report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=s.visionEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner="Americans";
        s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
        WorldObjectInstance object;object.instanceId=1;object.type=type.id;object.owner="Americans";
        object.kind=kind==0?WorldObjectKind::Infantry:WorldObjectKind::Unit;object.authoredAircraft=kind==2;
        object.alive=true;object.health=std::max(1,type.strength/3);object.mission="Guard";
        object.x=object.y=7;object.cell=runtimeCellKey(7,7);object.subCell=kind==0?2:-1;
        object.facing=64;object.turretFacing=192;object.harvestAutoSearchSuspended=true;
        object.airborne=kind==2||runtimeUnitUsesAirMovement(type);
        object.level=object.flightLevel=object.airborne?3:2;
        object.hasRenderPosition=true;object.renderX=7.25;object.renderY=7.5;
        object.renderLevel=object.airborne?3.875:2.375;object.renderMoveActive=false;
        s.world.accessObjects().push_back(object);ensureRuntimeParticlesInitialized(s);return box;
    };
    for(int kind=0;kind<3;++kind){
        const auto& list=kind==0?live->rules.getInfantryTypes():kind==1?live->rules.getVehicleTypes():live->rules.getAircraftTypes();
        for(const auto& type:list){
            ++listed;bool authored=false;
            for(const auto& name:type.damageParticleSystems){const auto* system=live->rules.findParticleSystemType(name);
                authored=authored||(system&&system->behavior==ParticleBehavior::Smoke&&live->rules.findParticleType(system->holdsWhat));}
            const bool expected=type.strength>0&&authored&&!type.missileSpawn;
            inventory<<type.id<<','<<kind<<','<<type.strength<<','<<expected<<','
                <<(expected?"":type.missileSpawn?"covered_by_special_damage_smoke_actual_launch_test":
                    type.strength<=0?"no_positive_strength":"no_valid_authored_Smoke_chain")<<'\n';
            if(!expected){++excluded;if(type.missileSpawn)continue;auto box=make(type,kind,30);
                if(runtimeVehicleDamageSmokeType(*box,box->world.getObjects().front()))
                    fail(type.id+(type.missileSpawn?" dedicated missile incorrectly entered ordinary aircraft smoke":" unauthored smoke enabled"));continue;}
            ++eligible;
            const bool representative=type.id=="DISK"||type.id=="ZEP"||type.id=="SHAD"||type.id=="SCHP"
                ||type.id=="ORCA"||type.id=="BEAG"||type.id=="MTNK"||type.id=="DEST";
            for(int hz:{15,30,60}){
                auto box=make(type,kind,hz);auto& s=*box;
                RuntimeSnapshotActiveStateScope scope(s);
                auto& object=s.world.accessObjects().front();
                std::uint64_t producer=0,firstPuff=0;RuntimeParticlePoint prior{},firstPosition{};
                double priorX=0,priorY=0,priorLevel=0;
                for(int pose=0;pose<2;++pose){
                    ++cases;const auto beforeFailures=failures;
                    object.renderMoveActive=pose==1;object.mission=pose==1?"Move":"Guard";
                    if(pose==1){object.renderX+=0.625;object.renderY-=0.375;object.renderLevel+=0.1875;}
                    for(int tick=0;tick<(pose==0?1:hz);++tick){++s.tick;advanceRuntimeParticles(s);}
                    const RuntimeParticleSystemInstance* emitter=nullptr;int emitting=0;
                    for(const auto& system:s.particles.systems())if(system.sourceTag=="vehicle-damage-smoke"&&system.emitting){emitter=&system;++emitting;}
                    const std::string label=type.id+"-"+std::to_string(hz)+"-"+(pose?"moving":"idle");
                    if(emitting!=1||!emitter){fail(label+" requires one producer, found "+std::to_string(emitting));continue;}
                    const auto origin=emitter->origin;
                    // Compare translation and height deltas against the rendered
                    // pose, independently of the corrected origin helper.
                    const double bodyLevel=object.renderMoveActive?object.renderLevel:double(object.level);
                    if(pose==1){
                        if(emitter->id!=producer)fail(label+" producer restarted");
                        if(std::abs((origin.x-prior.x)/256.0-(object.renderX-priorX))>1e-8
                            ||std::abs((origin.y-prior.y)/256.0-(object.renderY-priorY))>1e-8
                            ||std::abs((origin.z-prior.z)/256.0-(bodyLevel-priorLevel))>1e-8)fail(label+" continuous source motion diverged from body");
                        for(const auto& puff:s.particles.particles())if(puff.id==firstPuff){++oldPuffs;
                            if(puff.position.x!=firstPosition.x||puff.position.y!=firstPosition.y
                                ||puff.position.z<=firstPosition.z)fail(label+" old puff followed source or stopped rising");}
                    }else{
                        producer=emitter->id;prior=origin;priorX=object.renderX;priorY=object.renderY;priorLevel=bodyLevel;
                        if(!s.particles.particles().empty()){firstPuff=s.particles.particles().front().id;firstPosition=s.particles.particles().front().position;}
                    }
                    const auto snapshots=s.particles.renderSnapshots(s.rules,&s.art);
                    if(snapshots.empty())fail(label+" no original particle snapshots");
                    for(const auto& snapshot:snapshots)if(snapshot.screenOffsetY!=emitter->screenOffsetY
                        ||snapshot.airborneDamageSmoke!=(snapshot.screenOffsetY<0)
                        ||snapshot.renderKind!=RuntimeParticleRenderKind::Shp)fail(label+" puff lost screen attachment or original SHP");
                    rows<<type.id<<','<<hz<<','<<pose<<','<<std::setprecision(12)<<origin.x<<','<<origin.y<<','<<origin.z
                        <<','<<emitter->screenOffsetY<<','<<object.renderX<<','<<object.renderY<<','<<object.renderLevel
                        <<','<<object.level<<','<<(beforeFailures==failures?"PASS":"FAIL")<<'\n';
                    // All authored smoke types enter the independent anchor
                    // contract at 30 Hz; representatives also cover 15/60 Hz.
                    if(hz==30||representative){
                        // Real mature puffs are needed to test visibility after
                        // the full renderer's later aircraft-body pass. Keep
                        // every generated position/frame/offset unchanged.
                        if(representative)for(int n=0;n<hz*3;++n){++s.tick;advanceRuntimeParticles(s);}
                        poses.push_back(object);origins.push_back(origin);
                        previews.push_back(s.particles.renderSnapshots(s.rules,&s.art));labels.push_back(label);
                    }
                }
                if(representative){
                    std::ostringstream saved; s.particles.save(saved);IniFile ini;RuntimeParticleSystemState restored(hz);
                    std::string detail;
                    if(!ini.loadFromString(saved.str())||!restored.load(ini,s.rules,&detail)||restored.stateHash()!=s.particles.stateHash())
                        fail(type.id+" particle save/load "+detail);
                    else ++saves;
                }
                if(type.id=="ORCA"||type.id=="BEAG"){
                    object.dockedToObjectId=7000;object.dockIndex=0;object.airborne=true;
                    ++s.tick;advanceRuntimeParticles(s);
                    if(!runtimeVehicleDamageSmokeType(s,object))fail(type.id+" airborne airport reservation suppressed smoke");
                    object.airborne=false;
                    ++s.tick;advanceRuntimeParticles(s);
                    if(runtimeVehicleDamageSmokeType(s,object))fail(type.id+" occupied transport/dock concealment emitted smoke");
                }
                object.dockedToObjectId=-1;object.health=type.strength;
                ++s.tick;advanceRuntimeParticles(s);
                for(const auto& system:s.particles.systems())if(system.emitting&&system.sourceTag=="vehicle-damage-smoke")
                    fail(type.id+" healthy producer remained active");
                if(type.id=="MTNK"||type.id=="DEST"){
                    // Magnetron lifting sets airborne on a ground/naval body.
                    // It does not turn that body into an aircraft sprite.
                    auto lifted=make(type,kind,hz);auto& liftedState=*lifted;
                    RuntimeSnapshotActiveStateScope liftedScope(liftedState);
                    auto& carried=liftedState.world.accessObjects().front();
                    carried.airborne=true;carried.hasRenderPosition=carried.renderMoveActive=true;
                    carried.level=carried.flightLevel=5;carried.renderLevel=5.625;
                    for(int n=0;n<hz*3;++n){++liftedState.tick;advanceRuntimeParticles(liftedState);}
                    ++cases;const RuntimeParticleSystemInstance* emitter=nullptr;int emitting=0;
                    for(const auto& system:liftedState.particles.systems())if(system.emitting&&system.sourceTag=="vehicle-damage-smoke"){
                        emitter=&system;++emitting;}
                    const std::string label=type.id+"-"+std::to_string(hz)+"-magnet-lifted";
                    if(emitting!=1||!emitter)fail(label+" missing lifted damage source");
                    else{
                        if(emitter->screenOffsetY!=0)fail(label+" ground/naval source incorrectly used aircraft lift");
                        const auto savedSmoke=liftedState.particles.renderSnapshots(liftedState.rules,&liftedState.art);
                        for(const auto& puff:savedSmoke)if(puff.screenOffsetY!=0||puff.airborneDamageSmoke)
                            fail(label+" ground/naval puff incorrectly used aircraft lift/layer");
                        poses.push_back(carried);origins.push_back(emitter->origin);previews.push_back(savedSmoke);labels.push_back(label);
                        rows<<type.id<<','<<hz<<",magnet-lifted,"<<emitter->origin.x<<','<<emitter->origin.y<<','<<emitter->origin.z
                            <<','<<emitter->screenOffsetY<<','<<carried.renderX<<','<<carried.renderY<<','<<carried.renderLevel
                            <<','<<carried.level<<','<<(emitter->screenOffsetY==0?"PASS":"FAIL")<<'\n';
                    }
                }
                if(type.id=="SHAD"){
                    auto landing=make(type,kind,hz);auto& landed=*landing;
                    RuntimeSnapshotActiveStateScope landingScope(landed);
                    for(int n=0;n<hz*2;++n){++landed.tick;advanceRuntimeParticles(landed);}
                    const auto airborne=landed.particles.renderSnapshots(landed.rules,&landed.art);
                    std::set<std::uint64_t> oldIds;
                    for(const auto& puff:airborne){oldIds.insert(puff.instanceId);
                        if(!puff.airborneDamageSmoke||puff.screenOffsetY>=0)fail("SHAD airborne puff missing frozen air layer");}
                    auto& source=landed.world.accessObjects().front();source.airborne=false;
                    source.renderMoveActive=source.hasRenderPosition=false;source.level=source.flightLevel=0;
                    for(int n=0;n<hz;++n){++landed.tick;advanceRuntimeParticles(landed);}
                    int retainedAir=0,newGround=0;
                    for(const auto& puff:landed.particles.renderSnapshots(landed.rules,&landed.art)){
                        if(oldIds.contains(puff.instanceId)){++retainedAir;
                            if(!puff.airborneDamageSmoke||puff.screenOffsetY>=0)fail("SHAD landing moved old puff to ground layer");}
                        else{++newGround;if(puff.airborneDamageSmoke||puff.screenOffsetY!=0)
                            fail("SHAD newly grounded puff inherited air layer");}}
                    if(retainedAir==0||newGround==0)fail("SHAD landing requires both surviving air and new ground puffs");
                    landed.world.accessObjects().clear();++landed.tick;advanceRuntimeParticles(landed);
                    int orphanAir=0;for(const auto& puff:landed.particles.renderSnapshots(landed.rules,&landed.art))
                        if(oldIds.contains(puff.instanceId)){++orphanAir;if(!puff.airborneDamageSmoke)
                            fail("SHAD disappearing source demoted surviving smoke");}
                    if(orphanAir==0)fail("SHAD source removal lost old air smoke");
                    std::ostringstream saved;landed.particles.save(saved);IniFile ini;RuntimeParticleSystemState restored(hz);
                    std::string detail;
                    if(!ini.loadFromString(saved.str())||!restored.load(ini,landed.rules,&detail))fail("SHAD layered puff save "+detail);
                    else{int restoredAir=0;for(const auto& puff:restored.renderSnapshots(landed.rules,&landed.art))
                        if(oldIds.contains(puff.instanceId)&&puff.airborneDamageSmoke)++restoredAir;
                        if(restoredAir!=orphanAir||restored.stateHash()!=landed.particles.stateHash())fail("SHAD save changed detached air layer");
                        else ++saves;}
                    std::cout<<"Air damage smoke layer hz="<<hz<<" landingOldAir="<<retainedAir
                        <<" newGround="<<newGround<<" sourceGoneAir="<<orphanAir<<'\n';
                }
            }
        }
    }
    RuntimeMapRenderContext context;context.rules=&live->rules;context.art=&live->art;
    context.assetRootPath=runtimeAssetRootPath(config);context.palettePath=runtimePalettePath(config,live->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,live->report.map);context.shadowsEnabled=false;
    std::string renderError;
    if(!runRuntimeAirDamageSmokeRenderContractCheck(context,poses,origins,previews,labels,renderError))fail(renderError);
    std::cout<<"Air damage smoke: listed="<<listed<<" eligible="<<eligible<<" excluded="<<excluded
        <<" sourceCases="<<cases<<" oldPuffs="<<oldPuffs<<" saves="<<saves<<" failures="<<failures
        <<" diagnostic="<<diagnostic<<'\n';
    if(!eligible){error="Air damage smoke: empty inventory";return false;}
    if(failures){error="Air damage smoke: "+std::to_string(failures)+" failures; "+firstFailure;return false;}
    error.clear();
    if(!runRuntimeAirbornePickingOrderSmokeProbe(*live,error))return false;
    return runRuntimeVehicleDamageSmokeSmokeProbe(*live,error);
}
