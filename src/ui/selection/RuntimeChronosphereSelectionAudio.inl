// Private, wall-clock presentation for the Chronosphere's first target.
// Neither its sample bag nor its playback clock belongs to simulation state.
std::uint64_t runtimeChronosphereSelectionSoundHandle(bool decay = false) {
    return decay ? 0xD000000000000002ULL : 0xD000000000000001ULL;
}

bool runtimeChronosphereSelectionAudioOwnerVisible(const RuntimeWindowState& state,
    const RuntimeChronosphereSelectionAudioState& audio) {
    const auto owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    return !state.remoteHumanHouseScopeActive && !owner.empty()
        && owner == toLowerAsciiLocal(trimAsciiLocal(audio.owner));
}

bool runtimeChronosphereSelectionAudioBindingValid(const RuntimeWindowState& state) {
    const auto& audio = state.chronosphereSelectionAudio;
    const auto matches = [&](const RuntimeImpactEffect& marker) {
        return toLowerAsciiLocal(trimAsciiLocal(marker.owner))
                == toLowerAsciiLocal(trimAsciiLocal(audio.owner))
            && toLowerAsciiLocal(trimAsciiLocal(marker.weaponId))
                == toLowerAsciiLocal(trimAsciiLocal(audio.weaponId))
            && marker.cell.x == audio.cell.x && marker.cell.y == audio.cell.y;
    };
    const auto* source = state.superWeapons.findSidebarSnapshot(audio.owner, audio.weaponId);
    if (!source) return false;
    if (state.localChronosphereMarker && matches(state.localChronosphereMarker->animation)) {
        const auto& intent = state.multiplayerIntentRouter.superWeapon();
        const bool available = source->state == SuperWeaponRuntimeState::Ready
            || source->state == SuperWeaponRuntimeState::SelectingTarget
            || source->state == SuperWeaponRuntimeState::AwaitingSecondTarget;
        if (available && intent && intent->expectedTargetCount == 2 && !intent->targets.empty()
            && toLowerAsciiLocal(trimAsciiLocal(intent->superWeaponId))
                == toLowerAsciiLocal(trimAsciiLocal(audio.weaponId))
            && intent->targets.front().cell.x == audio.cell.x
            && intent->targets.front().cell.y == audio.cell.y) return true;
    }
    if (source->state != SuperWeaponRuntimeState::AwaitingSecondTarget
        || source->firstTargetCell.x != audio.cell.x
        || source->firstTargetCell.y != audio.cell.y) return false;
    return std::any_of(state.impactEffects.begin(), state.impactEffects.end(),
        [&](const RuntimeImpactEffect& effect) {
            return effect.lifecycleState == "ChronosphereSelection" && matches(effect);
        });
}

void hardStopRuntimeChronosphereSelectionAudio(RuntimeWindowState& state) {
    for (bool decay : {false, true}) {
        RuntimeWorldAudioAction action;
        action.kind = RuntimeWorldAudioActionKind::Stop;
        action.handleId = runtimeChronosphereSelectionSoundHandle(decay);
        action.eventTick = state.tick;
        action.reason = "private Chronosphere selection stopped";
        applyRuntimeWorldAudioBackendAction(state, action);
    }
    auto& sound = state.chronosphereSelectionAudio.sound;
    sound.active = false;
    sound.gainPermille = 0;
    sound.nextSampleMilliseconds = sound.queuedUntilMilliseconds = 0;
}

std::uint64_t playRuntimeChronosphereSelectionSample(RuntimeWindowState& state,
    const RuntimeWorldAudioSoundSpec& spec, const std::string& sample, bool decay) {
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    const auto variation = runtimeAudioVariationFor(spec.variation,
        runtimeAudioVariationSeed(spec.eventId, runtimeCellKey(audio.cell.x, audio.cell.y),
            sound.ordinal));
    RuntimeWorldAudioAction action;
    action.handleId = runtimeChronosphereSelectionSoundHandle(decay);
    action.eventId = spec.eventId;
    action.sampleName = sample;
    action.cell = audio.cell;
    action.eventTick = state.tick;
    action.playbackOrdinal = sound.ordinal++;
    action.frequencyPermille = variation.frequencyPermille;
    action.authoredGainPermille = std::clamp(spec.volumePercent * 10
        + variation.volumeDeltaPermille, 0, 1000);
    action.gainPermille = sound.gainPermille;
    action.local = spec.local;
    action.queueAfterCurrent = !decay && spec.loop;
    action.reason = decay ? "Chronosphere selection decay" : "Chronosphere selection sequence";
    applyRuntimeWorldAudioBackendAction(state, action);
    const auto path = findRuntimeSoundSamplePath(state, sample);
    const auto duration = std::uint64_t(path ? std::max(1, runtimeWavDurationTicks(*path, 1000)) : 1000);
    return std::max<std::uint64_t>(1,
        (duration * 1000 + std::max(1, variation.frequencyPermille) - 1)
            / std::max(1, variation.frequencyPermille));
}

