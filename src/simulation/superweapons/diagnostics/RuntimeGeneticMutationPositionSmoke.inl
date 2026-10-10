// Real sidebar/map casts latch each old infantry body's foot point.  The
// oracle is captured before the cast, never from the replacement or effect.
bool runRuntimeGeneticMutationPositionSmoke(RuntimeWindowState& live, std::string& error) {
    const char* requested = std::getenv("CONCORD_SUPERWEAPON_ACTIVATION_AUDIO_CASE");
    const std::string filter = requested ? requested : "";
    if (!filter.empty() && filter.rfind("GeneticPosition-", 0) != 0) return true;
    const bool allCases=filter.empty()||filter=="GeneticPosition-all";
    const char* destination = std::getenv("CONCORD_SUPERWEAPON_ACTIVATION_AUDIO_OUTPUT");
    const std::filesystem::path root = std::filesystem::path(destination && *destination
        ? destination : "build/issue22_14_genetic_position") / "genetic_position";
    std::filesystem::create_directories(root / "native-original");
    IniFile rawRules, rawArt; RulesDatabase rules; ArtDatabase art;
    if (!rawRules.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(rawRules)
        || !rawArt.loadFromFile(live.report.artPath) || !art.loadFromIni(rawArt)) {
        error = "Genetic position: original rules/art unavailable"; return false;
    }
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Genetic position " + label + " [" + stage + "]: " + why; return false;
    };
    const auto* mutation = rules.findSuperWeaponType("GeneticConverterSpecial");
    const auto* brute = rules.findUnitType("infantry", "BRUTE");
    const auto* deathArt = art.findArtType("GENDEATH");
    if (!mutation || mutation->impactAnim != "GENDEATH" || mutation->mutationType != "BRUTE"
        || !brute || brute->strength != 200 || !deathArt || deathArt->makeInfantry != 0)
        return fail("original GeneticConverterSpecial/GENDEATH/BRUTE definitions");
    std::ofstream sources(root / "sources.csv"), effects(root / "effects.csv"), cases(root / "cases.csv"),
        checkpoints(root / "checkpoints.csv"), pixelsCsv(root / "pixels.csv");
    if (!sources || !effects || !cases || !checkpoints || !pixelsCsv) return fail("output streams");
    sources << std::setprecision(17); effects << std::setprecision(17); pixelsCsv << std::setprecision(17);
    sources << "case,tick,hz,source,role,type,hp,logical_x,logical_y,logical_level,logical_bridge,subcell,render,active,body_x,body_y,body_level,body_bridge,foot_x,foot_y,sprite_image,sprite_source,canvas_width,canvas_height,pivot_x,pivot_y,fixed_x,fixed_y\n";
    effects << "case,format,tick,source,remaining,total,delay,steps,frame,precise,world_x,world_y,world_level,actor_x,actor_y,actor_level,screen_x,screen_y,fixed_x,fixed_y,actor_screen_x,actor_screen_y,actor_fixed_x,actor_fixed_y\n";
    cases << "case,hz,bridge,victims,mutations,observed_ticks,paired_ticks,pixels,canonical,first_tick,expiry_tick,raw_frames,frame_delay,total,replacement_moved\n";
    checkpoints << "case,format,tick,hz,effects,file\n";
    pixelsCsv << "case,format,source,tick,frame,tile_width,tile_height,world_x,world_y,world_level,screen_x,screen_y,opaque,transparent,cold_warm,shp_source\n";
    int land = -1, water = -1;
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (land < 0 && info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) land = tile;
        if (water < 0 && info.water && !info.shore && !info.bridge) water = tile;
        if (land >= 0 && water >= 0) break;
    }
    const auto& overlays = rules.getOverlayTypes();
    const auto bridgeType = std::find_if(overlays.begin(), overlays.end(), [](const auto& type) { return type.id == "BRIDGE1"; });
    if (land < 0 || water < 0 || bridgeType == overlays.end() || bridgeType->image != "BRIDGE") return fail("original land/water/high bridge assets");
    const int bridgeOrdinal = static_cast<int>(bridgeType - overlays.begin());
    if (bridgeOrdinal != 24) return fail("original high bridge ordinal differs from raw inventory");
    const RECT client{0,0,1024,768}, viewport = runtimeMapViewport(client);
    const wchar_t* className = L"ConcordGeneticMutationPositionSmoke";
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = runtimeWindowProc; wc.hInstance = instance; wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail("native input class");
    struct ClassCleanup { HINSTANCE instance; const wchar_t* name; ~ClassCleanup() { UnregisterClassW(name, instance); } } classCleanup{instance,className};
    HWND window = CreateWindowExW(0,className,L"Genetic source position",WS_POPUP,0,0,1024,768,nullptr,nullptr,instance,nullptr);
    if (!window) return fail("native input window");
    struct WindowCleanup { HWND window; ~WindowCleanup() { SetWindowLongPtrW(window,GWLP_USERDATA,0);
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if (GetCapture()==window) ReleaseCapture(); DestroyWindow(window); } } windowCleanup{window};
    const auto left = [&](RuntimeWindowState& s, POINT point) {
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s,false); completeRuntimeWorldRefresh(s);
    };
    const auto describe = [](const WorldObjectInstance* o) {
        if (!o) return std::string("missing"); std::ostringstream out; out << std::setprecision(17)
            << '#' << o->instanceId << ':' << o->type << " HP=" << o->health << " alive=" << o->alive
            << " logical=" << o->x << ',' << o->y << ',' << o->level << ':' << o->onBridge
            << " render=" << o->renderX << ',' << o->renderY << ',' << o->renderLevel << ':' << o->renderOnBridge
            << " has=" << o->hasRenderPosition << " moving=" << o->renderMoveActive << " sub=" << o->subCell << " mission=" << o->mission;
        return out.str();
    };
    const auto body = [](const WorldObjectInstance& o) {
        const bool render = worldObjectHasGroundRenderPose(o);
        return std::array<double,3>{render ? o.renderX : double(o.x),render ? o.renderY : double(o.y),render ? o.renderLevel : double(o.level)};
    };
    const auto subcell = [](int slot) {
        // Independent original slots: do not use the production offset helper.
        if (slot==2) return std::array<double,2>{.25,-.25};
        if (slot==4) return std::array<double,2>{.25,.25};
        if (slot==3) return std::array<double,2>{-.25,.25};
        return std::array<double,2>{0,0};
    };
    const auto make = [&](int hz, bool bridge) {
        auto result = std::make_unique<RuntimeWindowState>(); auto& s=*result;
        seedRuntimeLoadScratch(s,live,false); s.config=live.config; s.config.fixedTickHz=hz;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.crates=s.config.shortGame=false;
        s.config.superweapons=true; s.config.gameMode="Battle"; s.config.playerCountry="YuriCountry";
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules=rules; s.art=art; s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.mapFacts={};
        s.mapFacts.theater=live.mapFacts.theater; s.mapFacts.lighting=live.mapFacts.lighting;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y=55;y<=124;++y) for (int x=55;x<=124;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x; cell.tileIndex=land; cell.subTileIndex=0; cell.level=bridge?4:0;
            if (bridge && x>=90 && x<=109) {
                cell.tileIndex=water; cell.level=0;
                if (y==94) { cell.hasOverlay=true; cell.overlayIndex=bridgeOrdinal; cell.overlayFrame=0; cell.overlayType="BRIDGE1"; cell.overlayImage="BRIDGE"; }
            }
            s.world.accessTerrainCells().push_back(cell);
        }
        int next=1;
        const auto building = [&](const BuildingType& type, RuntimeCellPoint p, const std::string& owner) {
            WorldObjectInstance o; o.instanceId=next++; o.kind=WorldObjectKind::Structure; o.type=type.id; o.owner=owner;
            o.x=p.x; o.y=p.y; o.cell=runtimeCellKey(p.x,p.y); o.health=type.strength; o.alive=o.poweredOperational=true; o.mission="Sleep";
            s.world.accessObjects().push_back(o);
        };
        for (const auto& type:rules.getBuildingTypes()) if (type.superWeapon==mutation->id || type.superWeapon2==mutation->id) { building(type,{62,62},"Commander"); break; }
        if (const auto* power=rules.findBuildingType("NANRCT")) { building(*power,{62,84},"Commander"); building(*power,{68,84},"Commander"); building(*power,{56,56},"SkirmishPlayer2"); }
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner="Commander"; s.economy.credits=10000; s.economy.techLevel=10; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        RuntimeConfig::PlayerSlot local; local.enabled=local.human=local.local=true; local.name="Commander"; local.country="YuriCountry"; local.color="Gold";
        RuntimeConfig::PlayerSlot enemy; enemy.enabled=true; enemy.human=enemy.local=false; enemy.name="Quiet hostile"; enemy.country="Russians"; enemy.color="Red";
        s.config.playerSlots={local,enemy}; s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"]="YuriCountry"; s.skirmishCountryByOwner["skirmishplayer2"]="Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority authority; authority.owner="Commander"; authority.canonicalOwner="commander"; authority.country="YuriCountry"; authority.localFacade=true;
        s.humanHouseAuthorities.push_back(authority);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeSuperWeaponsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); auto context=runtimeSuperWeaponContext(s);
            context.emit=runtimeSuperWeaponEventSink(s); s.superWeapons.tick(context); completeRuntimeWorldRefresh(s); }
        return result;
    };
    const auto add = [&](RuntimeWindowState& s,const std::string& type,RuntimeCellPoint p) {
        const auto* original=rules.findUnitType("infantry",type); if (!original) return -1;
        auto context=runtimeSuperWeaponContext(s); WorldObjectInstance o; o.instanceId=context.allocateObjectId(); o.kind=WorldObjectKind::Infantry;
        o.type=type; o.owner="Commander"; o.x=p.x; o.y=p.y; o.cell=runtimeCellKey(p.x,p.y); o.health=original->strength; o.alive=true; o.mission="Sleep";
        initializeRuntimeObjectAmmo(o,runtimeAmmoRulesForObject(s.rules,o)); runtimeInitializeModExtensions(o,s.rules,s.config.fixedTickHz);
        const int id=o.instanceId; s.world.accessObjects().push_back(o); initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        assignRuntimeInfantrySubcellAtCurrentLocation(s,*findMutableObjectById(s,id)); return id;
    };
    const auto move = [&](RuntimeWindowState& s,int id,RuntimeCellPoint goal) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds={id};
        return dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,goal) && s.lastCommand.acceptedCount==1;
    };
    // Read original bytes from MIX independently of the production frame cache.
    const auto originalImage = [&](const std::string& image,std::vector<std::uint8_t>& bytes,std::string& source) {
      const std::string filename=toLowerAsciiLocal(image)+".shp";
      for (const char* directory:{"ra2md","ra2"}) {
        const auto dir=std::filesystem::path(runtimeAssetRootPath(live.config))/directory;
        if (!std::filesystem::is_directory(dir)) continue;
        std::vector<std::filesystem::path> archives;
        for (const auto& file:std::filesystem::directory_iterator(dir)) if(file.is_regular_file() && toLowerAsciiLocal(file.path().extension().string())==".mix") archives.push_back(file.path());
        std::sort(archives.begin(),archives.end());
        for (const auto& path:archives) { MixArchive archive; if(!archive.loadFromPath(path)) continue; const auto* entry=archive.findEntryByName(filename);
            if(!entry)continue; if(!archive.readEntryData(*entry,bytes,error))return fail("original SHP read: "+error); source=pathString(path)+"!"+filename; break; }
        if(!source.empty())break;
      }
      if(bytes.size()<8)return fail("original MIX SHP header not found: "+filename);
      const auto copy=root/"native-original"/filename;std::ofstream file(copy,std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return file?true:fail("original byte snapshot: "+filename);
    };
    std::vector<std::uint8_t> shpBytes; std::string shpSource;
    const std::string image = trimAsciiLocal(rawArt.getValue("GENDEATH","Image","GENDEATH"));
    const std::string filename=toLowerAsciiLocal(image)+".shp";
    if(!originalImage(image,shpBytes,shpSource))return false;
    if(shpBytes.size()<8)return fail("original GENDEATH MIX header not found");
    const int rawFrameCount=int(shpBytes[6])+(int(shpBytes[7])<<8);
    if(rawFrameCount<40||!rawArt.getValue("GENDEATH","Rate","").empty()
        ||toLowerAsciiLocal(rawArt.getValue("GENDEATH","Shadow","yes"))!="no")return fail("original complete GENDEATH frames/default Rate/no shadow contract");
    const auto shpPath=root/"native-original"/filename;
    std::map<int,RuntimeShpFrameImage> decoded;
    const auto frameImage = [&](int frame,RuntimeShpFrameImage& output) {
        if (const auto found=decoded.find(frame);found!=decoded.end()) { output=found->second; return true; }
        if(!decodeRuntimeShpFrameFile(pathString(shpPath),frame,output,error))return fail("raw GENDEATH frame "+std::to_string(frame)+": "+error);
        decoded.emplace(frame,output); return true;
    };
    struct SourceCanvas { std::string image,source; int width=0,height=0; std::array<int,4> pivot{}; };
    std::map<std::string,SourceCanvas> canvases;
    const auto sourceCanvas = [&](const std::string& type,SourceCanvas& output) {
        if(const auto found=canvases.find(type);found!=canvases.end()){output=found->second;return true;}
        const std::string artId=trimAsciiLocal(rawRules.getValue(type,"Image",type));
        output.image=trimAsciiLocal(rawArt.getValue(artId,"Image",artId));std::vector<std::uint8_t> bytes;
        if(!originalImage(output.image,bytes,output.source))return false;
        output.width=int(bytes[2])+(int(bytes[3])<<8);output.height=int(bytes[4])+(int(bytes[5])<<8);
        if(output.width<=0||output.width>4096||output.height<=0||output.height>4096)return fail("original source SHP global canvas: "+type);
        // Original ground SHP global canvas: left=-floor(W/2),
        // top=tileHeight/2-H. Its integer midpoint gives the body pivot.
        // The bitmap height is native fixed pixels; only the half-tile term
        // scales, so the two-scale affine correction is -ceil(H/2).
        const int halfCeiling=output.height-output.height/2;
        output.pivot={0,15-halfCeiling,0,-halfCeiling};canvases.emplace(type,output);return true;
    };
    struct Victim { int id; std::string role; WorldObjectInstance old; std::array<double,3> foot; SourceCanvas canvas; };
    int completed=0,totalPixels=0,totalPaired=0;
    for(int hz:{30,60})for(bool bridge:{false,true}) {
        label="GeneticPosition-"+std::string(bridge?"highbridge":"ground")+'-'+std::to_string(hz);
        if(!allCases&&filter!=label)continue;
        stage="native fixture and real Move/Stop"; auto storage=make(hz,bridge); auto& s=*storage; RuntimeSnapshotActiveStateScope active(s);
        std::vector<std::pair<int,std::string>> roles;
        for(int n=0;n<3;++n)roles.emplace_back(add(s,"E1",{96,94}),"same-cell-"+std::to_string(n));
        const int stopped=add(s,"INIT",{92,94}),moving=add(s,"ENGINEER",{94,94}); roles.emplace_back(stopped,"stopped-fractional"); roles.emplace_back(moving,"moving-fractional");
        if(std::any_of(roles.begin(),roles.end(),[](const auto& row){return row.first<0;}))return fail("registered victim types");
        if(!move(s,stopped,{93,94}))return fail("real INIT Move rejected");
        bool reached=false;
        for(int n=0;n<hz*10;++n) { step(s); const auto* o=findObjectById(s,stopped); if(o&&o->renderMoveActive&&o->hasRenderPosition&&std::abs(o->renderX-std::round(o->renderX))>.12) { reached=true; break; } }
        if(!reached)return fail("natural stopped sample never became fractional: "+describe(findObjectById(s,stopped)));
        s.selectedObjectIds={stopped}; const auto stoppedBefore=*findObjectById(s,stopped);
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)||s.lastCommand.acceptedCount!=1
            ||body(*findObjectById(s,stopped))!=body(stoppedBefore))return fail("real Stop snapped source body");
        if(!move(s,moving,{95,94}))return fail("real ENGINEER Move rejected");
        reached=false; for(int n=0;n<hz*10;++n) { step(s);const auto* o=findObjectById(s,moving);if(o&&o->renderMoveActive&&o->hasRenderPosition&&std::abs(o->renderX-std::round(o->renderX))>.12){reached=true;break;} }
        if(!reached)return fail("natural moving sample never became fractional: "+describe(findObjectById(s,moving)));
        stage="real sidebar targeting"; auto context=runtimeSuperWeaponContext(s); context.emit=runtimeSuperWeaponEventSink(s);
        if(!s.superWeapons.forceReady("Commander",mutation->id,&context))return fail("original power forceReady"); step(s);
        s.activeBuildTab=RuntimeBuildKind::Defense; const RECT sidebar=runtimeSidebarRect(client);std::optional<POINT> button;
        for(int page=0;page<runtimeBuildPageCount(s,sidebar)&&!button;++page) { runtimeSetActiveBuildPage(s,page);
            const int offset=static_cast<int>(runtimeVisibleBuildCatalogIndices(s,sidebar).size());const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(s,sidebar);
            for(std::size_t n=0;n<visible.size();++n)if(visible[n]->superWeaponId==mutation->id){const auto r=runtimeBuildButtonRect(sidebar,offset+static_cast<int>(n));button=POINT{(r.left+r.right)/2,(r.top+r.bottom)/2};break;} }
        if(!button)return fail("original sidebar button missing");left(s,*button);
        if(!s.multiplayerIntentRouter.superWeapon()||s.multiplayerIntentRouter.superWeapon()->superWeaponId!=mutation->id)return fail("real sidebar did not begin targeting: "+s.interactionStatus);
        std::vector<Victim> victims;std::set<int> slots;
        for(const auto& [id,role]:roles) { const auto* o=findObjectById(s,id);if(!o||!o->alive)return fail("source missing before cast");
            const auto b=body(*o);const auto offset=subcell(o->subCell);const bool onBridge=worldObjectHasGroundRenderPose(*o)?o->renderOnBridge:o->onBridge;
            if(b[2]!=(bridge?4:0)||onBridge!=bridge)return fail("source did not naturally occupy expected native deck: "+describe(o));
            if(role.rfind("same-cell-",0)==0)slots.insert(o->subCell);
            if(role=="moving-fractional"&&(!o->renderMoveActive||!o->hasRenderPosition||std::abs(b[0]-std::round(b[0]))<1e-4))return fail("cast lost real moving source");
            if(role=="stopped-fractional"&&(!worldObjectHasStoppedGroundPose(*o)||o->renderMoveActive))return fail("cast lost real stopped source");
            SourceCanvas canvas;if(!sourceCanvas(o->type,canvas))return false;
            victims.push_back({id,role,*o,{b[0]+offset[0],b[1]+offset[1],b[2]},canvas});
            sources<<label<<','<<s.tick<<','<<hz<<','<<id<<','<<role<<','<<o->type<<','<<o->health<<','<<o->x<<','<<o->y<<','<<o->level<<','<<o->onBridge<<','<<o->subCell<<','<<o->hasRenderPosition<<','<<o->renderMoveActive
                <<','<<b[0]<<','<<b[1]<<','<<b[2]<<','<<onBridge<<','<<b[0]+offset[0]<<','<<b[1]+offset[1]
                <<','<<canvas.image<<','<<canvas.source<<','<<canvas.width<<','<<canvas.height<<','<<canvas.pivot[0]<<','<<canvas.pivot[1]<<','<<canvas.pivot[2]<<','<<canvas.pivot[3]<<'\n';
        }
        if(slots!=std::set<int>{2,3,4})return fail("three same-cell victims did not use distinct original subcells");
        centerRuntimeCameraOnCell(s,client,{94,94});ensureRuntimeCameraForViewport(s,viewport);
        const POINT anchor=runtimeScreenPointForCell(s,viewport,{94,94},terrainLevelAtRuntimeCell(s,94,94));std::optional<POINT> picked;
        for(int dy=0;dy<=s.camera.tileHeight/2&&!picked;++dy)for(int dx:{0,-1,1}) { POINT p{anchor.x+dx,anchor.y+dy};const auto cell=approximateCellFromScreenPoint(s,viewport,p);
            if(pointInRectInclusive(viewport,p)&&cell.x==94&&cell.y==94){picked=p;break;} }
        if(!picked)return fail("native map has no visible target interior");left(s,*picked);
        if(s.multiplayerIntentRouter.superWeapon())return fail("real map click did not submit cast: "+s.interactionStatus);
        // Effects applies the cast before ground behavior advancement. Thus the
        // prior completed tick's five body snapshots are exact source authority.
        step(s);stage="first transformation frame";
        const int firstTick=s.tick,frameDelay=hz/15,expectedTotal=rawFrameCount*frameDelay,expiryTick=firstTick+expectedTotal;
        std::set<int> oldIds,newIds;for(const auto& v:victims)oldIds.insert(v.id);
        for(const auto& o:s.world.getObjects())if(o.alive&&o.type=="BRUTE") { if(o.health!=200||oldIds.contains(o.instanceId)||!newIds.insert(o.instanceId).second)return fail("full-health independent replacement identity"); }
        if(newIds.size()!=5)return fail("real cast did not produce five new Brutes");
        const auto mutationEffects = [](const RuntimeWindowState& q) { std::vector<const RuntimeImpactEffect*> result;
            for(const auto& e:q.impactEffects)if(e.animId=="GENDEATH"&&e.renderIntent=="superWeaponEffect"&&e.remainingTicks>0)result.push_back(&e);return result; };
        int observedFrames=0,pixelChecks=0,pairedTicks=0;
        const auto inspect = [&](const RuntimeWindowState& q,const char* format,bool allowExpired) {
            const int expectedRemaining=expectedTotal-(q.tick-firstTick);
            const auto actual=mutationEffects(q);
            if(expectedRemaining<=0)return actual.empty()&&allowExpired?true:fail(std::string(format)+" GENDEATH survived original SHP deadline");
            if(actual.size()!=victims.size())return fail(std::string(format)+" GENDEATH count="+std::to_string(actual.size())+" expected=5");
            std::set<std::size_t> matched;
            for(const auto* e:actual) { std::size_t index=victims.size();for(std::size_t n=0;n<victims.size();++n)if(std::abs(e->worldX-victims[n].foot[0])<1e-8&&std::abs(e->worldY-victims[n].foot[1])<1e-8&&std::abs(e->worldLevel-victims[n].foot[2])<1e-8){index=n;break;}
                if(!e->hasWorldPosition||index==victims.size()||!matched.insert(index).second){std::ostringstream out;out<<std::setprecision(17)<<format<<" effect has wrong/duplicate old body "<<e->worldX<<','<<e->worldY<<','<<e->worldLevel<<" precise="<<e->hasWorldPosition;return fail(out.str());}
                const auto actor=makeRuntimeImpactRenderActor(*e);if(actor.x!=e->worldX||actor.y!=e->worldY||actor.level!=e->worldLevel)return fail("render snapshot changed frozen foot XYZ");
                const auto& pivot=victims[index].canvas.pivot;
                if(e->screenOffsetX!=pivot[0]||e->screenOffsetY!=pivot[1]||e->nativeFixedX!=pivot[2]||e->nativeFixedY!=pivot[3]
                    ||actor.screenOffsetX!=pivot[0]||actor.screenOffsetY!=pivot[1]||actor.nativeFixedX!=pivot[2]||actor.nativeFixedY!=pivot[3])
                    return fail(std::string(format)+" old source raw canvas pivot changed for #"+std::to_string(victims[index].id));
                const int frame=runtimeImpactAnimationFrameForStep(*e,(e->totalTicks-e->remainingTicks)/std::max(1,e->animationFrameDelayTicks));
                if(e->totalTicks!=expectedTotal||e->remainingTicks!=expectedRemaining||e->animationFrameDelayTicks!=frameDelay
                    ||e->animationSequenceSteps!=rawFrameCount||frame!=(q.tick-firstTick)/frameDelay
                    ||(e->frameIndex>=0&&e->frameIndex!=frame))return fail(std::string(format)+" original SHP frame/decrement/deadline differs at tick "+std::to_string(q.tick));
                effects<<label<<','<<format<<','<<q.tick<<','<<victims[index].id<<','<<e->remainingTicks<<','<<e->totalTicks<<','<<e->animationFrameDelayTicks<<','<<e->animationSequenceSteps<<','<<frame<<','<<e->hasWorldPosition<<','<<e->worldX<<','<<e->worldY<<','<<e->worldLevel<<','<<actor.x<<','<<actor.y<<','<<actor.level
                    <<','<<e->screenOffsetX<<','<<e->screenOffsetY<<','<<e->nativeFixedX<<','<<e->nativeFixedY<<','<<actor.screenOffsetX<<','<<actor.screenOffsetY<<','<<actor.nativeFixedX<<','<<actor.nativeFixedY<<'\n';
            }
            for(const auto& v:victims)if(findObjectById(q,v.id))return fail("old source remained alive");
            return true;
        };
        if(!inspect(s,"original",false))return false;
        const auto restore = [&]() {
            std::ostringstream plain,extended,normalized;IniFile base,snapshot;
            if(!writeRuntimeStateToStream(s,plain,error)||!base.loadFromString(plain.str()))return std::unique_ptr<RuntimeWindowState>{};
            if(!writeRuntimeStateToStream(s,extended,error)||!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!snapshot.loadFromString(extended.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot,normalized,error)||!snapshot.loadFromString(normalized.str()))return std::unique_ptr<RuntimeWindowState>{};
            const std::string format="canonical-extension",name=label+'-'+format+".ini";
            {std::ofstream file(root/name,std::ios::binary);file<<normalized.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
            auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);RuntimeSnapshotActiveStateScope activeCopy(*copy);
            // V14 omits GENDEATH. Clear the scratch's old presentation before
            // loading so the extension must actually reconstruct all five.
            copy->impactEffects.clear();
            if(!materializeRuntimeLoadStateFromIni(*copy,base,error))return std::unique_ptr<RuntimeWindowState>{};
            copy->impactEffects.clear();
            if(!materializeRuntimeMultiplayerSnapshotExtension(*copy,snapshot,error))return std::unique_ptr<RuntimeWindowState>{};
            checkpoints<<label<<','<<format<<','<<s.tick<<','<<hz<<",5,"<<name<<'\n';return copy;
        };
        auto canonical=restore();if(!canonical)return fail("strict context-v14/canonical-extension restore: "+error);
        if(!inspect(*canonical,"ordinary14+canonical-extension",false))return false;
        // Move actual replacements through player commands; the old effects
        // must remain at their original bodies while their new owners leave.
        const int movingReplacement=*newIds.begin();
        for(auto* q:{&s,canonical.get()})if(!move(*q,movingReplacement,{98,94}))return fail("replacement real Move rejected");
        bool replacementMoved=false,restoredReplacementMoved=false;
        const auto pixelCheck = [&](RuntimeWindowState& q,const RuntimeImpactEffect& effect,int sourceId,const char* format,bool preview) {
            auto actor=makeRuntimeImpactRenderActor(effect);actor.owner.clear();const int frame=runtimeImpactAnimationFrameForStep(effect,
                (effect.totalTicks-effect.remainingTicks)/std::max(1,effect.animationFrameDelayTicks));actor.frameIndex=frame;
            RuntimeShpFrameImage raw;if(!frameImage(frame,raw))return false;
            const auto victim=std::find_if(victims.begin(),victims.end(),[&](const auto& row){return row.id==sourceId;});
            if(victim==victims.end())return fail("pixel check lacks independent old source");
            const auto& pivot=victim->canvas.pivot;
          for(int tile:{60,120}) {
            constexpr int width=180,height=180;constexpr std::uint32_t background=0x005c6b49;
            std::vector<std::uint32_t> actual(width*height,background);
            RuntimeMapRenderContext context;context.rules=&q.rules;context.art=&q.art;context.theater=&q.theater;context.assetRootPath=runtimeAssetRootPath(q.config);
            context.structureRootPath=runtimeStructureRootPath(q.config,q.report.map);context.palettePath=runtimePalettePath(q.config,q.report.map);context.structurePalettePath=runtimeStructurePalettePath(q.config,q.report.map);
            context.fixedTickHz=hz;context.animationTick=q.tick;context.surfacePixels=actual.data();context.surfaceWidth=width;context.surfaceHeight=height;context.effectOnlyProbe=true;
            std::vector<RuntimeImpactRenderActor> actors{actor};context.impactActors=&actors;
            RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=tile;camera.tileHeight=tile/2;
            const auto projected=runtimeProjectCellXY(actor.x,actor.y,actor.level,camera);
            camera.worldOffsetX=projected.x-width/2;camera.worldOffsetY=projected.y-height/2;const RECT bounds{0,0,width,height};
            GameWorld empty=q.world;empty.accessObjects().clear();auto cache=std::make_unique<RuntimeMapRenderCache>();auto fixedCamera=camera;
            const auto stats=drawRuntimeMap(nullptr,bounds,empty,context,fixedCamera,*cache);
            if(!stats.rendered||stats.impactActorDrawn!=1||stats.impactAnimMissing)return fail("original GENDEATH renderer missing");
            const auto cold=actual;std::fill(actual.begin(),actual.end(),background);fixedCamera=camera;drawRuntimeMap(nullptr,bounds,empty,context,fixedCamera,*cache);
            if(actual!=cold)return fail("cold/warm GENDEATH pixels changed");
            // GENDEATH keeps its own centered canvas. Move that canvas to the
            // old source's independently decoded global body midpoint. Fixed
            // bitmap pixels do not scale with the world's half-tile offset.
            const int centerX=width/2+static_cast<int>(std::lround(pivot[2]+(pivot[0]-pivot[2])*tile/60.0));
            const int centerY=height/2+static_cast<int>(std::lround(pivot[3]+(pivot[1]-pivot[3])*camera.tileHeight/30.0));
            const int left=centerX-raw.globalWidth/2+raw.offsetX,top=centerY-raw.globalHeight/2+raw.offsetY;
            int opaque=0,transparent=0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const bool inside=x>=left&&y>=top&&x<left+raw.width&&y<top+raw.height;
                const bool wanted=inside&&raw.pixels[(y-top)*raw.width+x-left]!=0;const bool visible=(actual[y*width+x]&0xffffffU)!=background;
                if(wanted!=visible)return fail("original SHP canvas/foot opacity differs frame="+std::to_string(frame)+" pixel="+std::to_string(x)+','+std::to_string(y));
                opaque+=wanted;transparent+=!wanted;}
            if(opaque<8)return fail("original transformation frame unexpectedly invisible");
            pixelsCsv<<label<<','<<format<<','<<sourceId<<','<<q.tick<<','<<frame<<','<<tile<<','<<camera.tileHeight<<','<<actor.x<<','<<actor.y<<','<<actor.level<<','<<centerX<<','<<centerY<<','<<opaque<<','<<transparent<<",1,"<<shpSource<<'\n';
            if(preview&&!writeRuntimeSmokeBmp(pathString(root/(label+"-source"+std::to_string(sourceId)+"-tile"+std::to_string(tile)+"-raw-anchor.bmp")),actual.data(),width,height,error))return fail("pixel preview write");
            ++pixelChecks;
          }
          return true;
        };
        for(const auto* e:mutationEffects(s)) { int id=-1;for(const auto& v:victims)if(std::abs(v.foot[0]-e->worldX)<1e-8&&std::abs(v.foot[1]-e->worldY)<1e-8)id=v.id;
            if(!pixelCheck(s,*e,id,"original",true))return false; }
        for(const auto* e:mutationEffects(*canonical)) { int id=-1;for(const auto& v:victims)if(std::abs(v.foot[0]-e->worldX)<1e-8&&std::abs(v.foot[1]-e->worldY)<1e-8)id=v.id;
            if(!pixelCheck(*canonical,*e,id,"ordinary14+canonical-extension",false))return false; }
        if(!drawRuntimeStateOffscreenAtCell(s,{94,94},640,480,error,pathString(root/(label+"-full-map.bmp")),false))return fail("actual map preview: "+error);
        stage="frozen native sequence and restored continuation";
        for(int n=0;n<hz*20;++n) {
            step(s);step(*canonical);++pairedTicks;
            if(!inspect(s,"original",true)||!inspect(*canonical,"ordinary14+canonical-extension",true))return false;
            const auto a=mutationEffects(s),c=mutationEffects(*canonical);
            if(a.size()!=c.size())return fail("restored effect natural expiry diverged");
            for(std::size_t i=0;i<a.size();++i)if(a[i]->remainingTicks!=c[i]->remainingTicks
                ||a[i]->totalTicks!=c[i]->totalTicks)return fail("restored animation deadline changed");
            const auto* replacement=findObjectById(s,movingReplacement);if(replacement&&replacement->hasRenderPosition&&replacement->renderMoveActive)replacementMoved=true;
            const auto* restoredReplacement=findObjectById(*canonical,movingReplacement);
            if(restoredReplacement&&restoredReplacement->hasRenderPosition&&restoredReplacement->renderMoveActive)restoredReplacementMoved=true;
            if(a.empty())break;++observedFrames;
            if(n==hz/2)for(const auto* e:a) {int id=-1;for(const auto& v:victims)if(std::abs(v.foot[0]-e->worldX)<1e-8&&std::abs(v.foot[1]-e->worldY)<1e-8)id=v.id;if(!pixelCheck(s,*e,id,"original-later",false))return false;}
        }
        if(!mutationEffects(s).empty()||!replacementMoved||!restoredReplacementMoved||s.tick!=expiryTick||pairedTicks!=expectedTotal)return fail("native sequence missed original expiry tick or replacement never truly moved in both branches");
        // A further complete authored second cannot create another animation.
        for(int n=0;n<hz;++n){step(s);step(*canonical);if(!mutationEffects(s).empty()||!mutationEffects(*canonical).empty())return fail("GENDEATH replayed after natural expiry");}
        cases<<label<<','<<hz<<','<<bridge<<",5,5,"<<observedFrames<<','<<pairedTicks<<','<<pixelChecks<<",1,"<<firstTick<<','<<expiryTick<<','<<rawFrameCount<<','<<frameDelay<<','<<expectedTotal<<','<<replacementMoved<<'\n';
        totalPixels+=pixelChecks;totalPaired+=pairedTicks;++completed;std::cout<<"Genetic position "<<label<<" PASS 5 old bodies/5 frozen GENDEATH/1 strict context canonical save/"<<pixelChecks<<" raw pixel anchors\n";
    }
    if(completed!=(allCases?4:1))return fail("requested exact position cases were not completed");
    std::cout<<"Genetic position passed: "<<completed<<" real UI casts, "<<completed*5<<" old body positions, "<<completed
        <<" context-v14/canonical checkpoints, "<<totalPixels<<" cold/warm raw SHP anchors, "<<totalPaired<<" paired ticks\n";
    return true;
}
