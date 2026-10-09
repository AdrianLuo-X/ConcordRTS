bool runRuntimeFactionResultVoiceSmoke(const RuntimeConfig& config,std::string& error){
    const auto fail=[&](const std::string& why){error="Faction result voice: "+why; return false;};
    auto p=std::make_unique<RuntimeWindowState>(); auto& s=*p; s.config=config;
    s.runtimePreferencePersistenceEnabled=false; s.runtimeSoundsEnabled=false;
    s.runtimeVoiceVolume=s.runtimeSoundVolume=0; s.runtimeMusicEnabled=false;
    s.report=buildRuntimeStartupReport(config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
    if(!s.report.success || !loadRuntimeSoundDatabase(s) || !s.evaLoaded) return fail("original EVA resource missing");
    s.tick=300; s.skirmishMatchFinished=true;
    int cases=0;
    const std::vector<std::pair<std::string,std::string>> factions{
        {"Americans","ceva"},{"British","ceva"},{"French","ceva"},{"Germans","ceva"},{"Alliance","ceva"},
        {"Russians","csof"},{"Confederation","csof"},{"Africans","csof"},{"Arabs","csof"},{"YuriCountry","cyur"}};
    for(const auto& [country,prefix]:factions) for(bool victory:{false,true}) for(bool mapped:{false,true}){
        s.campaignMode=false; s.economy.owner=mapped ? "Multi1" : country;
        s.skirmishCountryByOwner.clear();
        if(mapped) s.skirmishCountryByOwner["multi1"]=country;
        s.skirmishVictory=victory; s.resultPresentation={};
        s.runtimeSoundPlaybackHistory.clear(); s.runtimeSoundIntentHistory.clear();
        s.runtimeEvaBusyUntilTick=100000; // End-of-game presentation must clear stale combat audio.
        const int before=s.runtimeSoundEvents;
        advanceRuntimeResultPresentation(s,1000);
        const auto expected=prefix+(victory ? "022" : "023");
        if(s.runtimeSoundPlaybackHistory.size()!=1) return fail(country+" did not resolve one result announcement");
        const auto& sound=s.runtimeSoundPlaybackHistory.back();
        if(sound.sample!=expected || !sound.playable || !sound.eva
            || sound.section!=(victory ? "EVA_YouAreVictorious" : "EVA_YouHaveLost")
            || runtimeWavDurationTicks(sound.path,30)<15)
            return fail(country+" resolved "+sound.sample+" instead of "+expected+" status="+sound.status);
        advanceRuntimeResultPresentation(s,1001);
        if(s.runtimeSoundEvents!=before+1 || s.runtimeSoundPlaybackHistory.size()!=1)
            return fail("announcement repeated while the game was paused");
        if(!mapped) std::cout<<"Result voice "<<country<<" victory="<<victory<<" sample="<<sound.sample<<" native-audio/once=ok\n";
        ++cases;
    }
    std::cout<<"Faction result voices passed: "<<cases<<" stock country/outcome/direct-and-mapped-owner cases\n";
    return true;
}
