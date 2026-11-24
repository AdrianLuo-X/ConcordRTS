std::uint64_t runtimeSuperWeaponBuildingSoundHandle(int objectId, int channel) {
    return 0x8000000000000000ULL | (std::uint64_t(std::uint32_t(objectId))<<2) | unsigned(channel);
}

template<class SpatialGain, class ResolveDuration, class PublishAction>
bool advanceRuntimeSuperWeaponBuildingPlayback(
    const RuntimeGameSessionState& session,
    RuntimeSuperWeaponBuildingAudioState& audio,
    const RuntimeWorldAudioCatalog& catalog,
    const IniFile& soundIni,
    std::uint64_t nowMilliseconds,
    SpatialGain&& spatialGain,
    ResolveDuration&& resolveDuration,
    PublishAction&& publishAction
) {
    std::unordered_set<int> live;
    bool changed=false;
    const auto stop=[&](int id,int channel) {
        RuntimeWorldAudioAction action; action.kind=RuntimeWorldAudioActionKind::Stop;
        action.handleId=runtimeSuperWeaponBuildingSoundHandle(id,channel);
        action.eventTick=session.tick; action.reason="superweapon building animation stopped";
        publishAction(action); changed=true;
    };
    const auto gainFor=[&](const WorldObjectInstance& object,const RuntimeWorldAudioSoundSpec& sound) {
        if (!object.poweredOperational) return 0;
        return spatialGain(sound, runtimeSpatialAudioPositionForObject(object));
    };
    const auto play=[&](const WorldObjectInstance& object,const RuntimeWorldAudioSoundSpec& spec,
        const std::string& sample,int channel,int ordinal,int gain) {
        RuntimeWorldAudioAction action; action.kind=RuntimeWorldAudioActionKind::Play;
        action.handleId=runtimeSuperWeaponBuildingSoundHandle(object.instanceId,channel);
        action.eventId=spec.eventId; action.sampleName=sample;
        action.cell={object.x,object.y,object.level}; action.eventTick=session.tick;
        action.playbackOrdinal=ordinal; action.gainPermille=gain;
        action.reason="superweapon building animation StartSound";
        const auto variation=runtimeAudioVariationFor(spec.variation,
            runtimeAudioVariationSeed(spec.eventId,object.instanceId,ordinal));
        action.frequencyPermille=variation.frequencyPermille;
        action.gainPermille=std::clamp(action.gainPermille+variation.volumeDeltaPermille,0,1000);
        action.authoredGainPermille=std::clamp(spec.volumePercent*10+variation.volumeDeltaPermille,0,1000);
        action.sourceObjectId=object.instanceId;
        publishAction(action); changed=true;
        return resolveDuration(sample, action.frequencyPermille);
    };
    for(const auto& object:session.world.getObjects()) {
        const auto pose=runtimeSuperWeaponBuildingSnapshot(session,object);
        if(!pose || object.health<=0) continue;
        const auto* building=session.rules.findBuildingType(object.type);
        const auto* art=session.art.findArtType(building->image.empty()?object.type:building->image);
        if(!art) continue;
        live.insert(object.instanceId);
        const auto layerFor=[&](const std::string& key)->const ArtType::ActiveAnimation* {
            for(const auto& layer:art->auxiliaryAnimations) if(layer.key==key)return &layer;
            return nullptr;
        };
        const bool damaged=object.health<=building->strength
            * session.rules.getSpecialMechanismRules().conditionYellow;
        const auto animationFor=[&](const ArtType::ActiveAnimation* layer)->const ArtType* {
            return layer ? session.art.findArtType(damaged && !layer->damagedImage.empty()
                ?layer->damagedImage:layer->image) : nullptr;
        };
        std::string stage=pose->resourceId;
        auto* layer=layerFor(stage); const auto* animation=animationFor(layer);
        // The opening sequence ends before the weapon is fully charged.
        // Follow the renderer's effective layer, so its hum begins at that
        // transition rather than a minute later at the Ready state.
        if(animation && animation->hasAnimationEnd && (stage=="SuperAnimTwo" || stage=="SuperAnimFour")) {
            const int start=animation->hasAnimationStart?animation->animationStart:0;
            const int duration=std::max(1,animation->animationEnd-start+1)
                *runtimeSuperWeaponBuildingAnimationDelay(animation,session.config.fixedTickHz);
            if(pose->progress>=duration) {
                stage=stage=="SuperAnimTwo"?"SuperAnimThree":"SuperAnim";
                layer=layerFor(stage); animation=animationFor(layer);
            }
        }
        const std::string event=animation?animation->startSound:"";
        auto [it,inserted]=audio.sources.try_emplace(object.instanceId);
        auto& previous=it->second;
        const bool transition=inserted || previous.stage!=stage || previous.event!=event || previous.owner!=object.owner;
        if(transition) {
            if(previous.active) {
                stop(object.instanceId,1);
                const auto controls=toLowerAsciiLocal(soundIni.getValue(previous.event,"Control",""));
                const auto* old=catalog.find(previous.event);
                if(old && previous.owner==object.owner && object.poweredOperational
                    && controls.find("decay")!=std::string::npos && !old->samples.empty()) {
                    const int gain=gainFor(object,*old);
                    if(gain>0)play(object,*old,old->samples.back(),2,previous.ordinal,gain);
                }
            }
            previous={}; previous.stage=stage; previous.event=event; previous.owner=object.owner;
        }
        const auto* spec=catalog.find(event);
        if(!spec || spec->samples.empty()) continue;
        previous.loop=spec->loop;
        const int gain=gainFor(object,*spec);
        if(!spec->loop) {
            if(transition && audio.primed && gain>0) {
                const auto index=runtimeAudioVariationSeed(event,object.instanceId,0)%spec->samples.size();
                play(object,*spec,spec->samples[index],0,0,gain);
            }
            continue;
        }
        if(gain<=0) {
            if(previous.active)stop(object.instanceId,1);
            previous.active=false; previous.gainPermille=0;
            continue;
        }
        if(previous.active && previous.gainPermille!=gain) {
            RuntimeWorldAudioAction action; action.kind=RuntimeWorldAudioActionKind::SetGain;
            action.handleId=runtimeSuperWeaponBuildingSoundHandle(object.instanceId,1);
            action.eventTick=session.tick; action.gainPermille=gain;
            publishAction(action); changed=true;
        }
        previous.gainPermille=gain;
        if(previous.active && nowMilliseconds<previous.nextSampleMilliseconds) continue;
        const auto controls=toLowerAsciiLocal(soundIni.getValue(event,"Control",""));
        const bool attack=controls.find("attack")!=std::string::npos && spec->samples.size()>2;
        const bool decay=controls.find("decay")!=std::string::npos && spec->samples.size()>2;
        const bool firstAttack=attack && transition && audio.primed;
        const std::size_t first=attack?1:0,last=spec->samples.size()-(decay?1:0);
        const std::size_t index=firstAttack?0:first+runtimeAudioVariationSeed(event,object.instanceId,previous.ordinal)%(last-first);
        previous.sample=spec->samples[index];
        const int duration=play(object,*spec,previous.sample,1,previous.ordinal++,gain);
        previous.nextSampleMilliseconds=nowMilliseconds+std::uint64_t(duration);
        previous.active=true;
    }
    for(auto it=audio.sources.begin();it!=audio.sources.end();) {
        if(live.contains(it->first)){++it;continue;}
        for(int channel=0;channel<3;++channel)stop(it->first,channel);
        it=audio.sources.erase(it);
    }
    audio.primed=true;
    return changed;
}

bool advanceRuntimeSuperWeaponBuildingAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds) {
    if(!state.rulesLoaded || !state.artLoaded || !state.worldAudioCatalogLoaded) return false;
    return advanceRuntimeSuperWeaponBuildingPlayback(
        state, state.superWeaponBuildingAudio, state.worldAudioCatalog, state.soundIni, nowMilliseconds,
        [&](const RuntimeWorldAudioSoundSpec& sound, const RuntimeSpatialAudioPosition& position) {
            return runtimeSpatialAudioGainPermille(state, sound, position);
        },
        [&](const std::string& sample, int frequency) {
            const auto path = findRuntimeSoundSamplePath(state, sample);
            return path ? std::max(1, runtimeWavDurationTicks(*path, 1000) * 1000 / frequency) : 1000;
        },
        [&](const RuntimeWorldAudioAction& action) {
            applyRuntimeWorldAudioBackendAction(state, action);
        }
    );
}
