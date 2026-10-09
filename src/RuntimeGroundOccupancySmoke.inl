#include "RuntimeGroundPoseSpatialSmoke.inl"

// Exercise committed steps as well as settled occupancy. A departure is still
// visible in its old tile until the step finishes, even though x/y already moved.
bool runRuntimeGroundOccupancySmoke(std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Ground occupancy: " + why; return false; };
    IniFile ini; RulesDatabase rules;
    if (!ini.loadFromString("[VehicleTypes]\n0=CAR\n1=FAST\n[InfantryTypes]\n0=MAN\n1=SLOWMAN\n2=FASTMAN\n"
        "[CAR]\nStrength=200\nSpeed=4\nROT=128\nSpeedType=Track\nMovementZone=Normal\n"
        "[FAST]\nStrength=200\nSpeed=10\nROT=128\nSpeedType=Track\nMovementZone=Normal\n"
        "[MAN]\nStrength=100\nSpeed=6\nROT=128\nSpeedType=Foot\nMovementZone=Normal\n"
        "[SLOWMAN]\nStrength=100\nSpeed=3\nROT=128\nSpeedType=Foot\nMovementZone=Normal\n"
        "[FASTMAN]\nStrength=100\nSpeed=10\nROT=128\nSpeedType=Foot\nMovementZone=Normal\n")
        || !rules.loadFromIni(ini)) return fail("fixture rules");
    const auto make = [&]() {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.rules = rules; s.rulesLoaded = true; s.config.fixedTickHz = 30;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y);
            c.tileIndex=c.subTileIndex=0; s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        for (auto& [key,info] : s.terrainInfoByCell) {
            info = RuntimeTerrainInfo{};
            info.authoredLandType=info.movementLandType=RuntimeTerrainLandType::Clear;
        }
        return box;
    };
    const auto add = [](RuntimeWindowState& s, int id, bool infantry, RuntimeCellPoint p, int slot=2) {
        WorldObjectInstance a; a.instanceId=id; a.type=infantry?"MAN":"CAR";
        a.kind=infantry?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        a.owner="Player"; a.x=p.x; a.y=p.y; a.cell=runtimeCellKey(p.x,p.y);
        a.health=infantry?100:200; a.alive=a.poweredOperational=true;
        a.subCell=infantry?slot:-1; a.mission="Guard"; a.facing=runtimeFacingFromDelta(1,0);
        s.world.accessObjects().push_back(a);
    };
    int cases=0;
    for (bool residentInfantry : {false,true}) for (int count=1;count<=(residentInfantry?3:1);++count) {
        auto box=make(); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        for(int i=0;i<count;++i) add(s,i+1,residentInfantry,{12,12},RuntimeSubcellSystemState::activeSubCells()[i]);
        add(s,10,true,{11,12}); add(s,11,false,{11,13}); ensureRuntimeSubcellsInitialized(s);
        const auto occupancy=buildRuntimeOccupancyLookup(s);
        for(int id:{10,11}) {
            const auto* mover=findObjectById(s,id);
            const bool expected=id==10 && residentInfantry && count<3;
            if(runtimeCellIsPassable(s,s.terrainByCell,occupancy,{12,12},*mover)!=expected)
                return fail("settled capacity/mixed tile");
        }
        ++cases;
    }
    for (bool departingInfantry : {true,false}) for (bool followerInfantry : {false,true})
        for (int departingSlot : departingInfantry ? std::vector<int>{2,4,3} : std::vector<int>{2}) {
        auto box=make(); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        // Slot 2 and all vehicle scenarios keep their original route. A slot-3
        // body leaves northwest, while a slot-4 follower enters from southeast,
        // so the other two canonical residents remain stationary obstacles.
        const RuntimeCellPoint departureTarget = departingInfantry && followerInfantry && departingSlot==3
            ? RuntimeCellPoint{11,11} : RuntimeCellPoint{13,12};
        const RuntimeCellPoint followerStart = departingInfantry && followerInfantry && departingSlot==4
            ? RuntimeCellPoint{13,13} : RuntimeCellPoint{11,12};
        add(s,1,departingInfantry,{12,12},departingSlot);
        std::vector<std::pair<int,int>> sourceResidents;
        if(departingInfantry && followerInfantry) {
            int residentId=2;
            for(int slot : {2,4,3}) if(slot!=departingSlot) {
                add(s,residentId,true,{12,12},slot);
                sourceResidents.emplace_back(residentId++,slot);
            }
        }
        add(s,10,followerInfantry,followerStart,departingInfantry?departingSlot:2);
        ensureRuntimeSubcellsInitialized(s);
        const auto sourceBodiesRemainSafe = [&] {
            if(!departingInfantry || !followerInfantry) return true;
            std::vector<int> ids{1,10};
            for(const auto [id,slot] : sourceResidents) {
                const auto* resident=findObjectById(s,id);
                if(!resident || runtimeCellPointForObject(*resident)!=RuntimeCellPoint{12,12}
                    || resident->subCell!=slot || resident->hasRenderPosition || resident->renderMoveActive)
                    return false;
                ids.push_back(id);
            }
            for(std::size_t a=0;a<ids.size();++a) for(std::size_t b=a+1;b<ids.size();++b) {
                const auto* first=findObjectById(s,ids[a]);const auto* second=findObjectById(s,ids[b]);
                if(!first || !second) return false;
                const auto firstSlot=worldObjectSubCellOffset(*first),secondSlot=worldObjectSubCellOffset(*second);
                const double dx=runtimeObjectRenderX(*first)+firstSlot.x-runtimeObjectRenderX(*second)-secondSlot.x;
                const double dy=runtimeObjectRenderY(*first)+firstSlot.y-runtimeObjectRenderY(*second)-secondSlot.y;
                const double distanceSquared=dx*dx+dy*dy;
                if(!std::isfinite(distanceSquared) || distanceSquared+1e-9<0.25) return false;
            }
            return true;
        };
        auto& step=s.behaviors[1]; step.kind=RuntimeBehaviorKind::Move;
        step.moveTarget=departureTarget; step.path={departureTarget};
        auto* mover=findMutableObjectById(s,1);
        if(departingInfantry && !reserveRuntimeInfantryMoveDestination(s,*mover,departureTarget))
            return fail("departure reservation");
        (void)runtimeTickOccupancyLookup(s);
        if(!advanceRuntimeMoveBehavior(s,*mover,step) || runtimeCellPointForObject(*mover)!=departureTarget
            || !mover->renderMoveActive) return fail("actual committed departure");
        if(!sourceBodiesRemainSafe()) return fail("source-slot departure changed residents or overlapped a body");
        const auto* follower=findObjectById(s,10);
        if (!departingInfantry && !followerInfantry) {
            auto* following=findMutableObjectById(s,10); auto& follow=s.behaviors[10];
            follow.kind=RuntimeBehaviorKind::Move; follow.moveTarget={12,12}; follow.path={{12,12}};
            if(!advanceRuntimeMoveBehavior(s,*following,follow) || following->x!=12)
                return fail("ordinary convoy did not follow");
            while(step.moveCooldownTicks>0 || follow.moveCooldownTicks>0) {
                ++s.tick; advanceRuntimeMoveBehavior(s,*mover,step);
                advanceRuntimeMoveBehavior(s,*following,follow);
                if(std::hypot(runtimeObjectRenderX(*mover)-runtimeObjectRenderX(*following),
                        runtimeObjectRenderY(*mover)-runtimeObjectRenderY(*following))<0.99
                    || runtimeCellPointForObject(*mover)==runtimeCellPointForObject(*following))
                    return fail("convoy bodies/cells overlapped");
            }
            ++cases; continue;
        }
        auto& follow=s.behaviors[10]; follow.kind=RuntimeBehaviorKind::Move;
        follow.moveTarget={12,12}; follow.path={{12,12}};
        auto* following=findMutableObjectById(s,10);
        if(followerInfantry && !reserveRuntimeInfantryMoveDestination(s,*following,{12,12}))
            return fail("follower reservation");
        const bool initialBodyBlocked=runtimeInfantryBodySegmentAdmissionBlocked(s,*following,followerStart,{12,12});
        const bool initialDepartureHeld=runtimeGroundDepartureHoldsTransition(s,*following,followerStart,{12,12});
        advanceRuntimeMoveBehavior(s,*following,follow);
        const auto firstFollowPath=follow.path.empty()?RuntimeCellPoint{-1,-1}:follow.path.front();
        if(runtimeCellPointForObject(*following)!=followerStart || following->renderMoveActive || follow.path.empty())
            return fail(std::string(departingInfantry?"infantry":"vehicle")+" departure released tile before visible step finished");
        if(!sourceBodiesRemainSafe()) return fail("held source-slot follower moved a resident or overlapped a body");
        RuntimeObjectBehavior replaced;
        inheritRuntimeVehicleMoveSegment(*mover,replaced,step);
        if(replaced.moveSegmentFrom!=step.moveSegmentFrom || replaced.moveCooldownTicks!=step.moveCooldownTicks)
            return fail("replacement order released departure");
        int restoredId=-1; RuntimeObjectBehavior restored;
        if(!parseRuntimeMultiplayerSnapshotBehaviorRecord(runtimeMultiplayerSnapshotBehaviorRecord(1,step),
                restoredId,restored) || restoredId!=1 || restored.moveSegmentFrom!=step.moveSegmentFrom
            || restored.moveCooldownTicks!=step.moveCooldownTicks) return fail("snapshot lost departure");
        const auto root=std::filesystem::path("build/smoke/feature-set-20/04-ground-occupancy/save-contract");
        std::filesystem::create_directories(root);
        const auto save=root/(std::to_string(departingInfantry)+"-"+std::to_string(followerInfantry)
            +"-slot-"+std::to_string(departingSlot)+".ini");
        std::string saveError; IniFile saved;
        if(!saveRuntimeState(s,pathString(save),saveError) || !saved.loadFromFile(pathString(save))
            || !parseRuntimeSavedBehaviorRecord(splitRuntimeSaveFields(saved.getValue("Behaviors","0","")),
                *mover,14,restoredId,restored) || restored.moveSegmentFrom!=step.moveSegmentFrom)
            return fail("ordinary save lost departure: "+saveError);
        clearRuntimeRenderPosition(*mover);
        advanceRuntimeMoveBehavior(s,*following,follow);
        if(runtimeCellPointForObject(*following)!=followerStart || following->renderMoveActive)
            return fail("presentation reset released departure");
        if(!sourceBodiesRemainSafe()) return fail("presentation reset changed source residents or body spacing");
        while(step.moveCooldownTicks>0) {
            ++s.tick; advanceRuntimeMoveBehavior(s,*mover,step);
            if(!sourceBodiesRemainSafe()) return fail("source-slot departure tick changed residents or body spacing");
        }
        ++s.tick;
        advanceRuntimeMoveBehavior(s,*following,follow);
        if(runtimeCellPointForObject(*following)!=RuntimeCellPoint{12,12} || !following->renderMoveActive) {
            const auto next=follow.path.empty()?RuntimeCellPoint{-1,-1}:follow.path.front();
            std::ostringstream details;details<<"departure did not release after actual arrival"
                <<" departInf="<<departingInfantry<<" followInf="<<followerInfantry<<" sourceSlot="<<departingSlot
                <<" initialBodyBlocked="<<initialBodyBlocked<<" initialDepartureHeld="<<initialDepartureHeld
                <<" firstPath="<<firstFollowPath.x<<','<<firstFollowPath.y
                <<" remainingPath="<<next.x<<','<<next.y<<" follower="<<following->x<<','<<following->y
                <<" render="<<following->renderMoveActive<<" slot="<<following->subCell
                <<" state="<<int(follow.state)<<" cooldown="<<follow.moveCooldownTicks;
            return fail(details.str());
        }
        if(departingInfantry && followerInfantry && following->subCell!=departingSlot)
            return fail("actual source release committed the follower to a different vacated slot");
        if(!sourceBodiesRemainSafe()) return fail("released source-slot follower changed residents or body spacing");
        if(departingInfantry && followerInfantry) {
            while(follow.moveCooldownTicks>0) {
                ++s.tick; advanceRuntimeMoveBehavior(s,*following,follow);
                if(!sourceBodiesRemainSafe())
                    return fail("source-slot follower tick changed residents or body spacing");
            }
            if(following->renderMoveActive
                || runtimeCellPointForObject(*following)!=RuntimeCellPoint{12,12}
                || following->subCell!=departingSlot)
                return fail("source-slot follower did not complete in the exact vacated slot");
            const double expectedBodyX=departingSlot==3?11.75:12.25;
            const double expectedBodyY=departingSlot==2?11.75:12.25;
            const auto actualSlot=worldObjectSubCellOffset(*following);
            if(std::abs(runtimeObjectRenderX(*following)+actualSlot.x-expectedBodyX)>1e-9
                || std::abs(runtimeObjectRenderY(*following)+actualSlot.y-expectedBodyY)>1e-9)
                return fail("source-slot follower visible endpoint disagrees with canonical body slot");
            if(!sourceBodiesRemainSafe())
                return fail("source-slot follower arrival changed residents or body spacing");
        }
        ++cases;
    }
    // Use the actual Stop command after a real committed step. Navigation is
    // already in the next cell while the body still occupies its departure.
    for (bool stoppedInfantry : {false, true}) for (double stopPhase : {0.0,0.75})
        for (bool diagonalStop : {false,true}) {
        if (stoppedInfantry && (stopPhase>0.0 || diagonalStop)) continue;
        auto box=make(); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        s.economy.owner="Player";
        RuntimeHumanHouseAuthority authority; authority.owner="Player";
        authority.canonicalOwner="player"; authority.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        add(s,1,stoppedInfantry,{12,12});
        add(s,10,false,{11,12}); add(s,11,true,{11,13});
        add(s,12,true,{12,11}); add(s,13,true,{13,11});
        ensureRuntimeSubcellsInitialized(s);
        auto* stopped=findMutableObjectById(s,1);
        auto& original=s.behaviors[1]; original.kind=RuntimeBehaviorKind::Move;
        const RuntimeCellPoint stopTarget{13,diagonalStop?13:12};
        original.moveTarget=stopTarget; original.path={stopTarget};
        stopped->facing=runtimeFacingFromDelta(1,diagonalStop?1:0);
        if(stoppedInfantry && !reserveRuntimeInfantryMoveDestination(s,*stopped,stopTarget))
            return fail("Stop departure reservation");
        (void)runtimeTickOccupancyLookup(s);
        if(!advanceRuntimeMoveBehavior(s,*stopped,original) || stopped->x!=13
            || !stopped->renderMoveActive) return fail("Stop actual departure");
        ++s.tick; advanceRuntimeMoveBehavior(s,*stopped,original);
        while(1.0-double(original.renderMoveTicksRemaining)/original.renderMoveTicksTotal<stopPhase) {
            ++s.tick; advanceRuntimeMoveBehavior(s,*stopped,original);
            if(!stopped->renderMoveActive) return fail("late Stop missed active segment");
        }
        const double stoppedX=stopped->renderX, stoppedY=stopped->renderY;
        s.selectedObjectIds={1}; issueRuntimeStopCommand(s);
        if(s.lastCommand.acceptedCount!=1 || !worldObjectHasStoppedGroundPose(*stopped)
            || stopped->renderX!=stoppedX || stopped->renderY!=stoppedY
            || s.behaviors.contains(1)) return fail("Stop moved or retained its behavior");
        auto* car=findMutableObjectById(s,10); auto& drive=s.behaviors[10];
        drive.kind=RuntimeBehaviorKind::Move; drive.moveTarget={12,12}; drive.path={{12,12}};
        advanceRuntimeMoveBehavior(s,*car,drive);
        if(car->x!=11 || car->renderMoveActive)
            return fail("Stop physical departure was reused by a second vehicle");
        // Retire the rejected test driver so it does not block the later
        // infantry diagonal side cell for an unrelated reason.
        car->alive=false; car->health=0; invalidateRuntimeTickOccupancyCache(s);
        if (!stoppedInfantry && diagonalStop && stopPhase>0.0) {
            const auto placed=runtimeGroundMoveSegmentPlacementCells(s);
            for(auto cell:{RuntimeCellPoint{12,12},RuntimeCellPoint{13,12},RuntimeCellPoint{12,13}}) {
                if(!placed.contains(runtimeCellKey(cell.x,cell.y)))
                    return fail("late diagonal Stop vehicle released a touched foundation cell");
                if(!s.subcells.availableSubCells(s.world.getObjects(),s.world.getTerrainObjects(),
                        runtimeSubcellLocationForCell(s,cell),{11}).empty())
                    return fail("late diagonal Stop vehicle released a touched infantry cell");
            }
        }
        auto* first=findMutableObjectById(s,11);
        const auto destination=runtimeSubcellLocationForCell(s,{12,12});
        const auto slots=s.subcells.availableSubCells(s.world.getObjects(),
            s.world.getTerrainObjects(),destination,{11});
        if(!stoppedInfantry) {
            if(!slots.empty() || reserveRuntimeInfantryMoveDestination(s,*first,{12,12}))
                return fail("Stop vehicle body was reused by infantry");
        } else {
            if(slots!=std::vector<int>({4,3}))
                return fail("Stop infantry did not retain exactly its physical subcell");
            auto& firstMove=s.behaviors[11];firstMove.kind=RuntimeBehaviorKind::Move;
            firstMove.moveTarget={12,12};firstMove.path={{12,12}};
            if(!reserveRuntimeInfantryMoveDestination(s,*first,{12,12})
                || !advanceRuntimeMoveBehavior(s,*first,firstMove) || first->subCell!=4)
                return fail("Stop infantry prevented the first safe free-slot approach");
            auto* second=findMutableObjectById(s,12);auto& secondMove=s.behaviors[12];
            secondMove.kind=RuntimeBehaviorKind::Move;secondMove.moveTarget={12,12};
            // The original northern direct route crosses the stopped slot-2
            // body. Preserve three-slot capacity through a real safe side step.
            secondMove.path={{11,12},{12,12}};
            if(!reserveRuntimeInfantryMoveDestination(s,*second,{11,12})
                || !advanceRuntimeMoveBehavior(s,*second,secondMove) || second->x!=11 || second->y!=12)
                return fail("Stop infantry prevented the second safe side-step approach");
            const auto safeStoppedResidents=[&] {
                if(stopped->renderX!=stoppedX || stopped->renderY!=stoppedY || stopped->renderMoveActive)
                    return false;
                for(int a:{1,11,12,13}) for(int b:{1,11,12,13}) if(a<b) {
                    const auto* firstBody=findObjectById(s,a);const auto* secondBody=findObjectById(s,b);
                    const auto firstSlot=worldObjectSubCellOffset(*firstBody),secondSlot=worldObjectSubCellOffset(*secondBody);
                    const double dx=runtimeObjectRenderX(*firstBody)+firstSlot.x-runtimeObjectRenderX(*secondBody)-secondSlot.x;
                    const double dy=runtimeObjectRenderY(*firstBody)+firstSlot.y-runtimeObjectRenderY(*secondBody)-secondSlot.y;
                    if(dx*dx+dy*dy+1e-9<0.25) return false;
                }
                return true;
            };
            bool settled=false;
            for(int n=0;n<240 && !settled;++n) {
                ++s.tick;
                for(int id:{11,12}) {
                    auto* incoming=findMutableObjectById(s,id);auto& follow=s.behaviors[id];
                    if(follow.moveCooldownTicks<=0 && !follow.path.empty()
                        && !reserveRuntimeInfantryMoveDestination(s,*incoming,follow.path.front()))
                        return fail("Stop free-slot safe route reservation");
                    (void)advanceRuntimeMoveBehavior(s,*incoming,follow);
                    if(!safeStoppedResidents()) return fail("Stop free-slot route moved the stopped body or overlapped a resident");
                }
                settled=true;
                for(int id:{11,12}) {
                    const auto* incoming=findObjectById(s,id);const auto& follow=s.behaviors.at(id);
                    settled=settled && incoming->x==12 && incoming->y==12 && incoming->subCell!=2
                        && !incoming->renderMoveActive && follow.moveCooldownTicks<=0 && follow.renderMoveTicksRemaining<=0
                        && follow.state==RuntimeBehaviorState::Complete
                        && std::abs(runtimeObjectRenderX(*incoming)-12.0)<=1e-9
                        && std::abs(runtimeObjectRenderY(*incoming)-12.0)<=1e-9;
                }
            }
            if(!settled || first->subCell!=4 || second->subCell!=3)
                return fail("Stop infantry's two free subcells did not really settle via safe body routes");
            auto* fourth=findMutableObjectById(s,13);
            if(reserveRuntimeInfantryMoveDestination(s,*fourth,{12,12}))
                return fail("Stop infantry allowed four physical residents");
            // The same saved pose can be active while a resumed step first
            // returns through its committed cell. Keep the old physical slot.
            auto& resumed=s.behaviors[1]; resumed.kind=RuntimeBehaviorKind::Move;
            resumed.moveTarget={14,12}; resumed.path={{14,12}};
            if(!reserveRuntimeInfantryMoveDestination(s,*stopped,{14,12})
                || !advanceRuntimeMoveBehavior(s,*stopped,resumed)
                || !stopped->renderMoveActive)
                return fail("Stop infantry resumed departure");
            if(reserveRuntimeInfantryMoveDestination(s,*fourth,{12,12}))
                return fail("resumed fractional pose released its physical subcell");
        }
        ++cases;
    }
    // A resumed first leg can cross a 45-degree line unrelated to its
    // accepted departure. Current-point guessing must not release that tile.
    {
        auto box=make(); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        s.economy.owner="Player";
        RuntimeHumanHouseAuthority authority; authority.owner="Player";
        authority.canonicalOwner="player"; authority.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        add(s,1,true,{14,11}); add(s,10,false,{15,11}); add(s,11,true,{14,10});
        ensureRuntimeSubcellsInitialized(s);
        auto* actor=findMutableObjectById(s,1); actor->facing=runtimeFacingFromDelta(-1,1);
        auto& initial=s.behaviors[1]; initial.kind=RuntimeBehaviorKind::Move;
        initial.moveTarget={13,12}; initial.path={{13,12}};
        if(!reserveRuntimeInfantryMoveDestination(s,*actor,{13,12})
            || !advanceRuntimeMoveBehavior(s,*actor,initial) || !actor->renderMoveActive)
            return fail("fractional curve initial step");
        const auto diagonal=runtimeGroundRenderMovePoint(*actor,initial,0.2);
        actor->renderX=diagonal[0]; actor->renderY=diagonal[1];
        s.selectedObjectIds={1}; issueRuntimeStopCommand(s);
        if(s.lastCommand.acceptedCount!=1) return fail("fractional curve Stop command");
        auto& resumed=s.behaviors[1]; resumed.kind=RuntimeBehaviorKind::Move;
        resumed.moveTarget={14,12}; resumed.path={{14,12}};
        actor->facing=runtimeFacingFromDelta(1,0);
        if(!reserveRuntimeInfantryMoveDestination(s,*actor,{14,12})
            || !advanceRuntimeMoveBehavior(s,*actor,resumed)
            || !runtimeInfantryMoveUsesCommittedCorner(*actor,resumed))
            return fail("fractional curve resume step");
        // Fifteen ticks allocates eight to this first leg. Its third fixed
        // tick is exactly (13.5,11.5), then its infantry body is (13.75,11.25).
        resumed.renderMoveTicksTotal=resumed.renderMoveTicksRemaining=resumed.moveCooldownTicks=15;
        (void)runtimeTickOccupancyLookup(s);
        for(int n=0;n<3;++n) advanceRuntimeMoveBehavior(s,*actor,resumed);
        if(std::abs(actor->renderX-13.5)>1e-9 || std::abs(actor->renderY-11.5)>1e-9)
            return fail("fractional curve did not reach 45-degree crossing");
        const auto context=runtimeGateSubcellContext(s,actor);
        const auto destination=runtimeSubcellLocationForCell(s,{14,11});
        const auto expected=std::vector<int>{2,4};
        if(s.subcells.availableSubCells(s.world.getObjects(),s.world.getTerrainObjects(),
                destination,{11},context)!=expected)
            return fail("fractional curve released physical slot at 45-degree crossing");
        const auto batch=s.subcells.availableSubCellsBatch(s.world.getObjects(),
            s.world.getTerrainObjects(),{destination},{11},context);
        const auto reference=s.subcells.availableSubCellsBatch(s.world.getObjects(),
            s.world.getTerrainObjects(),{destination},{11},context,true);
        if(batch!=std::vector<std::vector<int>>{expected} || batch!=reference)
            return fail("fractional curve batch/reference held slots");
        {
            RuntimeSubcellReadOnlyQueryScope queries(s.world.getObjects(),true,runtimeGroundPoseContext(s));
            if(s.subcells.availableSubCells(s.world.getObjects(),s.world.getTerrainObjects(),
                    destination,{11},context)!=expected)
                return fail("fractional curve readonly held slots");
        }
        const auto* car=findObjectById(s,10);
        if(!runtimeGroundDepartureHoldsCell(s,*car,{14,11}))
            return fail("fractional curve released physical tile to a vehicle");
        {
            RuntimeGroundDepartureQueryBatch query(s,*car,{15,11});
            if(!runtimeGroundDepartureHoldsCell(s,*car,{14,11}))
                return fail("fractional curve departure batch released physical tile");
        }
        {
            RuntimePlannerEmptyCellCensusScope census(s);
            if(census.provesEmpty(s,{14,11}) || runtimeMovementTickProvesEmptyCell(s,{14,11}))
                return fail("fractional curve empty proof released physical tile");
        }
        const auto placement=runtimeGroundMoveSegmentPlacementCells(s);
        for(auto cell:{RuntimeCellPoint{13,11},RuntimeCellPoint{14,11},
                RuntimeCellPoint{13,12},RuntimeCellPoint{14,12}})
            if(!placement.contains(runtimeCellKey(cell.x,cell.y)))
                return fail("fractional first-leg foundation corridor released a side cell");
        ++cases;
    }
    // Different authored speeds must not erase the one-cell convoy spacing.
    // The fast follower already owns its accepted destination; its presentation
    // waits as necessary rather than overtaking the slow vehicle's body.
    for(bool leaderFirst : {true,false}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        add(s,1,false,{12,12});add(s,10,false,{11,12});
        auto* leader=findMutableObjectById(s,1);auto* follower=findMutableObjectById(s,10);
        follower->type="FAST";ensureRuntimeSubcellsInitialized(s);
        auto& leading=s.behaviors[1];leading.kind=RuntimeBehaviorKind::Move;
        leading.moveTarget={13,12};leading.path={{13,12}};
        auto& following=s.behaviors[10];following.kind=RuntimeBehaviorKind::Move;
        following.moveTarget={12,12};following.path={{12,12}};
        if(!advanceRuntimeMoveBehavior(s,*leader,leading) || leader->x!=13
            || !advanceRuntimeMoveBehavior(s,*follower,following) || follower->x!=12
            || !leader->renderMoveActive || !follower->renderMoveActive
            || leading.renderMoveTicksTotal<=following.renderMoveTicksTotal)
            return fail("different-speed convoy actual accepted intervals");
        for(int n=0;n<240 && following.moveCooldownTicks>0;++n) {
            ++s.tick;
            if(leaderFirst) {
                advanceRuntimeMoveBehavior(s,*leader,leading);advanceRuntimeMoveBehavior(s,*follower,following);
            } else {
                advanceRuntimeMoveBehavior(s,*follower,following);advanceRuntimeMoveBehavior(s,*leader,leading);
            }
            if(std::hypot(runtimeObjectRenderX(*leader)-runtimeObjectRenderX(*follower),
                    runtimeObjectRenderY(*leader)-runtimeObjectRenderY(*follower))+1e-9<1.0)
                return fail("fast convoy bodies overlapped slow leader");
        }
        if(following.moveCooldownTicks>0 || follower->renderMoveActive || follower->x!=12)
            return fail("different-speed convoy did not finish after spacing released");
        ++cases;
    }
    // Different infantry speeds can close below the half-cell spacing even
    // while both accepted cells and the three-slot capacity remain correct.
    // Exercise the same reserved slot, with either actor advanced first.
    for(bool leaderFirst : {true,false}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        add(s,1,true,{12,12});add(s,10,true,{11,12});
        auto* leader=findMutableObjectById(s,1);auto* follower=findMutableObjectById(s,10);
        leader->type="SLOWMAN";follower->type="FASTMAN";ensureRuntimeSubcellsInitialized(s);
        auto& leading=s.behaviors[1];leading.kind=RuntimeBehaviorKind::Move;
        leading.moveTarget={13,12};leading.path={{13,12}};
        auto& following=s.behaviors[10];following.kind=RuntimeBehaviorKind::Move;
        following.moveTarget={12,12};following.path={{12,12}};
        if(!reserveRuntimeInfantryMoveDestination(s,*leader,{13,12})
            || !advanceRuntimeMoveBehavior(s,*leader,leading) || leader->x!=13
            || !reserveRuntimeInfantryMoveDestination(s,*follower,{12,12})
            || !advanceRuntimeMoveBehavior(s,*follower,following) || follower->x!=12
            || !leader->renderMoveActive || !follower->renderMoveActive
            || leading.renderMoveTicksTotal<=following.renderMoveTicksTotal
            || leader->subCell!=follower->subCell)
            return fail("different-speed infantry actual accepted same-slot intervals");
        for(int n=0;n<240 && following.moveCooldownTicks>0;++n) {
            ++s.tick;
            if(leaderFirst) {
                advanceRuntimeMoveBehavior(s,*leader,leading);advanceRuntimeMoveBehavior(s,*follower,following);
            } else {
                advanceRuntimeMoveBehavior(s,*follower,following);advanceRuntimeMoveBehavior(s,*leader,leading);
            }
            const auto a=worldObjectSubCellOffset(*leader),b=worldObjectSubCellOffset(*follower);
            if(std::hypot(runtimeObjectRenderX(*leader)+a.x-runtimeObjectRenderX(*follower)-b.x,
                    runtimeObjectRenderY(*leader)+a.y-runtimeObjectRenderY(*follower)-b.y)+1e-9<0.5)
                return fail("fast infantry convoy bodies overlapped slow leader");
        }
        if(following.moveCooldownTicks>0 || follower->renderMoveActive || follower->x!=12
            || follower->subCell!=leader->subCell)
            return fail("different-speed infantry did not finish its accepted same-slot step");
        ++cases;
    }
    // Both steps commit before Stop: checking only later cell commits cannot
    // protect this accepted follower. Freeze its presentation and timers until
    // the leader moves again, then complete the same accepted segment.
    for(bool infantry : {false,true}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        s.economy.owner="Player";
        RuntimeHumanHouseAuthority authority;authority.owner="Player";
        authority.canonicalOwner="player";authority.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        add(s,1,infantry,{12,12});add(s,10,infantry,{11,12});
        ensureRuntimeSubcellsInitialized(s);
        auto* leader=findMutableObjectById(s,1);auto* follower=findMutableObjectById(s,10);
        auto& leading=s.behaviors[1];leading.kind=RuntimeBehaviorKind::Move;
        leading.moveTarget={13,12};leading.path={{13,12}};
        if(infantry && !reserveRuntimeInfantryMoveDestination(s,*leader,{13,12}))
            return fail("accepted convoy leader reservation");
        if(!advanceRuntimeMoveBehavior(s,*leader,leading) || leader->x!=13 || !leader->renderMoveActive)
            return fail("accepted convoy leader step");
        auto& following=s.behaviors[10];following.kind=RuntimeBehaviorKind::Move;
        following.moveTarget={12,12};following.path={{12,12}};
        if(infantry && !reserveRuntimeInfantryMoveDestination(s,*follower,{12,12}))
            return fail("accepted same-slot infantry follower reservation");
        if(!advanceRuntimeMoveBehavior(s,*follower,following) || follower->x!=12
            || !follower->renderMoveActive || (infantry && follower->subCell!=leader->subCell))
            return fail("accepted convoy follower step");
        ++s.tick;advanceRuntimeMoveBehavior(s,*leader,leading);
        advanceRuntimeMoveBehavior(s,*follower,following);
        s.selectedObjectIds={1};issueRuntimeStopCommand(s);
        if(s.lastCommand.acceptedCount!=1 || !worldObjectHasStoppedGroundPose(*leader))
            return fail("accepted convoy Stop leader");
        const auto body=[](const WorldObjectInstance& object) {
            const auto offset=worldObjectSubCellOffset(object);
            return std::array<double,2>{runtimeObjectRenderX(object)+offset.x,runtimeObjectRenderY(object)+offset.y};
        };
        const double minimum=infantry?0.5:1.0;
        const auto safe=[&] {
            const auto a=body(*leader),b=body(*follower);
            return std::hypot(a[0]-b[0],a[1]-b[1])+1e-9>=minimum;
        };
        bool waited=false;
        for(int n=0;n<240 && following.moveCooldownTicks>0;++n) {
            const int cooldown=following.moveCooldownTicks,remaining=following.renderMoveTicksRemaining;
            ++s.tick;advanceRuntimeMoveBehavior(s,*follower,following);
            if(!safe()) return fail("accepted convoy ran into stopped leader");
            if(following.moveCooldownTicks==cooldown && following.renderMoveTicksRemaining==remaining) {
                waited=true;break;
            }
        }
        if(!waited || !follower->renderMoveActive) return fail("accepted convoy did not wait in its segment");
        const int cooldown=following.moveCooldownTicks,remaining=following.renderMoveTicksRemaining;
        const auto waitingBody=body(*follower);
        for(int n=0;n<8;++n) {++s.tick;advanceRuntimeMoveBehavior(s,*follower,following);}
        if(following.moveCooldownTicks!=cooldown || following.renderMoveTicksRemaining!=remaining
            || body(*follower)!=waitingBody || !safe())
            return fail("accepted convoy waiting changed pose or movement clocks");
        // Existing overlaps can escape, but cannot close further.
        const double beforeX=follower->renderX,beforeY=follower->renderY;
        follower->renderX=leader->renderX-minimum*0.5;follower->renderY=leader->renderY;
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,*follower,follower->renderX-0.1,follower->renderY)
            || !runtimeStoppedGroundPoseBlocksRenderMove(s,*follower,follower->renderX+0.1,follower->renderY))
            return fail("accepted convoy overlap escape direction");
        follower->renderX=beforeX;follower->renderY=beforeY;
        auto& resumed=s.behaviors[1];resumed.kind=RuntimeBehaviorKind::Move;
        resumed.moveTarget={14,12};resumed.path={{14,12}};
        if(infantry && !reserveRuntimeInfantryMoveDestination(s,*leader,{14,12}))
            return fail("accepted convoy leader resume reservation");
        if(!advanceRuntimeMoveBehavior(s,*leader,resumed) || !leader->renderMoveActive)
            return fail("accepted convoy leader resume step");
        for(int n=0;n<240 && following.moveCooldownTicks>0;++n) {
            ++s.tick;advanceRuntimeMoveBehavior(s,*leader,resumed);
            advanceRuntimeMoveBehavior(s,*follower,following);
            if(!safe()) return fail("accepted convoy overlapped after leader resume");
        }
        if(following.moveCooldownTicks>0 || follower->renderMoveActive || follower->x!=12)
            return fail("accepted convoy did not release its same segment after leader resume");
        ++cases;
    }
    // Opposing diagonal moves have disjoint cell destinations but their body
    // trajectories cross. Keeping both accepted steps frozen at contact must
    // not leave the two real Move orders waiting forever.
    for(bool firstActorFirst : {true,false}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        add(s,1,true,{10,10},2);add(s,10,true,{11,10},2);
        ensureRuntimeSubcellsInitialized(s);
        auto* first=findMutableObjectById(s,1);auto* second=findMutableObjectById(s,10);
        auto& firstMove=s.behaviors[1];firstMove.kind=RuntimeBehaviorKind::Move;
        firstMove.moveTarget={11,11};firstMove.path={{11,11}};
        auto& secondMove=s.behaviors[10];secondMove.kind=RuntimeBehaviorKind::Move;
        secondMove.moveTarget={10,11};secondMove.path={{10,11}};
        if(!reserveRuntimeInfantryMoveDestination(s,*first,{11,11})
            || !reserveRuntimeInfantryMoveDestination(s,*second,{10,11}))
            return fail("opposing diagonal infantry reservations");
        const auto safe=[&]() {
            const auto a=worldObjectSubCellOffset(*first),b=worldObjectSubCellOffset(*second);
            return std::hypot(runtimeObjectRenderX(*first)+a.x-runtimeObjectRenderX(*second)-b.x,
                runtimeObjectRenderY(*first)+a.y-runtimeObjectRenderY(*second)-b.y)+1e-9>=0.5;
        };
        const auto done=[&]() {
            return first->x==11 && first->y==11 && second->x==10 && second->y==11
                && firstMove.moveTarget==RuntimeCellPoint{11,11} && secondMove.moveTarget==RuntimeCellPoint{10,11}
                && !first->renderMoveActive && !second->renderMoveActive
                && firstMove.moveCooldownTicks<=0 && secondMove.moveCooldownTicks<=0
                && firstMove.renderMoveTicksRemaining<=0 && secondMove.renderMoveTicksRemaining<=0
                && firstMove.state==RuntimeBehaviorState::Complete && secondMove.state==RuntimeBehaviorState::Complete
                && std::abs(runtimeObjectRenderX(*first)-11.0)<=1e-9 && std::abs(runtimeObjectRenderY(*first)-11.0)<=1e-9
                && std::abs(runtimeObjectRenderX(*second)-10.0)<=1e-9 && std::abs(runtimeObjectRenderY(*second)-11.0)<=1e-9;
        };
        for(int n=0;n<240 && !done();++n) {
            ++s.tick;
            // Match the real simulation's order: a queued body detour needs
            // its next reservation, and a consumed detour needs path results.
            (void)processRuntimePathRequests(s);
            prepareRuntimeInfantryStepReservations(s);
            if(firstActorFirst) {
                advanceRuntimeMoveBehavior(s,*first,firstMove);
                if(!safe()) return fail("opposing diagonal infantry bodies overlapped after first actor");
                advanceRuntimeMoveBehavior(s,*second,secondMove);
            } else {
                advanceRuntimeMoveBehavior(s,*second,secondMove);
                if(!safe()) return fail("opposing diagonal infantry bodies overlapped after second actor");
                advanceRuntimeMoveBehavior(s,*first,firstMove);
            }
            if(!safe()) return fail("opposing diagonal infantry bodies overlapped after pair");
        }
        if(!done()) return fail("opposing diagonal infantry Move orders did not both finish");
        ++cases;
    }
    if(cases!=26) return fail("source-slot departure coverage count");
    if(!runRuntimeGroundPoseSpatialSmoke(error)) return false;
    std::cout<<"Ground occupancy: passed cases="<<cases<<" settledCapacity=3-or-1 mixed=no departuresHeld=yes stopPhysicalHeld=early-late-diagonal stoppedInfantryCapacity=3 resumeHeld=yes acceptedConvoyWaitRelease=yes differentSpeedSpacing=vehicle-and-infantry opposingDiagonals=complete release=yes save/snapshot/order/reset=yes\n";
    return true;
}
