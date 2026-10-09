const UnitType* runtimeFixedWingProfile(const RuntimeWindowState& state,const WorldObjectInstance& object) {
    const auto* unit=findRuntimeUnitType(state.rules,object);
    return unit&&runtimeUnitUsesAirMovement(*unit)&&!runtimeUnitUsesJumpjetMovement(*unit)
        &&!unit->spawned&&!unit->missileSpawn&&!runtimeHasSupportFlightPose(object)
        &&!runtimeObjectManagedByAirstrike(state,object.instanceId)?unit:nullptr;
}

void applyRuntimeFixedWingPose(const RuntimeWindowState& state,WorldObjectInstance& object) {
    const auto* jumpjet = runtimeJumpjetProfile(state, object);
    if (jumpjet && runtimeUnitUsesAutomaticJumpjetLanding(*jumpjet)) return;
    if (runtimeCarrierHasDeckFlight(state, object)) {
        applyRuntimeCarrierDeckFlightPresentation(state, object); return;
    }
    object.aircraftDockFlightProgress=-1.0;
    const auto found=state.aircraftFlights.find(object.instanceId);
    if(found==state.aircraftFlights.end()||!object.alive||!object.airborne
        ||!runtimeFixedWingProfile(state,object))return;
    const auto& f=found->second;
    // External relocation (chrono, campaign actions) owns the new cell.
    if(object.x!=f.x/runtimeAircraftCellQ16||object.y!=f.y/runtimeAircraftCellQ16)return;
    object.hasRenderPosition=true;object.renderMoveActive=f.moving;
    object.renderX=double(f.x)/runtimeAircraftCellQ16;
    object.renderY=double(f.y)/runtimeAircraftCellQ16;
    object.renderLevel=f.z<0?object.flightLevel:double(f.z)/runtimeAircraftCellQ16;
    object.level=static_cast<int>(std::lround(object.renderLevel));
    if(f.dockFlightStartZ>f.dockFlightTargetZ&&f.dockFlightTargetZ>=0)
        object.aircraftDockFlightProgress=std::clamp(double(f.z-f.dockFlightTargetZ)
            /double(f.dockFlightStartZ-f.dockFlightTargetZ),0.0,1.0);
    object.onBridge=object.renderOnBridge=false;object.subCell=-1;
}

void reconcileRuntimeAircraftFlights(RuntimeWindowState& state) {
    for(auto it=state.aircraftFlights.begin();it!=state.aircraftFlights.end();) {
        auto* object=findMutableObjectById(state,it->first);const auto& f=it->second;
        if(!object||!object->alive||!object->airborne||!runtimeFixedWingProfile(state,*object)
            ||object->x!=f.x/runtimeAircraftCellQ16||object->y!=f.y/runtimeAircraftCellQ16) {
            it=state.aircraftFlights.erase(it);continue;
        }
        if(!state.behaviors.contains(object->instanceId))it->second.moving=false;
        applyRuntimeFixedWingPose(state,*object);++it;
    }
}

RuntimeAircraftFlight& runtimeAircraftFlightFor(RuntimeWindowState& state,const WorldObjectInstance& object) {
    auto [it,inserted]=state.aircraftFlights.try_emplace(object.instanceId);
    if(inserted||object.x!=it->second.x/runtimeAircraftCellQ16||object.y!=it->second.y/runtimeAircraftCellQ16) {
        it->second={};
        it->second.x=std::llround((object.hasRenderPosition?object.renderX:object.x)*runtimeAircraftCellQ16);
        it->second.y=std::llround((object.hasRenderPosition?object.renderY:object.y)*runtimeAircraftCellQ16);
    }
    if(it->second.z<0)it->second.z=std::int64_t(object.flightLevel)*runtimeAircraftCellQ16;
    return it->second;
}

std::int64_t runtimeFixedWingVerticalStep(const RuntimeWindowState& state,std::int64_t remaining) {
    // Retail non-dropship Fly locomotion: clamp((height-target)/20,20,50)
    // leptons per 15 Hz rule frame. Clamp the final scaled step to the pad.
    const auto leptons=std::clamp<std::int64_t>(remaining/(runtimeAircraftCellQ16/256*20),20,50);
    return std::min(remaining,std::max<std::int64_t>(1,std::llround(
        double(leptons)*runtimeAircraftCellQ16/256*15/std::max(1,state.config.fixedTickHz))));
}

