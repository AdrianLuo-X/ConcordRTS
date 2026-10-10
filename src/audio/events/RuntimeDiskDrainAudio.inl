std::uint64_t runtimeDiskDrainSoundHandle(int objectId, int channel = 0) {
    return 0x4000000000000000ULL | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
}
std::uint64_t runtimeDiskMoveSoundHandle(int objectId, int channel = 0) {
    return 0x0800000000000000ULL | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
}
struct RuntimeDiskLoopSource {
    const WorldObjectInstance* object = nullptr;
    std::string event;
    std::string stage;
};

bool advanceRuntimeDiskLoopSources(RuntimeWindowState& state, RuntimeSuperWeaponBuildingAudioState& audio,
    const std::vector<RuntimeDiskLoopSource>& sources, std::uint64_t nowMilliseconds,
    std::uint64_t (*handle)(int,int)) {
    bool changed = false;
    std::unordered_set<int> live;
    const auto stop = [&](int id, int channel) {
        RuntimeWorldAudioAction action; action.kind = RuntimeWorldAudioActionKind::Stop;
        action.handleId = handle(id,channel); action.eventTick = state.tick;
        action.reason = "disk loop source stopped";
        applyRuntimeWorldAudioBackendAction(state,action); changed = true;
    };
    const auto play = [&](const WorldObjectInstance& source,const RuntimeWorldAudioSoundSpec& sound,
        const std::string& sample,int channel,int ordinal,int gain) {
        RuntimeWorldAudioAction action; action.handleId = handle(source.instanceId,channel);
        action.eventId = sound.eventId; action.sampleName = sample;
        action.cell = {source.x,source.y,source.level}; action.sourceObjectId = source.instanceId;
        action.eventTick = state.tick; action.playbackOrdinal = ordinal;
        action.gainPermille = gain; action.authoredGainPermille = sound.volumePercent * 10;
        action.queueAfterCurrent = channel == 0 && sound.loop;
        action.reason = channel == 0 ? "continuous disk sound sequence" : "disk loop decay";
        applyRuntimeWorldAudioBackendAction(state,action); changed = true;
        const auto path = findRuntimeSoundSamplePath(state,sample);
        return std::uint64_t(path ? std::max(1,runtimeWavDurationTicks(*path,1000)) : 1000);
    };
    for(const auto& source:sources) {
        const auto* object = source.object;
        const auto* spec = state.worldAudioCatalog.find(source.event);
        if(!object || !spec || spec->samples.empty()) continue;
        const int id = object->instanceId; live.insert(id);
        const bool newlyCreated = audio.newlyCreatedSources.erase(id) > 0;
        auto [it,inserted] = audio.sources.try_emplace(id); auto& previous = it->second;
        const bool transition = inserted || previous.stage != source.stage
            || previous.owner != object->owner || previous.event != source.event;
        if(transition) {
            stop(id,0); stop(id,1); previous = {};
            previous.stage = source.stage; previous.owner = object->owner; previous.event = source.event;
        }
        const int gain = runtimeSpatialAudioGainPermille(state,*spec,runtimeSpatialAudioPositionForObject(*object));
        if(gain <= 0) {
            if(previous.active) stop(id,0);
            previous.active = false; previous.gainPermille = 0; continue;
        }
        if(previous.active && previous.gainPermille != gain) {
            RuntimeWorldAudioAction action; action.kind = RuntimeWorldAudioActionKind::SetGain;
            action.handleId = handle(id,0); action.eventTick = state.tick; action.gainPermille = gain;
            applyRuntimeWorldAudioBackendAction(state,action);
        }
        previous.gainPermille = gain;
        // A voice evicted by soundmd Limit stays silent until this motor run
        // ends or leaves hearing range; it must not steal another handle
        // on every presentation frame.
        if(state.runtimeSoundsEnabled && state.runtimeWorldAudioLimitEvictedHandles.contains(handle(id,0))) continue;
        const auto controls = toLowerAsciiLocal(state.soundIni.getValue(source.event,"Control",""));
        const bool attack = controls.find("attack") != std::string::npos && spec->samples.size() > 2;
        const bool decay = controls.find("decay") != std::string::npos && spec->samples.size() > 2;
        const bool all = controls.find("all") != std::string::npos;
        const std::size_t first = attack ? 1 : 0, last = spec->samples.size() - (decay ? 1 : 0);
        const auto loopSample = [&]() {
            if(previous.remainingLoopSamples.empty())
                for(std::size_t index=first;index<last;++index) previous.remainingLoopSamples.push_back(index);
            const std::size_t pick = spec->randomSample
                ? runtimeAudioVariationSeed(source.event,id,previous.ordinal) % previous.remainingLoopSamples.size() : 0;
            const auto sample = spec->samples[previous.remainingLoopSamples[pick]];
            previous.remainingLoopSamples.erase(previous.remainingLoopSamples.begin()+pick);
            if(!all) previous.remainingLoopSamples.clear();
            return sample;
        };
        if(!previous.active) {
            previous.sample = transition && (audio.primed || newlyCreated) && attack ? spec->samples.front() : loopSample();
            previous.nextSampleMilliseconds = nowMilliseconds + play(*object,*spec,previous.sample,0,previous.ordinal++,gain);
            previous.queuedUntilMilliseconds = previous.nextSampleMilliseconds;
            previous.active = true;
            // Queue the first body segment now: the audio device crosses the
            // boundary even when the next presentation frame arrives late.
            const auto next = loopSample();
            previous.queuedUntilMilliseconds += play(*object,*spec,next,0,previous.ordinal++,gain);
            previous.sample = next;
            continue;
        }
        bool needsBuffer = nowMilliseconds >= previous.nextSampleMilliseconds;
        if(state.runtimeSoundsEnabled && state.runtimeVoiceAudio && state.runtimeVoiceAudio->ready())
            needsBuffer = state.runtimeVoiceAudio->queuedBuffers(handle(id,0)) < 2;
        if(!needsBuffer) continue;
        previous.sample = loopSample();
        previous.nextSampleMilliseconds = std::max(nowMilliseconds,previous.queuedUntilMilliseconds);
        previous.queuedUntilMilliseconds = previous.nextSampleMilliseconds
            + play(*object,*spec,previous.sample,0,previous.ordinal++,gain);
    }
    for(auto it=audio.sources.begin();it!=audio.sources.end();) {
        if(live.contains(it->first)){++it;continue;}
        const bool evicted = state.runtimeWorldAudioLimitEvictedHandles.contains(handle(it->first,0));
        stop(it->first,0);
        const auto* object = findObjectById(state,it->first);
        const auto* old = state.worldAudioCatalog.find(it->second.event);
        const auto controls = toLowerAsciiLocal(state.soundIni.getValue(it->second.event,"Control",""));
        if(!evicted && object && object->alive && object->owner == it->second.owner && it->second.active
            && old && !old->samples.empty() && controls.find("decay") != std::string::npos) {
            const int gain = runtimeSpatialAudioGainPermille(state,*old,runtimeSpatialAudioPositionForObject(*object));
            if(gain > 0) play(*object,*old,old->samples.back(),1,it->second.ordinal,gain);
        }
        it = audio.sources.erase(it);
    }
    audio.primed = true;
    return changed;
}

