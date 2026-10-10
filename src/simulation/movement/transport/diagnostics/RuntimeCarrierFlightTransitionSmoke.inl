// Issue 22/21: actual map mouse orders, native carrier/Hornet assets and the
// first complete sortie. Original Ammo=1 and an undeclared Burst (effective1)
// remain intact; three shots are sortie policy, not an invented raw Burst.
// Ordinary14 is a world save. The canonical branch is its contextual extension.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeCarrierFlightTransitionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_CARRIER_FLIGHT_TRANSITION_OUTPUT");
    const std::filesystem::path output = configured && *configured
        ? configured : "build/issue22_21_carrier_flight_transition";
    const char* requested = std::getenv("CONCORD_CARRIER_FLIGHT_TRANSITION_CASE");
    const std::string filter = requested ? requested : "";
    std::filesystem::create_directories(output / "native-original");
    std::string label, stage;
    const auto fail = [&](const std::string& reason) {
        error = "Carrier flight transition " + label + " [" + stage + "]: " + reason; return false;
    };
    std::ofstream cases(output / "cases.csv"), movement(output / "movement.csv"),
        shots(output / "shots.csv"), commands(output / "commands.csv"),
        checkpoints(output / "checkpoints.csv"), pixels(output / "pixels.csv"), assetsCsv(output / "assets.csv");
    if (!cases || !movement || !shots || !commands || !checkpoints || !pixels || !assetsCsv) return fail("output streams");
    cases << "case,hz,facing,parent_moving,planes,launches,distinct_launch_ticks,shot_actors,shot_planes,damage,returned,takeoff_fractional_planes,landing_fractional_planes,continuous,port_launches,parent_distance,ordinary14,canonical_extension,paired_ticks,captures,pixel_checks,ticks,result,reason_hex,oblique_target,straight_samples,max_cross_error,max_cruise_error,nav_rebuild_checks,cruise_samples,turn_samples,max_turn,max_outbound_separated\n";
    movement << std::setprecision(17) << "case,branch,stage,tick,hz,object,type,owner,hp,alive,logical_x,logical_y,logical_z,physical_x,physical_y,physical_z,render_x,render_y,render_z,has_render,moving,facing,airborne,flight_level,docked_to,dock_index,current_ammo,dock_progress,phase,target,action_tick,ammo_remaining,carrier_record_hex,behavior,behavior_state,weapon_cooldown,move_cooldown,path_pending,path_nodes,render_ticks,parent_x,parent_y,parent_z,target_hp\n";
    shots << std::setprecision(17) << "case,tick,actor,plane,slot,ordinal,spawn_tick,weapon,projectile,warhead,damage,raw_damage,raw_rof,effective_burst,raw_ammo,from_x,from_y,from_z,target_x,target_y,target_z,plane_x,plane_y,plane_z,parent_x,parent_y,parent_z,phase,current_ammo,carrier_record_hex\n";
    commands << "case,branch,stage,tick,pixel_x,pixel_y,picked_id,command_type,order,target_id,target_x,target_y,accepted,accepted_count,reason_hex\n";
    checkpoints << "case,stage,format,tick,hz,carrier_hash,particle_hash,parent_x,parent_y,parent_z,shot_actors,flight_pose_count,carrier_children,file,result\n";
    pixels << std::setprecision(17) << "case,stage,tick,object,phase,world_x,world_y,world_z,progress,center_x,center_y,left,top,right,bottom,rendered_pixels,layers,model_sections,cold_warm_equal,pan_equal,file\n";
    assetsCsv << "kind,id,field,value,source,fnv,bytes,sections,frames\n";

    auto loaded = std::make_unique<RuntimeWindowState>(); auto& native = *loaded;
    native.config = config; native.config.playerSlots.clear(); native.config.computerAiEnabled = false;
    native.config.gameModeRulesOverride.clear();
    native.report = buildRuntimeStartupReport(native.config, &native.world, &native.theater,
        &native.art, &native.rules, &native.mapFacts);
    IniFile raw, rawArt;
    if (!native.report.success || !raw.loadFromFile(native.report.rulesPath) || !native.rules.loadFromIni(raw)
        || !rawArt.loadFromFile(native.report.artPath)) return fail("original startup/rules/art: " + native.report.errorMessage);
    native.rulesLoaded = native.artLoaded = native.theaterLoaded = true;
    const auto authored = [](const std::string& value) { return trimAsciiLocal(value.substr(0, value.find(';'))); };
    const auto number = [&](const std::string& section, const std::string& key) {
        return std::stod(authored(raw.getValue(section, key, "-999")));
    };
    const auto* carrier = native.rules.findUnitType("vehicle", "CARRIER");
    const auto* hornet = native.rules.findUnitType("aircraft", "HORNET");
    const auto* targetType = native.rules.findBuildingType("NAWEAP");
    const auto* bomb = native.rules.findWeaponType("HornetBomb");
    const auto* bombProjectile = native.rules.findProjectileType("NormalBomb");
    const auto* launcher = native.rules.findWeaponType("HornetLauncher");
    const auto verses = splitRuntimeTokenList(authored(raw.getValue("ORCAAP", "Verses", "")));
    if (!carrier || !hornet || !targetType || !bomb || !bombProjectile || !launcher
        || carrier->strength != 800 || carrier->spawns != "HORNET" || carrier->spawnsNumber != 3
        || hornet->strength != 75 || number("HORNET", "Ammo") != 1 || number("HORNET", "Speed") != 12 || number("HORNET", "ROT") != 3
        || number("CARRIER", "SpawnReloadRate") != 150 || number("CARRIER", "SpawnRegenRate") != 600
        || bomb->damage != 40 || bomb->rof != 3 || bomb->range != 5 || std::max(1, bomb->burst) != 1
        || bomb->projectile != "NormalBomb" || bomb->warhead != "ORCAAP"
        || targetType->strength != 1000 || authored(raw.getValue("NAWEAP", "Armor", "")) != "wood"
        || verses.size() != 11 || verses[6] != "100%" || !launcher->spawner
        || authored(raw.getValue("NormalBomb", "Image", "")) != "DRAGON"
        || authored(rawArt.getValue("HORNET", "PrimaryFireFLH", "")) != "0,32,0"
        || !authored(rawArt.getValue("CARRIER", "PrimaryFireFLH", "")).empty())
        return fail("stock 800/75 HP, three aircraft, Ammo1/Burst1, HornetBomb40/ROF3/wood100% differ");
    const std::uint64_t rulesHash = native.rules.getDetailRulesHash();
    for (const auto& value : std::array<std::array<const char*, 3>, 12>{{
        {{"CARRIER","Strength","800"}}, {{"CARRIER","SpawnsNumber","3"}},
        {{"CARRIER","SpawnReloadRate","150"}}, {{"CARRIER","SpawnRegenRate","600"}},
        {{"HORNET","Strength","75"}}, {{"HORNET","Ammo","1"}}, {{"HORNET","Speed","12"}},
        {{"HornetBomb","Damage","40"}}, {{"HornetBomb","ROF","3"}}, {{"HornetBomb","Burst","default1"}},
        {{"HornetBomb","Projectile","NormalBomb"}}, {{"HornetBomb","Warhead","ORCAAP"}}
    }}) assetsCsv << "rule," << value[0] << ',' << value[1] << ',' << value[2] << ',' << native.report.rulesPath << ",0,0,0,0\n";
    const auto u32 = [](const std::vector<std::uint8_t>& data, std::size_t at) {
        return std::uint32_t(data[at]) | (std::uint32_t(data[at+1]) << 8U)
            | (std::uint32_t(data[at+2]) << 16U) | (std::uint32_t(data[at+3]) << 24U);
    };
    const auto fnv = [](const auto& data) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (auto value : data) { hash ^= static_cast<std::uint8_t>(value); hash *= 1099511628211ULL; }
        return hash;
    };
    struct RawModel { std::vector<std::uint8_t> vxl, hva; int sections = 0, frames = 0; };
    std::map<std::string, RawModel> models;
    const auto resource = [&](const std::string& filename, std::vector<std::uint8_t>& data, std::string& source) {
        for (const char* directory : {"ra2md", "ra2"}) {
            const auto folder = std::filesystem::path(runtimeAssetRootPath(native.config)) / directory;
            if (!std::filesystem::is_directory(folder)) continue;
            std::vector<std::filesystem::path> archives;
            for (const auto& file : std::filesystem::directory_iterator(folder))
                if (file.is_regular_file() && toLowerAsciiLocal(file.path().extension().string()) == ".mix") archives.push_back(file.path());
            std::sort(archives.begin(), archives.end());
            for (const auto& path : archives) {
                MixArchive archive; if (!archive.loadFromPath(path)) continue;
                const auto* entry = archive.findEntryByName(filename); if (!entry) continue;
                if (!archive.readEntryData(*entry, data, error)) return false;
                source = pathString(path) + '!' + filename; return true;
            }
        }
        return false;
    };
    for (const std::string image : {"carrier", "hornet"}) {
        auto& model = models[image];
        for (const std::string extension : {".vxl", ".hva"}) {
            auto& data = extension == ".vxl" ? model.vxl : model.hva; std::string source;
            if (!resource(image + extension, data, source)) return fail("original MIX model missing: " + image + extension);
            std::ofstream file(output / "native-original" / (image + extension), std::ios::binary);
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!file) return fail("original model evidence write");
            if (extension == ".vxl") {
                if (data.size() < 802) return fail("raw VXL header");
                const std::uint32_t count = u32(data, 20);
                const std::size_t footer = 802 + std::size_t(count) * 28 + u32(data, 28);
                if (!count || count > 1024 || footer + std::size_t(count) * 92 > data.size()) return fail("raw complete VXL sections");
                model.sections = static_cast<int>(count);
                for (std::uint32_t section = 0; section < count; ++section)
                    for (int axis = 0; axis < 3; ++axis) if (!data[footer + section * 92 + 88 + axis]) return fail("empty raw VXL section");
            } else {
                if (data.size() < 24) return fail("raw HVA header");
                const std::uint32_t frames = u32(data, 16), count = u32(data, 20);
                if (!frames || !count || frames > 10000 || count > 1024
                    || 24 + std::size_t(count) * 16 + std::size_t(frames) * count * 48 > data.size()) return fail("raw complete HVA frames");
                model.frames = static_cast<int>(frames);
                if (count != static_cast<std::uint32_t>(model.sections)) return fail("original VXL/HVA part count mismatch");
            }
            assetsCsv << "model," << image << ',' << extension << ",original," << source << ',' << fnv(data) << ',' << data.size()
                << ',' << model.sections << ',' << model.frames << '\n';
        }
    }
    std::vector<std::uint8_t> dragon; std::string dragonSource;
    if (!resource("dragon.shp", dragon, dragonSource) || dragon.size() < 8) return fail("original HornetBomb DRAGON SHP");
    const int dragonFrames = dragon[6] + (int(dragon[7]) << 8);
    if (dragonFrames <= 0) return fail("original bomb frame header");
    std::ofstream dragonFile(output / "native-original" / "dragon.shp", std::ios::binary);
    dragonFile.write(reinterpret_cast<const char*>(dragon.data()), static_cast<std::streamsize>(dragon.size()));
    if (!dragonFile) return fail("original projectile evidence write");
    assetsCsv << "projectile,DRAGON,frames," << dragonFrames << ',' << dragonSource << ',' << fnv(dragon) << ',' << dragon.size() << ",0," << dragonFrames << '\n';

    int landTile = -1, waterTile = -1;
    const int waterSet = native.theater.getGeneralTileSetIndex("WaterSet");
    for (const auto& set : native.theater.getTileSets())
        if (set.index == waterSet && set.tilesInSet > 0) waterTile = set.startTileIndex;
    for (int tile = 0; tile < static_cast<int>(native.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(native, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0 || waterTile < 0) return fail("original Clear/Water TMP");
    assetsCsv << "terrain,Clear,tile," << landTile << ",original-theater,0,0,0,0\n"
        << "terrain,Water,tile," << waterTile << ",original-theater,0,0,0,0\n";
    const auto add = [](RuntimeWindowState& s, int id, const std::string& type, WorldObjectKind kind,
        RuntimeCellPoint cell, const std::string& owner, int facing) {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "CarrierFlight" + std::to_string(id);
        object.type = type; object.kind = kind; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.facing = object.turretFacing = facing; object.mission = "Sleep";
        object.health = runtimeMaxHealthForObject(s, object);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](int hz, int facing, RuntimeCellPoint targetCell) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, native, false); s.config = native.config; s.tick = 200;
        s.config.fixedTickHz = hz; s.config.playerCountry = "Americans"; s.config.gameMode = "Battle";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.crates = s.config.shortGame = false;
        s.config.superweapons = false; s.config.startingUnits = 0; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear(); s.campaignMode = false;
        s.rules = native.rules; s.art = native.art; s.theater = native.theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = native.mapFacts.theater; s.mapFacts.lighting = native.mapFacts.lighting;
        s.mapFactsLoaded = false; s.report.map.size.clear(); s.report.map.localSize.clear();
        s.report.map.width = s.report.map.height = -1;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 100; ++y) for (int x = 55; x <= 105; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = x >= 87 ? landTile : waterTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.computerAiByOwner.clear(); s.computerMiningByOwner.clear(); s.humanHouseAuthorities.clear();
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = "Americans"; local.color = "Gold";
        RuntimeConfig::PlayerSlot enemy; enemy.enabled = enemy.human = true; enemy.local = false;
        enemy.name = "Quiet hostile"; enemy.country = "Russians"; enemy.color = "Red";
        s.config.playerSlots = {local, enemy};
        s.skirmishCountryByOwner = {{"commander", "Americans"}, {"skirmishplayer2", "Russians"}};
        s.skirmishTeamByOwner.clear(); s.skirmishDifficultyByOwner.clear(); s.playerCommands.alliances.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = "Commander"; authority.canonicalOwner = "commander";
        authority.country = "Americans"; authority.localFacade = true; s.humanHouseAuthorities.push_back(std::move(authority));
        add(s, 1, "CARRIER", WorldObjectKind::Unit, {75,75}, "Commander", facing);
        add(s, 2, "NAWEAP", WorldObjectKind::Structure, targetCell, "SkirmishPlayer2", 0);
        add(s, 998, "NAPOWR", WorldObjectKind::Structure, {100,96}, "SkirmishPlayer2", 0);
        ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s); loadRuntimeSoundDatabase(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 10000; s.economy.techLevel = 10; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return box;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto waterAt = [](const RuntimeWindowState& s, int x, int y) {
        const auto cell = s.terrainByCell.find(runtimeCellKey(x, y));
        return cell != s.terrainByCell.end() && cell->second && runtimeTerrainInfoForCell(s, *cell->second).water;
    };
    const auto carrierRows = [](const RuntimeWindowState& s) {
        std::ostringstream text; s.specialMechanisms.save(text); IniFile ini; ini.loadFromString(text.str());
        std::map<int, std::string> result;
        const int count = std::stoi(ini.getValue("SpecialCarrierChildren", "Count", "0"));
        for (int index = 0; index < count; ++index) {
            const auto row = ini.getValue("SpecialCarrierChildren", std::to_string(index), "");
            const auto fields = splitRuntimeSaveFields(row);
            if (!fields.empty()) result.emplace(std::stoi(fields.front()), row);
        }
        return result;
    };
    const auto bodyRecords = [](const RuntimeWindowState& s) {
        std::map<int, std::string> result;
        for (const auto& o : s.world.getObjects()) {
            std::ostringstream text; text << std::setprecision(17) << o.type << '|' << o.owner << '|' << int(o.kind)
                << '|' << o.health << '|' << o.alive << '|' << o.x << '|' << o.y << '|' << o.level << '|' << o.cell
                << '|' << runtimeObjectRenderX(o) << '|' << runtimeObjectRenderY(o) << '|' << runtimeObjectRenderLevel(o)
                << '|' << o.renderMoveActive << '|' << o.facing << '|' << o.airborne << '|' << o.flightLevel
                << '|' << o.dockedToObjectId << '|' << o.dockIndex << '|' << o.currentAmmo << '|' << o.aircraftDockFlightProgress
                << '|' << runtimeSerializeFacingClock(o.bodyFacingClock);
            result.emplace(o.instanceId, text.str());
        }
        return result;
    };
    const auto projectileRecords = [](const RuntimeWindowState& s) {
        std::map<int, std::string> result;
        for (const auto& p : s.projectiles) result.emplace(p.actorId, serializeRuntimeProjectileEffect(p));
        return result;
    };
    const auto contextFor = [](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c; c.theater = &s.theater; c.art = &s.art; c.rules = &s.rules;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.tileRootPath = runtimeTileRootPath(s.config, s.report.map);
        c.palettePath = runtimePalettePath(s.config, s.report.map); c.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map); c.animationTick = s.tick;
        c.fixedTickHz = s.config.fixedTickHz; c.specialMechanismSnapshots = &s.specialMechanismRenderSnapshots;
        c.ownerRemapColors = s.ownerRemapColors.empty() ? nullptr : &s.ownerRemapColors; return c;
    };

    const RECT client{0,0,1024,768}, viewport = runtimeMapViewport(client);
    const HINSTANCE instance = GetModuleHandleW(nullptr); const wchar_t* className = L"ConcordCarrierFlightTransitionSmoke";
    WNDCLASSW wc{}; wc.lpfnWndProc = runtimeWindowProc; wc.hInstance = instance; wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail("real input window class");
    struct ClassCleanup { HINSTANCE instance; const wchar_t* name; ~ClassCleanup() { UnregisterClassW(name, instance); } } classCleanup{instance,className};
    HWND window = CreateWindowExW(0,className,L"Carrier flight transition",WS_POPUP,0,0,1024,768,nullptr,nullptr,instance,nullptr);
    if (!window) return fail("real input window");
    struct WindowCleanup { HWND window; ~WindowCleanup() {
        SetWindowLongPtrW(window,GWLP_USERDATA,0); SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if (GetCapture() == window) ReleaseCapture(); DestroyWindow(window);
    } } windowCleanup{window};
    const auto mouse = [&](RuntimeWindowState& s, POINT point) {
        RuntimeSnapshotActiveStateScope active(s); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
    };
    const auto pointFor = [&](RuntimeWindowState& s, int id, POINT& point) {
        const auto* o = findObjectById(s,id); if (!o) return fail("native click object absent");
        centerRuntimeCameraOnCell(s,client,{o->x,o->y}); ensureRuntimeCameraForViewport(s,viewport);
        const auto& projected = runtimeProjectedObjectsForViewport(s,viewport);
        for (const auto& item : projected) if (item.object && item.object->instanceId == id) {
            const RECT b = item.spriteBounds; const POINT middle{(b.left+b.right)/2,(b.top+b.bottom)/2};
            const int radius = std::max(b.right-b.left,b.bottom-b.top);
            for (int d=0; d<=radius; ++d) for (int dy=-d; dy<=d; ++dy) for (int dx=-d; dx<=d; ++dx) {
                if (std::max(std::abs(dx),std::abs(dy)) != d) continue;
                const POINT p{middle.x+dx,middle.y+dy};
                if (!PtInRect(&viewport,p)) continue;
                const auto* picked = findProjectedObjectAtPoint(projected,p);
                if (picked && picked->object && picked->object->instanceId == id) { point=p; return true; }
            }
        }
        return fail("actual stock sprite has no selectable pixel for #"+std::to_string(id));
    };
    const auto select = [&](RuntimeWindowState& s) {
        if (s.selectedObjectIds == std::vector<int>{1}) return true;
        s.selectedObjectIds.clear(); POINT point{}; if (!pointFor(s,1,point)) return false;
        mouse(s,point); return s.selectedObjectIds == std::vector<int>{1} ? true : fail("actual carrier selection");
    };
    int passed=0,selectedCases=0,totalOrdinary=0,totalCanonical=0,totalPaired=0,totalCaptures=0,totalPixels=0,totalShots=0;
    int totalStraightSamples=0,totalNavRebuilds=0,totalTurnSamples=0;
    std::vector<std::string> failures;
    const std::array<RuntimeCellPoint,4> directions{{{1,0},{0,1},{-1,0},{0,-1}}};
    for (bool oblique : {false,true})
    for (int hz : {30,60}) for (int direction=0; direction<4; ++direction) for (bool moving : {false,true}) {
        label=std::string(moving ? "moving" : "static")+"-d"+std::to_string(direction)+'-'+std::to_string(hz)
            +(oblique?"-diag-15-7":"");
        if (!filter.empty() && filter!=label) continue;
        ++selectedCases;
        stage="original water fixture"; const auto delta=directions[direction];
        const RuntimeCellPoint targetCell{90,oblique?82:75};
        auto box=make(hz,runtimeFacingFromDelta(delta.x,delta.y),targetCell); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        std::set<int> ids; std::map<int,int> slots;
        const auto parentState=s.specialMechanisms.carrierParents().find(1);
        if (parentState==s.specialMechanisms.carrierParents().end()) return fail("native parent registration");
        for (const auto& [slot,id] : parentState->second.childBySlot) { ids.insert(id); slots[id]=slot; }
        std::map<int,int> launched,landed,shotCount,previousShot;
        std::map<int,std::set<double>> riseLevels,fallLevels;
        std::set<int> shotIds,portIds; bool continuous=true,stopped=false,parentMoved=false;
        int caseOrdinary=0,caseCanonical=0,casePaired=0,caseCaptures=0,casePixels=0,naturalTicks=0;
        int straightSamples=0,caseNavRebuilds=0,cruiseSamples=0,turnSamples=0,maxTurn=0,maxOutboundSeparated=0;std::map<int,int> straightByChild;
        double maxCrossError=0,maxCruiseError=0;bool navRebuilt=false;
        // Independent retail Speed12 is 30 leptons per 15 Hz rule frame.
        // Do not use the production interval/flight helper as this oracle.
        const double originalCruiseStep=30.0*15.0/(256.0*hz);
        double parentDistance=0; int capturedCommands=0,acceptedCommands=0;
        POINT lastPoint{-1,-1}; int lastPicked=-1; std::string commandStage="attack",commandBranch="original";
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string& reason) {
            ++capturedCommands; const auto* order=std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&command.body);
            int targetId=-1,targetX=-1,targetY=-1;
            if (order) {
                if (const auto* target=std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&order->target)) targetId=target->objectId;
                if (const auto* target=std::get_if<RuntimeMultiplayerGameplayCellTarget>(&order->target)) { targetX=target->cell.x; targetY=target->cell.y; }
            }
            const bool ok=dispatchRuntimeOfflineGameplayCommand(s,command,reason); if(ok) ++acceptedCommands;
            commands<<label<<','<<commandBranch<<','<<commandStage<<','<<s.tick<<','<<lastPoint.x<<','<<lastPoint.y<<','<<lastPicked
                <<','<<int(runtimeMultiplayerGameplayCommandType(command))<<','<<(order?int(order->order):-1)<<','<<targetId<<','<<targetX<<','<<targetY<<','<<ok
                <<','<<s.lastCommand.acceptedCount<<','<<iniHexEncode(reason)<<'\n'; return ok;
        });
        std::array<std::unique_ptr<RuntimeWindowState>,2> replicas;
        const auto sample = [&](const RuntimeWindowState& q,const char* branch,const std::string& recordStage) {
            const auto rows=carrierRows(q); const auto* parent=findObjectById(q,1); const auto* victim=findObjectById(q,2);
            if (!parent || !victim || q.rules.getDetailRulesHash()!=rulesHash || q.config.noCooldown || q.skirmishMatchFinished)
                return fail("original parent/target/rules or unfinished battle authority");
            for (const auto& o : q.world.getObjects()) {
                const auto child=q.specialMechanisms.carrierChildren().find(o.instanceId);
                const auto b=q.behaviors.find(o.instanceId); const auto row=rows.find(o.instanceId);
                movement<<label<<','<<branch<<','<<recordStage<<','<<q.tick<<','<<hz<<','<<o.instanceId<<','<<o.type<<','<<o.owner
                    <<','<<o.health<<','<<o.alive<<','<<o.x<<','<<o.y<<','<<o.level<<','<<runtimeObjectRenderX(o)<<','<<runtimeObjectRenderY(o)
                    <<','<<runtimeObjectRenderLevel(o)<<','<<o.renderX<<','<<o.renderY<<','<<o.renderLevel<<','<<o.hasRenderPosition
                    <<','<<o.renderMoveActive<<','<<o.facing<<','<<o.airborne<<','<<o.flightLevel<<','<<o.dockedToObjectId<<','<<o.dockIndex
                    <<','<<o.currentAmmo<<','<<o.aircraftDockFlightProgress<<','<<(child==q.specialMechanisms.carrierChildren().end()?-1:int(child->second.phase))
                    <<','<<(child==q.specialMechanisms.carrierChildren().end()?-1:child->second.targetObjectId)
                    <<','<<(child==q.specialMechanisms.carrierChildren().end()?-1:child->second.actionTick)
                    <<','<<(child==q.specialMechanisms.carrierChildren().end()?-1:child->second.ammoRemaining)
                    <<','<<(row==rows.end()?"":iniHexEncode(row->second))<<','<<(b==q.behaviors.end()?-1:int(b->second.kind))
                    <<','<<(b==q.behaviors.end()?-1:int(b->second.state))<<','<<(b==q.behaviors.end()?0:b->second.weaponCooldownTicks)
                    <<','<<(b==q.behaviors.end()?0:b->second.moveCooldownTicks)<<','<<(b!=q.behaviors.end()&&b->second.pathRequestPending)
                    <<','<<(b==q.behaviors.end()?0:b->second.path.size())<<','<<(b==q.behaviors.end()?0:b->second.renderMoveTicksRemaining)
                    <<','<<runtimeObjectRenderX(*parent)<<','<<runtimeObjectRenderY(*parent)<<','<<runtimeObjectRenderLevel(*parent)<<','<<victim->health<<'\n';
            }
            if (parent->health!=800 || !parent->alive || !waterAt(q,parent->x,parent->y)) return fail("stock live carrier left water/HP");
            for(int id:ids) {
                const auto* child=findObjectById(q,id);
                if(!child || !child->alive || child->type!="HORNET" || child->owner!="Commander" || child->health!=75
                    || child->currentAmmo<0 || child->currentAmmo>1) return fail("original Hornet identity/HP/Ammo1 changed");
            }
            return true;
        };
        const auto same = [&](const RuntimeWindowState& q,bool exact) {
            if(q.tick!=s.tick || bodyRecords(q)!=bodyRecords(s) || carrierRows(q)!=carrierRows(s)
                || q.specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
                || q.particles.stateHash()!=s.particles.stateHash() || projectileRecords(q)!=projectileRecords(s)) return false;
            for(int id:ids) {
                const auto a=s.behaviors.find(id); const auto b=q.behaviors.find(id);
                if((a==s.behaviors.end())!=(b==q.behaviors.end()))return false;
                if(a!=s.behaviors.end() && runtimeMultiplayerSnapshotBehaviorRecord(id,a->second)!=runtimeMultiplayerSnapshotBehaviorRecord(id,b->second))return false;
            }
            // The canonical branch also carries transient presentation. An
            // ordinary14 save deliberately has no claim to these animations.
            if(exact) {
                if(q.impactEffects.size()!=s.impactEffects.size())return false;
                for(std::size_t i=0;i<s.impactEffects.size();++i)
                    if(runtimeMultiplayerSnapshotImpactRecord(q.impactEffects[i])!=runtimeMultiplayerSnapshotImpactRecord(s.impactEffects[i]))return false;
            }
            return true;
        };
        const auto save = [&](const std::string& saveStage) {
            std::ostringstream ordinaryText,extendedText,canonicalText; IniFile ordinaryIni,canonicalIni;
            if(!writeRuntimeStateToStream(s,ordinaryText,error)||!ordinaryIni.loadFromString(ordinaryText.str())
                ||ordinaryIni.getValue("RuntimeSave","Version","")!="14")return fail("ordinary14 checkpoint: "+error);
            extendedText<<ordinaryText.str();
            if(!writeRuntimeMultiplayerSnapshotExtension(s,extendedText,error)||!canonicalIni.loadFromString(extendedText.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(canonicalIni,canonicalText,error)||!canonicalIni.loadFromString(canonicalText.str()))
                return fail("contextual canonical checkpoint: "+error);
            for(int format=0;format<2;++format) {
                const std::string name=format?"canonical-extension":"ordinary14";
                const std::string filename=label+'-'+saveStage+'-'+name+".ini";
                std::ofstream saved(output/filename,std::ios::binary);saved<<(format?canonicalText.str():ordinaryText.str());saved.close();
                if(!saved)return fail("checkpoint evidence write");
                replicas[format]=std::make_unique<RuntimeWindowState>();auto& q=*replicas[format];seedRuntimeLoadScratch(q,s,false);
                q.impactEffects.clear(); q.projectiles.clear(); q.recentExpiredProjectiles.clear();
                { RuntimeSnapshotActiveStateScope restore(q);
                    if(!materializeRuntimeLoadStateFromIni(q,ordinaryIni,error))return fail(name+" materialization: "+error);
                    q.impactEffects.clear();
                    if(format&&!materializeRuntimeMultiplayerSnapshotExtension(q,canonicalIni,error))return fail(name+" extension: "+error);
                }
                q.runtimeSoundsEnabled=q.runtimePreferencePersistenceEnabled=false;q.runtimeSuppressPlayerAudioFeedback=true;
                initializeRuntimeOfflineGameplayRouting(q);
                if(!same(q,format!=0)||!sample(q,name.c_str(),saveStage+"-restored"))return fail(name+" changed continuous pose/order/projectiles/budget at "+saveStage);
                checkpoints<<label<<','<<saveStage<<','<<name<<','<<q.tick<<','<<hz<<','<<q.specialMechanisms.stateHash()<<','<<q.particles.stateHash()
                    <<','<<runtimeObjectRenderX(*findObjectById(q,1))<<','<<runtimeObjectRenderY(*findObjectById(q,1))
                    <<','<<runtimeObjectRenderLevel(*findObjectById(q,1))<<','<<shotIds.size()<<','<<ordinaryIni.getValue("CarrierFlightPoses","Count","0")
                    <<','<<q.specialMechanisms.carrierChildren().size()<<','<<filename<<",PASS\n";
                if(format)++caseCanonical;else ++caseOrdinary;
            }
            return true;
        };
        const auto rebuildNavigation = [&](RuntimeWindowState& q,const char* branch) {
            const auto bodies=bodyRecords(q),rows=carrierRows(q),projectiles=projectileRecords(q);
            const auto carrierHash=q.specialMechanisms.stateHash();
            std::map<int,std::string> orders;
            for(int id:ids){const auto b=q.behaviors.find(id);
                if(b!=q.behaviors.end())orders.emplace(id,runtimeMultiplayerSnapshotBehaviorRecord(id,b->second));}
            initializeRuntimeNavigation(q);
            std::map<int,std::string> afterOrders;
            for(int id:ids){const auto b=q.behaviors.find(id);
                if(b!=q.behaviors.end())afterOrders.emplace(id,runtimeMultiplayerSnapshotBehaviorRecord(id,b->second));}
            if(bodyRecords(q)!=bodies||carrierRows(q)!=rows||projectileRecords(q)!=projectiles
                ||q.specialMechanisms.stateHash()!=carrierHash||afterOrders!=orders)
                return fail(std::string(branch)+" navigation rebuild changed an actual in-flight body/order/sortie");
            ++caseNavRebuilds;return sample(q,branch,"outbound-nav-rebuilt");
        };
        const auto capture = [&](const std::string& captureStage,int childId) {
            const auto* child=findObjectById(s,childId);if(!child)return fail("capture aircraft absent");
            refreshRuntimeSpecialMechanismRenderSnapshots(s);auto context=contextFor(s);
            const RECT bounds{0,0,960,720};RuntimeMapCamera camera;camera.initialized=true;
            camera.worldOffsetX=(82-75)*camera.tileWidth/2-480;
            camera.worldOffsetY=(82+75)*camera.tileHeight/2-360;
            const double x=runtimeObjectRenderX(*child),y=runtimeObjectRenderY(*child),z=runtimeObjectRenderLevel(*child);
            // Native flat-water projection, independently expanded rather
            // than using the renderer's body-anchor query as the oracle.
            const int lift=child->airborne?int(std::lround(34.0*(child->aircraftDockFlightProgress>=0
                ?std::clamp(child->aircraftDockFlightProgress,0.0,1.0):1.0))):0;
            const POINT center{int(std::lround((x-y)*camera.tileWidth/2))-camera.worldOffsetX,
                int(std::lround((x+y-z)*camera.tileHeight/2))-camera.worldOffsetY-lift};
            RuntimeMobileSpriteBoundsProbe cold,warm,pan;auto cache=std::make_unique<RuntimeMapRenderCache>();
            if(!verifyRuntimeMobileObjectSpriteBounds(*child,center,context,camera,*cache,cold)
                ||!cold.resourceResolved||!cold.exactMatch||cold.renderedPixels<=0||cold.outsidePixels||cold.clipped)
                return fail("original Hornet body pixels/bounds: "+cold.diagnostic);
            if(!verifyRuntimeMobileObjectSpriteBounds(*child,center,context,camera,*cache,warm)
                ||cold.pixels!=warm.pixels||!warm.exactMatch)return fail("cold/warm real aircraft pixels changed");
            RuntimeMapCamera panned=camera;panned.worldOffsetX+=11;panned.worldOffsetY-=7;
            const POINT translated{center.x-11,center.y+7};
            if(!verifyRuntimeMobileObjectSpriteBounds(*child,translated,context,panned,*cache,pan)
                ||!pan.exactMatch||pan.pixels!=cold.pixels)return fail("panned native body pixels changed");
            bool originalModel=false;
            for(const auto& [key,source]:cache->voxelSourcesByImage)
                if(source.loaded&&toLowerAsciiLocal(source.metadata.imageId)=="hornet") {
                    const auto& model=models.at("hornet");
                    if(source.vxlBytes!=model.vxl||source.hvaBytes!=model.hva||source.metadata.vxlSectionCount!=model.sections
                        ||source.metadata.hvaFrameCount!=model.frames)return fail("renderer did not use complete original Hornet VXL/HVA");
                    originalModel=true;
                }
            if(!originalModel)return fail("native Hornet model source not observed");
            const auto placement=runtimeObjectWorldRenderPlacement(*child,s.world,bounds,context,camera,*cache);
            if(placement.center.x!=center.x||placement.center.y!=center.y)return fail("flat-water body anchor disagrees with independently projected physical XYZ");
            const std::string bodyName=label+'-'+captureStage+"-body.bmp";
            if(!writeRuntimeSmokeBmp(pathString(output/bodyName),cold.pixels.data(),cold.imageWidth,cold.imageHeight,error))return fail("native body preview: "+error);
            pixels<<label<<','<<captureStage<<','<<s.tick<<','<<childId<<','<<int(s.specialMechanisms.carrierChildren().at(childId).phase)
                <<','<<x<<','<<y<<','<<z<<','<<child->aircraftDockFlightProgress<<','<<center.x<<','<<center.y
                <<','<<cold.spriteBounds.left<<','<<cold.spriteBounds.top<<','<<cold.spriteBounds.right<<','<<cold.spriteBounds.bottom
                <<','<<cold.renderedPixels<<','<<cold.layerCount<<','<<models.at("hornet").sections<<",1,1,"<<bodyName<<'\n';++casePixels;
            const std::string mapName=label+'-'+captureStage+"-map.bmp";
            std::vector<std::uint32_t> fullCold,fullWarm;
            // The isolated cache has no effect on simulation and is restored
            // before another tick. The warm draw uses that same new cache.
            struct CacheScope { RuntimeWindowState& s; RuntimeMapRenderCache old;
                CacheScope(RuntimeWindowState& state):s(state),old(std::move(state.renderCache)){s.renderCache={};}
                ~CacheScope(){s.renderCache=std::move(old);} } isolated(s);
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{82,75},960,720,error,pathString(output/mapName),false,false,-1,&fullCold);
            const auto second=drawRuntimeStateOffscreenAtCell(s,{82,75},960,720,error,"",false,false,-1,&fullWarm);
            if(!stats||!second||!stats->rendered||stats->missingVoxelProxies||stats->visibleVoxelBodies<2||fullCold!=fullWarm)
                return fail("actual carrier/aircraft scene cold/warm paint");
            ++caseCaptures;return true;
        };
        struct Pose { double x=0,y=0,z=0;bool air=false;CarrierChildPhase phase=CarrierChildPhase::Docked;int facing=0; };
        std::map<int,Pose> previous;
        for(int id:ids)if(const auto* o=findObjectById(s,id))previous[id]={runtimeObjectRenderX(*o),runtimeObjectRenderY(*o),runtimeObjectRenderLevel(*o),o->airborne,CarrierChildPhase::Docked,o->facing};
        const auto shot = [&](const RuntimeProjectileEffect& p) {
            if(p.attackerObjectId==1&&p.weaponId=="HornetLauncher")return fail("carrier generated a fake direct HornetLauncher projectile");
            if(!ids.contains(p.attackerObjectId)||!shotIds.insert(p.actorId).second)return true;
            const int id=p.attackerObjectId;const auto* child=findObjectById(s,id);const auto* parent=findObjectById(s,1);
            const auto sortie=s.specialMechanisms.carrierChildren().find(id);
            if(!child||!parent||sortie==s.specialMechanisms.carrierChildren().end()||landed.contains(id)
                ||p.weaponId!="HornetBomb"||p.projectileId!="NormalBomb"||p.warheadId!="ORCAAP"||p.damage!=40
                ||p.targetObjectId!=2||!child->airborne)return fail("actual first-sortie projectile identity/damage/target");
            const int ordinal=++shotCount[id];const int spawn=p.spawnTick;
            if(ordinal>3)return fail("one aircraft exceeded three first-sortie projectiles");
            if(previousShot.contains(id)) {
                const int spacing=spawn-previousShot.at(id),minimum=3*hz/15,maximum=5*hz/15;
                if(spacing<minimum||spacing>maximum)return fail("shot spacing outside original ROF3 plus retail 0..2 frames for #"+std::to_string(id)+" spacing="+std::to_string(spacing));
            }
            previousShot[id]=spawn;const auto rows=carrierRows(s);
            shots<<label<<','<<s.tick<<','<<p.actorId<<','<<id<<','<<slots.at(id)<<','<<ordinal<<','<<spawn
                <<','<<p.weaponId<<','<<p.projectileId<<','<<p.warheadId<<','<<p.damage<<",40,3,1,1,"
                <<p.sourceX<<','<<p.sourceY<<','<<p.sourceZ<<','<<p.targetX<<','<<p.targetY<<','<<p.targetZ
                <<','<<runtimeObjectRenderX(*child)<<','<<runtimeObjectRenderY(*child)<<','<<runtimeObjectRenderLevel(*child)
                <<','<<runtimeObjectRenderX(*parent)<<','<<runtimeObjectRenderY(*parent)<<','<<runtimeObjectRenderLevel(*parent)
                <<','<<int(sortie->second.phase)<<','<<child->currentAmmo<<','<<iniHexEncode(rows.at(id))<<'\n';return true;
        };
        const auto orderStop = [&](RuntimeWindowState& q,const char* branch) {
            RuntimeSnapshotActiveStateScope scope(q);commandStage="nine-shot-stop";commandBranch=branch;
            if(!select(q))return false;
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&q));
            SendMessageW(window,WM_KEYDOWN,'S',0);SendMessageW(window,WM_KEYUP,'S',0);
            return q.lastCommand.acceptedCount==1&&!q.behaviors.contains(1)?true:fail("actual UI Stop after nine projectiles rejected");
        };
        const RuntimeCellPoint returnGoal{75+delta.x*2,75+delta.y*2};
        const auto orderMove = [&](RuntimeWindowState& q,const char* branch) {
            RuntimeSnapshotActiveStateScope scope(q);commandStage="moving-home";commandBranch=branch;
            if(!select(q))return false;centerRuntimeCameraOnCell(q,client,returnGoal);ensureRuntimeCameraForViewport(q,viewport);
            const POINT initial=runtimeScreenPointForCell(q,viewport,returnGoal,0);
            const auto& projected=runtimeProjectedObjectsForViewport(q,viewport);POINT point{};bool found=false;
            for(int distance=0;distance<=q.camera.tileHeight&&!found;++distance)
                for(int dy=-distance;dy<=distance&&!found;++dy)for(int dx=-distance;dx<=distance;++dx) {
                    if(std::max(std::abs(dx),std::abs(dy))!=distance)continue;
                    const POINT candidate{initial.x+dx,initial.y+dy};const POINT owned=approximateCellFromScreenPoint(q,viewport,candidate);
                    if(!PtInRect(&viewport,candidate)||owned.x!=returnGoal.x||owned.y!=returnGoal.y
                        ||findProjectedObjectAtPoint(projected,candidate))continue;
                    point=candidate;found=true;break;
                }
            if(!found||!waterAt(q,returnGoal.x,returnGoal.y))return fail("actual unoccupied water Move pixel");
            lastPoint=point;lastPicked=-1;mouse(q,point);
            const auto b=q.behaviors.find(1);
            return q.lastCommand.acceptedCount==1&&b!=q.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move
                ?true:fail("real UI carrier return-home movement rejected");
        };
        bool savedOutbound=false,savedVolley=false,savedReturn=false;
        bool captureLaunch=false,captureVolley=false,captureLanding=false;
        const auto run = [&]() {
            if(ids.size()!=3||!sample(s,"original","docked"))return fail("three original aircraft not naturally spawned");
            for(int id:ids) {
                const auto* o=findObjectById(s,id);const auto& c=s.specialMechanisms.carrierChildren().at(id);
                if(o->airborne||o->dockedToObjectId!=1||o->currentAmmo!=1||c.phase!=CarrierChildPhase::Docked
                    ||!runtimeSpecialObjectHiddenForInteraction(s,*o))return fail("original hidden deck aircraft state");
            }
            stage="real carrier mouse attack";if(!select(s)||!pointFor(s,2,lastPoint))return false;
            lastPicked=2;mouse(s,lastPoint);
            const auto parentOrder=s.behaviors.find(1);
            if(capturedCommands!=1||acceptedCommands!=1||s.lastCommand.acceptedCount!=1||parentOrder==s.behaviors.end()
                ||parentOrder->second.kind!=RuntimeBehaviorKind::Attack||parentOrder->second.targetObjectId!=2)return fail("actual enemy structure mouse attack did not launch original spawner order");
            for(int n=0;n<hz*45;++n) {
                stage="natural flight/volley/return";step(s);++naturalTicks;
                for(auto& q:replicas)if(q)step(*q);
                if(!sample(s,"original","sortie"))return false;
                for(int format=0;format<2;++format)if(replicas[format]) {
                    if(!same(*replicas[format],format!=0)||!sample(*replicas[format],format?"canonical-extension":"ordinary14","sortie"))
                        return fail("strict saved continuation diverged at tick="+std::to_string(s.tick)+" branch="+std::to_string(format));
                }
                if(replicas[0])++casePaired;
                for(const auto& p:s.projectiles)if(!shot(p))return false;
                for(const auto& p:s.recentExpiredProjectiles)if(!shot(p))return false;
                const auto* parent=findObjectById(s,1);
                parentDistance=std::max(parentDistance,std::hypot(runtimeObjectRenderX(*parent)-75,runtimeObjectRenderY(*parent)-75));
                for(int id:ids) {
                    const auto* o=findObjectById(s,id);const auto& c=s.specialMechanisms.carrierChildren().at(id);
                    const Pose now{runtimeObjectRenderX(*o),runtimeObjectRenderY(*o),runtimeObjectRenderLevel(*o),o->airborne,c.phase,o->facing};
                    const auto old=previous.at(id);
                    if(old.air&&now.air&&old.phase==CarrierChildPhase::Attacking
                        &&now.phase==CarrierChildPhase::Attacking) {
                        const auto* victim=findObjectById(s,2);
                        const double goalX=runtimeObjectRenderX(*victim)-old.x;
                        const double goalY=runtimeObjectRenderY(*victim)-old.y;
                        const double remaining=std::hypot(goalX,goalY);
                        // Stay well outside the original Range5 and the
                        // building foundation so real firing/rest is excluded.
                        if(remaining>9.0&&std::abs(now.z-old.z)<1e-8) {
                            const double dx=now.x-old.x,dy=now.y-old.y;
                            const double distance=std::hypot(dx,dy);
                            constexpr double tau=6.2831853071795864769;
                            const int desired=(static_cast<int>(std::lround(std::atan2(goalX-goalY,-goalX-goalY)*256.0/tau))%256+256)%256;
                            const auto facingDistance=[](int a,int b){const int delta=(a-b+256)%256;return std::min(delta,256-delta);};
                            const int turn=facingDistance(now.facing,old.facing);maxTurn=std::max(maxTurn,turn);
                            // Original ROT3 is a 15 Hz rule-frame budget. The
                            // retail integer FacingClass division and Dir256
                            // sampling can retain two units of endpoint residue.
                            const int ruleFrames=s.tick*15/hz-(s.tick-1)*15/hz;
                            const int turnBudget=3*ruleFrames+2;
                            const double cross=std::abs(dx*goalY-dy*goalX)/remaining;
                            const double speedError=std::abs(distance-originalCruiseStep);
                            maxCruiseError=std::max(maxCruiseError,speedError);++cruiseSamples;
                            const bool aligned=!o->bodyFacingClock.active()&&facingDistance(now.facing,desired)<=1;
                            bool wrongDirection=false;
                            if(aligned){
                                ++straightSamples;++straightByChild[id];maxCrossError=std::max(maxCrossError,cross);
                                wrongDirection=dx*goalX+dy*goalY<=0.0||cross>1e-6;
                            }else {
                                ++turnSamples;const double angle=now.facing*tau/256.0;
                                const double hx=(std::sin(angle)-std::cos(angle))*0.7071067811865475244;
                                const double hy=(-std::sin(angle)-std::cos(angle))*0.7071067811865475244;
                                const double headingCross=std::abs(dx*hy-dy*hx);
                                int desiredTurn=(desired-old.facing+256)%256;if(desiredTurn>=128)desiredTurn-=256;
                                int actualTurn=(now.facing-old.facing+256)%256;if(actualTurn>=128)actualTurn-=256;
                                wrongDirection=!o->bodyFacingClock.active()||o->bodyFacingClock.rate!=3*256
                                    ||actualTurn*desiredTurn<0||dx*hx+dy*hy<=0.0
                                    ||headingCross>originalCruiseStep*std::sin(tau/512.0)+1e-6;
                            }
                            if(distance<=1e-10||speedError>1e-6||turn>turnBudget||wrongDirection) {
                                std::ostringstream d;d<<std::setprecision(17)<<"actual carrier flight paused, left its heading or exceeded original Speed12/ROT3"
                                    <<" child="<<id<<" tick="<<s.tick<<" from="<<old.x<<','<<old.y
                                    <<" to="<<now.x<<','<<now.y<<" target="<<runtimeObjectRenderX(*victim)<<','<<runtimeObjectRenderY(*victim)
                                    <<" step="<<distance<<" original="<<originalCruiseStep<<" cross="<<cross
                                    <<" facing="<<old.facing<<"->"<<now.facing<<" desired="<<desired
                                    <<" active="<<o->bodyFacingClock.active()<<" turn="<<turn<<" budget="<<turnBudget
                                    <<" aligned="<<aligned;return fail(d.str());
                            }
                        }
                    }
                    if(now.air&&!launched.contains(id)) {
                        launched[id]=s.tick;
                        const double portDistance=std::hypot(now.x-runtimeObjectRenderX(*parent),now.y-runtimeObjectRenderY(*parent));
                        if(portDistance<=.5)portIds.insert(id);
                        if(now.z>1.0*30/hz+1e-8)continuous=false;
                        const auto mouth=runtimeCarrierDeckMouth(*parent);
                        if(std::hypot(now.x-mouth[0],now.y-mouth[1])>1e-8
                            ||std::abs(now.z-mouth[2])>1e-8)continuous=false;
                    }
                    // Only two visible airborne poses form a flight segment.
                    // Docked children are hidden at the parent's logical core.
                    if(old.air&&now.air) {
                        const double dx=now.x-old.x,dy=now.y-old.y,dz=now.z-old.z;
                        const double maximum=2.0/std::max(1,runtimeMovementIntervalTicks(s,*o));
                        if(std::hypot(dx,dy)>maximum+1e-8||std::abs(dz)>1.0*30/hz+1e-8)continuous=false;
                    }
                    if(old.air&&!now.air) {
                        const auto mouth=runtimeCarrierDeckMouth(*parent);
                        const double step=1.0/std::max(1,runtimeMovementIntervalTicks(s,*o));
                        const double vertical=50.0/256.0*15.0/hz+1.0/65536.0;
                        if(old.phase!=CarrierChildPhase::Returning||c.phase!=CarrierChildPhase::Docked
                            ||std::hypot(old.x-mouth[0],old.y-mouth[1])>step+1e-8
                            ||std::abs(old.z-mouth[2])>vertical+1e-8)continuous=false;
                    }
                    const double ground=runtimeObjectRenderLevel(*parent),cruise=runtimeAircraftCruiseLevel(s);
                    if(now.air&&now.z>ground+1e-8&&now.z<cruise-1e-8) {
                        if(c.phase==CarrierChildPhase::Returning)fallLevels[id].insert(now.z);
                        else riseLevels[id].insert(now.z);
                    }
                    if(launched.contains(id)&&c.phase==CarrierChildPhase::Docked&&!landed.contains(id))landed[id]=s.tick;
                    previous[id]=now;
                }
                // Use actual body XY, independently of launch timestamps and
                // the production mouth queue. All three must be airborne and
                // still armed together, at pairwise distance at least .75.
                std::vector<std::pair<double,double>> separatedOutbound;
                const bool allAirborneAndArmed=std::all_of(ids.begin(),ids.end(),[&](int id) {
                    const auto* child=findObjectById(s,id);
                    return child&&child->airborne&&child->currentAmmo==1;
                });
                if(allAirborneAndArmed) {
                    for(int id:ids) {
                        const auto* child=findObjectById(s,id);
                        const std::pair<double,double> body{runtimeObjectRenderX(*child),runtimeObjectRenderY(*child)};
                        if(std::none_of(separatedOutbound.begin(),separatedOutbound.end(),[&](const auto& prior) {
                            return std::hypot(prior.first-body.first,prior.second-body.second)<.75;
                        }))separatedOutbound.push_back(body);
                    }
                    maxOutboundSeparated=std::max(maxOutboundSeparated,static_cast<int>(separatedOutbound.size()));
                }
                if(!savedOutbound) {
                    for(int id:ids) {const auto* o=findObjectById(s,id);
                        if(o->airborne&&o->hasRenderPosition&&shotIds.empty()&&straightByChild[id]>=3) {
                            stage="outbound checkpoint";if(!save("outbound"))return false;savedOutbound=true;
                            stage="mid-flight navigation rebuild";
                            if(!rebuildNavigation(s,"original"))return false;
                            for(int format=0;format<2;++format){
                                if(!rebuildNavigation(*replicas[format],format?"canonical-extension":"ordinary14"))return false;
                                if(!same(*replicas[format],format!=0))return fail("navigation rebuild changed paired flight continuation");
                            }
                            navRebuilt=true;
                            if(!capture("outbound",id))return false;captureLaunch=true;break;
                        }
                    }
                }
                if(!savedVolley&&!shotIds.empty()) {
                    stage="first-real-shot checkpoint";if(!save("first-shot"))return false;savedVolley=true;
                    const int id=shotCount.begin()->first;if(!capture("first-shot",id))return false;captureVolley=true;
                }
                if(!stopped&&shotIds.size()==9) {
                    if(!orderStop(s,"original"))return false;
                    for(int format=0;format<2;++format)if(replicas[format]&&!orderStop(*replicas[format],format?"canonical-extension":"ordinary14"))return false;
                    stopped=true;
                    if(moving) {
                        if(!orderMove(s,"original"))return false;
                        for(int format=0;format<2;++format)if(replicas[format]&&!orderMove(*replicas[format],format?"canonical-extension":"ordinary14"))return false;
                    }
                }
                const auto parentBehavior=s.behaviors.find(1);
                if(moving&&stopped&&parentDistance>.25)parentMoved=true;
                // A moving home is exercised throughout return. Ordinary14
                // checkpoints wait for its real completed Move, so this does
                // not add a generic vehicle route serialization requirement.
                const bool parentSettled=!moving||(stopped&&parentMoved&&parentBehavior==s.behaviors.end()
                    &&!findObjectById(s,1)->renderMoveActive);
                if(!savedReturn&&parentSettled)for(int id:ids) {
                    const auto* o=findObjectById(s,id);const auto& c=s.specialMechanisms.carrierChildren().at(id);
                    if(c.phase==CarrierChildPhase::Returning&&o->airborne&&o->hasRenderPosition) {
                        stage="return checkpoint";if(!save("return"))return false;savedReturn=true;
                        break;
                    }
                }
                if(!captureLanding)for(int id:ids) {
                    const auto* o=findObjectById(s,id);const auto& c=s.specialMechanisms.carrierChildren().at(id);
                    if(c.phase==CarrierChildPhase::Returning&&o->airborne&&fallLevels[id].size()>=1) {
                        if(!capture("descending",id))return false;captureLanding=true;break;
                    }
                }
                if(landed.size()==3&&std::none_of(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return ids.contains(p.attackerObjectId);}))break;
                if(n+1==hz*45)return fail("first sortie did not naturally finish within 45 seconds");
            }
            stage="first-sortie result";
            std::set<int> launchTicks;int rise=0,fall=0;
            for(int id:ids) {
                if(launched.contains(id))launchTicks.insert(launched.at(id));
                if(riseLevels[id].size()>=2)++rise;if(fallLevels[id].size()>=2)++fall;
                const auto* o=findObjectById(s,id);const auto& c=s.specialMechanisms.carrierChildren().at(id);const auto* parent=findObjectById(s,1);
                if(c.phase!=CarrierChildPhase::Docked||o->airborne||o->dockedToObjectId!=1||o->currentAmmo!=1
                    ||std::hypot(runtimeObjectRenderX(*o)-runtimeObjectRenderX(*parent),runtimeObjectRenderY(*o)-runtimeObjectRenderY(*parent))>1e-8
                    ||std::abs(runtimeObjectRenderLevel(*o)-runtimeObjectRenderLevel(*parent))>1e-8
                    ||!runtimeSpecialObjectHiddenForInteraction(s,*o))return fail("natural final docking pose/hidden/Ammo1 identity");
            }
            std::ostringstream detail;detail<<"planes="<<ids.size()<<" launches="<<launched.size()<<" separate="<<launchTicks.size()
                <<" actors="<<shotIds.size()<<" damage="<<1000-findObjectById(s,2)->health<<" docked="<<landed.size()
                <<" rise="<<rise<<" descend="<<fall<<" continuous="<<continuous<<" ports="<<portIds.size()
                <<" parentTravel="<<parentDistance<<" checkpoints="<<caseOrdinary<<'/'<<caseCanonical
                <<" straightSamples="<<straightSamples<<" maxCross="<<maxCrossError<<" maxCruiseError="<<maxCruiseError
                <<" navRebuilds="<<caseNavRebuilds<<" cruiseSamples="<<cruiseSamples<<" turnSamples="<<turnSamples<<" maxTurn="<<maxTurn
                <<" maxOutboundSeparated="<<maxOutboundSeparated;
            if(launched.size()!=3||shotIds.size()!=9||shotCount.size()!=3||landed.size()!=3
                ||rise!=3||fall!=3||!continuous||portIds.size()!=3||1000-findObjectById(s,2)->health!=360
                ||!stopped||(moving&&!parentMoved)||!savedOutbound||!savedVolley||!savedReturn
                ||caseOrdinary!=3||caseCanonical!=3||!captureLaunch||!captureVolley||!captureLanding
                ||!navRebuilt||caseNavRebuilds!=3)
                return fail("three continuous native takeoffs/three real shots each/continuous landing: "+detail.str());
            if(maxOutboundSeparated!=3)return fail("three armed airborne aircraft never separated by .75 in actual XY: "+detail.str());
            for(int id:ids){if(shotCount.at(id)!=3)return fail("every original aircraft must fire exactly three projectiles");
                if(straightByChild[id]<3)return fail("every original aircraft must supply actual straight cruise trajectory samples");}
            return true;
        };
        const bool ok=run();std::set<int> launchTicks;int rise=0,fall=0;
        for(int id:ids){if(launched.contains(id))launchTicks.insert(launched.at(id));if(riseLevels[id].size()>=2)++rise;if(fallLevels[id].size()>=2)++fall;}
        cases<<label<<','<<hz<<','<<direction<<','<<moving<<','<<ids.size()<<','<<launched.size()<<','<<launchTicks.size()<<','<<shotIds.size()
            <<','<<shotCount.size()<<','<<(findObjectById(s,2)?1000-findObjectById(s,2)->health:-1)<<','<<landed.size()<<','<<rise<<','<<fall
            <<','<<continuous<<','<<portIds.size()<<','<<parentDistance<<','<<caseOrdinary<<','<<caseCanonical<<','<<casePaired
            <<','<<caseCaptures<<','<<casePixels<<','<<naturalTicks<<','<<(ok?"PASS":"FAIL")<<','<<iniHexEncode(error)
            <<','<<oblique<<','<<straightSamples<<','<<maxCrossError<<','<<maxCruiseError<<','<<caseNavRebuilds
            <<','<<cruiseSamples<<','<<turnSamples<<','<<maxTurn<<','<<maxOutboundSeparated<<'\n';
        cases.flush();movement.flush();shots.flush();commands.flush();checkpoints.flush();pixels.flush();assetsCsv.flush();
        if(ok){++passed;totalShots+=int(shotIds.size());totalOrdinary+=caseOrdinary;totalCanonical+=caseCanonical;
            totalPaired+=casePaired;totalCaptures+=caseCaptures;totalPixels+=casePixels;
            totalStraightSamples+=straightSamples;totalNavRebuilds+=caseNavRebuilds;totalTurnSamples+=turnSamples;
            std::cout<<"Carrier flight transition "<<label<<" PASS 3 native aircraft/9 original bombs/continuous takeoff+landing/3 paired checkpoints"
                <<" straightSamples="<<straightSamples<<" navRebuilds="<<caseNavRebuilds<<" turnSamples="<<turnSamples<<'\n';}
        else{failures.push_back(error);std::cout<<error<<'\n';error.clear();}
        SetWindowLongPtrW(window,GWLP_USERDATA,0);
    }
    if(passed==0&&failures.empty())return fail("unknown case filter: "+filter);
    if(!failures.empty()){error=failures.front()+" ("+std::to_string(failures.size())+" failing cases)";return false;}
    if(passed!=selectedCases||(filter.empty()&&passed!=32)||totalShots!=passed*9
        ||totalOrdinary!=passed*3||totalCanonical!=passed*3||totalNavRebuilds!=passed*3
        ||(filter.empty()&&totalTurnSamples==0))
        return fail("full selected original/oblique matrix did not complete every strong sortie/checkpoint contract");
    std::cout<<"Carrier flight transition passed: cases="<<passed<<" originalShotActors="<<totalShots
        <<" ordinary14="<<totalOrdinary<<" canonicalExtensions="<<totalCanonical<<" pairedTicks="<<totalPaired
        <<" nativeCaptures="<<totalCaptures<<" nativePixelChecks="<<totalPixels
        <<" straightSamples="<<totalStraightSamples<<" navRebuilds="<<totalNavRebuilds<<" turnSamples="<<totalTurnSamples
        <<" originalAmmo1/Burst1/HP/ROF/targetdamage=ok\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
