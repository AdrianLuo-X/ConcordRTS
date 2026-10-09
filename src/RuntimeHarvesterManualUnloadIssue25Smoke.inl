// Finite issue25 original mouse pixels, real manual returns and stock entry cancellation.
// Cargo is seeded as fixture input; all return, dock and credit transitions are normal simulation.
bool runRuntimeHarvesterManualUnloadIssue25Smoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "Issue25 manual unload: " + why; return false;
    };
    const char* output = std::getenv("CONCORD_HARVESTER_MANUAL_UNLOAD_ISSUE25_OUTPUT");
    if (!output || !*output) return fail("OUTPUT is required");
    const std::string root = output;
    std::filesystem::create_directories(root);
    constexpr int width = 960, height = 640;
    constexpr std::uint32_t background = 0x00283840U;
    const RECT client{0, 0, width, height};
    const RECT viewport = runtimeMapViewport(client);
    const RuntimeCellPoint foundation{64, 64};
    const auto quote = [](const std::string& value) {
        std::string result = "\"";
        for (char ch : value) { if (ch == '"') result += '"'; result += ch; }
        return result + '"';
    };
    const auto digest = [](std::span<const std::uint8_t> bytes) {
        const auto hash = runtimeMultiplayerSnapshotSha256(bytes);
        std::ostringstream text; text << std::hex << std::setfill('0');
        for (const auto byte : hash) text << std::setw(2) << static_cast<unsigned int>(byte);
        return text.str();
    };
    const auto fileDigest = [&](const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return std::string{};
        const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        return digest({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
    };
    const std::string mouseMix = pathString(std::filesystem::path(runtimeAssetRootPath(live.config)) / "ra2" / "conquer.mix");
    const std::string paletteMix = pathString(std::filesystem::path(runtimeAssetRootPath(live.config)) / "ra2" / "cache.mix");
    const std::string mouseSha = fileDigest(mouseMix), paletteSha = fileDigest(paletteMix);
    if (mouseSha != "e0e4be2af64ce35da8e9c865b32ab56b342329de24d3d0a75b1c636f6928d714"
        || paletteSha != "fa98851394eb2bbdfdf672c6d99aa9db8b18eca9f28426b6ce8c22a35926188e")
        return fail("original mouse/palette archive identity");
    std::ofstream casesOut(root + "/cases.csv"), framesOut(root + "/frames.csv");
    if (!casesOut || !framesOut) return fail("CSV output");
    casesOut << "case,type,refinery,miner_id,target_id,cargo_before,capacity,cargo_after,value_before,value_after,credits_before,credits_after,accepted,rejected,verb,phase_after,ticks,passed\n";
    framesOut << "case,type,phase,tick,frame,expected_frame,offset_x,offset_y,width,height,original_source,palette_source,mouse_archive_sha256,palette_archive_sha256,sprite_sha256,opaque_pixels,pixel_mismatches,world_hash_before,world_hash_after,passed\n";
    int caseCount = 0, minerFrames = 0, repairFrames = 0, bmpCount = 0, unloads = 0, exclusions = 0, mixed = 0;
    int totalOpaque = 0, pixelMismatches = 0;
    int emptyReturns=0, crossPairs=0, moveInterrupts=0, loadingInterrupts=0, attackInterrupts=0;
    int busyControls=0, entryControls=0, clockCheckpoints=0, restoreControls=0;
    struct Fixture {
        std::unique_ptr<RuntimeWindowState> storage;
        int minerId = -1, targetId = -1, tankId = -1;
        std::string type, refinery;
    };
    const auto makeFixture = [&](const std::string& type, bool repair, const std::string& refineryOverride = "", RuntimeCellPoint minerStart = {58,66}) {
        Fixture f; f.storage = std::make_unique<RuntimeWindowState>();
        auto& s = *f.storage;
        seedRuntimeLoadScratch(s, live, false);
        s.config.computerAiEnabled = false; s.config.multiplayerActive = false;
        s.config.fixedTickHz = 30; s.config.noCooldown = false; s.config.playerSlots.clear();
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimeSoundsEnabled = false; s.campaignMode = false; s.skirmishMatchFinished = false;
        s.computerAiByOwner.clear(); s.computerMiningByOwner.clear(); s.humanHouseAuthorities.clear();
        s.triggers = {}; s.triggers.bootstrapComplete = true;
        s.economy = {}; s.economy.owner = type == "CMIN" ? "Americans" : "Russians";
        s.economy.credits = 1000; s.economy.skirmishStartApplied = true;
        s.config.playerCountry = s.economy.owner; s.initialOreFieldsSeeded = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.structureFootprints.clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.visionEnabled = false;
        for (int y = 44; y <= 88; ++y) for (int x = 44; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = 0; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        f.type = type; f.refinery = repair ? "GADEPT" : !refineryOverride.empty() ? refineryOverride : type == "CMIN" ? "GAREFN" : "NAREFN";
        auto* target = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, f.refinery, foundation);
        if (!target) return f;
        f.targetId = target->instanceId; target->owner = s.economy.owner;
        target->mission = "Guard"; target->buildAnimTicksRemaining = target->buildAnimTicksTotal = 0;
        auto* miner = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, type, minerStart);
        if (!miner) return f;
        f.minerId = miner->instanceId; miner->owner = s.economy.owner; miner->mission = "Guard";
        miner->harvestAutoSearchSuspended = true;
        RuntimeHarvesterTask task; task.cargoUnits = 2; task.cargoValue = 50;
        normalizeRuntimeHarvesterTask(s, *miner, task); s.harvesterTasks[f.minerId] = task;
        auto* tank = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {57, 60});
        if (!tank) return f;
        f.tankId = tank->instanceId; tank->owner = s.economy.owner; tank->mission = "Guard";
        if (repair) tank->health = std::max(1, runtimeMaxHealthForObject(s, *tank) - 30);
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        s.selectedObjectIds = {repair ? f.tankId : f.minerId}; s.mouseClientPointValid = true;
        centerRuntimeCameraOnCell(s, client, foundation); invalidateRuntimeObjectProjectionCache(s);
        return f;
    };
    const auto targetPoint = [&](Fixture& f, POINT& point) {
        auto& s = *f.storage;
        const auto& projected = runtimeProjectedObjectsForViewport(s, viewport);
        for (const auto& item : projected) {
            if (!item.object || item.object->instanceId != f.targetId) continue;
            const RECT bounds = item.bounds;
            // Nine bounded points within the original target geometry; no hit-test override.
            for (int iy = 1; iy <= 3; ++iy) for (int ix = 1; ix <= 3; ++ix) {
                const POINT probe{bounds.left + (bounds.right - bounds.left) * ix / 4,
                    bounds.top + (bounds.bottom - bounds.top) * iy / 4};
                if (!PtInRect(&viewport, probe)) continue;
                const auto* hit = runtimeCursorObjectAtPoint(s, viewport, probe);
                if (hit && hit->instanceId == f.targetId) { point = probe; return true; }
            }
        }
        return false;
    };
    const auto caseRow = [&](const std::string& name, Fixture& f, int beforeUnits, int capacity,
                             int beforeValue, int beforeCredits, int ticks) {
        auto& s = *f.storage; const auto taskIt = s.harvesterTasks.find(f.minerId);
        if (taskIt == s.harvesterTasks.end()) return false;
        const auto& task = taskIt->second;
        casesOut << quote(name) << ',' << quote(f.type) << ',' << quote(f.refinery) << ','
            << f.minerId << ',' << f.targetId << ',' << beforeUnits << ',' << capacity << ',' << task.cargoUnits
            << ',' << beforeValue << ',' << task.cargoValue << ',' << beforeCredits << ',' << s.economy.credits
            << ',' << s.lastCommand.acceptedCount << ',' << s.lastCommand.rejectedCount << ',' << quote(s.lastCommand.verb)
            << ',' << quote(runtimeHarvesterPhaseName(task.phase)) << ',' << ticks << ",1\n";
        ++caseCount; return true;
    };
    const auto checkFrames = [&](Fixture& f, const std::string& name, bool writeBmp) {
        auto& s = *f.storage; RuntimeSnapshotActiveStateScope active(s);
        POINT point{}; if (!targetPoint(f, point)) return fail(name + " real target projection/hit");
        s.mouseClientPoint = point;
        struct Surface {
            RuntimeWindowState& state; HDC screen = GetDC(nullptr);
            ~Surface() { releaseRuntimeBackBuffer(state.backBuffer); if (screen) ReleaseDC(nullptr, screen); }
        } surface{s};
        if (!surface.screen || !ensureRuntimeBackBuffer(s.backBuffer, surface.screen, width, height)) return fail("DIB");
        std::vector<RuntimePaletteColor> palette; std::string paletteSource;
        if (!loadRuntimePaletteFromMixArchive(paletteMix, s.renderCache, "mousepal", palette, paletteSource, error)
            || palette.size() < 256) return fail("original palette: " + error);
        for (int phase = 0; phase < 10; ++phase) {
            s.tick = phase * 4; const auto worldBefore = s.world.deterministicSimulationStateHash();
            const auto cursor = runtimeMouseCursorFrameForPoint(s, client, point);
            if (!cursor || cursor->atlasId != "mouse" || cursor->frameIndex != 89 + phase
                || cursor->offsetX != 27 || cursor->offsetY != 21) return fail(name + " native Enter frame/hotspot");
            GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels), width * height, background);
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc, client, s); GdiFlush();
            std::vector<std::uint32_t> actual(static_cast<const std::uint32_t*>(s.backBuffer.pixels),
                static_cast<const std::uint32_t*>(s.backBuffer.pixels) + width * height);
            for (auto& color : actual) color &= 0xffffffU;
            RuntimeShpFrameImage sprite; std::string spriteSource;
            if (!loadRuntimeShpFrameFromMixArchive(mouseMix, s.renderCache, "mouse", 89 + phase, sprite, spriteSource, error))
                return fail("original mouse: " + error);
            std::vector<std::uint32_t> expected(width * height, background);
            int opaque = 0;
            for (int y = 0; y < sprite.height; ++y) for (int x = 0; x < sprite.width; ++x) {
                const int dx = point.x - 27 + sprite.offsetX + x, dy = point.y - 21 + sprite.offsetY + y;
                const auto index = sprite.pixels[y * sprite.width + x];
                if (index && dx >= 0 && dx < width && dy >= 0 && dy < height) {
                    const auto color = palette[index]; expected[dy * width + dx] = (color.r << 16) | (color.g << 8) | color.b; ++opaque;
                }
            }
            int different = 0; for (std::size_t i = 0; i < actual.size(); ++i) different += actual[i] != expected[i];
            const auto worldAfter = s.world.deterministicSimulationStateHash();
            if (opaque <= 0 || different || worldBefore != worldAfter) return fail(name + " original whole cursor pixels/authority");
            framesOut << quote(name) << ',' << quote(f.type) << ',' << phase << ',' << s.tick << ',' << cursor->frameIndex
                << ',' << 89 + phase << ',' << cursor->offsetX << ',' << cursor->offsetY << ',' << sprite.width << ',' << sprite.height
                << ',' << quote(spriteSource) << ',' << quote(paletteSource) << ',' << mouseSha << ',' << paletteSha
                << ',' << digest({sprite.pixels.data(), sprite.pixels.size()}) << ',' << opaque << ',' << different
                << ',' << worldBefore << ',' << worldAfter << ",1\n";
            totalOpaque += opaque; pixelMismatches += different;
            if (writeBmp && (phase == 0 || phase == 9)) {
                if (!writeRuntimeSmokeBmp(root + "/" + f.type + "-cursor-" + std::to_string(phase) + ".bmp",
                    actual.data(), width, height, error)) return fail(error);
                ++bmpCount;
            }
            if (writeBmp) ++minerFrames; else ++repairFrames;
        }
        if (writeBmp) {
            std::vector<std::uint32_t> scene;
            if (!drawRuntimeStateOffscreenAtCell(s, foundation, width, height, error, "", false, true, -1,
                    &scene, true, &s.camera)) return fail(name + " original scene: " + error);
            GdiFlush(); std::memcpy(s.backBuffer.pixels, scene.data(), scene.size() * sizeof(std::uint32_t));
            // The scene uses the same camera and the real mouse overlay, rather than a synthetic sprite.
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc, client, s); GdiFlush();
            if (!writeRuntimeSmokeBmp(root + "/" + f.type + "-scene.bmp", s.backBuffer.pixels, width, height, error)) return fail(error);
            ++bmpCount;
        }
        return true;
    };

    const auto originalRulesPath=pathString(std::filesystem::path(runtimeAssetRootPath(live.config))/"ra2md"/"localmd"/"rulesmd.ini");
    IniFile originalRules;
    if(fileDigest(originalRulesPath)!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        ||!originalRules.loadFromFile(originalRulesPath))return fail("original rules identity");
    for(const std::string type:{"HARV","CMIN"}){
        const auto* unit=findRuntimeUnitTypeById(live.rules,type);
        const int capacity=type=="HARV"?40:20;
        if(!unit||!unit->harvester||unit->storage!=capacity||unit->bunkerable
            ||originalRules.getValue(type,"Harvester")!="yes"
            ||originalRules.getValue(type,"Storage")!=std::to_string(capacity)
            ||originalRules.getValue(type,"Dock")!="NAREFN,GAREFN"
            ||originalRules.getValue(type,"Bunkerable")!="no")return fail(type+" original production miner rules");
    }
    const auto* alias=findRuntimeUnitTypeById(live.rules,"HORV");
    const auto* bunkerRules=findRuntimeBuildingTypeById(live.rules,"NATBNK");
    const auto* bunkerArt=live.art.findArtType("NATBNK");
    if(!alias||!alias->harvester||!alias->bunkerable
        ||originalRules.getValue("HORV","Harvester")!="yes"
        ||originalRules.getValue("HORV","TechLevel")!="-1"
        ||!originalRules.getValue("HORV","Bunkerable").empty()
        ||originalRules.getValue("HORV","Dock")!="NAREFN,GAREFN"
        ||!bunkerRules||!bunkerRules->bunker||!bunkerArt||bunkerArt->foundation!="2x2")
        return fail("original map/script HORV and NATBNK authority");
    for(const std::string type:{"GAREFN","NAREFN"}){
        const auto* rules=findRuntimeBuildingTypeById(live.rules,type);
        if(!rules||!rules->refinery||!rules->dockUnload||rules->numberOfDocks!=1||rules->storage!=200
            ||originalRules.getValue(type,"Refinery")!="yes"
            ||originalRules.getValue(type,"DockUnload")!="yes"
            ||originalRules.getValue(type,"NumberOfDocks")!="1"
            ||originalRules.getValue(type,"Storage")!="200")return fail(type+" original refinery rules");
    }
    const auto originalArtPath=pathString(std::filesystem::path(runtimeAssetRootPath(live.config))/"ra2md"/"localmd"/"artmd.ini");
    IniFile originalArt;const auto* queueArt=live.art.findArtType("NAREFN");
    if(fileDigest(originalArtPath)!="e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31"
        ||!originalArt.loadFromFile(originalArtPath)||originalArt.getValue("NAREFN","Foundation")!="4x3"
        ||originalArt.getValue("NAREFN","QueueingCell")!="4,1"||!queueArt||queueArt->foundation!="4x3"
        ||!queueArt->hasQueueingCell||queueArt->queueingCell.x!=4||queueArt->queueingCell.y!=1
        ||originalRules.getValue("General","HarvesterTooFarDistance")!="5"
        ||live.rules.getHarvesterTooFarDistance()!=5.0)return fail("original near-queue geometry and range");
    std::ofstream statesOut(root+"/states.csv"),sourcesOut(root+"/sources.csv");
    if(!statesOut||!sourcesOut)return fail("state/source CSV output");
    sourcesOut<<"kind,path,sha256,contract\nrules,"<<quote(originalRulesPath)<<','<<fileDigest(originalRulesPath)
        <<",HARV40/CMIN20/DockBoth/HORV-map-script-BunkerableDefault/NATBNK2x2\n";
    sourcesOut<<"art,"<<quote(originalArtPath)<<','<<fileDigest(originalArtPath)<<",NAREFN-Foundation4x3-Queueing4/1\n";
    statesOut<<"case,event,tick,type,target_id,cell_x,cell_y,render_x,render_y,active,phase,cargo,value,work,next_think,dock_x,dock_y,queue_x,queue_y,behavior,allowed_host,from_x,from_y,to_x,to_y,remaining,credits,trips,stored_units,stored_value,teleports,accepted,rejected\n";
    const auto sample=[&](Fixture& f,const std::string& name,const char* event){
        auto& s=*f.storage;const auto* o=findObjectById(s,f.minerId);
        const auto ti=s.harvesterTasks.find(f.minerId);const auto bi=s.behaviors.find(f.minerId);
        RuntimeHarvesterTask missing;const auto& t=ti==s.harvesterTasks.end()?missing:ti->second;
        statesOut<<quote(name)<<','<<event<<','<<s.tick<<','<<quote(f.type)<<','<<f.targetId
            <<','<<(o?o->x:-1)<<','<<(o?o->y:-1)<<','<<std::setprecision(17)<<(o?runtimeObjectRenderX(*o):-1)
            <<','<<(o?runtimeObjectRenderY(*o):-1)<<','<<(o?o->renderMoveActive:false)
            <<','<<(ti==s.harvesterTasks.end()?"missing":runtimeHarvesterPhaseName(t.phase))
            <<','<<t.cargoUnits<<','<<t.cargoValue<<','<<t.workTicksRemaining<<','<<t.nextThinkTick
            <<','<<t.dockCell.x<<','<<t.dockCell.y<<','<<t.queueCell.x<<','<<t.queueCell.y
            <<','<<(bi==s.behaviors.end()?-1:int(bi->second.kind))
            <<','<<(bi==s.behaviors.end()?-1:bi->second.pendingAllowedOccupiedObjectId)
            <<','<<(bi==s.behaviors.end()?-1:bi->second.renderFrom.x)<<','<<(bi==s.behaviors.end()?-1:bi->second.renderFrom.y)
            <<','<<(bi==s.behaviors.end()?-1:bi->second.renderTo.x)<<','<<(bi==s.behaviors.end()?-1:bi->second.renderTo.y)
            <<','<<(bi==s.behaviors.end()?0:bi->second.renderMoveTicksRemaining)
            <<','<<s.economy.credits<<','<<s.economy.resourceTrips<<','<<s.economy.storedResourceUnits
            <<','<<s.economy.storedResourceValue<<','<<s.economy.harvesterTeleports
            <<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<'\n';
        if(std::string(event)!="tick")statesOut.flush();
    };
    const auto step=[&](Fixture& f,const std::string& name){
        ++f.storage->tick;advanceRuntimeSimulation(*f.storage);sample(f,name,"tick");
    };
    const auto primary=[&](Fixture& f,const std::string& name){
        auto& s=*f.storage;s.selectedObjectIds={f.minerId};sample(f,name,"before-command");
        const auto* target=findObjectById(s,f.targetId);
        if(!target||!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*target),target)||s.lastCommand.verb!="harvesterUnload"
            ||s.lastCommand.targetObjectId!=f.targetId||s.lastCommand.acceptedCount!=1
            ||s.lastCommand.rejectedCount!=0)return fail(name+" actual Primary return dispatch");
        sample(f,name,"after-command");return true;
    };
    const auto clockCheckpoint=[&](Fixture& f,const std::string& name){
        auto& s=*f.storage;const auto before=s.harvesterTasks.at(f.minerId);
        const auto* o=findObjectById(s,f.minerId);const auto* refinery=findObjectById(s,f.targetId);
        if(!o||!refinery||!runtimeHarvesterIsAtDock(*o,before))return fail(name+" actual dock prerequisite");
        const auto credits=s.economy.credits,trips=s.economy.resourceTrips,units=s.economy.storedResourceUnits,value=s.economy.storedResourceValue;
        const auto cell=runtimeCellPointForObject(*o);const double x=runtimeObjectRenderX(*o),y=runtimeObjectRenderY(*o);
        const auto mission=o->mission,refineryMission=refinery->mission;
        if(!primary(f,name))return false;
        const auto* after=findObjectById(s,f.minerId);
        if(serializeRuntimeHarvesterTask(s.harvesterTasks.at(f.minerId))!=serializeRuntimeHarvesterTask(before)
            ||s.economy.credits!=credits||s.economy.resourceTrips!=trips||s.economy.storedResourceUnits!=units
            ||s.economy.storedResourceValue!=value||!after||runtimeCellPointForObject(*after)!=cell
            ||runtimeObjectRenderX(*after)!=x||runtimeObjectRenderY(*after)!=y||after->mission!=mission
            ||findObjectById(s,f.targetId)->mission!=refineryMission)return fail(name+" same-dock clock/cargo/pose changed");
        ++clockCheckpoints;return true;
    };
    const auto restoreUnloading=[&](Fixture& source){
        auto& s=*source.storage;const auto before=s.harvesterTasks.at(source.minerId);
        if(before.phase!=RuntimeHarvesterPhase::PreparingToUnload||before.workTicksRemaining<=0)
            return fail(source.type+" restore control not from real Preparing window");
        std::ostringstream saved;std::string loadError;
        if(!writeRuntimeStateToStream(s,saved,loadError))return fail("Preparing complete save: "+loadError);
        auto text=saved.str();const auto oldRecord=std::to_string(source.minerId)+"|"+serializeRuntimeHarvesterTask(before);
        auto input=before;input.phase=RuntimeHarvesterPhase::Unloading;
        const auto position=text.find(oldRecord);
        if(position==std::string::npos)return fail("Preparing save task record unavailable");
        // This is a labelled legal parser input, not a naturally published
        // simulation phase: retail-style whole-class dumping is atomic.
        text.replace(position,oldRecord.size(),std::to_string(source.minerId)+"|"+serializeRuntimeHarvesterTask(input));
        IniFile ini;Fixture loaded;loaded.storage=std::make_unique<RuntimeWindowState>();
        loaded.type=source.type;loaded.refinery=source.refinery;loaded.minerId=source.minerId;
        loaded.targetId=source.targetId;loaded.tankId=source.tankId;
        seedRuntimeLoadScratch(*loaded.storage,s,false);
        if(!ini.loadFromString(text)||!materializeRuntimeLoadStateFromIni(*loaded.storage,ini,loadError))
            return fail("Unloading restore-input: "+loadError);
        std::ofstream evidence(root+"/"+source.type+"-unloading-restore-input.ini");evidence<<text;
        RuntimeSnapshotActiveStateScope active(*loaded.storage);
        if(serializeRuntimeHarvesterTask(loaded.storage->harvesterTasks.at(loaded.minerId))!=serializeRuntimeHarvesterTask(input))
            return fail("Unloading parser changed legal dock input");
        const auto name=source.type+"-unloading-restore-input";
        if(!clockCheckpoint(loaded,name))return false;
        const int credits=loaded.storage->economy.credits,trips=loaded.storage->economy.resourceTrips;
        step(loaded,name);const auto& after=loaded.storage->harvesterTasks.at(loaded.minerId);
        if(after.cargoUnits||after.cargoValue||after.phase!=RuntimeHarvesterPhase::SeekingOre
            ||loaded.storage->economy.credits!=credits+input.cargoValue
            ||loaded.storage->economy.resourceTrips!=trips+1)return fail(name+" real restored dump conservation");
        if(!caseRow(name,loaded,input.cargoUnits,input.cargoCapacity,input.cargoValue,credits,1))return fail("restore CSV");
        ++restoreControls;return true;
    };
    const auto deliver=[&](Fixture& f,const std::string& name,bool issue=true){
        auto& s=*f.storage;const auto before=s.harvesterTasks.at(f.minerId);
        const int credits=s.economy.credits,trips=s.economy.resourceTrips,storedUnits=s.economy.storedResourceUnits,storedValue=s.economy.storedResourceValue;
        const auto start=runtimeCellPointForObject(*findObjectById(s,f.minerId));bool travel=false,docking=false,preparing=false,done=false;
        if(issue&&!primary(f,name))return false;
        if(s.harvesterTasks.at(f.minerId).cargoUnits!=before.cargoUnits||s.harvesterTasks.at(f.minerId).cargoValue!=before.cargoValue
            ||s.economy.credits!=credits)return fail(name+" command consumed cargo or granted money");
        int elapsed=0;
        for(;elapsed<3000;++elapsed){
            step(f,name);const auto it=s.harvesterTasks.find(f.minerId);const auto* o=findObjectById(s,f.minerId);
            if(it==s.harvesterTasks.end()||!o||!o->alive)return fail(name+" actor/task lost");
            const auto& t=it->second;travel=travel||runtimeCellPointForObject(*o)!=start;
            docking=docking||t.phase==RuntimeHarvesterPhase::Docking;preparing=preparing||t.phase==RuntimeHarvesterPhase::PreparingToUnload;
            if(s.economy.credits-credits+t.cargoValue!=before.cargoValue
                ||s.economy.resourceTrips<trips||s.economy.resourceTrips>trips+(before.cargoValue>0?1:0))
                return fail(name+" per-tick cargo/credits/trips conservation");
            if(before.cargoValue==0&&(s.economy.storedResourceUnits!=storedUnits||s.economy.storedResourceValue!=storedValue))
                return fail(name+" empty trip changed resource storage");
            if(docking&&preparing&&t.phase==RuntimeHarvesterPhase::SeekingOre&&!t.cargoUnits&&!t.cargoValue
                &&runtimeHarvesterIsAtDock(*o,t)&&!s.behaviors.contains(f.minerId)){done=true;++elapsed;break;}
        }
        const auto& after=s.harvesterTasks.at(f.minerId);
        if(!done||!travel||after.targetRefineryId!=f.targetId||s.economy.credits!=credits+before.cargoValue
            ||s.economy.resourceTrips!=trips+(before.cargoValue>0?1:0)
            ||s.economy.storedResourceUnits!=storedUnits+before.cargoUnits||s.economy.storedResourceValue!=storedValue+before.cargoValue)
            return fail(name+" real travel/dock/finish timeout or final conservation: "+serializeRuntimeHarvesterTask(after));
        if(!caseRow(name,f,before.cargoUnits,before.cargoCapacity,before.cargoValue,credits,elapsed))return fail("delivery CSV");
        return true;
    };
    const auto seedCargo=[&](Fixture& f,int units){
        auto& s=*f.storage;RuntimeHarvesterTask task;task.cargoUnits=units;task.cargoValue=units*25;
        normalizeRuntimeHarvesterTask(s,*findObjectById(s,f.minerId),task);s.harvesterTasks[f.minerId]=task;
    };
    const auto naturalLoading=[&](Fixture& f,const std::string& name){
        auto& s=*f.storage;const RuntimeCellPoint ore{55,68};
        for(auto& cell:s.world.accessTerrainCells())if(cell.x>=55&&cell.x<=57&&cell.y>=68&&cell.y<=70)
            setRuntimeResourceTerrainBails(cell,"TIB",12);
        refreshRuntimeObjectOccupancyAfterLocalChange(s);initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);
        s.harvesterTasks.erase(f.minerId);findMutableObjectById(s,f.minerId)->harvestAutoSearchSuspended=true;
        s.selectedObjectIds={f.minerId};issueRuntimeMoveCommand(s,ore);
        if(s.lastCommand.acceptedCount!=1||s.lastCommand.verb!="harvest")return fail(name+" original ore command");
        for(int tick=0;tick<2500;++tick){
            step(f,name);const auto ti=s.harvesterTasks.find(f.minerId);
            if(ti!=s.harvesterTasks.end()&&ti->second.cargoUnits>0&&ti->second.phase==RuntimeHarvesterPhase::Loading
                &&ti->second.workTicksRemaining>0&&s.economy.lastHarvesterGatherBeforeBails>s.economy.lastHarvesterGatherAfterBails){
                sample(f,name,"natural-loading");return true;
            }
        }
        return fail(name+" never naturally gathered a real bail");
    };

    for (const std::string type : {"HARV", "CMIN"}) {
        auto f = makeFixture(type, false); auto& s = *f.storage;
        if (f.minerId < 0 || f.targetId < 0 || f.tankId < 0) return fail(type + " stock fixture missing");
        const auto* unit = findRuntimeUnitType(s.rules, *findObjectById(s, f.minerId));
        const auto& input = s.harvesterTasks.at(f.minerId);
        const int capacity = input.cargoCapacity;
        if (!unit || !unit->harvester || capacity != unit->storage || input.cargoUnits != 2
            || input.cargoValue != 50 || capacity <= 2 || s.economy.harvesters != 1 || s.economy.refineries != 1
            || !runtimeHarvesterCanUnloadAtRefinery(s,
                *findObjectById(s, f.minerId), *findObjectById(s, f.targetId))) return fail(type + " partial/Dock rules fixture");
        if (!checkFrames(f, type + "-partial", true)) return false;
        RuntimeSnapshotActiveStateScope active(s);
        const WorldObjectInstance tankBefore = *findObjectById(s, f.tankId);
        const bool tankHadBehavior = s.behaviors.contains(f.tankId);
        if (type == "HARV") s.selectedObjectIds = {f.tankId, f.minerId};
        const int beforeCredits = s.economy.credits;
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary, foundation,
                findObjectById(s, f.targetId)) || s.lastCommand.verb != "harvesterUnload"
            || s.lastCommand.acceptedCount != 1 || s.lastCommand.targetObjectId != f.targetId
            || s.lastCommand.rejectedCount != (type == "HARV" ? 1 : 0)) return fail(type + " actual Primary dispatch");
        const auto& submitted = s.harvesterTasks.at(f.minerId);
        const auto* afterCommand = findObjectById(s, f.minerId);
        if (!afterCommand || afterCommand->harvestAutoSearchSuspended
            || submitted.phase != RuntimeHarvesterPhase::ReturningToRefinery || submitted.targetRefineryId != f.targetId
            || submitted.cargoUnits != 2 || submitted.cargoValue != 50 || s.economy.credits != beforeCredits)
            return fail(type + " command changed cargo or did not submit a real return");
        if (type == "HARV") {
            const auto* tankAfter = findObjectById(s, f.tankId);
            if (!tankAfter || tankAfter->owner != tankBefore.owner || tankAfter->mission != tankBefore.mission
                || tankAfter->x != tankBefore.x || tankAfter->y != tankBefore.y || tankAfter->health != tankBefore.health
                || s.behaviors.contains(f.tankId) != tankHadBehavior) return fail("mixed selection changed the rejected tank");
            if (!caseRow("mixed-selected-tank-miner", f, 2, capacity, 50, beforeCredits, 0)) return fail("mixed CSV");
            ++mixed;
        }
        bool delivered = false, sawDocking = false, sawPreparing = false, actualTravel = false;
        bool repeatedDock=false,repeatedPreparing=false;
        const auto startCell = runtimeCellPointForObject(*afterCommand);
        int elapsed = 0;
        for (; elapsed < 1800 && !delivered; ++elapsed) {
            ++s.tick; advanceRuntimeSimulation(s);
            const auto task = s.harvesterTasks.find(f.minerId); const auto* actor = findObjectById(s, f.minerId);
            if (task == s.harvesterTasks.end() || !actor || !actor->alive) return fail(type + " actor/task lost during return");
            if(task->second.phase==RuntimeHarvesterPhase::Docking&&!repeatedDock){
                if(!clockCheckpoint(f,type+"-natural-Docking-repeat"))return false;repeatedDock=true;
            }
            if(task->second.phase==RuntimeHarvesterPhase::PreparingToUnload&&!repeatedPreparing){
                if(!clockCheckpoint(f,type+"-natural-Preparing-repeat")||!restoreUnloading(f))return false;repeatedPreparing=true;
            }
            sawDocking = sawDocking || task->second.phase == RuntimeHarvesterPhase::Docking;
            sawPreparing = sawPreparing || task->second.phase == RuntimeHarvesterPhase::PreparingToUnload;
            actualTravel = actualTravel || runtimeCellPointForObject(*actor) != startCell;
            if (task->second.cargoUnits == 0 && task->second.cargoValue == 0) {
                if (s.economy.credits - beforeCredits != 50 || s.economy.lastResourceBaseValue != 50
                    || s.economy.lastResourcePurifierBonus != 0 || s.economy.resourceTrips != 1
                    || task->second.targetRefineryId != f.targetId) return fail(type + " actual unload credits/cargo/target");
                delivered = true;
            }
        }
        if (!delivered || !sawDocking || !sawPreparing || !actualTravel || !repeatedDock || !repeatedPreparing) return fail(type + " real travel/dock/deposit timeout");
        if (!caseRow(type + "-partial-unload", f, 2, capacity, 50, beforeCredits, elapsed)) return fail("delivery CSV");
        ++unloads;
        std::cout << "Issue25 manual unload: " << type << " partial=2/" << capacity << " Primary target=" << f.targetId
            << " real travel/dock delivered=50 cargo=0 credits=" << beforeCredits << "->" << s.economy.credits << '\n';
    }

    for(const std::string type:{"HARV","CMIN"})for(bool missing:{true,false}){
        auto f=makeFixture(type,false);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const auto name=type+std::string(missing?"-empty-no-task":"-empty-existing-task");
        if(missing)s.harvesterTasks.erase(f.minerId);else seedCargo(f,0);
        const int credits=s.economy.credits,trips=s.economy.resourceTrips,units=s.economy.storedResourceUnits,value=s.economy.storedResourceValue;
        if(!primary(f,name))return false;
        const auto& submitted=s.harvesterTasks.at(f.minerId);
        if(submitted.cargoUnits||submitted.cargoValue||s.economy.credits!=credits||s.economy.resourceTrips!=trips
            ||s.economy.storedResourceUnits!=units||s.economy.storedResourceValue!=value)return fail(name+" empty dispatch gained resources");
        if(!deliver(f,name,false))return false;++emptyReturns;
    }
    for(const std::string type:{"HARV","CMIN"}){
        const auto refinery=type=="HARV"?"GAREFN":"NAREFN";
        auto f=makeFixture(type,false,refinery);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const int capacity=s.harvesterTasks.at(f.minerId).cargoCapacity;
        seedCargo(f,type=="HARV"?1:capacity-1);
        if(!deliver(f,type+"-cross-Dock-"+refinery))return false;++crossPairs;
    }
    for(const std::string type:{"HARV","CMIN"}){
        auto f=makeFixture(type,false);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const auto name=type+"-active-Move-full-return";seedCargo(f,s.harvesterTasks.at(f.minerId).cargoCapacity);
        s.selectedObjectIds={f.minerId};issueRuntimeMoveCommand(s,{48,55});
        if(s.lastCommand.acceptedCount!=1)return fail(name+" normal ground Move rejected");
        bool segment=false;
        for(int tick=0;tick<600;++tick){
            step(f,name);const auto bi=s.behaviors.find(f.minerId);const auto* o=findObjectById(s,f.minerId);
            if(o&&o->renderMoveActive&&bi!=s.behaviors.end()&&bi->second.kind==RuntimeBehaviorKind::Move
                &&bi->second.moveCooldownTicks>1&&bi->second.renderMoveTicksRemaining>1){segment=true;break;}
        }
        if(!segment)return fail(name+" real continuous Move leg unavailable");
        const auto previous=s.behaviors.at(f.minerId);const auto* actor=findObjectById(s,f.minerId);
        const auto cell=runtimeCellPointForObject(*actor);const double x=runtimeObjectRenderX(*actor),y=runtimeObjectRenderY(*actor);
        const int teleports=s.economy.harvesterTeleports;
        if(!primary(f,name))return false;
        const auto bi=s.behaviors.find(f.minerId);actor=findObjectById(s,f.minerId);
        if(!actor||runtimeCellPointForObject(*actor)!=cell||runtimeObjectRenderX(*actor)!=x||runtimeObjectRenderY(*actor)!=y
            ||!actor->renderMoveActive||bi==s.behaviors.end()||bi->second.kind!=RuntimeBehaviorKind::Move
            ||bi->second.renderFrom!=previous.renderFrom||bi->second.renderTo!=previous.renderTo
            ||bi->second.moveCooldownTicks!=previous.moveCooldownTicks
            ||bi->second.renderMoveTicksRemaining!=previous.renderMoveTicksRemaining
            ||bi->second.renderMoveTicksTotal!=previous.renderMoveTicksTotal
            ||s.economy.harvesterTeleports!=teleports)return fail(name+" click changed the committed continuous leg");
        if(!deliver(f,name,false))return false;++moveInterrupts;
    }
    for(const std::string type:{"HARV","CMIN"}){
        auto f=makeFixture(type,false);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const auto name=type+"-natural-Loading-partial-return";
        if(!naturalLoading(f,name))return false;
        const int units=s.harvesterTasks.at(f.minerId).cargoUnits,value=s.harvesterTasks.at(f.minerId).cargoValue;
        if(units<=0||units>=s.harvesterTasks.at(f.minerId).cargoCapacity||value<=0)return fail(name+" actual partial intake prerequisite");
        if(!primary(f,name)||s.harvesterTasks.at(f.minerId).cargoUnits!=units
            ||s.harvesterTasks.at(f.minerId).cargoValue!=value||s.harvesterTasks.at(f.minerId).workTicksRemaining!=0
            ||s.harvesterTasks.at(f.minerId).targetOre!=RuntimeCellPoint{})return fail(name+" old intake was not cancelled intact");
        if(!deliver(f,name,false))return false;++loadingInterrupts;
    }
    {
        auto f=makeFixture("HARV",false);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const std::string name="HARV-real-Attack-return";auto* enemy=findMutableObjectById(s,f.tankId);
        enemy->owner="Americans";enemy->weaponCooldownTicksRemaining=100000;const int health=enemy->health;
        s.selectedObjectIds={f.minerId};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,
                runtimeCellPointForObject(*enemy),enemy)||s.lastCommand.acceptedCount!=1)return fail(name+" actual attack command");
        bool fighting=false;
        for(int tick=0;tick<1200;++tick){
            step(f,name);const auto bi=s.behaviors.find(f.minerId);enemy=findMutableObjectById(s,f.tankId);
            if(bi!=s.behaviors.end()&&bi->second.kind==RuntimeBehaviorKind::Attack
                &&(!s.projectiles.empty()||(enemy&&enemy->health<health))){fighting=true;break;}
        }
        if(!fighting)return fail(name+" never observed actual weapon combat");
        sample(f,name,"actual-combat");
        if(!primary(f,name))return false;
        if(const auto bi=s.behaviors.find(f.minerId);bi!=s.behaviors.end()&&bi->second.kind==RuntimeBehaviorKind::Attack)
            return fail(name+" attack survived Primary return");
        // Combat has been proved and Primary has revoked the explicit attack.
        // Give the enemy a normal retreat order so post-delivery Guard does not
        // acquire that same live enemy again and invalidate an idle-dock fixture.
        enemy=findMutableObjectById(s,f.tankId);const auto enemyStart=runtimeCellPointForObject(*enemy);
        const auto enemyOwner=enemy->owner;const RuntimeCellPoint retreat{44,44};
        if(!runtimeComputerAiIssueMove(s,*enemy,retreat,{f.tankId}))return fail(name+" normal enemy retreat rejected");
        const auto enemyMove=s.behaviors.find(f.tankId);
        if(enemyMove==s.behaviors.end()||enemyMove->second.kind!=RuntimeBehaviorKind::Move
            ||enemyMove->second.state!=RuntimeBehaviorState::Queued||enemyMove->second.moveTarget!=retreat)
            return fail(name+" accepted enemy Move was not queued");
        std::ofstream enemyOut(root+"/combat-retreat.csv");
        enemyOut<<"event,tick,id,owner,alive,health,cell_x,cell_y,render_x,render_y,active,behavior,target_x,target_y\n";
        const auto enemyRow=[&](const char* event){
            const auto* item=findObjectById(s,f.tankId);const auto bi=s.behaviors.find(f.tankId);
            if(!item)return;
            enemyOut<<event<<','<<s.tick<<','<<item->instanceId<<','<<quote(item->owner)<<','<<item->alive<<','<<item->health
                <<','<<item->x<<','<<item->y<<','<<std::setprecision(17)<<runtimeObjectRenderX(*item)<<','<<runtimeObjectRenderY(*item)
                <<','<<item->renderMoveActive<<','<<(bi==s.behaviors.end()?-1:int(bi->second.kind))
                <<','<<(bi==s.behaviors.end()?-1:bi->second.moveTarget.x)<<','<<(bi==s.behaviors.end()?-1:bi->second.moveTarget.y)<<'\n';
            enemyOut.flush();
        };
        enemyRow("normal-retreat-command");
        if(!deliver(f,name,false))return false;
        enemy=findMutableObjectById(s,f.tankId);enemyRow("miner-delivered-enemy-retreated");
        if(!enemy||!enemy->alive||enemy->owner!=enemyOwner||runtimeCellPointForObject(*enemy)==enemyStart
            ||runtimeCellDistance(runtimeCellPointForObject(*enemy),runtimeCellPointForObject(*findObjectById(s,f.minerId)))<=12.0
            ||!enemyOut)return fail(name+" live same-owner enemy did not actually retreat away from the dock");
        ++attackInterrupts;
    }
    {
        // A legitimate initial waiting cell, authored as QueueingCell=4,1,
        // lies outside NAREFN's 4x3 solid foundation and within the stock range.
        auto f=makeFixture("HARV",false,"",{68,65});auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const std::string name="busy-dock-retarget-other-refinery";const int originalTarget=f.targetId;
        auto* second=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"GAREFN",{75,74});
        if(!second)return fail(name+" stock second refinery");const int otherTarget=second->instanceId;
        second->owner=s.economy.owner;second->mission="Guard";second->buildAnimTicksRemaining=second->buildAnimTicksTotal=0;
        auto* busy=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"CMIN",{70,66});
        if(!busy)return fail(name+" stock occupying miner");const int busyId=busy->instanceId;
        busy->owner=s.economy.owner;busy->mission="Guard";busy->harvestAutoSearchSuspended=true;
        RuntimeHarvesterTask busyCargo;busyCargo.cargoUnits=20;busyCargo.cargoValue=500;
        normalizeRuntimeHarvesterTask(s,*busy,busyCargo);s.harvesterTasks[busyId]=busyCargo;
        std::ofstream dockOut(root+"/busy-dock-occupant.csv");
        dockOut<<"event,tick,id,phase,work,cargo,value,refinery_mission,dock_x,dock_y,cell_x,cell_y,credits,trips\n";
        const auto dockRow=[&](const char* event){
            const auto& t=s.harvesterTasks.at(busyId);const auto* o=findObjectById(s,busyId);
            dockOut<<event<<','<<s.tick<<','<<busyId<<','<<runtimeHarvesterPhaseName(t.phase)<<','<<t.workTicksRemaining
                <<','<<t.cargoUnits<<','<<t.cargoValue<<','<<quote(findObjectById(s,originalTarget)->mission)
                <<','<<t.dockCell.x<<','<<t.dockCell.y<<','<<(o?o->x:-1)<<','<<(o?o->y:-1)
                <<','<<s.economy.credits<<','<<s.economy.resourceTrips<<'\n';dockOut.flush();
        };
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);s.selectedObjectIds={busyId};
        const auto* refinery=findObjectById(s,originalTarget);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,foundation,refinery)
            ||s.lastCommand.acceptedCount!=1)return fail(name+" occupying miner normal return");
        bool occupied=false;
        for(int tick=0;tick<1200;++tick){
            step(f,name);const auto& t=s.harvesterTasks.at(busyId);
            if(t.phase==RuntimeHarvesterPhase::PreparingToUnload&&t.workTicksRemaining>=2&&t.cargoUnits==20&&t.cargoValue==500
                &&runtimeHarvesterIsAtDock(*findObjectById(s,busyId),t)
                &&findObjectById(s,originalTarget)->mission=="Unload"){occupied=true;break;}
        }
        dockRow("real-Preparing-occupancy");
        if(!occupied)return fail(name+" never reached actual Preparing/Unload dock");
        const auto* waiting=findObjectById(s,f.minerId);const auto* occupant=findObjectById(s,busyId);
        if(!waiting||!occupant||waiting->x<68||runtimeCellDistance({waiting->x,waiting->y},{occupant->x,occupant->y})<1.0
            ||runtimeObjectRenderX(*waiting)<67.5)return fail(name+" queue body overlaps original foundation or docked body");
        if(!primary(f,name+"-queue"))return false;
        bool queued=false;
        for(int tick=0;tick<8;++tick){
            step(f,name);if(s.harvesterTasks.at(f.minerId).phase==RuntimeHarvesterPhase::WaitingForRefineryDock){queued=true;break;}
        }
        dockRow("real-HARV-Waiting");
        const auto& occupiedTask=s.harvesterTasks.at(busyId);
        if(!queued||occupiedTask.phase!=RuntimeHarvesterPhase::PreparingToUnload||occupiedTask.workTicksRemaining<2
            ||occupiedTask.cargoUnits!=20||occupiedTask.cargoValue!=500
            ||findObjectById(s,originalTarget)->mission!="Unload")return fail(name+" real Waiting with live Preparing occupant unavailable");
        sample(f,name,"actual-busy-queue");
        const auto busyBefore=serializeRuntimeHarvesterTask(s.harvesterTasks.at(busyId));
        const auto refineryMission=findObjectById(s,originalTarget)->mission;
        const int credits=s.economy.credits,trips=s.economy.resourceTrips,storedUnits=s.economy.storedResourceUnits,storedValue=s.economy.storedResourceValue;
        const auto cargo=s.harvesterTasks.at(f.minerId);
        const int totalUnits=cargo.cargoUnits+s.harvesterTasks.at(busyId).cargoUnits;
        const int totalValue=cargo.cargoValue+s.harvesterTasks.at(busyId).cargoValue;
        f.targetId=otherTarget;f.refinery="GAREFN";
        dockRow("before-retarget");
        if(!primary(f,name+"-retarget"))return false;
        dockRow("after-retarget");
        if(serializeRuntimeHarvesterTask(s.harvesterTasks.at(busyId))!=busyBefore
            ||findObjectById(s,originalTarget)->mission!=refineryMission)return fail(name+" changed another miner's active dock/clock");
        bool done=false;int elapsed=0;
        for(;elapsed<3000;++elapsed){
            step(f,name);const auto& a=s.harvesterTasks.at(f.minerId);const auto& b=s.harvesterTasks.at(busyId);
            if(s.economy.credits-credits+a.cargoValue+b.cargoValue!=totalValue)return fail(name+" competing returns lost or duplicated cargo");
            if(!a.cargoUnits&&!b.cargoUnits&&!a.cargoValue&&!b.cargoValue){done=true;++elapsed;break;}
        }
        if(!done||s.economy.credits!=credits+totalValue||s.economy.resourceTrips!=trips+2
            ||s.economy.storedResourceUnits!=storedUnits+totalUnits||s.economy.storedResourceValue!=storedValue+totalValue
            ||s.harvesterTasks.at(f.minerId).targetRefineryId!=otherTarget||s.harvesterTasks.at(busyId).targetRefineryId!=originalTarget)
            return fail(name+" chosen dock delivery not exact");
        if(!caseRow(name,f,cargo.cargoUnits,cargo.cargoCapacity,cargo.cargoValue,credits,elapsed))return fail("busy CSV");
        dockRow("both-delivered");if(!dockOut)return fail("busy occupant CSV");
        ++busyControls;
    }


    for(bool blocked:{true,false}){
        auto f=makeFixture("HORV",false);auto& s=*f.storage;RuntimeSnapshotActiveStateScope active(s);
        const std::string name=blocked?"HORV-map-entry-no-safe-outside":"HORV-map-entry-safe-recovery";
        if(!naturalLoading(f,name))return false;
        const auto gathered=s.harvesterTasks.at(f.minerId);
        s.selectedObjectIds={f.minerId};issueRuntimeStopCommand(s);
        if(s.lastCommand.acceptedCount!=1||s.harvesterTasks.at(f.minerId).cargoUnits!=gathered.cargoUnits
            ||s.harvesterTasks.at(f.minerId).cargoValue!=gathered.cargoValue)return fail(name+" normal Stop did not retain natural cargo");
        const RuntimeCellPoint origin{59,67};auto* host=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"NATBNK",origin);
        if(!host)return fail(name+" original bunker unavailable");const int hostId=host->instanceId;
        host->owner=s.economy.owner;host->mission="Guard";host->buildAnimTicksRemaining=host->buildAnimTicksTotal=0;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);rebuildRuntimeEconomyPower(s);s.selectedObjectIds={f.minerId};
        if(!runtimeObjectCanEnterTankBunker(s,*findObjectById(s,f.minerId),*findObjectById(s,hostId),true)
            ||!queueRuntimeTargetAbility(s,*findObjectById(s,hostId)))return fail(name+" stock map/script entry command rejected");
        const auto insideCell=[&](const WorldObjectInstance& o){return o.x>=origin.x&&o.x<origin.x+2&&o.y>=origin.y&&o.y<origin.y+2;};
        const auto insideFoot=[&](const WorldObjectInstance& o){
            const auto offset=worldObjectSubCellOffset(o);const double x=runtimeObjectRenderX(o)+offset.x,y=runtimeObjectRenderY(o)+offset.y;
            return x>origin.x-.5+1e-9&&x<origin.x+1.5-1e-9&&y>origin.y-.5+1e-9&&y<origin.y+1.5-1e-9;
        };
        bool window=false;
        for(int tick=0;tick<2000;++tick){
            step(f,name);const auto* actor=findObjectById(s,f.minerId);const auto bi=s.behaviors.find(f.minerId);
            const auto pending=s.pendingTransportTargets.find(f.minerId);
            if(actor&&actor->tankBunkerHostId<0&&actor->dockedToObjectId<0&&actor->renderMoveActive
                &&insideCell(*actor)&&insideFoot(*actor)&&bi!=s.behaviors.end()&&bi->second.kind==RuntimeBehaviorKind::Move
                &&bi->second.pendingAllowedOccupiedObjectId==hostId&&bi->second.moveCooldownTicks>0
                &&bi->second.renderMoveTicksRemaining==1&&runtimeCellPointForObject(*actor)==bi->second.moveTarget
                &&pending!=s.pendingTransportTargets.end()&&pending->second==hostId){window=true;break;}
        }
        if(!window)return fail(name+" real authorized final entry segment unavailable");
        sample(f,name,"real-final-inside-entry");
        const auto cargo=s.harvesterTasks.at(f.minerId);
        if(blocked){
            std::vector<WorldObjectInstance> barriers;
            for(int y=44;y<=88;++y)for(int x=44;x<=88;++x){
                if((x>=origin.x&&x<origin.x+2&&y>=origin.y&&y<origin.y+2)
                    ||(x>=foundation.x&&x<foundation.x+4&&y>=foundation.y&&y<foundation.y+3))continue;
                bool occupied=false;
                for(const auto& o:s.world.getObjects())if(o.alive&&o.instanceId!=f.minerId&&o.x==x&&o.y==y){occupied=true;break;}
                if(occupied)continue;
                WorldObjectInstance barrier;barrier.instanceId=20000+y*512+x;barrier.kind=WorldObjectKind::Unit;
                barrier.type="MTNK";barrier.owner=s.economy.owner;barrier.health=400;barrier.alive=true;
                barrier.x=x;barrier.y=y;barrier.cell=runtimeCellKey(x,y);barrier.subCell=-1;
                barrier.mission="Guard";barrier.poweredOperational=true;barrier.weaponCooldownTicksRemaining=100000;
                barriers.push_back(barrier);
            }
            auto& objects=s.world.accessObjects();objects.insert(objects.end(),barriers.begin(),barriers.end());
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
        }
        const auto authority=[&](){
            const auto* o=findObjectById(s,f.minerId);const auto bi=s.behaviors.find(f.minerId);
            std::ostringstream key;key<<std::setprecision(17)<<s.world.deterministicSimulationStateHash()
                <<'|'<<serializeRuntimeHarvesterTask(s.harvesterTasks.at(f.minerId));
            if(o)key<<'|'<<o->x<<'|'<<o->y<<'|'<<o->cell<<'|'<<o->mission<<'|'<<o->hasRenderPosition<<'|'<<o->renderMoveActive
                <<'|'<<o->renderX<<'|'<<o->renderY<<'|'<<o->renderLevel<<'|'<<o->renderOnBridge<<'|'<<o->harvestAutoSearchSuspended
                <<'|'<<o->tankBunkerHostId<<'|'<<o->dockedToObjectId;
            if(bi!=s.behaviors.end()){
                const auto& b=bi->second;
                key<<'|'<<runtimeMultiplayerSnapshotBehaviorRecord(f.minerId,b)<<'|'<<b.moveStartupDistanceQ16
                    <<'|'<<b.renderStartupAccelerationDistance<<'|'<<b.renderFrom.x<<'|'<<b.renderFrom.y
                    <<'|'<<b.renderTo.x<<'|'<<b.renderTo.y<<'|'<<b.renderFromOffsetX<<'|'<<b.renderFromOffsetY
                    <<'|'<<b.renderToOffsetX<<'|'<<b.renderToOffsetY<<'|'<<b.renderFromLevel<<'|'<<b.renderToLevel
                    <<'|'<<b.renderFromOnBridge<<'|'<<b.renderToOnBridge<<'|'<<b.renderMoveTicksTotal
                    <<'|'<<b.renderMoveTicksRemaining<<'|'<<b.renderArrivalDistanceAfter;
            }else key<<"|missing-behavior";
            const auto pending=s.pendingTransportTargets.find(f.minerId);key<<'|'<<(pending==s.pendingTransportTargets.end()?-1:pending->second);
            key<<'|'<<s.economy.credits<<'|'<<s.economy.resourceTrips<<'|'<<s.economy.storedResourceUnits<<'|'<<s.economy.storedResourceValue;
            return key.str();
        };
        const auto old=authority();const int credits=s.economy.credits;
        s.selectedObjectIds={f.minerId};const auto* refinery=findObjectById(s,f.targetId);
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*refinery),refinery);
        sample(f,name,"entry-return-command");
        if(!s.lastCommand.valid||s.lastCommand.verb!="harvesterUnload"||s.lastCommand.targetObjectId!=f.targetId
            ||dispatched!=!blocked)return fail(name+" Primary acceptance/rejection return contract");
        if(blocked){
            if(s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||authority()!=old)
                return fail(name+" rejected cancellation changed original inside leg/task/permission/body");
            if(!caseRow(name,f,cargo.cargoUnits,cargo.cargoCapacity,cargo.cargoValue,credits,0))return fail("entry reject CSV");
        }else{
            const auto* actor=findObjectById(s,f.minerId);const auto bi=s.behaviors.find(f.minerId);
            if(s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||!actor||insideCell(*actor)||insideFoot(*actor)
                ||actor->renderMoveActive||s.pendingTransportTargets.contains(f.minerId)
                ||(bi!=s.behaviors.end()&&(bi->second.pendingAllowedOccupiedObjectId==hostId||bi->second.renderMoveTicksRemaining>0))
                ||s.harvesterTasks.at(f.minerId).cargoUnits!=cargo.cargoUnits||s.harvesterTasks.at(f.minerId).cargoValue!=cargo.cargoValue)
                return fail(name+" restored/reused the cancelled internal segment or consumed cargo");
            if(!deliver(f,name,false))return false;
        }
        ++entryControls;
    }

    for (const std::string name : {"no-selection", "ordinary-tank", "foreign-refinery", "force-attack"}) {
        auto f = makeFixture("HARV", false); auto& s = *f.storage;
        if (f.minerId < 0 || f.targetId < 0 || f.tankId < 0) return fail("negative fixture");
        RuntimeSnapshotActiveStateScope active(s);
        if (name == "no-selection") s.selectedObjectIds.clear();
        if (name == "ordinary-tank") s.selectedObjectIds = {f.tankId};
        if (name == "foreign-refinery") findMutableObjectById(s, f.targetId)->owner = "Americans";
        if (name == "force-attack") s.forceAttackHeld = true;
        POINT point{}; if (!targetPoint(f, point)) return fail(name + " target hit");
        s.mouseClientPoint = point; const auto cursor = runtimeMouseCursorFrameForPoint(s, client, point);
        if (!cursor || (cursor->frameIndex >= 89 && cursor->frameIndex <= 98)
            || (name == "force-attack" && (cursor->frameIndex < 53 || cursor->frameIndex > 57))) return fail(name + " cursor priority/exclusion");
        const auto taskBefore = s.harvesterTasks.at(f.minerId); const int beforeCredits = s.economy.credits;
        const auto order = name == "force-attack" ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
            : RuntimeMultiplayerGameplayTargetOrderKind::Primary;
        const bool accepted = dispatchRuntimeTargetOrderFromUi(s, order, foundation, findObjectById(s, f.targetId));
        (void)accepted; const auto& taskAfter = s.harvesterTasks.at(f.minerId);
        if (s.lastCommand.verb == "harvesterUnload" || taskAfter.phase == RuntimeHarvesterPhase::ReturningToRefinery
            || taskAfter.targetRefineryId != taskBefore.targetRefineryId || taskAfter.cargoUnits != taskBefore.cargoUnits
            || taskAfter.cargoValue != taskBefore.cargoValue || s.economy.credits != beforeCredits)
            return fail(name + " incorrectly unloaded/submitted a return");
        if (!caseRow(name, f, taskBefore.cargoUnits, runtimeHarvesterCargoCapacity(s, *findObjectById(s, f.minerId)),
                taskBefore.cargoValue, beforeCredits, 0)) return fail("negative CSV");
        ++exclusions;
    }
    auto repair = makeFixture("HARV", true);
    if (repair.minerId < 0 || repair.targetId < 0 || repair.tankId < 0) return fail("repair fixture missing");
    if (!checkFrames(repair, "tank-repair-original-enter", false)) return false;
    if (!caseRow("tank-repair-original-enter", repair, 2, repair.storage->harvesterTasks.at(repair.minerId).cargoCapacity,
            50, repair.storage->economy.credits, 0)) return fail("repair CSV");

    if (caseCount != 24 || unloads != 2 || exclusions != 4 || mixed != 1 || minerFrames != 20
        || repairFrames != 10 || bmpCount != 6 || totalOpaque <= 0 || pixelMismatches
        ||emptyReturns!=4||crossPairs!=2||moveInterrupts!=2||loadingInterrupts!=2||attackInterrupts!=1
        ||busyControls!=1||entryControls!=2||clockCheckpoints!=6||restoreControls!=2)
        return fail("finite terminal counts");
    casesOut.flush();framesOut.flush();statesOut.flush();sourcesOut.flush();
    if(!casesOut||!framesOut||!statesOut||!sourcesOut)return fail("CSV flush");
    std::ofstream summary(root+"/summary.json");
    summary<<"{\"issue\":25,\"cases\":"<<caseCount<<",\"productionMinerTypes\":2,\"mapScriptAlias\":\"HORV\",\"tickHz\":30"
        <<",\"baselinePartialUnloads\":"<<unloads<<",\"emptyReturns\":"<<emptyReturns<<",\"crossDockPairs\":"<<crossPairs
        <<",\"moveInterrupts\":"<<moveInterrupts<<",\"naturalLoadingInterrupts\":"<<loadingInterrupts
        <<",\"realCombatInterrupts\":"<<attackInterrupts<<",\"busyRetargets\":"<<busyControls<<",\"entryRecoveryControls\":"<<entryControls
        <<",\"sameDockClockCheckpoints\":"<<clockCheckpoints<<",\"unloadingRestoreInputs\":"<<restoreControls
        <<",\"exclusions\":"<<exclusions<<",\"mixedSelections\":"<<mixed<<",\"minerFrames\":"<<minerFrames<<",\"repairFrames\":"<<repairFrames
        <<",\"pixelMismatches\":"<<pixelMismatches<<",\"opaquePixels\":"<<totalOpaque<<",\"bmp\":"<<bmpCount
        <<",\"saves\":"<<restoreControls<<",\"desktopInteraction\":false,\"passed\":true}\n";
    summary.flush();if(!summary)return fail("summary flush");
    std::cout<<"Issue25 manual unload: cases=24 types=2 mapScriptAlias=HORV partial=2 empty=4 crossDock=2"
        <<" Move=2 naturalLoading=2 realAttack=1 busyRetarget=1 entryRecovery=2 dockClock=6 restoreUnloading=2"
        <<" exclusions=4 mixed=1 minerFrames=20 repairFrames=10 pixelMismatches=0 BMP=6 PASS\n";
    return true;
}
