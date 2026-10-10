// Include after RuntimePlannerPassabilityContractDetail::make/capture.
bool runRuntimePlannerKernelPrimitiveContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    const auto fail=[&](const std::string& why) {
        error="planner connectivity kernel: "+why; return false;
    };
    RuntimeComponentKernelReferenceScope enableKernel(false);
    auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
    RuntimeSnapshotActiveStateScope active(state);
    if (!make(state,config,0,0,error)) return false;
    const auto occupied=buildRuntimeOccupancyLookup(state);
    std::size_t edgeChecks=0,landChecks=0;
    Authority before,after;
    if (!capture(state,before,error)) return false;
    for (int id : {1,2,3,4,5,6}) {
        const auto* mover=findObjectById(state,id);
        if (!mover) return fail("missing synthetic ground/infantry/naval/amphibious/air/unknown actor");
        RuntimePlannerConnectivityKernel oldKernel(state,*mover,state.terrainByCell,occupied,true);
        RuntimePlannerConnectivityKernel newKernel(state,*mover,state.terrainByCell,occupied,false);
        if (oldKernel.precomputedMode() || !oldKernel.usesOriginalCost()
            || newKernel.precomputedMode()!=runtimeObjectMovementMode(state,*mover)
            || newKernel.usesOriginalCost()!=(runtimeObjectMovementMode(state,*mover)==PathMovementMode::Air))
            return fail("original cost mode laziness or actual one-time movement classification");
        for (int y=-1;y<=12;++y) for (int x=-1;x<=12;++x) {
            const RuntimeCellPoint current{x,y};
            for (const auto& next:runtimeNeighborsOf(current)) {
                const auto expected=runtimeCellTraversalCost(state,state.terrainByCell,occupied,current,next,*mover).has_value();
                ++edgeChecks;
                if (oldKernel.connects(current,next)!=expected || newKernel.connects(current,next)!=expected)
                    return fail("actual directed edge scalar/reference/fast result id="+std::to_string(id)
                        +" from="+std::to_string(x)+","+std::to_string(y));
            }
            for (const auto next : {current,RuntimeCellPoint{x+2,y},RuntimeCellPoint{x,y-2}}) {
                ++edgeChecks;
                const auto expected=runtimeCellTraversalCost(state,state.terrainByCell,occupied,current,next,*mover).has_value();
                if (oldKernel.connects(current,next)!=expected || newKernel.connects(current,next)!=expected)
                    return fail("same-cell/nonadjacent old scalar eligibility");
            }
        }
    }
    const auto* air=findObjectById(state,5);
    RuntimePlannerConnectivityKernel airKernel(state,*air,state.terrainByCell,occupied,false);
    if (!runtimeAirTransitionIsClear(state,air->instanceId,{11,11},{12,11})
        || state.terrainByCell.contains(runtimeCellKey(12,11))
        || !airKernel.usesOriginalCost() || airKernel.connects({11,11},{12,11}))
        return fail("real Air clear transition must still reject original missing-terrain cost tail");
    if (!capture(state,after,error) || !(before==after)) return fail("edge kernels mutated authority/full save");

    const auto* car=findObjectById(state,1);
    if (state.rules.findLandType("Clear")) return fail("missing-land fixture accidentally authored Clear");
    const auto verifyLand=[&](RuntimeWindowState& owner,const WorldObjectInstance& mover,
        RuntimeTerrainLandType land) {
        RuntimeTerrainInfo info; info.authoredLandType=land;
        // All other fields are deliberately irrelevant to the original query.
        info.water=true; info.bridge=true; info.passable=false; info.cost=-7;
        const auto expected=runtimeAuthoredLandTypeAllowsMoverUncached(owner,info,mover);
        ++landChecks;
        return runtimeAuthoredLandTypeAllowsMover(owner,info,mover)==expected;
    };
    {
        RuntimePlannerLandPermissionScope scope(state,*car);
        for (int n=-1;n<=10;++n) {
            if (!verifyLand(state,*car,static_cast<RuntimeTerrainLandType>(n))
                || !verifyLand(state,*car,static_cast<RuntimeTerrainLandType>(n)))
                return fail("unknown/missing original land permits differ");
        }
        for (int n : {std::numeric_limits<int>::min(),-2,11,std::numeric_limits<int>::max()})
            if (!verifyLand(state,*car,static_cast<RuntimeTerrainLandType>(n)))
                return fail("invalid enum original fallback differs");
        if (scope.materializations()!=12 || scope.hits()!=12)
            return fail("missing-land exact enum materializations/invalid-enum fallback counts");
    }
    // Reload only outside scopes; use real INI percent parsing and original
    // source selection across all supported movement classifications.
    std::string authored(rulesText);
    for (int n=0;n<=10;++n) {
        const char* name=runtimeTerrainLandTypeRulesId(static_cast<RuntimeTerrainLandType>(n));
        if (!name) return fail("authored enum-to-rule oracle missing");
        authored+=std::string("[")+name+"]\nTrack="+(n%3==0 ? "0%" : n%3==1 ? "-25%" : "150%")
            +"\nFoot="+(n%2==0 ? "50%" : "0%")
            +"\nWheel=20%\nHover=100%\nFloat=0%\nFloatBeach=-10%\nAmphibious=30%\n";
    }
    IniFile authoredRules;
    if (!authoredRules.loadFromString(authored) || !state.rules.loadFromIni(authoredRules))
        return fail("real authored-percent reload failed");
    if (!state.rules.findLandType("Clear") || state.rules.findLandType("Clear")->track!=0
        || state.rules.findLandType("Road")->track>=0 || state.rules.findLandType("Water")->track<=100)
        return fail("custom zero/negative/greater-than-100 percent readback");
    if (!capture(state,before,error)) return false;
    for (int id : {1,2,3,4,5,6}) {
        const auto* mover=findObjectById(state,id);
        for (bool reference : {true,false}) {
            RuntimeComponentKernelReferenceScope branch(reference);
            RuntimePlannerLandPermissionScope scope(state,*mover);
            for (int n=-1;n<=10;++n)
                if (!verifyLand(state,*mover,static_cast<RuntimeTerrainLandType>(n))
                    || !verifyLand(state,*mover,static_cast<RuntimeTerrainLandType>(n)))
                    return fail("authored original percentage permit differs id="+std::to_string(id));
            if (reference ? (scope.materializations()!=0 || scope.hits()!=0)
                : (scope.materializations()!=12 || scope.hits()!=12))
                return fail("authored fast/reference primitive did not exercise original/memo branches");
        }
    }
    if (!capture(state,after,error) || !(before==after)) return fail("land permissions mutated authority/full save");
    RuntimeTerrainInfo clear; clear.authoredLandType=RuntimeTerrainLandType::Clear;
    {
        RuntimePlannerLandPermissionScope outer(state,*car);
        if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.materializations()!=1)
            return fail("actual CAR Track=0 cold false materialization");
        auto moverCopy=*car; moverCopy.type="HOVER";
        if (!runtimeAuthoredLandTypeAllowsMover(state,clear,moverCopy)
            || outer.materializations()!=1 || outer.hits()!=0)
            return fail("foreign mover address borrowed CAR false result instead of HOVER true");
        auto foreign=std::make_unique<RuntimeWindowState>(); foreign->rules=state.rules;
        auto* land=const_cast<LandType*>(foreign->rules.findLandType("Clear"));
        land->track=200;
        if (!runtimeAuthoredLandTypeAllowsMover(*foreign,clear,*car)
            || outer.materializations()!=1 || outer.hits()!=0)
            return fail("foreign state address borrowed cached CAR false result");
        {
            RuntimePlannerLandPermissionScope disabled(state,*car,false);
            if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car)
                || disabled.materializations()!=0 || disabled.hits()!=0 || outer.hits()!=0)
                return fail("disabled nested scope borrowed outer permission");
        }
        {
            RuntimePlannerLandPermissionScope inner(state,moverCopy);
            if (!runtimeAuthoredLandTypeAllowsMover(state,clear,moverCopy)
                || !runtimeAuthoredLandTypeAllowsMover(state,clear,moverCopy)
                || inner.materializations()!=1 || inner.hits()!=1 || outer.hits()!=0)
                return fail("enabled nested mover did not keep an independent permission");
        }
        if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.hits()!=1)
            return fail("nested scope failed to restore original outer false result");
        {
            RuntimeComponentKernelReferenceScope reference(true);
            if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.hits()!=1)
                return fail("reference override reused active permission");
        }
        outer.invalidate();
        // Same-address rule edit and same-address mover edits are permitted
        // only after invalidate. Verify each real true/false transition.
        auto* liveLand=const_cast<LandType*>(state.rules.findLandType("Clear"));
        liveLand->track=50;
        if (!runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.hits()!=1)
            return fail("invalidated same-address rule edit reused old false permit");
        liveLand->track=0;
        if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.hits()!=1)
            return fail("invalidated restored rule edit reused old true permit");
        auto* mutableMover=findMutableObjectById(state,1);
        mutableMover->type="HOVER";
        if (!runtimeAuthoredLandTypeAllowsMover(state,clear,*mutableMover) || outer.hits()!=1)
            return fail("invalidated same-address mover type did not read HOVER percent");
        mutableMover->type="CAR";
        ++state.tick;
        if (runtimeAuthoredLandTypeAllowsMover(state,clear,*car) || outer.hits()!=1)
            return fail("invalidated changed tick reused permit");
    }
    if (runtimeActivePlannerLandPermission)
        return fail("land scope leaked past lexical lifetime");
    // A new rule load and a fresh scope must witness a changed real permit.
    std::string reloaded(rulesText); reloaded+="[Clear]\nTrack=75%\n";
    IniFile freshRules;
    if (!freshRules.loadFromString(reloaded) || !state.rules.loadFromIni(freshRules))
        return fail("fresh rules reload failed");
    {
        RuntimePlannerLandPermissionScope fresh(state,*car);
        if (!runtimeAuthoredLandTypeAllowsMover(state,clear,*car)
            || !runtimeAuthoredLandTypeAllowsMover(state,clear,*car)
            || fresh.materializations()!=1 || fresh.hits()!=1)
            return fail("fresh scope/rule reload reused old false permit");
    }
    // Explicit kernel invalidation also stops using a cached mover mode. The
    // original fallback is read after the mover's type changes in place.
    WorldObjectInstance probe=*car;
    RuntimePlannerConnectivityKernel changing(state,probe,state.terrainByCell,occupied,false);
    if (changing.precomputedMode()!=PathMovementMode::Ground) return fail("invalidate kernel initial ground mode");
    changing.invalidate(); probe.type="AIR"; probe.airborne=true;
    if (!changing.usesOriginalCost()
        || changing.connects({11,11},{12,11})!=runtimeCellTraversalCost(state,state.terrainByCell,occupied,{11,11},{12,11},probe).has_value())
        return fail("invalidated kernel mode switch bypassed original Air terrain tail");
    const int previous=runtimeComponentKernelReferenceOverride;
    {
        RuntimeComponentKernelReferenceScope outer(true);
        if (!runtimeComponentKernelReference()) return fail("kernel outer reference override");
        {
            RuntimeComponentKernelReferenceScope inner(false);
            if (runtimeComponentKernelReference()) return fail("kernel nested false override");
        }
        if (!runtimeComponentKernelReference()) return fail("kernel nested override restoration");
    }
    if (runtimeComponentKernelReferenceOverride!=previous)
        return fail("kernel outer override restoration");
    if (edgeChecks!=12936 || landChecks==0) return fail("kernel complete directed-edge/land matrix did not execute");
    std::cout << "Planner connectivity kernel primitives: exactDirectedEdges=" << edgeChecks
        << " actualLandQueries=" << landChecks << " AirMissingTerrainFallback=yes nativeRules=yes pass\n";
    error.clear(); return true;
}
