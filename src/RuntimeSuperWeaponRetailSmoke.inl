    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPERWEAPON_RETAIL_ONLY")) {
        const auto* reveal = stockRules.findSuperWeaponType("PsychicRevealSpecial");
        const auto* force = stockRules.findSuperWeaponType("ForceShieldSpecial");
        const auto* mutate = stockRules.findSuperWeaponType("GeneticConverterSpecial");
        const auto* storm = stockRules.findSuperWeaponType("LightningStormSpecial");
        if (stockRules.getSuperWeaponTypes().size()!=12 || !reveal || reveal->radius!=10
            || !force || force->durationFrames!=500 || force->blackoutDurationFrames!=1000
            || force->fadeSoundFrames!=75 || !mutate || !mutate->mutateExplosion || mutate->radius!=5
            || !storm || storm->boltExplosionAnim!="EXPLOLB"
            || stockRules.findSuperWeaponType("NukeSpecial")->startSound!=stockIni.getValue("AudioVisual","DigSound",""))
            return fail("retail cross-section defaults");
        const auto reload = [&](RuntimeWindowState& s, std::unique_ptr<RuntimeWindowState>& loaded) {
            std::ostringstream saved;
            if (!writeRuntimeStateToStream(s,saved,error)) return false;
            IniFile ini; loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
            return ini.loadFromString(saved.str()) && materializeRuntimeLoadStateFromIni(*loaded,ini,error);
        };
        for (const int hz : {30,60}) {
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                RuntimeImpactEffect bolt; bolt.animId="WCLBOLT1";
                initializeRuntimeSuperWeaponAnimation(s,bolt);
                if(bolt.animationFrameDelayTicks!=hz/15)
                    return fail("retail default animation delay");
            }
            {
                auto box=fixture("Russians",hz); auto& s=*box;
                const auto* rule=s.rules.findSuperWeaponType("NukeSpecial");
                if(rule->nuclearAltitudeLeptons!=20000 || rule->nuclearDownSpeed!=235)
                    return fail("nuclear retail altitude/ballistic speed");
                const int victim=addUnit(s,"NAHAND",WorldObjectKind::Structure,90,90,"Enemy",10000);
                const int flight=superWeaponNuclearFlightFrames(*rule,true)*hz/15;
                const int buildup=runtimeSpecialDetailAnimationDurationTicks(s,"NUKEBALL");
                if(!fire(s,"NukeSpecial",{{90,90},victim}))return fail(error);
                step(s,flight/2);
                if(!std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx) {
                    return fx.animId=="PSIWARN" && fx.lifecycleState=="NuclearTargetWarning";
                }))return fail("nuclear target warning lost during flight");
                s.runtimeSoundIntentHistory.clear();
                step(s,flight+1-s.tick);
                if(std::count(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),"sound:NukeIntro")!=1)
                    return fail("nuclear intro report missing/repeated");
                if(std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId=="PSIWARN";}))
                    return fail("nuclear warning survived missile contact");
                if(findObjectById(s,victim)->health!=10000 || !std::any_of(s.impactEffects.begin(),s.impactEffects.end(),
                    [](const auto& fx){return fx.animId=="NUKEBALL";}))return fail("nuke pre-impact missing/early damage");
                if(!capture(s,"retail-nuke-buildup-"+std::to_string(hz)))return fail(error);
                std::unique_ptr<RuntimeWindowState> loaded;
                if(!reload(s,loaded))return fail("nuclear buildup save: "+error);
                step(s,buildup-1); step(*loaded,buildup-1);
                if(findObjectById(s,victim)->health!=10000)return fail("nuke damage before buildup ended");
                s.runtimeSoundIntentHistory.clear();loaded->runtimeSoundIntentHistory.clear();
                step(s);step(*loaded);
                if(std::count(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),"sound:NukeExplosion")!=1
                    || std::count(loaded->runtimeSoundIntentHistory.begin(),loaded->runtimeSoundIntentHistory.end(),"sound:NukeExplosion")!=1)
                    return fail("nuclear explosion report missing/repeated after reload");
                if(findObjectById(s,victim)->health>=10000 || s.superWeapons.stateHash()!=loaded->superWeapons.stateHash()
                    || findObjectById(s,victim)->health!=findObjectById(*loaded,victim)->health)return fail("nuke buildup reload/damage");
                step(s,hz/2);
                if(!capture(s,"retail-nuke-blast-"+std::to_string(hz)))return fail(error);
                std::cout<<"Retail nuclear "<<hz<<" Hz flight="<<flight<<" buildup="<<buildup<<" damage-after-animation/save PASS\n";
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                s.vision.reset(); s.vision.update(s.world,nullptr,{{"Commander",{"Commander"}}},s.tick); s.visionEnabled=true;
                if (!fire(s,"PsychicRevealSpecial",{{100,100},-1})) return fail(error);
                step(s,2);
                if (!s.vision.isCellExploredBy("Commander",109,100)
                    || s.vision.isCellExploredBy("Commander",111,100))
                    return fail("psychic reveal ten-cell retail cap / wrong-section regression");
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                const int enemy=addUnit(s,"HTNK",WorldObjectKind::Unit,90,90,"Enemy");
                const int corner=addUnit(s,"MTNK",WorldObjectKind::Unit,91,91,"Commander");
                const int outside=addUnit(s,"MTNK",WorldObjectKind::Unit,92,90,"Commander");
                if (!fire(s,"IronCurtainSpecial",{{90,90},enemy})) return fail("enemy iron curtain target: "+error);
                step(s,2);
                if (!s.superWeapons.isObjectProtected(enemy,s.tick) || !s.superWeapons.isObjectProtected(corner,s.tick)
                    || s.superWeapons.isObjectProtected(outside,s.tick)) return fail("iron curtain all houses / 3 by 3");
                if (!fire(s,"ChronoSphereSpecial",{{90,90},enemy},SuperWeaponTarget{{103,100},-1})) return fail(error);
                step(s,2);
                if (findObjectById(s,enemy)->x!=90 || findObjectById(s,corner)->x!=91)
                    return fail("chronosphere moved iron-curtained vehicles");
                if (!capture(s,"retail-iron-"+std::to_string(hz))) return fail(error);
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy)
                    addUnit(s,"MTNK",WorldObjectKind::Unit,90+dx,90+dy,"Commander");
                initializeRuntimeNavigation(s);
                if (!fire(s,"ChronoSphereSpecial",{{90,90},-1})) return fail(error);
                step(s,2);
                if(!std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId=="CHRONOAR";}))
                    return fail("chrono first-click placement marker");
                if(!capture(s,"retail-chrono-selection-"+std::to_string(hz)))return fail(error);
                auto context=runtimeSuperWeaponContext(s);context.emit=runtimeSuperWeaponEventSink(s);
                if(!s.superWeapons.submitTarget("Commander","ChronoSphereSpecial",{{103,100},-1},context,&error))return fail(error);
                step(s,2);
                const auto countAnim=[&](const std::string& id) {
                    return std::count_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& fx){return fx.animId==id;});
                };
                if(countAnim("CHRONOFD")!=1 || countAnim("CHRONOTG")!=1 || countAnim("CHRONOAR")!=0)
                    return fail("chrono duplicated source/destination animation for a formation");
                if (!capture(s,"retail-chrono-"+std::to_string(hz))) return fail(error);
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                const int ally=addUnit(s,"HTNK",WorldObjectKind::Unit,90,90,"Ally");
                const int outside=addUnit(s,"HTNK",WorldObjectKind::Unit,92,90,"Enemy");
                s.config.gameMode="BATTLE"; s.skirmishModeRulesLoaded=true; s.skirmishModeAlliesAllowed=true;
                s.skirmishTeamByOwner={{"commander","1"},{"ally","1"},{"enemy","2"}};
                if (!fire(s,"PsychicDominatorSpecial",{{90,90},ally})) return fail(error);
                step(s);
                const int firstDuration=runtimeSpecialDetailAnimationDurationTicks(s,"PDFXCLD");
                const int fireDelay=(firstDuration*20+99)/100;
                step(s,fireDelay-1);
                if(findObjectById(s,ally)->owner!="Ally") return fail("dominator captured before authored 20 percent");
                step(s);
                const auto links=s.specialMechanisms.mindControlLinks();
                if(findObjectById(s,ally)->owner!="Commander" || findObjectById(s,outside)->owner!="Enemy"
                    || !links.contains(ally) || !links.at(ally).permanent || links.at(ally).controllerObjectId!=-1)
                    return fail("dominator all houses / permanent control link");
                const auto head=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId=="PDFXCLD";});
                if(head==s.impactEffects.end() || !head->hasWorldPosition
                    || std::abs(head->worldLevel-head->level-750.0*2.449489742783178/256.0)>0.001)
                    return fail("dominator head height");
                std::unique_ptr<RuntimeWindowState> loaded;
                if(!reload(s,loaded)) return fail("dominator full save: "+error);
                step(*loaded,2);
                if(!loaded->specialMechanisms.mindControlLinks().contains(ally)
                    || findObjectById(*loaded,ally)->owner!="Commander") return fail("dominator control lost on reload");
                if (!capture(s,"retail-dominator-"+std::to_string(hz))) return fail(error);
                std::cout<<"Retail dominator "<<hz<<" Hz firstAnimTicks="<<firstDuration<<" fireDelay="<<fireDelay<<" permanent/save PASS\n";
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                const int center=addUnit(s,"GAPILE",WorldObjectKind::Structure,90,90,"Commander");
                const int corner=addUnit(s,"GAPILE",WorldObjectKind::Structure,94,94,"Commander");
                if(!fire(s,"ForceShieldSpecial",{{90,90},center}))return fail(error);
                step(s,2);
                if(s.superWeapons.isObjectProtected(corner,s.tick))return fail("shield square corner outside cell spread");
                const int fadeTick=1+(500-75)*hz/15;
                std::vector<SuperWeaponEvent> events;
                auto sink=runtimeSuperWeaponEventSink(s);
                for(;s.tick<fadeTick-1;) {++s.tick; auto c=runtimeSuperWeaponContext(s); c.emit=[&](const auto& e){events.push_back(e);sink(e);};s.superWeapons.tick(c);}
                if(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind=="ForceShieldFading";}))return fail("premature shield fade sound");
                std::ostringstream saved; s.superWeapons.save(saved); IniFile ini; SuperWeaponSystem resumed; resumed.reset(s.rules);
                if(!ini.loadFromString(saved.str())||!resumed.load(ini,&error))return fail(error);
                for(int i=0;i<3;++i) {++s.tick;auto c=runtimeSuperWeaponContext(s);c.emit=[&](const auto& e){events.push_back(e);};resumed.tick(c);}
                if(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="ForceShieldFading"&&e.soundIntent=="ForceShieldFading";})!=1)
                    return fail("shield fade sound missing/repeated after reload");
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                const int inside=addUnit(s,"E1",WorldObjectKind::Infantry,95,90,"Enemy",125);
                const int outside=addUnit(s,"E1",WorldObjectKind::Infantry,95,95,"Enemy",125);
                const int dog=addUnit(s,"DOG",WorldObjectKind::Infantry,90,90,"Enemy");
                if(!fire(s,"GeneticConverterSpecial",{{90,90},-1}))return fail(error);
                step(s,2);
                if(findObjectById(s,inside)||!findObjectById(s,outside)||findObjectById(s,dog))return fail("mutator footprint/nonhuman explosion");
                if (!capture(s,"retail-mutator-"+std::to_string(hz))) return fail(error);
            }
            {
                auto box=fixture("Americans",hz); auto& s=*box;
                const int victim=addUnit(s,"NAHAND",WorldObjectKind::Structure,90,90,"Enemy",10000);
                if(!fire(s,"LightningStormSpecial",{{90,90},victim}))return fail(error);
                step(s,1+260*hz/15);
                if(findObjectById(s,victim)->health!=10000)return fail("storm struck before cloud midpoint");
                auto hostile=[](const std::string&,const std::string&){return false;};
                if(!s.superWeapons.isEnemyStormActive("Enemy",s.tick,hostile)
                    || s.superWeapons.isEnemyStormActive("Commander",s.tick,hostile))return fail("storm radar outage polarity");
                const auto cloud=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId.rfind("WCCLOUD",0)==0;});
                if(cloud==s.impactEffects.end()||!cloud->hasWorldPosition||cloud->worldLevel<=cloud->level+1)return fail("storm cloud ground anchoring");
                std::unique_ptr<RuntimeWindowState> loaded;
                if(!reload(s,loaded))return fail("pending lightning save: "+error);
                for(int i=0;i<hz*5;++i) {step(s);step(*loaded);if(s.superWeapons.stateHash()!=loaded->superWeapons.stateHash())return fail("pending lightning diverged after reload");}
                if(findObjectById(s,victim)->health>=10000 || findObjectById(s,victim)->health!=findObjectById(*loaded,victim)->health)
                    return fail("pending lightning damage lost/doubled after reload");
                if (!capture(s,"retail-storm-"+std::to_string(hz))) return fail(error);
            }
            std::cout<<"Retail superweapon audit "<<hz<<" Hz: reveal/iron/chrono/dominator/shield/mutator/storm PASS\n";
        }
        return true;
    }