bool advanceRuntimeDiskDrainAudio(RuntimeWindowState& state,std::uint64_t nowMilliseconds) {
    if(!state.rulesLoaded || !state.artLoaded || !state.worldAudioCatalogLoaded) return false;
    const auto* art = state.art.findArtType(state.rules.getSpecialMechanismRules().drainAnimationType);
    std::vector<RuntimeDiskLoopSource> sources;
    if(art) for(const auto& [id,drain]:state.combatDetails.diskDrains.sources) {
        if(!runtimeDiskDrainRecordActive(state,id,drain)) continue;
        const auto* object = findObjectById(state,id); if(!object) continue;
        sources.push_back({object,art->startSound,std::to_string(drain.startedTick)+":"+std::to_string(drain.targetId)});
    }
    return advanceRuntimeDiskLoopSources(state,state.diskDrainAudio,sources,nowMilliseconds,runtimeDiskDrainSoundHandle);
}

bool advanceRuntimeDiskMoveAudio(RuntimeWindowState& state,std::uint64_t nowMilliseconds) {
    if(!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    std::vector<RuntimeDiskLoopSource> sources;
    for(const auto& object:state.world.getObjects()) {
        const auto* unit = findRuntimeUnitType(state.rules,object);
        if(!unit || !runtimeDiskMoveSoundIsLoop(state,*unit) || !object.alive || object.health<=0
            || runtimeGateObjectIsLimbo(object) || object.garrisonHostObjectId>=0) continue;
        if(state.specialMechanismsInitialized && (state.specialMechanisms.isChronoInTransit(object.instanceId)
            || state.specialMechanisms.isTemporalFrozen(object.instanceId))) continue;
        const auto behavior = state.behaviors.find(object.instanceId);
        bool moving = object.renderMoveActive || (behavior!=state.behaviors.end()
            && (behavior->second.state==RuntimeBehaviorState::Moving || behavior->second.state==RuntimeBehaviorState::Chasing
                || (behavior->second.kind==RuntimeBehaviorKind::Move
                    && (behavior->second.state==RuntimeBehaviorState::Queued
                        // Movement's cadence wait still belongs to the same
                        // motor run while another path step remains.
                        || (behavior->second.state==RuntimeBehaviorState::Cooldown && !behavior->second.path.empty())))));
        if(!moving) continue;
        sources.push_back({&object,trimAsciiLocal(unit->moveSound),object.type});
    }
    return advanceRuntimeDiskLoopSources(state,state.diskMoveAudio,sources,nowMilliseconds,runtimeDiskMoveSoundHandle);
}
