// Included after RuntimePointerIngressSmoke.inl; its frozen snapshot/hex utilities
// are reused as diagnostics only. No prior input owner is migrated here.
#include "ui/input/diagnostics/RuntimeCursorOldOracle.inl"
#include <limits>

namespace RuntimeCursorSmokeDetail {
inline constexpr const wchar_t* windowClass = L"ConcordCursorPolicySmoke";
LRESULT literalCursorWindowProc(bool frozenOld,
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    RuntimeWindowState* campaignState =
        reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );

    // A synchronous window callback can enter during a local simulation
    // batch. Close its shared terrain signature before any handler mutates
    // state, including load/input paths that are ordinarily outside the batch.
    if (campaignState != nullptr)
        runtimeInvalidateVisionTerrainBatchForMessage(campaignState->world);

    if (campaignState != nullptr) {
        RuntimeForceAttackHeldUiHost forceHost(*campaignState, window);
        handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message, wParam), forceHost);
    }
    if (campaignState != nullptr) {
        RuntimeMapRightPointerGestureUiHost rightHost(*campaignState, window);
        const auto event = message == WM_KILLFOCUS ? RuntimeMapRightPointerCancelEvent::FocusLost
            : message == WM_CANCELMODE ? RuntimeMapRightPointerCancelEvent::CancelMode
            : message == WM_ACTIVATEAPP && wParam == FALSE
                ? RuntimeMapRightPointerCancelEvent::ApplicationDeactivated
                : RuntimeMapRightPointerCancelEvent::Other;
        cancelRuntimeMapRightPointer(event, rightHost);
    }
    if (campaignState != nullptr) {
        RuntimeCampaignInputAdmissionUiHost admissionHost(*campaignState);
        if (rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message), admissionHost))
            return 0;
    }
    if (campaignState != nullptr) {
        RuntimePointerIngressUiHost pointerHost(*campaignState, window);
        if (handleRuntimePointerIngress(
                runtimePointerIngressInputForMessage(message, wParam, lParam), pointerHost))
            return 0;
    }
    if (message != WM_SETCURSOR) return DefWindowProcW(window, message, wParam, lParam);
    RuntimeCursorUiHost host(window);
    const bool handled = frozenOld
        ? RuntimeCursorFrozenOld::setCursor(LOWORD(lParam) == HTCLIENT, host)
        : handleRuntimeCursor(LOWORD(lParam) == HTCLIENT, host);
    return handled ? TRUE : DefWindowProcW(window, message, wParam, lParam);
}
struct Window;
thread_local std::map<HWND, Window*> windows;
LRESULT CALLBACK cursorWindowProc(HWND, UINT, WPARAM, LPARAM);
struct Registration {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    bool ready = false;
    Registration() {
        WNDCLASSW value{}; value.lpfnWndProc = cursorWindowProc;
        value.hInstance = instance; value.lpszClassName = windowClass;
        ready = RegisterClassW(&value) != 0;
    }
    bool close() {
        if (!ready) return true;
        const BOOL removed = UnregisterClassW(windowClass, instance);
        if (removed) ready = false;
        WNDCLASSW remaining{};
        return removed != FALSE && GetClassInfoW(instance, windowClass, &remaining) == FALSE;
    }
    ~Registration() { if (ready) close(); }
};
struct Observation {
    std::string name; std::vector<std::string> arguments; std::string result;
    bool operator==(const Observation&) const = default;
};
std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto whole = std::chrono::system_clock::to_time_t(now);
    std::tm utc{}; gmtime_s(&utc, &whole);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    std::ostringstream output; output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setfill('0') << std::setw(6) << micros << "+00:00";
    return output.str();
}
std::string snapshot(const RuntimeWindowState& value) {
    std::ostringstream result;
    result << RuntimePointerIngressSmokeDetail::state(value, true, value.config.configPath)
        << ";superWeaponsInitialized=" << value.superWeaponsInitialized
        << ";superWeaponLastTick=" << value.superWeapons.lastTick()
        << ";superWeaponState=";
    std::ostringstream superWeaponState; value.superWeapons.save(superWeaponState);
    result << RuntimePointerIngressSmokeDetail::hex(superWeaponState.str());
    result << ";catalog=";
    for (const auto& row : value.economy.catalog)
        result << static_cast<int>(row.kind) << ',' << RuntimePointerIngressSmokeDetail::hex(row.typeId)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.label)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.owner)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.requiredHouses)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.forbiddenHouses)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.prerequisite)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.prerequisiteOverride)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.requiredFactory) << ',' << row.cost
            << ',' << row.buildTicks << ',' << row.powerDelta << ',' << row.techLevel
            << ',' << (row.buildLimit ? std::to_string(*row.buildLimit) : "null")
            << ',' << row.waterBound << ',' << row.naval << ',' << row.wall
            << ',' << row.techTreeAllowed << ',' << row.secretLabInjected
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.techTreeKind)
            << ',' << RuntimePointerIngressSmokeDetail::hex(row.techTreeBlockedReason) << ';';
    result << ";projectionCacheViewport=" << value.projectedObjectsCacheViewport.left << ','
        << value.projectedObjectsCacheViewport.top << ',' << value.projectedObjectsCacheViewport.right << ','
        << value.projectedObjectsCacheViewport.bottom << ";projectionCacheCamera="
        << value.projectedObjectsCacheCameraX << ',' << value.projectedObjectsCacheCameraY << ','
        << value.projectedObjectsCacheTileWidth << ',' << value.projectedObjectsCacheTileHeight << ';';
    result << ";projectionRows=" << value.projectedObjectsCache.size() << ';';
    const auto rect = [&](RECT row) { result << row.left << ',' << row.top << ',' << row.right << ',' << row.bottom << ';'; };
    const auto spans = [&](const auto& rows) { for (const auto& row : rows) result << row.y << ',' << row.left << ',' << row.right << ';'; result << '|'; };
    for (const auto& row : value.projectedObjectsCache) {
        int objectIdentity = row.object ? -2 : -1;
        for (const auto& object : value.world.getObjects())
            if (&object == row.object) { objectIdentity = object.instanceId; break; }
        result << "object=" << objectIdentity << ';' << row.observerType << ',' << row.observerOwner << ','
            << static_cast<int>(row.observerKind) << ',' << row.spyDisguiseProjected << ','
            << row.center.x << ',' << row.center.y << ',' << row.renderCenter.x << ',' << row.renderCenter.y << ';';
        rect(row.bounds); rect(row.spriteBounds); rect(row.selectionBounds); rect(row.selectionBaseBounds);
        result << row.hasMobileSpriteBounds << ',' << row.healthPixelOffsetY << ',' << row.drawLevel << ','
            << row.projectedX << ',' << row.projectedY << ',' << row.usesFootprintHit << ','
            << row.hasStructureSpriteGeometry << ',' << row.airborneForeground << ',' << row.hasBodyHitClip << ';';
        rect(row.bodyHitClip); result << row.factoryForegroundHostId << ';'; rect(row.footprintBounds);
        spans(row.structureSpriteHitSpans); spans(row.structureForegroundHitSpans);
        result << row.hasBuildingDepthVisibility << ';'; spans(row.visibleBuildingHitSpans);
        result << row.hasHostedSortOverride << ',' << row.hostedSortCellSum << ',' << row.footprintHalfWidth << ',' << row.footprintHalfHeight << ';';
        for (const auto& point : row.footprintHitCenters) result << point.x << ',' << point.y << ';';
    }
    if (const auto& targeting = value.multiplayerIntentRouter.superWeapon()) {
        result << ";routerSuperWeapon=" << targeting->superWeaponId
            << ',' << targeting->targets.size();
    }
    return result.str();
}
struct Window {
    RuntimeWindowState& state; int route; HWND handle = nullptr;
    bool recording = false, serializationRequired = true;
    bool bindCursorCoordinates = false;
    POINT desiredCursorPoint{};
    int fixtureWidth = 1024, fixtureHeight = 768;
    Window(RuntimeWindowState& value, int selected) : state(value), route(selected) {
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass,
            L"Cursor policy private contract", WS_POPUP, -32000, -32000, 1024, 768,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (handle) { windows.emplace(handle, this);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state)); }
    }
    LRESULT dispatch(UINT message, WPARAM wParam, LPARAM lParam) {
        if (!recording || message != WM_SETCURSOR) return DefWindowProcW(handle, message, wParam, lParam);
        if (route == 2) return runtimeWindowProc(handle, message, wParam, lParam);
        return literalCursorWindowProc(route == 0, handle, message, wParam, lParam);
    }
    bool close() {
        if (!handle) return true;
        recording = false; const HWND old = handle;
        SetWindowLongPtrW(old, GWLP_USERDATA, 0);
        const BOOL destroyed = DestroyWindow(old);
        const bool absent = IsWindow(old) == FALSE;
        windows.erase(old); handle = nullptr;
        return destroyed != FALSE && absent;
    }
    ~Window() { if (handle) close(); }
};
LRESULT CALLBACK cursorWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto found = windows.find(window);
    return found == windows.end() ? DefWindowProcW(window, message, wParam, lParam)
        : found->second->dispatch(message, wParam, lParam);
}
struct Sink final : RuntimeCursorUiDiagnosticSink {
    std::vector<Observation> observations;
    std::function<void(const Observation&)> after;
    bool nesting = false;
    std::function<void(HWND, const POINT&)> prepareCursorSample;
    void prepareOwnedFixtureCursorSample(HWND window, const POINT& screen) override {
        if (prepareCursorSample) prepareCursorSample(window, screen);
    }
    void observe(std::string name, std::vector<std::string> arguments, std::string result) override {
        Observation owned{std::move(name), std::move(arguments), std::move(result)};
        observations.push_back(owned);
        if (after && !nesting) { nesting = true; after(owned); nesting = false; }
    }
};
struct SinkBinding {
    RuntimeCursorUiDiagnosticSink* previous = runtimeCursorUiDiagnosticSink;
    explicit SinkBinding(RuntimeCursorUiDiagnosticSink& value) { runtimeCursorUiDiagnosticSink = &value; }
    ~SinkBinding() { runtimeCursorUiDiagnosticSink = previous; }
};
struct Saved {
    std::vector<Observation> observations;
    std::vector<std::string> states;
    std::vector<std::intptr_t> results;
    std::vector<std::uintptr_t> cursors;
    std::vector<RuntimePointerIngressSmokeDetail::Command> commands;
    struct Serialization {
        bool result = false; std::string reason, bytes;
        bool operator==(const Serialization&) const = default;
    };
    std::vector<Serialization> serializations;
    std::vector<std::size_t> clockReads;
    bool operator==(const Saved&) const = default;
};
struct CursorRestore {
    HCURSOR original = GetCursor();
    HCURSOR replaced = nullptr; bool restored = false;
    bool restore() { replaced = SetCursor(original); restored = GetCursor() == original; return restored; }
    ~CursorRestore() { if (!restored) restore(); }
};
struct BorrowResources {
    RuntimeWindowState& state; std::string original;
    BorrowResources(RuntimeWindowState& value, const std::string& resources)
        : state(value), original(value.config.localDataRootPath) { state.config.localDataRootPath = resources; }
    ~BorrowResources() { state.config.localDataRootPath = original; }
};
bool writeNewBytes(const std::filesystem::path& path, const std::string& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool result = true; std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1U << 20));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || written != count) { result = false; break; }
        offset += written;
    }
    return CloseHandle(file) != FALSE && result;
}
} // namespace RuntimeCursorSmokeDetail

