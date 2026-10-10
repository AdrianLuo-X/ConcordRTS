// Issue 22/19: selling an actual registered bridge hut must remove only its
// service descriptor. Ordinary14 and canonical extension restores use the
// same original-map seed; this is not a fresh-launch or entry-cancellation test.
bool runRuntimeSellBoundarySmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_SELL_BOUNDARY_OUTPUT");
    const std::filesystem::path output = configured && *configured
        ? configured : "build/issue22_19_sell_boundary";
    std::filesystem::create_directories(output);
    std::ofstream cases(output / "cases.csv"), checkpoints(output / "checkpoints.csv");
    std::string stage = "original all02 startup";
    const auto fail = [&](const std::string& why) {
        error = "Sell boundary actual-all02-CABHUT [" + stage + "]: " + why; return false;
    };
    if (!cases || !checkpoints) return fail("output unavailable");
    cases << "case,hut,x,y,hp,original_huts,original_spans,remaining_spans,terrain_cells,refund,ui_commands,result\n";
    checkpoints << "case,format,tick,hz,hut,remaining_spans,terrain_cells,result\n";
    auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
    s.config = live.config;
    s.config.defaultMapPath = pathString(std::filesystem::path(runtimeAssetRootPath(live.config)) / "mapsmd03/all02umd.map");
    s.config.playerSlots.clear(); s.config.skirmishLoadSavePath.clear();
    s.config.fixedTickHz = 30; s.config.playerCountry = "Americans"; s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
    s.config.multiplayerActive = s.config.noCooldown = s.config.computerAiEnabled = false;
    s.config.shortGame = s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0;
    s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
    if (!s.report.success || !s.world.getSummary().loaded) return fail(s.report.errorMessage);
    // The original campaign geometry is retained. Test actors use the same
    // stock rules as the existing all02 bridge contracts, not map overrides.
    s.rules = live.rules; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
    s.mapFactsLoaded = !s.mapFacts.cells.empty(); s.campaignMode = false; s.tick = 0;
    s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true;
    std::erase_if(s.world.accessObjects(), [&](const auto& o) {
        const auto* b = s.rules.findBuildingType(o.type); return !b || !b->bridgeRepairHut;
    });
    s.triggers = {}; s.triggers.loaded = s.triggers.bootstrapComplete = true;
    s.computerAiByOwner.clear(); s.computerMiningByOwner.clear();
    s.skirmishCountryByOwner = {{"americans", "Americans"}}; s.skirmishTeamByOwner.clear();
    s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
    int hutId = -1;
    for (auto& o : s.world.accessObjects()) if (o.x == 129 && o.y == 148) {
        if (hutId >= 0) return fail("duplicate original upper-bank hut");
        hutId = o.instanceId; o.owner = "Americans";
    }
    if (hutId < 0 || s.world.getObjects().size() != 12) return fail("original twelve huts/upper-bank identity differ");
    RuntimeSnapshotActiveStateScope active(s);
    initializeRuntimeNavigation(s); initializeRuntimeEconomy(s);
    s.economy.owner = "Americans"; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
    s.config.playerCountry = "Americans"; s.humanHouseAuthorities.clear();
    RuntimeHumanHouseAuthority house; house.owner = house.country = "Americans";
    house.canonicalOwner = "americans"; house.localFacade = true; s.humanHouseAuthorities.push_back(std::move(house));
    ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
    ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
    rebuildRuntimeEconomyPower(s); initializeRuntimeOfflineGameplayRouting(s);
    const auto* hut = findObjectById(s, hutId); const auto* type = s.rules.findBuildingType("CABHUT");
    if (!hut || !type || !type->bridgeRepairHut || type->unsellable || type->strength != 2000
        || hut->type != "CABHUT" || hut->health != type->strength || !runtimeStructureCanBeSold(s, *hut))
        return fail("original full-health, owned, sellable CABHUT unavailable");
    // Compare all native fields of every descriptor, not just its cell count.
    const auto spanRecords = [](const RuntimeWindowState& q) {
        std::map<int, std::string> records;
        for (const auto& [id, p] : q.bridgeRepairSpansByHut) {
            std::ostringstream text; text << p.hutObjectId << '|' << p.high << '|' << p.alongX << '|' << int(p.material)
                << '|' << p.startHead.x << ',' << p.startHead.y << '|' << p.endHead.x << ',' << p.endHead.y
                << '|' << p.level << '|' << p.overlayIndex << '|' << p.intactFrame << '|' << p.overlayType << '|' << p.overlayImage;
            for (const auto& c : p.cells) text << '|' << c.point.x << ',' << c.point.y << ',' << c.requiresOverlay << ',' << c.expectedFrame;
            records.emplace(id, text.str());
        }
        return records;
    };
    const auto terrainRecords = [](const RuntimeWindowState& q) {
        std::map<int, std::string> records;
        for (const auto& c : q.world.getTerrainCells()) {
            std::ostringstream text; text << c.x << '|' << c.y << '|' << c.cellIndex512 << '|' << c.tileIndex << '|' << c.subTileIndex
                << '|' << c.level << '|' << c.iceGrowth << '|' << c.hasOverlay << '|' << c.overlayIndex << '|' << c.overlayFrame
                << '|' << c.overlayType << '|' << c.overlayImage << '|' << c.overlayIsOre << '|' << c.overlayIsGem;
            records.emplace(runtimeCellKey(c.x, c.y), text.str());
        }
        return records;
    };
    const auto hutRecords = [hutId](const RuntimeWindowState& q) {
        std::map<int, std::string> records;
        for (const auto& o : q.world.getObjects()) {
            const auto* b = q.rules.findBuildingType(o.type);
            if (!b || !b->bridgeRepairHut || o.instanceId == hutId) continue;
            std::ostringstream text; text << std::setprecision(17) << o.type << '|' << o.owner << '|' << o.health << '|' << o.alive
                << '|' << o.x << '|' << o.y << '|' << o.level << '|' << o.cell << '|' << o.onBridge << '|' << o.mission
                << '|' << runtimeObjectRenderX(o) << '|' << runtimeObjectRenderY(o) << '|' << runtimeDrawLevelForObject(o);
            records.emplace(o.instanceId, text.str());
        }
        return records;
    };
    const auto originalSpans = spanRecords(s); auto remainingSpans = originalSpans;
    const auto originalTerrain = terrainRecords(s), otherHuts = hutRecords(s);
    if (originalSpans.size() != 12 || !remainingSpans.erase(hutId) || otherHuts.size() != 11
        || originalTerrain.size() != s.world.getTerrainCells().size()) return fail("natural registry/unique native terrain differs");
    const auto chosen = s.bridgeRepairSpansByHut.find(hutId); std::set<int> rawExpected, registered;
    for (int x = 132; x <= 158; ++x) {
        const auto cell = std::find_if(s.mapFacts.cells.begin(), s.mapFacts.cells.end(), [x](const auto& c) { return c.x == x && c.y == 150; });
        if (cell == s.mapFacts.cells.end() || !cell->hasOverlay || cell->overlay.overlayIndex != 24 || cell->overlay.frameIndex != 0)
            return fail("independent original upper bridge strip differs");
        rawExpected.insert(runtimeCellKey(x, 150));
    }
    for (const auto& c : chosen->second.cells) if (c.requiresOverlay) registered.insert(runtimeCellKey(c.point.x, c.point.y));
    if (registered != rawExpected) return fail("chosen natural descriptor is not the original upper bridge");
    const int beforeCredits = s.economy.credits, refund = runtimeBuildingSaleRefund(type, s.rules.getRefundPercent());
    int uiCommands = 0;
    s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
        if (!std::holds_alternative<RuntimeMultiplayerGameplaySellCommand>(command.body)) { reason = "expected actual Sell intent"; return false; }
        ++uiCommands; return dispatchRuntimeOfflineGameplayCommand(s, command, reason);
    });
    stage = "actual owner UI Sell";
    if (!dispatchRuntimeStructureToolFromUi(s, RuntimeSidebarTool::Sell, hutId) || uiCommands != 1) return fail("original owner Sell rejected or captured repeatedly");
    const auto verify = [&](const RuntimeWindowState& q) {
        const auto* sold = findRuntimeObjectByIdIncludingInactive(q, hutId);
        return sold && !sold->alive && sold->health == 2000 && sold->mission == "Sold" && sold->owner == "Americans"
            && q.world.getObjects().size() == 12 && q.economy.credits == beforeCredits + refund
            && spanRecords(q) == remainingSpans && terrainRecords(q) == originalTerrain && hutRecords(q) == otherHuts;
    };
    if (!verify(s)) return fail("sale changed another descriptor/hut/native bridge or retained sold service");
    stage = "strict same-seed checkpoint";
    std::ostringstream ordinary, full, normalized; IniFile base, extension;
    if (!writeRuntimeStateToStream(s, ordinary, error) || !base.loadFromString(ordinary.str())
        || base.getValue("RuntimeSave", "Version", "") != "14") return fail("ordinary14 writer: " + error);
    full << ordinary.str();
    if (!writeRuntimeMultiplayerSnapshotExtension(s, full, error) || !extension.loadFromString(full.str())
        || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension, normalized, error) || !extension.loadFromString(normalized.str())) return fail("canonical extension writer: " + error);
    for (const auto& file : {std::pair<const char*, std::string>{"actual-all02-CABHUT-sold-ordinary14.ini", ordinary.str()},
        {"actual-all02-CABHUT-sold-canonical-extension.ini", normalized.str()}}) {
        std::ofstream artifact(output / file.first, std::ios::binary); artifact << file.second;
        if (!artifact) return fail("checkpoint artifact unavailable");
    }
    for (bool exact : {false, true}) {
        auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false); RuntimeSnapshotActiveStateScope restore(*copy);
        if (!materializeRuntimeLoadStateFromIni(*copy, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*copy, extension, error))) return fail("strict restore: " + error);
        if (copy->tick != s.tick || copy->config.fixedTickHz != 30 || !verify(*copy)) return fail("strict restore changed registry/native bridge/Sold/refund");
        checkpoints << "actual-all02-CABHUT," << (exact ? "ordinary14+canonical-extension" : "ordinary14") << ',' << copy->tick
            << ",30," << hutId << ',' << copy->bridgeRepairSpansByHut.size() << ',' << copy->world.getTerrainCells().size() << ",PASS\n";
    }
    cases << "actual-all02-CABHUT," << hutId << ",129,148,2000,12,12,11," << s.world.getTerrainCells().size() << ',' << refund << ',' << uiCommands << ",PASS\n";
    std::cout << "Sell boundary passed: 1 actual all02 owner UI sale, 12->11 natural hut descriptors, all native terrain/11 other huts exact, 1 ordinary14 + 1 canonical extension strict same-seed restores\n";
    return true;
}
