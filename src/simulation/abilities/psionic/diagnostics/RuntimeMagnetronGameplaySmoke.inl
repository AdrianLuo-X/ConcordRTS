bool runRuntimeMagnetronGameplaySmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Magnetron gameplay: "+why;return false;};
    int cases=0,saves=0,pairedTicks=0;
    for(int hz:{15,30,60})for(bool noCooldown:{false,true})
    for(const std::string landing:{"empty","vehicle","friendly-vehicle","building"})
    for(const std::string release:{"stop","move"}) {
        const std::string label=landing+"-"+release+"-"+std::to_string(hz)+(noCooldown?"-instant":"-normal");
        std::cout<<"Magnetron gameplay: "<<label<<" begin\n";
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.fixedTickHz=hz;s.config.noCooldown=noCooldown;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=false;s.config.playerSlots.clear();
        s.config.playerCountry="YuriCountry";s.economy.owner="YuriCountry";
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainInfoByCell.clear();s.terrainByCell.clear();
        s.mapFacts={};s.mapFactsLoaded=true;s.visionEnabled=false;
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
            MapCellFacts facts;facts.x=x;facts.y=y;facts.cellIndex=y*512+x;
            facts.hasTerrain=true;facts.terrainClass="land";facts.passabilityHint="passable";
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex,s.mapFacts.cells.size());
            s.mapFacts.cells.push_back(facts);
        }
        const auto add=[&](RuntimeBuildKind kind,const std::string& type,const std::string& owner,RuntimeCellPoint cell) {
            auto* object=addRuntimeObjectInstance(s,kind,type,cell);if(!object)return -1;
            object->owner=owner;object->mission="Sleep";object->weaponCooldownTicksRemaining=100000;
            object->buildAnimTicksRemaining=object->buildAnimTicksTotal=0;clearRuntimeRenderPosition(*object);
            return object->instanceId;
        };
        const int sourceId=add(RuntimeBuildKind::Unit,"TELE","YuriCountry",{60,64});
        const int targetId=add(RuntimeBuildKind::Unit,"MTNK","Americans",{68,64});
        if(sourceId<0||targetId<0)return fail(label+" original units missing");
        auto* source=findMutableObjectById(s,sourceId);source->mission="Guard";
        source->weaponCooldownTicksRemaining=0;source->facing=source->turretFacing=runtimeFacingFromDelta(8,0);
        auto* target=findMutableObjectById(s,targetId);
        if(landing=="vehicle"||landing=="building")target->health=std::max(1,target->health/4);
        const int targetHealth=target->health;
        initializeRuntimeNavigation(s);ensureRuntimeSpecialMechanismsInitialized(s);
        if(!runtimeMapQueryCell(s,{67,64})||!runtimeMapQueryPassabilityAllows(s,{67,64},*target))
            return fail(label+" fixture terrain lookup/passability missing");
        RuntimeSnapshotActiveStateScope active(s);s.selectedObjectIds={sourceId};
        if(runtimeTargetCursorIntent(s,*target)!=RuntimeTargetCursorIntent::Attack
            ||!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{68,64},target)
            ||s.lastCommand.acceptedCount!=1)return fail(label+" ordinary cursor/click rejected");
        const auto step=[](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope scope(state);++state.tick;advanceRuntimeSimulation(state);
        };
        bool lifted=false;
        for(int i=0;i<hz*8;++i) {
            step(s);const auto* binding=runtimeMagBeamBindingForTarget(s,targetId);
            if(binding&&binding->kinematics.worldZ>=2.9&&binding->kinematics.worldX<67.0){lifted=true;break;}
        }
        const auto* binding=runtimeMagBeamBindingForTarget(s,targetId);target=findMutableObjectById(s,targetId);
        if(!lifted||!target||!binding||!target->airborne||!target->hasRenderPosition
            ||std::abs(target->renderLevel-binding->kinematics.worldZ)>0.001||target->health!=targetHealth)
            return fail(label+" full simulation did not lift/hold intact; "+s.simulationStatus
                +" lifted="+std::to_string(lifted)+" binding="+std::to_string(binding!=nullptr)
                +" z="+std::to_string(binding?binding->kinematics.worldZ:-1)
                +" x="+std::to_string(binding?binding->kinematics.worldX:-1)
                +" airborne="+std::to_string(target&&target->airborne)
                +" render="+std::to_string(target&&target->hasRenderPosition)
                +" renderZ="+std::to_string(target?target->renderLevel:-1)
                +" health="+std::to_string(target?target->health:-1));
        const auto saveClone=[&](RuntimeWindowState& from) {
            std::ostringstream saved;IniFile ini;auto copy=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*copy,from,false);
            if(!writeRuntimeStateToStream(from,saved,error)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*copy,ini,error))return std::unique_ptr<RuntimeWindowState>{};
            ++saves;return copy;
        };
        auto carried=saveClone(s);if(!carried)return fail(label+" carried full save: "+error);
        for(int t=0;t<hz;++t) {
            step(s);step(*carried);++pairedTicks;
            if(s.magBeams.stateHash()!=carried->magBeams.stateHash())return fail(label+" carried continuation diverged");
        }
        binding=runtimeMagBeamBindingForTarget(s,targetId);target=findMutableObjectById(s,targetId);
        if(!binding||!target)return fail(label+" carry ended before release");
        RuntimeMagBeamBindingState prediction=*binding;RuntimeCellPoint dropCell{-1,-1};
        for(int t=0;t<hz*5;++t) {
            const auto next=runtimeAdvanceMagBeamFallingKinematics(s,prediction,*target);
            if(next.kind==RuntimeMagBeamHostUpdateKind::ImpactResolved) {
                dropCell={next.impact.kinematics.location.x,next.impact.kinematics.location.y};break;
            }
            prediction.kinematics=next.kinematics;
        }
        if(dropCell.x<0)return fail(label+" no predicted landing");
        int obstacleId=-1,obstacleHealth=0;
        if(landing!="empty") {
            obstacleId=add(landing=="building"?RuntimeBuildKind::Building:RuntimeBuildKind::Unit,
                landing=="building"?"NAPOWR":"HTNK",landing=="friendly-vehicle"?"YuriCountry":"Americans",dropCell);
            if(obstacleId<0)return fail(label+" landing obstacle missing");
            obstacleHealth=findObjectById(s,obstacleId)->health;
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            if(runtimeMagBeamLandingOccupancy(s,targetId,dropCell).objectId!=obstacleId)
                return fail(label+" obstacle does not occupy the predicted landing cell");
        }
        // The falling vehicle is now correctly an air target. On a move order,
        // OpportunityFire can therefore select the nearby building instead of
        // the suspended tank. Isolate impact damage from a second MagneShake.
        findMutableObjectById(s,sourceId)->weaponCooldownTicksRemaining=100000;
        s.selectedObjectIds={sourceId};
        if(release=="stop")issueRuntimeStopCommand(s);else issueRuntimeMoveCommand(s,{58,64});
        step(s);binding=runtimeMagBeamBindingForTarget(s,targetId);
        if(!binding||binding->phase!=RuntimeMagBeamPhase::Falling)return fail(label+" command did not release the unit");
        auto falling=saveClone(s);if(!falling)return fail(label+" falling full save: "+error);
        const auto hp=[](const RuntimeWindowState& state,int id) {
            const auto* object=findObjectById(state,id);return object&&object->alive?std::max(0,object->health):0;
        };
        for(int t=0;t<hz*8&&!s.magBeams.bindings().empty();++t) {
            step(s);step(*falling);++pairedTicks;
            if(s.magBeams.stateHash()!=falling->magBeams.stateHash()||hp(s,targetId)!=hp(*falling,targetId)
                ||hp(s,obstacleId)!=hp(*falling,obstacleId))return fail(label+" falling continuation diverged");
        }
        if(!s.magBeams.bindings().empty())return fail(label+" unit remained suspended");
        if(landing=="empty") {
            const auto* landed=findObjectById(s,targetId);
            if(!landed||landed->health!=targetHealth||landed->airborne||landed->mission=="MagBeamSuspended")
                return fail(label+" empty landing did not restore the intact tank");
        } else if(landing=="building") {
            if(hp(s,targetId)!=0||hp(s,obstacleId)!=obstacleHealth-targetHealth)
                return fail(label+" building impact: tank="+std::to_string(hp(s,targetId))+" building="
                    +std::to_string(hp(s,obstacleId))+" expected="+std::to_string(obstacleHealth-targetHealth));
        } else if(hp(s,targetId)!=0||hp(s,obstacleId)!=0) {
            return fail(label+" vehicle collision left survivors: carried="+std::to_string(hp(s,targetId))
                +" occupant="+std::to_string(hp(s,obstacleId)));
        }
        ++cases;std::cout<<"Magnetron gameplay: "<<label<<" click/lift/carry/release/impact/full-save passed\n";
    }
    std::cout<<"Magnetron gameplay: "<<cases<<" full simulation cases, "<<saves<<" full saves, "<<pairedTicks<<" continuation ticks; passed\n";
    return true;
}
