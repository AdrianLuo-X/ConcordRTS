// Test-only support-aircraft continuity checks; both native probes keep their real orders.
struct RuntimeSupportAircraftSmokePose {
    double x=0,y=0,z=0; int cellX=0,cellY=0,level=0,hp=0,token=-1,phase=-1,startTick=-1,endTick=-1;
    bool hasPose=false,active=false,airborne=false; std::string mission;
};
using RuntimeSupportAircraftSmokePoses=std::map<int,RuntimeSupportAircraftSmokePose>;
RuntimeSupportAircraftSmokePoses runtimeSupportAircraftSmokePoses(const RuntimeWindowState& s) {
    RuntimeSupportAircraftSmokePoses result;
    for (const auto& o:s.world.getObjects()) if (o.alive&&(o.type=="BPLN"||o.type=="SPYP")) {
        RuntimeSupportAircraftSmokePose p;
        p.x=o.renderX;p.y=o.renderY;p.z=o.renderLevel;p.cellX=o.x;p.cellY=o.y;p.level=o.level;
        p.hp=o.health;p.hasPose=o.hasRenderPosition;p.active=o.renderMoveActive;p.airborne=o.airborne;p.mission=o.mission;
        for(const auto& [token,b]:s.airstrikePlanesByToken) if(b.objectId==o.instanceId) {
            p.token=token;const auto* plane=runtimeAirstrikePlaneStateForToken(s.airstrikes,token);
            if(plane) {
                p.phase=static_cast<int>(plane->phase);
                p.startTick=plane->phase==RuntimeAirstrikePlanePhase::Egress?b.egressStartTick:b.spawnTick;
                p.endTick=plane->phase==RuntimeAirstrikePlanePhase::Egress?b.despawnTick:b.bombTick;
            }
        }
        result.emplace(o.instanceId,std::move(p));
    }
    return result;
}
bool runtimeSupportAircraftSmokeEqual(const RuntimeSupportAircraftSmokePoses& a,
    const RuntimeSupportAircraftSmokePoses& b,std::string& error) {
    if(a.size()!=b.size()){error="native aircraft count differs";return false;}
    for(const auto& [id,p]:a) {
        const auto it=b.find(id);if(it==b.end()){error="native aircraft identity differs";return false;}
        const auto& q=it->second;
        if(std::abs(p.x-q.x)>1e-9||std::abs(p.y-q.y)>1e-9||std::abs(p.z-q.z)>1e-9
            ||p.cellX!=q.cellX||p.cellY!=q.cellY||p.level!=q.level||p.hp!=q.hp||p.token!=q.token||p.phase!=q.phase||p.startTick!=q.startTick||p.endTick!=q.endTick
            ||p.hasPose!=q.hasPose||p.active!=q.active||p.airborne!=q.airborne||p.mission!=q.mission) {
            std::ostringstream detail;detail<<std::setprecision(17)<<"id="<<id<<" XYZ="<<p.x<<','<<p.y<<','<<p.z
                <<" vs "<<q.x<<','<<q.y<<','<<q.z<<" phase="<<p.phase<<'/'<<q.phase;
            error=detail.str();return false;
        }
    }
    return true;
}
void runtimeSupportAircraftSmokeTrace(std::ostream* output,const std::string& scene,const char* event,
    int tick,const RuntimeSupportAircraftSmokePoses& poses,const RuntimeSupportAircraftSmokePoses& before,
    double bound) {
    if(!output)return;
    for(const auto& [id,p]:poses) {
        const auto old=before.find(id);const double delta=old==before.end()?0.0:std::hypot(p.x-old->second.x,p.y-old->second.y);
        *output<<std::setprecision(17)<<scene<<','<<event<<','<<tick<<','<<id<<','<<p.token<<','<<p.phase<<','
            <<p.x<<','<<p.y<<','<<p.z<<','<<p.cellX<<','<<p.cellY<<','<<delta<<','<<bound<<','
            <<p.startTick<<','<<p.endTick<<','<<p.hasPose<<','<<p.active<<','<<p.airborne<<','<<p.hp<<','<<p.mission<<'\n';
    }
}
bool runtimeSupportAircraftSmokeAnchors(RuntimeWindowState& s,std::string& error) {
    const auto savedCamera=s.camera;
    struct Restore {RuntimeWindowState& s;RuntimeMapCamera camera;~Restore(){s.camera=camera;invalidateRuntimeObjectProjectionCache(s);}} restore{s,savedCamera};
    auto context=runtimeTerrainMetadataContext(s);context.rules=&s.rules;context.art=&s.art;
    context.assetRootPath=runtimeAssetRootPath(s.config);context.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
    context.palettePath=runtimePalettePath(s.config,s.report.map);
    context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
    context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
    context.fixedTickHz=s.config.fixedTickHz;context.animationTick=s.tick;context.stableRenderConfiguration=true;
    context.mapFacts=s.mapFactsLoaded?&s.mapFacts:nullptr;
    context.ownerRemapColors=s.ownerRemapColors.empty()?nullptr:&s.ownerRemapColors;
    context.neutralRemapOwners=s.neutralRemapOwners.empty()?nullptr:&s.neutralRemapOwners;
    refreshRuntimeGateRenderSnapshots(s);refreshRuntimeSpecialMechanismRenderSnapshots(s);
    context.gateSnapshots=&s.gateRenderSnapshots;context.specialMechanismSnapshots=&s.specialMechanismRenderSnapshots;
    if(runtimeLocalVisionRestricted(s)){context.vision=&s.vision;context.visionObserverOwner=s.economy.owner;}
    const RECT viewport{0,0,1600,1200};
    const auto poses=runtimeSupportAircraftSmokePoses(s);
    for(const auto& [id,p]:poses) {
        if(!p.hasPose||!p.active||!p.airborne||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)
            ||p.cellX!=int(std::lround(p.x))||p.cellY!=int(std::lround(p.y))) {
            error="invalid native continuous pose id="+std::to_string(id);return false;
        }
        s.camera.worldOffsetX=int(std::llround((p.x-p.y)*s.camera.tileWidth/2.0))-800;
        s.camera.worldOffsetY=int(std::llround((p.x+p.y-p.z)*s.camera.tileHeight/2.0))-600;
        const RuntimeMapCamera requestedCamera=s.camera;
        // Picking initializes/clamps the camera; resolve that same native viewport first.
        ensureRuntimeCameraForViewport(s,viewport);
        invalidateRuntimeObjectProjectionCache(s);
        const auto* current=findObjectById(s,id);if(!current){error="native plane vanished before projection";return false;}
        const WorldObjectInstance object=*current;
        const POINT expected{viewport.left+int(std::llround((p.x-p.y)*s.camera.tileWidth/2.0))-s.camera.worldOffsetX,
            viewport.top+int(std::llround((p.x+p.y)*s.camera.tileHeight/2.0-p.z*s.camera.tileHeight/2.0))-s.camera.worldOffsetY};
        const auto renderer=runtimeObjectWorldRenderPlacement(object,s.world,viewport,context,s.camera,s.renderCache);
        const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        const auto found=std::find_if(projected.begin(),projected.end(),[&](const auto& q){return q.object&&q.object->instanceId==id;});
        // Raw isometric center is independent. Body-center agreement deliberately
        // includes the existing aircraft lift and actual structure roof clearance.
        if(found==projected.end()||found->center.x!=expected.x||found->center.y!=expected.y
            ||found->renderCenter.x!=renderer.center.x||found->renderCenter.y!=renderer.center.y) {
            std::ostringstream detail;detail<<std::setprecision(17)
                <<"renderer/picking continuous pose differs id="<<id<<" type="<<object.type
                <<" tick="<<s.tick<<" phase="<<p.phase<<" XYZ="<<p.x<<','<<p.y<<','<<p.z
                <<" cell="<<p.cellX<<','<<p.cellY<<" level="<<p.level
                <<" viewport="<<viewport.left<<','<<viewport.top<<','<<viewport.right<<','<<viewport.bottom
                <<" requestedCamera="<<requestedCamera.worldOffsetX<<','<<requestedCamera.worldOffsetY
                <<" tile="<<requestedCamera.tileWidth<<','<<requestedCamera.tileHeight
                <<" initialized="<<requestedCamera.initialized
                <<" actualCamera="<<s.camera.worldOffsetX<<','<<s.camera.worldOffsetY
                <<" tile="<<s.camera.tileWidth<<','<<s.camera.tileHeight<<" initialized="<<s.camera.initialized
                <<" rawExpected="<<expected.x<<','<<expected.y
                <<" rendererBody="<<renderer.center.x<<','<<renderer.center.y
                <<" pickingMissing="<<(found==projected.end());
            if(found!=projected.end())detail<<" pickingRaw="<<found->center.x<<','<<found->center.y
                <<" pickingBody="<<found->renderCenter.x<<','<<found->renderCenter.y<<" pickingDrawLevel="<<found->drawLevel;
            error=detail.str();return false;
        }
    }
    return true;
}
bool runtimeSupportAircraftSmokeCheckpoint(RuntimeWindowState& source,const std::string& root,
    const std::string& label,std::unique_ptr<RuntimeWindowState>& ordinary,
    std::unique_ptr<RuntimeWindowState>& canonical,std::string& error) {
    RuntimeSnapshotActiveStateScope scope(source);
    std::ostringstream normal,assembled,exact;
    if(!writeRuntimeStateToStream(source,normal,error))return false;
    assembled<<normal.str();if(!writeRuntimeMultiplayerSnapshotExtension(source,assembled,error))return false;
    IniFile combined;if(!combined.loadFromString(assembled.str())
        ||!writeRuntimeMultiplayerSnapshotCanonicalIni(combined,exact,error))return false;
    const std::string directory=root.empty()?source.config.localDataRootPath+"/support-aircraft-continuity-smoke":root;
    std::filesystem::create_directories(directory);
    const std::string normalPath=directory+"/"+label+"-ordinary14.ini",exactPath=directory+"/"+label+"-canonical-image.ini";
    const auto write=[](const std::string& path,const std::string& text) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;file.close();return bool(file);
    };
    if(!write(normalPath,normal.str())||!write(exactPath,exact.str())){error="actual checkpoint write failed";return false;}
    IniFile normalIni,exactIni;
    if(!normalIni.loadFromFile(normalPath)||!exactIni.loadFromFile(exactPath)){error="actual checkpoint reread failed";return false;}
    auto restoredOrdinary=std::make_unique<RuntimeWindowState>();
    auto restoredCanonical=std::make_unique<RuntimeWindowState>();
    for(auto* clone:{restoredOrdinary.get(),restoredCanonical.get()}) {
        seedRuntimeLoadScratch(*clone,source,false);
        if(!materializeRuntimeLoadStateFromIni(*clone,normalIni,error))return false;
    }
    // The canonical authority image is paired with its complete ordinary base.
    // Keep genuine in-flight projectiles; the extension does not recreate them.
    if(!materializeRuntimeMultiplayerSnapshotExtension(*restoredCanonical,exactIni,error))return false;
    const auto expected=runtimeSupportAircraftSmokePoses(source);
    if(!runtimeSupportAircraftSmokeEqual(expected,runtimeSupportAircraftSmokePoses(*restoredOrdinary),error)
        ||!runtimeSupportAircraftSmokeEqual(expected,runtimeSupportAircraftSmokePoses(*restoredCanonical),error))return false;
    ordinary=std::move(restoredOrdinary);canonical=std::move(restoredCanonical);
    return true;
}