std::string nextRuntimeChronosphereSelectionLoopSample(RuntimeWindowState& state,
    const RuntimeWorldAudioSoundSpec& spec) {
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(spec.eventId, "Control", ""));
    const bool attack = controls.find("attack") != std::string::npos && spec.samples.size() > 2;
    const bool decay = controls.find("decay") != std::string::npos && spec.samples.size() > 2;
    const bool all = controls.find("all") != std::string::npos;
    const std::size_t first = attack ? 1 : 0;
    const std::size_t last = spec.samples.size() - (decay ? 1 : 0);
    if (sound.remainingLoopSamples.empty())
        for (std::size_t index = first; index < last; ++index)
            sound.remainingLoopSamples.push_back(index);
    const std::size_t pick = spec.randomSample
        ? runtimeAudioVariationSeed(spec.eventId, runtimeCellKey(audio.cell.x, audio.cell.y),
            sound.ordinal) % sound.remainingLoopSamples.size() : 0;
    const auto sample = spec.samples[sound.remainingLoopSamples[pick]];
    sound.remainingLoopSamples.erase(sound.remainingLoopSamples.begin() + pick);
    if (!all) sound.remainingLoopSamples.clear();
    return sample;
}

void beginRuntimeChronosphereSelectionSequence(RuntimeWindowState& state,
    const RuntimeWorldAudioSoundSpec& spec, std::uint64_t nowMilliseconds, bool firstSelection) {
    auto& sound = state.chronosphereSelectionAudio.sound;
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(spec.eventId, "Control", ""));
    const bool attack = spec.loop && controls.find("attack") != std::string::npos
        && spec.samples.size() > 2;
    sound.sample = firstSelection && attack ? spec.samples.front()
        : nextRuntimeChronosphereSelectionLoopSample(state, spec);
    sound.nextSampleMilliseconds = nowMilliseconds
        + playRuntimeChronosphereSelectionSample(state, spec, sound.sample, false);
    sound.queuedUntilMilliseconds = sound.nextSampleMilliseconds;
    sound.active = true;
    if (!spec.loop) return;
    // Prequeue a body segment so the device crosses the attack boundary
    // without relying on the next paint or on the simulation's pause state.
    sound.sample = nextRuntimeChronosphereSelectionLoopSample(state, spec);
    sound.queuedUntilMilliseconds += playRuntimeChronosphereSelectionSample(state, spec, sound.sample, false);
}

void stopRuntimeChronosphereSelectionAudio(RuntimeWindowState& state, bool decay) {
    const auto previous = state.chronosphereSelectionAudio;
    // Clearing both the private marker and the host event in one command
    // must not cut off the decay that the first clear already requested.
    if (decay && !previous.selected && previous.sound.stage == "decay"
        && previous.sound.active) return;
    hardStopRuntimeChronosphereSelectionAudio(state);
    state.chronosphereSelectionAudio = {};
    const auto* spec = state.worldAudioCatalog.find(previous.sound.event);
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(previous.sound.event, "Control", ""));
    if (!decay || !previous.selected || !previous.sound.active || !previous.sound.loop
        || !runtimeChronosphereSelectionAudioOwnerVisible(state, previous)
        || state.runtimeSuppressPlayerAudioFeedback || !spec || spec->samples.size() <= 2
        || controls.find("decay") == std::string::npos) return;
    const int gain = runtimeSpatialAudioGainPermille(state, *spec,
        {double(previous.cell.x), double(previous.cell.y), double(previous.cell.level)});
    if (gain <= 0) return;
    auto& audio = state.chronosphereSelectionAudio;
    audio = previous;
    audio.selected = false;
    audio.sound.stage = "decay";
    audio.sound.gainPermille = gain;
    audio.sound.sample = spec->samples.back();
    audio.sound.nextSampleMilliseconds = GetTickCount64()
        + playRuntimeChronosphereSelectionSample(state, *spec, audio.sound.sample, true);
    audio.sound.queuedUntilMilliseconds = audio.sound.nextSampleMilliseconds;
    audio.sound.active = true;
}

