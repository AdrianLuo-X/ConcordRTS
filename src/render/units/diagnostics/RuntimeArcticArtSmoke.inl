bool runRuntimeArcticArtSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Arctic art: "+why;return false;};
    const char* requested=std::getenv("CONCORD_ARCTIC_ART_DIR");
    const std::string root=requested&&*requested?requested:"build/smoke/feature-checks/10-arctic-art/checks";
    std::filesystem::create_directories(root);
    std::array<std::unique_ptr<RuntimeWindowState>,2> worlds;
    std::array<RuntimeMapRenderContext,2> contexts;
    int gameplayCases=0,liveFrames=0,deathFrames=0;
    for(int i=0;i<2;++i) {
        worlds[i]=std::make_unique<RuntimeWindowState>();auto& s=*worlds[i];
        s.config=config;s.config.computerAiEnabled=false;s.config.playerSlots.clear();
        s.config.noCooldown=s.config.multiplayerActive=s.config.crates=false;
        s.config.defaultMapPath=i?"local_data/resources/mix_exports/mapsmd03/all07smd.map"
            :"local_data/resources/mix_exports/multimd/2peaks.map";
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success)return fail("original map/resources "+s.config.defaultMapPath);
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=s.mapFactsLoaded=true;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        const auto* unit=s.rules.findUnitType("InfantryType","GHOST");
        if(!unit||!unit->alternateArcticArt||unit->image!="SEAL")return fail("stock AlternateArcticArt not loaded");
        auto& context=contexts[i];context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;context.mapFacts=&s.mapFacts;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        rebuildRuntimeTerrainInfoCache(s);
        std::optional<RuntimeCellPoint> dry,wet;
        for(const auto& cell:s.world.getTerrainCells()) {
            const auto it=s.terrainInfoByCell.find(runtimeCellKey(cell.x,cell.y));
            if(it==s.terrainInfoByCell.end())continue;const auto& info=it->second;
            if(!dry&&!info.water&&!info.bridge&&!info.shore&&info.passable)dry=RuntimeCellPoint{cell.x,cell.y};
            if(!wet&&info.water&&!info.bridge&&!info.shore)wet=RuntimeCellPoint{cell.x,cell.y};
            if(dry&&wet)break;
        }
        if(!dry||!wet)return fail("original map needs land and open water for death checks");
        // RailShot is a stock InfDeath=2 warhead without Tiberium=yes. The
        // existing HE/TiberiumProof damage mismatch is documented separately.
        for(bool water:{false,true})for(const char* warhead:{"SA","RailShot"}) {
            auto box=std::make_unique<RuntimeWindowState>();auto& probe=*box;
            seedRuntimeLoadScratch(probe,s,false);probe.world.accessObjects().clear();
            probe.config.computerAiEnabled=false;probe.triggers.bootstrapComplete=true;
            probe.runtimeSoundsEnabled=false;probe.runtimeSuppressPlayerAudioFeedback=true;
            probe.runtimePreferencePersistenceEnabled=false;
            probe.economy.owner="ArcticProbe";probe.economy.credits=10000;probe.economy.techLevel=10;
            initializeRuntimeLocalProductionLanes(probe.economy);
            const auto cell=water?*wet:*dry;
            WorldObjectInstance object;object.instanceId=42;object.kind=WorldObjectKind::Infantry;
            object.type="GHOST";object.owner=probe.economy.owner;object.health=125;object.alive=true;
            object.x=cell.x;object.y=cell.y;object.cell=runtimeCellKey(cell.x,cell.y);object.mission="Guard";
            probe.world.accessObjects().push_back(object);initializeRuntimeNavigation(probe);
            const auto* art=runtimeArtTypeForObject(probe,object);
            if(!art||art->id!=(i?"SEALA":"SEAL"))return fail("simulation ART differs from render ART");
            const std::string path=root+"/living-"+(i?"snow":"temperate")+(water?"-wet-":"-land-")+warhead+".ini";
            if(!saveRuntimeState(probe,path,error)||!loadRuntimeState(probe,path,error))return fail("living save/load "+error);
            const auto* loaded=findObjectById(probe,42);
            const auto* loadedArt=loaded?runtimeArtTypeForObject(probe,*loaded):nullptr;
            if(!loaded||loaded->type!="GHOST"||loaded->owner!=object.owner||!loadedArt
                ||loadedArt->id!=(i?"SEALA":"SEAL"))return fail("living save changed type/owner/arctic ART");
            RuntimeProjectileEffect hit;hit.actorId=41;hit.targetObjectId=42;hit.owner="EnemyProbe";
            hit.toCell=hit.currentCell=cell;hit.damage=10000;hit.warheadId=warhead;
            hit.ag=hit.aa=hit.affectsAllies=hit.affectsEnemies=true;
            if(!applyRuntimeProjectileImpact(probe,hit))return fail("actual lethal hit");
            const auto body=std::find_if(probe.impactEffects.begin(),probe.impactEffects.end(),
                [](const auto& effect){return effect.renderIntent=="infantryDeath";});
            if(body==probe.impactEffects.end()||body->animId!=(i?"SEALA":"SEAL")||body->owner!=object.owner)
                return fail(std::string(i?"SNOW":"TEMPERATE")+(water?"/wet/":"/land/")+warhead
                    +" actual death lost snow body/owner; living="+std::to_string(findObjectById(probe,42)!=nullptr)
                    +" effects="+std::to_string(probe.impactEffects.size()));
            const auto* wh=s.rules.findWarheadType(warhead);
            const auto expected=runtimeInfantryDeathSequence(s.art,*unit,wh?wh->infDeath:1,object.facing,water,i==1);
            if(!expected||body->animationStartFrame!=expected->startFrame)return fail("actual wet/dry death strip");
            const RuntimeImpactEffect saved=*body;
            RuntimeImpactEffect restored;
            if(!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(saved),restored)
                ||!restored.animId.empty()||restored.lifecycleState!=saved.lifecycleState||restored.owner!=saved.owner
                ||restored.animationStartFrame!=saved.animationStartFrame
                ||restored.remainingTicks!=saved.remainingTicks)return fail("death snapshot changed image/strip/owner/phase");
            restored.remainingTicks-=restored.animationFrameDelayTicks*2;*body=restored;
            const auto* actors=runtimeImpactRenderActorsForRender(probe);
            if(!actors||std::none_of(actors->begin(),actors->end(),[&](const auto& actor){
                return actor.renderIntent=="infantryDeath"&&actor.animId==saved.animId
                    &&actor.owner==saved.owner&&actor.frameIndex==saved.animationStartFrame+2;}))
                return fail("restored death restarted or lost alternate body");
            ++gameplayCases;
        }
    }
    if(!runRuntimeArcticArtRenderContractCheck(contexts[0],contexts[1],root,liveFrames,deathFrames,error))return false;
    for(const auto& box:worlds) {
        if(box->rules.findUnitType("InfantryType","GHOST")->image!="SEAL")return fail("base rule image was mutated");
    }
    std::cout<<"Arctic art passed: gameplay="<<gameplayCases<<" liveFrames="<<liveFrames<<" deathFrames="<<deathFrames
        <<"; original SEAL/SEALA, both theaters, 30/60 Hz, 8 facings, 11 poses, owner remap, "
        <<"shoreline/bridge/swim regression, living full save/load, actual deaths/snapshot resume, immutable image and shared cache\n";
    return true;
}
