// Issue25/8: original Tanya, real UI orders and continuous outside C4 approach.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeTanyaC4ApproachIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;std::string label="original-input",stage;
    const auto fail=[&](const std::string& why){error="Issue25 Tanya C4: "+label+" ["+stage+"]: "+why;return false;};
    const char* out=std::getenv("CONCORD_TANYA_C4_APPROACH_ISSUE25_OUTPUT");
    const std::filesystem::path root=out&&*out?out:"build/smoke/feature-set-25/08-tanya-c4-approach";
    std::filesystem::create_directories(root);
    const auto quote=[](const std::string& v){std::string q="\"";for(char c:v){if(c=='"')q+='"';q+=c;}return q+'"';};
    const auto sha=[](std::span<const std::uint8_t> v){const auto h=runtimeMultiplayerSnapshotSha256(v);std::ostringstream o;o<<std::hex<<std::setfill('0');for(auto b:h)o<<std::setw(2)<<unsigned(b);return o.str();};
    const auto read=[](const std::string& p){std::ifstream in(p,std::ios::binary);return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};};
    const auto asset=std::filesystem::path(runtimeAssetRootPath(live.config));
    const auto rulesPath=pathString(asset/"ra2md/localmd/rulesmd.ini"),artPath=pathString(asset/"ra2md/localmd/artmd.ini");
    IniFile raw,art;
    if(sha(read(rulesPath))!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        ||!raw.loadFromFile(rulesPath)||!art.loadFromFile(artPath))return fail("original rules/ART input");
    const auto* tanya=live.rules.findUnitType("InfantryType","TANY");
    if(!tanya||!tanya->c4||tanya->speed!=6||tanya->strength!=200||raw.getValue("TANY","C4")!="yes"
        ||raw.getValue("TANY","Speed")!="6"||raw.getValue("TANY","Strength")!="200"
        ||raw.getValue("TANY","Locomotor")!="{4A582744-9839-11d1-B709-00A024DDAFD1}"
        ||raw.getValue("CombatDamage","C4Delay")!=".03"||art.getValue("TanyaSequence","Walk")!="8,6,6")return fail("original Tanya/C4/Walk declarations");
    struct Spec {const char* id;int w,h;};const std::array<Spec,3> specs{{{"GAPOWR",2,2},{"YAGNTC",3,3},{"GAREFN",4,3}}};
    for(const auto& p:specs)if(!live.rules.findBuildingType(p.id)||art.getValue(p.id,"Foundation")!=std::to_string(p.w)+"x"+std::to_string(p.h))return fail("original foundation "+std::string(p.id));
    const std::string c4Warhead=raw.getValue("CombatDamage","C4Warhead");if(c4Warhead.empty())return fail("original C4 warhead");
    // Pin the original textual minute value and independently preserve the
    // existing double conversion/truncation contract. At30Hz .03*60 then *30
    // is53.99999999999999, so floor is53; no epsilon or nearest rounding.
    const double c4Minutes=std::stod(raw.getValue("CombatDamage","C4Delay"));
    const double c4Seconds=c4Minutes*60.0;
    const double c4RawTicks=c4Seconds*30.0;
    const int expectedC4Ticks=static_cast<int>(std::floor(c4RawTicks));
    if(expectedC4Ticks!=53||live.rules.getSpecialMechanismRules().c4DelayMinutes!=c4Minutes)
        return fail("original raw C4 minutes/loaded value or fixed30Hz truncated delay");
    // Ordinary post-plant Guard fire is part of the real full simulation.
    // Pin its independent raw source instead of calling every HP change C4.
    if(raw.getValue("TANY","Primary")!="DoublePistols"||raw.getValue("DoublePistols","Damage")!="125"
        ||raw.getValue("DoublePistols","Warhead")!="HollowPoint2"||raw.getValue("DoublePistols","Projectile")!="InvisibleLow"
        ||raw.getValue("DoublePistols","ROF")!="5"||raw.getValue("HollowPoint2","Verses")!="100%,100%,100%,0%,0%,0%,1%,1%,1%,1%,100%"
        ||raw.getValue("GAPOWR","Armor")!="wood"||raw.getValue("YAGNTC","Armor")!="concrete"||raw.getValue("GAREFN","Armor")!="wood")
        return fail("original post-plant Tanya bullet/three-building armor declarations");
    const int ordinaryBulletDamage=static_cast<int>(std::floor(std::stod(raw.getValue("DoublePistols","Damage"))*.01));
    if(ordinaryBulletDamage!=1)return fail("independent original 125 x 1percent integer bullet damage");
    std::ofstream states(root/"states.csv"),cases(root/"cases.csv"),saves(root/"checkpoints.csv"),sources(root/"sources.txt"),hits(root/"damage.csv");
    if(!states||!cases||!saves||!sources||!hits)return fail("private evidence output");
    states<<std::setprecision(17)<<"case,stage,replica,tick,actor,navx,navy,level,subcell,render,active,footx,footy,footlevel,delta,inside,mission,behavior,goalx,goaly,path,allowed,cooldown,pending,armed,attached,deadline,cmdvalid,accepted,rejected,status,targetHP\n";
    cases<<"case,type,direction,relation,map_anchor,control,attached,deadline,finish_tick,source_alive,target_hp,motion_ticks,max_delta,passed\n";
    saves<<"case,stage,format,tick,source,footx,footy,pending,bombs,passed\n";
    hits<<"case,stage,replica,tick,beforeHP,afterHP,delta,contract,simulationStatus,raw_weapon,raw_warhead,original_single_hit\n";
    sources<<"Original rules "<<rulesPath<<" SHA "<<sha(read(rulesPath))<<"\nOriginal ART "<<artPath<<" SHA "<<sha(read(artPath))
        <<"\nTANY C4=yes Strength200 Speed6 Walk8,6,6; original raw C4Delay=.03 minutes; existing double conversion truncates at30Hz.\n"
        <<std::setprecision(17)<<"C4 raw_minutes="<<c4Minutes<<" raw_seconds="<<c4Seconds<<" raw_ticks="<<c4RawTicks<<" floor_ticks="<<expectedC4Ticks<<"\n"
        <<"Post-plant Guard DoublePistols Damage125/ROF5/InvisibleLow/HollowPoint2: original wood,steel,concrete Verses1%; GAPOWR and GAREFN Armorwood, YAGNTC Armorconcrete; integer single hit1. Natural shots remain enabled; early C4 requires persistent identical timer and live target, not full original HP.\n"
        <<"Foundation oracle uses original cell centers: (64-.5,64+W-.5) x (64-.5,64+H-.5), strict inside.\n"
        <<"Ordinary tick continuity bound includes an adjacent canonical subcell change; same-tick replacement must preserve actual foot exactly.\n"
        <<"All fixtures retain full terrain MapFacts; core adds an original-dimension structure anchor, fallback is a runtime-built target absent from the immutable structure snapshot.\n";sources.flush();
    auto metadata=runtimeTerrainMetadataContext(live);RuntimeMapRenderCache tileCache;std::optional<TerrainCell> flat;
    for(const auto& set:live.theater.getTileSets())for(int i=set.startTileIndex;i<set.startTileIndex+set.tilesInSet&&!flat;++i)
        for(int sub=0;sub<16&&!flat;++sub){TerrainCell c;c.tileIndex=i;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,c);
            if(info.passable&&!info.water&&!info.bridge&&!info.shore&&!info.cliff&&resolveRuntimeTerrainSubTileRampType(c,metadata,tileCache)==0)flat=c;}
    if(!flat)return fail("original flat land");
    struct Foot {double x=0,y=0,z=0;};
    const auto foot=[](const WorldObjectInstance& o){const auto slot=worldObjectSubCellOffset(o);return Foot{runtimeObjectRenderX(o)+slot.x,runtimeObjectRenderY(o)+slot.y,worldObjectHasGroundRenderPose(o)?o.renderLevel:double(o.level)};};
    const auto distance=[](Foot a,Foot b){return std::hypot(a.x-b.x,a.y-b.y);};
    const auto inside=[](const Spec& p,Foot a){return a.x>63.5+1e-9&&a.x<63.5+p.w-1e-9&&a.y>63.5+1e-9&&a.y<63.5+p.h-1e-9;};
    // Original ReadSpeed floor(6*256/100)=15 leptons/rule-frame. A cell
    // segment takes at least floor(256*30/(15*15))=34 ticks. Allow the
    // maximum normal adjacent-slot displacement without tolerating .25+
    // jumps caused by dropping a live body at its committed logical cell.
    const double maximumTickDelta=(std::sqrt(2.0)+std::sqrt(.5))/(256*30/(15*15))+1e-6;
    struct Scene {std::unique_ptr<RuntimeWindowState> state;Spec spec;int actor=-1,target=-1,direction=0;bool own=false,anchor=false;std::optional<Foot> legacyRecoverySource;std::unique_ptr<RuntimeWindowState> legacyPeer;};
    const std::array<RuntimeCellPoint,8> offsets{{{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}}};
    const auto make=[&](Spec spec,int direction,bool own,bool anchored){
        Scene f;f.spec=spec;f.direction=direction;f.own=own;f.anchor=anchored;f.state=std::make_unique<RuntimeWindowState>();auto& s=*f.state;
        seedRuntimeLoadScratch(s,live,false);s.config.fixedTickHz=30;s.config.playerCountry="Americans";s.config.playerSlots.clear();s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=s.config.crates=s.config.superweapons=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.campaignMode=s.skirmishMatchFinished=false;s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();
        s.skirmishTeamByOwner.clear();s.skirmishCountryByOwner.clear();s.skirmishDifficultyByOwner.clear();s.triggers={};s.triggers.bootstrapComplete=true;
        s.economy={};s.economy.owner="Americans";s.economy.credits=10000;s.economy.skirmishStartApplied=true;s.initialOreFieldsSeeded=true;s.tick=200;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=true;s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
        for(int y=48;y<=88;++y)for(int x=48;x<=88;++x){auto c=*flat;c.x=x;c.y=y;c.level=0;c.cellIndex512=runtimeCellKey(x,y);s.world.accessTerrainCells().push_back(c);
            {MapCellFacts v;v.x=x;v.y=y;v.cellIndex=c.cellIndex512;v.hasTerrain=true;v.terrain.x=x;v.terrain.y=y;v.terrain.tileIndex=c.tileIndex;v.terrain.subTileIndex=c.subTileIndex;
                v.terrainClass="clear";v.passabilityHint="passable";s.mapFacts.cellIndexLookup.emplace(v.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(v);}}
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x25080000+spec.w*100+spec.h+(anchored?10000:0);
        auto* target=addRuntimeObjectInstanceForOwner(s,own?"Americans":"Russians",RuntimeBuildKind::Building,spec.id,{64,64});
        if(!target)return f;f.target=target->instanceId;target->mapId="Issue25C4Target";target->mission="Sleep";target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        if(anchored){MapStructureAnchorFact a;a.id=target->mapId;a.type=spec.id;a.owner=target->owner;a.x=a.y=64;a.footprintWidth=spec.w;a.footprintHeight=spec.h;
            for(int y=63;y<=64+spec.h;++y)for(int x=63;x<=64+spec.w;++x)if(x==63||x==64+spec.w||y==63||y==64+spec.h){MapFootprintCellFact c;c.x=x;c.y=y;c.cellIndex=runtimeCellKey(x,y);c.inBounds=c.hasMapCell=c.approach=true;c.purpose="c4";a.approachCells.push_back(c);}
            s.mapFacts.structureAnchorIdLookup.emplace(toLowerAsciiLocal(a.id),s.mapFacts.structureAnchors.size());
            s.mapFacts.structureAnchors.push_back(std::move(a));}
        const auto d=offsets[direction];const RuntimeCellPoint start{d.x<0?58:d.x>0?64+spec.w+5:64+spec.w/2,d.y<0?58:d.y>0?64+spec.h+5:64+spec.h/2};
        auto* actor=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Infantry,"TANY",start);
        if(!actor)return f;f.actor=actor->instanceId;actor->mission="Sleep";actor->facing=runtimeFacingFromDelta(64-start.x,64-start.y);
        initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeHumanHouseAuthority a;a.owner=a.country="Americans";a.canonicalOwner="americans";a.localFacade=true;s.humanHouseAuthorities.push_back(std::move(a));
        initializeRuntimeOfflineGameplayRouting(s);s.selectedObjectIds={f.actor};return f;
    };    int fixtures=0,core=0,fallback=0,controls=0,ordinarySaves=0,canonicalSaves=0,bmps=0,pairedTicks=0,motionTicks=0,legacyRepairFrames=0,ordinaryHits=0;double largestDelta=0,legacyRepairDelta=0;
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto sample=[&](Scene& f,RuntimeWindowState& s,const char* replica,double delta){
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,f.actor);if(!o)return;
        const auto p=foot(*o);const auto b=s.behaviors.find(f.actor);const auto bomb=s.specialMechanisms.timedBombs().find(f.target);
        states<<quote(label)<<','<<quote(stage)<<','<<replica<<','<<s.tick<<','<<f.actor<<','<<o->x<<','<<o->y<<','<<o->level<<','<<o->subCell<<','<<o->hasRenderPosition<<','<<o->renderMoveActive
            <<','<<p.x<<','<<p.y<<','<<p.z<<','<<delta<<','<<inside(f.spec,p)<<','<<quote(o->mission)<<','<<(b==s.behaviors.end()?-1:int(b->second.state))
            <<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.x)<<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.y)<<',';
        std::string path;if(b!=s.behaviors.end())for(const auto cell:b->second.path)path+=std::to_string(cell.x)+":"+std::to_string(cell.y)+";";
        states<<quote(path)<<','<<(b==s.behaviors.end()?-2:b->second.pendingAllowedOccupiedObjectId)<<','<<(b==s.behaviors.end()?0:b->second.moveCooldownTicks)
            <<','<<s.pendingTimedBombIntents.contains(f.actor)<<','<<(bomb!=s.specialMechanisms.timedBombs().end())
            <<','<<(bomb==s.specialMechanisms.timedBombs().end()?-1:bomb->second.attachedTick)<<','<<(bomb==s.specialMechanisms.timedBombs().end()?-1:bomb->second.detonateTick)
            <<','<<s.lastCommand.valid<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<quote(s.interactionStatus+" / "+s.simulationStatus)<<','<<(findRuntimeObjectByIdIncludingInactive(s,f.target)?findRuntimeObjectByIdIncludingInactive(s,f.target)->health:-1)<<'\n';
    };
    const auto capture=[&](Scene& f,const std::string& name){auto& s=*f.state;const int width=800,height=600;const RECT viewport{0,0,width,height};
        struct Surface {RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);~Surface(){releaseRuntimeBackBuffer(buffer);if(screen)ReleaseDC(nullptr,screen);}} surface;
        if(!surface.screen||!ensureRuntimeBackBuffer(surface.buffer,surface.screen,width,height))return fail("actual BMP DIB");
        s.camera.initialized=true;s.camera.tileWidth=60;s.camera.tileHeight=30;s.camera.worldOffsetX=-400;s.camera.worldOffsetY=65*30-300;
        auto view=runtimeTerrainMetadataContext(s);view.rules=&s.rules;view.art=&s.art;
        view.palettePath=runtimePalettePath(s.config,s.report.map);view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.animationTick=s.tick;view.fixedTickHz=30;view.seeHiddenObjects=false;
        view.surfacePixels=static_cast<std::uint32_t*>(surface.buffer.pixels);view.surfaceWidth=width;view.surfaceHeight=height;
        const auto before=s.world.deterministicSimulationStateHash();
        if(!drawRuntimeMap(surface.buffer.memoryDc,viewport,s.world,view,s.camera,s.renderCache).rendered)return fail("actual original-material render");GdiFlush();
        if(s.world.deterministicSimulationStateHash()!=before)return fail("draw changed actual movement state");
        if(!writeRuntimeSmokeBmp(pathString(root/(label+"-"+name+".bmp")),view.surfacePixels,width,height,error))return false;++bmps;return true;
    };
    const auto physical=[&](Scene& f,RuntimeWindowState& s,Foot previous,const char* replica){
        const auto* o=findObjectById(s,f.actor);if(!o||o->health!=200)return fail("Tanya health/lifetime changed");const auto p=foot(*o);const auto delta=distance(previous,p);
        largestDelta=std::max(largestDelta,delta);if(delta>1e-10){++motionTicks;}
        sample(f,s,replica,delta);
        if(inside(f.spec,p)||std::abs(p.z)>1e-6||delta>maximumTickDelta){states.flush();
            if(&s==f.state.get())(void)capture(f,"physical-failure");
            return fail("actual foot entered original Foundation or jumped: foot="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)
                +" previous="+std::to_string(previous.x)+","+std::to_string(previous.y)+" delta="+std::to_string(delta)+" limit="+std::to_string(maximumTickDelta));}
        return true;
    };
    const auto command=[&](Scene& f,std::string& routeViolation){auto& s=*f.state;const auto* source=findObjectById(s,f.actor);const auto* target=findObjectById(s,f.target);
        if(!source||!target)return fail("real fixture objects unavailable");const auto before=foot(*source);s.selectedObjectIds={f.actor};
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,f.own?RuntimeMultiplayerGameplayTargetOrderKind::Attack:RuntimeMultiplayerGameplayTargetOrderKind::Primary,{target->x,target->y},target);
        const auto b=s.behaviors.find(f.actor);stage="C4-command";sample(f,s,"live",distance(before,foot(*findObjectById(s,f.actor))));states.flush();
        if(!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||s.lastCommand.verb!="c4"
            ||!s.pendingTimedBombIntents.contains(f.actor)||s.pendingTimedBombIntents.at(f.actor).targetObjectId!=f.target
            ||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move||b->second.forceFire!=f.own)return fail("actual UI C4 intent/Move missing");
        const auto after=foot(*findObjectById(s,f.actor));
        if(distance(before,after)>1e-6||std::abs(before.z-after.z)>1e-6){(void)capture(f,"command-foot-jump");return fail("same-tick C4 replacement dropped continuous foot: before="+std::to_string(before.x)+","+std::to_string(before.y)+" after="+std::to_string(after.x)+","+std::to_string(after.y));}
        // Defer an internal-route failure until an actual physical witness or
        // attachment is captured. This preserves all final assertions while
        // giving the unchanged baseline a user-visible body reproduction.
        if(b->second.pendingAllowedOccupiedObjectId!=-1)routeViolation="C4 granted whole-building passage";
        if(inside(f.spec,{double(b->second.moveTarget.x),double(b->second.moveTarget.y),0}))routeViolation+="; goal in original Foundation";
        for(auto c:b->second.path)if(inside(f.spec,{double(c.x),double(c.y),0})){routeViolation+="; path in original Foundation";break;}
        if(!routeViolation.empty()){sources<<label<<": deferred original route violation "<<routeViolation<<'\n';sources.flush();}
        return true;
    };
    const auto checkpoint=[&](Scene& f,RuntimeWindowState& s,bool canonical,bool legacyInput=false){
        std::ostringstream out,normalized;IniFile ordinary,exact;
        if(!writeRuntimeStateToStream(s,out,error)||!ordinary.loadFromString(out.str())||ordinary.getValue("RuntimeSave","Version")!="14")return std::unique_ptr<RuntimeWindowState>{};
        const auto ordinaryPath=pathString(root/(label+"-"+stage+"-ordinary14.ini"));
        {std::ofstream file(ordinaryPath,std::ios::binary);file<<out.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
        if(!ordinary.loadFromFile(ordinaryPath))return std::unique_ptr<RuntimeWindowState>{};
        if(canonical){if(!writeRuntimeMultiplayerSnapshotExtension(s,out,error)||!exact.loadFromString(out.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)||!exact.loadFromString(normalized.str()))return std::unique_ptr<RuntimeWindowState>{};
            const auto canonicalPath=pathString(root/(label+"-"+stage+"-canonical.ini"));
            {std::ofstream file(canonicalPath,std::ios::binary);file<<normalized.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
            if(!exact.loadFromFile(canonicalPath))return std::unique_ptr<RuntimeWindowState>{};}
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
        {RuntimeSnapshotActiveStateScope active(*copy);if(!materializeRuntimeLoadStateFromIni(*copy,ordinary,error)
            ||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return std::unique_ptr<RuntimeWindowState>{};}
        const auto* a=findObjectById(s,f.actor);const auto* b=findObjectById(*copy,f.actor);
        const auto p=a?foot(*a):Foot{},q=b?foot(*b):Foot{};
        bool intentOkay=true;
        if(s.pendingTimedBombIntents.contains(f.actor)){const auto pending=copy->pendingTimedBombIntents.find(f.actor);const auto move=copy->behaviors.find(f.actor);
            intentOkay=pending!=copy->pendingTimedBombIntents.end()&&pending->second.targetObjectId==f.target&&move!=copy->behaviors.end()
                &&move->second.kind==RuntimeBehaviorKind::Move&&move->second.forceFire==f.own
                &&move->second.pendingAllowedOccupiedObjectId==(legacyInput?f.target:-1);
            if(legacyInput){const auto original=s.behaviors.find(f.actor);
                intentOkay=intentOkay&&original!=s.behaviors.end()&&move!=copy->behaviors.end()
                    &&move->second.targetObjectId==original->second.targetObjectId&&move->second.moveTarget==original->second.moveTarget
                    &&move->second.renderFrom==original->second.renderFrom&&move->second.renderTo==original->second.renderTo
                    &&move->second.renderMoveTicksTotal==original->second.renderMoveTicksTotal
                    &&move->second.renderMoveTicksRemaining==original->second.renderMoveTicksRemaining
                    &&move->second.moveCooldownTicks==original->second.moveCooldownTicks
                    &&move->second.renderFromOffsetX==original->second.renderFromOffsetX&&move->second.renderFromOffsetY==original->second.renderFromOffsetY
                    &&move->second.renderToOffsetX==original->second.renderToOffsetX&&move->second.renderToOffsetY==original->second.renderToOffsetY;
            }}
        const bool okay=intentOkay&&a&&b&&copy->tick==s.tick&&distance(p,q)<=1e-6&&std::abs(p.z-q.z)<=1e-6
            &&copy->specialMechanisms.stateHash()==s.specialMechanisms.stateHash()&&copy->pendingTimedBombIntents.size()==s.pendingTimedBombIntents.size();
        saves<<quote(label)<<','<<stage<<','<<(canonical?"canonical":"ordinary14")<<','<<s.tick<<','<<f.actor<<','<<q.x<<','<<q.y<<','<<copy->pendingTimedBombIntents.size()<<','<<copy->specialMechanisms.timedBombs().size()<<','<<okay<<'\n';saves.flush();
        if(!okay){error="actual approach/armed checkpoint changed foot or bomb authority";return std::unique_ptr<RuntimeWindowState>{};}
        copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
        initializeRuntimeOfflineGameplayRouting(*copy);canonical?++canonicalSaves:++ordinarySaves;return copy;
    };    const auto run=[&](Scene f,bool save,bool screenshots,const std::string& control=""){
        if(!f.state||f.actor<0||f.target<0)return fail("original fixture creation");auto& s=*f.state;
        if(screenshots&&!capture(f,"before"))return false;
        const bool legacyInput=control=="legacy-outside-checkpoint"||control=="legacy-inside-runtime";
        const bool exactStopped=control=="stopped-same-cell";
        const auto stoppedFoot=foot(*findObjectById(s,f.actor));
        std::string routeViolation;
        if(legacyInput){const auto b=s.behaviors.find(f.actor);stage=f.legacyRecoverySource?"manual-legacy-runtime-input":"restored-manual-legacy-input";sample(f,s,"live",0);
            if(!s.pendingTimedBombIntents.contains(f.actor)||s.pendingTimedBombIntents.at(f.actor).targetObjectId!=f.target
                ||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move||b->second.targetObjectId!=f.target
                ||b->second.pendingAllowedOccupiedObjectId!=f.target)return fail("explicit old C4 input lost its accepted authority");
        } else if(!command(f,routeViolation))return false;
        if(exactStopped){const auto b=s.behaviors.find(f.actor);const auto* a=findObjectById(s,f.actor);
            if(b==s.behaviors.end()||b->second.moveTarget!=RuntimeCellPoint{a->x,a->y}||!b->second.path.empty()
                ||a->renderMoveActive||!a->hasRenderPosition||distance(stoppedFoot,foot(*a))>1e-6)
                return fail("same-cell C4 discarded the exact stopped exterior foot or invented an approach path");}
        if(control=="blocked-nearest"){
            // Original3x3 cells64..66 and retained C4 range2.001 make65,68
            // uniquely closest to the original south start65,72 (distance4).
            // The former blocker65,67 was behind this closer candidate.
            if(s.behaviors.at(f.actor).moveTarget==RuntimeCellPoint{65,68})return fail("blocked nearest outside candidate was chosen as a passable goal");
            for(auto p:s.behaviors.at(f.actor).path)if(p==RuntimeCellPoint{65,68})return fail("outside C4 path crossed the actual nearest-cell blocker");
        }
        std::unique_ptr<RuntimeWindowState> ordinary,canonical=std::move(f.legacyPeer);bool approachSaved=false,armedSaved=false;int attached=-1,deadline=-1;
        const auto replicas=[&](){std::vector<RuntimeWindowState*> v{&s};if(ordinary)v.push_back(ordinary.get());if(canonical)v.push_back(canonical.get());return v;};
        const auto same=[&](){const auto* a=findObjectById(s,f.actor);const auto* target=findRuntimeObjectByIdIncludingInactive(s,f.target);
            if(!a||!target)return false;const auto p=foot(*a);
            for(auto* q:replicas()){const auto* b=findObjectById(*q,f.actor);const auto* t=findRuntimeObjectByIdIncludingInactive(*q,f.target);
                if(!b||!t||q->tick!=s.tick||b->health!=a->health||t->health!=target->health||t->alive!=target->alive
                    ||distance(p,foot(*b))>1e-6||std::abs(p.z-foot(*b).z)>1e-6||q->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
                    ||q->pendingTimedBombIntents.size()!=s.pendingTimedBombIntents.size())return false;}
            return true;};
        const int initialMotion=motionTicks;
        for(int wait=0;wait<1800;++wait){stage="real-approach";
            for(auto* q:replicas()){const auto previous=foot(*findObjectById(*q,f.actor));step(*q);
                if(wait==0&&q==&s&&f.legacyRecoverySource){
                    // Only this explicitly authored old illegal interior input
                    // may move farther on its first compatibility repair frame.
                    // Its independent recovery oracle is the empty ordinary
                    // old segment source65,67/slot3/foot64.75,67.25, never a production helper.
                    stage="legacy-interior-first-repair";const auto* a=findObjectById(s,f.actor);if(!a||a->health!=200)return fail("old input repair lost Tanya");const auto p=foot(*a);
                    const auto b=s.behaviors.find(f.actor);const bool armed=s.specialMechanisms.timedBombs().contains(f.target);
                    legacyRepairDelta=distance(previous,p);sample(f,s,"live-legacy-repair",legacyRepairDelta);states.flush();
                    if(!inside(f.spec,previous)||a->x!=65||a->y!=67||a->subCell!=3||inside(f.spec,p)
                        ||distance(p,*f.legacyRecoverySource)>1e-6||std::abs(p.z)>1e-6||a->renderMoveActive
                        ||(!armed&&(!s.pendingTimedBombIntents.contains(f.actor)||b==s.behaviors.end()
                            ||b->second.pendingAllowedOccupiedObjectId!=-1)))return fail("old interior runtime input did not recover to its exact ordinary outside source");
                    ++legacyRepairFrames;if(screenshots&&!capture(f,"legacy-recovered"))return false;stage="real-approach";
                } else if(!physical(f,*q,previous,q==&s?"live":q==ordinary.get()?"ordinary14":"canonical"))return false;
                if(exactStopped&&(distance(stoppedFoot,foot(*findObjectById(*q,f.actor)))>1e-6
                    ||std::abs(stoppedFoot.z-foot(*findObjectById(*q,f.actor)).z)>1e-6))return fail("same-cell planting moved the stopped body");
                if(q!=&s)++pairedTicks;}
            if(!same())return fail("actual saved approach foot/authority diverged");
            const auto* source=findObjectById(s,f.actor);const auto b=s.behaviors.find(f.actor);
            if(s.pendingTimedBombIntents.contains(f.actor)&&b!=s.behaviors.end()){
                if(control=="blocked-nearest"){
                    if(b->second.moveTarget==RuntimeCellPoint{65,68})return fail("C4 retargeted the actual nearest-cell blocker");
                    for(auto p:b->second.path)if(p==RuntimeCellPoint{65,68})return fail("C4 retarget path crossed the actual nearest-cell blocker");
                }
                if(b->second.pendingAllowedOccupiedObjectId!=-1)routeViolation="C4 reacquired whole-building passage";
                if(inside(f.spec,{double(b->second.moveTarget.x),double(b->second.moveTarget.y),0}))routeViolation+="; retarget goal inside Foundation";
                for(auto c:b->second.path)if(inside(f.spec,{double(c.x),double(c.y),0})){routeViolation+="; retarget path inside Foundation";break;}
            }
            const auto bomb=s.specialMechanisms.timedBombs().find(f.target);
            if(bomb!=s.specialMechanisms.timedBombs().end()){
                attached=bomb->second.attachedTick;deadline=bomb->second.detonateTick;stage="attached";
                const auto* target=findObjectById(s,f.target);
                if(!routeViolation.empty()){states.flush();(void)capture(f,"route-failure-attached");return fail(routeViolation);}
                if(!target||bomb->second.kind!=TimedBombKind::C4||bomb->second.sourceObjectId!=f.actor||bomb->second.owner!="Americans"
                    ||bomb->second.damage!=std::numeric_limits<int>::max()||!bomb->second.forceDestruction||bomb->second.warheadId!=c4Warhead
                    ||deadline-attached!=expectedC4Ticks||s.pendingTimedBombIntents.contains(f.actor)||target->health!=s.rules.findBuildingType(f.spec.id)->strength){
                    std::ostringstream diagnostic;diagnostic<<"original actual armed C4 identity/exact truncated deadline expectedDelay="<<expectedC4Ticks
                        <<" source="<<bomb->second.sourceObjectId<<" owner="<<bomb->second.owner<<" kind="<<int(bomb->second.kind)
                        <<" damage="<<bomb->second.damage<<" force="<<bomb->second.forceDestruction<<" warhead="<<bomb->second.warheadId
                        <<" attached="<<attached<<" deadline="<<deadline<<" actualDelay="<<deadline-attached
                        <<" targetHP="<<(target?target->health:-1)<<" expectedTargetHP="<<s.rules.findBuildingType(f.spec.id)->strength
                        <<" pending="<<s.pendingTimedBombIntents.contains(f.actor);return fail(diagnostic.str());}
                if(screenshots&&!capture(f,"attach"))return false;
                if(save){ordinary=checkpoint(f,s,false);if(!ordinary)return fail(error);canonical=checkpoint(f,s,true);if(!canonical)return fail(error);armedSaved=true;}
                break;
            }
            if(save&&!approachSaved&&source&&source->renderMoveActive&&b!=s.behaviors.end()
                &&b->second.renderMoveTicksRemaining>1&&b->second.renderMoveTicksRemaining<b->second.renderMoveTicksTotal){
                stage="approach-save";ordinary=checkpoint(f,s,false);if(!ordinary)return fail(error);canonical=checkpoint(f,s,true);if(!canonical)return fail(error);approachSaved=true;
            }
            const auto* target=findObjectById(s,f.target);if(!target||target->health!=s.rules.findBuildingType(f.spec.id)->strength)return fail("target damaged before actual planting");
        }
        states.flush();if(attached<0||((!exactStopped&&!f.legacyRecoverySource)&&motionTicks==initialMotion)||(save&&(!approachSaved||!armedSaved)))return fail("real approach/armed/save witness unavailable");
        stage="after-attach";
        while(s.tick<deadline){for(auto* q:replicas()){
                const auto previous=foot(*findObjectById(*q,f.actor));const auto* oldTarget=findObjectById(*q,f.target);
                if(!oldTarget)return fail("C4 target was not alive before its exact deadline");const int beforeHP=oldTarget->health;
                step(*q);const char* replica=q==&s?"live":q==ordinary.get()?"ordinary14":"canonical";
                if(!physical(f,*q,previous,replica))return false;
                const auto* newTarget=findRuntimeObjectByIdIncludingInactive(*q,f.target);const int afterHP=newTarget?newTarget->health:-1;
                const int damage=beforeHP-afterHP;
                if(damage!=0){hits<<quote(label)<<','<<stage<<','<<replica<<','<<q->tick<<','<<beforeHP<<','<<afterHP<<','<<damage
                    <<','<<(q->tick<deadline?"ordinary-pre-deadline":"exact-C4-deadline")<<','<<quote(q->simulationStatus)
                    <<','<<(q->tick<deadline?"DoublePistols":"C4")<<','<<(q->tick<deadline?"HollowPoint2":c4Warhead)<<','<<ordinaryBulletDamage<<'\n';hits.flush();}
                if(q->tick<deadline&&damage!=0&&damage!=ordinaryBulletDamage)
                    return fail("pre-deadline HP change exceeds the original single bullet hit: delta="+std::to_string(damage)+" status="+q->simulationStatus);
                if(q->tick<deadline&&damage==ordinaryBulletDamage)++ordinaryHits;
                if(q!=&s)++pairedTicks;}
            if(!same())return fail("saved armed deadline/foot diverged");
            if(exactStopped&&distance(stoppedFoot,foot(*findObjectById(s,f.actor)))>1e-6)return fail("armed C4 snapped the stopped exterior body");
            if(s.tick==attached+1&&screenshots&&!capture(f,"after"))return false;
            if(s.tick<deadline)for(auto* q:replicas()){
                const auto* target=findObjectById(*q,f.target);const auto armed=q->specialMechanisms.timedBombs().find(f.target);
                if(!target||!target->alive||target->health<=0||target->health>s.rules.findBuildingType(f.spec.id)->strength
                    ||armed==q->specialMechanisms.timedBombs().end()||q->pendingTimedBombIntents.contains(f.actor))
                    return fail("C4 target died or its original timer disappeared before the exact deadline: tick="+std::to_string(q->tick)+" status="+q->simulationStatus);
                const auto& bomb=armed->second;
                if(bomb.kind!=TimedBombKind::C4||bomb.sourceObjectId!=f.actor||bomb.targetObjectId!=f.target||bomb.owner!="Americans"
                    ||bomb.damage!=std::numeric_limits<int>::max()||!bomb.forceDestruction||bomb.warheadId!=c4Warhead
                    ||bomb.attachedTick!=attached||bomb.detonateTick!=deadline)return fail("C4 identity or exact deadline changed during natural Guard fire");
            }
        }
        const auto* target=findRuntimeObjectByIdIncludingInactive(s,f.target);const auto* source=findObjectById(s,f.actor);
        if(s.tick!=deadline||!target||target->alive||target->health!=0||!source||source->health!=200
            ||s.specialMechanisms.timedBombs().contains(f.target)||s.pendingTimedBombIntents.contains(f.actor))return fail("real exact C4 destruction/source lifetime");
        if(control=="blocked-nearest"&&!std::any_of(s.world.getObjects().begin(),s.world.getObjects().end(),[](const auto& o){return o.type=="MTNK"&&o.alive&&o.x==65&&o.y==68;}))return fail("original blocker disappeared or moved");
        cases<<quote(label)<<','<<f.spec.id<<','<<f.direction<<','<<(f.own?"own-force":"enemy-primary")<<','<<f.anchor<<','<<control<<','<<attached<<','<<deadline<<','<<s.tick<<','<<source->alive<<','<<target->health<<','<<motionTicks-initialMotion<<','<<largestDelta<<",1\n";cases.flush();
        ++fixtures;std::cout<<"Issue25 Tanya C4: "<<label<<" PASS\n";return true;
    };
    // First run the runtime-built/no-anchor south approach against the
    // unchanged producer, recording physical entry rather than failing only
    // on the old whole-building permission representation.
    for(int direction:{5,0,1,2,3,4,6,7}){
        label="fallback-YAGNTC-d"+std::to_string(direction);stage="setup";
        if(!run(make(specs[1],direction,false,false),false,direction==5))return false;++fallback;
    }
    for(const auto& spec:specs)for(int direction=0;direction<8;++direction)for(bool own:{false,true}){
        label=std::string(spec.id)+"-d"+std::to_string(direction)+(own?"-own-force":"-enemy-primary");stage="setup";
        const bool save=(spec.id==std::string("GAPOWR")&&direction==0&&!own)||(spec.id==std::string("YAGNTC")&&direction==4&&own);
        if(!run(make(spec,direction,own,true),save,save))return false;++core;
    }    const auto awaitHalf=[&](Scene& f){auto& s=*f.state;for(int wait=0;wait<300;++wait){const auto previous=foot(*findObjectById(s,f.actor));step(s);
        if(!physical(f,s,previous,"live"))return false;const auto b=s.behaviors.find(f.actor);const auto* a=findObjectById(s,f.actor);
        if(a&&a->renderMoveActive&&b!=s.behaviors.end()&&b->second.renderMoveTicksRemaining>1
            &&b->second.renderMoveTicksRemaining*3<b->second.renderMoveTicksTotal*2&&b->second.renderMoveTicksRemaining*3>b->second.renderMoveTicksTotal)return true;}
        return fail("real half-completed movement segment unavailable");};
    const auto ordinaryMove=[&](Scene& f,RuntimeCellPoint point){auto& s=*f.state;s.selectedObjectIds={f.actor};
        return dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,point)&&s.lastCommand.acceptedCount==1;};
    for(bool newMove:{false,true}){
        label=newMove?"pending-C4-new-Move-cancel":"pending-C4-Stop-cancel";stage="setup";auto f=make(specs[1],3,true,true);
        if(f.actor<0)return fail("cancel fixture");auto& s=*f.state;std::string violation;
        if(!command(f,violation)||!violation.empty()||!awaitHalf(f))return fail("real outside pending C4 cancellation window: "+violation);
        const auto before=foot(*findObjectById(s,f.actor));s.selectedObjectIds={f.actor};stage="cancel-command";
        const bool dispatched=newMove?ordinaryMove(f,{55,55}):dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop);
        const auto after=foot(*findObjectById(s,f.actor));sample(f,s,"live",distance(before,after));states.flush();
        if(!dispatched||distance(before,after)>1e-6||std::abs(before.z-after.z)>1e-6||s.pendingTimedBombIntents.contains(f.actor)||s.specialMechanisms.timedBombs().contains(f.target))return fail("actual cancel lost foot or retained C4 intent");
        bool settled=!newMove;for(int wait=0;wait<(newMove?1200:90);++wait){const auto previous=foot(*findObjectById(s,f.actor));step(s);
            if(!physical(f,s,previous,"live")||s.pendingTimedBombIntents.contains(f.actor)||s.specialMechanisms.timedBombs().contains(f.target))return fail("cancelled C4 reappeared");
            const auto* a=findObjectById(s,f.actor);const auto* t=findObjectById(s,f.target);
            if(!t||t->health!=s.rules.findBuildingType(f.spec.id)->strength)return fail("cancelled timer damaged original building");
            if(!newMove&&distance(before,foot(*a))>1e-6)return fail("Stop did not retain actual outside body");
            if(newMove&&a->x==55&&a->y==55&&!a->renderMoveActive&&!s.behaviors.contains(f.actor)){settled=true;break;}}
        if(!settled)return fail("replacement real Move failed to finish");
        cases<<quote(label)<<",YAGNTC,3,own-force,1,cancel,-1,-1,"<<s.tick<<",1,"<<findObjectById(s,f.target)->health<<",0,"<<largestDelta<<",1\n";cases.flush();++fixtures;++controls;
    }
    for(int direction:{1,5}){
        label="half-walk-to-C4-d"+std::to_string(direction);stage="ordinary-Move";auto f=make(specs[1],direction,false,true);
        if(f.actor<0)return fail("moving replacement fixture");const auto* a=findObjectById(*f.state,f.actor);
        const RuntimeCellPoint destination{a->x,direction==1?a->y+3:a->y-3};
        if(!ordinaryMove(f,destination)||!awaitHalf(f))return fail("actual ordinary segment before C4 unavailable");
        // command() enforces same-tick actual foot equality at 1e-6; this
        // specifically catches clearRenderPosition snapping to committed nav.
        if(!run(std::move(f),false,true,"half-walk-replacement"))return false;++controls;
    }
    {
        label="own-Primary-refused-keeps-real-Move";stage="ordinary-Move";auto f=make(specs[1],7,true,true);if(f.actor<0)return fail("own Primary fixture");auto& s=*f.state;
        if(!ordinaryMove(f,{61,65})||!awaitHalf(f))return fail("own Primary real old Move window");
        const auto behaviorText=[&](){std::ostringstream o;const auto b=s.behaviors.find(f.actor);if(b!=s.behaviors.end()){const auto& v=b->second;
            o<<int(v.kind)<<'|'<<int(v.state)<<'|'<<v.moveTarget.x<<','<<v.moveTarget.y<<'|'<<v.targetObjectId<<'|'<<v.pendingAllowedOccupiedObjectId<<'|'<<v.forceFire
                <<'|'<<v.moveCooldownTicks<<'|'<<v.renderMoveTicksRemaining<<'|'<<v.renderMoveTicksTotal<<'|'<<v.renderFrom.x<<','<<v.renderFrom.y<<'|'<<v.renderTo.x<<','<<v.renderTo.y;
            for(auto p:v.path)o<<'|'<<p.x<<','<<p.y;}return o.str();};
        const auto behaviorBefore=behaviorText();const auto worldBefore=s.world.deterministicSimulationStateHash();const auto bombBefore=s.specialMechanisms.stateHash();const auto p=foot(*findObjectById(s,f.actor));
        const auto* target=findObjectById(s,f.target);s.selectedObjectIds={f.actor};stage="refused-own-Primary";
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{target->x,target->y},target);sample(f,s,"live",distance(p,foot(*findObjectById(s,f.actor))));states.flush();
        if(dispatched||s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||worldBefore!=s.world.deterministicSimulationStateHash()
            ||behaviorBefore!=behaviorText()||bombBefore!=s.specialMechanisms.stateHash()||s.pendingTimedBombIntents.contains(f.actor)
            ||distance(p,foot(*findObjectById(s,f.actor)))>1e-6)return fail("ordinary own Primary changed C4 authority or current Move");
        cases<<quote(label)<<",YAGNTC,7,own-primary,1,rejection,-1,-1,"<<s.tick<<",1,"<<target->health<<",0,"<<largestDelta<<",1\n";cases.flush();++fixtures;++controls;
    }
    {
        label="blocked-nearest-outside-candidate-routes-around";stage="setup";auto f=make(specs[1],5,false,true);if(f.actor<0)return fail("blocked candidate fixture");auto& s=*f.state;
        auto* blocker=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"MTNK",{65,68});if(!blocker)return fail("original stationary blocker");
        const int blockerId=blocker->instanceId;blocker->mission="Sleep";refreshRuntimeObjectOccupancyAfterLocalChange(s);
        if(!run(std::move(f),false,false,"blocked-nearest"))return false;
        // The blocker is an original live unit, never removed or made passable.
        sources<<label<<": original MTNK blocker id="<<blockerId<<" at65,68 (the uniquely nearest legal C4 cell from south65,72), surrounding original clear cells available.\n";sources.flush();++controls;
    }
    {
        label="near-building-Move-Stop-same-cell-C4";stage="real-final-ordinary-segment";
        auto f=make(specs[1],5,false,true);if(f.actor<0)return fail("near stopped fixture");auto& s=*f.state;
        if(!ordinaryMove(f,{65,67}))return fail("near building ordinary Move rejected");bool observed=false;
        for(int wait=0;wait<1200;++wait){const auto before=foot(*findObjectById(s,f.actor));step(s);
            if(!physical(f,s,before,"live"))return false;const auto* a=findObjectById(s,f.actor);const auto b=s.behaviors.find(f.actor);
            if(a->x==65&&a->y==67&&a->renderMoveActive&&b!=s.behaviors.end()
                &&b->second.renderMoveTicksRemaining*3>b->second.renderMoveTicksTotal
                &&b->second.renderMoveTicksRemaining*3<b->second.renderMoveTicksTotal*2){observed=true;break;}}
        if(!observed)return fail("real last ordinary segment at the exterior C4 goal was not observed");
        const auto* moving=findObjectById(s,f.actor);const auto before=foot(*moving);const auto slot=worldObjectSubCellOffset(*moving);
        if(inside(f.spec,before)||distance(before,{moving->x+slot.x,moving->y+slot.y,0})<1e-4)
            return fail("Stop input was canonical or inside instead of a real fractional exterior foot");
        s.selectedObjectIds={f.actor};stage="real-Stop-keeps-fractional-foot";
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)
            ||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0)return fail("real near-building Stop rejected");
        const auto* stopped=findObjectById(s,f.actor);sample(f,s,"live",distance(before,foot(*stopped)));states.flush();
        if(!stopped->hasRenderPosition||stopped->renderMoveActive||s.behaviors.contains(f.actor)
            ||s.pendingTimedBombIntents.contains(f.actor)||distance(before,foot(*stopped))>1e-6
            ||std::abs(before.z-foot(*stopped).z)>1e-6)return fail("Stop did not preserve the real noncanonical outside foot");
        sources<<label<<": naturally observed final ordinary Move segment, real Stop retained foot "<<std::setprecision(17)<<before.x<<','<<before.y
            <<" at exterior nav65,67; same-cell C4 must have no path and retain this exact foot through planting and detonation.\n";sources.flush();
        if(!run(std::move(f),false,true,"stopped-same-cell"))return false;++controls;
    }
    {
        label="manual-legacy-outside-active-two-formats";stage="real-outside-pending";
        auto f=make(specs[1],3,false,true);if(f.actor<0)return fail("legacy outside fixture");auto& s=*f.state;std::string violation;
        if(!command(f,violation)||!violation.empty()||!awaitHalf(f))return fail("real safe exterior legacy input precursor: "+violation);
        auto& b=s.behaviors.at(f.actor);const auto* a=findObjectById(s,f.actor);
        if(!a->renderMoveActive||inside(f.spec,foot(*a))||inside(f.spec,{double(b.renderFrom.x),double(b.renderFrom.y),0})
            ||inside(f.spec,{double(b.renderTo.x),double(b.renderTo.y),0}))return fail("outside legacy input segment was not physically legal");
        // Explicit old save-shape input, not a claim of a natural old-version
        // save reproduction: retain the real outside segment but restore its
        // historical whole-building permission before writing actual bytes.
        b.pendingAllowedOccupiedObjectId=f.target;stage="manual-legacy-outside-input";sample(f,s,"live",0);
        sources<<label<<": manually restored old allowed==target to an actually observed outside C4 segment; actual ordinary14 and canonical file write/read, then both loaded replicas advance in lockstep through exterior migration, arming, natural Guard fire and the exact deadline.\n";sources.flush();
        auto restored=checkpoint(f,s,false,true);if(!restored)return fail(error);
        auto canonical=checkpoint(f,s,true,true);if(!canonical)return fail(error);
        f.state=std::move(restored);f.legacyPeer=std::move(canonical);
        if(!run(std::move(f),false,true,"legacy-outside-checkpoint"))return false;++controls;
    }
    {
        label="manual-legacy-interior-active-runtime";stage="real-C4-authority-before-manual-input";
        auto f=make(specs[1],5,false,true);if(f.actor<0)return fail("legacy interior fixture");auto& s=*f.state;std::string violation;
        if(!command(f,violation)||!violation.empty())return fail("real C4 authority for explicit legacy input: "+violation);
        // Deliberately authored OLD interior runtime shape; no save load is attempted. Original Tanya
        // still has her real UI C4 intent; only this documented compatibility
        // input installs the historical last authorized foundation segment.
        // Source65,67 is an empty original clear cell and its valid slot3 is
        // independently known at foot64.75,67.25. Current foot is inside YAGNTC, not a new
        // command behavior allowed by this test.
        auto* a=findMutableObjectById(s,f.actor);if(!a)return fail("legacy mutable source");
        RuntimeObjectBehavior old;old.kind=RuntimeBehaviorKind::Move;old.state=RuntimeBehaviorState::Moving;
        old.moveTarget={65,66};old.targetObjectId=f.target;old.pendingAllowedOccupiedObjectId=f.target;
        old.moveSegmentFrom=old.renderFrom={65,67};old.renderTo={65,66};old.moveCooldownTicks=8;
        old.renderMoveTicksTotal=34;old.renderMoveTicksRemaining=8;
        a->x=65;a->y=66;a->cell=runtimeCellKey(65,66);a->level=0;a->onBridge=false;a->subCell=3;
        a->hasRenderPosition=a->renderMoveActive=true;a->renderX=65;a->renderY=66.0+8.0/34.0;a->renderLevel=0;a->renderOnBridge=false;
        a->facing=runtimeFacingFromDelta(0,-1);s.behaviors.at(f.actor)=std::move(old);
        releaseRuntimeInfantrySubcellReservation(s,f.actor);refreshRuntimeObjectOccupancyAfterLocalChange(s);
        if(!inside(f.spec,foot(*a))||a->x!=65||a->y!=66)return fail("explicit legacy interior input was not actually inside");
        f.legacyRecoverySource=Foot{64.75,67.25,0};stage="manual-legacy-interior-input";sample(f,s,"live-manual-old-input",0);
        sources<<label<<": MANUAL illegal old runtime input, NOT a successfully loaded canonical save or natural old-version save evidence: allowed==target, nav65,66/subcell3, from65,67 to65,66, zero segment offsets, total34 remaining8, render65,"<<std::setprecision(17)<<a->renderY
            <<", actual foot64.75,"<<foot(*a).y<<". The R4 manual subcell-1 variant was rejected after committed-subcell reconciliation reassigned it to an active slot. A complete save load of this subcell3 interior variant has not been verified; this control directly advances the documented old runtime state without claiming save compatibility. ONLY first advance may repair farther than ordinary speed, exactly to original clear source65,67/slot3/foot64.75,67.25/level0; all following frames retain the ordinary speed/outside/exact raw truncated deadline contracts.\n";sources.flush();
        if(!run(std::move(f),false,true,"legacy-inside-runtime"))return false;++controls;
    }
    states.flush();
    if(fixtures!=65||core!=48||fallback!=8||controls!=9||ordinarySaves!=5||canonicalSaves!=5||legacyRepairFrames!=1||pairedTicks<=0||motionTicks<=0||bmps<9)return fail("focused terminal guards");
    std::ofstream result(root/"result.txt");result<<"PASS\nfixtures="<<fixtures<<"\ncore="<<core<<"\nfallback="<<fallback<<"\ncontrols="<<controls
        <<"\nordinary14_saves="<<ordinarySaves<<"\ncanonical_saves="<<canonicalSaves<<"\npaired_ticks="<<pairedTicks<<"\nactual_BMPs="<<bmps<<"\nmotion_ticks="<<motionTicks
        <<"\nordinary_single_hits="<<ordinaryHits<<"\nlegacy_repair_frames="<<legacyRepairFrames<<"\nlegacy_repair_delta="<<legacyRepairDelta<<"\nmaximum_tick_delta="<<std::setprecision(17)<<largestDelta<<"\nindependent_delta_limit="<<maximumTickDelta<<"\n";
    std::cout<<"Issue25 Tanya C4: 65 fixtures / 10 real save loads PASS\n";return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif