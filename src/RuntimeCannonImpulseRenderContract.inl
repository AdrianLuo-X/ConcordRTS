bool runRuntimeCannonImpulseRenderCheck(
    const RuntimeMapRenderContext& context, const WorldObjectInstance& vehicle,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& timeline,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& restoredTimeline,
    const std::string& label, std::string& error) {
    const auto fail=[&](const std::string& why){error="cannon impulse "+label+": "+why;return false;};
    if(timeline.size()<3||timeline.size()!=restoredTimeline.size()) return fail("impact timeline");
    auto probe=context; probe.vision=nullptr; probe.stableRenderConfiguration=true;
    bool narrow=false, noAreaImpulse=false, hasImpact=false; double newLift=0,oldLift=0,newAngle=0,oldAngle=0;
    std::size_t oldPeak=0,newPeak=0;
    const auto equal=[](const RuntimeVehicleBodyPose& a,const RuntimeVehicleBodyPose& b) {
        return a.pitch==b.pitch&&a.roll==b.roll&&a.height==b.height;
    };
    // The previous released renderer used 0.65 for every non-Rocker impact.
    // Keep that calibration only in this comparison, using the same real VXL draw path.
    const auto legacy=[&](RuntimeVehicleBodyPoseRenderScope& scope) {
        scope.sources.prepare(probe); scope.prepared=true;
        for(auto& source:scope.sources.impacts) {
            const auto* wh=probe.rules->findWarheadType(source.actor->warheadId);
            source.coefficient=wh->rocker?probe.rules->getSpecialMechanismRules().directRockingCoefficient:0.65;
        }
    };
    std::ofstream csv;
    const char* output=std::getenv("CONCORD_CANNON_IMPULSE_OUTPUT");
    if(output){std::filesystem::create_directories(output);csv.open(std::string(output)+"/"+label+".csv");csv<<"frame,old_height,new_height,old_angle,new_angle\n";}
    for(std::size_t frame=0;frame<timeline.size();++frame) {
        probe.impactActors=&timeline[frame];
        for(const auto& hit:timeline[frame]) {
            const auto* wh=probe.rules->findWarheadType(hit.warheadId); if(!wh) return fail("warhead missing");
            narrow=!wh->rocker&&wh->cellSpread>0.0&&wh->cellSpread<1.0;
            noAreaImpulse=!wh->rocker&&wh->cellSpread<=0.0; hasImpact=true;
        }
        const auto current=runtimeVehicleBodyPoseForRender(vehicle,probe);
        RuntimeVehicleBodyPose previous;
        {RuntimeVehicleBodyPoseRenderScope scope(probe);legacy(scope);previous=runtimeVehicleBodyPoseForRender(vehicle,probe);}
        if(!narrow&&!equal(current,previous)) return fail("large/Rocker explosion changed");
        if(current.height<0||current.height>0.22) return fail("lift bounds");
        if(frame==0||frame+1==timeline.size()) if(current.active()||previous.active()) return fail("did not settle");
        if(current.height>newLift){newLift=current.height;newPeak=frame;}
        if(previous.height>oldLift){oldLift=previous.height;oldPeak=frame;}
        const auto angle=[](const auto& pose){return std::hypot(pose.pitch,pose.roll);};
        newAngle=std::max(newAngle,angle(current));oldAngle=std::max(oldAngle,angle(previous));
        probe.impactActors=&restoredTimeline[frame];
        if(!equal(current,runtimeVehicleBodyPoseForRender(vehicle,probe))) return fail("snapshot age/impulse differs");
        if(csv)csv<<frame<<','<<previous.height<<','<<current.height<<','<<angle(previous)<<','<<angle(current)<<'\n';
    }
    if(!hasImpact) return fail("missing impact");
    if(noAreaImpulse) {
        if(oldLift!=0||newLift!=0||oldAngle!=0||newAngle!=0) return fail("zero-spread anti-armor shell moved vehicle");
    } else if(oldLift<=0||oldAngle<=0) return fail("no measurable original impulse");
    if(narrow&&(newLift>oldLift*0.5+1e-9||newAngle>oldAngle*0.5+1e-9)) return fail("ordinary shell still too strong");
    if(!narrow&&newLift!=oldLift) return fail("grand cannon response changed");
    constexpr int width=180,height=150,columns=5;constexpr std::uint32_t background=0x004c5850U;
    const RECT viewport{0,0,width,height}; auto cache=std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapCamera camera;camera.initialized=true;
    const auto origin=runtimeObjectDrawPoint(vehicle,{},camera);
    camera.worldOffsetX=static_cast<int>(std::lround(origin.x))-width/2;
    camera.worldOffsetY=static_cast<int>(std::lround(origin.y))-110;
    std::vector<std::uint32_t> pixels(width*height),sheet(width*columns*height),baseline;
    const std::array<std::size_t,columns> frames{0,oldPeak,newPeak,oldPeak,timeline.size()-1};
    for(int col=0;col<columns;++col) {
        std::fill(pixels.begin(),pixels.end(),background);probe.surfacePixels=pixels.data();probe.surfaceWidth=width;probe.surfaceHeight=height;
        probe.impactActors=&timeline[frames[col]];
        RuntimeVehicleBodyPoseRenderScope scope(probe);if(col==1)legacy(scope);
        const auto center=runtimeObjectBodyAnchorForRender(vehicle,viewport,probe,camera);
        drawRuntimeWorldObjectShadow(nullptr,viewport,vehicle,center,probe,camera,*cache,true);
        RuntimeVoxelDrawResult result;
        if(!tryDrawVoxelSprite(nullptr,viewport,vehicle,center,probe,camera,*cache,&result)||!result.bodyDrawn
            ||(runtimeObjectHasTurretForRender(vehicle,probe)&&!result.turretDrawn)) return fail("native vehicle draw");
        if(col==0)baseline=pixels;
        if(col==4&&pixels!=baseline) return fail("settled vehicle pixels differ");
        for(int y=0;y<height;++y)std::copy_n(pixels.begin()+y*width,width,sheet.begin()+y*width*columns+col*width);
    }
    if(output&&context.fixedTickHz==30&&vehicle.type=="MTNK"
        &&!writeRuntimeBgraBmp(std::string(output)+"/"+label+".bmp",width*columns,height,sheet)) return fail("comparison sheet");
    std::cout<<"Cannon impulse "<<label<<" height="<<oldLift<<" -> "<<newLift<<" angle="<<oldAngle<<" -> "<<newAngle
        <<" frames="<<timeline.size()<<" native body/turret/rest/snapshot passed\n";
    return true;
}
