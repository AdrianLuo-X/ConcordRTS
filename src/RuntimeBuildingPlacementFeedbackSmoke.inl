// Issue 21: genuine Win32 placement attempts, original advisor assets and the
// existing dedicated MCI EVA channel. No ready-order or audio-device substitute.
bool runRuntimeBuildingPlacementFeedbackSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock; RulesDatabase rules;
    if (!stock.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(stock)) {
        error = "Building placement feedback: original rules unavailable"; return false;
    }
    const char* outputValue = std::getenv("CONCORD_BUILDING_PLACEMENT_FEEDBACK_OUTPUT");
    const char* caseValue = std::getenv("CONCORD_BUILDING_PLACEMENT_FEEDBACK_CASE");
    const std::string output = outputValue ? outputValue : "", filter = caseValue ? caseValue : "";
    if (!output.empty()) std::filesystem::create_directories(output);
    std::ofstream csv;
    if (!output.empty()) csv.open(output + "/placement-feedback.csv");
    if (csv) csv << "case,route,tick,serial,sample,status,requestedVoiceVolume,mciVolumeResult,mciLength,mciPosition1,mciPosition2,captured,credits,objects\n";
    struct Case { const char* country; const char* yard; const char* power; const char* voice; const char* sample; };
    const std::array<Case, 3> cases{{
        {"Americans", "GACNST", "GAPOWR", "Allied", "ceva063"},
        {"Russians", "NACNST", "NAPOWR", "Russian", "csof063"},
        {"YuriCountry", "YACNST", "YAPOWR", "Yuri", "cyur063"}
    }};
    constexpr const char* owner = "SkirmishPlayer1";
    constexpr int width = 1280, height = 720;
    const RECT client{0, 0, width, height}, viewport = runtimeMapViewport(client), sidebar = runtimeSidebarRect(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, ticks = 0, clicks = 0, paidOrders = 0, denied = 0, nativeRed = 0;
    int decodedAssets = 0, mciVoices = 0, fallbackVoices = 0, busyChecks = 0, silentChecks = 0;
    int networkChecks = 0, commits = 0;
    const auto fail = [&](const std::string& why) { error = "Placement " + label + " [" + stage + "]: " + why; return false; };
    int landTile = -1; auto metadata = std::make_unique<RuntimeMapRenderCache>();
    const auto terrainContext = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, terrainContext, *metadata) == 0) { landTile = tile; break; }
    }
    if (landTile < 0) return fail("original flat native TMP unavailable");
    const auto make = [&](const Case& c, int hz, bool fast) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config;
        s.config.fixedTickHz = hz; s.config.playerCountry = c.country; s.config.gameMode = "Battle";
        s.config.noCooldown = fast; s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.crates = false; s.config.startingUnits = 0; s.config.techLevel = 10;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyBindings.clear(); s.config.keyboardReferenceLayout = false;
        s.rules = rules; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 110; ++y) for (int x = 55; x <= 110; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id, const char* type, RuntimeCellPoint cell, const char* house) {
            WorldObjectInstance object; object.instanceId = id; object.mapId = "PlacementFeedback" + std::to_string(id);
            object.kind = WorldObjectKind::Structure; object.type = type; object.owner = house;
            object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
            object.alive = object.poweredOperational = true; object.health = runtimeMaxHealthForObject(s, object);
            object.currentAmmo = -1; object.mission = "Guard"; s.world.accessObjects().push_back(object);
        };
        add(1, c.yard, {72, 72}, owner); add(2, c.power, {80, 72}, owner);
        add(998, "NANRCT", {56, 56}, "SkirmishPlayer2");
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = false;
        s.runtimeSoundsEnabled = false; s.runtimeVoiceVolume = 1; s.runtimeSoundVolume = 1;
        s.runtimeMusicEnabled = false; s.runtimeSoundEffectsEnabled = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = owner; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.local = local.human = true;
        local.name = "Placement local"; local.country = c.country; local.team = "-";
        RuntimeConfig::PlayerSlot enemy; enemy.enabled = true; enemy.country = "Russians"; enemy.team = "-";
        s.config.playerSlots = {local, enemy};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["skirmishplayer1"] = c.country; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = owner; house.canonicalOwner = "skirmishplayer1"; house.country = c.country; house.localFacade = true;
        s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); return storage;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++ticks;
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        ~Input() {
            stopRuntimeEvaPlayback(state); stopRuntimeSfxPlayback(state); stopRuntimeWorldAudioBackend(state);
            state.multiplayerBridge = nullptr; state.multiplayerIntentRouter.setCaptureCallback({});
            if (window) {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
                if (GetCapture() == window) ReleaseCapture();
                releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window);
            }
        }
    };
    const auto move = [](Input& input, POINT point) {
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
    };
    const auto click = [&](Input& input, POINT point, bool right = false) {
        move(input, point);
        SendMessageW(input.window, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN,
            right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(input.window, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y)); ++clicks;
    };
    const auto records = [](const RuntimeWindowState& s, int after, const char* intent) {
        std::vector<RuntimeSoundPlaybackRecord> result;
        for (const auto& record : s.runtimeSoundPlaybackHistory)
            if (record.playbackOrder > static_cast<std::uint64_t>(after) && record.intent == intent) result.push_back(record);
        return result;
    };
    const auto awaitIdle = [&](RuntimeWindowState& s) {
        // MCI time is wall time; simulation priority/busy state is a separate
        // clock. Advance both naturally, never clear a queue or shorten a WAV.
        const auto start = GetTickCount64();
        for (int frame = 0; frame < s.config.fixedTickHz * 12 || GetTickCount64() - start < 4000; ++frame) {
            processRuntimeEvaQueue(s);
            if (!s.runtimeEvaMciChannelActive && !s.runtimeEvaUsingPlaySoundFallback
                && s.tick >= s.runtimeEvaBusyUntilTick && s.runtimeEvaQueue.empty()) return true;
            if (GetTickCount64() - start > 12000) break;
            const bool deviceActive = s.runtimeEvaMciChannelActive || s.runtimeEvaUsingPlaySoundFallback;
            step(s); if (deviceActive) Sleep(10);
            if (!fixtureFailure.empty()) return fail(fixtureFailure);
        }
        return fail("original EVA channel failed to become idle naturally tick=" + std::to_string(s.tick)
            + " busyUntil=" + std::to_string(s.runtimeEvaBusyUntilTick) + " active="
            + std::to_string(s.runtimeEvaMciChannelActive) + " queue=" + std::to_string(s.runtimeEvaQueue.size()));
    };
    struct Wave {
        AudioSampleResolveResult resolved; std::uint32_t frames = 0, rate = 0, averageBytesPerSecond = 0;
        std::size_t decodedBytes = 0, compressedBytes = 0;
    };
    const auto originalWave = [&](RuntimeWindowState& s, const Case& c, Wave& result) {
        if (!s.evaLoaded || s.evaIni.getValue("EVA_CannotDeployHere", c.voice, "") != c.sample
            || toUpperAsciiLocal(trimAsciiLocal(s.evaIni.getValue("EVA_CannotDeployHere", "Priority", ""))) != "LOW"
            || !s.evaIni.getValue("EVA_CannotDeployHere", "Type", "").empty()) return fail("original 063 side/LOW/no-queue contract changed");
        result.resolved = resolveAudioSampleToWave(runtimeAssetRootPath(s.config),
            pathString(std::filesystem::path(s.config.localDataRootPath) / "runtime_audio_cache"), c.sample);
        if (!result.resolved.success || result.resolved.sourcePath.empty() || result.resolved.wavPath.empty())
            return fail("original WAV resolution failed " + result.resolved.errorMessage);
        std::ifstream file(result.resolved.wavPath, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        MixArchive archive; std::vector<std::uint8_t> original;
        if (!archive.loadFromFile(result.resolved.sourcePath)) return fail("original MIX source unavailable");
        const auto* entry = archive.findEntryByName(std::string(c.sample) + ".wav");
        if (!entry || !archive.readEntryData(*entry, original, error) || original != bytes)
            return fail("actual cached WAV differs from original MIX 063 bytes: " + error);
        const auto le16 = [&](std::size_t p) { return std::uint16_t(bytes[p] | (std::uint16_t(bytes[p + 1]) << 8)); };
        const auto le32 = [&](std::size_t p) { return std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p + 1]) << 8)
            | (std::uint32_t(bytes[p + 2]) << 16) | (std::uint32_t(bytes[p + 3]) << 24); };
        if (!isWaveFileBytes(bytes) || bytes.size() < 12 || std::uint64_t(le32(4)) + 8 != bytes.size())
            return fail("original RIFF/WAVE size invalid");
        std::uint16_t format = 0, channels = 0, block = 0, bits = 0;
        std::vector<std::uint8_t> compressed;
        for (std::size_t p = 12; p + 8 <= bytes.size();) {
            const auto n = le32(p + 4); const auto end = std::uint64_t(p) + 8 + n;
            if (end > bytes.size()) return fail("original WAV chunk overruns RIFF");
            const std::string id(reinterpret_cast<const char*>(bytes.data() + p), 4);
            if (id == "fmt ") {
                if (n < 20) return fail("original IMA fmt too short");
                format = le16(p + 8); channels = le16(p + 10); result.rate = le32(p + 12);
                result.averageBytesPerSecond = le32(p + 16);
                block = le16(p + 20); bits = le16(p + 22);
            } else if (id == "fact") {
                if (n < 4) return fail("original fact too short"); result.frames = le32(p + 8);
            } else if (id == "data") compressed.assign(bytes.begin() + p + 8, bytes.begin() + static_cast<std::size_t>(end));
            p = static_cast<std::size_t>(end + (n & 1U));
        }
        if (format != 0x11 || channels != 1 || result.rate != 22050 || bits != 4 || block < 4
            || result.frames == 0 || result.averageBytesPerSecond == 0 || compressed.empty())
            return fail("original 063 IMA/mono/22050/FACT/data contract invalid");
        const auto decoded = decodeImaAdpcmBagSampleToWave(compressed, result.rate, channels, block);
        if (!decoded.success || decoded.pcmBytes.size() < std::size_t(result.frames) * channels * 2
            || std::none_of(decoded.pcmBytes.begin(), decoded.pcmBytes.end(), [](auto value) { return value != 0; }))
            return fail("original ADPCM payload did not decode real voice: " + decoded.errorMessage);
        result.decodedBytes = decoded.pcmBytes.size(); result.compressedBytes = compressed.size(); ++decodedAssets; return true;
    };
    const auto mciQuery = [](const wchar_t* command, std::wstring& value) {
        wchar_t buffer[256]{}; const auto code = mciSendStringW(command, buffer, 256, nullptr);
        value = buffer; return code;
    };
    const auto mciNumber = [](const std::wstring& value) -> long {
        if (value.empty() || value.size() > 9) return -1;
        long number = 0;
        for (const auto ch : value) {
            if (ch < L'0' || ch > L'9') return -1;
            number = number * 10 + static_cast<long>(ch - L'0');
        }
        return number;
    };
    const auto run = [&](const Case& c, bool fast) {
        const int hz = fast ? 60 : 30; auto storage = make(c, hz, fast); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s); Input input{s};
        input.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Original building placement feedback",
            WS_POPUP, -16000, -16000, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("hidden real Win32 input window unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        HDC screen = GetDC(nullptr); const bool dib = screen && ensureRuntimeBackBuffer(s.backBuffer, screen, width, height);
        if (screen) ReleaseDC(nullptr, screen);
        if (!input.original || !dib || !s.world.getSummary().loaded) return fail("original input/world/DIB unavailable");
        centerRuntimeCameraOnCell(s, client, {75, 74}); ensureRuntimeCameraForViewport(s, viewport);
        if (!s.camera.initialized || runtimeUsesGameplayIntentRouter(s) || runtimeNoCooldownForOwner(s, owner) != fast)
            return fail("direct/local/clock fixture authority invalid");
        Wave wave; if (!originalWave(s, c, wave)) return false;
        const auto catalog = std::find_if(s.economy.catalog.begin(), s.economy.catalog.end(), [&](const auto& item) { return item.typeId == c.power; });
        if (catalog == s.economy.catalog.end()) return fail("original power cameo absent");
        const RuntimeBuildCatalogItem item = *catalog;
        if (item.cost != parseRuntimeIntOrDefault(stock.getValue(c.power, "Cost", "0"), 0)
            || item.cost <= 0 || !runtimeBuildAvailability(s, item).canQueue)
            return fail("original paid prerequisite/factory invalid: " + runtimeBuildAvailability(s, item).reason);
        const auto cameo = [&]() -> std::optional<POINT> {
            s.activeBuildTab = RuntimeBuildKind::Building;
            for (int page = 0; page < runtimeBuildPageCount(s, sidebar); ++page) {
                runtimeSetActiveBuildPage(s, page); const auto visible = runtimeVisibleBuildCatalogIndices(s, sidebar);
                for (std::size_t n = 0; n < visible.size(); ++n) if (s.economy.catalog[visible[n]].typeId == c.power) {
                    const auto r = runtimeBuildButtonRect(sidebar, static_cast<int>(n));
                    return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                }
            }
            return std::nullopt;
        };
        const auto icon = cameo(); if (!icon) return fail("actual visible paid production button absent");
        stage = "real paid sidebar production"; const int creditsBefore = s.economy.credits;
        click(input, *icon);
        for (int frame = 0; frame < item.buildTicks * 3 + hz * 3 && !runtimeOldestReadyPlacement(s); ++frame) step(s);
        const auto ready = runtimeOldestReadyPlacement(s);
        if (!fixtureFailure.empty()) return fail(fixtureFailure);
        if (!ready || ready->order.itemKey != c.power || ready->order.creditsSpent != item.cost
            || s.economy.credits != creditsBefore - item.cost || runtimeSelectedPlacement(s))
            return fail("full ticks did not produce one paid unselected original Ready: " + s.economy.status);
        ++paidOrders; const auto serial = ready->order.serial; const auto laneHash = runtimeProductionLanesStateHash(s.economy.localProductionLanes);
        const int paidCredits = s.economy.credits; const auto objectCount = s.world.getObjects().size();
        const auto worldHash = s.world.deterministicSimulationStateHash();
        const auto footprint = runtimeStructureFootprintForBuildItem(s, item);
        const RuntimeCellPoint redCell{72, 72};
        const auto pointFor = [&](RuntimeCellPoint topLeft) -> std::optional<POINT> {
            const auto raw = runtimePlacementPreviewTargetCellForTopLeft(topLeft, footprint);
            centerRuntimeCameraOnCell(s, client, raw); ensureRuntimeCameraForViewport(s, viewport);
            const auto projected = runtimeScreenPointForCell(s, viewport, raw, terrainLevelAtRuntimeCell(s, raw.x, raw.y));
            for (int dy = -10; dy <= 16; ++dy) for (int dx = -6; dx <= 6; ++dx) {
                const POINT p{projected.x + dx, projected.y + dy};
                if (p.x < viewport.left || p.x >= viewport.right || p.y < viewport.top || p.y >= viewport.bottom) continue;
                const auto actual = approximateCellFromScreenPoint(s, viewport, p);
                if (actual.x == raw.x && actual.y == raw.y) return p;
            }
            return std::nullopt;
        };
        const auto retained = [&](bool selected, const char* route) {
            const auto oldest = runtimeOldestReadyPlacement(s), current = runtimeSelectedPlacement(s);
            if (!oldest || oldest->order.serial != serial || oldest->order.creditsSpent != item.cost
                || oldest->order.itemKey != c.power || bool(current) != selected
                || (selected && (current->order.serial != serial || runtimePlacementCellForUi(s) != redCell || runtimePlacementValidForUi(s)))
                || runtimeProductionLanesStateHash(s.economy.localProductionLanes) != laneHash
                || s.economy.credits != paidCredits || s.world.getObjects().size() != objectCount
                || s.world.deterministicSimulationStateHash() != worldHash)
                return fail(std::string(route) + " changed Ready/paid credits/objects/preview serial=" + std::to_string(serial) + " status=" + s.interactionStatus);
            return true;
        };
        const auto selectRed = [&]() -> std::optional<POINT> {
            const auto button = cameo(); if (!button) { fail("ready cameo disappeared"); return std::nullopt; }
            click(input, *button); const auto point = pointFor(redCell);
            if (!point) { fail("actual red footprint target is not in native surface picker"); return std::nullopt; }
            move(input, *point);
            if (!retained(true, "preview")) return std::nullopt;
            const auto cursor = runtimeMouseCursorFrameForPoint(s, client, *point);
            if (!cursor || cursor->frameIndex != 41) { fail("original invalid placement cursor is not mouse frame41"); return std::nullopt; }
            RuntimeShpFrameImage cursorImage; std::string resource;
            if (!loadRuntimeShpFrameFromMixArchive(pathString(std::filesystem::path(runtimeAssetRootPath(s.config)) / "ra2" / "conquer.mix"),
                    s.renderCache, "mouse", 41, cursorImage, resource, error) || resource.empty() || cursorImage.pixels.empty()) {
                fail("original invalid mouse.shp frame unavailable: " + error); return std::nullopt;
            }
            GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels), width * height, 0x00192328U);
            RuntimeMapRenderContext context; drawRuntimePlacementPreview(s.backBuffer.memoryDc, viewport, s, context); GdiFlush();
            const auto center = runtimeScreenPointForCell(s, viewport, redCell, 0);
            if (s.lastPlacementPreviewCells <= 0 || s.lastPlacementPreviewValidCells != 0
                || GetPixel(s.backBuffer.memoryDc, center.x, center.y) != RGB(242, 86, 86)) {
                fail("actual original invalid preview DIB is not red"); return std::nullopt;
            }
            ++nativeRed; return point;
        };
        const auto rejection = [&](int before, const char* route, bool played) {
            const auto sounds = records(s, before, runtimeSoundIntentCannotDeployHere);
            if (sounds.size() != 1 || !records(s, before, runtimeSoundIntentSidebarScold).empty())
                return fail(std::string(route) + " did not emit one original failure EVA without generic Scold");
            const auto& record = sounds.front();
            if (!record.eva || record.section != "EVA_CannotDeployHere" || record.sample != c.sample
                || record.path != wave.resolved.wavPath || !record.playable || record.queueRequested || record.volumePercent != 1
                || record.played != played || (played ? record.status != "played" : record.status != "resolved"))
                return fail(std::string(route) + " wrong original EVA sample/backend record sample=" + record.sample + " status=" + record.status);
            ++denied; return retained(true, route);
        };
        const auto cancel = [&](POINT p, const char* route) {
            const int before = s.runtimeSoundEvents; click(input, p, true);
            if (!records(s, before, runtimeSoundIntentCannotDeployHere).empty()) return fail(std::string(route) + " cancel requested failure EVA");
            ++silentChecks; return retained(false, route);
        };
        stage = "direct red WM attempt, no device substitution";
        auto redPoint = selectRed(); if (!redPoint || !awaitIdle(s)) return false;
        int before = s.runtimeSoundEvents;
        for (int n = 0; n < 4; ++n) move(input, *redPoint);
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(redPoint->x, redPoint->y));
        if (!records(s, before, runtimeSoundIntentCannotDeployHere).empty() || !retained(true, "hover/bare up")) return fail("hover or bare up attempted placement");
        ++silentChecks; before = s.runtimeSoundEvents; click(input, *redPoint);
        if (!rejection(before, "direct-resolved", false) || !cancel(*redPoint, "direct")) return false;

        stage = "real local network-facade red preview captures zero gameplay commands";
        RuntimeMultiplayerRuntimeBridge bridge; int captured = 0;
        s.multiplayerBridge = &bridge;
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto&, std::string&) { ++captured; return true; });
        redPoint = selectRed(); if (!redPoint || !awaitIdle(s)) return false;
        before = s.runtimeSoundEvents; click(input, *redPoint);
        if (!rejection(before, "network-facade-resolved", false) || captured != 0 || !cancel(*redPoint, "network facade") || captured != 0)
            return fail("network red/cancel captured authority or failed local feedback: " + error);
        ++networkChecks;
        // A valid green preview can still fail network capture. It must keep
        // the ordinary Scold, not be misclassified as a red placement failure.
        if (!fast && std::string(c.country) == "Americans") {
            stage = "real green network capture rejection retains generic feedback";
            const auto readyButton = cameo(); if (!readyButton) return fail("network ready cameo absent");
            click(input, *readyButton);
            const auto greenCell = findRuntimePlacementCellNearBase(s, item);
            const auto greenPoint = greenCell ? pointFor(*greenCell) : std::nullopt;
            if (!greenPoint) return fail("real network legal target unavailable");
            move(input, *greenPoint);
            if (!runtimePlacementValidForUi(s)) return fail("native green preview did not become valid");
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto&, std::string& reason) {
                ++captured; reason = "fixture capture unavailable"; return false;
            });
            before = s.runtimeSoundEvents; click(input, *greenPoint);
            const auto unchanged = runtimeSelectedPlacement(s);
            if (captured != 1 || !unchanged || unchanged->order.serial != serial || !runtimePlacementValidForUi(s)
                || runtimePlacementCellForUi(s) != *greenCell
                || s.economy.credits != paidCredits || s.world.getObjects().size() != objectCount
                || s.world.deterministicSimulationStateHash() != worldHash
                || runtimeProductionLanesStateHash(s.economy.localProductionLanes) != laneHash
                || !records(s, before, runtimeSoundIntentCannotDeployHere).empty()
                || records(s, before, runtimeSoundIntentSidebarScold).size() != 1)
                return fail("green capture failure acquired CannotDeployHere or changed Ready/authority");
            if (!cancel(*greenPoint, "green capture rejection") || captured != 1) return false;
            ++networkChecks;
        }
        s.multiplayerBridge = nullptr; s.multiplayerIntentRouter.setCaptureCallback({});

        stage = "actual offline routed 063 EVA on dedicated local MCI";
        int appliedPlacements = 0; initializeRuntimeOfflineGameplayRouting(s);
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
            const bool applied = dispatchRuntimeOfflineGameplayCommand(s, command, reason);
            if (applied && std::get_if<RuntimeMultiplayerGameplayPlaceBuildingCommand>(&command.body)) ++appliedPlacements;
            return applied;
        });
        redPoint = selectRed(); if (!redPoint || !awaitIdle(s)) return false;
        s.runtimeSoundsEnabled = true; before = s.runtimeSoundEvents; click(input, *redPoint);
        if (!rejection(before, "offline-routed-played", true) || appliedPlacements != 0) return false;
        const auto voice = records(s, before, runtimeSoundIntentCannotDeployHere).front();
        std::wstring mode, lengthText, position1Text, position2Text;
        const auto modeError = mciQuery(L"status concord_runtime_eva mode", mode);
        const auto lengthError = mciQuery(L"status concord_runtime_eva length", lengthText);
        const auto positionError = mciQuery(L"status concord_runtime_eva position", position1Text);
        const auto volumeResult = mciSendStringW(L"setaudio concord_runtime_eva volume to 10", nullptr, 0, nullptr);
        long lengthMs = 0, position1 = 0, position2 = 0;
        if (s.runtimeEvaMciChannelActive) {
            if (modeError || lengthError || positionError || mode != L"playing") return fail("real MCI alias did not accept original voice");
            lengthMs = mciNumber(lengthText); position1 = mciNumber(position1Text);
            // The original ADPCM WAV contains a padded final block. This
            // waveaudio driver reports encoded data/average-byte-rate length,
            // not the shorter authored FACT duration. Both are original data.
            const auto deviceMs = (std::uint64_t(wave.compressedBytes) * 1000) / wave.averageBytesPerSecond;
            if (lengthMs <= 0 || std::abs(static_cast<long long>(lengthMs) - static_cast<long long>(deviceMs)) > 1)
                return fail("MCI original compressed WAV length disagrees with data/average-byte-rate");
            Sleep(60);
            if (mciQuery(L"status concord_runtime_eva position", position2Text)) return fail("real MCI progress query failed");
            position2 = mciNumber(position2Text);
            if (position1 < 0 || position2 <= position1 || position2 > lengthMs) return fail("real MCI original voice timeline did not progress");
            ++mciVoices;
        } else if (s.runtimeEvaUsingPlaySoundFallback && voice.played) {
            // Actual fallback acceptance has a different proof boundary. Never
            // label it as MCI decode/position or invent an XAudio source voice.
            ++fallbackVoices; std::cout << "Placement " << label << " PlaySound fallback accepted; MCI timeline unavailable\n";
        } else return fail("played record has no actual MCI or PlaySound backend");
        if (csv) csv << label << ",offline-routed," << s.tick << ',' << serial << ',' << voice.sample << ',' << voice.status
            << ",1," << volumeResult << ',' << lengthMs << ',' << position1 << ',' << position2 << ',' << appliedPlacements
            << ',' << s.economy.credits << ',' << s.world.getObjects().size() << '\n';
        std::cout << "Placement " << label << " original=" << voice.sample << " source=" << wave.resolved.sourcePath
            << " WAV=" << voice.path << " decodedBytes=" << wave.decodedBytes << " MCIlength=" << lengthMs
            << " position=" << position1 << "->" << position2 << " requestedVoiceVolume=1 setaudioResult=" << volumeResult << '\n';
        stage = "existing native LOW/no-queue busy arbitration";
        before = s.runtimeSoundEvents; click(input, *redPoint);
        const auto busy = records(s, before, runtimeSoundIntentCannotDeployHere);
        if (busy.size() != 1 || busy.front().sample != c.sample || busy.front().status != "eva-suppressed-busy"
            || !busy.front().suppressed || busy.front().played || busy.front().queued || busy.front().queueRequested
            || !records(s, before, runtimeSoundIntentSidebarScold).empty() || !retained(true, "busy red"))
            return fail("second genuine click changed original LOW/no-queue busy contract");
        ++busyChecks; s.runtimeSoundsEnabled = false;
        if (!cancel(*redPoint, "offline routed") || appliedPlacements != 0 || !awaitIdle(s)) return false;
        stage = "real legal sidebar/map commit exactly once";
        const auto button = cameo(); if (!button) return fail("ready button lost after failures"); click(input, *button);
        const auto legal = findRuntimePlacementCellNearBase(s, item);
        if (!legal || !runtimeCellCanPlaceSidebarBuilding(s, item, *legal)) return fail("original legal adjacent foundation unavailable");
        const auto legalPoint = pointFor(*legal); if (!legalPoint) return fail("legal cell outside actual picker/viewport");
        move(input, *legalPoint);
        if (!runtimePlacementValidForUi(s) || runtimePlacementCellForUi(s) != *legal) return fail("actual legal WM preview rejected");
        before = s.runtimeSoundEvents; click(input, *legalPoint);
        if (runtimeOldestReadyPlacement(s) || runtimeSelectedPlacement(s) || appliedPlacements != 1
            || s.world.getObjects().size() != objectCount + 1 || s.economy.credits != paidCredits
            || !records(s, before, runtimeSoundIntentCannotDeployHere).empty()
            || records(s, before, runtimeSoundIntentPlacementConfirmed).size() != 1) return fail("valid real click consumed/charged/spawned/sounded incorrectly");
        const auto& placed = s.world.getObjects().back();
        if (!placed.alive || placed.type != c.power || placed.owner != owner || placed.x != legal->x || placed.y != legal->y)
            return fail("actual successful building differs from the selected original footprint");
        const auto confirmed = records(s, before, runtimeSoundIntentPlacementConfirmed).front();
        if (!confirmed.playable || confirmed.section != "PlaceBuilding" || confirmed.eva
            || stripRuntimeSoundComment(stock.getValue("AudioVisual", "BuildingSlam", "")) != "PlaceBuilding")
            return fail("original BuildingSlam=PlaceBuilding confirmation resource missing");
        before = s.runtimeSoundEvents;
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(legalPoint->x, legalPoint->y)); move(input, *legalPoint);
        if (appliedPlacements != 1 || s.world.getObjects().size() != objectCount + 1
            || !records(s, before, runtimeSoundIntentPlacementConfirmed).empty()
            || !records(s, before, runtimeSoundIntentCannotDeployHere).empty()) return fail("bare up repeated a completed placement");
        ++silentChecks; ++commits; return fixtureFailure.empty() || fail(fixtureFailure);
    };
    for (const auto& c : cases) for (const bool fast : {false, true}) {
        label = std::string(c.country) + (fast ? "-60-NoCooldown" : "-30-normal");
        if (!filter.empty() && filter != label) continue;
        stage = "original fixture"; fixtureFailure.clear();
        if (run(c, fast)) { ++scenes; std::cout << "Placement " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    }
    std::cout << "Placement feedback scenes=" << scenes << " fullTicks=" << ticks << " clicks=" << clicks
        << " paid=" << paidOrders << " rejected=" << denied << " nativeRed=" << nativeRed << " originalDecoded=" << decodedAssets
        << " actualMCI=" << mciVoices << " fallback=" << fallbackVoices << " busy=" << busyChecks << " silent=" << silentChecks
        << " networkCaptureZero=" << networkChecks << " commits=" << commits << '\n';
    if (!failures.empty()) { error = failures.front() + " (" + std::to_string(failures.size()) + " failed scenes)"; return false; }
    if (scenes == 0) return fail("case filter selected no scene");
    error.clear(); return true;
}
