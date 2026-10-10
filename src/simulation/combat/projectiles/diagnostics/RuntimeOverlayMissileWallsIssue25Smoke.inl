// Issue25/10 complete map-overlay wall acceptance smoke.
// Requires the reviewed original-wall value/save/combat implementation.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeOverlayMissileWallsIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    using namespace RuntimeBuildingDepthOracle;
    std::cout<<std::unitbuf;
    std::string label="original-input",stage="catalog";
    const auto fail=[&](const std::string& text){error="Issue25 map overlay walls: "+label+" ["+stage+"]: "+text;return false;};
    const char* directory=std::getenv("CONCORD_OVERLAY_MISSILE_WALLS_ISSUE25_OUTPUT");
    const std::filesystem::path out=directory&&*directory?directory:"build/smoke/feature-set-25/10-spawned-missile-walls/overlay-complete";
    std::filesystem::create_directories(out);
    const char* selected=std::getenv("CONCORD_OVERLAY_MISSILE_WALLS_ISSUE25_CASE");const std::string subset=selected?selected:"";
    if(!subset.empty()&&subset!="core"&&subset!="controls"&&subset!="highbank")return fail("CASE is core, controls or highbank; empty requires all40/10");
    const auto q=[](const std::string& v){std::string r="\"";for(char c:v){if(c=='"')r+='"';r+=c;}return r+'"';};
    try {
        const auto asset=std::filesystem::path(runtimeAssetRootPath(live.config));
        const auto rawBytes=readPinned(asset/"ra2md/localmd/rulesmd.ini","06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05");
        const auto artBytes=readPinned(asset/"ra2md/localmd/artmd.ini","e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31");
        const auto pal=readPinned(asset/"extracted_palettes/cache_unittem.pal","ed785e62eed291480f3198dd44f6b656ebe3a9b75e9f641944d710abc6bde3e3");
        IniFile raw,art;if(!raw.loadFromString(std::string(rawBytes.begin(),rawBytes.end()))||!art.loadFromString(std::string(artBytes.begin(),artBytes.end()))||pal.size()!=768||*std::max_element(pal.begin(),pal.end())>63)return fail("original INI/palette");
        const auto yes=[&](const std::string& id,const std::string& key){return toLowerAsciiLocal(raw.getValue(id,key,"no"))=="yes";};
        const auto num=[&](const std::string& id,const std::string& key,double fallback=-999){return std::stod(raw.getValue(id,key,std::to_string(fallback)));};
        const auto ordinal=[&](const char* id){const auto* s=raw.getSection("OverlayTypes");int n=0;if(s)for(const auto& [key,value]:*s){if(key.empty()||!std::all_of(key.begin(),key.end(),[](unsigned char c){return std::isdigit(c);}))continue;if(toUpperAsciiLocal(trimAsciiLocal(value))==id)return std::pair<int,int>{n,std::stoi(key)};++n;}return std::pair<int,int>{-1,-1};};
        struct Wall {const char* id;int ordinal,key,hp,levels,frames;const char* armor;const char* archive;const char* archiveHash;std::size_t offset,length;const char* hash;Bytes bytes;};
        std::array<Wall,8> walls{{
            {"GASAND",0,1,100,2,64,"wood","ra2/temperat.mix","e8cab1997e9aeba5a44570e38a7de00640a8d9124f458bfed6980c63861d0c78",2537606,36208,"cc6deb59d0c8e8edd3d3a9b4647a050be87989f805ddf68a2ba5186332a83f10",{}},
            {"GAWALL",2,3,300,3,96,"concrete","ra2/temperat.mix","e8cab1997e9aeba5a44570e38a7de00640a8d9124f458bfed6980c63861d0c78",2573814,87936,"23c35bc6da4e2f81cfaaae726ec431264d526303f788249c9e44087e1310b31a",{}},
            {"NAWALL",26,27,300,3,96,"concrete","ra2/temperat.mix","e8cab1997e9aeba5a44570e38a7de00640a8d9124f458bfed6980c63861d0c78",2661750,66488,"31e694ae51865eb378dcea863a7671afed4e5406617ada5598a59afa56d23c17",{}},
            {"CAFNCB",203,207,100,2,64,"wood","ra2/isotemp.mix","68a1c51c4e21ba76556d7aa460fd9fe034858f7b916769aa8166b40b9ce9c1d4",18193982,19544,"eefb4005b5a0e9c352cb255bb736b7cf6e5552d4ac58591a2b5eb9277779ac5d",{}},
            {"CAFNCW",204,208,100,2,64,"wood","ra2/isotemp.mix","68a1c51c4e21ba76556d7aa460fd9fe034858f7b916769aa8166b40b9ce9c1d4",18213534,19304,"c58f5e221d2d836a7399b8ac2e353c180e9fb2ac69a7d19a609f9082796d5999",{}},
            {"CAKRMW",240,244,300,2,96,"wood","ra2/isotemp.mix","68a1c51c4e21ba76556d7aa460fd9fe034858f7b916769aa8166b40b9ce9c1d4",18717134,103624,"3b2d34b8c336ec11af002f67d0f2afc4a3085a26b27eaaa4f38c8f765f2c95c7",{}},
            {"CAFNCP",241,245,100,2,64,"wood","ra2/isotemp.mix","68a1c51c4e21ba76556d7aa460fd9fe034858f7b916769aa8166b40b9ce9c1d4",18232846,85480,"1d0d4d6dd112ef7e229ab9f690c23de5f57ff64d92c142f3a18d797e68dfb72f",{}},
            {"GAFWLL",243,247,300,3,96,"concrete","ra2md/genermd.mix","00ec7c6f020f2286533551ba216b653b5a721805e6f7cacfc3d52e05122ccb0e",8267942,115720,"b6ff265df42434fee85317dfc4d5ea047ca98767052e0ecb6c98a34888e4b6e5",{}}}};
        struct Stock {const char* parent;const char* child;const char* launcher;const char* wh;const char* eliteWh;int hp,slots,damage,eliteDamage,pause,tilt,rof,burst,regen;double minimum,range;};
        const std::array<Stock,2> stocks{{{"V3","V3ROCKET","V3Launcher","V3WH","V3EWH",150,1,200,400,0,60,150,0,400,5,18},{"DRED","DMISL","DredLauncher","DMISLWH","DMISLEWH",800,2,300,600,20,60,50,2,80,8,25}}};
        std::ofstream input(out/"overlay-input.csv"),commands(out/"overlay-commands.csv"),states(out/"overlay-states.csv"),impacts(out/"overlay-impacts.csv"),checks(out/"overlay-checkpoints.csv"),cases(out/"overlay-cases.csv"),schemaChecks(out/"overlay-schema-memory.csv"),replayChecks(out/"overlay-replay-diff.csv"),displayChecks(out/"overlay-projectile-display-diff.csv"),sources(out/"overlay-sources.txt");
        if(!input||!commands||!states||!impacts||!checks||!cases||!schemaChecks||!replayChecks||!displayChecks||!sources)return fail("private output");for(auto* f:{&input,&commands,&states,&impacts,&checks,&cases,&schemaChecks,&replayChecks,&displayChecks,&sources})*f<<std::setprecision(17);
        input<<"id,ordinal,list_key,strength,armor,damage_levels,frame_count,archive,raw_hash,loaded_passed\n";
        commands<<"case,stage,replica,tick,source,cell_x,cell_y,force,raw_index,pixel_x,pixel_y,cursor,valid,accepted,rejected,verb,kind,target,attack_ground,goal_x,goal_y,passed\n";
        states<<"case,stage,replica,tick,x,y,health,has_overlay,live_index,live_frame,original_index,original_frame,original_type,original_image,expected_health,expected_mask,expected_bank,passed\n";
        impacts<<"case,stage,replica,tick,actor,source,weapon,warhead,spawned,base,multiplier,target,impact_x,impact_y,x,y,before_hp,after_hp,distance,raw_damage,expected_delta,actual_delta,passed\n";
        checks<<"case,stage,format,path,tick,live_world_hash,peer_world_hash,live_special_hash,peer_special_hash,world_equal,wall_equal,base_path,passed\n";
        cases<<"case,group,passed,shots_live,impacts_live,ordinary_reads,canonical_reads,paired_ticks,BMP\n";
        schemaChecks<<"case,stage,format,input_schema,executed,accepted,before_world_hash,after_world_hash,before_special_hash,after_special_hash,world_equal,wall_equal,status,error\n";
        replayChecks<<"case,stage,peer_index,format,live_tick,peer_tick,live_world_hash,peer_world_hash,live_special_hash,peer_special_hash,tick_equal,world_equal,special_equal,projectiles_equal,raw_projectiles_equal,persistent_behaviors_equal,canonical_behaviors_equal,player_commands_equal,wall_equal,required_behavior_equal,all_equal\n";
        displayChecks<<"case,stage,peer_index,format,tick,actor_id,raw_projectiles_equal,authority_projectiles_equal,live_raw_serializer,peer_raw_serializer\n";
        std::set<std::string> recordedDisplayDifferences;
        sources<<"Candidate40 =32 core and8 focused controls;10 complete physical state materializations(5 ordinary14+5 canonical), 10 unique physical files with15 file-reopen actions (canonical also reopens its existing complete14 base), plus2 complete legacy no-section and2 complete Schema1 seven-column in-memory materializations. Three malformed Schema2 inputs must atomically reject only when the actual writer emits Schema2; otherwise they are explicitly pending, not passed. Subset is not full acceptance.\nOriginal rulesSHA="<<sha256(rawBytes)<<" ART="<<sha256(artBytes)<<" unittem="<<sha256(pal)<<"\n"
            <<"Constructed fixture uses original flat TMP resources and immutable MapFacts overlay input; no claim of pre-existing walls in base map. Native index=y*512+x, runtime lookup=y*1000+x.\n"
            <<"Ordinary14 full persistent Behaviors fields and complete PlayerCommands serialized queues/targets/flags/clocks compare exactly; state/path/pathRequestPending are reconstructed by the original14 parser. Canonical additionally compares all behavior fields. Initial and every paired real tick keep raw world, special, authority Projectiles/5, wall identity/HP/currentframe exact, without ignored ticks. Only four sourceScreenOffsetXY/nativeFixedXY presentation scalars are excluded by the existing formal projectile authority contract; first raw display differences per scene/peer are logged. No future per-pixel rendering equality is claimed.\n"
            <<"Formal initializeWorldOverlayWallState creates each wall. Frame0 HP=rawStrength. CAKRMW DL2 frame37 quarter75 is explicitly project compatibility policy, not original exactHP. Positive extra bank2 persists, low4 connections are independently computed; death retains identity tombstone.\n"
            <<"Damage expected from raw terminal200/400,300/600, elite FIREPOWER1.1, raw Verses/falloff/rounding. Launcher1/50 is not payload. No HP/slot/released flag, damage call or proxy target seeded. Native SHP opaque index/color and real surface point dispatch actual HWND mouse input.\n";
        sources.flush();
        std::map<std::string,Bytes> archives;
        for(auto& w:walls){label=w.id;auto found=archives.find(w.archive);if(found==archives.end())found=archives.emplace(w.archive,readPinned(asset/w.archive,w.archiveHash)).first;
            const auto& a=found->second;if(w.offset>a.size()||w.length>a.size()-w.offset)return fail("original MIX slice");w.bytes.assign(a.begin()+w.offset,a.begin()+w.offset+w.length);
            const auto* loaded=live.rules.findBuildingType(w.id);const bool good=ordinal(w.id)==std::pair<int,int>{w.ordinal,w.key}&&yes(w.id,"Wall")&&num(w.id,"Strength")==w.hp&&toLowerAsciiLocal(raw.getValue(w.id,"Armor"))==w.armor
                &&art.getValue(w.id,"Foundation")=="1x1"&&art.getValue(w.id,"ToOverlay")==w.id&&std::stoi(art.getValue(w.id,"DamageLevels","1"))==w.levels&&sha256(w.bytes)==w.hash&&little(w.bytes,6,2)==unsigned(w.frames)&&loaded&&loaded->wall&&loaded->strength==w.hp&&toLowerAsciiLocal(loaded->armor)==w.armor;
            input<<w.id<<','<<w.ordinal<<','<<w.key<<','<<w.hp<<','<<w.armor<<','<<w.levels<<','<<w.frames<<','<<w.archive<<','<<sha256(w.bytes)<<','<<good<<'\n';input.flush();if(!good)return fail("original wall registry/rule/art/raw source/loaded contract");}
        const auto pinStock=[&](const RulesDatabase& rules,const Stock& v){const auto* p=rules.findUnitType("UnitType",v.parent);const auto* c=rules.findUnitType("AircraftType",v.child);const auto* w=rules.findWeaponType(v.launcher);const auto* m=rules.findSpawnedMissileProfile(v.child);const auto* life=rules.findWeaponLifecycleProfile(v.launcher);
            return p&&c&&w&&m&&life&&p->strength==v.hp&&p->spawns==v.child&&p->spawnsNumber==v.slots&&p->spawnRegenRate==v.regen&&c->strength==50&&c->spawned&&c->missileSpawn&&raw.getValue(v.child,"Selectable")=="no"
                &&w->spawner&&w->warhead=="Special"&&w->range==v.range&&w->minimumRange==v.minimum&&w->rof==v.rof&&w->burst==v.burst&&std::max(1,w->burst)==v.slots
                &&m->damage==v.damage&&m->eliteDamage==v.eliteDamage&&m->warhead==v.wh&&m->eliteWarhead==v.eliteWh&&m->pauseFrames==v.pause&&m->tiltFrames==v.tilt&&m->altitudeLeptons==768
                &&life->omniFire==yes(v.launcher,"OmniFire")&&life->fireOnce==yes(v.launcher,"FireOnce")&&life->distributedFire==yes(v.launcher,"DistributedFire")&&yes(v.wh,"Wall")&&yes(v.eliteWh,"Wall")
                &&num(v.parent,"Strength")==v.hp&&raw.getValue(v.parent,"Spawns")==v.child&&num(v.child,"Strength")==50&&num(v.launcher,"Range")==v.range&&num(v.launcher,"MinimumRange")==v.minimum&&num(v.launcher,"ROF")==v.rof&&art.getValue(v.child,"SpawnDelay")=="2";};
        for(const auto& v:stocks)if(!pinStock(live.rules,v))return fail("original launcher/payload/lifecycle input");
        std::optional<TerrainCell> land,water;RuntimeMapRenderCache tileCache;const auto metadata=runtimeTerrainMetadataContext(live);
        const int waterSet=live.theater.getGeneralTileSetIndex("WaterSet");const auto* waterRule=live.rules.findLandType("Water");
        if(waterSet<0||!waterRule||waterRule->floatSpeed!=100.0||num("Water","Float")!=100.0)return fail("original WaterSet/Water.Float100 input");
        for(const auto& set:live.theater.getTileSets())for(int tile=set.startTileIndex;tile<set.startTileIndex+set.tilesInSet&&(!land||!water);++tile)for(int sub=0;sub<16&&(!land||!water);++sub){TerrainCell c;c.tileIndex=tile;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,c);if(info.passable&&!info.bridge&&!info.shore&&!info.cliff&&resolveRuntimeTerrainSubTileRampType(c,metadata,tileCache)==0){
            if(info.water&&!water&&live.theater.resolveTileIndex(tile).tileSetIndex==waterSet&&resolveRuntimeTerrainSubTileLandType(c,metadata,tileCache)==RuntimeTerrainLandType::Water)water=c;else if(!info.water&&!land)land=c;}}
        if(!land||!water)return fail("actual original flat TMP Clear and WaterSet/Water/ramp0");
        sources<<"Naval source actual WaterSet="<<waterSet<<" tile="<<water->tileIndex<<" sub="<<water->subTileIndex<<" authoredLand="<<int(resolveRuntimeTerrainSubTileLandType(*water,metadata,tileCache))<<" ramp="<<int(resolveRuntimeTerrainSubTileRampType(*water,metadata,tileCache))<<" rawFloat="<<raw.getValue("Water","Float")<<" loadedFloat="<<waterRule->floatSpeed<<"\n";sources.flush();
        struct ExpectedWall {const Wall* rule=nullptr;RuntimeCellPoint cell{};int initialFrame=0,hp=0;};
        struct Scene {std::unique_ptr<RuntimeWindowState> s;Stock stock;int rank=0,parent=-1,tank=-1,infantry=-1;std::map<int,ExpectedWall> expected;std::vector<std::unique_ptr<RuntimeWindowState>> peers;std::map<int,std::map<int,int>> hpBefore;std::set<int> shotIds;int liveImpacts=0;std::string problem;bool preserveAuthoredTopology=false;};
        int coreCount=0,controlCount=0,ordinaryReads=0,canonicalReads=0,legacyMemoryLoads=0,schema1MemoryLoads=0,schema2InvalidRejects=0,schema2InvalidPending=0,observedWallSchema=-1,pairedTicks=0,bmps=0,physicalFileReopens=0;std::set<std::string> physicalUniquePaths;
        const auto stepOne=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
        const auto normalizeFresh=[&](WorldObjectInstance& o){const auto x=runtimeObjectRenderX(o),y=runtimeObjectRenderY(o),z=runtimeObjectRenderLevel(o);const int hp=o.health,dock=o.dockedToObjectId,slot=o.dockIndex,clock=o.weaponCooldownTicksRemaining;
            if(o.renderMoveActive||o.airborne||x!=double(o.x)||y!=double(o.y)||z!=double(o.level))return fail("new idle input actual pose differs from logical");clearRuntimeRenderPosition(o);
            return (runtimeObjectRenderX(o)==x&&runtimeObjectRenderY(o)==y&&runtimeObjectRenderLevel(o)==z&&o.health==hp&&o.dockedToObjectId==dock&&o.dockIndex==slot&&o.weaponCooldownTicksRemaining==clock)||fail("standard new idle input initializer changed authority");};
        const auto add=[&](RuntimeWindowState& s,const char* id,RuntimeBuildKind kind,RuntimeCellPoint at){auto* o=addRuntimeObjectInstanceForOwner(s,"Russians",kind,id,at);if(!o)return -1;o->mission="Sleep";if(kind==RuntimeBuildKind::Building)o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;return normalizeFresh(*o)?o->instanceId:-1;};
        const auto make=[&](Stock stock,int rank,const std::vector<ExpectedWall>& inputs,bool initialTopology=true){Scene f;f.preserveAuthoredTopology=!initialTopology;f.stock=stock;f.rank=rank;f.s=std::make_unique<RuntimeWindowState>();auto& s=*f.s;seedRuntimeLoadScratch(s,live,false);
            if(!pinStock(s.rules,stock)){fail("copied original stock profiles");f.s.reset();return f;}s.tick=200;s.config.fixedTickHz=30;s.config.playerCountry="Russians";s.config.playerSlots.clear();s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
            s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=s.config.crates=s.config.superweapons=false;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.campaignMode=s.skirmishMatchFinished=false;
            s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();s.skirmishTeamByOwner.clear();s.skirmishCountryByOwner.clear();s.skirmishDifficultyByOwner.clear();s.triggers={};s.triggers.bootstrapComplete=true;s.economy={};s.economy.owner="Russians";s.economy.credits=10000;s.economy.skirmishStartApplied=true;s.initialOreFieldsSeeded=true;
            s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};s.mapFactsLoaded=true;s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();s.jumpjetFlights.clear();s.aircraftFlights.clear();s.deathWeapons.reset();s.pendingObjectExits.clear();s.pendingObjectExitIds.clear();s.selectedObjectIds.clear();s.playerCommands={};s.specialMechanismsInitialized=s.superWeaponsInitialized=s.subcellsInitialized=false;s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
            for(const auto& original:s.world.getOverlayCells())for(const auto& w:inputs)if(original.x==w.cell.x&&original.y==w.cell.y){fail("base immutable pack occupies constructed wall coordinate");f.s.reset();return f;}
            for(auto w:inputs){const int bank=w.initialFrame/16;w.hp=bank>=2?std::max(1,w.rule->hp/4):bank==1?std::max(1,w.rule->hp/2):w.rule->hp;f.expected.emplace(runtimeCellKey(w.cell.x,w.cell.y),w);}
            for(int y=44;y<=96;++y)for(int x=44;x<=96;++x){auto c=x<68?*water:*land;c.x=x;c.y=y;c.level=0;c.cellIndex512=y*512+x;MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=c.cellIndex512;fact.hasTerrain=true;fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=c.tileIndex;fact.terrain.subTileIndex=c.subTileIndex;fact.terrainClass=x<68?"water":"clear";fact.isWater=x<68;fact.passabilityHint="passable";
                const auto w=f.expected.find(runtimeCellKey(x,y));if(w!=f.expected.end()){const auto& e=w->second;c.hasOverlay=true;c.overlayIndex=e.rule->ordinal;c.overlayFrame=e.initialFrame;c.overlayType=c.overlayImage=e.rule->id;fact.hasOverlay=true;fact.overlay.cellIndex=c.cellIndex512;fact.overlay.x=x;fact.overlay.y=y;fact.overlay.overlayIndex=e.rule->ordinal;fact.overlay.type=fact.overlay.image=e.rule->id;fact.overlay.frameIndex=fact.overlay.frame=fact.overlay.overlayData=fact.overlay.fallbackFrame=e.initialFrame;
                    initializeWorldOverlayWallState(c,s.rules);if(c.overlayWall.health!=e.hp||c.overlayWall.originalIndex!=e.rule->ordinal||c.overlayWall.originalFrame!=e.initialFrame||c.overlayWall.originalType!=e.rule->id){fail("formal initializer differs from authored input");f.s.reset();return f;}}
                s.world.accessTerrainCells().push_back(c);s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);}
            s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.overlayCellCount=inputs.size();s.mapFacts.contentHash=0x25104000+rank+inputs.size()*100+(std::string(stock.parent)=="DRED"?10:0);
            const RuntimeCellPoint p{std::string(stock.parent)=="DRED"?64:68,64};f.parent=add(s,stock.parent,RuntimeBuildKind::Unit,p);if(f.parent<0){f.s.reset();return f;}auto* parent=findMutableObjectById(s,f.parent);parent->veterancy=rank;parent->facing=parent->turretFacing=runtimeFacingFromDelta(1,0);
            const auto* domain=runtimeMapQueryCell(s,p);if(!domain||domain->isWater!=(std::string(stock.parent)=="DRED")||domain->cellIndex!=p.y*512+p.x){fail("native512 parent domain");f.s.reset();return f;}
            if(add(s,"GAPOWR",RuntimeBuildKind::Building,{92,88})<0){f.s.reset();return f;}auto* enemy=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,"GAPOWR",{92,92});if(!enemy){f.s.reset();return f;}enemy->mission="Sleep";enemy->buildAnimTicksRemaining=enemy->buildAnimTicksTotal=0;if(!normalizeFresh(*enemy)){f.s.reset();return f;}
            if(initialTopology)refreshRuntimeOverlayWallTopology(s);initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);RuntimeHumanHouseAuthority h;h.owner=h.country="Russians";h.canonicalOwner=runtimeCanonicalHumanHouseOwner("Russians");h.localFacade=true;s.humanHouseAuthorities={h};initializeRuntimeOfflineGameplayRouting(s);s.particles.reset(30);s.particlesInitialized=s.particles.alignEmptyStateTick(s.tick);ensureRuntimeSuperWeaponsInitialized(s);ensureRuntimeLightingInitialized(s);stepOne(s);
            for(auto& o:s.world.accessObjects())if(!normalizeFresh(o)){f.s.reset();return f;}s.camera.tileWidth=60;s.camera.tileHeight=30;return f;};
        const auto expectedMask=[](const Scene& f,const ExpectedWall& w){if(f.preserveAuthoredTopology)return w.initialFrame&15;int mask=0;constexpr int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0},bit[4]={1,2,4,8};for(int i=0;i<4;++i){const auto n=f.expected.find(runtimeCellKey(w.cell.x+dx[i],w.cell.y+dy[i]));if(n!=f.expected.end()&&n->second.hp>0&&std::string(n->second.rule->id)==w.rule->id)mask|=bit[i];}return mask;};
        const auto verify=[&](const Scene& f,const RuntimeWindowState& s,const char* replica){bool okay=true;for(const auto& [key,w]:f.expected){const auto* c=findRuntimeTerrainCellAt(s,w.cell);int bank=w.hp*4<=w.rule->hp?std::min(2,w.rule->levels-1):w.hp*2<=w.rule->hp?std::min(1,w.rule->levels-1):0;if(w.initialFrame/16>=w.rule->levels)bank=w.initialFrame/16;const int mask=expectedMask(f,w);
                const auto* original=runtimeMapQueryCell(s,w.cell);const bool good=c&&c->overlayWall.health==w.hp&&c->overlayWall.originalIndex==w.rule->ordinal&&c->overlayWall.originalFrame==w.initialFrame&&c->overlayWall.originalType==w.rule->id&&c->overlayWall.originalImage==w.rule->id
                    &&original&&original->hasOverlay&&original->overlay.overlayIndex==w.rule->ordinal&&original->overlay.frameIndex==w.initialFrame&&original->overlay.type==w.rule->id
                    &&(w.hp>0?(c->hasOverlay&&c->overlayIndex==w.rule->ordinal&&c->overlayFrame==bank*16+mask&&c->overlayType==w.rule->id):(!c->hasOverlay&&c->overlayIndex==-1&&c->overlayType.empty()));
                states<<q(label)<<','<<q(stage)<<','<<replica<<','<<s.tick<<','<<w.cell.x<<','<<w.cell.y<<','<<(c?c->overlayWall.health:-999)<<','<<(c&&c->hasOverlay)<<','<<(c?c->overlayIndex:-999)<<','<<(c?c->overlayFrame:-999)<<','<<(c?c->overlayWall.originalIndex:-999)<<','<<(c?c->overlayWall.originalFrame:-999)<<','<<(c?c->overlayWall.originalType:"missing")<<','<<(c?c->overlayWall.originalImage:"missing")<<','<<w.hp<<','<<mask<<','<<bank<<','<<good<<'\n';okay=okay&&good;}
            states.flush();return okay||fail("original identity/HP/topology/dead tombstone/immutable map input");};
        // Ordinary14 Behaviors persists the complete 12-field record and its
        // optional force/ground/source/clearance fields (writer35360, parser36900).
        // It reconstructs Attack-ground state as Queued and plans transient
        // state/path/request fields again; canonical additionally preserves those.
        // Every subsequent real tick still requires raw world/special and complete Projectiles/5 authority,
        // all persistent command fields and queues to agree without a skipped tick.
        const auto ordinaryBehavior=[](const RuntimeObjectBehavior& v){return std::make_tuple(v.kind,v.targetObjectId,v.moveTarget.x,v.moveTarget.y,v.weaponCooldownTicks,v.moveCooldownTicks,v.attackBridge,v.attackBridgeCell.x,v.attackBridgeCell.y,v.pendingAllowedOccupiedObjectId,v.moveIntervalRemainderQ16,v.forceFire,v.attackGround,v.moveSegmentFrom.x,v.moveSegmentFrom.y,v.factoryExitClearanceHostId);};
        // Match the existing Projectiles/5 authority hash (Shell52732/57474).
        // This pure value copy removes only four presentation scalars; every
        // other serialized identity, physical coordinate, clock, rule and flag
        // remains exact. Raw serializers are retained in failure/display logs.
        const auto projectileAuthorityRecord=[](const RuntimeProjectileEffect& value){auto authority=value;authority.sourceScreenOffsetX=authority.sourceScreenOffsetY=0;authority.sourceNativeFixedX=authority.sourceNativeFixedY=0;return serializeRuntimeProjectileEffect(authority);};
        const auto fullEqual=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical){if(a.tick!=b.tick||a.world.deterministicSimulationStateHash()!=b.world.deterministicSimulationStateHash()||a.specialMechanisms.stateHash()!=b.specialMechanisms.stateHash()||a.projectiles.size()!=b.projectiles.size()||a.behaviors.size()!=b.behaviors.size()||a.playerCommands.serialize()!=b.playerCommands.serialize())return false;
            for(std::size_t i=0;i<a.projectiles.size();++i)if(projectileAuthorityRecord(a.projectiles[i])!=projectileAuthorityRecord(b.projectiles[i]))return false;for(const auto& [id,v]:a.behaviors){const auto r=b.behaviors.find(id);if(r==b.behaviors.end()||ordinaryBehavior(v)!=ordinaryBehavior(r->second)||(canonical&&runtimeMultiplayerSnapshotBehaviorRecord(id,v)!=runtimeMultiplayerSnapshotBehaviorRecord(id,r->second)))return false;}return true;};
        const auto capture=[&](Scene& f,const char* name,RuntimeCellPoint center,std::vector<std::uint32_t>* pixels=nullptr){auto& s=*f.s;const auto a=s.world.deterministicSimulationStateHash(),b=s.specialMechanisms.stateHash();RECT client{0,0,1000,700};centerRuntimeCameraOnCell(s,client,center);const auto stats=drawRuntimeStateOffscreenAtCell(s,center,1000,700,error,pathString(out/(label+"-"+name+".bmp")),false,false,-1,pixels,true,&s.camera);if(!stats||!stats->rendered||s.world.deterministicSimulationStateHash()!=a||s.specialMechanisms.stateHash()!=b)return fail("actual wall paint mutated authority or failed: "+error);++bmps;return true;};
        const auto frameAt=[](const Bytes& bytes,int index){
            if(index<0||unsigned(index)>=little(bytes,6,2))throw std::runtime_error("original SHP frame range");
            const std::size_t header=8+std::size_t(index)*24;
            if(header+24>bytes.size())throw std::runtime_error("truncated original SHP frame table");
            if(little(bytes,header+8,4)!=2){Bytes view=bytes;std::copy_n(bytes.begin()+header,24,view.begin()+8);return decodeFrameZero(view);}
            // Original CTFNCB frames0/32/48 use format2: a little-endian
            // row byte count including its WORD, followed by raw palette
            // indices. Zero is a transparent pixel, never an RLE run marker.
            OriginalFrame frame;frame.canvasWidth=little(bytes,2,2);frame.canvasHeight=little(bytes,4,2);
            frame.x=little(bytes,header,2);frame.y=little(bytes,header+2,2);
            frame.width=little(bytes,header+4,2);frame.height=little(bytes,header+6,2);
            if(frame.canvasWidth<=0||frame.canvasHeight<=0||frame.width<=0||frame.height<=0||frame.width>4096||frame.height>4096)throw std::runtime_error("invalid original format2 SHP dimensions");
            frame.pixels.assign(std::size_t(frame.width)*frame.height,0);
            std::size_t cursor=little(bytes,header+20,4);
            for(int y=0;y<frame.height;++y){
                const std::size_t length=little(bytes,cursor,2);
                if(length<2||cursor>bytes.size()||length>bytes.size()-cursor)throw std::runtime_error("truncated original format2 SHP row");
                const std::size_t count=std::min(std::size_t(frame.width),length-2);
                std::copy_n(bytes.begin()+cursor+2,count,frame.pixels.begin()+std::size_t(y)*frame.width);
                cursor+=length;
            }
            return frame;
        };
        const auto originalFence0=frameAt(walls[3].bytes,0);
        if(little(walls[3].bytes,16,4)!=2||originalFence0.canvasWidth!=68||originalFence0.canvasHeight!=66||originalFence0.x!=33||originalFence0.y!=37||originalFence0.width!=2||originalFence0.height!=11
            ||sha256(originalFence0.pixels)!="467a1a910486a18f50428deb4d6547530c9b557d02710051f63928eb6db1f859")
            return fail("original CAFNCB format2 frame0 independent raw indices");
        sources<<"Original CTFNCB.SHP format2 frames0/32/48 (other61 format3); frame0 canvas68x66 crop33,37 2x11 offset1544, eleven length4 raw rows, opaque22 indicesSHA="<<sha256(originalFence0.pixels)<<". Local test decoder reads raw rows independently; no production decoder or synthesized pixels.\n";sources.flush();
        struct InputWindow{HWND hwnd=nullptr;LONG_PTR proc=0,data=0;InputWindow(RuntimeWindowState& s){hwnd=CreateWindowExW(0,L"STATIC",L"Issue25 real wall input",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(hwnd){proc=GetWindowLongPtrW(hwnd,GWLP_WNDPROC);data=GetWindowLongPtrW(hwnd,GWLP_USERDATA);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(runtimeWindowProc));}}~InputWindow(){if(hwnd){SetWindowLongPtrW(hwnd,GWLP_WNDPROC,proc);SetWindowLongPtrW(hwnd,GWLP_USERDATA,data);if(GetCapture()==hwnd)ReleaseCapture();DestroyWindow(hwnd);}}};
        const auto click=[&](Scene& f,RuntimeWindowState& s,int source,RuntimeCellPoint cell,bool force,const char* replica,bool blank=false){applySelection(s,{source},false,false);InputWindow window(s);if(!window.hwnd)return fail("real hidden fixture HWND");RECT client{};GetClientRect(window.hwnd,&client);s.camera.tileWidth=60;s.camera.tileHeight=30;centerRuntimeCameraOnCell(s,client,cell);const RECT viewport=runtimeMapViewport(client);const auto* terrain=findRuntimeTerrainCellAt(s,cell);const auto target=f.expected.find(runtimeCellKey(cell.x,cell.y));if(!terrain||target==f.expected.end())return fail("raw authored wall click input");const auto rawFrame=frameAt(target->second.rule->bytes,terrain->overlayFrame);
            std::vector<std::uint32_t> pixels;const auto stats=drawRuntimeStateOffscreenAtCell(s,cell,1000,700,error,"",false,false,-1,&pixels,true,&s.camera);if(!stats||pixels.size()!=700000)return fail("real current wall surface");bool originalSource=false;for(const auto& [key,value]:s.renderCache.auxiliaryShpBytesByImage)if(value.loaded&&sha256(value.bytes)==target->second.rule->hash)originalSource=true;if(!originalSource)return fail("actual wall renderer selected different original resource bytes");const auto center=runtimeScreenPoint(runtimeProjectCellXY(cell.x,cell.y,0,s.camera),viewport,s.camera);const int left=center.x-rawFrame.canvasWidth/2+rawFrame.x,top=center.y-rawFrame.canvasHeight/2+rawFrame.y;POINT chosen{};int chosenIndex=-1;long long score=std::numeric_limits<long long>::max();
            for(int y=0;y<rawFrame.height;++y)for(int x=0;x<rawFrame.width;++x){const int index=rawFrame.pixels[std::size_t(y)*rawFrame.width+x];if(blank?index!=0:(index==0||(index>=16&&index<=31)))continue;const POINT p{left+x,top+y};if(!PtInRect(&viewport,p))continue;const auto ground=approximateCellFromScreenPoint(s,viewport,p);if(ground.x!=cell.x||ground.y!=cell.y||runtimeCursorObjectAtPoint(s,viewport,p))continue;
                const std::uint32_t rgb=(std::uint32_t(pal[index*3]*4)<<16)|(std::uint32_t(pal[index*3+1]*4)<<8)|std::uint32_t(pal[index*3+2]*4);if(!blank&&(pixels[std::size_t(p.y)*1000+p.x]&0xffffffU)!=rgb)continue;const long long dx=p.x-center.x,dy=p.y-center.y,d=dx*dx+dy*dy;if(d<score){score=d;chosen=p;chosenIndex=index;}}
            if(chosenIndex<0)return fail(blank?"independent raw transparent crop point missing":"independent original opaque/current-palette/surface-cell point missing");const auto* before=findObjectById(s,source);if(!before)return fail("real selected source");const double x=runtimeObjectRenderX(*before),y=runtimeObjectRenderY(*before);s.lastCommand={};const WPARAM modifier=force?MK_CONTROL:0;const LPARAM position=MAKELPARAM(chosen.x,chosen.y);SendMessageW(window.hwnd,WM_MOUSEMOVE,modifier,position);const auto cursor=runtimeMouseCursorFrameForPoint(s,client,chosen);SendMessageW(window.hwnd,WM_LBUTTONDOWN,modifier|MK_LBUTTON,position);SendMessageW(window.hwnd,WM_LBUTTONUP,modifier,position);const auto b=s.behaviors.find(source);const auto* after=findObjectById(s,source);
            const bool attack=b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Attack&&b->second.attackGround&&b->second.attackBridgeCell.x==cell.x&&b->second.attackBridgeCell.y==cell.y;const bool good=after&&runtimeObjectRenderX(*after)==x&&runtimeObjectRenderY(*after)==y&&(blank?!attack:(s.lastCommand.valid&&s.lastCommand.acceptedCount==1&&s.lastCommand.rejectedCount==0&&attack&&s.selectedObjectIds==std::vector<int>{source}));
            commands<<q(label)<<','<<q(stage)<<','<<replica<<','<<s.tick<<','<<source<<','<<cell.x<<','<<cell.y<<','<<force<<','<<chosenIndex<<','<<chosen.x<<','<<chosen.y<<','<<(cursor?cursor->frameIndex:-1)<<','<<s.lastCommand.valid<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<q(s.lastCommand.verb)<<','<<(b==s.behaviors.end()?-1:int(b->second.kind))<<','<<(b==s.behaviors.end()?-1:b->second.targetObjectId)<<','<<(b!=s.behaviors.end()&&b->second.attackGround)<<','<<(b==s.behaviors.end()?-1:b->second.attackBridgeCell.x)<<','<<(b==s.behaviors.end()?-1:b->second.attackBridgeCell.y)<<','<<good<<'\n';commands.flush();return good||fail("actual visible wall/blank UI input relation or same-tick body changed");};
        const auto impactAttach=[&](Scene& f,RuntimeWindowState& s,const char* replica){s.runtimeProjectileImpactObserver=[&,state=&s,replica](const RuntimeProjectileEffect& p,bool after){if(!p.spawnedMissileDamage&&p.weaponId!="105mm"&&p.weaponId!="V3Airburst"&&p.weaponId!="V3Cluster")return;auto& old=f.hpBefore[p.actorId];if(!after){for(const auto& [key,w]:f.expected){const auto* c=findRuntimeTerrainCellAt(*state,w.cell);old[key]=c?c->overlayWall.health:-999;}return;}
                if(std::string(replica)=="live"){++f.liveImpacts;const std::string expectedWh=f.rank>=2?f.stock.eliteWh:f.stock.wh;if(p.spawnedMissileDamage&&(p.weaponId!=f.stock.launcher||p.projectileImageId!=f.stock.child||p.warheadId!=expectedWh||p.spawnedMissileBaseDamage!=(f.rank>=2?f.stock.eliteDamage:f.stock.damage)||p.veteranDamageMultiplier!=(f.rank>=2?1.1:1.0)))f.problem="original terminal payload/rank/warhead identity";}
                const auto verses=splitRuntimeTokenList(raw.getValue(p.warheadId,"Verses"));const double spread=num(p.warheadId,"CellSpread",0),atMax=num(p.warheadId,"PercentAtMax",1);const long long base=p.spawnedMissileDamage?p.spawnedMissileBaseDamage:std::llround(num(p.weaponId,"Damage"));
                for(auto& [key,w]:f.expected){const auto* c=findRuntimeTerrainCellAt(*state,w.cell);const int was=old.contains(key)?old.at(key):-999,now=c?c->overlayWall.health:-999;const double distance=std::hypot(double(p.toCell.x-w.cell.x),double(p.toCell.y-w.cell.y));const int armor=std::string(w.rule->armor)=="wood"?6:8;const bool reachable=p.ag&&(yes(p.warheadId,"Wall")||(std::string(w.rule->armor)=="wood"&&yes(p.warheadId,"Wood")))&&(spread>0?distance<=spread:distance==0)&&verses.size()==11;
                    const double falloff=spread>0?1-(1-std::clamp(atMax,0.0,1.0))*std::clamp(distance/std::max(.1,spread),0.0,1.0):1;const int damage=reachable&&was>0?std::max(0,int(std::lround(base*p.veteranDamageMultiplier*std::stod(verses[armor])/100.0*falloff))):0;const int expectedDelta=std::min(std::max(0,was),damage);const bool good=was>=0&&now==was-expectedDelta;
                    impacts<<q(label)<<','<<q(stage)<<','<<replica<<','<<state->tick<<','<<p.actorId<<','<<p.attackerObjectId<<','<<p.weaponId<<','<<p.warheadId<<','<<p.spawnedMissileDamage<<','<<base<<','<<p.veteranDamageMultiplier<<','<<p.targetObjectId<<','<<p.toCell.x<<','<<p.toCell.y<<','<<w.cell.x<<','<<w.cell.y<<','<<was<<','<<now<<','<<distance<<','<<damage<<','<<expectedDelta<<','<<was-now<<','<<good<<'\n';if(!good)f.problem="original independent wall impact HP/falloff";if(std::string(replica)=="live")w.hp=was-expectedDelta;}
                impacts.flush();old.clear();};};
    const auto objectFields=[&](const WorldObjectInstance& o){std::map<std::string,std::string> fields;
        const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
        const auto list=[](const auto& values){std::ostringstream a;for(const auto& v:values)a<<v<<',';return a.str();};
#define ISSUE25_HASH_OBJECT_FIELD(name) put(#name,o.name)
        ISSUE25_HASH_OBJECT_FIELD(instanceId);put("kind",int(o.kind));ISSUE25_HASH_OBJECT_FIELD(mapId);ISSUE25_HASH_OBJECT_FIELD(owner);ISSUE25_HASH_OBJECT_FIELD(type);
        ISSUE25_HASH_OBJECT_FIELD(health);ISSUE25_HASH_OBJECT_FIELD(x);ISSUE25_HASH_OBJECT_FIELD(y);ISSUE25_HASH_OBJECT_FIELD(cell);ISSUE25_HASH_OBJECT_FIELD(level);
        ISSUE25_HASH_OBJECT_FIELD(facing);ISSUE25_HASH_OBJECT_FIELD(turretFacing);ISSUE25_HASH_OBJECT_FIELD(subCell);ISSUE25_HASH_OBJECT_FIELD(mission);ISSUE25_HASH_OBJECT_FIELD(tag);
        if(o.bodyFacingClock.active()||o.turretFacingClock.active()){put("bodyFacingClock",runtimeSerializeFacingClock(o.bodyFacingClock));put("turretFacingClock",runtimeSerializeFacingClock(o.turretFacingClock));}
        ISSUE25_HASH_OBJECT_FIELD(veterancy);ISSUE25_HASH_OBJECT_FIELD(veterancyExperience);ISSUE25_HASH_OBJECT_FIELD(crateArmorMultiplier);ISSUE25_HASH_OBJECT_FIELD(crateFirepowerMultiplier);
        ISSUE25_HASH_OBJECT_FIELD(crateSpeedMultiplier);ISSUE25_HASH_OBJECT_FIELD(crateCloakable);if(o.modExtensions.initialized||!o.modExtensions.effects.empty())put("modExtensions",runtimeSerializeModExtensions(o.modExtensions));
        ISSUE25_HASH_OBJECT_FIELD(onBridge);ISSUE25_HASH_OBJECT_FIELD(authoredAircraft);ISSUE25_HASH_OBJECT_FIELD(alive);ISSUE25_HASH_OBJECT_FIELD(rubble);ISSUE25_HASH_OBJECT_FIELD(submerged);
        ISSUE25_HASH_OBJECT_FIELD(cloaked);ISSUE25_HASH_OBJECT_FIELD(cloakCooldownTicks);ISSUE25_HASH_OBJECT_FIELD(submergeCooldownTicks);ISSUE25_HASH_OBJECT_FIELD(deployed);
        ISSUE25_HASH_OBJECT_FIELD(deployStateTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(prone);ISSUE25_HASH_OBJECT_FIELD(proneTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(weaponCooldownTicksRemaining);
        ISSUE25_HASH_OBJECT_FIELD(radiationEruptionCooldownTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(hasRenderPosition);ISSUE25_HASH_OBJECT_FIELD(renderX);ISSUE25_HASH_OBJECT_FIELD(renderY);
        ISSUE25_HASH_OBJECT_FIELD(renderMoveActive);ISSUE25_HASH_OBJECT_FIELD(renderLevel);ISSUE25_HASH_OBJECT_FIELD(renderOnBridge);ISSUE25_HASH_OBJECT_FIELD(buildAnimTicksTotal);
        ISSUE25_HASH_OBJECT_FIELD(buildAnimTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(poweredOperational);ISSUE25_HASH_OBJECT_FIELD(factoryExitAnimationStartTick);
        ISSUE25_HASH_OBJECT_FIELD(factoryExitAnimationEndTick);ISSUE25_HASH_OBJECT_FIELD(factoryExitAirborne);ISSUE25_HASH_OBJECT_FIELD(factoryExitHostId);
        ISSUE25_HASH_OBJECT_FIELD(specialCashNextTick);ISSUE25_HASH_OBJECT_FIELD(specialCloneDeliveryObjectId);put("transportPassengerIds",list(o.transportPassengerIds));
        ISSUE25_HASH_OBJECT_FIELD(bioReactorAnimationStartTick);ISSUE25_HASH_OBJECT_FIELD(bioReactorAnimationPoweredStartTick);ISSUE25_HASH_OBJECT_FIELD(activeGunnerMode);
        ISSUE25_HASH_OBJECT_FIELD(activeGunnerWeaponIndex);ISSUE25_HASH_OBJECT_FIELD(activeGunnerTurretIndex);ISSUE25_HASH_OBJECT_FIELD(tankBunkerHostId);ISSUE25_HASH_OBJECT_FIELD(tankBunkerOccupantId);
        ISSUE25_HASH_OBJECT_FIELD(tankBunkerAnimationStartTick);ISSUE25_HASH_OBJECT_FIELD(tankBunkerLowering);ISSUE25_HASH_OBJECT_FIELD(dockedToObjectId);ISSUE25_HASH_OBJECT_FIELD(dockIndex);
        ISSUE25_HASH_OBJECT_FIELD(airborne);ISSUE25_HASH_OBJECT_FIELD(flightLevel);ISSUE25_HASH_OBJECT_FIELD(garrisonHostObjectId);put("garrisonOccupantIds",list(o.garrisonOccupantIds));
        ISSUE25_HASH_OBJECT_FIELD(garrisonFireCursor);ISSUE25_HASH_OBJECT_FIELD(currentAmmo);ISSUE25_HASH_OBJECT_FIELD(harvestAutoSearchSuspended);ISSUE25_HASH_OBJECT_FIELD(hasBeenCaptured);
#undef ISSUE25_HASH_OBJECT_FIELD
        return fields;};
    const auto terrainFields=[](const TerrainCell& o){std::map<std::string,std::string> fields;const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
#define ISSUE25_HASH_TERRAIN_FIELD(name) put(#name,o.name)
        ISSUE25_HASH_TERRAIN_FIELD(x);ISSUE25_HASH_TERRAIN_FIELD(y);ISSUE25_HASH_TERRAIN_FIELD(cellIndex512);ISSUE25_HASH_TERRAIN_FIELD(tileIndex);ISSUE25_HASH_TERRAIN_FIELD(subTileIndex);
        ISSUE25_HASH_TERRAIN_FIELD(level);ISSUE25_HASH_TERRAIN_FIELD(iceGrowth);ISSUE25_HASH_TERRAIN_FIELD(hasOverlay);ISSUE25_HASH_TERRAIN_FIELD(overlayIndex);ISSUE25_HASH_TERRAIN_FIELD(overlayFrame);
        ISSUE25_HASH_TERRAIN_FIELD(overlayType);ISSUE25_HASH_TERRAIN_FIELD(overlayImage);ISSUE25_HASH_TERRAIN_FIELD(overlayIsOre);ISSUE25_HASH_TERRAIN_FIELD(overlayIsGem);
        put("wallOriginalIndex",o.overlayWall.originalIndex);put("wallOriginalFrame",o.overlayWall.originalFrame);put("wallOriginalType",o.overlayWall.originalType);put("wallOriginalImage",o.overlayWall.originalImage);put("wallHealth",o.overlayWall.health);
#undef ISSUE25_HASH_TERRAIN_FIELD
        return fields;};
    const auto compareDump=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,const std::string& replica,const std::string& phase){
        const auto prefix=label+"-"+phase+"-"+replica;std::string e;
        for(const auto& v:std::array<std::pair<const char*,const RuntimeWindowState*>,2>{{{"live",&a},{"peer",&b}}}){std::ostringstream text;
            if(writeRuntimeStateToStream(*v.second,text,e)){std::ofstream file(out/(prefix+"-"+v.first+"-state14.txt"),std::ios::binary);file<<text.str();}}
        std::ofstream diff(out/(prefix+"-field-diff.csv"));diff<<"kind,id,field,live,peer\n";
        const auto diffFields=[&](const char* kind,const auto& id,const auto& left,const auto& right){std::set<std::string> keys;for(const auto& [k,v]:left)keys.insert(k);for(const auto& [k,v]:right)keys.insert(k);
            for(const auto& k:keys){const auto l=left.find(k),r=right.find(k);const std::string lv=l==left.end()?"<missing>":l->second,rv=r==right.end()?"<missing>":r->second;
                if(lv!=rv)diff<<kind<<','<<id<<','<<q(k)<<','<<q(lv)<<','<<q(rv)<<'\n';}};
        std::map<int,std::map<std::string,std::string>> ao,bo,at,bt;
        for(const auto& o:a.world.getObjects())ao[o.instanceId]=objectFields(o);for(const auto& o:b.world.getObjects())bo[o.instanceId]=objectFields(o);
        for(const auto& o:a.world.getTerrainCells())at[runtimeCellKey(o.x,o.y)]=terrainFields(o);for(const auto& o:b.world.getTerrainCells())bt[runtimeCellKey(o.x,o.y)]=terrainFields(o);
        std::set<int> ids;for(const auto& [id,v]:ao)ids.insert(id);for(const auto& [id,v]:bo)ids.insert(id);for(int id:ids)diffFields("object",id,ao[id],bo[id]);
        ids.clear();for(const auto& [id,v]:at)ids.insert(id);for(const auto& [id,v]:bt)ids.insert(id);for(int id:ids)diffFields("terrain",id,at[id],bt[id]);
        const auto terrainObjectFields=[](const WorldTerrainObject& o){std::map<std::string,std::string> fields;const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
#define ISSUE25_DIFF_TERRAIN_OBJECT(name) put(#name,o.name)
            ISSUE25_DIFF_TERRAIN_OBJECT(mapId);ISSUE25_DIFF_TERRAIN_OBJECT(type);ISSUE25_DIFF_TERRAIN_OBJECT(tag);ISSUE25_DIFF_TERRAIN_OBJECT(x);ISSUE25_DIFF_TERRAIN_OBJECT(y);ISSUE25_DIFF_TERRAIN_OBJECT(cell);ISSUE25_DIFF_TERRAIN_OBJECT(level);
            ISSUE25_DIFF_TERRAIN_OBJECT(strength);ISSUE25_DIFF_TERRAIN_OBJECT(health);ISSUE25_DIFF_TERRAIN_OBJECT(immune);ISSUE25_DIFF_TERRAIN_OBJECT(alive);ISSUE25_DIFF_TERRAIN_OBJECT(occupationBits);ISSUE25_DIFF_TERRAIN_OBJECT(footprintWidth);ISSUE25_DIFF_TERRAIN_OBJECT(footprintHeight);
#undef ISSUE25_DIFF_TERRAIN_OBJECT
            auto slots=o.occupiedSubCells;std::sort(slots.begin(),slots.end());std::ostringstream slotText;for(int n:slots)slotText<<n<<',';fields["occupiedSubCells"]=slotText.str();return fields;};
        const auto smudgeFields=[](const WorldSmudge& o){std::map<std::string,std::string> fields;const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
#define ISSUE25_DIFF_SMUDGE(name) put(#name,o.name)
            ISSUE25_DIFF_SMUDGE(mapId);ISSUE25_DIFF_SMUDGE(type);ISSUE25_DIFF_SMUDGE(x);ISSUE25_DIFF_SMUDGE(y);ISSUE25_DIFF_SMUDGE(cell);ISSUE25_DIFF_SMUDGE(level);ISSUE25_DIFF_SMUDGE(data);ISSUE25_DIFF_SMUDGE(authored);
#undef ISSUE25_DIFF_SMUDGE
            return fields;};
        const auto waypointFields=[](const WorldWaypoint& o){return std::map<std::string,std::string>{{"id",o.id},{"x",std::to_string(o.x)},{"y",std::to_string(o.y)},{"cell",std::to_string(o.cell)}};};
        const auto compareCollection=[&](const char* kind,const auto& left,const auto& right,const auto& extractor){std::map<std::string,std::map<std::string,std::string>> lm,rm;std::map<std::string,int> leftCounts,rightCounts;
            for(const auto& value:left){const auto fields=extractor(value);const std::string id=fields.contains("mapId")?fields.at("mapId"):fields.at("id");lm[id+"#"+std::to_string(leftCounts[id]++)]=fields;}
            for(const auto& value:right){const auto fields=extractor(value);const std::string id=fields.contains("mapId")?fields.at("mapId"):fields.at("id");rm[id+"#"+std::to_string(rightCounts[id]++)]=fields;}
            std::set<std::string> keys;for(const auto& [id,v]:lm)keys.insert(id);for(const auto& [id,v]:rm)keys.insert(id);for(const auto& id:keys)diffFields(kind,id,lm[id],rm[id]);};
        compareCollection("terrainObject",a.world.getTerrainObjects(),b.world.getTerrainObjects(),terrainObjectFields);
        compareCollection("smudge",a.world.getSmudges(),b.world.getSmudges(),smudgeFields);
        compareCollection("waypoint",a.world.getWaypoints(),b.world.getWaypoints(),waypointFields);
        if(a.playerCommands.serialize()!=b.playerCommands.serialize())diff<<"playerCommands,-1,fullSerializer,"<<q(a.playerCommands.serialize())<<','<<q(b.playerCommands.serialize())<<'\n';
        for(std::size_t i=0;i<std::max(a.projectiles.size(),b.projectiles.size());++i){const std::string l=i<a.projectiles.size()?serializeRuntimeProjectileEffect(a.projectiles[i]):"<missing>",r=i<b.projectiles.size()?serializeRuntimeProjectileEffect(b.projectiles[i]):"<missing>";
            if(l!=r)diff<<"projectile,"<<i<<",fullSerializer,"<<q(l)<<','<<q(r)<<'\n';}
        ids.clear();for(const auto& [id,v]:a.behaviors)ids.insert(id);for(const auto& [id,v]:b.behaviors)ids.insert(id);
        for(int id:ids){const auto l=a.behaviors.find(id),r=b.behaviors.find(id);const std::string lv=l==a.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,l->second),rv=r==b.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,r->second);
            if(lv!=rv)diff<<"behavior,"<<id<<",fullSerializer,"<<q(lv)<<','<<q(rv)<<'\n';}
        diff<<"summary,-1,worldHash,"<<a.world.deterministicSimulationStateHash()<<','<<b.world.deterministicSimulationStateHash()<<'\n'
            <<"summary,-1,specialHash,"<<a.specialMechanisms.stateHash()<<','<<b.specialMechanisms.stateHash()<<'\n';};
        const auto recordDisplayDifference=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical,std::size_t index){
            const std::string key=label+"|peer"+std::to_string(index);
            if(recordedDisplayDifferences.contains(key)||a.projectiles.size()!=b.projectiles.size())return;
            for(std::size_t i=0;i<a.projectiles.size();++i)if(projectileAuthorityRecord(a.projectiles[i])!=projectileAuthorityRecord(b.projectiles[i]))return;
            bool observed=false;
            for(std::size_t i=0;i<a.projectiles.size();++i){const auto original=serializeRuntimeProjectileEffect(a.projectiles[i]),restored=serializeRuntimeProjectileEffect(b.projectiles[i]);if(original==restored)continue;
                displayChecks<<q(label)<<','<<q(stage)<<','<<index<<','<<(canonical?"canonical":"ordinary14")<<','<<a.tick<<','<<a.projectiles[i].actorId<<",0,1,"<<q(original)<<','<<q(restored)<<'\n';observed=true;}
            if(observed){displayChecks.flush();recordedDisplayDifferences.insert(key);}
        };
        const auto checkpoint=[&](Scene& f,bool canonical){auto& s=*f.s;std::ostringstream ordinary,extended,normalized;IniFile base,exact;if(!writeRuntimeStateToStream(s,ordinary,error)||!base.loadFromString(ordinary.str())||base.getValue("RuntimeSave","Version")!="14")return std::unique_ptr<RuntimeWindowState>{};
            // Canonical normalization deliberately removes non-authoritative AV
            // sections while retaining Version14. Its extension must be applied
            // over a separately complete, physically reopened ordinary14 base.
            const std::string basePath=pathString(out/(label+"-"+stage+"-ordinary14.ini"));{std::ofstream file(basePath,std::ios::binary);file<<ordinary.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
            physicalUniquePaths.insert(basePath);++physicalFileReopens;if(!base.loadFromFile(basePath))return std::unique_ptr<RuntimeWindowState>{};std::string path=basePath;
            if(canonical){extended<<ordinary.str();if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!exact.loadFromString(extended.str())||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return std::unique_ptr<RuntimeWindowState>{};
                path=pathString(out/(label+"-"+stage+"-canonical.ini"));{std::ofstream file(path,std::ios::binary);file<<normalized.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}physicalUniquePaths.insert(path);++physicalFileReopens;if(!exact.loadFromFile(path))return std::unique_ptr<RuntimeWindowState>{};}
            auto peer=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*peer,s,false);{RuntimeSnapshotActiveStateScope active(*peer);if(!materializeRuntimeLoadStateFromIni(*peer,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*peer,exact,error)))return std::unique_ptr<RuntimeWindowState>{};}peer->runtimeSoundsEnabled=peer->runtimePreferencePersistenceEnabled=peer->visionEnabled=false;initializeRuntimeOfflineGameplayRouting(*peer);
            // Always publish typed current-frame/identity/HP evidence, even
            // when the initial raw world hash already differs.
            compareDump(s,*peer,canonical?"canonical":"ordinary14",stage+"-checkpoint-initial");
            recordDisplayDifference(s,*peer,canonical,f.peers.size()+(canonical?1U:0U));
            const bool equal=fullEqual(s,*peer,canonical),wallEqual=verify(f,*peer,canonical?"canonical":"ordinary14");const bool good=equal&&wallEqual;
            checks<<q(label)<<','<<q(stage)<<','<<(canonical?"canonical":"ordinary14")<<','<<q(path)<<','<<s.tick<<','<<s.world.deterministicSimulationStateHash()<<','<<peer->world.deterministicSimulationStateHash()<<','<<s.specialMechanisms.stateHash()<<','<<peer->specialMechanisms.stateHash()<<','<<equal<<','<<wallEqual<<','<<q(basePath)<<','<<good<<'\n';checks.flush();if(!good){fail("physical full state read changed authority");return std::unique_ptr<RuntimeWindowState>{};}canonical?++canonicalReads:++ordinaryReads;return peer;};
        const auto savePair=[&](Scene& f){auto a=checkpoint(f,false);const std::string ordinaryError=a?"":error;auto b=checkpoint(f,true);const std::string canonicalError=b?"":error;if(!a||!b)return fail("full-file reads ordinary14="+(a?std::string("exact"):ordinaryError)+"; canonical="+(b?std::string("exact"):canonicalError));f.peers.push_back(std::move(a));f.peers.push_back(std::move(b));for(std::size_t i=0;i<f.peers.size();++i)impactAttach(f,*f.peers[i],i%2?"canonical":"ordinary14");return true;};
        const auto dumpReplayFailure=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical,std::size_t index,bool wallEqual){
            const bool tickEqual=a.tick==b.tick,worldEqual=a.world.deterministicSimulationStateHash()==b.world.deterministicSimulationStateHash(),specialEqual=a.specialMechanisms.stateHash()==b.specialMechanisms.stateHash();
            bool projectileEqual=a.projectiles.size()==b.projectiles.size(),rawProjectileEqual=projectileEqual,persistentEqual=a.behaviors.size()==b.behaviors.size(),canonicalEqual=persistentEqual;
            for(std::size_t i=0;i<std::min(a.projectiles.size(),b.projectiles.size());++i){projectileEqual=projectileEqual&&projectileAuthorityRecord(a.projectiles[i])==projectileAuthorityRecord(b.projectiles[i]);rawProjectileEqual=rawProjectileEqual&&serializeRuntimeProjectileEffect(a.projectiles[i])==serializeRuntimeProjectileEffect(b.projectiles[i]);}
            for(const auto& [id,value]:a.behaviors){const auto found=b.behaviors.find(id);if(found==b.behaviors.end()){persistentEqual=canonicalEqual=false;continue;}persistentEqual=persistentEqual&&ordinaryBehavior(value)==ordinaryBehavior(found->second);canonicalEqual=canonicalEqual&&runtimeMultiplayerSnapshotBehaviorRecord(id,value)==runtimeMultiplayerSnapshotBehaviorRecord(id,found->second);}
            const bool commandsEqual=a.playerCommands.serialize()==b.playerCommands.serialize(),requiredBehaviorEqual=persistentEqual&&(!canonical||canonicalEqual);
            const bool allEqual=tickEqual&&worldEqual&&specialEqual&&projectileEqual&&requiredBehaviorEqual&&commandsEqual&&wallEqual;
            const std::string replica=std::string(canonical?"canonical":"ordinary14")+"-peer"+std::to_string(index);
            compareDump(a,b,replica,stage+"-replay-tick"+std::to_string(a.tick));
            replayChecks<<q(label)<<','<<q(stage)<<','<<index<<','<<(canonical?"canonical":"ordinary14")<<','<<a.tick<<','<<b.tick<<','<<a.world.deterministicSimulationStateHash()<<','<<b.world.deterministicSimulationStateHash()<<','<<a.specialMechanisms.stateHash()<<','<<b.specialMechanisms.stateHash()<<','<<tickEqual<<','<<worldEqual<<','<<specialEqual<<','<<projectileEqual<<','<<rawProjectileEqual<<','<<persistentEqual<<','<<canonicalEqual<<','<<commandsEqual<<','<<wallEqual<<','<<requiredBehaviorEqual<<','<<allEqual<<'\n';replayChecks.flush();
        };
        const auto step=[&](Scene& f){stepOne(*f.s);if(!f.problem.empty())return fail(f.problem);if(!verify(f,*f.s,"live"))return false;for(std::size_t i=0;i<f.peers.size();++i){stepOne(*f.peers[i]);++pairedTicks;if(!f.problem.empty())return fail(f.problem);const bool wallEqual=verify(f,*f.peers[i],i%2?"canonical":"ordinary14"),authorityEqual=fullEqual(*f.s,*f.peers[i],i%2!=0);if(!wallEqual||!authorityEqual){dumpReplayFailure(*f.s,*f.peers[i],i%2!=0,i,wallEqual);return fail("complete paired world/special/projectile/behavior replay diverged tick="+std::to_string(f.s->tick)+" peer="+std::to_string(i)+(i%2?" canonical":" ordinary14"));}recordDisplayDifference(*f.s,*f.peers[i],i%2!=0,i);}for(const auto& [id,m]:f.s->specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent){f.shotIds.insert(id);if(m.typeId!=f.stock.child||m.weaponId!=f.stock.launcher||m.warheadId!=(f.rank>=2?f.stock.eliteWh:f.stock.wh)||m.damage!=(f.rank>=2?f.stock.eliteDamage:f.stock.damage)||m.damageMultiplier!=(f.rank>=2?1.1:1.0)||m.releaseTick-m.startTick!=(f.stock.pause+f.stock.tilt)*2)return fail("actual original missile phase/launch clock/payload");}return (findObjectById(*f.s,f.parent)&&findObjectById(*f.s,f.parent)->health==f.stock.hp)||fail("unopposed original parent HP changed");};
        const auto commandPair=[&](Scene& f,int source,RuntimeCellPoint target,bool force,bool blank=false){if(!click(f,*f.s,source,target,force,"live",blank))return false;for(std::size_t i=0;i<f.peers.size();++i)if(!click(f,*f.peers[i],source,target,force,i%2?"canonical":"ordinary14",blank))return false;return true;};
        const auto stopPair=[&](Scene& f,int source){applySelection(*f.s,{source},false,false);if(!dispatchRuntimeSelectionCommandFromUi(*f.s,RuntimeMultiplayerGameplayCommandType::Stop)||!f.s->lastCommand.valid||f.s->lastCommand.acceptedCount!=1||f.s->lastCommand.rejectedCount!=0)return fail("real Stop rejected");for(auto& p:f.peers){applySelection(*p,{source},false,false);if(!dispatchRuntimeSelectionCommandFromUi(*p,RuntimeMultiplayerGameplayCommandType::Stop)||p->lastCommand.acceptedCount!=1||p->lastCommand.rejectedCount!=0)return fail("paired real Stop rejected");}return true;};
        const auto finish=[&](Scene& f,const char* group,int firstO,int firstC,int firstP,int firstB){cases<<q(label)<<','<<group<<",1,"<<f.shotIds.size()<<','<<f.liveImpacts<<','<<ordinaryReads-firstO<<','<<canonicalReads-firstC<<','<<pairedTicks-firstP<<','<<bmps-firstB<<'\n';cases.flush();std::cout<<"Overlay "<<label<<" PASS\n";};
        const auto attackToDeath=[&](Scene& f,RuntimeCellPoint target,bool force,bool saveConcrete){stage="real-mouse-Attack";if(!commandPair(f,f.parent,target,force))return false;bool partialRead=false,deadRead=false;for(int n=0;n<4500;++n){if(!step(f))return false;const auto at=f.expected.find(runtimeCellKey(target.x,target.y));if(at==f.expected.end())return fail("expected authored target missing");
                if(saveConcrete&&!partialRead&&at->second.hp>0&&at->second.hp<at->second.rule->hp){stage="actual-partial";if(!capture(f,"partial",target)||!savePair(f))return false;partialRead=true;}
                if(at->second.hp==0){stage="actual-dead";if(!capture(f,"dead",target))return false;if(saveConcrete){if(!partialRead||!savePair(f))return fail("concrete lacks actual positive partial read");deadRead=true;}for(int k=0;k<12;++k)if(!step(f))return false;return (!f.shotIds.empty()&&f.liveImpacts>0&&(!saveConcrete||(partialRead&&deadRead)))||fail("actual terminal shot/impact/read census");}}
            (void)capture(f,"failure-timeout",target);return fail("actual original missile Attack failed to destroy live map wall");};
        if(subset.empty()||subset=="core")for(const auto& stock:stocks)for(int rank:{0,2})for(const auto& wall:walls){label=std::string(stock.parent)+(rank?"-elite-":"-normal-")+wall.id;stage="formal-fixture";const bool force=(std::string(stock.parent)=="DRED")!=(rank==2);auto f=make(stock,rank,{{&wall,{80,64},0,0}});if(!f.s)return fail("original complete fixture "+error);const int firstO=ordinaryReads,firstC=canonicalReads,firstP=pairedTicks,firstB=bmps;if(!verify(f,*f.s,"live")||!capture(f,"before-real-click",{80,64}))return false;impactAttach(f,*f.s,"live");const bool save=std::string(stock.parent)=="V3"&&rank==0&&std::string(wall.id)=="GAWALL";if(!attackToDeath(f,{80,64},force,save))return false;finish(f,"core",firstO,firstC,firstP,firstB);++coreCount;}
        const auto realMove=[&](Scene& f,int source,RuntimeCellPoint target){const auto issue=[&](RuntimeWindowState& s){applySelection(s,{source},false,false);const auto* before=findObjectById(s,source);if(!before)return false;const auto x=runtimeObjectRenderX(*before),y=runtimeObjectRenderY(*before);const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,target,nullptr);const auto* after=findObjectById(s,source);const auto b=s.behaviors.find(source);
                return dispatched&&s.lastCommand.valid&&s.lastCommand.acceptedCount==1&&s.lastCommand.rejectedCount==0&&after&&runtimeObjectRenderX(*after)==x&&runtimeObjectRenderY(*after)==y&&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move;};
            if(!issue(*f.s))return fail("real normal Move rejected or jumped");for(auto& p:f.peers)if(!issue(*p))return fail("paired real normal Move rejected");return true;};
        const auto naturalPreparing=[&](Scene& f){for(int n=0;n<900;++n){if(!step(f))return false;int published=0;for(const auto& [id,m]:f.s->specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent&&!m.released){const auto* o=findObjectById(*f.s,id);if(o&&o->mission=="MissilePreparing"&&o->hasRenderPosition&&o->renderMoveActive&&m.lastTick==f.s->tick&&std::abs(runtimeObjectRenderX(*o)-m.x)<1e-8&&std::abs(runtimeObjectRenderY(*o)-m.y)<1e-8&&std::abs(runtimeObjectRenderLevel(*o)-m.z)<1e-8)++published;}if(published==f.stock.slots)return true;}return fail("actual Preparing bodies never published");};
        const auto injury=[&](Scene& f,RuntimeCellPoint target,bool requirePartial){const int hp=f.expected.at(runtimeCellKey(target.x,target.y)).hp;stage="ordinary-original-105mm";
            if(!commandPair(f,f.tank,target,false))return false;bool observed=false;for(int n=0;n<1000;++n){if(!step(f))return false;const int now=f.expected.at(runtimeCellKey(target.x,target.y)).hp;if(now<hp){observed=true;break;}}
            if(!observed)return fail("ordinary original105mm never caused a real wall impact");if(requirePartial&&f.expected.at(runtimeCellKey(target.x,target.y)).hp<=0)return fail("original ordinary partial fixture was lethal");return stopPair(f,f.tank);};
        if(subset.empty()||subset=="controls"||subset=="highbank"){
            // 1: Authored CAKRMW bank2 is a compatibility input, not native HP.
            // MTNK/AP real damage65 * rawwood65%=42 gives75->33. Neighbor
            // death changes only the low4 topology; each positive extra bank stays2.
            label="CAKRMW-authored-high-bank-real-partial-neighbor-death";stage="untouched-authored-frame37";
            const int firstO=ordinaryReads,firstC=canonicalReads,firstP=pairedTicks,firstB=bmps;
            {
                // Exact first-load authored art, with all four neighboring cells
                // playable Clear and no prior combat/topology refresh. A save
                // cannot silently turn the displayed original37 into32.
                auto fresh=make(stocks[0],0,{{&walls[5],{80,64},37,0}},false);
                if(!fresh.s||!verify(fresh,*fresh.s,"live")||!capture(fresh,"untouched-original37",{80,64})||!savePair(fresh))return fail("untouched original37 physical full-state read "+error);
                for(int n=0;n<12;++n)if(!step(fresh))return false;
            }
            stage="original-frame37-connected-chain";
            auto f=make(stocks[0],0,{{&walls[5],{80,64},37,0},{&walls[5],{80,63},36,0},{&walls[5],{80,65},33,0}});
            if(!f.s)return fail("highbank fixture "+error);
            f.tank=add(*f.s,"MTNK",RuntimeBuildKind::Unit,{76,64});if(f.tank<0||num("105mm","Damage")!=65||num("105mm","Range")!=5||raw.getValue("105mm","Warhead")!="AP"||!yes("AP","Wall")||!yes("AP","Wood"))return fail("original ordinary partial weapon");
            impactAttach(f,*f.s,"live");if(!verify(f,*f.s,"live")||!capture(f,"bank2-before",{80,64})||!injury(f,{80,64},true))return false;
            if(f.expected.at(runtimeCellKey(80,64)).hp!=33)return fail("independent original65/APwood65% expected75->33");
            stage="actual-high-bank-partial";if(!capture(f,"bank2-partial",{80,64})||!savePair(f))return false;
            stage="actual-neighbor-death";if(!commandPair(f,f.tank,{80,63},false))return false;bool neighborDead=false;
            for(int n=0;n<1400;++n){if(!step(f))return false;if(f.expected.at(runtimeCellKey(80,63)).hp==0){neighborDead=true;break;}}
            if(!neighborDead||f.expected.at(runtimeCellKey(80,64)).hp!=33||!stopPair(f,f.tank)||!capture(f,"bank2-neighbor-dead",{80,64}))return fail("real isolated neighbor death altered positive highbank HP");
            stage="real-terminal-high-bank";if(!attackToDeath(f,{80,64},false,false))return false;
            stage="actual-high-bank-dead";if(!savePair(f))return false;for(int n=0;n<15;++n)if(!step(f))return false;
            finish(f,"control-highbank",firstO,firstC,firstP,firstB);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 2: Independent wood ordinary partial, followed by real V3 death.
            label="GASAND-original-ordinary-partial-then-real-payload";stage="healthy-wood";
            auto f=make(stocks[0],0,{{&walls[0],{80,64},0,0}});if(!f.s)return fail("wood fixture "+error);
            const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;f.tank=add(*f.s,"MTNK",RuntimeBuildKind::Unit,{76,64});if(f.tank<0)return fail("original MTNK partial input");
            impactAttach(f,*f.s,"live");if(!injury(f,{80,64},true)||f.expected.at(runtimeCellKey(80,64)).hp!=58)return false;
            // The real105mm hit leaves the original translucent S_CLSN16 over
            // this low sandbag. Wait only for that actual finite animation;
            // keep every real simulation tick and the original pixel click.
            stage="natural-original-105mm-impact-settle";
            std::ofstream settle(out/"overlay-animation-settle.csv");
            if(!settle)return fail("private natural animation output");
            settle<<std::setprecision(17)<<"case,stage,replica,tick,matching,actor,attacker,target,cell_x,cell_y,weapon,warhead,anim,lifecycle,render_intent,frame,remaining,total,sequence_steps,frame_delay,wall_health,wall_frame,status\n";
            if(raw.getValue("AP","AnimList")!="S_CLSN16,S_CLSN22"||toLowerAsciiLocal(art.getValue("S_CLSN16","Translucent"))!="yes"||toLowerAsciiLocal(art.getValue("S_CLSN16","Crater"))!="yes")
                return fail("original AP translucent impact art input");
            const auto matchesImpact=[&](const RuntimeImpactEffect& e){return e.attackerObjectId==f.tank&&e.weaponId=="105mm"&&e.warheadId=="AP"&&e.cell.x==80&&e.cell.y==64&&e.remainingTicks>0;};
            bool invalidImpact=false;
            const auto sampleSettle=[&](const char* status){
                int count=0,remaining=0;const auto* wall=findRuntimeTerrainCellAt(*f.s,{80,64});
                for(const auto& e:f.s->impactEffects){
                    const bool matching=matchesImpact(e);if(matching){++count;remaining=std::max(remaining,e.remainingTicks);
                        if(e.animId!="S_CLSN16"||e.lifecycleState!="Impact"||e.renderIntent!="warheadImpact"||e.totalTicks<e.remainingTicks||e.totalTicks<=0)invalidImpact=true;}
                    settle<<q(label)<<','<<q(stage)<<",live,"<<f.s->tick<<','<<matching<<','<<e.projectileActorId<<','<<e.attackerObjectId<<','<<e.targetObjectId<<','<<e.cell.x<<','<<e.cell.y<<','<<q(e.weaponId)<<','<<q(e.warheadId)<<','<<q(e.animId)<<','<<q(e.lifecycleState)<<','<<q(e.renderIntent)<<','<<e.frameIndex<<','<<e.remainingTicks<<','<<e.totalTicks<<','<<e.animationSequenceSteps<<','<<e.animationFrameDelayTicks<<','<<(wall?wall->overlayWall.health:-999)<<','<<(wall?wall->overlayFrame:-999)<<','<<status<<'\n';
                }
                if(!count)settle<<q(label)<<','<<q(stage)<<",live,"<<f.s->tick<<",0,-1,-1,-1,80,64,\"\",\"\",\"\",\"\",\"\",-1,0,0,0,0,"<<(wall?wall->overlayWall.health:-999)<<','<<(wall?wall->overlayFrame:-999)<<','<<status<<'\n';
                settle.flush();return std::pair<int,int>{count,remaining};
            };
            const auto initialImpact=sampleSettle("initial-actual-effects");
            const int maximumBudget=std::max(1,f.s->config.fixedTickHz)*10;
            if(initialImpact.first<=0||invalidImpact||initialImpact.second<=0||initialImpact.second>maximumBudget)
                return fail("actual original105mm/AP finite impact unavailable or invalid; see overlay-animation-settle.csv");
            int remainingEffects=initialImpact.first;
            for(int n=0;remainingEffects>0&&n<=initialImpact.second;++n){
                if(!step(f))return false;
                const auto* wall=findRuntimeTerrainCellAt(*f.s,{80,64});
                if(!wall||wall->overlayWall.health!=58||wall->overlayFrame!=0||f.expected.at(runtimeCellKey(80,64)).hp!=58)
                    return fail("natural impact lifetime changed partial wall HP/frame");
                remainingEffects=sampleSettle("actual-full-simulation").first;
                if(invalidImpact)return fail("natural original impact identity changed");
            }
            if(remainingEffects)return fail("original impact did not expire within its actual remaining lifetime");
            sources<<"GASAND partial original105mm/AP is allowed to finish only its actual matching finite S_CLSN16 impact through full simulation; its initial/tick/end fields are in overlay-animation-settle.csv. No effects are erased or pixels probed to decide waiting.\n";sources.flush();
            if(!capture(f,"ordinary-partial",{80,64})||!attackToDeath(f,{80,64},false,false))return false;
            finish(f,"control-wood-partial",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 3: Genuine E1 normal Move uses wall obstruction. After original
            // missile death, the same immutable MapFacts cannot resurrect it.
            label="alive-wall-blocks-real-E1-then-dead-ground-is-passable";stage="alive-navigation";
            auto f=make(stocks[0],0,{{&walls[1],{80,64},0,0}});if(!f.s)return fail("navigation fixture "+error);
            const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;f.infantry=add(*f.s,"E1",RuntimeBuildKind::Infantry,{78,64});if(f.infantry<0)return fail("original E1 navigation input");impactAttach(f,*f.s,"live");
            if(!realMove(f,f.infantry,{80,64}))return false;for(int n=0;n<150;++n){if(!step(f))return false;const auto* o=findObjectById(*f.s,f.infantry);if(!o)return fail("real E1 unavailable");
                const double x=runtimeObjectRenderX(*o)+(o->subCell==1?-.25:o->subCell==2?.25:o->subCell==3?-.25:o->subCell==4?.25:0);
                const double y=runtimeObjectRenderY(*o)+(o->subCell==1?-.25:o->subCell==2?-.25:o->subCell==3?.25:o->subCell==4?.25:0);
                if(x>79.5&&x<80.5&&y>63.5&&y<64.5)return fail("real ordinary foot penetrated a live wall");}
            if(!stopPair(f,f.infantry)||!attackToDeath(f,{80,64},false,false))return false;stage="dead-navigation";if(!realMove(f,f.infantry,{80,64}))return false;bool reached=false;
            for(int n=0;n<900;++n){if(!step(f))return false;const auto* o=findObjectById(*f.s,f.infantry);if(o&&o->x==80&&o->y==64&&!o->renderMoveActive&&!f.s->behaviors.contains(f.infantry)){reached=true;break;}}
            if(!reached||!capture(f,"E1-on-freed-raw-TMP",{80,64}))return fail("actual E1 never reached cleared original ground");
            finish(f,"control-navigation",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 4: Transparent raw crop pixels cannot acquire a visible-wall hit.
            label="original-wall-transparent-crop-is-not-opaque-hit";stage="raw-blank";
            auto f=make(stocks[0],0,{{&walls[2],{80,64},0,0}});if(!f.s)return fail("blank fixture "+error);
            const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;impactAttach(f,*f.s,"live");if(!capture(f,"raw-crop-blank",{80,64})||!commandPair(f,f.parent,{80,64},false,true))return false;
            for(int n=0;n<8;++n)if(!step(f))return false;if(!f.shotIds.empty()||f.liveImpacts||f.expected.at(runtimeCellKey(80,64)).hp!=300)return fail("transparent raw wall pixel produced a wall attack");
            if(!stopPair(f,f.parent)||!attackToDeath(f,{80,64},false,false))return false;
            finish(f,"control-blank-and-opaque",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 5: Independently original BUILDNGZ body/wall depth witnesses
            // establish a genuinely occluded wall pixel before issuing input.
            label="original-wall-occluded-by-TESLA-opaque-body";stage="original-scene-Z";
            auto f=make(stocks[0],0,{{&walls[2],{80,64},0,0}});if(!f.s)return fail("hidden fixture "+error);
            const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;const int building=add(*f.s,"TESLA",RuntimeBuildKind::Building,{81,65});if(building<0||raw.getValue("TESLA","Image")!="NATSLA"||art.getValue("NATSLA","Foundation")!="1x1")return fail("original real Tesla source");
            const auto generic=readPinned(asset/"ra2/generic.mix","dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70");
            const Bytes bodyBytes(generic.begin()+3995822,generic.begin()+3995822+9184);if(sha256(bodyBytes)!="5035c5cce5e7a0036b64d451cbe175e012083e3b2ed608e4e70e40a243f707e4")return fail("original NGTSLA bytes");
            const auto body=decodeFrameZero(bodyBytes),shape=decodeFrameZero(retailTemplate(std::filesystem::current_path())),wall=frameAt(walls[2].bytes,0);
            std::vector<std::uint32_t> pixels;if(!capture(f,"original-opaque-occlusion",{80,64},&pixels))return false;
            auto& s=*f.s;InputWindow window(s);if(!window.hwnd)return fail("actual hidden-case HWND");RECT client{};GetClientRect(window.hwnd,&client);const RECT viewport=runtimeMapViewport(client);
            const auto wc=runtimeScreenPoint(runtimeProjectCellXY(80,64,0,s.camera),viewport,s.camera),bc=runtimeScreenPoint(runtimeProjectCellXY(81,65,0,s.camera),viewport,s.camera);
            const int wl=wc.x-wall.canvasWidth/2+wall.x,wt=wc.y-wall.canvasHeight/2+wall.y,bl=bc.x-body.canvasWidth/2+body.x,bt=bc.y-body.canvasHeight/2+body.y;
            const int wallNative=(80+64)*15-wall.canvasHeight/2+wall.y,bodyNative=(81+65)*15-body.canvasHeight/2+body.y,u0=body.x-body.canvasWidth/2+198,v0=body.y-body.canvasHeight/2+446;
            const int clipped=std::min(body.height,shape.height-v0)-std::max(0,-v0);POINT point{};bool witness=false;int wallZ=0,bodyZ=0;std::ofstream hidden(out/"hidden-original-depth.csv");hidden<<"x,y,wall_raw_index,body_raw_index,wall_depth,body_depth,picked_id,passed\n";
            for(int y=0;y<wall.height&&!witness;++y)for(int x=0;x<wall.width&&!witness;++x){const int wi=wall.pixels[std::size_t(y)*wall.width+x];if(!wi)continue;POINT p{wl+x,wt+y};const int bx=p.x-bl,by=p.y-bt,u=u0+bx,v=v0+by;if(bx<0||by<0||bx>=body.width||by>=body.height||u<0||v<0||u>=shape.width||v>=shape.height||!body.pixels[std::size_t(by)*body.width+bx])continue;
                const int base=32768-wallNative-wall.height-1;wallZ=(base/3)*3-(wall.height-1-y)/3;const int rawDepth=shape.pixels[std::size_t(v)*shape.width+u];int transformed=rawDepth?rawDepth-65:0;if(transformed>127)transformed-=256;if(transformed< -128)transformed+=256;bodyZ=32768-bodyNative-clipped-1-transformed;
                const auto* picked=runtimeCursorObjectAtPoint(s,viewport,p);const bool good=bodyZ<=wallZ&&picked&&picked->instanceId==building;
                hidden<<p.x<<','<<p.y<<','<<wi<<','<<int(body.pixels[std::size_t(by)*body.width+bx])<<','<<wallZ<<','<<bodyZ<<','<<(picked?picked->instanceId:-1)<<','<<good<<'\n';if(good){point=p;witness=true;}}
            hidden.flush();if(!witness)return fail("original body/template cannot establish a true opaque hidden-wall witness");
            const auto push=[&](){const LPARAM pos=MAKELPARAM(point.x,point.y);SendMessageW(window.hwnd,WM_MOUSEMOVE,0,pos);SendMessageW(window.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,pos);SendMessageW(window.hwnd,WM_LBUTTONUP,0,pos);};
            applySelection(s,{f.parent},false,false);push();const auto p=s.behaviors.find(f.parent);if(p!=s.behaviors.end()&&p->second.kind==RuntimeBehaviorKind::Attack&&p->second.attackGround)return fail("hidden wall acquired through a real foreground body");
            applySelection(s,{},false,false);push();for(int n=0;n<8;++n)if(!step(f))return false;if(f.expected.at(runtimeCellKey(80,64)).hp!=300||!f.shotIds.empty())return fail("hidden/no-selection input changed map wall");
            finish(f,"control-opaque-occlusion-and-selection",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 6: Stop is observed in a natural fully published preparation.
            label="actual-preparing-Stop-retains-original-wall";stage="natural-Preparing";
            auto f=make(stocks[0],0,{{&walls[1],{80,64},0,0}});if(!f.s)return fail("Stop fixture "+error);const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;impactAttach(f,*f.s,"live");
            if(!commandPair(f,f.parent,{80,64},false)||!naturalPreparing(f)||!capture(f,"actual-preparing",{80,64})||!stopPair(f,f.parent))return false;
            for(int n=0;n<140;++n)if(!step(f))return false;if(f.liveImpacts||f.expected.at(runtimeCellKey(80,64)).hp!=300||std::any_of(f.s->specialMechanisms.spawnedMissiles().begin(),f.s->specialMechanisms.spawnedMissiles().end(),[&](const auto& v){return v.second.parentObjectId==f.parent;}))return fail("Stop left stale preparation or wall damage");
            finish(f,"control-Stop",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 7: A real second UI target replaces only unreleased preparation.
            label="DRED-actual-preparing-retarget-second-map-wall";stage="first-wall-Preparing";
            auto f=make(stocks[1],0,{{&walls[1],{80,64},0,0},{&walls[1],{84,64},0,0}});if(!f.s)return fail("retarget fixture "+error);const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;impactAttach(f,*f.s,"live");
            if(!commandPair(f,f.parent,{80,64},true)||!naturalPreparing(f)||!capture(f,"original-preparation",{80,64}))return false;
            std::set<int> oldIds;for(const auto& [id,m]:f.s->specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent)oldIds.insert(id);stage="actual-retarget";
            if(!attackToDeath(f,{84,64},false,false))return false;for(const auto& [id,m]:f.s->specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent&&(m.targetX!=84||m.targetY!=64))return fail("new released missile retained old wall destination");
            if(f.expected.at(runtimeCellKey(80,64)).hp!=300)return fail("retarget continued damaging canceled old wall");
            finish(f,"control-retarget",a,b,c,d);++controlCount;
        }
        if(subset.empty()||subset=="controls"){
            // 8: Explicit legacy compatibility inputs lack WallOverlays.
            // Legacy absent-section TWO and Schema1 seven-column TWO are complete memory materializations, NOT physical reads.
            // The separate actual partial/dead8 files and untouched2 files preserve both authority and displayed-frame state.
            label="healthy-legacy-no-optional-section-complete-materialize";stage="legacy-memory-input";
            auto f=make(stocks[0],0,{{&walls[1],{80,64},0,0}});if(!f.s)return fail("legacy fixture "+error);const int a=ordinaryReads,b=canonicalReads,c=pairedTicks,d=bmps;
            std::ostringstream original;if(!writeRuntimeStateToStream(*f.s,original,error))return fail("complete legacy base write");const auto strip=[](const std::string& text){std::istringstream in(text);std::ostringstream out;std::string line;bool skip=false;while(std::getline(in,line)){const auto t=trimAsciiLocal(line);if(!t.empty()&&t.front()=='[')skip=toUpperAsciiLocal(t)=="[WALLOVERLAYS]";if(!skip)out<<line<<'\n';}return out.str();};
            const std::string legacy=strip(original.str());for(bool canonical:{false,true}){IniFile base,exact;if(!base.loadFromString(legacy)||base.getSection("WallOverlays"))return fail("legacy optional section still present");
                if(canonical){std::ostringstream extension;extension<<legacy;if(!writeRuntimeMultiplayerSnapshotExtension(*f.s,extension,error)||!exact.loadFromString(strip(extension.str())))return fail("complete legacy canonical input");}
                auto peer=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*peer,*f.s,false);{RuntimeSnapshotActiveStateScope active(*peer);if(!materializeRuntimeLoadStateFromIni(*peer,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*peer,exact,error)))return fail("complete legacy materialization: "+error);}
                initializeRuntimeOfflineGameplayRouting(*peer);if(!verify(f,*peer,canonical?"legacy-canonical-memory":"legacy14-memory")||!fullEqual(*f.s,*peer,canonical))return fail("legacy absent-section input lost healthy identity/state");
                checks<<q(label)<<','<<q(stage)<<','<<(canonical?"canonical-memory":"ordinary14-memory")<<",\"<memory-not-physical>\","<<f.s->tick<<','<<f.s->world.deterministicSimulationStateHash()<<','<<peer->world.deterministicSimulationStateHash()<<','<<f.s->specialMechanisms.stateHash()<<','<<peer->specialMechanisms.stateHash()<<",1,1,"<<q("<memory-not-physical>")<<",1\n";checks.flush();++legacyMemoryLoads;f.peers.push_back(std::move(peer));}
            // Explicit old seven-column schema retains complete ordinary14
            // authority; canonical adds its exact extension. Healthy frame0
            // permits exact hashes under both old and new topology policies.
            IniFile authored;if(!authored.loadFromString(original.str()))return fail("complete original schema input");
            observedWallSchema=std::stoi(authored.getValue("WallOverlays","Schema","-1"));
            const int authoredCount=std::stoi(authored.getValue("WallOverlays","Count","-1"));
            if((observedWallSchema!=1&&observedWallSchema!=2)||authoredCount!=1)return fail("actual writer wall-schema/healthy row census");
            auto fields=splitRuntimeSaveFields(authored.getValue("WallOverlays","0",""));
            if(fields.size()!=(observedWallSchema==1?7U:8U)||fields[5]!="0"||fields[6]!="300"||(observedWallSchema==2&&fields[7]!="0"))return fail("healthy original frame0 schema input");
            const auto wallInput=[&](int schema,const std::vector<std::string>& row){std::ostringstream stream;stream<<strip(original.str())<<"\n[WallOverlays]\nSchema="<<schema<<"\nCount=1\n0=";for(std::size_t i=0;i<row.size();++i){if(i)stream<<'|';stream<<row[i];}stream<<'\n';return stream.str();};
            auto oldFields=fields;oldFields.resize(7);
            const std::string schema1=wallInput(1,oldFields);
            stage="explicit-Schema1-seven-column-memory";
            for(bool canonical:{false,true}){IniFile base,exact;if(!base.loadFromString(schema1))return fail("complete Schema1 ordinary14 input");
                if(canonical){std::ostringstream extension;extension<<schema1;if(!writeRuntimeMultiplayerSnapshotExtension(*f.s,extension,error)||!exact.loadFromString(extension.str()))return fail("complete Schema1 canonical input");}
                auto peer=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*peer,*f.s,false);{RuntimeSnapshotActiveStateScope active(*peer);if(!materializeRuntimeLoadStateFromIni(*peer,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*peer,exact,error)))return fail("complete Schema1 seven-column materialization: "+error);}
                initializeRuntimeOfflineGameplayRouting(*peer);
                const bool equal=fullEqual(*f.s,*peer,canonical),wallEqual=verify(f,*peer,canonical?"schema1-canonical-memory":"schema1-ordinary14-memory");
                schemaChecks<<q(label)<<','<<q(stage)<<','<<(canonical?"canonical-memory":"ordinary14-memory")<<",1,1,1,"<<f.s->world.deterministicSimulationStateHash()<<','<<peer->world.deterministicSimulationStateHash()<<','<<f.s->specialMechanisms.stateHash()<<','<<peer->specialMechanisms.stateHash()<<','<<equal<<','<<wallEqual<<','<<(equal&&wallEqual?"exact":"failed")<<",\"\"\n";schemaChecks.flush();
                checks<<q(label)<<','<<q(stage)<<','<<(canonical?"canonical-schema1-memory":"ordinary14-schema1-memory")<<",\"<memory-not-physical>\","<<f.s->tick<<','<<f.s->world.deterministicSimulationStateHash()<<','<<peer->world.deterministicSimulationStateHash()<<','<<f.s->specialMechanisms.stateHash()<<','<<peer->specialMechanisms.stateHash()<<','<<equal<<','<<wallEqual<<','<<q("<memory-not-physical>")<<','<<(equal&&wallEqual)<<'\n';checks.flush();
                if(!equal||!wallEqual)return fail("complete Schema1 seven-column restore changed raw authority");
                ++schema1MemoryLoads;f.peers.push_back(std::move(peer));
            }
            // Malformed inputs are tested only after the actual writer proves
            // Schema2 is enabled. Each helper receives a separate world clone;
            // no rejected authority replaces the valid scene or replay peers.
            for(int bad=0;bad<3;++bad){const char* name=bad==0?"positive-current-frame256":bad==1?"dead-current-frame-not-minus1":"Schema2-seven-columns";
                stage=std::string("atomic-Schema2-reject-")+name;
                if(observedWallSchema!=2){schemaChecks<<q(label)<<','<<q(stage)<<",restore-helper-memory,2,0,0,0,0,0,0,0,0,pending-schema2,\"not executed: actual writer is Schema1\"\n";schemaChecks.flush();++schema2InvalidPending;continue;}
                auto malformed=fields;
                if(bad==0){malformed[6]="299";malformed[7]="256";}
                else if(bad==1){malformed[6]="0";malformed[7]="0";}
                else {malformed[6]="299";malformed.resize(7);}
                IniFile invalid;if(!invalid.loadFromString(wallInput(2,malformed)))return fail("complete malformed Schema2 input parse");
                auto clone=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*clone,*f.s,false);
                const auto worldBefore=clone->world.deterministicSimulationStateHash(),specialBefore=clone->specialMechanisms.stateHash();
                const auto sourceWorld=f.s->world.deterministicSimulationStateHash(),sourceSpecial=f.s->specialMechanisms.stateHash();
                std::string rejection;bool accepted=false;{RuntimeSnapshotActiveStateScope active(*clone);accepted=restoreRuntimeOverlayWallStates(*clone,invalid,rejection);}
                const auto worldAfter=clone->world.deterministicSimulationStateHash(),specialAfter=clone->specialMechanisms.stateHash();
                const bool unchanged=worldBefore==worldAfter&&specialBefore==specialAfter&&sourceWorld==f.s->world.deterministicSimulationStateHash()&&sourceSpecial==f.s->specialMechanisms.stateHash();
                const bool wallEqual=verify(f,*clone,"invalid-rejected-clone");
                const bool good=!accepted&&!rejection.empty()&&unchanged&&wallEqual;
                schemaChecks<<q(label)<<','<<q(stage)<<",restore-helper-memory,2,1,"<<accepted<<','<<worldBefore<<','<<worldAfter<<','<<specialBefore<<','<<specialAfter<<','<<unchanged<<','<<wallEqual<<','<<(good?"atomic-reject":"failed")<<','<<q(rejection)<<'\n';schemaChecks.flush();
                if(!good)return fail("malformed Schema2 input accepted or changed clone/source authority: "+std::string(name)+" "+rejection);
                ++schema2InvalidRejects;
            }
            stage="legacy-and-Schema1-paired-real-attack";
            impactAttach(f,*f.s,"live");for(std::size_t i=0;i<f.peers.size();++i)impactAttach(f,*f.peers[i],i<2?(i%2?"legacy-canonical-memory":"legacy14-memory"):(i%2?"schema1-canonical-memory":"schema1-ordinary14-memory"));
            if(!attackToDeath(f,{80,64},false,false))return false;finish(f,"control-legacy-memory",a,b,c,d);++controlCount;
        }
        if((subset.empty()||subset=="controls")&&(controlCount!=8||legacyMemoryLoads!=2||schema1MemoryLoads!=2||(observedWallSchema==2?(schema2InvalidRejects!=3||schema2InvalidPending!=0):(observedWallSchema!=1||schema2InvalidRejects!=0||schema2InvalidPending!=3))))return fail("focused original control/complete memory/schema rejection census");
        if(subset=="highbank"&&(controlCount!=1||ordinaryReads!=3||canonicalReads!=3||physicalUniquePaths.size()!=6||physicalFileReopens!=9))return fail("highbank focused physical census");
        if(subset.empty()&&(coreCount!=32||ordinaryReads!=5||canonicalReads!=5||legacyMemoryLoads!=2||schema1MemoryLoads!=2||physicalUniquePaths.size()!=10||physicalFileReopens!=15))return fail("complete map-wall census");
        std::ofstream result(out/"result.txt");result<<"PASS "<<(subset.empty()?"complete-overlay":"diagnostic-subset")<<"\ncore="<<coreCount<<"\ncontrols="<<controlCount<<"\nordinaryPhysicalReads="<<ordinaryReads<<"\ncanonicalPhysicalReads="<<canonicalReads<<"\nuniquePhysicalFiles="<<physicalUniquePaths.size()<<"\nphysicalFileReopens="<<physicalFileReopens<<"\nlegacyCompleteMemoryLoads="<<legacyMemoryLoads<<"\nschema1CompleteMemoryLoads="<<schema1MemoryLoads<<"\nvalidCompleteMemoryLoads="<<legacyMemoryLoads+schema1MemoryLoads<<"\nobservedWallSchema="<<observedWallSchema<<"\nschema2InvalidRejects="<<schema2InvalidRejects<<"\nschema2InvalidPending="<<schema2InvalidPending<<"\nschema2InvalidStatus="<<(observedWallSchema==2?"executed":observedWallSchema==1?"pending-schema2":"not-selected")<<"\npairedTicks="<<pairedTicks<<"\nBMP="<<bmps<<"\n";return true;
    }catch(const std::exception& e){return fail(e.what());}
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
