// Staged only. Include after the original TerrainQueries and ColdLookup contracts.
#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace RuntimePlannerMetadataBorrowContractDetail {
using InfoMap = RuntimeTerrainMetadataMap<RuntimeTerrainInfo>;
using TerrainMap = RuntimePlannerTerrainQueries::TerrainMap;
using Full = RuntimePlannerPassabilityContractDetail::Authority;
using Queries = RuntimePlannerPassabilityContractDetail::Observations;
bool sameInfo(const RuntimeTerrainInfo& a, const RuntimeTerrainInfo& b) {
    return RuntimePlannerTerrainQueriesContractDetail::sameInfo(a,b);
}
RuntimeTerrainInfo infoFor(std::size_t n) {
    RuntimeTerrainInfo v;
    constexpr auto lands=static_cast<std::size_t>(RuntimeTerrainLandType::Tunnel)+1;
    v.movementLandType=static_cast<RuntimeTerrainLandType>(n%lands);
    v.movementRampType=static_cast<unsigned char>(n%256);
    v.kind=static_cast<RuntimeTerrainKind>(n%5);
    v.authoredLandType=static_cast<RuntimeTerrainLandType>((n+3)%lands);
    v.passable=(n&1)!=0;v.water=(n&2)!=0;v.shore=(n&4)!=0;
    v.cliff=(n&8)!=0;v.ramp=(n&16)!=0;v.bridge=(n&32)!=0;
    v.cost=static_cast<int>(n%71)-35;return v;
}
const RuntimeTerrainInfo* original(const RuntimeWindowState& s,int key) {
    const auto it=s.terrainInfoByCell.find(key);
    return it==s.terrainInfoByCell.end()?nullptr:std::addressof(it->second);
}
struct Witness {
    const RuntimeWindowState* expected=nullptr;
    std::size_t enabled=0,borrowed=0,ownedBytes=0,examined=0,entries=0;
    bool foreign=false;
    static void append(void* opaque,const RuntimePlannerMetadataBorrowSummary& v) {
        auto& self=*static_cast<Witness*>(opaque);
        if(v.state!=self.expected){self.foreign=true;return;}
        self.enabled+=v.enabled;self.borrowed+=v.borrowed;self.ownedBytes+=v.ownedInfoBytes;
        self.examined+=v.sourceEntriesExamined;self.entries+=v.retainedInfoEntries;
    }
};
struct Counts { std::size_t addresses=0,values=0,writers=0,maps=0,commands=0,tickPairs=0,nativeBorrowed=0; };
bool checkMap(RuntimeWindowState& s,const RuntimePlannerTerrainQueries::ReadOnlyView& view,Counts& c) {
    for(const auto& [key,info]:s.terrainInfoByCell) {
        const auto* v=view.cachedInfoAtKey(key);
        if(v!=std::addressof(info)||!v||!sameInfo(*v,info))return false;
        ++c.addresses;++c.values;
    }
    return true;
}
bool primitive(std::string& error,Counts& counts) {
    const auto fail=[&](const std::string& why){error="component metadata borrow: "+why;return false;};
    RuntimeComponentTerrainQueryReferenceScope terrain(false);
    RuntimeComponentMetadataBorrowReferenceScope borrow(false);
    auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;s.tick=73;
    auto& cells=s.world.accessTerrainCells();cells.resize(4099);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto n=static_cast<std::size_t>(y)*64+x;auto& cell=cells[n];
        cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
        s.terrainByCell.emplace(runtimeCellKey(x,y),&cell);
        s.terrainInfoByCell.emplace(runtimeCellKey(x,y),infoFor(n));
    }
    cells[4096].x=1001;cells[4096].y=2;
    cells[4097].x=512;cells[4097].y=0;
    cells[4098].x=7;cells[4098].y=600;
    s.terrainByCell[runtimeCellKey(7,7)]=&cells[4096]; // Returned-cell metadata key differs.
    s.terrainByCell[runtimeCellKey(512,0)]=&cells[4097];
    s.terrainByCell[runtimeCellKey(7,600)]=&cells[4098];
    s.terrainByCell[runtimeCellKey(2,2)]=nullptr;
    s.terrainByCell.erase(runtimeCellKey(3,3));
    for(int key:{runtimeCellKey(511,511),runtimeCellKey(512,0),runtimeCellKey(7,600),
        -1,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
        s.terrainInfoByCell.emplace(key,infoFor(static_cast<std::size_t>(static_cast<unsigned int>(key))));
    s.terrainInfoByCell.at(runtimeCellKey(1,3)).water=true;
    s.terrainInfoByCell.at(runtimeCellKey(7,7)).water=false;
    const int missing=runtimeCellKey(9,9);s.terrainInfoByCell.erase(missing);
    if(s.terrainInfoByCell.size()<4096||s.terrainInfoByCell.denseValid())return fail("cold fixture admission");
    const auto startBuilds=s.terrainInfoByCell.denseBuildCount();
    {
        RuntimePlannerTerrainQueries q(s,s.terrainByCell);const auto view=q.viewFor(s,s.terrainByCell);
        if(!view||!q.metadataBorrowed()||q.ownedDenseInfoBytes()!=0
            ||q.denseInfoBytes()!=262144*sizeof(const RuntimeTerrainInfo*)
            ||q.metadataSourceEntriesExamined()!=s.terrainInfoByCell.size()
            ||s.terrainInfoByCell.denseBuildCount()!=startBuilds+1)
            return fail("cold exact owner table, no second owned info table");
        if(!checkMap(s,*view,counts))return fail("complete metadata node addresses/all11 fields");
        for(const auto& [key,cell]:s.terrainByCell) {
            if(view->terrainAtKey(key)!=cell)return fail("exact terrain key/null/payload pointer");
            ++counts.addresses;
            if(cell&&!sameInfo(view->infoForCell(*cell),runtimeCachedTerrainInfoForCell(s,*cell)))
                return fail("returned raw cell key, stale/misaligned map-key payload or scalar fallback");
            if(cell)++counts.values;
        }
        for(int y=0;y<512;++y)for(int x=0;x<512;++x) {
            if(view->cachedInfoAtKey(runtimeCellKey(x,y))!=original(s,runtimeCellKey(x,y)))
                return fail("all512 squared metadata holes/address identity");
            ++counts.addresses;
        }
        if(runtimeCellKey(1001,2)!=runtimeCellKey(1,3)||!view->infoForCell(cells[4096]).water)
            return fail("legacy alias/foreign-coordinate payload changed key selection");
        std::size_t fallbacks=0;const auto count=s.terrainInfoByCell.size();
        for(int n=0;n<4;++n) {
            const auto v=view->infoForCellWithFallback(cells[9*64+9],[&](const TerrainCell& cell){
                ++fallbacks;return runtimeCachedTerrainInfoForCell(s,cell);});
            if(!sameInfo(v,runtimeCachedTerrainInfoForCell(s,cells[9*64+9])))return fail("actual miss fallback fields");
        }
        if(fallbacks!=4||s.terrainInfoByCell.size()!=count||s.terrainInfoByCell.contains(missing))
            return fail("missing metadata was inserted/remembered/eagerly computed");
        auto foreign=std::make_unique<RuntimeWindowState>();foreign->tick=s.tick;
        auto foreignMap=s.terrainByCell;
        if(q.viewFor(*foreign,s.terrainByCell)||q.viewFor(s,foreignMap))return fail("foreign world/map borrowed");
    }
    const auto warmBuilds=s.terrainInfoByCell.denseBuildCount();
    for(int n=0;n<16;++n) {
        ++s.tick;RuntimePlannerTerrainQueries q(s,s.terrainByCell);
        if(!q.metadataBorrowed()||q.metadataSourceEntriesExamined()!=0||q.ownedDenseInfoBytes()!=0
            ||!q.viewFor(s,s.terrainByCell))return fail("warm lexical owner scanned/copied info again");
    }
    if(s.terrainInfoByCell.denseBuildCount()!=warmBuilds)return fail("warm map table rebuilt");
    {
        RuntimePlannerTerrainQueries q(s,s.terrainByCell);
        // Never use an already-acquired ReadOnlyView across these writes; acquire
        // again to prove that the table stores live node addresses, not values.
        const int key=runtimeCellKey(2,3);const auto generation=s.terrainInfoByCell.generation();
        s.terrainInfoByCell.at(key)=infoFor(22222);
        auto it=s.terrainInfoByCell.find(key);it->second.cost=-991;
        const auto view=q.viewFor(s,s.terrainByCell);
        if(!view||s.terrainInfoByCell.generation()!=generation
            ||view->cachedInfoAtKey(key)!=std::addressof(it->second)
            ||!sameInfo(*view->cachedInfoAtKey(key),it->second))return fail("at/iterator values were copied");
        ++counts.values;
    }
    const InfoMap baseline=s.terrainInfoByCell;
    for(int writer=0;writer<24;++writer) {
        s.terrainInfoByCell=baseline;
        RuntimePlannerTerrainQueries old(s,s.terrainByCell);
        if(!old.viewFor(s,s.terrainByCell)||!old.metadataBorrowed())return fail("structural precondition");
        const auto generation=s.terrainInfoByCell.generation();
        auto& map=s.terrainInfoByCell;const int key=runtimeCellKey(2,3);
        switch(writer) {
        case 0:map.reserve(map.size());break;
        case 1:map.rehash(map.bucket_count());break;
        case 2:map.clear();for(const auto& [k,v]:baseline)map.emplace(k,v);break;
        case 3:map.erase(key);map.emplace(key,infoFor(777));break;
        case 4:map.emplace(key,infoFor(778));break;
        case 5:map.try_emplace(key,infoFor(779));break;
        case 6:map[key]=infoFor(780);break;
        case 7:map[missing]=infoFor(781);break;
        case 8:map.erase(std::numeric_limits<int>::max()-1);break;
        case 9:map.erase(map.find(key));map.emplace(key,infoFor(782));break;
        case 10:{const auto it=map.cbegin();const auto k=it->first;map.erase(it);map.emplace(k,infoFor(783));break;}
        case 11:{const auto first=map.cbegin();auto last=first;++last;const auto k=first->first;
            map.erase(first,last);map.emplace(k,infoFor(784));break;}
        case 12:map=baseline;break;
        case 13:{InfoMap moved(baseline);(void)moved.densePointers();map=std::move(moved);
            if(moved.denseValid())return fail("move assignment source not dirty");break;}
        case 14:{InfoMap moved(std::move(map));if(moved.denseValid()||map.denseValid())return fail("move construction inherited pointers");map=std::move(moved);break;}
        case 15:{InfoMap other(baseline);other.at(key)=infoFor(785);(void)other.densePointers();
            using std::swap;swap(map,other);if(other.denseValid())return fail("ADLswap foreign pointers not dirty");break;}
        case 16:map={{key,infoFor(786)}};map=baseline;break;
        case 17:map.try_emplace(map.cbegin(),key,infoFor(787));break;
        case 18:map.try_emplace(int(key),infoFor(788));break;
        case 19:map.try_emplace(map.cbegin(),int(key),infoFor(789));break;
        case 20:map[int(key)]=infoFor(790);break;
        case 21:map=map;break;
        case 22:map=std::move(map);break;
        case 23:map.swap(map);break;
        }
        if(map.generation()==generation||map.denseValid()||old.viewFor(s,s.terrainByCell))
            return fail("structural writer stale view writer="+std::to_string(writer));
        RuntimePlannerTerrainQueries fresh(s,s.terrainByCell);const auto view=fresh.viewFor(s,s.terrainByCell);
        if(!view||!fresh.metadataBorrowed()||!checkMap(s,*view,counts))return fail("fresh owner points at old nodes");
        ++counts.writers;
    }
    {
        InfoMap copied(s.terrainInfoByCell);
        if(copied.denseValid())return fail("copied cache retained foreign node addresses");
        const auto& pointers=copied.densePointers();const int key=runtimeCellKey(2,3);
        if(pointers[3*512+2]!=std::addressof(copied.at(key))
            ||pointers[3*512+2]==original(s,key))return fail("copy table's node ownership");
        RuntimePlannerTerrainQueries q(s,s.terrainByCell);
        {RuntimeComponentMetadataBorrowReferenceScope ref(true);
            if(q.viewFor(s,s.terrainByCell))return fail("borrowed view survived independent reference change");
            RuntimePlannerTerrainQueries owned(s,s.terrainByCell);
            if(owned.metadataBorrowed()||owned.ownedDenseInfoBytes()!=262144*sizeof(const RuntimeTerrainInfo*)
                ||!owned.viewFor(s,s.terrainByCell))return fail("independent ref failed to restore old info allocation");
            {RuntimeComponentMetadataBorrowReferenceScope nested(false);
                RuntimePlannerTerrainQueries fast(s,s.terrainByCell);if(!fast.metadataBorrowed())return fail("nested fast override");}
            if(!runtimeComponentMetadataBorrowReference())return fail("nested ref restoration");
        }
        if(runtimeComponentMetadataBorrowReference())return fail("outer ref restoration");
        const auto builds=s.terrainInfoByCell.denseBuildCount();
        {RuntimeComponentTerrainQueryReferenceScope ref(true);RuntimePlannerTerrainQueries disabled(s,s.terrainByCell);
            if(disabled.metadataBorrowed()||disabled.denseInfoBytes()||disabled.denseTerrainBytes())return fail("old terrain ref acquired table");}
        RuntimePlannerTerrainQueries disabled(s,s.terrainByCell,false);
        if(disabled.metadataBorrowed()||disabled.denseInfoBytes()||s.terrainInfoByCell.denseBuildCount()!=builds)
            return fail("Air/disabled allocated or built info");
        q.invalidate();if(q.viewFor(s,s.terrainByCell))return fail("owner invalidate retained view");
        RuntimePlannerTerrainQueries tick(s,s.terrainByCell);++s.tick;
        if(tick.viewFor(s,s.terrainByCell))return fail("view crossed tick");
    }
    // The optional borrow adds no broader eligibility than the existing owner.
    // Empty/tiny metadata still uses the former owned table; an oversized map
    // preserves every original find instead of retaining another owner cache.
    {
        auto fallbackState=std::make_unique<RuntimeWindowState>();auto& f=*fallbackState;
        f.tick=s.tick;f.terrainByCell=s.terrainByCell;
        for(const int count:{0,17,4095}) {
            f.terrainInfoByCell.clear();
            for(int n=0;n<count;++n)f.terrainInfoByCell.emplace(runtimeCellKey(n%64,n/64),infoFor(n));
            const auto builds=f.terrainInfoByCell.denseBuildCount();
            RuntimePlannerTerrainQueries q(f,f.terrainByCell);const auto view=q.viewFor(f,f.terrainByCell);
            if(!view||q.metadataBorrowed()||q.ownedDenseInfoBytes()!=262144*sizeof(const RuntimeTerrainInfo*)
                ||f.terrainInfoByCell.denseValid()||f.terrainInfoByCell.denseBuildCount()!=builds
                ||!checkMap(f,*view,counts))return fail("empty/tiny metadata did not retain original owned route");
        }
        f.terrainInfoByCell.clear();f.terrainInfoByCell.reserve(262145);
        for(int n=0;n<262145;++n)f.terrainInfoByCell.emplace(n,infoFor(n));
        RuntimePlannerTerrainQueries q(f,f.terrainByCell);const auto view=q.viewFor(f,f.terrainByCell);
        if(!view||q.metadataBorrowed()||f.terrainInfoByCell.denseValid()
            ||q.metadataSourceEntriesExamined()!=262145||!checkMap(f,*view,counts))
            return fail("oversized metadata changed original finds/retained owner cache");
        auto tinyTerrain=f.terrainByCell;
        while(tinyTerrain.size()>=4096)tinyTerrain.erase(tinyTerrain.begin());
        RuntimePlannerTerrainQueries disabled(f,tinyTerrain);
        if(disabled.metadataBorrowed()||disabled.denseInfoBytes()||disabled.denseTerrainBytes())
            return fail("small supplied terrain acquired any optional table");
    }
    return true;
}

bool native(const RuntimeConfig& config,Counts& counts,std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    const auto fail=[&](const std::string& why){error="metadata borrow native: "+why;return false;};
    RuntimeComponentPassabilityReferenceScope passability(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrain(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    RuntimeComponentCrushAliasReferenceScope alias(false);
    RuntimeComponentIndexedTopologyReferenceScope indexed(true);
    RuntimeComponentColdLookupReferenceScope cold(false);
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded)return fail("stock assets unavailable");
    for(int side:{240,256})for(int history:{0,10000}) {
        const int population=side==240?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;std::array<std::vector<int>,2> ids;
        std::array<int,2> movers{-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& s=*states[branch];
            s.config=config;s.report=assets->report;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
            s.rulesLoaded=assets->report.rulesLoaded;s.artLoaded=assets->report.artLoaded;s.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(s);
            if(!prepareRuntimeMassMovementWorld(s,population,ids[branch],error,false,true,false,side,side,true,true))return false;
            s.config.defaultMapPath=s.report.map.path;
            if(s.report.map.path!="MassMovement-v1.map"||!s.mapFactsLoaded
                ||s.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side)
                ||ids[branch].size()!=static_cast<std::size_t>(population))return fail("native fixture identity/cardinality");
            if(history){
                const int first=nextRuntimeObjectInstanceId(s);
                if(first<=0||first>std::numeric_limits<int>::max()-history)return fail("history ID bound");
                s.world.accessObjects().reserve(s.world.getObjects().size()+history);
                for(int n=0;n<history;++n){auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";s.world.accessObjects().push_back(std::move(dead));}
                auto metadata=std::move(s.terrainInfoByCell);s.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(s);s.terrainInfoByCell=std::move(metadata);
                s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false;invalidateRuntimeTickOccupancyCache(s);
            }
            (void)completeRuntimeWorldRefresh(s);
            if(!branch)for(int id:ids[branch]){const auto* actor=findObjectById(s,id);if(!actor)return fail("native actor");
                if(runtimeObjectMovementMode(s,*actor)!=PathMovementMode::Ground)continue;
                if(actor->kind==WorldObjectKind::Unit&&movers[0]<0)movers[0]=id;
                if(actor->kind==WorldObjectKind::Infantry&&movers[1]<0)movers[1]=id;}
        }
        if(ids[0]!=ids[1]||movers[0]<0||movers[1]<0)return fail("native identical actor order");
        for(int id:movers){
            std::vector<std::pair<int,int>> expectedMap;Queries expectedQueries;std::optional<RuntimeCellPoint> expectedTie;
            Full expectedBefore;
            for(int branch=0;branch<2;++branch){auto& s=*states[branch];RuntimeSnapshotActiveStateScope active(s);
                RuntimeComponentMetadataBorrowReferenceScope route(branch==0);
                const auto* mover=findObjectById(s,id);if(!mover)return fail("component actor");
                const auto occupied=buildRuntimeOccupancyLookup(s);Full before,after;
                if(!capture(s,before,error))return false;
                if(!branch)expectedBefore=before;else if(!(before==expectedBefore))
                    return fail("native complete original initial writer identity");
                Witness witness;witness.expected=&s;Queries queries;std::unordered_map<int,int> result;
                {RuntimePlannerMetadataBorrowObserverScope owner(&Witness::append,&witness);
                    RuntimePlannerPassabilityObserverScope observe(&Queries::append,&queries);
                    result=buildRuntimeMoveComponentByCell(s,*mover,s.terrainByCell,occupied);}
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;if(const auto found=result.find(runtimeCellKey(mover->x,mover->y));found!=result.end())required=found->second;
                if(required<0||order.empty())return fail("native complete component not exercised");
                const auto tie=findNearestRuntimePassableCellWithOccupancy(s,*mover,{500,500},-1,nullptr,
                    s.terrainByCell,occupied,nullptr,&result,required);
                if(!tie)return fail("native actual global fallback winner absent");
                if(!branch){expectedMap=order;expectedQueries=std::move(queries);expectedTie=tie;}
                else if(order!=expectedMap||queries.values!=expectedQueries.values||tie!=expectedTie)
                    return fail("entire native ordered component/query/global equal-score winner");
                if(witness.foreign||witness.enabled!=1||!witness.entries
                    ||(branch?(witness.borrowed!=1||witness.ownedBytes!=0):(witness.borrowed!=0||witness.ownedBytes!=262144*sizeof(const RuntimeTerrainInfo*))))
                    return fail("actual native borrowed/old-owned positive owner witness");
                counts.nativeBorrowed+=witness.borrowed;++counts.maps;
                if(!capture(s,after,error)||!(before==after))return fail("BFS changed complete full writer");
            }
        }
        bool moved=false;
        for(int tick=1;tick<=32;++tick){Full expected;std::map<int,RuntimeCellPoint> expectedDestinations;int accepted=-1,rejected=-1;
            for(int branch=0;branch<2;++branch){auto& s=*states[branch];s.tick=tick;
                RuntimeSnapshotActiveStateScope active(s);RuntimeComponentMetadataBorrowReferenceScope route(branch==0);
                if(tick==1||tick==9||tick==17){std::map<int,RuntimeCellPoint> destinations;
                    const int mid=side/2;const RuntimeCellPoint goal=tick==9?RuntimeCellPoint{mid+22,mid+48}:RuntimeCellPoint{mid+76,mid+32};
                    Witness witness;witness.expected=&s;
                    {RuntimePlannerMetadataBorrowObserverScope observe(&Witness::append,&witness);
                        if(!issueRuntimeMassMovementOrder(s,ids[branch],goal,destinations,error,true))return false;}
                    if(!branch){expectedDestinations=destinations;accepted=s.lastCommand.acceptedCount;rejected=s.lastCommand.rejectedCount;}
                    else if(destinations!=expectedDestinations||accepted!=s.lastCommand.acceptedCount||rejected!=s.lastCommand.rejectedCount)
                        return fail("native actual command destination/accepted/rejected trajectory");
                    if(witness.foreign||witness.enabled!=1
                        ||(branch?(witness.borrowed!=1||witness.ownedBytes!=0):(witness.borrowed!=0||!witness.ownedBytes)))
                        return fail("native command missing true metadata route");
                    counts.nativeBorrowed+=witness.borrowed;++counts.commands;
                }
                // Complete the real tick before calling the original writer.
                (void)advanceRuntimeSimulation(s);(void)completeRuntimeWorldRefresh(s);
                Full current;if(!capture(s,current,error))return false;
                if(!branch)expected=std::move(current);else if(!(current==expected))return fail("native completed-tick all worlds/systems/save");
                moved|=std::any_of(s.behaviors.begin(),s.behaviors.end(),[](const auto& v){return v.second.moveCooldownTicks>0||!v.second.path.empty();});
            }
            ++counts.tickPairs;
        }
        if(!moved)return fail("native actual movement absent");
    }
    return counts.maps==16&&counts.commands==24&&counts.tickPairs==128&&counts.nativeBorrowed==20
        ?true:fail("native full matrix/witness count");
}
} // namespace RuntimePlannerMetadataBorrowContractDetail

bool runRuntimePlannerMetadataBorrowContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePlannerMetadataBorrowContractDetail;
    Counts counts;
    if(!primitive(error,counts))return false;
    // Keep actual original complete directed BFS and ColdLookup tests in both
    // routes. Their original counts, full map iteration, tie and saved-authority
    // assertions are not reconstructed or replaced by a hash-only model.
    for(bool reference:{true,false}){
        RuntimeComponentMetadataBorrowReferenceScope route(reference);
        if(!runRuntimePlannerTerrainQueriesPrimitiveContract(error)
            ||!runRuntimePlannerTerrainQueriesDirectedContract(config,error)
            ||!runRuntimePlannerColdCellLookupsActualContract(config,error))return false;
    }
    if(!native(config,counts,error))return false;
    std::cout<<"Component metadata borrow: structuralWriters="<<counts.writers
        <<" nodeAddresses="<<counts.addresses<<" allFieldCopies="<<counts.values
        <<" nativeWholeMaps="<<counts.maps<<" nativeCommands="<<counts.commands
        <<" nativeTickFullSavePairs="<<counts.tickPairs<<" nativeBorrowedOwners="<<counts.nativeBorrowed
        <<" originalDirected/orderedBFS/globalTie/fullWriter/native240/native256=exact passed=yes\n";
    error.clear();return true;
}