void advanceRuntimeFixedWingDepartureHeight(const RuntimeWindowState& state,
    const WorldObjectInstance& object,RuntimeAircraftFlight& f) {
    f.landingHostId=f.landingDockIndex=-1;
    const auto cruise=std::int64_t(object.flightLevel)*runtimeAircraftCellQ16;
    if(f.z<cruise)f.z+=runtimeFixedWingVerticalStep(state,cruise-f.z);
    if(f.z>=cruise) {
        f.dockFlightStartZ=f.dockFlightTargetZ=-1;f.landingSoundPlayed=false;
    }
}

bool recoverRuntimeFixedWingHeight(RuntimeWindowState& state,WorldObjectInstance& object) {
    if(!runtimeFixedWingProfile(state,object))return false;
    auto& f=runtimeAircraftFlightFor(state,object);
    if(f.lastTick==state.tick)return false;
    f.lastTick=state.tick;const auto before=f.z;
    advanceRuntimeFixedWingDepartureHeight(state,object,f);f.moving=false;
    applyRuntimeFixedWingPose(state,object);return f.z!=before;
}

bool runtimeFixedWingLandingReady(RuntimeWindowState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,const WorldObjectInstance& host,int dockIndex) {
    auto& f=runtimeAircraftFlightFor(state,object);
    const auto dock=runtimeAircraftDockSpawn(state,host,dockIndex);
    const auto targetZ=std::int64_t(dock.level)*runtimeAircraftCellQ16;
    if(f.z<=targetZ&&f.dockFlightStartZ<0)return true;
    if(f.landingHostId!=host.instanceId||f.landingDockIndex!=dockIndex) {
        f.landingHostId=host.instanceId;f.landingDockIndex=dockIndex;
        f.dockFlightStartZ=std::max(f.z,std::int64_t(object.flightLevel)*runtimeAircraftCellQ16);
        f.dockFlightTargetZ=targetZ;f.landingSoundPlayed=false;
    }
    const auto* unit=runtimeFixedWingProfile(state,object);
    const int desired=worldObjectFacingFromMapFacing(state.rules.getPoseDir())*256;
    if(f.heading<0)f.heading=normalizeRuntimeFacing(object.facing)*256;
    int delta=desired-f.heading;
    if(delta>32768)delta-=65536;if(delta< -32768)delta+=65536;
    if(f.lastTick!=state.tick) {
        f.lastTick=state.tick;
        f.z-=runtimeFixedWingVerticalStep(state,std::max<std::int64_t>(0,f.z-targetZ));
        const int maxTurn=std::max(1,static_cast<int>(std::llround(
            std::clamp(unit?unit->rot:1,1,127)*256.0*15/std::max(1,state.config.fixedTickHz))));
        f.heading=(f.heading+std::clamp(delta,-maxTurn,maxTurn)+65536)&65535;
        object.facing=((f.heading+128)/256)%256;object.bodyFacingClock={};
    }
    f.moving=false;applyRuntimeFixedWingPose(state,object);
    // Native AuxSound2 is a once-per-landing crossing below 300 leptons,
    // rather than a repeated request or an extra sound on final attachment.
    if(!f.landingSoundPlayed&&f.z-targetZ<300*(runtimeAircraftCellQ16/256)) {
        queueRuntimeAircraftAuxSound(state,object,false,0);f.landingSoundPlayed=true;
    }
    behavior.kind=RuntimeBehaviorKind::Move;behavior.state=RuntimeBehaviorState::Moving;
    behavior.moveTarget=dock.cell;behavior.pendingAllowedOccupiedObjectId=host.instanceId;
    behavior.path.clear();behavior.pathRequestPending=false;behavior.moveCooldownTicks=0;
    setRuntimeMissionIntent(state,object,RuntimeMissionIntent::Moving,"aircraftDescendingToDock");
    return f.z==targetZ&&f.heading==desired;
}

double runtimeFixedWingCruiseInterval(const RuntimeWindowState& state,const WorldObjectInstance& object) {
    // Airport fighters share the retail Speed clock. The old extra factor
    // compensated for the former accelerated ordinary movement cadence.
    return runtimeMovementBaseIntervalTicks(state,object);
}

