// Host-consumer contract. The original two-click full production path is a
// separate unchanged smoke. This fixture directly owns the pending event
// boundary so old/new consumption and refresh timing can be compared.
template<class MakeState, class AddObject, class StepState>
bool runRuntimeChronosphereRelocationHostContract(MakeState&& make,
    AddObject&& add, StepState&& step, std::string& error) {
    const char* configured=std::getenv("CONCORD_CHRONOSPHERE_RELOCATION_OUTPUT");
    if(!configured||!*configured){error="relocation contract requires isolated output";return false;}
    const std::filesystem::path output=configured;
    std::filesystem::create_directories(output);
    int checks=0,cases=0,loads=0,continuations=0;
    const auto need=[&](bool ok,const std::string& why){++checks;if(!ok)error=why;return ok;};
    const auto save=[&](RuntimeWindowState& s,const std::string& name,std::string& ordinary){
        RuntimeSnapshotActiveStateScope snapshotBorrow(s);
        std::string reason;std::ostringstream bytes;
        const bool ok=writeRuntimeStateToStream(s,bytes,reason);
        ordinary=bytes.str();std::ofstream f(output/(name+".ini"),std::ios::binary);f<<ordinary;
        std::ostringstream extension;std::string extReason;
        const bool extOk=writeRuntimeMultiplayerSnapshotExtension(s,extension,extReason);
        std::ofstream e(output/(name+".extension.ini"),std::ios::binary);e<<extension.str();
        const auto occupied=buildRuntimeOccupancyLookup(s);
        std::ostringstream full;full<<std::setprecision(17)<<ok<<'|'<<reason<<'|'<<extOk<<'|'<<extReason
            <<'|'<<s.tick<<'|'<<s.world.deterministicSimulationStateHash()<<'|'<<s.superWeapons.stateHash()
            <<'|'<<s.specialMechanisms.stateHash()<<'|'<<s.behaviors.size()<<'|'<<s.navigationRefreshPending
            <<'|'<<s.projectedObjectsCacheValid<<'|'<<s.repaintRequested<<'\n';
        for(int id:s.selectedObjectIds)full<<"selected "<<id<<'\n';
        for(int y=55;y<=124;++y)for(int x=55;x<=124;++x)full<<occupied.contains(runtimeCellKey(x,y));
        full<<'\n'<<ordinary<<"\nFULL EXTENSION\n"<<extension.str();
        std::ofstream observed(output/(name+".complete.txt"),std::ios::binary);observed<<full.str();
        return std::pair{ok && extOk,full.str()};
    };
    for(int hz:{30,60})for(const std::string scenario:{"missing","dead","same","cross","cached-bridge","storage-rebuild"}) {
        ++cases;const auto label=scenario+'-'+std::to_string(hz);
        std::array<std::string,2> finalRecords;
        for(int route=0;route<2;++route){
            auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
            for(int i=0;i<21;++i)step(s);
            const int allocated=add(s,"MTNK",WorldObjectKind::Unit,{90,90},"Commander");
            auto* object=findMutableObjectById(s,allocated);
            if(!need(object!=nullptr,label+" initial object"))return false;
            object->instanceId=104;object->hasRenderPosition=object->renderMoveActive=true;
            object->renderX=89.75;object->renderY=89.5;object->renderLevel=2.25;
            // Seed an actual stale offset hint, then end that borrow.
            object=nullptr;
            if(!need(findObjectById(s,104)!=nullptr,label+" primed sparse index"))return false;
            s.selectedObjectIds={104};s.behaviors.try_emplace(104);
            initializeRuntimeNavigation(s);ensureRuntimeSubcellsInitialized(s);
            const RuntimeCellPoint destination=scenario=="same"?RuntimeCellPoint{90,90}:RuntimeCellPoint{104,100};
            for(auto& cell:s.world.accessTerrainCells())if(cell.x==destination.x&&cell.y==destination.y)cell.level=3;
            initializeRuntimeNavigation(s);
            if(scenario=="cached-bridge")s.terrainInfoByCell[runtimeCellKey(destination.x,destination.y)].bridge=true;
            object=findMutableObjectById(s,104);
            object->x=destination.x;object->y=destination.y;object->cell=runtimeCellKey(object->x,object->y);
            if(scenario=="dead"){object->alive=false;object->health=0;}
            object=nullptr;
            SuperWeaponObjectLifecycleEvent event;event.kind=SuperWeaponObjectLifecycleKind::Relocated;
            event.tick=s.tick;event.objectId=scenario=="missing"?999999:104;
            event.superWeaponInstanceId=1;event.superWeaponId="ChronoSphereSpecial";
            event.sourceOwner="Commander";event.oldCell={90,90};event.newCell={destination.x,destination.y};
            event.healthBefore=event.healthAfter=scenario=="dead"?0:300;
            event.reason="chronosphere formation transfer";s.pendingSuperWeaponObjectEvents.push_back(event);
            if(scenario=="storage-rebuild"){
                auto replacement=s.world.getObjects();std::reverse(replacement.begin(),replacement.end());
                s.world.accessObjects().swap(replacement);
            }
            const auto* immediate=findRuntimeObjectByIdIncludingInactive(s,104);
            if(!need(immediate&&immediate->x==destination.x&&immediate->y==destination.y,label+" immediate ID query"))return false;
            immediate=nullptr;
            s.navigationRefreshPending=false;s.projectedObjectsCacheValid=true;s.repaintRequested=false;
            const bool changed=route?flushRuntimeSuperWeaponHostEvents(s):s15LegacyFlushRuntimeSuperWeaponHostEvents(s);
            if(!need(changed&&s.pendingSuperWeaponObjectEvents.empty(),label+" consume exactly once"))return false;
            if(!need(s.navigationRefreshPending&&!s.projectedObjectsCacheValid&&s.repaintRequested,label+" refresh flags"))return false;
            if(scenario!="missing"&&scenario!="dead"){
                const auto* fresh=findObjectById(s,104);
                if(!need(fresh&&fresh->level==terrainLevelAtRuntimeCell(s,destination.x,destination.y)
                    &&fresh->onBridge==runtimeCellIsBridge(s,destination.x,destination.y)
                    &&!fresh->hasRenderPosition&&!fresh->renderMoveActive&&!s.behaviors.contains(104),label+" terrain/bridge/behavior"))return false;
            }
            std::string ordinary;auto observed=save(s,label+'-'+std::to_string(route)+"-consumed",ordinary);
            finalRecords[route]=observed.second;
            (void)completeRuntimeWorldRefresh(s);
            if(!need(!s.navigationRefreshPending,label+" refresh boundary"))return false;
            auto refreshed=save(s,label+'-'+std::to_string(route)+"-refreshed",ordinary);
            finalRecords[route]+=refreshed.second;
            if(!need(!(route?flushRuntimeSuperWeaponHostEvents(s):s15LegacyFlushRuntimeSuperWeaponHostEvents(s)),label+" no duplicate consume"))return false;
            if(scenario=="same"||scenario=="cross"||scenario=="storage-rebuild"){
                if(!need(refreshed.first,label+" ordinary serialization"))return false;
                IniFile ini;if(!need(ini.loadFromString(ordinary),label+" ordinary parse"))return false;
                auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
                {RuntimeSnapshotActiveStateScope restore(*restored);
                    if(!need(materializeRuntimeLoadStateFromIni(*restored,ini,error),label+" ordinary materialization: "+error))return false;}
                ++loads;restored->runtimeSoundsEnabled=restored->runtimePreferencePersistenceEnabled=false;
                restored->runtimeSuppressPlayerAudioFeedback=true;initializeRuntimeOfflineGameplayRouting(*restored);
                if(!need(findObjectById(*restored,104)!=nullptr,label+" fresh post-load borrow"))return false;
                for(int i=0;i<2;++i){step(*restored);++continuations;}
                std::string restoredBytes;
                const auto restoredRecord=save(*restored,label+'-'+std::to_string(route)+"-restored",restoredBytes);
                if(!need(restoredRecord.first,label+" continued ordinary/extension serialization"))return false;
                finalRecords[route]+=restoredRecord.second;
            }
        }
        if(!need(finalRecords[0]==finalRecords[1],label+" full old/new save, extension, occupancy, index, behavior, selection, tick/hash/flags"))return false;
    }
    std::cout<<"Chronosphere host relocation contract passed: cases="<<cases<<" checks="<<checks
        <<" ordinaryLoads="<<loads<<" continuationTicks="<<continuations
        <<" oldLiteralFlush=1 fullBytes=1 nativeWindow=not-required performance=not-run\n";
    return true;
}