bool runtimeSupportAircraftSmokeLegacyCheck(RuntimeWindowState& context,const std::string& directory,int hz,
    const std::function<void(RuntimeWindowState&,int)>& tick,std::string& error) {
    const auto fail=[&](const std::string& why){error=why;return false;};
    const std::string stem=directory+"/boris-target-loss-"+std::to_string(hz);IniFile base,exact;
    if(directory.empty()||!base.loadFromFile(stem+"-ordinary14.ini")||!exact.loadFromFile(stem+"-canonical-image.ini"))return fail("legacy actual checkpoint reread failed");
    if(base.getSection("RuntimeAirstrikeEgressOrigins")||exact.getSection("RuntimeAirstrikeEgressOrigins")
        ||parseRuntimeIntOrDefault(base.getValue("RuntimeSave","Version",""),-1)!=14
        ||parseRuntimeIntOrDefault(base.getValue("SkirmishSession","FixedTickHz",""),-1)!=hz||context.config.fixedTickHz!=hz)return fail("legacy origin/version/clock provenance differs");
    const auto sameSection=[](const IniFile& a,const IniFile& b,const char* name) {const auto* x=a.getSection(name);const auto* y=b.getSection(name);
        return x&&y&&std::map<std::string,std::string>(x->begin(),x->end())==std::map<std::string,std::string>(y->begin(),y->end());};
    RuntimeAirstrikeSystemState oldCore,imageCore;
    if(!oldCore.load(base,&error)||!imageCore.load(exact,&error))return false;
    if(oldCore.stateHash()!=imageCore.stateHash()||!sameSection(base,exact,"RuntimeAirstrikeHost"))return fail("legacy original core/Host image differs");
    auto ordinary=std::make_unique<RuntimeWindowState>(),canonical=std::make_unique<RuntimeWindowState>();
    auto ordinaryRepeat=std::make_unique<RuntimeWindowState>(),canonicalRepeat=std::make_unique<RuntimeWindowState>();
    const std::array<RuntimeWindowState*,4> restored{{ordinary.get(),canonical.get(),ordinaryRepeat.get(),canonicalRepeat.get()}};
    for(auto* s:restored) {seedRuntimeLoadScratch(*s,context,false);
        if(!materializeRuntimeLoadStateFromIni(*s,base,error))return false;}
    for(auto* s:{canonical.get(),canonicalRepeat.get()})
        if(!materializeRuntimeMultiplayerSnapshotExtension(*s,exact,error))return false;
    for(auto* s:restored) {
        if(s->airstrikePlanesByToken.size()!=2||s->airstrikes.stateHash()!=oldCore.stateHash())return fail("legacy native core/count restore differs");
        for(const auto& [token,b]:s->airstrikePlanesByToken) {const RuntimeAirstrikeStrikeState* strike=nullptr;
            const auto* p=runtimeAirstrikePlaneStateForToken(s->airstrikes,token,&strike);
            if(!p||!strike||!strike->cancelled||p->phase!=RuntimeAirstrikePlanePhase::Egress
                ||b.egressStartTick!=s->tick||b.despawnTick-b.egressStartTick!=20)return fail("legacy cancelled Egress/20-clock missing");}
        RuntimeSnapshotActiveStateScope scope(*s);std::ostringstream text;if(!writeRuntimeStateToStream(*s,text,error))return false;
        IniFile round;if(!round.loadFromString(text.str())||!sameSection(base,round,"RuntimeAirstrikeHost")
            ||round.getSection("RuntimeAirstrikeEgressOrigins"))return fail("legacy original Host/schema20 roundtrip differs");}
    using Frames=std::map<int,RuntimeSupportAircraftSmokePoses>;Frames trace;
    const std::string label="hz="+std::to_string(hz)+" mode=3 relation=enemy";
    std::ifstream csv(directory+"/boris-motion.csv");std::string line;
    if(!csv||!std::getline(csv,line))return fail("legacy motion CSV unavailable");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="scene,event,tick,id,token,phase,x,y,z,logical_x,logical_y,delta_xy,bound,phase_start,phase_end,has_pose,active,airborne,hp,mission")return fail("legacy CSV schema differs");
    while(std::getline(csv,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();std::istringstream row(line);std::vector<std::string> fields;std::string field;
        while(std::getline(row,field,','))fields.push_back(field);if(fields.size()!=20)return fail("legacy CSV record width differs");
        if(fields[1]!="target-loss-egress"||fields[0]!=label)continue;
        int recordedTick=0,id=0;RuntimeSupportAircraftSmokePose p;double delta=0,bound=0;
        for(const auto [column,out]:std::array<std::pair<int,int*>,9>{{{2,&recordedTick},{3,&id},{4,&p.token},{5,&p.phase},{9,&p.cellX},{10,&p.cellY},{13,&p.startTick},{14,&p.endTick},{18,&p.hp}}})
            if(!parseRuntimeInt(fields[column],*out))return fail("legacy CSV integer invalid");
        for(const auto [column,out]:std::array<std::pair<int,bool*>,3>{{{15,&p.hasPose},{16,&p.active},{17,&p.airborne}}})
            if(!parseRuntimeStrictBool(fields[column],*out))return fail("legacy CSV pose flag invalid");
        if(!parseRuntimeFiniteDouble(fields[6],p.x)||!parseRuntimeFiniteDouble(fields[7],p.y)||!parseRuntimeFiniteDouble(fields[8],p.z)
            ||!parseRuntimeFiniteDouble(fields[11],delta)||!parseRuntimeFiniteDouble(fields[12],bound)
            ||std::abs(p.z)>INT_MAX||p.z!=std::round(p.z)||delta<0||bound!=-1||id<=0||p.token<=0||p.hp<=0
            ||p.phase!=int(RuntimeAirstrikePlanePhase::Egress)||!p.hasPose||!p.active||!p.airborne
            ||p.startTick<0||p.endTick<p.startTick||p.endTick-p.startTick!=20)return fail("legacy CSV native pose invalid");
        p.level=int(std::lround(p.z));p.mission=fields[19];
        if(!trace[recordedTick].emplace(id,p).second)return fail("duplicate legacy CSV tick/id");
    }
    if(csv.bad()||trace.size()!=20)return fail("legacy CSV read/20-tick catalogue incomplete");
    const int start=ordinary->tick,caller=ordinary->airstrikePlanesByToken.begin()->second.callerObjectId;
    const auto describe=[](std::ostringstream& out,const char* name,const RuntimeWindowState& s) {
        out<<name<<"[tick="<<s.tick<<" expired="<<s.recentExpiredProjectiles.size()<<" projectiles="<<s.projectiles.size()<<" impacts="<<s.impactEffects.size()
            <<" mapFacts="<<s.mapFactsLoaded<<" worldAudio="<<s.worldAudioInitialized<<" lighting="<<s.lightingInitialized
            <<" particles="<<s.particlesInitialized<<" superWeapons="<<s.superWeaponsInitialized<<" repairDepots="<<s.repairDepotsInitialized
            <<" aircraftServices="<<s.aircraftServicesInitialized<<"]";};
    using LegacyHashes=std::array<std::uint64_t,3>;
    const auto hashes=[](RuntimeWindowState& s,std::string& why,std::vector<std::pair<std::string,std::uint64_t>>* hashTrace=nullptr)->LegacyHashes {
        RuntimeSnapshotActiveStateScope scope(s);
        const auto authority=runtimeStandaloneSimulationStateHash(s,why,hashTrace);
        return {{authority,runtimePersistentTransientStateHash(s),s.airstrikes.stateHash()}};};
    int pairs=0,samples=0,hashPairs=0;for(int step=0;step<=20;++step) {
        const auto a=runtimeSupportAircraftSmokePoses(*ordinary),b=runtimeSupportAircraftSmokePoses(*canonical);
        for(auto* s:restored)if(s->tick!=start+step)return fail("legacy restored clock differs at step="+std::to_string(step));
        if(!runtimeSupportAircraftSmokeEqual(a,b,error))return fail("legacy loaded pair differs: "+error);
        if(!runtimeSupportAircraftSmokeEqual(a,runtimeSupportAircraftSmokePoses(*ordinaryRepeat),error)
            ||!runtimeSupportAircraftSmokeEqual(b,runtimeSupportAircraftSmokePoses(*canonicalRepeat),error))return fail("legacy same-format pose pair differs: "+error);
        if(ordinary->airstrikes.stateHash()!=canonical->airstrikes.stateHash())return fail("legacy cross-format Core hash differs at step="+std::to_string(step));
        if(step<20) {const auto frame=trace.find(start+step);
            if(frame==trace.end()||frame->second.size()!=2||!runtimeSupportAircraftSmokeEqual(a,frame->second,error))return fail("legacy actual old CSV trajectory differs: "+error);
            for(const auto& [id,p]:frame->second)if(p.startTick!=start||p.endTick!=start+20)return fail("legacy CSV phase clock differs");samples+=int(a.size());
        } else for(auto* s:restored)if(!runtimeSupportAircraftSmokePoses(*s).empty()||!s->airstrikePlanesByToken.empty())return fail("legacy native20 cleanup missing");
        // Each format keeps its real flags and transient image; compare full hashes only with another independent restore of that format.
        std::array<LegacyHashes,2> formatHashes{};
        for(int format=0;format<2;++format) {auto& first=*(format==0?ordinary.get():canonical.get());auto& second=*(format==0?ordinaryRepeat.get():canonicalRepeat.get());
            const char* name=format==0?"ordinary14":"ordinary14+canonical-extension";std::string errorA,errorB;
            const auto hA=hashes(first,errorA),hB=hashes(second,errorB);formatHashes[format]=hA;
            if(!hA[0]||!hB[0]||hA!=hB) {std::ostringstream detail;detail<<"legacy same-format authority/stable/Core hash pair differs: format="<<name<<" step="<<step
                    <<" authority="<<hA[0]<<"/"<<hB[0]<<" stable="<<hA[1]<<"/"<<hB[1]<<" Core="<<hA[2]<<"/"<<hB[2]<<" errors="<<errorA<<"/"<<errorB<<" ";
                describe(detail,"first",first);detail<<" ";describe(detail,"second",second);
                std::vector<std::pair<std::string,std::uint64_t>> traceA,traceB;std::string traceErrorA,traceErrorB;
                const auto tracedA=hashes(first,traceErrorA,&traceA),tracedB=hashes(second,traceErrorB,&traceB);
                detail<<" tracedAuthority="<<tracedA[0]<<"/"<<tracedB[0]<<" traceErrors="<<traceErrorA<<"/"<<traceErrorB;
                for(std::size_t i=0;i<std::max(traceA.size(),traceB.size());++i) {
                    if(i<traceA.size()&&i<traceB.size()&&traceA[i]==traceB[i])continue;
                    detail<<" trace["<<i<<"]=";if(i<traceA.size())detail<<traceA[i].first<<":"<<traceA[i].second;else detail<<"missing";
                    detail<<"/";if(i<traceB.size())detail<<traceB[i].first<<":"<<traceB[i].second;else detail<<"missing";}
                return fail(detail.str());}
            ++hashPairs;
        }
        if(step==0) {std::ostringstream info;info<<"Boris legacy loaded-format state: hz="<<hz<<" ordinaryAuthority="<<formatHashes[0][0]<<" canonicalAuthority="<<formatHashes[1][0]
                <<" ordinaryStable="<<formatHashes[0][1]<<" canonicalStable="<<formatHashes[1][1]<<" Core="<<formatHashes[0][2]<<" ";
            describe(info,"ordinary",*ordinary);info<<" ";describe(info,"canonical",*canonical);std::cout<<info.str()<<"\n";}
        ++pairs;if(step<20)for(auto* s:restored){RuntimeSnapshotActiveStateScope scope(*s);tick(*s,caller);}
    }
    if(pairs!=21||samples!=40||hashPairs!=42)return fail("legacy actual sample/pose/hash pair catalogue incomplete");
    std::cout<<"Boris legacy cancelled Egress: hz="<<hz<<" samples=40 paired=21 formatHashPairs=42 oldFiles=2 original20clock/actualCSV/ordinary-canonical-pose/format-local-hash/cleanup PASS\n";return true;
}

