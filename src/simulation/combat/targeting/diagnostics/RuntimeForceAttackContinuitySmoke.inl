// Issue 22/25. Every replacement is a real Ctrl mouse input. The oracle
// samples the body and committed clock immediately on both sides of input;
// no simulation tick, averaging, or invented interpolation can hide a jump.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeForceAttackContinuitySmokeProbe(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_FORCE_ATTACK_CONTINUITY_OUTPUT");
    const std::filesystem::path output = configured && *configured ? configured : "build/issue22_25_force_attack_continuity";
    const char* selected = std::getenv("CONCORD_FORCE_ATTACK_CONTINUITY_CASE");
    const std::string filter = selected ? selected : "";
    std::filesystem::create_directories(output);
    std::string label, stage;
    const auto fail = [&](const std::string& why) { error = "Force attack continuity " + label + " [" + stage + "]: " + why; return false; };
    std::ofstream cases(output/"cases.csv"), inputs(output/"inputs.csv"), motion(output/"movement.csv"),
        checkpoints(output/"checkpoints.csv"), shots(output/"shots.csv"), assetsCsv(output/"assets.csv");
    if (!cases || !inputs || !motion || !checkpoints || !shots || !assetsCsv) return fail("evidence streams unavailable");
    cases << "case,hz,type,route,inputs,accepted,rejected,wire_replays,ordinary14,canonical_extensions,paired_ticks,actual_actors,result\n";
    inputs << std::setprecision(17) << "case,branch,stage,ordinal,tick,id,target_object,target_x,target_y,picked_object,accepted,rejected,captured,applied,wire_bytes,before_x,before_y,before_z,after_x,after_y,after_z,before_cooldown,after_cooldown,before_remaining,after_remaining,before_total,after_total,before_q16,after_q16,result\n";
    motion << std::setprecision(17) << "case,branch,stage,tick,id,type,hp,alive,x,y,z,body_x,body_y,body_z,subcell,airborne,has_render,moving,behavior,move_cooldown,segment_remaining,segment_total,interval_q16,from_x,from_y,to_x,to_y,weapon_cooldown,target_object,attack_ground,target_x,target_y,animation_distance\n";
    checkpoints << "case,stage,format,tick,id,kind,target_object,attack_ground,cooldown,remaining,q16,file,result\n";
    shots << "case,tick,actor_id,attacker_id,weapon,projectile,target_object,damage,lifecycle\n";
    assetsCsv << "type,strength,speed,primary,secondary,naval,locomotor,rules_path,detail_hash,land_tile,water_tile\n";
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.computerAiEnabled=false; assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if (!assets->report.success) return fail("original assets: "+assets->report.errorMessage);
    IniFile stock; if(!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) return fail("original stock rules");
    assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
    const auto detailHash=assets->rules.getDetailRulesHash();
    const auto loadRules=[&](RuntimeWindowState& s) { s.rulesLoaded=s.rules.loadFromIni(stock); return s.rulesLoaded && s.rules.getDetailRulesHash()==detailHash; };
    int landTile=-1,waterTile=-1;
    for(int tile=0;tile<static_cast<int>(assets->theater.getTotalTileSlots());++tile) {
        TerrainCell cell; cell.tileIndex=tile; cell.subTileIndex=0; const auto info=runtimeTerrainInfoForCell(*assets,cell);
        if(landTile<0 && info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) landTile=tile;
        if(waterTile<0 && info.water && !info.bridge) waterTile=tile;
        if(landTile>=0 && waterTile>=0) break;
    }
    if(landTile<0 || waterTile<0) return fail("original clear/water TMP unavailable");
    const auto* rejectionType=findRuntimeUnitTypeById(assets->rules,"AEGIS");
    const auto* rejectionProjectile=assets->rules.findProjectileType("MedusaProjectile");
    if(!rejectionType || rejectionType->strength!=800 || !rejectionType->naval || rejectionType->primaryWeapon!="Medusa"
        || !rejectionProjectile || !rejectionProjectile->aa || rejectionProjectile->ag)return fail("original AEGIS AA-only rejection identity");
    assetsCsv << "AEGIS," << rejectionType->strength << ',' << rejectionType->speed << ',' << rejectionType->primaryWeapon << ',' << rejectionType->secondaryWeapon
        << ',' << rejectionType->naval << ',' << iniHexEncode(rejectionType->locomotor) << ',' << assets->report.rulesPath << ',' << detailHash << ',' << landTile << ',' << waterTile << '\n';
    const std::array<const char*,6> types{{"MTNK","E1","DEST","DLPH","JUMPJET","ORCA"}};
    for(const char* type:types) {
        const auto* unit=findRuntimeUnitTypeById(assets->rules,type); if(!unit || unit->strength<=0 || unit->speed<=0) return fail("stock actor identity "+std::string(type));
        assetsCsv << type << ',' << unit->strength << ',' << unit->speed << ',' << unit->primaryWeapon << ',' << unit->secondaryWeapon
            << ',' << unit->naval << ',' << iniHexEncode(unit->locomotor) << ',' << assets->report.rulesPath << ',' << detailHash << ',' << landTile << ',' << waterTile << '\n';
    }
    struct Scene { std::unique_ptr<RuntimeWindowState> state; int id=-1; std::array<int,2> targets{{-1,-1}};
        int rejectionId=-1; int initialHealth=0; int inputCount=0,accepted=0,rejected=0,replays=0,ordinary=0,canonical=0,paired=0; std::set<int> actors; };
    const auto make=[&](const char* type,int hz) {
        Scene scene; scene.state=std::make_unique<RuntimeWindowState>(); auto& s=*scene.state;
        s.config=assets->config; s.config.fixedTickHz=hz; s.config.playerSlots.clear(); s.config.playerCountry="Americans";
        s.config.gameMode="Battle"; s.config.noCooldown=s.config.multiplayerActive=s.config.computerAiEnabled=s.config.shortGame=false;
        s.config.crates=s.config.superweapons=false; s.config.skirmishLoadSavePath.clear();
        s.report=assets->report; s.art=assets->art; s.theater=assets->theater; s.artLoaded=s.theaterLoaded=true;
        if(!loadRules(s)) {scene.state.reset();return scene;}
        s.world=assets->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater=assets->mapFacts.theater; s.mapFacts.lighting=assets->mapFacts.lighting;
        const auto* unit=findRuntimeUnitTypeById(s.rules,type); const bool naval=unit && unit->naval;
        for(int y=64;y<=115;++y) for(int x=64;x<=115;++x) {
            TerrainCell cell; cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.subTileIndex=0;
            cell.tileIndex=(naval && x<103) || (x>=98 && x<=111 && y>=68 && y<=74)?waterTile:landTile;
            s.world.accessTerrainCells().push_back(cell);
        }
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);initializeRuntimeOfflineGameplayRouting(s);
        const auto add=[&](const char* id,RuntimeBuildKind kind,RuntimeCellPoint cell,const char* owner) {
            auto* o=addRuntimeObjectInstanceForOwner(s,owner,kind,id,cell); if(!o)return -1;
            o->mission="Sleep";o->facing=runtimeFacingFromDelta(1,0);o->turretFacing=o->facing;return o->instanceId;
        };
        scene.id=add(type,runtimeBuildKindForUnitTypeId(s.rules,type),{75,75},"Americans");
        for(int n=0;n<2;++n) scene.targets[n]=add(naval?"DRED":"GAPOWR",naval?RuntimeBuildKind::Unit:RuntimeBuildKind::Building,{96,75+5*n},"Americans");
        scene.rejectionId=add("AEGIS",RuntimeBuildKind::Unit,{100,72},"Americans");
        if(scene.id<0 || scene.targets[0]<0 || scene.targets[1]<0 || scene.rejectionId<0
            || add("GAPOWR",RuntimeBuildKind::Building,{111,108},"Russians")<0) {scene.state.reset();return scene;}
        scene.initialHealth=findObjectById(s,scene.id)->health;
        if(scene.initialHealth!=unit->strength) {scene.state.reset();return scene;}
        refreshRuntimeObjectOccupancyAfterLocalChange(s);return scene;
    };
    const HINSTANCE instance=GetModuleHandleW(nullptr);const wchar_t* name=L"ConcordForceAttackContinuitySmoke";
    WNDCLASSW windowClass{};windowClass.lpfnWndProc=runtimeWindowProc;windowClass.hInstance=instance;windowClass.lpszClassName=name;
    if(!RegisterClassW(&windowClass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return fail("input window class");
    HWND window=CreateWindowExW(0,name,L"Force attack continuity",WS_POPUP,0,0,1280,720,nullptr,nullptr,instance,nullptr);
    struct WindowCleanup {HWND window;HINSTANCE instance;const wchar_t* name;~WindowCleanup(){if(window){SetWindowLongPtrW(window,GWLP_USERDATA,0);SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));if(GetCapture()==window)ReleaseCapture();DestroyWindow(window);}UnregisterClassW(name,instance);}} windowCleanup{window,instance,name};
    if(!window)return fail("input window");
    const RECT client{0,0,1280,720};const RECT viewport=runtimeMapViewport(client);
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    struct Sample {std::array<double,3> body{};std::array<int,13> clocks{};std::array<double,8> geometry{};int x=0,y=0,z=0,subcell=0,hp=0;bool air=false;};
    const auto sample=[](const RuntimeWindowState& s,int id) {
        Sample result;const auto* o=findObjectById(s,id);if(!o)return result;
        const auto sub=worldObjectSubCellOffset(*o);result.body={runtimeObjectRenderX(*o)+sub.x,runtimeObjectRenderY(*o)+sub.y,runtimeObjectRenderLevel(*o)};
        result.x=o->x;result.y=o->y;result.z=o->level;result.subcell=o->subCell;result.hp=o->health;result.air=o->airborne;
        result.geometry[7]=o->movementAnimationDistance;
        if(const auto it=s.behaviors.find(id);it!=s.behaviors.end()) {
            const auto& b=it->second;result.clocks={b.moveCooldownTicks,b.renderMoveTicksRemaining,b.renderMoveTicksTotal,b.moveIntervalRemainderQ16,
                b.moveSegmentFrom.x,b.moveSegmentFrom.y,b.renderFrom.x,b.renderFrom.y,b.renderTo.x,b.renderTo.y,b.weaponCooldownTicks,int(b.renderFromOnBridge),int(b.renderToOnBridge)};
            result.geometry={b.renderFromOffsetX,b.renderFromOffsetY,b.renderToOffsetX,b.renderToOffsetY,b.renderFromLevel,b.renderToLevel,b.renderArrivalDistanceAfter,o->movementAnimationDistance};
        }
        return result;
    };
    const auto equal=[](const Sample& a,const Sample& b) {
        if(a.x!=b.x || a.y!=b.y || a.z!=b.z || a.subcell!=b.subcell || a.hp!=b.hp || a.air!=b.air || a.clocks!=b.clocks)return false;
        for(std::size_t n=0;n<a.body.size();++n)if(std::abs(a.body[n]-b.body[n])>1e-9)return false;
        for(std::size_t n=0;n<a.geometry.size();++n)if(std::abs(a.geometry[n]-b.geometry[n])>1e-9)return false;
        return true;
    };
    const auto record=[&](const RuntimeWindowState& s,int id,const char* branch) {
        const auto* o=findObjectById(s,id);if(!o)return;const auto p=sample(s,id);const auto it=s.behaviors.find(id);const auto* b=it==s.behaviors.end()?nullptr:&it->second;
        motion << label << ',' << branch << ',' << stage << ',' << s.tick << ',' << id << ',' << o->type << ',' << o->health << ',' << o->alive << ',' << o->x << ',' << o->y << ',' << o->level
            << ',' << p.body[0] << ',' << p.body[1] << ',' << p.body[2] << ',' << o->subCell << ',' << o->airborne << ',' << o->hasRenderPosition << ',' << o->renderMoveActive
            << ',' << (b?runtimeBehaviorKindName(b->kind):"none") << ',' << p.clocks[0] << ',' << p.clocks[1] << ',' << p.clocks[2] << ',' << p.clocks[3]
            << ',' << p.clocks[4] << ',' << p.clocks[5] << ',' << p.clocks[8] << ',' << p.clocks[9] << ',' << (b?b->weaponCooldownTicks:0) << ',' << (b?b->targetObjectId:-1)
            << ',' << (b?b->attackGround:false) << ',' << (b?b->attackBridgeCell.x:-1) << ',' << (b?b->attackBridgeCell.y:-1) << ',' << o->movementAnimationDistance << '\n';
    };
    const auto positioned=[&](const RuntimeWindowState& s,int id) {
        const auto* o=findObjectById(s,id);const auto it=s.behaviors.find(id);if(!o || !o->alive || !o->hasRenderPosition || !o->renderMoveActive || it==s.behaviors.end())return false;
        const auto& b=it->second;const auto p=sample(s,id);const double gap=std::hypot(p.body[0]-o->x-worldObjectSubCellOffset(*o).x,p.body[1]-o->y-worldObjectSubCellOffset(*o).y);
        if(runtimeFixedWingProfile(s,*o))return gap>0.02 && p.body[2]>o->level-1.0;
        if(runtimeJumpjetProfile(s,*o)) {
            const auto flight=s.jumpjetFlights.find(id);
            return o->airborne && b.moveCooldownTicks>0 && flight!=s.jumpjetFlights.end()
                && flight->second.active && flight->second.progress>0 && flight->second.progress<flight->second.length && gap>0.05;
        }
        return b.moveCooldownTicks>2 && b.renderMoveTicksTotal>b.renderMoveTicksRemaining
            && b.renderMoveTicksRemaining>0 && gap>0.05;
    };
    std::string pointFailure;
    const auto pointFor=[&](RuntimeWindowState& s,int target,RuntimeCellPoint cell)->std::optional<POINT> {
        pointFailure.clear();
        const auto* selectedObject=s.selectedObjectIds.empty()?nullptr:findObjectById(s,s.selectedObjectIds.front());
        const auto* targetObject=target>=0?findObjectById(s,target):nullptr;
        const RuntimeCellPoint focusTarget=targetObject?runtimeCellPointForObject(*targetObject):cell;
        const RuntimeCellPoint focus=selectedObject?RuntimeCellPoint{(selectedObject->x+focusTarget.x)/2,(selectedObject->y+focusTarget.y)/2}:focusTarget;
        ensureRuntimeCameraForViewport(s,viewport);centerRuntimeCameraOnCell(s,client,focus);invalidateRuntimeObjectProjectionCache(s);
        const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        if(target>=0)for(const auto& p:projected)if(p.object && p.object->instanceId==target) {
            for(int dy=1;dy<5;++dy)for(int dx=1;dx<5;++dx) {
                POINT point{p.bounds.left+(p.bounds.right-p.bounds.left)*dx/5,p.bounds.top+(p.bounds.bottom-p.bounds.top)*dy/5};
                const auto* picked=findProjectedObjectAtPoint(projected,point);if(picked && picked->object && picked->object->instanceId==target)return point;
            }
        }
        if(target>=0){pointFailure="projected object="+std::to_string(target)+" has no interior pixel picked as that object";return std::nullopt;}
        const POINT center=runtimeScreenPointForCell(s,viewport,cell,terrainLevelAtRuntimeCell(s,cell.x,cell.y));
        const auto* centerPicked=findProjectedObjectAtPoint(projected,center);
        const POINT centerDecoded=approximateCellFromScreenPoint(s,viewport,center);
        int tested=0,outsideViewport=0,occupiedPixels=0,wrongCellPixels=0;
        // A water TMP surface may own a pixel just beside its projected center.
        // Find a true mouse point inside this same cell's diamond: every
        // candidate must be on screen, unoccupied, and decode to the exact cell.
        for(int radius=0;radius<=6;++radius)for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
            if(std::max(std::abs(dx),std::abs(dy))!=radius
                || std::abs(dx)*s.camera.tileHeight+std::abs(dy)*s.camera.tileWidth>s.camera.tileWidth*s.camera.tileHeight/2)continue;
            const POINT point{center.x+dx,center.y+dy};++tested;
            if(!pointInRectInclusive(viewport,point)){++outsideViewport;continue;}
            const auto* picked=findProjectedObjectAtPoint(projected,point);
            if(picked && picked->object){++occupiedPixels;continue;}
            const POINT actual=approximateCellFromScreenPoint(s,viewport,point);
            if(actual.x!=cell.x || actual.y!=cell.y){++wrongCellPixels;continue;}
            return point;
        }
        std::ostringstream why;why<<"cell="<<cell.x<<','<<cell.y<<" focus="<<focus.x<<','<<focus.y
            <<" center="<<center.x<<','<<center.y<<" viewport="<<viewport.left<<','<<viewport.top<<','<<viewport.right<<','<<viewport.bottom
            <<" centerInViewport="<<pointInRectInclusive(viewport,center)<<" centerPicked="<<(centerPicked && centerPicked->object?centerPicked->object->instanceId:-1)
            <<" centerDecoded="<<centerDecoded.x<<','<<centerDecoded.y<<" cameraOffset="<<s.camera.worldOffsetX<<','<<s.camera.worldOffsetY
            <<" candidates="<<tested<<" outsideViewport="<<outsideViewport<<" occupied="<<occupiedPixels<<" wrongCell="<<wrongCellPixels;
        pointFailure=why.str();return std::nullopt;
    };
    int successful=0,totalInputs=0,totalAccepted=0,totalRejected=0,totalReplays=0,ordinarySaves=0,canonicalSaves=0,pairedTicks=0,captures=0;
    std::vector<std::string> failures;
    const auto capture=[&](RuntimeWindowState& s,const char* suffix) {
        const auto stats=drawRuntimeStateOffscreenAtCell(s,{86,78},1280,720,error,pathString(output/(label+'-'+suffix+".bmp")),false);
        if(!stats || !stats->rendered || stats->missingVoxelProxies!=0)return fail("original native screenshot: "+error);++captures;return true;
    };
    const auto click=[&](Scene& scene,RuntimeWindowState& s,int id,int target,RuntimeCellPoint cell,const char* branch,int ordinal,bool network,bool rejected) {
        RuntimeSnapshotActiveStateScope active(s);s.selectedObjectIds={id};const auto point=pointFor(s,target,cell);if(!point)return fail("actual projected target/empty cell pixel unavailable: "+pointFailure);
        const auto before=sample(s,id);const int tick=s.tick;int pickedId=-1;
        if(const auto* picked=findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s,viewport),*point);picked && picked->object)pickedId=picked->object->instanceId;
        std::vector<RuntimeMultiplayerGameplayCommand> captured;std::size_t wireBytes=0;bool applied=false;
        const auto slots=s.config.playerSlots;
        if(network) {
            RuntimeConfig::PlayerSlot local;local.enabled=local.human=local.local=true;s.config.playerSlots={local};
            s.multiplayerIntentRouter.setCaptureCallback([&](const RuntimeMultiplayerGameplayCommand& command,std::string& problem){captured.push_back(command);problem.clear();return true;});
        }
        struct RoutingScope {RuntimeWindowState& state;bool network;std::vector<RuntimeConfig::PlayerSlot> slots;~RoutingScope(){if(network){state.multiplayerIntentRouter.setCaptureCallback({});state.multiplayerIntentRouter.setAuthorityState({});state.multiplayerBridge=nullptr;state.config.playerSlots=std::move(slots);initializeRuntimeOfflineGameplayRouting(state);}}} routing{s,network,slots};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const LPARAM packed=MAKELPARAM(point->x,point->y);SendMessageW(window,WM_LBUTTONDOWN,MK_CONTROL|MK_LBUTTON,packed);
        SendMessageW(window,WM_LBUTTONUP,MK_CONTROL,packed);SetWindowLongPtrW(window,GWLP_USERDATA,0);
        if(network) {
            if(captured.size()!=1 || !equal(before,sample(s,id)))return fail("network input did not capture one command without changing local body/clock");
            const auto* order=std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&captured[0].body);
            const auto expected=target>=0?RuntimeMultiplayerGameplayTargetOrderKind::Attack:RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell;
            if(!order || order->order!=expected || order->objectIds!=std::vector<std::int32_t>{id} || order->append)return fail("captured wire lost forced target semantics");
            std::vector<std::uint8_t> wire;RuntimeMultiplayerGameplayCommand decoded;std::string problem;
            if(!encodeRuntimeMultiplayerGameplayCommand(captured[0],wire,problem) || !decodeRuntimeMultiplayerGameplayCommand(wire,decoded,problem)
                || !prevalidateRuntimeMultiplayerGameplayCommand(s,"Americans",decoded,problem))return fail("wire codec/authority: "+problem);
            wireBytes=wire.size();applied=applyRuntimeMultiplayerGameplayCommand(s,"Americans",decoded);++scene.replays;++totalReplays;
        }
        const auto after=sample(s,id);const bool accepted=s.lastCommand.valid && s.lastCommand.acceptedCount==1
            && s.lastCommand.verb==(target>=0?"force-attack":"force-fire-ground");
        const bool denied=s.lastCommand.valid && s.lastCommand.acceptedCount==0 && s.lastCommand.rejectedCount==1;
        const bool unchanged=s.tick==tick && equal(before,after);
        inputs << label << ',' << branch << ',' << stage << ',' << ordinal << ',' << tick << ',' << id << ',' << target << ',' << cell.x << ',' << cell.y << ',' << pickedId
            << ',' << accepted << ',' << denied << ',' << captured.size() << ',' << (network?applied:accepted) << ',' << wireBytes
            << ',' << before.body[0] << ',' << before.body[1] << ',' << before.body[2] << ',' << after.body[0] << ',' << after.body[1] << ',' << after.body[2]
            << ',' << before.clocks[0] << ',' << after.clocks[0] << ',' << before.clocks[1] << ',' << after.clocks[1] << ',' << before.clocks[2] << ',' << after.clocks[2]
            << ',' << before.clocks[3] << ',' << after.clocks[3] << ',' << (unchanged && (rejected?denied:accepted)?"PASS":"FAIL") << '\n';inputs.flush();record(s,id,branch);motion.flush();
        ++scene.inputCount;++totalInputs;if(accepted){++scene.accepted;++totalAccepted;}if(denied){++scene.rejected;++totalRejected;}
        if(!unchanged) {
            if(label=="MTNK-object-30" && std::string(branch)=="original")capture(s,"input-jump");
            std::ostringstream why;why<<std::setprecision(17)<<"input changed body/committed clock: before="<<before.body[0]<<','<<before.body[1]<<','<<before.body[2]
                <<" after="<<after.body[0]<<','<<after.body[1]<<','<<after.body[2]<<" cooldown="<<before.clocks[0]<<"->"<<after.clocks[0]<<" remaining="<<before.clocks[1]<<"->"<<after.clocks[1];return fail(why.str());
        }
        if(s.selectedObjectIds!=std::vector<int>{id} || (rejected?!denied:!accepted) || (network && applied!=!rejected))return fail("real Ctrl input acceptance/rejection/selection differs");
        if(!rejected) {const auto it=s.behaviors.find(id);if(it==s.behaviors.end() || it->second.kind!=RuntimeBehaviorKind::Attack || !it->second.forceFire
            || it->second.attackGround!=(target<0) || (target>=0 && it->second.targetObjectId!=target)
            || (target<0 && it->second.attackBridgeCell!=cell))return fail("force attack target identity was not retained");}
        return true;
    };
    const auto tickChecked=[&](RuntimeWindowState& s,int id,const char* branch) {
        const auto before=sample(s,id);step(s);const auto after=sample(s,id);record(s,id,branch);
        const auto* o=findObjectById(s,id);if(!o || !o->alive || o->health!=before.hp)return fail("mover identity/HP changed during continuity window");
        for(double value:after.body)if(!std::isfinite(value))return fail("nonfinite body coordinate");
        const double budget=0.5*30.0/s.config.fixedTickHz;
        if(std::hypot(after.body[0]-before.body[0],after.body[1]-before.body[1])>budget+1e-9)return fail("single simulation tick jumped more than the stock movement envelope");
        if(!o->airborne && std::abs(after.body[2]-before.body[2])>1e-9)return fail("flat original TMP ground height changed");
        return true;
    };
    const auto save=[&](Scene& scene) {
        auto& s=*scene.state;std::ostringstream ordinary,extension,canonical;IniFile normalIni,exactIni;
        if(!writeRuntimeStateToStream(s,ordinary,error) || !writeRuntimeStateToStream(s,extension,error) || !writeRuntimeMultiplayerSnapshotExtension(s,extension,error)
            || !normalIni.loadFromString(ordinary.str()) || !exactIni.loadFromString(extension.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni,canonical,error)
            || !exactIni.loadFromString(canonical.str()))return fail("checkpoint write: "+error);
        const auto restore=[&](bool exact) {
            auto q=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*q,s,false);if(!loadRules(*q))return std::unique_ptr<RuntimeWindowState>{};
            q->projectiles.clear();q->recentExpiredProjectiles.clear();q->impactEffects.clear();
            if(!materializeRuntimeLoadStateFromIni(*q,normalIni,error))return std::unique_ptr<RuntimeWindowState>{};
            if(exact){q->projectiles.clear();q->recentExpiredProjectiles.clear();q->impactEffects.clear();if(!materializeRuntimeMultiplayerSnapshotExtension(*q,exactIni,error))return std::unique_ptr<RuntimeWindowState>{};}
            return q;
        };
        auto ordinaryState=restore(false);auto exactState=restore(true);auto reference=restore(true);
        if(!ordinaryState || !exactState || !reference)return fail("checkpoint restore: "+error);
        const auto original=sample(s,scene.id);
        if(!equal(original,sample(*ordinaryState,scene.id)) || !equal(original,sample(*exactState,scene.id)))return fail("checkpoint changed committed body/clock/interval");
        const auto& b=s.behaviors.at(scene.id);
        for(int format=0;format<2;++format){const std::string file=label+(format?"-canonical-extension.ini":"-ordinary14.ini");if(std::filesystem::exists(output/file))return fail("checkpoint artifact exists");
            std::ofstream stream(output/file,std::ios::binary);stream<<(format?canonical.str():ordinary.str());if(!stream.good())return fail("checkpoint artifact write");
            checkpoints << label << ',' << stage << ',' << (format?"canonical-extension":"ordinary14") << ',' << s.tick << ',' << scene.id << ',' << runtimeBehaviorKindName(b.kind)
                << ',' << b.targetObjectId << ',' << b.attackGround << ',' << b.moveCooldownTicks << ',' << b.renderMoveTicksRemaining << ',' << b.moveIntervalRemainderQ16 << ',' << file << ",PASS\n";}
        ++scene.ordinary;++scene.canonical;++ordinarySaves;++canonicalSaves;
        // Exercise new inputs on genuinely restored state, before the saved
        // segment ends. Canonical context is deliberately seeded v14 + extension.
        for(int n=0;n<6;++n){const int target=n%2?scene.targets[1]:scene.targets[0];const RuntimeCellPoint cell=n%2?RuntimeCellPoint{94,83}:RuntimeCellPoint{94,85};
            stage="restored-repeat";
            for(auto* q:{reference.get(),ordinaryState.get(),exactState.get()})if(!click(scene,*q,scene.id,b.attackGround?-1:target,cell,q==reference.get()?"reference":q==ordinaryState.get()?"ordinary14":"canonical-extension",n,false,false))return false;
            if(!equal(sample(*reference,scene.id),sample(*ordinaryState,scene.id)) || !equal(sample(*reference,scene.id),sample(*exactState,scene.id)))return fail("restored input branches diverged");
            if(n<4){if(!tickChecked(*reference,scene.id,"reference") || !tickChecked(*ordinaryState,scene.id,"ordinary14") || !tickChecked(*exactState,scene.id,"canonical-extension"))return false;
                ++pairedTicks;++scene.paired;if(!equal(sample(*reference,scene.id),sample(*ordinaryState,scene.id)) || !equal(sample(*reference,scene.id),sample(*exactState,scene.id)))return fail("restored movement branches diverged");}}
        return true;
    };
    const auto collect=[&](Scene& scene) {
        const auto& s=*scene.state;const auto append=[&](const RuntimeProjectileEffect& p){if(p.attackerObjectId!=scene.id || !scene.actors.insert(p.actorId).second)return;
            shots<<label<<','<<s.tick<<','<<p.actorId<<','<<p.attackerObjectId<<','<<p.weaponId<<','<<p.projectileId<<','<<p.targetObjectId<<','<<p.damage<<','<<static_cast<int>(p.lifecycle)<<'\n';};
        for(const auto& p:s.projectiles)append(p);for(const auto& p:s.recentExpiredProjectiles)append(p);
    };
    for(const char* type:types)for(int hz:{30,60})for(int route=0;route<2;++route) {
        label=std::string(type)+(route?"-ground-":"-object-")+std::to_string(hz);if(!filter.empty() && filter!=label)continue;
        const auto run=[&]() {
            stage="startup";auto scene=make(type,hz);if(!scene.state)return fail("original fixture initialization");auto& s=*scene.state;
            s.selectedObjectIds={scene.id};if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{92,75}))return fail("real initial Move rejected");
            // The first cold infantry commit assigns its native destination
            // subcell. Establish that original movement before sampling input.
            stage="initial original subcell commit";
            step(s);record(s,scene.id,"original");
            stage="natural committed segment";bool found=false;for(int n=0;n<hz*15;++n){if(!tickChecked(s,scene.id,"original"))return false;if(positioned(s,scene.id)){found=true;break;}}
            if(!found)return fail("real movement never reached a fractional committed body");
            if(label=="MTNK-object-30" && !capture(s,"before-input"))return false;
            for(int n=0;n<8;++n){stage=n<2?"same-target-no-tick":n<6?"retarget-no-tick":"repeat-with-ticks";
                const int choice=n<2?0:n%2;const RuntimeCellPoint cell=choice?RuntimeCellPoint{94,83}:RuntimeCellPoint{94,85};
                if(!click(scene,s,scene.id,route?-1:scene.targets[choice],cell,"original",n,false,false))return false;
                if(n==0 && label=="MTNK-object-30" && !capture(s,"first-input"))return false;
                if(n==1 && std::string(type)=="MTNK"){stage="mid-attack-save";if(!save(scene))return false;}
                if(n>=5 && !tickChecked(s,scene.id,"original"))return false;
            }
            if(label=="MTNK-object-30" && !capture(s,"after-inputs"))return false;
            stage="natural attack";for(int n=0;n<hz*60 && scene.actors.empty();++n){step(s);record(s,scene.id,"original");collect(scene);}
            if(scene.actors.empty())return fail("accepted real attack emitted no actual projectile actor");
            cases<<label<<','<<hz<<','<<type<<','<<(route?"ground":"object")<<','<<scene.inputCount<<','<<scene.accepted<<','<<scene.rejected<<','<<scene.replays
                <<','<<scene.ordinary<<','<<scene.canonical<<','<<scene.paired<<','<<scene.actors.size()<<",PASS\n";return true;
        }();
        if(run){++successful;std::cout<<"Force attack continuity "<<label<<" PASS\n";}else{failures.push_back(error);std::cout<<error<<'\n';error.clear();}
    }
    for(int hz:{30,60}) {
        label="MTNK-wire-"+std::to_string(hz);if(!filter.empty() && filter!=label)continue;
        const auto run=[&]() {
            stage="wire startup";auto scene=make("MTNK",hz);if(!scene.state)return fail("wire fixture");auto& s=*scene.state;s.selectedObjectIds={scene.id};
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{92,75}))return fail("wire initial Move");
            step(s);record(s,scene.id,"original");
            bool found=false;for(int n=0;n<hz*15;++n){if(!tickChecked(s,scene.id,"original"))return false;if(positioned(s,scene.id)){found=true;break;}}if(!found)return fail("wire real committed body");
            for(int n=0;n<8;++n){stage="wire forced-repeat";if(!click(scene,s,scene.id,n%2?-1:scene.targets[n%4==0?0:1],{94,85},"original",n,true,false))return false;
                if(n>=5 && !tickChecked(s,scene.id,"original"))return false;}
            stage="AA-only real rejected ground input";s.selectedObjectIds={scene.rejectionId};if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{104,72}))return fail("AA-only control Move rejected");
            found=false;for(int n=0;n<hz*8;++n){step(s);record(s,scene.rejectionId,"rejected-control");if(positioned(s,scene.rejectionId)){found=true;break;}}if(!found)return fail("AA-only control did not enter a real step");
            for(int n=0;n<2;++n)if(!click(scene,s,scene.rejectionId,-1,{106,73},"rejected-control",n,false,true))return false;
            cases<<label<<','<<hz<<",MTNK,captured-wire,"<<scene.inputCount<<','<<scene.accepted<<','<<scene.rejected<<','<<scene.replays<<",0,0,0,0,PASS\n";return true;
        }();
        if(run){++successful;std::cout<<"Force attack continuity "<<label<<" PASS\n";}else{failures.push_back(error);std::cout<<error<<'\n';error.clear();}
    }
    if(!failures.empty()){std::ostringstream result;result<<failures.size()<<" force attack continuity scene(s) failed";for(const auto& why:failures)result<<'\n'<<why;error=result.str();return false;}
    if(successful==0)return fail("case filter selected no scene");
    std::cout<<"Force attack continuity passed: cases="<<successful<<" inputs="<<totalInputs<<" accepted="<<totalAccepted<<" rejected="<<totalRejected<<" wireReplays="<<totalReplays
        <<" ordinary14="<<ordinarySaves<<" canonicalExtensions="<<canonicalSaves<<" pairedTicks="<<pairedTicks<<" nativeCaptures="<<captures<<" stock/real-Ctrl-WndProc/instant-body-and-clock/retarget/wire/save=ok\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
