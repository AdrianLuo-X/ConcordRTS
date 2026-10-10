// Include globally AFTER the native mass fixture, complete writer/simulation,
// collectRuntimeFutureNormalVoxelTypeIds and both public render contract APIs.
// Requires RuntimePlannerPassabilityContractDetail::Authority/capture and the
// original RuntimeVisionTerrainCoordinateContractDetail::equalVision oracle.
#pragma once

namespace RuntimeFutureVoxelBootstrapWorldSaveContractDetail {
struct Full {
    RuntimePlannerPassabilityContractDetail::Authority authority;
    std::uint64_t special=0,spy=0,persistent=0;
    bool operator==(const Full&) const=default;
};
bool capture(RuntimeWindowState& state,Full& out,std::string& error) {
    RuntimeSnapshotActiveStateScope active(state);
    (void)completeRuntimeWorldRefresh(state);
    out.special=state.specialMechanisms.stateHash(); out.spy=state.spies.stateHash();
    out.persistent=runtimePersistentTransientStateHash(state);
    return RuntimePlannerPassabilityContractDetail::capture(state,out.authority,error);
}
RuntimeMapRenderContext context(const RuntimeWindowState& state) {
    RuntimeMapRenderContext out;
    out.rules=state.rulesLoaded?&state.rules:nullptr;
    out.art=state.artLoaded?&state.art:nullptr;
    out.theater=state.theaterLoaded?&state.theater:nullptr;
    out.mapFacts=state.mapFactsLoaded?&state.mapFacts:nullptr;
    out.assetRootPath=runtimeAssetRootPath(state.config);
    out.tileRootPath=runtimeTileRootPath(state.config,state.report.map);
    out.palettePath=runtimePalettePath(state.config,state.report.map);
    out.structureRootPath=runtimeStructureRootPath(state.config,state.report.map);
    out.structurePalettePath=runtimeStructurePalettePath(state.config,state.report.map);
    out.ownerRemapColors=&state.ownerRemapColors;
    out.neutralRemapOwners=&state.neutralRemapOwners;
    out.animationTick=state.tick; out.fixedTickHz=state.config.fixedTickHz;
    out.stableRenderConfiguration=true;
    return out;
}
bool hasSrefSource(const RuntimeMapRenderCache& cache) {
    return std::any_of(cache.voxelSourcesByImage.begin(),cache.voxelSourcesByImage.end(),[](const auto& source) {
        return source.first.find("|vxl=sref.vxl|hva=sref.hva")!=std::string::npos;
    });
}
bool hasReadySrefSource(const RuntimeMapRenderCache& cache) {
    return std::any_of(cache.voxelSourcesByImage.begin(),cache.voxelSourcesByImage.end(),[](const auto& source) {
        return source.first.find("|vxl=sref.vxl|hva=sref.hva")!=std::string::npos
            && source.second.loadAttempted && source.second.loaded
            && !source.second.vxlBytes.empty() && !source.second.hvaBytes.empty();
    });
}
std::vector<std::string> srefRasterKeys(const RuntimeMapRenderCache& cache) {
    std::vector<std::string> out;
    for(const auto& [key,entry]:cache.voxelObjectsByType) {
        (void)entry;
        if(key.rfind("sref|",0)==0) out.push_back(key);
    }
    std::sort(out.begin(),out.end()); return out;
}
bool readyRaster(const RuntimeVoxelCacheEntry& entry) {
    return entry.loadAttempted && entry.loaded && entry.hasSprite
        && entry.image.width>0 && entry.image.height>0
        && entry.image.pixels.size()==static_cast<std::size_t>(entry.image.width)*static_cast<std::size_t>(entry.image.height);
}
const RuntimeBuildCatalogItem* srefItem(const RuntimeWindowState& state) {
    const auto found=std::find_if(state.economy.catalog.begin(),state.economy.catalog.end(),[](const auto& item) {
        return item.kind==RuntimeBuildKind::Unit && item.typeId=="SREF";
    });
    return found==state.economy.catalog.end()?nullptr:&*found;
}
}

