bool runRuntimeWaterSinkingSaveSmoke(RuntimeWindowState& assets, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Water sinking save: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = assets.config; state.report = assets.report;
    state.rules = assets.rules; state.art = assets.art; state.theater = assets.theater;
    state.rulesLoaded = assets.rulesLoaded; state.artLoaded = assets.artLoaded;
    state.theaterLoaded = assets.theaterLoaded;
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true; state.runtimeSoundsEnabled = false;
    state.tick = 100;
    RuntimeWreckState wreck;
    wreck.object.instanceId = 1; wreck.object.type = "MTNK";
    wreck.object.kind = WorldObjectKind::Unit; wreck.object.owner = "Americans";
    wreck.object.alive = false; wreck.object.health = 0;
    wreck.object.x = 40; wreck.object.y = 40; wreck.object.cell = runtimeCellKey(40, 40);
    wreck.startTick = 100; wreck.sinking = true; wreck.x = 40.25; wreck.y = 40.375;
    wreck.groundLevel = 0.0; wreck.fallSpeed = 1.0;
    const auto pose = runtimeWreckObjectPose(wreck, state.tick, state.config.fixedTickHz);
    for (const char* phase : {"WreckWaterEntry", "WreckWake", "WreckWaterImpact"}) {
        const std::string animation = std::string(phase) == "WreckWake" ? state.rules.getWake() : "H2O_EXP3";
        spawnRuntimeWreckAnimation(state, wreck, pose, animation, true, phase);
        const int nativeFrameCount = std::string(phase) == "WreckWake" ? 15 : 16;
        if (state.impactEffects.empty() || state.impactEffects.back().lifecycleState != phase
            || state.impactEffects.back().animationFrameCount != nativeFrameCount
            || state.impactEffects.back().animationSequenceSteps != nativeFrameCount)
            return fail(std::string(phase) + " native animation/timeline unavailable");
        auto& effect = state.impactEffects.back();
        const int elapsed = std::max(1, std::min(effect.totalTicks - 1, effect.animationFrameDelayTicks * 2));
        effect.remainingTicks -= elapsed;
        effect.frameIndex = runtimeImpactAnimationFrameForStep(effect,
            elapsed / std::max(1, effect.animationFrameDelayTicks));
    }
    if (state.impactEffects.size() != 3) return fail("water actor count");
    RuntimeImpactEffect ordinary = state.impactEffects.front();
    ordinary.death = true; ordinary.lifecycleState = "UnitDestruction";
    ordinary.renderIntent = "unitDestruction"; ordinary.animId = "TWLT070";
    state.impactEffects.push_back(ordinary);
    const auto signature = [](const RuntimeImpactEffect& effect) {
        std::ostringstream out;
        out << runtimeMultiplayerSnapshotImpactRecord(effect) << '|' << iniHexEncode(effect.animId)
            << '|' << iniHexEncode(effect.renderIntent) << '|' << effect.frameIndex << '|'
            << effect.facing << '|' << effect.radius << '|' << runtimeBoolText(effect.bright);
        return out.str();
    };
    const auto allSignatures = [&](const RuntimeWindowState& source) {
        std::vector<std::string> records;
        for (const auto& effect : source.impactEffects) records.push_back(signature(effect));
        return records;
    };
    const auto encodedPresentation = [](const RuntimeImpactEffect& effect) {
        return iniHexEncode(effect.animId) + "|" + iniHexEncode(effect.renderIntent) + "|"
            + std::to_string(effect.frameIndex) + "|" + std::to_string(effect.facing) + "|"
            + std::to_string(effect.radius) + "|" + runtimeBoolText(effect.bright);
    };
    std::ostringstream saved; writeRuntimeUnitDeaths(state, saved);
    IniFile valid;
    if (!valid.loadFromString(saved.str()) || valid.getValue("UnitDeathAnimations", "Count") != "4")
        return fail("Schema 1 ordinary/mixed water export");
    RuntimeImpactEffect sentinel;
    sentinel.lifecycleState = "Impact"; sentinel.animId = "untouched";
    const auto reset = [&] { state.impactEffects = {sentinel}; };
    const auto expected = allSignatures(state);
    reset();
    const auto untouched = allSignatures(state);
    std::string diagnostic;
    if (!loadRuntimeUnitDeaths(state, valid, diagnostic, false) || allSignatures(state) != untouched)
        return fail("valid precommit validation changed effects");
    if (!loadRuntimeUnitDeaths(state, valid, diagnostic) || state.impactEffects.size() != expected.size() + 1)
        return fail("ordinary restoration failed: " + diagnostic);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (signature(state.impactEffects[index + 1]) != expected[index])
            return fail("restored water phase/presentation/timing changed");
    }
    // The complete canonical wire record must preserve each simulation clock
    // and the selected animation intent independently of presentation caches.
    int canonical = 0;
    for (std::size_t index = 1; index < state.impactEffects.size(); ++index) {
        const auto& source = state.impactEffects[index];
        RuntimeImpactEffect decoded;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(source), decoded)
            || runtimeMultiplayerSnapshotImpactRecord(decoded) != runtimeMultiplayerSnapshotImpactRecord(source))
            return fail("canonical water record changed");
        std::uint64_t before = runtimeStableHashString("WaterSinkingSave/1"), after = before;
        runtimeMultiplayerAuthoritativeMixImpact(before, source);
        runtimeMultiplayerAuthoritativeMixImpact(after, decoded);
        if (before != after) return fail("canonical water record hash changed");
        if (index <= 3) {
            auto advanced = source; --advanced.remainingTicks;
            std::uint64_t tickHash = runtimeStableHashString("WaterSinkingSave/1");
            runtimeMultiplayerAuthoritativeMixImpact(tickHash, advanced);
            if (tickHash == before) return fail("water animation clock absent from authority hash");
        }
        ++canonical;
    }
    const auto lastWater = state.impactEffects[3];
    int invalidLate = 0;
    const auto reject = [&](const std::string& patch, const std::string& label) {
        IniFile bad = valid, overlay;
        if (!overlay.loadFromString("[UnitDeathAnimations]\n" + patch + "\n")) return fail(label + " patch");
        bad.appendFrom(overlay);
        reset();
        std::string reason;
        if (loadRuntimeUnitDeaths(state, bad, reason) || reason.empty() || allSignatures(state) != untouched)
            return fail(label + " accepted or partially appended");
        reason.clear();
        if (loadRuntimeUnitDeaths(state, bad, reason, false) || reason.empty() || allSignatures(state) != untouched)
            return fail(label + " precommit validation accepted or mutated effects");
        ++invalidLate; return true;
    };
    if (!reject("Impact2=broken", "late corrupt wire record")) return false;
    for (int variant = 0; variant < 7; ++variant) {
        auto bad = lastWater;
        if (variant == 0) bad.lifecycleState = "WreckWaterOther";
        if (variant == 1) bad.death = true;
        if (variant == 2) bad.damage = 1;
        if (variant == 3) bad.cellSpread = 0.5;
        if (variant == 4) bad.hasWorldPosition = false;
        if (variant == 5) bad.worldX = std::numeric_limits<double>::quiet_NaN();
        if (variant == 6) bad.warheadImpactIntentId = "H2O_EXP1";
        if (!reject("Impact2=" + runtimeMultiplayerSnapshotImpactRecord(bad), "late invalid water actor " + std::to_string(variant)))
            return false;
    }
    for (int variant = 0; variant < 4; ++variant) {
        auto bad = lastWater;
        if (variant == 0) bad.renderIntent = "squidSplash";
        if (variant == 1) bad.animId = "H2O_EXP1";
        if (variant == 2) bad.animId.clear();
        if (variant == 3) bad.facing = 300;
        if (!reject("Presentation2=" + encodedPresentation(bad), "late invalid water presentation " + std::to_string(variant)))
            return false;
    }
    if (!reject("Schema=2", "unknown schema") || !reject("Schema=", "missing schema")) return false;
    reset();
    IniFile legacy;
    if (!loadRuntimeUnitDeaths(state, legacy, diagnostic) || allSignatures(state) != untouched
        || !loadRuntimeUnitDeaths(state, legacy, diagnostic, false) || allSignatures(state) != untouched)
        return fail("legacy missing section changed effects");
    std::cout << "Water sinking save: phases=3 mixedDeaths=1 canonical=" << canonical
        << " invalidLate=" << invalidLate << " precommit=PASS legacy=PASS\n";
    return true;
}
