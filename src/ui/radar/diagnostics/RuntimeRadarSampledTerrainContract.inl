// Include after the actual radar drawer and its native pixel-store oracle.
// Actual full-DIB pairs use runRuntimeRadarPixelStoreActualContract(...,true)
// with only this mask reference changed; its independent producer loops,
// resource/cache/error snapshots and source readbacks remain in force.
static bool runRuntimeRadarSampledTerrainPrimitiveContract(std::string& error) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar sampled terrain primitive: ") + detail;
        return false;
    };
    std::uint64_t checks = 0;
    RuntimeRadarSampledTerrainKeys keys;
    if (!keys.begin(256,256,1.0,1.0,false)) return fail("inclusive 65536 pixel bound");
    std::unordered_set<int> original;
    for (int y = 0; y < 512; y += 8) for (int x = 0; x < 512; x += 8) {
        const int key = runtimeCellKey(x,y);
        if (!keys.add(key) || !keys.add(key)) return fail("dense insertion");
        original.insert(key);
    }
    for (int key : {-1,-1000,std::numeric_limits<int>::min(),512,1000+512,
        512000,1000999,std::numeric_limits<int>::max()}) {
        if (!keys.add(key) || !keys.add(key)) return fail("fallback signed insertion");
        original.insert(key);
    }
    for (int y = 0; y < 512; y += 7) for (int x = 0; x < 512; x += 7) {
        const int key=runtimeCellKey(x,y); ++checks;
        if (keys.contains(key) != original.contains(key)) return fail("dense membership differs from original key set");
    }
    for (int key : original) { ++checks; if (!keys.contains(key)) return fail("fallback membership missing"); }
    if (keys.size()!=original.size()) return fail("duplicate insertion changed unique count");
    // Pass actual legacy keys. Different noncanonical raw coordinates can
    // alias a fallback key; all negative raw coordinates share the key -1.
    if (runtimeCellKey(1512,0) != runtimeCellKey(512,1)
        || !keys.contains(runtimeCellKey(1512,0))
        || runtimeCellKey(-17,100) != runtimeCellKey(100,-17)
        || !keys.contains(runtimeCellKey(-17,100))) return fail("raw aliases normalized inconsistently");
    const std::array<std::pair<double,double>,8> raw{{
        {0,0},{511,511},{512,512},{1512,0},{-1,0},{0,-1},
        {static_cast<double>(std::numeric_limits<int>::max()),0},{647,2147483}}};
    for (const auto [x,y] : raw) {
        int actual=99; ++checks;
        if (!RuntimeRadarSampledTerrainKeys::exactLegacyKey(x,y,actual)
            || actual!=runtimeCellKey(static_cast<int>(x),static_cast<int>(y)))
            return fail("complete signed key conversion changed original arithmetic");
    }
    for (const auto [x,y] : std::array<std::pair<double,double>,6>{{
        {648,2147483},{0,2147484},{1e30,0},{0,-1e30},
        {std::numeric_limits<double>::quiet_NaN(),0},{0,std::numeric_limits<double>::infinity()}}}) {
        int untouched=123; ++checks;
        if (RuntimeRadarSampledTerrainKeys::exactLegacyKey(x,y,untouched) || untouched!=123)
            return fail("unsupported conversion did not refuse without an output write");
    }
    for (int variant=0; variant<9; ++variant) {
        std::int64_t w=221,h=147; double sx=1.0,sy=1.0;
        switch(variant) {
        case 0:w=0;break; case 1:h=-1;break; case 2:w=257;h=256;break;
        case 3:w=std::numeric_limits<std::int64_t>::max();break;
        case 4:sx=-0.0;break; case 5:sy=-1.0;break;
        case 6:sx=std::numeric_limits<double>::quiet_NaN();break;
        case 7:sy=std::numeric_limits<double>::infinity();break;
        case 8:w=512;h=512;break;
        }
        ++checks;
        if(keys.begin(w,h,sx,sy,false) || keys.active() || keys.size()
            || !keys.contains(0) || !keys.contains(-1) || !keys.contains(std::numeric_limits<int>::max()))
            return fail("unsupported raster failed to restore complete old-loop membership");
    }
    if(!keys.begin(65536,1,1.0,1.0,false) || !keys.add(17))
        return fail("exact inclusive narrow raster bound");
    keys.disable();
    if(keys.active() || !keys.contains(17) || !keys.contains(19)) return fail("explicit fallback");
    const bool saved=runtimeRadarSampledTerrainReference();
    {
        RuntimeRadarSampledTerrainReferenceScope outer(true);
        if(keys.begin(1,1,1.0,1.0) || !keys.contains(171717)) return fail("independent old-path reference");
        {
            RuntimeRadarSampledTerrainReferenceScope inner(false);
            if(!keys.begin(1,1,1.0,1.0) || !keys.add(11) || keys.contains(12))
                return fail("nested override positive membership");
        }
        if(!runtimeRadarSampledTerrainReference()) return fail("nested reference restore");
    }
    if(runtimeRadarSampledTerrainReference()!=saved) return fail("outer reference restore");
    std::cout << "Radar sampled terrain primitive: checks=" << checks
        << " legacySignedAliasKeys=exact uniqueMembership=exact cap65536=exact"
        << " NaNInfinityOverflowAndReferenceFallback=exact nestedRestore=exact\n";
    return true;
}
