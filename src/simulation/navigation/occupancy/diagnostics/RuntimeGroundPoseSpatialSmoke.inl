bool runRuntimeGroundPoseSpatialSmoke(std::string& error) {
    const auto fail=[&](const std::string& why) {error="Ground pose spatial: "+why;return false;};
    IniFile ini;RulesDatabase rules;
    if(!ini.loadFromString("[VehicleTypes]\n0=SCAR\n1=SMINER\n[InfantryTypes]\n0=SMAN\n1=SWORK\n"
        "[BuildingTypes]\n0=SENTRY\n1=SDEPOT\n"
        "[SCAR]\nStrength=200\nSpeed=8\nROT=128\nSpeedType=Wheel\nMovementZone=Normal\n"
        "[SMAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
        "[SMINER]\nStrength=200\nSpeed=8\nSpeedType=Wheel\nMovementZone=Normal\n"
        "ResourceGatherer=yes\nResourceDestination=yes\nSlavesNumber=5\nDeploysInto=SDEPOT\n"
        "[SWORK]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\nSlaved=yes\n"
        "[SENTRY]\nStrength=1000\n[SDEPOT]\nStrength=1000\nEnslaves=SWORK\n")
        || !rules.loadFromIni(ini)) return fail("rules");
    const auto make=[&] {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.rules=rules;s.rulesLoaded=true;s.config.fixedTickHz=30;s.config.computerAiEnabled=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);
        for(auto& [key,info]:s.terrainInfoByCell) {(void)key;info=RuntimeTerrainInfo{};}
        return box;
    };
    const auto actor=[](int id,bool foot,int x,int y,int slot=-1) {
        WorldObjectInstance o;o.instanceId=id;o.kind=foot?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        o.type=foot?"SMAN":"SCAR";o.owner="Player";o.alive=true;o.health=100;o.mission="Guard";
        o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.subCell=slot;return o;
    };
    using Held=std::array<long long,5>;
    const auto compare=[&](RuntimeWindowState& s,RuntimeCellPoint cell) {
        std::vector<Held> expected,actual;const auto context=runtimeGroundPoseContext(s);
        const auto append=[](auto& entries,const auto& o,const auto& held) {
            entries.push_back(Held{o.instanceId,held.x,held.y,held.subCell,held.onBridge});
        };
        // Independent full-object scalar inventory, not the cached candidates.
        for(const auto& o:s.world.getObjects()) {
            if(runtimeObjectOffGroundByMagBeam(s,o.instanceId)
                || runtimeObjectMovementMode(s,o)==PathMovementMode::Air) continue;
            forEachRuntimeAdditionalGroundPoseOccupancy(o,[&](const auto& held) {
                if(held.x==cell.x && held.y==cell.y) append(expected,o,held);
            },&context);
        }
        forEachRuntimeAdditionalGroundPoseOccupantAtCell(s,cell,[&](const auto& o,const auto& held) {
            append(actual,o,held);
        });
        std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
        return actual==expected;
    };
    int checks=0;std::size_t farVisits=0;
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto moving=actor(1,true,14,12,2);moving.hasRenderPosition=moving.renderMoveActive=true;
        moving.renderX=13.8;moving.renderY=11.2;
        s.world.accessObjects()={moving,actor(2,false,15,11)};
        auto& bend=s.behaviors[1];bend.kind=RuntimeBehaviorKind::Move;
        bend.renderFrom={13,12};bend.renderTo=bend.moveTarget={14,12};
        bend.renderFromOffsetX=0.8;bend.renderFromOffsetY=-0.8;
        bend.renderMoveTicksTotal=bend.renderMoveTicksRemaining=bend.moveCooldownTicks=15;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeGroundPoseSpatialIndexIsCurrent(s) || !runtimeMovementTickProvesEmptyCell(s,{5,5}))
            return fail("active envelope revoked a distant empty proof");
        for(int y=9;y<=14;++y) for(int x=11;x<=16;++x)
            if(!compare(s,{x,y})) return fail("active start buckets differ from scalar");
        auto* current=findMutableObjectById(s,1);
        for(int n=0;n<8;++n) if(!advanceRuntimeRenderMove(s,*current,bend))
            return fail("actual first-leg interpolation");
        // Query explicitly with the runtime context: no tick increment or
        // cache rebuild occurs while the body enters this different cell.
        const auto context=runtimeGroundPoseContext(s);
        const auto newHeld=runtimeAdditionalGroundPoseOccupancy(*current,&context);
        if(!newHeld || newHeld->x!=13 || newHeld->y!=12 || !runtimeGroundPoseSpatialIndexIsCurrent(s))
            return fail("same-tick first leg did not reach the new physical cell");
        for(int y=9;y<=14;++y) for(int x=11;x<=16;++x)
            if(!compare(s,{x,y})) return fail("same-tick future envelope differs from scalar");
        if(runtimeMovementTickProvesEmptyCell(s,{13,12})) return fail("active held cell proved empty");
        current->alive=false;current->health=0;
        if(!compare(s,{13,12})) return fail("cached dead candidate became occupancy");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto wheel=actor(1,false,20,20);wheel.hasRenderPosition=true;wheel.renderX=wheel.renderY=19.75;
        s.world.accessObjects()={wheel,actor(2,false,25,25)};
        for(int slot:{2,4,3}) {
            auto foot=actor(slot+2,true,8,8,slot);foot.hasRenderPosition=true;
            foot.renderX=foot.renderY=8.05;s.world.accessObjects().push_back(foot);
        }
        for(int n=0;n<5000;++n) {
            auto paused=actor(n+10,false,100+n%100,100+n/100);paused.hasRenderPosition=true;
            paused.renderX=paused.x-0.25;paused.renderY=paused.y-0.25;
            s.world.accessObjects().push_back(paused);
        }
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeGroundPoseSpatialIndexIsCurrent(s)) return fail("paused army index not ready");
        for(const auto cell:{RuntimeCellPoint{19,19},RuntimeCellPoint{20,19},RuntimeCellPoint{19,20},RuntimeCellPoint{20,20},RuntimeCellPoint{8,8}})
            if(!compare(s,cell)) return fail("stopped wheel/three-slot bucket differs from scalar");
        const auto* car=findObjectById(s,2);
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*car,20.1,19.75)
            || !runtimeStoppedGroundPoseBlocksRenderMove(s,*car,8.3,8.05))
            return fail("same-navigation stopped body missing from spacing guard");
        for(int n=0;n<1000;++n) for(int y=3;y<=5;++y) for(int x=3;x<=5;++x)
            forEachRuntimeGroundPoseCandidateAtCell(s,{x,y},[](const auto&) {},&farVisits);
        if(farVisits!=0 || runtimeStoppedGroundPoseBlocksRenderMove(s,*car,4,4)
            || !runtimeMovementTickProvesEmptyCell(s,{4,4}))
            return fail("far paused army caused candidate visits or false occupancy");
        s.movementEmptyCellProof.additionalGroundPoseIndexReady=false;
        if(!compare(s,{19,19})) return fail("invalid index fallback differs from scalar");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects().push_back(actor(1,false,25,25));
        for(int n=0;n<4000;++n) s.world.accessObjects().push_back(actor(n+2,false,100+n%100,100+n/100));
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto& proof=s.movementEmptyCellProof;
        // A harmless old candidate bucket is retained for the rest of the
        // tick. Its disappearance would expose an unintended full rebuild.
        const int oldBucket=runtimeCellKey(31,31);
        proof.additionalGroundPoseIndicesByCell[oldBucket];
        for(std::size_t n=1;n<s.world.getObjects().size();++n) {
            auto& object=s.world.accessObjects()[n];auto& move=s.behaviors[object.instanceId];
            const RuntimeCellPoint from{object.x,object.y},to{object.x+1,object.y};
            object.x=to.x;object.y=to.y;object.cell=runtimeCellKey(to.x,to.y);
            move.kind=RuntimeBehaviorKind::Move;move.moveCooldownTicks=10;
            beginRuntimeRenderMove(object,move,from,to,0.0,false,10,&s);
            if(!runtimeTrackGroundPoseSpatialMove(s,object) || !runtimeTrackGroundPoseSpatialMove(s,object))
                return fail("ordinary accepted segment could not append its local envelope");
        }
        if(!runtimeGroundPoseSpatialIndexIsCurrent(s) || proof.additionalGroundPoseObjectIndices.size()!=4000
            || !proof.additionalGroundPoseIndicesByCell.contains(oldBucket)
            || !runtimeMovementTickProvesEmptyCell(s,{4,4}))
            return fail("4000 same-tick starts rebuilt/revoked the cache or duplicated inventory");
        for(std::size_t n=1;n<s.world.getObjects().size();++n)
            if(!proof.additionalGroundPoseIndexedObjects[n]) return fail("new active body was not listed");
        std::size_t nearVisits=0,emptyVisits=0;
        forEachRuntimeGroundPoseCandidateAtCell(s,{100,100},[](const auto&) {},&nearVisits);
        for(int n=0;n<4000;++n)
            forEachRuntimeGroundPoseCandidateAtCell(s,{4,4},[](const auto&) {},&emptyVisits);
        if(nearVisits!=1 || emptyVisits!=0) return fail("hot append duplicated a bucket or visited far bodies");
        const auto* car=findObjectById(s,1);
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*car,100.1,100.0)
            || !compare(s,{100,100})) return fail("ordinary moving body bypassed local spacing/occupancy query");
        int held=0;
        forEachRuntimeAdditionalGroundPoseOccupantAtCell(s,{100,100},[&](const auto&,const auto&) {++held;});
        if(held!=0) return fail("ordinary active body changed additional-cell convoy capacity");
        // Already-overlapping bodies must be able to separate; approaching
        // the same moving body while overlapped still waits.
        auto overlap=actor(99999,false,100,100);overlap.hasRenderPosition=true;
        overlap.renderX=99.8;overlap.renderY=100.0;
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,overlap,99.7,100.0)
            || !runtimeStoppedGroundPoseBlocksRenderMove(s,overlap,99.9,100.0))
            return fail("moving overlap could not separate or was allowed to close");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto blocker=actor(2,false,11,10);blocker.hasRenderPosition=true;
        blocker.renderX=10.8;blocker.renderY=10.0;blocker.renderOnBridge=true;
        // Deliberately opposed committed/body layers prove which is queried.
        blocker.onBridge=false;s.world.accessObjects()={actor(1,false,9,10),blocker};
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* mover=findMutableObjectById(s,1);auto* other=findMutableObjectById(s,2);
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,*mover,10.8,10.0))
            return fail("same xy on different bridge layers blocked an authoritative ground body");
        mover->onBridge=true;
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*mover,10.8,10.0))
            return fail("same bridge layer did not hold an approaching body");
        mover->hasRenderPosition=true;mover->renderX=9.8;mover->renderY=10.0;
        mover->renderOnBridge=false;
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,*mover,10.8,10.0))
            return fail("stopped ground body used its prematurely committed bridge layer");
        mover->renderOnBridge=true;mover->onBridge=false;
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*mover,10.8,10.0))
            return fail("stopped bridge body ignored its current layer");
        other->renderOnBridge=false;
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,*mover,10.8,10.0))
            return fail("candidate layer changed in this tick but guard kept a cached layer");
        checks+=3;
    }
    for(bool fromBridge:{false,true}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        const bool toBridge=!fromBridge;
        auto mover=actor(1,false,11,10);mover.onBridge=toBridge;mover.level=toBridge?4:0;
        auto obstacle=actor(2,false,10,10);obstacle.hasRenderPosition=true;
        obstacle.renderX=10.1;obstacle.renderY=10.0;obstacle.renderOnBridge=toBridge;
        obstacle.onBridge=fromBridge;s.world.accessObjects()={mover,obstacle};
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* current=findMutableObjectById(s,1);auto* other=findMutableObjectById(s,2);
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;move.moveCooldownTicks=4;
        beginRuntimeRenderMove(*current,move,{10,10},{11,10},fromBridge?4.0:0.0,fromBridge,4,&s);
        if(runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move)
            || !advanceRuntimeRenderMove(s,*current,move) || current->renderOnBridge!=fromBridge)
            return fail("ramp body switched layers before half-segment");
        // Its xy separation is increasing, but it was on the other layer:
        // entering an occupied layer is not an existing overlap escaping.
        if(!runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move))
            return fail("ramp preview entered an occupied layer through the overlap escape exception");
        other->renderOnBridge=fromBridge;
        if(runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move)
            || !advanceRuntimeRenderMove(s,*current,move) || current->renderOnBridge!=toBridge)
            return fail("ramp half-segment switch waited for the layer it had left");
        checks+=2;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto lead=actor(1,true,11,11,2);lead.hasRenderPosition=lead.renderMoveActive=true;
        lead.renderX=10.0;lead.renderY=10.0;s.world.accessObjects()={lead,actor(2,true,11,10,2)};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;
        move.renderFrom=move.moveSegmentFrom={10,10};move.renderTo={11,11};
        move.renderMoveTicksTotal=move.renderMoveTicksRemaining=move.moveCooldownTicks=10;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* other=findMutableObjectById(s,1);auto* next=findMutableObjectById(s,2);
        if(!runtimeGroundDepartureHoldsTransition(s,*next,{11,10},{10,11}))
            return fail("opposing X body segment was admitted behind an active segment");
        s.movementEmptyCellProof.additionalGroundPoseIndexReady=false;
        if(!runtimeInfantryActiveSegmentCrossesTransition(s,*next,{11,10},{10,11}))
            return fail("crossing scalar fallback missed an active body");
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        other->renderOnBridge=true;
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{11,10},{10,11}))
            return fail("crossing guard reserved another bridge layer");
        other->renderOnBridge=false;other->factoryExitHostId=99;
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{11,10},{10,11}))
            return fail("crossing guard included a hosted infantry body");
        other->factoryExitHostId=-1;other->alive=false;
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{11,10},{10,11}))
            return fail("crossing guard included a dead infantry body");
        other->alive=true;
        for(int n=0;n<8;++n) if(!advanceRuntimeRenderMove(s,*other,move)) return fail("crossing lead advance");
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{11,10},{10,11}))
            return fail("crossing guard reserved the part already behind the active body");
        // Endpoint sharing uses subcell allocation, not whole-tile reservation.
        next->x=12;next->y=11;next->cell=runtimeCellKey(12,11);
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{12,11},{11,11}))
            return fail("shared endpoint was mistaken for a proper body crossing");
        checks+=2;
    }
    for(int slot:{2,4,3}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto lead=actor(1,true,11,10,2);lead.hasRenderPosition=lead.renderMoveActive=true;
        lead.renderX=10.0;lead.renderY=10.0;
        s.world.accessObjects()={lead,actor(2,true,10,10,slot)};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;
        move.renderFrom=move.moveSegmentFrom={10,10};move.renderTo={11,10};
        move.renderMoveTicksTotal=move.renderMoveTicksRemaining=move.moveCooldownTicks=10;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        const auto* next=findObjectById(s,2);
        if(runtimeGroundDepartureHoldsTransition(s,*next,{10,10},{11,10}))
            return fail("parallel same-direction subcell capacity was withdrawn");
        if(runtimeGroundDepartureHoldsTransition(s,*next,{9,10},{10,10}))
            return fail("same-direction infantry convoy was mistaken for a crossing");
    }
    ++checks;
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto lead=actor(1,true,11,10,2);lead.hasRenderPosition=lead.renderMoveActive=true;
        lead.renderX=10.8;lead.renderY=9.2;s.world.accessObjects()={lead,actor(2,true,10,9,2)};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;
        move.renderFrom=move.moveSegmentFrom={10,10};move.renderTo={11,10};
        move.renderFromOffsetX=0.8;move.renderFromOffsetY=-0.8;
        move.renderMoveTicksTotal=move.renderMoveTicksRemaining=move.moveCooldownTicks=15;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        const auto* next=findObjectById(s,2);auto* current=findMutableObjectById(s,1);
        if(!runtimeInfantryActiveSegmentCrossesTransition(s,*next,{10,9},{11,10}))
            return fail("remaining committed first leg was omitted from crossing guard");
        for(int n=0;n<10;++n) if(!advanceRuntimeRenderMove(s,*current,move)) return fail("crossing bend advance");
        if(runtimeInfantryActiveSegmentCrossesTransition(s,*next,{10,9},{11,10}))
            return fail("completed first leg kept its past crossing reserved");
        ++checks;
    }
    // Factory timing is fixed after commitment. These fixtures exercise the
    // real Move admission, not a relaxed per-tick linear contract.
    {
        const auto factoryFixture=[&]() {
            auto box=make();auto& s=*box;
            s.world.accessObjects()={actor(1,false,20,10)};
            auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;
            move.state=RuntimeBehaviorState::Queued;move.moveTarget={21,10};move.path.push_back({21,10});
            move.moveIntervalRemainderQ16=123;
            RuntimeFactoryDeliveryAcquireRequest request;
            request.lease.canonicalOwner="player";request.lease.hostId=99;request.lease.producedObjectId=1;
            request.lease.orderSerial=1;request.lease.rallyCell={21,10,false};
            if(!s.factoryDeliveries.acquire(request).acquired) return std::unique_ptr<RuntimeWindowState>{};
            return box;
        };
        auto box=factoryFixture();if(!box) return fail("factory admission lease");
        auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto lead=actor(2,false,22,10);lead.hasRenderPosition=lead.renderMoveActive=true;
        lead.renderX=21.2;lead.renderY=10.0;s.world.accessObjects().push_back(lead);
        auto& leadMove=s.behaviors[2];leadMove.kind=RuntimeBehaviorKind::Move;
        leadMove.moveTarget=leadMove.renderTo={22,10};leadMove.renderFrom=leadMove.moveSegmentFrom={21,10};
        leadMove.renderMoveTicksTotal=10;leadMove.renderMoveTicksRemaining=leadMove.moveCooldownTicks=8;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* current=findMutableObjectById(s,1);auto& move=s.behaviors[1];const auto original=move;
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(s,*current,{20,10},{21,10})
            || advanceRuntimeMoveBehavior(s,*current,move)
            || current->x!=20 || current->y!=10 || current->hasRenderPosition || current->renderMoveActive
            || move.path!=original.path || move.moveIntervalRemainderQ16!=original.moveIntervalRemainderQ16
            || move.moveCooldownTicks!=0 || move.renderMoveTicksRemaining!=0 || move.renderMoveTicksTotal!=0)
            return fail("near factory front body consumed its cell/path/clock before clearance");
        for(int n=0;n<8;++n) {++s.tick;(void)advanceRuntimeMoveBehavior(s,*findMutableObjectById(s,2),leadMove);}
        (void)runtimeTickOccupancyLookup(s);
        if(runtimeFactoryGroundSegmentAdmissionBlocked(s,*current,{20,10},{21,10})
            || !advanceRuntimeMoveBehavior(s,*current,move) || current->x!=21
            || !current->renderMoveActive || move.path.size()!=0 || move.moveCooldownTicks<=0)
            return fail("factory front's already-passed source stayed reserved after physical clearance");
        ++checks;
        // A factory host marker is provenance, never outside-body transparency.
        auto marked=factoryFixture();if(!marked) return fail("marked factory fixture");auto& m=*marked;
        auto obstacle=actor(2,false,22,10);obstacle.hasRenderPosition=true;
        obstacle.renderX=21.4;obstacle.renderY=10.0;obstacle.factoryExitHostId=99;
        m.world.accessObjects().push_back(obstacle);invalidateRuntimeTickOccupancyCache(m);(void)runtimeTickOccupancyLookup(m);
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(m,*findObjectById(m,1),{20,10},{21,10}))
            return fail("outside body inherited factory-host-marker exemption");
        m.movementEmptyCellProof.allGroundBodyIndexReady=false;
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(m,*findObjectById(m,1),{20,10},{21,10}))
            return fail("factory full-body scalar fallback lost the marked obstacle");
        checks+=2;
        auto stationary=factoryFixture();if(!stationary) return fail("stationary factory fixture");auto& t=*stationary;
        t.world.accessObjects().push_back(actor(2,false,21,10));
        invalidateRuntimeTickOccupancyCache(t);(void)runtimeTickOccupancyLookup(t);
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(t,*findObjectById(t,1),{20,10},{21,10}))
            return fail("stationary unrendered vehicle missing from factory body clearance");
        t.world.accessObjects().erase(t.world.accessObjects().begin()+1);
        for(int slot:{2,4,3}) t.world.accessObjects().push_back(actor(slot+2,true,21,10,slot));
        invalidateRuntimeTickOccupancyCache(t);(void)runtimeTickOccupancyLookup(t);
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(t,*findObjectById(t,1),{20,10},{21,11}))
            return fail("all three unrendered infantry slots were reduced to one safe representative");
        checks+=2;
        auto factoryFarScene=factoryFixture();if(!factoryFarScene) return fail("far factory fixture");auto& f=*factoryFarScene;
        for(int n=0;n<5000;++n) f.world.accessObjects().push_back(actor(n+2,false,100+n%100,100+n/100));
        invalidateRuntimeTickOccupancyCache(f);(void)runtimeTickOccupancyLookup(f);std::size_t visits=0;
        if(runtimeFactoryGroundSegmentAdmissionBlocked(f,*findObjectById(f,1),{20,10},{21,10},&visits) || visits!=0)
            return fail("factory admission visited a distant stationary army");
        auto side=actor(6002,false,21,11);f.world.accessObjects().push_back(side);
        invalidateRuntimeTickOccupancyCache(f);(void)runtimeTickOccupancyLookup(f);
        if(runtimeFactoryGroundSegmentAdmissionBlocked(f,*findObjectById(f,1),{20,10},{21,10}))
            return fail("parallel factory side body at exact spacing was blocked");
        auto* neighbor=findMutableObjectById(f,6002);neighbor->hasRenderPosition=true;
        neighbor->renderX=21.0;neighbor->renderY=10.8;neighbor->renderOnBridge=true;
        invalidateRuntimeTickOccupancyCache(f);(void)runtimeTickOccupancyLookup(f);
        if(runtimeFactoryGroundSegmentAdmissionBlocked(f,*findObjectById(f,1),{20,10},{21,10}))
            return fail("factory admission reserved another bridge layer");
        neighbor->renderOnBridge=false;
        if(!runtimeFactoryGroundSegmentAdmissionBlocked(f,*findObjectById(f,1),{20,10},{21,10}))
            return fail("factory same-tick bridge-layer change retained stale clearance");
        checks+=3;
        // Ordinary movements keep their existing admission and clocks.
        (void)f.factoryDeliveries.releaseByObject(1);visits=0;
        if(runtimeFactoryGroundSegmentAdmissionBlocked(f,*findObjectById(f,1),{20,10},{21,10},&visits) || visits!=0)
            return fail("ordinary movement entered the factory clearance hot path");
        ++checks;
        for(bool rearFirst:{false,true}) {
            auto convoy=factoryFixture();if(!convoy) return fail("factory rear fixture");auto& c=*convoy;RuntimeSnapshotActiveStateScope convoyScope(c);
            auto rear=actor(2,false,20,10);rear.hasRenderPosition=rear.renderMoveActive=true;
            rear.renderX=19.0;rear.renderY=10.0;c.world.accessObjects().push_back(rear);
            auto& follow=c.behaviors[2];follow.kind=RuntimeBehaviorKind::Move;
            follow.moveTarget=follow.renderTo={20,10};follow.renderFrom=follow.moveSegmentFrom={19,10};
            follow.renderMoveTicksTotal=follow.renderMoveTicksRemaining=follow.moveCooldownTicks=6;
            auto* front=findMutableObjectById(c,1);front->facing=runtimeFacingFromDelta(1,0);
            invalidateRuntimeTickOccupancyCache(c);(void)runtimeTickOccupancyLookup(c);
            auto& forward=c.behaviors[1];
            if(runtimeFactoryGroundSegmentAdmissionBlocked(c,*front,{20,10},{21,10})
                || !advanceRuntimeMoveBehavior(c,*front,forward) || !front->renderMoveActive)
                return fail("factory front deadlocked behind a following shared endpoint");
            const int committed=forward.renderMoveTicksTotal;
            for(int n=1;n<=committed;++n) {
                ++c.tick;
                if(rearFirst) (void)advanceRuntimeMoveBehavior(c,*findMutableObjectById(c,2),follow);
                (void)advanceRuntimeMoveBehavior(c,*front,forward);
                if(!rearFirst) (void)advanceRuntimeMoveBehavior(c,*findMutableObjectById(c,2),follow);
                if(std::abs(runtimeObjectRenderX(*front)-(20.0+double(n)/committed))>1e-9
                    || runtimeObjectRenderX(*front)-runtimeObjectRenderX(*findObjectById(c,2))<1.0-1e-9)
                    return fail("factory front/rear update order froze linear delivery or reduced body spacing");
            }
            ++checks;
        }
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto moving=actor(1,true,18,10,4);moving.hasRenderPosition=moving.renderMoveActive=true;
        moving.renderX=17.0+30.0/51.0;moving.renderY=9.5+15.0/51.0;
        auto parked=actor(2,true,18,10,2);
        // Unrendered bodies must use authoritative coordinates, not stale draw anchors.
        parked.renderX=parked.renderY=-100.0;s.world.accessObjects()={moving,parked};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;
        move.moveTarget=move.renderTo={18,10};move.renderFrom=move.moveSegmentFrom={17,10};
        move.renderFromOffsetY=-0.5;move.renderMoveTicksTotal=51;
        move.renderMoveTicksRemaining=move.moveCooldownTicks=21;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* current=findMutableObjectById(s,1);auto* resident=findMutableObjectById(s,2);
        const double beforeX=current->renderX,beforeY=current->renderY;
        if(!runtimeAllGroundBodySpatialIndexIsCurrent(s)
            || !runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move)
            || advanceRuntimeMoveBehavior(s,*current,move)
            || move.renderMoveTicksRemaining!=21 || move.moveCooldownTicks!=21
            || current->renderX!=beforeX || current->renderY!=beforeY)
            return fail("unrendered resident allowed the actual next foot step to overlap or consume its clock");
        ++checks;
        s.movementEmptyCellProof.allGroundBodyIndexReady=false;
        if(!runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move))
            return fail("unrendered resident disappeared from the scalar foot guard fallback");
        s.movementEmptyCellProof.allGroundBodyIndexReady=true;
        resident->onBridge=true;
        if(runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move))
            return fail("unrendered resident's authoritative bridge layer blocked the other floor");
        resident->onBridge=false;resident->alive=false;resident->health=0;
        if(runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move))
            return fail("same-tick dead stationary resident retained a body guard");
        resident->alive=true;resident->health=100;
        if(!runtimeGroundRenderStepWaitsForStoppedBody(s,*current,move))
            return fail("same-tick authoritative stationary layer change kept stale clearance");
        checks+=3;
        // A real departing resident frees the waiting actor, without manually
        // erasing its body, loosening spacing, or resetting the follower clock.
        resident->x=19;resident->cell=runtimeCellKey(19,10);
        auto& lead=s.behaviors[2];lead.kind=RuntimeBehaviorKind::Move;lead.moveTarget={19,10};
        lead.moveCooldownTicks=4;beginRuntimeRenderMove(*resident,lead,{18,10},{19,10},0.0,false,4,&s);
        for(int n=0;n<24;++n) {
            ++s.tick;
            for(int id:{2,1}) {
                (void)advanceRuntimeMoveBehavior(s,*findMutableObjectById(s,id),s.behaviors[id]);
                const auto firstSlot=worldObjectSubCellOffset(*current),secondSlot=worldObjectSubCellOffset(*resident);
                const double dx=runtimeObjectRenderX(*current)+firstSlot.x-runtimeObjectRenderX(*resident)-secondSlot.x;
                const double dy=runtimeObjectRenderY(*current)+firstSlot.y-runtimeObjectRenderY(*resident)-secondSlot.y;
                if(dx*dx+dy*dy+1e-9<0.25) return fail("stationary-resident release reduced actual foot spacing");
            }
        }
        if(current->renderMoveActive || move.renderMoveTicksRemaining!=0 || move.moveCooldownTicks!=0
            || move.state!=RuntimeBehaviorState::Complete || runtimeCellPointForObject(*current)!=RuntimeCellPoint{18,10})
            return fail("waiting foot did not really finish after the stationary resident departed");
        ++checks;
    }
    for(int leavingSlot:{2,4,3}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        const RuntimeCellPoint from{10,10};
        const RuntimeCellPoint to=leavingSlot==2?RuntimeCellPoint{10,9}:
            (leavingSlot==4?RuntimeCellPoint{11,10}:RuntimeCellPoint{9,10});
        s.world.accessObjects().push_back(actor(1,true,to.x,to.y,leavingSlot));
        int id=2;for(int slot:{2,4,3}) if(slot!=leavingSlot)
            s.world.accessObjects().push_back(actor(id++,true,10,10,slot));
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;move.moveTarget=to;
        move.moveCooldownTicks=6;auto* current=findMutableObjectById(s,1);
        beginRuntimeRenderMove(*current,move,from,to,0.0,false,6,&s);
        for(int n=0;n<6;++n) {
            ++s.tick;
            if(!advanceRuntimeMoveBehavior(s,*current,move))
                return fail("ordinary three-slot source residents blocked a legal outward departure");
            for(int residentId:{2,3}) {
                const auto* resident=findObjectById(s,residentId);
                const auto a=worldObjectSubCellOffset(*current),b=worldObjectSubCellOffset(*resident);
                const double dx=runtimeObjectRenderX(*current)+a.x-runtimeObjectRenderX(*resident)-b.x;
                const double dy=runtimeObjectRenderY(*current)+a.y-runtimeObjectRenderY(*resident)-b.y;
                if(dx*dx+dy*dy+1e-9<0.25) return fail("ordinary three-slot departure overlapped a stationary resident");
            }
        }
        if(current->renderMoveActive || move.state!=RuntimeBehaviorState::Complete)
            return fail("ordinary three-slot departure did not finish");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,true,10,10,2),actor(2,true,11,10,2)};
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* current=findMutableObjectById(s,1);auto* resident=findMutableObjectById(s,2);
        if(runtimeInfantryBodyPreviewDestinationSubcell(s,*current,{11,10})!=4
            || !runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10})
            || !reserveRuntimeInfantryMoveDestination(s,*current,{11,10})
            || !runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10}))
            return fail("unreserved/reserved foot preview missed the stationary target slot along the full body route");
        ++checks;
        auto detour=*current;detour.x=10;detour.y=11;detour.cell=runtimeCellKey(10,11);
        if(runtimeInfantryBodySegmentAdmissionBlocked(s,detour,{10,11},{11,10}))
            return fail("safe final diagonal approach to the free slot was rejected");
        s.movementEmptyCellProof.allGroundBodyIndexReady=false;
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10}))
            return fail("full foot admission scalar fallback lost the stationary body");
        s.movementEmptyCellProof.allGroundBodyIndexReady=true;
        resident->onBridge=true;
        if(runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10}))
            return fail("full foot admission confused a stationary resident's bridge floor");
        resident->onBridge=false;checks+=2;
        // Its accepted future endpoint is this source. A noncollinear forward
        // departure increases distance to the entire rear half-plane.
        resident->x=10;resident->cell=runtimeCellKey(10,10);
        resident->hasRenderPosition=resident->renderMoveActive=true;resident->renderX=9.0;resident->renderY=10.0;
        auto& rear=s.behaviors[2];rear.kind=RuntimeBehaviorKind::Move;
        rear.renderFrom=rear.moveSegmentFrom={9,10};rear.renderTo=rear.moveTarget={10,10};
        rear.renderMoveTicksTotal=rear.renderMoveTicksRemaining=rear.moveCooldownTicks=6;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10}))
            return fail("foot front could not depart away from a rear shared endpoint");
        rear.renderTo={11,10};resident->x=11;resident->cell=runtimeCellKey(11,10);
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*current,{10,10},{11,10}))
            return fail("active foot remaining capsule reserved only proper center crossings");
        checks+=2;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto beside=actor(2,true,11,10,4);beside.hasRenderPosition=beside.renderMoveActive=true;
        beside.renderX=beside.renderY=10.0;
        s.world.accessObjects()={actor(1,true,10,10,2),beside};
        auto& parallel=s.behaviors[2];parallel.kind=RuntimeBehaviorKind::Move;
        parallel.renderFrom=parallel.moveSegmentFrom={10,10};parallel.renderTo=parallel.moveTarget={11,10};
        parallel.renderMoveTicksTotal=parallel.renderMoveTicksRemaining=parallel.moveCooldownTicks=6;
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!reserveRuntimeInfantryMoveDestination(s,*findObjectById(s,1),{11,10})
            || runtimeInfantryBodySegmentAdmissionBlocked(s,*findObjectById(s,1),{10,10},{11,10}))
            return fail("exact half-cell parallel foot lanes lost their legal admission");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects().push_back(actor(1,true,10,10,2));
        for(int n=0;n<5000;++n) s.world.accessObjects().push_back(actor(n+2,true,100+n%100,100+n/100,2));
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        std::size_t visits=0;
        if(runtimeInfantryBodySegmentAdmissionBlocked(s,*findObjectById(s,1),{10,10},{11,10},&visits) || visits!=0)
            return fail("full foot admission scanned a distant stationary army");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.config.crates=s.config.superweapons=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner=s.config.playerCountry="Player";
        s.economy.skirmishStartApplied=s.triggers.bootstrapComplete=true;
        s.world.accessObjects()={actor(1,true,10,10,2),actor(2,true,11,10,2)};
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        s.selectedObjectIds={1};issueRuntimeMoveCommand(s,{11,10});
        if(s.lastCommand.acceptedCount!=1) return fail("stationary-target slot join move command");
        bool complete=false;
        for(int n=0;n<240 && !complete;++n) {
            ++s.tick;advanceRuntimeSimulation(s);
            const auto* incoming=findObjectById(s,1);const auto* resident=findObjectById(s,2);
            const auto a=worldObjectSubCellOffset(*incoming),b=worldObjectSubCellOffset(*resident);
            const double ix=runtimeObjectRenderX(*incoming)+a.x,iy=runtimeObjectRenderY(*incoming)+a.y;
            const double rx=runtimeObjectRenderX(*resident)+b.x,ry=runtimeObjectRenderY(*resident)+b.y;
            if(std::abs(rx-11.25)>1e-9 || std::abs(ry-9.75)>1e-9 || resident->renderMoveActive)
                return fail("stationary target resident moved to make the join artificially pass");
            if((ix-rx)*(ix-rx)+(iy-ry)*(iy-ry)+1e-9<0.25)
                return fail("actual stationary-target slot join overlapped the resident");
            const auto found=s.behaviors.find(1);
            complete=incoming->x==11 && incoming->y==10 && incoming->subCell==4 && !incoming->renderMoveActive
                && std::abs(ix-11.25)<=1e-9 && std::abs(iy-10.25)<=1e-9
                && (found==s.behaviors.end() || (found->second.moveCooldownTicks<=0
                    && found->second.renderMoveTicksRemaining<=0 && found->second.state==RuntimeBehaviorState::Complete));
        }
        if(!complete) return fail("actual stationary-target slot join did not safely arrive in the free slot");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto leader=actor(2,true,12,10,2);leader.hasRenderPosition=leader.renderMoveActive=true;
        leader.renderX=11.0;leader.renderY=10.0;s.world.accessObjects()={actor(1,true,10,10,2),leader};
        auto& ahead=s.behaviors[2];ahead.kind=RuntimeBehaviorKind::Move;
        ahead.renderFrom=ahead.moveSegmentFrom={11,10};ahead.renderTo=ahead.moveTarget={12,10};
        ahead.renderMoveTicksTotal=ahead.renderMoveTicksRemaining=ahead.moveCooldownTicks=6;
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        const auto* follower=findObjectById(s,1);auto* front=findMutableObjectById(s,2);
        if(!reserveRuntimeInfantryMoveDestination(s,*follower,{11,10})
            || runtimeInfantryBodySegmentAdmissionBlocked(s,*follower,{10,10},{11,10}))
            return fail("accepted collinear front's source endpoint blocked a legal following foot step");
        ++checks;
        front->renderMoveActive=false;
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*follower,{10,10},{11,10}))
            return fail("stationary front inherited the moving convoy exception");
        front->renderMoveActive=true;ahead.renderTo={12,11};
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*follower,{10,10},{11,10}))
            return fail("noncollinear front inherited the moving convoy exception");
        ahead.renderFrom={11,11};ahead.renderFromOffsetY=-1.0;ahead.renderTo={12,11};
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*follower,{10,10},{11,10}))
            return fail("corner front inherited the straight moving convoy exception");
        ahead.renderFrom={11,10};ahead.renderFromOffsetY=0.0;ahead.renderTo={11,10};
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*follower,{10,10},{11,10}))
            return fail("front ending at the follower's destination had no final body clearance");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto foreign=actor(3,true,10,10,4);foreign.owner="Enemy";
        foreign.hasRenderPosition=foreign.renderMoveActive=true;foreign.renderX=foreign.renderY=11.0;
        s.world.accessObjects()={actor(1,true,10,10,2),actor(2,true,11,10,2),foreign};
        auto& crossing=s.behaviors[3];crossing.kind=RuntimeBehaviorKind::Move;
        crossing.renderFrom=crossing.moveSegmentFrom={11,11};crossing.renderTo=crossing.moveTarget={10,10};
        crossing.renderMoveTicksTotal=crossing.renderMoveTicksRemaining=crossing.moveCooldownTicks=6;
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* incoming=findMutableObjectById(s,1);
        if(!reserveRuntimeInfantryMoveDestination(s,*incoming,{11,10})
            || runtimeInfantryBodyPreviewDestinationSubcell(s,*incoming,{11,10})!=4
            || !runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,10},{11,10}))
            return fail("ordinary foot admission wrapper lost the two-body blocked route");
        std::vector<int> blockers;
        if(!runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,*incoming,{10,10},{11,10},4,&blockers))
            return fail("explicit target slot lost the blocked body route");
        std::sort(blockers.begin(),blockers.end());
        if(blockers!=std::vector<int>{2,3})
            return fail("body admission evidence omitted a stationary or foreign remaining-capsule blocker");
        ++checks;
        // In this scalar order resident 2 blocks the point first. Object 3's
        // current body is clear, but its accepted remaining diagonal is not.
        s.movementEmptyCellProof.allGroundBodyIndexReady=false;
        if(!runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,*incoming,{10,10},{11,10},4,&blockers))
            return fail("scalar explicit-slot admission lost the two-body blocked route");
        std::sort(blockers.begin(),blockers.end());
        if(blockers!=std::vector<int>{2,3})
            return fail("a prior point blocker suppressed later remaining-capsule evidence");
        s.movementEmptyCellProof.allGroundBodyIndexReady=true;++checks;
        auto* other=findMutableObjectById(s,3);other->alive=false;other->health=0;
        auto future=*incoming;future.x=10;future.y=11;future.cell=runtimeCellKey(10,11);
        future.hasRenderPosition=future.renderMoveActive=false;future.renderX=future.renderY=-100.0;
        const bool virtualSafe=runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,future,{10,11},{11,10},4,&blockers);
        if(virtualSafe || !blockers.empty()
            || !runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,future,{10,11},{11,10},2))
            return fail("virtual source reused a draw anchor or ignored its explicit destination slot");
        ++checks;
        incoming->x=10;incoming->y=11;incoming->cell=runtimeCellKey(10,11);
        incoming->hasRenderPosition=incoming->renderMoveActive=false;incoming->renderX=incoming->renderY=-100.0;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,*incoming,{10,11},{11,10},4)!=virtualSafe
            || runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,11},{11,10}))
            return fail("virtual future-cell admission disagreed with the actual unrendered source and matching slot");
        ++checks;
    }
    // Conservative physical holds reserve future capacity. They cannot
    // invalidate a canonical resident whose actual body is safely separated.
    for (bool active:{false,true}) {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,true,9,8,3),actor(2,true,9,9,2)};
        const auto context=runtimeGateSubcellContext(s);
        const RuntimeSubcellLocation source{9,9,runtimeCellKey(9,9),false};
        // Seed a real slot-2 reservation before the additional pose exists.
        // Reconciliation must drop that future claim, yet keep resident 2 fixed.
        const auto initial=s.subcells.reserveForDestination(s.world.getObjects(),
            s.world.getTerrainObjects(),2,source,context);
        if(!initial.reserved() || initial.subCell!=2) return fail("pose reconcile seed reservation");
        auto* moving=findMutableObjectById(s,1);moving->hasRenderPosition=true;
        moving->renderMoveActive=active;moving->renderX=9.0;moving->renderY=8.45;
        if(active) {
            auto& bend=s.behaviors[1];bend.kind=RuntimeBehaviorKind::Move;
            bend.renderFrom=bend.moveSegmentFrom={9,9};bend.renderTo=bend.moveTarget={9,8};
            bend.renderFromOffsetX=-0.5;bend.renderFromOffsetY=0.5;
            bend.renderMoveTicksTotal=15;bend.renderMoveTicksRemaining=bend.moveCooldownTicks=5;
        }
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        const auto slot=worldObjectSubCellOffset(*moving);
        const double bodyX=runtimeObjectRenderX(*moving)+slot.x,bodyY=runtimeObjectRenderY(*moving)+slot.y;
        const auto held=runtimeAdditionalGroundPoseOccupancy(*moving,&context.groundPoseContext);
        int indexed=0;forEachRuntimeAdditionalGroundPoseOccupantAtCell(s,{9,9},
            [&](const auto& object,const auto& pose) {
                if(object.instanceId==1 && pose.subCell==2 && !pose.onBridge) ++indexed;
            });
        if(std::abs(bodyX-8.75)>1e-9 || std::abs(bodyY-8.70)>1e-9
            || std::hypot(bodyX-9.25,bodyY-8.75)+1e-9<0.5
            || !held || held->x!=9 || held->y!=9 || held->subCell!=2 || indexed!=1)
            return fail(active?"active north bend did not capture its safe but conservative hold"
                :"stopped foot did not capture its safe but conservative hold");
        const auto report=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),context);
        const auto* resident=findObjectById(s,2);
        if(report.reassignedInfantry!=0 || report.needsMoveOut!=0
            || resident->subCell!=2 || runtimeCellPointForObject(*resident)!=RuntimeCellPoint{9,9}
            || resident->hasRenderPosition || resident->renderMoveActive
            || runtimeObjectRenderX(*resident)!=9.0 || runtimeObjectRenderY(*resident)!=9.0)
            return fail(active?"active north bend hold moved a safely separated canonical resident"
                :"stopped foot hold moved a safely separated canonical resident");
        if(report.droppedReservations!=1 || s.subcells.reservations().contains(2))
            return fail("safe resident preservation made its obstructed future reservation transparent");
        const auto available=s.subcells.availableSubCells(s.world.getObjects(),s.world.getTerrainObjects(),
            source,{2},context);
        const auto next=s.subcells.reserveForDestination(s.world.getObjects(),s.world.getTerrainObjects(),2,source,context);
        if(available!=std::vector<int>{4,3} || !next.reserved() || next.subCell!=4)
            return fail("canonical resident preservation removed a foot pose hold from queries or new reservations");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,false,10,10),actor(2,true,11,10,2)};
        const auto context=runtimeGateSubcellContext(s);
        const RuntimeSubcellLocation source{11,10,runtimeCellKey(11,10),false};
        const auto initial=s.subcells.reserveForDestination(s.world.getObjects(),
            s.world.getTerrainObjects(),2,source,context);
        if(!initial.reserved() || initial.subCell!=2) return fail("stopped wheel reconcile seed reservation");
        auto* wheel=findMutableObjectById(s,1);wheel->hasRenderPosition=true;
        wheel->renderX=wheel->renderY=10.2;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        int indexed=0;forEachRuntimeAdditionalGroundPoseOccupantAtCell(s,{11,10},
            [&](const auto& object,const auto& held) {if(object.instanceId==1 && held.subCell==-1) ++indexed;});
        if(std::hypot(wheel->renderX-11.25,wheel->renderY-9.75)+1e-9<0.75 || indexed!=1)
            return fail("stopped wheel fixture lost safe spacing or its conservative touched cell");
        const auto report=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),context);
        const auto* resident=findObjectById(s,2);
        if(report.reassignedInfantry!=0 || report.needsMoveOut!=0 || resident->subCell!=2
            || runtimeCellPointForObject(*resident)!=RuntimeCellPoint{11,10}
            || resident->hasRenderPosition || runtimeObjectRenderX(*resident)!=11.0 || runtimeObjectRenderY(*resident)!=10.0)
            return fail("stopped wheel's conservative touched cell displaced a safely separated foot resident");
        if(report.droppedReservations!=1 || s.subcells.reservations().contains(2))
            return fail("stopped wheel preservation retained a blocked future reservation");
        const auto available=s.subcells.availableSubCells(s.world.getObjects(),s.world.getTerrainObjects(),
            source,{2},context);
        const auto next=s.subcells.reserveForDestination(s.world.getObjects(),s.world.getTerrainObjects(),2,source,context);
        if(!available.empty() || next.status!=RuntimeSubcellReservationStatus::HardBlocked || next.reserved())
            return fail("stopped wheel touched cell became transparent to future queries or reservation");
        checks+=4;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,true,12,12,2)};
        WorldTerrainObject terrain;terrain.x=terrain.y=12;terrain.cell=runtimeCellKey(12,12);
        terrain.occupationBits=1;terrain.occupiedSubCells={2};s.world.accessTerrainObjects().push_back(terrain);
        const auto report=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),runtimeGateSubcellContext(s));
        if(report.reassignedInfantry!=1 || report.needsMoveOut!=0 || findObjectById(s,1)->subCell!=4)
            return fail("actual terrain slot 2 ceased to invalidate a canonical resident");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,true,12,12,2),actor(2,true,12,12,2)};
        const auto report=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),runtimeGateSubcellContext(s));
        if(report.retainedInfantry!=1 || report.reassignedInfantry!=1 || report.needsMoveOut!=0
            || findObjectById(s,1)->subCell!=2 || findObjectById(s,2)->subCell!=4)
            return fail("actual duplicate canonical slots stopped deterministic reassignment");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects()={actor(1,false,12,12),actor(2,true,12,12,2)};
        const auto report=s.subcells.reconcile(s.world.accessObjects(),s.world.getTerrainObjects(),runtimeGateSubcellContext(s));
        if(report.needsMoveOut!=1 || findObjectById(s,2)->subCell!=-1)
            return fail("actual committed whole-cell unit ceased to invalidate an illegal foot resident");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto incoming=actor(1,true,10,10,2);incoming.facing=runtimeFacingFromDelta(1,0);
        auto host=actor(10,false,11,10);host.kind=WorldObjectKind::Structure;host.type="SENTRY";host.health=1000;
        auto wrongHost=host;wrongHost.instanceId=11;wrongHost.x=wrongHost.y=20;wrongHost.cell=runtimeCellKey(20,20);
        s.world.accessObjects()={incoming,host,wrongHost,actor(2,true,20,21,2)};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;move.moveTarget={11,10};move.path={{11,10}};
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* foot=findMutableObjectById(s,1);
        const auto denied=[&] {return !runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*foot,{11,10})
            && runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10});};
        if(!denied()) return fail("ordinary foot used a structure's slotless destination without an entry marker");
        ++checks;
        auto& foreignMove=s.behaviors[2];foreignMove.kind=RuntimeBehaviorKind::Move;foreignMove.pendingAllowedOccupiedObjectId=10;
        if(!denied()) return fail("another actor's entry marker authorized an ordinary foot");
        s.behaviors.erase(2);++checks;
        move.pendingAllowedOccupiedObjectId=11;
        if(!denied()) return fail("a mismatched structure marker authorized another footprint");
        move.pendingAllowedOccupiedObjectId=2;
        if(!denied()) return fail("a non-host infantry marker authorized a slotless destination");
        move.pendingAllowedOccupiedObjectId=10;move.kind=RuntimeBehaviorKind::Attack;
        if(!denied()) return fail("a non-Move marker authorized a slotless destination");
        move.kind=RuntimeBehaviorKind::Move;checks+=3;
        auto* entryHost=findMutableObjectById(s,10);
        for(const std::string owner:{std::string("Neutral"),std::string("Enemy")}) {
            entryHost->owner=owner;
            if(!runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*foot,{11,10})
                || runtimeInfantryBodyPreviewDestinationSubcell(s,*foot,{11,10})!=-1
                || runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10}))
                return fail("verified neutral/enemy structure entry lost its slotless body route");
            ++checks;
        }
        entryHost->owner="Player";entryHost->alive=false;
        if(runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*foot,{11,10}))
            return fail("dead structure retained slotless entry authority");
        entryHost->alive=true;++checks;
        auto* other=findMutableObjectById(s,2);
        other->x=11;other->y=10;other->cell=runtimeCellKey(11,10);other->subCell=-1;
        other->hasRenderPosition=other->renderMoveActive=true;other->renderX=10.75;other->renderY=10.25;
        auto& second=s.behaviors[2];second.kind=RuntimeBehaviorKind::Move;second.pendingAllowedOccupiedObjectId=10;
        second.renderFrom={10,11};second.renderTo=second.moveTarget={11,10};
        second.renderMoveTicksTotal=second.moveCooldownTicks=4;second.renderMoveTicksRemaining=1;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10}))
            return fail("slotless structure entry ignored another accepted foot's current body");
        other->renderX=11.0;other->renderY=9.25;second.renderFrom={11,9};
        second.renderMoveTicksRemaining=second.moveCooldownTicks=3;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10}))
            return fail("slotless structure entry ignored another accepted foot's remaining capsule");
        checks+=2;
        other->kind=WorldObjectKind::Unit;other->type="SCAR";other->subCell=-1;
        other->x=12;other->y=10;other->cell=runtimeCellKey(12,10);other->renderMoveActive=false;
        other->renderX=11.7;other->renderY=10.25;s.behaviors.erase(2);
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10}))
            return fail("slotless structure entry ignored a neighboring stopped vehicle body");
        ++checks;
        other->kind=WorldObjectKind::Infantry;other->type="SMAN";other->subCell=3;
        other->x=12;other->y=9;other->cell=runtimeCellKey(12,9);other->renderX=11.85;other->renderY=9.25;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        // This body clears the actual host center but lies inside the wrong
        // old-slot horizontal endpoint's half-cell capsule.
        if(runtimeInfantryBodySegmentAdmissionBlocked(s,*foot,{10,10},{11,10})
            || runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(s,*foot,{10,10},{11,10},2))
            return fail("slotless structure admission previewed the old source slot at its destination");
        ++checks;
        other->alive=false;other->health=0;invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        const auto offset=worldObjectSubCellOffset(*foot);
        const std::array<double,2> before{runtimeObjectRenderX(*foot)+offset.x,runtimeObjectRenderY(*foot)+offset.y};
        prepareRuntimeInfantryStepReservations(s);
        if(!advanceRuntimeMoveBehavior(s,*foot,move) || foot->subCell!=-1 || !foot->renderMoveActive
            || runtimeInfantryMoveUsesCommittedCorner(*foot,move)
            || std::abs(runtimeObjectRenderX(*foot)-before[0])>1e-9 || std::abs(runtimeObjectRenderY(*foot)-before[1])>1e-9)
            return fail("actual source-slot to slotless structure commit jumped the body or inserted a fake pivot");
        const auto start=runtimeGroundRenderMovePoint(*foot,move,0.0),end=runtimeGroundRenderMovePoint(*foot,move,1.0);
        if(std::abs(start[0]-before[0])>1e-9 || std::abs(start[1]-before[1])>1e-9
            || std::abs(end[0]-11.0)>1e-9 || std::abs(end[1]-10.0)>1e-9)
            return fail("actual authorized slotless saved segment disagreed with its zero-offset destination preview");
        ++checks;
    }
    {
        auto box=make();auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        auto worker=actor(1,true,10,10,2);worker.type="SWORK";
        auto miner=actor(10,false,11,10);miner.type="SMINER";
        s.world.accessObjects()={worker,miner,actor(2,false,20,20)};
        auto& move=s.behaviors[1];move.kind=RuntimeBehaviorKind::Move;move.pendingAllowedOccupiedObjectId=10;move.moveTarget={11,10};
        auto& task=s.slaveMinerTasks[1];task.refineryId=10;task.phase=RuntimeSlaveMinerPhase::Returning;
        ensureRuntimeSubcellsInitialized(s);invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        auto* incoming=findMutableObjectById(s,1);auto* host=findMutableObjectById(s,10);
        const auto allowed=[&] {return runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*incoming,{11,10})
            && runtimeInfantryBodyPreviewDestinationSubcell(s,*incoming,{11,10})==-1
            && !runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,10},{11,10});};
        const auto denied=[&] {return !runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*incoming,{11,10})
            && runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,10},{11,10});};
        if(!allowed()) return fail("verified returning slave worker could not enter its mobile unit host");
        ++checks;
        incoming->owner="Enemy";if(!denied()) return fail("foreign-owner slave entry borrowed a miner host");
        incoming->owner="Player";incoming->type="SMAN";if(!denied()) return fail("non-slaved foot borrowed a miner host");
        incoming->type="SWORK";task.refineryId=2;if(!denied()) return fail("mismatched slave refinery task borrowed a miner host");
        task.refineryId=10;task.phase=RuntimeSlaveMinerPhase::Stored;if(!denied()) return fail("stored slave phase authorized a live return step");
        task.phase=RuntimeSlaveMinerPhase::Harvesting;if(!denied()) return fail("non-returning external slave phase authorized a miner entry");
        task.phase=RuntimeSlaveMinerPhase::Returning;host->type="SCAR";if(!denied()) return fail("ordinary vehicle borrowed mobile slave-miner host authority");
        host->type="SMINER";checks+=6;
        if(runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*incoming,{11,9}))
            return fail("slave miner marker authorized a mismatched destination cell");
        host->alive=false;if(runtimeInfantryBodyUsesAuthorizedSlotlessEntry(s,*incoming,{11,10}))
            return fail("dead slave miner retained entry authority");
        host->alive=true;checks+=2;
        incoming->x=11;incoming->y=10;incoming->cell=runtimeCellKey(11,10);incoming->subCell=-1;
        incoming->hasRenderPosition=incoming->renderMoveActive=true;incoming->renderX=10.25;incoming->renderY=9.75;
        move.renderFrom={10,10};move.renderTo={11,10};move.renderFromOffsetX=0.25;move.renderFromOffsetY=-0.25;
        move.renderMoveTicksTotal=move.renderMoveTicksRemaining=move.moveCooldownTicks=6;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(runtimeStoppedGroundPoseBlocksRenderMove(s,*incoming,11.0,10.0))
            return fail("late body guard froze a verified slave return before its own unit host");
        task.phase=RuntimeSlaveMinerPhase::Stored;
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*incoming,11.0,10.0))
            return fail("late body guard made an unverified unit host transparent");
        task.phase=RuntimeSlaveMinerPhase::Returning;checks+=2;
        auto* neighbor=findMutableObjectById(s,2);neighbor->x=12;neighbor->y=10;neighbor->cell=runtimeCellKey(12,10);
        neighbor->hasRenderPosition=true;neighbor->renderX=11.7;neighbor->renderY=10.25;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeStoppedGroundPoseBlocksRenderMove(s,*incoming,11.0,10.0))
            return fail("verified unit-host return made a neighboring vehicle transparent to the late guard");
        ++checks;
        incoming->x=10;incoming->cell=runtimeCellKey(10,10);incoming->subCell=2;
        incoming->hasRenderPosition=incoming->renderMoveActive=false;
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,10},{11,10}))
            return fail("verified unit-host return made a neighboring vehicle transparent to admission");
        ++checks;
        neighbor->kind=WorldObjectKind::Infantry;neighbor->type="SMAN";neighbor->subCell=-1;
        neighbor->x=11;neighbor->y=10;neighbor->cell=runtimeCellKey(11,10);
        neighbor->renderMoveActive=true;neighbor->renderX=11.0;neighbor->renderY=9.25;
        auto& other=s.behaviors[2];other.kind=RuntimeBehaviorKind::Move;
        other.renderFrom={11,9};other.renderTo={11,10};other.renderMoveTicksTotal=4;
        other.renderMoveTicksRemaining=other.moveCooldownTicks=3;
        invalidateRuntimeTickOccupancyCache(s);(void)runtimeTickOccupancyLookup(s);
        if(!runtimeInfantryBodySegmentAdmissionBlocked(s,*incoming,{10,10},{11,10}))
            return fail("verified unit-host return ignored another foot's remaining capsule");
        ++checks;
    }
    std::cout<<"Ground pose spatial: "<<checks<<" active/stopped/bridge/crossing/dead/fallback checks, 5000 far paused bodies, "
        <<"9000 far cell queries visited "<<farVisits<<" candidates, 4000 starts retained local buckets passed\n";
    return true;
}
