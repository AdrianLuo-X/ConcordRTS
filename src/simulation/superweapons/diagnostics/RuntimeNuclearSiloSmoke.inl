bool runRuntimeNuclearSiloSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "nuclear silo: " + reason; return false; };
    const char* output = std::getenv("CONCORD_NUCLEAR_SILO_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    const auto fixture = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = live.config; s.config.computerAiEnabled = false; s.config.superweapons = true;
        s.report = live.report; s.rules = live.rules; s.rulesLoaded = true;
        s.art = live.art; s.artLoaded = true; s.theater = live.theater; s.theaterLoaded = live.theaterLoaded;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 20; y <= 115; ++y) for (int x = 20; x <= 115; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        for (const auto* id : {"NAMISL", "NANRCT"}) {
            WorldObjectInstance object; object.instanceId = static_cast<int>(s.world.getObjects().size()) + 1;
            object.type = id; object.kind = WorldObjectKind::Structure; object.owner = "Russians";
            object.alive = true; object.health = s.rules.findBuildingType(id)->strength;
            object.x = object.y = object.instanceId == 1 ? 90 : 20;
            object.cell = runtimeCellKey(object.x, object.y); object.mission = "Guard";
            s.world.accessObjects().push_back(object);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSuperWeaponsInitialized(s);
        auto context = runtimeSuperWeaponContext(s); s.superWeapons.tick(context);
        return storage;
    };
    const auto* type = live.rules.findBuildingType("NAMISL");
    if (!type || type->chargedAnimTime != 1.0) return fail("original ChargedAnimTime missing");
    for (const auto* id : {"NAMISL_E", "NAMISL_ED", "NAMISL_F", "NAMISL_FD",
            "NAMISL_G", "NAMISL_GD", "NAMISL_H", "NAMISL_HD"}) {
        if (!live.art.findArtType(id)) return fail(std::string("original art missing: ") + id);
    }
    auto storage = fixture(); auto& s = *storage;
    const auto step = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) { ++s.tick; advanceRuntimeEffects(s); }
    };
    const auto render = [&](RuntimeWindowState& target, const std::string& name,
            const std::string& image, int frame) {
        const auto stats = drawRuntimeStateOffscreenAtCell(target, {90,90}, 640, 480, error,
            root.empty() ? "" : root + "/" + name + ".bmp", false);
        if (!stats) return false;
        if (stats->missingStructureSprites || stats->buildingArtAnimationMissing
                || stats->buildingArtAnimationLayers != 1 || stats->firstBuildingArtAnimationImage != image
                || stats->firstBuildingArtAnimationFrame != frame) {
            error = name + " expected=" + image + ":" + std::to_string(frame)
                + " actual=" + stats->firstBuildingArtAnimationImage + ":"
                + std::to_string(stats->firstBuildingArtAnimationFrame)
                + " layers=" + std::to_string(stats->buildingArtAnimationLayers)
                + " missing=" + std::to_string(stats->missingStructureSprites + stats->buildingArtAnimationMissing);
            return false;
        }
        return true;
    };
    if (!render(s, "01-idle", "NAMISL_E", 0)) return fail(error);
    findMutableObjectById(s,1)->health = 400;
    if (!render(s, "02-idle-damaged", "NAMISL_ED", 1)) return fail(error);
    findMutableObjectById(s,1)->health = type->strength;

    // Charge the actual source, including its authored one-minute ready window.
    const int window = s.config.fixedTickHz * 60;
    for (int i = 0; i < s.config.fixedTickHz * 600; ++i) {
        const auto* weapon = s.superWeapons.findSidebarSnapshot("Russians", "NukeSpecial");
        if (weapon && weapon->remainingTicks <= window) break;
        ++s.tick; auto context = runtimeSuperWeaponContext(s); s.superWeapons.tick(context);
    }
    if (!render(s, "03-opening", "NAMISL_F", 0)) return fail(error);
    step(35);
    if (!render(s, "04-opening-mid", "NAMISL_F", 7)) return fail(error);
    findMutableObjectById(s,1)->health = 400;
    if (!render(s, "05-opening-damaged", "NAMISL_FD", 23)) return fail(error);
    findMutableObjectById(s,1)->health = type->strength;
    if (!root.empty()) {
        const auto path = root + "/opening-save.ini";
        if (!saveRuntimeState(s,path,error)) return fail("save: " + error);
        auto loaded = fixture();
        if (!loadRuntimeState(*loaded,path,error)
                || !render(*loaded,"06-opening-reloaded","NAMISL_F",7)) return fail("reload: " + error);
    }
    for (int i = 0; i < 30; ++i) {
        ++s.tick; auto context = runtimeSuperWeaponContext(s);
        context.ownerPowered = [](const auto&) { return false; }; s.superWeapons.tick(context);
    }
    if (!render(s, "07-power-paused", "NAMISL_F", 7)) return fail(error);
    step(50);
    if (!render(s, "08-charged", "NAMISL_G", 0)) return fail(error);
    findMutableObjectById(s,1)->health = 400;
    if (!render(s, "09-charged-damaged", "NAMISL_GD", 1)) return fail(error);
    findMutableObjectById(s,1)->health = type->strength;
    auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
    if (!s.superWeapons.forceReady("Russians","NukeSpecial",&context)
            || !s.superWeapons.beginTargetSelection("Russians","NukeSpecial",context,&error)
            || !s.superWeapons.submitTarget("Russians","NukeSpecial",{{108,108},-1},context,&error))
        return fail("launch: " + error);
    const int launch = s.superWeapons.findSidebarSnapshot("Russians","NukeSpecial")->launchTick;
    if (launch < 0) return fail("launch clock missing");
    step(std::max(0, launch - s.tick));
    if (!render(s, "10-closing", "NAMISL_H", 0)) return fail(error);
    step(35);
    if (!render(s, "11-closing-mid", "NAMISL_H", 7)) return fail(error);
    findMutableObjectById(s,1)->health = 400;
    if (!render(s, "12-closing-damaged", "NAMISL_HD", 23)) return fail(error);
    findMutableObjectById(s,1)->health = type->strength;
    step(45);
    if (!render(s, "13-reset", "NAMISL_E", 0)) return fail(error);
    step(100);
    if (!render(s, "14-recharging", "NAMISL_E", 0)) return fail(error);
    std::cout << "Nuclear silo: original idle/open/charged/close layers, damaged halves, power pause, save/load and launch reset passed\n";
    return true;
}