bool runRuntimeCursorSmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
    using namespace RuntimeCursorSmokeDetail;
    std::size_t checks = 0, failures = 0, triples = 0, inputs = 0, ordinaryLoads = 0;
    std::size_t ordinaryCases = 0, diagnosticReentrantCases = 0, naturalBuildings = 0;
    const auto need = [&](bool condition, const std::string& label) {
        ++checks; if (!condition) { ++failures; output << "Cursor failure: " << label << '\n'; }
        return condition;
    };
    char* requested = nullptr; std::size_t requestedLength = 0;
    const int environment = _dupenv_s(&requested, &requestedLength, "CONCORD_CURSOR_OUTPUT");
    const std::filesystem::path directory = requested ? requested : ""; std::free(requested);
    if (!need(environment == 0 && !directory.empty(), "explicit private output directory")) return false;
    std::error_code directoryError; std::filesystem::create_directories(directory, directoryError);
    if (!need(!directoryError, "private output directory")) return false;
    const auto tracePath = directory / "cursor-trace.txt";
    if (!need(!std::filesystem::exists(tracePath), "new complete cursor trace")) return false;
    std::ofstream trace(tracePath, std::ios::binary | std::ios::out);
    if (!need(static_cast<bool>(trace), "trace opened")) return false;
    if (!need(stock.report.success && stock.world.getSummary().loaded && stock.rulesLoaded
        && stock.artLoaded && stock.theaterLoaded, "actual package rules/art/theater/world")) return false;
    const std::string resources = stock.config.localDataRootPath;
    const auto privateRuntimeRoot = directory / "private-runtime";
    if (!need(!std::filesystem::exists(privateRuntimeRoot), "probe-private runtime root must be new")) return false;
    std::filesystem::create_directories(privateRuntimeRoot, directoryError);
    if (!need(!directoryError, "private runtime root created")) return false;
    Registration registration;
    if (!need(registration.ready, "own unique cursor class registered")) return false;
    CursorRestore threadCursorRestore;
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
    std::size_t sampleBoundQueries = 0;
    const auto placeCursorByMovingOwnWindow = [&](Window& window, POINT desired, int width = 1024, int height = 768) {
        window.desiredCursorPoint = desired;
        window.fixtureWidth = width; window.fixtureHeight = height;
        POINT screen{}; const BOOL positioned = GetCursorPos(&screen);
        const BOOL moved = positioned ? SetWindowPos(window.handle, nullptr, screen.x - desired.x,
            screen.y - desired.y, width, height, SWP_NOACTIVATE | SWP_NOZORDER) : FALSE;
        POINT actual{}; const BOOL reread = GetCursorPos(&actual);
        const BOOL converted = reread ? ScreenToClient(window.handle, &actual) : FALSE;
        RECT client{}; const BOOL measured = GetClientRect(window.handle, &client);
        POINT anchored = screen;
        const BOOL anchoredConverted = positioned && moved ? ScreenToClient(window.handle, &anchored) : FALSE;
        trace << "coordinatePrecondition route=" << window.route << " hwnd="
            << reinterpret_cast<std::uintptr_t>(window.handle) << " positioned=" << positioned
            << " moved=" << moved << " reread=" << reread << " converted=" << converted
            << " measured=" << measured << " actual=" << actual.x << ',' << actual.y
            << " desired=" << desired.x << ',' << desired.y << " client=" << client.left << ','
            << client.top << ',' << client.right << ',' << client.bottom
            << " anchoredConverted=" << anchoredConverted << " anchored=" << anchored.x << ',' << anchored.y << '\n';
        // Independent global samples can differ. Prove the mapping of the
        // captured original screen sample here, and the real handler below.
        window.bindCursorCoordinates = positioned && moved && reread && converted && measured
            && anchoredConverted && anchored.x == desired.x && anchored.y == desired.y
            && client.left == 0 && client.top == 0 && client.right == width && client.bottom == height;
        return need(window.bindCursorCoordinates, "real successful coordinate APIs and captured-sample mapping before legal input");
    };
    using Body = std::function<void(RuntimeWindowState&, Window&, Sink&, const std::function<void(int)>&)>;
    const auto run = [&](const std::string& name, RuntimeWindowState& seed, const Body& body, bool diagnostic = false) {
        std::array<Saved, 3> saved;
        for (int route = 0; route < 3; ++route) {
            const auto label = name + "-" + std::to_string(route);
            RuntimePointerIngressSmokeDetail::Clock clock;
            auto storage = clone(seed, label, &clock); if (!storage) return;
            auto& state = *storage; RuntimeSnapshotActiveStateScope active(state);
            RuntimePointerIngressSmokeDetail::ClockBinding clockBinding(state, clock);
            Window window(state, route);
            if (!need(window.handle != nullptr, "own private cursor window " + label)) return;
            const auto roleStartedUtc = utcNow();
            trace << "roleStart case=" << name << " route=" << route << " utc=" << roleStartedUtc
                << " hwnd=" << reinterpret_cast<std::uintptr_t>(window.handle)
                << " class=ConcordCursorPolicySmoke pid=" << GetCurrentProcessId() << " tid=" << GetCurrentThreadId() << '\n';
            // Own private-window witness residency only. No clock read, input,
            // simulation tick, message pumping or performance claim is added.
            Sleep(50);
            trace << "witnessResidency case=" << name << " route=" << route << " milliseconds=50 tick=" << state.tick << '\n';
            CursorRestore restore; const HCURSOR baselinePrevious = SetCursor(nullptr);
            trace << "cursorBaseline case=" << name << " route=" << route << " original="
                << reinterpret_cast<std::uintptr_t>(restore.original) << " requested=0 previous="
                << reinterpret_cast<std::uintptr_t>(baselinePrevious) << " actual="
                << reinterpret_cast<std::uintptr_t>(GetCursor()) << '\n';
            RuntimePointerIngressSmokeDetail::Witness witness(state, 1);
            Sink sink; SinkBinding binding(sink); window.recording = true;
            struct CoordinateBinding {
                POINT screen{}, desired{};
                int width = 0, height = 0;
                std::size_t queryIndex = 0;
                BOOL moved = FALSE;
                bool owned = false;
            };
            std::vector<CoordinateBinding> coordinateBindings;
            coordinateBindings.reserve(4);
            sink.prepareCursorSample = [&](HWND target, const POINT& screen) {
                const auto ownedWindow = windows.find(target);
                const bool owned = target == window.handle && ownedWindow != windows.end()
                    && ownedWindow->second == &window && window.recording && window.bindCursorCoordinates;
                const auto x = std::int64_t(screen.x) - window.desiredCursorPoint.x;
                const auto y = std::int64_t(screen.y) - window.desiredCursorPoint.y;
                BOOL moved = FALSE;
                if (owned && x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max()
                    && y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max())
                    moved = SetWindowPos(target, nullptr, static_cast<int>(x), static_cast<int>(y),
                        0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
                coordinateBindings.push_back({screen, window.desiredCursorPoint,
                    window.fixtureWidth, window.fixtureHeight, sink.observations.size(), moved, owned});
            };
            const auto serialize = [&](const std::string& phase) {
                Saved::Serialization row; std::ostringstream bytes;
                row.result = writeRuntimeStateToStream(state, bytes, row.reason); row.bytes = bytes.str();
                const auto file = directory / (label + "-" + phase + "-" + std::to_string(saved[route].serializations.size()) + ".ini");
                const bool fileWritten = writeNewBytes(file, row.bytes);
                need(fileWritten, "complete new original-format serialization file " + label + " " + phase);
                trace << "serialization case=" << name << " route=" << route << " phase=" << phase
                    << " originalResult=" << row.result << " originalReason="
                    << RuntimePointerIngressSmokeDetail::hex(row.reason) << " bytes=" << row.bytes.size()
                    << " file=" << RuntimePointerIngressSmokeDetail::hex(pathString(file))
                    << " fileWritten=" << fileWritten << '\n';
                if (window.serializationRequired) need(row.result, "healthy original complete serializer succeeds " + label + ": " + row.reason);
                saved[route].serializations.push_back(std::move(row));
            };
            const auto send = [&](int hit) {
                const auto before = snapshot(state); saved[route].states.push_back(before);
                serialize("before");
                saved[route].clockReads.push_back(clock.reads);
                LRESULT result = 0;
                {
                    BorrowResources originalSynchronousCursorChain(state, resources);
                    result = SendMessageW(window.handle, WM_SETCURSOR,
                        reinterpret_cast<WPARAM>(window.handle), MAKELPARAM(hit, WM_MOUSEMOVE));
                }
                saved[route].results.push_back(result);
                saved[route].cursors.push_back(reinterpret_cast<std::uintptr_t>(GetCursor()));
                saved[route].states.push_back(snapshot(state)); ++inputs;
                serialize("after");
                saved[route].clockReads.push_back(clock.reads);
                trace << "input case=" << name << " route=" << route << " hwnd="
                    << reinterpret_cast<std::uintptr_t>(window.handle) << " hit=" << hit
                    << " result=" << result << " cursor=" << saved[route].cursors.back()
                    << " diagnosticReentry=" << diagnostic << '\n';
            };
            body(state, window, sink, send);
            const auto actualPointQueries = std::count_if(sink.observations.begin(), sink.observations.end(),
                [](const auto& row) { return row.name == "cursorPoint"; });
            need(coordinateBindings.size() == static_cast<std::size_t>(actualPointQueries),
                "each actual original cursorPoint has one owned sample-bound fixture mapping " + label);
            for (const auto& sample : coordinateBindings) {
                const auto desired = std::to_string(sample.desired.x) + "," + std::to_string(sample.desired.y);
                const bool pointPresent = sample.queryIndex < sink.observations.size();
                const bool rectPresent = sample.queryIndex + 1 < sink.observations.size();
                need(sample.owned && sample.moved, "sample binding moves only the owned hidden NOACTIVATE window " + label);
                if (need(pointPresent && rectPresent, "sample binding retains original point and adjacent client query " + label)) {
                    const auto& point = sink.observations[sample.queryIndex];
                    const auto& rect = sink.observations[sample.queryIndex + 1];
                    need(point.name == "cursorPoint" && point.arguments == std::vector<std::string>{"1", "1"}
                        && point.result == desired, "actual original GCP/STC reaches the exact planned signed point " + label);
                    need(rect.name == "clientRect" && rect.arguments == std::vector<std::string>{"1"}
                        && rect.result == "0,0," + std::to_string(sample.width) + "," + std::to_string(sample.height),
                        "actual original client query retains planned geometry " + label);
                }
                trace << "sampleBinding case=" << name << " route=" << route << " queryIndex=" << sample.queryIndex
                    << " screen=" << sample.screen.x << ',' << sample.screen.y << " desired=" << desired
                    << " moved=" << sample.moved << " owned=" << sample.owned
                    << " width=" << sample.width << " height=" << sample.height << '\n';
                ++sampleBoundQueries;
            }
            saved[route].observations = sink.observations;
            saved[route].commands = witness.commands;
            for (std::size_t index = 0; index < saved[route].clockReads.size(); ++index)
                trace << "clock case=" << name << " route=" << route << " index=" << index
                    << " reads=" << saved[route].clockReads[index] << " fixtureValue=" << clock.value << '\n';
            for (std::size_t index = 0; index < witness.commands.size(); ++index) {
                const auto& command = witness.commands[index];
                std::ostringstream payload;
                for (const auto value : command.payload)
                    payload << std::hex << std::setfill('0') << std::setw(2) << unsigned(value);
                trace << "command case=" << name << " route=" << route << " index=" << index
                    << " tick=" << command.tick << " accepted=" << command.accepted << " applied=" << command.applied
                    << " payload=" << payload.str() << " error=" << RuntimePointerIngressSmokeDetail::hex(command.error) << '\n';
            }
            trace << "savedStreams case=" << name << " route=" << route
                << " commandCount=" << witness.commands.size() << " clockReadSamples=" << saved[route].clockReads.size()
                << " serializationCount=" << saved[route].serializations.size() << '\n';
            for (std::size_t index = 0; index < sink.observations.size(); ++index) {
                const auto& row = sink.observations[index];
                trace << "query case=" << name << " route=" << route << " index=" << index
                    << " name=" << row.name << " args=";
                for (const auto& argument : row.arguments)
                    trace << RuntimePointerIngressSmokeDetail::hex(argument) << ',';
                trace << " result=" << RuntimePointerIngressSmokeDetail::hex(row.result) << '\n';
            }
            for (std::size_t index = 0; index < saved[route].states.size(); ++index)
                trace << "state case=" << name << " route=" << route << " index=" << index
                    << " owned=" << RuntimePointerIngressSmokeDetail::hex(saved[route].states[index]) << '\n';
            const auto authority = RuntimePointerIngressSmokeDetail::authority(state);
            trace << "routeComplete case=" << name << " route=" << route
                << " authority=" << RuntimePointerIngressSmokeDetail::hex(authority) << '\n';
            const bool destroyed = window.close();
            const bool cursorRestored = restore.restore();
            need(destroyed, "own window destroyed " + label);
            need(cursorRestored, "original thread cursor restored " + label);
            trace << "roleEnd case=" << name << " route=" << route << " utc=" << utcNow()
                << " roleStartedUtc=" << roleStartedUtc << " destroyed=" << destroyed
                << " cursorRestored=" << cursorRestored << " restoreRequested="
                << reinterpret_cast<std::uintptr_t>(restore.original) << " restorePrevious="
                << reinterpret_cast<std::uintptr_t>(restore.replaced) << " actualCursor="
                << reinterpret_cast<std::uintptr_t>(GetCursor()) << '\n';
        }
        need(saved[0] == saved[1], name + " old/direct all ordered observations/results/cursors/owned state");
        need(saved[0] == saved[2], name + " old/native all ordered observations/results/cursors/owned state");
        ++triples; if (diagnostic) ++diagnosticReentrantCases; else ++ordinaryCases;
    };
    for (int mode = 0; mode < 7; ++mode)
        run("gate-" + std::to_string(mode), *prototype, [&](auto& state, auto& window, auto&, const auto& send) {
            if (mode == 0) SetWindowLongPtrW(window.handle, GWLP_USERDATA, 0);
            if (mode == 1) { state.world = GameWorld{}; window.serializationRequired = false; }
            if (mode == 2) state.runtimeMenuOpen = true;
            if (mode == 3) state.skirmishMatchFinished = true;
            if (mode == 4) state.playerCommunication.diplomacyOpen = true;
            if (mode == 5) state.playerCommunication.chatOpen = true;
            if (mode < 5 || placeCursorByMovingOwnWindow(window, {100, 100})) send(mode == 6 ? HTCAPTION : HTCLIENT);
        });
    for (const POINT point : {POINT{100, 100}, POINT{-1, 20}, POINT{1025, 20}, POINT{1024, 768}, POINT{0, 0}})
        run("signed-point-" + std::to_string(point.x) + "-" + std::to_string(point.y), *prototype,
            [&](auto&, auto& window, auto&, const auto& send) { if (placeCursorByMovingOwnWindow(window, point)) send(HTCLIENT); });
    run("ordinary-active-load-first-cursor", *prototype, [&](auto& state, auto& window, auto&, const auto& send) {
        state.config.shortGame = true; (void)advanceRuntimeParticles(state);
        if (!need(state.tick == 21 && state.particlesInitialized && state.particles.currentTick() == 21
            && !state.campaignMode && !runtimeLocalObserver(state.config) && !state.skirmishMatchFinished,
            "ordinary active natural tick21 save preconditions")) return;
        const std::string resourceRoot = state.config.localDataRootPath;
        const auto privateRoot = directory / ("ordinary-active-save-" + std::to_string(window.route));
        std::error_code saveError; std::filesystem::create_directories(privateRoot, saveError);
        if (!need(!saveError, "private ordinary save directory")) return;
        state.config.localDataRootPath = pathString(privateRoot);
        const auto savePath = runtimeDefaultSavePath(state); std::string reason;
        if (!need(!std::filesystem::exists(savePath), "new ordinary save file")) { state.config.localDataRootPath = resourceRoot; return; }
        const RuntimeUiClock* borrowedClock = state.uiClock;
        const bool saved = saveRuntimeState(state, savePath, reason);
        bool loaded = false;
        if (saved) { BorrowResources originalLoadChain(state, resources); loaded = loadRuntimeState(state, savePath, reason); }
        state.config.localDataRootPath = resourceRoot;
        trace << "ordinaryLoad route=" << window.route << " saved=" << saved << " loaded=" << loaded
            << " file=" << RuntimePointerIngressSmokeDetail::hex(savePath) << " reason="
            << RuntimePointerIngressSmokeDetail::hex(reason) << '\n';
        if (!need(saved && loaded && state.tick == 21 && state.uiClock == borrowedClock,
            "real ordinary save/load with original borrowed clock: " + reason)) return;
        state.runtimeMenuOpen = false; state.playerCommunication.chatOpen = false;
        state.playerCommunication.diplomacyOpen = false;
        invalidateRuntimeObjectProjectionCache(state);
        if (!placeCursorByMovingOwnWindow(window, {100, 100}, 800, 600)) return;
        RECT client{}; if (!need(GetClientRect(window.handle, &client) != FALSE, "fresh post-load geometry")) return;
        state.cameraClampViewport = runtimeMapViewport(client); clampRuntimeMapCamera(state, state.cameraClampViewport);
        send(HTCLIENT); ++ordinaryLoads;
    });
    // Every item is queued, naturally produced, legally placed and fully built.
    // No lane clock, catalog, authority, power or SW readiness is manufactured.
    RuntimePointerIngressSmokeDetail::Clock naturalPrototypeClock;
    auto superPrototype = clone(*prototype, "natural-superweapon-prototype", &naturalPrototypeClock);
    if (!superPrototype) return false;
    {
        auto& state = *superPrototype; RuntimeSnapshotActiveStateScope active(state);
        BorrowResources originalProductionDependencyChain(state, resources);
        if (!need(deployRuntimeMcv(state, state.economy.skirmishStartObjectId, false), "real starting MCV deployment")) return false;
        const auto build = [&](const std::string& type) {
            const auto* found = findRuntimeGameplayCatalogItem(state, state.economy.owner, type);
            if (!need(found != nullptr, "actual initialized catalog item " + type)) return false;
            const RuntimeBuildCatalogItem item = *found;
            if (!need(queueRuntimeBuildOrder(state, item), "real legal queue accepted " + type)) return false;
            const int budget = std::max(1, state.config.fixedTickHz) * 60 * 10;
            for (int step = 0; step < budget && !runtimeReadyProductionForItem(state, item); ++step) {
                ++state.tick; (void)advanceRuntimeSimulation(state, false);
            }
            const auto ready = runtimeOldestReadyPlacement(state);
            if (!need(runtimeReadyProductionForItem(state, item) && ready && ready->item
                && ready->item->typeId == type, "natural original lane ready " + type)) return false;
            if (!need(beginRuntimePlacementFromUi(state, *ready, true), "real placement selection " + type)) return false;
            const auto cell = findRuntimePlacementCellNearBase(state, item);
            if (!need(cell.has_value(), "actual legal placement cell " + type)) return false;
            const auto previous = state.world.getObjects().size();
            if (!need(placeRuntimeReadyBuilding(state, *cell) && state.world.getObjects().size() > previous,
                "real placement adds building " + type)) return false;
            int objectId = -1;
            for (std::size_t index = previous; index < state.world.getObjects().size(); ++index) {
                const auto& object = state.world.getObjects()[index];
                if (object.alive && object.type == type && object.owner == state.economy.owner) objectId = object.instanceId;
            }
            bool completed = false;
            for (int step = 0; step < budget; ++step) {
                const auto* object = findObjectById(state, objectId);
                if (!need(object && object->alive, "real building remains alive " + type)) return false;
                if (object->buildAnimTicksRemaining == 0) { completed = true; break; }
                ++state.tick; (void)advanceRuntimeSimulation(state, false);
            }
            (void)completeRuntimeWorldRefresh(state);
            trace << "naturalBuilding type=" << type << " object=" << objectId << " completed=" << completed
                << " tick=" << state.tick << " power=" << state.economy.powerProduced << ',' << state.economy.powerUsed << '\n';
            if (completed) ++naturalBuildings;
            return need(completed, "natural construction completed " + type);
        };
        int extraPowerPlants = 0;
        for (const std::string type : {"GAPOWR", "GAPOWR", "GAPOWR", "GAPILE", "GAREFN", "GAWEAP", "AMRADR", "GATECH", "GACSPH", "GAWEAT"}) {
            if (!build(type)) return false;
            while (state.economy.powerProduced < state.economy.powerUsed && extraPowerPlants < 12) {
                if (!build("GAPOWR")) return false;
                ++extraPowerPlants;
            }
            if (!need(state.economy.powerProduced >= state.economy.powerUsed,
                "real completed power sources cover actual stock drain")) return false;
        }
        ensureRuntimeSuperWeaponsInitialized(state);
        const int budget = std::max(1, state.config.fixedTickHz) * 60 * 30;
        bool bothReady = false;
        for (int step = 0; step < budget; ++step) {
            bothReady = state.superWeapons.stateOf(state.economy.owner, "ChronoSphereSpecial") == SuperWeaponRuntimeState::Ready
                && state.superWeapons.stateOf(state.economy.owner, "LightningStormSpecial") == SuperWeaponRuntimeState::Ready;
            if (bothReady) break;
            ++state.tick; (void)advanceRuntimeSimulation(state, false);
        }
        if (!need(bothReady, "real Chronosphere and Weather Control naturally charge from actual completed source buildings")) return false;
        trace << "naturalSuperWeapons tick=" << state.tick << " state="
            << RuntimePointerIngressSmokeDetail::hex(snapshot(state)) << '\n';
    }
    for (const std::string id : {"ChronoSphereSpecial", "LightningStormSpecial"})
        for (const bool routed : {false, true}) for (int scenario = 0; scenario < 4; ++scenario) {
            const bool reentrant = scenario != 0;
            const bool requiredPreview = scenario == 1 || scenario == 2;
            const bool requiredObject = scenario == 1;
            run(id + (routed ? "-router" : "-local") + (reentrant
                    ? requiredObject ? "-diagnostic-reentrant-valid-object-preview"
                        : requiredPreview ? "-diagnostic-reentrant-valid-noobject-preview" : "-diagnostic-reentrant-invalid-noobject-preview"
                    : "-stable-local-hidden"),
                *superPrototype, [&](auto& state, auto& window, auto& sink, const auto& send) {
                    state.offlineGameplayIntentRouting = routed;
                    if (!need(state.superWeapons.stateOf(state.economy.owner, id) == SuperWeaponRuntimeState::Ready,
                        "clone retains naturally Ready actual SW")) return;
                    if (!need(!state.skirmishMatchFinished, "natural fixture remains an active match")) return;
                    POINT desired{100, 100};
                    int expectedObjectId = -1;
                    if (reentrant) {
                        const bool requiredGeometryPreview = true;
                        BorrowResources originalGeometryDependencyChain(state, resources);
                        RECT client{};
                        if (!need(GetClientRect(window.handle, &client) != FALSE,
                            "real client before preview geometry fixture")) return;
                        const RECT viewport = runtimeMapViewport(client);
                        if (requiredObject) {
                            const auto candidate = std::find_if(state.world.getObjects().begin(), state.world.getObjects().end(),
                                [&](const auto& object) { return object.alive && object.kind != WorldObjectKind::Structure
                                    && object.owner == state.economy.owner; });
                            if (!need(candidate != state.world.getObjects().end(), "real naturally started surviving owned unit")) return;
                            centerRuntimeCameraOnCell(state, client, {candidate->x, candidate->y});
                        }
                        ensureRuntimeCameraForViewport(state, viewport);
                        const auto& scene = runtimeProjectedObjectsForViewport(state, viewport);
                        bool found = false;
                        const int stride = requiredObject ? 2 : 8;
                        for (int y = viewport.top; y <= viewport.bottom && !found; y += stride)
                            for (int x = viewport.left; x <= viewport.right && !found; x += stride) {
                                const POINT candidate{x, y};
                                const auto cell = approximateCellFromScreenPoint(state, viewport, candidate);
                                const auto* object = findProjectedObjectAtPoint(scene, candidate);
                                const bool actualObject = object && object->object;
                                if (actualObject != requiredObject) continue;
                                const int objectId = object && object->object
                                    && !runtimeSuperWeaponTargetsGroundArea(state, id)
                                    ? object->object->instanceId : -1;
                                auto context = runtimeSuperWeaponContext(state); std::string reason;
                                const bool valid = state.superWeapons.previewTarget(state.economy.owner, id,
                                    {{cell.x, cell.y}, objectId}, context, &reason);
                                if (valid == requiredGeometryPreview) {
                                    desired = candidate; found = true; expectedObjectId = objectId;
                                    trace << "previewFixture route=" << window.route << " id=" << id
                                        << " required=" << requiredGeometryPreview << " actual=" << valid
                                        << " point=" << x << ',' << y << " cell=" << cell.x << ',' << cell.y
                                        << " objectId=" << objectId << " groundArea="
                                        << runtimeSuperWeaponTargetsGroundArea(state, id) << " actualObject=" << actualObject << " reason="
                                        << RuntimePointerIngressSmokeDetail::hex(reason) << '\n';
                                }
                            }
                        if (!need(found, "real geometry and original preview establish legal object/noobject target before diagnostic transition")) return;
                    }
                    const auto begin = [&]() {
                        BorrowResources originalTargetingDependencyChain(state, resources);
                        std::string reason; bool begun = false;
                        if (routed) begun = state.multiplayerIntentRouter.beginSuperWeapon(id, id == "ChronoSphereSpecial" ? 2 : 1, reason);
                        else { auto context = runtimeSuperWeaponContext(state);
                            begun = state.superWeapons.beginTargetSelection(state.economy.owner, id, context, &reason); }
                        trace << "actualTargetSelection route=" << window.route << " id=" << id
                            << " routed=" << routed << " diagnosticReentry=" << reentrant << " accepted=" << begun
                            << " reason=" << RuntimePointerIngressSmokeDetail::hex(reason) << '\n';
                        need(begun, "actual original target selection accepted: " + reason);
                    };
                    bool injected = false;
                    bool disableAttempted = false, disabled = false;
                    if (reentrant) {
                        sink.after = [&](const Observation& row) {
                            if (!injected && row.name == "activeLocalSuperWeaponTargeting" && row.result == "0") {
                                injected = true; begin();
                            }
                            if (!requiredPreview && injected && !disableAttempted
                                && row.name == "superWeaponSnapshots") {
                                disableAttempted = true;
                                const std::string ownedOwner = state.economy.owner;
                                const auto replayState = [&]() {
                                    std::ostringstream saved;
                                    saved << state.nextReplayEventSequence << ';'
                                        << state.snapshotCache.replayEventSnapshotsDirty << ';'
                                        << RuntimePointerIngressSmokeDetail::hex(state.simulationStatus) << ';';
                                    for (const auto& event : state.recentReplayEvents) {
                                        const auto text = [&](const std::string& value) {
                                            saved << RuntimePointerIngressSmokeDetail::hex(value) << ',';
                                        };
                                        const auto cell = [&](const RuntimeSnapshotCell& value) {
                                            saved << value.x << ',' << value.y << ',';
                                        };
                                        saved << event.sequence << ',' << event.tick << ',';
                                        text(event.kind); saved << event.objectId << ',' << event.targetObjectId << ','
                                            << event.projectileActorId << ','; text(event.commandVerb);
                                        saved << event.selectedCount << ',' << event.acceptedCount << ','
                                            << event.rejectedCount << ','; cell(event.commandTargetCell);
                                        text(event.missionFrom); text(event.missionTo); text(event.reason);
                                        text(event.weaponId); text(event.weaponSlot); text(event.projectileId);
                                        text(event.projectileImageId); text(event.warheadId); text(event.lifecycleState);
                                        saved << event.spawnTick << ',' << event.impactTick << ',' << event.expiredTick << ',';
                                        text(event.renderIntentId); saved << event.renderIntentHash << ',';
                                        text(event.trailerIntentId); text(event.impactIntentId); text(event.warheadImpactIntentId);
                                        cell(event.sourceCell); cell(event.targetCell); cell(event.currentCell);
                                        text(event.superWeaponId); text(event.superWeaponEvent); cell(event.secondTargetCell);
                                        saved << ';';
                                    }
                                    return saved.str();
                                };
                                const std::string replayBefore = replayState();
                                const int tickBefore = state.tick;
                                const std::uint64_t sequenceBefore = state.nextReplayEventSequence;
                                {
                                    BorrowResources originalDisableDependencyChain(state, resources);
                                    auto context = runtimeSuperWeaponContext(state);
                                    context.emit = runtimeSuperWeaponEventSink(state);
                                    disabled = state.superWeapons.disable(ownedOwner, id, &context);
                                }
                                const bool unavailable = state.superWeapons.stateOf(ownedOwner, id)
                                    == SuperWeaponRuntimeState::Unavailable;
                                const bool replayObserved = !state.recentReplayEvents.empty()
                                    && state.nextReplayEventSequence == sequenceBefore + 1
                                    && state.recentReplayEvents.back().sequence == sequenceBefore
                                    && state.recentReplayEvents.back().tick == tickBefore
                                    && state.recentReplayEvents.back().kind == "SuperWeapon"
                                    && state.recentReplayEvents.back().superWeaponId == id
                                    && state.recentReplayEvents.back().superWeaponEvent == "SuperWeaponDisabled"
                                    && state.recentReplayEvents.back().lifecycleState == "Unavailable";
                                const bool statusObserved = state.simulationStatus
                                    == "superweapon " + id + " SuperWeaponDisabled";
                                need(disabled, "original diagnostic disable succeeds after owned snapshot");
                                need(unavailable, "original diagnostic disable leaves actual state Unavailable");
                                need(state.tick == tickBefore, "original diagnostic disable does not advance authoritative tick");
                                need(replayObserved && statusObserved && state.snapshotCache.replayEventSnapshotsDirty,
                                    "actual original disable event sink records replay sequence tick state and status");
                                const std::string replayAfter = replayState();
                                sink.observe("diagnosticDisable",
                                    {ownedOwner, id, std::to_string(tickBefore), std::to_string(state.tick),
                                        unavailable ? "1" : "0", replayObserved ? "1" : "0", statusObserved ? "1" : "0",
                                        replayBefore, replayAfter}, disabled ? "1" : "0");
                            }
                        };
                    } else begin();
                    if (!placeCursorByMovingOwnWindow(window, desired)) return;
                    send(HTCLIENT);
                    const bool previewed = std::any_of(sink.observations.begin(), sink.observations.end(),
                        [](const auto& row) { return row.name == "previewSuperWeaponTarget"; });
                    need(previewed == reentrant, "stable path hides; only labeled diagnostic reentry reaches actual preview");
                    if (reentrant) {
                        const auto preview = std::find_if(sink.observations.begin(), sink.observations.end(),
                            [](const auto& row) { return row.name == "previewSuperWeaponTarget"; });
                        if (need(preview != sink.observations.end(), "actual preview observed"))
                            need(preview->result == (requiredPreview ? "1" : "0"), "actual preview retains proven real validity");
                        if (preview != sink.observations.end() && need(preview->arguments.size() == 5,
                            "private preview trace preserves all original owner/id/cell/object/reason arguments"))
                            need(preview->arguments[3] == std::to_string(expectedObjectId),
                                "actual preview object ID retains original picked-object/ground-area short circuit");
                        const auto picked = std::find_if(sink.observations.begin(), sink.observations.end(),
                            [](const auto& row) { return row.name == "pickedProjectionPresent"; });
                        if (need(picked != sink.observations.end(), "actual picker result observed"))
                            need(picked->result == (requiredObject ? "1" : "0"), "actual projected object/noobject fixture domain");
                        if (!requiredPreview) {
                            need(disableAttempted && disabled,
                                "only invalid-preview diagnostic scenario invokes actual disable once");
                            if (preview != sink.observations.end()
                                && need(preview->arguments.size() == 5, "actual disabled preview has original reason argument"))
                                need(preview->arguments[4] == "disabled by game option",
                                    "actual original preview reports precise disabled reason");
                            const auto loadedCursor = std::find_if(sink.observations.begin(), sink.observations.end(),
                                [](const auto& effect) { return effect.name == "loadRuntimeStockCursor"; });
                            need(loadedCursor != sink.observations.end() && loadedCursor->arguments.size() == 1
                                && loadedCursor->arguments[0] == "32648", "actual disabled preview requests original stock forbidden cursor");
                            const auto setCursor = std::find_if(sink.observations.begin(), sink.observations.end(),
                                [](const auto& effect) { return effect.name == "SetCursor"; });
                            need(setCursor != sink.observations.end() && setCursor->arguments.size() == 2
                                && setCursor->arguments[0] == std::to_string(static_cast<unsigned>(RuntimeCursorRequest::StockForbidden)),
                                "actual disabled preview retains original SetCursor semantic request");
                        }
                    }
                }, reentrant);
        }
    run("fresh-owner-and-client-geometry", *prototype, [&](auto& state, auto& window, auto& sink, const auto& send) {
        bool changed = false;
        sink.after = [&](const Observation& row) {
            if (!changed && row.name == "economyOwner") {
                changed = true;
                for (char& value : state.economy.owner)
                    value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
            }
        };
        if (!placeCursorByMovingOwnWindow(window, {100, 100})) return;
        send(HTCLIENT);
        if (!placeCursorByMovingOwnWindow(window, {200, 200}, 800, 600)) return;
        send(HTCLIENT);
    }, true);
    run("fresh-menu-after-owned-world-gate", *prototype, [&](auto& state, auto&, auto& sink, const auto& send) {
        bool injected = false;
        sink.after = [&](const Observation& row) {
            if (!injected && row.name == "worldLoaded" && row.result == "1") { injected = true; state.runtimeMenuOpen = true; }
        };
        send(HTCLIENT);
    }, true);
    need(ordinaryLoads == 3, "all three routes include real ordinary active load first cursor");
    need(triples == 31 && inputs == 96, "all31 cases and96 planned inputs actually execute");
    need(sampleBoundQueries == 75, "all75 original cursorPoint queries use real sample-bound fixture geometry");
    trace << "sampleBindingSummary actualQueries=" << sampleBoundQueries << " expectedQueries=75 plannedInputs=96\n";
    trace << "summary checks=" << checks << " failures=" << failures << " triples=" << triples
        << " inputs=" << inputs << " ordinaryLoads=" << ordinaryLoads << " ordinaryCases=" << ordinaryCases
        << " diagnosticReentrantCases=" << diagnosticReentrantCases << " naturalBuildings=" << naturalBuildings << '\n';
    const bool classRemoved = registration.close();
    const bool cursorRestored = threadCursorRestore.restore();
    need(classRemoved, "own class successfully unregistered and absent");
    need(cursorRestored, "whole-probe original cursor restored");
    trace << "cleanup utc=" << utcNow() << " ownedWindowsRemaining=" << windows.size()
        << " classUnregisteredAndAbsent=" << classRemoved << " wholeCursorRestored=" << cursorRestored
        << " restoreRequested=" << reinterpret_cast<std::uintptr_t>(threadCursorRestore.original)
        << " restorePrevious=" << reinterpret_cast<std::uintptr_t>(threadCursorRestore.replaced)
        << " actualCursor=" << reinterpret_cast<std::uintptr_t>(GetCursor()) << '\n';
    trace.close(); need(static_cast<bool>(trace), "complete trace flush");
    output << "Cursor native " << (failures ? "failed" : "passed") << ": checks=" << checks
        << " oldCases=" << triples << " directCases=" << triples << " nativeCases=" << triples
        << " inputs=" << inputs << " ordinaryActiveLoadCases=" << ordinaryLoads
        << " ordinaryCases=" << ordinaryCases << " diagnosticReentrantCases=" << diagnosticReentrantCases
        << " naturalBuildings=" << naturalBuildings << " failures=" << failures
        << " completeSeedRoundTrips=" << seedRoundTrips
        << " sameProductionCpp=1 completeOldOuter=1 rawPathsRetained=1 physicalInput=not-run IME=not-run"
        << " runningNetwork=not-run performance=not-run allWindowThreadsProven=not-claimed W012FullMatrix=not-covered\n";
    return failures == 0;
}
