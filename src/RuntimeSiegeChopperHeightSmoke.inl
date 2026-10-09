bool runRuntimeSiegeChopperHeightSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    const char* output=std::getenv("CONCORD_SIEGE_CHOPPER_HEIGHT_OUTPUT");
    const std::string root=output?output:"build/siege-chopper-height";
    std::filesystem::create_directories(root);
    std::ofstream frames(root+"/poses.csv");
    frames<<"scene,tick,mission,airborne,level,renderLevel,body,turret,shots\n";
    std::string scene;
    const auto fail=[&](const std::string& why){error="Siege chopper height "+scene+": "+why;return false;};
    IniFile original,overlay;RulesDatabase rules;
    if(!original.loadFromFile(live.report.rulesPath)||!overlay.loadFromString("[MTNK]\nStrength=100000\n"))return fail("durable target rules");
    original.appendFrom(overlay);if(!rules.loadFromIni(original))return fail("target rules overlay");
    const auto step=[](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);
    };
    int cases=0,saves=0,paired=0,captures=0,checks=0,takeoffs=0;
    for(int hz:{15,30,60})for(int mode=0;mode<4;++mode) {
        scene=std::to_string(hz)+"-"+std::to_string(mode);
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        seedRuntimeLoadScratch(s,live,false);s.world=live.world;s.rules=rules;
        s.config.fixedTickHz=hz;s.config.noCooldown=false;s.config.playerSlots.clear();
        s.config.playerCountry="Russians";s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=false;
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=s.visionEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=44;y<=80;++y)for(int x=44;x<=80;++x) {
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        auto* chopper=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"SCHP",{60,60});
        if(!chopper)return fail("source missing");const int id=chopper->instanceId;
        RuntimeSnapshotActiveStateScope active(s);
        if(!executeRuntimeSecondaryAction(s,id))return fail("deploy rejected");
        for(int n=0;n<hz*15&&!findObjectById(s,id)->deployed;++n)step(s);
        const auto initial=*findObjectById(s,id);
        if(initial.airborne||!initial.deployed||initial.level!=0)return fail("landing/deployment incomplete");
        const RuntimeCellPoint targetCell=mode==1?RuntimeCellPoint{60,66}:mode==2?RuntimeCellPoint{54,60}:RuntimeCellPoint{66,60};
        auto* target=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"MTNK",targetCell);
        if(!target)return fail("target missing");const int targetId=target->instanceId;
        target->mission="Sleep";target->health=100000;target->weaponCooldownTicksRemaining=100000;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);applySelection(s,{id},false,false);
        const auto pose=[&](const RuntimeWindowState& state) {
            const auto* o=findObjectById(state,id);RuntimeMapRenderContext context;context.rules=&state.rules;context.art=&state.art;
            return o&&o->alive&&o->deployed&&!o->airborne&&o->x==initial.x&&o->y==initial.y
                &&o->level==initial.level&&o->flightLevel==initial.flightLevel&&o->facing==initial.facing
                &&(!o->hasRenderPosition||(o->renderX==initial.x&&o->renderY==initial.y&&o->renderLevel==initial.level))
                &&resolveRuntimeObjectVisualTypeId(*o,context)=="SCHD";
        };
        const auto capture=[&](const char* phase) {
            const auto drawn=drawRuntimeStateOffscreenAtCell(s,{60,60},800,600,error,root+"/"+scene+"-"+phase+".bmp",false);
            if(!drawn||!drawn->rendered)return false;++captures;return true;
        };
        if(hz==30&&mode==0&&!capture("idle"))return fail("idle render");
        if(mode==0)issueRuntimeAttackCommand(s,*findObjectById(s,targetId));
        else if(mode==1)issueRuntimeCellForceFireCommand(s,targetCell,false);
        else if(mode==2) {
            // Guard acquisition is scheduled, not an immediate UI command.
            for(int n=0;n<hz&&!s.behaviors.contains(id);++n)step(s);
        }
        else if(!runtimeComputerAiIssueAttack(s,*findMutableObjectById(s,id),*findObjectById(s,targetId),false,0))return fail("AI order rejected");
        if(!s.behaviors.contains(id))return fail("attack not queued");
        if(!pose(s))return fail("attack acceptance lifted or moved the deployed body");
        std::set<int> shots;bool turretTurned=false;std::unique_ptr<RuntimeWindowState> restored;
        for(int n=0;n<hz*10;++n) {
            step(s);const auto* o=findObjectById(s,id);
            for(const auto& p:s.projectiles)if(p.attackerObjectId==id)shots.insert(p.actorId);
            for(const auto& p:s.recentExpiredProjectiles)if(p.attackerObjectId==id)shots.insert(p.actorId);
            if(!pose(s))return fail("body changed during firing/reload tick="+std::to_string(s.tick));++checks;
            turretTurned=turretTurned||o->turretFacing!=initial.turretFacing;
            frames<<scene<<','<<s.tick<<','<<o->mission<<','<<o->airborne<<','<<o->level<<','
                <<(o->hasRenderPosition?o->renderLevel:o->level)<<','<<o->facing<<','<<o->turretFacing<<','<<shots.size()<<'\n';
            if(restored) {
                step(*restored);const auto* b=findObjectById(*restored,id);
                if(!pose(*restored)||b->turretFacing!=o->turretFacing||b->mission!=o->mission
                    ||b->weaponCooldownTicksRemaining!=o->weaponCooldownTicksRemaining
                    ||findObjectById(s,targetId)->health!=findObjectById(*restored,targetId)->health)
                    return fail("save continuation diverged tick="+std::to_string(s.tick)
                        +" pose="+std::to_string(pose(*restored))+" airborne="+std::to_string(b->airborne)
                        +" level="+std::to_string(b->level)+" facing="+std::to_string(b->facing)
                        +" turret="+std::to_string(o->turretFacing)+"/"+std::to_string(b->turretFacing)
                        +" mission="+o->mission+"/"+b->mission
                        +" cooldown="+std::to_string(o->weaponCooldownTicksRemaining)+"/"+std::to_string(b->weaponCooldownTicksRemaining)
                        +" health="+std::to_string(findObjectById(s,targetId)->health)+"/"+std::to_string(findObjectById(*restored,targetId)->health));++paired;
            }
            if(!restored&&!shots.empty()) {
                if(hz==30&&mode==0&&!capture("firing"))return fail("firing render");
                std::ostringstream text;IniFile ini;restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
                if(!writeRuntimeStateToStream(s,text,error)||(mode%2&&!writeRuntimeMultiplayerSnapshotExtension(s,text,error))
                    ||!ini.loadFromString(text.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error)
                    ||(mode%2&&!materializeRuntimeMultiplayerSnapshotExtension(*restored,ini,error)))return fail("save "+error);
                ++saves;
            }
        }
        if(shots.size()<2||!turretTurned||!restored)return fail("multiple shots/turret movement absent");
        if(hz==30&&mode==0&&!capture("reload"))return fail("reload render");
        applySelection(s,{id},false,false);issueRuntimeStopCommand(s);
        if(!pose(s)||!executeRuntimeSecondaryAction(s,id))return fail("stop or undeploy rejected");
        for(int n=0;n<hz*15&&findObjectById(s,id)->mission!="Guard";++n)step(s);
        chopper=findMutableObjectById(s,id);
        if(chopper->deployed||!chopper->airborne||!runtimeObjectCanMove(s,*chopper)||chopper->flightLevel<=0)
            return fail("explicit undeploy no longer takes off");++takeoffs;++cases;
    }
    std::cout<<"Siege chopper height passed: cases="<<cases<<" fixedBodyFrames="<<checks<<" saves="<<saves
        <<" continuedTicks="<<paired<<" takeoffs="<<takeoffs<<" captures="<<captures<<'\n';return true;
}
