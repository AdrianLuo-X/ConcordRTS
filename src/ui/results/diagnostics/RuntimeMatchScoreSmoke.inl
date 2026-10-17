bool runRuntimeMatchScoreStateSmoke(std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "match score: " + reason; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    IniFile rules;
    rules.loadFromString("[InfantryTypes]\n0=SOLDIER\n1=DECORATION\n[BuildingTypes]\n0=BASE\n"
        "[SOLDIER]\nStrength=100\nCost=100\n[DECORATION]\nStrength=100\nCost=900\nDontScore=yes\n"
        "[BASE]\nStrength=100\nCost=500\n");
    probe.rules.loadFromIni(rules);
    probe.rulesLoaded = true;
    probe.config.fixedTickHz = 30;
    probe.config.shortGame = false;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.economy.owner = "SkirmishPlayer1";
    probe.skirmishTeamByOwner["skirmishplayer1"] = "A";
    probe.skirmishTeamByOwner["skirmishplayer3"] = "A";
    const auto add = [&](int id, const char* owner, const char* type, WorldObjectKind kind) {
        WorldObjectInstance object;
        object.instanceId = id;
        object.owner = owner;
        object.type = type;
        object.kind = kind;
        object.health = 100;
        object.x = 10 + id;
        object.y = 10;
        object.cell = runtimeCellKey(object.x, object.y);
        probe.world.accessObjects().push_back(object);
    };
    add(1, "SkirmishPlayer2", "SOLDIER", WorldObjectKind::Infantry);
    add(2, "SkirmishPlayer2", "BASE", WorldObjectKind::Structure);
    add(3, "SkirmishPlayer1", "SOLDIER", WorldObjectKind::Infantry);
    add(4, "SkirmishPlayer2", "SOLDIER", WorldObjectKind::Infantry);
    add(5, "SkirmishPlayer2", "BASE", WorldObjectKind::Structure);
    add(6, "SkirmishPlayer2", "DECORATION", WorldObjectKind::Infantry);
    add(7, "Neutral", "SOLDIER", WorldObjectKind::Infantry);
    add(8, "SkirmishPlayer2", "SOLDIER", WorldObjectKind::Infantry);
    const auto exit = [&](int id, const std::string& owner, RuntimeObjectExitKind kind) {
        RuntimeObjectExitRequest request;
        request.objectId = id;
        request.sourceOwner = owner;
        request.kind = kind;
        request.suppressDeathWeapon = true;
        request.destructionEffectsSpawned = true;
        request.tick = ++probe.tick;
        return finalizeRuntimeObjectExit(probe, request);
    };
    if (!exit(1, "SkirmishPlayer1", RuntimeObjectExitKind::Destroyed).accepted) return fail("normal death rejected");
    const auto once = probe.matchScores;
    if (!exit(1, "SkirmishPlayer1", RuntimeObjectExitKind::Destroyed).duplicate || once != probe.matchScores)
        return fail("duplicate death changed score");
    if (!exit(2, "SkirmishPlayer3", RuntimeObjectExitKind::TemporalDestroyed).accepted
        || !exit(3, "SkirmishPlayer3", RuntimeObjectExitKind::Destroyed).accepted
        || !exit(4, "", RuntimeObjectExitKind::Destroyed).accepted
        || !exit(5, "SkirmishPlayer1", RuntimeObjectExitKind::Sold).accepted
        || !exit(6, "SkirmishPlayer1", RuntimeObjectExitKind::Destroyed).accepted
        || !exit(7, "SkirmishPlayer1", RuntimeObjectExitKind::Destroyed).accepted
        || !exit(8, "SkirmishPlayer1", RuntimeObjectExitKind::TransformSource).accepted)
        return fail("exit fixture failed");
    if (probe.matchScores.size() != 3
        || probe.matchScores.at("skirmishplayer1") != RuntimeMatchScoreEntry{1, 0, 1, 0, 100}
        || probe.matchScores.at("skirmishplayer2") != RuntimeMatchScoreEntry{0, 0, 2, 1, 0}
        || probe.matchScores.at("skirmishplayer3") != RuntimeMatchScoreEntry{0, 1, 0, 0, 500})
    {
        std::ostringstream diagnostic;
        writeRuntimeMatchScore(probe, diagnostic);
        return fail("kill attribution, friendly losses or excluded exits were wrong: " + diagnostic.str());
    }
    RuntimeBuildCatalogItem produced;
    produced.typeId = "SOLDIER";
    produced.kind = RuntimeBuildKind::Infantry;
    recordRuntimeCompletedBuild(probe, "SkirmishPlayer1", produced, 4);
    recordRuntimeCompletedBuild(probe, "SkirmishPlayer2", produced, 7);
    recordRuntimeCompletedBuild(probe, "SkirmishPlayer1", produced, 0);
    recordRuntimeCompletedBuild(probe, "Neutral", produced);
    probe.campaignMode = true;
    recordRuntimeCompletedBuild(probe, "SkirmishPlayer1", produced);
    probe.campaignMode = false;
    probe.skirmishMatchFinished = true;
    recordRuntimeCompletedBuild(probe, "SkirmishPlayer1", produced);
    probe.skirmishMatchFinished = false;
    if (probe.matchScores.size() != 3 || probe.matchScores.at("skirmishplayer1").built != 4
        || probe.matchScores.at("skirmishplayer2").built != 7) return fail("completed production count/exclusions wrong");
    // Pure codec/hash checks live in tests/infrastructure/RuntimeMatchScoreContract.cpp.
    error.clear();
    std::cout << "Match score facts: attributedKills=ok temporal=ok friendlyLoss=ok once=ok "
        "soldTransformNeutralDontScore=excluded built=ok pureCodec=runtime_match_score_contract_test\n";
    return true;
}

