bool runRuntimeChaosTargetingSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Chaos targeting: "+why;return false;};
    IniFile gasDefaults;
    auto parsed=std::make_unique<RulesDatabase>();
    if(!gasDefaults.loadFromString("[Warheads]\n0=GasDefault\n1=GasNoAllies\n2=Ordinary\n"
        "[GasDefault]\nPsychedelic=yes\n[GasNoAllies]\nPsychedelic=yes\nAffectsAllies=no\n[Ordinary]\n")
        ||!parsed->loadFromIni(gasDefaults))return fail("gas rule fixture");
    for(const std::string id:{"GasDefault","GasNoAllies","Ordinary"}) {
        const auto* gas=parsed->findWarheadType(id);
        if(!gas||gas->affectsAllies!=(id=="GasDefault")||gas->profile.affectsAllies!=gas->affectsAllies)
            return fail("gas default or explicit AffectsAllies override: "+id);
    }
    int cases=0,saves=0,retargets=0,expiries=0,protectedDrones=0;
    for(int hz:{15,30,60}) for(const std::string actorType:{"E1","MTNK"})
    for(const std::string actorOwner:{"YuriCountry","Russians","Neutral"})
    for(const std::string relation:{"own","ally","enemy"}) {
        const std::string label=actorType+"-"+actorOwner+"-"+relation+"-"+std::to_string(hz);
        std::cout<<"Chaos targeting: "<<label<<" begin\n";
        try {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.config.playerCountry="YuriCountry";s.config.gameMode="BATTLE";s.economy.owner="YuriCountry";
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimeSoundsEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.world.accessTerrainCells().clear();s.visionEnabled=false;
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;
        const std::string nearOwner=relation=="own"?actorOwner:relation=="ally"?"Americans":"French";
        s.skirmishTeamByOwner={{"yuricountry","9"},{"alliance","3"},{"french","2"},{"americans","1"}};
        s.skirmishTeamByOwner[toLowerAsciiLocal(actorOwner)]="1";
        // Exercise authored houses and alliances without skirmish slot victory
        // settlement: this small battle fixture intentionally has no bases.
        s.skirmishCountryByOwner.clear();s.config.shortGame=false;
        if(runtimeOwnersAreAllied(s,actorOwner,nearOwner)!=(relation!="enemy"))
            return fail(label+" fixture relation mismatch");
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add=[&](RuntimeBuildKind kind,const std::string& type,const std::string& owner,RuntimeCellPoint cell) {
            auto* o=addRuntimeObjectInstance(s,kind,type,cell);if(!o)return -1;
            o->owner=owner;o->mission="Sleep";o->weaponCooldownTicksRemaining=100000;
            o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;clearRuntimeRenderPosition(*o);return o->instanceId;
        };
        const int sourceId=add(RuntimeBuildKind::Unit,"CAOS","YuriCountry",{60,64});
        const int actorId=add(actorType=="E1"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,actorType,actorOwner,{62,64});
        const int nearId=add(RuntimeBuildKind::Unit,"MTNK",nearOwner,{62,66});
        const int farId=add(RuntimeBuildKind::Unit,"MTNK","Alliance",{65,64});
        if(sourceId<0||actorId<0||nearId<0||farId<0)return fail(label+" original fixture");
        initializeRuntimeNavigation(s);ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeSnapshotActiveStateScope active(s);
        auto* source=findMutableObjectById(s,sourceId);source->deployed=true;source->mission="Deployed";
        const auto profile=runtimeWeaponProfileForObject(s,*source,source);
        if(!profile.weapon||!profile.weapon->areaFire||!profile.warhead||!profile.warhead->psychedelic)
            return fail(label+" original deployed gas weapon");
        const int originalHealth=findObjectById(s,actorId)->health;
        spawnRuntimeProjectile(s,*source,*source,*profile.weapon,profile,profile.weapon->damage);
        if(s.projectiles.size()!=1)return fail(label+" original gas projectile");
        const auto shot=s.projectiles.front();s.projectiles.clear();
        if(!applyRuntimeProjectileImpact(s,shot)||!s.psychicStatuses.findByTarget(actorId)
            ||findObjectById(s,actorId)->health!=originalHealth)
            return fail(label+" gas did not affect actor without HP damage");
        const int deadline=s.psychicStatuses.findByTarget(actorId)->expiresTick;
        if(deadline-s.tick!=40*hz)return fail(label+" original gas duration");
        findMutableObjectById(s,actorId)->weaponCooldownTicksRemaining=0;
        const auto step=[](RuntimeWindowState& p,int ticks=1) {
            RuntimeSnapshotActiveStateScope scope(p);
            for(int t=0;t<ticks;++t){++p.tick;advanceRuntimeEffects(p);advanceRuntimeSimulation(p);}
        };
        int chosen=-1;
        for(int t=0;t<hz*2&&chosen<0;++t) {
            step(s);const auto it=s.behaviors.find(actorId);
            if(it!=s.behaviors.end()&&it->second.kind==RuntimeBehaviorKind::Attack)chosen=it->second.targetObjectId;
        }
        if(chosen!=nearId)return fail(label+" nearest unit was ignored; chosen="+std::to_string(chosen)
            +" expected="+std::to_string(nearId)+" ordinary unit BerserkFriendly=no");
        if(runtimePsychicCanAttackTarget(s,*findObjectById(s,actorId),*findObjectById(s,sourceId),true,true))
            return fail(label+" BerserkFriendly drone lost native target protection");
        ++protectedDrones;
        const int nearHealth=findObjectById(s,nearId)->health;
        for(int t=0;t<hz*6&&findObjectById(s,nearId)->health==nearHealth;++t)step(s);
        if(!findObjectById(s,nearId)||findObjectById(s,nearId)->health>=nearHealth
            ||s.behaviors.at(actorId).forceFire)return fail(label+" autonomous shot did not damage nearest unit");

        std::ostringstream saved;if(!writeRuntimeStateToStream(s,saved,error))return fail(label+" save: "+error);
        IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error))
            return fail(label+" restore: "+error);
        for(int t=0;t<hz;++t) {
            step(s);step(*restored);
            if(s.psychicStatuses.stateHash()!=restored->psychicStatuses.stateHash()
                ||findObjectById(s,nearId)->health!=findObjectById(*restored,nearId)->health
                ||s.behaviors.at(actorId).targetObjectId!=restored->behaviors.at(actorId).targetObjectId)
                return fail(label+" save continuation diverged");
        }
        ++saves;
        auto* nearUnit=findMutableObjectById(s,nearId);nearUnit->alive=false;nearUnit->health=0;
        eraseRuntimeBehavior(s,nearId);refreshRuntimeObjectOccupancyAfterLocalChange(s);
        bool reacquired=false;const int farHealth=findObjectById(s,farId)->health;
        for(int t=0;t<hz*8;++t) {
            step(s);const auto it=s.behaviors.find(actorId);
            reacquired=reacquired||(it!=s.behaviors.end()&&it->second.targetObjectId==farId);
            if(findObjectById(s,farId)->health<farHealth)break;
        }
        if(!reacquired||findObjectById(s,farId)->health>=farHealth)return fail(label+" did not acquire and damage next nearby enemy");
        ++retargets;

        // Keep a restored active attack on the original target through expiry.
        // Old in-flight shots are already committed, so inspect new intents.
        restored->tick=deadline;advanceRuntimePsychicStatuses(*restored);
        if(restored->psychicStatuses.findByTarget(actorId))return fail(label+" status did not expire");
        if(runtimeOwnersAreAllied(*restored,actorOwner,nearOwner)) {
            const int firstNewShot=restored->nextProjectileActorId;
            const int healthAtExpiry=findObjectById(*restored,nearId)->health;
            const bool committedShot=std::any_of(restored->projectiles.begin(),restored->projectiles.end(),
                [&](const auto& projectile){return projectile.attackerObjectId==actorId&&projectile.targetObjectId==nearId;});
            // A tank can keep its completed attack record while returning its
            // turret. Require no new allied shots throughout that transition.
            for(int t=0;t<hz*8;++t) {
                step(*restored);
                for(const auto& projectile:restored->projectiles)
                    if(projectile.actorId>=firstNewShot&&projectile.attackerObjectId==actorId&&projectile.targetObjectId==nearId)
                        return fail(label+" fired a new allied shot after expiry");
            }
            if(!committedShot&&findObjectById(*restored,nearId)->health!=healthAtExpiry)
                return fail(label+" damaged its ally after expiry");
            const auto it=restored->behaviors.find(actorId);
            if(it!=restored->behaviors.end()&&it->second.kind==RuntimeBehaviorKind::Attack&&it->second.targetObjectId==nearId)
                return fail(label+" continued an invalid attack after expiry; auto="+std::to_string(it->second.autoAcquired)
                    +" state="+std::to_string(static_cast<int>(it->second.state))
                    +" actualOwner="+findObjectById(*restored,actorId)->owner
                    +" targetOwner="+findObjectById(*restored,nearId)->owner);
        }
        ++expiries;++cases;
        std::cout<<"Chaos targeting: "<<label<<" gas/nearest/damage/save/retarget/expiry passed\n";
        } catch(const std::exception& exception) {
            return fail(label+" unexpected fixture state: "+exception.what());
        }
    }
    std::cout<<"Chaos targeting: "<<cases<<" native cases, "<<saves<<" full saves, "<<retargets
        <<" enemy reacquisitions, "<<expiries<<" expiries, "<<protectedDrones<<" protected drones; passed\n";
    return true;
}
