bool runRuntimeDemoTruckDeploySmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail = [&](const std::string& why) { error="Demo truck deploy: "+scene+": "+why; return false; };
    int cases=0, saved=0;
    for (int hz : {15,30,60}) for (int mode=0; mode!=4; ++mode) {
        scene="hz="+std::to_string(hz)+" mode="+std::to_string(mode);
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);
        s.config.fixedTickHz=hz; s.config.multiplayerActive=false; s.config.computerAiEnabled=false;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.visionEnabled=false;
        s.economy.owner="Americans";
        s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear(); s.deathWeapons.reset();
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        for (int y=45;y<=90;++y) for (int x=45;x<=90;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y); cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);
        const auto add=[&](const char* type, RuntimeCellPoint cell) {
            auto* o=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,cell);
            if (!o) return -1;
            o->mission="Guard"; o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;
            return o->instanceId;
        };
        const int truck=add("DTRUCK",{64,64});
        const int second=mode==1?add("DTRUCK",{78,64}):-1;
        const int tank=add("MTNK",{84,84});
        const int target=add("MTNK",{66,64});
        if (truck<0||tank<0||target<0||(mode==1&&second<0)) return fail("native types missing");
        findMutableObjectById(s,target)->owner="Russians";
        findMutableObjectById(s,target)->health=100000;
        auto* object=findMutableObjectById(s,truck);
        if (runtimeSecondaryAbilityForObject(s,*object)!=RuntimeSecondaryAbility::Deploy
            || !runtimeObjectHasSecondaryAction(s,*object)) return fail("no deploy cursor/action");
        if (executeRuntimeSecondaryAction(s,tank)||!findObjectById(s,tank)->alive)
            return fail("ordinary vehicle gained self-destruction");
        if (mode==3) {
            std::ostringstream text;
            if (!writeRuntimeStateToStream(s,text,error)) return fail(error);
            IniFile ini; if (!ini.loadFromString(text.str())) return fail("pre-deploy save parsing");
            auto loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
            if (!materializeRuntimeLoadStateFromIni(*loaded,ini,error)) return fail(error);
            if (runtimeSecondaryAbilityForObject(*loaded,*findObjectById(*loaded,truck))!=RuntimeSecondaryAbility::Deploy
                || !dispatchRuntimeSecondaryActionFromUi(*loaded,truck)) return fail("loaded deploy failed");
            if (!loaded->deathWeapons.hasTriggered(truck)) return fail("loaded deploy did not explode");
            ++saved;
        }
        applySelection(s,mode==1?std::vector<int>{truck,second,tank}:std::vector<int>{truck},false,false);
        bool accepted=false;
        if (mode==1) accepted=dispatchRuntimeSecondaryActionForSelectionFromUi(s);
        else if (mode==2) {
            RuntimeHumanHouseAuthority authority;
            authority.owner=s.economy.owner;
            authority.canonicalOwner=runtimeCanonicalHumanHouseOwner(authority.owner);
            authority.localFacade=true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            RuntimeMultiplayerGameplayCommand command;
            command.body=RuntimeMultiplayerGameplaySecondaryActionCommand{
                RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle,{truck}};
            std::string validation;
            if (prevalidateRuntimeMultiplayerGameplayCommand(s,"Russians",command,validation))
                return fail("enemy owner was allowed to deploy");
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s,s.economy.owner,command,validation)) return fail(validation);
            accepted=applyRuntimeMultiplayerGameplayCommand(s,s.economy.owner,command);
        } else accepted=dispatchRuntimeSecondaryActionFromUi(s,truck);
        if (!accepted) return fail("UI/authority rejected deployment");
        const auto emitted=std::count_if(s.projectiles.begin(),s.projectiles.end(),[](const auto& p) {
            return p.weaponId=="Demobomb" && p.deathWeaponRequestId!=0;
        });
        if (emitted!=(mode==1?2:1)) return fail("death weapon count="+std::to_string(emitted));
        for (int id : {truck,second}) if (id>=0) {
            const auto* dead=findRuntimeObjectByIdIncludingInactive(s,id);
            if (!dead||dead->alive||dead->health!=0||!s.deathWeapons.hasTriggered(id)
                || selectionContains(s,id)||s.behaviors.contains(id)) return fail("truck not fully removed");
            if (dispatchRuntimeSecondaryActionFromUi(s,id)||executeRuntimeSecondaryAction(s,id))
                return fail("duplicate deployment accepted");
        }
        if (!findObjectById(s,tank)->alive) return fail("mixed-selection tank was destroyed");
        const auto requestEnd=s.deathWeapons.nextRequestId();
        bool blast=false, radiation=false;
        for (int step=0;step!=hz*3;++step) {
            ++s.tick; advanceRuntimeEffects(s);
            radiation=radiation||!s.specialMechanisms.radiationAreas().empty();
            blast=blast||std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& p) {
                return toLowerAsciiLocal(p.animId)=="demtexp";
            });
        }
        if (findObjectById(s,target)->health>=100000) return fail("no nearby blast damage");
        if (!radiation) return fail("no native radiation field");
        if (!blast) return fail("no DEMTEXP animation");
        if (s.deathWeapons.nextRequestId()!=requestEnd) return fail("extra death weapon emitted");
        if (mode==3) {
            std::ostringstream text;
            if (!writeRuntimeStateToStream(s,text,error)) return fail(error);
            IniFile ini; if (!ini.loadFromString(text.str())) return fail("post-deploy save parsing");
            auto loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
            if (!materializeRuntimeLoadStateFromIni(*loaded,ini,error)) return fail(error);
            if (!loaded->deathWeapons.hasTriggered(truck)||executeRuntimeSecondaryAction(*loaded,truck)
                || loaded->deathWeapons.nextRequestId()!=requestEnd) return fail("load duplicated explosion");
            ++saved;
        }
        ++cases;
    }
    std::cout<<"Demo truck deploy: passed cases="<<cases<<" saves="<<saved
        <<" UI/group/DeployToggle=explodes once; damage/radiation/DEMTEXP=present; ordinary/enemy/duplicate=rejected\n";
    return true;
}
