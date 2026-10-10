    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPPORT_NO_COOLDOWN_ONLY")) {
        const HINSTANCE module=GetModuleHandleW(nullptr);
        const wchar_t* className=L"ConcordSupportRepeatSmoke";
        WNDCLASSW windowClass{};windowClass.lpfnWndProc=runtimeWindowProc;
        windowClass.hInstance=module;windowClass.lpszClassName=className;
        if(!RegisterClassW(&windowClass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            return fail("repeat window class");
        struct ClassCleanup {HINSTANCE module;const wchar_t* name;
            ~ClassCleanup(){UnregisterClassW(name,module);}} classCleanup{module,className};
        int cases=0,casts=0,saves=0,images=0,continuationChecks=0;
        const std::string owner=runtimeSkirmishOwnerForSlot(0),enemy=runtimeSkirmishOwnerForSlot(1);
        const auto repeatFixture=[&](int hz) {
            auto storage=fixture("Americans",hz);auto& state=*storage;
            state.config.playerSlots.clear();
            RuntimeConfig::PlayerSlot slot;slot.enabled=slot.human=slot.local=true;
            slot.country="Americans";slot.color="Gold";slot.startPosition=1;
            state.config.playerSlots.push_back(slot);
            slot.human=slot.local=false;slot.country="Russians";slot.color="Red";slot.startPosition=2;
            state.config.playerSlots.push_back(slot);
            initializeRuntimeSkirmishPlayerIdentities(state,"Americans");
            initializeRuntimeLocalProductionLanes(state.economy);
            initializeRuntimeHumanHouseAuthorities(state);
            for(auto& object:state.world.accessObjects())object.owner=owner;
            state.superWeapons.reset(state.rules);
            return storage;
        };
        const char* filter=std::getenv("CONCORD_SUPPORT_REPEAT_FILTER");
        for(int hz:{15,30,60}) for(const auto& id:expected) for(bool routed:{false,true}) {
            if(filter && id!=filter)continue;
            const std::string scene=id+"-"+std::to_string(hz)+(routed?"-routed":"-direct");
            auto storage=repeatFixture(hz);auto& s=*storage;
            const auto bad=[&](const std::string& why){return fail("repeat "+scene+": "+why+" ui="+s.interactionStatus);};
            s.config.noCooldown=true;s.config.multiplayerActive=false;s.config.gameMode="Battle";
            s.config.shortGame=false;s.visionEnabled=false;s.runtimeSoundsEnabled=false;
            s.activeBuildTab=RuntimeBuildKind::Defense;
            addUnit(s,"GACNST",WorldObjectKind::Structure,112,74,enemy,100000);
            const std::array<RuntimeCellPoint,3> cells{{{86,100},{98,100},{110,100}}};
            std::array<int,3> victims{-1,-1,-1};
            for(int use=0;use<3;++use) {
                const auto cell=cells[use];
                if(id=="ForceShieldSpecial") victims[use]=addUnit(s,"GAPILE",WorldObjectKind::Structure,cell.x,cell.y,owner);
                else if(id=="IronCurtainSpecial" || id=="ChronoSphereSpecial")
                    victims[use]=addUnit(s,"MTNK",WorldObjectKind::Unit,cell.x,cell.y,owner);
                else if(id=="GeneticConverterSpecial")
                    victims[use]=addUnit(s,"E1",WorldObjectKind::Infantry,cell.x,cell.y,enemy);
                else if(id=="PsychicDominatorSpecial")
                    victims[use]=addUnit(s,"MTNK",WorldObjectKind::Unit,cell.x,cell.y,enemy,100000);
                else if(id=="NukeSpecial" || id=="LightningStormSpecial")
                    victims[use]=addUnit(s,"GAPILE",WorldObjectKind::Structure,cell.x,cell.y,enemy,100000);
            }
            initializeRuntimeNavigation(s);
            const auto tick=[](RuntimeWindowState& state){
                RuntimeSnapshotActiveStateScope scope(state);
                ++state.tick;advanceRuntimeSimulation(state);
            };
            // Ordinary game saves rebuild interpolated object poses from
            // logical cells (parseRuntimeSavedObject). Compare their logical
            // world together with the independently restored support state.
            const auto logicalWorldHash=[](const RuntimeWindowState& state) {
                auto world=state.world;
                for(auto& object:world.accessObjects())clearRuntimeRenderPosition(object);
                return world.deterministicSimulationStateHash();
            };
            tick(s);
            const auto* initial=s.superWeapons.findSidebarSnapshot(owner,id);
            if(!initial || initial->state!=SuperWeaponRuntimeState::Ready || initial->rechargeTicks!=1)
                return bad("initial ready source");
            const auto grantCount=s.superWeapons.sidebarSnapshots().size();
            if(std::count_if(s.superWeapons.sidebarSnapshots().begin(),s.superWeapons.sidebarSnapshots().end(),
                [&](const auto& entry){return entry.owner==owner;})!=expected.size())return bad("owned sidebar inventory");
            int commands=0;
            if(routed) {
                initializeRuntimeOfflineGameplayRouting(s);
                s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string& reason) {
                    const auto* body=std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
                    if(!body)return false;++commands;
                    return dispatchRuntimeOfflineGameplayCommand(s,command,reason);
                });
            }
            RuntimeSnapshotActiveStateScope activeScope(s);
            const RECT client{0,0,1024,768};const RECT sidebar=runtimeSidebarRect(client);
            const RECT viewport=runtimeMapViewport(client);
            HWND window=CreateWindowExW(0,className,L"Support repeat check",WS_POPUP,0,0,1024,768,
                nullptr,nullptr,module,nullptr);
            if(!window)return bad("native input window");
            struct WindowCleanup {HWND window;~WindowCleanup(){
                SetWindowLongPtrW(window,GWLP_USERDATA,0);
                SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));DestroyWindow(window);
            }} windowCleanup{window};
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
            const auto left=[&](POINT p){
                SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
                SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));
            };
            const auto cast=[&](int use) {
                std::optional<POINT> icon;
                for(int page=0;page<runtimeBuildPageCount(s,sidebar) && !icon;++page) {
                    runtimeSetActiveBuildPage(s,page);
                    const auto offset=runtimeVisibleBuildCatalogIndices(s,sidebar).size();
                    const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(s,sidebar);
                    for(std::size_t n=0;n<visible.size();++n)if(visible[n]->superWeaponId==id) {
                        const auto rect=runtimeBuildButtonRect(sidebar,static_cast<int>(offset+n));
                        icon=POINT{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};break;
                    }
                }
                if(!icon)return false;
                left(*icon);
                const auto* selection=runtimeActiveLocalSuperWeaponTargeting(s);
                if(!selection || selection->superWeaponId!=id)return false;
                centerRuntimeCameraOnCell(s,client,cells[use]);ensureRuntimeCameraForViewport(s,viewport);
                left(runtimeScreenPointForCell(s,viewport,cells[use],0));
                if(id=="ChronoSphereSpecial")left(runtimeScreenPointForCell(s,viewport,{cells[use].x,106},0));
                const auto* ready=s.superWeapons.findSidebarSnapshot(owner,id);
                return !runtimeActiveLocalSuperWeaponTargeting(s) && ready
                    && ready->state==SuperWeaponRuntimeState::Ready && runtimeSuperWeaponHasReadyLabel(*ready)
                    && ready->remainingTicks==0 && commands==(routed?use+1:0);
            };
            std::uint64_t seen=0;
            std::map<std::string,int> events;
            std::map<std::string,std::set<int>> eventTargets;
            std::set<int> planes,photographedPlanes;
            const auto collect=[&]() {
                for(const auto& event:s.recentReplayEvents)if(event.sequence>seen) {
                    if(event.superWeaponId==id) {
                        ++events[event.superWeaponEvent];eventTargets[event.superWeaponEvent].insert(event.targetCell.x);
                        if(event.superWeaponEvent=="ParadropAircraftLaunched" || event.superWeaponEvent=="SpyPlaneAircraftLaunched")
                            planes.insert(event.objectId);
                        if(event.superWeaponEvent=="SpyPlaneRevealArea")photographedPlanes.insert(event.objectId);
                    }
                    seen=std::max(seen,event.sequence);
                }
            };
            const auto reload=[&](const std::string& stage,std::unique_ptr<RuntimeWindowState>& loaded) {
                std::ostringstream saved;
                if(!writeRuntimeStateToStream(s,saved,error))return false;
                if(const char* out=std::getenv("CONCORD_NO_COOLDOWN_DIR")) {
                    std::filesystem::create_directories(out);
                    std::ofstream file(std::filesystem::path(out)/(scene+"-"+stage+".ini"),std::ios::binary);
                    file<<saved.str();if(!file)return false;
                }
                IniFile ini;loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
                loaded->config.noCooldown=false;
                if(!ini.loadFromString(saved.str()) || !materializeRuntimeLoadStateFromIni(*loaded,ini,error))return false;
                if(!loaded->config.noCooldown || loaded->superWeapons.stateHash()!=s.superWeapons.stateHash()
                    || logicalWorldHash(*loaded)!=logicalWorldHash(s)) {
                    std::ostringstream restored;std::string detail;
                    writeRuntimeStateToStream(*loaded,restored,detail);
                    if(const char* out=std::getenv("CONCORD_NO_COOLDOWN_DIR")) {
                        std::ofstream file(std::filesystem::path(out)/(scene+"-"+stage+"-restored.ini"),std::ios::binary);
                        file<<restored.str();
                    }
                    error="restore hashes: noCooldown="+std::to_string(loaded->config.noCooldown)
                        +" support="+std::to_string(s.superWeapons.stateHash())+"/"+std::to_string(loaded->superWeapons.stateHash())
                        +" logicalWorld="+std::to_string(logicalWorldHash(s))+"/"
                        +std::to_string(logicalWorldHash(*loaded));
                    return false;
                }
                ++saves;return true;
            };
            // Two complete UI submissions share a tick; the third happens
            // after those payloads have actually launched (including blackout).
            if(!cast(0) || !cast(1))return bad("second queued cast before prior launch");
            collect();
            std::unique_ptr<RuntimeWindowState> loaded;
            if(!reload("queued",loaded))return bad("queued full save: "+error);
            tick(s);tick(*loaded);collect();
            if(s.superWeapons.stateHash()!=loaded->superWeapons.stateHash()
                || logicalWorldHash(s)!=logicalWorldHash(*loaded))
                return bad("queued save continuation");
            ++continuationChecks;
            if(!cast(2))return bad("third cast while earlier effects run");
            collect();tick(s);collect();
            if(events["SuperWeaponCommandAccepted"]!=3 || events["SuperWeaponLaunched"]!=3
                || eventTargets["SuperWeaponLaunched"]!=std::set<int>{86,98,110})return bad("lost/duplicated launch target");
            if(s.superWeapons.sidebarSnapshots().size()!=grantCount)return bad("duplicate sidebar icons");
            if(id=="ForceShieldSpecial" || id=="IronCurtainSpecial") {
                for(int victim:victims)if(!s.superWeapons.isObjectProtected(victim,s.tick))return bad("overlapping protection");
                if(id=="ForceShieldSpecial" && !s.superWeapons.isOwnerBlackout(owner,s.tick))return bad("blackout effect removed");
            }
            if(id=="ChronoSphereSpecial")for(int use=0;use<3;++use) {
                const auto* object=findObjectById(s,victims[use]);
                if(!object || object->x!=cells[use].x || object->y!=106)return bad("independent two-stage teleport");
            }
            if(id=="LightningStormSpecial" || id=="PsychicDominatorSpecial") {
                if(s.superWeapons.activeLifecycleSnapshots().size()!=3 || s.activeLightingTokens.size()!=3)
                    return bad("independent active lighting lifecycles");
            }
            for(const auto& object:s.world.getObjects())if(object.owner==owner && object.kind==WorldObjectKind::Structure) {
                const auto* building=s.rules.findBuildingType(object.type);
                if(building && building->superWeapon==id) {
                    const auto pose=runtimeSuperWeaponBuildingSnapshot(s,object);
                    if(pose && pose->resourceId!="SuperAnimFour")return bad("ready button suppressed firing art");
                }
            }
            if(!reload("active",loaded))return bad("overlapping active full save: "+error);
            const bool picture=hz==30 && !routed && (id=="NukeSpecial" || id=="LightningStormSpecial"
                || id=="AmericanParaDropSpecial" || id=="ForceShieldSpecial");
            bool photographed=false;
            const auto capture=[&]() {
                const auto render=[&](RuntimeWindowState& state,const std::string& suffix,std::vector<std::uint32_t>& pixels) {
                    state.activeBuildTab=RuntimeBuildKind::Defense;
                    for(int page=0;page<runtimeBuildPageCount(state,sidebar);++page) {
                        runtimeSetActiveBuildPage(state,page);
                        const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(state,sidebar);
                        if(std::any_of(visible.begin(),visible.end(),[&](const auto* entry){return entry->superWeaponId==id;}))break;
                    }
                    const auto stats=drawRuntimeStateOffscreenAtCell(state,{100,102},1024,768,error,"",false,false,-1,&pixels,true);
                    if(!stats)return false;
                    std::cout<<"Support repeat render: "<<scene<<suffix<<" visible="<<stats->visibleWorldObjects
                        <<" culled="<<stats->culledWorldObjects<<" parachutes="<<stats->parachuteSprites<<'\n';
                    RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);
                    const bool created=ensureRuntimeBackBuffer(buffer,screen,1024,768);
                    if(screen)ReleaseDC(nullptr,screen);
                    if(!created)return false;
                    std::copy(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(buffer.pixels));
                    SetBkMode(buffer.memoryDc,TRANSPARENT);
                    snapRuntimeSidebarPowerAnimation(state);snapRuntimeCreditAudio(state);
                    drawRuntimeSidebar(buffer.memoryDc,client,state,0);
                    drawRuntimeSuperWeaponTimerList(buffer.memoryDc,{0,0,sidebar.left,768},state);
                    GdiFlush();
                    const auto* data=static_cast<const std::uint32_t*>(buffer.pixels);
                    pixels.assign(data,data+1024*768);
                    const bool written=writeRuntimeSmokeBmp(root+"/repeat-"+id+suffix+".bmp",buffer.pixels,1024,768,error);
                    releaseRuntimeBackBuffer(buffer);return written;
                };
                std::vector<std::uint32_t> original,restored;
                if(!render(s,"",original) || !render(*loaded,"-loaded",restored) || original!=restored)
                    return false;
                images+=2;photographed=true;return true;
            };
            bool finished=false;
            for(int elapsed=0;elapsed<180*hz;++elapsed) {
                tick(s);tick(*loaded);collect();
                const bool visibleParatroopers=id=="AmericanParaDropSpecial"
                    && eventTargets["ParadropInfantryDropped"].size()==3
                    && std::any_of(s.world.getObjects().begin(),s.world.getObjects().end(),[&](const auto& object) {
                        return object.alive && object.kind==WorldObjectKind::Infantry && object.owner==owner
                            && object.airborne && object.renderLevel>0 && object.renderLevel<=8;
                    });
                if(picture && !root.empty() && !photographed && ((id=="ForceShieldSpecial" && elapsed>=3*hz)
                    || events["NukeImpact"]>0 || events["LightningBolt"]>0 || visibleParatroopers))
                    if(!capture())return bad("native overlapping render/save: "+error);
                if(elapsed%hz!=0)continue;
                if(s.superWeapons.stateHash()!=loaded->superWeapons.stateHash()
                    || logicalWorldHash(s)!=logicalWorldHash(*loaded))
                    return bad("active save continuation at "+std::to_string(s.tick));
                ++continuationChecks;
                std::ostringstream saved;s.superWeapons.save(saved);IniFile ini;ini.loadFromString(saved.str());
                if(ini.getValue("SuperWeapons","Count")==std::to_string(grantCount)){finished=true;break;}
            }
            if(!finished || s.superWeapons.stateOf(owner,id)!=SuperWeaponRuntimeState::Ready
                || !s.superWeapons.activeLifecycleSnapshots().empty() || !s.activeLightingTokens.empty())
                return bad("effects did not finish/retire independently");
            if(picture && !root.empty() && !photographed)return bad("no visible payload capture");
            if(id=="NukeSpecial" || id=="LightningStormSpecial") {
                for(int victim:victims){const auto* object=findObjectById(s,victim);
                    if(object && object->alive && object->health>=100000)return bad("missed payload damage");}
                if(id=="NukeSpecial" && events["NukeImpact"]!=3)return bad("nuclear payload count");
            }
            if(id=="PsychicDominatorSpecial")for(int victim:victims) {
                const auto* object=findObjectById(s,victim);
                if(!object || !object->alive || object->owner!=owner)return bad("lost dominator payload");
            }
            if(id=="GeneticConverterSpecial" && events["GeneticMutationApplied"]!=3)return bad("lost mutation payload");
            if(id=="PsychicRevealSpecial" && events["PsychicRevealArea"]!=3)return bad("lost reveal payload");
            if(id=="SpyPlaneSpecial" && (planes.size()!=3 || planes!=photographedPlanes))return bad("lost reconnaissance flight");
            if(id=="AmericanParaDropSpecial" || id=="ParaDropSpecial") {
                int landed=0;
                for(const auto& object:s.world.getObjects())if(object.alive && object.kind==WorldObjectKind::Infantry
                    && object.owner==owner) {
                    ++landed;
                    if(object.airborne || object.mission=="ChronoFrozen" || !RuntimeSubcellSystemState::isActiveSubCell(object.subCell))
                        return bad("unfinished or frozen paratrooper");
                }
                if(planes.size()!=3 || eventTargets["ParadropInfantryDropped"]!=std::set<int>{86,98,110}
                    || events["ParadropInfantryDropped"]<=0 || events["ParadropInfantryLanded"]!=landed
                    || events["ParadropInfantryDropped"]!=landed)return bad("lost airdrop or landing payload");
            }
            ++cases;casts+=3;
            std::cout<<"Support repeat: "<<scene<<" UI=3 queued/active save=2 independent-payload/cleanup=ok\n";
        }
        std::cout<<"Support repeat: passed cases="<<cases<<" casts="<<casts<<" saves="<<saves
            <<" continuationChecks="<<continuationChecks<<" images="<<images<<"\n";
        return true;
    }
