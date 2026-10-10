// Included after the production menu/window/result paths in RuntimeShell.cpp.
template <typename Paint, typename Save>
bool runRuntimeResignPresentationSmoke(RuntimeWindowState& state, HWND window,
    const Paint& paint, const Save& save, std::string& error) {
    const auto fail = [&](const char* reason) { error = reason; return false; };
    const bool soundsEnabled = state.runtimeSoundsEnabled;
    state.runtimeSoundsEnabled = false;
    if (!loadRuntimeSoundDatabase(state) || !state.evaLoaded)
        return fail("original resignation EVA database missing");
    state.campaignMode = false;
    state.config.multiplayerActive = false;
    state.config.playerSlots.resize(2);
    state.config.playerSlots[0].enabled = true;
    state.config.playerSlots[0].local = true;
    state.config.playerSlots[0].human = true;
    state.config.playerSlots[0].name = "指挥官";
    state.config.playerSlots[1].enabled = true;
    state.config.playerSlots[1].name = "电脑";
    state.economy.owner = "SkirmishPlayer1";
    state.matchScores.clear();
    state.matchScores["skirmishplayer1"] = {3, 0, 1, 0, 3200, 19};
    state.matchScores["skirmishplayer2"] = {1, 0, 3, 0, 800, 12};
    const auto scoreBefore = state.matchScores.at("skirmishplayer1").score;
    const int frozenTick = state.tick;
    struct Faction { const char* country; const char* sample; const char* output; };
    const Faction factions[] = {
        {"Americans", "ceva024", "result_resign_allied.bmp"},
        {"Russians", "csof024", "result_resign_soviet.bmp"},
        {"YuriCountry", "cyur024", "result_resign_yuri.bmp"}
    };
    for (const auto& faction : factions) {
        state.config.playerCountry = faction.country;
        state.config.playerSlots[0].country = faction.country;
        state.skirmishCountryByOwner["skirmishplayer1"] = faction.country;
        for (const int volume : {100, 0}) {
            state.runtimeVoiceVolume = volume;
            state.skirmishMatchFinished = false;
            state.skirmishVictory = false;
            state.resultPresentation = {};
            state.runtimeExitRequested = false;
            setRuntimeMenuOpen(state, true);
            activateRuntimeMenuAction(window, state, RuntimeMenuAction::Exit);
            activateRuntimeMenuAction(window, state, RuntimeMenuAction::CancelExit);
            if (state.skirmishMatchFinished || state.runtimeExitRequested || !IsWindow(window))
                return fail("cancel abandonment ended the match");
            activateRuntimeMenuAction(window, state, RuntimeMenuAction::Exit);
            // Simulate a queued battle announcement with a frozen tick clock.
            state.runtimeEvaBusyUntilTick = state.tick + 1000;
            state.runtimeEvaQueue.push_back(RuntimeSoundPlaybackRecord{});
            const int evaBefore = state.runtimeEvaEvents;
            activateRuntimeMenuAction(window, state, RuntimeMenuAction::ConfirmExit);
            if (!IsWindow(window) || state.runtimeExitRequested || !state.skirmishMatchFinished
                || state.skirmishVictory || state.runtimeMenuOpen || !state.resultPresentation.bannerVisible)
                return fail("abandonment skipped result presentation or closed the window");
            if (state.runtimeSoundPlaybackHistory.empty()) return fail("resignation EVA missing");
            const auto& voice = state.runtimeSoundPlaybackHistory.back();
            std::cout << "Resignation voice: section=" << voice.section << " sample=" << voice.sample
                << " playable=" << voice.playable << " volume=" << voice.volumePercent
                << " status=" << voice.status << " pending=" << state.runtimeEvaQueue.size() << '\n';
            if (voice.section != "EVA_YouHaveResigned" || voice.sample != faction.sample
                || !voice.playable || voice.queued || voice.suppressed || voice.volumePercent != volume
                || !state.runtimeEvaQueue.empty())
                return fail("resignation EVA faction, queue clearing or volume preference");
            const auto start = state.resultPresentation.startedAtMs;
            advanceRuntimeResultPresentation(state, start + 5999);
            if (!state.resultPresentation.bannerVisible || state.runtimeEvaEvents != evaBefore + 1)
                return fail("resignation EVA repeated or score entered before its delay");
            advanceRuntimeResultPresentation(state, start + 6000);
            if (!IsWindow(window) || state.runtimeExitRequested || state.resultPresentation.bannerVisible
                || !state.resultPresentation.scoreMusicSelected || state.tick != frozenTick
                || state.matchScores.at("skirmishplayer1").score != scoreBefore)
                return fail("abandonment failed to retain statistics on the score page");
            if (volume == 100 && !save(faction.output, paint()))
                return fail("resignation score capture failed");
            std::cout << "Resignation " << faction.country << ": sample=" << voice.sample
                << " volume=" << volume << " cancel=ok oneShot=ok score=ok frozenTick=ok\n";
        }
    }
    state.runtimeVoiceVolume = 0;
    state.runtimeSoundsEnabled = soundsEnabled;
    state.resultPresentation = {};
    if (!runRuntimeRankedDepartureSmoke(state, window, error)) return false;
    return true;
}
