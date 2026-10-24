bool runNoCooldownMenuSmoke(
    MenuState& state,
    const std::string& lobbyPath,
    std::string& error
) {
    const RuntimeConfig originalConfig = state.config;
    const bool originalNoCooldown = state.selection.noCooldown;
    const bool originalAccepted = state.selection.accepted;
    const ShellMenuPhase originalPhase = state.phase;
    const auto restore = [&]() {
        state.config = originalConfig;
        state.selection.noCooldown = originalNoCooldown;
        state.selection.accepted = originalAccepted;
        state.phase = originalPhase;
        state.backDirty = true;
        renderShellBackBuffer(state);
    };
    const auto fail = [&](const std::string& reason) {
        error = "no-cooldown option: " + reason;
        restore();
        return false;
    };
    fs::path preferencePath(lobbyPath);
    preferencePath.replace_filename(preferencePath.stem().string() + "_no-cooldown_preference.ini");
    {
        std::ofstream fixture(preferencePath, std::ios::binary);
        fixture << "; preserve user settings\n[Runtime]\nTargetFps=120\n"
                   "[Skirmish]\nNoCooldown=yes\nUnitCount=7\n"
                   "[CustomMod]\nKeepMe=unchanged\n";
        if (!fixture.good()) return fail("could not write isolated preferences");
    }
    state.config.configPath = pathString(preferencePath);
    for (const bool enabled : {false, true}) {
        state.phase = ShellMenuPhase::Idle;
        state.selection.noCooldown = !enabled;
        state.backDirty = true;
        if (!renderShellBackBuffer(state)) return fail("lobby render failed");
        const int controls = static_cast<int>(std::count_if(
            state.hits.begin(), state.hits.end(), [](const HitRegion& region) {
                return region.target.kind == HitKind::ToggleNoCooldown;
            }));
        // Exercise both the original checkbox and its Chinese label hit area.
        const HitRegion* hit = hitTest(state, POINT{enabled ? 331 : 281, 453});
        if (controls != 1 || hit == nullptr || hit->target.kind != HitKind::ToggleNoCooldown)
            return fail("checkbox or label is obscured by another lobby control");
        const HitTarget target = hit->target;
        activateShellTarget(state, target, 1);
        if (state.selection.noCooldown != enabled) return fail("click did not toggle noCooldown");
        state.backDirty = true;
        if (!renderShellBackBuffer(state)) return fail("changed lobby render failed");
        fs::path screenshot(lobbyPath);
        screenshot.replace_filename(screenshot.stem().string()
            + (enabled ? "_noCooldown_on.bmp" : "_noCooldown_off.bmp"));
        if (!writeBgraBmp(pathString(screenshot), state.composeFrame.bytes().data(),
                kBaseWidth, kBaseHeight, error)) return fail(error);

        // Use the real Start completion path, then reopen from disk with a
        // deliberately opposite in-memory default to catch stale state.
        completeMenu(state, true);
        RuntimeConfig reloadConfig = state.config;
        reloadConfig.noCooldown = !enabled;
        RuntimeSkirmishSelection reopened;
        loadSkirmishDefaults(reloadConfig, reopened);
        RuntimeConfigOverrides overrides;
        overrides.configPath = state.config.configPath;
        const RuntimeConfig runtimeConfig = loadRuntimeConfig(overrides);
        if (!state.selection.accepted || state.selection.noCooldown != enabled
                || reopened.noCooldown != enabled || runtimeConfig.noCooldown != enabled)
            return fail("accepted setup, reopened menu and match configuration disagree");
        IniFile saved;
        if (!saved.loadFromFile(state.config.configPath)
                || iniValueNoCase(saved, "Runtime", "TargetFps") != "120"
                || iniValueNoCase(saved, "Skirmish", "UnitCount") != "7"
                || iniValueNoCase(saved, "CustomMod", "KeepMe") != "unchanged")
            return fail("saving the no-cooldown option overwrote unrelated preferences");
        std::ifstream source(preferencePath, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(source)), {});
        if (text.find("; preserve user settings") == std::string::npos)
            return fail("saving the no-cooldown option lost user comments");
    }
    state.config.multiplayerActive = true;
    state.selection.noCooldown = false;
    state.phase = ShellMenuPhase::Idle;
    state.backDirty = true;
    renderShellBackBuffer(state);
    if (std::any_of(state.hits.begin(),state.hits.end(),[](const HitRegion& r) {
            return r.target.kind == HitKind::ToggleNoCooldown;
        })) return fail("offline option appeared in a network lobby");
    activateShellTarget(state,HitTarget{HitKind::ToggleNoCooldown,-1},1);
    RuntimeSkirmishSelection networkDefaults;
    loadSkirmishDefaults(state.config, networkDefaults);
    if (state.selection.noCooldown || networkDefaults.noCooldown)
        return fail("network setup inherited offline preference");
    restore();
    return true;
}
