bool runRuntimeMinimumRangeSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why) { error="Minimum range: "+scene+": "+why; return false; };
    const auto tick=[](RuntimeWindowState& s) {
        ++s.tick;
        processRuntimePathRequests(s);
        if (s.tick<=3) for (const auto& [id,b] : s.behaviors) {
            std::cout<<"Range path tick="<<s.tick<<" actor="<<id<<" path="<<b.path.size()
                <<" pending="<<b.pathRequestPending<<" next="<<b.nextRepathTick
                <<" state="<<static_cast<int>(b.state)
                <<" targetCell="<<b.pendingPathGoal.x<<","<<b.pendingPathGoal.y;
            if (!b.path.empty()) {
                const auto* o=findObjectById(s,id);
                std::cout<<" first="<<b.path.front().x<<","<<b.path.front().y
                    <<" passable="<<(o&&runtimeCellTransitionIsPassable(s,s.terrainByCell,
                        runtimeTickOccupancyLookup(s),runtimeCellPointForObject(*o),b.path.front(),*o));
            }
            std::cout<<" status="<<s.simulationStatus<<'\n';
        }
        advanceRuntimeWeaponCooldowns(s);
        std::vector<int> ids;
        for (const auto& [id,b] : s.behaviors) if (b.kind==RuntimeBehaviorKind::Attack) ids.push_back(id);
        for (int id : ids) {
            auto* o=findMutableObjectById(s,id);
            auto b=s.behaviors.find(id);
            if (o&&b!=s.behaviors.end()) advanceRuntimeAttackBehavior(s,*o,b->second);
        }
        advanceRuntimeEffects(s);
        advanceRuntimeSpecialMechanisms(s);
    };
    const auto missile=[](const RuntimeWindowState& s,int parent) -> const SpawnedMissileState* {
        for (const auto& [id,m] : s.specialMechanisms.spawnedMissiles())
            if (m.parentObjectId==parent) return &m;
        return nullptr;
    };
    int cases=0, saves=0;
    for (int hz : {15,30,60}) for (int mode=0;mode!=21;++mode) {
        const bool naval=mode>=18;
        const bool ground=(mode>=9&&mode<=15)||mode==18||mode==19;
        const bool blocked=mode==3||mode==14, stop=mode==6||mode==13;
        const bool inRange=mode==7||mode==15;
        const bool save=mode==5||mode==8||mode==11||mode==12||mode==19;
        const bool exact=mode==8||mode==12;
        const bool building=mode==2||mode==16||mode==17||mode==20;
        const RuntimeCellPoint targetCell=mode==1?RuntimeCellPoint{65,65}
            :mode==16?RuntimeCellPoint{59,63}:mode==17?RuntimeCellPoint{61,61}
            :(mode==2||mode==20)?RuntimeCellPoint{66,64}:inRange?RuntimeCellPoint{74,64}
            :mode==10?RuntimeCellPoint{64,64}:RuntimeCellPoint{65,64};
        scene=std::string(naval?"DRED":"V3")+" hz="+std::to_string(hz)+" mode="+std::to_string(mode);
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);
        s.config.fixedTickHz=hz; s.config.multiplayerActive=false; s.config.computerAiEnabled=false;
        s.config.noCooldown=false; s.config.gameMode="Battle"; s.campaignMode=false;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false; s.visionEnabled=false;
        s.economy.owner="Russians"; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        // The synthetic flat map must not retain the live campaign's water,
        // cliff and structure passability facts at these same coordinates.
        s.mapFacts={}; s.mapFactsLoaded=false;
        int waterTile=-1;
        if(naval) {
            for(int index=0;index<static_cast<int>(s.theater.getTotalTileSlots());++index) {
                TerrainCell cell;cell.tileIndex=index;cell.subTileIndex=0;
                const auto info=runtimeTerrainInfoForCell(s,cell);
                if(info.water&&!info.bridge&&!info.shore&&!info.cliff){waterTile=index;break;}
            }
            if(waterTile<0)return fail("native water fixture");
        }
        const int low=blocked?63:45, high=blocked?65:88;
        for (int y=low;y<=high;++y) for (int x=low;x<=high;++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y); c.tileIndex=naval?waterTile:0; c.subTileIndex=0;
            if(naval&&building&&x>=targetCell.x&&x<targetCell.x+2&&y>=targetCell.y&&y<targetCell.y+2)c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        const auto add=[&](const char* type, RuntimeBuildKind kind, RuntimeCellPoint cell) {
            auto* o=addRuntimeObjectInstance(s,kind,type,cell); if (!o) return -1;
            o->mission="Guard"; o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;
            return o->instanceId;
        };
        const int id=add(naval?"DRED":"V3",RuntimeBuildKind::Unit,{64,64});
        auto groundTarget=ground?runtimeGroundAttackTargetObject(s,targetCell):std::optional<WorldObjectInstance>{};
        if(ground&&!groundTarget)return fail("ground target fixture");
        const int targetId=ground?groundTarget->instanceId:add((mode==16||mode==17)?"GAWEAP":building?"GAPOWR":"MTNK",
            building?RuntimeBuildKind::Building:RuntimeBuildKind::Unit,targetCell);
        if (id<0||(!ground&&targetId<0)) return fail("native fixture missing");
        auto* target=ground?&*groundTarget:findMutableObjectById(s,targetId);
        if(!ground){target->owner="Americans";target->health=100000;}
        if (mode==1) add("GAPOWR",RuntimeBuildKind::Building,{60,62});
        auto* source=findMutableObjectById(s,id);
        source->facing=source->turretFacing=runtimeFacingFromDelta(1,0);
        const char* weaponId=naval?"DredLauncher":"V3Launcher";
        const auto* weapon=s.rules.findWeaponType(weaponId);
        if (!weapon||weapon->minimumRange!=(naval?8.0:5.0)||weapon->range!=(naval?25.0:18.0)) return fail("native range changed");
        if(mode==16&&(runtimeCellDistance({64,64},targetCell)<=weapon->minimumRange
            ||runtimeWeaponDistanceToTarget(s,*source,*target,*weapon)>=weapon->minimumRange))return fail("wide building far-edge fixture");
        advanceRuntimeSpecialMechanisms(s);
        applySelection(s,{id},false,false);
        if(ground)issueRuntimeCellForceFireCommand(s,targetCell,false);
        else issueRuntimeAttackCommand(s,*findObjectById(s,targetId));
        if (s.lastCommand.acceptedCount!=1) return fail("close attack order rejected");
        if (mode==0) {
            const auto plan=resolveRuntimePathTowardRange(s,*findObjectById(s,id),*findObjectById(s,targetId),weapon->range);
            std::cout<<"Range direct plan: reached="<<plan.reachable<<" path="<<plan.path.size()
                <<" goal="<<plan.resolvedGoal.x<<","<<plan.resolvedGoal.y
                <<" noPath="<<plan.realNoPath<<" budget="<<plan.budgetExhausted
                <<" candidates="<<s.attackApproachStats.candidates<<'\n';
        }
        // A ground command retains its synthetic behavior ID, while its missile
        // has no world target to track. Its fixed aim must still match this cell
        // and the effective terrain level used by runtimeGroundAttackTargetObject.
        const auto checkPrepared=[&](const RuntimeWindowState& current,
            const WorldObjectInstance& actor,const WorldObjectInstance& aim,
            const SpawnedMissileState& m,const char* phase) {
            const double distance=runtimeWeaponDistanceToTarget(current,actor,aim,*weapon);
            const int expectedMissileTarget=ground?-1:targetId;
            const auto aimPosition=runtimeProjectileTargetPosition(current,aim);
            const int groundLevel=ground?terrainLevelAtRuntimeCell(current,targetCell.x,targetCell.y):aimPosition.level;
            const bool groundAimInput=!ground||(aim.x==targetCell.x&&aim.y==targetCell.y
                &&aim.level==groundLevel&&aimPosition.x==static_cast<double>(targetCell.x)
                &&aimPosition.y==static_cast<double>(targetCell.y)&&aimPosition.level==groundLevel);
            const bool fixedGroundAim=!ground||(m.targetX==aimPosition.x&&m.targetY==aimPosition.y
                &&m.targetZ==static_cast<double>(groundLevel));
            std::ostringstream detail;
            detail<<std::setprecision(17)<<"Minimum range prepared: "<<scene<<" phase="<<phase
                <<" tick="<<current.tick<<" distance="<<distance<<" bounds="<<weapon->minimumRange<<".."<<weapon->range
                <<" actorCell="<<actor.x<<","<<actor.y<<" missileTarget="<<m.targetObjectId
                <<" expectedMissileTarget="<<expectedMissileTarget<<" commandTarget="<<targetId
                <<" fixedXYZ="<<m.targetX<<","<<m.targetY<<","<<m.targetZ
                <<" aimXYZ="<<aimPosition.x<<","<<aimPosition.y<<","<<aimPosition.level
                <<" groundLevel="<<groundLevel<<" groundInput="<<groundAimInput<<" fixedGround="<<fixedGroundAim
                <<" weapon="<<m.weaponId<<" expectedWeapon="<<weaponId
                <<" slot="<<m.slot<<" start="<<m.startTick<<" release="<<m.releaseTick<<" last="<<m.lastTick;
            if(distance+0.001<weapon->minimumRange||distance>weapon->range+0.001
                ||m.targetObjectId!=expectedMissileTarget||m.weaponId!=weaponId||!groundAimInput||!fixedGroundAim)
                return fail("missile prepared outside firing range or changed target/aim/weapon; "+detail.str());
            std::cout<<detail.str()<<" PASS\n";
            return true;
        };
        bool moved=false, retargeted=false, stopped=false;
        RuntimeCellPoint stoppedAt;
        std::unique_ptr<RuntimeWindowState> loaded;
        int firedTick=-1, savedTick=-1;
        RuntimeCellPoint firedAt;
        const auto restoredStateDetail=[&](const RuntimeWindowState& current) {
            std::ostringstream detail;
            detail<<std::setprecision(17)<<" saveTick="<<savedTick<<" tick="<<current.tick;
            if(const auto* actor=findObjectById(current,id)) {
                detail<<" actorCell="<<actor->x<<","<<actor->y
                    <<" bodyXYZ="<<runtimeObjectRenderX(*actor)<<","<<runtimeObjectRenderY(*actor)
                    <<","<<runtimeDrawLevelForObject(*actor)
                    <<" facing="<<actor->facing<<" hasPose="<<actor->hasRenderPosition
                    <<" active="<<actor->renderMoveActive;
            } else detail<<" actor=missing";
            const auto found=current.behaviors.find(id);
            if(found!=current.behaviors.end()) {
                const auto& b=found->second;
                detail<<" behavior="<<static_cast<int>(b.kind)<<"/"<<static_cast<int>(b.state)
                    <<" targetId="<<b.targetObjectId<<" moveCooldown="<<b.moveCooldownTicks
                    <<" path="<<b.path.size()<<" pending="<<b.pathRequestPending
                    <<" pathGoal="<<b.pendingPathGoal.x<<","<<b.pendingPathGoal.y
                    <<" moveTarget="<<b.moveTarget.x<<","<<b.moveTarget.y
                    <<" nextRepath="<<b.nextRepathTick<<" queue="<<current.pendingPathObjectIds.size();
                if(!b.path.empty())detail<<" pathFirst="<<b.path.front().x<<","<<b.path.front().y
                    <<" pathLast="<<b.path.back().x<<","<<b.path.back().y;
            } else detail<<" behavior=missing";
            return detail.str();
        };
        for (int step=0;step!=hz*(blocked?5:naval?60:30);++step) {
            tick(s);
            source=findMutableObjectById(s,id); target=ground?&*groundTarget:findMutableObjectById(s,targetId);
            if (!source||!target) return fail("fixture object disappeared");
            const RuntimeCellPoint cell=runtimeCellPointForObject(*source);
            if (cell!=RuntimeCellPoint{64,64}) moved=true;
            if (const auto* m=missile(s,id)) {
                if(!checkPrepared(s,*source,*target,*m,"live"))return false;
                if (blocked||stop) return fail("blocked/stopped launcher fired");
                firedTick=m->startTick; firedAt=cell; break;
            }
            if (moved&&mode==4&&!retargeted) {
                setRuntimeObjectCell(s,*target,{source->x+1,source->y});
                clearRuntimeRenderPosition(*target); retargeted=true;
            }
            if (moved&&save&&!loaded) {
                savedTick=s.tick;
                std::ostringstream text;
                if (!writeRuntimeStateToStream(s,text,error)) return fail(error);
                if (exact&&!writeRuntimeMultiplayerSnapshotExtension(s,text,error)) return fail(error);
                IniFile ini; if (!ini.loadFromString(text.str())) return fail("mid-retreat save parsing");
                loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
                if (!materializeRuntimeLoadStateFromIni(*loaded,ini,error,exact)) return fail(error);
                if (exact&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error)) return fail(error);
                const auto restored=loaded->behaviors.find(id);
                if (restored==loaded->behaviors.end()||restored->second.kind!=RuntimeBehaviorKind::Attack
                    ||restored->second.targetObjectId!=targetId
                    ||restored->second.moveCooldownTicks!=s.behaviors.at(id).moveCooldownTicks
                    ||runtimeCellPointForObject(*findObjectById(*loaded,id))!=cell)
                    return fail("save lost attack target, committed move cell or movement clock");
                std::cout<<"Minimum range restore state: "<<scene<<" phase=initial"
                    <<restoredStateDetail(*loaded)<<'\n';
                if (const char* output=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT")) {
                    const auto prefix=std::filesystem::path(output)/("minimum-range-"+std::to_string(hz)+"-"+std::to_string(mode));
                    std::ofstream original(prefix.string()+"-saved.ini"); original<<text.str();
                    std::ofstream resumed(prefix.string()+"-restored.ini");
                    if (!writeRuntimeStateToStream(*loaded,resumed,error)) return fail(error);
                    if (exact&&!writeRuntimeMultiplayerSnapshotExtension(*loaded,resumed,error)) return fail(error);
                }
                ++saves;
            }
            if (moved&&stop&&!stopped) {
                applySelection(s,{id},false,false); issueRuntimeStopCommand(s);
                stoppedAt=cell; stopped=true;
            }
            if (stopped&&cell!=stoppedAt) return fail("Stop did not cancel retreat");
        }
        if (blocked) {
            if (moved||firedTick>=0) return fail("no-route launcher moved or fired");
        } else if (stop) {
            if (!moved||!stopped) return fail("Stop fixture never began retreat");
        } else if (firedTick<0||(!inRange&&!moved)||(inRange&&moved)) {
            const auto found=s.behaviors.find(id);
            const auto profile=runtimeWeaponProfileForObject(s,*source,target);
            std::ostringstream detail;
            detail<<"retreat/fire expectation failed moved="<<moved<<" fire="<<firedTick
                <<" cell="<<source->x<<","<<source->y<<" target="<<target->x<<","<<target->y
                <<" mission="<<source->mission<<" movable="<<runtimeObjectCanMove(s,*source)
                <<" visible="<<runtimeObjectVisibleToOwner(s,source->owner,*target)
                <<" pendingBurst="<<s.combatDetails.hasPendingBurst(id)
                <<" weapon="<<(profile.weapon?profile.weapon->id:"none");
            if (found!=s.behaviors.end()) {
                const auto& b=found->second;
                detail<<" behavior="<<static_cast<int>(b.kind)<<"/"<<static_cast<int>(b.state)
                    <<" targetId="<<b.targetObjectId<<" path="<<b.path.size()
                    <<" pending="<<b.pathRequestPending<<" queue="<<s.pendingPathObjectIds.size()
                    <<" moveCooldown="<<b.moveCooldownTicks<<" nextRepath="<<b.nextRepathTick;
            } else detail<<" no behavior";
            return fail(detail.str()+": "+s.simulationStatus);
        }
        if (mode==4&&!retargeted) return fail("moving-target fixture did not retarget");
        if (save) {
            if (!loaded) return fail("no mid-retreat save");
            int loadedFire=-1, loadedRelease=-1, observedSteps=0; RuntimeCellPoint loadedCell;
            // Use the same native slow-ship observation budget as the live scene.
            // At 15 Hz the actual DRED save tick 98 to live fire tick 569 needs
            // 471 ticks, exceeding the former unconditional 450-tick budget.
            const int restoreBudget=hz*(naval?60:30);
            for (int step=0;step!=restoreBudget;++step) {
                tick(*loaded);
                observedSteps=step+1;
                if (const auto* m=missile(*loaded,id)) {
                    loadedFire=m->startTick; loadedRelease=m->releaseTick;
                    const auto loadedGround=ground?runtimeGroundAttackTargetObject(*loaded,targetCell):std::optional<WorldObjectInstance>{};
                    const auto* loadedSource=findObjectById(*loaded,id);
                    const auto* loadedTarget=ground?(loadedGround?&*loadedGround:nullptr):findObjectById(*loaded,targetId);
                    if(!loadedSource||!loadedTarget)return fail("loaded prepared launcher or aim object disappeared");
                    loadedCell=runtimeCellPointForObject(*loadedSource);
                    if(!checkPrepared(*loaded,*loadedSource,*loadedTarget,*m,"restore"))return false;
                    break;
                }
            }
            std::cout<<"Minimum range restore state: "<<scene<<" phase=final observedSteps="<<observedSteps
                <<" budget="<<restoreBudget<<" fireStart="<<loadedFire<<" release="<<loadedRelease
                <<restoredStateDetail(*loaded)<<" status="<<loaded->simulationStatus<<'\n';
            // Ordinary v14 saves intentionally rebuild paths from the saved
            // committed cell. Exact multiplayer snapshots retain the complete path.
            if (loadedFire<0||(exact&&(loadedFire!=firedTick||loadedCell!=firedAt))) {
                std::ostringstream detail;
                detail<<"save/load retreat expected fire="<<firedTick<<" at="<<firedAt.x<<","<<firedAt.y
                    <<" actual="<<loadedFire<<" at="<<loadedCell.x<<","<<loadedCell.y
                    <<" observedSteps="<<observedSteps<<" budget="<<restoreBudget
                    <<" release="<<loadedRelease<<restoredStateDetail(*loaded)
                    <<" status="<<loaded->simulationStatus;
                return fail(detail.str());
            }
            std::cout<<"Minimum range restore: hz="<<hz<<" exact="<<exact
                <<" originalTick="<<firedTick<<" restoredTick="<<loadedFire
                <<" saveTick="<<savedTick<<" observedSteps="<<observedSteps
                <<" releaseTick="<<loadedRelease<<" PASS\n";
        }
        std::cout<<"Minimum range scene: "<<scene<<" moved="<<moved<<" fireTick="<<firedTick<<" PASS\n";
        ++cases;
    }
    std::cout<<"Minimum range: passed cases="<<cases<<" mid-retreat saves="<<saves
        <<" near/diagonal/building/blocked/movingTarget/load/Stop/inRange/exactSnapshot/ground/ownCell/wideBuilding/naval; V3=5..18 DRED=8..25\n";
    return true;
}