bool runRuntimeAirstrikeTintRenderContract(const RulesDatabase& rules, std::string& error);

bool runRuntimeBorisAirstrikeSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout<<std::unitbuf;
    std::string scene;
    const auto fail=[&](const std::string& why) { error="Boris airstrike: "+scene+": "+why; return false; };
    IniFile ini; RulesDatabase stock;
    if (!ini.loadFromFile(live.report.rulesPath)||!stock.loadFromIni(ini)) return fail("stock rules unavailable");
    if (!runRuntimeAirstrikeTintRenderContract(stock,error)) return false;
    const char* output=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT");
    const std::string root=output?output:"";
    const auto health=[](const RuntimeWindowState& s,int id) -> std::optional<int> {
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);
        return o?std::optional<int>(o->health):std::nullopt;
    };
    const auto tick=[](RuntimeWindowState& s,int caller) {
        ++s.tick; processRuntimePathRequests(s); advanceRuntimeWeaponCooldowns(s);
        auto* o=findMutableObjectById(s,caller); const auto b=s.behaviors.find(caller);
        if (o&&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Attack
            &&b->second.state!=RuntimeBehaviorState::Complete) advanceRuntimeAttackBehavior(s,*o,b->second);
        advanceRuntimeEffects(s);
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
    };
    const auto marker=[](const RuntimeWindowState& s,int target) -> const SpecialMechanismRenderSnapshot* {
        for (const auto& m:s.specialMechanismRenderSnapshots)
            if (m.kind=="airstrikeMarker"&&m.targetObjectId==target) return &m;
        return nullptr;
    };
    const char* forcedOwner=std::getenv("CONCORD_BORIS_FORCE_OWNER");
    const bool forceFriendly=forcedOwner!=nullptr;
    const bool sameOwner=forceFriendly&&std::string(forcedOwner)=="own";
    const char* motionOutput=std::getenv("CONCORD_SUPPORT_AIRCRAFT_CONTINUITY_OUTPUT");
    const std::string motionRoot=motionOutput?motionOutput:root;
    std::ofstream motionCsv;if(!motionRoot.empty()) {
        std::filesystem::create_directories(motionRoot);motionCsv.open(motionRoot+"/boris-motion.csv");
        if(!motionCsv)return fail("motion CSV unavailable");
        motionCsv<<"scene,event,tick,id,token,phase,x,y,z,logical_x,logical_y,delta_xy,bound,phase_start,phase_end,has_pose,active,airborne,hp,mission\n";
    }
    std::vector<std::string> motionFailures;
    int cases=0,saves=0,captures=0,motionSamples=0,navChecks=0,targetLossChecks=0,precisePairs=0;
    const auto motionProblem=[&](const std::string& why) {
        motionFailures.push_back(scene+": "+why);std::cerr<<"Support aircraft continuity: "<<scene<<": "<<why<<'\n';
    };
    for (int hz:{15,30,60}) for (int mode=0;mode<(forceFriendly?9:8);++mode) {
        if (const char* only=std::getenv("CONCORD_BORIS_SMOKE_MODE"))
            if (mode!=std::atoi(only)) continue;
        const bool primary=mode>=7;
        scene="hz="+std::to_string(hz)+" mode="+std::to_string(mode)
            +" relation="+(forceFriendly?(sameOwner?"own":"ally"):"enemy");
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
        seedRuntimeLoadScratch(s,live,false); s.rules=stock; s.rulesLoaded=true;
        s.config.fixedTickHz=hz; s.config.multiplayerActive=false; s.config.computerAiEnabled=false;
        s.config.noCooldown=false; s.config.gameMode="Battle"; s.campaignMode=false;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false; s.visionEnabled=false;
        s.economy.owner="Russians"; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;
        s.skirmishTeamByOwner={{"russians","1"},{"british","1"},{"americans","2"}};
        s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear(); s.airstrikes.reset();
        s.airstrikePlanesByToken.clear(); s.airstrikeRootAuthoritiesByActor.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainInfoByCell.clear(); s.terrainByCell.clear(); s.mapFacts={}; s.mapFactsLoaded=false;
        for (int y=45;y<=86;++y) for (int x=45;x<=86;++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y); c.tileIndex=0; c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        const auto add=[&](const char* type,RuntimeBuildKind kind,RuntimeCellPoint cell) {
            auto* o=addRuntimeObjectInstance(s,kind,type,cell); if (!o) return -1;
            o->mission="Guard"; o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0; return o->instanceId;
        };
        const int caller=add("BORIS",RuntimeBuildKind::Infantry,{60,65});
        const int target=add(mode==8?"MTNK":primary?"E1":"NACNST",
            mode==8?RuntimeBuildKind::Unit:primary?RuntimeBuildKind::Infantry:RuntimeBuildKind::Building,{66,65});
        if (caller<0||target<0) return fail("native objects unavailable");
        auto* enemy=findMutableObjectById(s,target);
        enemy->owner=forceFriendly?(sameOwner?"Russians":"British"):"Americans";
        const int initialHealth=enemy->health;
        auto* boris=findMutableObjectById(s,caller); boris->veterancy=mode==1?2:0;
        boris->facing=runtimeFacingFromDelta(6,0);
        const auto profile=runtimeWeaponProfileForObject(s,*boris,enemy);
        if (!profile.weapon||profile.weapon->id!=(primary?"AKM":"Flare")) return fail("building secondary/infantry primary selection");
        advanceRuntimeSpecialMechanisms(s);
        applySelection(s,{caller},false,false);
        if(forceFriendly) {
            issueRuntimeAttackCommand(s,*findObjectById(s,target));
            if(s.lastCommand.acceptedCount!=0)return fail("ordinary attack accepted friendly target");
        }
        issueRuntimeAttackCommand(s,*findObjectById(s,target),forceFriendly);
        if (s.lastCommand.acceptedCount!=1) return fail("attack command rejected");
        int markedTick=-1,spawnTick=-1,bombTick=-1,damageTick=-1;
        std::set<int> planes,missiles;
        bool cleared=false,stopped=false,destroyed=false;
        bool markCaptured=false,flightCaptured=false;
        std::unique_ptr<RuntimeWindowState> loaded,precise;
        const auto* nativePlane=s.rules.findUnitType("AircraftType","BPLN");
        if(!nativePlane||nativePlane->speed!=16)return fail("original BPLN Speed16 unavailable");
        // Independent original Speed16 -> floor(16*256/100)=40 leptons.
        // Q16 ticks-per-cell introduces at most half one Q16 unit of denominator error.
        const double normalBound=1.0/(256.0*hz/(15.0*40.0)-0.5/65536.0)+1e-9;
        for (int step=0;step<hz*35;++step) {
            const auto beforeFlight=runtimeSupportAircraftSmokePoses(s);
            tick(s,caller);
            const auto afterFlight=runtimeSupportAircraftSmokePoses(s);
            runtimeSupportAircraftSmokeTrace(motionCsv?&motionCsv:nullptr,scene,"normal",s.tick,afterFlight,beforeFlight,normalBound);
            for(const auto& [id,p]:afterFlight) {
                const auto old=beforeFlight.find(id);++motionSamples;
                if(old!=beforeFlight.end()&&(std::hypot(p.x-old->second.x,p.y-old->second.y)>normalBound
                    ||std::abs(p.z-old->second.z)>1e-9))motionProblem("normal raw-speed/height bound id="+std::to_string(id));
            }
            std::string anchorError;
            if(!runtimeSupportAircraftSmokeAnchors(s,anchorError))motionProblem(anchorError);
            if(precise) {
                tick(*precise,caller);
                if(!runtimeSupportAircraftSmokeEqual(afterFlight,runtimeSupportAircraftSmokePoses(*precise),anchorError))
                    motionProblem("canonical paired continuation "+anchorError);
                ++precisePairs;
            }
            if (loaded) {
                tick(*loaded,caller);
                std::string pairedError;
                if(!runtimeSupportAircraftSmokeEqual(runtimeSupportAircraftSmokePoses(s),runtimeSupportAircraftSmokePoses(*loaded),pairedError))
                    motionProblem("ordinary paired continuation "+pairedError);
                if (s.airstrikes.stateHash()!=loaded->airstrikes.stateHash()
                    ||health(s,target)!=health(*loaded,target))
                    return fail("save replay mismatch at tick="+std::to_string(s.tick));
            }
            const auto* mark=marker(s,target);
            if (mark&&markedTick<0) {
                markedTick=s.tick;
                if(forceFriendly) {
                    std::ostringstream encoded;s.airstrikes.save(encoded);
                    if(encoded.str().find("ForceFire=1") == std::string::npos)
                        return fail("strike lost explicit force-fire authority");
                }
            }
            if(forceFriendly&&!primary&&markedTick<0&&step>=hz*5
                &&s.airstrikes.strikes().empty())return fail("friendly airstrike never marked: "+s.simulationStatus);
            if (mark&&(!mark->hasBeamEndpoints||mark->targetX<=66.0||mark->intensity<=0
                ||mark->sourceScreenOffsetY>=0))
                return fail("building-center targeting endpoints missing");
            if (loaded&&bool(mark)!=bool(marker(*loaded,target))) return fail("loaded target tint mismatch");
            for (const auto& [token,b]:s.airstrikePlanesByToken) {
                planes.insert(token); if (spawnTick<0) spawnTick=s.tick;
                const auto* o=findObjectById(s,b.objectId);
                if (!o||o->type!="BPLN"||!o->authoredAircraft||!o->airborne)
                    return fail("native MIG plane actor missing");
            }
            // Destruction fragments inherit the causal airstrike identity,
            // but only generation-zero actors are the planes' missiles.
            for (const auto& p:s.projectiles) if (p.airstrikeStrikeId>0&&p.generation==0) {
                missiles.insert(p.actorId); if (bombTick<0) bombTick=s.tick;
                if (p.weaponId!=(mode==1?"Maverick3E":"Maverick3")||p.projectileId!="AirToGroundMissile")
                    return fail("MIG missile profile changed: weapon="+p.weaponId+" projectile="+p.projectileId
                        +" generation="+std::to_string(p.generation)+" callerRank="
                        +std::to_string(findObjectById(s,caller)->veterancy)+" planes="+std::to_string(planes.size()));
            }
            if (!health(s,target)) return fail("target identity lost");
            if (*health(s,target)<initialHealth&&damageTick<0) damageTick=s.tick;
            if (mark&&hz==30&&mode==0&&!root.empty()&&!markCaptured) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,65},960,640,error,
                    root+"/boris-red-designator.bmp",false,false);
                if (!stats||!stats->rendered||stats->specialMechanismRenderActors<2)
                    return fail("target tint/laser frame failed: "+error);
                markCaptured=true; ++captures;
                auto unmarked=std::move(s.airstrikes); s.airstrikes.reset();
                const auto control=drawRuntimeStateOffscreenAtCell(s,{64,65},960,640,error,
                    root+"/boris-untinted-control.bmp",false,false);
                s.airstrikes=std::move(unmarked); refreshRuntimeSpecialMechanismRenderSnapshots(s);
                if (!control||!control->rendered) return fail("untinted control failed");
                ++captures;
            }
            const int expectedArrival=s.airstrikes.strikes().empty()?INT_MAX
                :s.airstrikes.strikes().front().planes.front().bombTick;
            if (hz==30&&mode==0&&spawnTick>=0&&bombTick<0&&s.tick>=expectedArrival-hz/4
                &&!root.empty()&&!flightCaptured) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,65},960,640,error,
                    root+"/boris-mig-approach.bmp",false,false);
                if (!stats||!stats->rendered) return fail("MIG approach frame failed");
                flightCaptured=true; ++captures;
            }
            if (mode==5&&markedTick>=0&&!destroyed) {
                RuntimeObjectExitRequest exit; exit.objectId=target; exit.tick=s.tick;
                exit.kind=RuntimeObjectExitKind::ScriptRemoved; exit.reason="airstrike target removed smoke";
                exit.suppressDeathWeapon=true; exit.suppressCrew=true;
                if (queueRuntimeObjectExit(s,exit)!=RuntimeObjectExitQueueResult::Queued) return fail("remove target rejected");
                drainRuntimeObjectExitQueue(s); destroyed=true;
            }
            if (mode==6&&markedTick>=0&&!stopped) {
                applySelection(s,{caller},false,false); issueRuntimeStopCommand(s);
                refreshRuntimeSpecialMechanismRenderSnapshots(s);
                const auto* after=marker(s,target);
                if (after&&after->cell.x>=0) return fail("Stop retained designator beam");
                stopped=true;
            }
            const bool saveNow=(mode==2&&markedTick>=0&&spawnTick<0)
                ||(mode==3&&spawnTick>=0&&s.tick>=spawnTick+hz&&bombTick<0)
                ||(mode==4&&bombTick>=0);
            if (saveNow&&!loaded) {
                std::ostringstream text;
                if (!writeRuntimeStateToStream(s,text,error)) return fail("save: "+error);
                IniFile saved; if (!saved.loadFromString(text.str())) return fail("save parse");
                loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
                if (!materializeRuntimeLoadStateFromIni(*loaded,saved,error)) return fail("restore: "+error);
                refreshRuntimeSpecialMechanismRenderSnapshots(*loaded);
                if (s.airstrikes.stateHash()!=loaded->airstrikes.stateHash()) return fail("restore core identity");
                std::unique_ptr<RuntimeWindowState> ordinary;
                if(!runtimeSupportAircraftSmokeCheckpoint(s,motionRoot,"boris-"+std::to_string(hz)+"-"+std::to_string(mode),
                    ordinary,precise,error))return fail("actual ordinary/canonical checkpoint: "+error);
                loaded=std::move(ordinary);
                if(mode==3) {
                    std::unique_ptr<RuntimeWindowState> navProbe,cancelled;
                    if(!runtimeSupportAircraftSmokeCheckpoint(s,motionRoot,"boris-probe-"+std::to_string(hz),navProbe,cancelled,error))
                        return fail("native inflight probe clone: "+error);
                    const auto navBefore=runtimeSupportAircraftSmokePoses(*navProbe);
                    bool fractional=false;for(const auto& [id,p]:navBefore)
                        fractional|=std::abs(p.x-std::round(p.x))>1e-6||std::abs(p.y-std::round(p.y))>1e-6;
                    if(!fractional)return fail("mode3 did not reach a real fractional BPLN pose");
                    initializeRuntimeNavigation(*navProbe);const auto navAfter=runtimeSupportAircraftSmokePoses(*navProbe);
                    ++navChecks;
                    runtimeSupportAircraftSmokeTrace(motionCsv?&motionCsv:nullptr,scene,"navigation-rebuild",s.tick,navAfter,navBefore,0.0);
                    if(!runtimeSupportAircraftSmokeEqual(navBefore,navAfter,error))motionProblem("navigation same-tick jump: "+error);
                    if(!runtimeSupportAircraftSmokeAnchors(*navProbe,error))motionProblem("navigation anchor: "+error);
                    // Preserve mode6 Stop. This separate genuine in-flight clone loses
                    // its target by the same ScriptRemoved path as original mode5.
                    RuntimeObjectExitRequest exit;exit.objectId=target;exit.tick=cancelled->tick;
                    exit.kind=RuntimeObjectExitKind::ScriptRemoved;exit.reason="inflight airstrike target loss continuity";
                    exit.suppressDeathWeapon=true;exit.suppressCrew=true;
                    if(queueRuntimeObjectExit(*cancelled,exit)!=RuntimeObjectExitQueueResult::Queued)return fail("inflight target loss rejected");
                    drainRuntimeObjectExitQueue(*cancelled);
                    std::unique_ptr<RuntimeWindowState> cancelledOrdinary,cancelledPrecise;
                    bool egressSeen=false;int pairedEgress=0;
                    std::map<int,std::array<double,4>> egressLines;
                    for(int egressStep=0;egressStep<24;++egressStep) {
                        const auto old=runtimeSupportAircraftSmokePoses(*cancelled);tick(*cancelled,caller);
                        const auto now=runtimeSupportAircraftSmokePoses(*cancelled);
                        runtimeSupportAircraftSmokeTrace(motionCsv?&motionCsv:nullptr,scene,"target-loss-egress",cancelled->tick,now,old,-1.0);
                        bool entered=false;
                        for(const auto& [id,p]:now) {
                            const auto previous=old.find(id);
                            if(p.phase==static_cast<int>(RuntimeAirstrikePlanePhase::Egress)&&previous!=old.end()
                                &&previous->second.phase!=p.phase) {
                                entered=true;const auto b=cancelled->airstrikePlanesByToken.find(p.token);
                                if(b==cancelled->airstrikePlanesByToken.end()||b->second.egressStartTick!=cancelled->tick
                                    ||b->second.despawnTick-b->second.egressStartTick!=20)return fail("target-loss original 20-tick phase changed");
                                egressLines[id]={previous->second.x,previous->second.y,double(b->second.route.exit.x),double(b->second.route.exit.y)};
                                if(std::hypot(p.x-previous->second.x,p.y-previous->second.y)>1e-9
                                    ||std::abs(p.z-previous->second.z)>1e-9)motionProblem("target-loss first-egress reanchor id="+std::to_string(id));
                            }
                        }
                        for(const auto& [id,line]:egressLines) {
                            const auto p=now.find(id),previous=old.find(id);if(previous==old.end())continue;
                            const double dx=line[2]-line[0],dy=line[3]-line[1],distance=std::hypot(dx,dy),phaseStep=distance/20.0;
                            if(p!=now.end()&&previous->second.phase==static_cast<int>(RuntimeAirstrikePlanePhase::Egress)) {
                                const auto& body=p->second;
                                if(std::abs(std::hypot(body.x-previous->second.x,body.y-previous->second.y)-phaseStep)>1e-9
                                    ||std::abs((body.x-line[0])*dy-(body.y-line[1])*dx)>1e-8
                                    ||std::abs(body.z-previous->second.z)>1e-9)
                                    motionProblem("target-loss original 20-clock line continuity id="+std::to_string(id));
                            } else if(p==now.end()&&std::hypot(line[2]-previous->second.x,line[3]-previous->second.y)>phaseStep+1e-9)
                                motionProblem("target-loss disappeared before native exit endpoint id="+std::to_string(id));
                        }
                        if(!runtimeSupportAircraftSmokeAnchors(*cancelled,error))motionProblem("target-loss anchor: "+error);
                        if(cancelledOrdinary) {
                            tick(*cancelledOrdinary,caller);tick(*cancelledPrecise,caller);
                            if(!runtimeSupportAircraftSmokeEqual(now,runtimeSupportAircraftSmokePoses(*cancelledOrdinary),error)
                                ||!runtimeSupportAircraftSmokeEqual(now,runtimeSupportAircraftSmokePoses(*cancelledPrecise),error))
                                motionProblem("target-loss paired continuation: "+error);
                            if(!now.empty())++pairedEgress;
                        }
                        if(entered&&!egressSeen) {
                            egressSeen=true;++targetLossChecks;
                            if(!runtimeSupportAircraftSmokeCheckpoint(*cancelled,motionRoot,"boris-target-loss-"+std::to_string(hz),
                                cancelledOrdinary,cancelledPrecise,error))return fail("target-loss actual checkpoint: "+error);
                        }
                    }
                    if(!egressSeen||pairedEgress<12||!cancelled->airstrikePlanesByToken.empty())
                        return fail("target-loss native phase/pair/cleanup not completed");
                }
                if (!root.empty()) { std::ofstream file(root+"/boris-"+std::to_string(hz)+"-"+std::to_string(mode)+".ini"); file<<text.str(); }
                ++saves;
            }
            if (markedTick>=0&&!mark&&s.airstrikes.strikes().empty()&&s.airstrikePlanesByToken.empty()) {
                cleared=true; break;
            }
            if (primary&&damageTick>=0) break;
        }
        if (primary) {
            if (markedTick>=0||!planes.empty()||damageTick<0) return fail("infantry attack called airstrike or did not shoot");
        } else if (mode==5) {
            if (!destroyed||!cleared||!planes.empty()||!missiles.empty()) return fail("removed target retained attack");
        } else {
            const int expected=mode==1?4:2;
            if (markedTick<0||spawnTick<=markedTick||bombTick<=spawnTick+hz||damageTick<bombTick
                ||planes.size()!=static_cast<std::size_t>(expected)||missiles.empty()||!cleared
                ||s.airstrikes.nextStrikeId()!=2||s.airstrikes.callerReadyTick(caller)<=s.tick)
                return fail("flight sequence/count/cooldown: mark="+std::to_string(markedTick)
                    +" spawn="+std::to_string(spawnTick)+" bomb="+std::to_string(bombTick)
                    +" damage="+std::to_string(damageTick)+" planes="+std::to_string(planes.size())
                    +" missiles="+std::to_string(missiles.size())+" cleared="+std::to_string(cleared)
                    +" status="+s.simulationStatus);
            if (mode>=2&&mode<=4&&!loaded) return fail("save phase not reached");
        }
        if(const char* legacy=std::getenv("CONCORD_SUPPORT_AIRCRAFT_LEGACY_DIR")) {
            const char* selectedMode=std::getenv("CONCORD_BORIS_SMOKE_MODE");
            if(!*legacy||(selectedMode&&std::atoi(selectedMode)!=3))return fail("legacy compatibility requires full catalogue or selected mode3");
            if(mode==3&&!runtimeSupportAircraftSmokeLegacyCheck(s,legacy,hz,tick,error))return fail("legacy actual compatibility: "+error);
        }
        std::cout<<"Boris airstrike scene: "<<scene<<" mark="<<markedTick<<" spawn="<<spawnTick
            <<" bomb="<<bombTick<<" damage="<<damageTick<<" planes="<<planes.size()
            <<" missiles="<<missiles.size()<<" clear="<<cleared<<" PASS\n"; ++cases;
    }
    std::cout<<"Boris airstrike: passed cases="<<cases<<" saves="<<saves<<" captures="<<captures<<'\n';
    std::cout<<"Boris continuity: samples="<<motionSamples<<" nav="<<navChecks<<" targetLoss="<<targetLossChecks
        <<" precisePairs="<<precisePairs<<" violations="<<motionFailures.size()<<'\n';
    const bool selected=std::getenv("CONCORD_BORIS_SMOKE_MODE")!=nullptr;
    if(cases!=(selected?3:forceFriendly?27:24)||(!selected&&(navChecks!=3||targetLossChecks!=3)))
        return fail("original scene/continuity catalogue incomplete");
    if(!motionFailures.empty())return fail("continuity violations="+std::to_string(motionFailures.size())+" first="+motionFailures.front());
    return true;
}
