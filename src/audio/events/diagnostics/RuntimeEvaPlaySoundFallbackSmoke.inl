bool runRuntimeEvaPlaySoundFallbackSmoke(const RuntimeConfig& config,std::string& error) {
    int checks=0;
    const auto require=[&](bool good,const char* reason){++checks;if(!good){error=std::string("EVA PlaySound fallback: ")+reason;return false;}return true;};
    std::size_t count=0;
    if(_wgetenv_s(&count,nullptr,0,L"CONCORD_EVA_PLAYSOUND_NATIVE_WAVE")!=0||count<2){error="Explicit original qualified native WAV input required";return false;}
    std::vector<wchar_t> text(count);
    if(_wgetenv_s(&count,text.data(),text.size(),L"CONCORD_EVA_PLAYSOUND_NATIVE_WAVE")!=0){error="Native WAV input read failed";return false;}
    const std::filesystem::path path(text.data());
    if(!require(path.is_absolute()&&std::filesystem::is_regular_file(path),"real absolute PCM input"))return false;
    const std::string nativePath=runtimeConfigLegacyPathString(path);
    auto box=std::make_unique<RuntimeWindowState>();auto& state=*box;
    state.config=config;state.config.fixedTickHz=20;state.tick=17;
    state.runtimeSoundsEnabled=true;state.runtimeVoiceVolume=state.runtimeSoundVolume=100;
    state.runtimePreferencePersistenceEnabled=false;
    struct Cleanup {RuntimeWindowState& state;~Cleanup(){stopRuntimeEvaPlayback(state);}} cleanup{state};
    RuntimeSoundPlaybackRecord record;record.eva=true;record.path=nativePath;
    record.section="EVA_PrivateFallbackProbe";record.sample="qualified-nonempty-PCM";
    record.priority=7;record.subtitleText="Original fallback subtitle";
    // Same original provider and file as the pre-edit real-native qualification.
    // MCI genuinely rejects this filename; no injected backend or fake clock.
    if(!require(!playRuntimeEvaOnDedicatedChannel(nativePath,100),"real MCI long-filename failure required"))return false;
    playRuntimeResolvedSound(state,record);
    if(!require(state.runtimeSoundPlaybackHistory.size()==1,"actual production history one record"))return false;
    const auto played=state.runtimeSoundPlaybackHistory.back();
    if(!require(played.played&&played.status=="played","actual production PlaySound result recorded"))return false;
    if(!require(!state.runtimeEvaMciChannelActive&&state.runtimeEvaUsingPlaySoundFallback,"actual fallback flags"))return false;
    if(!require(state.runtimeEvaActivePriority==7&&state.runtimeEvaBusyUntilTick>state.tick,"original priority and duration"))return false;
    const auto subtitle=state.runtimeEvaSubtitleText;const auto until=state.runtimeEvaSubtitleUntilTick;
    if(!require(subtitle==record.subtitleText&&until>state.tick,"original subtitle state"))return false;
    muteRuntimeEvaPlaybackPreservingSubtitle(state);
    if(!require(!state.runtimeEvaMciChannelActive&&!state.runtimeEvaUsingPlaySoundFallback,"mute clears actual playback flags"))return false;
    if(!require(state.runtimeEvaSubtitleText==subtitle&&state.runtimeEvaSubtitleUntilTick==until&&state.runtimeEvaBusyUntilTick>=until,"mute retains original subtitle lifetime"))return false;
    playRuntimeResolvedSound(state,record);
    if(!require(state.runtimeEvaUsingPlaySoundFallback&&state.runtimeSoundPlaybackHistory.back().played,"real fallback reentry"))return false;
    stopRuntimeEvaPlayback(state);
    if(!require(!state.runtimeEvaUsingPlaySoundFallback&&state.runtimeEvaBusyUntilTick==0&&state.runtimeEvaActivePriority==-1&&state.runtimeEvaSubtitleText.empty(),"original full stop state"))return false;
    record.path=nativePath+".missing";playRuntimeResolvedSound(state,record);
    if(!require(state.runtimeSoundPlaybackHistory.back().played&&state.runtimeSoundPlaybackHistory.back().status=="played"&&state.runtimeEvaUsingPlaySoundFallback,"original missing-file asynchronous acceptance result"))return false;
    if(!require(state.runtimeEvaSubtitleText==record.subtitleText&&state.runtimeEvaSubtitleUntilTick>state.tick,"missing request retains original subtitle policy"))return false;
    stopRuntimeEvaPlayback(state);
    {
        auto rejected=concord::audio::createAsyncSoundDeviceWin32ForDiagnostics(+[](const wchar_t*,std::uint32_t){return false;});
        struct Override {
            concord::audio::RuntimeAsyncSoundDevice* previous=runtimeAsyncSoundOverrideForDiagnostics;
            explicit Override(concord::audio::RuntimeAsyncSoundDevice* device){runtimeAsyncSoundOverrideForDiagnostics=device;}
            ~Override(){runtimeAsyncSoundOverrideForDiagnostics=previous;}
        } override(rejected.get());
        record.path=nativePath;playRuntimeResolvedSound(state,record);
        if(!require(!state.runtimeSoundPlaybackHistory.back().played&&state.runtimeSoundPlaybackHistory.back().status=="play-failed"&&!state.runtimeEvaUsingPlaySoundFallback,"specified injected false API result preserves failure record"))return false;
        if(!require(state.runtimeEvaSubtitleText==record.subtitleText&&state.runtimeEvaBusyUntilTick>=state.runtimeEvaSubtitleUntilTick,"injected failure retains original readable subtitle lifetime"))return false;
    }
    state.runtimeVoiceVolume=state.runtimeSoundVolume=0;record.path=nativePath;playRuntimeResolvedSound(state,record);
    if(!require(state.runtimeSoundPlaybackHistory.back().status=="eva-subtitle-only-muted"&&!state.runtimeEvaUsingPlaySoundFallback,"original muted policy unchanged"))return false;
    if(!require(state.tick==17,"presentation call does not advance authority tick"))return false;
    std::cout<<"Runtime EVA PlaySound fallback: checks="<<checks<<" failures=0 skipped=0 realMciFailure=1 realFallback=1 stop=1 muteSubtitle=1 missingAsyncAccepted=1 injectedFalseResult=1 muted=1 tickUnchanged=1 physicalOutput=not-captured"<<std::endl;
    return true;
}
