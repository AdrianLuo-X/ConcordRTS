// Include after RuntimePlannerIndexedTopologyContract.inl and TerrainQueriesContract.
// This new corpus extends the authored synthetic terrain only in this fixture;
// the old fixtures, rules, assertions and production graph remain untouched.
#pragma once
#include <array>
#include <limits>
#include <memory>
#include <tuple>

namespace RuntimePlannerColdLookupContractDetail {
using namespace RuntimePlannerPassabilityContractDetail;
struct Witness {
    std::size_t owners=0,enabled=0,fullValidations=0,views=0,fallbacks=0;
    std::size_t facts=0,stateTerrain=0,suppliedTerrain=0,info=0;
    static void append(void* raw,std::size_t active,std::size_t full,std::size_t used,
        std::size_t fallback,std::size_t queriedFacts,std::size_t queriedState,
        std::size_t queriedSupplied,std::size_t queriedInfo) {
        auto& out=*static_cast<Witness*>(raw); ++out.owners; out.enabled+=active;
        out.fullValidations+=full; out.views+=used; out.fallbacks+=fallback;
        out.facts+=queriedFacts; out.stateTerrain+=queriedState;
        out.suppliedTerrain+=queriedSupplied; out.info+=queriedInfo;
    }
};
bool sameInfo(const RuntimeTerrainInfo& a,const RuntimeTerrainInfo& b) {
    return std::tie(a.movementLandType,a.movementRampType,a.kind,a.authoredLandType,
        a.passable,a.water,a.shore,a.cliff,a.ramp,a.bridge,a.cost)
        ==std::tie(b.movementLandType,b.movementRampType,b.kind,b.authoredLandType,
        b.passable,b.water,b.shore,b.cliff,b.ramp,b.bridge,b.cost);
}
bool largeFixture(RuntimeWindowState& state,const RuntimeConfig& config,int history,int phase,std::string& error) {
    if(!RuntimePlannerIndexedTopologyContractDetail::fixture(state,config,history,phase,error)) return false;
    // Preserve every original authored 12x12 cell/duplicate/object/reservation,
    // gate/repair rule and metadata value. Extra clear cells admit the existing
    // 4096-entry terrain table without weakening its production threshold.
    auto& cells=state.world.accessTerrainCells(); cells.reserve(4097);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) if(x>=12 || y>=12) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x; cells.push_back(cell);
    }
    state.terrainByCell=buildRuntimeTerrainLookup(state.world);
    for(const auto& [key,cell]:state.terrainByCell) {
        (void)cell;
        if(!state.terrainInfoByCell.contains(key)) {
            RuntimeTerrainInfo info; info.authoredLandType=info.movementLandType=RuntimeTerrainLandType::Clear;
            state.terrainInfoByCell.emplace(key,info);
        }
    }
    state.mapFacts.cells.clear(); state.mapFacts.cellIndexLookup.clear();
    state.mapFacts.cells.reserve(4096);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const auto& info=state.terrainInfoByCell.at(runtimeCellKey(x,y));
        MapCellFacts fact; fact.x=x; fact.y=y; fact.cellIndex=y*512+x; fact.hasTerrain=true;
        fact.isWater=info.water; fact.isShore=info.shore; fact.isBridge=info.bridge;
        fact.isCliff=info.cliff; fact.isRamp=info.ramp;
        state.mapFacts.cellIndexLookup.emplace(fact.cellIndex,state.mapFacts.cells.size());
        state.mapFacts.cells.push_back(std::move(fact));
    }
    state.mapFactsLoaded=true;
    // The original facts lookup honors the index, not these payload coordinates.
    state.mapFacts.cells[1].x=-42; state.mapFacts.cells[1].y=100000;
    state.mapFacts.cells[1].cellIndex=std::numeric_limits<int>::max();
    state.mapFacts.cellIndexLookup[2]=state.mapFacts.cells.size()+11;
    state.mapFacts.cellIndexLookup.erase(3);
    invalidateRuntimeTickOccupancyCache(state);
    state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
    if(state.terrainByCell.size()!=4096 || cells.size()!=4097)
        {error="planner cold lookups: enlarged authored fixture cardinality";return false;}
    return true;
}
const TerrainCell* scalarTerrain(const RuntimePlannerTerrainQueries::TerrainMap& map,int key) {
    const auto found=map.find(key); return found==map.end()?nullptr:found->second;
}
} // namespace RuntimePlannerColdLookupContractDetail

bool runRuntimePlannerColdCellLookupsActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePlannerColdLookupContractDetail;
    RuntimeComponentPassabilityReferenceScope memo(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrainMemo(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    RuntimeComponentCrushAliasReferenceScope alias(false);
    RuntimeComponentIndexedTopologyReferenceScope indexed(true);
    RuntimeComponentColdLookupReferenceScope cold(false);
    const auto fail=[&](const std::string& why) {error="planner cold lookups: "+why;return false;};
    std::size_t pointerChecks=0,boundaries=0,edges=0,components=0,commands=0,ticks=0;
    Witness total;
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!largeFixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const auto* mover=findObjectById(state,1); if(!mover) return fail("pointer mover missing");
        Authority before,after; if(!capture(state,before,error)) return false;
        RuntimePlannerTerrainQueries table(state,state.terrainByCell);
        RuntimePlannerColdCellLookups owner(state,*mover,state.terrainByCell,occupied,table);
        if(!owner.enabled() || owner.fullValidations()!=1
            || owner.extraBytes()!=RuntimePlannerColdCellLookups::denseCellCount*sizeof(const MapCellFacts*))
            return fail("same-map alias must borrow existing tables and allocate only facts");
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            const RuntimeCellPoint point{x,y}; const auto view=owner.viewFor(state,*mover,state.terrainByCell,occupied,point);
            const auto* terrain=scalarTerrain(state.terrainByCell,runtimeCellKey(x,y));
            if(!view || view->facts()!=runtimeMapQueryCell(state,point) || view->stateTerrain()!=terrain
                || view->suppliedTerrain()!=terrain || !terrain
                || !sameInfo(view->infoForCell(*terrain),runtimeCachedTerrainInfoForCell(state,*terrain)))
                return fail("complete typed pointers/stale full metadata point="+std::to_string(x)+","+std::to_string(y));
            ++pointerChecks;
        }
        if(!capture(state,after,error) || !(before==after)) return fail("pointer preparation/query changed full writer");
        for(const auto point:{RuntimeCellPoint{-1,0},{512,0},{0,512},{1000,0}}) {
            if(owner.viewFor(state,*mover,state.terrainByCell,occupied,point)) return fail("noncanonical point borrowed");
            ++boundaries;
        }
        auto foreignMap=state.terrainByCell; auto foreignOccupancy=occupied; const auto foreignMover=*mover;
        auto foreignState=std::make_unique<RuntimeWindowState>();
        if(owner.viewFor(*foreignState,*mover,state.terrainByCell,occupied,{0,0})
            || owner.viewFor(state,foreignMover,state.terrainByCell,occupied,{0,0})
            || owner.viewFor(state,*mover,foreignMap,occupied,{0,0})
            || owner.viewFor(state,*mover,state.terrainByCell,foreignOccupancy,{0,0}))
            return fail("foreign state/mover/map/occupancy borrowed");
        boundaries+=4;
        {
            RuntimeComponentColdLookupReferenceScope reference(true);
            if(owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("dynamic reference borrowed");
            RuntimePlannerColdCellLookups disabled(state,*mover,state.terrainByCell,occupied,table);
            if(disabled.enabled() || disabled.extraBytes()) return fail("reference constructed/allocated");
            ++boundaries;
        }
        {
            RuntimeComponentTerrainQueryReferenceScope reference(true);
            if(owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("terrain reference borrowed");
            ++boundaries;
        }
        {
            RuntimePlannerColdCellLookups disabled(state,*mover,state.terrainByCell,occupied,table,false);
            if(disabled.enabled() || disabled.extraBytes() || owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0}))
                return fail("disabled nested did not mask outer");
            ++boundaries;
        }
        {
            RuntimePlannerColdCellLookups nested(state,*mover,state.terrainByCell,occupied,table);
            if(!nested.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})
                || owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("nested owner identity");
            ++boundaries;
        }
        {
            RuntimePlannerTerrainQueries foreignTable(state,foreignMap);
            RuntimePlannerColdCellLookups foreign(state,*mover,foreignMap,occupied,foreignTable);
            if(!foreign.enabled() || foreign.extraBytes()!=RuntimePlannerColdCellLookups::maximumExtraBytes
                || foreign.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})
                || owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("foreign nested or separate map allocation");
            ++boundaries;
        }
        {
            RuntimePlannerTerrainQueries foreignTable(*foreignState,foreignState->terrainByCell);
            RuntimePlannerColdCellLookups foreign(*foreignState,foreignMover,foreignState->terrainByCell,foreignOccupancy,foreignTable);
            if(foreign.enabled() || owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0}))
                return fail("foreign-world disabled owner did not mask outer");
            ++boundaries;
        }
        if(!owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("outer not restored");
        ++state.tick;
        if(owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})) return fail("tick boundary borrowed");
        --state.tick; ++boundaries;
        owner.invalidate();
        state.mapFacts.cells[0].hasTerrain=false;
        if(owner.viewFor(state,*mover,state.terrainByCell,occupied,{0,0})
            || runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},*mover))
            return fail("explicit invalidate before same-address facts edit must read new false");
        ++boundaries;
        const auto* oldFactsData=state.mapFacts.cells.data();
        state.mapFacts.cells.reserve(state.mapFacts.cells.capacity()+1);
        state.mapFacts.cells.push_back(MapCellFacts{});
        if(oldFactsData==state.mapFacts.cells.data()) return fail("facts layout fixture must really relocate");
        {
            RuntimePlannerTerrainQueries freshTable(state,state.terrainByCell);
            RuntimePlannerColdCellLookups fresh(state,*mover,state.terrainByCell,occupied,freshTable);
            const auto view=fresh.viewFor(state,*mover,state.terrainByCell,occupied,{0,0});
            if(!view || view->facts()!=runtimeMapQueryCell(state,{0,0}) || view->facts()->hasTerrain
                || runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},*mover))
                return fail("fresh owner after actual facts relocation must read new pointer/value");
            ++boundaries;
        }
    }
    // Original state-terrain answers are separate from supplied terrain. An
    // existing bridge metadata value in the supplied map cannot manufacture a
    // runtime bridge when the state map has a hole. No facts cell is generated.
    for(int variant=0;variant<12;++variant) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!largeFixture(state,config,0,0,error)) return false;
        if(variant==1) {
            // The 4097 authored cells include a duplicate: their lookup has
            // exactly 4096 unique entries. Preserve the original table's 4096
            // minimum AFTER removing the supplied bridge cell. Append before
            // copying maps/borrowing cell pointers, then rebuild all pointers
            // because accessTerrainCells().push_back can relocate the vector.
            const int extraKey=runtimeCellKey(64,63);
            if(state.terrainByCell.size()!=RuntimePlannerTerrainQueries::minimumTerrainEntries
                || state.terrainByCell.contains(extraKey) || state.terrainInfoByCell.contains(extraKey))
                return fail("supplied-hole fixture before canonical padding must contain 4096 unique keys");
            TerrainCell extraCell; extraCell.x=64; extraCell.y=63; extraCell.cellIndex512=63*512+64;
            state.world.accessTerrainCells().push_back(extraCell);
            state.terrainByCell=buildRuntimeTerrainLookup(state.world);
            RuntimeTerrainInfo extraInfo;
            extraInfo.authoredLandType=extraInfo.movementLandType=RuntimeTerrainLandType::Clear;
            const bool inserted=state.terrainInfoByCell.emplace(extraKey,extraInfo).second;
            const auto* insertedCell=scalarTerrain(state.terrainByCell,extraKey);
            if(!inserted || !insertedCell || insertedCell->x!=64 || insertedCell->y!=63
                || state.terrainByCell.size()!=RuntimePlannerTerrainQueries::minimumTerrainEntries+1
                || state.world.getTerrainCells().size()!=4098
                || state.terrainInfoByCell.at(extraKey).authoredLandType!=RuntimeTerrainLandType::Clear)
                return fail("supplied-hole canonical padding must produce 4097 unique keys before bridge removal");
        }
        auto supplied=state.terrainByCell;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const auto* mover=findObjectById(state,1); if(!mover) return fail("admission mover missing");
        const int key=runtimeCellKey(10,5); const auto* original=state.terrainByCell.at(key);
        TerrainCell replaced=*original;
        if(variant==0) {
            state.terrainByCell.erase(key);
            auto& fact=state.mapFacts.cells.at(state.mapFacts.cellIndexLookup.at(5*512+10));
            fact.isBridge=false;fact.isShore=false;fact.isWater=true;
        }
        if(variant==1) supplied.erase(key);
        if(variant==2) state.mapFactsLoaded=false;
        if(variant==3) state.mapFacts.cells.clear();
        if(variant==4) supplied[key]=nullptr;
        if(variant==5) state.terrainByCell[key]=nullptr;
        if(variant==6) {replaced.x=1000;replaced.y=0;supplied[key]=&replaced;}
        if(variant==7) supplied.emplace(std::numeric_limits<int>::max(),original);
        if(variant==8) supplied.emplace(-1,original);
        if(variant==9) {replaced.x=512;supplied[key]=&replaced;}
        if(variant==10) state.terrainInfoByCell.erase(key);
        if(variant==11) {state.terrainInfoByCell.at(key).bridge=false;state.terrainInfoByCell.at(key).passable=false;}
        if(variant==1 && (supplied.contains(key)
            || supplied.size()!=RuntimePlannerTerrainQueries::minimumTerrainEntries
            || state.terrainByCell.size()!=RuntimePlannerTerrainQueries::minimumTerrainEntries+1))
            return fail("supplied-hole removal must leave 4096 entries and retain the original state bridge");
        RuntimePlannerTerrainQueries table(state,supplied);
        RuntimePlannerColdCellLookups owner(state,*mover,supplied,occupied,table);
        const bool admissible=variant<=3 || variant==11;
        if(owner.enabled()!=admissible) return fail("whole fallback admission variant="+std::to_string(variant)
            +" expectedEnabled="+std::to_string(admissible)+" actualEnabled="+std::to_string(owner.enabled())
            +" suppliedEntries="+std::to_string(supplied.size())
            +" stateEntries="+std::to_string(state.terrainByCell.size())
            +" worldCells="+std::to_string(state.world.getTerrainCells().size())
            +" minimumTerrainEntries="+std::to_string(RuntimePlannerTerrainQueries::minimumTerrainEntries)
            +" terrainTableEntries="+std::to_string(table.denseTerrainEntries())
            +" metadataTableEntries="+std::to_string(table.denseInfoEntries())
            +" fullValidations="+std::to_string(owner.fullValidations()));
        const RuntimeCellPoint point{10,5}; const auto view=owner.viewFor(state,*mover,supplied,occupied,point);
        if(view && (view->facts()!=runtimeMapQueryCell(state,point)
            || view->stateTerrain()!=scalarTerrain(state.terrainByCell,key)
            || view->suppliedTerrain()!=scalarTerrain(supplied,key))) return fail("separate original maps/presence variant="+std::to_string(variant));
        bool expected=false,actual=false;
        const auto metadataCount=state.terrainInfoByCell.size();
        const auto metadataGeneration=state.terrainInfoByCell.generation();
        {RuntimeComponentColdLookupReferenceScope reference(true);
            expected=runtimeCellIsPassableUncached(state,supplied,occupied,point,*mover);}
        actual=runtimeCellIsPassableUncached(state,supplied,occupied,point,*mover);
        if(actual!=expected) return fail("anomalous scalar/facts/stale metadata result variant="+std::to_string(variant));
        if((variant==0 || variant==1 || variant==11) && expected)
            return fail("separate-map bridge/hole/stale-blocker negative witness variant="+std::to_string(variant));
        if(variant==10 && (metadataCount!=state.terrainInfoByCell.size()
            || metadataGeneration!=state.terrainInfoByCell.generation()))
            return fail("missing metadata fallback inserted a cache entry");
        ++boundaries;
    }
    // Kernel invalidation also closes the existing hot memo before in-place
    // edits. Layout/value changes are intentionally preceded by invalidate;
    // the cheap hot lease does not claim to detect arbitrary same-address edits.
    for(int variant=0;variant<8;++variant) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!largeFixture(state,config,0,0,error)) return false;
        // Keep the borrowed mover address valid when variant 5 appends one
        // actor after invalidation. This is a layout-change test, not a test
        // which deliberately dereferences a vector-invalidated reference.
        state.world.accessObjects().reserve(state.world.getObjects().size()+1);
        state.objectIndex=RuntimeObjectIndex{};
        auto occupied=buildRuntimeOccupancyLookup(state);
        const auto* mover=findObjectById(state,1); if(!mover) return fail("mutation mover missing");
        RuntimePlannerCellPassabilityScope memoScope(state,*mover,state.terrainByCell,occupied);
        RuntimePlannerConnectivityKernel owned(state,*mover,state.terrainByCell,occupied,false,&memoScope);
        const bool warm=owned.seedIsPassable({0,0});
        if(!warm) return fail("mutation fixture initial clear seed");
        owned.invalidate();
        if(variant==0) state.terrainInfoByCell.at(0).passable=false;
        if(variant==1) state.mapFacts.cells[0].hasTerrain=false;
        if(variant==2) state.terrainByCell[0]=nullptr;
        if(variant==3) state.terrainInfoByCell.erase(0);
        if(variant==4) state.mapFacts.cellIndexLookup[0]=state.mapFacts.cells.size()+1;
        if(variant==5) state.world.accessObjects().push_back(object(900,WorldObjectKind::Unit,"CAR",0,0));
        if(variant==6) occupied[0]=30;
        if(variant==7) std::reverse(state.world.accessTerrainCells().begin(),state.world.accessTerrainCells().end());
        bool expected=false;
        {RuntimeComponentColdLookupReferenceScope reference(true);
            expected=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},*mover);}
        if(owned.seedIsPassable({0,0})!=expected) return fail("invalidate mutation fallback variant="+std::to_string(variant));
        ++boundaries;
    }
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!largeFixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const std::array<RuntimeCellPoint,20> starts{{{0,0},{1,1},{2,1},{10,5},{10,4},{9,4},{8,5},{8,6},
            {5,7},{1,8},{8,4},{8,7},{8,8},{4,4},{4,6},{3,6},{63,63},{64,63},{1000,0},{-1,0}}};
        const std::array<std::optional<PathMovementMode>,6> modes{std::nullopt,PathMovementMode::Ground,
            PathMovementMode::Naval,PathMovementMode::Amphibious,PathMovementMode::Air,static_cast<PathMovementMode>(99)};
        const std::unordered_set<int> emptySoft,enemySoft{35};
        Authority before,after; if(!capture(state,before,error)) return false;
        RuntimeComponentPassabilityReferenceScope scalar(true);
        for(int id:{1,2,3,4,5,6,41}) {
            const auto* mover=findObjectById(state,id); if(!mover) return fail("edge mover missing");
            RuntimeBuildingDepartureReadOnlyQueryScope departure(state,*mover);
            RuntimePlannerConnectivityKernel owned(state,*mover,state.terrainByCell,occupied,false);
            for(const auto& from:starts) {
                std::vector<RuntimeCellPoint> targets{from,{from.x+2,from.y}};
                const auto adjacent=runtimeNeighborsOf(from); targets.insert(targets.end(),adjacent.begin(),adjacent.end());
                for(const auto& to:targets) for(auto mode:modes) for(int allowed:{-1,31}) for(bool crush:{false,true})
                    for(const auto* soft:{static_cast<const std::unordered_set<int>*>(nullptr),&emptySoft,&enemySoft}) {
                        Observations oldQueries,newQueries; bool expected=false,actual=false;
                        {RuntimeComponentColdLookupReferenceScope reference(true);
                            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&oldQueries);
                            expected=runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,from,to,*mover,allowed,soft,crush,mode);}
                        {RuntimePlannerPassabilityObserverScope observe(&Observations::append,&newQueries);
                            actual=runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,from,to,*mover,allowed,soft,crush,mode);}
                        if(actual!=expected || newQueries.values!=oldQueries.values)
                            return fail("full ordered scalar transition id="+std::to_string(id)+" from="+std::to_string(from.x)+","+std::to_string(from.y));
                        ++edges;
                    }
            }
        }
        if(!capture(state,after,error) || !(before==after)) return fail("directed kernel/metadata changed full writer");
    }
    for(int history:{0,1000,10000}) for(int phase:{0,1}) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!largeFixture(state,config,history,phase,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        Authority before,after; if(!capture(state,before,error)) return false;
        for(int id:{1,2,3,4,5,6,41}) {
            const auto* mover=findObjectById(state,id); if(!mover) return fail("component mover missing");
            std::vector<std::pair<int,int>> expected; Observations expectedQueries;
            std::optional<RuntimeCellPoint> expectedFallback;
            for(int branch=0;branch<2;++branch) {
                RuntimeComponentColdLookupReferenceScope reference(branch==0);
                Observations queries; Witness witness; std::unordered_map<int,int> result;
                {RuntimePlannerPassabilityObserverScope observe(&Observations::append,&queries);
                    RuntimePlannerColdLookupObserverScope observeOwner(&Witness::append,&witness);
                    result=buildRuntimeMoveComponentByCell(state,*mover,state.terrainByCell,occupied);}
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1; if(const auto found=result.find(runtimeCellKey(mover->x,mover->y));found!=result.end()) required=found->second;
                const auto fallback=findNearestRuntimePassableCellWithOccupancy(state,*mover,{100,100},-1,nullptr,
                    state.terrainByCell,occupied,nullptr,&result,required);
                if(!branch) {expected=order;expectedQueries=std::move(queries);expectedFallback=fallback;}
                else if(order!=expected || queries.values!=expectedQueries.values || fallback!=expectedFallback)
                    return fail("complete BFS seed/labels/unordered iteration/ordered queries/global tie history="+std::to_string(history)+" id="+std::to_string(id));
                if(!branch && (witness.enabled || witness.views || witness.fullValidations)) return fail("reference built/borrowed lookup");
                if(branch && runtimeObjectMovementMode(state,*mover)!=PathMovementMode::Air
                    && (witness.enabled!=1 || witness.fullValidations!=1 || !witness.views || !witness.suppliedTerrain || !witness.info))
                    return fail("ground complete BFS missing cold lookup/full-validation-once witness");
                total.enabled+=witness.enabled;total.views+=witness.views;total.facts+=witness.facts;
                total.stateTerrain+=witness.stateTerrain;total.suppliedTerrain+=witness.suppliedTerrain;total.info+=witness.info;
                ++components;
                if(!capture(state,after,error) || !(before==after)) return fail("complete BFS changed full authority");
            }
        }
    }
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded) return fail("actual stock assets unavailable");
    for(int history:{0,1000,10000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            state.config=config;state.report=assets->report;state.rules=assets->rules;state.art=assets->art;state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded;state.artLoaded=assets->report.artLoaded;state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,48,ids[branch],error,false,true,false,240,240,true,true)) return false;
            state.config.defaultMapPath=config.defaultMapPath.empty()?assets->report.map.path:config.defaultMapPath;
            if(state.config.defaultMapPath.empty() || state.world.getTerrainCells().size()!=114960 || ids[branch].size()!=48
                || !state.mapFactsLoaded) return fail("actual native240 cardinality/map identity/facts required");
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                if(first<=0 || first>std::numeric_limits<int>::max()-history) return fail("history ID capacity");
                for(int n=0;n<history;++n) {auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";state.world.accessObjects().push_back(std::move(dead));}
                auto metadata=std::move(state.terrainInfoByCell);state.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(state);state.terrainInfoByCell=std::move(metadata);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;invalidateRuntimeTickOccupancyCache(state);
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]) return fail("actual roster IDs/order");
        std::vector<int> infantry;
        for(int id:ids[0]) {const auto* actor=findObjectById(*states[0],id);if(actor && actor->kind==WorldObjectKind::Infantry) infantry.push_back(id);}
        if(infantry.size()!=24) return fail("actual infantry-only roster");
        // Compare the entire native 114960-cell result and its unordered
        // iteration, not merely the destinations returned by later commands.
        for(int id:{ids[0].front(),infantry.front()}) {
            std::vector<std::pair<int,int>> expectedMap;Observations expectedQueries;
            std::optional<RuntimeCellPoint> expectedFallback;
            for(int branch=0;branch<2;++branch) {
                RuntimeComponentColdLookupReferenceScope reference(branch==0);
                auto& state=*states[branch];RuntimeSnapshotActiveStateScope active(state);
                const auto* actor=findObjectById(state,id);if(!actor) return fail("actual whole component actor");
                const auto occupied=buildRuntimeOccupancyLookup(state);
                Authority before,after;if(!capture(state,before,error)) return false;
                Observations queries;Witness witness;std::unordered_map<int,int> result;
                {RuntimePlannerPassabilityObserverScope observe(&Observations::append,&queries);
                    RuntimePlannerColdLookupObserverScope observeOwner(&Witness::append,&witness);
                    result=buildRuntimeMoveComponentByCell(state,*actor,state.terrainByCell,occupied);}
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;if(const auto found=result.find(runtimeCellKey(actor->x,actor->y));found!=result.end()) required=found->second;
                const auto fallback=findNearestRuntimePassableCellWithOccupancy(state,*actor,{500,500},-1,nullptr,
                    state.terrainByCell,occupied,nullptr,&result,required);
                if(!branch) {expectedMap=order;expectedQueries=std::move(queries);expectedFallback=fallback;}
                else if(order!=expectedMap || queries.values!=expectedQueries.values || fallback!=expectedFallback)
                    return fail("actual complete native labels/iteration/queries/global tie history="+std::to_string(history));
                if(branch && (witness.enabled!=1 || witness.fullValidations!=1 || !witness.views || !witness.info))
                    return fail("actual full native graph positive lookup witness");
                if(!branch && (witness.enabled || witness.views || witness.fullValidations))
                    return fail("actual full native graph reference used lookup");
                if(!capture(state,after,error) || !(before==after)) return fail("actual full native graph mutated full writer");
                ++components;
            }
        }
        bool moving=false;
        for(int tick=1;tick<=32;++tick) {
            Authority expected;Observations expectedQueries;std::map<int,RuntimeCellPoint> expectedDestinations;
            int expectedAccepted=-1,expectedRejected=-1;
            for(int branch=0;branch<2;++branch) {
                RuntimeComponentColdLookupReferenceScope reference(branch==0);
                auto& state=*states[branch];state.tick=tick;RuntimeSnapshotActiveStateScope active(state);
                if(tick==1 || tick==9 || tick==17) {
                    const auto& selected=tick==9?infantry:ids[branch];
                    Observations queries;Witness witness;std::map<int,RuntimeCellPoint> destinations;
                    {RuntimePlannerPassabilityObserverScope observe(&Observations::append,&queries);
                        RuntimePlannerColdLookupObserverScope observeOwner(&Witness::append,&witness);
                        if(!issueRuntimeMassMovementOrder(state,selected,tick==9?RuntimeCellPoint{142,168}:RuntimeCellPoint{196,152},destinations,error,true)) return false;}
                    if(!branch) {expectedQueries=std::move(queries);expectedDestinations=destinations;
                        expectedAccepted=state.lastCommand.acceptedCount;expectedRejected=state.lastCommand.rejectedCount;}
                    else if(queries.values!=expectedQueries.values || destinations!=expectedDestinations
                        || state.lastCommand.acceptedCount!=expectedAccepted || state.lastCommand.rejectedCount!=expectedRejected)
                        return fail("actual ordered command queries/destinations tick="+std::to_string(tick));
                    if(!branch && (witness.enabled || witness.views || witness.fullValidations)) return fail("actual reference built/borrowed");
                    if(branch && (witness.enabled!=1 || witness.fullValidations!=1 || !witness.views
                        || !witness.facts || !witness.stateTerrain || !witness.suppliedTerrain || !witness.info))
                        return fail("actual positive facts/state/supplied/metadata and validation-once witness");
                    total.enabled+=witness.enabled;total.views+=witness.views;total.facts+=witness.facts;
                    total.stateTerrain+=witness.stateTerrain;total.suppliedTerrain+=witness.suppliedTerrain;total.info+=witness.info;
                    ++commands;
                }
                (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                Authority current;if(!capture(state,current,error)) return false;
                if(!branch) expected=std::move(current);
                else if(!(current==expected)) return fail("actual full writer/world/systems/path history="+std::to_string(history)+" tick="+std::to_string(tick));
                if(branch) for(int id:ids[1]) {const auto* actor=findObjectById(state,id);moving|=actor && actor->renderMoveActive;}
            }
            ++ticks;
        }
        if(!moving) return fail("actual movement/render trajectory absent");
    }
    if(pointerChecks!=4096 || boundaries!=37 || edges!=100800 || components!=96 || commands!=18 || ticks!=96
        || !total.views || !total.facts || !total.stateTerrain || !total.suppliedTerrain || !total.info
        || runtimeActivePlannerColdCellLookups) return fail("coverage/positive cold queries/restored TLS");
    std::cout<<"Planner cold scalar lookups: exactTypedPointers="<<pointerChecks<<" boundaryCases="<<boundaries
        <<" exactOrderedTransitions="<<edges<<" completeOrderedComponents="<<components
        <<" actualNative240Commands="<<commands<<" perTickFullSaveHashes="<<ticks
        <<" lookupViews="<<total.views<<" factsQueries="<<total.facts<<" stateTerrainQueries="<<total.stateTerrain
        <<" suppliedTerrainQueries="<<total.suppliedTerrain<<" metadataQueries="<<total.info
        <<" H0/H1000/H10000=yes validationOnceAndOriginalDynamicOrder=yes pass\n";
    error.clear();return true;
}
