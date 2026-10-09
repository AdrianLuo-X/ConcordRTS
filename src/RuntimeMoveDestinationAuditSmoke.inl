// Stock-rule inventory audit. Run through the same command and simulation paths
// as normal play; never replace the accepted destination or generated path.
bool runRuntimeMoveDestinationAuditSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* requested = std::getenv("CONCORD_MOVE_DESTINATION_OUTPUT");
    const std::filesystem::path root = requested && *requested ? requested
        : "build/smoke/feature-set-19/01-move-destination/checks";
    std::filesystem::create_directories(root);
    const char* only = std::getenv("CONCORD_MOVE_DESTINATION_TYPE");
    const bool diagnostic = std::getenv("CONCORD_MOVE_DESTINATION_DIAGNOSTIC") != nullptr;
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config; stock->config.playerSlots.clear();
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world,
        &stock->theater, &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) { error = "Move destination audit: original resources unavailable"; return false; }
    IniFile authored;
    if (!authored.loadFromFile(stock->report.rulesPath)) { error = "Move destination audit: original rule census unavailable"; return false; }
    std::ofstream inventory(root / "inventory.csv"), rows(root / "destinations.csv"),
        curves(root / "continuity.csv"), rawInventory(root / "raw-inventory.csv");
    if (!inventory || !rows || !curves || !rawInventory) { error = "Move destination audit: cannot open evidence files"; return false; }
    curves << "id,hz,direction,water,tick,event,logical_x,logical_y,slot,body_x,body_y,body_z,delta_xy,delta_z,limit_xy,limit_z,chrono_arrival,active,remaining,total,anchor_x,anchor_y,projected_x,projected_y,anchor_checked\n";
    curves << std::setprecision(17);
    rawInventory << "list,id,section_exists,loaded,speed,spawned,missile_spawn\n";
    const std::array<const char*,3> rawLists{{"InfantryTypes","VehicleTypes","AircraftTypes"}};
    for (int kind=0; kind<3; ++kind) {
        const auto& loaded=kind==0?stock->rules.getInfantryTypes():kind==1?
            stock->rules.getVehicleTypes():stock->rules.getAircraftTypes();
        const auto* list=authored.getSection(rawLists[kind]);
        if (!list) { error="Move destination audit: missing original type list"; return false; }
        for (const auto& [index,id] : *list) {
            (void)index; const auto it=std::find_if(loaded.begin(),loaded.end(),
                [&](const auto& type) { return type.id==id; });
            rawInventory << rawLists[kind] << "," << id << "," << bool(authored.getSection(id)) << ","
                << (it!=loaded.end()) << "," << (it==loaded.end()?0:it->speed) << ","
                << (it!=loaded.end()&&it->spawned) << "," << (it!=loaded.end()&&it->missileSpawn) << '\n';
        }
    }
    inventory << "id,kind,speed,movement,included,reason\n";
    rows << "id,hz,direction,water,goal_x,goal_y,accepted,target_x,target_y,ticks,"
        "logical_x,logical_y,render_x,render_y,subcell_x,subcell_y,airborne,"
        "render_active,ground_pixel_dx,ground_pixel_dy,result\n";
    int listed = 0, excluded = 0, types = 0, cases = 0, passed = 0, failed = 0, projections = 0;
    int expectedCases=0, continuitySamples=0, chronoArrivals=0, renderAnchors=0;
    const auto slotOffset=[](const WorldObjectInstance& object) -> std::array<double,2> {
        if (object.kind!=WorldObjectKind::Infantry) return {0.0,0.0};
        if (object.subCell==2) return {0.25,-0.25};
        if (object.subCell==4) return {0.25,0.25};
        if (object.subCell==3) return {-0.25,0.25};
        return {0.0,0.0};
    };
    const auto body=[&](const WorldObjectInstance& object) {
        const auto d=slotOffset(object);
        const bool visualHeight=object.renderMoveActive||worldObjectHasStoppedGroundPose(object)
            ||(object.airborne&&object.hasRenderPosition)||(object.factoryExitHostId>=0&&object.hasRenderPosition);
        return std::array<double,3>{(object.hasRenderPosition?object.renderX:double(object.x))+d[0],
            (object.hasRenderPosition?object.renderY:double(object.y))+d[1],
            visualHeight?object.renderLevel:double(object.level)};
    };
    std::string firstFailure;
    constexpr RuntimeCellPoint start{24,24};
    const std::array<RuntimeCellPoint,8> directions{{{13,5},{5,13},{-5,13},{-13,5},
        {-13,-5},{-5,-13},{5,-13},{13,-5}}};
    const RECT client{0,0,1280,720}; const RECT viewport = runtimeMapViewport(client);
    const auto make = [&](const UnitType& type, int hz, bool water) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = stock->config; s.config.fixedTickHz = hz; s.config.gameMode = "Battle";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.report = stock->report;
        // This fixture owns the synthetic Clear/Water cells below; original-map
        // query facts must not override their controlled passability.
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        RuntimeHumanHouseAuthority authority; authority.owner = s.economy.owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true; s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y=0; y<52; ++y) for (int x=0; x<52; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* object = addRuntimeObjectInstance(s, runtimeBuildKindForUnitTypeId(s.rules,type.id), type.id, start);
        if (object) {
            object->facing = runtimeFacingFromDelta(1,0);
            object->harvestAutoSearchSuspended = true;
            s.selectedObjectIds = {object->instanceId};
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        for (auto& [key, info] : s.terrainInfoByCell) {
            (void)key; info = RuntimeTerrainInfo{};
            info.kind = water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
            info.authoredLandType = info.movementLandType = water
                ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
            info.water=water; info.passable=true; info.cost=10;
        }
        s.pathTerrainCache.valid = s.pathTerrainCache.dynamicValid = false;
        centerRuntimeCameraOnCell(s, client, start); ensureRuntimeCameraForViewport(s,viewport);
        return box;
    };
    const auto runCase = [&](const UnitType& type, int hz, int direction, bool water) {
        ++cases;
        auto box = make(type,hz,water); auto& s = *box;
        RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint goal{start.x+directions[direction].x,start.y+directions[direction].y};
        const int id = s.selectedObjectIds.empty() ? -1 : s.selectedObjectIds.front();
        std::string reasons;
        const auto problem = [&](const std::string& reason) {
            if (!reasons.empty()) reasons += ";";
            reasons += reason;
        };
        auto* object = findMutableObjectById(s,id);
        if (!object) problem("creation_failed");
        else if (!runtimeObjectCanMove(s,*object)) problem("runtime_cannot_move");
        // Verify the actual ground click's inverse, including camera pan and zoom.
        // A flyer's body is above this ground point; do not compare its screen Y
        // with the cursor without removing flight height and infantry subcells.
        for (int width : {48,60,72}) for (int pan : {0,37}) {
            s.camera.tileWidth=width; s.camera.tileHeight=width/2;
            const auto center=runtimeProjectCellXY(goal.x,goal.y,0,s.camera);
            s.camera.worldOffsetX=center.x-400+pan; s.camera.worldOffsetY=center.y-240-pan;
            const POINT screen=runtimeScreenPointForCell(s,viewport,goal,0);
            const POINT hit=approximateCellFromScreenPoint(s,viewport,screen); ++projections;
            if (hit.x!=goal.x || hit.y!=goal.y) problem("mouse_projection_mismatch");
        }
        s.camera.tileWidth=60; s.camera.tileHeight=30;
        centerRuntimeCameraOnCell(s,client,goal);
        const POINT click=runtimeScreenPointForCell(s,viewport,goal,0);
        const POINT hit=approximateCellFromScreenPoint(s,viewport,click);
        const auto beforeCommand=object?body(*object):std::array<double,3>{};
        double commandDeltaXY=0.0,commandDeltaZ=0.0;
        if (object && runtimeObjectCanMove(s,*object)) issueRuntimeMoveCommand(s,{hit.x,hit.y});
        object=findMutableObjectById(s,id);
        if (object) {
            const auto afterCommand=body(*object);
            commandDeltaXY=std::hypot(afterCommand[0]-beforeCommand[0],afterCommand[1]-beforeCommand[1]);
            commandDeltaZ=std::abs(afterCommand[2]-beforeCommand[2]);
            if (commandDeltaXY>1e-9||commandDeltaZ>1e-9) problem("command_changed_body_without_a_tick");
        } else {
            problem("object_lost_after_command");
        }
        const int accepted=s.lastCommand.acceptedCount;
        RuntimeCellPoint assigned{-1,-1};
        const auto command=s.behaviors.find(id);
        if (accepted!=1 || command==s.behaviors.end()) problem("move_rejected");
        else { assigned=command->second.moveTarget; if (assigned!=goal) problem("accepted_target_changed"); }
        bool arrived=false; int ticks=0;
        const bool fixedWing=runtimeUnitUsesAirMovement(type) && !runtimeUnitUsesJumpjetMovement(type);
        const bool jumpjet=runtimeUnitUsesJumpjetMovement(type);
        const bool personalChrono=type.teleporter&&!type.harvester;
        const int speedLeptons=std::min(255,std::clamp(type.speed,0,100)*256/100);
        double terrainMaximum=1.0;
        if (const auto* land=s.rules.findLandType(water?"Water":"Clear"))
            terrainMaximum=std::max({1.0,land->foot/100.0,land->track/100.0,land->wheel/100.0,
                land->floatSpeed/100.0,land->floatBeach/100.0,land->hover/100.0,land->amphibious/100.0});
        const double cruise=15.0*speedLeptons/(256.0*hz);
        // Independent upper bounds retain original speed units and admit only
        // whole-tick duration and per-axis fixed-point rounding. No arrival curve
        // is used; a legal infantry slot fallback may add lateral distance.
        const double groundCruise=cruise*terrainMaximum;
        const double minimumWholeTicks=std::max(1.0,std::floor(1.0/std::max(1e-12,groundCruise)));
        const double groundLimit=groundCruise*(minimumWholeTicks+1.0)/minimumWholeTicks;
        const double xyLimit=jumpjet?type.jumpjetSpeed*15.0/(256.0*hz)+1e-6:
            fixedWing?cruise+std::sqrt(2.0)/(2.0*65536.0)+1e-9:
            groundLimit*(object&&object->kind==WorldObjectKind::Infantry?2.0:1.0)+1e-9;
        const double zLimit=jumpjet?std::max(type.jumpjetClimb,type.jumpjetCrash)*15.0/(256.0*hz)
            +(type.jumpjetNoWobbles?0.0:2.0*std::abs(type.jumpjetDeviation)/256.0)+1e-6:
            fixedWing?50.0*15.0/(256.0*hz)+1e-6:1e-9;
        auto context=runtimeTerrainMetadataContext(s); context.rules=&s.rules; context.art=&s.art;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.tileRootPath=runtimeTileRootPath(s.config,s.report.map); context.fixedTickHz=hz;
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        const auto continuityProblem=[&](const std::string& why) {
            if (reasons.find(why)==std::string::npos) problem(why);
        };
        const auto recordBody=[&](WorldObjectInstance current,const char* event,
            double deltaXY,double deltaZ,bool chronoArrival) {
            const auto point=body(current); ++continuitySamples;
            if (!std::isfinite(point[0])||!std::isfinite(point[1])||!std::isfinite(point[2])) {
                continuityProblem("nonfinite_body"); return;
            }
            POINT anchor{}, projected{}; bool anchorChecked=false;
            const bool inTransit=s.specialMechanismsInitialized
                &&s.specialMechanisms.isChronoInTransit(id);
            if (!inTransit) {
                const double visualLevel=point[2]-(current.submerged?1.0/3.0:0.0);
                POINT expected{viewport.left+int(std::llround((point[0]-point[1])*s.camera.tileWidth/2.0))-s.camera.worldOffsetX,
                    viewport.top+int(std::llround((point[0]+point[1])*s.camera.tileHeight/2.0
                        -visualLevel*s.camera.tileHeight/2.0))-s.camera.worldOffsetY};
                if (current.airborne&&runtimeObjectUsesAircraftSpriteForRender(current,context))
                    expected.y-=current.aircraftDockFlightProgress>=0.0?
                        int(std::lround(34.0*std::clamp(current.aircraftDockFlightProgress,0.0,1.0))):34;
                context.animationTick=s.tick;
                anchor=runtimeObjectWorldRenderPlacement(current,s.world,viewport,context,s.camera,s.renderCache).center;
                const auto& visible=runtimeProjectedObjectsForViewport(s,viewport);
                const auto found=std::find_if(visible.begin(),visible.end(),[&](const auto& p) {
                    return p.object&&p.object->instanceId==id;
                });
                if (found==visible.end()) continuityProblem("visible_mover_missing_from_projection");
                else { projected=found->renderCenter; anchorChecked=true; ++renderAnchors; }
                if (anchor.x!=expected.x||anchor.y!=expected.y||(anchorChecked
                    &&(projected.x!=expected.x||projected.y!=expected.y)))
                    continuityProblem("same_tick_renderer_projection_body_mismatch");
            }
            const auto bi=s.behaviors.find(id);
            const int remaining=bi==s.behaviors.end()?0:bi->second.renderMoveTicksRemaining;
            const int total=bi==s.behaviors.end()?0:bi->second.renderMoveTicksTotal;
            curves<<type.id<<','<<hz<<','<<direction<<','<<water<<','<<s.tick<<','<<event<<','
                <<current.x<<','<<current.y<<','<<current.subCell<<','<<point[0]<<','<<point[1]<<','<<point[2]<<','
                <<deltaXY<<','<<deltaZ<<','<<xyLimit<<','<<zLimit<<','<<chronoArrival<<','<<current.renderMoveActive
                <<','<<remaining<<','<<total<<','<<anchor.x<<','<<anchor.y<<','<<projected.x<<','<<projected.y
                <<','<<anchorChecked<<'\n';
        };
        if (object) recordBody(*object,"command",commandDeltaXY,commandDeltaZ,false);
        object=findMutableObjectById(s,id);
        if (accepted==1&&object) for (; ticks<hz*180; ++ticks) {
            const auto previousObject=*object;
            const auto previousPoint=body(previousObject);
            const auto previousBehavior=s.behaviors.find(id);
            const bool hadBehavior=previousBehavior!=s.behaviors.end();
            const auto oldBehavior=hadBehavior?previousBehavior->second:RuntimeObjectBehavior{};
            const auto nextEvent=s.nextReplayEventSequence;
            ++s.tick; advanceRuntimeSimulation(s);
            object=findMutableObjectById(s,id);
            if (!object || !object->alive) { problem("object_lost"); break; }
            const auto currentPoint=body(*object);
            const double deltaXY=std::hypot(currentPoint[0]-previousPoint[0],currentPoint[1]-previousPoint[1]);
            const double deltaZ=std::abs(currentPoint[2]-previousPoint[2]);
            bool chronoArrival=false;
            for (const auto& event:s.recentReplayEvents) if (event.sequence>=nextEvent&&event.tick==s.tick
                &&event.kind=="ChronoWarpIn"&&event.objectId==id&&event.commandTargetCell.x==object->x
                &&event.commandTargetCell.y==object->y) chronoArrival=true;
            if (chronoArrival&&!personalChrono) continuityProblem("unexpected_chrono_relocation");
            if (chronoArrival&&personalChrono) ++chronoArrivals;
            else if (deltaXY>xyLimit||deltaZ>zLimit) continuityProblem("body_delta_exceeds_original_speed_envelope");
            const bool committed=previousObject.x!=object->x||previousObject.y!=object->y;
            const auto currentBehavior=s.behaviors.find(id);
            if (!fixedWing&&!jumpjet&&!chronoArrival&&committed) {
                if (std::abs(previousObject.x-object->x)>1||std::abs(previousObject.y-object->y)>1)
                    continuityProblem("nonadjacent_ground_commit");
                if (currentBehavior!=s.behaviors.end()&&object->renderMoveActive
                    &&currentBehavior->second.renderMoveTicksRemaining==currentBehavior->second.renderMoveTicksTotal) {
                    auto endpoint=previousPoint;
                    if (previousObject.renderMoveActive) {
                        if (!hadBehavior||oldBehavior.renderMoveTicksRemaining!=1)
                            continuityProblem("ground_commit_before_previous_segment_endpoint");
                        const auto d=slotOffset(previousObject);
                        endpoint={oldBehavior.renderTo.x+oldBehavior.renderToOffsetX+d[0],
                            oldBehavior.renderTo.y+oldBehavior.renderToOffsetY+d[1],oldBehavior.renderToLevel};
                    }
                    if (std::hypot(currentPoint[0]-endpoint[0],currentPoint[1]-endpoint[1])>1e-9
                        ||std::abs(currentPoint[2]-endpoint[2])>1e-9)
                        continuityProblem("ground_commit_changed_body_without_interpolation");
                }
            }
            if (!fixedWing&&!jumpjet&&!chronoArrival&&hadBehavior&&currentBehavior!=s.behaviors.end()
                &&previousObject.renderMoveActive&&object->renderMoveActive
                &&oldBehavior.renderFrom==currentBehavior->second.renderFrom
                &&oldBehavior.renderTo==currentBehavior->second.renderTo
                &&oldBehavior.renderMoveTicksTotal==currentBehavior->second.renderMoveTicksTotal) {
                const int consumed=oldBehavior.renderMoveTicksRemaining-currentBehavior->second.renderMoveTicksRemaining;
                if (consumed<0||consumed>1) continuityProblem("active_ground_clock_not_zero_or_one_tick");
            }
            recordBody(*object,chronoArrival?"chrono-arrival":committed?"commit":"tick",deltaXY,deltaZ,chronoArrival);
            object=findMutableObjectById(s,id);
            if (!object||!object->alive) { problem("object_lost_during_projection"); break; }
            const auto behavior=s.behaviors.find(id);
            const bool complete=behavior==s.behaviors.end() || behavior->second.state==RuntimeBehaviorState::Complete;
            const bool exact=runtimeCellPointForObject(*object)==goal
                && std::hypot(runtimeObjectRenderX(*object)-goal.x,runtimeObjectRenderY(*object)-goal.y)<1e-6;
            // Observe the first completed arrival: airport aircraft can then
            // receive a separate, legitimate automatic return-to-base order.
            if (exact && (fixedWing || (complete && !object->renderMoveActive))) { arrived=true; ++ticks; break; }
            if (complete && !object->renderMoveActive) { problem("completed_short_of_destination"); ++ticks; break; }
        }
        object=findMutableObjectById(s,id);
        int pixelX=0,pixelY=0;
        if (accepted==1 && !arrived && reasons.empty()) problem("arrival_timeout");
        if (object) {
            if (runtimeCellPointForObject(*object)!=goal) problem("logical_endpoint_mismatch");
            if (std::hypot(runtimeObjectRenderX(*object)-goal.x,runtimeObjectRenderY(*object)-goal.y)>=1e-6)
                problem("render_endpoint_mismatch");
            const auto subcell=worldObjectSubCellOffset(*object);
            const POINT actual=runtimeOverlayCenterForObject(s,*object,viewport);
            const POINT expected=runtimeScreenPoint(runtimeProjectCellXY(goal.x+subcell.x,
                goal.y+subcell.y,runtimeDrawLevelForObject(*object),s.camera),viewport,s.camera);
            pixelX=actual.x-expected.x; pixelY=actual.y-expected.y;
            if (pixelX || pixelY) problem("ground_anchor_mismatch");
            rows << type.id << ',' << hz << ',' << direction << ',' << water << ',' << goal.x << ',' << goal.y
                << ',' << accepted << ',' << assigned.x << ',' << assigned.y << ',' << ticks
                << ',' << object->x << ',' << object->y << ',' << std::setprecision(12)
                << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object)
                << ',' << subcell.x << ',' << subcell.y << ',' << object->airborne
                << ',' << object->renderMoveActive << ',' << pixelX << ',' << pixelY << ',';
        } else rows << type.id << ',' << hz << ',' << direction << ',' << water << ',' << goal.x << ',' << goal.y
            << ',' << accepted << ',' << assigned.x << ',' << assigned.y << ',' << ticks << ",,,,,,,,,,";
        rows << (reasons.empty()?"PASS":reasons) << '\n'; rows.flush();
        if (reasons.empty()) ++passed;
        else {
            ++failed;
            const std::string detail=type.id+" hz="+std::to_string(hz)+" direction="+std::to_string(direction)+" "+reasons;
            if (firstFailure.empty()) firstFailure=detail;
            std::cout << "Move destination FAIL " << detail;
            if (object) std::cout << " cell=" << object->x << ',' << object->y << " render="
                << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object)
                << " expected=" << goal.x << ',' << goal.y << " ticks=" << ticks;
            std::cout << '\n';
        }
        if (diagnostic && reasons.empty()) std::cout << "Move destination PASS " << type.id
            << " hz=" << hz << " direction=" << direction << " ticks=" << ticks << '\n';
    };
    for (int kind=0; kind<3; ++kind) {
        const auto& list=kind==0?stock->rules.getInfantryTypes():kind==1?
            stock->rules.getVehicleTypes():stock->rules.getAircraftTypes();
        for (const auto& type : list) {
            ++listed;
            const auto mode=runtimeUnitMovementModeProfile(type);
            const bool air=kind==2 || mode==PathMovementMode::Air;
            std::string reason;
            if (type.missileSpawn) reason="MissileSpawn";
            else if (type.spawned) reason="Spawned";
            else if (type.speed<=0) reason="Speed_not_positive";
            else if (only && *only && type.id!=only) reason="requested_type_filter";
            inventory << type.id << ',' << kind << ',' << type.speed << ',' << static_cast<int>(mode)
                << ',' << reason.empty() << ',' << reason << '\n';
            if (!reason.empty()) { ++excluded; std::cout << "Move destination EXCLUDED " << type.id << " " << reason << '\n'; continue; }
            ++types;
            const bool allDirections=type.id=="SHAD" || type.id=="SCHP";
            expectedCases+=3*(allDirections?8:1)+(mode==PathMovementMode::Amphibious?1:0);
            const bool water=mode==PathMovementMode::Naval;
            for (int hz : {15,30,60}) {
                for (int direction=0; direction<(allDirections?8:1); ++direction) runCase(type,hz,direction,water);
            }
            // Amphibious units must also reach the same point over water.
            if (mode==PathMovementMode::Amphibious) runCase(type,30,0,true);
        }
    }
    std::cout << "Move destination audit: listed=" << listed << " included=" << types
        << " excluded=" << excluded << " cases=" << cases << " passed=" << passed
        << " failed=" << failed << " mouseProjections=" << projections << '\n';
    if (!types) { error="Move destination audit: no types tested"; return false; }
    if (failed) { error="Move destination audit: "+std::to_string(failed)+" failed cases; "+firstFailure; return false; }
    if (cases!=expectedCases||passed!=expectedCases||continuitySamples<=0||renderAnchors<=0
        ||!curves||!rawInventory) { error="Move destination audit: incomplete continuity evidence"; return false; }
    std::cout << "Move continuity audit: expected=" << expectedCases << " samples=" << continuitySamples
        << " renderAnchors=" << renderAnchors << " authorizedChronoArrivals=" << chronoArrivals << '\n';
    error.clear(); return true;
}
