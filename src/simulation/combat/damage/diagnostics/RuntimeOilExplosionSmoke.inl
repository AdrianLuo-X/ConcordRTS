// Retail death weapons detonate without the firing-only WeaponType Anim.
bool runRuntimeOilExplosionSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* output = std::getenv("CONCORD_OIL_EXPLOSION_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    std::string scene;
    const auto fail = [&](const std::string& why) { error = "oil explosion: " + scene + ": " + why; return false; };
    const auto* oil = live.rules.findBuildingType("CAOILD");
    const auto* weapon = live.rules.findWeaponType("OilExplosion");
    const auto* warhead = live.rules.findWarheadType("OilExplosionWH");
    if (!oil || !weapon || !warhead || oil->deathWeapon != "OilExplosion"
        || weapon->anim != "RING1" || weapon->damage != 600 || warhead->cellSpread != 4
        || oil->explosion != "TWLT070,S_BANG48,S_BRNL58,S_CLSN58,S_TUMU60" || !oil->leaveRubble)
        return fail("retail oil explosion declarations changed");
    const auto nativeAnims = splitRuntimeTokenList(oil->explosion);
    int cases=0, saves=0, continuationChecks=0, captures=0, blasts=0;
    const auto clone = [&](RuntimeWindowState& s) -> std::unique_ptr<RuntimeWindowState> {
        std::ostringstream out; if (!writeRuntimeStateToStream(s,out,error)) return nullptr;
        IniFile ini; if (!ini.loadFromString(out.str())) return nullptr;
        auto restored=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,s,false);
        if (!materializeRuntimeLoadStateFromIni(*restored,ini,error)) return nullptr;
        ++saves; return restored;
    };
    const auto extraMuzzle = [](const RuntimeWindowState& s) {
        return std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx) {
            return fx.weaponId=="OilExplosion" && (fx.animId=="RING1" || fx.lifecycleState=="Muzzle");
        });
    };
    const auto health = [](const RuntimeWindowState& s,int id) {
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);return o?o->health:-1;
    };
    const auto kill = [&](RuntimeWindowState& s,int id,int mode) {
        if (mode==0) {
            const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o)return false;
            const auto center=runtimeProjectileTargetPosition(s,*o);
            RuntimeProjectileEffect hit;hit.actorId=nextRuntimeProjectileActorId(s);
            hit.targetObjectId=id;hit.owner=o->owner=="Americans"?"Russians":"Americans";
            hit.toCell=center.cell;hit.toLevel=center.level;hit.currentCell=center.cell;
            hit.damage=20000;hit.warheadId="AP";hit.impactAnimId="PIFF";
            hit.ag=true;hit.affectsEnemies=hit.affectsAllies=true;
            return applyRuntimeProjectileImpact(s,hit);
        }
        RuntimeObjectExitRequest request;request.objectId=id;request.tick=s.tick;request.suppressCrew=true;
        request.kind=mode==3?RuntimeObjectExitKind::Sold:mode==4?RuntimeObjectExitKind::TemporalDestroyed
            :RuntimeObjectExitKind::Destroyed;
        request.suppressDeathWeapon=mode==2;request.reason="oil explosion regression";
        return queueRuntimeObjectExit(s,request)==RuntimeObjectExitQueueResult::Queued
            && drainRuntimeObjectExitQueue(s);
    };
    for (int hz : {15,30,60}) for (int mode=0;mode<5;++mode)
        for (const std::string owner : {"Neutral","Russians","Americans"}) {
        scene="hz="+std::to_string(hz)+" mode="+std::to_string(mode)+" owner="+owner;
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        seedRuntimeLoadScratch(s,live,false);
        s.tick=100;s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;
        s.config.multiplayerActive=false;s.config.gameMode="Battle";s.campaignMode=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();
        s.impactEffects.clear();s.deathWeapons.reset();
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.world.accessTerrainCells().clear();
        s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=45;y<=85;++y)for(int x=45;x<=85;++x){
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        auto* derrick=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"CAOILD",{64,64});
        if(!derrick)return fail("stock oil derrick missing");
        derrick->owner=owner;derrick->mission="Guard";
        derrick->buildAnimTicksTotal=derrick->buildAnimTicksRemaining=0;
        const int id=derrick->instanceId;
        const auto center=runtimeProjectileTargetPosition(s,*derrick);
        auto* tank=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{66,67});
        if(!tank)return fail("stock victim missing");
        tank->owner=owner=="Americans"?"Russians":"Americans";tank->mission="Guard";
        const int victim=tank->instanceId, initialHealth=tank->health;
        const bool pictured=hz==30 && mode==0 && owner=="Neutral" && !root.empty();
        const auto capture=[&](const std::string& stage){
            if(!pictured)return true;
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{65,65},800,600,error,
                root+"/oil-"+stage+".bmp",false,false);
            ++captures;return stats && stats->rendered && stats->fallbackTerrainCells==0;
        };
        if(!capture("before"))return fail(error);
        std::unique_ptr<RuntimeWindowState> pre;
        if(mode<2){pre=clone(s);if(!pre)return fail("pre-death save: "+error);}
        if(!kill(s,id,mode) || extraMuzzle(s))return fail("death created a firing-only blue RING1");
        const auto countPayload=[](const RuntimeWindowState& state){
            return std::count_if(state.projectiles.begin(),state.projectiles.end(),[](const auto& p){
                return p.weaponId=="OilExplosion" && p.deathWeaponRequestId>0 && p.damage==600;
            });
        };
        if(countPayload(s)!=(mode<2?1:0))return fail("death payload missing or suppressed exit exploded");
        if(mode<3){
            const auto fx=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& a){
                return a.targetObjectId==id && a.lifecycleState=="BuildingDestruction";
            });
            if(fx==s.impactEffects.end() || std::find(nativeAnims.begin(),nativeAnims.end(),fx->animId)==nativeAnims.end()
                || !fx->hasWorldPosition || fx->worldX!=center.x || fx->worldY!=center.y
                || fx->worldLevel!=center.level)return fail("native building blast missing or displaced");
            ++blasts;
            if(pictured)std::cout<<"Native oil blast="<<fx->animId<<" center="<<fx->worldX<<','<<fx->worldY<<'\n';
        }
        if(!capture("birth"))return fail(error);
        std::unique_ptr<RuntimeWindowState> post;
        if(mode<2){
            if(!kill(*pre,id,mode) || extraMuzzle(*pre))return fail("pre-death reload recreated blue ring");
            post=clone(s);if(!post)return fail("post-death save: "+error);
        }
        const auto compare=[&](const RuntimeWindowState& other,bool compareVisuals){
            // Ordinary saves deliberately omit transient building flashes.
            // A pre-death save must reproduce the same newly-created blast;
            // a post-death save must keep damage and omit the invalid ring.
            if(extraMuzzle(other) || health(s,victim)!=health(other,victim)
                || (compareVisuals && s.impactEffects.size()!=other.impactEffects.size())) {
                std::cout<<"Continuation count/health mismatch tick="<<s.tick<<" live="<<s.impactEffects.size()
                    <<" saved="<<other.impactEffects.size()<<" hp="<<health(s,victim)<<','<<health(other,victim)<<'\n';
                return false;
            }
            if(compareVisuals)for(std::size_t i=0;i<s.impactEffects.size();++i)
                if(runtimeMultiplayerSnapshotImpactRecord(s.impactEffects[i])!=runtimeMultiplayerSnapshotImpactRecord(other.impactEffects[i])
                    || s.impactEffects[i].animId!=other.impactEffects[i].animId
                    || s.impactEffects[i].frameIndex!=other.impactEffects[i].frameIndex) {
                    std::cout<<"Continuation mismatch tick="<<s.tick<<" index="<<i
                        <<" live="<<runtimeMultiplayerSnapshotImpactRecord(s.impactEffects[i])<<" anim="<<s.impactEffects[i].animId<<" frame="<<s.impactEffects[i].frameIndex
                        <<" saved="<<runtimeMultiplayerSnapshotImpactRecord(other.impactEffects[i])<<" anim="<<other.impactEffects[i].animId<<" frame="<<other.impactEffects[i].frameIndex<<'\n';
                    return false;
                }
            ++continuationChecks;return true;
        };
        for(int age=0;age<=hz*5;++age){
            if(extraMuzzle(s) || (pre && !compare(*pre,true)) || (post && !compare(*post,false)))
                return fail("saved death continued with a different effect or damage");
            if(age==2 || age==hz/2 || age==hz || age==hz*2)
                if(!capture("age-"+std::to_string(age)))return fail(error);
            ++s.tick;advanceRuntimeEffects(s);
            if(pre){++pre->tick;advanceRuntimeEffects(*pre);}
            if(post){++post->tick;advanceRuntimeEffects(*post);}
        }
        // Suppressing DeathWeapon does not suppress the stock building's
        // separate damaging debris. Sold/temporal exits have neither source.
        if((mode<2 && health(s,victim)>=initialHealth) || (mode>=3 && health(s,victim)!=initialHealth))
            return fail("oil blast missing or sold/temporal exit did damage: "+std::to_string(initialHealth-health(s,victim)));
        RuntimeObjectExitRequest repeat;repeat.objectId=id;repeat.kind=RuntimeObjectExitKind::Destroyed;
        const auto sequence=s.deathWeapons.nextRequestId();
        if(queueRuntimeObjectExit(s,repeat)!=RuntimeObjectExitQueueResult::AlreadyFinalized
            || s.deathWeapons.nextRequestId()!=sequence || countPayload(s)!=0 || extraMuzzle(s))
            return fail("duplicate death repeated the explosion");
        if(!capture("finished"))return fail(error);
        std::cout<<"Oil explosion hz="<<hz<<" mode="<<mode<<" owner="<<owner
            <<" blueRing=0 victimDamage="<<initialHealth-health(s,victim)<<'\n';
        ++cases;
    }
    std::cout<<"Oil explosion passed: cases="<<cases<<" saves="<<saves<<" continuationChecks="
        <<continuationChecks<<" nativeBlasts="<<blasts<<" captures="<<captures<<'\n';
    return true;
}
