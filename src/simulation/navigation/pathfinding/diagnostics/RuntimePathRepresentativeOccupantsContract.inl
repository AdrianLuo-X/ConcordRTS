#include "simulation/navigation/pathfinding/RuntimePathRepresentativeOccupants.h"

#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

bool runRuntimePathRepresentativeOccupantsPrimitiveContract(std::string& error) {
    const auto fail=[&](const std::string& why) {
        error="path representative occupant lookup: "+why; return false;
    };
    using Lookup=RuntimePathRepresentativeOccupants;
    using Map=Lookup::Map;
    std::size_t checks=0;
    Map occupants;
    for (int y=0;y<512;y+=7) for (int x=0;x<512;x+=13)
        occupants.emplace(y*1000+x,(x+y)%3==0 ? -1 : (x+y)%3==1 ? 0 : x-y);
    for (const auto& [key,value]:std::array<std::pair<int,int>,13>{{
            {0,0},{1000,-1},{511511,std::numeric_limits<int>::min()},
            {4000,std::numeric_limits<int>::max()},{512,-999},{999,-2},
            {1512,-3},{511999,-4},{512000,-5},{513000,-6},
            {-1,-7},{std::numeric_limits<int>::min(),0},{std::numeric_limits<int>::max(),-1}}})
        occupants.insert_or_assign(key,value);
    if (occupants.emplace(1000,123).second || occupants.at(1000)!=-1)
        return fail("duplicate integer-key insertion did not preserve first representative");
    const Map original=occupants;
    const auto buckets=occupants.bucket_count();
    const auto verify=[&](const Lookup& lookup,int key,const Map& source) {
        const auto found=source.find(key);
        const int* expected=found==source.end() ? nullptr : &found->second;
        const int* actual=lookup.find(key);
        ++checks;
        return actual==expected && (!actual || *actual==found->second);
    };
    {
        Lookup scalar(occupants,Lookup::minimumDenseQueries,true);
        Lookup dense(occupants,Lookup::minimumDenseQueries,false);
        if (scalar.usingDense() || scalar.denseBytes()!=0 || !scalar.reference()
            || !dense.usingDense() || dense.reference()
            || dense.denseBytes()!=Lookup::maximumDenseBytes
            || dense.denseBytes()>2U*1024U*1024U)
            return fail("reference/dense materialization or exact bounded storage");
        for (int y=0;y<512;++y) for (int x=0;x<512;++x) {
            const int key=y*1000+x;
            if (!verify(scalar,key,occupants) || !verify(dense,key,occupants))
                return fail("canonical complete-domain pointer/presence/value mismatch key="+std::to_string(key));
        }
        for (int key : {-1,-1000,std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),
                512,999,1512,511999,512000,513000,1000000,511512,511511,511000,1000,0})
            if (!verify(scalar,key,occupants) || !verify(dense,key,occupants))
                return fail("negative/extreme/noncanonical/edge original map fallback key="+std::to_string(key));
        const auto legacyKey=[](int x,int y){return x<0 || y<0 ? -1 : y*1000+x;};
        if (legacyKey(1000,0)!=legacyKey(0,1)
            || dense.find(legacyKey(1000,0))!=dense.find(legacyKey(0,1))
            || !dense.find(1000) || *dense.find(1000)!=-1
            || !dense.find(0) || *dense.find(0)!=0
            || !dense.find(511511) || *dense.find(511511)!=std::numeric_limits<int>::min()
            || dense.find(1001)!=nullptr)
            return fail("raw alias/negative-valued or zero-valued representative presence");
    }
    if (occupants!=original || occupants.bucket_count()!=buckets)
        return fail("helper mutated source values/order-independent content or bucket layout");
    for (std::size_t queries : {std::size_t{0},std::size_t{1},Lookup::minimumDenseQueries-1U}) {
        Lookup fewQueries(occupants,queries,false);
        if (fewQueries.usingDense() || fewQueries.denseBytes()!=0 || !verify(fewQueries,1000,occupants))
            return fail("small-map/few-query no-allocation original fallback");
    }
    {
        Map empty;
        Lookup emptyLookup(empty,std::numeric_limits<std::size_t>::max(),false);
        Map outside{{-1,-1},{512,-2},{512000,-3},{std::numeric_limits<int>::max(),-4}};
        Lookup outsideLookup(outside,std::numeric_limits<std::size_t>::max(),false);
        Lookup bounded(occupants,std::numeric_limits<std::size_t>::max(),false);
        if (emptyLookup.usingDense() || emptyLookup.denseBytes()!=0 || emptyLookup.find(0)
            || outsideLookup.usingDense() || outsideLookup.denseBytes()!=0
            || !verify(outsideLookup,512,outside) || !verify(outsideLookup,0,outside)
            || bounded.denseBytes()!=Lookup::maximumDenseBytes)
            return fail("empty/foreign-only huge-query hints or hard allocation bound");
    }
    {
        Map foreign=occupants; foreign[1000]=77;
        Lookup local(occupants,Lookup::minimumDenseQueries,false);
        Lookup other(foreign,Lookup::minimumDenseQueries,false);
        if (!verify(local,1000,occupants) || !verify(other,1000,foreign)
            || local.find(1000)==other.find(1000) || *other.find(1000)!=77)
            return fail("independently-owned map pointer/value identities were mixed");
    }
    // The input may change only after the previous lexical helper is destroyed.
    // A fresh helper must observe a reload/rehash and changed representatives.
    Map reloaded{{1000,-1}};
    { Lookup previous(reloaded,Lookup::minimumDenseQueries,false);
      if (!verify(previous,1000,reloaded)) return fail("reload initial pointer"); }
    reloaded.clear(); reloaded.reserve(32768); reloaded.emplace(1000,19); reloaded.emplace(512,-8);
    { Lookup fresh(reloaded,Lookup::minimumDenseQueries,false);
      if (!verify(fresh,1000,reloaded) || !verify(fresh,512,reloaded) || *fresh.find(1000)!=19)
          return fail("fresh lookup reused an expired map node after reload"); }
    const int previousOverride=runtimePathSnapshotOccupancyQueryReferenceOverride;
    {
        RuntimePathSnapshotOccupancyQueryReferenceScope outer(true);
        Lookup old(occupants,Lookup::minimumDenseQueries);
        if (!runtimePathSnapshotOccupancyQueryReference() || old.usingDense() || !old.reference())
            return fail("default constructor did not respect independent reference override");
        {
            RuntimePathSnapshotOccupancyQueryReferenceScope inner(false);
            Lookup optimized(occupants,Lookup::minimumDenseQueries);
            if (runtimePathSnapshotOccupancyQueryReference() || !optimized.usingDense() || optimized.reference())
                return fail("nested optimized override or original flag independence");
        }
        if (!runtimePathSnapshotOccupancyQueryReference()) return fail("nested reference restoration");
    }
    if (runtimePathSnapshotOccupancyQueryReferenceOverride!=previousOverride
        || occupants!=original || occupants.bucket_count()!=buckets || checks<524288)
        return fail("reference restoration/full domain coverage/source immutability");
    std::cout << "Path representative occupant query primitives: exactPointers=" << checks
        << " full512Domain=yes bytes=" << Lookup::maximumDenseBytes
        << " scalar/dense/legacyAlias/negativeIDs/outside/empty/lifetime/reference=yes pass\n";
    error.clear(); return true;
}
