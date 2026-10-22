// W012-01: complete frozen native WndProc, same-CPP private-host route,
// and actual production WndProc. Only diagnostic observers were added to old.
#include "ui/input/diagnostics/RuntimeInputCancellationOld.inl"
#include "ui/input/diagnostics/RuntimeInputCancellationDirect.inl"

namespace RuntimeInputCancellationSmokeDetail {
using RuntimeCursorSmokeDetail::BorrowResources;
using RuntimeCursorSmokeDetail::snapshot;
using RuntimeCursorSmokeDetail::writeNewBytes;
using RuntimePointerIngressSmokeDetail::hex;
using RuntimePointerIngressSmokeDetail::Clock;
using RuntimePointerIngressSmokeDetail::ClockBinding;
using RuntimePointerIngressSmokeDetail::Witness;
using RuntimePointerIngressSmokeDetail::Command;
inline constexpr const wchar_t* className = L"ConcordInputCancellationSmoke";
struct Window;
thread_local std::map<HWND, Window*> windows;
LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
struct Registration {
    HINSTANCE instance=GetModuleHandleW(nullptr); bool ready=false;
    Registration(){WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=instance;c.lpszClassName=className;ready=RegisterClassW(&c)!=0;}
    bool close(){if(!ready)return true;const BOOL value=UnregisterClassW(className,instance);if(value)ready=false;return value!=FALSE;}
    ~Registration(){if(ready)close();}
};
std::string menuState(const RuntimeWindowState& s) {
    std::ostringstream o;
    o<<s.tick<<';'<<s.leftPointer.active<<','<<s.leftPointer.moved<<','<<s.rightPointer.active<<','<<s.rightPointer.moved
     <<';'<<static_cast<int>(s.runtimeMenuSliderDrag)<<','<<static_cast<int>(s.runtimeMenuPressed)
     <<';'<<s.forceAttackHeld<<','<<s.repaintRequested<<';'<<s.config.gameSpeed<<','<<s.runtimeScrollRate<<','<<s.runtimeVisualDetail
     <<','<<s.runtimeMusicEnabled<<','<<s.runtimeSoundEffectsEnabled<<','<<s.runtimeMusicVolume<<','<<s.runtimeSoundVolume<<','<<s.runtimeVoiceVolume;
    for(wchar_t c:s.runtimeMenuStatus)o<<','<<static_cast<unsigned>(c);
    return o.str();
}
struct Window final:RuntimeInputCancellationDiagnostics {
    RuntimeWindowState& state; int route; const std::string resources;
    HWND handle=nullptr,other=nullptr; bool recording=false,nesting=false;int depth=0,mutation=0;
    std::size_t sequence=0,foreignCaptureObservations=0;std::vector<std::string> events,observations;std::ostream& trace;
    Window(RuntimeWindowState& s,int path,std::string root,std::ostream& stream):state(s),route(path),resources(std::move(root)),trace(stream) {
        handle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,className,L"Input cancellation private contract",
            WS_POPUP,-32000,-32000,1024,768,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(handle){windows.emplace(handle,this);SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));}
        other=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Input cancellation owned capture peer",
            WS_POPUP,-32000,-32000,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    }
    int kind(HWND value)const{return !value?0:value==handle?1:value==other?2:3;}
    std::string handleValue(HWND value, bool canonical) const {
        if (!canonical) return std::to_string(reinterpret_cast<std::uintptr_t>(value));
        if (!value) return "none";
        if (value == handle) return "self";
        if (value == other) return "peer";
        if (value == HWND_TOPMOST) return "constant-topmost";
        if (value == HWND_NOTOPMOST) return "constant-notopmost";
        if (value == HWND_BOTTOM) return "constant-bottom";
        return "unowned:" + std::to_string(reinterpret_cast<std::uintptr_t>(value));
    }
    void rectValue(std::ostream& out, const RECT& rect) const {
        out << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom;
    }
    std::string positionAfterValue(HWND value, bool canonical) const {
        const auto ordinary = handleValue(value, canonical);
        if (!canonical || !value || value == handle || value == other
            || value == HWND_TOPMOST || value == HWND_NOTOPMOST || value == HWND_BOTTOM)
            return ordinary;
        // This only identifies WINDOWPOS.hwndInsertAfter. The raw HWND and
        // complete WINDOWPOS remain in the trace; unqualified values stay raw.
        using DefaultImeWindow = HWND (WINAPI*)(HWND);
        const HMODULE module = GetModuleHandleW(L"imm32.dll");
        const auto query = module ? reinterpret_cast<DefaultImeWindow>(
            GetProcAddress(module, "ImmGetDefaultIMEWnd")) : nullptr;
        const HWND actualDefault = query ? query(handle) : nullptr;
        DWORD pid = 0; const DWORD tid = GetWindowThreadProcessId(value, &pid);
        wchar_t name[256]{}; const int length = GetClassNameW(value, name, 256);
        if (query && actualDefault == value && pid == GetCurrentProcessId()
            && tid == GetCurrentThreadId() && length == 3 && std::wstring(name, 3) == L"IME") {
            trace << "positionIdentity route=" << route << " seq=" << (sequence - 1)
                << " hwnd=" << reinterpret_cast<std::uintptr_t>(handle)
                << " value=" << reinterpret_cast<std::uintptr_t>(value)
                << " queriedDefault=" << reinterpret_cast<std::uintptr_t>(actualDefault)
                << " pid=" << pid << " tid=" << tid
                << " actualPid=" << GetCurrentProcessId() << " actualTid=" << GetCurrentThreadId()
                << " class=" << hex("IME") << " utc=" << RuntimeCursorSmokeDetail::utcNow()
                << " field=WINDOWPOS.hwndInsertAfter qualified=1\n";
            return "own-default-ime:" + std::to_string(pid) + ":" + std::to_string(tid) + ":IME";
        }
        return ordinary;
    }
    void positionValue(std::ostream& out, const WINDOWPOS& position, bool canonical) const {
        out << handleValue(position.hwnd, canonical) << ',' << positionAfterValue(position.hwndInsertAfter, canonical)
            << ',' << position.x << ',' << position.y << ',' << position.cx << ',' << position.cy << ',' << position.flags;
    }
    std::string payload(UINT message, WPARAM raw, LPARAM value, bool canonical) const {
        std::ostringstream out;
        if (message == WM_CAPTURECHANGED || message == WM_ACTIVATE)
            out << "window:" << handleValue(reinterpret_cast<HWND>(value), canonical);
        else if (message == WM_DPICHANGED && value) {
            out << "dpi:"; rectValue(out, *reinterpret_cast<const RECT*>(value));
        } else if ((message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED) && value) {
            out << "position:"; positionValue(out, *reinterpret_cast<const WINDOWPOS*>(value), canonical);
        } else if (message == WM_NCCALCSIZE && value) {
            out << "client:";
            if (raw) {
                const auto& input = *reinterpret_cast<const NCCALCSIZE_PARAMS*>(value);
                for (const auto& rect : input.rgrc) { rectValue(out, rect); out << ';'; }
                if (!canonical) out << "rawPositionPointer=" << reinterpret_cast<std::uintptr_t>(input.lppos) << ';';
                if (input.lppos) positionValue(out, *input.lppos, canonical);
                else out << "null";
            } else rectValue(out, *reinterpret_cast<const RECT*>(value));
        } else if (message == WM_GETMINMAXINFO && value) {
            const auto& limits = *reinterpret_cast<const MINMAXINFO*>(value);
            for (const auto point : {limits.ptReserved, limits.ptMaxSize, limits.ptMaxPosition, limits.ptMinTrackSize, limits.ptMaxTrackSize})
                out << point.x << ',' << point.y << ';';
        } else out << static_cast<std::int64_t>(value);
        return out.str();
    }
    void log(UINT message,WPARAM w,LPARAM l,bool enter) {
        const auto capture=GetCapture();std::ostringstream semantic;
        DWORD capturePid=0;const DWORD captureTid=capture?GetWindowThreadProcessId(capture,&capturePid):0;
        if(capture && (kind(capture)>2 || capturePid!=GetCurrentProcessId() || captureTid!=GetCurrentThreadId()))++foreignCaptureObservations;
        semantic<<sequence++<<','<<depth<<','<<message<<','<<enter<<','<<kind(capture)<<',';
        if(message==WM_KILLFOCUS||message==WM_SETFOCUS||message==WM_SETCURSOR||message==WM_MOUSEACTIVATE)
            semantic<<"window:"<<handleValue(reinterpret_cast<HWND>(w),true);
        else semantic<<static_cast<std::uint64_t>(w);
        semantic<<','<<payload(message,w,l,true);
        semantic<<';'<<snapshot(state)<<";menu-detail="<<menuState(state);
        events.push_back(semantic.str());
        trace<<"event route="<<route<<" seq="<<(sequence-1)<<" depth="<<depth<<" entering="<<enter<<" msg="<<message
            <<" hwnd="<<reinterpret_cast<std::uintptr_t>(handle)<<" capture="<<reinterpret_cast<std::uintptr_t>(capture)
            <<" wParam="<<static_cast<std::uint64_t>(w)<<" lParam="<<static_cast<std::int64_t>(l)
            <<" capturePid="<<capturePid<<" captureTid="<<captureTid<<" rawPayload="<<hex(payload(message,w,l,false))
            <<" semantic="<<hex(semantic.str())<<'\n';
    }
    void observe(const char* operation,RuntimeMenuAction value) override {
        std::ostringstream o;o<<depth<<','<<state.tick<<','<<kind(GetCapture())<<','<<operation<<','<<static_cast<int>(value)<<';'<<menuState(state);
        observations.push_back(o.str());trace<<"observation route="<<route<<" operation="<<hex(o.str())<<'\n';
    }
    LRESULT dispatch(UINT message,WPARAM w,LPARAM l) {
        if(!recording)return DefWindowProcW(handle,message,w,l);
        ++depth;log(message,w,l,true);
        if(message==WM_CAPTURECHANGED && mutation && !nesting){
            const int action=mutation;mutation=0;nesting=true;
            if(action==1)SendMessageW(handle,WM_CANCELMODE,0,0);
            if(action==2)SetWindowPos(handle,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
            if(action==3){SendMessageW(handle,WM_ACTIVATEAPP,FALSE,0);SendMessageW(handle,WM_KILLFOCUS,0,0);}
            if(action==4)SendMessageW(handle,WM_CLOSE,0,0);
            nesting=false;
        }
        LRESULT result=0;
        {BorrowResources borrowed(state,resources);
            if(route==0)result=inputCancellationOldWindowProc(handle,message,w,l);
            else if(route==1)result=inputCancellationDirectWindowProc(handle,message,w,l);
            else result=runtimeWindowProc(handle,message,w,l);}
        log(message,w,l,false);--depth;return result;
    }
    void close(){
        recording=false;
        if(GetCapture()==handle||GetCapture()==other)ReleaseCapture();
        if(handle){if(IsWindow(handle)){SetWindowLongPtrW(handle,GWLP_USERDATA,0);DestroyWindow(handle);}windows.erase(handle);handle=nullptr;}
        if(other){if(IsWindow(other))DestroyWindow(other);other=nullptr;}
    }
    ~Window(){close();}
};
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto found=windows.find(h);return found==windows.end()?DefWindowProcW(h,m,w,l):found->second->dispatch(m,w,l);}
struct ObserverBinding{
    RuntimeInputCancellationDiagnostics* previous;
    explicit ObserverBinding(RuntimeInputCancellationDiagnostics& observer):previous(runtimeInputCancellationDiagnostics){runtimeInputCancellationDiagnostics=&observer;}
    ~ObserverBinding(){runtimeInputCancellationDiagnostics=previous;}
};
struct Enumeration { HWND expected=nullptr;bool found=false;DWORD pid=0,tid=0;std::wstring name; };
BOOL CALLBACK enumerate(HWND h,LPARAM parameter){
    auto& e=*reinterpret_cast<Enumeration*>(parameter);
    if(h==e.expected){wchar_t name[256]{};e.tid=GetWindowThreadProcessId(h,&e.pid);const int count=GetClassNameW(h,name,256);e.name.assign(name,count>0?count:0);e.found=true;}
    return TRUE;
}
bool witness(Window& w,std::ostream& trace){
    Enumeration e;e.expected=w.handle;DWORD peerPid=0;const DWORD peerTid=GetWindowThreadProcessId(w.other,&peerPid);
    wchar_t peerClass[256]{};const int peerClassLength=GetClassNameW(w.other,peerClass,256);
    const HDESK desktop=GetThreadDesktop(GetCurrentThreadId());
    SetLastError(ERROR_SUCCESS);const BOOL success=desktop?EnumDesktopWindows(desktop,enumerate,reinterpret_cast<LPARAM>(&e)):FALSE;const DWORD error=GetLastError();
    const bool matched=success && e.found && e.pid==GetCurrentProcessId()&&e.tid==GetCurrentThreadId()&&e.name==className&&peerPid==GetCurrentProcessId()&&peerTid==GetCurrentThreadId()&&peerClassLength>0;
    trace<<"ownedWindow route="<<w.route<<" utc="<<RuntimeCursorSmokeDetail::utcNow()<<" hwnd="<<reinterpret_cast<std::uintptr_t>(w.handle)
        <<" peer="<<reinterpret_cast<std::uintptr_t>(w.other)<<" desktop="<<reinterpret_cast<std::uintptr_t>(desktop)
        <<" enumerated="<<success<<" win32="<<error<<" found="<<e.found<<" pid="<<e.pid<<" tid="<<e.tid
        <<" peerPid="<<peerPid<<" peerTid="<<peerTid<<" peerClassLength="<<peerClassLength
        <<" actualPid="<<GetCurrentProcessId()<<" actualTid="<<GetCurrentThreadId()<<" class="<<hex(std::string(e.name.begin(),e.name.end()))<<" matched="<<matched<<'\n';
    return matched;
}
std::string preferences(const RuntimeWindowState& s){std::ifstream f(s.config.configPath,std::ios::binary);std::ostringstream bytes;bytes<<f.rdbuf();return bytes.str();}
struct Saved {
    std::vector<std::string> states,serializations,reasons,preferences,events,observations;
    std::vector<bool> serialized;std::vector<LRESULT> results;std::vector<std::size_t> clockReads;
    std::vector<Command> commands;
    bool operator==(const Saved&)const=default;
};
} // namespace RuntimeInputCancellationSmokeDetail

