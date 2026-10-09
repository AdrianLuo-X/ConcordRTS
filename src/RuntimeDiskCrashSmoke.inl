bool runRuntimeDiskCrashSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="disk crash: "+why;return false;};
    int cases=0, saves=0;
    for (int hz : {15,30,60}) {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        s.config=live.config; s.config.fixedTickHz=hz; s.config.computerAiEnabled=false;
        s.config.playerSlots.clear(); s.config.multiplayerActive=false;
        s.report=live.report; s.art=live.art; s.theater=live.theater;
        IniFile rules; if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules)) return fail("stock rules");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.world=live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner="YuriCountry"; s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true; s.visionEnabled=false;
        s.runtimeSoundsEnabled=false; s.runtimeSoundEffectsEnabled=true; s.runtimeSoundVolume=100;
        s.runtimeSuppressPlayerAudioFeedback=false; s.runtimePreferencePersistenceEnabled=false;
        for(int y=1;y<=24;++y) for(int x=1;x<=24;++x) {
            TerrainCell cell; cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell); s.terrainInfoByCell.emplace(cell.cellIndex512,RuntimeTerrainInfo{});
        }
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);
        WorldObjectInstance disk; disk.instanceId=1;disk.type="DISK";disk.kind=WorldObjectKind::Unit;
        disk.owner="YuriCountry";disk.alive=true;disk.mission="Guard";disk.currentAmmo=-1;
        disk.x=disk.y=10;disk.cell=runtimeCellKey(10,10);disk.airborne=true;disk.flightLevel=disk.level=4;
        disk.health=runtimeMaxHealthForObject(s,disk);
        auto target=disk;target.instanceId=2;target.type="E1";target.kind=WorldObjectKind::Infantry;
        target.owner="Americans";target.airborne=false;target.flightLevel=target.level=0;target.mission="Sleep";
        target.health=10000;target.weaponCooldownTicksRemaining=100000;
        s.world.accessObjects()={disk,target}; initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope active(s);
        if(!loadRuntimeSoundDatabase(s)) return fail("native sounds");
        const auto countSound=[&](const std::string& name){return std::count_if(s.runtimeSoundPlaybackHistory.begin(),s.runtimeSoundPlaybackHistory.end(),
            [&](const auto& record){return record.section==name;});};
        RuntimeObjectExitRequest death;death.objectId=1;death.kind=RuntimeObjectExitKind::Destroyed;
        death.tick=s.tick;death.suppressCrew=true;
        if(!finalizeRuntimeObjectExit(s,death).accepted || s.wrecks.size()!=1)
            return fail("retained hull/native death payload hz="+std::to_string(hz)+" wrecks="+std::to_string(s.wrecks.size())
                +" projectiles="+std::to_string(s.projectiles.size()));
        const auto deathPayload=std::find_if(s.projectiles.begin(),s.projectiles.end(),runtimeProjectileIsCrashDeathDetonation);
        if(deathPayload==s.projectiles.end() || deathPayload->damage!=25)
            return fail("native death payload missing or damage changed");
        const int payload=deathPayload->actorId;
        const auto bombVisible=[&](const auto* actors){return actors && std::any_of(actors->begin(),actors->end(),
            [&](const auto& actor){return actor.actorId==payload || toLowerAsciiLocal(actor.projectileImageId)=="zbomb";});};
        const auto* visible=runtimeProjectileRenderActorsForRender(s);
        if(bombVisible(visible) || countSound("KirovAttack")!=0 || countSound("FloatingDiscDie")!=1)
            return fail("extra bomb sprite/sound or missing native crashing sound");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
            ||!materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("full airborne reload: "+error);
        visible=runtimeProjectileRenderActorsForRender(*restored);
        if(bombVisible(visible) || restored->wrecks.size()!=1) return fail("reload exposed bomb or lost wreck");
        ++saves;
        double height=s.wrecks.front().startLevel;int descending=0;bool impact=false;
        for(int tick=0;tick<hz*12;++tick) {
            ++s.tick;advanceRuntimeEffects(s);
            {RuntimeSnapshotActiveStateScope restoredActive(*restored);++restored->tick;advanceRuntimeEffects(*restored);}
            if(!s.wrecks.empty()) {
                const auto pose=runtimeWreckObjectPose(s.wrecks.front(),s.tick,hz);
                if(pose.renderLevel<height)++descending;
                if(pose.renderLevel>height+1e-8)return fail("wreck moved upwards");height=pose.renderLevel;
            }
            for(const auto& effect:s.impactEffects) if(effect.lifecycleState=="WreckImpact") impact=true;
            visible=runtimeProjectileRenderActorsForRender(s);
            if(visible&&std::any_of(visible->begin(),visible->end(),[&](const auto& actor){return actor.actorId==payload;}))
                return fail("hidden crash bomb became visible in flight");
            const auto* victim=findObjectById(s,2);const auto* copy=findObjectById(*restored,2);
            if(!victim||!copy||victim->health!=copy->health||s.wrecks.size()!=restored->wrecks.size())return fail("reload damage/wreck continuation");
        }
        if(!s.wrecks.empty() || !s.projectiles.empty() || descending<3 || !impact
            || findObjectById(s,2)->health>=target.health || countSound("KirovAttack")!=0)
            return fail("fall/impact/damage completion hz="+std::to_string(hz)+" descending="+std::to_string(descending)
                +" impact="+std::to_string(impact)+" health="+std::to_string(findObjectById(s,2)->health));
        // The same bomb art and report remain available to a normal Kirov attack.
        auto bomber=disk;bomber.instanceId=3;bomber.type="ZEP";bomber.x=12;bomber.cell=runtimeCellKey(12,10);
        s.world.accessObjects().push_back(bomber);
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,3),findObjectById(s,2));
        if(!profile.weapon)return fail("normal Kirov weapon");
        spawnRuntimeProjectile(s,*findObjectById(s,3),*findObjectById(s,2),*profile.weapon,profile,profile.weapon->damage);
        visible=runtimeProjectileRenderActorsForRender(s);
        if(!visible||visible->size()!=1||s.projectiles.back().projectileImageId!="ZBOMB"
            ||runtimeProjectileIsCrashDeathDetonation(s.projectiles.back())||countSound("KirovAttack")!=1)
            return fail("normal Kirov bomb/report was suppressed");
        ++cases;
    }
    std::cout<<"Disk crash passed: "<<cases<<" native falls, "<<saves
        <<" full reloads, native crash sound/ground damage retained, no extra bomb/report, normal Kirov preserved\n";
    return true;
}
