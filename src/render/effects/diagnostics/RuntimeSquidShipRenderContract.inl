bool runRuntimeSquidShipRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail = [&](const std::string& text) { error = "squid ship: " + text; return false; };
    struct RetailPose { int age, action, frame; float roll; };
    // Native disassembly/constants: emergence, either signed rocking strip,
    // zero at each strip boundary and the authored +/-45-degree peak.
    const RetailPose retail[] = {
        {0,0,0,0}, {15,0,5,0}, {30,1,0,0}, {34,1,1,-0.24270138144493103f},
        {50,1,5,-0.7853981852531433f}, {70,2,0,0},
        {74,2,1,0.24270138144493103f}, {90,2,5,0.7853981852531433f}, {110,1,0,0}
    };
    int checked = 0;
    for (int hz : {15,30,60}) for (int facing = 0; facing < 8; ++facing) {
        for (const auto& native : retail) {
            const int age = native.age * hz / 15;
            const auto pose = runtimeSquidGrapplePose(age,hz);
            if (pose.action != native.action || pose.frame != native.frame
                || std::abs(pose.roll-native.roll) > 1e-7
                || runtimeSquidGrappleFrame(worldObjectFacingFromMapFacing(facing*32),age,hz)
                    != native.action*80+facing*10+native.frame)
                return fail("native pose/strip/facing cadence at age="+std::to_string(native.age));
            ++checked;
        }
    }
    constexpr int width=208,height=208,columns=8;
    constexpr std::uint32_t background=0x00242c30U;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*columns*height*3,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    auto probe=context; probe.vision=nullptr; probe.fixedTickHz=30;
    probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    probe.stableRenderConfiguration=true; probe.impactActors=nullptr;
    std::unordered_map<std::string,COLORREF> colors{{"russians",RGB(210,25,35)}, {"americans",RGB(245,220,45)}};
    probe.ownerRemapColors=&colors;
    std::vector<SpecialMechanismRenderSnapshot> links(1);
    auto& link=links[0]; link.kind="parasite"; link.phase="organic";
    link.objectId=1; link.targetObjectId=2; link.intensity=15; link.resourceId="ParasitePlus";
    WorldObjectInstance squid; squid.instanceId=1; squid.kind=WorldObjectKind::Unit;
    squid.type="SQD"; squid.owner="Russians"; squid.alive=true; squid.health=200;
    auto host=squid; host.instanceId=2; host.owner="Americans";
    RuntimeMapCamera camera; camera.initialized=true;
    camera.worldOffsetX=-width/2; camera.worldOffsetY=-height/2;
    const RECT viewport{0,0,width,height};
    const char* output=std::getenv("CONCORD_SQUID_GRAPPLE_OUTPUT");
    int rendered=0;
    for (const std::string type : {"DEST","DRED","AEGIS","CARRIER","SUB","BSUB"}) {
        host.type=type;
        const auto* unit=context.rules->findUnitType("Vehicle",type);
        if (!unit) return fail("stock ship missing: "+type);
        host.health=unit->strength; host.submerged=unit->underwater; host.cloaked=unit->underwater;
        std::fill(sheet.begin(),sheet.end(),background);
        for (int direction=0;direction<8;++direction) {
            host.facing=worldObjectFacingFromMapFacing(direction*32); host.turretFacing=host.facing;
            const POINT center=runtimeObjectBodyAnchorForRender(host,viewport,probe,camera);
            probe.specialMechanismSnapshots=nullptr;
            std::fill(pixels.begin(),pixels.end(),background);
            RuntimeVoxelDrawResult flatDraw;
            if (!tryDrawVoxelSprite(nullptr,viewport,host,center,probe,camera,*cache,&flatDraw))
                return fail(type+" original VXL/HVA missing");
            const auto flat=pixels;
            probe.specialMechanismSnapshots=&links;
            for (int row=0;row<3;++row) {
                link.progress=(row==0?15:row==1?50:90)*2;
                const auto pose=runtimeVehicleBodyPoseForRender(host,probe);
                const auto rotation=runtimeVehicleBodyRotation(pose,-100.0);
                const double sign=row==1?-1.0:1.0;
                if (!pose.hullLocalRoll || pose.pitch!=0 || pose.height!=0 || rotation.floorZ!=0
                    || (row==0 && pose.active())
                    || (row>0 && std::abs(pose.roll-sign*0.7853981852531433)>1e-7))
                    return fail(type+" hull-local tilt/pivot at row "+std::to_string(row));
                // A longitudinal vector stays fixed. A port/starboard vector
                // gains the signed height, for every facing including diagonals.
                const double yaw=(direction*4-8)*6.283185307179586/32;
                const auto forward=rotation.rotate(std::cos(yaw),std::sin(yaw),0);
                const auto side=rotation.rotate(-std::sin(yaw),std::cos(yaw),0);
                if (std::abs(forward[2])>1e-7 || (row>0
                    && std::abs(side[2]-sign*0.7071067966408575)>1e-7))
                    return fail(type+" rocked around a screen/world axis instead of its own hull");
                std::fill(pixels.begin(),pixels.end(),background);
                RuntimeVoxelDrawResult drawn;
                if (!tryDrawVoxelSprite(nullptr,viewport,host,center,probe,camera,*cache,&drawn)
                    || !drawn.bodyDrawn || drawn.bodyInfo.fitScale!=flatDraw.bodyInfo.fitScale
                    || drawn.turretDrawn!=flatDraw.turretDrawn)
                    return fail(type+" body/turret/scale changed");
                int changed=0;
                for (std::size_t i=0;i<pixels.size();++i) changed+=pixels[i]!=flat[i];
                if ((row==0&&changed!=0)||(row>0&&changed<20)) return fail(type+" tilt pixels missing");
                RuntimeMapRenderStats stats;
                if (drawRuntimeSquidGrapple(nullptr,viewport,squid,host,link,probe,camera,*cache,stats)<0)
                    return fail(type+" tentacles missing");
                const auto composed=pixels;
                // Repaint the same paused state with the populated voxel cache.
                std::fill(pixels.begin(),pixels.end(),background);
                if (!tryDrawVoxelSprite(nullptr,viewport,host,center,probe,camera,*cache)
                    || drawRuntimeSquidGrapple(nullptr,viewport,squid,host,link,probe,camera,*cache,stats)<0
                    || pixels!=composed) return fail(type+" painting advanced a paused pose");
                for (int y=0;y<height;++y) std::copy_n(pixels.data()+y*width,width,
                    sheet.data()+(row*height+y)*width*columns+direction*width);
                ++rendered;
            }
            probe.specialMechanismSnapshots=nullptr;
            std::fill(pixels.begin(),pixels.end(),background);
            if (runtimeVehicleBodyPoseForRender(host,probe).active()
                || !tryDrawVoxelSprite(nullptr,viewport,host,center,probe,camera,*cache) || pixels!=flat)
                return fail(type+" did not return to original hull pose on release");
        }
        if (output && !writeRuntimeBgraBmp(std::string(output)+"/hull-"+type+".bmp",width*columns,height*3,sheet))
            return fail("hull evidence write");
    }
    int splashes=0;
    probe.specialMechanismSnapshots=nullptr;
    const auto* palette=runtimeAnimPaletteForRender(probe,*cache);
    if (!palette) return fail("original anim palette missing");
    for (int variant=1;variant<=3;++variant) {
        const std::string id="H2O_EXP"+std::to_string(variant);
        const auto* art=context.art->findArtType(id);
        const auto* shp=loadRuntimeAuxiliaryShpSpriteFromCache(id,0,probe,*cache,false);
        const int offset=variant==1?-18:variant==2?-10:-8;
        if (!art || art->yDrawOffset!=offset || art->translucency!=25 || !shp || shp->frameCount!=16)
            return fail("original water frame count/offset/translucency: "+id);
        for (int frame=0;frame<16;++frame) {
            RuntimeImpactRenderActor actor; actor.animId=id; actor.frameIndex=frame;
            const POINT center{width/2,height/2}; RuntimeMapRenderStats stats;
            std::fill(pixels.begin(),pixels.end(),background);
            drawRuntimeSquidSplash(nullptr,viewport,center,actor,probe,camera,*cache,stats);
            const auto production=pixels;
            std::fill(pixels.begin(),pixels.end(),background);
            if (stats.squidSplashDrawn!=1 || stats.squidSplashMissing
                || !drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,center,probe,camera,*cache,
                    id,frame,*palette,0,offset,false,false,RGB(255,255,255),191,true)
                || pixels!=production) return fail("water source pixels/palette/offset/opacity: "+id);
            ++splashes;
        }
    }
    std::cout<<"Squid ship: "<<checked<<" retail pose/direction/time checks, "<<rendered
        <<" original ship+tentacle composites, hull-axis/pivot/scale/release/paused cache, "
        <<splashes<<" original water frames passed\n";
    return true;
}
