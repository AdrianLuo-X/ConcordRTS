#ifdef _MSC_VER
#pragma optimize("", off)
#endif
// Native infantry and death Anim share a whole-canvas center. The project
// infantry foot convention is adapted once at death; original frame crops
// remain unchanged. Expected pivots below use raw SHP headers, not the new
// production canvas-capture helper. Fixed15Hz is the project clock policy.
bool runRuntimeRadiationDeathAnchorSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* outputEnv=std::getenv("CONCORD_RADIATION_DEATH_ANCHOR_OUTPUT");
    const char* filterEnv=std::getenv("CONCORD_RADIATION_DEATH_ANCHOR_CASE");
    const auto output=std::filesystem::absolute(outputEnv&&*outputEnv?std::filesystem::path(outputEnv)
        :std::filesystem::path("build/issue21_radiation_death_anchor"));
    const std::string filter=filterEnv?filterEnv:"";std::filesystem::create_directories(output);
    std::ofstream csv(output/"radiation-anchor-frames.csv"), manifest(output/"original-infantry-manifest.csv");
    csv<<"case,hz,tick,type,slot,moving,worldX,worldY,worldZ,screenX,screenY,fixedX,fixedY,tileHeight,frame,remaining,total,opaque,pivotX,pivotY,left,top\n";
    manifest<<"type,strength,armor,notHuman,radImmune,jumpjet,image,classification,source,rawWidth,rawHeight,rawFrames,sha256\n";
    std::string label,phase;std::vector<std::string> failures;
    int cases=0,fullTicks=0,wmKeys=0,wmClicks=0,deaths=0,frameChecks=0,pixelChecks=0;
    int ordinarySaves=0,exactSaves=0,paired=0,coldChecks=0,bmps=0,movingDeaths=0;
    int cooldownChecks=0,cooldownCodecChecks=0;
    std::set<std::pair<int,int>> slotWitnesses;std::set<int> movingRateWitnesses;
    const auto fail=[&](const std::string& reason){error="Radiation anchor "+label+" ["+phase+"]: "+reason;return false;};
    IniFile rulesIni;
    if(!live.rulesLoaded||!live.artLoaded||!live.theaterLoaded||!rulesIni.loadFromFile(live.report.rulesPath))
        return fail("actual original startup/rules unavailable");
    if(live.rules.getInfantryDeathAnim(7)!="NUKEDIE")return fail("original InfantryNuked mapping differs");
    const auto* radWeapon=live.rules.findWeaponType("RadEruptionWeapon");
    const auto* radWarhead=live.rules.findWarheadType("RadSite");
    if(!radWeapon||radWeapon->radLevel!=500||!radWarhead||radWarhead->infDeath!=7||!radWarhead->radiation)
        return fail("original DESO/site declarations unavailable");
    int landTile=-1;RuntimeMapRenderCache terrainCache;const auto metadata=runtimeTerrainMetadataContext(live);
    for(int n=0;n<static_cast<int>(live.theater.getTotalTileSlots());++n){TerrainCell cell;cell.tileIndex=n;cell.subTileIndex=0;
        const auto info=runtimeTerrainInfoForCell(live,cell);
        if(info.passable&&!info.water&&!info.shore&&!info.cliff&&!info.bridge
            &&resolveRuntimeTerrainSubTileRampType(cell,metadata,terrainCache)==0){landTile=n;break;}}
    if(landTile<0)return fail("original flat TMP unavailable");
    using Bytes=std::vector<unsigned char>;
    const auto u16=[](Bytes& b,unsigned int v){b.push_back(static_cast<unsigned char>(v));b.push_back(static_cast<unsigned char>(v>>8));};
    const auto u32=[&](Bytes& b,unsigned int v){u16(b,v);u16(b,v>>16);};
    const auto pack=[&](const Bytes& raw) {
        Bytes result;
        for (std::size_t at=0;at<raw.size();at+=8192) {
            const auto count=std::min<std::size_t>(8192,raw.size()-at);Bytes chunk;
            if(count<=238)chunk.push_back(static_cast<unsigned char>(17+count));
            else {chunk.push_back(0);std::size_t n=count-18;while(n>255){chunk.push_back(0);n-=255;}chunk.push_back(static_cast<unsigned char>(n));}
            chunk.insert(chunk.end(),raw.begin()+at,raw.begin()+at+count);chunk.insert(chunk.end(),{17,0,0});
            u16(result,static_cast<unsigned int>(chunk.size()));u16(result,static_cast<unsigned int>(count));result.insert(result.end(),chunk.begin(),chunk.end());
        }
        return result;
    };
    const auto base64=[](const Bytes& b) {
        constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;
        for(std::size_t at=0;at<b.size();at+=3){const unsigned int v=(unsigned(b[at])<<16)
            |(at+1<b.size()?unsigned(b[at+1])<<8:0)|(at+2<b.size()?unsigned(b[at+2]):0);
            out+=alphabet[(v>>18)&63];out+=alphabet[(v>>12)&63];out+=at+1<b.size()?alphabet[(v>>6)&63]:'=';out+=at+2<b.size()?alphabet[v&63]:'=';}
        return out;
    };
    Bytes raw;
    for(int y=50;y<=120;++y)for(int x=50;x<=120;++x){u16(raw,x);u16(raw,y);u32(raw,landTile);
        raw.insert(raw.end(),{0,2,0});}
    u32(raw,0);const auto encoded=base64(pack(raw));
    const auto mapPath=(output/"radiation-anchor-native.map").generic_string();
    {std::ofstream map(mapPath,std::ios::binary);map<<"[Basic]\nName=NativeRadiationAnchor\nMultiplayerOnly=yes\nNewINIFormat=4\n"
        "[Header]\nWidth=180\nHeight=180\nStartX=0\nStartY=0\nNumberStartingPoints=0\n"
        "[Map]\nTheater="<<live.report.map.theater<<"\nSize=0,0,90,90\nLocalSize=0,0,90,90\n"
        "[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n[IsoMapPack5]\n";
        for(std::size_t at=0;at<encoded.size();at+=70)map<<at/70+1<<'='<<encoded.substr(at,70)<<'\n';
        map.close();if(!map)return fail("native backed map write failed");}
    constexpr int width=1024,height=768;
    const RECT client{0,0,width,height},viewport=runtimeMapViewport(client);
    const std::array<std::string,3> owners{"SkirmishPlayer1","SkirmishPlayer2","SkirmishPlayer3"};
    const std::array<COLORREF,3> colors{RGB(255,220,40),RGB(40,110,255),RGB(220,40,40)};
    const auto make=[&](int hz,int viewer) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,live,false);
        s.config=live.config;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Russians";
        s.config.defaultMapPath=mapPath;s.config.noCooldown=false;s.config.superweapons=false;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=s.config.crates=false;
        s.config.startingUnits=0;s.config.techLevel=10;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout=false;s.config.keyBindings.clear();s.campaignMode=false;
        s.skirmishMatchFinished=s.skirmishVictory=false;s.skirmishResult.clear();s.tick=0;
        IniFile mapIni;MapDatabase map;
        if(!mapIni.loadFromFile(mapPath)||!map.loadFromIni(mapIni,&rulesIni)||!s.world.loadFromMap(map,&s.rules)){
            fail("native map startup parse failed");return std::unique_ptr<RuntimeWindowState>{};}
        const auto& summary=map.getSummary();s.report.map.loaded=true;s.report.map.path=mapPath;s.report.map.name=summary.name;
        s.report.map.theater=summary.theater;s.report.map.size=summary.size;s.report.map.localSize=summary.localSize;
        s.report.map.width=summary.width;s.report.map.height=summary.height;s.report.map.isoTiles=static_cast<int>(map.getIsoTiles().size());
        s.report.world=s.world.getSummary();s.mapFacts=map.buildMapFacts(&s.theater);s.mapFactsLoaded=!s.mapFacts.cells.empty();
        // Loading changes vector storage. Rebuild pointer caches BEFORE any
        // object authoring/terrain helper, not just at later navigation init.
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);s.terrainInfoByCell.clear();
        if(s.world.getTerrainCells().size()!=5041||s.mapFacts.cells.size()!=5041){fail("actual map/facts domain differs");return std::unique_ptr<RuntimeWindowState>{};}
        const MapQuery query(s.mapFacts);
        for(const auto& c:s.world.getTerrainCells()){const auto* fact=query.getCell(c.x,c.y);
            if(!fact||!fact->hasTerrain||c.cellIndex512!=c.y*512+c.x||fact->terrain.tileIndex!=c.tileIndex||fact->heightLevel!=c.level){
                fail("actual native TMP/facts/512 identity differs");return std::unique_ptr<RuntimeWindowState>{};}}
        const auto add=[&](int id,const std::string& type,RuntimeCellPoint cell,const std::string& owner){
            WorldObjectInstance o;o.instanceId=id;o.mapId="RadAnchor"+std::to_string(id);o.type=type;o.owner=owner;o.kind=WorldObjectKind::Structure;
            o.x=cell.x;o.y=cell.y;o.cell=runtimeCellKey(o.x,o.y);o.level=terrainLevelAtRuntimeCell(s,o.x,o.y);o.alive=o.poweredOperational=true;
            o.health=runtimeMaxHealthForObject(s,o);o.mission="Guard";s.world.accessObjects().push_back(std::move(o));};
        for(int i=0;i<3;++i){const RuntimeCellPoint base=i==0?RuntimeCellPoint{54,54}:i==1?RuntimeCellPoint{54,112}:RuntimeCellPoint{112,54};
            add(10+i*10,"GAAIRC",base,owners[i]);add(11+i*10,"GAPOWR",{base.x+5,base.y},owners[i]);}
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.visionEnabled=true;s.playerCommunication={};s.playerLocalCommands={};s.selectedObjectIds.clear();
        s.ownerRemapColors.clear();s.neutralRemapOwners.clear();
        loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.owner=owners[viewer];s.economy.credits=20000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();s.humanHouseAuthorities.clear();s.playerCommands.alliances.clear();
        s.skirmishModeAlliesAllowed=s.skirmishModeAllyChangeAllowed=true;s.skirmishModeMustAlly=false;
        for(int i=0;i<3;++i){RuntimeConfig::PlayerSlot slot;slot.enabled=slot.human=true;slot.local=i==viewer;slot.country="Russians";
            slot.name="Radiation viewer "+std::to_string(i+1);slot.team="-";s.config.playerSlots.push_back(slot);
            const auto canonical=runtimeCanonicalHumanHouseOwner(owners[i]);s.skirmishCountryByOwner[canonical]="Russians";
            s.ownerRemapColors[canonical]=colors[i];}
        s.localRadarColor=colors[viewer];
        // Create the full remote economies/catalogs through normal startup,
        // after the real roster and local facade owner have been authored.
        {RuntimeSnapshotActiveStateScope active(s);initializeRuntimeHumanHouseAuthorities(s);}
        int localHouses=0;
        for(const auto& house:s.humanHouseAuthorities){
            if(house.slotIndex>=owners.size()||house.owner!=owners[house.slotIndex]
                ||house.canonicalOwner!=runtimeCanonicalHumanHouseOwner(house.owner)
                ||house.localFacade!=(house.slotIndex==static_cast<std::size_t>(viewer))
                ||(!house.localFacade&&(!house.remoteEconomy||!house.remoteTechTree
                    ||house.remoteEconomy->owner!=house.owner))){
                fail("normal human-house authority bundle differs");return std::unique_ptr<RuntimeWindowState>{};}
            localHouses+=house.localFacade?1:0;
        }
        if(s.humanHouseAuthorities.size()!=3||localHouses!=1||s.economy.owner!=owners[viewer]
            ||s.humanHouseScopeActive||s.remoteHumanHouseScopeActive){
            fail("three complete human authorities / restored local facade unavailable");return std::unique_ptr<RuntimeWindowState>{};}
        ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeGarrisonsInitialized(s);
        ensureRuntimeGatesInitialized(s);ensureRuntimeRepairDepotsInitialized(s);(void)ensureRuntimeAircraftServicesInitialized(s);
        // Finish normal startup before UI/hash baselines; a later lazy module
        // initialization must not be misattributed to death presentation.
        ensureRuntimeSuperWeaponsInitialized(s);
        ensureRuntimeMenuKeyboardLoaded(s);initializeRuntimeOfflineGameplayRouting(s);rebuildRuntimeEconomyPower(s);
        RuntimeMultiplayerGameplayPlayerCommunicationCommand ally;ally.kind=RuntimePlayerCommunicationKind::Alliance;ally.value=1;
        ally.text="skirmishplayer2";applyRuntimePlayerCommunication(s,owners[0],ally);ally.text="skirmishplayer1";applyRuntimePlayerCommunication(s,owners[1],ally);
        s.playerCommunication.lines.clear();s.vision.reset();updateRuntimeVisionState(s);completeRuntimeWorldRefresh(s);
        if(!runtimeOwnersAreAllied(s,owners[0],owners[1])||runtimeOwnersAreAllied(s,owners[0],owners[2])){
            fail("three real Battle houses / mutual alliance not initialized");return std::unique_ptr<RuntimeWindowState>{};}
        return box;
    };
    const auto step=[&](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);++fullTicks;
        return !s.skirmishMatchFinished?true:fail("fixture Battle ended: "+s.skirmishResult);};

    const auto context=[&](const RuntimeWindowState& s){RuntimeMapRenderContext c;c.rules=&s.rules;c.art=&s.art;c.theater=&s.theater;
        c.assetRootPath=runtimeAssetRootPath(s.config);c.palettePath=runtimePalettePath(s.config,s.report.map);
        c.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);c.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        c.fixedTickHz=s.config.fixedTickHz;c.animationTick=s.tick;c.effectOnlyProbe=true;c.vision=nullptr;c.lightingTint=nullptr;return c;};
    struct Input{
        RuntimeWindowState& s;HWND window=nullptr;WNDPROC previous=nullptr;
        ~Input(){if(window){if(GetCapture()==window)ReleaseCapture();SetWindowLongPtrW(window,GWLP_USERDATA,0);
            if(previous)SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));DestroyWindow(window);}
            releaseRuntimeBackBuffer(s.backBuffer);releaseRuntimeBackBuffer(s.sidebarStaticBuffer);releaseRuntimeBackBuffer(s.radarTerrainBuffer);}
    };
    const auto open=[&](Input& in){in.window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Radiation native anchor smoke",
        WS_POPUP,-30000,-30000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!in.window)return fail("hidden real Win32 window failed");SetWindowLongPtrW(in.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&in.s));
        in.previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(in.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        HDC dc=GetDC(nullptr);const bool ok=dc&&ensureRuntimeBackBuffer(in.s.backBuffer,dc,width,height);if(dc)ReleaseDC(nullptr,dc);
        return in.previous&&ok?true:fail("actual WndProc / DIB unavailable");};
    const auto key=[&](Input& in,int code,int id){RuntimeSnapshotActiveStateScope active(in.s);
        if(in.s.selectedObjectIds!=std::vector<int>{id})applySelection(in.s,{id},false,false);
        SendMessageW(in.window,WM_KEYDOWN,code,1);SendMessageW(in.window,WM_KEYUP,code,1);++wmKeys;};
    const auto move=[&](Input& in,int id,RuntimeCellPoint cell){RuntimeSnapshotActiveStateScope active(in.s);
        applySelection(in.s,{id},false,false);centerRuntimeCameraOnCell(in.s,client,cell);ensureRuntimeCameraForViewport(in.s,viewport);
        const POINT p=runtimeScreenPointForCell(in.s,viewport,cell,terrainLevelAtRuntimeCell(in.s,cell.x,cell.y));
        const auto actual=approximateCellFromScreenPoint(in.s,viewport,p);
        if(!pointInRectInclusive(viewport,p)||actual.x!=cell.x||actual.y!=cell.y)return fail("real Move cell not pickable");
        SendMessageW(in.window,WM_MOUSEMOVE,0,MAKELPARAM(p.x,p.y));SendMessageW(in.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(in.window,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));++wmClicks;
        const auto b=in.s.behaviors.find(id);return b!=in.s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move&&b->second.moveTarget==cell
            ?true:fail("real Move rejected: "+in.s.interactionStatus);};
    const auto add=[&](RuntimeWindowState& s,const std::string& type,RuntimeCellPoint cell){RuntimeSnapshotActiveStateScope active(s);
        auto* o=addRuntimeObjectInstanceForOwner(s,owners[0],RuntimeBuildKind::Infantry,type,cell);
        if(!o){fail("original infantry startup authoring rejected: "+type);return -1;}
        o->mission="Sleep";const auto* u=s.rules.findUnitType("InfantryType",type);
        if(!u||o->health!=u->strength){fail("original infantry Strength changed: "+type);return -1;}return o->instanceId;};
    const auto death=[](const RuntimeWindowState& s,int id)->const RuntimeImpactEffect*{
        for(const auto& fx:s.impactEffects)if(fx.death&&fx.targetObjectId==id)return &fx;return nullptr;};
    const auto sha=[](const Bytes& b){const auto h=runtimeMultiplayerSnapshotSha256(b);std::string text;
        for(auto c:h){text+="0123456789ABCDEF"[c>>4];text+="0123456789ABCDEF"[c&15];}return text;};
    struct Sprite{RuntimeShpFrameImage first;std::string id,path,hash;int frames=0;bool rulesOnlyNoCanvas=false;};
    auto originalCache=std::make_unique<RuntimeMapRenderCache>();std::map<std::string,Sprite> originals;
    const auto sprite=[&](const RuntimeWindowState& s,const std::string& id)->const Sprite*{
        if(const auto found=originals.find(id);found!=originals.end())return &found->second;
        Sprite value;value.id=id;std::string resolved,why;
        if(id=="WEEDGUY"){
            // Original weapon-registration placeholder, not a substitute SHP.
            // Only this exact native rules identity may lack a source canvas.
            const auto* unit=s.rules.findUnitType("InfantryType",id);
            if(!unit||unit->strength!=130||unit->techLevel!=-1||unit->allowedToStartInMultiplayer
                ||!unit->image.empty()||unit->primaryWeapon!="NukeCarrier"||unit->secondaryWeapon!="V3Cluster"
                ||rulesIni.getValue(id,"Name","")!="ZZZ Chem Spray Infantry"
                ||!rulesIni.getValue(id,"Image","").empty()||s.art.findArtType(id)!=nullptr){
                fail("original rules-only WEEDGUY identity differs");return nullptr;}
            if(loadRuntimeAuxiliaryShpFrameImage(context(s),*originalCache,id,0,value.first,resolved,value.path,why,false,&value.frames)
                ||why.rfind("auxiliary SHP was not found:",0)!=0){
                fail("WEEDGUY must have no original source SHP, not a malformed/decoded asset: "+why);return nullptr;}
            value.rulesOnlyNoCanvas=true;value.first={};value.path.clear();value.hash.clear();value.frames=0;
            return &originals.emplace(id,std::move(value)).first->second;
        }
        if(!loadRuntimeAuxiliaryShpFrameImage(context(s),*originalCache,id,0,value.first,resolved,value.path,why,false,&value.frames)
            ||value.first.globalWidth<=0||value.first.globalHeight<=0||value.frames<=0){fail("original source SHP header unavailable: "+id+" "+why);return nullptr;}
        const RuntimeAuxiliaryShpBytesCacheEntry* raw=nullptr;
        for(const auto& [cacheKey,entry]:originalCache->auxiliaryShpBytesByImage){(void)cacheKey;
            if(entry.loaded&&entry.sourcePath==value.path&&entry.bytes.size()>=8){raw=&entry;break;}}
        if(!raw){fail("decoded source lost actual raw SHP bytes: "+id);return nullptr;}
        const auto read16=[&](int at){return int(raw->bytes[at])|(int(raw->bytes[at+1])<<8);};
        if(read16(2)!=value.first.globalWidth||read16(4)!=value.first.globalHeight||read16(6)!=value.frames){
            fail("original raw header / decoded canvas mismatch: "+id);return nullptr;}
        value.hash=sha(raw->bytes);return &originals.emplace(id,std::move(value)).first->second;};
    const auto bodyImage=[&](const UnitType& u){std::string id=u.image.empty()?u.id:u.image;
        if(const auto* art=live.art.findArtType(id);art&&!art->image.empty())id=art->image;return id;};
    // Three original subslots, independently spelled out instead of the new
    // anchor/canvas capture helper (or the live corpse's cleared render pose).
    const auto offset=[](int slot){return slot==2?std::pair<double,double>{.25,-.25}:slot==4?std::pair<double,double>{.25,.25}
        :slot==3?std::pair<double,double>{-.25,.25}:std::pair<double,double>{0,0};};
    struct Frozen{WorldObjectInstance object;double x=0,y=0,z=0;int sx=0,sy=0,fx=0,fy=0;};
    const auto freeze=[&](const WorldObjectInstance& o,const Sprite& body){Frozen value;value.object=o;const auto sub=offset(o.subCell);
        value.x=(o.hasRenderPosition?o.renderX:o.x)+sub.first;value.y=(o.hasRenderPosition?o.renderY:o.y)+sub.second;
        value.z=o.hasRenderPosition?o.renderLevel:o.level;
        if(body.rulesOnlyNoCanvas)return value;
        const int h=body.first.globalHeight;value.sy=15-(h+1)/2;const int doublePivot=30-(h+1)/2;value.fy=2*value.sy-doublePivot;
        return value;};
    const auto expectedOffset=[](int screen,int fixed,int tileHeight){return int(std::lround(fixed+(screen-fixed)*tileHeight/30.0));};
    const auto checkAnchor=[&](const RuntimeImpactEffect& fx,const Frozen& frozen){
        return fx.hasWorldPosition&&fx.worldX==frozen.x&&fx.worldY==frozen.y&&fx.worldLevel==frozen.z
            &&fx.screenOffsetX==frozen.sx&&fx.screenOffsetY==frozen.sy&&fx.nativeFixedX==frozen.fx&&fx.nativeFixedY==frozen.fy
            ?true:fail("frozen single-slot world point / original whole-canvas affine offsets differ: "+runtimeMultiplayerSnapshotImpactRecord(fx));};
    const auto record=[](const RuntimeImpactEffect& fx){std::ostringstream out;out<<runtimeMultiplayerSnapshotImpactRecord(fx)<<'|'
        <<iniHexEncode(fx.animId)<<'|'<<iniHexEncode(fx.renderIntent)<<'|'<<fx.frameIndex<<'|'<<fx.facing<<'|'<<fx.radius<<'|'<<runtimeBoolText(fx.bright);return out.str();};
    const auto archive=std::filesystem::path(runtimeAssetRootPath(live.config))/"ra2";
    const auto slice=[&](const std::filesystem::path& path,std::size_t at,std::size_t count){std::ifstream stream(path,std::ios::binary);
        Bytes b(count);stream.seekg(static_cast<std::streamoff>(at));stream.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(count));if(!stream)b.clear();return b;};
    const auto originalNuke=slice(archive/"conquer.mix",5193644,26584);
    const auto originalGi=slice(archive/"conquer.mix",9819548,186032);
    const auto originalPalette=slice(archive/"cache.mix",85828,768);
    if(sha(originalNuke)!="72558430EE8283BF7A8140EBF58BABD5C06B8E5F8CE6F5207B21F3F6725FE38D"
        ||sha(originalGi)!="BCEAEE9C270D6382D80D509F2C4B2893C10565F779096B3ADD37A4D5321E8BE1"
        ||sha(originalPalette)!="341E85171041E7C8BE53D5533D96A9BF86B402D9D67335713369354AAA5707AF")
        return fail("original conquer GI/NUKEDIE or cache ANIM bytes differ");
    if(toUpperAsciiLocal(live.report.map.theater)!="TEMPERATE")return fail("registered original source-image aliases require the temperate fixture");
    std::array<RuntimeShpFrameImage,60> nukes;std::string resourcePath,resourceError;
    for(int f=0;f<60;++f)if(!loadRuntimeShpFrameFromMixArchive((archive/"conquer.mix").generic_string(),*originalCache,"nukedie.shp",f,nukes[f],resourcePath,resourceError)
        ||nukes[f].globalWidth!=60||nukes[f].globalHeight!=60
        ||nukes[f].pixels.size()!=std::size_t(nukes[f].width*nukes[f].height))return fail("raw original NUKEDIE frame "+std::to_string(f)+": "+resourceError);
    if(nukes[59].width!=0||nukes[59].height!=0||!nukes[59].pixels.empty())return fail("original final empty NUKEDIE frame changed");
    std::vector<RuntimePaletteColor> palette;
    if(!loadRuntimePaletteResource(metadata,*originalCache,"anim.pal",palette,resourcePath,resourceError)||palette.size()!=256)
        return fail("actual ANIM palette decode failed: "+resourceError);
    for(int n=0;n<256;++n)if(palette[n].r!=originalPalette[n*3]*4||palette[n].g!=originalPalette[n*3+1]*4||palette[n].b!=originalPalette[n*3+2]*4)
        return fail("actual effect palette differs from original six-bit ANIM DAC");
    const auto* gi=sprite(live,"GI");if(!gi||gi->hash!=sha(originalGi)||gi->first.globalWidth!=78||gi->first.globalHeight!=66)
        return fail("live physical GI does not resolve the verified original canvas");
    const auto* nuArt=live.art.findArtType("NUKEDIE");
    if(!nuArt||nuArt->translucent||(nuArt->shadowSpecified&&nuArt->shadow)||nuArt->reportSound!="InfantryMelt")
        return fail("original NUKEDIE full opaque animation/report changed");
    const auto color=[&](unsigned char index){return (std::uint32_t(originalPalette[index*3]*4)<<16)
        |(std::uint32_t(originalPalette[index*3+1]*4)<<8)|std::uint32_t(originalPalette[index*3+2]*4);};
    const auto renderActor=[&](const RuntimeWindowState& s,int id)->std::optional<RuntimeImpactRenderActor>{
        RuntimeSnapshotActiveStateScope active(s);const auto* actors=runtimeImpactRenderActorsForRender(s);
        if(actors)for(const auto& actor:*actors)if(actor.targetObjectId==id&&actor.lifecycleState=="UnitDestruction"&&actor.animId=="NUKEDIE")return actor;
        return std::nullopt;};
    constexpr int rasterWidth=320,rasterHeight=240;const RECT rasterViewport{0,0,rasterWidth,rasterHeight};
    const auto raster=[&](RuntimeWindowState& s,const RuntimeImpactEffect& fx,const Frozen& frozen,int tileHeight,
        const RuntimeImpactRenderActor& actor,bool cold,const std::string& bmp){
        phase="original centered-canvas pixels";if(!checkAnchor(fx,frozen)||actor.animId!="NUKEDIE"||!actor.centerOnAnchor
            ||actor.x!=frozen.x||actor.y!=frozen.y||actor.level!=frozen.z
            ||actor.screenOffsetX!=frozen.sx||actor.screenOffsetY!=frozen.sy||actor.nativeFixedX!=frozen.fx||actor.nativeFixedY!=frozen.fy)
            return fail("materialized actor changed the frozen canvas/world point");
        const auto pausedRecord=record(fx);const int pausedTick=s.tick;
        const int frame=actor.frameIndex;if(frame<0||frame>=60)return fail("materialized NUKEDIE frame outside original strip");
        const auto& image=nukes[frame];RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=tileHeight*2;camera.tileHeight=tileHeight;
        // Independently project the physical point. The bitmap is 1:1 at both
        // zooms; only the tile contribution to its frozen pivot changes.
        const int projectedX=int(std::llround((frozen.x-frozen.y)*camera.tileWidth/2.0));
        const int projectedY=int(std::llround((frozen.x+frozen.y-frozen.z)*camera.tileHeight/2.0));
        camera.worldOffsetX=projectedX-rasterWidth/2;camera.worldOffsetY=projectedY-rasterHeight/2;
        const int pivotX=rasterWidth/2+expectedOffset(frozen.sx,frozen.fx,tileHeight);
        const int pivotY=rasterHeight/2+expectedOffset(frozen.sy,frozen.fy,tileHeight);
        const int left=pivotX-image.globalWidth/2+image.offsetX,top=pivotY-image.globalHeight/2+image.offsetY;
        constexpr std::uint32_t bg1=0x001b2634,bg2=0x00492a16;
        std::vector<std::uint32_t> first(rasterWidth*rasterHeight,bg1),second(rasterWidth*rasterHeight,bg2),expectedFirst=first,expectedSecond=second;
        int opaque=0;for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){const auto index=image.pixels[y*image.width+x];if(!index)continue;
            const int dx=left+x,dy=top+y;if(dx<0||dy<0||dx>=rasterWidth||dy>=rasterHeight)return fail("original full frame would be clipped");
            const auto p=std::size_t(dy)*rasterWidth+dx;expectedFirst[p]=expectedSecond[p]=color(index);++opaque;}
        auto c=context(s);c.surfaceWidth=rasterWidth;c.surfaceHeight=rasterHeight;c.animationTick=s.tick;
        // Original palette/mask probe isolates the SHP from the existing
        // rule-derived Bright scene light. Keep the actual actor unchanged;
        // the separate full-map captures retain rules and scene lighting.
        c.rules=nullptr;
        c.visionObserverOwner=owners[tileHeight==30?0:1];std::vector<RuntimeImpactRenderActor> actors{actor};c.impactActors=&actors;
        // A cold render receives the already materialized frozen actor, with
        // no corpse/body or tile to consult. This removes presentation cache
        // dependencies without deleting real authority or asking an empty
        // wire image to invent the removed target's type.
        GameWorld absent=s.world;absent.accessObjects().clear();
        const GameWorld& renderedWorld=cold?absent:s.world;RuntimeMapRenderCache cache;
        auto copyCamera=camera;c.surfacePixels=first.data();const auto one=drawRuntimeMap(nullptr,rasterViewport,renderedWorld,c,copyCamera,cache);
        copyCamera=camera;c.surfacePixels=second.data();const auto two=drawRuntimeMap(nullptr,rasterViewport,renderedWorld,c,copyCamera,cache);
        if(!one.rendered||!two.rendered||one.impactActorSnapshots!=1||two.impactActorSnapshots!=1||one.localLightSnapshots||two.localLightSnapshots)
            return fail("actual original effect renderer did not consume the single actor");
        const auto equalRgb=[](const auto& a,const auto& b){if(a.size()!=b.size())return false;
            for(std::size_t n=0;n<a.size();++n)if((a[n]&0xffffffU)!=(b[n]&0xffffffU))return false;return true;};
        if(!equalRgb(first,expectedFirst)||!equalRgb(second,expectedSecond)){
            std::size_t diff=0;while(diff<first.size()&&(first[diff]&0xffffffU)==(expectedFirst[diff]&0xffffffU))++diff;
            std::ostringstream why;why<<"native NU pixel pivot/crop mismatch frame="<<frame<<" zoom="<<tileHeight<<" cold="<<cold
                <<" rawCanvas="<<image.globalWidth<<'x'<<image.globalHeight<<" crop="<<image.offsetX<<','<<image.offsetY<<','<<image.width<<','<<image.height
                <<" expectedTopLeft="<<left<<','<<top<<" firstDifference="<<diff%rasterWidth<<','<<diff/rasterWidth;
            if(diff<first.size())why<<" expectedRGB="<<std::hex<<expectedFirst[diff]<<" actualRGB="<<first[diff];return fail(why.str());}
        std::vector<std::uint32_t> repeated(rasterWidth*rasterHeight,bg1);copyCamera=camera;c.surfacePixels=repeated.data();
        drawRuntimeMap(nullptr,rasterViewport,renderedWorld,c,copyCamera,cache);
        if(!equalRgb(first,repeated)||s.tick!=pausedTick||record(fx)!=pausedRecord)return fail("same-tick original death repaint advanced pixels/clock/frozen record");
        csv<<label<<','<<s.config.fixedTickHz<<','<<s.tick<<','<<frozen.object.type<<','<<frozen.object.subCell<<','<<runtimeBoolText(frozen.object.renderMoveActive)
            <<','<<std::setprecision(17)<<frozen.x<<','<<frozen.y<<','<<frozen.z<<','<<frozen.sx<<','<<frozen.sy<<','<<frozen.fx<<','<<frozen.fy
            <<','<<tileHeight<<','<<frame<<','<<fx.remainingTicks<<','<<fx.totalTicks<<','<<opaque<<','<<pivotX<<','<<pivotY<<','<<left<<','<<top<<'\n';
        if(!bmp.empty()){if(!writeRuntimeSmokeBmp((output/(bmp+".bmp")).generic_string(),first.data(),rasterWidth,rasterHeight,error))return false;++bmps;}
        ++pixelChecks;if(cold)++coldChecks;return true;};
    const auto checkpoint=[&](RuntimeWindowState& s,bool exact){RuntimeSnapshotActiveStateScope active(s);
        std::ostringstream ordinary,complete,canonical;IniFile base,extension;
        if(!writeRuntimeStateToStream(s,ordinary,error)||!base.loadFromString(ordinary.str())
            ||base.getValue("UnitDeathAnimations","Count","")=="0")return std::unique_ptr<RuntimeWindowState>{};
        if(!exact){
            // Two isolated codec inputs are derived from the actual ordinary
            // save. They never alter the live clocks, HP, rules or firing run.
            // Modern rows retain independent nonzero clocks; the old 56-field
            // row keeps its preexisting behavior-mirror import semantics.
            const auto* savedObjects=base.getSection("Objects");bool audited=false;
            if(savedObjects)for(const auto& entry:*savedObjects){
                auto fields=splitRuntimeSaveFields(entry.second);int objectId=-1;
                if(fields.size()<57U||!parseRuntimeInt(fields[1],objectId))continue;
                const auto* object=findObjectById(s,objectId);
                if(!object||object->type!="DESO"||!object->deployed||object->radiationEruptionCooldownTicksRemaining<=0)continue;
                const auto originalBehavior=s.behaviors.find(objectId);
                if(originalBehavior==s.behaviors.end()||originalBehavior->second.kind!=RuntimeBehaviorKind::Attack
                    ||!originalBehavior->second.returnToDeployed){fail("actual saved DESO is not a deployed eruption behavior");return std::unique_ptr<RuntimeWindowState>{};}
                const int paidEruption=object->radiationEruptionCooldownTicksRemaining;
                const int paidOrdinary=object->weaponCooldownTicksRemaining;
                const int mirror=originalBehavior->second.weaponCooldownTicks;
                if(paidEruption>=std::numeric_limits<int>::max()){fail("saved eruption clock cannot form bounded codec input");return std::unique_ptr<RuntimeWindowState>{};}
                const auto probe=[&](const std::vector<std::string>& values,int expectedOrdinary,int expectedEruption,const char* suffix){
                    std::ostringstream row;row<<"[Objects]\n"<<entry.first<<'=';
                    for(std::size_t index=0;index<values.size();++index)row<<(index?"|":"")<<values[index];row<<'\n';
                    IniFile overlay,candidate=base;
                    if(!overlay.loadFromString(row.str()))return fail("isolated saved cooldown row did not parse");candidate.appendFrom(overlay);
                    auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
                    {RuntimeSnapshotActiveStateScope current(*loaded);
                        if(!materializeRuntimeLoadStateFromIni(*loaded,candidate,error))return fail(std::string(suffix)+" cooldown codec load: "+error);}
                    const auto* restored=findObjectById(*loaded,objectId);const auto b=loaded->behaviors.find(objectId);
                    if(!restored||restored->weaponCooldownTicksRemaining!=expectedOrdinary
                        ||restored->radiationEruptionCooldownTicksRemaining!=expectedEruption
                        ||b==loaded->behaviors.end()||b->second.weaponCooldownTicks!=mirror)
                        return fail(std::string(suffix)+" independent object clocks / saved behavior mirror differ");
                    ++cooldownCodecChecks;return true;};
                auto modern=fields;modern[46]=std::to_string(paidEruption+1);
                if(!probe(modern,paidEruption+1,paidEruption,"modern-dual-nonzero"))return std::unique_ptr<RuntimeWindowState>{};
                fields.resize(56U);
                if(!probe(fields,std::max(paidOrdinary,mirror),paidOrdinary,"legacy-no-eruption-field"))return std::unique_ptr<RuntimeWindowState>{};
                audited=true;break;
            }
            if(!audited){fail("actual saved positive DESO eruption codec witness absent");return std::unique_ptr<RuntimeWindowState>{};}}
        if(exact&&(!writeRuntimeStateToStream(s,complete,error)||!writeRuntimeMultiplayerSnapshotExtension(s,complete,error)
            ||!extension.loadFromString(complete.str())||!writeRuntimeMultiplayerSnapshotCanonicalIni(extension,canonical,error)
            ||!extension.loadFromString(canonical.str())))return std::unique_ptr<RuntimeWindowState>{};
        {std::ofstream file(output/(label+(exact?"-canonical-extension.ini":"-ordinary.ini")),std::ios::binary);
            file<<(exact?canonical.str():ordinary.str());if(!file)return std::unique_ptr<RuntimeWindowState>{};}
        auto other=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*other,s,false);
        if(!materializeRuntimeLoadStateFromIni(*other,base,error)
            ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*other,extension,error)))return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other);
        // Presentation camera/viewer can differ; authority and all FOUR
        // captured offsets remain directly comparable (hash excludes them).
        other->camera.initialized=true;other->camera.tileWidth=120;other->camera.tileHeight=60;
        if(exact)++exactSaves;else ++ordinarySaves;return other;};
    const auto compare=[&](RuntimeWindowState& s,RuntimeWindowState& other,int id,bool exact){RuntimeSnapshotActiveStateScope active(s);
        const auto* a=death(s,id);const auto* b=death(other,id);if(s.tick!=other.tick||bool(a)!=bool(b))return fail("saved death presence/clock differs");
        if(a&&(record(*a)!=record(*b)||a->screenOffsetX!=b->screenOffsetX||a->screenOffsetY!=b->screenOffsetY
            ||a->nativeFixedX!=b->nativeFixedX||a->nativeFixedY!=b->nativeFixedY))return fail(std::string(exact?"canonical":"ordinary")
                +" saved 49+6 death record differs: source="+record(*a)+" restored="+record(*b));
        const auto* x=findRuntimeObjectByIdIncludingInactive(s,id);const auto* y=findRuntimeObjectByIdIncludingInactive(other,id);
        if(!x||!y||x->alive!=y->alive||x->health!=y->health||x->cell!=y->cell||x->subCell!=y->subCell||x->level!=y->level)
            return fail("saved corpse identity/HP/slot/ground point differs");
        for(const auto& object:s.world.getObjects())if(object.alive&&object.type=="DESO"){
            const auto* restored=findObjectById(other,object.instanceId);
            if(!restored||restored->weaponCooldownTicksRemaining!=object.weaponCooldownTicksRemaining
                ||restored->radiationEruptionCooldownTicksRemaining!=object.radiationEruptionCooldownTicksRemaining)
                return fail(std::string(exact?"canonical":"ordinary")+" saved ordinary/eruption clocks differ id="+std::to_string(object.instanceId));
            ++cooldownChecks;}
        if(exact){std::string sourceError,restoredError;
            std::vector<std::pair<std::string,std::uint64_t>> sourceTrace,restoredTrace;
            const auto sourceHash=runtimeStandaloneSimulationStateHash(s,sourceError,&sourceTrace);
            const auto restoredHash=runtimeStandaloneSimulationStateHash(other,restoredError,&restoredTrace);
            if(!sourceError.empty()||!restoredError.empty()||sourceHash!=restoredHash){
                std::string firstDifference="final";
                for(std::size_t index=0;index<std::max(sourceTrace.size(),restoredTrace.size());++index){
                    if(index>=sourceTrace.size()||index>=restoredTrace.size()||sourceTrace[index]!=restoredTrace[index]){
                        firstDifference=index<sourceTrace.size()?sourceTrace[index].first:restoredTrace[index].first;break;}}
                const auto stem=label+"-hash-tick"+std::to_string(s.tick);std::string diagnosticErrors;
                {std::ofstream trace(output/(stem+"-trace.csv"),std::ios::binary);
                    trace<<"index,sourceBlock,sourceHash,restoredBlock,restoredHash\n";
                    for(std::size_t index=0;index<std::max(sourceTrace.size(),restoredTrace.size());++index){
                        trace<<index<<',';
                        if(index<sourceTrace.size())trace<<sourceTrace[index].first<<','<<sourceTrace[index].second;else trace<<',';
                        trace<<',';
                        if(index<restoredTrace.size())trace<<restoredTrace[index].first<<','<<restoredTrace[index].second;else trace<<',';
                        trace<<'\n';}
                    if(!trace)diagnosticErrors+=" trace-write";}
                // Diagnostic files preserve each state's real ordinary base
                // and exact extension independently; nothing is copied back.
                const auto dump=[&](RuntimeWindowState& state,const char* side){RuntimeSnapshotActiveStateScope current(state);
                    std::ostringstream ordinary,complete,canonical;IniFile exactIni;std::string why;
                    if(writeRuntimeStateToStream(state,ordinary,why)){
                        std::ofstream file(output/(stem+'-'+side+"-ordinary.ini"),std::ios::binary);file<<ordinary.str();
                        if(!file)diagnosticErrors+=" "+std::string(side)+"-ordinary-write";
                    }else diagnosticErrors+=" "+std::string(side)+"-ordinary:"+why;
                    why.clear();
                    if(writeRuntimeStateToStream(state,complete,why)&&writeRuntimeMultiplayerSnapshotExtension(state,complete,why)
                        &&exactIni.loadFromString(complete.str())&&writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni,canonical,why)){
                        std::ofstream file(output/(stem+'-'+side+"-canonical-extension.ini"),std::ios::binary);file<<canonical.str();
                        if(!file)diagnosticErrors+=" "+std::string(side)+"-extension-write";
                    }else diagnosticErrors+=" "+std::string(side)+"-extension:"+why;};
                dump(s,"source");dump(other,"restored");
                std::ostringstream why;why<<"canonical authority differs at "<<firstDifference<<" tick="<<s.tick
                    <<" source/restored="<<sourceHash<<'/'<<restoredHash
                    <<" world="<<s.world.deterministicSimulationStateHash()<<'/'<<other.world.deterministicSimulationStateHash()
                    <<" errors="<<sourceError<<" / "<<restoredError<<" diagnostics="<<stem<<diagnosticErrors;
                return fail(why.str());}}
        ++paired;return true;};
    const auto identify=[&](const UnitType& u,const std::string& classification,const Sprite* image){
        manifest<<u.id<<','<<u.strength<<','<<u.armor<<','<<runtimeBoolText(u.notHuman)<<','<<runtimeBoolText(u.immuneToRadiation)<<','<<runtimeBoolText(u.jumpJet)
            <<','<<bodyImage(u)<<','<<(image&&image->rulesOnlyNoCanvas?"affected-ground-human-rules-only-no-canvas":classification)
            <<','<<(image?image->path:std::string{})<<','<<(image?image->first.globalWidth:0)
            <<','<<(image?image->first.globalHeight:0)<<','<<(image?image->frames:0)<<','<<(image?image->hash:std::string{})<<'\n';};
    struct Victim{int id=-1;std::string type;const Sprite* body=nullptr;Frozen frozen;int born=-1,expires=-1;bool moved=false,movingDeath=false;
        std::set<int> visited;};
    const auto source=[&](RuntimeWindowState& s,RuntimeCellPoint cell){const auto* unit=s.rules.findUnitType("InfantryType","DESO");
        if(!unit||!unit->immuneToRadiation||!unit->deployFire||unit->deployWeapon!="RadEruptionWeapon"){
            fail("original immune DESO deployment differs");return -1;}
        return add(s,"DESO",cell);};
    const auto deploy=[&](Input& in,int id){key(in,'D',id);const auto* o=findObjectById(in.s,id);
        return o&&(o->deployed||runtimeMissionIsDeploying(*o))?true:fail("real WM D did not begin original DESO deployment: "+in.s.interactionStatus);};
    const auto site=[&](const RuntimeWindowState& s,int id){for(const auto& [unused,area]:s.specialMechanisms.radiationAreas()){
        (void)unused;if(area.sourceObjectId==id&&area.warheadId=="RadSite"&&area.maximumStrength==500&&area.radius==10&&area.remainingTicks>0)return true;}return false;};
    const auto inspect=[&](RuntimeWindowState& s,std::vector<Victim>& victims,const std::map<int,Frozen>& before,int sourceId,bool fullRaster){
        for(auto& v:victims){const auto* current=findObjectById(s,v.id);const auto* fx=death(s,v.id);
            if(v.born<0&&current){const auto old=before.find(v.id);if(old!=before.end()){
                const auto now=freeze(*current,*v.body);if(now.x!=old->second.x||now.y!=old->second.y)v.moved=true;}
                continue;}
            if(v.born<0){const auto old=before.find(v.id);if(!fx||old==before.end())return fail("natural lethal radiation lost living pre-effects pose: "+v.type);
                v.frozen=old->second;v.born=s.tick;v.expires=s.tick+fx->totalTicks;v.movingDeath=v.moved&&v.frozen.object.renderMoveActive;
                if(fx->animId!="NUKEDIE"||fx->renderIntent!="unitDestruction"||fx->warheadId!="RadSite"||fx->attackerObjectId!=sourceId
                    ||fx->owner!=v.frozen.object.owner||fx->animationFrameCount!=60||fx->animationSequenceSteps!=60
                    ||fx->animationStartFrame!=0||fx->animationEndFrame!=60||fx->animationLoopCount!=1
                    ||fx->animationFrameDelayTicks!=s.config.fixedTickHz/15||fx->remainingTicks!=fx->totalTicks
                    ||fx->totalTicks!=60*fx->animationFrameDelayTicks||!checkAnchor(*fx,v.frozen))
                    return fail("natural original RadSite death/60-frame clock differs: "+v.type+' '+(fx?record(*fx):"absent"));
                const auto count=std::count_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& e){return e.death&&e.targetObjectId==v.id;});
                if(count!=1||std::find(s.selectedObjectIds.begin(),s.selectedObjectIds.end(),v.id)!=s.selectedObjectIds.end())
                    return fail("death duplicated / retained actual selection");
                ++deaths;if(v.movingDeath){++movingDeaths;movingRateWitnesses.insert(s.config.fixedTickHz);}slotWitnesses.emplace(s.config.fixedTickHz,v.frozen.object.subCell);
                std::cout<<"Radiation anchor death type="<<v.type<<" id="<<v.id<<" slot="<<v.frozen.object.subCell<<" moved="<<v.moved
                    <<" movingAtDeath="<<v.movingDeath<<" sourceCanvas="<<v.body->first.globalWidth<<'x'<<v.body->first.globalHeight
                    <<" point="<<std::setprecision(17)<<v.frozen.x<<','<<v.frozen.y<<','<<v.frozen.z
                    <<" offset="<<v.frozen.sx<<','<<v.frozen.sy<<','<<v.frozen.fx<<','<<v.frozen.fy<<" tick="<<v.born<<'\n';}
            if(s.tick==v.expires){if(fx)return fail("original NUKEDIE did not naturally expire");continue;}
            if(s.tick>v.expires){if(fx)return fail("expired corpse was reconstructed/restarted");continue;}
            if(!fx||current||!checkAnchor(*fx,v.frozen)||fx->remainingTicks!=v.expires-s.tick)
                return fail("natural remaining life/frozen center changed: "+v.type);
            const int expectedFrame=std::min(59,(s.tick-v.born)/(s.config.fixedTickHz/15));const auto actor=renderActor(s,v.id);
            if(!actor||actor->frameIndex!=expectedFrame)return fail("original 60-frame finite order changed: "+v.type+" expected="+std::to_string(expectedFrame));
            ++frameChecks;const bool newFrame=v.visited.insert(expectedFrame).second;
            if(newFrame&&(fullRaster||v.body->rulesOnlyNoCanvas||expectedFrame==0||expectedFrame==30||expectedFrame==59)){
                if(!raster(s,*fx,v.frozen,30,*actor,false,"")||!raster(s,*fx,v.frozen,60,*actor,false,""))return false;
                if(expectedFrame==30&&!raster(s,*fx,v.frozen,60,*actor,true,""))return false;}
        }return true;};
    const auto beforePoses=[&](RuntimeWindowState& s,const std::vector<Victim>& victims){std::map<int,Frozen> result;
        for(const auto& v:victims)if(const auto* o=findObjectById(s,v.id))result.emplace(v.id,freeze(*o,*v.body));return result;};
    const auto completeVictims=[&](const std::vector<Victim>& victims,int tick){return std::all_of(victims.begin(),victims.end(),
        [&](const auto& v){return v.born>=0&&tick>=v.expires+2;});};
    const auto finishVictims=[&](const std::vector<Victim>& victims,bool moving){for(const auto& v:victims)
        if(v.born<0||v.visited.size()!=60||!v.visited.contains(59)||(moving&&!v.movingDeath))
            return fail("missing original death/full60-frame strip/true moving witness: "+v.type+" born="+std::to_string(v.born)
                +" visited="+std::to_string(v.visited.size())+" moved="+runtimeBoolText(v.moved)+" moving="+runtimeBoolText(v.movingDeath));return true;};
    // An independently bounded service window: all candidates are original
    // ground human Infantry with positive RadSite armor multiplier. The sweep
    // occupies at most a 6-cell radius, giving >=200 original intensity until
    // refresh. Original DESO remains deployed and pays its original ROF.
    // 60 seconds is a finite integration guard, not a modified weapon timer.
    constexpr int radiationDeadlineSeconds=60;
    const auto subject=[&](RuntimeWindowState& s,const std::string& type,RuntimeCellPoint cell)->std::optional<Victim>{
        const auto* unit=s.rules.findUnitType("InfantryType",type);if(!unit||unit->strength<=0||unit->notHuman||unit->immuneToRadiation||unit->jumpJet)
            {fail("core source is not an original affected ground human: "+type);return std::nullopt;}
        const auto* body=sprite(s,bodyImage(*unit));if(!body)return std::nullopt;
        if(body->rulesOnlyNoCanvas&&type!="WEEDGUY"){
            fail("only original WEEDGUY may use the no-canvas control");return std::nullopt;}
        const int id=add(s,type,cell);
        if(id<0)return std::nullopt;const auto* o=findObjectById(s,id);if(!o||o->airborne||o->subCell<2||o->subCell>4)
            {fail("original startup did not allocate an actual ground subslot");return std::nullopt;}
        Victim v;v.id=id;v.type=type;v.body=body;return v;};
    const auto runPrimary=[&](const std::string& type,int hz,int requestedSlot,bool moving){
        phase="real map/startup/fullHP";auto storage=make(hz,0);if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input))return false;
        const int deso=source(s,{77,80});if(deso<0)return false;
        // Allocate additional ordinary immune objects before the subject to
        // exercise actual original subcell assignment. Never write subCell.
        for(int n=0;n<3;++n){const int next=nextRuntimeObjectInstanceId(s);constexpr std::array<int,3> nativeSlots{2,4,3};
            if(nativeSlots[std::size_t(next)%3]==requestedSlot)break;
            if(add(s,"DESO",{67+n,70})<0)return false;}
        auto added=subject(s,type,{80,80});if(!added)return false;std::vector<Victim> victims{*added};const int id=added->id;
        if(findObjectById(s,id)->subCell!=requestedSlot)return fail("actual original subcell allocation did not exercise requested slot");
        bool issuedMove=false;
        if(moving){if(!move(input,id,{72,80}))return false;issuedMove=true;
            for(int n=0;n<hz*6&&!victims.front().moved;++n){const auto before=beforePoses(s,victims);
                if(!step(s)||!inspect(s,victims,before,deso,true))return false;}
            const auto* body=findObjectById(s,id);
            if(!body||!victims.front().moved||!body->renderMoveActive)return fail("original Move never began a real interpolated segment before radiation");}
        if(!deploy(input,deso))return false;
        std::unique_ptr<RuntimeWindowState> ordinary,exact;bool saved=false,seenSite=false;int captured=0;
        for(int frame=0;frame<hz*radiationDeadlineSeconds;++frame){
            const auto before=beforePoses(s,victims);if(!step(s))return false;
            if(ordinary&&!step(*ordinary))return false;if(exact&&!step(*exact))return false;
            seenSite=seenSite||site(s,deso);
            if(!inspect(s,victims,before,deso,true))return false;auto& v=victims.front();
            if(ordinary&&!compare(s,*ordinary,id,false))return false;if(exact&&!compare(s,*exact,id,true))return false;
            if(v.born>=0&&s.tick<v.expires){const auto* fx=death(s,id);const auto actor=renderActor(s,id);if(!fx||!actor)return fail("primary original actor absent");
                const int age=s.tick-v.born;
                if(moving&&type=="E1"&&!saved&&age>=20*(hz/15)){
                    phase="real ordinary/canonical checkpoint";ordinary=checkpoint(s,false);exact=checkpoint(s,true);
                    if(!ordinary||!exact)return fail("real save materialization failed: "+error);
                    if(!compare(s,*ordinary,id,false)||!compare(s,*exact,id,true))return false;saved=true;}
                if(saved){const auto* a=death(*ordinary,id);const auto* b=death(*exact,id);
                    const auto aa=renderActor(*ordinary,id),bb=renderActor(*exact,id);
                    if(!a||!b||!aa||!bb)return fail("restored frozen actor disappeared");
                    if(age%(hz/15)==0&&(!raster(*ordinary,*a,v.frozen,60,*aa,true,"")||!raster(*exact,*b,v.frozen,30,*bb,true,"")))return false;}
                if(type=="E1"&&!moving&&requestedSlot==2&&age==15*(hz/15)&&captured==0){
                    phase="actual map/SHP capture";const auto stats=drawRuntimeStateOffscreenAtCell(s,{80,80},800,520,error,
                        (output/(label+"-actual-map.bmp")).generic_string(),false,false);
                    if(!stats||stats->impactAnimDrawn<=0||stats->fallbackTerrainCells!=0)return fail("actual original TMP/body/death map render missing: "+error);
                    ++bmps;if(!raster(s,*fx,v.frozen,60,*actor,true,label+"-original-centered-120"))return false;++captured;}
            }
            if(completeVictims(victims,s.tick))break;
        }
        if(!seenSite||!finishVictims(victims,moving)||(moving&&!issuedMove)||(moving&&type=="E1"&&!saved))return fail("primary original flow/moving/save was not fully exercised: "+error);
        return true;};
    // Registered actual rules classification uses the original verse string,
    // not the new death-offset helper or a hard-coded list of human IDs.
    const std::array<std::string,11> nativeArmors{"none","flak","plate","light","medium","heavy","wood","steel","concrete","special_1","special_2"};
    std::vector<double> radVerses;{std::istringstream values(radWarhead->verses);std::string value;
        while(std::getline(values,value,',')){const auto percent=value.find('%');if(percent!=std::string::npos)value.erase(percent);radVerses.push_back(std::stod(value)/100.0);}}
    if(radVerses.size()!=nativeArmors.size())return fail("original 11-armor RadSite verses unavailable");
    std::vector<std::string> affected,isolatedDeathWeapons;
    for(const auto& unit:live.rules.getInfantryTypes()){
        const auto armor=std::find(nativeArmors.begin(),nativeArmors.end(),toLowerAsciiLocal(unit.armor));
        const bool damage=armor!=nativeArmors.end()&&radVerses[std::size_t(armor-nativeArmors.begin())]>0;
        const std::string classification=unit.strength<=0?"no-positive-Strength":unit.immune?"original-all-damage-immune":unit.immuneToRadiation?"original-rad-immune"
            :unit.jumpJet?"original-airborne":unit.notHuman?"original-nonhuman-sequence":!damage?"original-zero-RadSite-verse"
            :!unit.deathWeapon.empty()?"affected-human-deathweapon-isolated":"affected-ground-human";
        const auto* body=unit.strength>0?sprite(live,bodyImage(unit)):nullptr;if(unit.strength>0&&!body)return false;
        identify(unit,classification,body);if(classification=="affected-ground-human")affected.push_back(unit.id);
        if(classification=="affected-human-deathweapon-isolated")isolatedDeathWeapons.push_back(unit.id);
    }
    if(affected.empty()||affected.size()>64)return fail("registered original affected sweep count outside bounded fixture");
    const auto runSweep=[&](){phase="parallel original fullHP registry";auto storage=make(30,0);if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input))return false;
        const int deso=source(s,{80,80});if(deso<0)return false;std::vector<Victim> victims;std::vector<RuntimeCellPoint> locations;
        for(int y=76;y<=83;++y)for(int x=76;x<=83;++x)if(x!=80||y!=80)locations.push_back({x,y});
        if(affected.size()>locations.size())return fail("parallel registry cannot be authored without overlap");
        for(std::size_t n=0;n<affected.size();++n){auto v=subject(s,affected[n],locations[n]);if(!v)return false;victims.push_back(*v);}
        if(!deploy(input,deso))return false;bool seenSite=false;
        for(int frame=0;frame<30*radiationDeadlineSeconds;++frame){const auto before=beforePoses(s,victims);if(!step(s))return false;
            seenSite=seenSite||site(s,deso);if(!inspect(s,victims,before,deso,false))return false;if(completeVictims(victims,s.tick))break;}
        if(!seenSite||!finishVictims(victims,false))return false;
        std::cout<<"Radiation anchor parallel registry originalHumanDeaths="<<victims.size()<<" classifications="<<live.rules.getInfantryTypes().size()<<'\n';return true;};
    const auto objectPoint=[&](RuntimeWindowState& s,int id)->std::optional<POINT>{RuntimeSnapshotActiveStateScope active(s);
        const auto* o=findObjectById(s,id);if(!o)return std::nullopt;centerRuntimeCameraOnCell(s,client,{o->x,o->y});ensureRuntimeCameraForViewport(s,viewport);
        invalidateRuntimeObjectProjectionCache(s);const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        const auto hit=[&](POINT point){const auto* at=findProjectedObjectAtPoint(projected,point);return pointInRectInclusive(viewport,point)&&at&&at->object&&at->object->instanceId==id;};
        for(const auto& p:projected)if(p.object&&p.object->instanceId==id){for(const auto point:p.footprintHitCenters)if(hit(point))return point;
            for(int y=p.bounds.top;y<p.bounds.bottom;y+=2)for(int x=p.bounds.left;x<p.bounds.right;x+=2)if(hit({x,y}))return POINT{x,y};}
        return std::nullopt;};
    const auto forceAttack=[&](Input& in,int sourceId,int targetId){RuntimeSnapshotActiveStateScope active(in.s);applySelection(in.s,{sourceId},false,false);
        const auto point=objectPoint(in.s,targetId);if(!point)return fail("ordinary original body target is not actually hit-testable");
        SendMessageW(in.window,WM_MOUSEMOVE,MK_CONTROL,MAKELPARAM(point->x,point->y));
        SendMessageW(in.window,WM_LBUTTONDOWN,MK_CONTROL|MK_LBUTTON,MAKELPARAM(point->x,point->y));
        SendMessageW(in.window,WM_LBUTTONUP,MK_CONTROL,MAKELPARAM(point->x,point->y));++wmClicks;
        const auto b=in.s.behaviors.find(sourceId);return b!=in.s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Attack
            &&b->second.targetObjectId==targetId&&b->second.forceFire?true:fail("real forced ordinary Attack rejected: "+in.s.interactionStatus);};
    const auto runControls=[&](int hz){phase="original immunity/NotHuman/air controls";auto storage=make(hz,0);if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input))return false;
        const int deso=source(s,{77,80}),immune=add(s,"DESO",{80,80}),dog=add(s,"DOG",{81,80}),air=add(s,"JUMPJET",{80,81}),outside=add(s,"E1",{103,100});
        if(deso<0||immune<0||dog<0||air<0||outside<0)return false;
        const auto* dogRule=s.rules.findUnitType("InfantryType","DOG");const auto* airRule=s.rules.findUnitType("InfantryType","JUMPJET");
        const auto* immuneRule=s.rules.findUnitType("InfantryType","DESO");
        if(!dogRule||!dogRule->notHuman||dogRule->immuneToRadiation||!airRule||!airRule->jumpJet||!immuneRule||!immuneRule->immuneToRadiation
            ||!findObjectById(s,air)->airborne)return fail("original control flags/startup airborne state differ");
        const int desoHealth=findObjectById(s,deso)->health,immuneHealth=findObjectById(s,immune)->health,airHealth=findObjectById(s,air)->health,outsideHealth=findObjectById(s,outside)->health;
        bool dogDied=false,seenSite=false;const auto* dogBody=sprite(s,bodyImage(*dogRule));if(!dogBody)return false;
        if(!deploy(input,deso))return false;
        for(int n=0;n<hz*10;++n){std::optional<Frozen> dogBefore;if(const auto* d=findObjectById(s,dog))dogBefore=freeze(*d,*dogBody);
            if(!step(s))return false;seenSite=seenSite||site(s,deso);
            for(const auto pair:std::array<std::pair<int,int>,4>{{{deso,desoHealth},{immune,immuneHealth},{air,airHealth},{outside,outsideHealth}}}){
                const auto* body=findObjectById(s,pair.first);if(!body||body->health!=pair.second||death(s,pair.first))return fail("immune/air/outside original HP or death changed id="+std::to_string(pair.first));}
            if(!findObjectById(s,dog)&&!dogDied){const auto* fx=death(s,dog);
                if(!fx||!dogBefore||fx->animId=="NUKEDIE"||fx->renderIntent!="infantryDeath"||fx->warheadId!="RadSite"
                    ||fx->worldX!=dogBefore->x||fx->worldY!=dogBefore->y||fx->worldLevel!=dogBefore->z
                    ||fx->screenOffsetX||fx->screenOffsetY||fx->nativeFixedX||fx->nativeFixedY)
                    return fail("original NotHuman sequence acquired human canvas correction / lost physical point");dogDied=true;}
            if(!findObjectById(s,air)->airborne)return fail("original BalloonHover control unexpectedly touched ground");}
        if(!seenSite||!dogDied)return fail("original controls did not exercise actual RadSite / nonhuman lethal damage");
        // A separate genuine ordinary lethal attack uses original weapon/HP.
        // Original HTNK 120mm/AP has InfDeath=3: the authored S_BANG34 Anim
        // remains UnitDestruction, with zero radiation canvas correction.
        phase="ordinary InfDeath control real forced body Attack";auto normal=make(hz,0);if(!normal)return false;auto& n=*normal;Input normalInput{n};if(!open(normalInput))return false;
        int attacker=-1;{RuntimeSnapshotActiveStateScope active(n);auto* body=addRuntimeObjectInstanceForOwner(n,owners[0],RuntimeBuildKind::Unit,"HTNK",{77,80});
            const auto* type=n.rules.findUnitType("UnitType","HTNK");if(!body||!type||body->health!=type->strength)return fail("ordinary original HTNK startup unavailable");
            attacker=body->instanceId;body->mission="Sleep";}
        const auto* originalTank=n.rules.findUnitType("UnitType","HTNK");
        const auto* originalWeapon=n.rules.findWeaponType("120mm");const auto* originalWarhead=n.rules.findWarheadType("AP");
        if(!originalTank||originalTank->primaryWeapon!="120mm"||!originalWeapon||originalWeapon->warhead!="AP"
            ||!originalWarhead||originalWarhead->infDeath!=3||n.rules.getInfantryDeathAnim(3)!="S_BANG34"
            ||!n.art.findArtType("S_BANG34")||rulesIni.getValue("HTNK","Primary","")!="120mm"
            ||rulesIni.getValue("120mm","Warhead","")!="AP"||rulesIni.getValue("AP","InfDeath","")!="3")
            return fail("original HTNK/120mm/AP3/S_BANG34 declaration differs");
        const int target=add(n,"E1",{80,80});if(target<0||!forceAttack(normalInput,attacker,target))return false;bool ordinaryDeath=false;
        for(int f=0;f<hz*25;++f){const auto* body=findObjectById(n,target);std::optional<Frozen> previous;
            if(body)previous=freeze(*body,*gi);if(!step(n))return false;
            if(!findObjectById(n,target)){const auto* fx=death(n,target);
                const auto* corpse=findRuntimeObjectByIdIncludingInactive(n,target);
                const auto tombstone=std::find_if(n.deathWeapons.triggeredObjects().begin(),n.deathWeapons.triggeredObjects().end(),
                    [&](const auto& object){return object.objectId==target;});
                const auto count=std::count_if(n.impactEffects.begin(),n.impactEffects.end(),[&](const auto& effect){return effect.death&&effect.targetObjectId==target;});
                if(!fx||!previous||!corpse||corpse->alive||corpse->health!=0||corpse->mission!="Destroyed"
                    ||tombstone==n.deathWeapons.triggeredObjects().end()||tombstone->deathTick!=n.tick||tombstone->type!="E1"||count!=1
                    ||!fx->death||fx->lifecycleState!="UnitDestruction"||fx->renderIntent!="unitDestruction"||fx->animId!="S_BANG34"
                    ||fx->weaponId!="120mm"||fx->weaponSlot!="primary"||fx->projectileId!=originalWeapon->projectile||fx->warheadId!="AP"
                    ||fx->attackerObjectId!=attacker||fx->targetObjectId!=target||fx->projectileActorId<0||fx->damage<=0
                    ||fx->owner!=previous->object.owner||!fx->hasWorldPosition||fx->totalTicks<=1||fx->remainingTicks<=0
                    ||fx->totalTicks-fx->remainingTicks!=1
                    ||fx->worldX!=previous->x||fx->worldY!=previous->y||fx->worldLevel!=previous->z
                    ||fx->screenOffsetX||fx->screenOffsetY||fx->nativeFixedX||fx->nativeFixedY)
                    return fail("ordinary original HTNK/120mm/AP3/S_BANG34 death cause/lifecycle or zero-offset point differs: "+(fx?record(*fx):"absent"));
                ordinaryDeath=true;break;}}
        return ordinaryDeath?true:fail("original forced ordinary weapon did not naturally kill within its finite integration budget");};
    const auto run=[&](const std::string& name,const auto& callback){if(!filter.empty()&&filter!=name)return;
        label=name;phase="start";error.clear();std::cout<<"Radiation anchor BEGIN "<<label<<'\n';
        if(callback()){++cases;std::cout<<"Radiation anchor "<<label<<" PASS\n";}else{failures.push_back(error);std::cerr<<error<<'\n';}};
    run("parallel-human-sweep-30",[&](){return runSweep();});
    for(const auto& type:isolatedDeathWeapons)run("isolated-"+type+"-30",[&](){return runPrimary(type,30,2,false);});
    for(int hz:{30,60})for(int slot:{2,4,3})run("static-E1-"+std::to_string(hz)+"-slot"+std::to_string(slot),[&](){return runPrimary("E1",hz,slot,false);});
    for(int hz:{30,60})for(const std::string type:{"E1","ENGINEER","TANY"})run("moving-"+type+'-'+std::to_string(hz),[&](){return runPrimary(type,hz,2,true);});
    for(int hz:{30,60})run("original-controls-"+std::to_string(hz),[&](){return runControls(hz);});
    if(!filter.empty()&&cases==0&&failures.empty())return fail("unknown CASE "+filter);
    if(failures.empty()&&filter.empty()){
        for(int hz:{30,60})for(int slot:{2,4,3})if(!slotWitnesses.contains({hz,slot}))failures.push_back("missing true ground subcell death witness");
        if(ordinarySaves!=2||exactSaves!=2||movingRateWitnesses.size()!=2||bmps!=4||cooldownCodecChecks!=4||cooldownChecks<=0)
            failures.push_back("incomplete full-run checkpoint/moving/two-scale/cooldown artifact witnesses");}
    if(!failures.empty()){std::ostringstream combined;combined<<"Radiation death anchor failed "<<failures.size()<<" scene(s)";
        for(const auto& failure:failures)combined<<"\n"<<failure;error=combined.str();return false;}
    std::cout<<"Radiation death anchor passed cases="<<cases<<" registeredAffected="<<affected.size()+isolatedDeathWeapons.size()
        <<" originalDeathWeaponIsolated="<<isolatedDeathWeapons.size()<<" fullTicks="<<fullTicks
        <<" naturalDeaths="<<deaths<<" movingDeaths="<<movingDeaths<<" finiteFrames="<<frameChecks<<" originalPixelDraws="<<pixelChecks
        <<" coldTargetRemoval="<<coldChecks<<" ordinarySaves="<<ordinarySaves<<" canonicalExtensionSaves="<<exactSaves<<" paired="<<paired
        <<" independentCooldownChecks="<<cooldownChecks<<" isolatedCooldownCodecChecks="<<cooldownCodecChecks
        <<" WMkeys="<<wmKeys<<" WMclicks="<<wmClicks<<" BMP="<<bmps
        <<" nativeCanvas=rawHeader/crop affine60/120=independent NU59=empty ordinary=49+6 exact=49+6/authority"
        <<" sweepPixels=frames0/30/59 corePixels=all60 noPortableV15Envelope noEarListening\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
