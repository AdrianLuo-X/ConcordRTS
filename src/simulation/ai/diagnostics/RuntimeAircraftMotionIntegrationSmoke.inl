bool runRuntimeAircraftMotionIntegrationSmoke(RuntimeWindowState& assets,std::string& error) {
    const auto fail=[&](const std::string& detail){error="Aircraft flight integration: "+detail;return false;};
    int scenes=0,saves=0,paired=0,redirects=0,stops=0,landings=0,snapshots=0,rejections=0,frames=0,attacks=0,legacyLoads=0,groups=0;
    const char* output=std::getenv("CONCORD_AIRCRAFT_MOTION_OUTPUT");
    std::optional<int> layoutShift;
    for(const auto* type:{"ORCA","BEAG"})for(int hz:{15,30,60})for(bool instant:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=assets.config;s.config.fixedTickHz=hz;s.config.noCooldown=instant;
        s.config.computerAiEnabled=false;s.config.shortGame=false;s.config.startingUnits=0;
        s.config.playerCountry=std::string(type)=="BEAG"?"Alliance":"Americans";
        s.config.playerSlots.clear();RuntimeConfig::PlayerSlot player;
        player.enabled=player.local=player.human=true;player.country=s.config.playerCountry;
        player.startPosition=1;player.color="Gold";s.config.playerSlots.push_back(player);
        player.local=player.human=false;player.country="Russians";player.startPosition=2;
        player.color="Red";s.config.playerSlots.push_back(player);
        s.rules=assets.rules;s.art=assets.art;s.theater=assets.theater;
        s.world=assets.world;s.world.accessObjects().clear();s.report=assets.report;s.mapFacts=assets.mapFacts;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        ensureRuntimeRepairDepotsInitialized(s);
        s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        // Keep the original 80x80 fixture's relative geometry when the map grows.
        // Translate the entire scene together, using only existing flat terrain.
        const auto occupied=buildRuntimeOccupancyLookup(s);
        const auto legalLayout=[&](int shift) {
            for(int y=72;y<=111;++y)for(int x=40;x<=111;++x) {
                const auto* terrain=findRuntimeTerrainCellAt(s,{x+shift,y+shift});
                if(!terrain||terrain->level!=0)return false;
            }
            for(const auto& building:std::array<std::pair<const char*,RuntimeCellPoint>,2>{{
                    {"GAAIRC",{44,76}},{"NAPOWR",{105,92}}}}) {
                WorldObjectInstance probe;probe.kind=WorldObjectKind::Structure;
                probe.type=building.first;probe.x=building.second.x+shift;probe.y=building.second.y+shift;
                for(const auto at:runtimeStructureFootprintCellsForObject(s,probe)) {
                    const auto* terrain=findRuntimeTerrainCellAt(s,at);
                    const auto* facts=runtimeMapQueryCell(s,at);
                    if(!terrain||!facts||!facts->hasTerrain||facts->isWater||facts->isShore
                        ||facts->isBridge||facts->isCliff||facts->isRamp
                        ||occupied.count(runtimeCellKey(at.x,at.y))
                        ||runtimeResourceOverlayBlocksPlacement(s,*terrain))return false;
                }
            }
            return true;
        };
        if(!layoutShift) {
            int front=1024;
            for(const auto& terrain:s.world.getTerrainCells())front=std::min(front,terrain.x+terrain.y);
            // The initial map's first diagonal was x+y=81; preserve its margin.
            for(int shift=std::max(0,(front-81+1)/2);shift+111<512;++shift)
                if(legalLayout(shift)){layoutShift=shift;break;}
            if(!layoutShift)return fail("no legal translated fixture terrain");
            std::cout<<"Aircraft flight integration: layoutShift="<<*layoutShift<<','<<*layoutShift<<'\n';
        }
        if(!legalLayout(*layoutShift))return fail("translated fixture terrain changed between scenes");
        const auto cell=[&](int x,int y){return RuntimeCellPoint{x+*layoutShift,y+*layoutShift};};
        auto* airport=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"GAAIRC",cell(44,76));
        if(!airport)return fail("airport creation");const int hostId=airport->instanceId;
        airport->buildAnimTicksRemaining=0;
        auto* aircraft=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,cell(46,78));
        if(!aircraft)return fail("aircraft creation");const int id=aircraft->instanceId;
        applyRuntimeAircraftDockPose(s,*aircraft,*findObjectById(s,hostId),0);
        const auto pose=[&](const RuntimeWindowState& state){const auto* p=findObjectById(state,id);
            return std::array<double,3>{p->hasRenderPosition?p->renderX:double(p->x),
                p->hasRenderPosition?p->renderY:double(p->y),p->hasRenderPosition?p->renderLevel:double(p->level)};};
        const auto step=[&](RuntimeWindowState& state){++state.tick;advanceRuntimeSimulation(state,false);completeRuntimeWorldRefresh(state);};
        initializeRuntimeOfflineGameplayRouting(s);s.selectedObjectIds={id};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,cell(103,88))) {
            const auto* rejected=findObjectById(s,id);
            std::ostringstream detail;
            detail<<"normal move command type="<<type<<" hz="<<hz<<" instant="<<instant
                <<" owner="<<s.economy.owner<<" mode="<<s.config.gameMode
                <<" multiplayer="<<s.config.multiplayerActive<<" observer="<<runtimeLocalObserver(s.config)
                <<" terminal="<<s.skirmishMatchFinished<<" authorities="<<s.humanHouseAuthorities.size()
                <<" selection="<<s.selectedObjectIds.size()<<" last="<<s.lastCommand.valid
                <<'/'<<s.lastCommand.acceptedCount<<'/'<<s.lastCommand.rejectedCount
                <<" targetTerrain="<<s.terrainByCell.count(runtimeCellKey(cell(103,88).x,cell(103,88).y))
                <<" interaction="<<s.interactionStatus<<" simulation="<<s.simulationStatus;
            if(rejected)detail<<" actor="<<rejected->instanceId<<'/'<<rejected->owner
                <<" at="<<rejected->x<<','<<rejected->y<<" alive="<<rejected->alive
                <<" mission="<<rejected->mission<<" canMove="<<runtimeObjectCanReceiveMoveCommand(s,*rejected)
                <<" allowsTarget="<<runtimeObjectAllowsMoveToCell(s,*rejected,cell(103,88));
            else detail<<" actor=missing";
            return fail(detail.str());
        }
        for(int n=0;n<7;++n)step(s);
        if(!findObjectById(s,id)->airborne||!findObjectById(s,id)->renderMoveActive)return fail("never took off");
        auto previous=pose(s);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,cell(94,105))
            ||pose(s)!=previous)return fail("redirect snapped pose");
        ++redirects;step(s);previous=pose(s);
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)
            ||pose(s)!=previous)return fail("Stop snapped pose");
        for(int n=0;n<3;++n){step(s);if(pose(s)!=previous)return fail("stopped aircraft drifted");}
        ++stops;
        for(bool stopped:{true,false}) {
            if(!stopped) {
                if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,cell(105,91)))return fail("resume");
                for(int n=0;n<5;++n)step(s);
            }
            std::ostringstream saved;IniFile ini;
            if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str()))return fail("full save: "+error);
            auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
            if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("full restore: "+error);
            initializeRuntimeNavigation(*restored);completeRuntimeWorldRefresh(*restored);
            if(pose(s)!=pose(*restored)||serializeRuntimeActiveAircraftFlights(s)!=serializeRuntimeActiveAircraftFlights(*restored))
                return fail("save lost continuous position");
            ++saves;
            for(int n=0;n<12;++n){step(s);step(*restored);++paired;
                if(pose(s)!=pose(*restored)||serializeRuntimeActiveAircraftFlights(s)!=serializeRuntimeActiveAircraftFlights(*restored))
                    return fail("restored flight diverged");}
            if(!stopped) {
                auto legacy=saved.str();const auto begin=legacy.find("[AircraftFlights]\n");
                const auto end=legacy.find("\n[",begin+1);
                if(begin==std::string::npos||end==std::string::npos)return fail("legacy save fixture");
                legacy.erase(begin,end+1-begin);IniFile oldIni;oldIni.loadFromString(legacy);
                auto old=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*old,s,false);
                if(!materializeRuntimeLoadStateFromIni(*old,oldIni,error)||!old->aircraftFlights.empty())
                    return fail("old save without continuous position rejected: "+error);
                const auto before=pose(*old);step(*old);
                if(!old->aircraftFlights.contains(id)||pose(*old)==before)return fail("legacy flight did not resume");
                ++legacyLoads;
            }
        }
        // Exact network snapshots must preserve the same flight authority,
        // including its fractional position between simulation boundaries.
        std::ostringstream snapshot;IniFile snapshotIni;
        if(!writeRuntimeStateToStream(s,snapshot,error)
            ||!writeRuntimeMultiplayerSnapshotExtension(s,snapshot,error)
            ||!snapshotIni.loadFromString(snapshot.str()))return fail("snapshot write: "+error);
        auto replica=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*replica,s,false);
        if(!materializeRuntimeLoadStateFromIni(*replica,snapshotIni,error))return fail("snapshot restore: "+error);
        initializeRuntimeNavigation(*replica);completeRuntimeWorldRefresh(*replica);
        if(!materializeRuntimeMultiplayerSnapshotExtension(*replica,snapshotIni,error))return fail("snapshot exact state: "+error);
        for(int n=0;n<12;++n) {
            std::vector<std::pair<std::string,std::uint64_t>> left,right;
            const auto a=runtimeStandaloneSimulationStateHash(s,error,&left),b=runtimeStandaloneSimulationStateHash(*replica,error,&right);
            if(!a||a!=b||pose(s)!=pose(*replica)) {
                std::cout<<"Aircraft snapshot mismatch tick="<<s.tick<<" pair="<<n<<" hash="<<a<<'/'<<b
                    <<" world="<<s.world.deterministicSimulationStateHash()<<'/'<<replica->world.deterministicSimulationStateHash()
                    <<" poses="<<(pose(s)==pose(*replica))<<" flights="
                    <<(serializeRuntimeActiveAircraftFlights(s)==serializeRuntimeActiveAircraftFlights(*replica))<<'\n';
                for(std::size_t i=0;i<std::min(left.size(),right.size());++i)if(left[i]!=right[i]) {
                    std::cout<<"Aircraft snapshot first section "<<left[i].first<<'\n';break;
                }
                if(output){std::ofstream before(std::filesystem::path(output)/"snapshot-source.ini"),after(std::filesystem::path(output)/"snapshot-restored.ini");
                    std::string diagnostic;writeRuntimeStateToStream(s,before,diagnostic);writeRuntimeMultiplayerSnapshotExtension(s,before,diagnostic);
                    writeRuntimeStateToStream(*replica,after,diagnostic);writeRuntimeMultiplayerSnapshotExtension(*replica,after,diagnostic);}
                return fail("full snapshot flight diverged: "+error);
            }
            step(s);step(*replica);++paired;
        }
        ++snapshots;
        const auto original=serializeRuntimeActiveAircraftFlights(s);
        for(int corrupt=0;corrupt<7;++corrupt) {
            auto bad=s.aircraftFlights;auto& f=bad.at(id);
            if(corrupt==0)f.x=-1;
            if(corrupt==1)f.y=512*runtimeAircraftCellQ16;
            if(corrupt==2)f.lastTick=s.tick+1;
            if(corrupt==3)f.x+=2*runtimeAircraftCellQ16;
            if(corrupt==4){auto copy=f;bad.clear();bad.emplace(999999,copy);}
            if(corrupt==5)f.heading=-2;
            if(corrupt==6)f.heading=65536;
            IniFile invalid;std::string rejectedError;
            invalid.loadFromString("[AircraftFlights]\nSchema=1\nDataHex="+iniHexEncode(serializeRuntimeAircraftFlights(bad))+"\n");
            if(readRuntimeAircraftFlights(s,invalid,rejectedError)||rejectedError.empty()
                ||serializeRuntimeActiveAircraftFlights(s)!=original)return fail("invalid flight state was not rejected atomically");
            ++rejections;
        }
        bool landed=false;
        for(int n=0;n<hz*100;++n){step(s);const auto* p=findObjectById(s,id);
            if(!p||!p->alive)return fail("aircraft lost during return");
            if(!p->airborne&&p->mission=="Dock"){landed=true;break;}}
        if(!landed)return fail("did not return to assigned airport");
        const auto* p=findObjectById(s,id);const auto dock=runtimeAircraftDockSpawn(s,*findObjectById(s,hostId),p->dockIndex);
        if(p->renderX!=dock.renderX||p->renderY!=dock.renderY)return fail("wrong landing point");
        ++landings;++scenes;
        std::cout<<"Aircraft flight integration: "<<type<<" hz="<<hz<<" noCooldown="<<instant<<" redirect/stop/full-save/return=ok\n";
        if(output&&hz==30&&!instant) {
            s.selectedObjectIds={id};s.mouseClientPointValid=false;
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,cell(105,93)))return fail("visual sortie");
            for(int n=0;n<35;++n)step(s);
            for(int n=0;n<24;++n){step(s);
                const auto path=std::filesystem::path(output)/(std::string(type)+"-flight-"+(n<10?"0":"")+std::to_string(n)+".bmp");
                if(!drawRuntimeStateOffscreenAtCell(s,cell(66,81),800,600,error,path.string(),false,true,-1,nullptr,true))
                    return fail("native flight frame: "+error);
                ++frames;
            }
        }
        auto* victim=addRuntimeObjectInstanceForOwner(s,runtimeSkirmishOwnerForSlot(1),
            RuntimeBuildKind::Building,"NAPOWR",cell(105,92),std::nullopt,false);
        if(!victim)return fail("attack target creation");const int victimId=victim->instanceId;
        victim->buildAnimTicksRemaining=0;const int beforeHealth=victim->health;
        s.selectedObjectIds={id};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,cell(105,92),victim))
            return fail("contextual aircraft attack");
        bool fired=false;int chased=0;
        for(int n=0;n<hz*60;++n) {
            const auto before=pose(s);const int beforeFacing=findObjectById(s,id)->facing;
            step(s);const auto after=pose(s);
            const auto b=s.behaviors.find(id);
            if(b!=s.behaviors.end()&&b->second.state==RuntimeBehaviorState::Chasing) {
                if(std::hypot(after[0]-before[0],after[1]-before[1])<1e-8
                    ||std::hypot(after[0]-before[0],after[1]-before[1])
                        >1.0/runtimeFixedWingCruiseInterval(s,*findObjectById(s,id))+0.0001
                    ||runtimeFacingAbsDifference(beforeFacing,findObjectById(s,id)->facing)
                        >static_cast<int>(std::ceil(3.0*15/hz))+1)
                    return fail("attack chase paused, exceeded cruise speed or snapped heading");
                ++chased;
            }
            const auto* target=findObjectById(s,victimId);
            if(!target||!target->alive||target->health<beforeHealth){fired=true;break;}
        }
        if(!fired||chased<4)return fail("real attack never reached and damaged its target");
        ++attacks;
        if(hz==30) {
            std::vector<int> group{id};
            for(int slot=1;slot<4;++slot) {
                auto* member=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,cell(46,78));
                if(!member)return fail("four-plane group creation");group.push_back(member->instanceId);
                applyRuntimeAircraftDockPose(s,*member,*findObjectById(s,hostId),slot);
            }
            s.selectedObjectIds=group;
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,cell(88,105))
                ||s.lastCommand.acceptedCount!=4)return fail("four-plane move order");
            std::set<int> destinations;
            for(int member:group){const auto& target=s.behaviors.at(member).moveTarget;
                destinations.insert(runtimeCellKey(target.x,target.y));}
            if(destinations.size()!=4)return fail("formation lost separate destinations");
            bool allDocked=false;
            for(int n=0;n<hz*100;++n){step(s);allDocked=true;std::set<int> slots;
                for(int member:group){const auto* p=findObjectById(s,member);
                    if(!p||!p->alive)return fail("four-plane flight lost aircraft");
                    allDocked=allDocked&&!p->airborne&&p->mission=="Dock"&&p->dockedToObjectId==hostId;
                    slots.insert(p->dockIndex);}
                allDocked=allDocked&&slots.size()==4;if(allDocked)break;
            }
            if(!allDocked)return fail("four-plane flight did not return");++groups;
        }
    }
    std::cout<<"Aircraft flight integration: passed scenes="<<scenes<<" saves="<<saves
        <<" pairedTicks="<<paired<<" redirects="<<redirects<<" stops="<<stops<<" landings="<<landings
        <<" snapshots="<<snapshots<<" invalidSaves="<<rejections<<" nativeFrames="<<frames<<" attacks="<<attacks
        <<" legacyLoads="<<legacyLoads<<" fourPlaneGroups="<<groups<<'\n';
    return true;
}
