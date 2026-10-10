// Issue 23/7: original weapons, real Ctrl/Facade orders and native impacts.
// Completed concealment is initial fixture state; no damage or projectile is
// manufactured by this test. Ordinary visibility and naval restrictions remain.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeSubmergedForceAttackSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label="assets", stage="startup";
    const auto fail=[&](const std::string& why){error="Submerged force attack "+label+" ["+stage+"]: "+why;return false;};
    const char* configured=std::getenv("CONCORD_SUBMERGED_FORCE_ATTACK_OUTPUT");
    const char* selected=std::getenv("CONCORD_SUBMERGED_FORCE_ATTACK_CASE");
    const std::string filter=selected?selected:"";
    const std::filesystem::path output=configured && *configured?std::filesystem::path(configured)
        :std::filesystem::path(config.localDataRootPath).parent_path()/"build/smoke/submerged-force-attack";
    std::filesystem::create_directories(output);
    std::ofstream cases(output/"cases.csv"), inputs(output/"inputs.csv"), shots(output/"shots.csv"),
        impacts(output/"impacts.csv"), checkpoints(output/"checkpoints.csv"), rulesEvidence(output/"rules.csv");
    if(!cases || !inputs || !shots || !impacts || !checkpoints || !rulesEvidence)return fail("evidence files unavailable");
    cases<<"scene,type,hz,mode,source,target,initial_hp,expected_damage,actual_damage,primary_actors,ordinary14,canonical,paired_ticks,result\n";
    inputs<<"scene,tick,source,requested_object,cell_x,cell_y,picked_object,ctrl,verb,accepted,rejected,force_fire,attack_ground,internal_target,expected_internal_target,result\n";
    shots<<"scene,branch,tick,actor,attacker,target,weapon,projectile,warhead,payload_damage,raw_rule_damage,parent_actor,generation,target_x,target_y\n";
    impacts<<"scene,branch,tick,actor,target,type,hp_before,hp_after,submerged_before,cloaked_before,visible_before,submerged_after,cloaked_after,visible_after,expected_damage,result\n";
    checkpoints<<"scene,format,tick,source,target,force_fire,attack_ground,actor,file,result,base_file,role\n";
    rulesEvidence<<"type,strength,armor,primary,secondary,naval_targeting,underwater,organic,damage,rof,range,projectile,warhead,rules_hash,rules_path,land_tile,water_tile\n";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->config.playerSlots.clear();assets->config.computerAiEnabled=false;assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original assets: "+assets->report.errorMessage);
    IniFile stock;if(!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock))return fail("original rules unavailable");
    assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
    const auto rulesHash=assets->rules.getDetailRulesHash();
    const auto* gun=assets->rules.findWeaponType("105mm");
    const auto* cannon=assets->rules.findProjectileType("Cannon");
    const auto* ap=assets->rules.findWarheadType("AP");
    const auto* tank=findRuntimeUnitTypeById(assets->rules,"MTNK");
    const auto verses=splitRuntimeTokenList(stock.getValue("AP","Verses",""));
    if(!tank || tank->strength!=300 || tank->primaryWeapon!="105mm" || tank->navalTargeting!=0
        || !gun || gun->damage!=65 || gun->rof!=60 || std::abs(gun->range-5.0)>1e-9
        || gun->projectile!="Cannon" || gun->warhead!="AP" || !cannon || cannon->aa || !cannon->ag
        || !ap || std::abs(ap->cellSpread-.3)>1e-9 || std::abs(ap->percentAtMax-.5)>1e-9
        || verses.size()!=11 || verses[3]!="75%" || verses[5]!="100%")return fail("original MTNK/105mm/Cannon/AP contract changed");
    int landTile=-1,waterTile=-1;
    const int waterSet=assets->theater.getGeneralTileSetIndex("WaterSet");
    for(const auto& set:assets->theater.getTileSets())if(set.index==waterSet && set.tilesInSet>0)waterTile=set.startTileIndex;
    for(int tile=0;tile<static_cast<int>(assets->theater.getTotalTileSlots());++tile){
        TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;const auto info=runtimeTerrainInfoForCell(*assets,cell);
        if(info.passable && !info.water && !info.shore && !info.cliff && !info.bridge){landTile=tile;break;}
    }
    if(landTile<0 || waterTile<0)return fail("original clear land/water TMP unavailable");
    for(const char* id:{"MTNK","SUB","BSUB","DLPH","SQD","AEGIS","DRON","ASW","STLN","DEST"}){
        const auto* type=findRuntimeUnitTypeById(assets->rules,id);if(!type)return fail("missing original type "+std::string(id));
        const auto* weapon=assets->rules.findWeaponType(type->primaryWeapon);
        rulesEvidence<<id<<','<<type->strength<<','<<type->armor<<','<<type->primaryWeapon<<','<<type->secondaryWeapon
            <<','<<type->navalTargeting<<','<<type->underwater<<','<<type->organic<<','<<(weapon?weapon->damage:0)
            <<','<<(weapon?weapon->rof:0)<<','<<(weapon?weapon->range:0)<<','<<(weapon?weapon->projectile:"")
            <<','<<(weapon?weapon->warhead:"")<<','<<rulesHash<<','<<assets->report.rulesPath<<','<<landTile<<','<<waterTile<<'\n';
    }
    const auto loadRules=[&](RuntimeWindowState& s){s.rulesLoaded=s.rules.loadFromIni(stock);return s.rulesLoaded && s.rules.getDetailRulesHash()==rulesHash;};
    const auto add=[](RuntimeWindowState& s,int id,const std::string& type,const std::string& owner,RuntimeCellPoint cell){
        WorldObjectInstance o;o.instanceId=id;o.mapId="SubmergedForce"+std::to_string(id);o.type=type;o.owner=owner;
        o.kind=runtimeBuildKindForUnitTypeId(s.rules,type)==RuntimeBuildKind::Infantry?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        o.x=cell.x;o.y=cell.y;o.cell=runtimeCellKey(cell.x,cell.y);o.alive=o.poweredOperational=true;o.mission="Sleep";
        o.facing=o.turretFacing=runtimeFacingFromDelta(1,0);o.health=runtimeMaxHealthForObject(s,o);
        initializeRuntimeObjectAmmo(o,runtimeAmmoRulesForObject(s.rules,o));applyRuntimeInitialObjectFlightState(s.rules,o);
        runtimeInitializeModExtensions(o,s.rules,s.config.fixedTickHz);s.world.accessObjects().push_back(std::move(o));
    };
    const auto make=[&](const char* type,int hz,bool enemy,const char* source="MTNK"){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;s.config=assets->config;s.config.fixedTickHz=hz;
        s.config.playerSlots.clear();s.config.playerCountry="Americans";s.config.gameMode="Battle";s.config.startingUnits=0;
        s.config.noCooldown=s.config.multiplayerActive=s.config.computerAiEnabled=s.config.shortGame=false;
        s.config.crates=s.config.superweapons=false;s.config.skirmishLoadSavePath.clear();s.campaignMode=false;
        s.report=assets->report;s.art=assets->art;s.theater=assets->theater;s.artLoaded=s.theaterLoaded=true;
        if(!loadRules(s))return std::unique_ptr<RuntimeWindowState>{};
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};
        s.mapFacts.theater=assets->mapFacts.theater;s.mapFacts.lighting=assets->mapFacts.lighting;s.mapFactsLoaded=false;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
        for(int y=44;y<=90;++y)for(int x=44;x<=90;++x){TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;
            cell.tileIndex=x<=65?landTile:waterTile;cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);}
        const auto* sourceType=findRuntimeUnitTypeById(s.rules,source);
        add(s,1,source,"Americans",sourceType && sourceType->naval?RuntimeCellPoint{66,63}:RuntimeCellPoint{64,64});
        add(s,2,type,enemy?"Russians":"Americans",{67,64});
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);initializeRuntimeOfflineGameplayRouting(s);
        {RuntimeSnapshotActiveStateScope active(s);advanceRuntimeSpecialMechanisms(s);completeRuntimeWorldRefresh(s);}
        if(auto* target=findMutableObjectById(s,2);target && findRuntimeUnitType(s.rules,*target)->underwater){
            // Begin after the stock initial cloak/dive transition has completed.
            target->submerged=target->cloaked=true;target->submergeCooldownTicks=target->cloakCooldownTicks=0;
            s.cloakObservedHealthByObject[2]=target->health;
        }
        invalidateRuntimeObjectProjectionCache(s);return box;
    };
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto stop=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);s.selectedObjectIds={1};
        const int tick=s.tick;std::vector<std::string> before;for(const auto& p:s.projectiles)before.push_back(serializeRuntimeProjectileEffect(p));
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop) || s.lastCommand.acceptedCount!=1
            || s.tick!=tick || s.combatDetails.hasPendingBurst(1)
            || (s.behaviors.contains(1) && s.behaviors.at(1).kind==RuntimeBehaviorKind::Attack))return false;
        std::vector<std::string> after;for(const auto& p:s.projectiles)after.push_back(serializeRuntimeProjectileEffect(p));if(before!=after)return false;
        // The real Stop has already ended Attack without clearing its in-flight
        // shot. Sleep only prevents a new ordinary Guard acquisition in this fixture.
        if(auto* source=findMutableObjectById(s,1))source->mission="Sleep";return true;};
    const auto describe=[](const RuntimeWindowState& s,int id){std::ostringstream text;const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);
        text<<"tick="<<s.tick<<" id="<<id;if(!o)return text.str()+" missing";
        text<<" type="<<o->type<<" hp="<<o->health<<" alive="<<o->alive<<" owner="<<o->owner<<" cell="<<o->x<<','<<o->y
            <<" submerged="<<o->submerged<<" cloaked="<<o->cloaked<<" mission="<<o->mission<<" ammo="<<o->currentAmmo;
        if(const auto it=s.behaviors.find(id);it!=s.behaviors.end())text<<" behavior="<<runtimeBehaviorKindName(it->second.kind)
            <<" force="<<it->second.forceFire<<" ground="<<it->second.attackGround<<" target="<<it->second.targetObjectId;
        return text.str();};
    const HINSTANCE instance=GetModuleHandleW(nullptr);const wchar_t* className=L"ConcordSubmergedForceAttackSmoke";
    WNDCLASSW wc{};wc.lpfnWndProc=runtimeWindowProc;wc.hInstance=instance;wc.lpszClassName=className;
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return fail("native Ctrl input window class");
    HWND window=CreateWindowExW(0,className,L"Submerged force attack",WS_POPUP,0,0,1280,720,nullptr,nullptr,instance,nullptr);
    struct WindowCleanup{HWND window;HINSTANCE instance;const wchar_t* name;~WindowCleanup(){if(window){
        SetWindowLongPtrW(window,GWLP_USERDATA,0);SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if(GetCapture()==window)ReleaseCapture();DestroyWindow(window);}UnregisterClassW(name,instance);}} cleanup{window,instance,className};
    if(!window)return fail("native Ctrl input window");
    const RECT client{0,0,1280,720},viewport=runtimeMapViewport(client);
    std::string pointFailure;
    const auto pointFor=[&](RuntimeWindowState& s,int target,RuntimeCellPoint cell)->std::optional<POINT>{
        pointFailure.clear();
        ensureRuntimeCameraForViewport(s,viewport);centerRuntimeCameraOnCell(s,client,{65,64});invalidateRuntimeObjectProjectionCache(s);
        const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        if(target>=0){for(const auto& p:projected)if(p.object && p.object->instanceId==target)
            for(int y=1;y<8;++y)for(int x=1;x<8;++x){POINT pt{p.bounds.left+(p.bounds.right-p.bounds.left)*x/8,p.bounds.top+(p.bounds.bottom-p.bounds.top)*y/8};
                const auto* picked=findProjectedObjectAtPoint(projected,pt);if(picked && picked->object && picked->object->instanceId==target)return pt;}
            pointFailure="object="+std::to_string(target)+" has no interior pixel picked as that object";return std::nullopt;}
        const POINT center=runtimeScreenPointForCell(s,viewport,cell,terrainLevelAtRuntimeCell(s,cell.x,cell.y));
        const POINT centerDecoded=approximateCellFromScreenPoint(s,viewport,center);
        const auto* centerPicked=findProjectedObjectAtPoint(projected,center);
        int tested=0,outside=0,occupied=0,wrongCell=0;std::map<std::pair<int,int>,int> decodedCells;
        // Search the complete independent geometric cell diamond, including
        // its edges. A textured TMP pixel must still decode to this exact cell
        // and must have no projected object; no object ID is used for blind fire.
        const int radiusLimit=std::max(s.camera.tileWidth,s.camera.tileHeight)/2;
        for(int radius=0;radius<=radiusLimit;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){
            if(std::max(std::abs(x),std::abs(y))!=radius || std::abs(x)*s.camera.tileHeight+std::abs(y)*s.camera.tileWidth>s.camera.tileWidth*s.camera.tileHeight/2)continue;
            ++tested;const POINT pt{center.x+x,center.y+y};if(!pointInRectInclusive(viewport,pt)){++outside;continue;}
            const auto* picked=findProjectedObjectAtPoint(projected,pt);if(picked && picked->object){++occupied;continue;}
            const POINT decoded=approximateCellFromScreenPoint(s,viewport,pt);++decodedCells[{decoded.x,decoded.y}];
            if(decoded.x==cell.x && decoded.y==cell.y)return pt;++wrongCell;
        }
        std::ostringstream why;why<<"cell="<<cell.x<<','<<cell.y<<" center="<<center.x<<','<<center.y
            <<" centerDecoded="<<centerDecoded.x<<','<<centerDecoded.y<<" centerPicked="<<(centerPicked && centerPicked->object?centerPicked->object->instanceId:-1)
            <<" tile="<<s.camera.tileWidth<<','<<s.camera.tileHeight<<" viewport="<<viewport.left<<','<<viewport.top<<','<<viewport.right<<','<<viewport.bottom
            <<" camera="<<s.camera.worldOffsetX<<','<<s.camera.worldOffsetY<<" candidates="<<tested<<" outside="<<outside<<" occupied="<<occupied<<" wrongCell="<<wrongCell<<" decoded=";
        for(const auto& [xy,count]:decodedCells)why<<xy.first<<':'<<xy.second<<':'<<count<<';';
        pointFailure=why.str();return std::nullopt;
    };
    const auto click=[&](RuntimeWindowState& s,int target,RuntimeCellPoint cell){
        RuntimeSnapshotActiveStateScope active(s);s.selectedObjectIds={1};const auto pt=pointFor(s,target,cell);
        if(!pt)return fail("actual target/empty water pixel unavailable: "+pointFailure+"; "+describe(s,2));
        int pickedId=-1;if(const auto* picked=findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s,viewport),*pt);picked && picked->object)pickedId=picked->object->instanceId;
        const int beforeTick=s.tick,beforeHp=findObjectById(s,2)->health;
        const auto surrogate=target<0?runtimeGroundAttackTargetObject(s,cell):std::optional<WorldObjectInstance>{};
        const int expectedTarget=surrogate?surrogate->instanceId:target;
        const bool validSurrogate=target>=0 || (surrogate && surrogate->instanceId<-1 && surrogate->instanceId!=2
            && surrogate->kind==WorldObjectKind::Structure && surrogate->type=="RuntimeGround" && surrogate->owner=="RuntimeGround"
            && surrogate->x==cell.x && surrogate->y==cell.y && surrogate->cell==runtimeCellKey(cell.x,cell.y)
            && std::none_of(s.world.getObjects().begin(),s.world.getObjects().end(),[&](const auto& o){return o.instanceId==surrogate->instanceId;}));
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));const LPARAM packed=MAKELPARAM(pt->x,pt->y);
        SendMessageW(window,WM_LBUTTONDOWN,MK_CONTROL|MK_LBUTTON,packed);SendMessageW(window,WM_LBUTTONUP,MK_CONTROL,packed);
        SetWindowLongPtrW(window,GWLP_USERDATA,0);
        const auto it=s.behaviors.find(1);const bool accepted=s.lastCommand.valid && s.lastCommand.acceptedCount==1
            && s.lastCommand.verb==(target>=0?"force-attack":"force-fire-ground");
        const bool force=it!=s.behaviors.end() && it->second.forceFire,ground=it!=s.behaviors.end() && it->second.attackGround;
        const int internalTarget=it==s.behaviors.end()?-1:it->second.targetObjectId;
        const bool valid=accepted && force && validSurrogate && ground==(target<0) && s.selectedObjectIds==std::vector<int>{1}
            && s.tick==beforeTick && findObjectById(s,2)->health==beforeHp && it->second.kind==RuntimeBehaviorKind::Attack
            && internalTarget==expectedTarget && (target>=0?pickedId==target:(pickedId==-1 && it->second.attackBridgeCell==cell));
        inputs<<label<<','<<s.tick<<",1,"<<target<<','<<cell.x<<','<<cell.y<<','<<pickedId<<",1,"<<s.lastCommand.verb
            <<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<force<<','<<ground<<','<<internalTarget<<','<<expectedTarget
            <<','<<(valid?"PASS":"FAIL")<<'\n';
        if(!valid)return fail("real Ctrl input lost exact force/object/cell semantics: "+describe(s,1)+" expectedInternalTarget="+std::to_string(expectedTarget));
        return true;
    };
    int completed=0,controls=0,ordinaryCount=0,canonicalCount=0,pairedTicks=0;
    const auto firstGun=[&](RuntimeWindowState& s,int& actor){
        for(const auto& p:s.projectiles)if(p.attackerObjectId==1 && p.generation==0){
            if(actor>=0 && actor!=p.actorId)return fail("more than one primary before real Stop");
            const auto* original=s.rules.findWeaponType("105mm");
            if(p.weaponId!="105mm" || p.projectileId!="Cannon" || p.warheadId!="AP" || !p.ag || p.aa
                || p.parentActorId>=0 || p.particleDamagePulse || p.voxelDebris.enabled || !original
                || runtimeCombatProjectileAuthoredDamage(s,p,*original)!=65)return fail("unknown/non-original primary: "+serializeRuntimeProjectileEffect(p));
            actor=p.actorId;
        }return true;
    };
    const auto equal=[&](const RuntimeWindowState& a,const RuntimeWindowState& b){
        if(a.tick!=b.tick || a.specialMechanisms.stateHash()!=b.specialMechanisms.stateHash() || a.projectiles.size()!=b.projectiles.size())return false;
        for(std::size_t n=0;n<a.projectiles.size();++n)if(serializeRuntimeProjectileEffect(a.projectiles[n])!=serializeRuntimeProjectileEffect(b.projectiles[n]))return false;
        for(int id:{1,2}){const auto* x=findObjectById(a,id);const auto* y=findObjectById(b,id);if(!x || !y || x->health!=y->health
            || x->alive!=y->alive || x->x!=y->x || x->y!=y->y || x->level!=y->level || x->submerged!=y->submerged || x->cloaked!=y->cloaked
            || x->cloakCooldownTicks!=y->cloakCooldownTicks || x->submergeCooldownTicks!=y->submergeCooldownTicks
            || x->currentAmmo!=y->currentAmmo || x->weaponCooldownTicksRemaining!=y->weaponCooldownTicksRemaining)return false;
            const auto ax=a.behaviors.find(id),bx=b.behaviors.find(id);if((ax==a.behaviors.end())!=(bx==b.behaviors.end()))return false;
            if(ax!=a.behaviors.end() && (ax->second.kind!=bx->second.kind || ax->second.forceFire!=bx->second.forceFire
                || ax->second.attackGround!=bx->second.attackGround || ax->second.targetObjectId!=bx->second.targetObjectId
                || ax->second.attackBridgeCell!=bx->second.attackBridgeCell || ax->second.weaponCooldownTicks!=bx->second.weaponCooldownTicks))return false;
        }return true;
    };
    struct Impact{bool before=false,after=false;int hp=0,actor=-1,damage=0;bool water=false,cloak=false,visible=false;std::string problem;};
    struct Branch{RuntimeWindowState* state;std::string name;Impact hit;};
    struct ObserverScope{RuntimeWindowState& state;std::function<void(const RuntimeProjectileEffect&,bool)> previous;
        explicit ObserverScope(RuntimeWindowState& s):state(s),previous(std::move(s.runtimeProjectileImpactObserver)){}
        ~ObserverScope(){state.runtimeProjectileImpactObserver=std::move(previous);}};
    const auto deliver=[&](RuntimeWindowState& live,int actor,int expected,bool hidden,bool miss,bool save){
        std::unique_ptr<RuntimeWindowState> normal,exact;std::vector<Branch> branches;branches.reserve(3);branches.push_back({&live,"live",{}});
        if(save){stage="actual force/in-flight checkpoint";std::ostringstream ordinary,extension,canonical;IniFile normalIni,exactIni;
            if(!writeRuntimeStateToStream(live,ordinary,error) || !writeRuntimeStateToStream(live,extension,error)
                || !writeRuntimeMultiplayerSnapshotExtension(live,extension,error) || !exactIni.loadFromString(extension.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni,canonical,error))return fail("real save writer: "+error);
            const std::string ordinaryFile=label+"-ordinary14.ini",canonicalFile=label+"-canonical.ini";
            for(int format=0;format<2;++format){std::ofstream out(output/(format?canonicalFile:ordinaryFile),std::ios::binary);
                out<<(format?canonical.str():ordinary.str());out.close();if(!out.good())return fail("checkpoint file write");}
            if(!normalIni.loadFromFile((output/ordinaryFile).string()) || !exactIni.loadFromFile((output/canonicalFile).string()))
                return fail("actual checkpoint file read");
            if(normalIni.getValue("RuntimeSave","Version","")!="14" || exactIni.getValue("RuntimeSave","Version","")!="14"
                || !normalIni.getSection("RuntimeWorldAudioHost") || !normalIni.getSection("WorldAudioRuntime")
                || !normalIni.getSection("RuntimeLightingHost") || !normalIni.getSection("LightingRuntime")
                || !normalIni.getSection("RuntimeParticleHost") || !normalIni.getSection("ParticleRuntime"))
                return fail("actual ordinary14 audiovisual authority/version missing");
            // The canonical authority image is an extension paired with this
            // complete ordinary14 base. It is not a standalone v14/v15 save.
            // The extension retains live projectiles from the ordinary loader.
            const auto restore=[&](bool precise){auto q=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*q,live,false);
                if(!loadRules(*q) || !materializeRuntimeLoadStateFromIni(*q,normalIni,error)
                    || (precise && !materializeRuntimeMultiplayerSnapshotExtension(*q,exactIni,error)))return std::unique_ptr<RuntimeWindowState>{};
                initializeRuntimeOfflineGameplayRouting(*q);return q;};
            normal=restore(false);exact=restore(true);if(!normal || !exact)return fail("actual save restore: "+error);
            if(!equal(live,*normal) || !equal(live,*exact))return fail("ordinary/canonical changed live force/projectile/cloak authority");
            for(int format=0;format<2;++format){const auto& b=live.behaviors.at(1);
                checkpoints<<label<<','<<(format?"canonical":"ordinary14")<<','<<live.tick
                    <<",1,2,"<<b.forceFire<<','<<b.attackGround<<','<<actor<<','<<(format?canonicalFile:ordinaryFile)
                    <<",PASS,"<<ordinaryFile<<','<<(format?"authority-extension-v14":"complete-save-v14")<<'\n';}
            ++ordinaryCount;++canonicalCount;branches.push_back({normal.get(),"ordinary14",{}});branches.push_back({exact.get(),"canonical",{}});
        }
        std::vector<std::unique_ptr<ObserverScope>> observerScopes;
        for(auto& branch:branches){auto* q=branch.state;auto* hit=&branch.hit;const std::string name=branch.name;
            observerScopes.push_back(std::make_unique<ObserverScope>(*q));
            q->runtimeProjectileImpactObserver=[&,q,hit,name](const RuntimeProjectileEffect& p,bool after){
                if(p.actorId!=actor)return;
                const auto* target=findObjectById(*q,2);const auto* original=q->rules.findWeaponType("105mm");
                if(p.attackerObjectId!=1 || p.weaponId!="105mm" || p.projectileId!="Cannon" || p.warheadId!="AP"
                    || p.generation!=0 || p.parentActorId>=0 || !original || runtimeCombatProjectileAuthoredDamage(*q,p,*original)!=65
                    || !target){hit->problem="native impact source identity changed";return;}
                if(!after){if(hit->before){hit->problem="native first actor impacted twice";return;}
                    hit->before=true;hit->hp=target->health;hit->actor=p.actorId;hit->water=target->submerged;hit->cloak=target->cloaked;
                    hit->visible=runtimeObjectVisibleToOwner(*q,"Americans",*target);
                    if(!target->alive || !hit->water || (hidden && (!hit->cloak || hit->visible)))hit->problem="impact-before did not witness true concealed/submerged target";
                    return;}
                hit->after=true;hit->damage=hit->hp-target->health;
                const bool visible=runtimeObjectVisibleToOwner(*q,"Americans",*target);
                const bool valid=hit->before && hit->damage==expected && target->alive
                    && (miss?(target->submerged && target->cloaked && !visible):(!target->submerged && !target->cloaked && visible));
                impacts<<label<<','<<name<<','<<q->tick<<','<<p.actorId<<",2,"<<target->type<<','<<hit->hp<<','<<target->health
                    <<','<<hit->water<<','<<hit->cloak<<','<<hit->visible<<','<<target->submerged<<','<<target->cloaked<<','<<visible
                    <<','<<expected<<','<<(valid?"PASS":"FAIL")<<'\n';
                if(!valid)hit->problem="immediate original HP/reveal differed: "+describe(*q,2)+" expectedDamage="+std::to_string(expected)+" actualDamage="+std::to_string(hit->damage);
            };
            for(const auto& p:q->projectiles)if(p.actorId==actor)shots<<label<<','<<name<<','<<q->tick<<','<<p.actorId<<','<<p.attackerObjectId
                <<','<<p.targetObjectId<<','<<p.weaponId<<','<<p.projectileId<<','<<p.warheadId<<','<<p.damage<<",65,"<<p.parentActorId<<','<<p.generation<<','<<p.toCell.x<<','<<p.toCell.y<<'\n';
            if(!stop(*q))return fail("real Stop after first native projectile rejected");
        }
        stage="original impact after real Stop";
        for(int n=0;n<live.config.fixedTickHz*5;++n){for(auto& b:branches){step(*b.state);
                for(const auto& p:b.state->projectiles)if(p.attackerObjectId==1 && p.generation==0 && p.actorId!=actor)
                    return fail("a second native primary escaped the real Stop");}
            if(save){++pairedTicks;if(!equal(live,*normal) || !equal(live,*exact))return fail("uninterrupted live differs from actual ordinary/canonical continuation");}
            bool done=true;for(auto& b:branches){if(!b.hit.problem.empty())return fail(b.name+": "+b.hit.problem);done=done && b.hit.after;}
            if(done){for(const auto& b:branches)if(!b.hit.before || b.hit.damage!=expected)return fail("missing strict native impact evidence");return true;}
        }return fail("first original Cannon did not actually impact in five seconds: "+describe(live,2));
    };
    for(const char* type:{"SUB","BSUB","DLPH","SQD"})for(int hz:{15,30,60})for(bool hidden:{false,true}){
        label=std::string(type)+(hidden?"-blind-cell-":"-own-object-")+std::to_string(hz);if(!filter.empty() && filter!=label)continue;
        stage="original completed concealment";auto box=make(type,hz,hidden);if(!box)return fail("original fixture setup");auto& s=*box;
        const auto* target=findObjectById(s,2);const auto* original=findRuntimeUnitType(s.rules,*target);
        const int initial=target->health,expected=original->organic?49:65;
        if(initial!=original->strength || !target->submerged || !target->cloaked || runtimeObjectVisibleToOwner(s,"Americans",*target)==hidden)
            return fail("completed stock concealment/visibility invalid: "+describe(s,2));
        s.selectedObjectIds={1};
        if(hidden){stage="hidden object ID and passive acquisition negative";
            if(dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,{67,64},target)
                || s.behaviors.contains(1) || !s.projectiles.empty() || target->health!=initial)return fail("hidden explicit object ID was accepted");
            findMutableObjectById(s,1)->mission="Guard";
            for(int n=0;n<hz;++n){step(s);target=findObjectById(s,2);
                if(!target || target->health!=initial || !target->submerged || !target->cloaked || runtimeObjectVisibleToOwner(s,"Americans",*target)
                    || (s.behaviors.contains(1) && s.behaviors.at(1).kind==RuntimeBehaviorKind::Attack)
                    || std::any_of(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){return p.attackerObjectId==1;}))
                    return fail("ordinary Guard acquired/revealed a hidden underwater target: "+describe(s,1));}
            findMutableObjectById(s,1)->mission="Sleep";
        }
        stage="actual Ctrl forced target";if(!click(s,hidden?-1:2,{67,64}))return false;
        int actor=-1;for(int n=0;n<hz*5 && actor<0;++n){step(s);if(!firstGun(s,actor))return false;}
        if(actor<0)return fail("accepted force order emitted no original 105mm: "+describe(s,1));
        const int beforePairs=pairedTicks;if(!deliver(s,actor,expected,hidden,false,hz==30))return false;
        cases<<label<<','<<type<<','<<hz<<','<<(hidden?"blind-cell":"own-object")<<",MTNK,2,"<<initial<<','<<expected<<','<<expected
            <<",1,"<<(hz==30)<<','<<(hz==30)<<','<<(pairedTicks-beforePairs)<<",PASS\n";
        ++completed;std::cout<<"Submerged force attack "<<label<<" PASS originalDamage="<<expected<<" impact-before-submerged/after-original-reveal\n";
    }
    const auto negative=[&](const char* name,const char* source,const char* type,int contract){
        label=name;if(!filter.empty() && filter!=label)return true;stage="independent original target-domain restriction";
        auto box=make(type,30,false,source);if(!box)return fail("negative fixture");auto& s=*box;
        const auto* attacker=findObjectById(s,1);const auto* target=findObjectById(s,2);const auto* native=findRuntimeUnitType(s.rules,*attacker);
        const auto* weapon=s.rules.findWeaponType(native->primaryWeapon);if(!weapon)return fail("negative source original weapon absent");
        const bool domain=runtimeWeaponSlotCanTargetDomain(s,*attacker,RuntimeWeaponSlot::Primary,*target,true);
        const bool rawTarget=runtimeWeaponCanTargetObject(s,*weapon,*target);
        const bool selectedWeapon=runtimeWeaponProfileForObject(s,*attacker,target,true).weapon!=nullptr;
        const auto* rawProjectile=s.rules.findProjectileType(weapon->projectile);
        if(!runtimeObjectHasAttackAmmo(s.rules,*attacker) || !runtimeWeaponProfileForObject(s,*attacker,nullptr,true).weapon
            || (contract!=2 && !runtimeObjectAcceptsPlayerSelection(s,*attacker)))return fail("negative source lacks independent ammo/weapon/selection eligibility");
        if((contract==6 && (native->navalTargeting!=6 || domain || !rawTarget))
            || (contract==2 && (native->navalTargeting!=2 || domain || !rawTarget || !attacker->airborne))
            || (contract==1 && (rawTarget || domain || native->primaryWeapon!="Medusa"
                || !rawProjectile || !rawProjectile->aa || rawProjectile->ag))
            || (contract==0 && (!domain || rawTarget)))return fail("negative gates were masked/misidentified: "+describe(s,1));
        s.selectedObjectIds={1};
        // This free original ASW proves Only2 weapon eligibility, without
        // claiming an independent player-control/launch contract. The original
        // Destroyer native probe exercises registered ASW dispatch separately.
        if(contract!=2){dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,{67,64},target);
            if(s.lastCommand.acceptedCount!=0 || s.lastCommand.rejectedCount!=1 || s.behaviors.contains(1)
                || !s.projectiles.empty())return fail("forced negative command passed its original restriction");}
        if(selectedWeapon)return fail("force incorrectly selected a prohibited stock weapon");
        cases<<label<<','<<type<<",30,"<<(contract==2?"Only2-profile":"forced-rejection")<<','<<source<<",2,"<<target->health<<",0,0,0,0,0,0,PASS\n";
        ++controls;std::cout<<"Submerged force attack "<<label<<" PASS original-domain/weapon/profile controls\n";return true;
    };
    if(!negative("control-Nav6-DRON","DRON","SUB",6)
        || !negative("control-AAAG-AEGIS","AEGIS","SUB",1)
        || !negative("control-Only2-ASW","ASW","DRED",2)
        || !negative("control-Verses0-STLN","STLN","SUB",0))return false;
    for(const char* type:{"DLPH","SQD"}){
        label=std::string("control-visible-enemy-ordinary-")+type;if(!filter.empty() && filter!=label)continue;
        stage="real organic sensor detection without surfacing";auto box=make(type,30,true);if(!box)return fail("visible enemy fixture");auto& s=*box;
        add(s,20,"DEST","Americans",{70,64});refreshRuntimeObjectOccupancyAfterLocalChange(s);
        {RuntimeSnapshotActiveStateScope active(s);advanceRuntimeCloakState(s);}
        const auto* target=findObjectById(s,2);const auto* source=findObjectById(s,1);const auto* weapon=s.rules.findWeaponType("105mm");
        if(!target || !source || !target->submerged || target->cloaked || !runtimeObjectVisibleToOwner(s,"Americans",*target)
            || runtimeOwnersAreAllied(s,source->owner,target->owner) || !runtimeWeaponCanTargetObject(s,*weapon,*target)
            || runtimeWeaponSlotCanTargetDomain(s,*source,RuntimeWeaponSlot::Primary,*target,false)
            || runtimeWeaponProfileForObject(s,*source,target,false).weapon
            || !runtimeWeaponProfileForObject(s,*source,target,true).weapon)return fail("normal Naval0 proof lacks visible enemy/positive weapon eligibility");
        s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*target,false);
        if(s.lastCommand.acceptedCount!=0 || s.lastCommand.rejectedCount!=1 || s.behaviors.contains(1) || !s.projectiles.empty())return fail("ordinary attack selected visible submerged enemy");
        cases<<label<<','<<type<<",30,visible-enemy-ordinary,MTNK,2,"<<target->health<<",0,0,0,0,0,0,PASS\n";
        ++controls;std::cout<<"Submerged force attack "<<label<<" PASS visible-enemy/Naval0 ordinary rejection\n";
    }
    label="control-blind-wrong-cell-SUB";
    if(filter.empty() || filter==label){stage="blind shot must not track hidden object";auto box=make("SUB",30,true);if(!box)return fail("wrong-cell fixture");auto& s=*box;
        if(!click(s,-1,{67,65}))return false;int actor=-1;for(int n=0;n<150 && actor<0;++n){step(s);if(!firstGun(s,actor))return false;}
        if(actor<0 || !deliver(s,actor,0,true,true,false))return fail("adjacent blind cell damaged/revealed or failed native impact: "+error);
        cases<<label<<",SUB,30,blind-adjacent-cell,MTNK,2,600,0,0,1,0,0,0,PASS\n";++controls;
        std::cout<<"Submerged force attack "<<label<<" PASS original AP CellSpread=.3/no hidden tracking\n";
    }
    label="control-protected-deck-ASW";
    if(filter.empty() || filter==label){stage="force cannot bypass registered deck protection";auto box=make("DEST",30,false);if(!box)return fail("deck fixture");auto& s=*box;
        const auto parent=s.specialMechanisms.carrierParents().find(2);if(parent==s.specialMechanisms.carrierParents().end() || parent->second.childBySlot.size()!=1)return fail("stock DEST child registration");
        const int childId=parent->second.childBySlot.begin()->second;const auto* child=findObjectById(s,childId);const auto* source=findObjectById(s,1);
        if(!child || child->type!="ASW" || child->health!=30 || child->airborne || child->dockedToObjectId!=2 || child->dockIndex!=0
            || !runtimeCarrierAircraftHasDeckProtection(s,*child) || !runtimeSpecialObjectHiddenForInteraction(s,*child)
            || runtimeWeaponCanTargetObject(s,*s.rules.findWeaponType("105mm"),*child)
            || runtimeWeaponProfileForObject(s,*source,child,true).weapon)return fail("force exposed true original ASW deck child");
        s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*child,true);
        if(s.lastCommand.acceptedCount!=0 || s.lastCommand.rejectedCount!=1 || s.behaviors.contains(1))return fail("direct forced deck-child command accepted");
        // A real first shell hits the parent. The child remains protected from
        // the original splash, independently of command/picking rejection.
        if(!click(s,2,{67,64}))return false;int actor=-1;for(int n=0;n<150 && actor<0;++n){step(s);if(!firstGun(s,actor))return false;}
        if(actor<0 || !stop(s))return fail("parent cannon/real Stop failed");
        bool before=false,after=false;std::string problem;ObserverScope observer(s);
        s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& p,bool done){if(p.actorId!=actor)return;
            const auto* ship=findObjectById(s,2);const auto* plane=findObjectById(s,childId);
            if(!ship || !plane || plane->health!=30 || !plane->alive || plane->airborne || plane->dockedToObjectId!=2 || plane->dockIndex!=0
                || !runtimeCarrierAircraftHasDeckProtection(s,*plane))problem="actual AP splash injured/detached protected ASW";
            if(!done){before=true;if(!ship || ship->health!=600)problem="parent not original full HP before first shell";}
            else {after=true;if(!before || !ship || ship->health!=535)problem="native AP65 did not harm parent exactly";}
            impacts<<label<<",live,"<<s.tick<<','<<p.actorId<<','<<childId<<",ASW,30,"<<(plane?plane->health:-1)<<",0,0,1,0,0,1,0,"<<(problem.empty()?"PASS":"FAIL")<<'\n';};
        for(int n=0;n<150 && !after;++n){step(s);if(!problem.empty())return fail(problem);}
        if(!before || !after)return fail("parent splash had no actual native impact witness");
        cases<<label<<",ASW,30,true-DEST-deck,MTNK,"<<childId<<",30,0,0,1,0,0,0,PASS\n";++controls;
        std::cout<<"Submerged force attack "<<label<<" PASS real AP65 parent damage/deck immunity\n";
    }
    if((filter.empty() && (completed!=24 || controls!=8 || ordinaryCount!=8 || canonicalCount!=8))
        || (!filter.empty() && completed+controls!=1))return fail("incomplete/unknown native scene selection");
    std::cout<<"Submerged force attack passed: scenes="<<completed<<" controls="<<controls<<" ordinary14="<<ordinaryCount
        <<" canonical="<<canonicalCount<<" canonicalRequiresOrdinaryBase=yes actualFileReads=yes uninterruptedPairedTicks="<<pairedTicks
        <<" stock65/49/real-Ctrl-Facade/first-native-Cannon/Stop/impact-before-hidden/after-original-reveal/visibility-and-domain-gates=ok\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
