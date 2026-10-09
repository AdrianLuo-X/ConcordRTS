bool runRuntimeObserverMenuSmoke(
    MenuState& state, const std::string& lobbyPath, std::string& error
) {
    const auto originalConfig = state.config;
    const auto originalSelection = state.selection;
    const auto originalStarts = state.startingSpots;
    const auto originalPhase = state.phase;
    const auto originalDropdown = state.openDropdown;
    const int originalDropdownSlot = state.openDropdownSlot;
    const int originalMode = state.selectedMode;
    const int originalMap = state.selectedMap;
    HDC screenDc = GetDC(nullptr);
    const auto restore = [&]() {
        state.config = originalConfig;
        state.selection = originalSelection;
        state.startingSpots = originalStarts;
        state.phase = originalPhase;
        state.openDropdown = originalDropdown;
        state.openDropdownSlot = originalDropdownSlot;
        state.selectedMode = originalMode;
        state.selectedMap = originalMap;
        state.backDirty = true;
        renderShellBackBuffer(state);
        if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
    };
    const auto fail = [&](const std::string& reason) {
        error = "observer option: " + reason;
        restore();
        return false;
    };
    const auto battle = std::find_if(state.modes.begin(), state.modes.end(),
        [](const auto& mode) { return equalsAsciiNoCase(mode.key, "Battle"); });
    const auto twoPlayerMap = std::find_if(state.maps.begin(), state.maps.end(),
        [](const auto& map) {
            return map.minPlayers == 2 && map.maxPlayers == 2
                && mapSupportsFilter(map, "standard");
        });
    if (battle == state.modes.end() || twoPlayerMap == state.maps.end())
        return fail("original Battle mode or two-player map unavailable");
    state.selectedMode = static_cast<int>(battle - state.modes.begin());
    state.selectedMap = static_cast<int>(twoPlayerMap - state.maps.begin());
    state.config.multiplayerActive = false;
    state.phase = ShellMenuPhase::Idle;
    for (std::size_t i = 0; i < state.selection.slots.size(); ++i) {
        auto& slot = state.selection.slots[i];
        slot.enabled = i < 2;
        slot.human = i == 0;
        slot.country = i == 0 ? "Americans" : "Russians";
        slot.team = "-";
        state.startingSpots[i] = 0;
    }
    const int observerIndex = findCountryIndex(state, "Observer");
    if (observerIndex <= 0 || state.countries[observerIndex].displayName != L"观察")
        return fail("visible Chinese observer option missing");
    toggleDropdown(state, DropdownKind::Country, 0);
    const auto options = countryOptionIndices(state, 0);
    const auto observerOption = std::find(options.begin(), options.end(), observerIndex);
    if (observerOption == options.end()) return fail("human dropdown excludes observer");
    if (screenDc == nullptr || !ensureShellBackBuffer(state, screenDc)
            || !renderShellBackBuffer(state)) return fail("dropdown render failed");
    fs::path screenshot = lobbyPath.empty()
        ? fs::path(state.config.localDataRootPath) / "runtime_observer_menu.bmp"
        : fs::path(lobbyPath);
    screenshot.replace_filename(screenshot.stem().string() + "_observer_dropdown.bmp");
    if (!writeBgraBmp(pathString(screenshot), state.composePixels.data(),
            kBaseWidth, kBaseHeight, error)) return fail(error);
    selectDropdownOption(state, static_cast<int>(observerOption - options.begin()));
    if (state.selection.playerCountry != "Observer"
            || !runtimeObserverSlot(state.selection.slots[0])
            || state.selection.loadingScreenAssetId != "ls800obs.shp"
            || !state.selection.playerCountryBrief.empty()
            || selectedMapLimit(state) != 3 || selectedMapCombatantLimit(state) != 2
            || dropdownCanOpen(state, DropdownKind::Start, 0)
            || dropdownCanOpen(state, DropdownKind::Team, 0))
        return fail("selection/loading/extra combat slot/observer restrictions disagree");
    std::wstring reason;
    if (canStart(state, reason)) return fail("one computer plus observer starts a finished match");
    state.selection.slots[2].enabled = true;
    state.selection.slots[1].team = "1";
    state.selection.slots[2].team = "1";
    if (canStart(state, reason)) return fail("observer counts as an opposing team");
    state.selection.slots[2].team = "2";
    if (!canStart(state, reason)) return fail("two opposing computers cannot start on a two-player map");
    for (int ai : {1, 2}) {
        const auto aiOptions = countryOptionIndices(state, ai);
        if (std::find(aiOptions.begin(), aiOptions.end(), observerIndex) != aiOptions.end())
            return fail("observer entered an AI country pool");
    }
    state.backDirty = true;
    if (!renderShellBackBuffer(state)) return fail("selected observer lobby render failed");
    screenshot.replace_filename(fs::path(lobbyPath.empty() ? "runtime_observer_menu" : lobbyPath).stem().string()
        + "_observer_selected.bmp");
    if (!writeBgraBmp(pathString(screenshot), state.composePixels.data(),
            kBaseWidth, kBaseHeight, error)) return fail(error);

    RuntimeConfig reopen = state.config;
    reopen.playerCountry = state.selection.playerCountry;
    reopen.playerSlots.clear();
    for (const auto& source : state.selection.slots) {
        RuntimeConfig::PlayerSlot slot;
        slot.enabled = source.enabled; slot.human = source.human;
        slot.country = source.country; slot.team = source.team;
        slot.startPosition = source.startPosition;
        reopen.playerSlots.push_back(slot);
    }
    if (!runtimeLocalObserver(reopen)) return fail("accepted setup lost observer role");
    state.config = reopen;
    buildPlayerSlots(state); syncSelection(state);
    if (state.selection.playerCountry != "Observer" || !runtimeLocalObserver(state.config))
        return fail("reopened configuration resets observer to a real country");
    state.config.multiplayerActive = true;
    const auto networkOptions = countryOptionIndices(state, 0);
    if (runtimeLocalObserver(state.config)
            || std::find(networkOptions.begin(), networkOptions.end(), observerIndex) != networkOptions.end())
        return fail("offline role leaked into network combatant countries");
    state.config.multiplayerActive = false;
    const auto cooperative = std::find_if(state.modes.begin(), state.modes.end(),
        isUnavailableCooperativeMode);
    if (cooperative != state.modes.end()) {
        state.selectedMode = static_cast<int>(cooperative - state.modes.begin());
        const auto cooperativeOptions = countryOptionIndices(state, 0);
        if (std::find(cooperativeOptions.begin(), cooperativeOptions.end(), observerIndex)
                != cooperativeOptions.end()) return fail("scripted cooperative slot became an observer");
    }
    state.selectedMode = static_cast<int>(battle - state.modes.begin());
    state.openDropdown = DropdownKind::Country;
    state.openDropdownSlot = 0;
    const int americanOption = findCountryIndex(state, "Americans");
    selectDropdownOption(state, americanOption);
    if (selectedMapLimit(state) != 2 || state.selection.slots[2].enabled)
        return fail("returning to combatant retained an extra map slot");
    restore();
    std::cout << "Observer menu: human selection, native loading, two-player map capacity, "
        "opposing AI validation, no start/team, reopen, network/cooperative exclusion passed\n";
    return true;
}
