// Native regressions for the local, temporary player-defeat HUD.
// The production outcome functions create notices; the reference painter below
// knows only the authored text/style and does not call the production HUD.
struct RuntimePlayerDefeatNoticeSmokeSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    HGDIOBJ previousFont = nullptr;
    HFONT font = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    RuntimePlayerDefeatNoticeSmokeSurface(int w, int h) : width(w), height(h) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* raw = nullptr;
        if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        pixels = static_cast<std::uint32_t*>(raw);
        if (bitmap) previousBitmap = SelectObject(dc, bitmap);
        font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
        if (dc && font) previousFont = SelectObject(dc, font);
        if (dc) SetBkMode(dc, TRANSPARENT);
    }
    ~RuntimePlayerDefeatNoticeSmokeSurface() {
        if (dc && previousFont) SelectObject(dc, previousFont);
        if (dc && previousBitmap) SelectObject(dc, previousBitmap);
        if (font) DeleteObject(font);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    bool valid() const { return dc && bitmap && pixels && font; }
};

struct RuntimePlayerDefeatNoticeSmokeWindow {
    HWND window = nullptr;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordPlayerDefeatNoticeSmoke";
    bool registered = false;
    int width = 0, height = 0;
    RuntimeWindowState& state;
    RuntimePlayerDefeatNoticeSmokeWindow(RuntimeWindowState& value, int w, int h)
        : width(w), height(h), state(value) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = runtimeWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        registered = RegisterClassW(&windowClass) != 0;
        if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        // Hidden and far outside the desktop: real WM_PAINT without mouse hover.
        window = CreateWindowExW(0, className, L"Player defeat notice test",
            WS_POPUP, -32000, -32000, width, height, nullptr, nullptr, instance, nullptr);
        if (window) SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(&state));
    }
    ~RuntimePlayerDefeatNoticeSmokeWindow() {
        if (window) DestroyWindow(window);
        if (registered) UnregisterClassW(className, instance);
    }
    std::vector<std::uint32_t> paint(bool request = true) {
        if (!window) return {};
        if (request) state.repaintRequested = true;
        InvalidateRect(window, nullptr, FALSE);
        SendMessageW(window, WM_PAINT, 0, 0);
        GdiFlush();
        const auto* pixels = static_cast<const std::uint32_t*>(state.backBuffer.pixels);
        if (!pixels || state.backBuffer.width != width || state.backBuffer.height != height)
            return {};
        return {pixels, pixels + width * height};
    }
};

std::wstring runtimePlayerDefeatNoticeSmokeFitName(HDC dc,
    const std::wstring& name, int width) {
    const std::wstring suffix = L"已被击败！";
    const auto fits = [&](const std::wstring& text) {
        SIZE size{};
        return GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size)
            && size.cx <= width;
    };
    if (fits(name + suffix)) return name + suffix;
    std::wstring best = suffix;
    const std::wstring ending = L"…" + suffix;
    if (!fits(ending)) return best;
    best = ending;
    // Deliberately linear, independent of the production binary-search fitter.
    for (std::size_t length = 1; length <= name.size(); ++length) {
        if (length < name.size() && name[length - 1] >= 0xd800
            && name[length - 1] <= 0xdbff && name[length] >= 0xdc00 && name[length] <= 0xdfff)
            continue;
        const auto candidate = name.substr(0, length) + ending;
        if (fits(candidate)) best = candidate;
    }
    return best;
}

