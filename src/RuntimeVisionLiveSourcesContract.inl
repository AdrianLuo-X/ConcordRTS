#include "RuntimeVisionTerrainCoordinateContract.inl"

namespace RuntimeVisionLiveSourcesContractDetail {
struct Observation {
    std::vector<RuntimeVisionLiveSourcesStats> calls;
    const GameWorld* expected=nullptr;
    bool foreign=false;
    static void observe(void* raw,const GameWorld& world,int,const RuntimeVisionLiveSourcesStats& stats) {
        auto& out=*static_cast<Observation*>(raw);
        if(out.expected!=&world) out.foreign=true;
        out.calls.push_back(stats);
    }
};
inline bool primitive(std::string& error) {
    using RuntimeVisionTerrainCoordinateContractDetail::equalVision;
    const auto fail=[&](const std::string& why) { error="vision live sources primitive: "+why; return false; };
    const std::string originalRules=
        "[InfantryTypes]\n0=SCOUT\n[BuildingTypes]\n0=SPY\n1=GAP\n"
        "[General]\nVeteranSight=2\n[SCOUT]\nStrength=100\nSight=3\n"
        "VeteranAbilities=SIGHT\nEliteAbilities=SIGHT\n"
        "[SPY]\nStrength=100\nSight=1\nSpySat=yes\n"
        "[GAP]\nStrength=100\nSight=1\nGapGenerator=yes\nGapRadiusInCells=4\nPowered=yes\n";
    std::size_t comparisons=0,savedRangeEntries=0,completePassWitnesses=0,addressChecks=0;
    for(int history:{0,1000,10000}) for(bool memo:{false,true}) {
        GameWorld world;
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            world.accessTerrainCells().push_back(cell);
        }
        IniFile ini; RulesDatabase rules;
        if(!ini.loadFromString(originalRules) || !rules.loadFromIni(ini)) return fail("authored rules load");
        WorldObjectInstance scout;
        scout.instanceId=1; scout.kind=WorldObjectKind::Infantry; scout.type="SCOUT";
        scout.owner=" Alpha "; scout.x=scout.y=6; scout.alive=true; scout.health=100;
        scout.mission="Guard"; world.accessObjects().push_back(scout);
        auto duplicate=scout; duplicate.x=22; duplicate.y=6; duplicate.health=0;
        world.accessObjects().push_back(duplicate); // Same ID and live health=0 must BOTH emit sight.
        auto passenger=scout; passenger.instanceId=2; passenger.x=6; passenger.y=22;
        world.accessObjects().push_back(passenger);
        auto host=scout; host.instanceId=3; host.kind=WorldObjectKind::Unit; host.type="UNKNOWN";
        host.alive=false; host.health=0; host.transportPassengerIds={2};
        world.accessObjects().push_back(host); // Dead hosts still suppress living passenger sight.
        auto gap=scout; gap.instanceId=4; gap.kind=WorldObjectKind::Structure; gap.type="GAP";
        gap.owner="Beta"; gap.x=gap.y=16; gap.poweredOperational=true;
        world.accessObjects().push_back(gap);
        auto spy=gap; spy.instanceId=5; spy.type="SPY"; spy.owner="Alpha"; spy.alive=false;
        world.accessObjects().push_back(spy);
        auto alliedGap=gap; alliedGap.instanceId=6; alliedGap.owner="Alpha";
        world.accessObjects().push_back(alliedGap); // Each observer has an enemy gap and its own sight.
        for(int n=0;n<history;++n) {
            auto dead=scout; dead.instanceId=7+n; dead.alive=false; dead.health=0;
            dead.owner=n%2?"Beta":"Alpha"; world.accessObjects().push_back(std::move(dead));
        }
        // Literal address/order oracle for the helper, before any actual update.
        {
            RuntimeVisionLiveSources list(world,1,false);
            for(const auto& object:world.getObjects()) list.appendDuringCargoScan(object);
            auto it=list.begin();
            for(const auto& object:world.getObjects()) if(object.alive) {
                if(!(it!=list.end()) || &*it!=&object) return fail("source pointer/order/duplicate identity");
                ++it; ++addressChecks;
            }
            if(it!=list.end() || list.stats().cargoHostVisits!=world.getObjects().size())
                return fail("source address range/cargo host count");
        }
        RuntimeVisionSystem systems[2];
        for(auto& vision:systems) vision.setSourceMemoEnabledForDiagnostics(memo);
        std::vector<RuntimeVisionObserverDefinition> observers{{"Alpha",{"Alpha"}},{"Beta",{"Beta"}}};
        for(int stage=0;stage<9;++stage) {
            auto& objects=world.accessObjects();
            if(stage==1) { objects[3].transportPassengerIds.clear(); objects[2].mission="InsideTransport"; }
            if(stage==2) { objects[2].mission="Guard"; objects[3].transportPassengerIds={1}; }
            if(stage==3) { objects[3].transportPassengerIds.clear(); objects[5].alive=true; objects[5].health=0; }
            if(stage==4) { objects[5].poweredOperational=false; objects[4].poweredOperational=false; }
            if(stage==5) {
                objects[4].poweredOperational=true;
                observers[0].sharedVisionOwners.push_back("Beta");
                std::reverse(objects.begin(),objects.end());
            }
            if(stage==6) {
                std::string changed=originalRules;
                changed.replace(changed.find("Sight=3"),std::strlen("Sight=3"),"Sight=5");
                if(!ini.loadFromString(changed) || !rules.loadFromIni(ini)) return fail("rules reload");
                observers[0].sharedVisionOwners={"Alpha"};
            }
            if(stage==7) {
                for(auto& object:objects) if(object.type=="SCOUT" && object.alive) {
                    object.hasRenderPosition=true; object.renderX=14.49; object.renderY=14.51;
                    object.veterancy=2;
                }
                for(auto& vision:systems) {
                    (void)vision.resetShroudArea("Alpha",14,14,3);
                    (void)vision.revealArea("Beta",30,30,1);
                }
                const auto attacker=std::find_if(objects.begin(),objects.end(),[](const auto& object) {
                    return object.alive && object.type=="SCOUT";
                });
                const auto target=std::find_if(objects.begin(),objects.end(),[](const auto& object) {
                    return object.type=="GAP" && object.owner=="Beta";
                });
                if(attacker==objects.end() || target==objects.end()) return fail("fire-reveal source fixture");
                WeaponType weapon; weapon.revealOnFire=true;
                if(systems[0].notifyWeaponFired(*attacker,*target,weapon,107,15)
                    !=systems[1].notifyWeaponFired(*attacker,*target,weapon,107,15))
                    return fail("native fire-reveal action comparison");
            }
            if(stage==8) { observers.pop_back(); world.accessTerrainCells()[0].x=33; }
            const auto before=world.deterministicSimulationStateHash();
            bool changed[2]{}; Observation records[2];
            for(int branch=0;branch<2;++branch) {
                RuntimeVisionLiveSourcesReferenceScope route(branch==0);
                records[branch].expected=&world;
                RuntimeVisionLiveSourcesObserverScope observe(&Observation::observe,&records[branch]);
                changed[branch]=systems[branch].update(world,stage==8?nullptr:&rules,observers,100+stage);
            }
            std::string detail;
            if(changed[0]!=changed[1] || !equalVision(systems[1],systems[0],100+stage,detail)
                || world.deterministicSimulationStateHash()!=before)
                return fail("complete public vision/world mismatch history="+std::to_string(history)
                    +" memo="+std::to_string(memo)+" stage="+std::to_string(stage)+" "+detail);
            if(records[0].foreign || records[1].foreign || records[0].calls.size()!=1 || records[1].calls.size()!=1)
                return fail("actual update observation owner/count");
            const auto& old=records[0].calls.front(); const auto& now=records[1].calls.front();
            std::size_t alive=0; for(const auto& object:objects) if(object.alive) ++alive;
            if(!old.reference || now.reference || old.cargoHostVisits!=objects.size()
                || now.cargoHostVisits!=objects.size() || old.aliveAddresses!=alive || now.aliveAddresses!=alive
                || old.sourcePasses!=now.sourcePasses || old.sourceRangeEntries!=old.originalRangeEntries
                || now.sourceRangeEntries!=alive*now.sourcePasses || now.originalRangeEntries!=old.originalRangeEntries)
                return fail("actual source-pass/cargo/health0/duplicate witness");
            if(stage==0 && old.sourcePasses!=(memo?8u:6u))
                return fail("both observers must execute all enabled original source loops");
            if(stage==0) {
                const auto* alpha=systems[1].statsFor("Alpha");
                if(!alpha || alpha->visionSources!=3 || alpha->spySatSources!=0
                    || systems[1].visibilityFor("Alpha",6,22)!=RuntimeCellVisibility::Shrouded)
                    return fail("duplicate live health0 sight/dead-host passenger oracle");
                if(memo) ++completePassWitnesses;
            }
            savedRangeEntries+=old.sourceRangeEntries-now.sourceRangeEntries;
            ++comparisons;
        }
    }
    // There is no borrowed list across update calls: nested reference changes
    // affect only the next locally constructed range, even for another world.
    {
        GameWorld a,b; WorldObjectInstance alive,dead; alive.alive=true; dead.alive=false;
        a.accessObjects()={alive,dead}; b.accessObjects()={dead,alive,dead};
        RuntimeVisionLiveSourcesReferenceScope outer(false);
        { RuntimeVisionLiveSources first(a,1); for(const auto& o:a.getObjects()) first.appendDuringCargoScan(o);
          if(first.sourceCount()!=1) return fail("outer route optimized"); }
        { RuntimeVisionLiveSourcesReferenceScope disabled(true);
          RuntimeVisionLiveSources old(b,1); for(const auto& o:b.getObjects()) old.appendDuringCargoScan(o);
          if(old.sourceCount()!=3 || &*old.begin()!=&b.getObjects().front()) return fail("nested reference/foreign route"); }
        a.accessObjects().front().alive=false; a.accessObjects().back().alive=true;
        RuntimeVisionLiveSources fresh(a,1); for(const auto& o:a.getObjects()) fresh.appendDuringCargoScan(o);
        if(fresh.sourceCount()!=1 || &*fresh.begin()!=&a.getObjects().back())
            return fail("same-tick resurrection requires fresh local addresses");
    }
    {
        GameWorld empty; RuntimeVisionSystem old,current;
        const std::vector<RuntimeVisionObserverDefinition> observers{{"Alpha",{"Alpha"}}};
        bool changed[2]{};
        { RuntimeVisionLiveSourcesReferenceScope reference(true); changed[0]=old.update(empty,nullptr,observers,0); }
        { RuntimeVisionLiveSourcesReferenceScope reference(false); changed[1]=current.update(empty,nullptr,observers,0); }
        std::string detail;
        if(changed[0]!=changed[1] || !equalVision(current,old,0,detail)) return fail("empty world/null rules: "+detail);
    }
    if(comparisons!=54 || completePassWitnesses!=3 || !savedRangeEntries || addressChecks!=30)
        return fail("primitive coverage/witness incomplete");
    std::cout<<"Vision live sources primitive: "<<comparisons<<" complete public grid/stats/revision/epoch/world checks, "
        <<completePassWitnesses<<" four-source-loop cases, "<<addressChecks<<" exact source addresses, "
        <<savedRangeEntries<<" original dead-filter candidate slots omitted.\n";
    error.clear(); return true;
}
}

bool runRuntimeVisionLiveSourcesActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeVisionLiveSourcesContractDetail;
    using RuntimeVisionTerrainCoordinateContractDetail::equalVision;
    using RuntimePlannerPassabilityContractDetail::Authority;
    using RuntimePlannerPassabilityContractDetail::capture;
    const auto fail=[&](const std::string& why) { error="vision live sources actual: "+why; return false; };
    if(!primitive(error)) return false;
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("actual stock resource setup");
    if(trimAsciiLocal(assets->config.defaultMapPath).empty())
        return fail("actual startup source map identity absent");
    std::size_t comparisons=0,actualUpdates=0,savedRangeEntries=0,cargoVisits=0;
    for(int history:{0,1000,10000}) for(bool memo:{false,true}) {
        std::unique_ptr<RuntimeWindowState> states[2];
        for(int branch=0;branch<2;++branch) {
            RuntimeVisionLiveSourcesReferenceScope route(branch==0);
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            state.config=assets->config; state.report=assets->report; state.rules=assets->rules;
            state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state); std::vector<int> ids;
            if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true,false,96,96,true,true))
                return fail("native stock48 fixture: "+error);
            // Native generated map content is retained. The complete writer
            // needs a nonempty identity: reuse the verified startup source
            // path. Saves are compared, never loaded as that original map.
            state.config.defaultMapPath=assets->config.defaultMapPath;
            int next=nextRuntimeObjectInstanceId(state);
            for(int n=0;n<history;++n) {
                auto dead=RuntimePlannerPassabilityContractDetail::object(next+n,WorldObjectKind::Unit,"MTNK",2,2);
                dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                if(n==0) dead.transportPassengerIds={ids.back()};
                state.world.accessObjects().push_back(std::move(dead));
            }
            auto info=std::move(state.terrainInfoByCell); state.objectIndex=RuntimeObjectIndex{};
            initializeRuntimeNavigation(state); state.terrainInfoByCell=std::move(info);
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            invalidateRuntimeTickOccupancyCache(state); (void)completeRuntimeWorldRefresh(state);
            state.vision.setSourceMemoEnabledForDiagnostics(memo);
            state.tick=1; const auto* mover=findObjectById(state,ids.front());
            if(!mover) return fail("native mover lookup");
            const RuntimeCellPoint goal{mover->x+8,mover->y+8};
            (void)state.vision.exploreArea(state.economy.owner,goal.x,goal.y,8);
            applySelection(state,{ids.front()},false,false); issueRuntimeMoveCommand(state,goal);
            if(state.lastCommand.acceptedCount!=1 || state.behaviors.empty())
                return fail("native normal command failed to create real continuation");
            (void)completeRuntimeWorldRefresh(state);
        }
        for(int step=0;step<4;++step) {
            Authority full[2]; bool changed[2]{}; Observation records[2];
            for(int branch=0;branch<2;++branch) {
                auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
                RuntimeVisionLiveSourcesReferenceScope route(branch==0);
                records[branch].expected=&state.world;
                { RuntimeVisionLiveSourcesObserverScope observe(&Observation::observe,&records[branch]);
                  changed[branch]=advanceRuntimeSimulation(state); }
                (void)completeRuntimeWorldRefresh(state);
                if(!capture(state,full[branch],error)) return fail("actual complete serialized writer: "+error);
            }
            std::string detail;
            if(changed[0]!=changed[1] || !(full[0]==full[1])
                || !equalVision(states[1]->vision,states[0]->vision,states[1]->tick,detail))
                return fail("per-tick world/full-save/vision history="+std::to_string(history)
                    +" memo="+std::to_string(memo)+" step="+std::to_string(step)+" "+detail);
            if(records[0].foreign || records[1].foreign || records[0].calls.size()<2
                || records[0].calls.size()!=records[1].calls.size())
                return fail("native first/final vision caller owner/count");
            for(std::size_t i=0;i<records[0].calls.size();++i) {
                const auto& old=records[0].calls[i]; const auto& now=records[1].calls[i];
                if(!old.reference || now.reference || old.sourcePasses!=now.sourcePasses
                    || old.cargoHostVisits!=now.cargoHostVisits || old.aliveAddresses!=now.aliveAddresses
                    || old.sourceRangeEntries!=old.originalRangeEntries || now.sourceRangeEntries>old.sourceRangeEntries)
                    return fail("native ordered cargo/source pass witness");
                if(history && old.sourcePasses && old.sourceRangeEntries-now.sourceRangeEntries<static_cast<std::size_t>(history)*old.sourcePasses)
                    return fail("native history source loops did not omit every dead record");
                savedRangeEntries+=old.sourceRangeEntries-now.sourceRangeEntries; cargoVisits+=now.cargoHostVisits;
                ++actualUpdates;
            }
            ++comparisons; ++states[0]->tick; ++states[1]->tick;
        }
    }
    if(comparisons!=24 || actualUpdates<48 || !savedRangeEntries || !cargoVisits)
        return fail("actual full comparison/caller/saving witness incomplete");
    std::cout<<"Vision live source update: fullWorld/fullSave/publicGrids/stats/revisions/epochs="<<comparisons
        <<" actualUpdates="<<actualUpdates<<" deadFilterRangeEntriesSaved="<<savedRangeEntries
        <<" allCargoHostVisits="<<cargoVisits<<" histories=0/1000/10000 passed\n";
    error.clear(); return true;
}
