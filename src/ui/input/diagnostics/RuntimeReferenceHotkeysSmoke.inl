bool runRuntimeReferenceHotkeysSmokeProbe(std::string& error) {
    const auto fail = [&](const std::string& why) { error = "reference hotkeys: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
    s.economy.owner = "alpha"; s.visionEnabled = false;
    s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
    s.runtimePreferencePersistenceEnabled = false; s.config.fixedTickHz = 30;
    s.config.keyBindings = {{"StopObject",'W'}, {"SetPrimaryFactory",'U'}, {"Follow",'H'}};
    s.config.resourceRootPath = "out/reference_hotkeys/no_resources";
    s.config.localDataRootPath = "out/reference_hotkeys/no_resources";
    IniFile ini;
    if (!ini.loadFromString("[VehicleTypes]\n0=HKUNIT\n[InfantryTypes]\n0=HKINF\n"
        "[BuildingTypes]\n0=HKBASE\n[HKUNIT]\nStrength=200\nSpeed=6\nPrimary=HKGUN\n"
        "[HKINF]\nStrength=100\nSpeed=5\nPrimary=HKGUN\n[HKBASE]\nStrength=1000\nConstructionYard=yes\n"
        "[HKGUN]\nDamage=5\nRange=4\nROF=30\nProjectile=Invisible\nWarhead=SA\n")
        || !s.rules.loadFromIni(ini)) return fail("fixture rules");
    s.rulesLoaded = true;
    for (int y=0;y<40;++y) for (int x=0;x<40;++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        s.world.accessTerrainCells().push_back(cell);
        s.terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
    }
    s.terrainByCell = buildRuntimeTerrainLookup(s.world);
    const auto add = [&](int id, const std::string& owner, WorldObjectKind kind, int health) {
        WorldObjectInstance object;
        object.instanceId=id; object.kind=kind; object.owner=owner;
        object.type=kind==WorldObjectKind::Structure ? "HKBASE" : kind==WorldObjectKind::Infantry ? "HKINF" : "HKUNIT";
        object.x=10+id%5; object.y=10; object.cell=runtimeCellKey(object.x,object.y);
        object.health=health; object.alive=true; object.mission="Guard"; object.poweredOperational=true;
        s.world.accessObjects().push_back(object);
    };
    add(1,"alpha",WorldObjectKind::Unit,200); add(2,"alpha",WorldObjectKind::Unit,100);
    add(3,"alpha",WorldObjectKind::Unit,20); add(4,"alpha",WorldObjectKind::Infantry,100);
    add(10,"alpha",WorldObjectKind::Structure,1000); add(20,"bravo",WorldObjectKind::Unit,200);
    add(30,"charlie",WorldObjectKind::Unit,200);
    for (const auto* owner : {"alpha","bravo","charlie"}) {
        RuntimeHumanHouseAuthority authority; authority.owner=authority.canonicalOwner=owner;
        authority.localFacade=authority.owner=="alpha"; s.humanHouseAuthorities.push_back(authority);
    }
    const RECT client{0,0,1000,700};
    ensureRuntimeMenuKeyboardLoaded(s);
    std::set<int> used;
    for (const auto& [id,code] : s.runtimeMenuKeyboardBindings)
        if (!used.insert(code).second) return fail("duplicate default binding: " + id);
    if (runtimeMenuKeyboardBindingCode(s,"StopObject",0)!='S'
        || runtimeMenuKeyboardBindingCode(s,"SetPrimaryFactory",0)!=('U'|runtimePlayerControlKey)
        || runtimeMenuKeyboardBindingCode(s,"Follow",0)!='F' || !s.config.keyboardReferenceLayout)
        return fail("older conflicting preferences overrode reference");
    std::uint64_t now=1000;
    const auto key = [&](int code, bool repeat=false) {
        now+=700; return handleRuntimePlayerCommandCode(s,client,code,repeat,now);
    };
    const auto worldBefore = s.world.deterministicSimulationStateHash();
    for (const auto& [code,tab] : std::vector<std::pair<int,RuntimeBuildKind>>{
            {'Q',RuntimeBuildKind::Building},{'W',RuntimeBuildKind::Defense},
            {'E',RuntimeBuildKind::Infantry},{'R',RuntimeBuildKind::Unit}})
        if (!key(code) || s.activeBuildTab!=tab) return fail("production tab");
    if (!key('K') || s.activeSidebarTool!=RuntimeSidebarTool::Repair
        || !key('K',true) || s.activeSidebarTool!=RuntimeSidebarTool::Repair
        || !key('L') || s.activeSidebarTool!=RuntimeSidebarTool::Sell
        || !key('L') || s.activeSidebarTool!=RuntimeSidebarTool::None) return fail("repair/sell toggle or repeat");
    s.selectedObjectIds={1,2};
    if (!key('1'|runtimePlayerControlKey)) return fail("team creation");
    s.selectedObjectIds.clear();
    if (!key(VK_NUMPAD1) || s.selectedObjectIds!=std::vector<int>{1,2}) return fail("keypad team selection");
    s.selectedObjectIds={3}; key('1'|runtimePlayerShiftKey);
    if (s.selectedObjectIds!=std::vector<int>{1,2,3}) return fail("additive team selection");
    key('F');
    if (!s.playerLocalCommands.cameraFollow || s.playerLocalCommands.followTargeting) return fail("F must follow camera");
    key('H'); if (s.playerLocalCommands.cameraFollow || !s.camera.initialized) return fail("H must leave follow and center base");
    key('O'); if (!s.playerLocalCommands.showCoordinates) return fail("coordinates");
    key('Z'); if (!s.playerLocalCommands.planning) return fail("planning"); key('Z');
    key(VK_F1|runtimePlayerControlKey);
    if (!s.playerLocalCommands.views[0]) return fail("view bookmark");
    key('F'); key(VK_F1);
    if (s.playerLocalCommands.cameraFollow) return fail("view recall did not override camera following");
    if (s.world.deterministicSimulationStateHash()!=worldBefore) return fail("local keys mutated the world");
    {
        RuntimePlayerLocalCommands selection;
        const std::vector<RuntimePlayerSelectionCandidate> candidates{
            {1,"TANK",0,true,true,200,200}, {2,"TANK",1,true,true,50,100},
            {3,"TANK",2,true,true,30,200}, {4,"TANK",0,true,false,100,100},
            {5,"MINER",0,false,true,100,100}};
        if (selection.quickSelect(RuntimePlayerQuickSelection::Health,{},candidates,false,false)!=std::vector<int>{1,5}
            || selection.quickSelect(RuntimePlayerQuickSelection::Health,{},candidates,true,false,true)!=std::vector<int>{1,4,5}
            || selection.quickSelect(RuntimePlayerQuickSelection::Health,{},candidates,false,false)!=std::vector<int>{2}
            || selection.quickSelect(RuntimePlayerQuickSelection::Health,{},candidates,false,false)!=std::vector<int>{3})
            return fail("health fraction cycle or double-press scope");
        if (selection.quickSelect(RuntimePlayerQuickSelection::Veterancy,{},candidates,false,false)!=std::vector<int>{1,5}
            || selection.quickSelect(RuntimePlayerQuickSelection::Veterancy,{},candidates,true,false,true)!=std::vector<int>{1,4,5})
            return fail("double Y changed rank instead of expanding scope");
        if (selection.quickSelect(RuntimePlayerQuickSelection::Type,{1},candidates,false,false)!=std::vector<int>{1,2,3}
            || selection.quickSelect(RuntimePlayerQuickSelection::Combatant,{},candidates,true,false)!=std::vector<int>{1,2,3,4})
            return fail("T/P selection eligibility");
    }
    HWND window=CreateWindowExW(0,L"STATIC",L"Reference hotkey smoke",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!window) return fail("hidden input fixture");
    struct HiddenWindow { HWND window; ~HiddenWindow(){ DestroyWindow(window); } } hidden{window};
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
    key(VK_RETURN);
    runtimeWindowProc(window,WM_KEYDOWN,'S',0);
    runtimeWindowProc(window,WM_CHAR,L'中',0); runtimeWindowProc(window,WM_CHAR,L'文',0);
    if (!s.playerCommunication.chatOpen || s.playerCommunication.chatDraft!=u"中文"
        || s.world.deterministicSimulationStateHash()!=worldBefore) return fail("chat leaked gameplay keys or lost Unicode");
    runtimeWindowProc(window,WM_KEYDOWN,VK_RETURN,0);
    if (s.playerCommunication.chatOpen || s.playerCommunication.lines.empty()
        || s.playerCommunication.lines.back().text!="中文") return fail("chat submit");
    key(VK_HOME); if (!s.playerCommunication.chatAllies) return fail("Home allied chat");
    handleRuntimePlayerModalKey(s,VK_ESCAPE,false);
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind=RuntimePlayerCommunicationKind::ChatAllies; command.text="private test";
    const auto lines=s.playerCommunication.lines.size();
    applyRuntimePlayerCommunication(s,"bravo",command);
    if (s.playerCommunication.lines.size()!=lines) return fail("enemy saw allied chat");
    command.kind=RuntimePlayerCommunicationKind::Alliance; command.text="bravo"; command.value=1;
    if (!validateRuntimePlayerCommunication(s,"alpha",command,error)
        || !applyRuntimePlayerCommunication(s,"alpha",command)
        || runtimeOwnersAreAllied(s,"alpha","bravo")) return fail("one-sided human alliance");
    command.text="alpha"; applyRuntimePlayerCommunication(s,"bravo",command);
    if (!runtimeOwnersAreAllied(s,"alpha","bravo")) return fail("mutual alliance");
    const auto saved=s.playerCommands.serialize(); RuntimePlayerCommandState restored;
    if (!restored.restore(saved,error) || restored.stateHash()!=s.playerCommands.stateHash()) return fail("alliance save/hash");
    RuntimePlayerCommandState legacy;
    if (!legacy.restore("1 0 0 0 0",error) || !legacy.alliances.empty()) return fail("legacy command save");
    s.config.gameMode="FreeForAll";
    if (validateRuntimePlayerCommunication(s,"bravo",command,error)) return fail("FFA diplomacy restriction");
    s.config.gameMode="Standard";
    command={}; command.kind=RuntimePlayerCommunicationKind::PlaceBeacon; command.cell={12,12};
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command) || s.playerCommunication.beacons.size()!=1) return fail("place beacon");
    command.kind=RuntimePlayerCommunicationKind::DeleteBeacon; applyRuntimePlayerCommunication(s,"bravo",command);
    if (s.playerCommunication.beacons.empty()) return fail("remote house deleted another house beacon");
    dispatchRuntimePlayerCommunicationFromUi(s,command);
    if (!s.playerCommunication.beacons.empty()) return fail("delete own beacon");
    key('B'); if (!s.playerCommunication.beaconTargeting) return fail("B targeting");
    runtimeWindowProc(window,WM_KEYDOWN,VK_ESCAPE,0);
    if (s.playerCommunication.beaconTargeting || s.runtimeMenuOpen) return fail("Escape targeting cancellation");
    key('C');
    if (!s.playerCommunication.cheerUntilTick.contains(4)
        || s.world.deterministicSimulationStateHash()!=worldBefore) return fail("cheer changed orders");
    const auto previous=s.playerCommunication.lines.size(); key(VK_F9); const auto after=s.playerCommunication.lines.size();
    key(VK_F9,true); if (after<=previous || s.playerCommunication.lines.size()!=after) return fail("F9 taunt/repeat routing");
    key(VK_TAB); if (!s.playerCommunication.diplomacyOpen) return fail("Tab diplomacy");
    handleRuntimePlayerModalKey(s,VK_TAB,false);
    RuntimeMultiplayerRuntimeBridge bridge; s.multiplayerBridge=&bridge;
    RuntimeConfig::PlayerSlot slot; slot.enabled=slot.human=slot.local=true; s.config.playerSlots={slot};
    std::vector<RuntimeMultiplayerGameplayCommand> captured;
    s.multiplayerIntentRouter.setCaptureCallback([&](const auto& c,std::string& e){captured.push_back(c);e.clear();return true;});
    command={}; command.kind=RuntimePlayerCommunicationKind::PlaceBeacon; command.cell={13,13};
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command) || captured.size()!=1
        || !s.playerCommunication.beacons.empty()) return fail("multiplayer bypassed authoritative capture");
    if (!prevalidateRuntimeMultiplayerGameplayCommand(s,"alpha",captured.front(),error)
        || !applyRuntimeMultiplayerGameplayCommand(s,"alpha",captured.front())
        || s.playerCommunication.beacons.size()!=1) return fail("multiplayer authoritative communication");
    s.multiplayerBridge=nullptr;
    if (const char* path=std::getenv("CONCORD_REFERENCE_HOTKEYS_BMP")) {
        RuntimeBackBuffer buffer; HDC screen=GetDC(nullptr);
        if (!ensureRuntimeBackBuffer(buffer,screen,1000,700)) { ReleaseDC(nullptr,screen); return fail("overlay buffer"); }
        ReleaseDC(nullptr,screen);
        s.playerCommunication.diplomacyOpen=true;
        s.playerCommunication.chatOpen=true; s.playerCommunication.chatAllies=true;
        s.playerCommunication.chatDraft=u"在这里输入消息，Enter 发送，Esc 取消";
        drawRuntimePlayerHotkeyOverlay(buffer.memoryDc,client,s);
        GdiFlush(); // Complete GDI batches before reading the DIB pixels.
        const bool written=writeRuntimeSmokeBmp(path,buffer.pixels,1000,700,error);
        releaseRuntimeBackBuffer(buffer); if (!written) return false;
    }
    if (!runRuntimePlayerCheerSequenceCheck(error)) return false;
    error.clear();
    std::cout << "Reference hotkeys: migration, QWER/KL/H/F/O/Z, teams/keypad, T/P/Y/U, views, Unicode/modal input, diplomacy, beacons, cheer/taunts, save/hash and multiplayer authority PASS\n";
    return true;
}