bool runRuntimeFutureVoxelBootstrapWorldSaveContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeFutureVoxelBootstrapWorldSaveContractDetail;
    const auto fail=[&](const std::string& why) { error="future voxel native production: "+why; return false; };
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.gameModeRulesOverride.clear();
    assets->config.techLevel=10; assets->config.startingCredits=100000;
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    const auto* srefRules=assets->rules.findUnitType("UnitType","SREF");
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded
        || !srefRules || srefRules->allowedToStartInMultiplayer || srefRules->cost<=0
        || srefRules->speed!=4 || srefRules->rot!=5 || assets->config.fixedTickHz!=30
        || trimAsciiLocal(assets->config.defaultMapPath).empty())
        return fail("verified stock SREF Speed4/ROT5/30Hz/assets/real source map identity required");
    // Original native facing starts at0. East egress targets96: ROT5 needs
    // floor(96/5)=19 rule frames. At30Hz its tick1 clock starts at ruleframe0.
    const int expectedLaunchTick=runtimeRuleFramesToTicks(96/srefRules->rot,assets->config.fixedTickHz);
    const double stockBaseTicks=256.0*assets->config.fixedTickHz
        /(runtimeRuleFramesPerSecond*runtimeRuleSpeedLeptons(srefRules->speed));
    const auto stockBaseQ16=static_cast<std::int64_t>(std::llround(stockBaseTicks*65536.0));
    const auto terminalQ16=static_cast<std::int64_t>(std::llround(
        stockBaseQ16*RuntimeVehicleArrivalMotion::durationScale(1.0,0.0)));
    const int expectedSegmentTicks=static_cast<int>((terminalQ16+(1LL<<15))>>16);
    const int expectedFinishTick=expectedLaunchTick+expectedSegmentTicks;
    // The original post-behavior delivery pass observes completion in this
    // SAME tick, after finished behaviors are erased (Shell101590-101594).
    const int expectedReleaseTick=expectedFinishTick;
    if(expectedLaunchTick!=38 || expectedSegmentTicks!=145 || expectedFinishTick!=183 || expectedReleaseTick!=183)
        return fail("original SREF turn/braking/release golden changed");
    const auto prepare=[&](RuntimeWindowState& state,std::vector<int>& ids) {
        state.config=assets->config; state.report=assets->report; state.rules=assets->rules;
        state.art=assets->art; state.theater=assets->theater;
        state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
        state.theaterLoaded=assets->report.theaterLoaded;
        RuntimeSnapshotActiveStateScope active(state);
        // Original opening roster only: 24 actual MTNK +24 E1, nine real
        // Allied buildings including a powered GAWEAP and a live GATECH.
        if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true,false,96,96,true,false)) return false;
        state.config.defaultMapPath=assets->config.defaultMapPath;
        if(ids.size()!=48 || state.world.getObjects().size()!=57 || state.tick!=0 || state.config.fixedTickHz!=30
            || std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),
                [](const auto& object) { return object.type=="SREF"; }))
            return fail("native exact opening must contain no SREF prototype");
        (void)completeRuntimeWorldRefresh(state);
        return true;
    };
    std::unique_ptr<RuntimeWindowState> states[2];
    std::array<std::vector<int>,2> ids;
    for(int branch=0;branch<2;++branch) {
        states[branch]=std::make_unique<RuntimeWindowState>();
        if(!prepare(*states[branch],ids[branch])) return fail("native opening: "+error);
    }
    if(ids[0]!=ids[1]) return fail("native opening IDs/order differ");
    // Public renderer asset contract runs FIRST; all its cache/discovery
    // objects are internal, and no prototype can enter either GameWorld.
    Full assetBefore,assetAfter;
    if(!capture(*states[0],assetBefore,error)
        || !runRuntimeFutureVoxelBootstrapAssetContractCheck(context(*states[0]),error)
        || !capture(*states[0],assetAfter,error)) return fail("public asset contract/full writer: "+error);
    if(!(assetBefore==assetAfter)) return fail("public asset contract changed native authority");
    std::size_t fullChecks=0,renderChecks=0,pairedTicks=0;
    const auto pair=[&](const std::string& label) {
        Full out[2];
        if(!capture(*states[0],out[0],error) || !capture(*states[1],out[1],error))
            return fail(label+" complete writer: "+error);
        if(!(out[0]==out[1]) || !RuntimeVisionTerrainCoordinateContractDetail::equalVision(
            states[0]->vision,states[1]->vision,states[0]->tick,error))
            return fail(label+" complete authority/save/public vision diverged: "+error);
        ++fullChecks; return true;
    };
    if(!pair("opening")) return false;
    const auto renderPair=[&](RuntimeCellPoint center,bool requireProduced,const std::string& label) {
        std::vector<std::uint32_t> pixels[2]; std::optional<RuntimeMapRenderStats> stats[2];
        std::array<std::int64_t,2> hitDeltas{},missDeltas{};
        std::array<std::vector<std::string>,2> srefBefore,srefReadyBefore,srefAfter;
        for(int branch=0;branch<2;++branch) {
            auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
            Full before,after;
            if(!capture(state,before,error)) return fail(label+" before render writer: "+error);
            const auto hitsBefore=static_cast<std::int64_t>(state.renderCache.voxelCacheHits);
            const auto missesBefore=static_cast<std::int64_t>(state.renderCache.voxelCacheMisses);
            srefBefore[branch]=srefRasterKeys(state.renderCache);
            for(const auto& key:srefBefore[branch])
                if(readyRaster(state.renderCache.voxelObjectsByType.at(key))) srefReadyBefore[branch].push_back(key);
            if(requireProduced && (branch==0
                ? hasSrefSource(state.renderCache) || !srefBefore[branch].empty()
                : !hasReadySrefSource(state.renderCache) || srefReadyBefore[branch].empty()))
                return fail(label+" actual cold first-use/prepared SREF source/raster gate branch="+std::to_string(branch));
            stats[branch]=drawRuntimeStateOffscreenAtCell(state,center,800,600,error,"",
                false,false,-1,&pixels[branch],false,nullptr,false,true);
            if(!stats[branch] || !stats[branch]->rendered || pixels[branch].size()!=800U*600U)
                return fail(label+" complete native DIB render: "+error);
            hitDeltas[branch]=static_cast<std::int64_t>(state.renderCache.voxelCacheHits)-hitsBefore;
            missDeltas[branch]=static_cast<std::int64_t>(state.renderCache.voxelCacheMisses)-missesBefore;
            if(hitDeltas[branch]<0 || missDeltas[branch]<0
                || stats[branch]->voxelCacheHits!=state.renderCache.voxelCacheHits
                || stats[branch]->voxelCacheMisses!=state.renderCache.voxelCacheMisses)
                return fail(label+" renderer cumulative-counter readback/nonnegative draw delta branch="+std::to_string(branch));
            srefAfter[branch]=srefRasterKeys(state.renderCache);
            if(!capture(state,after,error) || !(before==after))
                return fail(label+" renderer changed complete game authority: "+error);
        }
        if(pixels[0]!=pixels[1]) return fail(label+" complete 32-bit DIB differs");
        std::string field;
        if(!runtimeFutureVoxelBootstrapRenderContractStatsEqual(*stats[0],*stats[1],field))
            return fail(label+" full non-timing render stats differ: "+field);
        if(requireProduced && (stats[0]->voxelSpriteDiagnostic.find("SREF.body=")==std::string::npos
            || stats[1]->voxelSpriteDiagnostic.find("SREF.body=")==std::string::npos
            || stats[0]->visibleVoxelBodies<=0 || stats[1]->visibleVoxelBodies<=0
            || stats[0]->voxelShadowsDrawn<=0 || stats[1]->voxelShadowsDrawn<=0))
            return fail(label+" actual produced SREF body/shadow must participate in complete image");
        if(!requireProduced && (stats[0]->voxelSpriteDiagnostic.find("MTNK.body=")==std::string::npos
            || stats[1]->voxelSpriteDiagnostic.find("MTNK.body=")==std::string::npos))
            return fail(label+" original opening MTNK must really seed the normal voxel cache");
        std::size_t exactPreparedRasterHits=0;
        if(requireProduced) {
            if(srefAfter[0].empty() || !hasReadySrefSource(states[0]->renderCache)
                || !hasReadySrefSource(states[1]->renderCache))
                return fail(label+" cold first-use must actually load complete SREF source/rasters");
            // Only the exact keys used by the actual cold standard pose are
            // claimed, rather than all conceivable unsupported future poses.
            for(const auto& key:srefAfter[0]) {
                const auto cold=states[0]->renderCache.voxelObjectsByType.find(key);
                if(cold==states[0]->renderCache.voxelObjectsByType.end())
                    return fail(label+" cold first-use raster key disappeared: "+key);
                // Missing optional parts retain the original negative cache
                // outcome; only complete actual rasters claim a prepared hit.
                if(!readyRaster(cold->second)) continue;
                const auto prepared=states[1]->renderCache.voxelObjectsByType.find(key);
                if(cold->second.animationFrameIndex!=0
                    || !std::binary_search(srefReadyBefore[1].begin(),srefReadyBefore[1].end(),key)
                    || prepared==states[1]->renderCache.voxelObjectsByType.end() || !readyRaster(prepared->second)
                    || !prepared->second.image.rasterCacheHit || prepared->second.image.rasterCacheKey!=key)
                    return fail(label+" actual cold first-use key was not an existing prepared complete raster hit: "+key);
                ++exactPreparedRasterHits;
            }
            if(missDeltas[1]>=missDeltas[0] || hitDeltas[1]<=hitDeltas[0] || exactPreparedRasterHits==0)
                return fail(label+" actual single-render cache witness absent coldHitDelta="
                    +std::to_string(hitDeltas[0])+" preparedHitDelta="+std::to_string(hitDeltas[1])
                    +" coldMissDelta="+std::to_string(missDeltas[0])+" preparedMissDelta="+std::to_string(missDeltas[1])
                    +" coldLifetimeMisses="+std::to_string(stats[0]->voxelCacheMisses)
                    +" preparedLifetimeMisses="+std::to_string(stats[1]->voxelCacheMisses));
        }
        std::cout<<"Future voxel native render delta: stage="<<(requireProduced?"produced":"opening")
            <<" coldHits="<<hitDeltas[0]<<" preparedHits="<<hitDeltas[1]
            <<" coldMisses="<<missDeltas[0]<<" preparedMisses="<<missDeltas[1]
            <<" coldSREFBefore="<<srefBefore[0].size()<<" preparedSREFBefore="<<srefBefore[1].size()
            <<" actualExactPreparedRasterHits="<<exactPreparedRasterHits<<" countersAreDrawDeltas=yes\n";
        ++renderChecks; return pair(label+" after paint");
    };
    // Read palettes and stock indexes by rendering the actual opening world;
    // no SREF discovery/prototype is added to these two source caches.
    if(!renderPair({64,70},false,"opening resource preparation")) return false;
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
        Full before,after; if(!capture(state,before,error)) return fail("normal prewarm before writer: "+error);
        if(hasSrefSource(state.renderCache)) return fail("SREF source existed in actual opening cache");
        RuntimeVoxelLoadingPrewarmLimits limits; RuntimeVoxelLoadingPrewarmStats stats;
        prewarmRuntimeCurrentWorldNormalVoxels(state.world,context(state),state.renderCache,limits,stats);
        if(stats.referenceSkipped || stats.families==0)
            return fail("actual current-world normal prewarm not enabled reference="+std::to_string(stats.referenceSkipped)
                +" paletteNotReady="+std::to_string(stats.paletteNotReadySkipped)
                +" unsupported="+std::to_string(stats.unsupportedProfileSkipped)
                +" families="+std::to_string(stats.families));
        if(hasSrefSource(state.renderCache)) return fail("normal current-world prewarm fabricated future SREF");
        if(!capture(state,after,error) || !(before==after)) return fail("normal prewarm changed full writer authority: "+error);
    }
    // Removing the actual prerequisite must block the REAL queue predicate,
    // while the read-only future candidate list must still contain SREF.
    auto qualification=std::make_unique<RuntimeWindowState>(); std::vector<int> qualifiedIds;
    if(!prepare(*qualification,qualifiedIds)) return fail("missing-prerequisite fixture: "+error);
    {
        RuntimeSnapshotActiveStateScope active(*qualification);
        std::size_t removed=0;
        for(auto& object:qualification->world.accessObjects()) if(object.type=="GATECH" && object.alive) {
            object.alive=false; object.health=0; object.mission="Destroyed"; ++removed;
        }
        if(removed!=1) return fail("native GATECH prerequisite fixture identity");
        qualification->navigationRefreshPending=true; qualification->powerRefreshPending=true;
        (void)completeRuntimeWorldRefresh(*qualification);
        const auto* item=srefItem(*qualification);
        if(!item || runtimeBuildAvailability(*qualification,*item).canQueue)
            return fail("without GATECH the original SREF queue must be forbidden");
        Full before,after; if(!capture(*qualification,before,error)) return fail("candidate prerequisite before writer: "+error);
        const auto candidates=collectRuntimeFutureNormalVoxelTypeIds(*qualification);
        if(!capture(*qualification,after,error) || !(before==after)
            || std::find(candidates.begin(),candidates.end(),"SREF")==candidates.end())
            return fail("future candidate must retain SREF without GATECH and change no authority");
    }
    std::vector<std::string> candidates[2];
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
        Full before,after; if(!capture(state,before,error)) return fail("candidate before writer: "+error);
        candidates[branch]=collectRuntimeFutureNormalVoxelTypeIds(state);
        if(std::find(candidates[branch].begin(),candidates[branch].end(),"SREF")==candidates[branch].end()
            || !capture(state,after,error) || !(before==after)) return fail("native future candidates/SREF/full writer");
    }
    if(candidates[0]!=candidates[1]) return fail("native potential type IDs/order differ");
    RuntimeFutureVoxelBootstrapStats future;
    {
        auto& state=*states[1]; RuntimeSnapshotActiveStateScope active(state);
        Full before,after; if(!capture(state,before,error)) return fail("future bootstrap before writer: "+error);
        RuntimeFutureVoxelBootstrapLimits limits;
        prewarmRuntimeFutureNormalVoxelTypes(candidates[1],context(state),state.renderCache,limits,future);
        if(future.referenceSkipped || !hasSrefSource(state.renderCache) || future.newSources==0
            || future.newEntries==0 || future.newEntries>limits.maximumNewEntries
            || future.addedPayloadBytes>limits.maximumAddedPayloadBytes)
            return fail("native candidate bootstrap must prepare absent SREF within bounds reference="+std::to_string(future.referenceSkipped)
                +" source="+std::to_string(hasSrefSource(state.renderCache))+" newSources="+std::to_string(future.newSources)
                +" newEntries="+std::to_string(future.newEntries)+" indexNotReady="+std::to_string(future.indexNotReadySkipped)
                +" unknownEarlier="+std::to_string(future.unknownEarlierArchives)
                +" paletteNotReady="+std::to_string(future.paletteNotReadySkipped)
                +" unsupported="+std::to_string(future.unsupportedProfileSkipped)
                +" timeStopped="+std::to_string(future.timeStopped)+" budgetStopped="+std::to_string(future.budgetStopped));
        if(!capture(state,after,error) || !(before==after)) return fail("future bootstrap changed complete writer: "+error);
    }
    if(hasSrefSource(states[0]->renderCache)) return fail("cold reference unexpectedly discovered future SREF");
    if(!pair("post bootstrap, cold reference retains no SREF")) return false;
    std::array<RuntimeBuildCatalogItem,2> items;
    std::array<int,2> startingCredits{},factoryIds{-1,-1},producedIds{-1,-1};
    std::array<RuntimeCellPoint,2> firstProducedCell,nativeSpawnCells;
    std::array<std::optional<RuntimeFactoryDeliveryLease>,2> observedDeliveryLeases;
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
        const auto* item=srefItem(state);
        if(!item || !runtimeBuildAvailability(state,*item).canQueue) return fail("native powered/prerequisite SREF queue: "+state.economy.status);
        items[branch]=*item; startingCredits[branch]=state.economy.credits;
        const auto spawn=findRuntimeProductionSpawnForOwner(state,items[branch],state.economy.owner,false);
        if(!spawn || !spawn->anchor || spawn->anchor->type!="GAWEAP") return fail("native SREF delivery must select actual GAWEAP");
        factoryIds[branch]=spawn->anchor->instanceId;
        // Read the original native production spawn BEFORE simulation. The
        // first observed logical position after advanceSimulation may already
        // be its exit target: that SAME tick spawns and commits the first Move.
        nativeSpawnCells[branch]=spawn->cell;
        if(!queueRuntimeBuildOrder(state,items[branch])) return fail("actual SREF queue rejected: "+state.economy.status);
        (void)advanceRuntimeProductionEconomy(state);
        if(!completeRuntimeActiveProductionForProbe(state,items[branch])) return fail("actual active SREF remaining work/payment completion failed");
        if(state.economy.credits!=startingCredits[branch]-items[branch].cost
            || std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),[](const auto& object) {return object.type=="SREF";}))
            return fail("paid remaining work must debit exactly once and not fabricate the SREF");
        (void)completeRuntimeWorldRefresh(state);
        const auto readySpawn=findRuntimeProductionSpawnForOwner(state,items[branch],state.economy.owner,false);
        if(!readySpawn || !readySpawn->anchor || readySpawn->anchor->instanceId!=factoryIds[branch]
            || readySpawn->cell!=nativeSpawnCells[branch])
            return fail("queued paid work changed the original native spawn before simulation");
    }
    if(factoryIds[0]!=factoryIds[1]) return fail("native production factory identity differs");
    if(!pair("queued and fully paid before original delivery")) return false;
    std::array<bool,2> factoryDoorWitness{},movementWitness{},originalSegmentWitness{};
    std::array<int,2> launchTicks{-1,-1},finishTicks{-1,-1},releaseTicks{-1,-1},previousRemaining{-1,-1};
    bool finished=false;
    const auto witness=[&](int branch) {
        const auto& state=*states[branch]; std::ostringstream out;
        const auto* factory=findObjectById(state,factoryIds[branch]);
        const auto* produced=findObjectById(state,producedIds[branch]);
        const auto behavior=state.behaviors.find(producedIds[branch]);
        out<<" route="<<branch<<" tick="<<state.tick<<" completedUnits="<<state.economy.completedUnits
            <<" objects="<<state.world.getObjects().size()<<" producedId="<<producedIds[branch]
            <<" nativeSpawn="<<nativeSpawnCells[branch].x<<','<<nativeSpawnCells[branch].y
            <<" firstObserved="<<firstProducedCell[branch].x<<','<<firstProducedCell[branch].y
            <<" doorSeen="<<factoryDoorWitness[branch]<<" moved="<<movementWitness[branch]
            <<" originalSegmentSeen="<<originalSegmentWitness[branch]
            <<" launchTick="<<launchTicks[branch]<<" finishTick="<<finishTicks[branch]<<" releaseTick="<<releaseTicks[branch];
        if(factory) out<<" factoryId="<<factory->instanceId<<" doorStart="<<factory->factoryExitAnimationStartTick
            <<" doorEnd="<<factory->factoryExitAnimationEndTick<<" doorAir="<<factory->factoryExitAirborne;
        else out<<" factory=missing";
        if(produced) out<<" cell="<<produced->x<<','<<produced->y<<" hasRender="<<produced->hasRenderPosition
            <<" render="<<produced->renderX<<','<<produced->renderY<<" renderActive="<<produced->renderMoveActive
            <<" renderDistance="<<produced->movementAnimationDistance<<" mission="<<produced->mission
            <<" exitDistance="<<std::max(std::abs(produced->x-nativeSpawnCells[branch].x),
                std::abs(produced->y-nativeSpawnCells[branch].y));
        else out<<" produced=missing";
        if(behavior!=state.behaviors.end()) out<<" behaviorKind="<<static_cast<int>(behavior->second.kind)
            <<" behaviorState="<<static_cast<int>(behavior->second.state)<<" path="<<behavior->second.path.size()
            <<" pending="<<behavior->second.pathRequestPending<<" cooldown="<<behavior->second.moveCooldownTicks
            <<" renderRemaining="<<behavior->second.renderMoveTicksRemaining
            <<" renderTotal="<<behavior->second.renderMoveTicksTotal<<" arrivalAfter="<<behavior->second.renderArrivalDistanceAfter
            <<" segmentFrom="
            <<behavior->second.moveSegmentFrom.x<<','<<behavior->second.moveSegmentFrom.y
            <<" actualRenderLaunch="<<behavior->second.renderFrom.x<<','<<behavior->second.renderFrom.y
            <<" actualRenderTarget="<<behavior->second.renderTo.x<<','<<behavior->second.renderTo.y
            <<" target="<<behavior->second.moveTarget.x<<','<<behavior->second.moveTarget.y;
        else out<<" behavior=absent";
        if(observedDeliveryLeases[branch]) {
            const auto& lease=*observedDeliveryLeases[branch];
            out<<" actualLeaseHost="<<lease.hostId<<" actualLeaseDock="<<lease.dockIndex
                <<" actualLeaseLane="<<static_cast<int>(lease.lane)<<" actualLeaseDomain="<<static_cast<int>(lease.domain)
                <<" actualLeaseSerial="<<lease.orderSerial<<" actualLeaseAcquiredTick="<<lease.acquiredTick
                <<" actualLeaseRally="<<lease.rallyCell.x<<','<<lease.rallyCell.y<<','<<lease.rallyCell.onBridge;
        } else out<<" actualLease=notObserved";
        out<<" deliveryLeases="<<state.factoryDeliveries.leases().size()<<" status="<<state.economy.status;
        return out.str();
    };
    for(int step=0;step<expectedReleaseTick && !finished;++step) {
        bool changed[2]{};
        for(int branch=0;branch<2;++branch) {
            auto& state=*states[branch]; RuntimeSnapshotActiveStateScope active(state);
            ++state.tick; changed[branch]=advanceRuntimeSimulation(state,false);
            (void)completeRuntimeWorldRefresh(state);
            std::size_t count=0;
            for(const auto& object:state.world.getObjects()) if(object.type=="SREF") {
                ++count;
                if(!object.alive || object.health<=0 || object.kind!=WorldObjectKind::Unit) return fail("normal simulation produced invalid SREF");
                if(producedIds[branch]<0) {
                    producedIds[branch]=object.instanceId; firstProducedCell[branch]=runtimeCellPointForObject(object);
                    const auto& leases=state.factoryDeliveries.leases();
                    const auto lease=std::find_if(leases.begin(),leases.end(),[&](const auto& candidate) {
                        return candidate.producedObjectId==object.instanceId;
                    });
                    if(lease!=leases.end()) observedDeliveryLeases[branch]=*lease;
                    if(lease==leases.end() || lease->hostId!=factoryIds[branch] || lease->dockIndex!=-1
                        || lease->lane!=RuntimeProductionLane::Vehicles || lease->domain!=RuntimeFactoryDeliveryDomain::Land
                        || lease->orderSerial==0 || lease->acquiredTick!=state.tick || state.tick!=1 || object.facing!=0
                        || lease->rallyCell.x!=nativeSpawnCells[branch].x+1
                        || lease->rallyCell.y!=nativeSpawnCells[branch].y || lease->rallyCell.onBridge)
                        return fail("actual first delivery must retain the original GAWEAP Vehicles exit lease"+witness(branch));
                    std::cout<<"Future voxel native first observed delivery:"<<witness(branch)<<'\n';
                }
                else if(producedIds[branch]!=object.instanceId) return fail("normal delivery changed produced identity");
            }
            if(count>1 || state.economy.completedUnits>1 || state.economy.credits!=startingCredits[branch]-items[branch].cost)
                return fail("normal simulation duplicated delivery or payment");
        }
        ++pairedTicks;
        if(changed[0]!=changed[1] || producedIds[0]!=producedIds[1]) return fail("normal production changed flag or SREF identity differs");
        if(!pair("normal production/exit tick="+std::to_string(states[0]->tick))) return false;
        if(producedIds[0]>=0) {
            finished=true;
            for(int branch=0;branch<2;++branch) {
                const auto& state=*states[branch];
                const auto* factory=findObjectById(state,factoryIds[branch]);
                const auto* produced=findObjectById(state,producedIds[branch]);
                if(!factory || !produced) return fail("native produced object/factory lookup"+witness(branch));
                factoryDoorWitness[branch]|=factory->factoryExitAnimationStartTick>=0 && factory->factoryExitAnimationEndTick>state.tick;
                movementWitness[branch]|=runtimeCellPointForObject(*produced)!=nativeSpawnCells[branch];
                const auto behavior=state.behaviors.find(producedIds[branch]);
                if(behavior!=state.behaviors.end() && produced->renderMoveActive
                    && behavior->second.moveSegmentFrom==nativeSpawnCells[branch]
                    && behavior->second.renderFrom==nativeSpawnCells[branch]
                    && behavior->second.renderTo!=nativeSpawnCells[branch]
                    && behavior->second.renderMoveTicksRemaining>0) {
                    originalSegmentWitness[branch]=true;
                    const auto& move=behavior->second;
                    if(launchTicks[branch]<0) {
                        launchTicks[branch]=state.tick;
                        if(state.tick!=expectedLaunchTick || move.renderMoveTicksTotal!=expectedSegmentTicks
                            || move.renderMoveTicksRemaining!=expectedSegmentTicks || move.moveCooldownTicks!=expectedSegmentTicks)
                            return fail("actual first native segment violated original turn/duration golden"+witness(branch));
                    } else if(previousRemaining[branch]<=0 || move.renderMoveTicksRemaining!=previousRemaining[branch]-1)
                        return fail("native segment countdown restarted, paused or skipped a fixed tick"+witness(branch));
                    if(move.renderMoveTicksTotal!=expectedSegmentTicks || move.renderArrivalDistanceAfter!=0.0
                        || move.moveCooldownTicks!=move.renderMoveTicksRemaining
                        || state.tick!=launchTicks[branch]+expectedSegmentTicks-move.renderMoveTicksRemaining)
                        return fail("native segment duration/cooldown/progress clock changed"+witness(branch));
                    previousRemaining[branch]=move.renderMoveTicksRemaining;
                } else if(launchTicks[branch]>=0 && finishTicks[branch]<0) {
                    if(produced->renderMoveActive || previousRemaining[branch]!=1 || state.tick!=expectedFinishTick)
                        return fail("native render segment failed exact countdown completion"+witness(branch));
                    finishTicks[branch]=state.tick; previousRemaining[branch]=0;
                }
                const bool deliveryClaimed=std::any_of(state.factoryDeliveries.leases().begin(),
                    state.factoryDeliveries.leases().end(),[&](const auto& lease) {
                        return lease.producedObjectId==producedIds[branch];
                    });
                const bool reachedActualRally=observedDeliveryLeases[branch]
                    && produced->x==observedDeliveryLeases[branch]->rallyCell.x
                    && produced->y==observedDeliveryLeases[branch]->rallyCell.y
                    && produced->onBridge==observedDeliveryLeases[branch]->rallyCell.onBridge;
                if(finishTicks[branch]>=0 && !reachedActualRally)
                    return fail("native countdown completion left its actual lease rally/layer"+witness(branch));
                if(!deliveryClaimed && releaseTicks[branch]<0) {
                    releaseTicks[branch]=state.tick;
                    if(finishTicks[branch]!=expectedFinishTick || state.tick!=expectedReleaseTick || !reachedActualRally)
                        return fail("native lease release violated original same-tick post-move golden"+witness(branch));
                }
                if(finishTicks[branch]==state.tick && deliveryClaimed)
                    return fail("native lease survived its original same-tick post-move release phase"+witness(branch));
                finished=finished && factoryDoorWitness[branch] && movementWitness[branch]
                    && originalSegmentWitness[branch] && reachedActualRally && !produced->renderMoveActive && !deliveryClaimed
                    && finishTicks[branch]==expectedFinishTick && releaseTicks[branch]==expectedReleaseTick
                    && factory->factoryExitAnimationStartTick<0 && factory->factoryExitAnimationEndTick<0;
            }
        }
    }
    if(!finished || producedIds[0]<0 || !factoryDoorWitness[0] || !factoryDoorWitness[1]
        || !movementWitness[0] || !movementWitness[1] || !originalSegmentWitness[0] || !originalSegmentWitness[1]
        || states[0]->economy.completedUnits!=1 || states[1]->economy.completedUnits!=1
        || states[0]->world.getObjects().size()!=58 || states[1]->world.getObjects().size()!=58)
        return fail("normal GAWEAP SREF spawn/door/clearance did not complete within derived183ticks"+witness(0)+witness(1));
    const auto* produced=findObjectById(*states[0],producedIds[0]);
    if(!produced) return fail("completed SREF identity absent");
    if(!renderPair(runtimeCellPointForObject(*produced),true,"actually produced exited SREF")) return false;
    if(renderChecks!=2 || pairedTicks==0 || fullChecks<pairedTicks+4 || future.newEntries==0)
        return fail("actual renderer/full-save/production witnesses incomplete");
    std::cout<<"Future voxel native cadence: stockSpeed=4 stockROT=5 fixedHz=30 launch="<<launchTicks[0]<<','<<launchTicks[1]
        <<" segmentTicks="<<expectedSegmentTicks<<" finish="<<finishTicks[0]<<','<<finishTicks[1]
        <<" release="<<releaseTicks[0]<<','<<releaseTicks[1]<<" exactCountdown=yes passed=yes\n";
    std::cout<<"Future voxel native production: stockSREFAbsent=yes potentialWithoutGATECH=yes normalPrewarmFullSave=yes"
        <<" legalQueue=yes paidOnce=yes originalSimulationSpawn=yes GAWEAPExit=yes fullDIBs=2 renderPairs="<<renderChecks
        <<" pairedTicks="<<pairedTicks<<" completeWorldSaveVision="<<fullChecks
        <<" bootstrapNewSources="<<future.newSources<<" bootstrapNewEntries="<<future.newEntries
        <<" completeRenderStats=353 cacheCountersOnlyDiffer=yes passed=yes\n";
    error.clear(); return true;
}
