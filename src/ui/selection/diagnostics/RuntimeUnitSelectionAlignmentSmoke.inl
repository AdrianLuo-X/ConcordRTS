#include "ui/selection/diagnostics/RuntimeUnitSelectionSpecialSmoke.inl"

bool runRuntimeUnitSelectionAlignmentSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "unit alignment: " + why; return false; };
    const char* requested = std::getenv("CONCORD_UNIT_ALIGNMENT_DIR");
    const std::string root = requested && *requested ? requested : "build/smoke/feature-set-15/01-units/checks";
    std::filesystem::create_directories(root);
    auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; s.config = config;
    s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = false;
    s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
    s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
    if (!s.report.success) return fail("original resources/startup");
    s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true;
    s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
    s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
    s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
    auto context = runtimeTerrainMetadataContext(s);
    context.rules = &s.rules; context.art = &s.art;
    context.palettePath = runtimePalettePath(config, s.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(config, s.report.map);
    context.structureRootPath = runtimeStructureRootPath(config, s.report.map);
    context.shadowsEnabled = false;
    const RECT viewport{0,0,640,480};
    std::ofstream rows(root + "/unit-poses.csv");
    rows << "id,kind,pose,facing,tile_width,level,resource,pixels,layers,left,top,right,bottom,clicks\n";
    std::ofstream inventory(root + "/unit-inventory.csv");
    inventory << "id,kind,tech_level,poses,rendered_poses,missing_poses\n";
    int types = 0, cases = 0, rendered = 0, clicks = 0, missing = 0;
    std::vector<std::string> failures;
    std::vector<std::uint32_t> atlas(1280 * 960, 0x00242c30U);
    int atlasSlot = 0, atlasPage = 0;
    const auto flushAtlas = [&]() {
        if (!atlasSlot) return true;
        const bool ok = writeRuntimeSmokeBmp(root + "/unit-atlas-" + std::to_string(atlasPage++) + ".bmp",
            atlas.data(), 1280, 960, error);
        std::fill(atlas.begin(),atlas.end(),0x00242c30U); atlasSlot = 0; return ok;
    };
    std::ofstream atlasIndex(root + "/atlas-index.csv"); atlasIndex << "page,row,column,id\n";
    for (int kind = 0; kind < 3; ++kind) {
        const auto& list = kind == 0 ? s.rules.getInfantryTypes()
            : kind == 1 ? s.rules.getVehicleTypes() : s.rules.getAircraftTypes();
        for (const auto& type : list) {
            ++types; int typeCases = 0, typeRendered = 0, typeMissing = 0; bool pictured = false;
            std::vector<std::string> poses{"Guard","Move","Fire","SecondaryFire"};
            if (kind == 0) poses.push_back("ProneFire");
            if (type.deployer || type.deployFire || type.id == "SCHP") poses.push_back("DeployedFire");
            if (type.underwater) poses.push_back("Submerged");
            if (kind == 2 || type.jumpJet) poses.push_back("Airborne");
            for (const auto& pose : poses) for (int facing = 0; facing < 8; ++facing) {
                ++cases; ++typeCases;
                WorldObjectInstance object; object.instanceId = 15001; object.type = type.id;
                object.kind = kind == 0 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
                object.authoredAircraft = kind == 2; object.owner = s.economy.owner;
                object.alive = true; object.health = std::max(1,type.strength);
                object.currentAmmo = type.ammo < 0 ? -1 : type.ammo;
                object.x = 38; object.y = 61; object.cell = runtimeCellKey(38,61);
                object.facing = facing * 32; object.turretFacing = (object.facing + 64) % 256;
                object.mission = pose; object.level = facing % 2 ? 2 : 0;
                object.prone = pose == "ProneFire"; if (object.prone) object.mission = "Fire";
                object.deployed = pose == "DeployedFire";
                object.submerged = pose == "Submerged"; if (object.submerged) object.mission = "Guard";
                object.airborne = pose == "Airborne"; if (object.airborne) {object.mission="Guard";object.level=4;object.flightLevel=4;}
                if (pose == "Move") {
                    object.hasRenderPosition = object.renderMoveActive = true;
                    object.renderX = object.x + 0.375; object.renderY = object.y + 0.125;
                    object.renderLevel = object.level; object.movementAnimationDistance = 0.75;
                }
                s.world.accessObjects() = {object}; s.tick = 9 + facing; context.animationTick = s.tick;
                s.camera = {}; s.camera.initialized = true;
                s.camera.tileWidth = facing < 6 ? 60 : facing == 6 ? 48 : 72;
                s.camera.tileHeight = s.camera.tileWidth / 2;
                const auto cell = runtimeProjectCellXY(object.x,object.y,object.level,s.camera);
                s.camera.worldOffsetX = cell.x - 320; s.camera.worldOffsetY = cell.y - 240;
                ensureRuntimeCameraForViewport(s,viewport);
                const POINT center = runtimeObjectBodyAnchorForRender(object,viewport,context,s.camera);
                RuntimeMobileSpriteBoundsProbe probe;
                const bool valid = verifyRuntimeMobileObjectSpriteBounds(object,center,context,s.camera,s.renderCache,probe);
                if (!valid) {
                    if (probe.resourceResolved) return fail(type.id + " " + pose + " " + std::to_string(facing) + ": " + probe.diagnostic);
                    ++missing; ++typeMissing;
                    rows << type.id << ',' << kind << ',' << pose << ',' << facing*32 << ',' << s.camera.tileWidth
                         << ',' << object.level << ",missing,0,0,0,0,0,0,0\n";
                    continue;
                }
                ++rendered; ++typeRendered;
                invalidateRuntimeObjectProjectionCache(s);
                const auto projected = buildProjectedObjects(s,viewport);
                if (projected.size()!=1) return fail(type.id + " missing interaction projection");
                const auto& projectedObject = projected.front();
                // Raw spawned missile entries need their flight snapshot. Their
                // real launch/hover/selection is covered by the missile smoke.
                if (!type.missileSpawn && !runtimeRectEquals(projectedObject.spriteBounds,probe.renderedBounds))
                    return fail(type.id + " " + pose + " shell/body geometry differs");
                if (!type.missileSpawn) {
                    const auto& frame = projectedObject.selectionBounds; const auto& body = probe.renderedBounds;
                    if (frame.left>body.left || frame.top>body.top || frame.right<body.right || frame.bottom<body.bottom)
                        return fail(type.id + " " + pose + " blue frame excludes artwork");
                    int perCaseClicks = 0;
                    for (LONG y=body.top;y<body.bottom;++y) for(LONG x=body.left;x<body.right;++x) {
                        const auto* hit=findProjectedObjectAtPoint(projected,{x,y});
                        if(!hit || hit->object->instanceId!=object.instanceId)return fail(type.id+" body click missed");
                        ++clicks; ++perCaseClicks;
                    }
                    for(const POINT point:{POINT{frame.left,frame.top},POINT{frame.right-1,frame.top},
                        POINT{frame.left,frame.bottom-1},POINT{frame.right-1,frame.bottom-1}}) {
                        if(!findProjectedObjectAtPoint(projected,point))return fail(type.id+" frame click missed");
                        ++clicks; ++perCaseClicks;
                    }
                    rows << type.id << ',' << kind << ',' << pose << ',' << facing*32 << ',' << s.camera.tileWidth
                         << ',' << object.level << ",resolved," << probe.renderedPixels << ',' << probe.layerCount
                         << ',' << frame.left << ',' << frame.top << ',' << frame.right << ',' << frame.bottom << ',' << perCaseClicks << '\n';
                }
                if (!pictured && pose=="Guard" && facing==2 && !probe.pixels.empty()) {
                    const int tileX=(atlasSlot%8)*160, tileY=(atlasSlot/8)*160;
                    const auto& body=probe.renderedBounds;
                    const auto& shownFrame = type.missileSpawn ? body : projectedObject.selectionBounds;
                    const double scale=std::min(1.0,150.0/std::max(shownFrame.right-shownFrame.left,shownFrame.bottom-shownFrame.top));
                    const double cx=(shownFrame.left+shownFrame.right)*0.5,cy=(shownFrame.top+shownFrame.bottom)*0.5;
                    for(int y=0;y<160;++y)for(int x=0;x<160;++x) {
                        const int sx=static_cast<int>(std::floor(cx+(x-80)/scale))-probe.imageOrigin.x;
                        const int sy=static_cast<int>(std::floor(cy+(y-80)/scale))-probe.imageOrigin.y;
                        if(sx>=0&&sx<probe.imageWidth&&sy>=0&&sy<probe.imageHeight)
                            atlas[(tileY+y)*1280+tileX+x]=probe.pixels[sy*probe.imageWidth+sx];
                    }
                    const RECT frame{tileX+80+static_cast<LONG>(std::floor((shownFrame.left-cx)*scale)),
                        tileY+80+static_cast<LONG>(std::floor((shownFrame.top-cy)*scale)),
                        tileX+80+static_cast<LONG>(std::ceil((shownFrame.right-cx)*scale)),
                        tileY+80+static_cast<LONG>(std::ceil((shownFrame.bottom-cy)*scale))};
                    const auto dot=[&](int x,int y){if(x>=tileX&&x<tileX+160&&y>=tileY&&y<tileY+160)atlas[y*1280+x]=0x000080ffU;};
                    for(int x=frame.left;x<frame.right;++x){dot(x,frame.top);dot(x,frame.bottom-1);}
                    for(int y=frame.top;y<frame.bottom;++y){dot(frame.left,y);dot(frame.right-1,y);}
                    atlasIndex << atlasPage << ',' << atlasSlot/8 << ',' << atlasSlot%8 << ',' << type.id << '\n';
                    ++atlasSlot; pictured=true; if(atlasSlot==48&&!flushAtlas())return false;
                }
            }
            inventory<<type.id<<','<<kind<<','<<type.techLevel<<','<<typeCases<<','<<typeRendered<<','<<typeMissing<<'\n';
            // These two retail entries only instantiate weapons in the INI
            // loader (the original comments explicitly describe the workaround).
            // They have no actual unit artwork. Every other entry must render
            // every exercised pose, including campaign-only/non-buildable units.
            const bool weaponRegistrationDummy = type.id=="DeathDummy" || type.id=="WEEDGUY";
            if(typeMissing>0 && !weaponRegistrationDummy) failures.push_back(type.id);
            std::cout<<"unit "<<type.id<<" rendered="<<typeRendered<<" missing="<<typeMissing<<'\n';
        }
    }
    if(!flushAtlas())return false;
    if(!failures.empty()) {
        std::string list; for(const auto& id:failures) list += (list.empty()?"":",") + id;
        return fail("available types without artwork: "+list);
    }
    if (!runRuntimeUnitSelectionSpecialSmoke(s,context,root,error)) return false;
    std::ofstream summary(root+"/summary.json");
    summary<<"{\"types\":"<<types<<",\"poses\":"<<cases<<",\"rendered\":"<<rendered
        <<",\"missingResourcePoses\":"<<missing<<",\"clicks\":"<<clicks<<",\"atlasPages\":"<<atlasPage<<"}\n";
    std::cout<<"Unit alignment passed: types="<<types<<" poses="<<cases<<" rendered="<<rendered<<" clicks="<<clicks<<" missing="<<missing<<'\n';
    return true;
}
