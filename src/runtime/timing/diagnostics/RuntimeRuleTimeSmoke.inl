bool runRuntimeRuleTimeSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    try {
        const auto require = [](bool value, const std::string& reason) { if (!value) throw std::runtime_error(reason); };
        const char* output = std::getenv("CONCORD_RULE_TIME_DIR");
        const std::string root = output && *output ? output : "build/smoke/feature-checks/21-timebase/checks";
        std::filesystem::create_directories(root);
        auto base = std::make_unique<RuntimeWindowState>(); base->config = config;
        base->config.defaultMapPath = (std::filesystem::path(config.localDataRootPath)
            / "resources/mix_exports/multimd/2peaks.map").string();
        base->config.computerAiEnabled = base->config.noCooldown = base->config.multiplayerActive = false;
        base->config.playerSlots.clear(); base->config.gameModeRulesOverride.clear();
        base->report = buildRuntimeStartupReport(base->config, &base->world, &base->theater,
            &base->art, &base->rules, &base->mapFacts);
        require(base->report.success, "original startup resources");
        const auto make = [&](int hz, bool realMap = false) {
            auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
            s.config = base->config; s.config.fixedTickHz = hz;
            s.config.crates = s.config.superweapons = s.config.shortGame = false;
            s.report = base->report; s.rules = base->rules; s.art = base->art; s.theater = base->theater;
            if (realMap) s.world = base->world;
            s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
            if (!realMap) for (int y=1;y<=24;++y) for(int x=1;x<=48;++x) {
                TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
                cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            }
            s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
            s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
            s.runtimeSuppressPlayerAudioFeedback=true; s.economy.owner="Americans";
            s.economy.credits=10000; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
            initializeRuntimeLocalProductionLanes(s.economy); initializeRuntimeNavigation(s);
            if(!realMap) for(auto& [key,info]:s.terrainInfoByCell) {
                info.movementLandType=info.authoredLandType=RuntimeTerrainLandType::Clear;
                info.water=info.shore=info.bridge=false; info.passable=true; info.cost=10;
            }
            RuntimeHumanHouseAuthority authority; authority.owner="Americans";
            authority.canonicalOwner="americans"; authority.localFacade=true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            return box;
        };
        std::ofstream csv(root+"/timebase-events.csv");
        csv<<"kind,type,hz,ordinal,tick,nominalSeconds,expected,actual\n";
        int movementCases=0,shots=0,impacts=0,turns=0,saves=0,checks=0;
        // Unit and projectile Speed share the retail percentage conversion.
        require(runtimeRuleSpeedLeptons(6)==15&&runtimeRuleSpeedLeptons(7)==17
            &&runtimeRuleSpeedLeptons(10)==25&&runtimeRuleSpeedLeptons(100)==255,
            "original ReadSpeed conversion changed"); checks+=4;
        std::ifstream retailRof(std::filesystem::path(root).parent_path()/"retail-rof.csv");
        require(retailRof.good(),"retail GetROF evidence missing");
        std::string row; int retailCases=0;
        while(std::getline(retailRof,row)) {
            std::vector<std::string> fields; std::stringstream record(row); std::string field;
            while(std::getline(record,field,',')) fields.push_back(field);
            if(fields.size()!=8||fields[0]!="ordinary") continue;
            const int input=static_cast<int>(std::stoi(fields[1])*std::stod(fields[2])+std::stoi(fields[5]));
            const double multiplier=std::stoi(fields[4])>0?std::stod(fields[3]):1.0;
            require(runtimeRofScaledFrames(input,multiplier)==std::stoi(fields[7]),
                "x64 ROF differs from original native result: "+row);
            ++retailCases;
        }
        require(retailCases==810,"incomplete retail GetROF matrix"); checks+=retailCases;
        // Fixed retail TechnoType::Read_INI results over 24 cardinal cells.
        // Goldens are independent of the production timing helper.
        struct MovementCadenceGolden { const char* type; int speed; int leptons; };
        constexpr MovementCadenceGolden movementGoldens[] = {
            {"MTNK",7,17}, {"HTNK",6,15}, {"FV",10,25}
        };
        std::map<std::string,std::vector<double>> shotReferences;
        for(int hz:{15,30,60}) {
            for(const auto& golden:movementGoldens) {
                const std::string type=golden.type;
                auto box=make(hz); auto& s=*box;
                auto* object=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,{4,12});
                require(object!=nullptr,type+" movement object"); const int id=object->instanceId;
                object->facing=runtimeFacingFromDelta(1,0);
                const auto* unit=findRuntimeUnitType(s.rules,*object); require(unit!=nullptr,"movement rules");
                require(unit->speed==golden.speed,type+" authored Speed changed from retail cadence fixture");
                const double expected=24.0*256.0*hz/(15.0*golden.leptons);
                applySelection(s,{id},false,false); issueRuntimeMoveCommand(s,{44,12});
                require(s.lastCommand.acceptedCount==1&&s.behaviors.contains(id),type+" move command");
                std::vector<RuntimeCellPoint> path; for(int x=5;x<=44;++x) path.push_back({x,12});
                replaceRuntimeBehaviorPath(s.behaviors.at(id),path); s.behaviors.at(id).pathRequestPending=false;
                int first=-1,last=-1;
                for(int i=0;i<hz*70&&last<0;++i) {
                    ++s.tick; advanceRuntimeSimulation(s); object=findMutableObjectById(s,id);
                    require(object&&object->alive,"movement object disappeared");
                    if(object->x==8&&first<0) first=s.tick;
                    if(object->x==32&&last<0) last=s.tick;
                }
                csv<<"move,"<<type<<','<<hz<<",24,"<<last-first<<','<<double(last-first)/hz<<','<<expected<<','<<last-first<<'\n';
                require(first>=0&&last>=0&&std::abs((last-first)-expected)<=1.1,
                    type+" Hz="+std::to_string(hz)+" cruise="+std::to_string(last-first)+" expected="+std::to_string(expected));
                ++movementCases;
            }
            for(const std::string type:{"MTNK","HTNK"}) {
                auto box=make(hz); auto& s=*box;
                auto* a=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,{8,12}); require(a,"shooter"); const int aId=a->instanceId;
                auto* b=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,"MTNK",{11,12}); require(b,"target"); const int bId=b->instanceId;
                auto* targetType=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*b)); require(targetType,"target rules");
                targetType->strength=1000000; b->health=targetType->strength; b->mission="Sleep"; b->weaponCooldownTicksRemaining=1000000;
                a=findMutableObjectById(s,aId); a->facing=a->turretFacing=runtimeFacingFromDelta(3,0);
                const auto profile=runtimeWeaponProfileForObject(s,*a,b); require(profile.weapon,"weapon profile");
                const auto weapon=*profile.weapon; const int burst=std::clamp(weapon.burst,1,256);
                applySelection(s,{aId},false,false); issueRuntimeAttackCommand(s,*b);
                require(s.lastCommand.acceptedCount==1,"attack command");
                std::vector<int> fired; std::vector<double> seconds; int previousHealth=b->health;
                for(int i=0;i<hz*80&&fired.size()<12;++i) {
                    ++s.tick; advanceRuntimeSimulation(s);
                    for(const auto& p:s.projectiles) if(p.attackerObjectId==aId&&p.parentActorId<0&&p.spawnTick==s.tick) {
                        fired.push_back(s.tick); seconds.push_back(double(s.tick)/hz); ++shots;
                        csv<<"shot,"<<type<<','<<hz<<','<<fired.size()<<','<<s.tick<<','<<double(s.tick)/hz<<','<<weapon.rof<<','<<p.totalTicks<<'\n';
                        require(p.totalTicks==runtimeProjectileTravelTicks(s.rules,weapon,3.0,hz),"actual projectile lifetime differs from rule clock");
                    }
                    b=findMutableObjectById(s,bId); require(b&&b->alive,"durable target lost");
                    if(b->health!=previousHealth) {
                        csv<<"impact,"<<type<<','<<hz<<','<<impacts<<','<<s.tick<<','<<double(s.tick)/hz<<','<<previousHealth-b->health<<','<<b->health<<'\n';
                        previousHealth=b->health; ++impacts;
                    }
                }
                require(fired.size()>=12,"not enough actual shots");
                for(std::size_t i=1;i<fired.size();++i) {
                    const bool insideBurst=i%burst!=0;
                    const int minimum=runtimeRuleFramesToTicks(insideBurst?3:weapon.rof,hz);
                    const int maximum=runtimeRuleFramesToTicks(insideBurst?5:weapon.rof+2,hz);
                    require(fired[i]-fired[i-1]>=minimum&&fired[i]-fired[i-1]<=maximum,
                        type+" actual firing interval="+std::to_string(fired[i]-fired[i-1])+" expected="+std::to_string(minimum)+".."+std::to_string(maximum));
                    ++checks;
                }
                for(auto& value:seconds) value-=double(fired.front())/hz;
                const auto [it,inserted]=shotReferences.try_emplace(type,seconds);
                if(!inserted) for(std::size_t i=0;i<seconds.size();++i)
                    require(std::abs(seconds[i]-it->second[i])<0.000001,"same deterministic volley changed with FixedTickHz");
            }
            // Actual body/turret callers, including halfway retarget and a
            // complete save while both timers still own fractional directions.
            auto box=make(hz,true); auto& s=*box;
            const auto center=runtimeWorldAudioContext(s).listenerCell;
            auto* a=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{center.x,center.y}); require(a,"save fixture"); const int id=a->instanceId;
            a->facing=a->turretFacing=0;
            WorldObjectInstance target=*a; target.x+=4; target.y+=1;
            for(int i=0;i<3;++i) {
                ++s.tick; a=findMutableObjectById(s,id);
                updateRuntimeMoveFacingToward(s,*a,{center.x,center.y},{center.x+1,center.y});
                updateRuntimeWeaponFacingToward(s,*a,target);
            }
            require(a->bodyFacingClock.active()&&a->turretFacingClock.active(),"real callers lost independent turn timers");
            WeaponType zeroRof; zeroRof.rof=0; zeroRof.sonic=true;
            require(runtimeWeaponCooldownTicks(s,*a,zeroRof)==runtimeRuleFramesToTicks(1,hz),
                "zero ROF fired faster than one original simulation frame"); ++checks;
            const auto body=a->bodyFacingClock,turret=a->turretFacingClock;
            const auto originalHash=s.world.deterministicSimulationStateHash(); a->turretFacingClock.elapsedNumerator+=hz;
            require(s.world.deterministicSimulationStateHash()!=originalHash,"turn timer absent from authoritative hash"); a->turretFacingClock=turret;
            const auto save=root+"/turn-"+std::to_string(hz)+".ini";
            const bool saved=saveRuntimeState(s,save,error); require(saved,"save: "+error);
            auto restored=make(hz,true); const bool loaded=loadRuntimeState(*restored,save,error);
            require(loaded,"load: "+error); ++saves;
            auto* r=findMutableObjectById(*restored,id);
            require(r&&r->bodyFacingClock==body&&r->turretFacingClock==turret,"complete load changed turn precision");
            for(int i=0;i<hz*5;++i) {
                ++s.tick; ++restored->tick;
                if(i==hz/3) {target.x-=7; target.y+=4;}
                a=findMutableObjectById(s,id); r=findMutableObjectById(*restored,id);
                updateRuntimeMoveFacingToward(s,*a,{center.x,center.y},{center.x+1,center.y});
                updateRuntimeMoveFacingToward(*restored,*r,{center.x,center.y},{center.x+1,center.y});
                updateRuntimeWeaponFacingToward(s,*a,target); updateRuntimeWeaponFacingToward(*restored,*r,target);
                require(a->facing==r->facing&&a->turretFacing==r->turretFacing
                    &&a->bodyFacingClock==r->bodyFacingClock&&a->turretFacingClock==r->turretFacingClock,"turn diverged after full load");
                ++turns;
            }
            require(!a->bodyFacingClock.active()&&!a->turretFacingClock.active(),"turn timer failed to finish");
            for(const std::string invalid:{"0,0,0,0,-1,-1,7","0,0,0,0,-1", "0,70000,256,0,1,2", "0,256,256,0,9999999,2", "0,256,256,-1,1,2"}) {
                RuntimeFacingClock untouched=body;
                require(!runtimeParseFacingClock(invalid,s.tick,untouched)&&untouched==body,"malformed timer mutated state"); ++checks;
            }
            // Buildup and the dedicated DiskLaser already used original
            // frames. Verify this correction does not apply conversion twice.
            const auto buildup=runtimeBuildupTiming(0.06,26,hz);
            require(buildup.fixedTicks==runtimeRuleFramesToTicks(buildup.legacyTicks,hz),"buildup clock regression");
            require(runtimeAuthoredFramesToFixedTicks(s,runtimeDiskLaserChargeFrames)==runtimeRuleFramesToTicks(runtimeDiskLaserChargeFrames,hz),"disk laser double conversion");
            for(int rate:{0,60,120,450,600,900,1200}) {
                ArtType art; art.rate=rate;
                require(runtimeImpactAnimationFrameDelayTicks(&art,hz)==runtimeRuleFramesToTicks(rate>0?std::max(1,900/rate):1,hz),"AnimType Rate conversion"); ++checks;
            }
        }
        require(impacts>=30,"actual damage dispatch was not exercised");
        std::cout<<"Rule time passed: movement="<<movementCases<<" actualShots="<<shots<<" actualImpacts="<<impacts
            <<" restoredTurnUpdates="<<turns<<" fullSaves="<<saves<<" boundaries="<<checks<<" Hz=15/30/60\n";
        return true;
    } catch(const std::exception& e) {error=std::string("RuleTime: ")+e.what(); return false;}
}
