bool runSkirmishCrateOptionSmoke(
    MenuState& state,
    const std::string& lobbyPath,
    std::string& error
) {
    const RuntimeConfig originalConfig = state.config;
    const bool originalCrates = state.selection.crates;
    const bool originalAccepted = state.selection.accepted;
    const ShellMenuPhase originalPhase = state.phase;
    const auto restore = [&]() {
        state.config = originalConfig;
        state.selection.crates = originalCrates;
        state.selection.accepted = originalAccepted;
        state.phase = originalPhase;
        state.backDirty = true;
        renderShellBackBuffer(state);
    };
    const auto fail = [&](const std::string& reason) {
        error = "crate option: " + reason;
        restore();
        return false;
    };
    fs::path preferencePath(lobbyPath);
    preferencePath.replace_filename(preferencePath.stem().string() + "_crate_preference.ini");
    {
        std::ofstream fixture(preferencePath, std::ios::binary);
        fixture << "; preserve user settings\n[Runtime]\nTargetFps=120\n"
                   "[Skirmish]\nCrates=yes\nUnitCount=7\n"
                   "[CustomMod]\nKeepMe=unchanged\n";
        if (!fixture.good()) return fail("could not write isolated preferences");
    }
    state.config.configPath = pathString(preferencePath);
    for (const bool enabled : {false, true}) {
        state.phase = ShellMenuPhase::Idle;
        state.selection.crates = !enabled;
        state.backDirty = true;
        if (!renderShellBackBuffer(state)) return fail("lobby render failed");
        const int controls = static_cast<int>(std::count_if(
            state.hits.begin(), state.hits.end(), [](const HitRegion& region) {
                return region.target.kind == HitKind::ToggleCrates;
            }));
        // Exercise both the original checkbox and its Chinese label hit area.
        const HitRegion* hit = hitTest(state, POINT{enabled ? 118 : 67, 381});
        if (controls != 1 || hit == nullptr || hit->target.kind != HitKind::ToggleCrates)
            return fail("checkbox or label is obscured by another lobby control");
        const HitTarget target = hit->target;
        activateShellTarget(state, target, 1);
        if (state.selection.crates != enabled) return fail("click did not toggle crates");
        state.backDirty = true;
        if (!renderShellBackBuffer(state)) return fail("changed lobby render failed");
        fs::path screenshot(lobbyPath);
        screenshot.replace_filename(screenshot.stem().string()
            + (enabled ? "_crates_on.bmp" : "_crates_off.bmp"));
        if (!writeBgraBmp(pathString(screenshot), state.composeFrame.bytes().data(),
                kBaseWidth, kBaseHeight, error)) return fail(error);

        // Use the real Start completion path, then reopen from disk with a
        // deliberately opposite in-memory default to catch stale state.
        completeMenu(state, true);
        RuntimeConfig reloadConfig = state.config;
        reloadConfig.crates = !enabled;
        RuntimeSkirmishSelection reopened;
        loadSkirmishDefaults(reloadConfig, reopened);
        RuntimeConfigOverrides overrides;
        overrides.configPath = state.config.configPath;
        const RuntimeConfig runtimeConfig = loadRuntimeConfig(overrides);
        if (!state.selection.accepted || state.selection.crates != enabled
                || reopened.crates != enabled || runtimeConfig.crates != enabled)
            return fail("accepted setup, reopened menu and match configuration disagree");
        IniFile saved;
        if (!saved.loadFromFile(state.config.configPath)
                || iniValueNoCase(saved, "Runtime", "TargetFps") != "120"
                || iniValueNoCase(saved, "Skirmish", "UnitCount") != "7"
                || iniValueNoCase(saved, "CustomMod", "KeepMe") != "unchanged")
            return fail("saving the crate option overwrote unrelated preferences");
        std::ifstream source(preferencePath, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(source)), {});
        if (text.find("; preserve user settings") == std::string::npos)
            return fail("saving the crate option lost user comments");
    }
    restore();
    return true;
}
