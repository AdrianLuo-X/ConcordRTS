bool runRuntimeControlledGarrisonSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="Controlled garrison: "+why;return false;};
    int cases=0,saves=0,mixed=0,released=0,pending=0,transactions=0,permanent=0;
    for(const std::string controllerType:{"YURI","YURIPR","MIND","YAPSYT","YAPPET"})
    for(const std::string infantryType:{"E1","E2","INIT"})
    for(const bool neutral:{true,false}) {
        const std::string label=controllerType+"-"+infantryType+(neutral?"-neutral":"-owned");
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.computerAiEnabled=false;s.config.multiplayerActive=false;
        s.config.fixedTickHz=30;s.config.noCooldown=false;s.config.playerSlots.clear();s.config.playerCountry="YuriCountry";
        s.config.superweapons=true;s.economy.owner="YuriCountry";s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimeSoundsEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.world.accessTerrainCells().clear();
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;s.visionEnabled=false;
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add=[&](RuntimeBuildKind kind,const std::string& type,RuntimeCellPoint cell) {
            auto* object=addRuntimeObjectInstance(s,kind,type,cell);if(!object)return -1;
            object->mission="Sleep";object->weaponCooldownTicksRemaining=100000;
            object->buildAnimTicksTotal=object->buildAnimTicksRemaining=0;clearRuntimeRenderPosition(*object);
            return object->instanceId;
        };
        const auto sourceKind=controllerType=="YAPSYT"||controllerType=="YAPPET"?RuntimeBuildKind::Building:
            controllerType=="MIND"?RuntimeBuildKind::Unit:RuntimeBuildKind::Infantry;
        const int sourceId=add(sourceKind,controllerType,controllerType=="YAPPET"?RuntimeCellPoint{54,64}:RuntimeCellPoint{60,64});
        const int controlledId=add(RuntimeBuildKind::Infantry,infantryType,{64,64});
        const int regularId=add(RuntimeBuildKind::Infantry,infantryType,{66,72});
        const int hostId=add(RuntimeBuildKind::Building,neutral?"CARUS07":"NABNKR",{70,68});
        const int releaseHostId=add(RuntimeBuildKind::Building,"CARUS07",{64,56});
        if(sourceId<0||controlledId<0||regularId<0||hostId<0||releaseHostId<0
            ||add(RuntimeBuildKind::Building,"NANRCT",{54,58})<0)return fail(label+" stock fixture");
        findMutableObjectById(s,controlledId)->owner="Russians";
        findMutableObjectById(s,releaseHostId)->owner="Neutral";
        if(neutral)findMutableObjectById(s,hostId)->owner="Neutral";
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);
        ensureRuntimeGarrisonsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeSnapshotActiveStateScope active(s);
        const auto step=[&](int ticks=1){for(int t=0;t<ticks;++t){++s.tick;advanceRuntimeEffects(s);advanceRuntimeSimulation(s);}};
        if(neutral) {
            s.selectedObjectIds={controlledId};
            if(!queueRuntimeOccupyTarget(s,*findObjectById(s,hostId)))return fail(label+" original-owner entry fixture");
            step();
            if(!s.pendingOccupyTargets.contains(controlledId))return fail(label+" original entry did not stay pending");
        }
        if(controllerType=="YAPPET") {
            ensureRuntimeSuperWeaponsInitialized(s);auto context=runtimeSuperWeaponContext(s);
            context.emit=runtimeSuperWeaponEventSink(s);s.superWeapons.tick(context);
            const auto* target=findObjectById(s,controlledId);
            if(!s.superWeapons.forceReady(s.economy.owner,"PsychicDominatorSpecial",&context)
                ||!s.superWeapons.beginTargetSelection(s.economy.owner,"PsychicDominatorSpecial",context,&error)
                ||!s.superWeapons.submitTarget(s.economy.owner,"PsychicDominatorSpecial",{{target->x,target->y},controlledId},context,&error))
                return fail(label+" actual dominator launch: "+error);
            for(int t=0;t<300&&!s.specialMechanisms.mindControlLinks().contains(controlledId);++t) {
                ++s.tick;advanceRuntimeEffects(s);
            }
        } else {
            const auto* source=findObjectById(s,sourceId);const auto* target=findObjectById(s,controlledId);
            const auto profile=runtimeWeaponProfileForObject(s,*source,target);
            if(!profile.weapon||!profile.warhead||!profile.warhead->mindControl)return fail(label+" native weapon");
            spawnRuntimeProjectile(s,*source,*target,*profile.weapon,profile,profile.weapon->damage);
            if(s.projectiles.empty())return fail(label+" actual psychic projectile");
            const auto projectile=s.projectiles.back();s.projectiles.clear();
            if(!applyRuntimeProjectileImpact(s,projectile))return fail(label+" actual psychic impact");
        }
        if(!s.specialMechanisms.mindControlLinks().contains(controlledId)
            ||findObjectById(s,controlledId)->owner!=s.economy.owner)return fail(label+" real control missing");
        if(neutral&&s.pendingOccupyTargets.contains(controlledId))return fail(label+" prior-owner entry survived capture");
        s.selectedObjectIds={controlledId};issueRuntimeStopCommand(s);
        const auto* host=findObjectById(s,hostId);
        const bool advertised=runtimeTargetCursorIntent(s,*host)==RuntimeTargetCursorIntent::Enter;
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host);
        if(s.pendingOccupyTargets.contains(controlledId)) {
            for(int t=0;t<900;++t) {
                step();const auto* unit=findRuntimeObjectByIdIncludingInactive(s,controlledId);
                if(unit&&unit->garrisonHostObjectId==hostId)
                    return fail(label+" controlled unit really entered the building after Primary click");
            }
            return fail(label+" controlled unit accepted a pending entry");
        }
        if(advertised||runtimeObjectCanOccupyTarget(s,*findObjectById(s,controlledId),*findObjectById(s,hostId)))
            return fail(label+" entry advertised while controlled");
        issueRuntimeStopCommand(s);

        // Full restoration must retain the relation used by the entry gate,
        // even though the ordinary Stop order has changed the unit's mission.
        std::ostringstream saved;if(!writeRuntimeStateToStream(s,saved,error))return fail(label+" save: "+error);
        IniFile ini;auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
        if(!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*loaded,ini,error))
            return fail(label+" restore: "+error);
        if(loaded->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
            ||runtimeObjectCanOccupyTarget(*loaded,*findObjectById(*loaded,controlledId),*findObjectById(*loaded,hostId)))
            return fail(label+" restore bypassed control restriction");
        loaded->selectedObjectIds={controlledId};
        if(queueRuntimeOccupyTarget(*loaded,*findObjectById(*loaded,hostId)))return fail(label+" restored order entered");
        ++saves;

        // A retained or late pending order must revalidate at actual arrival.
        s.pendingOccupyTargets[controlledId]=hostId;
        if(completeRuntimeOccupyEntry(s,controlledId)||s.pendingOccupyTargets.contains(controlledId)
            ||findRuntimeObjectByIdIncludingInactive(s,controlledId)->garrisonHostObjectId>=0)
            return fail(label+" final entry gate accepted controlled unit");
        ++pending;
        if(neutral) {
            const auto relationHash=s.specialMechanisms.stateHash();
            const auto garrisonHash=s.garrisons.stateHash(s.world.getObjects());
            const RuntimeOwnerTransitionGarrisonPreparation preparation{controlledId,hostId};
            const auto result=applyRuntimeOwnerTransitionBatch(s,{{hostId,"Neutral",s.economy.owner}},
                RuntimeOwnerTransitionRepairPolicy::ReleaseRelations,"controlledGarrisonProbe",nullptr,nullptr,false,&preparation);
            if(result.applied||result.error.find("mind-controlled")==std::string::npos
                ||findObjectById(s,hostId)->owner!="Neutral"||s.specialMechanisms.stateHash()!=relationHash
                ||s.garrisons.stateHash(s.world.getObjects())!=garrisonHash)
                return fail(label+" neutral ownership transaction bypassed/repaired restriction: "+result.error);
            ++transactions;
        }

        s.selectedObjectIds={controlledId,regularId};host=findObjectById(s,hostId);
        if(runtimeTargetCursorIntent(s,*host)!=RuntimeTargetCursorIntent::Enter)return fail(label+" mixed selection lost legal entry");
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host);
        if(!s.pendingOccupyTargets.contains(regularId)||s.pendingOccupyTargets.contains(controlledId))
            return fail(label+" mixed selection admitted wrong units");
        for(int t=0;t<900&&findRuntimeObjectByIdIncludingInactive(s,regularId)->garrisonHostObjectId<0;++t)step();
        if(findRuntimeObjectByIdIncludingInactive(s,regularId)->garrisonHostObjectId!=hostId
            ||!findObjectById(s,controlledId)||!s.specialMechanisms.mindControlLinks().contains(controlledId)
            ||findObjectById(s,hostId)->garrisonOccupantIds!=std::vector<int>{regularId})
            return fail(label+" actual mixed-group entry/control relation");
        ++mixed;

        if(controllerType!="YAPPET") {
            if(!s.specialMechanisms.releaseMindControlByController(s.world.accessObjects(),sourceId,s.tick,nullptr,"garrison smoke release"))
                return fail(label+" release failed");
            // Continue from the restored original owner's point of view.
            s.economy.owner="Russians";s.config.playerCountry="Russians";s.selectedObjectIds={controlledId};
            const auto* releasedHost=findObjectById(s,releaseHostId);
            if(!runtimeObjectCanOccupyTarget(s,*findObjectById(s,controlledId),*releasedHost)
                ||runtimeTargetCursorIntent(s,*releasedHost)!=RuntimeTargetCursorIntent::Enter
                ||!queueRuntimeOccupyTarget(s,*releasedHost))return fail(label+" release did not restore legal entry");
            // Keep the former controller from immediately recapturing its target.
            findMutableObjectById(s,sourceId)->weaponCooldownTicksRemaining=100000;
            for(int t=0;t<900&&findRuntimeObjectByIdIncludingInactive(s,controlledId)->garrisonHostObjectId<0;++t)step();
            if(findRuntimeObjectByIdIncludingInactive(s,controlledId)->garrisonHostObjectId!=releaseHostId)
                return fail(label+" released unit did not actually enter");
            ++released;
        } else {
            if(!s.specialMechanisms.mindControlLinks().at(controlledId).permanent
                ||runtimeObjectCanOccupyTarget(s,*findObjectById(s,controlledId),*findObjectById(s,releaseHostId)))
                return fail(label+" permanent control lost restriction");
            ++permanent;
        }
        ++cases;std::cout<<"Controlled garrison: "<<label<<" cursor/order/save/arrival/mixed/release passed\n";
    }
    std::cout<<"Controlled garrison: "<<cases<<" cases, "<<saves<<" full saves, "<<pending<<" arrival gates, "
        <<transactions<<" atomic neutral vetoes, "<<mixed<<" real mixed entries, "<<released
        <<" released entries, "<<permanent<<" permanent controls; passed\n";
    return true;
}
