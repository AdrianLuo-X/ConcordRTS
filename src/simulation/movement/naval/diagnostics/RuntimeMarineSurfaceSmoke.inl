bool runRuntimeMarineSurfaceSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="marine surface: "+why;return false;};
    const char* output=std::getenv("CONCORD_MARINE_SURFACE_OUTPUT");
    const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    std::optional<TerrainCell> water;
    for(const auto& cell:live.world.getTerrainCells())
        if(runtimeCachedTerrainInfoForCell(live,cell).water){water=cell;break;}
    if(!water)return fail("original water tile missing");
    std::ofstream geometry;
    if(!root.empty())geometry.open(root+"/geometry.csv");
    if(geometry)geometry<<"type,zoom,facing,pose,bodyRise,anchorRise,muzzleRise,pixels\n";
    int pairs=0,pixelChecks=0,muzzleChecks=0,mismatches=0,cases=0,saves=0,continuations=0;
    for(const std::string type:{"SQD","DLPH","SUB","BSUB"}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.fixedTickHz=30;s.config.noCooldown=false;
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        s.visionEnabled=false;s.economy.owner="Russians";s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        for(int y=44;y<=90;++y)for(int x=44;x<=90;++x) {
            auto cell=*water;cell.x=x;cell.y=y;cell.level=0;cell.cellIndex512=y*512+x;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance body;body.instanceId=1;body.kind=WorldObjectKind::Unit;
        body.type=type;body.owner=s.economy.owner;body.alive=true;
        body.health=runtimeMaxHealthForObject(s,body);body.x=body.y=64;
        body.cell=runtimeCellKey(64,64);body.level=0;body.mission="Guard";
        body.submerged=body.cloaked=true;
        s.world.accessObjects()={body};initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope active(s);
        RuntimeMapRenderContext context;context.rules=&s.rules;context.art=&s.art;
        context.theater=s.theaterLoaded?&s.theater:nullptr;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.fixedTickHz=30;context.animationTick=8;
        const RECT viewport{0,0,960,640};
        for(int zoom:{48,60,72})for(int direction=0;direction<8;++direction)
            for(int pose=0;pose<3;++pose) {
            body.facing=direction*32;body.turretFacing=body.facing;
            body.renderMoveActive=pose==1;body.movementAnimationDistance=1.0;
            body.mission=pose==2?"Attack":pose==1?"Move":"Guard";
            body.spriteFiringStartTick=pose==2?0:-1;
            body.weaponCooldownTicksRemaining=pose==2?20:0;
            RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
            std::array<RuntimeMobileSpriteBoundsProbe,2> probes;
            std::array<POINT,2> centers{},muzzles{};
            for(int state=0;state<2;++state) {
                body.submerged=body.cloaked=state==0;s.world.accessObjects().front()=body;
                centers[state]=runtimeObjectWorldRenderPlacement(body,s.world,viewport,context,camera,s.renderCache).center;
                if(!verifyRuntimeMobileObjectSpriteBounds(body,centers[state],context,camera,s.renderCache,probes[state]))
                    return fail(type+" geometry/pixel agreement: "+probes[state].diagnostic);
                ++pixelChecks;
                if(type=="DLPH"&&!resolveRuntimeMarineWeaponMuzzlePoint(body,centers[state],context,camera,s.renderCache,muzzles[state]))
                    return fail("original dolphin sonar attachment missing");
            }
            const auto& hidden=probes[0].renderedBounds;const auto& visible=probes[1].renderedBounds;
            const int rise=hidden.top-visible.top,expected=zoom/12;
            const int muzzleRise=type=="DLPH"?muzzles[0].y-muzzles[1].y:0;
            const bool valid=rise==expected&&hidden.bottom-visible.bottom==expected
                &&hidden.left==visible.left&&hidden.right==visible.right
                &&centers[0].y-centers[1].y==expected&&centers[0].x==centers[1].x;
            if(!valid)++mismatches;
            if(type=="DLPH") {
                if(muzzleRise!=expected||muzzles[0].x!=muzzles[1].x)++mismatches;
                ++muzzleChecks;
            }
            if(geometry)geometry<<type<<','<<zoom<<','<<direction<<','<<pose<<','<<rise<<','
                <<centers[0].y-centers[1].y<<','<<muzzleRise<<','<<probes[0].renderedPixels<<'\n';
            if(zoom==60&&direction==2&&pose==0)
                std::cout<<"Marine surface geometry: "<<type<<" body rise="<<rise<<"px, expected=5px\n";
            ++pairs;
        }
    }
    for(int hz:{15,30,60})for(const std::string type:{"SQD","DLPH","SUB","BSUB"}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.config.playerCountry="Russians";s.economy.owner="Russians";
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        s.visionEnabled=false;s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        for(int y=44;y<=90;++y)for(int x=44;x<=90;++x) {
            auto cell=*water;cell.x=x;cell.y=y;cell.level=0;cell.cellIndex512=y*512+x;
            s.world.accessTerrainCells().push_back(cell);
        }
        auto* target=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,{64,64});
        if(!target)return fail(type+" missing native unit");const int id=target->instanceId;
        target->owner=s.economy.owner;target->mission="Guard";target->facing=64;
        target->buildAnimTicksTotal=target->buildAnimTicksRemaining=0;
        auto* source=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"SUB",{85,64});
        if(!source)return fail("missing torpedo attacker");const int sourceId=source->instanceId;
        source->owner="Americans";source->mission="Sleep";source->weaponCooldownTicksRemaining=100000;
        initializeRuntimeNavigation(s);RuntimeSnapshotActiveStateScope active(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        target=findMutableObjectById(s,id);target->submerged=target->cloaked=true;
        target->submergeCooldownTicks=target->cloakCooldownTicks=0;
        const int health=target->health;
        const auto hidden=[&](RuntimeWindowState& q){const auto* unit=findObjectById(q,id);
            return unit&&unit->submerged&&unit->cloaked&&!runtimeObjectVisibleToOwner(q,"Americans",*unit);};
        const auto revealed=[&](RuntimeWindowState& q){const auto* unit=findObjectById(q,id);
            return unit&&!unit->submerged&&!unit->cloaked&&runtimeObjectVisibleToOwner(q,"Americans",*unit);};
        const auto capture=[&](const std::string& name) {
            if(hz!=30||root.empty())return true;
            s.selectedObjectIds={id};s.mouseClientPointValid=false;
            return drawRuntimeStateOffscreenAtCell(s,{64,64},800,600,error,
                root+"/"+type+"-"+name+".bmp",false,true,-1,nullptr,true).has_value();
        };
        if(!hidden(s)||!capture("hidden"))return fail(type+" hidden fixture/capture: "+error);
        const auto* weapon=s.rules.findWeaponType("SubTorpedo");RuntimeWeaponProfile profile;
        if(!weapon)return fail("native torpedo missing");
        finalizeRuntimeWeaponProfile(profile,s,RuntimeWeaponSlot::Primary,*weapon,"marineReveal");
        spawnRuntimeProjectile(s,*findObjectById(s,sourceId),*findObjectById(s,id),*weapon,profile,weapon->damage);
        if(s.projectiles.empty())return fail("actual torpedo missing");
        const auto shot=s.projectiles.back();s.projectiles.clear();
        if(!applyRuntimeProjectileImpact(s,shot)||!revealed(s)||findObjectById(s,id)->health>=health)
            return fail(type+" actual damage did not reveal");
        if(!capture("revealed"))return fail(type+" reveal capture: "+error);
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
            ||!materializeRuntimeLoadStateFromIni(*restored,ini,error)||!revealed(*restored))
            return fail(type+" visible full reload: "+error);
        ++saves;
        bool concealed=false;
        for(int tick=0;tick<hz*7;++tick) {
            for(auto* q:{&s,restored.get()}) {
                RuntimeSnapshotActiveStateScope scope(*q);++q->tick;advanceRuntimeEffects(*q);
            }
            const auto* first=findObjectById(s,id);const auto* second=findObjectById(*restored,id);
            if(!first||!second||first->cloaked!=second->cloaked||first->submerged!=second->submerged
                ||first->cloakCooldownTicks!=second->cloakCooldownTicks
                ||first->submergeCooldownTicks!=second->submergeCooldownTicks
                ||first->x!=64||first->y!=64||first->level!=0)
                return fail(type+" restore clocks or physical location changed");
            ++continuations;
            if(hidden(s)){concealed=true;break;}
        }
        if(!concealed||!capture("recovered"))return fail(type+" did not resume concealment: "+error);
        ++cases;std::cout<<"Marine surface damage: "<<type<<" hz="<<hz<<" real hit/reveal/reload/recovery passed\n";
    }
    std::cout<<"Marine surface: "<<pairs<<" geometry pairs, "<<pixelChecks<<" exact pixel bounds, "
        <<muzzleChecks<<" sonar attachments, "<<cases<<" damage cases, "<<saves<<" saves, "
        <<continuations<<" restored clocks; mismatches="<<mismatches<<'\n';
    return mismatches==0?true:fail("excessive visual rise: "+std::to_string(mismatches));
}