// Keep flying while ROT limits the heading. Close, off-axis destinations
// reduce forward speed so that a point inside the cruise turning circle can
// still be reached instead of being orbited forever.
bool advanceRuntimeFixedWingToward(RuntimeWindowState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,double goalX,double goalY) {
    auto& f=runtimeAircraftFlightFor(state,object);
    if(f.lastTick==state.tick){applyRuntimeFixedWingPose(state,object);return false;}
    f.lastTick=state.tick;
    advanceRuntimeFixedWingDepartureHeight(state,object,f);
    behavior.path.clear();behavior.pathRequestPending=false;behavior.moveCooldownTicks=0;
    behavior.moveSegmentFrom={-1,-1};behavior.renderMoveTicksTotal=behavior.renderMoveTicksRemaining=0;
    const auto targetX=std::llround(std::clamp(goalX,0.0,511.999)*runtimeAircraftCellQ16);
    const auto targetY=std::llround(std::clamp(goalY,0.0,511.999)*runtimeAircraftCellQ16);
    const double dx=double(targetX-f.x),dy=double(targetY-f.y),distance=std::hypot(dx,dy);
    double step=std::min(distance,runtimeAircraftCellQ16/runtimeFixedWingCruiseInterval(state,object));
    const auto oldX=f.x,oldY=f.y;const auto previous=runtimeCellPointForObject(object);
    if(distance>0.0 && step>0.0) {
        constexpr double tau=6.2831853071795864769;
        const int desired=static_cast<int>(std::llround(std::atan2(dx-dy,-dx-dy)*65536.0/tau))&65535;
        if(f.heading<0||((f.heading+128)/256)%256!=normalizeRuntimeFacing(object.facing))
            f.heading=normalizeRuntimeFacing(object.facing)*256;
        int delta=desired-f.heading;
        if(delta>32768)delta-=65536;
        if(delta< -32768)delta+=65536;
        const auto* unit=runtimeFixedWingProfile(state,object);
        const bool bounded=unit&&unit->airportBound&&unit->rot>0;
        const int maxTurn=bounded?std::max(1,static_cast<int>(std::llround(
            std::clamp(unit->rot,1,127)*256.0*15/std::max(1,state.config.fixedTickHz)))):32768;
        const bool aligned=std::abs(delta)<=maxTurn;
        f.heading=(f.heading+std::clamp(delta,-maxTurn,maxTurn)+65536)&65535;
        if(aligned) {
            f.x+=std::llround(dx*step/distance);f.y+=std::llround(dy*step/distance);
        } else {
            // Radius <= half the remaining distance also handles a nearby
            // click behind the aircraft without stopping its forward travel.
            step=std::min(step,distance*(maxTurn*tau/65536.0)*0.5);
            const double angle=f.heading*tau/65536.0;
            const double sine=std::sin(angle),cosine=std::cos(angle);
            f.x+=std::llround((sine-cosine)*0.7071067811865475244*step);
            f.y+=std::llround((-sine-cosine)*0.7071067811865475244*step);
        }
        f.x=std::clamp<std::int64_t>(f.x,0,512*runtimeAircraftCellQ16-1);
        f.y=std::clamp<std::int64_t>(f.y,0,512*runtimeAircraftCellQ16-1);
        object.facing=((f.heading+128)/256)%256;object.bodyFacingClock={};
    }
    const RuntimeCellPoint cell{static_cast<int>(f.x/runtimeAircraftCellQ16),static_cast<int>(f.y/runtimeAircraftCellQ16)};
    if(cell!=previous){setRuntimeObjectCell(state,object,cell,false);updateRuntimeTickOccupancyForMove(state,object,previous,cell);}
    f.moving=f.x!=oldX||f.y!=oldY;
    object.movementAnimationDistance=std::fmod(object.movementAnimationDistance
        +std::hypot(double(f.x-oldX),double(f.y-oldY))/runtimeAircraftCellQ16,1024.0);
    applyRuntimeFixedWingPose(state,object);
    behavior.state=behavior.kind==RuntimeBehaviorKind::Attack?RuntimeBehaviorState::Chasing:RuntimeBehaviorState::Moving;
    setRuntimeMissionIntent(state,object,RuntimeMissionIntent::Moving,"continuousAircraftFlight");
    if(f.moving)++state.actionSummary.movedThisTick;
    return f.moving;
}

std::pair<double,double> runtimeFixedWingMoveGoal(const RuntimeWindowState& state,
    const WorldObjectInstance& object,const RuntimeObjectBehavior& behavior) {
    const auto* host=findObjectById(state,behavior.pendingAllowedOccupiedObjectId);
    if(host&&host->instanceId==object.dockedToObjectId&&object.dockIndex>=0) {
        const auto dock=runtimeAircraftDockSpawn(state,*host,object.dockIndex);
        if(dock.cell==behavior.moveTarget)return {dock.renderX,dock.renderY};
    }
    return {double(behavior.moveTarget.x),double(behavior.moveTarget.y)};
}
