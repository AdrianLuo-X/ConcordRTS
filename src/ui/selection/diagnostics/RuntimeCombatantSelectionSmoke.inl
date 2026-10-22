bool runRuntimeCombatantSelectionSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Combatant selection: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    state.config = config; state.config.gameMode = "Battle"; state.config.computerAiEnabled = false;
    state.config.keyboardReferenceLayout = true; state.config.keyBindings.clear();
    state.runtimeSuppressPlayerAudioFeedback = true; state.runtimeSoundsEnabled = false;
    state.runtimePreferencePersistenceEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world, &state.theater,
        &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("stock resources unavailable");
    state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
    state.economy.owner = "Americans"; state.visionEnabled = false;
    for (const auto* id : {"MTNK", "E1"}) {
        const auto* unit = findRuntimeUnitTypeById(state.rules, id);
        if (!unit || !unit->isSelectableCombatant) return fail(std::string(id) + " stock combatant flag");
    }
    for (const auto* id : {"SPY", "HARV", "CMIN", "SMIN", "AMCV", "ENGINEER"}) {
        const auto* unit = findRuntimeUnitTypeById(state.rules, id);
        if (!unit || unit->isSelectableCombatant) return fail(std::string(id) + " incorrectly opted into P");
    }
    // Keep this small synthetic scene inside the loaded map's camera bounds,
    // including the expanded default map. The authored LocalSize is retained.
    const int origin = std::max(0, state.world.getSummary().width / 2 - 40);
    state.world.accessObjects().clear(); state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear(); state.world.accessSmudges().clear();
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x + origin; cell.y = y + origin;
        cell.cellIndex512 = cell.y * 512 + cell.x;
        cell.tileIndex = cell.subTileIndex = 0; state.world.accessTerrainCells().push_back(cell);
    }
    const auto add = [&](int id, const std::string& type, int x, int y, const std::string& owner = "Americans") {
        const auto* unit = findRuntimeUnitTypeById(state.rules, type);
        WorldObjectInstance object;
        object.instanceId = id; object.type = type; object.owner = owner;
        object.kind = runtimeKindForBuildItem(runtimeBuildKindForUnitTypeId(state.rules, type));
        object.x = x + origin; object.y = y + origin; object.cell = runtimeCellKey(object.x, object.y);
        object.health = unit ? unit->strength : 100; object.mission = "Guard";
        state.world.accessObjects().push_back(object);
    };
    add(1,"MTNK",40,40); add(2,"E1",42,42); add(3,"SPY",38,40);
    add(4,"HARV",39,36); add(5,"CMIN",35,44); add(6,"SMIN",43,37);
    add(7,"AMCV",46,43); add(8,"ENGINEER",43,40); add(9,"MTNK",100,10);
    add(10,"MTNK",45,42,"Russians"); add(11,"MTNK",40,45);
    state.world.accessObjects().back().alive = false; state.world.accessObjects().back().health = 0;
    add(12,"E1",40,45); state.world.accessObjects().back().alive = false;
    state.world.accessObjects().back().mission = "InsideTransport";
    add(13,"V3ROCKET",40,47); add(15,"CMIN",100,15);
    initializeRuntimeNavigation(state); ensureRuntimeMenuKeyboardLoaded(state);
    const RECT client{0,0,1000,700}; const RECT viewport = runtimeMapViewport(client);
    centerRuntimeCameraOnCell(state, client, {41+origin,41+origin});
    const auto worldHash = state.world.deterministicSimulationStateHash();
    if (!handleRuntimePlayerCommandCode(state,client,'P',false,1000)
        || state.selectedObjectIds != std::vector<int>{1,2}) return fail("single P screen filter");
    if (!handleRuntimePlayerCommandCode(state,client,'P',false,1200)
        || state.selectedObjectIds != std::vector<int>{1,2,9}) return fail("double P map filter");
    const auto allCombatants = state.selectedObjectIds;
    if (!handleRuntimePlayerCommandCode(state,client,'P',true,1300)
        || state.selectedObjectIds != allCombatants) return fail("key repeat changed selection");
    state.selectedObjectIds = {5};
    handleRuntimePlayerCommandCode(state,client,'T',false,2000);
    handleRuntimePlayerCommandCode(state,client,'T',false,2200);
    if (state.selectedObjectIds != std::vector<int>{5,15}) return fail("unarmed miner type selection");
    for (int code : {'Y','U'}) {
        handleRuntimePlayerCommandCode(state,client,code,false,3000 + (code == 'U' ? 1000 : 0));
        handleRuntimePlayerCommandCode(state,client,code,false,3200 + (code == 'U' ? 1000 : 0));
        if (state.selectedObjectIds != std::vector<int>{1,2,3,4,5,6,7,8,9,15})
            return fail("rank/health inherited P restriction or selected a missile");
    }
    state.selectedObjectIds.clear();
    std::uint64_t now = 5000;
    for (int id : {1,2,3,4,5,6,7,8,9,15,1}) {
        if (!handleRuntimePlayerCommandCode(state,client,'N',false,now += 700)
            || state.selectedObjectIds != std::vector<int>{id}) return fail("N excluded a normal noncombatant");
    }
    if (!handleRuntimePlayerCommandCode(state,client,'M',false,now += 700)
        || state.selectedObjectIds != std::vector<int>{15}) return fail("M excluded a miner");
    centerRuntimeCameraOnCell(state,client,{41+origin,41+origin});
    const auto& projected = runtimeProjectedObjectsForViewport(state,viewport);
    const auto miner = std::find_if(projected.begin(),projected.end(),[](const auto& view) {
        return view.object && view.object->instanceId == 5;
    });
    if (miner == projected.end()) return fail("miner projection unavailable");
    const RECT bounds = miner->bounds;
    const POINT point{(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2};
    const auto* picked = findProjectedObjectAtPoint(projected,point);
    if (!picked || !picked->object || picked->object->instanceId != 5) return fail("miner mouse fixture overlap");
    HWND window = CreateWindowExW(0,L"STATIC",L"Selection check",WS_POPUP,0,0,1000,700,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!window) return fail("selection input window");
    struct Cleanup { HWND window; ~Cleanup(){DestroyWindow(window);} } cleanup{window};
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    state.selectedObjectIds.clear(); state.leftPointer.active = true; state.leftPointer.moved = false;
    runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
    if (state.selectedObjectIds != std::vector<int>{5}) return fail("normal miner mouse click rejected");
    state.selectedObjectIds.clear(); state.leftPointer.active = state.leftPointer.moved = true;
    state.leftPointer.start = {bounds.left,bounds.top}; state.leftPointer.current = {bounds.right,bounds.bottom};
    runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(bounds.right,bounds.bottom));
    if (std::find(state.selectedObjectIds.begin(),state.selectedObjectIds.end(),5) == state.selectedObjectIds.end())
        return fail("miner box selection rejected");
    if (state.world.deterministicSimulationStateHash() != worldHash) return fail("selection changed simulation state");
    IniFile yesIni, noIni;
    const std::string rules = "[VehicleTypes]\n0=TEST\n[TEST]\nStrength=100\nSpeed=5\n";
    yesIni.loadFromString(rules + "IsSelectableCombatant=yes\n"); noIni.loadFromString(rules);
    RulesDatabase yesRules,noRules; yesRules.loadFromIni(yesIni); noRules.loadFromIni(noIni);
    const auto* optIn = yesRules.findUnitType("UnitType","TEST");
    const auto* optOut = noRules.findUnitType("UnitType","TEST");
    if (!optIn || !optOut || !optIn->isSelectableCombatant || optOut->isSelectableCombatant
        || !optIn->primaryWeapon.empty() || yesRules.getDetailRulesHash() == noRules.getDetailRulesHash())
        return fail("unarmed opt-in/default/rules fingerprint");
    RuntimePlayerLocalCommands local;
    const std::vector<RuntimePlayerSelectionCandidate> custom{{1,"TEST",0,optIn->isSelectableCombatant,true,100,100}};
    if (local.quickSelect(RuntimePlayerQuickSelection::Combatant,{},custom,false,false) != std::vector<int>{1})
        return fail("unarmed flagged unit not selected");
    std::cout << "Combatant selection: stock flag/default=yes screen/map P=yes custom unarmed opt-in=yes "
        "T/Y/U/M/N independent=yes miner mouse/box=yes hidden/enemy/missile excluded=yes local-only=yes\n";
    error.clear(); return true;
}