bool runRuntimePlayerDefeatNoticeSmoke(RuntimeWindowState& stock, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "Player defeat notice: " + reason;
        return false;
    };
    if (!stock.report.success || !stock.world.getSummary().loaded
        || !stock.rulesLoaded || !stock.artLoaded || !stock.theaterLoaded
        || !stock.rules.findUnitType("UnitType", "AMCV"))
        return fail("real map/rules/art/theater and stock MCV are required");
    if (runtimePlayerDefeatNoticeDurationMs != 15000)
        return fail("authored notice duration must be fifteen seconds");
    const std::size_t capacity = static_cast<std::size_t>(runtimeMultiplayerMatchMaximumSlots);
    if (capacity < 5) return fail("combatant fixture has insufficient player slots");
    const auto make = [&](int mode) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = stock.config;
        state.config.gameMode = "Battle";
        state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive = mode == 1;
        state.config.playerCountry = mode == 2 ? "Observer" : "Americans";
        state.config.showMainMenu = state.config.showSkirmishMenu = false;
        state.config.computerAiEnabled = false;
        state.config.shortGame = true;
        state.config.playerSlots.assign(capacity, {});
        state.report = stock.report;
        state.world = stock.world;
        state.theater = stock.theater;
        state.rules = stock.rules;
        state.art = stock.art;
        state.mapFacts = stock.mapFacts;
        state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
        state.mapFactsLoaded = stock.mapFactsLoaded;
        state.terrainInfoByCell = stock.terrainInfoByCell;
        state.world.accessObjects().clear();
        state.runtimeSoundsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        state.runtimeVoiceVolume = state.runtimeSoundVolume = 0;
        state.runtimeMusicEnabled = false;
        state.visionEnabled = false;
        state.economy.owner = runtimeSkirmishOwnerForSlot(0);
        state.economy.skirmishStartApplied = true;
        state.triggers.bootstrapComplete = true;
        state.tick = 10;
        const std::size_t enabled = mode == 2 ? 5 : 4;
        for (std::size_t slotIndex = 0; slotIndex < enabled; ++slotIndex) {
            auto& slot = state.config.playerSlots[slotIndex];
            slot.enabled = true;
            slot.human = true;
            slot.local = slotIndex == 0;
            slot.country = mode == 2 && slotIndex == 0 ? "Observer"
                : slotIndex % 2 ? "Russians" : "Americans";
            slot.team = "-";
            slot.name = "Defeat player " + std::to_string(slotIndex + 1);
            slot.color = slotIndex % 2 ? "Red" : "Blue";
            const auto key = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slotIndex));
            state.skirmishCountryByOwner[key] = slot.country;
            // This derived map stores normalized teams, as the real startup does.
            state.skirmishTeamByOwner[key] = runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.config.playerSlots[0].name = "甲&蓝";
        state.config.playerSlots[1].name = "红队";
        state.localRadarColor = RGB(19, 251, 27);
        initializeRuntimeNavigation(state);
        // Navigation loads the original map palette when neutral houses have
        // not been captured. Apply this independent controlled player palette
        // after that real initialization, so the reference keeps exact colors.
        for (std::size_t slotIndex = 0; slotIndex < enabled; ++slotIndex) {
            const auto key = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slotIndex));
            state.ownerRemapColors[key] = slotIndex % 2 ? RGB(230, 63, 51) : RGB(49, 137, 243);
        }
        return storage;
    };
    const auto populate = [&](RuntimeWindowState& state) {
        std::vector<int> ids(capacity, -1);
        const auto& terrain = state.world.getTerrainCells();
        if (terrain.empty()) return ids;
        std::size_t ordinal = 0;
        for (std::size_t slotIndex = 0; slotIndex < capacity; ++slotIndex) {
            const auto& slot = state.config.playerSlots[slotIndex];
            if (!slot.enabled || runtimeObserverSlot(slot)) continue;
            const auto& cell = terrain[(ordinal++ * 137) % terrain.size()];
            auto* object = addRuntimeObjectInstanceForOwner(state,
                runtimeSkirmishOwnerForSlot(slotIndex), RuntimeBuildKind::Unit,
                "AMCV", {cell.x, cell.y});
            if (!object) return std::vector<int>{};
            object->mission = "Guard";
            object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
            clearRuntimeRenderPosition(*object);
            ids[slotIndex] = object->instanceId;
        }
        return ids;
    };
    const auto destroy = [&](RuntimeWindowState& state, int id) {
        auto* object = findMutableObjectById(state, id);
        if (!object || !object->alive || object->health <= 0) return false;
        object->alive = false;
        object->health = 0;
        invalidateRuntimeObjectProjectionCache(state);
        return true;
    };
    const auto ownerKey = [](std::size_t slot) {
        return runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slot));
    };
    const auto describeOutcomeFixture = [&](const RuntimeWindowState& state, int mode) {
        std::ostringstream detail;
        detail << "mode=" << (mode == 0 ? "solo" : mode == 1 ? "MP" : "observer")
            << " finished=" << state.skirmishMatchFinished << " victory=" << state.skirmishVictory
            << " result=" << state.skirmishResult << " notices=[";
        for (const auto& notice : state.playerDefeatNotices.lines)
            detail << notice.owner << "@" << notice.untilMs << ",";
        detail << "] actualObjects=" << state.world.getObjects().size();
        for (std::size_t slotIndex = 0; slotIndex < state.config.playerSlots.size(); ++slotIndex) {
            const auto& slot = state.config.playerSlots[slotIndex];
            if (!slot.enabled) continue;
            const auto owner = runtimeSkirmishOwnerForSlot(slotIndex);
            const auto team = state.skirmishTeamByOwner.find(ownerKey(slotIndex));
            int aliveMatches = 0, shortGameMatches = 0;
            for (const auto& object : state.world.getObjects()) {
                if (object.owner != owner) continue;
                const bool survives = runtimeSkirmishObjectSurvives(state, object);
                const bool base = runtimeSkirmishObjectCountsForShortGame(state, object);
                aliveMatches += survives;
                shortGameMatches += survives && base;
                const auto* unit = object.kind == WorldObjectKind::Unit
                    ? findRuntimeUnitType(state.rules, object) : nullptr;
                detail << " {id=" << object.instanceId << " owner=" << object.owner
                    << " type=" << object.type << " kind=" << static_cast<int>(object.kind)
                    << " alive=" << object.alive << " hp=" << object.health
                    << " survives=" << survives << " shortGameBase=" << base
                    << " deploysInto=" << (unit ? unit->deploysInto : "<missing-unit>") << "}";
            }
            detail << " [slot=" << slotIndex << " expectedOwner=" << owner
                << " observer=" << runtimeObserverSlot(slot)
                << " aliveMatches=" << aliveMatches << " shortGameMatches=" << shortGameMatches
                << " defeated=" << runtimeSkirmishOwnerDefeated(state, owner)
                << " alliedLocal=" << runtimeOwnersAreAllied(state, state.economy.owner, owner)
                << " team=" << (team == state.skirmishTeamByOwner.end() ? "<missing>" : team->second)
                << "]";
        }
        return detail.str();
    };
    int outcomeCases = 0, pixelCases = 0, pixelSamples = 0;
    for (int mode : {0, 1, 2}) {
        auto storage = make(mode);
        auto& state = *storage;
        const auto ids = populate(state);
        if (ids.size() != capacity || ids[1] < 0) return fail("stock MCV creation");
        advanceRuntimeSkirmishOutcome(state);
        if (!state.playerDefeatNotices.lines.empty() || state.skirmishMatchFinished)
            return fail("living roster produced a notice or terminal result; "
                + describeOutcomeFixture(state, mode));
        if (!destroy(state, ids[1])) return fail("real outcome victim missing");
        state.tick += 10;
        const std::uint64_t before = GetTickCount64();
        advanceRuntimeSkirmishOutcome(state);
        const std::uint64_t after = GetTickCount64();
        const auto& lines = state.playerDefeatNotices.lines;
        if (state.skirmishMatchFinished || lines.size() != 1 || lines.front().owner != ownerKey(1)
            || lines.front().untilMs < before + runtimePlayerDefeatNoticeDurationMs
            || lines.front().untilMs > after + runtimePlayerDefeatNoticeDurationMs
            || runtimePlayerDefeatNoticeText(state, lines.front().owner) != L"红队已被击败！")
            return fail("solo/MP/observer production outcome did not announce the real defeated player; "
                + describeOutcomeFixture(state, mode));
        const auto deadline = lines.front().untilMs;
        state.tick += 10;
        advanceRuntimeSkirmishOutcome(state);
        if (state.playerDefeatNotices.lines.size() != 1
            || state.playerDefeatNotices.lines.front().untilMs != deadline
            || enqueueRuntimePlayerDefeatNotice(state, runtimeSkirmishOwnerForSlot(1), after))
            return fail("repeated outcome/accepted event restarted a player's deadline");
        if (mode == 2 && state.playerDefeatNotices.announcedOwners.contains(ownerKey(0)))
            return fail("empty observer house was announced as defeated");
        ++outcomeCases;
    }

    // An accepted Running->Running cooperative snapshot returns changed=false.
    // Losing A while partner B survives must still publish A's notice locally.
    {
        auto storage = make(0);
        auto& state = *storage;
        IniFile scenarioIni;
        CooperativeScenarioDatabase database;
        const auto scenarioPath = runtimeCooperativeScenarioPath(stock.config);
        if (scenarioPath.empty() || !scenarioIni.loadFromFile(pathString(scenarioPath))
            || !database.loadFromIni(scenarioIni) || database.scenarios().empty())
            return fail("original cooperative scenario database unavailable");
        const auto& scenario = database.scenarios().front();
        state.config.gameMode = "Cooperative";
        state.config.gameModeRulesOverride = "MPCoopMD.ini";
        state.config.defaultMapPath = pathString(std::filesystem::path(stock.config.localDataRootPath)
            / "resources" / "mix_exports" / "multimd"
            / (toLowerAsciiLocal(scenario.mapId) + ".map"));
        state.config.playerCountry = scenario.slots[0].defaultCountry;
        for (std::size_t index = 0; index < runtimeCooperativeSlotCount; ++index) {
            auto& slot = state.config.playerSlots[index];
            slot.enabled = true;
            slot.human = index == 0;
            slot.local = index == 0;
            slot.startPosition = static_cast<int>(index) + 1;
            slot.country = scenario.slots[index].defaultCountry;
            slot.team = index < 2 ? "1" : "2";
            state.skirmishCountryByOwner[ownerKey(index)] = slot.country;
        }
        if (!initializeRuntimeCooperativeSession(state) || !state.cooperativeSession.active())
            return fail("real validated cooperative session: " + state.cooperativeSessionStatus);
        const auto ids = populate(state);
        if (ids.size() != capacity || ids[0] < 0 || ids[1] < 0)
            return fail("cooperative live participants missing");
        advanceRuntimeSkirmishOutcome(state);
        if (state.skirmishMatchFinished || !state.playerDefeatNotices.lines.empty())
            return fail("living cooperative roster was announced defeated");
        if (!destroy(state, ids[0])) return fail("cooperative A victim missing");
        state.tick = 20;
        if (advanceRuntimeSkirmishOutcome(state) || state.skirmishMatchFinished
            || state.cooperativeSession.status() != RuntimeCooperativeSessionStatus::Running
            || !state.cooperativeSession.hasOutcomeSnapshot()
            || state.cooperativeSession.lastOutcomeSnapshot().tick != 20
            || state.cooperativeSession.lastOutcomeSnapshot().liveTechnos[0] != 0
            || state.cooperativeSession.lastOutcomeSnapshot().liveTechnos[1] <= 0
            || !state.multiplayerDefeatedOwners.contains(ownerKey(0))
            || state.playerDefeatNotices.lines.size() != 1
            || state.playerDefeatNotices.lines.front().owner != ownerKey(0))
            return fail("accepted nonterminal changed=false cooperative outcome lost A's notice");
        const auto deadline = state.playerDefeatNotices.lines.front().untilMs;
        state.tick = 30;
        if (advanceRuntimeSkirmishOutcome(state) || state.skirmishMatchFinished
            || state.cooperativeSession.lastOutcomeSnapshot().tick != 30
            || state.playerDefeatNotices.lines.size() != 1
            || state.playerDefeatNotices.lines.front().untilMs != deadline)
            return fail("repeated cooperative snapshot restarted its notice");
        ++outcomeCases;
    }

    // Pure UI-clock boundaries: no sleeps and no simulation tick advances.
    for (int hz : {15, 30, 60}) {
        auto storage = make(0);
        auto& state = *storage;
        state.config.fixedTickHz = hz;
        const auto ids = populate(state);
        const std::uint64_t start = GetTickCount64() + 60000;
        if (ids.size() != capacity || !destroy(state, ids[1])
            || !observeRuntimePlayerDefeatNotices(state, start))
            return fail("first timed defeat was not detected");
        if (!destroy(state, ids[2]) || !observeRuntimePlayerDefeatNotices(state, start + 1000)
            || state.playerDefeatNotices.lines.size() != 2)
            return fail("later defeat did not retain both players");
        const int frozenTick = state.tick;
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto firstEnd = start + runtimePlayerDefeatNoticeDurationMs;
        state.repaintRequested = false;
        if (expireRuntimePlayerDefeatNotices(state, firstEnd - 1)
            || state.repaintRequested || state.playerDefeatNotices.lines.size() != 2)
            return fail("notice ended before its millisecond deadline");
        if (!expireRuntimePlayerDefeatNotices(state, firstEnd) || !state.repaintRequested
            || state.playerDefeatNotices.lines.size() != 1
            || state.playerDefeatNotices.lines.front().owner != ownerKey(2)
            || state.playerDefeatNotices.lines.front().untilMs != firstEnd + 1000)
            return fail("independent player expiry or repaint invalidation");
        observeRuntimePlayerDefeatNotices(state, firstEnd + 1);
        if (state.playerDefeatNotices.lines.size() != 1
            || !expireRuntimePlayerDefeatNotices(state, firstEnd + 1000)
            || !state.playerDefeatNotices.lines.empty()
            || state.playerDefeatNotices.announcedOwners.size() != 2
            || state.tick != frozenTick || state.world.deterministicSimulationStateHash() != worldHash)
            return fail("expired notice repeated or UI-clock work changed simulation");
    }

    // Original AI placeholders use the translated difficulty name, while
    // custom AI names and human names retain their exact spelling.
    {
        auto storage = make(0);
        auto& state = *storage;
        auto& slot = state.config.playerSlots[2];
        slot.human = false;
        slot.name = "Computer 2";
        for (const std::string difficulty : {"Easy", "Medium", "Hard"}) {
            slot.difficulty = difficulty;
            const auto medium = runtimeMenuLabel(state, "GUI:Medium", L"Medium Enemy");
            const auto name = difficulty == "Easy" ? runtimeMenuLabel(state, "GUI:AIEasy", L"Easy Enemy")
                : difficulty == "Hard" ? runtimeMenuLabel(state, "GUI:AIHard", L"Brutal Enemy")
                : runtimeMenuLabel(state, "GUI:AINormal", medium.c_str());
            if (runtimePlayerDefeatNoticeText(state, ownerKey(2)) != name + L"已被击败！")
                return fail("default computer difficulty label lost its translated player name");
        }
        slot.name = "Computer 2 & custom";
        if (runtimePlayerDefeatNoticeText(state, ownerKey(2)) != L"Computer 2 & custom已被击败！")
            return fail("custom computer name was replaced by a difficulty label");
        slot.human = true;
        slot.name = "Computer 2";
        if (runtimePlayerDefeatNoticeText(state, ownerKey(2)) != L"Computer 2已被击败！")
            return fail("human name matching an AI placeholder was replaced");
        state.config.multiplayerActive = true;
        slot.human = false;
        slot.name = "Computer 3";
        slot.difficulty = "Hard";
        const auto multiplayerAiName = runtimeMenuLabel(state, "GUI:AIHard", L"Brutal Enemy");
        if (runtimePlayerDefeatNoticeText(state, ownerKey(2)) != multiplayerAiName + L"已被击败！")
            return fail("one-based multiplayer default AI did not use its translated difficulty label");
        slot.human = true;
        if (runtimePlayerDefeatNoticeText(state, ownerKey(2)) != L"Computer 3已被击败！")
            return fail("one-based multiplayer human name was replaced by an AI label");
    }

    RuntimePlayerDefeatNoticeSmokeSurface glyphSurface(640, 480);
    if (!glyphSurface.valid()) return fail("native Unicode reference DIB/font unavailable");
    const std::wstring required = L"甲&蓝红队第十二已被击败！长玩家同屏聊天中文…";
    std::vector<WORD> glyphs(required.size(), 0xffff);
    if (GetGlyphIndicesW(glyphSurface.dc, required.c_str(), static_cast<int>(required.size()),
            glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR
        || std::find(glyphs.begin(), glyphs.end(), WORD{0xffff}) != glyphs.end())
        return fail("actual HUD font lacks the required Chinese/name/suffix glyphs");

    const auto verifyPixels = [&](RuntimeWindowState& state,
            RuntimePlayerDefeatNoticeSmokeWindow& window,
            const std::vector<std::wstring>& names, const std::vector<COLORREF>& colors,
            std::vector<std::uint32_t>* baselineOut = nullptr,
            const std::wstring& chatText = L"") {
        if (!window.window || names.size() != colors.size()
            || names.size() != state.playerDefeatNotices.lines.size()
            || (chatText.empty() ? !state.playerCommunication.lines.empty()
                : state.playerCommunication.lines.size() != 1))
            return fail("native window or authored row fixture unavailable");
        // Preserve diagnostic identities even if actual WM_PAINT expires rows.
        const auto diagnosticRows = state.playerDefeatNotices.lines;
        const RECT client{0, 0, window.width, window.height};
        const RECT viewport = runtimeMapViewport(client);
        auto retained = std::move(state.playerDefeatNotices.lines);
        auto retainedChat = std::move(state.playerCommunication.lines);
        const std::uint64_t baselinePaintStart = GetTickCount64();
        const auto baseline = window.paint();
        const std::uint64_t baselinePaintEnd = GetTickCount64();
        state.playerDefeatNotices.lines = std::move(retained);
        state.playerCommunication.lines = std::move(retainedChat);
        const std::uint64_t actualPaintStart = GetTickCount64();
        const auto actual = window.paint();
        const std::uint64_t actualPaintEnd = GetTickCount64();
        if (baseline.size() != static_cast<std::size_t>(window.width * window.height)
            || actual.size() != baseline.size()) return fail("production WM_PAINT surface unavailable");
        RuntimePlayerDefeatNoticeSmokeSurface expected(window.width, window.height);
        if (!expected.valid()) return fail("reference painter surface unavailable");
        std::copy(baseline.begin(), baseline.end(), expected.pixels);
        IntersectClipRect(expected.dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        constexpr UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        const LONG left = viewport.left + 10, right = viewport.right - 10;
        for (std::size_t row = 0; row < names.size(); ++row) {
            const auto text = runtimePlayerDefeatNoticeSmokeFitName(expected.dc, names[row],
                static_cast<int>(right - left));
            const LONG top = viewport.top + 12 + static_cast<LONG>(row) * 27;
            RECT bounds{left, top, right, top + 25};
            RECT shadow = bounds;
            OffsetRect(&shadow, 1, 1);
            SetTextColor(expected.dc, RGB(0, 0, 0));
            DrawTextW(expected.dc, text.c_str(), static_cast<int>(text.size()), &shadow, flags);
            SetTextColor(expected.dc, colors[row]);
            DrawTextW(expected.dc, text.c_str(), static_cast<int>(text.size()), &bounds, flags);
        }
        if (!chatText.empty()) {
            // Independent authored layout: every actual notice occupies 27px.
            // Do not use the production visible-row/stack-height helpers.
            const LONG top = viewport.top + 12 + static_cast<LONG>(names.size()) * 27;
            RECT bounds{left, top, std::min(viewport.right - 10, viewport.left + 850L), top + 25};
            HBRUSH panel = CreateSolidBrush(RGB(19, 29, 39));
            if (!panel) return fail("reference chat panel unavailable");
            FillRect(expected.dc, &bounds, panel);
            DeleteObject(panel);
            SetTextColor(expected.dc, RGB(235, 240, 245));
            DrawTextW(expected.dc, chatText.c_str(), static_cast<int>(chatText.size()), &bounds,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        GdiFlush();
        std::vector<int> samples(names.size() + (chatText.empty() ? 0 : 1), 0);
        int mismatch = 0, outside = 0;
        for (int y = 0; y < window.height; ++y) for (int x = 0; x < window.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * window.width + x;
            const auto wanted = expected.pixels[at] & 0x00ffffffU;
            const auto observed = actual[at] & 0x00ffffffU;
            const auto base = baseline[at] & 0x00ffffffU;
            const bool inside = x >= left && x < viewport.right
                && y >= viewport.top + 12
                && y < viewport.top + 12 + static_cast<int>(samples.size()) * 27;
            if (inside) mismatch += wanted != observed;
            else outside += base != observed;
            if (wanted != base && y >= viewport.top + 12) {
                const auto row = static_cast<std::size_t>((y - viewport.top - 12) / 27);
                if (row < samples.size()) ++samples[row];
            }
        }
        for (int count : samples) if (count < 40) return fail("native name/suffix row contained too few glyph pixels");
        if (mismatch || outside) {
            std::ostringstream detail;
            detail << "native HUD text/color/position/clipping differs from independent oracle: "
                << mismatch << " row pixels, " << outside << " outside pixels"
                << " pixelCase=" << pixelCases << " outcomes=" << outcomeCases
                << " beforeRows=" << diagnosticRows.size()
                << " afterRows=" << state.playerDefeatNotices.lines.size()
                << " baselinePaintMs=" << baselinePaintEnd - baselinePaintStart
                << " actualPaintMs=" << actualPaintEnd - actualPaintStart
                << " actualPaintNow=" << actualPaintEnd
                << " viewport=" << viewport.left << "," << viewport.top << ","
                << viewport.right << "," << viewport.bottom;
            for (std::size_t row = 0; row < samples.size(); ++row) {
                const LONG top = viewport.top + 12 + static_cast<LONG>(row) * 27;
                std::unordered_map<std::uint32_t, int> expectedHistogram, actualHistogram;
                int rowMismatch = 0;
                bool firstDifference = true;
                detail << " [row=" << row << " glyphPixels=" << samples[row];
                if (row < names.size()) {
                    const auto& owner = diagnosticRows[row].owner;
                    const auto palette = state.ownerRemapColors.find(runtimeCanonicalHumanHouseOwner(owner));
                    detail << " owner=" << owner << " untilMs=" << diagnosticRows[row].untilMs
                        << " nameUnits=" << names[row].size();
                    for (std::size_t slotIndex = 0; slotIndex < state.config.playerSlots.size(); ++slotIndex)
                        if (runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slotIndex)) == owner)
                            detail << " configuredName=" << state.config.playerSlots[slotIndex].name;
                    detail << " expectedColor=0x" << std::hex << colors[row] << " palette=";
                    if (palette == state.ownerRemapColors.end()) detail << "missing";
                    else detail << "0x" << palette->second;
                    detail << " renderedColor=0x" << runtimeSuperWeaponTimerColor(state, owner) << std::dec;
                }
                for (LONG y = top; y < std::min(top + 27, viewport.bottom); ++y)
                    for (LONG x = left; x < viewport.right; ++x) {
                        const std::size_t at = static_cast<std::size_t>(y) * window.width + x;
                        const auto wanted = expected.pixels[at] & 0x00ffffffU;
                        const auto observed = actual[at] & 0x00ffffffU;
                        const auto base = baseline[at] & 0x00ffffffU;
                        if (wanted != base) ++expectedHistogram[wanted];
                        if (observed != base) ++actualHistogram[observed];
                        if (wanted == observed) continue;
                        ++rowMismatch;
                        if (firstDifference) {
                            detail << " first=" << x << "," << y << ":0x" << std::hex
                                << wanted << "/0x" << observed << "/base0x" << base << std::dec;
                            firstDifference = false;
                        }
                    }
                const auto histogram = [&](const auto& counts) {
                    std::vector<std::pair<std::uint32_t, int>> ordered(counts.begin(), counts.end());
                    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
                        return a.second != b.second ? a.second > b.second : a.first < b.first;
                    });
                    for (std::size_t index = 0; index < std::min<std::size_t>(5, ordered.size()); ++index)
                        detail << "0x" << std::hex << ordered[index].first << std::dec << ":"
                            << ordered[index].second << ",";
                };
                detail << " mismatch=" << rowMismatch << " expectedRGB={";
                histogram(expectedHistogram); detail << "} actualRGB={";
                histogram(actualHistogram); detail << "}]";
            }
            return fail(detail.str());
        }
        pixelSamples += std::accumulate(samples.begin(), samples.end(), 0);
        ++pixelCases;
        if (baselineOut) *baselineOut = baseline;
        return true;
    };

    auto nativeStorage = make(1);
    auto& native = *nativeStorage;
    auto nativeIds = populate(native);
    if (nativeIds.size() != capacity || !destroy(native, nativeIds[0]) || !destroy(native, nativeIds[1]))
        return fail("two real native defeat victims missing");
    native.tick += 10;
    advanceRuntimeSkirmishOutcome(native);
    if (native.skirmishMatchFinished || native.playerDefeatNotices.lines.size() != 2
        || native.playerDefeatNotices.lines[0].owner != ownerKey(0)
        || native.playerDefeatNotices.lines[1].owner != ownerKey(1))
        return fail("same-outcome row order or mid-match admission");
    // A future injected observation keeps native asset warmup independent of
    // machine speed; duration and real-clock outcomes were checked above.
    resetRuntimePlayerDefeatNotices(native);
    observeRuntimePlayerDefeatNotices(native, GetTickCount64() + 60000);
    const std::vector<std::wstring> shortNames{L"甲&蓝", L"红队"};
    const std::vector<COLORREF> shortColors{RGB(49, 137, 243), RGB(230, 63, 51)};
    {
        RuntimePlayerDefeatNoticeSmokeWindow window(native, 800, 600);
        std::vector<std::uint32_t> baseline;
        if (!verifyPixels(native, window, shortNames, shortColors, &baseline)) return false;
        RuntimeMultiplayerGameplayPlayerCommunicationCommand chat;
        chat.kind = RuntimePlayerCommunicationKind::ChatAll;
        chat.value = 0;
        chat.text = "同屏聊天&中文";
        std::string chatError;
        const auto sender = runtimeSkirmishOwnerForSlot(2);
        if (!validateRuntimePlayerCommunication(native, sender, chat, chatError)
            || !applyRuntimePlayerCommunication(native, sender, chat)
            || native.playerCommunication.lines.size() != 1
            || native.playerCommunication.lines.front().untilTick <= native.tick)
            return fail("real accepted same-screen chat: " + chatError);
        const auto chatText = widenUtf8(ownerKey(2) + ": 同屏聊天&中文");
        if (!verifyPixels(native, window, shortNames, shortColors, nullptr, chatText)) return false;
        const int chatFrozenTick = native.tick;
        native.playerDefeatNotices.lines.front().untilMs = GetTickCount64() - 1;
        native.repaintRequested = false;
        window.paint(false);
        if (native.tick != chatFrozenTick || native.playerDefeatNotices.lines.size() != 1
            || native.playerDefeatNotices.lines.front().owner != ownerKey(1))
            return fail("one expired row did not release its chat layout space");
        if (!verifyPixels(native, window, {L"红队"}, {shortColors[1]}, nullptr, chatText)) return false;
        native.playerDefeatNotices.lines.front().untilMs = GetTickCount64() - 1;
        native.repaintRequested = false;
        window.paint(false);
        if (native.tick != chatFrozenTick || !native.playerDefeatNotices.lines.empty())
            return fail("last expired row did not release its chat layout space");
        if (!verifyPixels(native, window, {}, {}, nullptr, chatText)) return false;
        native.playerCommunication.lines.clear();
        // Restore a genuine observed presentation for the original paused
        // notice-only cache-expiry regression below.
        resetRuntimePlayerDefeatNotices(native);
        observeRuntimePlayerDefeatNotices(native, GetTickCount64() + 60000);
        if (!verifyPixels(native, window, shortNames, shortColors, &baseline)) return false;
        const int frozenTick = native.tick;
        const int renderedBefore = native.renderedFramesThisSecond;
        native.repaintRequested = false;
        const std::uint64_t past = GetTickCount64() > 0 ? GetTickCount64() - 1 : 0;
        for (auto& notice : native.playerDefeatNotices.lines) notice.untilMs = past;
        const auto expired = window.paint(false);
        const RECT viewport = runtimeMapViewport({0, 0, 800, 600});
        if (!native.playerDefeatNotices.lines.empty() || native.tick != frozenTick
            || native.renderedFramesThisSecond != renderedBefore + 1
            || expired.size() != baseline.size())
            return fail("paused production paint did not expire and redraw its cached notices");
        for (LONG y = viewport.top + 12; y < viewport.top + 66; ++y)
            for (LONG x = viewport.left + 10; x < viewport.right; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * 800 + x;
                if ((expired[at] & 0x00ffffffU) != (baseline[at] & 0x00ffffffU))
                    return fail("expired cached frame retained player-name pixels");
            }
    }

    // Re-observe real defeated authority in a fresh presentation for the second
    // viewport; the original simulation remains frozen and the text does not
    // inherit map zoom, camera displacement or a nonzero authored explosion.
    resetRuntimePlayerDefeatNotices(native);
    observeRuntimePlayerDefeatNotices(native, GetTickCount64() + 60000);
    native.camera.tileWidth = 48;
    native.camera.tileHeight = 24;
    native.camera.worldOffsetX += 137;
    native.camera.worldOffsetY += 79;
    native.camera.initialized = true;
    bool shakeReady = false;
    for (const auto& warhead : native.rules.getWarheadTypes()) {
        const auto* profile = native.rules.findWarheadLifecycleProfile(warhead.id);
        if (!profile || (!profile->shakeXlo && !profile->shakeXhi
                && !profile->shakeYlo && !profile->shakeYhi)) continue;
        RuntimeImpactEffect impact;
        impact.projectileActorId = 917;
        impact.warheadId = warhead.id;
        const auto* anchor = findObjectById(native, nativeIds[3]);
        if (!anchor) return fail("live screen-shake anchor missing");
        impact.cell = runtimeCellPointForObject(*anchor);
        impact.totalTicks = impact.remainingTicks = 8;
        impact.renderIntent = "warheadImpact";
        native.impactEffects = {impact};
        RuntimeMapRenderContext context;
        context.rules = &native.rules;
        context.fixedTickHz = native.config.fixedTickHz;
        context.impactActors = runtimeImpactRenderActorsForRender(native);
        const auto offset = runtimeScreenShakeForRender(context);
        if (offset.x || offset.y) { shakeReady = true; break; }
    }
    if (!shakeReady) return fail("original rules did not provide a real nonzero screen shake");
    std::wstring longName = L"甲&";
    std::string longUtf8 = "甲&";
    for (int index = 0; index < 60; ++index) { longName += L"长玩家"; longUtf8 += "长玩家"; }
    longName += L"\xd83d\xde80";
    longUtf8 += "\xf0\x9f\x9a\x80";
    native.config.playerSlots[0].name = longUtf8;
    if (runtimePlayerDefeatNoticeText(native, ownerKey(0)) != longName + L"已被击败！")
        return fail("long Unicode player identity was changed before fitting");
    {
        RuntimePlayerDefeatNoticeSmokeWindow window(native, 1280, 720);
        if (!verifyPixels(native, window, {longName, L"红队"}, shortColors)) return false;
        const RECT viewport = runtimeMapViewport({0, 0, 1280, 720});
        const auto fitted = runtimePlayerDefeatNoticeFitText(glyphSurface.dc,
            longName + L"已被击败！", static_cast<int>(viewport.right - viewport.left - 20));
        const std::wstring suffix = L"已被击败！";
        if (fitted.find(L"…") == std::wstring::npos
            || fitted.size() < suffix.size() || fitted.substr(fitted.size() - suffix.size()) != suffix)
            return fail("long-name fitting lost the exact defeat suffix");
    }
    native.impactEffects.clear();
    native.config.playerSlots[0].name = "甲&蓝";

    // Load over the same painted window. Save at a non-outcome tick so a new
    // defeat before tick 30 cannot be accidentally seeded as a historical one.
    {
        RuntimePlayerDefeatNoticeSmokeWindow window(native, 800, 600);
        // Continue forward from the painted tick 20, rather than rewinding an
        // already initialized audiovisual authority for this save fixture.
        native.tick = 21;
        (void)advanceRuntimeParticles(native);
        if (!native.particlesInitialized || native.particles.currentTick() != native.tick
            || native.particles.fixedTickHz() != std::max(1, native.config.fixedTickHz))
            return fail("save fixture did not synchronize the real particle authority");
        if (!verifyPixels(native, window, shortNames, shortColors)) return false;
        const char* outputRoot = std::getenv("CONCORD_PLAYER_DEFEAT_NOTICE_OUTPUT");
        const auto directory = outputRoot && *outputRoot
            ? std::filesystem::path(outputRoot)
            : std::filesystem::path(native.config.localDataRootPath).parent_path()
                / "build" / "23" / "03" / "private";
        std::error_code directoryError;
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError) return fail("private native save directory: " + directoryError.message());
        const auto savePath = pathString(directory / "player-defeat-notice.ini");
        if (!saveRuntimeState(native, savePath, error)) return fail("save: " + error);
        const auto failedLines = native.playerDefeatNotices.lines;
        const auto failedSeen = native.playerDefeatNotices.announcedOwners;
        const auto failedHash = native.world.deterministicSimulationStateHash();
        const auto failedPixels = window.paint();
        const int failedRendered = native.renderedFramesThisSecond;
        native.repaintRequested = false;
        std::string failedError;
        bool failedAccepted = false;
        {
            struct RestoreLoadFault {
                RuntimeLoadFaultPointForSmoke previous = runtimeLoadFaultPointForSmoke;
                ~RestoreLoadFault() { runtimeLoadFaultPointForSmoke = previous; }
            } restoreFault;
            // The loaded-world notice baseline has already been prepared at
            // this fault point. Its uncommitted state must not leak into live UI.
            runtimeLoadFaultPointForSmoke = RuntimeLoadFaultPointForSmoke::BeforeCommit;
            failedAccepted = loadRuntimeState(native, savePath, failedError);
        }
        if (failedAccepted || failedError.find("load materialization failed:") != 0
            || native.playerDefeatNotices.lines.size() != failedLines.size()
            || native.playerDefeatNotices.announcedOwners != failedSeen
            || native.playerDefeatNotices.suppressExisting || native.tick != 21
            || native.repaintRequested || native.world.deterministicSimulationStateHash() != failedHash)
            return fail("failed actual load changed the visible match/notice baseline");
        for (std::size_t index = 0; index < failedLines.size(); ++index)
            if (native.playerDefeatNotices.lines[index].owner != failedLines[index].owner
                || native.playerDefeatNotices.lines[index].untilMs != failedLines[index].untilMs)
                return fail("failed actual load changed a notice owner/deadline");
        if (window.paint(false) != failedPixels || native.renderedFramesThisSecond != failedRendered)
            return fail("failed actual load invalidated or replaced the cached notice pixels");
        const int renderedBefore = native.renderedFramesThisSecond;
        native.repaintRequested = false;
        if (!loadRuntimeState(native, savePath, error)) return fail("actual live load: " + error);
        if (native.tick != 21 || !native.playerDefeatNotices.lines.empty()
            || native.playerDefeatNotices.suppressExisting
            || !native.playerDefeatNotices.announcedOwners.contains(ownerKey(0))
            || !native.playerDefeatNotices.announcedOwners.contains(ownerKey(1))
            || native.playerDefeatNotices.announcedOwners.contains(ownerKey(2)))
            return fail("loaded-world baseline did not separate old defeats from its live participant");
        const auto cleared = window.paint(false);
        const auto fresh = window.paint();
        if (cleared.size() != fresh.size() || cleared.empty()
            || native.renderedFramesThisSecond < renderedBefore + 2)
            return fail("successful load retained the visible window's cached notice frame");
        const RECT viewport = runtimeMapViewport({0, 0, 800, 600});
        for (LONG y = viewport.top + 12; y < viewport.top + 66; ++y)
            for (LONG x = viewport.left + 10; x < viewport.right; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * 800 + x;
                if ((cleared[at] & 0x00ffffffU) != (fresh[at] & 0x00ffffffU))
                    return fail("first post-load paint retained defeated-player pixels");
            }
        if (!destroy(native, nativeIds[2])) return fail("post-load new victim missing");
        native.tick = 30;
        advanceRuntimeSkirmishOutcome(native);
        if (!native.skirmishMatchFinished || native.playerDefeatNotices.lines.size() != 1
            || native.playerDefeatNotices.lines.front().owner != ownerKey(2)
            || !native.playerDefeatNotices.announcedOwners.contains(ownerKey(2)))
            return fail("first post-load outcome swallowed the newly defeated living participant");
        native.resultPresentation.started = true;
        native.resultPresentation.bannerVisible = false;
        const auto color = native.ownerRemapColors.find(ownerKey(2));
        if (color == native.ownerRemapColors.end()) return fail("loaded participant color missing");
        if (!verifyPixels(native, window, {L"Defeat player 3"}, {color->second})) return false;
        initializeRuntimeEconomy(native);
        if (!native.playerDefeatNotices.lines.empty() || !native.playerDefeatNotices.announcedOwners.empty()
            || native.playerDefeatNotices.suppressExisting)
            return fail("actual new-match initialization retained old notices");
        native.world.accessObjects().clear();
        initializeRuntimeNavigation(native);
        native.economy.owner = runtimeSkirmishOwnerForSlot(0);
        native.economy.skirmishStartApplied = true;
        native.skirmishMatchFinished = false;
        native.tick = 10;
        nativeIds = populate(native);
        if (nativeIds.size() != capacity || !destroy(native, nativeIds[0]))
            return fail("restarted participant victim missing");
        advanceRuntimeSkirmishOutcome(native);
        if (native.playerDefeatNotices.lines.size() != 1
            || native.playerDefeatNotices.lines.front().owner != ownerKey(0))
            return fail("new match could not announce the same slot after a new defeat");
        const auto restartedColor = native.ownerRemapColors.find(ownerKey(0));
        if (restartedColor == native.ownerRemapColors.end()) return fail("restarted participant color missing");
        if (!verifyPixels(native, window, {L"甲&蓝"}, {restartedColor->second})) return false;
    }

    if (capacity >= 12) {
        auto storage = make(1);
        auto& state = *storage;
        auto& slot = state.config.playerSlots[11];
        slot.enabled = slot.human = true;
        slot.country = "Russians";
        slot.name = "第十二玩家&";
        slot.team = "-";
        state.skirmishCountryByOwner[ownerKey(11)] = "Russians";
        state.ownerRemapColors[ownerKey(11)] = RGB(213, 101, 225);
        const auto ids = populate(state);
        if (ids.size() != capacity || !destroy(state, ids[11])) return fail("twelfth real player fixture");
        advanceRuntimeSkirmishOutcome(state);
        if (state.playerDefeatNotices.lines.size() != 1
            || state.playerDefeatNotices.lines.front().owner != ownerKey(11)
            || runtimePlayerDefeatNoticeText(state, ownerKey(11)) != L"第十二玩家&已被击败！")
            return fail("twelfth player name/defeat was omitted");
        RuntimePlayerDefeatNoticeSmokeWindow window(state, 800, 600);
        if (!verifyPixels(state, window, {L"第十二玩家&"}, {RGB(213, 101, 225)})) return false;
        ++outcomeCases;
    }
    std::cout << "Player defeat notice smoke passed: outcomes=" << outcomeCases
        << " nativePixelCases=" << pixelCases << " independentGlyphPixels=" << pixelSamples
        << " durationMs=" << runtimePlayerDefeatNoticeDurationMs
        << " pausedExpiry=1 chatStackTransitions=3 cooperativeRunning=1 failedLoadPreserved=1"
        << " loadSeed=1 restart=1 maxSlots=" << capacity << '\n';
    return true;
}
