// Issue 23/10 test-only candidate; install only after the reviewed dispatcher proposal.
bool runRuntimeTransportMotionContinuitySmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Transport continuity: " + why; return false; };
    const char* phaseEnv = std::getenv("CONCORD_TRANSPORT_MOTION_CONTINUITY_PHASE");
    const std::string phase = phaseEnv && *phaseEnv ? phaseEnv : "after";
    if (phase != "before" && phase != "after") return fail("phase must be exactly before or after");
    const bool diagnosticBefore = phase == "before";
    const char* filterEnv = std::getenv("CONCORD_TRANSPORT_MOTION_CONTINUITY_CASE");
    const std::string filter = filterEnv ? filterEnv : "";
    const char* outputEnv = std::getenv("CONCORD_TRANSPORT_MOTION_CONTINUITY_OUTPUT");
    const auto output = outputEnv && *outputEnv ? std::filesystem::path(outputEnv)
        : std::filesystem::path(config.localDataRootPath) / "issue23-transport-continuity" / phase;
    std::filesystem::create_directories(output);
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original startup/resources unavailable");
    IniFile authored, authoredArt; RulesDatabase rules; ArtDatabase art;
    if (!authored.loadFromFile(assets->report.rulesPath) || !rules.loadFromIni(authored)
        || !authoredArt.loadFromFile(assets->report.artPath) || !art.loadFromIni(authoredArt))
        return fail("original rules/art unavailable");
    assets->rules = rules; assets->art = art;
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    const std::map<std::string, std::pair<int,int>> expected{{"FV",{1,1}}, {"BFRT",{5,2}},
        {"HTK",{5,2}}, {"BUS",{5,2}}, {"DDBX",{5,2}}, {"PROPA",{5,2}},
        {"LCRF",{12,6}}, {"SAPC",{12,6}}, {"YHVR",{12,6}}, {"SHAD",{5,2}}, {"HIND",{10,2}}};
    std::ofstream catalogue(output / "catalogue.csv"), motionRows(output / "motion.csv"), cases(output / "cases.csv");
    if (!catalogue || !motionRows || !cases) return fail("evidence files unavailable");
    catalogue << "type,passengers,size_limit,speed,locomotor,mode,jumpjet_speed,climb,balloon_hover\n";
    std::set<std::string> loaded;
    for (const auto& type : rules.getVehicleTypes()) if (type.passengers > 0) {
        const auto found = expected.find(type.id);
        if (found == expected.end() || found->second != std::pair{type.passengers,type.sizeLimit})
            return fail("original transport catalogue changed: " + type.id);
        loaded.insert(type.id);
        catalogue << type.id << ',' << type.passengers << ',' << type.sizeLimit << ',' << type.speed << ','
            << type.locomotor << ',' << int(runtimeUnitMovementModeProfile(type)) << ','
            << type.jumpjetSpeed << ',' << type.jumpjetClimb << ',' << type.balloonHover << '\n';
    }
    if (loaded.size() != 11) return fail("all 11 original transport VehicleTypes must load");
    for (const auto& [id, capacity] : expected) if (!loaded.contains(id)) return fail("unregistered transport " + id);
    for (const char* id : {"SCHP","SCHD"}) {
        const auto* type = rules.findUnitType("UnitType", id);
        if (!type || type->passengers != 0) return fail(std::string(id) + " is not an authored cargo transport");
    }
    auto tileCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(*assets);
    std::optional<TerrainCell> clear, water;
    for (const auto& set : assets->theater.getTileSets()) for (int tile = set.startTileIndex;
            tile < set.startTileIndex + set.tilesInSet && (!clear || !water); ++tile) for (int sub=0; sub<16; ++sub) {
        TerrainCell cell; cell.tileIndex=tile; cell.subTileIndex=sub;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (!info.passable || info.bridge || info.cliff || info.shore
            || resolveRuntimeTerrainSubTileRampType(cell, metadata, *tileCache) != 0) continue;
        if (!clear && !info.water) clear=cell;
        if (!water && info.water && resolveRuntimeTerrainSubTileLandType(cell, metadata, *tileCache) == RuntimeTerrainLandType::Water) water=cell;
    }
    if (!clear || !water) return fail("original flat land and water TMP subtiles required");
    motionRows << "scene,tick,event,id,live,contained,hp,slot,logical_x,logical_y,body_x,body_y,body_z,air,active,host_logical_x,host_logical_y,host_x,host_y,host_z,host_air,host_active,xy_delta,xy_bound,host_distance,remaining,total,goal_x,goal_y,path_size,allowed_host,anchor_x,anchor_y,pick_x,pick_y,host_height_q16,host_climb_quantum_q16\n";
    cases << "scene,hz,core,observed_full_boarding,observed_settled_egress,violations,ordinary_files,paired_ticks,captures\n";
    motionRows << std::setprecision(17); cases << std::setprecision(17);
    std::string label; int violations=0, finished=0, coreFinished=0, saves=0, paired=0, captures=0;
    int localViolations=0, localSaves=0, localPairs=0, localCaptures=0;
    bool localBoarded=false,localUnloaded=false;
    const auto problem = [&](const std::string& why) {
        ++violations; ++localViolations; std::cout << "Transport " << label << " VIOLATION " << why << '\n';
        return diagnosticBefore ? true : fail(label + ": " + why);
    };
    struct Pose { double x=0,y=0,z=0; bool live=false,air=false,active=false; int hp=0,slot=-1,cx=0,cy=0; };
    const auto body = [](const WorldObjectInstance& object) {
        Pose p; p.x=object.hasRenderPosition ? object.renderX : object.x;
        p.y=object.hasRenderPosition ? object.renderY : object.y;
        p.z=object.hasRenderPosition ? object.renderLevel : object.level;
        if (object.kind == WorldObjectKind::Infantry && !object.airborne) {
            if (object.subCell==2) { p.x+=.25; p.y-=.25; }
            else if (object.subCell==4) { p.x+=.25; p.y+=.25; }
            else if (object.subCell==3) { p.x-=.25; p.y+=.25; }
        }
        p.live=object.alive; p.hp=object.health; p.slot=object.subCell;
        p.air=object.airborne; p.active=object.renderMoveActive; p.cx=object.x; p.cy=object.y; return p;
    };
    const auto equal = [](const Pose& a, const Pose& b) {
        return std::abs(a.x-b.x)<=1e-9 && std::abs(a.y-b.y)<=1e-9 && std::abs(a.z-b.z)<=1e-9;
    };
    const auto xy = [](const Pose& a, const Pose& b) { return std::hypot(a.x-b.x,a.y-b.y); };
    const auto bound = [&](const RuntimeWindowState& s, const WorldObjectInstance& object) {
        const auto* type=findRuntimeUnitType(s.rules,object); if (!type) return 0.0;
        if (runtimeUnitUsesJumpjetMovement(*type) && object.airborne)
            return type->jumpjetSpeed * 15.0 / (256.0*s.config.fixedTickHz) + 1e-6;
        const int leptons=std::min(255,std::clamp(type->speed,0,100)*256/100);
        const double q=leptons*15.0/(256.0*s.config.fixedTickHz);
        // Authored speed plus the upper envelope of rounding a segment's tick
        // budget down by one; does not reproduce the production route curve.
        return q>0.0 && q<1.0 ? q/(1.0-q)+1e-6 : q+1e-6;
    };
    const auto physicalHeight = [&](const RuntimeWindowState& s,int id) {
        const auto f=s.jumpjetFlights.find(id);const auto* o=findObjectById(s,id);
        return f!=s.jumpjetFlights.end()?f->second.height:std::llround((o?body(*o).z:0.0)*runtimeJumpjetCellQ16);
    };
    const auto make = [&](int hz) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box; seedRuntimeLoadScratch(s,*assets,false);
        s.config=config; s.config.fixedTickHz=hz; s.config.gameMode="Battle"; s.config.playerCountry="Americans";
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=false;
        s.config.crates=s.config.superweapons=false; s.config.startingUnits=0; s.config.gameModeRulesOverride.clear();
        s.config.skirmishLoadSavePath.clear(); s.config.playerSlots.clear();
        s.rules=rules; s.art=art; s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.mapFacts={}; s.mapFacts.theater=assets->mapFacts.theater; s.mapFacts.lighting=assets->mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y=45;y<=105;++y) for (int x=45;x<=115;++x) {
            auto cell=x>=73 && x<=80 ? *water : *clear;
            cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.level=0; s.world.accessTerrainCells().push_back(cell);
        }
        const auto structure=[&](int id,const char* type,RuntimeCellPoint cell,const char* owner) {
            WorldObjectInstance o; o.instanceId=id;o.mapId="TransportContinuity"+std::to_string(id);
            o.kind=WorldObjectKind::Structure;o.type=type;o.owner=owner;o.x=cell.x;o.y=cell.y;o.cell=runtimeCellKey(o.x,o.y);
            o.alive=o.poweredOperational=true;o.mission="Sleep";o.health=runtimeMaxHealthForObject(s,o);o.currentAmmo=-1;
            s.world.accessObjects().push_back(o);
        };
        structure(100,"GAPOWR",{109,96},"Commander"); structure(998,"NANRCT",{47,47},"SkirmishPlayer2");
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.owner="Commander";s.economy.credits=20000;s.economy.techLevel=10;
        s.economy.skirmishStartApplied=s.triggers.bootstrapComplete=true;
        RuntimeConfig::PlayerSlot local;local.enabled=local.human=local.local=true;local.name="Commander";
        local.country="Americans";local.color="Gold";RuntimeConfig::PlayerSlot hostile;
        hostile.enabled=true;hostile.country="Russians";hostile.color="Red";s.config.playerSlots={local,hostile};
        s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"]="Americans";s.skirmishCountryByOwner["skirmishplayer2"]="Russians";
        s.humanHouseAuthorities.clear();RuntimeHumanHouseAuthority authority;authority.owner="Commander";
        authority.canonicalOwner="commander";authority.country="Americans";authority.localFacade=true;
        s.humanHouseAuthorities.push_back(authority);
        ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeRepairDepotsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);ensureRuntimeMenuKeyboardLoaded(s);
        {RuntimeSnapshotActiveStateScope active(s);advanceRuntimeSpecialMechanisms(s);completeRuntimeWorldRefresh(s);}
        rebuildRuntimeEconomyPower(s);return box;
    };
    const auto add = [&](RuntimeWindowState& s,const char* type,RuntimeCellPoint cell) {
        auto* object=addRuntimeObjectInstance(s,runtimeBuildKindForUnitTypeId(s.rules,type),type,cell);
        return object ? object->instanceId : -1;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);
    };
    const auto settle = [&](RuntimeWindowState& s,int id) {
        for(int n=0;n<s.config.fixedTickHz*20;++n) {const auto* o=findObjectById(s,id);
            if(!o)return false;if(!o->airborne&&!o->renderMoveActive)return true;step(s);}
        return false;
    };
    const auto snapshot = [&](const RuntimeWindowState& s,const std::vector<int>& ids) {
        std::map<int,Pose> result;for(int id:ids)if(const auto* o=findRuntimeObjectByIdIncludingInactive(s,id))result.emplace(id,body(*o));
        return result;
    };
    const auto record = [&](RuntimeWindowState& s,int hostId,int id,const char* event,const Pose* previous) {
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);const auto* h=findObjectById(s,hostId);
        if(!o||!h)return fail(label+" missing actual actor/host");const auto p=body(*o),host=body(*h);
        const bool contained=std::find(h->transportPassengerIds.begin(),h->transportPassengerIds.end(),id)!=h->transportPassengerIds.end();
        POINT anchor{-1,-1},pick{-1,-1};
        if(o->alive && o->kind==WorldObjectKind::Infantry) {
            const RECT client{0,0,960,640},viewport=runtimeMapViewport(client);
            centerRuntimeCameraOnCell(s,client,{64,64});ensureRuntimeCameraForViewport(s,viewport);
            auto context=runtimeTerrainMetadataContext(s);context.rules=&s.rules;context.art=&s.art;
            context.palettePath=runtimePalettePath(s.config,s.report.map);context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
            context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);context.animationTick=s.tick;
            anchor=runtimeObjectWorldRenderPlacement(*o,s.world,viewport,context,s.camera,s.renderCache).center;
            invalidateRuntimeObjectProjectionCache(s);const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
            const auto it=std::find_if(projected.begin(),projected.end(),[&](const auto& q){return q.object&&q.object->instanceId==id;});
            if(it==projected.end())return fail(label+" exposed passenger missing from actual picking projection");pick=it->renderCenter;
            const POINT independent{viewport.left+int(std::lround((p.x-p.y)*s.camera.tileWidth/2.0))-s.camera.worldOffsetX,
                viewport.top+int(std::lround((p.x+p.y)*s.camera.tileHeight/2.0-p.z*s.camera.tileHeight/2.0))-s.camera.worldOffsetY};
            if((anchor.x!=independent.x||anchor.y!=independent.y||pick.x!=anchor.x||pick.y!=anchor.y)
                && !problem("same-frame passenger physical/render/picking anchor disagrees"))return false;
        }
        const auto b=s.behaviors.find(id);const RuntimeObjectBehavior* segment=b==s.behaviors.end()?nullptr:&b->second;
        const auto* hostType=findRuntimeUnitType(s.rules,*h);
        const auto climbQuantum=hostType&&runtimeUnitUsesJumpjetMovement(*hostType)
            ? std::llround(hostType->jumpjetClimb*runtimeJumpjetQ16*15.0/s.config.fixedTickHz):0;
        motionRows<<label<<','<<s.tick<<','<<event<<','<<id<<','<<p.live<<','<<contained<<','<<p.hp<<','<<p.slot<<','
            <<p.cx<<','<<p.cy<<','<<p.x<<','<<p.y<<','<<p.z<<','<<p.air<<','<<p.active<<','<<host.cx<<','<<host.cy<<','
            <<host.x<<','<<host.y<<','<<host.z<<','<<host.air<<','<<host.active<<','<<(previous?xy(*previous,p):0.0)<<','
            <<bound(s,*o)<<','<<xy(p,host)<<','<<(segment?segment->renderMoveTicksRemaining:0)<<','
            <<(segment?segment->renderMoveTicksTotal:0)<<','<<(segment?segment->moveTarget.x:-1)<<','
            <<(segment?segment->moveTarget.y:-1)<<','<<(segment?segment->path.size():0)<<','
            <<(segment?segment->pendingAllowedOccupiedObjectId:-1)<<','<<anchor.x<<','<<anchor.y<<','<<pick.x<<','<<pick.y<<','
            <<physicalHeight(s,hostId)<<','<<climbQuantum<<'\n';
        return true;
    };
    const auto inspect = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids,const std::map<int,Pose>& old,const char* event,bool command) {
        const auto* h=findObjectById(s,hostId);if(!h)return fail(label+" host died unexpectedly");const auto hp=body(*h);
        for(int id:ids) {
            const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);const auto prior=old.find(id);
            if(!o||prior==old.end()||o->health!=prior->second.hp||o->health<=0)return fail(label+" passenger identity/HP changed");
            const auto p=body(*o);const bool aboard=std::find(h->transportPassengerIds.begin(),h->transportPassengerIds.end(),id)!=h->transportPassengerIds.end();
            if(!record(s,hostId,id,event,&prior->second))return false;
            if(p.live&&aboard)return fail(label+" exposed passenger remained in hidden manifest");
            if(!p.live && (!aboard||o->mission!="InsideTransport"||p.slot!=-1||o->airborne||o->hasRenderPosition))
                return fail(label+" invalid hidden cargo authority");
            if(prior->second.live&&p.live) {
                if(command&&!equal(prior->second,p)&&!problem("command changed an exposed passenger's real body"))return false;
                if(!command&&xy(prior->second,p)>bound(s,*o)&&!problem("visible tick exceeds original speed envelope"))return false;
            } else if(prior->second.live&&!p.live) {
                if(command&&!problem("Enter command instantly hid an exposed passenger"))return false;
                if(xy(prior->second,hp)>bound(s,*o)+1e-6
                    && !problem("last visible passenger did not walk the final distance to actual host body"))return false;
            } else if(!prior->second.live&&p.live) {
                // A hidden cargo record may track logical host cells. Its first
                // visible exit must instead start from the host's physical body.
                const double allowance=command?1e-9:bound(s,*o)+1e-6;
                if(xy(p,hp)>allowance&&!problem("first visible exit appeared away from actual host body"))return false;
                if(hp.air||p.air||std::abs(p.z-hp.z)>1e-6) {
                    if(!problem("cargo appeared before carrier made real ground contact"))return false;
                }
            }
            if(aboard && !p.live && (o->cell!=h->cell||o->level!=h->level))return fail(label+" contained binding diverged from host");
        }
        for(std::size_t a=0;a<ids.size();++a)for(std::size_t b=a+1;b<ids.size();++b) {
            const auto* p=findObjectById(s,ids[a]);const auto* q=findObjectById(s,ids[b]);if(!p||!q)continue;
            const double minimum=p->kind==WorldObjectKind::Infantry?(q->kind==WorldObjectKind::Infantry?.5:.75)
                :(q->kind==WorldObjectKind::Infantry?.75:1.0);
            if(xy(body(*p),body(*q))+1e-9<minimum && !problem("exposed passengers overlap during physical entry/exit"))return false;
        }
        return true;
    };
    const auto tick = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids) {
        const auto old=snapshot(s,ids);const auto* host=findObjectById(s,hostId);if(!host)return false;
        const auto before=body(*host);const auto beforeHeight=physicalHeight(s,hostId);
        step(s);host=findObjectById(s,hostId);if(!host)return fail(label+" tick lost host");
        if(s.skirmishMatchFinished)return fail(label+" fixture battle ended: "+s.skirmishResult);
        const auto after=body(*host);const auto* type=findRuntimeUnitType(s.rules,*host);
        if(xy(before,after)>bound(s,*host)&&!problem("host tick exceeds original speed envelope"))return false;
        if(type&&runtimeUnitUsesJumpjetMovement(*type)) {
            const auto quantum=std::llround(type->jumpjetClimb*runtimeJumpjetQ16*15.0/s.config.fixedTickHz);
            if(std::abs(physicalHeight(s,hostId)-beforeHeight)>quantum
                &&!problem("physical height snapped or exceeded original Climb"))return false;
            const double wobble=type->jumpjetNoWobbles||type->jumpjetWobbles<=0.0
                ? 0.0:2.0*std::max(0.0,double(type->jumpjetDeviation))/256.0;
            const double displayBound=double(quantum)/runtimeJumpjetCellQ16+wobble+1.0/runtimeJumpjetCellQ16;
            if(std::abs(after.z-before.z)>displayBound
                &&!problem("display height exceeded original Climb plus authored wobble envelope"))return false;
        }
        return inspect(s,hostId,ids,old,"tick",false);
    };
    const auto enter = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids) {
        const auto old=snapshot(s,ids);const auto* h=findObjectById(s,hostId);if(!h)return false;const auto hostBefore=body(*h);
        applySelection(s,ids,false,false);
        const auto enterCursor=runtimeTargetCursorIntent(s,*h);bool enterDispatched=false;
        if(enterCursor==RuntimeTargetCursorIntent::Enter)
            enterDispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*h),h);
        bool queuedEnter=!ids.empty();
        for(int id:ids) {
            const auto pending=s.pendingTransportTargets.find(id);const auto move=s.behaviors.find(id);
            queuedEnter=queuedEnter&&pending!=s.pendingTransportTargets.end()&&pending->second==hostId
                &&move!=s.behaviors.end()&&move->second.kind==RuntimeBehaviorKind::Move
                &&move->second.pendingAllowedOccupiedObjectId==hostId;
        }
        const bool legacyReportedFalse=diagnosticBefore&&enterCursor==RuntimeTargetCursorIntent::Enter
            &&!enterDispatched&&queuedEnter;
        if(enterCursor!=RuntimeTargetCursorIntent::Enter||!queuedEnter||(!enterDispatched&&!legacyReportedFalse)) {
            std::ostringstream detail;detail<<label<<" actual Enter facade/cursor rejected cursor="<<int(enterCursor)
                <<" expected="<<int(RuntimeTargetCursorIntent::Enter)<<" dispatched="<<enterDispatched<<" queued="<<queuedEnter
                <<" selected="<<s.selectedObjectIds.size()<<" owner="<<s.economy.owner<<" status="<<s.interactionStatus;
            const auto* actualHost=findRuntimeObjectByIdIncludingInactive(s,hostId);
            if(actualHost)detail<<" host="<<actualHost->instanceId<<'/'<<actualHost->type<<'/'<<actualHost->owner
                <<" alive="<<actualHost->alive<<" hp="<<actualHost->health<<" air="<<actualHost->airborne
                <<" active="<<actualHost->renderMoveActive<<" capacity="<<runtimeTransportProfileForObject(s,*actualHost).passengerCapacity;
            for(int id:ids) {
                if(const auto* actualPassenger=findRuntimeObjectByIdIncludingInactive(s,id))
                    detail<<" passenger="<<id<<'/'<<actualPassenger->type<<'/'<<actualPassenger->owner
                        <<" alive="<<actualPassenger->alive<<" hp="<<actualPassenger->health<<" air="<<actualPassenger->airborne
                        <<" slot="<<actualPassenger->subCell<<" selected="<<selectionContains(s,id)
                        <<" canEnter="<<(actualHost&&runtimeObjectCanEnterTransport(s,*actualPassenger,*actualHost,true));
                const auto pending=s.pendingTransportTargets.find(id);const auto move=s.behaviors.find(id);
                detail<<" intentId="<<id<<" pendingHost="<<(pending==s.pendingTransportTargets.end()?-1:pending->second)
                    <<" hasMove="<<(move!=s.behaviors.end());
                if(move!=s.behaviors.end())detail<<" kind="<<int(move->second.kind)<<" state="<<int(move->second.state)
                    <<" allowedHost="<<move->second.pendingAllowedOccupiedObjectId<<" goal="<<move->second.moveTarget.x<<','<<move->second.moveTarget.y
                    <<" path="<<move->second.path.size()<<" cooldown="<<move->second.moveCooldownTicks
                    <<" remaining="<<move->second.renderMoveTicksRemaining<<" total="<<move->second.renderMoveTicksTotal;
            }
            return fail(detail.str());
        }
        if(legacyReportedFalse&&!problem("legacyAPIReportedFalse: actual per-passenger host intent and Move authority created"))return false;
        h=findObjectById(s,hostId);if(!h)return false;
        if(!equal(hostBefore,body(*h))&&!problem("Enter command changed current host body"))return false;
        return inspect(s,hostId,ids,old,"input-enter",true);
    };
    const auto unload = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids) {
        const auto old=snapshot(s,ids);const auto* h=findObjectById(s,hostId);if(!h)return false;const auto before=body(*h);
        applySelection(s,{hostId},false,false);
        if(!dispatchRuntimeSecondaryActionFromUi(s,hostId))return fail(label+" actual Secondary unload rejected");
        h=findObjectById(s,hostId);if(!h)return false;
        if(!equal(before,body(*h))&&!problem("Unload command snapped the host's current physical pose"))return false;
        return inspect(s,hostId,ids,old,"input-unload",true);
    };
    const auto move = [&](RuntimeWindowState& s,int id,RuntimeCellPoint goal) {
        const auto* o=findObjectById(s,id);if(!o)return false;const auto before=body(*o);applySelection(s,{id},false,false);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,goal))return fail(label+" actual Move rejected");
        o=findObjectById(s,id);return o && (equal(before,body(*o))||problem("Move command changed current body"));
    };
    const auto stop = [&](RuntimeWindowState& s,int id) {
        const auto* o=findObjectById(s,id);if(!o)return false;const auto before=body(*o);applySelection(s,{id},false,false);
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))return fail(label+" actual Stop rejected");
        o=findObjectById(s,id);if(!o)return false;
        if(!equal(before,body(*o))&&!problem("Stop command changed current body"))return false;
        return !s.pendingTransportTargets.contains(id) ? true : fail(label+" Stop retained pending boarding authority");
    };
    const auto arrived = [&](const RuntimeWindowState& s,int hostId,const std::vector<int>& ids,bool aboard) {
        const auto* host=findObjectById(s,hostId);if(!host)return false;
        for(int id:ids){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o)return false;
            const bool contained=std::find(host->transportPassengerIds.begin(),host->transportPassengerIds.end(),id)!=host->transportPassengerIds.end();
            if(aboard?(!contained||o->alive||s.pendingTransportTargets.contains(id))
                :(contained||!o->alive||o->airborne||o->renderMoveActive||s.behaviors.contains(id)))return false;}
        return true;
    };
    const auto wait = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids,bool aboard) {
        for(int n=0;n<s.config.fixedTickHz*60;++n){if(arrived(s,hostId,ids,aboard)){if(aboard)localBoarded=true;else localUnloaded=true;return true;}if(!tick(s,hostId,ids))return false;}
        return fail(label+" natural entry/exit exceeded 60-second bound tick="+std::to_string(s.tick));
    };
    const auto checkpoint = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids,const std::string& suffix) {
        const auto file=output/(label+"-"+suffix+"-ordinary14.ini");
        if(!saveRuntimeState(s,file.string(),error))return std::unique_ptr<RuntimeWindowState>{};
        IniFile actual;if(!actual.loadFromFile(file.string())||actual.getValue("RuntimeSave","Version","")!="14") {
            error="actual complete ordinary14 file readback";return std::unique_ptr<RuntimeWindowState>{};}
        auto clone=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*clone,s,false);
        if(!loadRuntimeState(*clone,file.string(),error))return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*clone);
        const auto& a=s.world.getTerrainCells();const auto& b=clone->world.getTerrainCells();
        if(a.size()!=b.size()){error="ordinary load replaced synthetic TMP fixture";return std::unique_ptr<RuntimeWindowState>{};}
        for(std::size_t i=0;i<a.size();++i)if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].cellIndex512!=b[i].cellIndex512
            ||a[i].tileIndex!=b[i].tileIndex||a[i].subTileIndex!=b[i].subTileIndex||a[i].level!=b[i].level) {
            error="ordinary load changed actual fixture cell";return std::unique_ptr<RuntimeWindowState>{};}
        const auto* host=findObjectById(s,hostId);const auto* restored=findObjectById(*clone,hostId);
        if(!host||!restored||host->transportPassengerIds!=restored->transportPassengerIds||!equal(body(*host),body(*restored))) {
            error="ordinary load changed host body/manifest";return std::unique_ptr<RuntimeWindowState>{};}
        for(int id:ids){const auto* x=findRuntimeObjectByIdIncludingInactive(s,id);const auto* y=findRuntimeObjectByIdIncludingInactive(*clone,id);
            if(!x||!y||x->alive!=y->alive||x->health!=y->health||x->subCell!=y->subCell||!equal(body(*x),body(*y))) {
                error="ordinary load changed passenger body/identity";return std::unique_ptr<RuntimeWindowState>{};}}
        ++saves;++localSaves;return clone;
    };
    const auto compare = [&](const RuntimeWindowState& a,const RuntimeWindowState& b,int hostId,const std::vector<int>& ids) {
        if(a.tick!=b.tick)return fail(label+" ordinary saved-vs-live tick differs");
        const auto* x=findObjectById(a,hostId);const auto* y=findObjectById(b,hostId);
        if(!x||!y||!equal(body(*x),body(*y))||x->airborne!=y->airborne||x->transportPassengerIds!=y->transportPassengerIds
            ||serializeRuntimeActiveJumpjetFlights(a)!=serializeRuntimeActiveJumpjetFlights(b))return fail(label+" saved-vs-live host/height/manifest differs");
        for(int id:ids){const auto* p=findRuntimeObjectByIdIncludingInactive(a,id);const auto* q=findRuntimeObjectByIdIncludingInactive(b,id);
            if(!p||!q||!equal(body(*p),body(*q))||p->alive!=q->alive||p->health!=q->health||p->subCell!=q->subCell
                ||p->mission!=q->mission||a.pendingTransportTargets.contains(id)!=b.pendingTransportTargets.contains(id))
                {std::ostringstream detail;detail<<std::setprecision(17)<<label<<" ordinary saved-vs-live passenger trajectory differs id="<<id;
                    const auto append=[&](const char* tag,const RuntimeWindowState& state,const WorldObjectInstance* object) {
                        detail<<' '<<tag<<" tick="<<state.tick;if(!object){detail<<" missing";return;}
                        const auto pose=body(*object);detail<<" alive="<<object->alive<<" hp="<<object->health<<" slot="<<object->subCell
                            <<" logical="<<object->x<<','<<object->y<<" body="<<pose.x<<','<<pose.y<<','<<pose.z
                            <<" mission="<<object->mission<<" hasPose="<<object->hasRenderPosition<<" active="<<object->renderMoveActive
                            <<" layer="<<(object->hasRenderPosition?object->renderOnBridge:object->onBridge);
                        const auto pending=state.pendingTransportTargets.find(id);detail<<" pending="<<(pending==state.pendingTransportTargets.end()?-1:pending->second);
                        const auto found=state.behaviors.find(id);if(found==state.behaviors.end()){detail<<" behavior=none";return;}
                        const auto& move=found->second;detail<<" kind="<<int(move.kind)<<" state="<<int(move.state)<<" goal="<<move.moveTarget.x<<','<<move.moveTarget.y
                            <<" cooldown="<<move.moveCooldownTicks<<" clocks="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal
                            <<" pathPending="<<move.pathRequestPending<<" blocked="<<move.blockedTicks<<" pathSize="<<move.path.size();
                        if(!move.path.empty())detail<<" pathFront="<<move.path.front().x<<','<<move.path.front().y<<" pathBack="<<move.path.back().x<<','<<move.path.back().y;
                        detail<<" allowed="<<move.pendingAllowedOccupiedObjectId<<" transfer="<<int(move.transportTransferKind)<<'/'<<move.transportTransferHostId
                            <<" from="<<move.renderFrom.x<<','<<move.renderFrom.y<<','<<move.renderFromLevel<<" fromOffset="<<move.renderFromOffsetX<<','<<move.renderFromOffsetY
                            <<" to="<<move.renderTo.x<<','<<move.renderTo.y<<','<<move.renderToLevel<<" toOffset="<<move.renderToOffsetX<<','<<move.renderToOffsetY;
                    };append("live",a,p);append("restored",b,q);return fail(detail.str());}}
        ++paired;++localPairs;return true;
    };
    struct DiskReplicas { std::unique_ptr<RuntimeWindowState> ordinary, canonical; };
    int canonicalFiles=0;
    const auto checkpointBoth = [&](RuntimeWindowState& s,int hostId,const std::vector<int>& ids,const std::string& suffix) {
        auto replicas=std::make_unique<DiskReplicas>();
        replicas->ordinary=checkpoint(s,hostId,ids,suffix);
        if(!replicas->ordinary)return std::unique_ptr<DiskReplicas>{};
        const auto ordinaryFile=output/(label+"-"+suffix+"-ordinary14.ini");
        std::ifstream actualBase(ordinaryFile,std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(actualBase),std::istreambuf_iterator<char>{}};
        if(actualBase.bad()||bytes.empty()){error="actual ordinary base file unavailable";return std::unique_ptr<DiskReplicas>{};}
        std::ostringstream extended,normalized;extended<<bytes;IniFile authority;
        if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!authority.loadFromString(extended.str())
            ||!writeRuntimeMultiplayerSnapshotCanonicalIni(authority,normalized,error))return std::unique_ptr<DiskReplicas>{};
        const auto canonicalFile=output/(label+"-"+suffix+"-canonical-image.ini");
        {std::ofstream disk(canonicalFile,std::ios::binary);disk<<normalized.str();disk.close();
            if(!disk.good()){error="canonical authority image disk write failed";return std::unique_ptr<DiskReplicas>{};}}
        IniFile actualAuthority;if(!actualAuthority.loadFromFile(canonicalFile.string())) {
            error="canonical authority image actual disk readback";return std::unique_ptr<DiskReplicas>{};}
        replicas->canonical=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*replicas->canonical,s,false);
        if(!loadRuntimeState(*replicas->canonical,ordinaryFile.string(),error))return std::unique_ptr<DiskReplicas>{};
        {RuntimeSnapshotActiveStateScope active(*replicas->canonical);
            if(!materializeRuntimeMultiplayerSnapshotExtension(*replicas->canonical,actualAuthority,error))
                return std::unique_ptr<DiskReplicas>{};}
        initializeRuntimeOfflineGameplayRouting(*replicas->canonical);
        if(!compare(s,*replicas->ordinary,hostId,ids)||!compare(s,*replicas->canonical,hostId,ids))
            return std::unique_ptr<DiskReplicas>{};
        ++canonicalFiles;return replicas;
    };
    const auto pairTwelve = [&](RuntimeWindowState& s,DiskReplicas& replicas,int hostId,const std::vector<int>& ids,bool compareMoveGoals=false) {
        for(int n=0;n<12;++n) {
            if(!tick(s,hostId,ids))return false;step(*replicas.ordinary);step(*replicas.canonical);
            for(const auto* copy:{replicas.ordinary.get(),replicas.canonical.get()}) {
                if(!compare(s,*copy,hostId,ids))return false;
                for(int id:ids) {
                    if(compareMoveGoals) {
                        const auto live=s.behaviors.find(id);const auto saved=copy->behaviors.find(id);
                        if((live==s.behaviors.end())!=(saved==copy->behaviors.end())
                            ||(live!=s.behaviors.end()&&(live->second.kind!=saved->second.kind
                                ||live->second.moveTarget!=saved->second.moveTarget)))
                            return fail(label+" restored mid-egress accepted kind/goal differs");
                    }
                    const auto p=s.pendingTransportTargets.find(id);const auto q=copy->pendingTransportTargets.find(id);
                    if((p==s.pendingTransportTargets.end())!=(q==copy->pendingTransportTargets.end())
                        ||(p!=s.pendingTransportTargets.end()&&p->second!=q->second))return fail(label+" restored exact transport intent differs");
                    const auto* a=findRuntimeObjectByIdIncludingInactive(s,id);const auto* b=findRuntimeObjectByIdIncludingInactive(*copy,id);
                    if(!a||!b||a->x!=b->x||a->y!=b->y||a->onBridge!=b->onBridge
                        ||(a->hasRenderPosition?a->renderOnBridge:a->onBridge)!=(b->hasRenderPosition?b->renderOnBridge:b->onBridge))
                        return fail(label+" restored transport source/layer differs");
                }
            }
        }
        return true;
    };
    const auto finalBoardingSample = [&](RuntimeWindowState& s,int hostId,int id) {
        for(int n=0;n<s.config.fixedTickHz*60;++n) {
            const auto* p=findObjectById(s,id);const auto* h=findObjectById(s,hostId);
            if(!p||!h)break;const auto intent=s.pendingTransportTargets.find(id);
            const double distance=xy(body(*p),body(*h));const auto move=s.behaviors.find(id);
            if(p->renderMoveActive&&intent!=s.pendingTransportTargets.end()&&intent->second==hostId
                &&distance>.12&&distance<.60&&move!=s.behaviors.end()&&move->second.renderMoveTicksRemaining>24)return true;
            if(!tick(s,hostId,{id}))return false;
        }
        return problem("no actual visible last boarding segment available for cancellation control")&&diagnosticBefore;
    };
    const auto capture = [&](RuntimeWindowState& s,int id,const std::string& suffix) {
        const auto* o=findObjectById(s,id);if(!o)return false;
        const auto stats=drawRuntimeStateOffscreenAtCell(s,runtimeCellPointForObject(*o),960,640,error,
            (output/(label+"-"+suffix+".bmp")).string(),false,true);
        if(!stats||!stats->rendered)return fail(label+" original real-frame render: "+error);
        ++captures;++localCaptures;return true;
    };
    const auto scene = [&](const std::string& name,int hz,bool core,const auto& run) {
        if(!filter.empty()&&filter!=name)return true;label=name;localViolations=localSaves=localPairs=localCaptures=0;localBoarded=localUnloaded=false;
        if(!run())return false;++finished;coreFinished+=core;
        cases<<name<<','<<hz<<','<<core<<','<<localBoarded<<','<<localUnloaded<<','<<localViolations<<','<<localSaves<<','<<localPairs<<','<<localCaptures<<'\n';
        std::cout<<"Transport "<<name<<' '<<(localViolations?"DIAGNOSTIC-FAIL":"PASS")<<'\n';return true;
    };
    for(const auto& [type,capacity]:expected)for(int hz:{15,30,60}) {
        if(!scene(type+"-"+std::to_string(hz)+"-cycle",hz,true,[&]() {
            auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
            const int host=add(s,type.c_str(),{64,64}),passenger=add(s,"E1",{58,64});
            if(host<0||passenger<0||!settle(s,host))return fail(label+" original actor/ground contact");
            const std::vector<int> ids{passenger};if(!enter(s,host,ids))return false;
            std::unique_ptr<RuntimeWindowState> clone;bool saved=false,nav=false,pictured=false;int pairs=0;
            for(int n=0;n<hz*60&&!arrived(s,host,ids,true);++n) {
                if(!tick(s,host,ids))return false;const auto* p=findObjectById(s,passenger);
                if(p&&p->renderMoveActive&&!nav) {const auto before=body(*p);initializeRuntimeNavigation(s);
                    p=findObjectById(s,passenger);if(!p)return fail(label+" nav lost actor");
                    if(!equal(before,body(*p))&&!problem("nav rebuild changed in-progress passenger body"))return false;nav=true;}
                if(clone&&pairs<12){step(*clone);if(!compare(s,*clone,host,ids))return false;++pairs;}
                if(p&&p->renderMoveActive&&!saved&&hz==30&&(type=="FV"||type=="LCRF"||type=="SHAD"||type=="HIND")) {
                    clone=checkpoint(s,host,ids,"approach");if(!clone)return fail(label+" actual ordinary approach checkpoint: "+error);saved=true;}
                if(p&&p->renderMoveActive&&!pictured&&hz==30&&(type=="FV"||type=="SHAD")) {
                    if(!capture(s,passenger,"approach"))return false;pictured=true;}
            }
            if(!arrived(s,host,ids,true)||!nav||(saved&&pairs!=12))return fail(label+" incomplete native approach/save sequence");localBoarded=true;
            clone.reset();const auto* rule=rules.findUnitType("UnitType",type);
            if(!rule)return fail(label+" original transport missing");
            if(runtimeUnitUsesAutomaticJumpjetLanding(*rule)) {
                if(!move(s,host,{70,64}))return false;bool launched=false;
                for(int n=0;n<hz*20&&!launched;++n){if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
                    const auto flight=s.jumpjetFlights.find(host);
                    launched=h&&h->airborne&&h->renderMoveActive&&h->hasRenderPosition
                        &&flight!=s.jumpjetFlights.end()&&flight->second.progress>0;}
                if(!launched)return fail(label+" original loaded flight never began");
                if(!stop(s,host))return false;const auto hover=body(*findObjectById(s,host));
                for(int n=0;n<15;++n){if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
                    if(!h||!h->airborne||xy(hover,body(*h))>1e-9||!arrived(s,host,ids,true))
                        return fail(label+" original loaded Stop hover/cargo contract changed");}
                if(!move(s,host,{70,64}))return false;
                for(int n=0;n<hz*20;++n){if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
                    if(h&&h->airborne&&h->renderMoveActive)break;if(n==hz*20-1)return fail(label+" flight resume missing");}
            } else {
                if(!move(s,host,{67,64}))return false;
                for(int n=0;n<hz*30;++n){if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
                    if(h&&h->x==67&&h->y==64&&!h->renderMoveActive)break;if(n==hz*30-1)return fail(label+" loaded ground transit");}
            }
            if(!unload(s,host,ids))return false;
            // Capture and restore the actual pending exit/descent state, never
            // change HP, air flags, timer budgets or original phase clocks.
            if(hz==30&&(type=="FV"||type=="SHAD")){clone=checkpoint(s,host,ids,"unload");
                if(!clone)return fail(label+" actual ordinary exit/landing checkpoint: "+error);pairs=0;}
            for(int n=0;n<hz*60;++n){if(!tick(s,host,ids))return false;
                if(clone&&pairs<12){step(*clone);if(!compare(s,*clone,host,ids))return false;++pairs;}
                if(arrived(s,host,ids,false)&&(!clone||pairs==12))break;
                if(n==hz*60-1)return fail(label+" natural continuous unload/descent timeout");}
            if(!arrived(s,host,ids,false))return fail(label+" exit did not settle");localUnloaded=true;
            const auto* p=findObjectById(s,passenger);const auto* h=findObjectById(s,host);
            if(!p||!h||p->subCell<0||p->health!=rules.findUnitType("InfantryType","E1")->strength)
                return fail(label+" final original infantry identity/HP/slot");
            if(xy(body(*p),body(*h))+.000000001<.75)return fail(label+" settled passenger overlaps exterior host body");
            if(hz==30&&(type=="FV"||type=="SHAD"))if(!capture(s,passenger,"egress-settled"))return false;
            return true;
        }))return false;
    }
    if(!scene("FV-30-walking-enter-stop-reissue",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const int host=add(s,"FV",{67,64}),p=add(s,"E1",{58,64});if(host<0||p<0)return fail("boundary actors");
        const std::vector<int> ids{p};if(!move(s,p,{63,64}))return false;
        for(int n=0;n<300;++n){if(!tick(s,host,ids))return false;const auto* o=findObjectById(s,p);
            if(o&&o->renderMoveActive&&o->hasRenderPosition&&std::abs(body(*o).x-o->x)>.02)break;
            if(n==299)return fail(label+" actual fractional walking source missing");}
        if(!enter(s,host,ids))return false;
        for(int n=0;n<2;++n)if(!tick(s,host,ids))return false;
        if(!enter(s,host,ids)||!stop(s,p))return false;const auto resting=body(*findObjectById(s,p));
        for(int n=0;n<5;++n){if(!tick(s,host,ids))return false;const auto* o=findObjectById(s,p);
            if(!o||!equal(resting,body(*o))||s.pendingTransportTargets.contains(p))return fail(label+" stopped boarding moved/retained intent");}
        return enter(s,host,ids)&&wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("HTK-30-moving-host-enter",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const int host=add(s,"HTK",{64,64}),p=add(s,"E1",{59,64});if(host<0||p<0)return fail("moving host actors");
        const std::vector<int> ids{p};if(!move(s,host,{70,64}))return false;
        for(int n=0;n<3;++n)if(!tick(s,host,ids))return false;
        if(!enter(s,host,ids))return false;for(int n=0;n<5;++n)if(!tick(s,host,ids))return false;
        if(!enter(s,host,ids)||!stop(s,host))return false;
        return wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("LCRF-30-full-vehicle-water-exit",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);const int host=add(s,"LCRF",{64,64});
        std::vector<int> ids;for(int n=0;n<4;++n)ids.push_back(add(s,"MTNK",{58,60+n*2}));
        const int excess=add(s,"MTNK",{58,70});if(host<0||excess<0||std::find(ids.begin(),ids.end(),-1)!=ids.end())return fail("mixed original actors");
        if(!enter(s,host,ids)||runtimeTransportReservedPassengerCount(s,host)!=12||!wait(s,host,ids,true))return false;
        const auto* h=findObjectById(s,host);applySelection(s,{excess},false,false);
        if(!h||runtimeTargetCursorIntent(s,*h)!=RuntimeTargetCursorIntent::NoEnter)return fail(label+" full Size capacity cursor");
        const auto excessBefore=body(*findObjectById(s,excess));const auto fullManifest=h->transportPassengerIds;
        const bool handled=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*h),h);
        const auto* extra=findObjectById(s,excess);h=findObjectById(s,host);
        if(!extra||!h||!equal(excessBefore,body(*extra))||extra->health!=excessBefore.hp||s.pendingTransportTargets.contains(excess)
            ||h->transportPassengerIds!=fullManifest||runtimeTransportUsedSpace(s,*h)!=12)return fail(label+" full hold overbooked/lost excess");
        if(!record(s,host,excess,handled?"full-input-handled":"full-input-rejected",&excessBefore))return false;
        if(!move(s,host,{76,64}))return false;for(int n=0;n<1800;++n){if(!tick(s,host,ids))return false;h=findObjectById(s,host);
            if(h&&h->x==76&&h->y==64&&!h->renderMoveActive)break;if(n==1799)return fail(label+" loaded native water crossing");}
        if(!unload(s,host,ids)||!arrived(s,host,ids,true))return fail(label+" ground vehicles escaped into deep water");
        if(!move(s,host,{82,64}))return false;for(int n=0;n<1800;++n){if(!tick(s,host,ids))return false;h=findObjectById(s,host);
            if(h&&h->x==82&&h->y==64&&!h->renderMoveActive)break;if(n==1799)return fail(label+" far-shore transit");}
        return unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("BFRT-30-full-foot-blocked-exit",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);const int host=add(s,"BFRT",{64,64});
        std::vector<int> ids;for(int n=0;n<5;++n)ids.push_back(add(s,"E1",{57,60+n*2}));
        const int excess=add(s,"E1",{58,72});if(host<0||excess<0||std::find(ids.begin(),ids.end(),-1)!=ids.end())return fail("full foot actors");
        if(!enter(s,host,ids)||!wait(s,host,ids,true))return false;const auto* h=findObjectById(s,host);applySelection(s,{excess},false,false);
        if(!h||runtimeTargetCursorIntent(s,*h)!=RuntimeTargetCursorIntent::NoEnter)return fail(label+" full five-person capacity");
        std::vector<int> blockers;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
            blockers.push_back(add(s,"MTNK",{64+dx,64+dy}));
        if(std::find(blockers.begin(),blockers.end(),-1)!=blockers.end())return fail(label+" original blocking vehicles");
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        h=findObjectById(s,host);if(!h)return fail(label+" blocked host missing after actual blocker creation");
        const auto originalManifest=h->transportPassengerIds;const int originalMode=h->activeGunnerMode;
        if(!unload(s,host,ids))return false;
        if(!arrived(s,host,ids,true)){if(!problem("sealed adjacent exit released hidden cargo beyond blockers"))return false;return diagnosticBefore;}
        for(int n=0;n<12;++n) {h=findObjectById(s,host);
            if(!h||h->transportPassengerIds!=originalManifest||h->activeGunnerMode!=originalMode)
                return fail(label+" blocked unload reordered cargo or changed the first gunner");
            if(!tick(s,host,ids)||!arrived(s,host,ids,true))return fail(label+" sealed exit changed hidden cargo authority");}
        for(std::size_t i=0;i<blockers.size();++i)if(!move(s,blockers[i],{52+int(i)*2,86}))return false;
        bool cleared=false,midEgressSaved=false;int clearingTicks=0;
        while(clearingTicks<1800) {
            const int beforeTick=s.tick;
            if(!tick(s,host,ids))return false;
            if(s.tick-beforeTick!=1)return fail(label+" clearing tick did not advance once");
            clearingTicks+=s.tick-beforeTick;
            const auto* currentHost=findObjectById(s,host);
            if(!currentHost)return fail(label+" mid-egress host missing");
            bool activeExit=false;
            for(int id:ids) {
                const auto* passenger=findObjectById(s,id);const auto b=s.behaviors.find(id);
                activeExit=activeExit||(passenger&&passenger->renderMoveActive
                    &&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move);
            }
            if(!midEgressSaved&&!currentHost->transportPassengerIds.empty()
                &&currentHost->transportPassengerIds.size()<ids.size()&&activeExit) {
                if(clearingTicks+12>1800)return fail(label+" mid-egress pair exceeded original clearing budget");
                auto replicas=checkpointBoth(s,host,ids,"mid-egress-future-goals");
                if(!replicas)return fail(label+" mid-egress actual disk restore: "+error);
                for(const auto* copy:{replicas->ordinary.get(),replicas->canonical.get()}) {
                    for(int id:ids) {
                        const auto live=s.behaviors.find(id);const auto saved=copy->behaviors.find(id);
                        if((live==s.behaviors.end())!=(saved==copy->behaviors.end())
                            ||(live!=s.behaviors.end()&&(live->second.kind!=saved->second.kind
                                ||live->second.moveTarget!=saved->second.moveTarget)))
                            return fail(label+" initial mid-egress restored kind/goal differs");
                    }
                }
                const int pairStarted=s.tick;
                if(!pairTwelve(s,*replicas,host,ids,true))return false;
                if(s.tick-pairStarted!=12)return fail(label+" mid-egress pair did not advance twelve live ticks");
                clearingTicks+=s.tick-pairStarted;midEgressSaved=true;
            }
            cleared=true;currentHost=findObjectById(s,host);
            if(!currentHost)return fail(label+" cleared host missing after paired ticks");
            for(int id:blockers){const auto* o=findObjectById(s,id);if(!o||xy(body(*o),body(*currentHost))<4.0)cleared=false;}
            if(cleared&&midEgressSaved)break;
        }
        if(!cleared)return fail(label+" actual blocker moves never cleared exit");
        if(!midEgressSaved)return fail(label+" real partial-cargo active egress disk checkpoint was not observed");
        h=findObjectById(s,host);if(!h)return fail(label+" cleared host missing");
        if(h->transportPassengerIds.empty()) {
            // The accepted Unload continues while real blockers leave. An
            // already empty hold has no contextual Secondary action to repeat.
            const auto old=snapshot(s,ids);const auto before=body(*h);
            const int mode=h->activeGunnerMode;const auto mission=h->mission;
            const bool available=runtimeObjectHasSecondaryAction(s,*h);
            applySelection(s,{host},false,false);
            const bool handled=dispatchRuntimeSecondaryActionFromUi(s,host);
            h=findObjectById(s,host);
            if(available||handled||!h||!equal(before,body(*h))||h->health!=before.hp
                ||h->airborne!=before.air||h->renderMoveActive!=before.active
                ||h->x!=before.cx||h->y!=before.cy||h->mission!=mission
                ||!h->transportPassengerIds.empty()||h->activeGunnerMode!=mode)
                return fail(label+" empty repeat unload was not a harmless rejection");
            if(!inspect(s,host,ids,old,"empty-unload-rejected",true))return false;
            return wait(s,host,ids,false);
        }
        return unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("LCRF-30-sole-shore-stopped-foot",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        for(auto& c:s.world.accessTerrainCells())if(c.x>=72&&c.x<=74&&c.y>=63&&c.y<=65) {
            const bool access=c.x==72&&c.y==64;const auto& original=access?*clear:*water;
            c.tileIndex=original.tileIndex;c.subTileIndex=original.subTileIndex;
        }
        initializeRuntimeNavigation(s);
        const int host=add(s,"LCRF",{73,64}),p=add(s,"E1",{70,64});const std::vector<int> ids{p};
        if(host<0||p<0||!move(s,p,{72,64}))return fail(label+" sole-shore actors/actual approach");
        int accessCount=0;const auto* passenger=findObjectById(s,p);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)
            if(findRuntimePassableCellNear(s,*passenger,{73+dx,64+dy},0,-1))++accessCount;
        if(accessCount!=1)return fail(label+" exactly one original-land adjacent access is required");
        bool fractional=false;for(int n=0;n<600;++n) {
            if(!tick(s,host,ids))return false;passenger=findObjectById(s,p);if(!passenger)return false;
            const auto current=body(*passenger);const auto slot=worldObjectSubCellOffset(*passenger);
            if(passenger->x==72&&passenger->y==64&&passenger->renderMoveActive
                &&std::abs(current.x-(72+slot.x))>.10){fractional=true;break;}
        }
        if(!fractional||!stop(s,p))return fail(label+" natural noncanonical Stop at sole access missing");
        const auto stopped=body(*findObjectById(s,p));
        bool canonical=false;for(int slot:{2,4,3}) {const auto offset=worldInfantrySubCellOffset(slot);
            canonical=canonical||std::hypot(stopped.x-72-offset.x,stopped.y-64-offset.y)<1e-9;}
        if(canonical)return fail(label+" Stop accidentally selected a canonical body");
        if(!enter(s,host,ids)||!wait(s,host,ids,true))return false;
        return unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("LCRF-30-fractional-host-egress",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const int host=add(s,"LCRF",{64,64});const std::vector<int> ids{add(s,"E1",{58,63}),add(s,"E1",{58,66})};
        if(host<0||std::find(ids.begin(),ids.end(),-1)!=ids.end()||!enter(s,host,ids)||!wait(s,host,ids,true)
            ||!move(s,host,{68,64}))return false;
        bool fractional=false;for(int n=0;n<300;++n) {
            if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
            if(h&&h->renderMoveActive&&std::abs(body(*h).x-h->x)>.10){fractional=true;break;}
        }
        if(!fractional||!stop(s,host))return fail(label+" natural fractional LCRF Stop unavailable");
        const auto hull=body(*findObjectById(s,host));
        if(!unload(s,host,ids))return false;
        auto replicas=checkpointBoth(s,host,ids,"accepted-fractional-egress");
        if(!replicas)return fail(label+" actual accepted egress dual-format checkpoint: "+error);
        if(!pairTwelve(s,*replicas,host,ids)||!wait(s,host,ids,false))return false;
        const auto* h=findObjectById(s,host);if(!h||!equal(hull,body(*h)))return fail(label+" resting hull moved during unload");
        for(int id:ids) {const auto* p=findObjectById(s,id);if(!p||xy(body(*p),body(*h))+1e-9<.75)
            return fail(label+" final foot body failed true hull clearance");}
        return true;
    }))return false;
    for(const char* type:{"FV","HIND"})if(!scene(std::string(type)+"-30-unload-moving-host",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);const int host=add(s,type,{64,64});
        std::vector<int> ids{add(s,"E1",{58,64})};if(std::string(type)=="HIND")ids.push_back(add(s,"E1",{58,67}));
        if(host<0||std::find(ids.begin(),ids.end(),-1)!=ids.end()||!settle(s,host)||!enter(s,host,ids)
            ||!wait(s,host,ids,true)||!move(s,host,{69,64}))return false;
        bool traveled=false;for(int n=0;n<600;++n) {
            if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
            const auto f=s.jumpjetFlights.find(host);
            traveled=h&&h->renderMoveActive&&xy(body(*h),Pose{64,64})>.03
                &&(std::string(type)=="FV"||(h->airborne&&f!=s.jumpjetFlights.end()&&f->second.progress>0));
            if(traveled)break;
        }
        if(!traveled||!unload(s,host,ids))return fail(label+" actual moving unload unavailable");
        auto replicas=checkpointBoth(s,host,ids,"pending-natural-unload");
        if(!replicas)return fail(label+" moving-host unload checkpoint: "+error);
        if(!pairTwelve(s,*replicas,host,ids))return false;
        for(int n=0;n<1800;++n) {
            const auto* h=findObjectById(s,host);if(!h)return false;
            if(!h->transportPassengerIds.empty()&&toLowerAsciiLocal(h->mission)!=toLowerAsciiLocal(runtimeMissionName(RuntimeMissionIntent::Unloading)))
                if(!problem("accepted moving-host unload lost its mission while cargo remained"))return false;
            if(arrived(s,host,ids,false)){localUnloaded=true;return true;}
            if(!tick(s,host,ids))return false;
        }
        return fail(label+" pending unload did not finish after old ground/flight segment");
    }))return false;
    for(const char* action:{"Stop","NewMove","HostMove"})if(!scene(std::string("FV-30-final-entry-")+action,30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const int host=add(s,"FV",{65,64}),p=add(s,"E1",{61,64});const std::vector<int> ids{p};
        if(host<0||p<0||!enter(s,host,ids)||!finalBoardingSample(s,host,p))return false;
        if(!findObjectById(s,p))return diagnosticBefore;
        auto inflight=checkpointBoth(s,host,ids,"visible-final-entry");
        if(!inflight)return fail(label+" actual in-flight final entry save: "+error);
        if(!pairTwelve(s,*inflight,host,ids))return false;
        const auto* stillWalking=findObjectById(s,p);
        if(!stillWalking||!stillWalking->renderMoveActive)return fail(label+" final-entry cancellation sample already completed");
        const auto before=snapshot(s,ids);
        if(std::string(action)=="Stop") {if(!stop(s,p))return false;}
        else if(std::string(action)=="NewMove") {if(!move(s,p,{60,60}))return false;}
        else if(!move(s,host,{68,64}))return false;
        if(!inspect(s,host,ids,before,"final-entry-cancel-input",true))return false;
        if(std::string(action)!="HostMove"&&s.pendingTransportTargets.contains(p))return fail(label+" canceled input retained entry authority");
        auto replicas=checkpointBoth(s,host,ids,"canceled-or-host-invalid");
        if(!replicas)return fail(label+" actual final-entry cancellation save: "+error);
        if(!pairTwelve(s,*replicas,host,ids))return false;
        if(std::string(action)=="Stop") {
            const auto* stopped=findObjectById(s,p);if(!stopped||!equal(before.at(p),body(*stopped))
                ||s.pendingTransportTargets.contains(p))return fail(label+" real stopped body/authority changed after save");
            return enter(s,host,ids)&&wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
        }
        if(std::string(action)=="NewMove") {
            for(int n=0;n<1800;++n) {const auto* current=findObjectById(s,p);if(!current)return fail(label+" canceled passenger hid");
                if(current->x==60&&current->y==60&&!current->renderMoveActive&&!s.behaviors.contains(p))return true;
                if(!tick(s,host,ids))return false;}
            return fail(label+" canceled passenger did not reach its real new Move goal");
        }
        const auto* canceled=findObjectById(s,p);const auto* currentHost=findObjectById(s,host);
        if(!canceled||!currentHost||canceled->renderMoveActive||!equal(before.at(p),body(*canceled))
            ||s.pendingTransportTargets.contains(p)
            ||std::find(currentHost->transportPassengerIds.begin(),currentHost->transportPassengerIds.end(),p)!=currentHost->transportPassengerIds.end())
            return fail(label+" moving host did not cancel visible final entry while preserving the stopped body");
        bool hostSettled=false;for(int n=0;n<1800;++n) {currentHost=findObjectById(s,host);
            if(currentHost&&currentHost->x==68&&currentHost->y==64&&!currentHost->renderMoveActive
                &&!s.behaviors.contains(host)){hostSettled=true;break;}
            if(!tick(s,host,ids))return false;}
        if(!hostSettled)return fail(label+" invalidating host Move did not naturally finish");
        return enter(s,host,ids)&&wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("LCRF-30-factory-lease-fractional-z-entry",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        std::optional<TerrainCell> ramp;
        for(const auto& set:s.theater.getTileSets())for(int tile=set.startTileIndex;tile<set.startTileIndex+set.tilesInSet&&!ramp;++tile)
            for(int sub=0;sub<16&&!ramp;++sub) {TerrainCell c;c.tileIndex=tile;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(s,c);
                if(info.passable&&info.ramp&&!info.water&&!info.bridge&&!info.shore
                    &&resolveRuntimeTerrainSubTileRampType(c,runtimeTerrainMetadataContext(s),*tileCache)>0)ramp=c;}
        if(!ramp)return fail(label+" original ramp TMP required for natural fractional Z");
        for(auto& c:s.world.accessTerrainCells())if(c.x>=65&&c.x<=70&&c.y>=60&&c.y<=68) {
            c.level=1;if(c.x==65&&c.y==64){c.tileIndex=ramp->tileIndex;c.subTileIndex=ramp->subTileIndex;}}
        initializeRuntimeNavigation(s);
        const int host=add(s,"LCRF",{64,64}),p=add(s,"MTNK",{66,64});const std::vector<int> ids{p};
        if(host<0||p<0||!move(s,host,{65,64}))return fail(label+" original ramp actors");
        bool fraction=false;for(int n=0;n<600;++n) {
            if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
            if(h&&h->renderMoveActive&&body(*h).z>.15&&body(*h).z<.65){fraction=true;break;}}
        if(!fraction||!stop(s,host))return fail(label+" actual ramp Move did not produce a fractional stopped height");
        const auto stopped=body(*findObjectById(s,host));if(stopped.active||stopped.z==std::round(stopped.z))return fail(label+" height source was not a real Stop");
        const auto building=[&](const char* type,RuntimeCellPoint cell) {
            return addRuntimeObjectInstanceForOwner(s,"Commander",RuntimeBuildKind::Building,type,cell,std::nullopt,false);};
        const auto* factory=building("GAWEAP",{91,78});const int factoryId=factory?factory->instanceId:-1;
        const auto* yard=building("GACNST",{98,84});const int yardId=yard?yard->instanceId:-1;
        const auto* power=building("GAPOWR",{93,90});const int powerId=power?power->instanceId:-1;
        if(factoryId<0||yardId<0||powerId<0)return fail(label+" original production hosts unavailable");
        const auto factoryCountries=s.skirmishCountryByOwner;
        const auto factoryHouseAuthorities=s.humanHouseAuthorities;
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);s.economy.owner="Commander";
        s.skirmishCountryByOwner=factoryCountries;
        s.humanHouseAuthorities=factoryHouseAuthorities;
        s.economy.credits=20000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;rebuildRuntimeEconomyPower(s);
        const auto* rebuiltHost=findObjectById(s,host);
        if(!rebuiltHost||!equal(stopped,body(*rebuiltHost)))return fail(label+" factory navigation setup replaced the real stopped hull");
        bool factoryReady=false;for(int n=0;n<30*30;++n) {
            factoryReady=true;for(int id:{factoryId,yardId,powerId}) {
                const auto* structure=findObjectById(s,id);
                factoryReady=factoryReady&&structure&&structure->buildAnimTicksRemaining<=0
                    &&toLowerAsciiLocal(structure->mission)!="construction";
            }
            if(factoryReady)break;if(!tick(s,host,ids))return false;}
        rebuiltHost=findObjectById(s,host);
        if(!factoryReady||!rebuiltHost||!equal(stopped,body(*rebuiltHost)))
            return fail(label+" actual original factory buildup did not complete while the hull remained stopped");
        rebuildRuntimeEconomyPower(s);
        const auto item=std::find_if(s.economy.catalog.begin(),s.economy.catalog.end(),[](const auto& c){return c.kind==RuntimeBuildKind::Unit&&c.typeId=="MTNK";});
        if(item==s.economy.catalog.end())return fail(label+" original paid MTNK catalogue absent: rules="+s.report.rulesPath
            +" owner="+s.economy.owner+" country="+runtimeCountryForOwner(s,s.economy.owner));
        const auto availability=runtimeBuildAvailability(s,*item);
        if(!availability.canQueue) {
            const auto& d=availability.decision;
            std::cout<<"Transport "<<label<<" productionAvailability reason="<<availability.reason
                <<" rules="<<s.report.rulesPath<<" owner="<<s.economy.owner
                <<" country="<<runtimeCountryForOwner(s,s.economy.owner)<<" credits="<<s.economy.credits
                <<" tech="<<s.economy.techLevel<<" static="<<d.staticAllowed<<" ownerAllowed="<<d.ownerAllowed
                <<" techAllowed="<<d.techAllowed<<" factory="<<d.factoryAvailable<<" prerequisite="<<d.prerequisitesMet
                <<" missingFactory="<<d.missingFactory<<" missingPrerequisite="<<d.missingPrerequisite
                <<" itemPrerequisite="<<item->prerequisite<<" rawPrerequisite="<<authored.getValue("MTNK","Prerequisite","")
                <<" requiredFactory="<<item->requiredFactory<<'\n';
            return fail(label+" original paid MTNK catalogue unavailable: "+availability.reason);
        }
        const RuntimeBuildCatalogItem paid=*item;const int beforeCredits=s.economy.credits;
        if(!dispatchRuntimeQueueProductionFromUi(s,paid))return fail(label+" actual paid production UI rejected");
        bool ordered=false,captured=false;int born=-1;
        for(int n=0;n<paid.buildTicks+30*55;++n) {
            const auto* manufacturing=runtimeActiveProductionForItem(s,paid);
            const auto productionTiming=runtimeProductionTimingForItem(s,paid,
                static_cast<int>(findRuntimeFactoryAnchorCandidatesForOwner(s,paid,s.economy.owner,false).size()),
                double(s.economy.powerProduced),double(s.economy.powerUsed));
            const auto productionRate=runtimeSidebarProductionRate(paid.kind,productionTiming.productionRate);
            if(!ordered&&manufacturing&&productionTiming.valid&&productionRate.numerator>0&&productionRate.denominator>0
                &&std::uint64_t(manufacturing->remainingTicks)*productionRate.denominator<=8ULL*productionRate.numerator) {
                if(!enter(s,host,ids))return false;ordered=true;
            }
            if(!tick(s,host,ids))return false;
            for(const auto& lease:s.factoryDeliveries.leases())if(lease.hostId==factoryId&&lease.producedObjectId>=0) {
                const auto* product=findObjectById(s,lease.producedObjectId);
                const auto* passenger=findObjectById(s,p);const auto* actualHost=findObjectById(s,host);
                const auto intent=s.pendingTransportTargets.find(p);const auto motion=s.behaviors.find(p);
                if(product&&product->type=="MTNK"&&product->health==rules.findUnitType("UnitType","MTNK")->strength
                    &&actualHost&&!actualHost->renderMoveActive&&equal(stopped,body(*actualHost))
                    &&passenger&&passenger->renderMoveActive&&intent!=s.pendingTransportTargets.end()&&intent->second==host
                    &&motion!=s.behaviors.end()&&motion->second.kind==RuntimeBehaviorKind::Move
                    &&motion->second.transportTransferKind==RuntimeTransportTransferKind::Boarding
                    &&motion->second.transportTransferHostId==host
                    &&motion->second.pendingAllowedOccupiedObjectId==host&&motion->second.renderMoveTicksRemaining>0
                    &&motion->second.renderTo==runtimeCellPointForObject(*actualHost)) {born=product->instanceId;captured=true;break;}}
            if(captured)break;
        }
        if(!captured||!ordered||born<0) {
            const auto& lane=s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
            std::cout<<"Transport "<<label<<" overlapDiagnostic tick="<<s.tick<<" ordered="<<ordered
                <<" captured="<<captured<<" capturedProduct="<<born<<" credits="<<s.economy.credits
                <<" beforeCredits="<<beforeCredits<<" itemCost="<<paid.cost<<" itemBuildTicks="<<paid.buildTicks
                <<" laneActive="<<bool(lane.active)<<" laneQueued="<<lane.queued.size()<<" laneReady="<<bool(lane.ready)
                <<" laneHold="<<lane.onHold<<" economyStatus="<<s.economy.status;
            if(lane.active)std::cout<<" activeItem="<<lane.active->itemKey<<" remaining="<<lane.active->remainingTicks
                <<" total="<<lane.active->totalTicks<<" creditsSpent="<<lane.active->creditsSpent;
            if(lane.ready)std::cout<<" readyItem="<<lane.ready->itemKey;
            std::cout<<" leases="<<s.factoryDeliveries.leases().size()<<'\n';
            for(const auto& lease:s.factoryDeliveries.leases())if(lease.hostId==factoryId)
                std::cout<<"Transport "<<label<<" factoryLease host="<<lease.hostId<<" product="<<lease.producedObjectId
                    <<" order="<<lease.orderSerial<<" acquired="<<lease.acquiredTick<<'\n';
            for(const auto& product:s.world.getObjects())if(product.kind==WorldObjectKind::Unit&&product.type=="MTNK"&&product.instanceId!=p)
                std::cout<<"Transport "<<label<<" actualMTNK id="<<product.instanceId<<" live="<<product.alive
                    <<" hp="<<product.health<<" cell="<<product.x<<','<<product.y<<" mission="<<product.mission<<'\n';
            const auto* passenger=findRuntimeObjectByIdIncludingInactive(s,p);const auto motion=s.behaviors.find(p);
            const auto intent=s.pendingTransportTargets.find(p);
            if(passenger){const auto physical=body(*passenger);std::cout<<"Transport "<<label<<" actualPassenger id="<<p
                <<" body="<<physical.x<<','<<physical.y<<','<<physical.z<<" live="<<passenger->alive<<" active="<<passenger->renderMoveActive
                <<" pendingHost="<<(intent==s.pendingTransportTargets.end()?-1:intent->second)<<" hasBehavior="<<(motion!=s.behaviors.end());
                if(motion!=s.behaviors.end())std::cout<<" transfer="<<int(motion->second.transportTransferKind)
                    <<" transferHost="<<motion->second.transportTransferHostId<<" remaining="<<motion->second.renderMoveTicksRemaining;
                std::cout<<'\n';}
            return fail(label+" no actual overlap of paid factory lease and fractional-Z Unit boarding");
        }
        if(s.economy.credits!=beforeCredits-paid.cost)return fail(label+" original paid manufacture accounting changed");
        if(!record(s,host,p,"real-factory-lease-and-boarding",nullptr))return false;
        std::cout<<"Transport "<<label<<" realFactory="<<factoryId<<" produced="<<born<<" tick="<<s.tick<<" actualStoppedZ="<<stopped.z<<'\n';
        auto replicas=checkpointBoth(s,host,ids,"real-lease-and-unit-boarding");
        if(!replicas)return fail(label+" factory early movement proof checkpoint rejected: "+error);
        for(const auto* copy:{replicas->ordinary.get(),replicas->canonical.get()})
            if(copy->factoryDeliveries.stateHash()!=s.factoryDeliveries.stateHash())return fail(label+" active factory lease changed on load");
        return pairTwelve(s,*replicas,host,ids)&&wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    if(!scene("FV-30-bridge-half-layer-entry",30,false,[&]() {
        auto box=make(30);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const auto& overlays=s.rules.getOverlayTypes();const auto bridge=std::find_if(overlays.begin(),overlays.end(),[](const auto& o){return o.id=="BRIDGEB1";});
        if(bridge==overlays.end()||int(bridge-overlays.begin())!=237)return fail(label+" original high wooden bridge overlay ordinal unavailable");
        for(auto& c:s.world.accessTerrainCells())if(c.x>=55&&c.x<=90&&c.y>=50&&c.y<=80) {
            c.tileIndex=clear->tileIndex;c.subTileIndex=clear->subTileIndex;c.level=4;}
        const auto cell=[&](RuntimeCellPoint point)->TerrainCell* {
            for(auto& c:s.world.accessTerrainCells())if(c.x==point.x&&c.y==point.y)return &c;return nullptr;};
        const auto originalBytes=[&](const std::string& filename,std::vector<std::uint8_t>& bytes,std::string& source) {
            std::string ext=s.theater.getTileExtension();if(!ext.empty()&&ext.front()=='.')ext.erase(ext.begin());
            const std::string theater=ext=="des"?"desert":ext=="sno"?"snow":ext=="urb"?"urban":ext=="ubn"?"urbann":ext=="lun"?"lunar":"temperat";
            const std::string iso=ext=="tem"?"isotemp.mix":ext=="sno"?"isosnow.mix":"iso"+ext+".mix";
            const std::string name=toLowerAsciiLocal(filename);
            for(const std::string directory:{"ra2md","ra2"})for(const std::string archiveName:{"iso"+ext+"md.mix",iso,theater+"md.mix",theater+".mix",ext+".mix"}) {
                const auto path=std::filesystem::path(runtimeAssetRootPath(s.config))/directory/archiveName;MixArchive archive;
                if(!std::filesystem::is_regular_file(path)||!archive.loadFromPath(path))continue;
                if(const auto* entry=archive.findEntryByName(name)) {
                    if(!archive.readEntryData(*entry,bytes,error))return false;source=pathString(path)+"!"+name;return true;
                }
            }
            error="original bridge head TMP unavailable: "+name;return false;
        };
        const auto stampHead=[&](RuntimeCellPoint boundary,bool start) {
            const int setIndex=s.theater.getGeneralTileSetIndex("WoodBridgeSet");
            const int head=s.theater.getGeneralTileSetIndex(start?"BridgeTopLeft1":"BridgeBottomRight1");
            const auto set=std::find_if(s.theater.getTileSets().begin(),s.theater.getTileSets().end(),[&](const auto& t){return t.index==setIndex;});
            if(set==s.theater.getTileSets().end()||head<=0||head>set->tilesInSet)return false;
            const int tile=set->startTileIndex+head-1;std::vector<std::uint8_t> bytes;std::string originalSource;
            if(!originalBytes(s.theater.resolveTileIndex(tile).expectedFileName,bytes,originalSource)
                ||bytes.size()<20||originalSource.empty())return false;
            const auto u32=[&](int i){return unsigned(bytes[i])|(unsigned(bytes[i+1])<<8)|(unsigned(bytes[i+2])<<16)|(unsigned(bytes[i+3])<<24);};
            const int width=int(u32(0)),height=int(u32(4));if(width<=0||height<=0||width>16||height>16)return false;
            const RuntimeCellPoint origin{boundary.x-(start?width-1:0),boundary.y-height/2};
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {auto* c=cell({origin.x+x,origin.y+y});if(!c)return false;
                c->tileIndex=tile;c->subTileIndex=y*width+x;c->level=4;}
            return true;
        };
        if(!stampHead({64,64},true)||!stampHead({74,64},false))return fail(label+" original complete bridgehead TMP stamp unavailable: "+error);
        for(int x=65;x<=73;++x)for(int lane=0;lane<3;++lane) {auto* c=cell({x,64+lane});if(!c)return false;
            c->tileIndex=water->tileIndex;c->subTileIndex=water->subTileIndex;c->level=0;
            if(lane==0) {c->hasOverlay=true;c->overlayIndex=int(bridge-overlays.begin());c->overlayType=bridge->id;
                c->overlayImage=bridge->image;c->overlayFrame=0;c->overlayIsOre=c->overlayIsGem=false;}}
        const auto* startHut=addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,"CABHUT",{61,63},std::nullopt,false);
        const int hutId=startHut?startHut->instanceId:-1;
        if(hutId<0||!addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,"CABHUT",{77,63},std::nullopt,false))
            return fail(label+" original bridge hut registration actors");
        initializeRuntimeNavigation(s);
        const auto span=s.bridgeRepairSpansByHut.find(hutId);
        if(span==s.bridgeRepairSpansByHut.end()||!span->second.high||!span->second.alongX)return fail(label+" genuine high bridge span did not register");
        std::set<int> required,actual;for(int x=65;x<=73;++x)required.insert(runtimeCellKey(x,64));
        for(const auto& c:span->second.cells)if(c.requiresOverlay)actual.insert(runtimeCellKey(c.point.x,c.point.y));
        if(actual!=required)return fail(label+" natural registered bridge body differs from the original nine-cell strip");
        const int host=add(s,"FV",{60,68}),p=add(s,"E1",{58,68});const std::vector<int> ids{p};
        if(host<0||p<0||!move(s,host,{66,64}))return fail(label+" actual bank-to-bridge movement unavailable");
        bool half=false;for(int n=0;n<30*60;++n) {
            if(!tick(s,host,ids))return false;const auto* h=findObjectById(s,host);
            if(h&&h->renderMoveActive&&h->hasRenderPosition&&h->onBridge&&!h->renderOnBridge
                &&xy(body(*h),Pose{60,68})>.03){half=true;break;}}
        if(!half||!stop(s,host))return fail(label+" actual Move did not produce the pre-half-layer bridge Stop");
        const auto* h=findObjectById(s,host);if(!h||h->airborne||h->renderMoveActive||!h->onBridge||h->renderOnBridge||body(*h).z!=4)
            return fail(label+" bridge sample did not preserve original actual/logical layer split");
        if(!enter(s,host,ids)||!finalBoardingSample(s,host,p))return false;
        if(!findObjectById(s,p))return diagnosticBefore;
        auto replicas=checkpointBoth(s,host,ids,"actual-half-layer-final-entry");
        if(!replicas)return fail(label+" valid strict half-layer boarding checkpoint rejected: "+error);
        for(const auto* copy:{replicas->ordinary.get(),replicas->canonical.get()}) {
            const auto* originalHost=findObjectById(s,host);const auto* restoredHost=findObjectById(*copy,host);
            if(!originalHost||!restoredHost||runtimeCellPointForObject(*restoredHost)!=runtimeCellPointForObject(*originalHost)
                ||restoredHost->level!=originalHost->level||!restoredHost->onBridge||restoredHost->renderOnBridge
                ||!restoredHost->hasRenderPosition||restoredHost->airborne||restoredHost->renderMoveActive||body(*restoredHost).z!=4)
                return fail(label+" saved host did not preserve the actual ground/logical bridge half-layer split");
        }
        return pairTwelve(s,*replicas,host,ids)&&wait(s,host,ids,true)&&unload(s,host,ids)&&wait(s,host,ids,false);
    }))return false;
    motionRows.close();cases.close();catalogue.close();
    if(!motionRows.good()||!cases.good()||!catalogue.good())return fail("evidence close failed");
    if(filter.empty()?(coreFinished!=33||finished!=46):(finished!=1))return fail("complete/unambiguous 33-core + 13-boundary catalogue required");
    std::cout<<"Transport continuity: core="<<coreFinished<<" scenes="<<finished<<" violations="<<violations
        <<" ordinaryDisk="<<saves<<" canonicalImageDisk="<<canonicalFiles<<" savedVsLiveTicks="<<paired
        <<" actualFrames="<<captures<<" phase="<<phase<<'\n';
    if(violations)return fail("actual motion violations="+std::to_string(violations)+"; before diagnostics do not constitute acceptance");
    error.clear();return true;
}
