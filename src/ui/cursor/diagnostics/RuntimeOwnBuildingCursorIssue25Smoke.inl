// Issue25/5: independently decoded original cursor art, stock relationships and actual UI commands.
// Cursor expectations below come from original mouse records and authored rules, not target-intent results.
bool runRuntimeOwnBuildingCursorIssue25Smoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "Issue25 own-building cursor: " + message; return false;
    };
    const char* output = std::getenv("CONCORD_OWN_BUILDING_CURSOR_ISSUE25_OUTPUT");
    if (!output || !*output) return fail("OUTPUT is required");
    const std::string root = output;
    std::filesystem::create_directories(root);
    constexpr int width = 960, height = 640;
    constexpr std::uint32_t background = 0x00283840U;
    const RECT client{0, 0, width, height};
    const RECT viewport = runtimeMapViewport(client);
    const RuntimeCellPoint targetCell{64, 64};
    const auto quote = [](const std::string& value) {
        std::string out = "\"";
        for (char ch : value) { if (ch == '"') out += '"'; out += ch; }
        return out + '"';
    };
    const auto digest = [](std::span<const std::uint8_t> bytes) {
        const auto hash = runtimeMultiplayerSnapshotSha256(bytes);
        std::ostringstream out; out << std::hex << std::setfill('0');
        for (auto byte : hash) out << std::setw(2) << static_cast<unsigned int>(byte);
        return out.str();
    };
    const auto read = [](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto asset = [&](const std::string& relative) {
        return pathString(std::filesystem::path(runtimeAssetRootPath(live.config)) / relative);
    };
    const auto slice = [&](const std::string& path, std::size_t offset, std::size_t count,
                           const std::string& archiveHash, const std::string& entryHash) {
        const auto bytes = read(path);
        if (digest(bytes) != archiveHash || offset > bytes.size() || count > bytes.size() - offset)
            return std::vector<std::uint8_t>{};
        std::vector<std::uint8_t> result(bytes.begin() + offset, bytes.begin() + offset + count);
        if (digest(result) != entryHash) result.clear();
        return result;
    };
    const auto mouse = slice(asset("ra2/conquer.mix"), 13404540, 359800,
        "e0e4be2af64ce35da8e9c865b32ab56b342329de24d3d0a75b1c636f6928d714",
        "e5a356737787d681dd1a2b1255c7c7d1e9bd8334e5674705f2ccc39cd12634df");
    const auto palette = slice(asset("ra2/cache.mix"), 87364, 768,
        "fa98851394eb2bbdfdf672c6d99aa9db8b18eca9f28426b6ce8c22a35926188e",
        "4dcbe78b534a58fc972bce4b25aa58a2a56c9a5bb0ffbfcf5d6194be5dbde60c");
    if (mouse.empty() || palette.size() != 768) return fail("original mouse/palette archive and raw-slice identity");
    const auto u16 = [&](std::size_t p) -> unsigned int {
        if (p + 2 > mouse.size()) return 0;
        return mouse[p] | (static_cast<unsigned int>(mouse[p + 1]) << 8);
    };
    const auto u32 = [&](std::size_t p) -> std::uint32_t {
        if (p + 4 > mouse.size()) return 0;
        return mouse[p] | (static_cast<std::uint32_t>(mouse[p + 1]) << 8)
            | (static_cast<std::uint32_t>(mouse[p + 2]) << 16) | (static_cast<std::uint32_t>(mouse[p + 3]) << 24);
    };
    struct OriginalFrame { int x=0,y=0,w=0,h=0; std::vector<std::uint8_t> indices; };
    const auto decode = [&](int frame) {
        OriginalFrame f;
        if (frame < 0 || frame >= static_cast<int>(u16(6))) return f;
        const std::size_t header = 8 + static_cast<std::size_t>(frame) * 24;
        f.x = u16(header); f.y = u16(header + 2); f.w = u16(header + 4); f.h = u16(header + 6);
        const auto data = u32(header + 20);
        const std::size_t count = static_cast<std::size_t>(f.w) * f.h;
        // Original mouse frames used here are format1: crop rows are stored literally.
        if (u32(header + 8) != 1 || f.w <= 0 || f.h <= 0 || data > mouse.size() || count > mouse.size() - data)
            return OriginalFrame{};
        f.indices.assign(mouse.begin() + data, mouse.begin() + data + count); return f;
    };
    const auto noAction = decode(41);
    if (u16(2) != 55 || u16(4) != 43 || u16(6) != 450 || noAction.x != 5 || noAction.y != 5
        || noAction.w != 46 || noAction.h != 34 || noAction.indices.size() != 1564
        || std::count_if(noAction.indices.begin(), noAction.indices.end(), [](auto i) { return i != 0; }) != 779
        || digest(noAction.indices) != "a9ae9551f3b92122bcc27eaaa3f047f9ea89dbc9d1ec603bca6955767b663ba6")
        return fail("original screenshot frame41 dimensions/779-index-mask identity");
    const auto originalRulesPath = asset("ra2md/localmd/rulesmd.ini");
    const auto rulesBytes = read(originalRulesPath);
    IniFile originalRules;
    if (digest(rulesBytes) != "06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        || !originalRules.loadFromFile(originalRulesPath)) return fail("original rules identity");
    const auto* catalog = originalRules.getSection("InfantryTypes");
    std::set<std::string> distinct, occupiers;
    std::ofstream catalogOut(root + "/catalog.csv"), framesOut(root + "/frames.csv"), casesOut(root + "/cases.csv");
    if (!catalog || !catalogOut || !framesOut || !casesOut) return fail("raw catalog/output");
    catalogOut << "key,type,original_section,original_occupier,loaded_occupier\n";
    for (const auto& [key, type] : *catalog) {
        const auto* loaded = findRuntimeUnitTypeById(live.rules, type);
        const bool section = originalRules.getSection(type) != nullptr;
        const bool occupier = originalRules.getValue(type, "Occupier") == "yes";
        catalogOut << quote(key) << ',' << quote(type) << ',' << section << ',' << occupier << ','
            << (loaded && loaded->occupier) << '\n';
        if (!distinct.insert(type).second || !section || !loaded || loaded->occupier != occupier)
            return fail(type + " original/loaded infantry catalog");
        if (occupier) occupiers.insert(type);
    }
    if (catalog->size() != 65 || distinct.size() != 65 || occupiers != std::set<std::string>{"E1", "E2", "INIT"})
        return fail("original Occupier catalog is not exactly three stock types");
    for (const std::string type : {"GAPOWR", "GAREFN", "NAREFN"}) {
        const auto* loaded = findRuntimeBuildingTypeById(live.rules, type);
        if (!loaded || loaded->canBeOccupied || originalRules.getValue(type, "CanBeOccupied") == "yes")
            return fail(type + " ordinary non-garrison original authority");
    }
    const auto* civilian = findRuntimeBuildingTypeById(live.rules, "CAPRS03");
    if (!civilian || !civilian->canBeOccupied || civilian->maxNumberOccupants != 10
        || originalRules.getValue("CAPRS03", "CanBeOccupied") != "yes"
        || originalRules.getValue("CAPRS03", "MaxNumberOccupants") != "10") return fail("original CAPRS03 ten-slot authority");
    std::ofstream sources(root + "/sources.txt");
    sources << "Original rules SHA256 " << digest(rulesBytes) << "\nInfantryTypes raw=65 unique=65 missing=0 Occupier=E1,E2,INIT\n"
        << "mouse conquer.mix absolute 13404540 length359800 SHA256 " << digest(mouse)
        << "\nmousepal cache.mix absolute87364 length768 SHA256 " << digest(palette)
        << "\nframe41 original crop5,5,46,34 format1 rawoffset44488 nonzero779 indexSHA256 " << digest(noAction.indices)
        << "\nNative MouseClass table index0x13 VA0x82D23C:41,1,0,52,1,12345,12345; hotspot27,21\n"
        << "CAPRS03 CanBeOccupied=yes MaxNumberOccupants=10; all occupants enter through normal commands and simulation.\n";
    framesOut << "case,type,target,tick,frame,expected_frame,hotspot_x,hotspot_y,opaque_pixels,index_sha256,pixel_mismatches,world_before,world_after,passed\n";
    casesOut << "case,type,target,actor_id,target_id,tick,frame,dispatched,command_valid,accepted,rejected,verb,old_move_active,authority_same,occupants,passed\n";
    int caseCount=0, coreCount=0, engineerCount=0, selectionCount=0, garrisonCount=0, forceCount=0;
    int cursorFrames=0, opaqueTotal=0, bmpCount=0, completedOccupants=0;
    struct Fixture { std::unique_ptr<RuntimeWindowState> storage; int actorId=-1,targetId=-1; std::string type,target; };
    const auto makeFixture = [&](const std::string& type, const std::string& targetType) {
        Fixture f; f.storage = std::make_unique<RuntimeWindowState>(); auto& s = *f.storage;
        seedRuntimeLoadScratch(s, live, false);
        s.config.computerAiEnabled=false; s.config.multiplayerActive=false; s.config.fixedTickHz=30;
        s.config.noCooldown=false; s.config.playerSlots.clear();
        s.runtimePreferencePersistenceEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.runtimeSoundsEnabled=false;
        s.campaignMode=false; s.skirmishMatchFinished=false; s.computerAiByOwner.clear();
        s.computerMiningByOwner.clear(); s.humanHouseAuthorities.clear();
        s.triggers={}; s.triggers.bootstrapComplete=true;
        s.economy={}; s.economy.owner="Americans"; s.economy.credits=1000; s.economy.skirmishStartApplied=true;
        s.config.playerCountry=s.economy.owner; s.initialOreFieldsSeeded=true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.structureFootprints.clear(); s.mapFacts={}; s.mapFactsLoaded=false; s.visionEnabled=false;
        for (int y=44; y<=88; ++y) for (int x=44; x<=88; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x; cell.tileIndex=0; cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        f.type=type; f.target=targetType;
        auto* target=addRuntimeObjectInstance(s, RuntimeBuildKind::Building, targetType, targetCell);
        if (!target) return f;
        f.targetId=target->instanceId; target->owner=s.economy.owner; target->mission="Guard";
        target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        auto* actor=addRuntimeObjectInstance(s, type=="MTNK"?RuntimeBuildKind::Unit:RuntimeBuildKind::Infantry, type, {58,64});
        if (!actor) return f;
        f.actorId=actor->instanceId; actor->owner=s.economy.owner; actor->mission="Guard";
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        s.selectedObjectIds={f.actorId}; s.mouseClientPointValid=true;
        centerRuntimeCameraOnCell(s,client,targetCell); invalidateRuntimeObjectProjectionCache(s); return f;
    };
    const auto targetPoint = [&](Fixture& f, POINT& point) {
        auto& s=*f.storage;
        for (const auto& item : runtimeProjectedObjectsForViewport(s,viewport)) {
            if (!item.object || item.object->instanceId!=f.targetId) continue;
            const RECT bounds=item.bounds;
            for (int iy=1; iy<=3; ++iy) for (int ix=1; ix<=3; ++ix) {
                const POINT p{bounds.left+(bounds.right-bounds.left)*ix/4,bounds.top+(bounds.bottom-bounds.top)*iy/4};
                if (!PtInRect(&viewport,p)) continue;
                const auto* hit=runtimeCursorObjectAtPoint(s,viewport,p);
                if (hit && hit->instanceId==f.targetId) { point=p; return true; }
            }
        }
        return false;
    };    const auto authority = [&](Fixture& f) {
        const auto& s=*f.storage; std::ostringstream out; out << std::setprecision(17)
            << s.world.deterministicSimulationStateHash() << '|' << s.tick
            << '|' << s.economy.credits << '|' << s.economy.resourceTrips << '|' << s.economy.storedResourceUnits
            << '|' << s.economy.storedResourceValue;
        for (int id : s.selectedObjectIds) out << "|selected:" << id;
        std::vector<int> ids;
        for (const auto& [id,b] : s.behaviors) { (void)b; ids.push_back(id); }
        std::sort(ids.begin(),ids.end());
        for (int id : ids) {
            const auto& b=s.behaviors.at(id);
            out << "|behavior:" << runtimeMultiplayerSnapshotBehaviorRecord(id,b)
                << '|' << b.moveStartupDistanceQ16 << '|' << b.renderStartupAccelerationDistance
                << '|' << b.renderFrom.x << '|' << b.renderFrom.y << '|' << b.renderTo.x << '|' << b.renderTo.y
                << '|' << b.renderFromOffsetX << '|' << b.renderFromOffsetY << '|' << b.renderToOffsetX << '|' << b.renderToOffsetY
                << '|' << b.renderFromLevel << '|' << b.renderToLevel << '|' << b.renderFromOnBridge << '|' << b.renderToOnBridge
                << '|' << b.renderMoveTicksTotal << '|' << b.renderMoveTicksRemaining << '|' << b.renderArrivalDistanceAfter;
        }
        const auto intMap = [&](const auto& map, const char* name) {
            std::vector<int> keys; for (const auto& [id,value] : map) { (void)value; keys.push_back(id); }
            std::sort(keys.begin(),keys.end()); out << '|' << name << ':' << keys.size();
            for (int id : keys) out << '|' << id << ':' << static_cast<int>(map.at(id));
        };
        intMap(s.pendingEngineerCaptures,"captures"); intMap(s.pendingEngineerActions,"engineerActions");
        intMap(s.pendingOccupyTargets,"occupy"); intMap(s.pendingTransportTargets,"transport");
        out << "|timedBombs:" << s.pendingTimedBombIntents.size();
        ids.clear(); for (const auto& [id,t] : s.harvesterTasks) { (void)t; ids.push_back(id); }
        std::sort(ids.begin(),ids.end()); out << "|harvesterTasks:" << ids.size();
        for (int id : ids) out << '|' << id << ':' << serializeRuntimeHarvesterTask(s.harvesterTasks.at(id));
        ids.clear(); for (const auto& [id,t] : s.slaveMinerTasks) { (void)t; ids.push_back(id); }
        std::sort(ids.begin(),ids.end()); out << "|slaveMinerTasks:" << ids.size();
        for (int id : ids) out << '|' << id << ':' << serializeRuntimeSlaveMinerTask(s.slaveMinerTasks.at(id));
        if (const auto* o=findObjectById(s,f.actorId)) {
            out << "|actor:" << o->x << '|' << o->y << '|' << o->cell << '|' << o->mission << '|' << o->subCell
                << '|' << o->hasRenderPosition << '|' << o->renderMoveActive << '|' << o->renderX << '|' << o->renderY
                << '|' << o->renderLevel << '|' << o->renderOnBridge << '|' << o->garrisonHostObjectId
                << '|' << o->tankBunkerHostId << '|' << o->dockedToObjectId << '|' << o->health;
        }
        return out.str();
    };
    const auto checkFrame = [&](Fixture& f, const std::string& name, int expectedFrame, bool bmp=false) {
        auto& s=*f.storage; POINT point{};
        if (!targetPoint(f,point)) return fail(name+" actual target projection/hit");
        s.mouseClientPoint=point;
        const auto original=decode(expectedFrame);
        if (original.indices.empty()) return fail(name+" original format1 frame unavailable");
        const auto before=s.world.deterministicSimulationStateHash();
        const auto cursor=runtimeMouseCursorFrameForPoint(s,client,point);
        const int hotX=expectedFrame==0?0:27, hotY=expectedFrame==0?0:21;
        if (!cursor || cursor->atlasId!="mouse" || cursor->frameIndex!=expectedFrame
            || cursor->offsetX!=hotX || cursor->offsetY!=hotY) {
            std::ostringstream detail; detail << name << " native frame/hotspot expected=" << expectedFrame
                << " actual=" << (cursor?cursor->frameIndex:-1) << " tick=" << s.tick;
            return fail(detail.str());
        }
        struct Surface {
            RuntimeWindowState& state; HDC screen=GetDC(nullptr);
            ~Surface() { releaseRuntimeBackBuffer(state.backBuffer); if (screen) ReleaseDC(nullptr,screen); }
        } surface{s};
        if (!surface.screen || !ensureRuntimeBackBuffer(s.backBuffer,surface.screen,width,height)) return fail(name+" DIB");
        GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels),width*height,background);
        drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s); GdiFlush();
        std::vector<std::uint32_t> actual(static_cast<const std::uint32_t*>(s.backBuffer.pixels),
            static_cast<const std::uint32_t*>(s.backBuffer.pixels)+width*height);
        std::vector<std::uint32_t> expected(width*height,background); int opaque=0;
        for (int y=0;y<original.h;++y) for (int x=0;x<original.w;++x) {
            const auto index=original.indices[y*original.w+x]; if (!index) continue;
            const int dx=point.x-hotX+original.x+x,dy=point.y-hotY+original.y+y;
            if (dx<0||dy<0||dx>=width||dy>=height) continue;
            const auto r=static_cast<std::uint32_t>(palette[index*3])*4;
            const auto g=static_cast<std::uint32_t>(palette[index*3+1])*4;
            const auto b=static_cast<std::uint32_t>(palette[index*3+2])*4;
            expected[dy*width+dx]=(r<<16)|(g<<8)|b; ++opaque;
        }
        int differences=0;
        for (std::size_t i=0;i<actual.size();++i) { actual[i]&=0xffffffU; differences+=actual[i]!=expected[i]; }
        const auto after=s.world.deterministicSimulationStateHash();
        framesOut << quote(name) << ',' << quote(f.type) << ',' << quote(f.target) << ',' << s.tick << ','
            << cursor->frameIndex << ',' << expectedFrame << ',' << hotX << ',' << hotY << ',' << opaque
            << ',' << digest(original.indices) << ',' << differences << ',' << before << ',' << after
            << ',' << (differences==0&&before==after) << '\n'; framesOut.flush();
        if (opaque<=0 || differences || before!=after || (expectedFrame==41&&opaque!=779))
            return fail(name+" whole-DIB original pixels/world mutation pixels="+std::to_string(differences));
        if (bmp) {
            if (!writeRuntimeSmokeBmp(root+"/"+name+".bmp",actual.data(),width,height,error)) return false;
            ++bmpCount;
        }
        ++cursorFrames; opaqueTotal+=opaque; return true;
    };
    const auto row = [&](Fixture& f,const std::string& name,int frame,bool dispatched,bool oldActive,bool unchanged) {
        const auto& s=*f.storage; const auto* target=findObjectById(s,f.targetId);
        casesOut << quote(name) << ',' << quote(f.type) << ',' << quote(f.target) << ',' << f.actorId << ',' << f.targetId
            << ',' << s.tick << ',' << frame << ',' << dispatched << ',' << s.lastCommand.valid << ',' << s.lastCommand.acceptedCount
            << ',' << s.lastCommand.rejectedCount << ',' << quote(s.lastCommand.verb) << ',' << oldActive << ',' << unchanged
            << ',' << (target?target->garrisonOccupantIds.size():0) << ",1\n";
        casesOut.flush(); ++caseCount;
    };
    const auto startRealMove = [&](Fixture& f,const std::string& name) {
        auto& s=*f.storage; issueRuntimeMoveCommand(s,{48,55});
        if (!s.lastCommand.valid||s.lastCommand.acceptedCount!=1) return fail(name+" initial real Move rejected");
        for (int step=0;step<600;++step) {
            ++s.tick; advanceRuntimeSimulation(s);
            const auto* actor=findObjectById(s,f.actorId); const auto b=s.behaviors.find(f.actorId);
            if (actor&&actor->renderMoveActive&&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move
                &&b->second.renderMoveTicksRemaining>1&&b->second.moveCooldownTicks>1) return true;
        }
        return fail(name+" real continuous Move segment unavailable");
    };
    const auto rejectedPrimary = [&](Fixture& f,const std::string& name,int frame,bool oldActive) {
        auto& s=*f.storage; const auto before=authority(f); const auto* target=findObjectById(s,f.targetId);
        if (!target) return fail(name+" missing command target");
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*target),target);
        const bool unchanged=authority(f)==before;
        if (dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||!unchanged) {
            casesOut << quote(name) << ",FAIL," << dispatched << ',' << s.lastCommand.valid << ','
                << s.lastCommand.acceptedCount << ',' << s.lastCommand.rejectedCount << ',' << unchanged << ',' << quote(s.lastCommand.verb) << '\n';
            casesOut.flush(); return fail(name+" actual Primary rejection changed Move/pose/permissions/tasks");
        }
        row(f,name,frame,dispatched,oldActive,unchanged); return true;
    };
    for (const std::string type : {"E1","E2","INIT","SHK","FLAKT","MTNK"})
        for (const std::string target : {"GAPOWR","GAREFN","NAREFN"}) {
            auto f=makeFixture(type,target); auto& s=*f.storage; RuntimeSnapshotActiveStateScope active(s);
            const std::string name=type+"-own-"+target+"-NoAction41";
            if (f.actorId<0||f.targetId<0||!startRealMove(f,name)) return fail(name+" fixture/Move prerequisite: "+error);
            const auto moving=authority(f);
            if (!checkFrame(f,name,41,type=="E1"&&target=="GAPOWR")) return false;
            if (authority(f)!=moving) return fail(name+" hover changed active Move/actual pose/tasks");
            if (!rejectedPrimary(f,name,41,true)) return false;
            ++coreCount;
        }    // Fill ten original slots one at a time with actual Primary commands and ordinary simulation.
    // Each new E1 is created only after the preceding entrant completed; no position/manifest is seeded.
    {
        auto f=makeFixture("E1","CAPRS03"); auto& s=*f.storage; RuntimeSnapshotActiveStateScope active(s);
        if (f.actorId<0||f.targetId<0) return fail("CAPRS03 fixture");
        std::ofstream entriesOut(root+"/entries.csv");
        entriesOut << "index,actor_id,target_id,command_tick,entered_tick,start_x,start_y,pending_on_command,allowed_host,dispatched,command_valid,accepted,rejected,final_host,occupants,passed\n";
        if (!entriesOut) return fail("CAPRS03 entries output");
        const auto entryFailure = [&](const std::string& label) {
            std::ofstream out(root+"/entry-failure.csv");
            out << "reason,tick,id,target_id,alive,x,y,foot_x,foot_y,mission,has_render,render_active,garrison_host,behavior_kind,behavior_state,goal_x,goal_y,allowed_host,next_repath,blocked_ticks,path_pending,render_ticks_remaining,path\n";
            std::vector<int> pendingIds;
            for (const auto& [id,target] : s.pendingOccupyTargets) { (void)target; pendingIds.push_back(id); }
            std::sort(pendingIds.begin(),pendingIds.end());
            for (int id:pendingIds) {
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,id);
                const auto move=s.behaviors.find(id);
                out << quote(label) << ',' << s.tick << ',' << id << ',' << s.pendingOccupyTargets.at(id);
                if (actor) {
                    const auto offset=worldObjectSubCellOffset(*actor);
                    out << ',' << actor->alive << ',' << actor->x << ',' << actor->y
                        << ',' << std::setprecision(17) << runtimeObjectRenderX(*actor)+offset.x
                        << ',' << runtimeObjectRenderY(*actor)+offset.y << ',' << quote(actor->mission)
                        << ',' << actor->hasRenderPosition << ',' << actor->renderMoveActive << ',' << actor->garrisonHostObjectId;
                } else out << ",0,-1,-1,0,0,missing,0,0,-1";
                if (move!=s.behaviors.end()) {
                    const auto& b=move->second;
                    out << ',' << static_cast<int>(b.kind) << ',' << static_cast<int>(b.state)
                        << ',' << b.moveTarget.x << ',' << b.moveTarget.y << ',' << b.pendingAllowedOccupiedObjectId
                        << ',' << b.nextRepathTick << ',' << b.blockedTicks << ',' << b.pathRequestPending
                        << ',' << b.renderMoveTicksRemaining << ',' << quote(runtimeMultiplayerSnapshotCellsText(b.path));
                } else out << ",-1,-1,-1,-1,-1,-1,-1,0,0,missing";
                out << '\n';
            }
            const auto* host=findObjectById(s,f.targetId);
            std::ostringstream detail; detail << label << " occupants=" << (host?host->garrisonOccupantIds.size():0)
                << " pending=" << s.pendingOccupyTargets.size() << " tick=" << s.tick << " status=" << s.simulationStatus;
            return fail(detail.str());
        };
        std::vector<int> entrants;
        for (int index=0;index<10;++index) {
            const auto* host=findObjectById(s,f.targetId);
            if (!host||host->garrisonOccupantIds.size()!=static_cast<std::size_t>(index)||!s.pendingOccupyTargets.empty())
                return entryFailure("CAPRS03 preceding entrant did not genuinely complete");
            if (index>0) {
                auto* actor=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"E1",{58,64});
                if (!actor) return entryFailure("CAPRS03 original next soldier creation failed");
                actor->owner=s.economy.owner; actor->mission="Guard"; f.actorId=actor->instanceId;
                refreshRuntimeObjectOccupancyAfterLocalChange(s);
            }
            entrants.push_back(f.actorId); s.selectedObjectIds={f.actorId};
            invalidateRuntimeObjectProjectionCache(s);
            if (index==0&&!checkFrame(f,"CAPRS03-legal-ten-sequential-entry",89,true)) return false;
            const auto* actor=findObjectById(s,f.actorId);
            if (!actor) return entryFailure("CAPRS03 live original entrant missing");
            const int startX=actor->x,startY=actor->y,commandTick=s.tick;
            host=findObjectById(s,f.targetId);
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*host),host);
            const auto pending=s.pendingOccupyTargets.find(f.actorId); const auto move=s.behaviors.find(f.actorId);
            if (s.pendingOccupyTargets.size()!=1||pending==s.pendingOccupyTargets.end()||pending->second!=f.targetId
                ||move==s.behaviors.end()||move->second.kind!=RuntimeBehaviorKind::Move
                ||move->second.pendingAllowedOccupiedObjectId!=f.targetId) {
                std::ostringstream label; label << "CAPRS03 actual sequential entry order missing index=" << index
                    << " dispatched=" << dispatched << " valid=" << s.lastCommand.valid
                    << " accepted=" << s.lastCommand.acceptedCount << " rejected=" << s.lastCommand.rejectedCount;
                return entryFailure(label.str());
            }
            // Contextual queues publish pending Move/host permissions rather than lastCommand counts.
            const bool commandValid=s.lastCommand.valid; const int accepted=s.lastCommand.acceptedCount,rejected=s.lastCommand.rejectedCount;
            if (index==0) { row(f,"CAPRS03-legal-ten-sequential-entry",89,dispatched,false,false); ++garrisonCount; }
            bool entered=false;
            for (int step=0;step<1200;++step) {
                ++s.tick; advanceRuntimeSimulation(s);
                const auto* entrant=findRuntimeObjectByIdIncludingInactive(s,f.actorId);
                host=findObjectById(s,f.targetId);
                if (entrant&&entrant->garrisonHostObjectId==f.targetId&&host
                    &&host->garrisonOccupantIds.size()==static_cast<std::size_t>(index+1)&&s.pendingOccupyTargets.empty()) {
                    entered=true; break;
                }
            }
            if (!entered) return entryFailure("CAPRS03 real sequential entrant failed index="+std::to_string(index));
            const auto* entrant=findRuntimeObjectByIdIncludingInactive(s,f.actorId); host=findObjectById(s,f.targetId);
            entriesOut << index << ',' << f.actorId << ',' << f.targetId << ',' << commandTick << ',' << s.tick
                << ',' << startX << ',' << startY << ",1," << f.targetId << ',' << dispatched << ',' << commandValid
                << ',' << accepted << ',' << rejected << ',' << entrant->garrisonHostObjectId
                << ',' << host->garrisonOccupantIds.size() << ",1\n"; entriesOut.flush();
        }
        for (int id:entrants) {
            const auto* actor=findRuntimeObjectByIdIncludingInactive(s,id);
            if (!actor||actor->garrisonHostObjectId!=f.targetId) return entryFailure("CAPRS03 completed original occupant relation missing");
        }
        completedOccupants=10;
        auto* outsider=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"E1",{58,64});
        if (!outsider) return fail("CAPRS03 eleventh original soldier");
        outsider->owner=s.economy.owner; outsider->mission="Guard"; f.actorId=outsider->instanceId;
        refreshRuntimeObjectOccupancyAfterLocalChange(s); s.selectedObjectIds={f.actorId};
        invalidateRuntimeObjectProjectionCache(s);
        if (!checkFrame(f,"CAPRS03-full-real-ten-NoEnter99",99,true)
            ||!rejectedPrimary(f,"CAPRS03-full-real-ten-NoEnter99",99,false)) return false;
        ++garrisonCount;
    }
    for (const std::string type : {"ENGINEER","SENGINEER","YENGINEER"}) for (bool damaged : {false,true}) {
        const auto* originalUnit=findRuntimeUnitTypeById(live.rules,type);
        if (!originalUnit||!originalUnit->engineer||originalRules.getValue(type,"Engineer")!="yes")
            return fail(type+" original Engineer authority");
        auto f=makeFixture(type,"GAPOWR"); auto& s=*f.storage; RuntimeSnapshotActiveStateScope active(s);
        auto* building=findMutableObjectById(s,f.targetId);
        if (!building||f.actorId<0) return fail(type+" engineer fixture");
        const int maximum=runtimeMaxHealthForObject(s,*building);
        if (maximum<=1||building->health!=maximum) return fail(type+" real full-health target prerequisite");
        if (damaged) building->health=maximum-1;
        const int expectedFrame=damaged?150:99;
        const std::string name=type+(damaged?"-own-damaged-repair150":"-own-full-NoEnter99");
        invalidateRuntimeObjectProjectionCache(s);
        if (!checkFrame(f,name,expectedFrame,type=="ENGINEER")) return false;
        if (!damaged) {
            if (!rejectedPrimary(f,name,expectedFrame,false)) return false;
        } else {
            const auto* target=findObjectById(s,f.targetId);
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*target),target);
            const auto action=s.pendingEngineerActions.find(f.actorId);
            const auto pending=s.pendingEngineerCaptures.find(f.actorId);
            const auto move=s.behaviors.find(f.actorId);
            if (s.pendingEngineerActions.size()!=1||s.pendingEngineerCaptures.size()!=1
                ||action==s.pendingEngineerActions.end()||action->second!=RuntimeEngineerAction::Repair
                ||pending==s.pendingEngineerCaptures.end()||pending->second!=f.targetId
                ||move==s.behaviors.end()||move->second.kind!=RuntimeBehaviorKind::Move
                ||move->second.pendingAllowedOccupiedObjectId!=f.targetId) {
                std::ostringstream detail; detail << name << " original legal repair order missing actions=" << s.pendingEngineerActions.size()
                    << " targets=" << s.pendingEngineerCaptures.size() << " dispatched=" << dispatched
                    << " valid=" << s.lastCommand.valid << " accepted=" << s.lastCommand.acceptedCount
                    << " rejected=" << s.lastCommand.rejectedCount;
                return fail(detail.str());
            }
            row(f,name,expectedFrame,dispatched,false,false);
        }
        ++engineerCount;
    }
    for (bool selectedStructure : {false,true}) {
        auto f=makeFixture("E1","GAPOWR"); auto& s=*f.storage; RuntimeSnapshotActiveStateScope active(s);
        const std::string name=selectedStructure?"own-selected-ordinary-structure-arrow0":"no-selection-arrow0";
        if (f.actorId<0||f.targetId<0) return fail(name+" fixture");
        s.selectedObjectIds.clear(); if (selectedStructure) s.selectedObjectIds={f.targetId};
        const auto before=authority(f);
        if (!checkFrame(f,name,0)||authority(f)!=before) return fail(name+" default arrow altered world/task state");
        row(f,name,0,false,false,true); ++selectionCount;
    }
    {
        auto f=makeFixture("E1","GAPOWR"); auto& s=*f.storage; RuntimeSnapshotActiveStateScope active(s);
        if (f.actorId<0||f.targetId<0) return fail("ForceCtrl fixture");
        s.forceAttackHeld=true;
        for (int phase=0;phase<5;++phase) {
            s.tick=phase*4;
            if (!checkFrame(f,"E1-own-ForceCtrl-"+std::to_string(phase),53+phase,phase==0)) return false;
        }
        const auto* target=findObjectById(s,f.targetId);
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,
            runtimeCellPointForObject(*target),target);
        const auto b=s.behaviors.find(f.actorId);
        if (!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0
            ||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Attack||b->second.targetObjectId!=f.targetId
            ||!b->second.forceFire) return fail("ForceCtrl own-building actual forced Attack priority");
        row(f,"E1-own-ForceCtrl-five-original-frames",57,dispatched,false,false); ++forceCount;
    }
    if (caseCount!=29||coreCount!=18||garrisonCount!=2||engineerCount!=6||selectionCount!=2||forceCount!=1
        ||completedOccupants!=10||cursorFrames!=33||bmpCount!=6) return fail("focused terminal/frame/source coverage count");
    std::ofstream result(root+"/result.txt");
    result << "PASS\nterminalCases=" << caseCount << "\nordinaryOwnNoAction41=" << coreCount
        << "\noriginalInfantryTypes=65\noriginalOccupierTypes=3\nrealContinuousMoveRejections=18\n"
        << "garrisonCases=" << garrisonCount << "\ncompletedOriginalGarrisonOccupants=" << completedOccupants
        << "\nengineerCases=" << engineerCount << "\nselectionControls=" << selectionCount
        << "\nforceAttackControls=" << forceCount << "\noriginalCursorFrames=" << cursorFrames
        << "\noriginalOpaquePixels=" << opaqueTotal << "\npixelMismatches=0\nBMPs=" << bmpCount << '\n';
    std::cout << "Issue25 own-building cursor: 29 terminals, 18 real Move rejection controls, 65 original Infantry/3 Occupiers, "
        << "10 real completed garrison entries, 6 engineers, 33 original cursor frames/zero whole-DIB differences passed\n";
    return true;
}