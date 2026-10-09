bool runRuntimeSkirmishModeTransitionSmoke(MenuState& state, std::string& error) {
    const auto modeIndex = [&](const char* key) {
        for (std::size_t i = 0; i < state.modes.size(); ++i)
            if (equalsAsciiNoCase(state.modes[i].key, key)) return static_cast<int>(i);
        return -1;
    };
    const int battle = modeIndex("Battle");
    const int coop = modeIndex("Cooperative");
    const int unholy = modeIndex("UnholyAlliance");
    if (battle < 0 || coop < 0 || unholy < 0) {
        error = "required original modes are unavailable";
        return false;
    }
    activateTarget(state, {HitKind::Mode, battle}, 1);
    for (std::size_t i = 0; i < state.selection.slots.size(); ++i) {
        auto& slot = state.selection.slots[i];
        slot.enabled = i < 2;
        slot.human = i == 0;
        slot.country = i == 0 ? "Americans" : "Russians";
        slot.team = "-";
        slot.startPosition = 0;
        state.startingSpots[i] = 0;
    }
    syncSelection(state);
    activateShellTarget(state, {HitKind::ChooseMap, -1}, 1);
    activateShellTarget(state, {HitKind::Mode, coop}, 1);
    activateShellTarget(state, {HitKind::Mode, unholy}, 1);
    int twoPlayerMap = -1;
    for (std::size_t i = 0; i < state.filteredMaps.size(); ++i)
        if (state.maps[state.filteredMaps[i]].maxPlayers == 2) { twoPlayerMap = static_cast<int>(i); break; }
    if (twoPlayerMap < 0) { error = "no original two-player map"; return false; }
    activateShellTarget(state, {HitKind::Map, twoPlayerMap}, 1);
    activateShellTarget(state, {HitKind::UseMap, -1}, 1);
    std::wstring reason;
    const bool starts = canStart(state, reason);
    const bool inheritedAlliance = state.selection.slots[0].team == "1"
        && state.selection.slots[1].team == "1";
    std::cout << "Unholy mode switch: team0=" << state.selection.slots[0].team
        << " team1=" << state.selection.slots[1].team << " active="
        << std::count_if(state.selection.slots.begin(), state.selection.slots.end(),
            [](const auto& slot) { return slot.enabled; }) << " startAllowed=" << starts << '\n';
    if (inheritedAlliance || state.selection.slots[0].country != "Americans"
        || state.selection.slots[1].country != "Russians"
        || state.startingSpots[0] != 0 || state.startingSpots[1] != 0 || !starts) {
        error = "cooperative A/B alliance, countries or starting positions leaked into Unholy Alliance";
        return false;
    }
    const auto fail = [&](const std::string& detail) { error = detail; return false; };
    const auto setup = [&] { return SkirmishSlotSetup{state.selection.slots, state.startingSpots}; };
    const auto matches = [&](const SkirmishSlotSetup& expected) {
        if (expected.starts != state.startingSpots || expected.slots.size() != state.selection.slots.size())
            return false;
        for (std::size_t i = 0; i < expected.slots.size(); ++i) {
            const auto& a = expected.slots[i];
            const auto& b = state.selection.slots[i];
            if (a.enabled != b.enabled || a.human != b.human || a.name != b.name
                || a.country != b.country || a.color != b.color || a.team != b.team
                || a.difficulty != b.difficulty || a.startPosition != b.startPosition) return false;
        }
        return true;
    };
    const auto act = [&](HitKind kind, int index = -1) {
        activateShellTarget(state, {kind, index}, 1);
    };

    // Explicit alliances are preserved, but a match with no opposing side is
    // rejected. FFA ignores teams; every spelling of "no team" stays hostile.
    state.selection.slots[0].team = state.selection.slots[1].team = "1";
    for (int mode : {battle, unholy}) {
        act(HitKind::Mode, mode);
        if (canStart(state, reason) || reason.empty()
            || state.selection.slots[0].team != "1" || state.selection.slots[1].team != "1")
            return fail("an all-allied lobby started or its deliberate teams were overwritten");
    }
    const int ffa = modeIndex("FreeForAll");
    if (ffa < 0) return fail("Free For All unavailable");
    act(HitKind::Mode, ffa);
    if (!canStart(state, reason)) return fail("Free For All incorrectly honored teams");
    act(HitKind::Mode, unholy);
    for (const char* team : {"-", "", "0", "None", " No Team "}) {
        state.selection.slots[0].team = state.selection.slots[1].team = team;
        if (!canStart(state, reason)) return fail("unassigned teams were treated as allies");
    }

    state.selection.slots[0].country = "Africans";
    state.selection.slots[1].country = "YuriCountry";
    state.selection.slots[0].team = "3";
    state.selection.slots[1].team = "4";
    state.startingSpots[0] = 1;
    state.startingSpots[1] = 2;
    syncSelection(state);
    const auto ordinary = setup();
    act(HitKind::ChooseMap);
    act(HitKind::Mode, coop);
    act(HitKind::Mode, coop); // Clicking the active mode must not replace the saved setup.
    act(HitKind::Mode, unholy);
    act(HitKind::UseMap);
    if (!matches(ordinary) || !canStart(state, reason) || state.beforeCooperative.has_value())
        return fail("repeat mode changes failed to restore the complete ordinary setup");

    act(HitKind::ChooseMap);
    act(HitKind::Mode, coop);
    act(HitKind::CancelMap);
    if (!matches(ordinary) || state.beforeCooperative.has_value())
        return fail("canceling a cooperative preview retained temporary slot state");

    // Commit Cooperative, preview ordinary play, cancel back to Cooperative,
    // then leave again: the ordinary setup must survive both page snapshots.
    act(HitKind::ChooseMap);
    act(HitKind::Mode, coop);
    act(HitKind::UseMap);
    const auto cooperative = setup();
    act(HitKind::ChooseMap);
    act(HitKind::Mode, unholy);
    act(HitKind::CancelMap);
    if (!modeIsCooperative(state) || !matches(cooperative) || !state.beforeCooperative.has_value()
        || !canStart(state, reason)) return fail("cancel did not restore the cooperative page snapshot");
    act(HitKind::ChooseMap);
    act(HitKind::Mode, unholy);
    act(HitKind::UseMap);
    if (!matches(ordinary) || state.beforeCooperative.has_value())
        return fail("canceling from Cooperative lost the earlier ordinary setup");

    RuntimeConfig directCooperative = state.config;
    directCooperative.gameMode = "Cooperative";
    directCooperative.gameModeRulesOverride = "MPCoopMD.ini";
    resetMenuForEntry(state, directCooperative);
    if (!modeIsCooperative(state) || !canStart(state, reason))
        return fail("direct cooperative menu fixture failed");
    act(HitKind::Mode, unholy);
    for (std::size_t i = 0; i < state.filteredMaps.size(); ++i) {
        if (state.maps[state.filteredMaps[i]].maxPlayers == 2) {
            act(HitKind::Map, static_cast<int>(i));
            break;
        }
    }
    if (!canStart(state, reason) || state.selection.slots[0].team != "-"
        || state.selection.slots[1].team != "-" || state.startingSpots[0] != 0
        || state.startingSpots[1] != 0)
        return fail("direct cooperative entry leaked forced teams or starts when leaving");

    for (const char* key : {"Battle", "FreeForAll", "UnholyAlliance", "Megawealth",
            "Duel", "MeatGrind", "NavalWar", "TeamGame"}) {
        const int index = modeIndex(key);
        if (index < 0) return fail(std::string("original mode missing: ") + key);
        act(HitKind::Mode, index);
        if (state.filteredMaps.empty()) return fail(std::string("mode has no maps: ") + key);
        state.selection.slots[0].team = "1";
        state.selection.slots[1].team = "2";
        syncSelection(state);
        if (state.selection.gameMode != key
            || !equalsAsciiNoCase(state.selection.gameModeRulesOverride,
                state.modes[index].rulesOverride) || !canStart(state, reason))
            return fail(std::string("mode/rules/map selection failed: ") + key);
    }

    std::cout << "Skirmish mode transitions: all eight ordinary mode/rules/map selections; ordinary slots restored, repeated switches, "
        "cancel in both directions, direct Cooperative entry, all-allied rejection and FFA passed\n";
    return true;
}
