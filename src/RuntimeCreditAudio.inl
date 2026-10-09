void snapRuntimeCreditAudio(RuntimeWindowState& state) {
    auto& audio = state.creditAudio;
    audio.observed = true;
    audio.owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    audio.balance = state.economy.credits;
    audio.pendingDirection = 0;
    audio.lastObservationMs = 0;
    audio.nextSoundMs = 0;
    audio.burstEndMs = 0;
    audio.burstPlayed = false;
}

void prepareRuntimeCreditAudio(RuntimeWindowState& state) {
    auto& audio = state.creditAudio;
    const std::string configured = stripRuntimeSoundComment(
        state.rules.getAudioVisualValue("CreditTicks"));
    if (audio.samplesPrepared && audio.configuredPair == configured) return;
    audio.configuredPair = configured;
    audio.sections = {"CreditUp", "CreditDown"};
    audio.intervalMs = {80, 80};
    if (!configured.empty()) {
        const auto comma = configured.find(',');
        audio.sections[0] = trimAsciiLocal(configured.substr(0, comma));
        audio.sections[1] = comma == std::string::npos ? ""
            : trimAsciiLocal(configured.substr(comma + 1));
    }
    for (std::size_t index = 0; index < audio.sections.size(); ++index) {
        auto& section = audio.sections[index];
        if (toLowerAsciiLocal(section) == "none") section.clear();
        if (section.empty()) continue;
        const auto sample = resolveRuntimeSoundSampleName(state, "sound:" + section, section);
        if (!sample) continue;
        const auto path = findRuntimeSoundSamplePath(state, *sample);
        if (path) audio.intervalMs[index] = std::max(80, runtimeWavDurationTicks(*path, 1000));
    }
    audio.samplesPrepared = state.soundLoaded;
}

void advanceRuntimeCreditAudio(RuntimeWindowState& state, std::uint64_t nowMs) {
    // Economy facades are temporary accounts used to advance other houses.
    // Observe the local account only, after those transactions have finished.
    if (state.remoteHumanHouseScopeActive || state.computerMiningScopeActive) return;
    auto& audio = state.creditAudio;
    const auto owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    if (!audio.observed || owner != audio.owner || nowMs < audio.lastObservationMs
        || owner.empty() || state.runtimeMenuOpen || state.skirmishMatchFinished
        || state.runtimeSuppressPlayerAudioFeedback || !state.runtimeSoundEffectsEnabled
        || state.runtimeSoundVolume <= 0) {
        snapRuntimeCreditAudio(state);
        audio.lastObservationMs = nowMs;
        return;
    }
    audio.lastObservationMs = nowMs;
    if (state.economy.credits != audio.balance) {
        prepareRuntimeCreditAudio(state);
        const int direction = state.economy.credits > audio.balance ? 1 : -1;
        if (direction != audio.pendingDirection) audio.burstPlayed = false;
        audio.pendingDirection = direction;
        auto remaining = std::abs(static_cast<long long>(state.economy.credits) - audio.balance);
        int ticks = 0;
        // A lump-sum transaction still counts audibly. Larger amounts use
        // more ticks, with a bounded tail; rapid transactions extend this
        // short burst instead of appending an unbounded queue of sounds.
        while (remaining > 0 && ticks < 24) {
            remaining -= std::max(1LL, (remaining + 3) / 4);
            ++ticks;
        }
        const std::size_t index = direction > 0 ? 0U : 1U;
        audio.burstEndMs = nowMs + static_cast<std::uint64_t>(ticks) * audio.intervalMs[index];
        audio.balance = state.economy.credits;
    }
    if (audio.burstPlayed && nowMs >= audio.burstEndMs) audio.pendingDirection = 0;
    if (audio.pendingDirection == 0 || nowMs < audio.nextSoundMs) return;
    prepareRuntimeCreditAudio(state);
    const std::size_t index = audio.pendingDirection > 0 ? 0U : 1U;
    if (audio.sections[index].empty()) {
        audio.pendingDirection = 0;
        return;
    }
    audio.burstPlayed = true;
    // Keep the original sample/volume and let each short tick finish. A late
    // presentation frame can emit at most one cue, never a catch-up pileup.
    audio.nextSoundMs = nowMs + static_cast<std::uint64_t>(audio.intervalMs[index]);
    emitRuntimeSound(state, "sound:" + audio.sections[index]);
}
