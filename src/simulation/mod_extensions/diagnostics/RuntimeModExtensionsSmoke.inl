#include "simulation/mod_extensions/diagnostics/RuntimeModShieldBarSmoke.inl"
bool runRuntimeModExtensionsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "Mod extensions: " + message; return false; };
    IniFile fixture;
    if (!fixture.loadFromFile("tests/fixtures/mod_extensions.ini")) return fail("fixture missing");
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fail("stock resources");
    const auto make = [&]() {
        auto s=std::make_unique<RuntimeWindowState>();s->config=config;s->report=stock->report;
        s->config.computerAiEnabled=false;s->config.playerSlots.clear();s->config.shortGame=false;
        s->runtimeSoundsEnabled=false;s->runtimeSuppressPlayerAudioFeedback=true;s->runtimePreferencePersistenceEnabled=false;
        s->rules.loadFromIni(fixture);s->rulesLoaded=true;s->art=stock->art;s->artLoaded=true;
        s->economy.owner="Alpha";s->tick=10;
        s->visionEnabled=false;
        for(int y=0;y<30;++y)for(int x=0;x<30;++x){TerrainCell cell;cell.x=x;cell.y=y;cell.tileIndex=0;cell.cellIndex512=y*512+x;
            s->world.accessTerrainCells().push_back(cell);s->terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});}
        for(int id=1;id<=3;++id){WorldObjectInstance object;object.instanceId=id;object.kind=WorldObjectKind::Unit;
            object.type=id==1?"SHOOTER":id==2?"TARGET":"BOOST";object.owner=id==1?"Alpha":"Beta";
            object.health=1000;object.x=5+id*2;object.y=10;object.cell=runtimeCellKey(object.x,object.y);object.mission="Guard";
            runtimeInitializeModExtensions(object,s->rules,s->config.fixedTickHz);s->world.accessObjects().push_back(object);}
        s->terrainByCell=buildRuntimeTerrainLookup(s->world);
        return s;
    };
    auto s=make();
    {
        auto command=make();command->world.accessObjects().front().type="CASTER";
        command->world.accessObjects().resize(2);command->selectedObjectIds={1};initializeRuntimeNavigation(*command);
        issueRuntimeAttackCommand(*command,*findObjectById(*command,2));
        if(command->lastCommand.acceptedCount!=1)return fail("zero-damage effect attack command rejected");
        for(int n=0;n<100 && findObjectById(*command,2)->modExtensions.effects.empty();++n){++command->tick;advanceRuntimeSimulation(*command);}
        if(findObjectById(*command,2)->modExtensions.effects.empty())return fail("zero-damage effect command never fired");
        auto healing=make();healing->world.accessObjects().front().type="MEDIC";
        healing->world.accessObjects().resize(2);healing->selectedObjectIds={1};
        auto* ally=findMutableObjectById(*healing,2);ally->owner="Alpha";ally->modExtensions.shieldHealth=40;
        initializeRuntimeNavigation(*healing);issueRuntimeAttackCommand(*healing,*ally);
        if(healing->lastCommand.acceptedCount!=1)return fail("full-body shield repair command rejected");
        for(int n=0;n<100 && ally->modExtensions.shieldHealth==40;++n){++healing->tick;advanceRuntimeSimulation(*healing);}
        if(ally->health!=1000 || ally->modExtensions.shieldHealth<=40)return fail("shield-only repair command did not heal");
        auto normal=make();auto faster=make();
        normal->world.accessObjects().resize(1);faster->world.accessObjects().resize(1);
        runtimeAttachEffect(faster->world.accessObjects().front(),faster->rules,"BUFFWH",true,faster->config.fixedTickHz);
        normal->selectedObjectIds=faster->selectedObjectIds={1};initializeRuntimeNavigation(*normal);initializeRuntimeNavigation(*faster);
        issueRuntimeMoveCommand(*normal,{25,10});issueRuntimeMoveCommand(*faster,{25,10});
        for(int n=0;n<20;++n){++normal->tick;++faster->tick;advanceRuntimeSimulation(*normal);advanceRuntimeSimulation(*faster);}
        const auto* a=findObjectById(*normal,1);const auto* b=findObjectById(*faster,1);
        if(b->renderX<=a->renderX)return fail("speed effect did not advance real movement: normal="+std::to_string(a->x)+"/"+std::to_string(a->renderX)
            +" faster="+std::to_string(b->x)+"/"+std::to_string(b->renderX)+" commands="+std::to_string(normal->lastCommand.acceptedCount)
            +"/"+std::to_string(faster->lastCommand.acceptedCount)+" status="+normal->simulationStatus+"/"+faster->simulationStatus);
        std::cout<<"Mod extensions: actual effect-weapon, shield-repair commands and movement passed\n";
    }
    const auto hit=[&](const char* weaponId,int target=2){
        const auto* weapon=s->rules.findWeaponType(weaponId);if(!weapon)return false;
        const auto* victim=findObjectById(*s,target);
        RuntimeProjectileEffect p;p.actorId=100+s->tick;p.attackerObjectId=1;p.targetObjectId=target;p.owner="Alpha";
        p.fromCell={7,10};p.toCell={victim->x,victim->y};p.currentCell=p.toCell;p.weaponId=weaponId;
        p.warheadId=weapon->warhead;p.damage=weapon->damage;p.ag=true;p.affectsEnemies=true;p.affectsAllies=true;
        p.totalTicks=p.remainingTicks=1;p.lifecycle=RuntimeProjectileLifecycleState::Impacting;p.lifecycleState="Impacting";
        return applyRuntimeProjectileImpact(*s,p);
    };
    auto* target=findMutableObjectById(*s,2);
    if(!runRuntimeModShieldBarCheck(*s,*target,error))return fail(error);
    if(!hit("GUN") || target->health!=1000 || target->modExtensions.shieldHealth!=40)return fail("first real hit did not stop at shield: "+std::to_string(target->health)+"/"+std::to_string(target->modExtensions.shieldHealth));
    if(!hit("GUN") || target->health!=995 || target->modExtensions.shieldHealth!=0)return fail("real shield-break overflow");
    for(int n=0;n<s->config.fixedTickHz*12;++n){++s->tick;advanceRuntimeModExtensions(*s);}
    if(target->modExtensions.shieldHealth!=50)return fail("runtime respawn");
    if(!hit("HEAL") || target->modExtensions.shieldHealth!=70 || target->health!=995)return fail("real repair weapon");
    if(!hit("PIERCE") || target->modExtensions.shieldHealth!=70 || target->health!=980)return fail("real penetrating weapon");
    if(!hit("BUFF") || target->modExtensions.effects.size()!=1)return fail("zero-damage warhead failed to attach");
    const auto& gun=*s->rules.findWeaponType("GUN");
    const int buffedReload=runtimeWeaponCooldownTicks(*s,*target,gun);
    if(buffedReload<runtimeRuleFramesToTicks(gun.rof/2,s->config.fixedTickHz)
        || buffedReload>runtimeRuleFramesToTicks((gun.rof+2)/2,s->config.fixedTickHz)
        || runtimeVeterancyAbilityMultiplier(*s,*target,VeteranAbility::Faster)!=1.5
        || runtimeVeterancyAbilityMultiplier(*s,*target,VeteranAbility::Firepower)!=2
        || runtimeVeterancyAbilityMultiplier(*s,*target,VeteranAbility::Stronger)!=2)return fail("runtime stat consumers");
    target->weaponCooldownTicksRemaining=15;
    const auto active=runtimeSerializeModExtensions(target->modExtensions);
    const auto* actors=runtimeImpactRenderActorsForRender(*s);
    const auto animation=std::find_if(actors->begin(),actors->end(),[](const auto& a){return a.renderIntent=="attachEffect";});
    if(animation==actors->end() || animation->animId!="MINDANIM" || animation->targetObjectId!=2)return fail("attached animation projection");
    const auto firstActor=*animation;
    target->cloaked=true;actors=runtimeImpactRenderActorsForRender(*s);
    if(std::any_of(actors->begin(),actors->end(),[](const auto& a){return a.renderIntent=="attachEffect";}))return fail("cloaked attached effect leaked");
    target->cloaked=false;
    if(runtimeSerializeModExtensions(target->modExtensions)!=active)return fail("rendering changed extension state");
    RuntimeMapRenderContext render;render.art=&s->art;render.rules=&s->rules;render.assetRootPath=runtimeAssetRootPath(config);
    render.structureRootPath=runtimeStructureRootPath(config,stock->report.map);render.palettePath=runtimePalettePath(config,stock->report.map);
    render.structurePalettePath=runtimeStructurePalettePath(config,stock->report.map);render.fixedTickHz=s->config.fixedTickHz;
    (void)firstActor;
    RuntimeShpFrameImage pixels;std::string resolved,path,loadError;int frameCount=0;
    if(!loadRuntimeAuxiliaryShpFrameImage(render,s->renderCache,"MINDANIM",0,pixels,resolved,path,loadError,false,&frameCount)
        || frameCount<=1)return fail("original attached animation resource: "+loadError);
    if(!runRuntimeModExtensionRenderContractCheck(render,error))return fail(error);
    std::cout<<"Mod extensions: real damage/overflow/heal/penetration, attached modifiers and original animation passed\n";
    // A live reload timer must not be rescaled when the supplying effect expires.
    for(int n=0;n<s->config.fixedTickHz*2;++n){++s->tick;advanceRuntimeModExtensions(*s);}
    const int ordinaryReload=runtimeWeaponCooldownTicks(*s,*target,gun);
    if(!target->modExtensions.effects.empty() || target->weaponCooldownTicksRemaining!=15
        || ordinaryReload<runtimeRuleFramesToTicks(gun.rof,s->config.fixedTickHz)
        || ordinaryReload>runtimeRuleFramesToTicks(gun.rof+2,s->config.fixedTickHz))return fail("expiry retroactively changed reload");
    hit("BUFF");hit("STACK");
    initializeRuntimeNavigation(*s);
    std::ostringstream save;
    if(!writeRuntimeStateToStream(*s,save,error))return fail("full save: "+error);
    IniFile saved;if(!saved.loadFromString(save.str()))return fail("saved INI");
    auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,*s,false);
    if(!materializeRuntimeLoadStateFromIni(*restored,saved,error))return fail("full restore: "+error);
    for(int n=0;n<100;++n){
        const auto* a=findObjectById(*s,2);const auto* b=findObjectById(*restored,2);
        if(!b || runtimeSerializeModExtensions(a->modExtensions)!=runtimeSerializeModExtensions(b->modExtensions))return fail("restored continuation differs");
        ++s->tick;++restored->tick;advanceRuntimeModExtensions(*s);advanceRuntimeModExtensions(*restored);
    }
    std::cout<<"Mod extensions: full save and continued clocks passed\n";
    hit("BUFF");
    s->config.multiplayerActive=true;s->config.multiplayerSetupHash=1;
    s->config.multiplayerCanonicalMapId="mod-extensions";s->config.multiplayerMapContentFingerprint=2;
    s->config.multiplayerTimelineEpoch=1;s->config.simulationRandomSeed=1;
    s->config.playerSlots.resize(1);
    auto& localSlot=s->config.playerSlots.front();localSlot.enabled=true;localSlot.human=true;localSlot.local=true;
    localSlot.country="Americans";localSlot.name="Extension validation";localSlot.peerId="extension-local";
    s->economy.owner=runtimeSkirmishOwnerForSlot(0);
    for(auto& object:s->world.accessObjects())if(object.owner=="Alpha")object.owner=s->economy.owner;
    RuntimeHumanHouseAuthority local;local.owner=s->economy.owner;local.canonicalOwner=runtimeCanonicalHumanHouseOwner(local.owner);
    local.country="Americans";local.localFacade=true;s->humanHouseAuthorities={local};
    completeRuntimeWorldRefresh(*s);initializeRuntimeLocalProductionLanes(s->economy);
    s->particles.reset(s->config.fixedTickHz);s->particlesInitialized=true;
    s->superWeapons.reset(std::vector<SuperWeaponRule>{});s->superWeaponsInitialized=true;
    if(!s->particles.alignEmptyStateTick(s->tick) || !s->superWeapons.alignEmptyStateTick(s->tick))return fail("snapshot clocks");
    const auto hash=runtimeMultiplayerAuthoritativeStateHash(*s,error);
    if(!hash)return fail("snapshot authority: "+error);
    const auto before=runtimeSerializeModExtensions(findObjectById(*s,2)->modExtensions);
    --findMutableObjectById(*s,2)->modExtensions.shieldHealth;
    if(hash==runtimeMultiplayerAuthoritativeStateHash(*s,error))return fail("shield absent from authority hash");
    ++findMutableObjectById(*s,2)->modExtensions.shieldHealth;
    RuntimeMultiplayerSnapshotIdentity identity;identity.proposalId=1;identity.sourceTimelineEpoch=1;
    identity.snapshotTick=identity.barrierTick=s->tick;identity.snapshotToken="mod-extension-state";
    identity.authoritativeStateHash=hash;identity.gameplaySetupFingerprint=1;
    RuntimeMultiplayerSnapshotMemoryImage image;RuntimeMultiplayerSnapshotStagedState staged;
    if(!exportRuntimeMultiplayerSnapshotV15(*s,identity,image,error)
        || !stageRuntimeMultiplayerSnapshot(*s,image.bytes,image.descriptor,staged,error))return fail("snapshot stage: "+error);
    commitRuntimeMultiplayerSnapshotAuthority(*s,std::move(staged));
    if(runtimeMultiplayerAuthoritativeStateHash(*s,error)!=hash
        || runtimeSerializeModExtensions(findObjectById(*s,2)->modExtensions)!=before)return fail("snapshot commit lost extensions");
    std::cout<<"Mod extensions: full multiplayer snapshot and authoritative hash passed\n";
    auto powers=make();WorldObjectInstance host;host.instanceId=20;host.kind=WorldObjectKind::Structure;
    host.type="HOST";host.owner="Alpha";host.health=1000;host.x=15;host.y=15;host.cell=runtimeCellKey(15,15);
    powers->world.accessObjects().push_back(host);ensureRuntimeSuperWeaponsInitialized(*powers);
    auto context=runtimeSuperWeaponContext(*powers);context.ownerPowered=[](const std::string&){return true;};
    powers->superWeapons.tick(context);
    for(const auto* id:{"ONE","TWO","THREE","FOUR"}) if(!powers->superWeapons.findSidebarSnapshot("Alpha",id))return fail(std::string("extra superweapon missing: ")+id);
    for(int n=0;n<100;++n){++context.tick;powers->superWeapons.tick(context);}
    if(!powers->superWeapons.beginTargetSelection("Alpha","THREE",context,&error))return fail("third superweapon cannot select: "+error);
    powers->superWeapons.cancelTargetSelection("Alpha","THREE",context);
    powers->world.accessObjects().back().owner="Beta";context.tick++;powers->superWeapons.tick(context);
    if(powers->superWeapons.stateOf("Alpha","THREE")!=SuperWeaponRuntimeState::Unavailable
        || !powers->superWeapons.findSidebarSnapshot("Beta","FOUR"))return fail("capture did not transfer extra superweapons");
    powers->world.accessObjects().back().alive=false;context.tick++;powers->superWeapons.tick(context);
    if(powers->superWeapons.stateOf("Beta","FOUR")!=SuperWeaponRuntimeState::Unavailable)return fail("destroyed host retained extra superweapon");
    std::cout<<"Mod extensions: four real sidebar sources, selection, capture and loss passed\n";
    auto blast=make();
    const auto victim=blast->world.getObjects()[1];blast->world.accessObjects()={victim};
    SuperWeaponRule nuke;nuke.id="EXTNUKE";nuke.type=nuke.action="Nuke";nuke.isPowered=false;
    nuke.rechargeTimeMinutes=0.01;nuke.damage=60;nuke.warhead=nuke.payloadWarhead="NORMAL";
    blast->superWeapons.reset(std::vector<SuperWeaponRule>{nuke});blast->superWeaponsInitialized=true;
    auto blastContext=runtimeSuperWeaponContext(*blast);blastContext.mapWidth=blastContext.mapHeight=30;
    blastContext.explored=[](const std::string&,int,int){return true;};
    blastContext.cellLegal=[](int,int,SuperWeaponTargetMode){return true;};
    blast->superWeapons.grant("Alpha","EXTNUKE",-1,&blastContext);
    for(int n=0;n<100;++n){++blastContext.tick;blast->superWeapons.tick(blastContext);}
    SuperWeaponTarget destination;destination.cell={victim.x,victim.y};destination.objectId=victim.instanceId;
    if(!blast->superWeapons.beginTargetSelection("Alpha","EXTNUKE",blastContext,&error)
        || !blast->superWeapons.submitTarget("Alpha","EXTNUKE",destination,blastContext,&error))return fail("nuclear shield probe launch: "+error);
    for(int n=0;n<1000 && findObjectById(*blast,2)->modExtensions.shieldHealth==100;++n){++blastContext.tick;blast->superWeapons.tick(blastContext);}
    const auto* afterBlast=findObjectById(*blast,2);
    if(!afterBlast || afterBlast->health!=1000 || afterBlast->modExtensions.shieldHealth!=40)return fail("superweapon bypassed shield or failed to impact: "+(afterBlast?std::to_string(afterBlast->health)+"/"+std::to_string(afterBlast->modExtensions.shieldHealth):"removed"));
    std::cout<<"Mod extensions: actual nuclear superweapon also uses shield damage passed\n";
    std::cout<<"Mod extensions: passed\n";
    return true;
}
