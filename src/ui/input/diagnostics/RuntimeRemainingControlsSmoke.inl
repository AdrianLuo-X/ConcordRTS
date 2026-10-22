bool runRuntimeRemainingControlsSmoke(const RuntimeConfig& config,std::string& error) {
    const auto fail=[&](const std::string& why){error="Remaining controls: "+why;return false;};
    const char* output=std::getenv("CONCORD_CONTROLS_OUTPUT");
    const std::filesystem::path root=output?output:"build/smoke/remaining-controls";
    std::filesystem::create_directories(root);
    auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
    s.config=config;s.config.computerAiEnabled=false;s.config.playerSlots.clear();s.config.gameMode="Battle";
    s.config.gameModeRulesOverride.clear();s.config.crates=false;
    s.config.configPath=(root/"preferences.ini").string();s.config.keyboardReferenceLayout=true;s.config.keyBindings.clear();
    s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
    s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
    if (!s.report.success) return fail("original resources missing");
    s.rulesLoaded=s.report.rulesLoaded;s.artLoaded=s.report.artLoaded;s.theaterLoaded=s.report.theaterLoaded;s.mapFactsLoaded=true;
    s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.visionEnabled=false;s.triggers.bootstrapComplete=true;
    s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessTerrainCells().clear();s.world.accessSmudges().clear();
    for (int y=0;y<128;++y) for (int x=0;x<128;++x) {
        TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=0;cell.subTileIndex=0;
        s.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(s);
    const auto add=[&](int id,const std::string& type,const std::string& owner,WorldObjectKind kind,bool alive=true) {
        WorldObjectInstance object;object.instanceId=id;object.type=type;object.kind=kind;object.owner=owner;
        object.x=76+id%8;object.y=80;object.cell=runtimeCellKey(object.x,object.y);object.health=200;
        object.alive=alive;object.mission="Guard";s.world.accessObjects().push_back(object);
    };
    add(1,"MTNK","Americans",WorldObjectKind::Unit);add(2,"E1","Americans",WorldObjectKind::Infantry);
    add(3,"CMIN","Americans",WorldObjectKind::Unit);add(5,"GAPOWR","Americans",WorldObjectKind::Structure);
    add(6,"MTNK","Americans",WorldObjectKind::Unit,false);add(7,"HTNK","Russians",WorldObjectKind::Unit);
    ensureRuntimeSubcellsInitialized(s);
    ensureRuntimeMenuKeyboardLoaded(s);
    const RECT client{0,0,800,600};std::uint64_t now=1000;
    const auto key=[&](int code,bool repeated=false){now+=700;return handleRuntimePlayerCommandCode(s,client,code,repeated,now);};
    const auto worldHash=s.world.deterministicSimulationStateHash();
    if (!key('N') || s.selectedObjectIds!=std::vector<int>{1}
        || !key('N') || s.selectedObjectIds!=std::vector<int>{2}
        || !key('N') || s.selectedObjectIds!=std::vector<int>{3}
        || !key('N') || s.selectedObjectIds!=std::vector<int>{1}
        || !key('M') || s.selectedObjectIds!=std::vector<int>{3}
        || !key('N',true) || s.selectedObjectIds!=std::vector<int>{3}) return fail("M/N ordering, wrap or eligibility");
    const auto indexOf=[&](const std::string& id) {
        for (std::size_t i=0;i<s.runtimeMenuKeyboardBindings.size();++i)
            if (s.runtimeMenuKeyboardBindings[i].first==id) return static_cast<int>(i);
        return -1;
    };
    if (!assignRuntimeMenuKeyboardBinding(s,indexOf("NextObject"),'V',false)
        || key('N') || !key('V') || s.selectedObjectIds!=std::vector<int>{1}) return fail("rebound cycling ignored");
    if (!s.camera.initialized || s.world.deterministicSimulationStateHash()!=worldHash) return fail("cycling moved objects");
    for (std::size_t i=0;i<s.runtimeMenuKeyboardBindings.size();++i) s.runtimeMenuKeyboardBindings[i].second=runtimeKeyboardDefaultAt(static_cast<int>(i));
    setRuntimeMenuOpen(s,true);activateRuntimeMenuAction(nullptr,s,RuntimeMenuAction::OpenKeyboard);
    std::set<int> visited;
    for (int category=0;category<5;++category) {
        s.runtimeMenuKeyboardCategory=category;s.runtimeMenuKeyboardPage=0;
        const int pages=runtimeKeyboardCategoryPages(s);
        for (int page=0;page<pages;++page) {
            s.runtimeMenuKeyboardPage=page;
            for (const auto& control:runtimeMenuContentControls(s,client)) {
                const int index=runtimeKeyboardBindingIndexForAction(control.action);
                if (index<0) continue;
                if (!visited.insert(index).second || control.label.empty()
                    || control.label==widenUtf8(s.runtimeMenuKeyboardBindings[index].first)) return fail("missing/localization/duplicate command row");
                activateRuntimeMenuAction(nullptr,s,control.action);
                if (s.runtimeMenuKeyCaptureIndex!=index || !handleRuntimeMenuKeyCapture(s,VK_F20)
                    || s.runtimeMenuKeyboardBindings[index].second!=VK_F20) return fail("command capture not connected");
                activateRuntimeMenuAction(nullptr,s,control.action);
                if (!handleRuntimeMenuKeyCapture(s,VK_BACK)
                    || s.runtimeMenuKeyboardBindings[index].second!=runtimeKeyboardDefaultAt(index)) return fail("default restoration outside first six rows");
            }
        }
    }
    if (visited.size()!=s.runtimeMenuKeyboardBindings.size() || visited.size()!=88) return fail("incomplete categorized catalog");
    const int next=indexOf("NextObject"),previous=indexOf("PreviousObject");
    if (!assignRuntimeMenuKeyboardBinding(s,next,'M',false)
        || s.runtimeMenuKeyboardBindings[previous].second!='N') return fail("conflicting assignment not swapped");
    activateRuntimeMenuAction(nullptr,s,RuntimeMenuAction::KeyboardResetAll);
    if (s.runtimeMenuKeyboardBindings[next].second!='N' || s.runtimeMenuKeyboardBindings[previous].second!='M') return fail("reset-all incomplete");
    s.runtimeMenuKeyboardCategory=2;s.runtimeMenuKeyboardPage=0;
    activateRuntimeMenuAction(nullptr,s,RuntimeMenuAction::KeyboardPreviousPage);
    if (s.runtimeMenuKeyboardPage!=7) return fail("team page navigation");
    activateRuntimeMenuAction(nullptr,s,RuntimeMenuAction::KeyboardNextPage);
    if (s.runtimeMenuKeyboardPage!=0) return fail("team pages did not wrap");
    assignRuntimeMenuKeyboardBinding(s,next,'V'|runtimePlayerControlKey,false);
    {std::ofstream sentinel(s.config.configPath);sentinel<<"[Preserved]\nSentinel=keep\n";}
    s.runtimePreferencePersistenceEnabled=true;
    if (!persistRuntimeMenuPreferences(s)) return fail("preference save");
    s.runtimePreferencePersistenceEnabled=false;
    RuntimeConfigOverrides overrides;overrides.configPath=s.config.configPath;
    overrides.resourceRootPath=config.resourceRootPath;overrides.localDataRootPath=config.localDataRootPath;
    auto restored=std::make_unique<RuntimeWindowState>();restored->config=loadRuntimeConfig(overrides);
    ensureRuntimeMenuKeyboardLoaded(*restored);
    if (restored->runtimeMenuKeyboardBindings!=s.runtimeMenuKeyboardBindings) return fail("full preferences roundtrip");
    IniFile saved;if (!saved.loadFromFile(s.config.configPath) || saved.getValue("Preserved","Sentinel")!="keep") return fail("unrelated preferences changed");
    RuntimeConfig copied;copyRuntimePlayerPreferences(s.config,copied);
    if (!copied.keyboardReferenceLayout || copied.keyBindings!=s.config.keyBindings) return fail("new match discarded customized layout");
    restored->config.keyBindings={{"StopObject",'M'}};restored->runtimeMenuKeyboardLoadAttempted=false;
    ensureRuntimeMenuKeyboardLoaded(*restored);
    std::set<int> used;
    for (const auto& binding:restored->runtimeMenuKeyboardBindings) if (!used.insert(binding.second).second) return fail("new M/N default collided with saved key");
    if (runtimeMenuKeyboardBindingCode(*restored,"StopObject",0)!='M') return fail("old custom key lost");

    RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);
    const bool buffered=ensureRuntimeBackBuffer(buffer,screen,800,600);ReleaseDC(nullptr,screen);
    if (!buffered) return fail("native image buffer");
    struct BufferCleanup {RuntimeBackBuffer& buffer;~BufferCleanup(){releaseRuntimeBackBuffer(buffer);}} cleanup{buffer};
    const auto write=[&](const std::string& name) {GdiFlush();return writeRuntimeSmokeBmp((root/(name+".bmp")).string(),buffer.pixels,800,600,error);};
    for (int category=0;category<5;++category) {
        s.runtimeMenuKeyboardCategory=category;s.runtimeMenuKeyboardPage=0;s.runtimeMenuStatus=L"选命令改键 · Esc取消 · Backspace默认";
        drawRuntimeMenuOverlay(buffer.memoryDc,client,s);
        if (!write("keyboard-"+std::to_string(category))) return false;
    }
    setRuntimeMenuOpen(s,false);
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind=RuntimePlayerCommunicationKind::PlaceBeacon;command.cell={80,80};
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command)) return fail("beacon placement");
    centerRuntimeCameraOnCell(s,client,{80,80});
    const RECT viewport=runtimeMapViewport(client);
    const POINT beaconPoint=runtimeScreenPointForCell(s,viewport,{80,80},0);
    if (!handleRuntimePlayerTargetClick(s,viewport,beaconPoint) || !s.playerCommunication.selectedBeacon)
        return fail("beacon cannot be selected by mouse");
    key(VK_RETURN);
    if (!s.playerCommunication.chatOpen || !s.playerCommunication.beaconEditing) return fail("Enter did not edit selected marker");
    HWND window=CreateWindowExW(0,L"STATIC",L"Controls input check",WS_POPUP,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!window) return fail("input window");
    struct WindowCleanup {HWND window;~WindowCleanup(){DestroyWindow(window);}} windowCleanup{window};
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
    runtimeWindowProc(window,WM_KEYDOWN,'S',0);
    for (wchar_t character:std::wstring(L"从这里进攻")) runtimeWindowProc(window,WM_CHAR,character,0);
    runtimeWindowProc(window,WM_KEYDOWN,VK_RETURN,0);
    if (s.playerCommunication.chatOpen || !runtimePlayerSelectedBeacon(s)
        || runtimePlayerSelectedBeacon(s)->text!="从这里进攻"
        || s.world.deterministicSimulationStateHash()!=worldHash) return fail("Unicode annotation leaked orders or failed");
    key(VK_RETURN);s.playerCommunication.chatDraft=u"取消的文字";handleRuntimePlayerModalKey(s,VK_ESCAPE,false);
    if (runtimePlayerSelectedBeacon(s)->text!="从这里进攻") return fail("cancel changed marker");
    command.kind=RuntimePlayerCommunicationKind::BeaconText;command.text="不能更改他人的信标";
    if (!applyRuntimePlayerCommunication(s,"Russians",command) || runtimePlayerSelectedBeacon(s)->text!="从这里进攻") return fail("foreign annotation changed owner marker");
    // Reuse the actual intent and authoritative receive paths for the annotation.
    RuntimeMultiplayerRuntimeBridge bridge;s.multiplayerBridge=&bridge;
    s.humanHouseAuthorities.clear();
    RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
    s.humanHouseAuthorities.push_back(authority);
    RuntimeConfig::PlayerSlot slot;slot.enabled=slot.human=slot.local=true;slot.country="Americans";s.config.playerSlots={slot};
    std::vector<RuntimeMultiplayerGameplayCommand> captured;
    s.multiplayerIntentRouter.setCaptureCallback([&](const auto& value,std::string& problem){captured.push_back(value);problem.clear();return true;});
    command.text="队友从这里进攻";
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command) || captured.size()!=1 || runtimePlayerSelectedBeacon(s)->text!="从这里进攻") return fail("annotation bypassed input queue");
    if (!prevalidateRuntimeMultiplayerGameplayCommand(s,"Americans",captured.front(),error)
        || !applyRuntimeMultiplayerGameplayCommand(s,"Americans",captured.front())
        || runtimePlayerSelectedBeacon(s)->text!=command.text) return fail("authoritative annotation receive");
    s.multiplayerBridge=nullptr;
    // An allied observer receives the text; an enemy observer receives nothing.
    const auto ownBeacons=s.playerCommunication.beacons;
    s.playerCommunication.beacons.clear();s.economy.owner="French";
    s.playerCommands.alliances[{"french","americans"}]=true;
    s.playerCommands.alliances[{"americans","french"}]=true;
    auto place=command;place.kind=RuntimePlayerCommunicationKind::PlaceBeacon;place.text.clear();
    applyRuntimePlayerCommunication(s,"Americans",place);applyRuntimePlayerCommunication(s,"Americans",command);
    if (s.playerCommunication.beacons.size()!=1 || s.playerCommunication.beacons.front().text!=command.text) return fail("ally did not receive marker text");
    s.playerCommunication.beacons.clear();s.economy.owner="Russians";
    applyRuntimePlayerCommunication(s,"Americans",place);applyRuntimePlayerCommunication(s,"Americans",command);
    if (!s.playerCommunication.beacons.empty()) return fail("enemy received marker text");
    s.economy.owner="Americans";s.playerCommunication.beacons=ownBeacons;
    std::vector<std::uint32_t> pixels;
    if (!drawRuntimeStateOffscreenAtCell(s,{80,80},800,600,error,"",false,false,-1,&pixels,true,&s.camera)) return false;
    std::copy(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(buffer.pixels));drawRuntimePlayerHotkeyOverlay(buffer.memoryDc,client,s);
    if (!write("beacon-text")) return false;
    key(VK_RETURN);s.playerCommunication.chatDraft.clear();handleRuntimePlayerModalKey(s,VK_RETURN,false);
    if (!runtimePlayerSelectedBeacon(s)->text.empty()) return fail("cannot clear annotation");
    command.kind=RuntimePlayerCommunicationKind::DeleteBeacon;command.text.clear();
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command) || runtimePlayerSelectedBeacon(s)) return fail("selected marker deletion");
    command.kind=RuntimePlayerCommunicationKind::BeaconText;command.text="expired";
    if (!dispatchRuntimePlayerCommunicationFromUi(s,command) || !s.playerCommunication.beacons.empty()) return fail("editing recreated removed marker");
    if (!runRuntimeReferenceHotkeysSmokeProbe(error)) return false;
    std::cout<<"Remaining controls: 88 categorized commands, capture/reset/persistence, M/N cycling, Unicode beacon annotations and multiplayer authority passed.\n";
    return true;
}
