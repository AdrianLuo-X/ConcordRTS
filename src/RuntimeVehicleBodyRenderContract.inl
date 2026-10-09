#include "RuntimeVehicleBodyCacheContract.inl"
#include "RuntimeVehicleBodyPoseContract.inl"
#include "RuntimeVehicleBodyImpactSpatialContract.inl"
#include "RuntimeVehicleBlastRenderContract.inl"
#include "RuntimeCannonImpulseRenderContract.inl"

bool runRuntimeVehicleBodyRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    if (!runRuntimeVehicleBodyCacheContractCheck(error)) return false;
    if (!runRuntimeVehicleBodyPoseContractCheck(context, error)) return false;
    if (!runRuntimeVehicleBodyImpactSpatialContractCheck(error)) return false;
    const auto fail = [&](const std::string& text) { error = "vehicle body dynamics: " + text; return false; };
    const auto* parasite = context.rules->findWarheadType("Parasite");
    const auto* v3 = context.rules->findWarheadType("V3WH");
    const auto* dread = context.rules->findWarheadType("DMISLWH");
    if (parasite == nullptr || !parasite->rocker || v3 == nullptr || !v3->rocker
        || dread == nullptr || dread->rocker) return fail("original Rocker rules were not parsed");
    const auto light = runtimeVehicleBlastMotion(0.08,0.8,1,0,0.7,3,200,1,1);
    const auto heavy = runtimeVehicleBlastMotion(0.08,0.8,1,0,0.7,3,200,5,1);
    const auto distant = runtimeVehicleBlastMotion(0.08,0.8,1,0,2.5,3,200,1,1);
    const auto opposite = runtimeVehicleBlastMotion(0.08,0.8,-1,0,0.7,3,200,1,1);
    if (light.height <= heavy.height || std::abs(light.pitch) <= std::abs(distant.pitch)
        || std::abs(light.pitch + opposite.pitch) > 1e-10
        || runtimeVehicleBlastMotion(0.8,0.8,1,0,0.7,3,200,1,1).active())
        return fail("mass, distance, direction or final settling");
    const auto hz30 = runtimeVehicleParasiteMotion(7.0/30.0,3.5);
    const auto hz60 = runtimeVehicleParasiteMotion(14.0/60.0,3.5);
    if (hz30.pitch != hz60.pitch || hz30.roll != hz60.roll || hz30.height != hz60.height)
        return fail("rendering rate changed the motion");
    if (!runtimeVehicleParasiteMotion(0.0,3.5).active()
        || runtimeVehicleParasiteMotion(-1.0,3.5).active()
        || runtimeVehicleParasiteMotion(0.72,3.5).active()
        || runtimeVehicleParasiteMotion(4.0,3.5).active())
        return fail("bite did not jolt immediately and settle between damage events");
    IniFile overrideIni;
    RulesDatabase overrideRules;
    if (!overrideIni.loadFromString("[VehicleTypes]\n0=Test\n[Test]\nStrength=100\n"
            "[AudioVisual]\nDirectRockingCoefficient=2.75\n")
        || !overrideRules.loadFromIni(overrideIni)
        || overrideRules.getSpecialMechanismRules().directRockingCoefficient != 2.75)
        return fail("AudioVisual rocking override ignored");

    constexpr int width=256, height=210, columns=4;
    constexpr std::uint32_t background=0x004c5850U;
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext probe=context;
    probe.vision=nullptr; probe.stableRenderConfiguration=true;
    probe.fixedTickHz=30;
    RuntimeMapCamera camera; camera.initialized=true;
    const RECT viewport{0,0,width,height};
    std::vector<std::uint32_t> pixels(width*height,background);
    std::vector<std::uint32_t> sheet(width*columns*height*4,background);
    std::vector<SpecialMechanismRenderSnapshot> links(1);
    links[0].kind="parasite"; links[0].phase="mechanical";
    links[0].resourceId="Parasite"; links[0].objectId=99; links[0].intensity=30;
    links[0].requiredProgress=120; links[0].expireTick=120;
    std::vector<RuntimeImpactRenderActor> impacts(1);
    impacts[0].renderIntent="warheadImpact"; impacts[0].warheadId="DMISLWH";
    impacts[0].damage=300; impacts[0].totalTicks=30; impacts[0].remainingTicks=27;
    const char* output=std::getenv("CONCORD_VEHICLE_BODY_OUTPUT");
    const std::string root=output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    const char* types[]={"MTNK","HTNK","APOC","MIND"};
    for (int col=0;col<columns;++col) {
        WorldObjectInstance object;
        object.instanceId=col+1; object.kind=WorldObjectKind::Unit;
        object.type=types[col]; object.owner="Americans"; object.alive=true;
        const auto* unit=context.rules->findUnitType("Vehicle",object.type);
        if (unit==nullptr) return fail("stock vehicle missing");
        object.health=unit->strength; object.facing=96;
        // Turret yaw intentionally differs from hull yaw.
        object.turretFacing=160; object.x=object.y=7;
        links[0].targetObjectId=object.instanceId;
        impacts[0].x=7.7; impacts[0].y=7.0;
        const auto origin=runtimeObjectDrawPoint(object,{},camera);
        camera.worldOffsetX=static_cast<int>(std::lround(origin.x))-width/2;
        camera.worldOffsetY=static_cast<int>(std::lround(origin.y))-155;
        std::vector<std::uint32_t> baseline;
        RuntimeVoxelCacheEntry* flat=loadRuntimeVoxelFromCache(object,probe,*cache);
        if (!flat || !flat->hasSprite || !flat->modelBounds.valid) return fail("original VXL/HVA body missing");
        const double fit=flat->rasterFitScale;
        const auto bounds=flat->modelBounds;
        const auto testRotation=runtimeVehicleBodyRotation(hz30,bounds.minimum[2]);
        const double yaw=(facingBucketForObject(object,runtimeVoxelFacingBucketCount)-8)
            *6.283185307179586/runtimeVoxelFacingBucketCount;
        const bool originPivot=runtimeVxlRasterSizingForOwner(object.type).modelOriginPivot;
        if (runtimeVehicleGroundSupportLift(bounds,testRotation,yaw,originPivot)<=0)
            return fail("tilted body lacks ground contact support");
        for (int row=0;row<5;++row) {
            std::fill(pixels.begin(),pixels.end(),background);
            probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
            probe.specialMechanismSnapshots=(row==1 || row==2) ? &links : nullptr;
            probe.impactActors=row==3 ? &impacts : nullptr;
            links[0].progress=row==1 ? 1 : 10; probe.animationTick=links[0].progress;
            const POINT center=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
            drawRuntimeWorldObjectShadow(nullptr,viewport,object,center,probe,camera,*cache,true);
            RuntimeVoxelDrawResult drawn;
            if (!tryDrawVoxelSprite(nullptr,viewport,object,center,probe,camera,*cache,&drawn)
                || !drawn.bodyDrawn || std::abs(drawn.bodyInfo.fitScale-fit)>1e-10)
                return fail("original body scale/draw changed: "+object.type);
            if (runtimeObjectHasTurretForRender(object,probe) && !drawn.turretDrawn)
                return fail("turret detached or missing: "+object.type);
            if (row==0) baseline=pixels;
            else {
                int changed=0;
                for (std::size_t i=0;i<pixels.size();++i) changed+=pixels[i]!=baseline[i];
                if ((row==4 && changed!=0) || (row<4 && changed<20))
                    return fail("motion or return-to-rest pixels: "+object.type+" row="+std::to_string(row));
            }
            if (row<4) for (int y=0;y<height;++y)
                std::copy_n(pixels.begin()+y*width,width,
                    sheet.begin()+(row*height+y)*width*columns+col*width);
        }
        if (col==0 && !root.empty()) {
            probe.specialMechanismSnapshots=&links; probe.impactActors=nullptr;
            for (int frame=0;frame<66;++frame) {
                std::fill(pixels.begin(),pixels.end(),background);
                links[0].progress=frame*4; probe.animationTick=frame*4;
                links[0].expireTick=(probe.animationTick/120+1)*120;
                const POINT center=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
                drawRuntimeWorldObjectShadow(nullptr,viewport,object,center,probe,camera,*cache,true);
                if (!tryDrawVoxelSprite(nullptr,viewport,object,center,probe,camera,*cache))
                    return fail("motion sequence draw failed");
                if (!writeRuntimeBgraBmp(root+"/parasite-"+std::to_string(frame)+".bmp",width,height,pixels))
                    return fail("motion sequence write failed");
            }
        }
        probe.specialMechanismSnapshots=nullptr; probe.impactActors=nullptr;
        links[0].expireTick=120;
        auto hidden=object; hidden.dockedToObjectId=100;
        probe.specialMechanismSnapshots=&links;
        if (runtimeVehicleBodyPoseForRender(hidden,probe).active()) return fail("passenger rocked outside transport");
        hidden=object; hidden.alive=false;
        if (runtimeVehicleBodyPoseForRender(hidden,probe).active()) return fail("dead host kept rocking");
        probe.specialMechanismSnapshots=nullptr;
    }
    if (!root.empty() && !writeRuntimeBgraBmp(root+"/vehicle-body-poses.bmp",width*columns,height*4,sheet))
        return fail("comparison sheet write failed");
    return true;
}