template <typename Paint, typename Save>
bool runRuntimeMatchScorePageSmoke(RuntimeWindowState& state, HWND window, Paint paint, Save save, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "match score page: " + reason; return false; };
    state.campaignMode = false;
    state.config.shortGame = false;
    state.config.playerSlots.resize(8);
    state.economy.owner = "SkirmishPlayer1";
    constexpr std::array<COLORREF, 8> colors{{RGB(255, 210, 54), RGB(218, 60, 60), RGB(74, 167, 235),
        RGB(73, 190, 101), RGB(231, 148, 59), RGB(177, 112, 213), RGB(86, 201, 204), RGB(234, 123, 177)}};
    for (int player = 0; player < 8; ++player) {
        auto& slot = state.config.playerSlots[player];
        slot.enabled = true;
        slot.name = player == 0 ? "指挥官" : player == 7 ? "第八位玩家 · 较长的中文名称测试" : "电脑 " + std::to_string(player);
        const auto owner = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(player));
        state.ownerRemapColors[owner] = colors[player];
        state.matchScores[owner] = {12 + player * 2, 3 + player, 8 + player, 2, 4200 + player * 700, 33 + player * 4};
    }
    state.localRadarColor = colors[0];
    state.tick = 30 * (18 * 60 + 42);
    const int finishedTick = state.tick;
    for (bool victory : {true, false}) {
        state.skirmishVictory = victory;
        state.config.multiplayerActive = !victory;
        state.resultPresentation = {};
        state.multiplayerWinningOwners = {victory ? "skirmishplayer1" : "skirmishplayer2"};
        const auto start = GetTickCount64() - 7000;
        advanceRuntimeResultPresentation(state, start);
        advanceRuntimeResultPresentation(state, start + 5999);
        if (!state.resultPresentation.bannerVisible) return fail("banner ended early");
        if (state.resultPresentation.scoreMusicSelected) return fail("score music started before the score page");
        advanceRuntimeResultPresentation(state, start + 6000);
        if (state.resultPresentation.bannerVisible) return fail("skirmish/network never entered score page");
        if (!state.resultPresentation.scoreMusicSelected || toLowerAsciiLocal(state.runtimeMusicThemeId) != "score"
            || toLowerAsciiLocal(std::filesystem::path(state.runtimeMusicPath).filename().string()) != "scorex.wav"
            || state.runtimeMusicStarted) return fail("muted result did not select original SCORE/ScoreX");
        const auto scorePath = state.runtimeMusicPath;
        const int changes = state.runtimeMusicTrackChanges;
        // Exercise the same advance request used when MCI reaches track end.
        if (findRuntimeMusicPath(state, true) != std::optional<std::string>{scorePath})
            return fail("score completion advanced into the battle playlist");
        advanceRuntimeResultPresentation(state, start + 7000);
        ensureRuntimeMusicPlaying(state);
        if (state.runtimeMusicStarted || state.runtimeMusicTrackChanges != changes)
            return fail("score restarted every frame or ignored muted music");
        const auto rows = runtimeMatchScoreRows(state);
        if (rows.size() != 8 || !rows[0].local || rows[7].statistics.score != 9100
            || rows[0].winner != victory || rows[0].defeated == victory)
            return fail("missing eliminated player, local outcome or score");
        const auto pixels = paint();
        if (state.tick != finishedTick || !save(victory ? "result_players_victory.bmp" : "result_players_defeat.bmp", pixels))
            return fail("score page did not render while the world was paused");
        const auto palette = runtimeUiPalette(state, "mpyscrn");
        if (loadRuntimeUiShpFrame(state, "", "mpyscrnl", 0) == nullptr || palette == nullptr)
            return fail("original multiplayer score background missing");
        for (const char* asset : {"sdtp", "sdbtnbkgd", "sdbtm", "lwscrnl", "mnbttn", "mpyscrnlbar01", "mpyscrnlbar10"}) {
            const auto* art = loadRuntimeUiShpFrame(state, "match_score", asset, 0);
            if (!art) return fail(std::string("original score/shell asset missing: ") + asset);
            if (art->embeddedPalette.size() != 256) return fail(std::string("score asset has no matching palette: ") + asset);
        }
        if (runtimeResultBitFont(state).bytes.empty()) return fail("original game font missing");
        // The full-width shell occupies the right side, not a black margin.
        if (pixels.size() != 800 * 600 || pixels[120 * 800 + 790] == 0xff000000U)
            return fail("original right shell was not drawn");
        const auto button = runtimeMatchScoreContinueRect(RECT{0, 0, 800, 600});
        if (button.left < 632 || button.top < 500 || button.bottom > 600)
            return fail("Continue does not occupy the retail sidebar");
        if (victory) {
            const auto country = state.config.playerCountry;
            std::vector<std::uint32_t> previousFaction;
            for (const auto& faction : {std::pair{"Americans", "result_players_allied.bmp"},
                    std::pair{"Russians", "result_players_soviet.bmp"}, std::pair{"YuriCountry", "result_players_yuri.bmp"}}) {
                state.config.playerCountry = faction.first;
                state.repaintRequested = true;
                const auto rendered = paint();
                if (!save(faction.second, rendered) || rendered == previousFaction)
                    return fail("faction score artwork was missing or a cached page was reused");
                previousFaction = rendered;
                const std::string prefix = std::string(faction.first) == "Americans" ? "mpa"
                    : std::string(faction.first) == "Russians" ? "mps" : "mpy";
                const auto* background = runtimeMatchScoreFrame(state, prefix + "scrnl", prefix + "scrn");
                const auto* strip = runtimeMatchScoreFrame(state, prefix + "scrnlbar01", prefix + "scrn");
                if (!background || !strip || background->embeddedPalette.size() != 256 || !strip->pcxFallback)
                    return fail("faction score resource family missing");
                std::cout << "Match score art " << faction.first << ": " << background->sourcePath
                    << " strip=" << strip->sourcePath << '\n';
            }
            state.config.playerCountry = country;
            state.repaintRequested = true;
        }
        state.resultPresentation = {};
        advanceRuntimeResultPresentation(state, GetTickCount64());
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
        if (!IsWindow(window) || state.resultPresentation.bannerVisible || state.runtimeExitRequested
            || !state.resultPresentation.scoreMusicSelected)
            return fail("Enter skipped statistics and exited");
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, static_cast<LPARAM>(1) << 30);
        if (!IsWindow(window)) return fail("held Enter dismissed the score page");
    }
    state.config.playerSlots.resize(2);
    state.matchScores.clear();
    state.matchScores["skirmishplayer1"] = {3, 0, 0, 0, 3200, 19};
    state.matchScores["skirmishplayer2"] = {0, 0, 3, 0, 0, 12};
    state.config.multiplayerActive = false;
    state.config.playerCountry = "Americans";
    state.skirmishVictory = true;
    state.repaintRequested = true;
    if (!save("result_players_two.bmp", paint())) return fail("two-player score rendering failed");
    // The Continue control must work with the mouse; other clicks stay here.
    SendMessageW(window, WM_LBUTTONDOWN, 0, MAKELPARAM(25, 25));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(25, 25));
    if (!IsWindow(window)) return fail("outside click dismissed the page");
    SendMessageW(window, WM_LBUTTONDOWN, 0, MAKELPARAM(710, 547));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(710, 547));
    if (IsWindow(window) || !state.runtimeExitRequested) return fail("Continue button did not return to the menu");
    std::cout << "Match score page: victory=ok defeat=ok players=8 frozenTick=ok originalArt=ok "
        "retailShell=ok originalFont=ok scoreMusic=ok repeat=ok mute=ok "
        "enterToScore=ok heldKey=ok mouseContinue=ok\n";
    return true;
}
