// Include after RuntimeChronoMoveTerrainContract.inl. Actual stock rules,
// production finders/commands and the unchanged native mass-world fixture are
// the oracle. The old reference is a bug witness, never a performance reference.
namespace RuntimeManualFormationMappedContractDetail {
using Authority=RuntimePlannerPassabilityContractDetail::Authority;
using FullState=RuntimeChronoMoveTerrainContractDetail::FullState;
struct Fixture { std::unique_ptr<RuntimeWindowState> state; int actor=-1; };
Fixture flat(const RuntimeWindowState& stock,const char* type,RuntimeBuildKind kind,
    const char* owner="Americans",bool ore=false) {
    Fixture out; out.state=std::make_unique<RuntimeWindowState>(); auto& s=*out.state;
    s.config=stock.config; s.config.fixedTickHz=30; s.config.playerSlots.clear();
    // Keep the genuinely loaded stock map identity. Synthetic world contents
    // are serialized in full; a fictitious disk path would change map-rule I/O.
    s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=false;
    s.config.crates=s.config.superweapons=s.config.noCooldown=false;
    s.report=stock.report; s.rules=stock.rules; s.art=stock.art; s.theater=stock.theater;
    s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=true;
    s.economy.owner=owner; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
    s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
    s.runtimeSuppressPlayerAudioFeedback=true;
    RuntimeHumanHouseAuthority house; house.owner=owner;
    house.canonicalOwner=toLowerAsciiLocal(trimAsciiLocal(owner)); house.localFacade=true;
    s.humanHouseAuthorities.push_back(std::move(house));
    for(int y=1;y<=64;++y) for(int x=1;x<=64;++x) {
        TerrainCell c; c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=c.subTileIndex=0;
        if(ore && x==40 && y==40) setRuntimeResourceTerrainBails(c,"TIB",12);
        s.world.accessTerrainCells().push_back(c);
    }
    if(auto* actor=addRuntimeObjectInstance(s,kind,type,{12,12})) out.actor=actor->instanceId;
    initializeRuntimeNavigation(s);
    for(auto& [key,info]:s.terrainInfoByCell) {
        (void)key; info={}; info.kind=RuntimeTerrainKind::Land;
        info.authoredLandType=info.movementLandType=RuntimeTerrainLandType::Clear;
    }
    rebuildRuntimeEconomyPower(s); updateRuntimeVisionState(s);
    return out;
}
bool unchanged(RuntimeWindowState& state,const Authority& before,std::string& error) {
    Authority after;
    return RuntimePlannerPassabilityContractDetail::capture(state,after,error) && before==after;
}
std::unique_ptr<RuntimeWindowState> native(const RuntimeWindowState& stock,int count,
    int extent,std::vector<int>& ids,std::string& error) {
    auto out=std::make_unique<RuntimeWindowState>(); auto& s=*out;
    s.config=stock.config;s.config.fixedTickHz=30;
    s.report=stock.report;s.rules=stock.rules;s.art=stock.art;s.theater=stock.theater;
    s.rulesLoaded=stock.rulesLoaded;s.artLoaded=stock.artLoaded;s.theaterLoaded=stock.theaterLoaded;
    s.visionEnabled=true;s.runtimeSoundsEnabled=false;s.runtimePreferencePersistenceEnabled=false;
    RuntimeSnapshotActiveStateScope active(s);
    if(!prepareRuntimeMassMovementWorld(s,count,ids,error,false,true,false,extent,extent,true,true)) return {};
    // The real native fixture intentionally clears defaultMapPath. Restore its
    // own producer-reported identity before the unchanged complete-save oracle.
    if(!s.report.map.loaded || s.report.map.path!="MassMovement-v1.map") {
        error="manual formation Mapped: native producer map identity readback"; return {};
    }
    s.config.defaultMapPath=s.report.map.path;
    // Finish exactly the live driver's 60 original warmup ticks. Full saves
    // belong to completed ticks; callers enter the tick61 input boundary next.
    // No extra scouts, exploration, radius changes or private chrono recovery.
    for(int tick=1;tick<=60;++tick) { ++s.tick;advanceRuntimeSimulation(s); }
    return out;
}
} // namespace RuntimeManualFormationMappedContractDetail

bool runRuntimeManualFormationMappedContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeManualFormationMappedContractDetail;
    const auto fail=[&](const std::string& why){error="manual formation Mapped: "+why;return false;};
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,
        &stock->art,&stock->rules,&stock->mapFacts);
    stock->rulesLoaded=stock->report.rulesLoaded;stock->artLoaded=stock->report.artLoaded;
    stock->theaterLoaded=stock->report.theaterLoaded;
    if(!stock->report.success || !stock->rulesLoaded) return fail("stock resources unavailable");
    const auto* chrono=stock->rules.findUnitType("InfantryType","CLEG");
    if(!chrono || chrono->moveToShroud || !chrono->teleporter) return fail("stock CLEG rule readback");
    std::size_t queryChecks=0,commandChecks=0,saveChecks=0;
    const int previousOverride=runtimeManualFormationMappedReferenceOverride;
    {
        RuntimeManualFormationMappedReferenceScope outer(false);
        if(runtimeManualFormationMappedReference()) return fail("outer fixed override");
        { RuntimeManualFormationMappedReferenceScope inner(true);
            if(!runtimeManualFormationMappedReference()) return fail("nested old override"); }
        if(runtimeManualFormationMappedReference()) return fail("nested override did not restore fixed route");
    }
    if(runtimeManualFormationMappedReferenceOverride!=previousOverride) return fail("TLS override leaked");
    // Actual original searches pick the unMapped desired cell; the opt-in
    // manual predicate picks the independently explored legal alternative.
    {
        auto f=flat(*stock,"CLEG",RuntimeBuildKind::Infantry);
        if(f.actor<0) return fail("CLEG flat actor");auto& s=*f.state;
        RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint illegal{40,40},legal{41,40};
        if(!s.vision.hasObserver("Americans") || s.vision.isCellExploredBy("Americans",40,40))
            return fail("unMapped fixture readback");
        s.vision.restoreExploredCellIndices("Americans",{legal.y*512+legal.x});
        const auto* mover=findObjectById(s,f.actor);
        const auto occupied=buildRuntimeOccupancyLookup(s);
        RuntimeManualFormationMappedFilter filter(s,*mover,nullptr,true);
        if(!filter.restriction() || filter.allows(illegal) || !filter.allows(legal)) return fail("actual owner Mapped predicate");
        Authority before;if(!RuntimePlannerPassabilityContractDetail::capture(s,before,error)) return false;
        const auto best=[&](const RuntimeManualFormationMappedFilter* selected,const RuntimeCellPoint& target) {
            return findBestRuntimeFormationTargetCellWithOccupancy(s,*mover,target,-1,nullptr,
                s.terrainByCell,occupied,nullptr,nullptr,-1,selected);
        };
        const auto nearest=[&](const RuntimeManualFormationMappedFilter* selected,const RuntimeCellPoint& target,
            const std::unordered_map<int,int>* components=nullptr) {
            return findNearestRuntimePassableCellWithOccupancy(s,*mover,target,-1,nullptr,
                s.terrainByCell,occupied,nullptr,components,components?7:-1,selected);
        };
        if(best(nullptr,illegal)!=std::optional<RuntimeCellPoint>(illegal)
            || nearest(nullptr,illegal)!=std::optional<RuntimeCellPoint>(illegal)
            || best(filter.restriction(),illegal)!=std::optional<RuntimeCellPoint>(legal)
            || nearest(filter.restriction(),illegal)!=std::optional<RuntimeCellPoint>(legal))
            return fail("best/nearest old illegal vs manual legal");
        queryChecks+=4;
        // Nullable automatic callers and already legal destinations keep the
        // exact old winner/score. The score-pruning implementation is also
        // compared with the complete scalar search under the manual filter.
        if(best(nullptr,legal)!=best(filter.restriction(),legal)
            || nearest(nullptr,legal)!=nearest(filter.restriction(),legal)
            || findBestRuntimeFormationTargetCellWithOccupancyImpl(s,*mover,illegal,-1,nullptr,
                s.terrainByCell,occupied,nullptr,nullptr,-1,false,filter.restriction())
                !=best(filter.restriction(),illegal)) return fail("legal gold/pruned scalar mismatch");
        queryChecks+=3;
        // Force the original global fallback (both candidates outside radius24).
        // The oracle walks the SAME unordered container and retains first ties.
        std::unordered_map<int,int> components;
        for(RuntimeCellPoint cell:{RuntimeCellPoint{39,40},RuntimeCellPoint{40,39}}) {
            components.emplace(runtimeCellKey(cell.x,cell.y),7);
            s.vision.restoreExploredCellIndices("Americans",{cell.y*512+cell.x});
        }
        const RuntimeCellPoint farDesired{90,90};
        const auto expected=nearest(nullptr,farDesired,&components),actual=nearest(filter.restriction(),farDesired,&components);
        if(!expected || expected!=actual) return fail("global fallback unordered strict tie changed");
        queryChecks+=2;
        s.vision.resetShroudArea("Americans",expected->x,expected->y,0);
        RuntimeManualFormationMappedFilter mixed(s,*mover,nullptr,true);
        const auto filtered=nearest(mixed.restriction(),farDesired,&components);
        if(!filtered || filtered==expected || !mixed.allows(*filtered)
            || nearest(nullptr,farDesired,&components)!=expected)
            return fail("global fallback retained old illegal winner instead of legal candidate");
        queryChecks+=2;
        // Visibility is a rendering option, not owner Mapped. No invented
        // NoFog API is used: exercise the actual SeeHiddenObjects switch.
        s.config.seeHiddenObjects=true;
        if(filter.allows(illegal) || runtimeObjectAllowsMoveToCell(s,*mover,illegal)) return fail("SeeHiddenObjects bypassed Mapped");
        s.config.seeHiddenObjects=false;
        if(filter.allows(illegal) || runtimeObjectAllowsMoveToCell(s,*mover,illegal)) return fail("hidden drawing option changed Mapped");
        s.config.seeHiddenObjects=stock->config.seeHiddenObjects;
        // The exploration edits are part of this fixture, so reset the baseline
        // before checking that the actual finder calls themselves are pure.
        if(!RuntimePlannerPassabilityContractDetail::capture(s,before,error)) return false;
        (void)best(filter.restriction(),illegal);(void)nearest(filter.restriction(),illegal);
        if(!unchanged(s,before,error)) return fail("read-only finder changed full saved authority");
        ++saveChecks;queryChecks+=4;
        s.vision.resetShroudArea("Americans",legal.x,legal.y,0);
        RuntimeManualFormationMappedFilter reshroud(s,*mover,nullptr,true);
        if(reshroud.allows(legal) || runtimeObjectAllowsMoveToCell(s,*mover,legal)) return fail("re-shroud did not remove Mapped");
        ++queryChecks;
        // Rebuild only another observer. A foreign owner's explored cell must
        // never satisfy the mover's owner rule; missing observer denies all.
        s.vision.reset();s.vision.update(s.world,&s.rules,{{"Russians",{}}},s.tick);
        s.vision.restoreExploredCellIndices("Russians",{illegal.y*512+illegal.x});
        RuntimeManualFormationMappedFilter absent(s,*mover,nullptr,true);
        if(s.vision.hasObserver("Americans") || !s.vision.isCellExploredBy("Russians",40,40)
            || absent.allows(illegal) || best(absent.restriction(),illegal).has_value()
            || nearest(absent.restriction(),illegal).has_value()) return fail("missing/foreign owner Mapped bypass");
        queryChecks+=3;
        auto* editable=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*mover));editable->moveToShroud=true;
        RuntimeManualFormationMappedFilter permission(s,*mover,nullptr,true);
        if(permission.restriction()!=nullptr || !permission.allows(illegal)
            || best(permission.restriction(),illegal)!=best(nullptr,illegal)) return fail("MoveToShroud=yes old gold");
        editable->moveToShroud=false;s.visionEnabled=false;
        RuntimeManualFormationMappedFilter disabled(s,*mover,nullptr,true);
        if(disabled.restriction()!=nullptr || !disabled.allows(illegal)) return fail("vision disabled old rule");
        s.visionEnabled=true;
        RuntimeManualFormationMappedFilter reference(s,*mover,nullptr,false);
        WorldObjectInstance unknown=*mover;unknown.type="MAPPED_CONTRACT_MISSING";
        RuntimeManualFormationMappedFilter missing(s,unknown,nullptr,true);
        if(reference.restriction()!=nullptr || missing.restriction()!=nullptr) return fail("reference/unknown rule fallback");
        queryChecks+=4;
    }
    // Preserve the original click gate under both manual dispatch choices.
    // The presentation flag does not permit an actually unMapped click.
    for(bool showHidden:{false,true}) {
        Authority captures[2];
        for(int branch=0;branch<2;++branch) {
            auto f=flat(*stock,"CLEG",RuntimeBuildKind::Infantry);if(f.actor<0) return fail("click gate actor");
            auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
            s.config.seeHiddenObjects=showHidden;const RuntimeCellPoint click{40,40};
            if(s.vision.isCellExploredBy("Americans",40,40)) return fail("click unMapped witness");
            RuntimeManualFormationMappedReferenceScope reference(branch==0);
            applySelection(s,{f.actor},false,false);issueRuntimeMoveCommand(s,click);
            if(s.lastCommand.acceptedCount!=0 || s.lastCommand.rejectedCount!=1
                || s.specialMechanisms.isChronoInTransit(f.actor) || s.behaviors.contains(f.actor)
                || runtimeCellPointForObject(*findObjectById(s,f.actor))!=RuntimeCellPoint{12,12})
                return fail("unMapped clicked location passed original gate");
            if(!RuntimePlannerPassabilityContractDetail::capture(s,captures[branch],error)) return false;
            ++commandChecks;
        }
        if(captures[0]!=captures[1]) return fail("invalid click old/new complete save mismatch");
        ++saveChecks;
    }
    // Stock naval mover with a private MoveToShroud override: the generic
    // candidate paths retain real water/land passability and owner Mapped.
    {
        auto f=flat(*stock,"DLPH",RuntimeBuildKind::Unit);if(f.actor<0) return fail("DLPH actor");
        auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        const auto* mover=findObjectById(s,f.actor);auto* unit=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*mover));
        if(!unit || !unit->naval || runtimeCellPointForObject(*mover)!=RuntimeCellPoint{12,12}) return fail("stock DLPH Naval/spawn readback");unit->moveToShroud=false;
        for(auto& [key,info]:s.terrainInfoByCell) {
            (void)key;info={};info.kind=RuntimeTerrainKind::Water;info.water=true;
            info.authoredLandType=info.movementLandType=RuntimeTerrainLandType::Water;
        }
        const RuntimeCellPoint desired{40,40},legal{41,40};
        s.vision.restoreExploredCellIndices(mover->owner,{legal.y*512+legal.x});
        RuntimeManualFormationMappedFilter filter(s,*mover,unit,true);const auto occupied=buildRuntimeOccupancyLookup(s);
        if(!filter.restriction() || filter.allows(desired) || !filter.allows(legal)) return fail("naval owner Mapped witness");
        const auto query=[&](bool nearest,const RuntimeManualFormationMappedFilter* selected) {
            return nearest?findNearestRuntimePassableCellWithOccupancy(s,*mover,desired,-1,nullptr,s.terrainByCell,occupied,
                nullptr,nullptr,-1,selected):findBestRuntimeFormationTargetCellWithOccupancy(s,*mover,desired,-1,nullptr,
                s.terrainByCell,occupied,nullptr,nullptr,-1,selected);
        };
        Authority before;if(!RuntimePlannerPassabilityContractDetail::capture(s,before,error)) return false;
        for(bool nearest:{false,true}) {
            if(query(nearest,nullptr)!=std::optional<RuntimeCellPoint>(desired)
                || query(nearest,filter.restriction())!=std::optional<RuntimeCellPoint>(legal))
                return fail("mod naval best/nearest actual passability and Mapped");queryChecks+=2;
        }
        if(!unchanged(s,before,error)) return fail("naval finder changed full saved authority");++saveChecks;
    }
    // Actual air special finder, using a stock air-capable vehicle with only
    // the authored MoveToShroud flag overridden in this private rules fixture.
    {
        auto f=flat(*stock,"SCHP",RuntimeBuildKind::Unit);if(f.actor<0) return fail("SCHP actor");
        auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        auto* mover=findMutableObjectById(s,f.actor);mover->airborne=true;
        auto* unit=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*mover));
        if(!unit) return fail("SCHP rules");unit->moveToShroud=false;
        const RuntimeCellPoint desired{40,40},legal{41,40};s.vision.restoreExploredCellIndices(mover->owner,{40*512+41});
        RuntimeManualFormationMappedFilter filter(s,*mover,unit,true);
        Authority before;if(!RuntimePlannerPassabilityContractDetail::capture(s,before,error)) return false;
        if(findRuntimeAirMoveTarget(s,*mover,desired)!=std::optional<RuntimeCellPoint>(desired)
            || findRuntimeAirMoveTarget(s,*mover,desired,nullptr,nullptr,filter.restriction())!=std::optional<RuntimeCellPoint>(legal)
            || findRuntimeAirMoveTarget(s,*mover,legal)!=findRuntimeAirMoveTarget(s,*mover,legal,nullptr,nullptr,filter.restriction()))
            return fail("mod air special landing filter/legal gold");
        if(!unchanged(s,before,error)) return fail("air finder changed full saved authority");
        queryChecks+=3;++saveChecks;
    }
    // The original slave placement helper already checks Mapped for EVERY
    // footprint cell through runtimeCellCanPlaceBuildingFast. Verify this
    // actual negative gate and unchanged legal stock anchor instead of making
    // an impossible same-owner "old unMapped placement" fixture.
    {
        auto f=flat(*stock,"SMIN",RuntimeBuildKind::Unit,"YuriCountry",true);
        if(f.actor<0) return fail("SMIN actor");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        const auto* mover=findObjectById(s,f.actor);
        auto* unit=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*mover));
        if(!unit || !runtimeUnitIsMobileSlaveMiner(*unit)) return fail("stock SMIN mobile rule");
        unit->moveToShroud=false;const RuntimeCellPoint ore{40,40};
        if(!runtimeCellHasHarvestableResource(s,ore)) return fail("actual TIB resource readback");
        RuntimeManualFormationMappedFilter none(s,*mover,unit,true);
        if(findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore)
            || findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore,none.restriction()))
            return fail("slave original full footprint Mapped gate bypass");
        s.vision.exploreArea(mover->owner,ore.x,ore.y,8);
        RuntimeManualFormationMappedFilter selected(s,*mover,unit,true);
        const auto old=findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore);
        if(!old || !selected.allows(*old)) return fail("slave actual legal placement/path/worker anchor witness");
        Authority before;if(!RuntimePlannerPassabilityContractDetail::capture(s,before,error)) return false;
        if(findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore,selected.restriction())!=old)
            return fail("slave legal old anchor gold changed");
        if(!unchanged(s,before,error)) return fail("slave finder changed full saved authority");
        s.vision.resetShroudArea(mover->owner,old->x,old->y,0);
        RuntimeManualFormationMappedFilter reshroud(s,*mover,unit,true);
        const auto automatic=findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore);
        const auto manual=findRuntimeSlaveMinerDeployCellNearOre(s,*mover,ore,reshroud.restriction());
        if(automatic!=manual || (manual && !reshroud.allows(*manual))) return fail("slave re-shrouded footprint legality");
        queryChecks+=5;++saveChecks;
    }
    // Native producer regression: EXACT original 256x256 mixed stock world,
    // radius8 scouts and tick61. Old reference must exhibit the real CLEG211
    // illegal landing, fixed production command must accept all256 legally.
    {
        std::vector<int> oldIds,newIds;auto old=native(*stock,256,256,oldIds,error);
        if(!old) return false;auto fixed=native(*stock,256,256,newIds,error);if(!fixed) return false;
        if(oldIds!=newIds || oldIds.size()!=256 || old->tick!=60 || fixed->tick!=60) return fail("native256 fixture IDs/completed warmup tick");
        FullState oldBefore,newBefore;
        if(!RuntimeChronoMoveTerrainContractDetail::capture(*old,oldBefore,error)
            || !RuntimeChronoMoveTerrainContractDetail::capture(*fixed,newBefore,error)) return false;
        if(oldBefore!=newBefore) return fail("native256 initial complete authority/save mismatch");++saveChecks;
        const auto beforeTerrain=RuntimeChronoMoveTerrainContractDetail::terrainState(*fixed);
        // Preserve the real ++tick -> beforeSimulation(command) -> simulation
        // order. Never fabricate subsystem clocks to save an unfinished tick.
        ++old->tick; ++fixed->tick;
        if(old->tick!=61 || fixed->tick!=61) return fail("native256 original command tick61");
        constexpr RuntimeCellPoint click{204,160};std::map<int,RuntimeCellPoint> oldTargets,newTargets;
        RuntimeMassMovementRejectSink rejects;
        {
            RuntimeSnapshotActiveStateScope active(*old);RuntimeManualFormationMappedReferenceScope reference(true);
            RuntimeMassMovementRejectScope sink(old.get(),old->tick,&RuntimeMassMovementRejectSink::receive,&rejects);
            applySelection(*old,oldIds,false,false);issueRuntimeMoveCommand(*old,click,&oldTargets);
        }
        bool witness=false;
        for(std::size_t i=0;i<rejects.count;++i) {const auto& r=rejects.records[i];
            witness=witness || (r.objectId==211 && std::string_view(r.type.bytes.data())=="CLEG"
                && std::string_view(r.reason.bytes.data())=="landing-owner-shroud"
                && r.clickX==204 && r.clickY==160 && r.clickMapped==1 && r.candidateMapped==0
                && r.moveToShroud==0 && r.teleporter==1 && r.chronoLocked==0);
        }
        if(!witness || old->lastCommand.acceptedCount!=255 || oldTargets.size()!=255)
            return fail("native256 old CLEG211 illegal landing witness: accepted="+std::to_string(old->lastCommand.acceptedCount));
        {
            RuntimeSnapshotActiveStateScope active(*fixed);RuntimeManualFormationMappedReferenceScope reference(false);
            if(!issueRuntimeMassMovementOrder(*fixed,newIds,click,newTargets,error,false)) return false;
        }
        if(fixed->lastCommand.acceptedCount!=256 || fixed->lastCommand.rejectedCount!=0 || newTargets.size()!=256)
            return fail("native256 fixed complete acceptance");
        for(const auto& [id,cell]:newTargets) {
            const auto* mover=findObjectById(*fixed,id);
            if(!mover || !runtimeObjectAllowsMoveToCell(*fixed,*mover,cell)) return fail("native256 fixed illegal owner landing");
        }
        if(beforeTerrain!=RuntimeChronoMoveTerrainContractDetail::terrainState(*fixed)) return fail("native256 command changed terrain/assets");
        for(auto* state:{old.get(),fixed.get()}) {
            RuntimeSnapshotActiveStateScope active(*state);
            (void)advanceRuntimeSimulation(*state);
            if(state->tick!=61) return fail("native256 completed command tick changed");
        }
        FullState oldAfter,newAfter;
        if(!RuntimeChronoMoveTerrainContractDetail::capture(*old,oldAfter,error)
            || !RuntimeChronoMoveTerrainContractDetail::capture(*fixed,newAfter,error)) return false;
        if(oldAfter.authority.serialized.empty() || newAfter.authority.serialized.empty()
            || oldAfter==newAfter) return fail("native256 fullsave intentional behavior change witness");
        commandChecks+=2;saveChecks+=2;
        std::cout<<"Manual formation native256: oldAccepted=255 oldIllegal=CLEG211 fixedAccepted=256 fixedAllLandingsLegal=yes fullsave=yes originalRadius8=yes tick=61\n";
    }
    // A previously legal stock native48 command must retain exact destinations,
    // complete saved state and every authoritative subsystem under old/new paths.
    {
        std::vector<int> oldIds,newIds;auto old=native(*stock,48,240,oldIds,error);
        if(!old) return false;auto fixed=native(*stock,48,240,newIds,error);if(!fixed) return false;
        if(oldIds!=newIds || old->tick!=60 || fixed->tick!=60) return fail("native48 IDs/completed warmup tick");
        std::map<int,RuntimeCellPoint> oldTargets,newTargets;const RuntimeCellPoint click{196,152};
        for(int branch=0;branch<2;++branch) {
            auto& s=branch?*fixed:*old;RuntimeSnapshotActiveStateScope active(s);
            RuntimeManualFormationMappedReferenceScope reference(branch==0);
            ++s.tick;
            if(s.tick!=61) return fail("native48 original command tick61");
            if(!issueRuntimeMassMovementOrder(s,branch?newIds:oldIds,click,branch?newTargets:oldTargets,error,false)) return false;
            (void)advanceRuntimeSimulation(s);
            if(s.tick!=61) return fail("native48 completed command tick changed");
        }
        FullState a,b;
        if(!RuntimeChronoMoveTerrainContractDetail::capture(*old,a,error)
            || !RuntimeChronoMoveTerrainContractDetail::capture(*fixed,b,error)) return false;
        if(oldTargets!=newTargets || a!=b
            || RuntimeChronoMoveTerrainContractDetail::commandState(*old)!=RuntimeChronoMoveTerrainContractDetail::commandState(*fixed))
            return fail("native48 original legal destination/fullsave gold changed");
        commandChecks+=2;++saveChecks;
    }
    std::cout<<"Manual formation Mapped contract passed: actualQueries="<<queryChecks
        <<" actualCommands="<<commandChecks<<" fullSaveChecks="<<saveChecks
        <<" stockNative256=yes airSpecial=yes slaveSpecial=yes automaticDefaults=yes reShroud=yes SeeHiddenObjectsVsMapped=yes\n";
    error.clear();return true;
}
