// Append after the unchanged RuntimePathSnapshotMetadataQueriesContract.inl.
#pragma once

namespace RuntimePathSnapshotConditionalQueriesContractDetail {
struct Observation {
    const RuntimeWindowState* expected=nullptr;
    bool foreign=false;
    std::vector<RuntimePathSnapshotConditionalQueryStats> calls;
    static void observe(void* raw,const RuntimeWindowState& state,const RuntimePathSnapshotConditionalQueryStats& stats) {
        auto& out=*static_cast<Observation*>(raw);
        out.foreign|=out.expected!=&state;
        out.calls.push_back(stats);
    }
};
struct Full {
    RuntimePlannerPassabilityContractDetail::Authority authority;
    std::uint64_t special=0,persistent=0;
    bool operator==(const Full&) const=default;
};
bool capture(RuntimeWindowState& state,Full& out,std::string& error) {
    RuntimeSnapshotActiveStateScope active(state);
    (void)completeRuntimeWorldRefresh(state);
    out.special=state.specialMechanisms.stateHash();
    out.persistent=runtimePersistentTransientStateHash(state);
    return RuntimePlannerPassabilityContractDetail::capture(state,out.authority,error);
}
}

bool runRuntimePathSnapshotConditionalQueriesContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePathSnapshotConditionalQueriesContractDetail;
    using RuntimePathSnapshotMetadataQueriesContractDetail::fields;
    const auto fail=[&](const std::string& why) {
        error="path snapshot conditional pure queries: "+why; return false;
    };
    std::size_t comparisons=0,fieldCells=0,airSkipped=0,blockedSkipped=0;
    const auto compare=[&](RuntimeWindowState& old,RuntimeWindowState& current,
        int moverId,int allowed,const std::string& label) {
        Full before[2],after[2];
        std::vector<PathTerrainCell> snapshots[2]; Observation observations[2];
        bool air=false;
        { RuntimeSnapshotActiveStateScope active(old);
          const auto* mover=findObjectById(old,moverId);
          if(!mover) return fail(label+" original mover absent");
          air=runtimeObjectMovementMode(old,*mover)==PathMovementMode::Air; }
        for(int branch=0;branch<2;++branch) {
            auto& state=branch?current:old;
            RuntimeSnapshotActiveStateScope active(state);
            if(!capture(state,before[branch],error)) return fail(label+" full writer before: "+error);
            RuntimePathSnapshotMetadataQueryReferenceScope reference(branch==0);
            state.pathTerrainCache.dynamicValid=false;
            observations[branch].expected=&state;
            { RuntimePathSnapshotConditionalQueryObserverScope observe(&Observation::observe,&observations[branch]);
              const auto* mover=findObjectById(state,moverId);
              if(!mover) return fail(label+" mover vanished");
              snapshots[branch]=buildRuntimePathTerrainSnapshot(state,*mover,allowed,nullptr,false); }
            if(!capture(state,after[branch],error)) return fail(label+" full writer after: "+error);
        }
        if(!(before[0]==before[1]) || !(after[0]==after[1]) || !(before[0]==after[0]) || !(before[1]==after[1]))
            return fail(label+" complete authority/full save mutated or diverged");
        if(snapshots[0].empty() || snapshots[0].size()!=snapshots[1].size()) return fail(label+" snapshot shape");
        for(std::size_t n=0;n<snapshots[0].size();++n) {
            if(fields(snapshots[0][n])!=fields(snapshots[1][n]))
                return fail(label+" all-12-fields/order offset="+std::to_string(n));
            ++fieldCells;
        }
        if(observations[0].foreign || observations[1].foreign
            || observations[0].calls.size()!=1 || observations[1].calls.size()!=1)
            return fail(label+" cold production observer owner/count");
        const auto& a=observations[0].calls.front(); const auto& b=observations[1].calls.front();
        const auto count=snapshots[0].size();
        if(!a.reference || b.reference || a.metadataLookups!=count || a.occupantLookups!=count
            || a.metadataSkipped || a.occupantSkipped
            || b.metadataLookups+b.metadataSkipped!=count || b.occupantLookups+b.occupantSkipped!=count)
            return fail(label+" exact actual main-loop query invocation counts");
        if(air) {
            if(b.metadataLookups || b.occupantLookups || b.metadataSkipped!=count || b.occupantSkipped!=count)
                return fail(label+" Air must skip ALL unused metadata/occupancy finds");
            airSkipped+=b.metadataSkipped+b.occupantSkipped;
        } else blockedSkipped+=b.metadataSkipped+b.occupantSkipped;
        ++comparisons; return true;
    };
    // Keep the existing 216-case large metadata/full-vector matrix unchanged.
    // This additional bounded matrix witnesses the NEW production call counts,
    // actual repair reservation, source mutation and native radiation field.
    for(int history:{0,1000}) for(int phase=0;phase<5;++phase) {
        std::unique_ptr<RuntimeWindowState> states[2];
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            RuntimeSnapshotActiveStateScope active(state);
            if(!RuntimePlannerPassabilityContractDetail::make(state,config,history,0,error))
                return fail("synthetic production setup: "+error);
            if(state.repairDepots.relations().size()!=1 || !state.terrainInfoByCell.at(runtimeCellKey(10,5)).bridge)
                return fail("actual repair/bridge readback");
            IniFile authored;
            std::string rules=std::string(RuntimePlannerPassabilityContractDetail::rulesText)
                +"[Clear]\nTrack=100%\nFoot=100%\nFloat=0%\nHover=100%\n"
                 "[Water]\nTrack=0%\nFoot=0%\nFloat=100%\nHover=100%\n"
                 "[Rock]\nTrack=0%\nFoot=0%\nFloat=0%\nHover=0%\n"
                 "[Tiberium]\nTrack=75%\nFoot=50%\nFloat=0%\nHover=100%\n";
            if(phase==4) rules+="[Clear]\nTrack=0%\n";
            if(!authored.loadFromString(rules) || !state.rules.loadFromIni(authored)) return fail("movement rules load/reload");
            // Rules replacement ends all old capability/terrain-query lifetimes.
            state.capabilityCache.profilesByType.clear();
            if(phase==1) {
                for(auto& cell:state.world.accessTerrainCells()) if(cell.x==2 && cell.y==2) {
                    cell.hasOverlay=cell.overlayIsOre=true; cell.overlayIsGem=false;
                }
                state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=RuntimeTerrainLandType::Rock;
            }
            if(phase==2) for(auto& [key,info]:state.terrainInfoByCell) { (void)key; info.passable=false; }
            if(phase==3) {
                state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                const int area=state.specialMechanisms.spawnRadiationArea(1,"Player",{2,2},4,100,1,
                    "",state.tick,true,SpecialEventSink{});
                const auto at=state.specialMechanisms.radiationAreas().find(area);
                if(area<0 || at==state.specialMechanisms.radiationAreas().end()
                    || at->second.strength<=0 || at->second.remainingTicks<=0)
                    return fail("actual native radiation site readback");
                state.specialMechanismsInitialized=true;
            }
            if(phase==4) {
                state.terrainInfoByCell.erase(runtimeCellKey(2,2));
                state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=RuntimeTerrainLandType::Clear;
                auto* mover=findMutableObjectById(state,1);
                if(!mover || !mover->alive || mover->instanceId!=1) return fail("source-edit actor identity");
                mover->facing=173;
            }
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            state.foundationCollisionTick=-1; invalidateRuntimeTickOccupancyCache(state);
            (void)completeRuntimeWorldRefresh(state);
        }
        for(int mover:{1,2,3,4,5,6}) for(int allowed:{-1,30,33})
            if(!compare(*states[0],*states[1],mover,allowed,"history="+std::to_string(history)
                +" phase="+std::to_string(phase)+" mover="+std::to_string(mover)+" allowed="+std::to_string(allowed)))
                return false;
        if(phase==1) {
            auto& state=*states[1]; RuntimeSnapshotActiveStateScope active(state);
            const auto* mover=findObjectById(state,1);
            const auto* tiberium=state.rules.findLandType("Tiberium");
            if(!mover || !tiberium) return fail("actual resource profile witness");
            const double percent=runtimeLandTypePercentForMover(*tiberium,state,*mover);
            const auto cells=buildRuntimePathTerrainSnapshot(state,*mover);
            const auto at=std::find_if(cells.begin(),cells.end(),[](const auto& cell) {
                return cell.cell.x==2 && cell.cell.y==2;
            });
            const auto& base=state.pathTerrainCache.staticCells;
            const auto original=std::find_if(base.begin(),base.end(),[](const auto& cell) {
                return cell.cell.x==2 && cell.cell.y==2;
            });
            if(percent!=75.0 || at==cells.end() || original==base.end() || !at->resource || !at->passable
                || at->cost!=runtimeLandTypeCost(original->cost,percent) || at->cost==original->cost)
                return fail("actual native resource cost transform not exercised");
        }
        if(phase==3) {
            RuntimeSnapshotActiveStateScope active(*states[1]);
            const auto cells=buildRuntimePathTerrainSnapshot(*states[1],*findObjectById(*states[1],1));
            if(std::none_of(cells.begin(),cells.end(),[](const auto& cell){return cell.hazardCost>0;}))
                return fail("radiation matrix never produced an actual hazard cost");
        }
    }
    // Native stock assets and a normal 48-actor map prove Air and Ground here
    // are actual runtime movement modes, rather than merely forced enum cases.
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("native stock resources");
    if(trimAsciiLocal(assets->config.defaultMapPath).empty())
        return fail("native startup source map identity absent");
    for(int phase=0;phase<2;++phase) {
        std::unique_ptr<RuntimeWindowState> states[2]; std::array<std::vector<int>,2> ids;
        std::array<int,2> groundIds{-1,-1},airIds{-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            state.config=assets->config; state.report=assets->report; state.rules=assets->rules;
            state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,48,ids[branch],error,false,true,false,96,96,true,true))
                return fail("native stock48 map: "+error);
            // Generated native map contents are compared under a real startup
            // source identity; this is a full-writer comparison, not reload.
            state.config.defaultMapPath=assets->config.defaultMapPath;
            for(int id:ids[branch]) {
                const auto* actor=findObjectById(state,id); if(!actor) return fail("native actor ID");
                const auto mode=runtimeObjectMovementMode(state,*actor);
                if(mode==PathMovementMode::Ground && groundIds[branch]<0) groundIds[branch]=id;
                if(mode==PathMovementMode::Air && airIds[branch]<0) airIds[branch]=id;
            }
            if(groundIds[branch]<0 || airIds[branch]<0) return fail("native broad roster actual Ground/Air absent");
            if(phase) {
                for(auto& [key,info]:state.terrainInfoByCell) {
                    (void)key; info.passable=false; info.cost=27;
                }
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1] || groundIds[0]!=groundIds[1] || airIds[0]!=airIds[1]) return fail("native independent actor IDs/order");
        if(!compare(*states[0],*states[1],groundIds[0],-1,"native Ground phase="+std::to_string(phase))
            || !compare(*states[0],*states[1],airIds[0],-1,"native Air phase="+std::to_string(phase))) return false;
    }
    if(comparisons!=184 || !airSkipped || !blockedSkipped || !fieldCells)
        return fail("actual matrix/query-elision coverage witnesses incomplete");
    std::cout<<"Path conditional pure lookups: fullSnapshotCases="<<comparisons<<" fullFieldCells="<<fieldCells
        <<" fields=12 AirUnusedFindsSkipped="<<airSkipped<<" blockedUnusedFindsSkipped="<<blockedSkipped
        <<" completeWorld/fullSave/special/repair/bridge/resource/radiation/sourceMutation=native passed=yes\n";
    error.clear(); return true;
}