void startRuntimeChronosphereSelectionAudio(RuntimeWindowState& state,
    const RuntimeImpactEffect& marker, std::uint64_t nowMilliseconds = GetTickCount64()) {
    if (state.runtimeSuppressPlayerAudioFeedback || state.remoteHumanHouseScopeActive
        || trimAsciiLocal(marker.owner).empty()
        || toLowerAsciiLocal(trimAsciiLocal(marker.owner))
            != toLowerAsciiLocal(trimAsciiLocal(state.economy.owner))) return;
    // Resource-only smoke fixtures may create their first marker before the
    // normal startup has loaded soundmd. Loading clears only audio playback.
    if (!state.worldAudioCatalogLoaded) loadRuntimeSoundDatabase(state);
    const auto event = runtimeArtAnimationSound(state, state.art.findArtType(marker.animId));
    const auto* spec = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(event) : nullptr;
    if (!spec || spec->samples.empty()) return;
    const auto& previous = state.chronosphereSelectionAudio;
    if (previous.selected && toLowerAsciiLocal(trimAsciiLocal(previous.owner))
            == toLowerAsciiLocal(trimAsciiLocal(marker.owner))
        && toLowerAsciiLocal(trimAsciiLocal(previous.weaponId))
            == toLowerAsciiLocal(trimAsciiLocal(marker.weaponId))
        && previous.cell.x == marker.cell.x && previous.cell.y == marker.cell.y
        && previous.sound.event == spec->eventId) return;
    stopRuntimeChronosphereSelectionAudio(state, false);
    auto& audio = state.chronosphereSelectionAudio;
    audio.owner = marker.owner;
    audio.weaponId = marker.weaponId;
    audio.cell = {marker.cell.x, marker.cell.y, marker.level};
    audio.selected = true;
    audio.sound.stage = "selection";
    audio.sound.owner = marker.owner;
    audio.sound.event = spec->eventId;
    audio.sound.loop = spec->loop;
    noteRuntimeSoundIntent(state, "sound:" + spec->eventId);
    audio.sound.gainPermille = runtimeSpatialAudioGainPermille(state, *spec,
        {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)});
    if (audio.sound.gainPermille > 0)
        beginRuntimeChronosphereSelectionSequence(state, *spec, nowMilliseconds, true);
}

bool advanceRuntimeChronosphereSelectionAudio(RuntimeWindowState& state, std::uint64_t nowMilliseconds) {
    // Remote-house simulation temporarily borrows the facade owner. It is
    // not an actual listener/view switch and must not restart this loop.
    if (state.remoteHumanHouseScopeActive) return false;
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    if (!audio.selected) {
        // Only the already-requested decay is managed here. A restored
        // authoritative first marker never recreates a selection attack.
        if (sound.stage != "decay" || !sound.active) return false;
        const auto* spec = state.worldAudioCatalog.find(sound.event);
        const int gain = spec ? runtimeSpatialAudioGainPermille(state, *spec,
            {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)}) : 0;
        if (nowMilliseconds >= sound.nextSampleMilliseconds || gain <= 0
            || state.runtimeSuppressPlayerAudioFeedback
            || !runtimeChronosphereSelectionAudioOwnerVisible(state, audio)) {
            hardStopRuntimeChronosphereSelectionAudio(state);
            audio = {};
            return true;
        }
        return false;
    }
    if (!runtimeChronosphereSelectionAudioBindingValid(state)) {
        stopRuntimeChronosphereSelectionAudio(state, true);
        return true;
    }
    const auto* spec = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(sound.event) : nullptr;
    const int gain = spec ? runtimeSpatialAudioGainPermille(state, *spec,
        {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)}) : 0;
    if (!spec || spec->samples.empty() || gain <= 0 || state.runtimeSuppressPlayerAudioFeedback
        || !runtimeChronosphereSelectionAudioOwnerVisible(state, audio)) {
        if (!sound.active) return false;
        hardStopRuntimeChronosphereSelectionAudio(state);
        return true;
    }
    bool changed = false;
    if (sound.active && sound.gainPermille != gain) {
        RuntimeWorldAudioAction action;
        action.kind = RuntimeWorldAudioActionKind::SetGain;
        action.handleId = runtimeChronosphereSelectionSoundHandle();
        action.eventId = spec->eventId;
        action.cell = audio.cell;
        action.eventTick = state.tick;
        action.authoredGainPermille = spec->volumePercent * 10;
        action.gainPermille = gain;
        action.reason = "Chronosphere selection listener gain changed";
        applyRuntimeWorldAudioBackendAction(state, action);
        changed = true;
    }
    sound.gainPermille = gain;
    if (!spec->loop) {
        if (sound.active && nowMilliseconds >= sound.nextSampleMilliseconds) {
            sound.active = false;
            changed = true;
        }
        return changed;
    }
    if (!sound.active) {
        beginRuntimeChronosphereSelectionSequence(state, *spec, nowMilliseconds, false);
        return true;
    }
    bool needsBuffer = nowMilliseconds >= sound.nextSampleMilliseconds;
    if (state.runtimeSoundsEnabled && state.runtimeVoiceAudio && state.runtimeVoiceAudio->ready())
        needsBuffer = state.runtimeVoiceAudio->queuedBuffers(runtimeChronosphereSelectionSoundHandle()) < 2;
    if (!needsBuffer) return changed;
    sound.sample = nextRuntimeChronosphereSelectionLoopSample(state, *spec);
    sound.nextSampleMilliseconds = std::max(nowMilliseconds, sound.queuedUntilMilliseconds);
    sound.queuedUntilMilliseconds = sound.nextSampleMilliseconds
        + playRuntimeChronosphereSelectionSample(state, *spec, sound.sample, false);
    return true;
}
