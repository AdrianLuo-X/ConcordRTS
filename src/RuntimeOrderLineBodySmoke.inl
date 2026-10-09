// Body-origin oracle uses independently scanned real compositor pixels.
// Inventory poses are explicitly synthetic; the separate route cases below
// receive real screen/UI Move orders and never replace their path or targets.
bool runRuntimeOrderLineBodySmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="order line body: "+why;return false;};
    const char* output=std::getenv("CONCORD_ORDER_LINE_BODY_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-19-complete/10-order-line-body/checks";
    std::filesystem::create_directories(root);
    std::ofstream rows(root/"origins.csv"),inventory(root/"inventory.csv"),routes(root/"routes.csv");
    if(!rows||!inventory||!routes)return fail("evidence files");
    rows<<"case,type,tick,zoom,pan,body_left,body_top,body_right,body_bottom,expected_x,expected_y,old_x,old_y,gdi_mismatch,batch_mismatch,raster_mismatch,old_origin_witness,rendered_body_pixels,pass\n";
    inventory<<"type,kind,included,reason\n";
    routes<<"type,selected,accepted,requested_x,requested_y,first_path,interpolated,final_segment,settled,ticks,pass\n";
    auto stock=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*stock,live,false);
    const auto original=std::filesystem::path(stock->config.localDataRootPath)/"resources/mix_exports/ra2md/localmd";
    IniFile rulesIni,artIni;
    if(!rulesIni.loadFromFile((original/"rulesmd.ini").string())||!artIni.loadFromFile((original/"artmd.ini").string())
        ||!stock->rules.loadFromIni(rulesIni)||!stock->art.loadFromIni(artIni))return fail("original rules/art");
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;
    const auto metadata=runtimeTerrainMetadataContext(*stock);int land=-1,water=-1;
    for(int tile=0;tile<static_cast<int>(stock->theater.getTotalTileSlots());++tile){TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
        if(resolveRuntimeTerrainSubTileRampType(cell,metadata,stock->renderCache))continue;
        const auto movement=resolveRuntimeTerrainSubTileLandType(cell,metadata,stock->renderCache);
        const auto set=stock->theater.resolveTileIndex(tile).tileSetIndex;
        if(land<0&&set==stock->theater.getGeneralTileSetIndex("ClearTile")&&movement==RuntimeTerrainLandType::Clear)land=tile;
        if(water<0&&set==stock->theater.getGeneralTileSetIndex("WaterSet")&&movement==RuntimeTerrainLandType::Water)water=tile;
        if(land>=0&&water>=0)break;
    }
    if(land<0||water<0)return fail("original flat Clear/Water TMP");
    constexpr int width=960,height=640;const RECT client{0,0,width,height},viewport=runtimeMapViewport(client);
    struct Surfaces {
        std::array<RuntimeBackBuffer,5> buffers;HDC screen=GetDC(nullptr);
        ~Surfaces(){for(auto& b:buffers)releaseRuntimeBackBuffer(b);if(screen)ReleaseDC(nullptr,screen);}
    } surfaces;
    if(!surfaces.screen)return fail("screen DC");
    for(auto& b:surfaces.buffers)if(!ensureRuntimeBackBuffer(b,surfaces.screen,width,height))return fail("DIB surfaces");
    const auto make=[&](bool naval){auto p=std::make_unique<RuntimeWindowState>();auto& s=*p;seedRuntimeLoadScratch(s,*stock,false);
        s.config.fixedTickHz=30;s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=false;s.config.playerSlots.clear();s.config.gameMode="Battle";
        s.config.gameModeRulesOverride.clear();s.config.playerCountry="Americans";s.campaignMode=false;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.economy.credits=10000;
        s.humanHouseAuthorities.clear();RuntimeHumanHouseAuthority a;a.owner="Americans";a.canonicalOwner="americans";a.localFacade=true;s.humanHouseAuthorities.push_back(a);
        s.triggers={};s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimeTargetLinesEnabled=true;s.skirmishMatchFinished=s.skirmishVictory=false;
        for(int y=0;y<48;++y)for(int x=0;x<48;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;
            c.tileIndex=naval?water:land;c.subTileIndex=0;s.world.accessTerrainCells().push_back(c);}
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);
        s.camera.initialized=true;return p;
    };
    const auto contextFor=[&](RuntimeWindowState& s){auto c=runtimeTerrainMetadataContext(s);
        c.rules=&s.rules;c.art=&s.art;c.theater=&s.theater;c.assetRootPath=runtimeAssetRootPath(s.config);
        c.palettePath=runtimePalettePath(s.config,s.report.map);c.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        c.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);c.shadowsEnabled=false;c.vision=nullptr;
        c.animationTick=s.tick;c.fixedTickHz=30;c.ownerRemapColors=&s.ownerRemapColors;c.neutralRemapOwners=&s.neutralRemapOwners;
        refreshRuntimeSpecialMechanismRenderSnapshots(s);c.specialMechanismSnapshots=&s.specialMechanismRenderSnapshots;return c;
    };
    const auto pointFor=[&](const RuntimeWindowState& s,double x,double y,double z){return POINT{
        viewport.left+static_cast<LONG>(std::llround((x-y)*s.camera.tileWidth/2.0))-s.camera.worldOffsetX,
        viewport.top+static_cast<LONG>(std::llround((x+y-z)*s.camera.tileHeight/2.0))-s.camera.worldOffsetY};};
    const auto difference=[](const RuntimeBackBuffer& a,const RuntimeBackBuffer& b){
        const auto* x=static_cast<const std::uint32_t*>(a.pixels);const auto* y=static_cast<const std::uint32_t*>(b.pixels);std::size_t n=0;
        for(int i=0;i<width*height;++i)n+=((x[i]^y[i])&0xffffffU)!=0;return n;};
    int checks=0,failed=0,included=0,excluded=0,realRoutes=0,jumpjetWitnesses=0;std::string firstFailure;
    const auto note=[&](const std::string& why){++failed;if(firstFailure.empty())firstFailure=why;};
    const auto check=[&](RuntimeWindowState& s,const std::string& label,int zoom,int pan,bool picture){
        RuntimeSnapshotActiveStateScope scope(s);s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
        s.camera.worldOffsetX=-((viewport.right-viewport.left)/2)+pan;
        s.camera.worldOffsetY=40*s.camera.tileHeight/2-(viewport.bottom-viewport.top)/2-pan;
        invalidateRuntimeObjectProjectionCache(s);auto context=contextFor(s);
        std::map<int,POINT> centers,oldCenters;std::map<int,RuntimeMobileSpriteBoundsProbe> probes;
        for(int id:s.selectedObjectIds){const auto* o=findObjectById(s,id);if(!o)return fail(label+" selected object absent");
            const auto placement=runtimeObjectWorldRenderPlacement(*o,s.world,viewport,context,s.camera,s.renderCache);
            auto& probe=probes[id];if(!verifyRuntimeMobileObjectSpriteBounds(*o,placement.center,context,s.camera,s.renderCache,probe))
                return fail(label+" real body oracle: "+probe.diagnostic);
            const RECT body=probe.renderedBounds;centers[id]={body.left+(body.right-body.left)/2,body.top+(body.bottom-body.top)/2};
            const auto subcell=worldObjectSubCellOffset(*o);oldCenters[id]=pointFor(s,runtimeObjectRenderX(*o)+subcell.x,runtimeObjectRenderY(*o)+subcell.y,runtimeObjectRenderLevel(*o));
        }
        std::vector<std::uint32_t> background(width*height,0x001c2834U);
        if(picture&&!drawRuntimeStateOffscreenAtCell(s,{20,20},width,height,error,"",false,false,-1,&background,true,&s.camera))return fail(label+" scene render");
        GdiFlush();for(auto& b:surfaces.buffers){std::copy(background.begin(),background.end(),static_cast<std::uint32_t*>(b.pixels));
            SetBkMode(b.memoryDc,TRANSPARENT);SetROP2(b.memoryDc,R2_COPYPEN);SelectClipRgn(b.memoryDc,nullptr);
            IntersectClipRect(b.memoryDc,viewport.left,viewport.top,viewport.right,viewport.bottom);MoveToEx(b.memoryDc,7,11,nullptr);}
        const auto reference=[&](RuntimeBackBuffer& b,const std::map<int,POINT>& anchors){
            HDC dc=b.memoryDc;HPEN path=CreatePen(PS_DOT,1,RGB(105,230,190)),target=CreatePen(PS_SOLID,2,RGB(126,245,118));
            HGDIOBJ pen=SelectObject(dc,path),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            for(int id:s.selectedObjectIds){const auto found=s.behaviors.find(id);if(found==s.behaviors.end()||found->second.kind!=RuntimeBehaviorKind::Move)continue;
                POINT previous=anchors.at(id);int count=0;
                for(const auto& cell:found->second.path){if(count++>=96)break;const auto p=pointFor(s,cell.x,cell.y,0);
                    MoveToEx(dc,previous.x,previous.y,nullptr);LineTo(dc,p.x,p.y);Rectangle(dc,p.x-2,p.y-2,p.x+3,p.y+3);previous=p;}
                const auto p=pointFor(s,found->second.moveTarget.x,found->second.moveTarget.y,0);SelectObject(dc,target);
                MoveToEx(dc,p.x,p.y-10,nullptr);LineTo(dc,p.x+10,p.y);LineTo(dc,p.x,p.y+10);LineTo(dc,p.x-10,p.y);LineTo(dc,p.x,p.y-10);SelectObject(dc,path);
            }
            SelectObject(dc,brush);SelectObject(dc,pen);DeleteObject(path);DeleteObject(target);GdiFlush();
        };
        reference(surfaces.buffers[0],centers);reference(surfaces.buffers[4],oldCenters);
        std::string hashError;const auto before=runtimeStandaloneSimulationStateHash(s,hashError);if(!before||!hashError.empty())return fail(label+" authority hash: "+hashError);
        std::array<int,3> drawn{};
        for(int mode=0;mode<3;++mode){RuntimeSelectedPathRasterReferenceScope rasterReference(mode!=2);
            auto& b=surfaces.buffers[mode+1];
            if(mode==2){std::swap(s.backBuffer,b);drawn[mode]=drawRuntimeSelectedPaths(s.backBuffer.memoryDc,viewport,s,true);std::swap(s.backBuffer,b);}
            else drawn[mode]=drawRuntimeSelectedPaths(b.memoryDc,viewport,s,mode==1);GdiFlush();
        }
        const auto after=runtimeStandaloneSimulationStateHash(s,hashError);if(before!=after||!hashError.empty())return fail(label+" drawing changed authority");
        const std::array<std::size_t,3> mismatches{{difference(surfaces.buffers[0],surfaces.buffers[1]),difference(surfaces.buffers[0],surfaces.buffers[2]),difference(surfaces.buffers[0],surfaces.buffers[3])}};
        const auto wrong=difference(surfaces.buffers[0],surfaces.buffers[4]);
        const bool ok=!mismatches[0]&&!mismatches[1]&&!mismatches[2]
            &&drawn[0]==static_cast<int>(s.selectedObjectIds.size())&&drawn[1]==drawn[0]&&drawn[2]==drawn[0];
        ++checks;if(!ok)note(label+" body-origin pixels");
        for(int id:s.selectedObjectIds){const auto* o=findObjectById(s,id);const auto& probe=probes.at(id);const RECT b=probe.renderedBounds;
            rows<<label<<','<<o->type<<','<<s.tick<<','<<zoom<<','<<pan<<','<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<','
                <<centers.at(id).x<<','<<centers.at(id).y<<','<<oldCenters.at(id).x<<','<<oldCenters.at(id).y<<','
                <<mismatches[0]<<','<<mismatches[1]<<','<<mismatches[2]<<','<<wrong<<','<<probe.renderedPixels<<','<<ok<<'\n';
            if(o->type=="JUMPJET"&&label.find("real-")==0&&o->airborne&&wrong>0)++jumpjetWitnesses;
        }
        if(picture){const auto prefix=label+"-z"+std::to_string(zoom)+"-p"+std::to_string(pan);
            for(const auto& item:std::array<std::pair<int,const char*>,3>{{{1,"actual"},{0,"expected"},{4,"old-foot"}}})
                if(!writeRuntimeSmokeBmp((root/(prefix+"-"+item.second+".bmp")).string(),static_cast<const std::uint32_t*>(surfaces.buffers[item.first].pixels),width,height,error))return fail("image evidence");}
        return true;
    };
    // One static display pose per ordinary mobile type, not a simulated route.
    auto fixed=make(false);auto& posed=*fixed;RuntimeSnapshotActiveStateScope poseScope(posed);
    for(int kind=0;kind<3;++kind){const auto& types=kind==0?stock->rules.getInfantryTypes():kind==1?stock->rules.getVehicleTypes():stock->rules.getAircraftTypes();
        for(const auto& type:types){std::string reason;
            if(type.missileSpawn)reason="generated missile; ordinary Move unavailable";else if(type.spawned)reason="generated child; ordinary Move unavailable";
            else if(type.speed<=0)reason="nonmoving registration";else if(type.id=="WEEDGUY"||type.id=="DeathDummy")reason="original registration has no body asset";
            inventory<<type.id<<','<<kind<<','<<reason.empty()<<','<<reason<<'\n';if(!reason.empty()){++excluded;continue;}++included;
            WorldObjectInstance o;o.instanceId=1001;o.type=type.id;o.kind=kind==0?WorldObjectKind::Infantry:WorldObjectKind::Unit;
            o.owner=posed.economy.owner;o.alive=true;o.health=std::max(1,type.strength);o.currentAmmo=type.ammo;o.mission="Guard";
            o.authoredAircraft=kind==2;o.airborne=kind==2||runtimeUnitMovementModeProfile(type)==PathMovementMode::Air;
            o.x=o.y=20;o.cell=runtimeCellKey(20,20);o.level=o.airborne?4:0;o.flightLevel=o.level;o.facing=96;o.turretFacing=160;
            o.subCell=kind==0&&!o.airborne?0:-1;o.submerged=type.underwater;posed.world.accessObjects()={o};posed.selectedObjectIds={o.instanceId};
            posed.behaviors.clear();auto& move=posed.behaviors[o.instanceId];move.kind=RuntimeBehaviorKind::Move;move.moveTarget={25,23};move.path={{21,20},{22,21},{25,23}};
            if(!check(posed,"inventory-"+type.id,60,0,false))return false;
        }
    }
    for(const char* type:{"JUMPJET","E1","MTNK","DRON","SHAD","SQD"}){
        const auto* rule=stock->rules.findUnitType(std::string(type)=="E1"||std::string(type)=="JUMPJET"?"InfantryType":"UnitType",type);
        if(!rule)return fail(std::string(type)+" original route type");auto storage=make(runtimeUnitMovementModeProfile(*rule)==PathMovementMode::Naval);
        auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);const RuntimeCellPoint start{20,20},goal{26,23};std::vector<int> ids;
        for(int n=0;n<(std::string(type)=="E1"?3:1);++n){auto* o=addRuntimeObjectInstance(s,runtimeBuildKindForUnitTypeId(s.rules,type),type,start);
            if(!o)return fail(std::string(type)+" real spawn");ids.push_back(o->instanceId);o->harvestAutoSearchSuspended=true;}
        if(!addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Building,"NAPOWR",{42,42}))return fail("distant surviving opponent");
        s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);reconcileRuntimeSubcells(s,false);
        applySelection(s,ids,false,false);s.camera.tileWidth=60;s.camera.tileHeight=30;s.camera.worldOffsetX=-300;s.camera.worldOffsetY=350;
        const POINT click=pointFor(s,goal.x,goal.y,0);const POINT hit=approximateCellFromScreenPoint(s,viewport,click);
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{hit.x,hit.y},nullptr);
        const int accepted=s.lastCommand.acceptedCount;std::map<int,RuntimeCellPoint> goals;
        for(int id:ids){const auto found=s.behaviors.find(id);if(found!=s.behaviors.end())goals[id]=found->second.moveTarget;}
        if(!dispatched||accepted!=static_cast<int>(ids.size())||hit.x!=goal.x||hit.y!=goal.y||goals.size()!=ids.size())return fail(std::string(type)+" actual screen/UI Move");
        bool first=false,interpolated=false,last=false,settled=false;int ticks=0;
        for(;ticks<30*180;++ticks){++s.tick;advanceRuntimeSimulation(s,false);
            bool hasPath=false,middle=false,close=false,done=true;
            for(int id:ids){const auto* o=findObjectById(s,id);if(!o||!o->alive)return fail(std::string(type)+" mover lost");const auto b=s.behaviors.find(id);
                hasPath=hasPath||(b!=s.behaviors.end()&&!b->second.path.empty());
                middle=middle||(o->renderMoveActive&&(std::abs(runtimeObjectRenderX(*o)-o->x)>1e-5||std::abs(runtimeObjectRenderY(*o)-o->y)>1e-5));
                close=close||(std::hypot(runtimeObjectRenderX(*o)-goals.at(id).x,runtimeObjectRenderY(*o)-goals.at(id).y)<2.0);
                done=done&&!o->renderMoveActive&&(b==s.behaviors.end()||b->second.state==RuntimeBehaviorState::Complete)
                    &&runtimeCellPointForObject(*o)==goals.at(id)&&std::hypot(runtimeObjectRenderX(*o)-goals.at(id).x,runtimeObjectRenderY(*o)-goals.at(id).y)<1e-6;
            }
            const int phase=hasPath&&!first?0:hasPath&&middle&&!interpolated?1:hasPath&&close&&!last?2:-1;
            if(phase>=0){if(phase==0)first=true;if(phase==1)interpolated=true;if(phase==2)last=true;
                for(int camera=0;camera<3;++camera)if(!check(s,std::string("real-")+type+"-phase"+std::to_string(phase),camera==0?60:camera==1?48:72,camera==1?37:0,
                    (std::string(type)=="JUMPJET"||std::string(type)=="E1")&&phase==1&&camera==0))return false;}
            if(done){settled=true;++ticks;break;}
        }
        const bool ok=first&&interpolated&&last&&settled;++realRoutes;if(!ok)note(std::string(type)+" real route observations");
        routes<<type<<','<<ids.size()<<','<<accepted<<','<<goal.x<<','<<goal.y<<','<<first<<','<<interpolated<<','<<last<<','<<settled<<','<<ticks<<','<<ok<<'\n';
        std::cout<<"Order line route "<<type<<": samples="<<first<<interpolated<<last<<" settled="<<settled<<" ticks="<<ticks<<'\n';
    }
    if(!jumpjetWitnesses)note("no real airborne Jumpjet old-origin pixel witness");
    std::cout<<"Order line body: included="<<included<<" excluded="<<excluded<<" checks="<<checks<<" routes="<<realRoutes
        <<" realJumpjetWitnesses="<<jumpjetWitnesses<<" failures="<<failed<<'\n';
    if(failed)return fail(std::to_string(failed)+" failed checks; first="+firstFailure+"; evidence retained");error.clear();return true;
}
