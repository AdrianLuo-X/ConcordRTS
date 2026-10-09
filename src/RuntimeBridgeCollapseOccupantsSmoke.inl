// Issue 6: native bridge mutations, full-health occupants and strict saves.
// Side-lane poses are explicit authored fixture positions, not a claim that
// high bridge side-lane navigation was implemented. One anchor walks by UI.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBridgeCollapseOccupantsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout<<std::unitbuf;std::string label="startup",stage;
    const auto fail=[&](const std::string& why){error="Bridge collapse occupants "+label+" ["+stage+"]: "+why;return false;};
    const char* focus=std::getenv("CONCORD_BRIDGE_COLLAPSE_OCCUPANTS_CASE");
    const char* output=std::getenv("CONCORD_BRIDGE_COLLAPSE_OCCUPANTS_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/issue22_06_bridge_collapse_occupants";
    std::filesystem::create_directories(root);std::ofstream csv(root/"occupants.csv");
    csv<<std::setprecision(17)<<"case,replica,stage,id,type,owner,role,cellX,cellY,bodyX,bodyY,bodyZ,bodyBridge,airborne,expectedDeath,alive,hp,tombstone\n";
    std::ofstream soundCsv(root/"sound_capture.csv"),returnCsv(root/"pending_returns.csv");
    soundCsv<<"case,replica,order,tick,section,x,y,z,spatial,playable,sample,path,status\n";
    returnCsv<<"case,replica,id,x,y,z,onBridge,alive,hp,tombstone,rawDieSound\n";
    const auto quoted=[](const std::string& value){std::string out="\"";for(char c:value){if(c=='\"')out+='\"';out+=c;}return out+'\"';};
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->config.playerSlots.clear();stock->config.gameModeRulesOverride.clear();
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    IniFile raw;if(!stock->report.success||!raw.loadFromFile(stock->report.rulesPath)||!stock->rules.loadFromIni(raw))return fail("original stock resources/rules");
    // Validate every literal actor type against the independent original list,
    // then require the parsed native registration; unknown generic fallbacks
    // must never become full-health actors in this save/death contract.
    for(const auto& [section,id]:std::array<std::pair<const char*,const char*>,14>{{
        {"InfantryTypes","E1"},{"InfantryTypes","ENGINEER"},{"InfantryTypes","SHK"},{"InfantryTypes","TANY"},{"InfantryTypes","JUMPJET"},
        {"VehicleTypes","MTNK"},{"VehicleTypes","SMCV"},{"VehicleTypes","SREF"},{"VehicleTypes","HTNK"},{"VehicleTypes","HTK"},{"VehicleTypes","DRON"},
        {"BuildingTypes","CABHUT"},{"BuildingTypes","NAIRON"},{"BuildingTypes","NANRCT"}}}){
        bool listed=false;if(const auto* values=raw.getSection(section))for(const auto& [key,value]:*values){(void)key;
            if(trimAsciiLocal(value.substr(0,value.find(';')))==id)listed=true;}
        bool parsed=false;const std::string category=section;
        if(category=="BuildingTypes"){const auto* type=findRuntimeBuildingTypeById(stock->rules,id);parsed=type&&type->id==id;}
        else {const auto* type=stock->rules.findUnitType(category=="InfantryTypes"?"infantry":"vehicle",id);parsed=type&&type->id==id;}
        if(!listed||!parsed)return fail(std::string("original actor is not exactly registered in ")+section+": "+id);
    }
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;const auto rulesHash=stock->rules.getDetailRulesHash();
    const int strength=std::stoi(raw.getValue("CombatDamage","BridgeStrength","0"));
    if(strength!=1500||stock->rules.getBridgeStrength()!=strength)return fail("raw BridgeStrength");
    int land=-1,water=-1;for(int i=0;i<static_cast<int>(stock->theater.getTotalTileSlots());++i){TerrainCell c;c.tileIndex=i;c.subTileIndex=0;
        const auto info=runtimeTerrainInfoForCell(*stock,c);if(land<0&&info.passable&&!info.water&&!info.bridge&&!info.shore&&!info.cliff&&!info.ramp)land=i;
        if(water<0&&info.water&&!info.bridge&&!info.shore&&!info.cliff&&!info.ramp)water=i;if(land>=0&&water>=0)break;}
    if(land<0||water<0)return fail("original clear/water TMP");
    const auto ordinal=[&](const std::string& id){const auto& types=stock->rules.getOverlayTypes();
        for(std::size_t i=0;i<types.size();++i)if(types[i].id==id)return static_cast<int>(i);return -1;};
    if(ordinal("BRIDGE1")!=24||ordinal("BRIDGE2")!=25||ordinal("BRIDGEB1")!=237||ordinal("BRIDGEB2")!=238
        ||ordinal("LOBRDG01")!=74||ordinal("LOBRDB01")!=205)return fail("original OverlayPack ordinal contract");
    const auto lowId=[](bool wood,int offset){std::ostringstream id;id<<(wood?"LOBRDG":"LOBRDB")<<std::setw(2)<<std::setfill('0')<<offset+1;return id.str();};
    const auto setOverlay=[&](TerrainCell& c,const std::string& id,int frame){const int index=ordinal(id);if(index<0)return fail("native overlay "+id);
        c.hasOverlay=true;c.overlayIndex=index;c.overlayType=id;c.overlayImage=stock->rules.getOverlayTypes()[index].image;c.overlayFrame=frame;
        c.overlayIsOre=c.overlayIsGem=false;return true;};
    const auto facade=[](RuntimeWindowState& s,const std::string& owner="Americans"){s.economy.owner=owner;s.config.playerCountry=owner;
        s.humanHouseAuthorities.clear();RuntimeHumanHouseAuthority a;a.owner=a.country=owner;a.canonicalOwner=runtimeCanonicalHumanHouseOwner(owner);
        a.localFacade=true;s.humanHouseAuthorities.push_back(std::move(a));};
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto nativeBytes=[&](RuntimeWindowState& s,const std::string& filename,std::vector<std::uint8_t>& bytes){
        auto ext=s.theater.getTileExtension();if(!ext.empty()&&ext.front()=='.')ext.erase(ext.begin());
        const std::string theater=ext=="des"?"desert":ext=="sno"?"snow":ext=="urb"?"urban":ext=="ubn"?"urbann":ext=="lun"?"lunar":"temperat";
        const std::string iso=ext=="tem"?"isotemp.mix":ext=="sno"?"isosnow.mix":"iso"+ext+".mix";
        for(const std::string directory:{"ra2md","ra2"})for(const std::string archiveName:{"iso"+ext+"md.mix",iso,theater+"md.mix",theater+".mix",ext+".mix"}){
            const auto path=std::filesystem::path(runtimeAssetRootPath(s.config))/directory/archiveName;MixArchive archive;
            if(!std::filesystem::is_regular_file(path)||!archive.loadFromPath(path))continue;
            if(const auto* entry=archive.findEntryByName(toLowerAsciiLocal(filename)))return archive.readEntryData(*entry,bytes,error);}
        return fail("original head TMP unavailable: "+filename);};
    const auto describe=[](const WorldObjectInstance* o){if(!o)return std::string("missing");std::ostringstream out;
        out<<o->type<<" owner="<<o->owner<<" alive="<<o->alive<<" hp="<<o->health<<" logical="<<o->x<<','<<o->y<<','<<o->level<<':'<<o->onBridge
            <<" body="<<o->renderX<<','<<o->renderY<<','<<o->renderLevel<<':'<<o->renderOnBridge<<" render="<<o->hasRenderPosition
            <<" moving="<<o->renderMoveActive<<" airborne="<<o->airborne<<" mission="<<o->mission;return out.str();};
    struct Expected {WorldObjectInstance before;std::string role;bool death=false;RuntimeSpatialAudioPosition sound;};
    struct SoundCaptureScope {RuntimeWindowState& state;std::vector<RuntimeSoundPlaybackRecord>* previous;
        SoundCaptureScope(RuntimeWindowState& s,std::vector<RuntimeSoundPlaybackRecord>& records):state(s),previous(s.runtimeSoundPlaybackCapture){state.runtimeSoundPlaybackCapture=&records;}
        ~SoundCaptureScope(){state.runtimeSoundPlaybackCapture=previous;}};
    int scenes=0,deaths=0,survivors=0,saves=0,sounds=0,moves=0,motionChecks=0,boardingChecks=0,protectedChecks=0,nestedChecks=0;
    int pendingLaunches=0,pendingReturnChecks=0,pendingEffectCleanups=0;
    const auto checkpoint=[&](RuntimeWindowState& s,bool canonical){std::ostringstream text,normalized;IniFile base,exact;
        const auto rejectLoad=[&](){auto reconciled=s.specialMechanisms;auto objects=s.world.getObjects();std::vector<SpecialMechanismAction> actions;
            const auto beforeHash=reconciled.stateHash();reconciled.reconcile(objects,s.tick,SpecialEventSink{},&actions);std::ostringstream detail;
            detail<<error<<" tick="<<s.tick<<" specialHash="<<beforeHash<<"->"<<reconciled.stateHash()<<" actions="<<actions.size()
                <<" camouflage="<<s.specialMechanisms.camouflageStates().size()<<"->"<<reconciled.camouflageStates().size()
                <<" pending="<<s.specialMechanisms.pendingParasiteLaunches().size()<<"->"<<reconciled.pendingParasiteLaunches().size();
            for(const auto& a:actions)detail<<"\naction="<<static_cast<int>(a.kind)<<" source="<<a.objectId<<" target="<<a.targetObjectId
                <<" actor="<<a.projectileId<<" reason="<<a.reason;
            for(const auto& [id,c]:s.specialMechanisms.camouflageStates())detail<<"\ncamouflage="<<id<<" active="<<c.active
                <<" preserved="<<reconciled.camouflageStates().contains(id)<<" object="<<describe(findRuntimeObjectByIdIncludingInactive(s,id));
            for(const auto& [id,p]:s.specialMechanisms.pendingParasiteLaunches())detail<<"\npendingActor="<<id<<" source="<<p.sourceObjectId
                <<" target="<<p.targetObjectId<<" sourceState="<<describe(findRuntimeObjectByIdIncludingInactive(s,p.sourceObjectId))
                <<" targetState="<<describe(findRuntimeObjectByIdIncludingInactive(s,p.targetObjectId));
            error=detail.str();std::ofstream report(root/(label+"-"+stage+(canonical?"-canonical":"-ordinary")+"-load-diagnostic.txt"));report<<error<<'\n';
            return std::unique_ptr<RuntimeWindowState>{};};
        if(!writeRuntimeStateToStream(s,text,error)||!base.loadFromString(text.str()))return std::unique_ptr<RuntimeWindowState>{};
        {std::ofstream saved(root/(label+"-"+stage+"-ordinary.ini"),std::ios::binary);saved<<text.str();}
        // Explicit ground fixture stops use the existing V14 pose section;
        // their fractional body must not silently revert to navigation's layer.
        const int stoppedCount=parseRuntimeIntOrDefault(base.getValue("StoppedMovePoses","Count","-1"),-1);
        if(stoppedCount<0){error="ordinary checkpoint lacks StoppedMovePoses";return std::unique_ptr<RuntimeWindowState>{};}
        for(const auto& o:s.world.getObjects())if(o.alive&&worldObjectHasStoppedGroundPose(o)){
            bool found=false;for(int i=0;i<stoppedCount;++i){const auto fields=splitRuntimeSaveFields(base.getValue("StoppedMovePoses",std::to_string(i),""));
                int id=-1;double x=0,y=0,z=0;bool bridge=false;
                if(fields.size()==6&&parseRuntimeInt(fields[0],id)&&id==o.instanceId&&parseRuntimeFiniteDouble(fields[1],x)
                    &&parseRuntimeFiniteDouble(fields[2],y)&&parseRuntimeFiniteDouble(fields[3],z)&&parseRuntimeStrictBool(fields[4],bridge))
                    found=x==o.renderX&&y==o.renderY&&z==o.renderLevel&&bridge==o.renderOnBridge;}
            if(!found){error="ordinary checkpoint omitted exact stopped ground body #"+std::to_string(o.instanceId);return std::unique_ptr<RuntimeWindowState>{};}}
        if(canonical){if(!writeRuntimeMultiplayerSnapshotExtension(s,text,error)||!exact.loadFromString(text.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)||!exact.loadFromString(normalized.str()))return std::unique_ptr<RuntimeWindowState>{};
            std::ofstream saved(root/(label+"-"+stage+"-canonical.ini"),std::ios::binary);saved<<normalized.str();}
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);RuntimeSnapshotActiveStateScope active(*copy);
        if(!materializeRuntimeLoadStateFromIni(*copy,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return rejectLoad();
        ++saves;return copy;};
    for(int hz:{30,60})for(bool high:{true,false})for(bool alongX:{true,false})for(bool whole:{false,true}){
        const bool wood=high==alongX;label=std::string(high?"high":"low")+"-"+(wood?"wood":"concrete")+"-"+(alongX?"X":"Y")+"-"+(whole?"whole":"local")+"-"+std::to_string(hz);
        if(focus&&*focus&&label.find(focus)==std::string::npos)continue;stage="native fixture";error.clear();
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,*stock,false);
        s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.skirmishLoadSavePath.clear();s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.shortGame=s.config.crates=s.config.superweapons=false;s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;s.campaignMode=false;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;s.mapFacts={};s.mapFactsLoaded=false;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.tick=100;facade(s);const auto point=[&](int axial,int lane,int parallel=0){return RuntimeCellPoint{alongX?axial:32+lane+parallel,alongX?32+lane+parallel:axial};};
        const RuntimeCellPoint target=point(35,0);const int deck=high?4:0;
        for(int y=20;y<=68;++y)for(int x=20;x<=68;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=land;c.subTileIndex=0;c.level=deck;s.world.accessTerrainCells().push_back(c);}
        const auto cell=[&](RuntimeCellPoint p)->TerrainCell*{for(auto& c:s.world.accessTerrainCells())if(c.x==p.x&&c.y==p.y)return &c;return nullptr;};
        const auto stamp=[&](RuntimeCellPoint boundary,bool start){const int set=s.theater.getGeneralTileSetIndex(wood?"WoodBridgeSet":"BridgeSet");
            const int head=s.theater.getGeneralTileSetIndex(alongX?(start?"BridgeTopLeft1":"BridgeBottomRight1"):(start?"BridgeTopRight1":"BridgeBottomLeft1"));
            const auto found=std::find_if(s.theater.getTileSets().begin(),s.theater.getTileSets().end(),[&](const TheaterTileSet& t){return t.index==set;});
            if(found==s.theater.getTileSets().end()||head<=0||head>found->tilesInSet)return fail("native high bridge head ordinal");
            const int tile=found->startTileIndex+head-1;std::vector<std::uint8_t> bytes;
            if(!nativeBytes(s,s.theater.resolveTileIndex(tile).expectedFileName,bytes)||bytes.size()<20)return false;
            const auto u32=[&](int i){return unsigned(bytes[i])|(unsigned(bytes[i+1])<<8)|(unsigned(bytes[i+2])<<16)|(unsigned(bytes[i+3])<<24);};
            const int w=static_cast<int>(u32(0)),h=static_cast<int>(u32(4));if(w<=0||h<=0||w>16||h>16)return fail("native TMP head dimensions");
            const RuntimeCellPoint origin{boundary.x-(alongX?(start?w-1:0):w/2),boundary.y-(alongX?h/2:(start?h-1:0))};
            for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){auto* c=cell({origin.x+dx,origin.y+dy});if(!c)return fail("head outside fixture");
                // Native high bridge heads and huts share the elevated bank;
                // only the overlay body sits above the level-zero water.
                c->tileIndex=tile;c->subTileIndex=dy*w+dx;c->level=deck;}return true;};
        int hut=-1;for(int parallel:{0,14}){
            const auto begin=point(30,0,parallel),end=point(40,0,parallel);
            if(high&&(!stamp(begin,true)||!stamp(end,false)))return false;
            for(int a=30;a<=40;++a)for(int lane=0;lane<3;++lane){auto* c=cell(point(a,lane,parallel));if(!c)return fail("bridge cell missing");
                if(a>30&&a<40){c->tileIndex=water;c->subTileIndex=0;c->level=0;
                    if(!high||lane==0){const std::string id=high?(wood?(alongX?"BRIDGEB1":"BRIDGEB2"):(alongX?"BRIDGE1":"BRIDGE2")):lowId(wood,alongX?0:9);
                        if(!setOverlay(*c,id,high?(alongX?0:9):lane))return false;}}
                else if(!high&&!setOverlay(*c,lowId(wood,alongX?(a==30?20:18):(a==30?22:24)),lane))return false;}
            for(bool start:{true,false}){const auto boundary=start?begin:end;const RuntimeCellPoint p{boundary.x+(alongX?(start?-3:3):-1),boundary.y+(alongX?-1:(start?-3:3))};
                auto* o=addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,"CABHUT",p,std::nullopt,false);if(!o)return fail("native CABHUT");
                o->mission="Sleep";if(start&&!parallel)hut=o->instanceId;}}
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeGarrisonsInitialized(s);
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);initializeRuntimeOfflineGameplayRouting(s);facade(s);s.destroyableBridges=true;
        const auto registered=s.bridgeRepairSpansByHut.find(hut);if(registered==s.bridgeRepairSpansByHut.end()||registered->second.high!=high
            ||registered->second.alongX!=alongX){std::ostringstream detail;const auto* o=findObjectById(s,hut);
                detail<<"natural hut registration hut="<<hut<<" "<<describe(o)<<" spanCount="<<s.bridgeRepairSpansByHut.size();
                for(const auto& c:s.world.getTerrainCells())if(o&&std::max(std::abs(c.x-o->x),std::abs(c.y-o->y))<=3
                    &&runtimeHighBridgeHeadIsBoundary(runtimeHighBridgeHeadTypeForTerrain(s,c)))
                    detail<<" head="<<c.x<<','<<c.y<<" tile="<<c.tileIndex<<" sub="<<c.subTileIndex<<" level="<<c.level;
                return fail(detail.str());}
        std::set<int> required;for(const auto& c:registered->second.cells)if(c.requiresOverlay)required.insert(runtimeCellKey(c.point.x,c.point.y));
        std::set<int> independent;for(int a=31;a<=39;++a)for(int lane=0;lane<(high?1:3);++lane){const auto p=point(a,lane);independent.insert(runtimeCellKey(p.x,p.y));}
        if(required!=independent)return fail("registered whole strip differs from original geometry");
        const auto add=[&](const std::string& type,const std::string& owner,RuntimeCellPoint p)->int{
            const bool infantry=stock->rules.findUnitType("Infantry",type)!=nullptr;
            const auto* registeredType=stock->rules.findUnitType(infantry?"infantry":"vehicle",type);
            if(!registeredType||registeredType->id!=type)return -1;
            auto* o=addRuntimeObjectInstanceForOwner(s,owner,infantry?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,type,p,std::nullopt,false);
            if(!o)return -1;o->mission="Sleep";return o->instanceId;};
        // One anchor enters the doomed area through the actual UI/path clock.
        const int walker=add("HTNK","Americans",point(31,0));if(walker<0)return fail("stock walker");s.selectedObjectIds={walker};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,target)||s.lastCommand.acceptedCount!=1)return fail("native anchor Move rejected");
        bool arrived=false;for(int t=0;t<hz*40&&!arrived;++t){step(s);const auto* o=findObjectById(s,walker);
            arrived=o&&o->x==target.x&&o->y==target.y&&o->onBridge&&(!o->renderMoveActive||std::hypot(o->renderX-target.x,o->renderY-target.y)<.05);}
        if(!arrived)return fail("native walker never reached doomed deck: "+describe(findObjectById(s,walker)));
        s.selectedObjectIds={walker};if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)||s.lastCommand.acceptedCount!=1)return fail("native walker Stop rejected");++moves;
        findMutableObjectById(s,walker)->mission="Sleep";
        // Real native boarding, followed by explicit placement of the loaded
        // transport. Cargo IDs, inactive passenger state and relationships
        // are created only by the production entry pipeline.
        const int transport=add("HTK","Americans",{24,24}),passenger=add("E1","Americans",{23,24});
        if(transport<0||passenger<0)return fail("stock transport/passenger");s.selectedObjectIds={passenger};
        if(!queueRuntimeTransportTarget(s,*findObjectById(s,transport)))return fail("native boarding queue rejected");
        bool boarded=false;for(int t=0;t<hz*20&&!boarded;++t){step(s);const auto* host=findObjectById(s,transport);boarded=host&&host->transportPassengerIds==std::vector<int>{passenger};}
        if(!boarded)return fail("native transport entry never completed: "+describe(findObjectById(s,transport)));++boardingChecks;
        if(hz==30&&high&&alongX&&!whole){
            for(const auto& [type,p]:std::array<std::pair<const char*,RuntimeCellPoint>,3>{{{"NAIRON",{58,58}},{"NANRCT",{62,58}},{"NANRCT",{58,62}}}}){
                auto* o=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,type,p,std::nullopt,false);if(!o)return fail("native Iron Curtain provider");o->mission="Sleep";}
            s.config.superweapons=true;rebuildRuntimeEconomyPower(s);ensureRuntimeSuperWeaponsInitialized(s);auto context=runtimeSuperWeaponContext(s);
            context.emit=runtimeSuperWeaponEventSink(s);s.superWeapons.tick(context);
            if(!s.superWeapons.forceReady("Americans","IronCurtainSpecial",&context)
                ||!s.superWeapons.beginTargetSelection("Americans","IronCurtainSpecial",context,&error)
                ||!s.superWeapons.submitTarget("Americans","IronCurtainSpecial",SuperWeaponTarget{{24,24},transport},context,&error))return fail("original Iron Curtain activation: "+error);
            ++s.tick;advanceRuntimeEffects(s);(void)drainRuntimeObjectExitQueue(s);completeRuntimeWorldRefresh(s);
            if(!s.superWeapons.isObjectProtected(transport,s.tick))return fail("original Iron Curtain did not protect transport");++protectedChecks;
        }
        const auto author=[&](int id,RuntimeCellPoint p,bool bridge,double level,bool airborne=false){auto* o=findMutableRuntimeObjectByIdIncludingInactive(s,id);
            if(!o)return false;setRuntimeObjectCell(s,*o,p,false);o->level=static_cast<int>(level);o->onBridge=bridge;o->airborne=airborne;
            if(airborne)o->flightLevel=static_cast<int>(level);
            // Same-cell fractional stops are explicit authored body positions,
            // preserved by the existing StoppedMovePoses ordinary save format.
            o->hasRenderPosition=true;o->renderX=p.x+(airborne?0.0:.125);o->renderY=p.y;o->renderLevel=level;o->renderOnBridge=bridge;o->renderMoveActive=false;
            assignRuntimeInfantrySubcellAtCurrentLocation(s,*o);
            o->mission="Sleep";eraseRuntimeBehavior(s,id);syncRuntimeTransportCargoPositions(s,*o);return true;};
        int pendingHost=-1,pendingSource=-1,pendingActor=-1;
        const auto reservedReturnSlotLegal=[&](RuntimeWindowState& q){if(pendingSource<0)return true;const auto p=point(34,0);
            const auto* source=findRuntimeObjectByIdIncludingInactive(q,pendingSource);if(!source)return fail("native return-slot source missing");
            for(const auto& o:q.world.getObjects())if(o.instanceId!=pendingSource&&o.alive&&!o.airborne&&o.kind!=WorldObjectKind::Structure
                &&((o.x==p.x&&o.y==p.y)||(worldObjectHasGroundRenderPose(o)&&std::lround(o.renderX)==p.x&&std::lround(o.renderY)==p.y)))
                return fail("original bridge return slot has a live fixture blocker #"+std::to_string(o.instanceId)+" "+describe(&o));
            const auto terrain=q.terrainByCell.find(runtimeCellKey(p.x,p.y));const auto occupied=buildRuntimeOccupancyLookup(q,pendingSource);
            if(terrain==q.terrainByCell.end()||!terrain->second||terrain->second->overlayType!="BRIDGEB1"||terrain->second->overlayFrame!=0
                ||occupied.contains(runtimeCellKey(p.x,p.y))||!runtimeCellIsBridge(q,p.x,p.y)
                ||!runtimeCellIsPassableUncached(q,q.terrainByCell,occupied,p,*source))
                return fail("original empty return slot is not natively occupiable bridge at "+std::to_string(p.x)+","+std::to_string(p.y));
            return true;};
        if(hz==30&&high&&alongX&&!whole){
            // Capture a real stock leap before contact. The host's side lane
            // makes the old-navigation return ring land on this local span.
            pendingHost=add("HTNK","Americans",point(35,1));pendingSource=add("DRON","Russians",point(34,0));
            if(pendingHost<0||pendingSource<0||!author(pendingHost,point(35,1),true,deck)||!author(pendingSource,point(34,0),true,deck))
                return fail("original pending parasite source/host fixture");
            // Preserve a distinct spatial cue from the native MTNK which
            // later occupies this same logical vehicle cell.
            findMutableObjectById(s,pendingHost)->renderX+=.25;
            if(!reservedReturnSlotLegal(s))return false;
            facade(s,"Russians");s.selectedObjectIds={pendingSource};const auto* host=findObjectById(s,pendingHost);
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host)
                ||s.lastCommand.acceptedCount!=1)return fail("original DRON UI Primary rejected accepted="+std::to_string(s.lastCommand.acceptedCount)
                    +" rejected="+std::to_string(s.lastCommand.rejectedCount)+" verb="+s.lastCommand.verb+" status="+s.simulationStatus);
            for(int t=0;t<hz*5&&pendingActor<0;++t){step(s);
                for(const auto& p:s.projectiles)if(p.attackerObjectId==pendingSource&&p.targetObjectId==pendingHost&&p.weaponId=="DroneJump"
                    &&s.specialMechanisms.pendingParasiteLaunches().contains(p.actorId)){pendingActor=p.actorId;break;}}
            const auto* source=findRuntimeObjectByIdIncludingInactive(s,pendingSource);
            if(pendingActor<0||!source||source->alive||source->health!=100||source->mission!="ParasiteLimbo"
                ||!s.specialMechanisms.parasiteLinks().empty())return fail("native leap never produced a pending limbo source: "+describe(source));
            // The real launch follows this tick's Special stage. One further
            // complete native tick naturally retires its stale inactive
            // camouflage tracking before a strict mid-flight checkpoint.
            step(s);source=findRuntimeObjectByIdIncludingInactive(s,pendingSource);host=findObjectById(s,pendingHost);
            const auto actor=std::find_if(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return p.actorId==pendingActor;});
            const auto pending=s.specialMechanisms.pendingParasiteLaunches().find(pendingActor);
            if(!source||source->alive||source->health!=100||source->mission!="ParasiteLimbo"||!host||host->health!=400
                ||actor==s.projectiles.end()||actor->elapsedTicks<1||actor->elapsedTicks>=actor->totalTicks
                ||pending==s.specialMechanisms.pendingParasiteLaunches().end()||pending->second.sourceObjectId!=pendingSource
                ||pending->second.targetObjectId!=pendingHost||!s.specialMechanisms.parasiteLinks().empty()
                ||s.specialMechanisms.camouflageStates().contains(pendingSource))
                return fail("native launch-following tick did not preserve safe mid-flight authority: source="+describe(source)+" host="+describe(host)
                    +" camouflage="+std::to_string(s.specialMechanisms.camouflageStates().contains(pendingSource)));
            if(!reservedReturnSlotLegal(s))return false;
            facade(s);++pendingLaunches;
        }
        std::vector<Expected> expected;
        const auto remember=[&](int id,const std::string& role,bool death,std::optional<RuntimeSpatialAudioPosition> parent=std::nullopt){
            const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o||o->health!=runtimeMaxHealthForObject(s,*o))return fail("missing/full-health actor "+role);
            RuntimeSpatialAudioPosition sound{o->hasRenderPosition?o->renderX:double(o->x),o->hasRenderPosition?o->renderY:double(o->y),
                worldObjectHasGroundRenderPose(*o)?o->renderLevel:double(o->level)};
            expected.push_back({*o,role,death,parent.value_or(sound)});return true;};
        if(!remember(walker,"nativeAnchorMove",true))return false;
        if(pendingActor>=0&&(!remember(pendingHost,"nativePendingHost",true)||!remember(pendingSource,"nativePendingReturn",true)))return false;
        const std::array<const char*,8> types{"E1","ENGINEER","SHK","MTNK","SMCV","SREF","TANY","HTNK"};
        const std::array<const char*,3> owners{"Americans","Russians","Neutral"};int n=0;
        for(int a=whole?31:(high?34:35);a<=(whole?39:(high?36:35));++a)for(int lane=0;lane<3;++lane){
            // Reserve the pending source's original anchor. Three real ground
            // infantry share the adjacent lane's three native subcell slots.
            const bool reserve=pendingActor>=0&&a==34&&lane==0;const auto p=point(a,reserve?1:lane);
            if(p==target)continue;const int id=add(types[n%types.size()],owners[lane],p);++n;
            if(id<0||!author(id,p,true,deck)||!remember(id,reserve?"authoredReservedReturnLane1":"authoredDeckLane"+std::to_string(lane),true))return fail("native full-health deck actor");}
        // Units share the native anchor; infantry retain free side-lane slots.
        // Fractional drawing offsets alone do not make infantry/vehicle logic
        // occupancy valid, so no ground infantry is placed in this unit cell.
        const auto transportPoint=target;if(!author(transport,transportPoint,true,deck))return false;
        findMutableObjectById(s,transport)->renderX+=.25;
        if(!remember(transport,"nativeLoadedTransport",true))return false;
        const auto transportSound=expected.back().sound;if(!remember(passenger,"nativeContainedPassenger",true,transportSound))return false;
        const auto groundedPoint=point(whole?31:(high?34:35),pendingActor>=0?1:(high?0:2));
        const int grounded=add("JUMPJET","Neutral",groundedPoint);if(grounded<0||!author(grounded,groundedPoint,true,deck)||!remember(grounded,"authoredGroundedJumpjet",true))return false;
        // Separate continuous positions distinguish coincident fixture audio.
        findMutableObjectById(s,grounded)->renderX+=.125;expected.back().before=*findObjectById(s,grounded);expected.back().sound.x+=.125;
        for(const auto& entry:std::array<std::tuple<const char*,RuntimeCellPoint,bool,double>,5>{{
            {"bank",point(29,0),false,double(deck)},{"parallel",point(35,0,14),true,double(deck)},
            {whole?"farBank":"intactSegment",whole?point(41,0):point(38,0),!whole,double(deck)},
            {"underBridge",target,false,0.0},{"flyingJumpjet",target,false,double(deck+8)}}}){
            const auto& [role,p,bridge,z]=entry;const bool air=std::string(role)=="flyingJumpjet";const int id=add(air?"JUMPJET":"MTNK","Russians",p);
            if(id<0||!author(id,p,bridge,z,air)||!remember(id,role,false))return fail("native survivor control");}
        // Real begin/advance produces opposite logical/body bridge identities.
        // These are authored endpoint setups, not fabricated fractional XYZ.
        const auto motion=[&](bool departing){const auto deckPoint=target,shorePoint=point(35,-1);
            const auto from=departing?deckPoint:shorePoint,to=departing?shorePoint:deckPoint;const int id=add("HTNK","Neutral",to);
            if(id<0||!author(id,to,!departing,deck))return fail("motion actor");auto* o=findMutableObjectById(s,id);
            o->hasRenderPosition=false;o->mission="Move";RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Move;b.state=RuntimeBehaviorState::Moving;
            b.moveTarget=to;b.moveCooldownTicks=10;beginRuntimeRenderMove(*o,b,from,to,deck,departing,10,&s);
            if(!advanceRuntimeRenderMove(s,*o,b)||o->renderOnBridge!=departing||std::lround(o->renderX)!=from.x||std::lround(o->renderY)!=from.y
                ||o->onBridge==o->renderOnBridge)return fail("real motion sample lacks required opposed identities: "+describe(o));
            b.moveCooldownTicks=9;s.behaviors[id]=b;++motionChecks;return remember(id,departing?"bodyDeckLogicalBank":"bodyBankLogicalDeck",departing);};
        if(!motion(true)||!motion(false))return false;
        const auto subcellCheck=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),runtimeGateSubcellContext(s));
        for(const auto& action:subcellCheck.actions)if(action.kind!=RuntimeSubcellReconcileActionKind::DroppedReservation)
            return fail("native fixture has uncommitted subcell occupancy #"+std::to_string(action.objectId)+" previous="
                +std::to_string(action.previousSubCell)+" "+describe(findRuntimeObjectByIdIncludingInactive(s,action.objectId)));
        if(!reservedReturnSlotLegal(s))return false;
        const auto originalTerrain=s.world.getTerrainCells();
        std::set<int> initialIds;for(const auto& o:s.world.getObjects())initialIds.insert(o.instanceId);
        refreshRuntimeObjectOccupancyAfterLocalChange(s);s.selectedObjectIds.clear();for(const auto& e:expected)if(e.before.alive)s.selectedObjectIds.push_back(e.before.instanceId);
        if(!loadRuntimeSoundDatabase(s))return fail("original sound bank");s.cameraClampViewport={0,0,1400,900};s.camera.initialized=true;
        const auto center=runtimeProjectCellXY(target.x,target.y,deck,s.camera);s.camera.worldOffsetX=center.x-700;s.camera.worldOffsetY=center.y-450;
        s.runtimeSoundPlaybackHistory.clear();s.runtimeSoundIntentHistory.clear();stage="before";
        for(const auto& e:expected)if(e.role=="flyingJumpjet"){const auto* o=findObjectById(s,e.before.instanceId);
            if(!o||!o->airborne||o->flightLevel!=deck+8||o->level!=deck+8||!o->hasRenderPosition||o->renderLevel!=deck+8||o->renderOnBridge)
                return fail("airborne fixture lacks consistent original flight/body altitude: "+describe(o));}
        auto ordinary=checkpoint(s,false),canonical=checkpoint(s,true);if(!ordinary||!canonical)return fail("strict before save: "+error);
        std::vector<std::pair<std::string,RuntimeWindowState*>> replicas{{"original",&s},{"ordinary",ordinary.get()},{"canonical",canonical.get()}};
        const auto verify=[&](RuntimeWindowState& q,const std::string& replica,bool collapsed,bool audio,bool recordStats=true){
            if(q.rules.getDetailRulesHash()!=rulesHash)return fail(replica+" original rules changed");
            if(audio&&!q.runtimeSoundPlaybackCapture)return fail(replica+" full original playback observation is not bound");
            for(const auto& before:originalTerrain){const int a=alongX?before.x:before.y,lane=alongX?before.y-32:before.x-32;
                const bool affected=collapsed&&lane>=0&&lane<3&&a>=(whole?30:34)&&a<=(whole?40:36);
                const auto found=q.terrainByCell.find(runtimeCellKey(before.x,before.y));const auto* now=found==q.terrainByCell.end()?nullptr:found->second;
                if(!now||now->tileIndex!=before.tileIndex||now->subTileIndex!=before.subTileIndex||now->level!=before.level||now->iceGrowth!=before.iceGrowth
                    ||(!affected&&(now->hasOverlay!=before.hasOverlay||now->overlayIndex!=before.overlayIndex||now->overlayFrame!=before.overlayFrame
                        ||now->overlayType!=before.overlayType||now->overlayImage!=before.overlayImage))||now->overlayIsOre!=before.overlayIsOre||now->overlayIsGem!=before.overlayIsGem)
                    return fail(replica+" original terrain/isolated bridge differs at "+std::to_string(before.x)+","+std::to_string(before.y));}
            std::set<int> actualIds;for(const auto& o:q.world.getObjects())actualIds.insert(o.instanceId);
            if(actualIds!=initialIds)return fail(replica+" collapse emitted surviving crew or removed unrelated object identity");
            for(const auto& e:expected){const int id=e.before.instanceId;const auto* o=findRuntimeObjectByIdIncludingInactive(q,id);const bool dead=collapsed&&e.death;
                if(!o||o->type!=e.before.type||o->owner!=e.before.owner||(dead?(o->alive||o->health!=0||!q.deathWeapons.hasTriggered(id)):
                    (o->alive!=e.before.alive||o->health!=e.before.health||q.deathWeapons.hasTriggered(id))))return fail(replica+" "+e.role+" expectedDeath="+std::to_string(dead)+" got="+describe(o));
                if(e.role=="flyingJumpjet"&&(!o->airborne||o->level!=e.before.level||o->flightLevel!=e.before.flightLevel||o->renderLevel!=e.before.renderLevel||o->renderOnBridge))
                    return fail(replica+" airborne control changed its original flight/body altitude: "+describe(o));
                if(!collapsed&&e.before.alive&&!e.before.airborne){
                    const bool exactPose=e.before.renderMoveActive||worldObjectHasStoppedGroundPose(e.before);
                    const double x=e.before.hasRenderPosition?e.before.renderX:double(e.before.x),y=e.before.hasRenderPosition?e.before.renderY:double(e.before.y);
                    const double z=e.before.hasRenderPosition?e.before.renderLevel:double(e.before.level);
                    const bool bridge=e.before.hasRenderPosition?e.before.renderOnBridge:e.before.onBridge;
                    const double actualX=o->hasRenderPosition?o->renderX:double(o->x),actualY=o->hasRenderPosition?o->renderY:double(o->y);
                    const double actualZ=o->hasRenderPosition?o->renderLevel:double(o->level);const bool actualBridge=o->hasRenderPosition?o->renderOnBridge:o->onBridge;
                    if((exactPose&&(!o->hasRenderPosition||o->renderMoveActive!=e.before.renderMoveActive))||actualX!=x||actualY!=y||actualZ!=z||actualBridge!=bridge)
                        return fail(replica+" saved ground body/bridge identity differs: "+e.role+" "+describe(o));}
                if(dead&&(q.behaviors.contains(id)||std::find(q.selectedObjectIds.begin(),q.selectedObjectIds.end(),id)!=q.selectedObjectIds.end()
                    ||q.pendingTransportTargets.contains(id)||q.pendingTimedBombIntents.contains(id)))return fail(replica+" dead actor retained command/selection/relationship "+std::to_string(id));
                if(dead&&e.role=="nativePendingReturn"&&(!o->onBridge||o->level!=deck||o->x<34||o->x>36||o->y<32||o->y>34||o->hasRenderPosition))
                    return fail(replica+" pending source corpse lacks its actual collapsed-deck return body: "+describe(o));
                csv<<label<<','<<replica<<','<<(collapsed?"after":"before")<<','<<id<<','<<e.before.type<<','<<e.before.owner<<','<<e.role<<','
                    <<e.before.x<<','<<e.before.y<<','<<e.before.renderX<<','<<e.before.renderY<<','<<e.before.renderLevel<<','<<e.before.renderOnBridge<<','
                    <<e.before.airborne<<','<<dead<<','<<o->alive<<','<<o->health<<','<<q.deathWeapons.hasTriggered(id)<<'\n';
                if(audio&&dead){auto choices=splitRuntimeTokenList(raw.getValue(e.before.type,"DieSound",""));
                    if(choices.empty()&&e.before.kind==WorldObjectKind::Unit)choices={"GenVehicleDie"};int count=0;
                    RuntimeSpatialAudioPosition sound=e.sound;
                    if(e.role=="nativePendingReturn")sound={double(o->x),double(o->y),double(o->level)};
                    for(const auto& r:*q.runtimeSoundPlaybackCapture)if(r.spatial&&std::abs(r.sourcePosition.x-sound.x)<1e-8&&std::abs(r.sourcePosition.y-sound.y)<1e-8
                        &&std::abs(r.sourcePosition.level-sound.level)<1e-8&&std::find(choices.begin(),choices.end(),r.section)!=choices.end()){
                        if(!r.playable||r.sample.empty()||r.path.empty())return fail(replica+" unplayable original death cue "+r.section);++count;}
                    if(count!=(choices.empty()?0:1)){std::ostringstream detail;detail<<std::setprecision(17)<<replica<<" death cue count="<<count<<" for "<<e.role<<" id="<<id
                        <<" expectedXYZ="<<sound.x<<','<<sound.y<<','<<sound.level<<" rawSections=";
                        for(const auto& c:choices)detail<<c<<';';detail<<" historySize="<<q.runtimeSoundPlaybackHistory.size()<<" captured="<<q.runtimeSoundPlaybackCapture->size()<<" soundEvents="<<q.runtimeSoundEvents
                            <<" corpse="<<describe(o)<<" before="<<describe(&e.before);
                        for(const auto& r:*q.runtimeSoundPlaybackCapture)detail<<"\nactual="<<r.section<<" XYZ="<<r.sourcePosition.x<<','<<r.sourcePosition.y<<','<<r.sourcePosition.level
                            <<" order="<<r.playbackOrder<<" tick="<<r.tick<<" spatial="<<r.spatial<<" playable="<<r.playable<<" sample="<<r.sample
                            <<" status="<<r.status<<" suppressed="<<r.suppressed<<" gain="<<r.gain16384;
                        std::ofstream report(root/(label+"-"+replica+"-death-audio-diagnostic.txt"));report<<detail.str()<<'\n';return fail(detail.str());}if(count&&recordStats)++sounds;}
            }
            if(pendingActor>=0){const auto* source=findRuntimeObjectByIdIncludingInactive(q,pendingSource);
                const auto actor=std::find_if(q.projectiles.begin(),q.projectiles.end(),[&](const auto& p){return p.actorId==pendingActor;});
                if(!collapsed){const auto pending=q.specialMechanisms.pendingParasiteLaunches().find(pendingActor);
                    if(!source||source->alive||source->health!=100||source->mission!="ParasiteLimbo"||actor==q.projectiles.end()
                        ||actor->attackerObjectId!=pendingSource||actor->targetObjectId!=pendingHost||actor->weaponId!="DroneJump"
                        ||pending==q.specialMechanisms.pendingParasiteLaunches().end()||pending->second.sourceObjectId!=pendingSource
                        ||pending->second.targetObjectId!=pendingHost||!q.specialMechanisms.parasiteLinks().empty())
                        return fail(replica+" strict save lost original pending leap authority");}
                else {const int axial=source?(alongX?source->x:source->y):-1,lane=source?(alongX?source->y-32:source->x-32):-1;
                    // The dead record retains the actual integer return body,
                    // independently of its original launch and of audio events.
                    if(!source||source->alive||source->health!=0||!q.deathWeapons.hasTriggered(pendingSource)||!source->onBridge
                        ||source->level!=deck||axial<34||axial>36||lane<0||lane>2||source->hasRenderPosition
                        ||!q.specialMechanisms.pendingParasiteLaunches().empty()||!q.specialMechanisms.parasiteLinks().empty())
                        return fail(replica+" pending source did not return onto the collapsed footprint and die in the next sweep: "+describe(source));
                    if(audio&&recordStats){++pendingReturnChecks;returnCsv<<std::setprecision(17)<<label<<','<<replica<<','<<pendingSource<<','<<source->x<<','<<source->y<<','
                        <<source->level<<','<<source->onBridge<<','<<source->alive<<','<<source->health<<','<<q.deathWeapons.hasTriggered(pendingSource)<<','
                        <<quoted(raw.getValue(source->type,"DieSound",""))<<'\n';returnCsv.flush();}}}
            if(collapsed){const auto* host=findRuntimeObjectByIdIncludingInactive(q,transport);if(!host||!host->transportPassengerIds.empty())return fail(replica+" transport cargo not cleared");
                if(!q.pendingObjectExits.empty()||q.drainingObjectExits||!q.pendingBridgeCollapses.empty()||!q.deferredBridgeMutationCells.empty())
                    return fail(replica+" destruction/bridge mutation queues not drained");}
            return true;};
        for(auto [replica,q]:replicas){RuntimeSnapshotActiveStateScope active(*q);if(!verify(*q,replica,false,false))return false;
            if(!reservedReturnSlotLegal(*q))return false;
            q->runtimeSoundPlaybackHistory.clear();stage="collapse";const auto* repairHut=findObjectById(*q,hut);
            std::vector<RuntimeSoundPlaybackRecord> captured;SoundCaptureScope captureScope(*q,captured);std::size_t captureRows=0;
            const auto writeCaptured=[&](){for(;captureRows<captured.size();++captureRows){const auto& r=captured[captureRows];
                soundCsv<<std::setprecision(17)<<label<<','<<replica<<','<<r.playbackOrder<<','<<r.tick<<','<<quoted(r.section)<<','
                    <<r.sourcePosition.x<<','<<r.sourcePosition.y<<','<<r.sourcePosition.level<<','<<r.spatial<<','<<r.playable<<','
                    <<quoted(r.sample)<<','<<quoted(r.path)<<','<<quoted(r.status)<<'\n';}soundCsv.flush();};
            // State-machine contract for a mutation nested in an active exit
            // batch. This does not claim to dispatch an actual Ivan blast.
            const bool nested=replica=="canonical"&&hz==30&&high&&alongX;
            if(nested)q->drainingObjectExits=true;
            const auto result=whole?demolishRuntimeBridgeByHut(*q,*repairHut,-1,"Americans"):damageRuntimeBridgeAtCell(*q,target,strength,0,-1,"Americans");
            if(!result.hit||!result.destroyed)return fail(replica+" native mutation failed");
            if(nested){if(q->pendingBridgeCollapses.empty()||q->deferredBridgeMutationCells.empty())return fail("nested collapse lost deferred footprint/refresh");
                for(const auto& e:expected){const auto* o=findRuntimeObjectByIdIncludingInactive(*q,e.before.instanceId);
                    if(!o||o->alive!=e.before.alive||o->health!=e.before.health||o->renderLevel!=e.before.renderLevel||o->renderOnBridge!=e.before.renderOnBridge)
                        return fail("nested collapse refreshed or finalized body before outer drain: "+e.role+" "+describe(o));}
                q->drainingObjectExits=false;++nestedChecks;}
            (void)drainRuntimeObjectExitQueue(*q);
            writeCaptured();
            if(!verify(*q,replica,true,true))return false;
            const auto beforeEvents=q->runtimeSoundEvents;const auto beforeTombstones=q->deathWeapons.triggeredObjects().size();const auto beforeRecords=captured.size();
            for(const auto& e:expected)if(e.death){RuntimeObjectExitRequest duplicate;duplicate.objectId=e.before.instanceId;duplicate.tick=q->tick;
                if(queueRuntimeObjectExit(*q,duplicate)!=RuntimeObjectExitQueueResult::AlreadyFinalized)return fail(replica+" duplicate death accepted");}
            (void)drainRuntimeObjectExitQueue(*q);writeCaptured();if(q->runtimeSoundEvents!=beforeEvents||captured.size()!=beforeRecords
                ||q->deathWeapons.triggeredObjects().size()!=beforeTombstones)return fail(replica+" repeated drain replayed death");
            if(pendingActor>=0){
                // Immediate corpse/audio/queue checks above precede this native
                // Effects-only retirement. Saves must not retain a live leap
                // actor whose host exit already removed its pending authority.
                const auto live=[&](){return std::any_of(q->projectiles.begin(),q->projectiles.end(),[&](const auto& p){return p.actorId==pendingActor;});};
                for(int t=0;t<hz*3&&live();++t){++q->tick;advanceRuntimeEffects(*q);(void)drainRuntimeObjectExitQueue(*q);}
                if(live()||!q->specialMechanisms.pendingParasiteLaunches().empty()||!q->specialMechanisms.parasiteLinks().empty())
                    return fail(replica+" original Effects failed to retire the terminated pending leap");
                if(q->deathWeapons.triggeredObjects().size()!=beforeTombstones)return fail(replica+" leap Effects retirement caused a second death");
                writeCaptured();if(!verify(*q,replica+"-effects-retired",true,true,false))return false;++pendingEffectCleanups;}
        }
        stage="after";auto saved=checkpoint(s,false),exact=checkpoint(s,true);if(!saved||!exact)return fail("strict after save: "+error);
        if(!verify(*saved,"after-ordinary",true,false)||!verify(*exact,"after-canonical",true,false))return false;
        for(const auto& e:expected){if(e.death)++deaths;else ++survivors;}++scenes;csv.flush();
        std::cout<<"Bridge collapse occupants "<<label<<" PASS nativeAnchorMove/explicitSideLanes/groundedJumpjet/flying/underBridge/bodyLogicalOpposition/cargo/crew/sound/ordinary/canonical/once\n";
    }
    if(!scenes)return fail("filter matched no scenes");std::ofstream summary(root/"summary.txt");
    summary<<"scenes="<<scenes<<" deaths="<<deaths<<" survivors="<<survivors<<" saves="<<saves<<" deathSamples="<<sounds
        <<" nativeMoves="<<moves<<" motionSamples="<<motionChecks<<" boarding="<<boardingChecks<<" protected="<<protectedChecks<<" nestedContracts="<<nestedChecks
        <<" pendingLaunches="<<pendingLaunches<<" pendingReturnChecks="<<pendingReturnChecks<<" pendingEffectCleanups="<<pendingEffectCleanups<<'\n';
    std::cout<<"Bridge collapse occupants summary PASS scenes="<<scenes<<" deaths="<<deaths<<" survivors="<<survivors<<" saves="<<saves
        <<" deathSamples="<<sounds<<" nativeMoves="<<moves<<" motionSamples="<<motionChecks<<" boarding="<<boardingChecks<<" protected="<<protectedChecks<<" nestedContracts="<<nestedChecks
        <<" pendingLaunches="<<pendingLaunches<<" pendingReturnChecks="<<pendingReturnChecks<<" pendingEffectCleanups="<<pendingEffectCleanups<<'\n';return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