bool runRuntimeInputCancellationSmokeProbe(RuntimeWindowState& stock,std::ostream& output){
    using namespace RuntimeInputCancellationSmokeDetail;
    std::size_t checks=0,failures=0,triples=0,inputs=0,fullSerializations=0,ordinaryLoads=0,accepted=0,rejected=0,replayed=0,ownedWindows=0;
    const auto need=[&](bool value,const std::string& label){++checks;if(!value){++failures;output<<"Input cancellation failure: "<<label<<std::endl;}return value;};
    const char* requested=std::getenv("CONCORD_INPUT_CANCELLATION_OUTPUT");
    if(!need(requested&&*requested,"explicit private output directory"))return false;
    const std::filesystem::path directory=requested;std::error_code error;std::filesystem::create_directories(directory,error);
    if(!need(!error,"private output directory"))return false;
    const auto tracePath=directory/"input-cancellation-trace.txt";
    if(!need(!std::filesystem::exists(tracePath),"new complete trace"))return false;
    std::ofstream trace(tracePath,std::ios::binary);
    if(!need(bool(trace),"trace opened"))return false;
    if(!need(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"real package rules/art/theater/world"))return false;
    const std::string resources=stock.config.localDataRootPath;
    const auto privateRuntimeRoot=directory/"private-runtime";
    if(!need(!std::filesystem::exists(privateRuntimeRoot),"new private runtime root"))return false;
    std::filesystem::create_directories(privateRuntimeRoot,error);
    if(!need(!error,"private runtime root created"))return false;
    Registration registration;if(!need(registration.ready,"own class registration"))return false;
    const auto prototype = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config = stock.config; state.config.gameMode = "Battle";
        state.config.gameModeRulesOverride.clear(); state.config.computerAiEnabled = false;
        state.config.multiplayerActive = false; state.config.noCooldown = false;
        state.config.superweapons = true; state.config.techLevel = 10;
        state.config.shortGame = false;
        state.config.startingCredits = 100000; state.config.startingUnits = 3;
        state.config.playerCountry = "Americans";
        state.config.showMainMenu = state.config.showSkirmishMenu = false;
        state.config.localDataRootPath = pathString(privateRuntimeRoot);
        state.config.configPath = pathString(privateRuntimeRoot / "prototype-private.ini");
        state.config.playerSlots.assign(3, {});
        for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
            auto& slot = state.config.playerSlots[index]; slot.enabled = slot.human = true;
            slot.local = index == 0; slot.country = index % 2 ? "Russians" : "Americans";
            slot.team = "-"; slot.startPosition = static_cast<int>(index) + 1;
            slot.name = "Cursor player " + std::to_string(index + 1);
            const auto owner = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[owner] = slot.country;
            state.skirmishTeamByOwner[owner] = runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report = stock.report; state.world = stock.world; state.world.accessObjects().clear();
        state.rules = stock.rules; state.art = stock.art; state.theater = stock.theater;
        state.mapFacts = stock.mapFacts; state.mapFactsLoaded = stock.mapFactsLoaded;
        state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
        state.terrainInfoByCell = stock.terrainInfoByCell;
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        state.runtimeSoundsEnabled = state.runtimeMusicEnabled = state.runtimeSoundEffectsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        BorrowResources originalResourceChain(state, resources);
        state.visionEnabled = false; initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
        if (!need(initializeRuntimeSkirmishStart(state), "ordinary legal three-house start"))
            return std::unique_ptr<RuntimeWindowState>{};
        state.triggers.bootstrapComplete = true;
        state.skirmishModeAlliesAllowed = state.skirmishModeAllyChangeAllowed = true;
        state.skirmishModeMustAlly = false; state.tick = 21;
        const RECT client{0, 0, 1024, 768};
        ensureRuntimeCameraForViewport(state, runtimeMapViewport(client));
        return storage;
    }();
    if (!prototype) return false;
    const auto campaignPrototype = [&]() {
        CampaignDatabaseOptions options;
        options.resourceRootPath = stock.config.resourceRootPath;
        options.localDataRootPath = resources;
        options.modRootPath = stock.config.modRootPath;
        const auto database = loadCampaignDatabase(options);
        const CampaignMissionDefinition* mission = nullptr;
        for (const auto& campaign : database.campaigns) {
            for (const auto& candidate : campaign.missions) {
                if (!mission && candidate.available && !candidate.playerHouse.empty())
                    mission = &candidate;
            }
        }
        if (!need(database.loaded && mission, "real mounted campaign mission identity"))
            return std::unique_ptr<RuntimeWindowState>{};
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config = stock.config;
        state.config.gameMode = "CampaignMission"; state.config.gameModeRulesOverride.clear();
        state.config.defaultMapPath = mission->mapPath;
        state.config.campaignId = mission->campaignId;
        state.config.campaignMissionId = mission->id;
        state.config.campaignSourceId = mission->sourceId;
        state.config.campaignPlayerHouse = mission->playerHouse;
        state.config.campaignNextMissionId = mission->nextMissionId;
        state.config.playerCountry = mission->playerCountry;
        state.config.startingCredits = mission->startingCredits;
        state.config.startingUnits = 0; state.config.shortGame = false;
        state.config.multiplayerActive = false; state.config.computerAiEnabled = false;
        state.config.showMainMenu = state.config.showSkirmishMenu = false;
        state.config.localDataRootPath = pathString(privateRuntimeRoot);
        state.config.configPath = pathString(privateRuntimeRoot / "campaign-prototype-private.ini");
        state.campaignMode = true;
        state.runtimeSoundsEnabled = state.runtimeMusicEnabled = state.runtimeSoundEffectsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        {
            BorrowResources originalResources(state, resources);
            state.report = buildRuntimeStartupReport(state.config, &state.world, &state.theater,
                &state.art, &state.rules, &state.mapFacts);
            state.rulesLoaded = state.report.rulesLoaded;
            state.artLoaded = state.report.artLoaded;
            state.theaterLoaded = state.report.theaterLoaded;
            state.mapFactsLoaded = state.report.map.loaded;
            if (!need(state.report.success && state.world.getSummary().loaded,
                "original real campaign map/rules/art/theater startup"))
                return std::unique_ptr<RuntimeWindowState>{};
            loadRuntimeSoundDatabase(state);
            ensureRuntimeLightingInitialized(state);
            ensureRuntimeParticlesInitialized(state);
            (void)initializeRuntimeCampaignOverlayCrates(state);
            initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
            if (!need(initializeRuntimeTriggers(state), "original campaign trigger initialization"))
                return std::unique_ptr<RuntimeWindowState>{};
        }
        // Original campaign navigation above owns this map's terrain metadata.
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        state.visionEnabled = false;
        const RECT client{0, 0, 1024, 768};
        ensureRuntimeCameraForViewport(state, runtimeMapViewport(client));
        trace << "completeCampaignPrototype campaign=" << hex(state.config.campaignId)
            << " mission=" << hex(state.config.campaignMissionId)
            << " source=" << hex(state.config.campaignSourceId)
            << " map=" << hex(state.config.defaultMapPath) << " tick=" << state.tick
            << " originalStartup=" << state.report.success
            << " objects=" << state.world.getObjects().size() << '\n';
        return storage;
    }();
    if (!campaignPrototype) return false;
    std::map<const RuntimeWindowState*, std::filesystem::path> seedFiles;
    std::size_t seedRoundTrips = 0;
    const auto clone = [&](RuntimeWindowState& seed, const std::string& label, const RuntimeUiClock* clock = nullptr) {
        auto savedSeed = seedFiles.find(&seed);
        if (savedSeed == seedFiles.end()) {
            if (!need(seed.config.localDataRootPath == pathString(privateRuntimeRoot),
                "seed full serialization uses only the probe-private root")) return std::unique_ptr<RuntimeWindowState>{};
            std::ostringstream bytes; std::string reason;
            const bool serialized = writeRuntimeStateToStream(seed, bytes, reason);
            const auto file = privateRuntimeRoot / ("complete-seed-" + std::to_string(seedFiles.size()) + ".ini");
            const bool fileWritten = writeNewBytes(file, bytes.str());
            trace << "completeSeed originalResult=" << serialized << " originalReason="
                << RuntimePointerIngressSmokeDetail::hex(reason) << " bytes=" << bytes.str().size()
                << " file=" << RuntimePointerIngressSmokeDetail::hex(pathString(file))
                << " fileWritten=" << fileWritten << " owned="
                << RuntimePointerIngressSmokeDetail::hex(snapshot(seed)) << '\n';
            if (!need(fileWritten, "new complete seed raw file including any original partial bytes")
                || !need(serialized, "healthy original complete seed serializer succeeds: " + reason))
                return std::unique_ptr<RuntimeWindowState>{};
            savedSeed = seedFiles.emplace(&seed, file).first;
        }
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config = seed.config; state.report = seed.report; state.world = seed.world;
        state.campaignMode = seed.campaignMode;
        state.rules = seed.rules; state.art = seed.art; state.theater = seed.theater;
        state.rulesLoaded = seed.rulesLoaded; state.artLoaded = seed.artLoaded;
        state.theaterLoaded = seed.theaterLoaded; state.mapFacts = seed.mapFacts;
        state.mapFactsLoaded = seed.mapFactsLoaded; state.economy = seed.economy;
        state.playerCommands = seed.playerCommands; state.humanHouseAuthorities = seed.humanHouseAuthorities;
        state.skirmishCountryByOwner = seed.skirmishCountryByOwner;
        state.skirmishTeamByOwner = seed.skirmishTeamByOwner;
        state.terrainInfoByCell = seed.terrainInfoByCell; state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        state.superWeapons = seed.superWeapons; state.superWeaponsInitialized = seed.superWeaponsInitialized;
        state.worldAudio = seed.worldAudio; state.worldAudioInitialized = seed.worldAudioInitialized;
        state.lighting = seed.lighting; state.lightingInitialized = seed.lightingInitialized;
        state.activeLightingTokens = seed.activeLightingTokens;
        state.particles = seed.particles; state.particlesInitialized = seed.particlesInitialized;
        state.tick = seed.tick; state.camera = seed.camera;
        state.triggers.bootstrapComplete = true; state.visionEnabled = false;
        state.runtimeSoundsEnabled = state.runtimeMusicEnabled = state.runtimeSoundEffectsEnabled = false;
        state.runtimeMusicVolume = state.runtimeSoundVolume = state.runtimeVoiceVolume = 0;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        state.uiClock = clock;
        state.config.localDataRootPath = pathString(privateRuntimeRoot);
        state.config.configPath = pathString(privateRuntimeRoot / (label + "-preferences.ini"));
        if (!need(!std::filesystem::exists(state.config.configPath), "new private preferences " + label))
            return std::unique_ptr<RuntimeWindowState>{};
        std::ofstream preferences(state.config.configPath, std::ios::binary | std::ios::out);
        preferences << "[Preserved]\nSentinel=keep\n"; preferences.close();
        if (!need(static_cast<bool>(preferences), "private preferences saved " + label))
            return std::unique_ptr<RuntimeWindowState>{};
        const RuntimeUiClock* originalClock = state.uiClock;
        std::string loadReason; bool loaded = false;
        {
            BorrowResources originalLoadDependencyChain(state, resources);
            if (seed.campaignMode) {
                // Production establishes these original host catalogs and
                // compiled trigger definitions before a campaign restore.
                loadRuntimeSoundDatabase(state);
                initializeRuntimeWorldAudio(state);
                initializeRuntimeNavigation(state);
                initializeRuntimeEconomy(state);
                if (!need(initializeRuntimeTriggers(state),
                    "original campaign preload trigger context"))
                    return std::unique_ptr<RuntimeWindowState>{};
                if (!need(state.worldAudioCatalog.stateHash() == seed.worldAudioCatalog.stateHash(),
                    "original complete campaign audio catalog matches saved seed"))
                    return std::unique_ptr<RuntimeWindowState>{};
            }
            loaded = loadRuntimeState(state, pathString(savedSeed->second), loadReason);
        }
        trace << "completeSeedLoad label=" << label << " originalResult=" << loaded
            << " originalReason=" << RuntimePointerIngressSmokeDetail::hex(loadReason)
            << " source=" << RuntimePointerIngressSmokeDetail::hex(pathString(savedSeed->second))
            << " retainedClock=" << (state.uiClock == originalClock)
            << " privateRootRestored=" << (state.config.localDataRootPath == pathString(privateRuntimeRoot))
            << " owned=" << RuntimePointerIngressSmokeDetail::hex(snapshot(state)) << '\n';
        if (!need(loaded && state.uiClock == originalClock && state.config.localDataRootPath == pathString(privateRuntimeRoot)
            && state.config.configPath == pathString(privateRuntimeRoot / (label + "-preferences.ini")),
            "actual full seed restoration retains all original authority, borrowed clock and private root: " + loadReason))
            return std::unique_ptr<RuntimeWindowState>{};
        ++seedRoundTrips;
        initializeRuntimeOfflineGameplayRouting(state); ensureRuntimeMenuKeyboardLoaded(state);
        state.runtimeMenuOpen = false; state.runtimeMenuKeyCaptureIndex = -1;
        state.playerCommunication = {}; state.playerCommunication.chatDraft = u"cursor-kept";
        state.selectedObjectIds = seed.selectedObjectIds; state.activeSidebarTool = RuntimeSidebarTool::None;
        state.repaintRequested = false; state.interactionStatus = "cursor initial";
        return storage;
    };

    const auto run=[&](const std::string& name,int commandMode,const auto& setup,const auto& action){
        const auto failuresBefore=failures;std::array<Saved,3> saved;
        for(int route=0;route<3;++route){
            const std::string label=name+"-route-"+std::to_string(route);
            Clock clock;auto storage=clone(name.starts_with("matrix-6-")?*campaignPrototype:*prototype,label,&clock);
            if(!storage)return false;
            auto& state=*storage;RuntimeSnapshotActiveStateScope active(state);ClockBinding clockBinding(state,clock);
            Witness commands(state,commandMode);Window window(state,route,resources,trace);
            if(!need(window.handle&&window.other,label+" own target and peer HWND"))return false;
            if(!need(GetCapture()==nullptr,label+" thread starts with no foreign capture"))return false;
            trace<<"roleStart case="<<name<<" route="<<route<<" utc="<<RuntimeCursorSmokeDetail::utcNow()<<'\n';
            if(!need(witness(window,trace),label+" exact class/PID/TID/own-desktop enumeration"))return false;++ownedWindows;
            state.runtimePreferencePersistenceEnabled=true;state.runtimeMenuStatus=L"capture status kept";state.forceAttackHeld=true;
            state.leftPointer.start={33,44};state.leftPointer.current={55,66};
            state.rightPointer.start={31,47};state.rightPointer.last={77,99};state.rightPointer.cursorDelta={17,-11};
            setup(window,state);commands.attach();ObserverBinding observer(window);
            window.recording=true;window.sequence=0;std::size_t step=0;
            const auto save=[&](const std::string& phase,LRESULT result){
                RECT client{};const BOOL measured=IsWindow(window.handle)?GetClientRect(window.handle,&client):FALSE;
                std::ostringstream complete;complete<<snapshot(state)<<";menu-detail="<<menuState(state)
                    <<";alive="<<(IsWindow(window.handle)!=FALSE)<<";enabled="<<(IsWindowEnabled(window.handle)!=FALSE)
                    <<";capture="<<window.kind(GetCapture())<<";client="<<measured<<','<<client.left<<','<<client.top<<','<<client.right<<','<<client.bottom;
                saved[route].states.push_back(complete.str());saved[route].results.push_back(result);saved[route].clockReads.push_back(clock.reads);
                saved[route].preferences.push_back(preferences(state));
                std::ostringstream serialized;std::string reason;
                const bool ok=writeRuntimeStateToStream(state,serialized,reason);const auto bytes=serialized.str();
                const auto file=directory/(label+"-"+std::to_string(step)+"-"+phase+".ini");
                need(writeNewBytes(file,bytes),label+" new complete original serialization file");
                need(ok,label+" original complete serializer succeeds: "+reason);
                saved[route].serialized.push_back(ok);saved[route].reasons.push_back(reason);saved[route].serializations.push_back(bytes);++fullSerializations;
                trace<<"snapshot case="<<name<<" route="<<route<<" step="<<step++<<" phase="<<phase<<" result="<<result
                    <<" utc="<<RuntimeCursorSmokeDetail::utcNow()<<" serialized="<<ok<<" file="<<hex(pathString(file))
                    <<" reason="<<hex(reason)<<" fullState="<<hex(complete.str())<<" preferences="<<hex(saved[route].preferences.back())<<'\n';
            };
            save("initial",0);
            const auto send=[&](UINT message,WPARAM raw,LPARAM flags){
                if(!need(IsWindow(window.handle)!=FALSE,label+" target alive before message"))return LRESULT{0};
                const LRESULT result=SendMessageW(window.handle,message,raw,flags);++inputs;save("message-"+std::to_string(message),result);return result;
            };
            action(window,state,send);
            const auto count=commands.commands.size();std::string replayError;
            need(commands.replay(replayError),label+" original complete canonical replay: "+replayError);
            if(commandMode==2)replayed+=count;
            save("final",0);saved[route].commands=commands.commands;
            for(const auto& command:commands.commands){
                std::string commandError;RuntimeMultiplayerGameplayCommand decoded;std::vector<std::uint8_t> canonical;
                need(command.tick==state.tick&&!command.payload.empty()&&decodeRuntimeMultiplayerGameplayCommand(command.payload,decoded,commandError)
                    &&encodeRuntimeMultiplayerGameplayCommand(decoded,canonical,commandError)&&canonical==command.payload,label+" exact command/tick/wire roundtrip");
                trace<<"command case="<<name<<" route="<<route<<" tick="<<command.tick<<" accepted="<<command.accepted<<" applied="<<command.applied
                    <<" payload="<<hex(std::string(command.payload.begin(),command.payload.end()))<<" error="<<hex(command.error)<<'\n';
                if(command.accepted)++accepted;else++rejected;
            }
            if(name.starts_with("alliance-"))need(commands.commands.size()==1,label+" exactly one original alliance intent");
            saved[route].events=window.events;saved[route].observations=window.observations;
            need(window.foreignCaptureObservations==0,label+" every actual capture owner is null, own target or owned peer with exact PID/TID");
            window.recording=false;window.close();need(window.handle==nullptr&&window.other==nullptr,label+" own handles cleaned");
            MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
            trace<<"roleEnd case="<<name<<" route="<<route<<" utc="<<RuntimeCursorSmokeDetail::utcNow()<<'\n';
        }
        need(saved[0]==saved[1],name+" complete old / same production CPP state, callbacks, commands, full bytes and effects");
        need(saved[0]==saved[2],name+" complete old / actual WndProc state, callbacks, commands, full bytes and effects");
        trace<<"caseCompleted name="<<name<<" oldSameCpp="<<(saved[0]==saved[1])<<" oldNative="<<(saved[0]==saved[2])<<'\n';trace.flush();
        ++triples;if(triples%12==0)output<<"Input cancellation native progress: cases="<<triples<<" inputs="<<inputs<<" failures="<<failures<<std::endl;
        return failures==failuresBefore;
    };
    const auto context=[&](RuntimeWindowState& s,int mode){
        if(mode==1)setRuntimeMenuOpen(s,true); // original world pause/menu-main
        if(mode==2){setRuntimeMenuOpen(s,true);resetRuntimeMenuPage(s,RuntimeMenuPage::Settings);}
        if(mode==3)s.playerCommunication.chatOpen=true;
        if(mode==4)openRuntimeDiplomacyPage(s);
        if(mode==5){s.skirmishMatchFinished=true;s.resultPresentation.bannerVisible=true;}
        if(mode==6){s.campaignMode=true;s.exitAfterPendingCampaignVideos=true;}
        if(mode==7){setRuntimeMenuOpen(s,true);resetRuntimeMenuPage(s,RuntimeMenuPage::Sound);}
    };
    const auto capture=[&](Window& w,int owner){if(owner==1)SetCapture(w.handle);else if(owner==2)SetCapture(w.other);need(w.kind(GetCapture())==owner,"actual initial capture owner");};
    const UINT messages[]={WM_KILLFOCUS,WM_CANCELMODE,WM_ACTIVATEAPP,WM_CAPTURECHANGED,WM_ACTIVATEAPP,WM_ACTIVATEAPP};
    for(int type=0;type<3;++type)for(int interruption=0;interruption<4;++interruption){
        if(!run("actual-drag-"+std::to_string(type)+"-"+std::to_string(interruption),1,
            [&](Window&,RuntimeWindowState& s){s.forceAttackHeld=false;s.selectedObjectIds.clear();},
            [&](Window& w,RuntimeWindowState& s,const auto& send){
                send(type==1?WM_RBUTTONDOWN:WM_LBUTTONDOWN,type==1?0:MK_LBUTTON,MAKELPARAM(100,120));
                if(type==2)send(WM_RBUTTONDOWN,0,MAKELPARAM(100,120));
                send(WM_MOUSEMOVE,type==1?0:MK_LBUTTON,MAKELPARAM(140,155));
                if(interruption==0){SetWindowPos(w.handle,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);RECT suggested{0,0,800,600};send(WM_DPICHANGED,MAKELPARAM(144,144),reinterpret_cast<LPARAM>(&suggested));}
                if(interruption==1)SetCapture(w.other);
                if(interruption==2)EnableWindow(w.handle,FALSE);
                if(interruption==3)send(WM_KILLFOCUS,0,0);
                send(type==1?WM_RBUTTONUP:WM_LBUTTONUP,0,MAKELPARAM(-29,-17));
                if(type==2)send(WM_RBUTTONUP,0,MAKELPARAM(-29,-17));
                if(type!=1)need(!s.leftPointer.active,"original outside left-up completes after capture interruption");
                if(!IsWindowEnabled(w.handle))EnableWindow(w.handle,TRUE);
            }))return false;
    }
    for(int mode=0;mode<8;++mode)for(int gesture=0;gesture<4;++gesture)for(int owner=0;owner<3;++owner)for(int event=0;event<6;++event){
        const std::string name="matrix-"+std::to_string(mode)+"-"+std::to_string(gesture)+"-"+std::to_string(owner)+"-"+std::to_string(event);
        if(!run(name,1,[&](Window& w,RuntimeWindowState& s){context(s,mode);s.leftPointer.active=s.leftPointer.moved=(gesture&1)!=0;s.rightPointer.active=s.rightPointer.moved=(gesture&2)!=0;capture(w,owner);},
            [&](Window& w,RuntimeWindowState& s,const auto& send){
                const bool before=s.leftPointer.active;const WPARAM raw=event==4?WPARAM{1}:event==5?(WPARAM{1}<<32):WPARAM{0};
                send(messages[event],raw,event==3?reinterpret_cast<LPARAM>(owner==1?w.handle:owner==2?w.other:nullptr):0);
                if(mode!=4)need(s.leftPointer.active==before,"capture/focus does not add left clear outside original diplomacy owner");
                if(messages[event]==WM_KILLFOCUS||event==2)need(!s.forceAttackHeld,"original force-held focus/deactivation clear");
            }))return false;
    }
    const RuntimeMenuAction sliders[]={RuntimeMenuAction::CycleGameSpeed,RuntimeMenuAction::CycleScrollRate,RuntimeMenuAction::CycleVisualDetail,
        RuntimeMenuAction::AdjustMusicVolume,RuntimeMenuAction::AdjustSoundVolume,RuntimeMenuAction::AdjustVoiceVolume};
    for(const auto slider:sliders)for(int owner=0;owner<3;++owner)for(int event=0;event<6;++event){
        const std::string name="slider-"+std::to_string(static_cast<int>(slider))+"-"+std::to_string(owner)+"-"+std::to_string(event);
        if(!run(name,1,[&](Window& w,RuntimeWindowState& s){setRuntimeMenuOpen(s,true);resetRuntimeMenuPage(s,static_cast<int>(slider)>=static_cast<int>(RuntimeMenuAction::AdjustMusicVolume)?RuntimeMenuPage::Sound:RuntimeMenuPage::Settings);
                s.runtimeMenuSliderDrag=s.runtimeMenuPressed=slider;capture(w,owner);},
            [&](Window& w,RuntimeWindowState& s,const auto& send){
                if(event==0){if(GetCapture()==w.handle)ReleaseCapture();else send(WM_CAPTURECHANGED,0,0);}
                if(event==1){if(GetCapture()!=w.handle)SetCapture(w.handle);SetCapture(w.other);}
                if(event==2)send(WM_KILLFOCUS,0,0);
                if(event==3)send(WM_CANCELMODE,0,0);
                if(event==4)send(WM_ACTIVATEAPP,FALSE,0);
                if(event==5){RECT rect{0,0,800,600};send(WM_DPICHANGED,MAKELPARAM(144,144),reinterpret_cast<LPARAM>(&rect));send(WM_LBUTTONUP,0,MAKELPARAM(-23,-37));}
                if(event<2)need(s.runtimeMenuSliderDrag==RuntimeMenuAction::None&&s.runtimeMenuPressed==RuntimeMenuAction::None,"actual capture transfer interrupts slider and pressed");
            }))return false;
    }
    for(int owner=0;owner<3;++owner)for(int interruption=0;interruption<4;++interruption){
        if(!run("pressed-"+std::to_string(owner)+"-"+std::to_string(interruption),1,
            [&](Window& w,RuntimeWindowState& s){setRuntimeMenuOpen(s,true);s.runtimeMenuPressed=RuntimeMenuAction::Options;capture(w,owner);},
            [&](Window& w,RuntimeWindowState& s,const auto& send){if(interruption==0)send(WM_CAPTURECHANGED,0,reinterpret_cast<LPARAM>(w.other));
                else if(interruption==1)send(WM_CANCELMODE,0,0);else if(interruption==2)send(WM_KILLFOCUS,0,0);else send(WM_ACTIVATEAPP,FALSE,0);
                (void)s;}))return false;
    }
    for(int mutation=1;mutation<=4;++mutation){
        if(!run("synchronous-nested-"+std::to_string(mutation),1,
            [&](Window& w,RuntimeWindowState& s){s.rightPointer.active=s.rightPointer.moved=true;s.leftPointer.active=s.leftPointer.moved=true;SetCapture(w.handle);w.mutation=mutation;},
            [&](Window&,RuntimeWindowState&,const auto& send){send(WM_CANCELMODE,0,0);} ))return false;
    }
    for(int commandMode=1;commandMode<=3;++commandMode){
        if(!run("alliance-"+std::to_string(commandMode),commandMode,
            [&](Window&,RuntimeWindowState& s){openRuntimeDiplomacyPage(s);},
            [&](Window& w,RuntimeWindowState&,const auto& send){
                send(WM_KILLFOCUS,0,0);RECT client{};GetClientRect(w.handle,&client);const auto rect=runtimeDiplomacyCheckboxRect(client,1743);
                const auto point=MAKELPARAM(static_cast<short>((rect.left+rect.right)/2),static_cast<short>((rect.top+rect.bottom)/2));
                send(WM_LBUTTONDOWN,MK_LBUTTON,point);send(WM_LBUTTONUP,0,point);
            }))return false;
    }
    for(int first=0;first<3;++first){
        if(!run("ordinary-active-load-"+std::to_string(first),1,
            [&](Window& w,RuntimeWindowState& s){
                s.config.shortGame=true;(void)advanceRuntimeParticles(s);
                need(!s.campaignMode&&!s.skirmishMatchFinished&&s.tick==21&&s.particlesInitialized&&s.particles.currentTick()==21,"ordinary active natural save preconditions");
                const auto root=privateRuntimeRoot/("ordinary-"+std::to_string(first)+"-"+std::to_string(w.route));std::filesystem::create_directories(root);
                const auto previous=s.config.localDataRootPath;s.config.localDataRootPath=pathString(root);const auto path=runtimeDefaultSavePath(s);std::string why;
                const auto clock=s.uiClock;const bool saved=saveRuntimeState(s,path,why);bool loaded=false;
                if(saved){BorrowResources borrowed(s,resources);loaded=loadRuntimeState(s,path,why);}s.config.localDataRootPath=previous;
                need(saved&&loaded&&s.tick==21&&s.uiClock==clock,"ordinary original save/load preserves actual tick and clock: "+why);if(saved&&loaded)++ordinaryLoads;
                s.leftPointer.active=s.leftPointer.moved=true;s.rightPointer.active=s.rightPointer.moved=true;SetCapture(w.handle);},
            [&](Window&,RuntimeWindowState&,const auto& send){send(first==0?WM_KILLFOCUS:first==1?WM_CANCELMODE:WM_CAPTURECHANGED,0,0);} ))return false;
    }
    if(!need(registration.close(),"own native class cleanup"))return false;
    output<<"Input cancellation native "<<(failures?"failed":"passed")<<": checks="<<checks<<" cases="<<triples
        <<" routes="<<triples*3<<" inputs="<<inputs<<" fullSerializations="<<fullSerializations<<" seedRoundTrips="<<seedRoundTrips
        <<" ordinaryLoads="<<ordinaryLoads<<" accepted="<<accepted<<" rejected="<<rejected<<" replayed="<<replayed<<" ownedWindows="<<ownedWindows
        <<" failures="<<failures<<" sameProductionCpp=1 completeOldWndProc=1 fullMatrix=1 physicalInput=not-run fullIME=not-run runningNetwork=not-run"
        <<" actualMonitorDpiTransition=not-run fullModalUI=not-run secondBackend=not-run secondSystem=not-run performance=not-run"<<std::endl;
    return failures==0;
}
