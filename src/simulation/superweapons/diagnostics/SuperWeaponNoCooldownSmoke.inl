namespace {
bool smokeSuperWeaponNoCooldown() {
    const auto fail=[](const std::string& message){
        std::cerr<<"no-cooldown superweapon: "<<message<<'\n';return false;};
    // Accept another command while the previous payload is still queued,
    // then prove that every accepted target survives launch and save/load.
    for(int hz:{15,30,60}) {
        SuperWeaponRule rule;rule.id="REPEAT";rule.type=rule.action="PsychicReveal";
        rule.targetMode="Area";rule.rechargeTimeMinutes=10;rule.durationFrames=30;
        SuperWeaponSystem system;system.reset(std::vector<SuperWeaponRule>{rule});
        SuperWeaponWorldContext context;context.fixedTickHz=hz;
        context.mapWidth=context.mapHeight=64;
        context.cellLegal=[](int,int,auto){return true;};
        context.noCooldown=[](const auto&){return true;};
        std::vector<SuperWeaponEvent> events;
        context.emit=[&](const auto& event){events.push_back(event);};
        system.grant("Player",rule.id,-1,&context);system.tick(context);
        for(int use=0;use<3;++use) {
            std::string reason;
            if(!system.beginTargetSelection("Player",rule.id,context,&reason)
                || !system.submitTarget("Player",rule.id,{{20+use*10,20},-1},context,&reason))
                return fail("overlap hz="+std::to_string(hz)+" cast="+std::to_string(use+1)+": "+reason);
        }
        const auto roundTrip=[&]() {
            std::ostringstream saved;system.save(saved);IniFile ini;
            SuperWeaponSystem restored;restored.reset(std::vector<SuperWeaponRule>{rule});
            if(!ini.loadFromString(saved.str()) || !restored.load(ini)
                || restored.stateHash()!=system.stateHash())return false;
            system=std::move(restored);return true;
        };
        if(!roundTrip())return fail("queued overlapping save");
        ++context.tick;system.tick(context);
        std::set<int> castIds, targets;
        for(const auto& event:events)if(event.kind=="SuperWeaponLaunched") {
            castIds.insert(event.instanceId);targets.insert(event.targetCell.x);
        }
        if(castIds.size()!=3 || targets!=std::set<int>{20,30,40}
            || system.sidebarSnapshots().size()!=1
            || system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready
            || system.renderSnapshots().size()!=3 || !roundTrip())
            return fail("overlapping effects/one sidebar/active save");
        std::ostringstream active;system.save(active);
        for(const auto& replacement:std::vector<std::pair<std::string,std::string>>{
            {"EffectOnly=yes","EffectOnly=no"},{"EffectOnly=yes","EffectOnly=invalid"},
            {"OneShot=no","OneShot=yes"},{"State=Active","State=Ready"},
            {"Owner=Player","Owner=Orphan"}}) {
            std::string invalid=active.str();const auto position=invalid.find(replacement.first);
            if(position==std::string::npos)return fail("malformed-save fixture");
            invalid.replace(position,replacement.first.size(),replacement.second);
            IniFile bad;const auto hash=system.stateHash();
            if(!bad.loadFromString(invalid) || system.load(bad) || system.stateHash()!=hash)
                return fail("detached cast malformed save was accepted or changed live state");
        }
        for(int i=0;i<4*hz;++i){++context.tick;system.tick(context);}
        const auto ended=std::count_if(events.begin(),events.end(),[](const auto& event){
            return event.kind=="SuperWeaponEffectEnded";});
        std::ostringstream finished;system.save(finished);IniFile finishedIni;
        if(ended!=3 || !system.renderSnapshots().empty() || !roundTrip()
            || !finishedIni.loadFromString(finished.str())
            || finishedIni.getValue("SuperWeapons","Count")!="1")
            return fail("overlapping completion/retired cast cleanup");
    }
    // Enabling the option during an already running storm (including an old
    // save) preserves its token; later cancellation must close every token.
    {
        SuperWeaponRule rule;rule.id="TOGGLE";rule.type=rule.action="LightningStorm";
        rule.targetMode="Area";rule.rechargeTimeMinutes=10;rule.durationFrames=120;
        bool instant=false;SuperWeaponWorldContext context;context.mapWidth=context.mapHeight=64;
        context.cellLegal=[](int,int,auto){return true;};context.noCooldown=[&](const auto&){return instant;};
        std::vector<SuperWeaponActiveLifecycleEvent> lifecycles;
        context.onSuperWeaponActiveLifecycle=[&](auto event){lifecycles.push_back(event);};
        SuperWeaponSystem system;system.reset(std::vector<SuperWeaponRule>{rule});
        system.forceReady("Player",rule.id,&context);
        if(!system.beginTargetSelection("Player",rule.id,context)
            || !system.submitTarget("Player",rule.id,{{20,20},-1},context))return fail("toggle initial launch");
        ++context.tick;system.tick(context);
        const auto token=system.activeLifecycleSnapshots().front().activeToken;
        std::ostringstream saved;system.save(saved);std::string legacy=saved.str();
        legacy.replace(legacy.find("Version=8"),9,"Version=7");IniFile ini;
        if(!ini.loadFromString(legacy) || !system.load(ini))return fail("v7 active migration");
        instant=true;++context.tick;system.tick(context);
        if(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready
            || system.activeLifecycleSnapshots().size()!=1
            || system.activeLifecycleSnapshots().front().activeToken!=token)return fail("toggle interrupted old cast");
        for(int use=0;use<2;++use) {
            if(!system.beginTargetSelection("Player",rule.id,context)
                || !system.submitTarget("Player",rule.id,{{30+use*10,20},-1},context))return fail("toggle repeat");
        }
        ++context.tick;system.tick(context);
        if(system.activeLifecycleSnapshots().size()!=3)return fail("parallel lifecycle tokens");
        if(!system.disable("Player",rule.id,&context) || system.beginTargetSelection("Player",rule.id,context)
            || !system.activeLifecycleSnapshots().empty())return fail("disable left an active cast");
        std::set<int> started,ended;
        for(const auto& event:lifecycles)
            (event.phase==SuperWeaponActiveLifecyclePhase::Started?started:ended).insert(event.activeToken);
        if(started.size()!=3 || started!=ended)return fail("lifecycle cancellation was not paired");
        ++context.tick;system.tick(context);
        if(!system.remove("Player",rule.id,&context))return fail("remove reusable grant");
        ++context.tick;system.tick(context);
        if(!system.sidebarSnapshots().empty() || !system.renderSnapshots().empty())return fail("removed cast resurrected");
    }
    for(int hz:{15,30,60}) {
        SuperWeaponRule rule;rule.id="INSTANT";rule.type=rule.action="PsychicReveal";
        rule.targetMode="Area";rule.rechargeTimeMinutes=10;rule.isPowered=true;
        rule.durationFrames=15;rule.defermentFrames=1;
        SuperWeaponSystem system;system.reset(std::vector<SuperWeaponRule>{rule});
        bool powered=true,noCooldown=true;
        SuperWeaponWorldContext context;context.fixedTickHz=hz;context.mapWidth=context.mapHeight=64;
        context.cellLegal=[](int,int,auto){return true;};
        context.ownerPowered=[&](const auto&){return powered;};
        context.noCooldown=[&](const auto& owner){return noCooldown && (owner=="Player" || owner=="OneShot");};
        system.grant("Player",rule.id,-1,&context);system.grant("AI",rule.id,-1,&context);
        system.grantOneShot("OneShot",rule.id,&context);system.tick(context);
        if(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready
            || system.stateOf("AI",rule.id)!=SuperWeaponRuntimeState::Charging
            || system.findSidebarSnapshot("AI",rule.id)->rechargeTicks!=600*hz) return fail("owner scope/timer");
        for(int use=0;use<3;++use) {
            if(!system.beginTargetSelection("Player",rule.id,context)
                || !system.submitTarget("Player",rule.id,{{20,20},-1},context)) return fail("repeat use");
            int ticks=0;
            while(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready && ticks<5*hz) {
                ++ticks;++context.tick;system.tick(context);
                if(ticks==1) {
                    const auto* snapshot=system.findSidebarSnapshot("Player",rule.id);
                    if(snapshot->remainingTicks!=0) return fail("charge did not finish during effect");
                    std::ostringstream saved;system.save(saved);IniFile ini;
                    SuperWeaponSystem restored;restored.reset(std::vector<SuperWeaponRule>{rule});
                    if(!ini.loadFromString(saved.str()) || !restored.load(ini)
                        || restored.stateHash()!=system.stateHash()) return fail("active save");
                    system=std::move(restored);
                }
            }
            if(ticks>=5*hz) return fail("extra cooldown after effect");
        }
        if(!system.beginTargetSelection("OneShot",rule.id,context)
            || !system.submitTarget("OneShot",rule.id,{{20,20},-1},context)) return fail("one-shot launch");
        for(int i=0;i<5*hz;++i){++context.tick;system.tick(context);}
        if(system.beginTargetSelection("OneShot",rule.id,context)) return fail("one-shot grant became infinite");
        system.resetCharge("Player",rule.id,&context);powered=false;
        ++context.tick;system.tick(context);
        if(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready)
            return fail("no-cooldown power bypass");
        noCooldown=false;system.resetCharge("Player",rule.id,&context);
        ++context.tick;system.tick(context);
        if(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Suspended) return fail("power gate");
        powered=true;noCooldown=true;
        for(int i=0;i<2;++i){++context.tick;system.tick(context);}
        if(system.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Ready) return fail("power recovery");
        context.superWeaponsAllowed=false;rule.disableableFromShell=true;
        SuperWeaponSystem disabled;disabled.reset(std::vector<SuperWeaponRule>{rule});
        disabled.grant("Player",rule.id,-1,&context);disabled.tick(context);
        if(disabled.stateOf("Player",rule.id)!=SuperWeaponRuntimeState::Unavailable) return fail("shell gate");
    }
    std::cout<<"No cooldown superweapons: queued/active overlap at three rates, 15 malformed-save rejections, v7 toggle, lifecycle cancellation/cleanup, player/AI, one-shot, no-cooldown power bypass and normal power gate passed\n";
    return true;
}
}
