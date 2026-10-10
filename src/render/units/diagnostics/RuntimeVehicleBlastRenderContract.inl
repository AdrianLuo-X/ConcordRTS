bool runRuntimeVehicleBlastRenderContractCheck(
    const RuntimeMapRenderContext& context, const std::vector<WorldObjectInstance>& vehicles,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& timeline,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& restoredTimeline,
    const std::string& label, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "vehicle blast " + label + ": " + why; return false; };
    if (vehicles.size()!=4 || timeline.size()<context.fixedTickHz || timeline.size()!=restoredTimeline.size())
        return fail("live impact samples missing");
    const char* output = std::getenv("CONCORD_VEHICLE_BODY_OUTPUT");
    const std::string root = output ? output : "";
    RuntimeMapRenderContext probe = context; probe.vision = nullptr; probe.stableRenderConfiguration = true;
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int width = 192, height = 168, columns = 6;
    constexpr std::uint32_t background = 0x004c5850U;
    const RECT viewport{0,0,width,height};
    std::vector<std::uint32_t> pixels(width*height), sheet(width*columns*height*vehicles.size(),background);
    std::ofstream csv;
    if (!root.empty()) {
        std::filesystem::create_directories(root);
        csv.open(root+"/"+label+"-body.csv");
        csv << "frame,vehicle,pitch,roll,height\n";
    }
    int visibleChanges = 0, visiblyMoved = 0;
    for (std::size_t v=0; v<vehicles.size(); ++v) {
        const auto& object = vehicles[v];
        double highest = 0.0, strongest = 0.0; std::size_t peak = 0;
        for (std::size_t frame=0; frame<timeline.size(); ++frame) {
            probe.impactActors = &timeline[frame];
            const auto pose = runtimeVehicleBodyPoseForRender(object,probe);
            probe.impactActors = &restoredTimeline[frame];
            const auto resumed = runtimeVehicleBodyPoseForRender(object,probe);
            if (pose.pitch!=resumed.pitch || pose.roll!=resumed.roll || pose.height!=resumed.height)
                return fail("snapshot interrupted motion: "+object.type+" frame="+std::to_string(frame));
            if ((frame==0 || frame+1==timeline.size()) && pose.active()) return fail("did not rest before/after blast");
            if (pose.height<0 || pose.height>0.22) return fail("below ground or excessive lift");
            if (pose.height>highest) highest=pose.height;
            const double strength = std::abs(pose.pitch)+std::abs(pose.roll)+pose.height;
            if (strength>strongest) { strongest=strength; peak=frame; }
            if (csv) csv << frame << ',' << object.type << ',' << pose.pitch << ',' << pose.roll << ',' << pose.height << '\n';
            auto outside = object; outside.x += 15;
            if (runtimeVehicleBodyPoseForRender(outside,probe).active()) return fail("distant vehicle moved");
            auto docked = object; docked.dockedToObjectId = 900;
            if (runtimeVehicleBodyPoseForRender(docked,probe).active()) return fail("docked vehicle moved");
        }
        if (highest<=0.001 || strongest<=0.005) return fail("nearby vehicle never lifted: "+object.type);
        RuntimeMapCamera camera; camera.initialized=true;
        const auto origin=runtimeObjectDrawPoint(object,{},camera);
        camera.worldOffsetX=static_cast<int>(std::lround(origin.x))-width/2;
        camera.worldOffsetY=static_cast<int>(std::lround(origin.y))-120;
        const auto end=timeline.size()-1;
        const std::size_t samples[] = {0,peak,std::min(end,peak+static_cast<std::size_t>(context.fixedTickHz/15)),
            std::min(end,peak+static_cast<std::size_t>(context.fixedTickHz/5)),
            std::min(end,peak+static_cast<std::size_t>(context.fixedTickHz)),end};
        std::vector<std::uint32_t> baseline;
        for (int col=0; col<columns; ++col) {
            std::fill(pixels.begin(),pixels.end(),background);
            probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
            probe.impactActors=&timeline[samples[col]];
            const auto center=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
            drawRuntimeWorldObjectShadow(nullptr,viewport,object,center,probe,camera,*cache,true);
            RuntimeVoxelDrawResult result;
            if (!tryDrawVoxelSprite(nullptr,viewport,object,center,probe,camera,*cache,&result) || !result.bodyDrawn
                || (runtimeObjectHasTurretForRender(object,probe) && !result.turretDrawn)) return fail("native body/turret missing");
            if (col==0) baseline=pixels;
            if (col==1) {
                int changed=0; for (std::size_t i=0; i<pixels.size(); ++i) changed+=pixels[i]!=baseline[i];
                // At the outer edge a real impulse can remain below one pixel
                // and the two-degree VXL bins. Do not inflate that weak motion.
                if (changed<4 && highest*camera.tileHeight>=0.5)
                    return fail("resolvable lift missing from vehicle pixels: "+object.type);
                if (changed>=4) ++visiblyMoved;
                visibleChanges+=changed;
            }
            if (col==columns-1 && pixels!=baseline) return fail("body/turret did not settle to original pixels");
            for (int y=0; y<height; ++y) std::copy_n(pixels.begin()+y*width,width,
                sheet.begin()+(v*height+y)*width*columns+col*width);
        }
        std::cout << "Blast pose " << label << ' ' << object.type << " peakHeight=" << highest << " cells, settled=ok\n";
    }
    if (visiblyMoved<2) return fail("nearest vehicles did not visibly move");
    if (!root.empty() && context.fixedTickHz==30 && !writeRuntimeBgraBmp(root+"/"+label+"-body.bmp",
            width*columns,height*static_cast<int>(vehicles.size()),sheet)) return fail("sheet output");
    std::cout << "Blast render " << label << " frames=" << timeline.size() << " changedPixels=" << visibleChanges
        << " visibleVehicles=" << visiblyMoved << " lift/gravity/rest/snapshot-identical=ok\n";
    return true;
}
