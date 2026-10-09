#ifdef _MSC_VER
#pragma optimize("", off)
#endif
// Native PBEACON/RDRBEACN geometry and frame banks are independent of the
// runtime beacon helpers. The 15 native frames/s conversion and generic owner
// remap ramp are this engine's adaptations, not retail scanout measurements.
bool runRuntimePlayerBeaconSmoke(RuntimeWindowState& live, std::string& error) {
    const char* outputEnv = std::getenv("CONCORD_PLAYER_BEACON_OUTPUT");
    const char* caseEnv = std::getenv("CONCORD_PLAYER_BEACON_CASE");
    const std::filesystem::path output = std::filesystem::absolute(outputEnv && *outputEnv
        ? std::filesystem::path(outputEnv) : std::filesystem::path("build/issue21_player_beacon"));
    std::filesystem::create_directories(output);
    std::ofstream csv(output / "beacon-frames.csv");
    csv << "case,tick,hz,event,owner,x,y,level,selected,mapFrame,radarFrame,opaque,baseColors,remapColors\n";
    constexpr int width = 1024, height = 768;
    const RECT client{0,0,width,height}, viewport = runtimeMapViewport(client);
    const std::array<std::string,3> owners{"SkirmishPlayer1","SkirmishPlayer2","SkirmishPlayer3"};
    const std::array<COLORREF,3> colors{RGB(255,220,40),RGB(40,110,255),RGB(220,40,40)};
    std::string label, stage, hashFailure;
    int scenes=0, fullTicks=0, keys=0, clicks=0, paints=0, nativePixels=0, remapPixels=0;
    int frameChecks=0, radarChecks=0, privacyChecks=0, codecChecks=0, annotations=0, capacityChecks=0, bmps=0;
    const auto fail = [&](const std::string& why) { error="Player beacon " + label + " [" + stage + "]: " + why; return false; };
    if (!live.rulesLoaded || !live.artLoaded || !live.theaterLoaded) return fail("original startup resources unavailable");
    IniFile rulesIni;
    if (!rulesIni.loadFromFile(live.report.rulesPath)) return fail("actual original rules source unavailable");
    int landTile=-1; RuntimeMapRenderCache terrainCache;
    const auto metadata=runtimeTerrainMetadataContext(live);
    for (int tile=0;tile<static_cast<int>(live.theater.getTotalTileSlots());++tile) {
        TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
        const auto info=runtimeTerrainInfoForCell(live,cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell,metadata,terrainCache)==0) {landTile=tile;break;}
    }
    if (landTile<0) return fail("original flat native TMP unavailable");
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
        raw.insert(raw.end(),{0,static_cast<unsigned char>(x>=80&&x<=88&&y>=80&&y<=88?2:0),0});}
    u32(raw,0);const auto encoded=base64(pack(raw));
    const auto mapPath=(output/"beacon-native.map").generic_string();
    {std::ofstream map(mapPath,std::ios::binary);map<<"[Basic]\nName=NativePlayerBeacon\nMultiplayerOnly=yes\nNewINIFormat=4\n"
        "[Header]\nWidth=180\nHeight=180\nStartX=0\nStartY=0\nNumberStartingPoints=0\n"
        "[Map]\nTheater="<<live.report.map.theater<<"\nSize=0,0,90,90\nLocalSize=0,0,90,90\n"
        "[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n[IsoMapPack5]\n";
        for(std::size_t at=0;at<encoded.size();at+=70)map<<at/70+1<<'='<<encoded.substr(at,70)<<'\n';
        map.close();if(!map)return fail("native backed map write failed");}
    const auto make=[&](int hz,int viewer,bool chrono=false) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,live,false);
        s.config=live.config;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";
        s.config.defaultMapPath=mapPath;s.config.noCooldown=chrono;s.config.superweapons=chrono;
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
            WorldObjectInstance o;o.instanceId=id;o.mapId="Beacon"+std::to_string(id);o.type=type;o.owner=owner;o.kind=WorldObjectKind::Structure;
            o.x=cell.x;o.y=cell.y;o.cell=runtimeCellKey(o.x,o.y);o.level=terrainLevelAtRuntimeCell(s,o.x,o.y);o.alive=o.poweredOperational=true;
            o.health=runtimeMaxHealthForObject(s,o);o.mission="Guard";s.world.accessObjects().push_back(std::move(o));};
        for(int i=0;i<3;++i){const RuntimeCellPoint base=i==0?RuntimeCellPoint{54,54}:i==1?RuntimeCellPoint{54,112}:RuntimeCellPoint{112,54};
            add(10+i*10,"GAAIRC",base,owners[i]);add(11+i*10,"GAPOWR",{base.x+5,base.y},owners[i]);}
        if(chrono){add(40,"GACSPH",{60,59},owners[0]);add(41,"GAPOWR",{65,54},owners[0]);}
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.visionEnabled=true;s.playerCommunication={};s.playerLocalCommands={};s.selectedObjectIds.clear();
        s.ownerRemapColors.clear();s.neutralRemapOwners.clear();
        loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.owner=owners[viewer];s.economy.credits=20000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();s.humanHouseAuthorities.clear();s.playerCommands.alliances.clear();
        s.skirmishModeAlliesAllowed=s.skirmishModeAllyChangeAllowed=true;s.skirmishModeMustAlly=false;
        for(int i=0;i<3;++i){RuntimeConfig::PlayerSlot slot;slot.enabled=slot.human=true;slot.local=i==viewer;slot.country="Americans";
            slot.name="Beacon viewer "+std::to_string(i+1);slot.team="-";s.config.playerSlots.push_back(slot);
            const auto canonical=runtimeCanonicalHumanHouseOwner(owners[i]);s.skirmishCountryByOwner[canonical]="Americans";
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
        // B checks/cancels existing SW targeting. Initialize the normal module
        // before the UI/hash baseline so lazy startup is not misattributed to B.
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
    enum class Route{Direct,Offline,Captured};
    struct Input{
        RuntimeWindowState& s;HWND window=nullptr;WNDPROC previous=nullptr;RuntimeMultiplayerRuntimeBridge bridge{};
        Route route=Route::Offline;std::vector<RuntimeMultiplayerGameplayCommand> commands;
        ~Input(){s.multiplayerIntentRouter.setCaptureCallback({});s.multiplayerBridge=nullptr;
            if(window){if(GetCapture()==window)ReleaseCapture();SetWindowLongPtrW(window,GWLP_USERDATA,0);
                if(previous)SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));DestroyWindow(window);}
            releaseRuntimeBackBuffer(s.backBuffer);releaseRuntimeBackBuffer(s.sidebarStaticBuffer);releaseRuntimeBackBuffer(s.radarTerrainBuffer);}
    };
    const auto open=[&](Input& in,Route route){auto& s=in.s;in.route=route;s.multiplayerBridge=nullptr;s.offlineGameplayIntentRouting=false;
        s.multiplayerIntentRouter.setCaptureCallback({});
        if(route==Route::Offline){initializeRuntimeOfflineGameplayRouting(s);s.multiplayerIntentRouter.setCaptureCallback([&in](const auto& cmd,std::string& reason){
            in.commands.push_back(cmd);return dispatchRuntimeOfflineGameplayCommand(in.s,cmd,reason);});}
        else if(route==Route::Captured){s.multiplayerBridge=&in.bridge;s.multiplayerIntentRouter.setCaptureCallback([&in](const auto& cmd,std::string& reason){in.commands.push_back(cmd);reason.clear();return true;});}
        if(runtimeUsesGameplayIntentRouter(s)!=(route!=Route::Direct))return fail("actual routing facade differs");
        in.window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Original player beacon smoke",WS_POPUP,-30000,-30000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!in.window)return fail("hidden Win32 window failed");SetWindowLongPtrW(in.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        in.previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(in.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        HDC screen=GetDC(nullptr);const bool dib=screen&&ensureRuntimeBackBuffer(s.backBuffer,screen,width,height);if(screen)ReleaseDC(nullptr,screen);
        return in.previous&&dib?true:fail("real WndProc / 32bit DIB failed");};
    const auto key=[&](Input& in,int code,bool repeat=false){RuntimeSnapshotActiveStateScope active(in.s);
        SendMessageW(in.window,WM_KEYDOWN,code,LPARAM(1)|(repeat?(LPARAM(1)<<30):0));
        SendMessageW(in.window,WM_KEYUP,code,LPARAM(1)|(LPARAM(1)<<30)|(LPARAM(1)<<31));++keys;};
    const auto click=[&](Input& in,POINT p,bool right=false){RuntimeSnapshotActiveStateScope active(in.s);
        SendMessageW(in.window,WM_MOUSEMOVE,0,MAKELPARAM(p.x,p.y));SendMessageW(in.window,right?WM_RBUTTONDOWN:WM_LBUTTONDOWN,right?MK_RBUTTON:MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(in.window,right?WM_RBUTTONUP:WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));++clicks;};
    const auto pixel=[&](RuntimeWindowState& s,RuntimeCellPoint cell)->std::optional<POINT>{RuntimeSnapshotActiveStateScope active(s);
        centerRuntimeCameraOnCell(s,client,cell);ensureRuntimeCameraForViewport(s,viewport);invalidateRuntimeObjectProjectionCache(s);
        const POINT p=runtimeScreenPointForCell(s,viewport,cell,terrainLevelAtRuntimeCell(s,cell.x,cell.y));
        const auto picked=approximateCellFromScreenPoint(s,viewport,p);
        if(pointInRectInclusive(viewport,p)&&picked.x==cell.x&&picked.y==cell.y)return p;
        return std::nullopt;};
    const auto paint=[&](Input& in){RuntimeSnapshotActiveStateScope active(in.s);in.s.repaintRequested=true;
        // Exclude only the local software cursor from sprite probes; the real
        // input and all map/fog/sidebar/marker rendering remain unchanged.
        const bool mouseValid=in.s.mouseClientPointValid;in.s.mouseClientPointValid=false;
        InvalidateRect(in.window,nullptr,FALSE);SendMessageW(in.window,WM_PAINT,0,0);GdiFlush();++paints;
        in.s.mouseClientPointValid=mouseValid;
        return in.s.backBuffer.pixels&&in.s.backBuffer.width==width&&in.s.backBuffer.height==height?true:fail("actual WM_PAINT surface differs");};
    const auto pixels=[&](RuntimeWindowState& s){const auto* p=static_cast<const std::uint32_t*>(s.backBuffer.pixels);
        return std::vector<std::uint32_t>(p,p+width*height);};
    const auto readyRadar=[&](Input& in){for(int n=0;n<in.s.config.fixedTickHz*6;++n){if(!paint(in))return false;
        if(runtimeRadarAvailable(in.s)&&!runtimeRadarTransitionActive(in.s))return true;if(!step(in.s))return false;}
        return fail("original powered radar did not complete normal opening");};
    // A presentation-only removal provides the genuine map/fog/radar underlay
    // for absence/off-phase checks. No fabricated bitmap or black-frame fill.
    const auto without=[&](Input& in){auto beacons=in.s.playerCommunication.beacons;const auto selected=in.s.playerCommunication.selectedBeacon;
        in.s.playerCommunication.beacons.clear();in.s.playerCommunication.selectedBeacon.reset();
        const bool ok=paint(in);auto result=ok?pixels(in.s):std::vector<std::uint32_t>{};
        in.s.playerCommunication.beacons=std::move(beacons);in.s.playerCommunication.selectedBeacon=selected;return result;};
    const auto token=[&](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);std::string problem;const auto hash=runtimeStandaloneSimulationStateHash(s,problem);
        if(!problem.empty()&&hashFailure.empty())hashFailure=problem;
        return std::tuple<std::uint64_t,int,std::size_t>{hash,s.economy.credits,s.world.getObjects().size()};};
    const auto marker=[&](RuntimeWindowState& s,RuntimeCellPoint cell)->RuntimePlayerBeacon*{
        for(auto& b:s.playerCommunication.beacons)if(b.owner=="skirmishplayer1"&&b.cell.x==cell.x&&b.cell.y==cell.y)return &b;return nullptr;};
    const auto receive=[&](RuntimeWindowState& s,const RuntimeMultiplayerGameplayCommand& sent,const std::string& sender){
        RuntimeSnapshotActiveStateScope active(s);std::vector<std::uint8_t> bytes;RuntimeMultiplayerGameplayCommand decoded;std::string reason;
        if(!encodeRuntimeMultiplayerGameplayCommand(sent,bytes,reason)||!decodeRuntimeMultiplayerGameplayCommand(bytes,decoded,reason)||decoded!=sent)
            return fail("captured actual communication codec: "+reason);
        if(!prevalidateRuntimeMultiplayerGameplayCommand(s,sender,decoded,reason)||!applyRuntimeMultiplayerGameplayCommand(s,sender,decoded))
            return fail("authenticated actual receive: "+reason);++codecChecks;return true;};
    const auto place=[&](Input& in,RuntimeCellPoint cell){stage="B / actual terrain click";const auto p=pixel(in.s,cell);if(!p)return fail("ground bottompoint cannot be actually picked");
        const auto before=token(in.s);const auto count=in.commands.size();key(in,'B');
        if(!in.s.playerCommunication.beaconTargeting||in.s.playerCommunication.selectedBeacon)return fail("B did not enter initially unselected mode");
        key(in,'B',true);if(!in.s.playerCommunication.beaconTargeting)return fail("autorepeat toggled B");click(in,*p);
        if(in.s.playerCommunication.beaconTargeting||in.s.playerCommunication.selectedBeacon||token(in.s)!=before)
            return fail("actual Place changed authority or immediately selected cone");
        if(in.route==Route::Captured){if(in.commands.size()!=count+1||marker(in.s,cell))return fail("captured Place leaked optimistic beacon");
            const auto* body=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&in.commands.back().body);
            if(!body||body->kind!=RuntimePlayerCommunicationKind::PlaceBeacon||body->cell.x!=cell.x||body->cell.y!=cell.y)return fail("actual B payload differs");}
        else {const auto* b=marker(in.s,cell);if(!b||b->untilTick!=-1||b->level!=terrainLevelAtRuntimeCell(in.s,cell.x,cell.y))return fail("stored original level / no-expiry sentinel differs");}
        return true;};
    const auto select=[&](Input& in,RuntimeCellPoint cell){stage="actual base selection";const auto p=pixel(in.s,cell);if(!p)return fail("base cannot be picked");
        const auto before=token(in.s);click(in,*p);
        if(!in.s.playerCommunication.selectedBeacon||in.s.playerCommunication.selectedBeacon->x!=cell.x||in.s.playerCommunication.selectedBeacon->y!=cell.y||token(in.s)!=before)
            return fail("ground base selection failed or changed authority");return true;};
    const auto annotate=[&](Input& in,RuntimeCellPoint cell,const std::wstring& text,bool cancel=false){stage="actual Enter / Unicode / Enter";
        if(!select(in,cell))return false;const auto before=token(in.s);const auto* old=marker(in.s,cell);const auto oldText=old?old->text:std::string{};
        key(in,VK_RETURN);if(!in.s.playerCommunication.chatOpen||!in.s.playerCommunication.beaconEditing)return fail("selected marker did not enter annotation");
        // Clear via genuine Backspace and type through the normal WM_CHAR path.
        for(std::size_t n=0;n<widenUtf8(oldText).size();++n)key(in,VK_BACK);
        for(wchar_t ch:text){RuntimeSnapshotActiveStateScope active(in.s);SendMessageW(in.window,WM_CHAR,ch,1);}
        key(in,cancel?VK_ESCAPE:VK_RETURN);
        if(in.s.playerCommunication.chatOpen||in.s.playerCommunication.beaconEditing||token(in.s)!=before)return fail("text input changed authority / remained modal");
        if(in.route!=Route::Captured){const auto* b=marker(in.s,cell);const auto expected=cancel?oldText:runtimePlayerChatUtf8(text);
            if(!b||b->text!=expected)return fail("Unicode commit/cancel differs");}
        ++annotations;return true;};
    // Read ORIGINAL archive ranges, then decode that archive independently of
    // the beacon cache/loader. Expected pixels never call new beacon helpers.
    const auto mix=(std::filesystem::path(runtimeAssetRootPath(live.config))/"ra2"/"sidec01.mix").generic_string();
    const auto slice=[&](std::size_t at,std::size_t size){std::ifstream f(mix,std::ios::binary);Bytes b(size);f.seekg(static_cast<std::streamoff>(at));
        f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(size));if(!f)b.clear();return b;};
    const auto sha=[&](const Bytes& b){const auto hash=runtimeMultiplayerSnapshotSha256(b);std::string text;
        for(auto byte:hash){text+="0123456789ABCDEF"[byte>>4];text+="0123456789ABCDEF"[byte&15];}return text;};
    const auto originalCone=slice(861316,17496),originalRadar=slice(878820,448);
    if(originalCone.size()<8||originalRadar.size()<8||originalCone[6]!=20||originalCone[7]!=0||originalRadar[6]!=8||originalRadar[7]!=0
        ||toUpperAsciiLocal(sha(originalCone))!="78DB2DEE3317417C6E0710E3D2B630D5E71915DC5A0B4AB7E6B26990D18F8FFD"
        ||toUpperAsciiLocal(sha(originalRadar))!="DA12867D1A52F7481466F5CB25824D2C2B52E6F296E1BD3DA0DBB8AC49924912")
        return fail("original native PBEACON / RDRBEACN archive bytes differ");
    RuntimeMapRenderCache originalCache;std::array<RuntimeShpFrameImage,20> cones;std::array<RuntimeShpFrameImage,8> radars;
    std::string resourcePath,resourceError;
    for(int n=0;n<20;++n)if(!loadRuntimeShpFrameFromMixArchive(mix,originalCache,"pbeacon.shp",n,cones[n],resourcePath,resourceError)
        ||cones[n].globalWidth!=66||cones[n].globalHeight!=122||cones[n].pixels.size()!=std::size_t(cones[n].width*cones[n].height))return fail("original PBEACON frame: "+resourceError);
    for(int n=0;n<8;++n)if(!loadRuntimeShpFrameFromMixArchive(mix,originalCache,"rdrbeacn.shp",n,radars[n],resourcePath,resourceError)
        ||radars[n].globalWidth!=15||radars[n].globalHeight!=15)return fail("original RDRBEACN frame: "+resourceError);
    std::vector<RuntimePaletteColor> palette;
    if(!loadRuntimePaletteResource(metadata,originalCache,"unittem.pal",palette,resourcePath,resourceError)||palette.size()!=256)
        return fail("original UNIT TEM palette: "+resourceError);
    if(toUpperAsciiLocal(live.report.map.theater)!="TEMPERATE")return fail("this native UNIT TEM pixel fixture requires the registered temperate map");
    const auto paletteFile=std::filesystem::path(runtimeAssetRootPath(live.config))/"extracted_palettes"/"cache_unittem.pal";
    std::ifstream originalPalette(paletteFile,std::ios::binary);Bytes paletteBytes((std::istreambuf_iterator<char>(originalPalette)),std::istreambuf_iterator<char>());
    if(paletteBytes.size()!=768||toUpperAsciiLocal(sha(paletteBytes))!="ED785E62EED291480F3198DD44F6B656EBE3A9B75E9F641944D710ABC6BDE3E3")
        return fail("native UNIT TEM raw palette identity differs");
    // Independent raw 6-bit DAC expansion, not the new palette wrapper.
    for(std::size_t n=0;n<256;++n)if(palette[n].r!=paletteBytes[n*3]*4||palette[n].g!=paletteBytes[n*3+1]*4||palette[n].b!=paletteBytes[n*3+2]*4)
        return fail("loaded UNIT TEM colors differ from original PAL bytes");
    const auto rgb=[&](unsigned char index,COLORREF color){
        if(index>=16&&index<=31){constexpr std::array<int,16> ramp{63,59,55,52,48,44,41,37,33,30,26,22,19,15,11,8};
            const double gain=ramp[index-16]*4.0/255.0;return (std::uint32_t(std::lround(GetRValue(color)*gain))<<16)
                |(std::uint32_t(std::lround(GetGValue(color)*gain))<<8)|std::uint32_t(std::lround(GetBValue(color)*gain));}
        const auto c=palette[index];return (std::uint32_t(c.r)<<16)|(std::uint32_t(c.g)<<8)|c.b;};
    const auto frameIndex=[](const RuntimeWindowState& s,bool selected){return int((std::int64_t(s.tick)*15/std::max(1,s.config.fixedTickHz))%10)+(selected?10:0);};
    const auto radarIndex=[](const RuntimeWindowState& s){const int n=int((std::int64_t(s.tick)*15/std::max(1,s.config.fixedTickHz))%32);return n<8?n:-1;};
    const auto basePoint=[&](const RuntimeWindowState& s,const RuntimePlayerBeacon& b){
        const int x=int(std::llround((double(b.cell.x)-b.cell.y)*s.camera.tileWidth/2.0));
        const int y=int(std::llround((double(b.cell.x)+b.cell.y-b.level)*s.camera.tileHeight/2.0));
        return POINT{viewport.left+x-s.camera.worldOffsetX,viewport.top+y-s.camera.worldOffsetY};};
    const auto radarPoint=[&](const RuntimeWindowState& s,const RuntimePlayerBeacon& b){const auto projection=buildRuntimeRadarProjection(s);
        const auto layout=runtimeRadarLayout(runtimeRadarInteriorRect(runtimeRadarRect(runtimeSidebarRect(client))),projection);
        auto dxy=runtimeRadarDxyPoint(b.cell.x,b.cell.y,projection.fullMapWidth);dxy.y+=1.0;return runtimeRadarPointFromDxy(layout,projection,dxy);};
    const auto checkImage=[&](const std::vector<std::uint32_t>& actual,const RuntimeShpFrameImage& image,POINT center,COLORREF color,bool radar){
        const int left=center.x-image.globalWidth/2+image.offsetX,top=center.y-image.globalHeight/2+image.offsetY;
        int opaque=0,normal=0,remapped=0;
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){const auto index=image.pixels[y*image.width+x];if(index==0)continue;
            const int px=left+x,py=top+y;if(px<0||px>=width||py<0||py>=height)return fail("original sprite escaped actual surface");
            const auto expected=rgb(index,color),found=actual[py*width+px]&0xffffffU;
            if(found!=expected){std::ostringstream d;d<<"original "<<(radar?"radar":"cone")<<" pixel "<<px<<','<<py<<" index="<<unsigned(index)<<" expected="<<expected<<" actual="<<found;return fail(d.str());}
            ++opaque;if(index>=16&&index<=31)++remapped;else ++normal;
        }
        if(!opaque||(!radar&&(!normal||!remapped)))return fail("native sprite color coverage blank");
        nativePixels+=normal;remapPixels+=remapped;
        if(radar)++radarChecks;else ++frameChecks;return true;};
    const auto raster=[&](Input& in,RuntimeCellPoint cell,bool selected,bool checkRadar,const std::string& bmp=std::string{}){
        stage="original native pixels";auto* b=marker(in.s,cell);if(!b)return fail("expected marker missing");const auto frozen=*b;
        const auto blank=without(in);if(blank.size()!=width*height||!paint(in))return false;const auto actual=pixels(in.s);
        if(in.s.renderStats.visionShroudedCells<=0||in.s.renderCache.visionOverlay.nativeShroudFingerprint==0)
            return fail("actual native shroud compositor did not render this black-fog viewport");
        const int fi=frameIndex(in.s,selected),ri=radarIndex(in.s);const auto bp=basePoint(in.s,frozen);
        if(!checkImage(actual,cones[fi],bp,colors[0],false))return false;
        if(!frozen.text.empty()){
            if(bp.x<110||bp.x+110>=width||bp.y+65>=height)return fail("actual caption escaped surface");
            bool changed=false;for(int y=bp.y+16;y<bp.y+65;++y)for(int x=bp.x-110;x<bp.x+110;++x)
                changed|=(actual[y*width+x]&0xffffffU)!=(blank[y*width+x]&0xffffffU);
            if(!changed)return fail("committed annotation produced no actual caption pixels");
        }
        if(checkRadar){if(!runtimeRadarAvailable(in.s)||runtimeRadarTransitionActive(in.s))return fail("actual radar not ready");
            const auto rp=radarPoint(in.s,frozen);
            if(ri>=0){if(!checkImage(actual,radars[ri],rp,colors[0],true))return false;}
            else {if(rp.x<7||rp.x+7>=width||rp.y<7||rp.y+7>=height)return fail("actual radar center escaped surface");
                for(int y=rp.y-7;y<=rp.y+7;++y)for(int x=rp.x-7;x<=rp.x+7;++x)
                if((actual[y*width+x]&0xffffffU)!=(blank[y*width+x]&0xffffffU))return fail("radar original off phase still drew marker");++radarChecks;}}
        csv<<label<<','<<in.s.tick<<','<<in.s.config.fixedTickHz<<",native,"<<frozen.owner<<','<<frozen.cell.x<<','<<frozen.cell.y<<','<<frozen.level<<','<<selected<<','<<fi<<','<<ri<<','
            <<std::count_if(cones[fi].pixels.begin(),cones[fi].pixels.end(),[](unsigned char c){return c!=0;})<<','<<nativePixels<<','<<remapPixels<<'\n';
        if(!bmp.empty()){if(!writeRuntimeSmokeBmp((output/(bmp+".bmp")).string(),in.s.backBuffer.pixels,width,height,error))return false;++bmps;}return true;};
    const auto black=[&](RuntimeWindowState& s,RuntimeCellPoint cell){return !s.vision.isCellExploredBy(s.economy.owner,cell.x,cell.y)
        &&!s.vision.isCellVisibleTo(s.economy.owner,cell.x,cell.y);};
    const auto hidden=[&](Input& in,RuntimeCellPoint cell){stage="current alliance privacy";
        if(!pixel(in.s,cell))return fail("privacy camera base cannot be picked");const auto blank=without(in);if(!paint(in))return false;const auto actual=pixels(in.s);
        const RuntimePlayerBeacon expected{"skirmishplayer1",{cell.x,cell.y},-1,{},terrainLevelAtRuntimeCell(in.s,cell.x,cell.y)};
        const auto bp=basePoint(in.s,expected),rp=radarPoint(in.s,expected);
        if(bp.x<110||bp.x+110>=width||bp.y<61||bp.y+65>=height||rp.x<7||rp.x+7>=width||rp.y<7||rp.y+7>=height)
            return fail("privacy pixel rectangles escaped actual surface");
        for(int y=bp.y-61;y<bp.y+61;++y)for(int x=bp.x-33;x<bp.x+33;++x)
            if((actual[y*width+x]&0xffffffU)!=(blank[y*width+x]&0xffffffU))return fail("hidden marker leaked original cone / caption");
        for(int y=bp.y+16;y<bp.y+65;++y)for(int x=bp.x-110;x<bp.x+110;++x)
            if((actual[y*width+x]&0xffffffU)!=(blank[y*width+x]&0xffffffU))return fail("withdrawn marker leaked old caption");
        for(int y=rp.y-7;y<=rp.y+7;++y)for(int x=rp.x-7;x<=rp.x+7;++x)
            if((actual[y*width+x]&0xffffffU)!=(blank[y*width+x]&0xffffffU))return fail("hidden marker leaked original radar sprite");
        ++privacyChecks;return true;};
    const auto runOwn=[&](bool capacity){auto box=make(capacity?60:30,0);if(!box)return false;auto& s=*box;Input input{s};if(!open(input,capacity?Route::Offline:Route::Direct))return false;
        const RuntimeCellPoint cell{84,84};if(!black(s,cell)||!place(input,cell)||!black(s,cell))return fail("actual black-fog Place failed: "+error);
        std::set<int> unselectedFrames,selectedFrames;
        for(int n=0;n<s.config.fixedTickHz*2;++n){const int fi=frameIndex(s,false);if(!unselectedFrames.contains(fi)){
            if(!raster(input,cell,false,false,capacity&&unselectedFrames.empty()?label:std::string{}))return false;unselectedFrames.insert(fi);}if(unselectedFrames.size()==10)break;if(!step(s))return false;}
        if(unselectedFrames.size()!=10||!select(input,cell))return fail("unselected native frame-bank coverage incomplete");
        for(int n=0;n<s.config.fixedTickHz*2;++n){const int fi=frameIndex(s,true);if(!selectedFrames.contains(fi)){
            if(!raster(input,cell,true,false))return false;selectedFrames.insert(fi);}if(selectedFrames.size()==10)break;if(!step(s))return false;}
        if(selectedFrames.size()!=10)return fail("selected original frame-bank coverage incomplete");
        if(!annotate(input,cell,L"从这里进攻\u2605")||!annotate(input,cell,L"取消文字",true))return false;
        if(!capacity&&!raster(input,cell,true,false,label))return false;
        // Cone opaque pixels do not select: the bottom point is the native hit target.
        const auto p=pixel(s,cell);if(!p)return fail("selection point missing");click(input,{p->x,p->y-45});
        if(s.playerCommunication.selectedBeacon)return fail("upper cone pixels incorrectly selected bottompoint");
        if(!select(input,cell))return false;const auto count=s.playerCommunication.beacons.size();
        const auto selectedBefore=s.playerCommunication.selectedBeacon;const auto bareBefore=token(s);
        SendMessageW(input.window,WM_LBUTTONUP,0,MAKELPARAM(p->x+80,p->y));if(s.playerCommunication.beacons.size()!=count||s.playerCommunication.selectedBeacon!=selectedBefore||token(s)!=bareBefore)
            return fail("bare LMBUP changed selected marker or authority");
        if(capacity){if(!place(input,{94,84})||!place(input,{84,94}))return false;
            if(s.playerCommunication.beacons.size()!=3)return fail("three original slots missing");
            const auto commands=input.commands.size();const auto before=token(s);key(input,'B');
            if(s.playerCommunication.beaconTargeting||input.commands.size()!=commands||s.playerCommunication.beacons.size()!=3||token(s)!=before)return fail("fourth B did not refuse overflow without eviction");++capacityChecks;
            // No tick jump / fabricated TTL: complete ordinary simulation for >30s.
            for(int n=0;n<s.config.fixedTickHz*31;++n)if(!step(s))return false;
            if(s.playerCommunication.beacons.size()!=3||!marker(s,cell)||marker(s,cell)->text!="从这里进攻\u2605"||!black(s,cell))return fail("native no-expiry / black fog changed after >30s");++capacityChecks;
            if(!select(input,cell)||!raster(input,cell,true,false))return false;
        }
        if(!select(input,cell))return false;const auto before=token(s);key(input,VK_DELETE);
        if(marker(s,cell)||s.playerCommunication.selectedBeacon||token(s)!=before)return fail("actual Del did not remove selected own marker locally");
        if(capacity){if(!place(input,{94,94})||s.playerCommunication.beacons.size()!=3)return fail("delete did not free original slot");++capacityChecks;}
        else {const auto countBefore=s.playerCommunication.beacons.size();key(input,'B');click(input,*p,true);
            if(s.playerCommunication.beaconTargeting||s.playerCommunication.beacons.size()!=countBefore)return fail("actual right click failed to cancel B");key(input,'B');key(input,VK_ESCAPE);
            if(s.playerCommunication.beaconTargeting||s.runtimeMenuOpen)return fail("Esc B cancellation opened menu / left mode");}
        return true;};
    const auto runCaptured=[&](){auto own=make(30,0),ally=make(30,1),enemy=make(30,2);if(!own||!ally||!enemy)return false;
        Input sender{*own},friendView{*ally},enemyView{*enemy};if(!open(sender,Route::Captured)||!open(friendView,Route::Offline)||!open(enemyView,Route::Offline))return false;
        const RuntimeCellPoint cell{84,84};if(!place(sender,cell))return false;const auto placement=sender.commands.back();
        for(auto* state:{own.get(),ally.get(),enemy.get()})if(!receive(*state,placement,owners[0]))return false;
        if(!marker(*own,cell)||!marker(*ally,cell)||marker(*enemy,cell))return fail("authenticated three viewer visibility differs");
        if(!pixel(*ally,cell)||!readyRadar(friendView)||!raster(friendView,cell,false,true)||!hidden(enemyView,cell))return false;
        if(!annotate(sender,cell,L"盟友从这里进攻"))return false;const auto edit=sender.commands.back();
        for(auto* state:{own.get(),ally.get(),enemy.get()})if(!receive(*state,edit,owners[0]))return false;
        if(marker(*ally,cell)->text!="盟友从这里进攻"||marker(*enemy,cell))return fail("authenticated annotation privacy differs");
        if(!hidden(enemyView,cell)||!writeRuntimeSmokeBmp((output/(label+".bmp")).string(),enemy->backBuffer.pixels,width,height,error))return false;++bmps;
        // Authentication owns the marker. An ally cannot edit/delete its owner's slot.
        const auto friendCount=friendView.commands.size();if(!pixel(*ally,cell))return false;
        const auto pp=pixel(*ally,cell);click(friendView,*pp);key(friendView,VK_DELETE);
        if(ally->playerCommunication.selectedBeacon||friendView.commands.size()!=friendCount||!marker(*ally,cell))return fail("ally selected/deleted foreign marker");
        RuntimeMultiplayerGameplayCommand foreign=edit;auto& body=std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(foreign.body);body.text="foreign mutation";
        if(!receive(*ally,foreign,owners[1])||marker(*ally,cell)->text!="盟友从这里进攻")return fail("authenticated wrong owner changed marker");
        if(!select(sender,cell))return false;key(sender,VK_DELETE);const auto deletion=sender.commands.back();
        if(!marker(*own,cell))return fail("captured Del bypassed receive");
        for(auto* state:{own.get(),ally.get(),enemy.get()})if(!receive(*state,deletion,owners[0]))return false;
        if(marker(*own,cell)||marker(*ally,cell)||marker(*enemy,cell)||!hidden(friendView,cell))return fail("authenticated deletion did not converge");return true;};
    const auto runRadar=[&](){auto own=make(60,0,true),ally=make(60,1);if(!own||!ally)return false;Input sender{*own},view{*ally};
        if(!open(sender,Route::Captured)||!open(view,Route::Offline))return false;
        // Actual ready SW button, not forceReady or a fabricated selected field.
        stage="ready target exclusivity";for(int n=0;n<120;++n){const auto* snap=own->superWeapons.findSidebarSnapshot(owners[0],"ChronoSphereSpecial");
            if(snap&&snap->state==SuperWeaponRuntimeState::Ready)break;if(!step(*own))return false;}
        const auto* ready=own->superWeapons.findSidebarSnapshot(owners[0],"ChronoSphereSpecial");
        if(!ready||ready->state!=SuperWeaponRuntimeState::Ready)return fail("original source did not naturally become Ready");
        own->activeBuildTab=RuntimeBuildKind::Defense;const auto sidebar=runtimeSidebarRect(client);std::optional<POINT> button;
        for(int page=0;page<runtimeBuildPageCount(*own,sidebar)&&!button;++page){runtimeSetActiveBuildPage(*own,page);
            const int offset=static_cast<int>(runtimeVisibleBuildCatalogIndices(*own,sidebar).size());const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(*own,sidebar);
            for(std::size_t n=0;n<visible.size();++n)if(visible[n]->superWeaponId=="ChronoSphereSpecial"){const auto r=runtimeBuildButtonRect(sidebar,offset+static_cast<int>(n));button=POINT{(r.left+r.right)/2,(r.top+r.bottom)/2};break;}}
        if(!button)return fail("real ready sidebar button missing");click(sender,*button);
        if(!own->multiplayerIntentRouter.superWeapon())return fail("actual sidebar did not enter SW targeting");
        const auto sw=own->superWeapons.stateHash();const auto authority=token(*own);key(sender,'B');
        if(!own->playerCommunication.beaconTargeting||own->multiplayerIntentRouter.superWeapon()||own->superWeapons.stateHash()!=sw||token(*own)!=authority)
            return fail("B did not cancel targeting exclusively / consumed ready SW");key(sender,VK_ESCAPE);
        const RuntimeCellPoint cell{84,84};if(!place(sender,cell))return false;const auto placed=sender.commands.back();
        if(!receive(*own,placed,owners[0])||!receive(*ally,placed,owners[0]))return false;
        if(!annotate(sender,cell,L"黑幕中的盟友信标"))return false;const auto edited=sender.commands.back();if(!receive(*own,edited,owners[0])||!receive(*ally,edited,owners[0]))return false;
        if(!pixel(*ally,cell)||!black(*ally,cell)||!readyRadar(view))return fail("ally black fog / normal radar opening incomplete: "+error);
        std::set<int> frames,offPhases;bool wrote=false;
        for(int n=0;n<60*6;++n){if(!step(*ally))return false;const int phase=int((std::int64_t(ally->tick)*15/60)%32),ri=radarIndex(*ally);
            if(runtimeRadarAvailable(*ally)&&!runtimeRadarTransitionActive(*ally)
                &&(ri>=0?!frames.contains(ri):!offPhases.contains(phase))){if(!raster(view,cell,false,true,!wrote?label:std::string{}))return false;
                if(ri>=0)frames.insert(ri);else offPhases.insert(phase);wrote=true;}
            if(frames.size()==8&&offPhases.size()==24)break;}
        if(frames.size()!=8||offPhases.size()!=24||!black(*ally,cell))return fail("actual global radar cycle / black fog coverage incomplete");
        // Withdraw via the original diplomacy checkbox and authenticated offline route.
        stage="actual diplomacy withdrawal";key(view,VK_TAB);if(!ally->playerCommunication.diplomacyOpen)return fail("Tab did not open diplomacy");
        const auto rows=runtimeDiplomacyRows(*ally);int control=0;for(std::size_t n=1;n<rows.size();++n)if(rows[n].owner=="skirmishplayer1")control=1742+static_cast<int>(n);
        if(!control)return fail("ally's original diplomatic row missing");const auto r=runtimeDiplomacyControlRect(client,control);click(view,{(r.left+r.right)/2,(r.top+r.bottom)/2});key(view,VK_ESCAPE);
        if(runtimeOwnersAreAllied(*ally,owners[0],owners[1])||!marker(*ally,cell)||!hidden(view,cell))return fail("current withdrawal did not hide existing map/text/radar marker");
        const auto p=pixel(*ally,cell);click(view,*p);if(ally->playerCommunication.selectedBeacon)return fail("hidden old marker remained selectable");
        return true;};
    std::vector<std::string> failures;
    const auto scene=[&](const std::string& name,const auto& run){if(caseEnv&&*caseEnv&&name!=caseEnv)return;label=name;stage="fixture";hashFailure.clear();
        const bool ok=run();if(!ok)failures.push_back(error);else if(!hashFailure.empty()){fail("actual authority hash failed: "+hashFailure);failures.push_back(error);}
        else{++scenes;std::cout<<"Player beacon "<<name<<": PASS\n";}};
    scene("direct-own-30",[&](){return runOwn(false);});scene("offline-cap-60",[&](){return runOwn(true);});
    scene("captured-three-viewers-30",runCaptured);scene("allied-radar-60",runRadar);
    if(!failures.empty()){std::ostringstream out;for(const auto& message:failures)out<<message<<'\n';error=out.str();return false;}
    if(!scenes)return fail("CASE matched no scene");
    std::cout<<"Player beacon: passed "<<scenes<<" bounded scenes; fullTicks="<<fullTicks<<" keys="<<keys<<" clicks="<<clicks<<" actualPaints="<<paints
        <<" originalFrames="<<frameChecks<<" radarPhases="<<radarChecks<<" rawNonRemapPixels="<<nativePixels<<" remapPixels="<<remapPixels
        <<" annotations="<<annotations<<" capChecks="<<capacityChecks<<" privacy="<<privacyChecks<<" authenticatedCodec="<<codecChecks<<" BMP="<<bmps
        <<". Native assets + project clock/remap adaptation; CPU DIB evidence, not display scanout or live-LAN timing.\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
