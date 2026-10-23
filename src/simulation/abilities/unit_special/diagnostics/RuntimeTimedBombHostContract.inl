// Additional S21 fixture; the original host probe and all its assertions remain.
bool runRuntimeTimedBombContinuationContract(
    RuntimeWindowState& state, std::string& error
) {
    const char* configured=std::getenv("CONCORD_TIMED_BOMB_CONTRACT_OUTPUT");
    if(configured==nullptr || *configured=='\0') return true;
    const std::filesystem::path output=configured;
    std::filesystem::create_directories(output);
    const int begin=state.tick;
    int end=begin;
    for(const auto& [id,bomb]:state.specialMechanisms.timedBombs()) {
        (void)id;end=std::max(end,bomb.detonateTick+2);
    }
    if(end<=begin || end-begin>1000) {error="TimedBomb continuation fixture deadline";return false;}
    const int checkpoint=begin+(end-begin)/2;
    const auto seed=output/"seed.ini";
    if(!saveRuntimeState(state,pathString(seed),error)) return false;
    std::vector<std::string> expected;
    const auto capture=[&](const std::string& route) {
        std::vector<std::pair<std::string,std::uint64_t>> domains;
        const auto full=runtimeStandaloneSimulationStateHash(state,error,&domains);
        if(full==0 || !error.empty()) return std::string{};
        std::ostringstream authority;
        authority<<"tick="<<state.tick<<"\nfull-authority="<<full<<'\n';
        for(const auto& [name,value]:domains) authority<<"domain="<<name<<'|'<<value<<'\n';
        authority<<"world="<<state.world.deterministicSimulationStateHash()<<'\n';
        for(const auto& o:state.world.getObjects())
            authority<<"object="<<o.instanceId<<'|'<<o.alive<<'|'<<o.health<<'|'<<o.owner<<'|'<<o.x<<'|'<<o.y<<'\n';
        state.specialMechanisms.save(authority);
        RuntimeSnapshotActiveStateScope active(state);
        std::ostringstream ordinary,extension;
        if(!writeRuntimeStateToStream(state,ordinary,error)
            || !writeRuntimeMultiplayerSnapshotExtension(state,extension,error)) return std::string{};
        const std::string stem=route+"-"+std::to_string(state.tick);
        std::ofstream a(output/(stem+".authority.txt"),std::ios::binary);
        a<<authority.str();a.close();
        std::ofstream all(output/(stem+".complete.txt"),std::ios::binary);
        all<<authority.str()<<"\nORDINARY\n"<<ordinary.str()<<"\nEXTENSION\n"<<extension.str();all.close();
        if(!a || !all) {error="TimedBomb continuation evidence write";return std::string{};}
        return authority.str();
    };
    // Both runs read the same original-format seed. The second also restores
    // an active timer halfway through its continuation.
    for(int route=0;route<2;++route) {
        if(!loadRuntimeState(state,pathString(seed),error)) return false;
        for(int tick=begin;tick<=end;++tick) {
            state.tick=tick;
            if(tick>begin) {
                (void)advanceRuntimeEffects(state);
                (void)syncRuntimeLightingSuperWeaponProfiles(state);
                (void)advanceRuntimeLighting(state);
                (void)advanceRuntimeParticles(state);
                (void)completeRuntimeWorldRefresh(state);
            }
            if(route==1 && tick==checkpoint) {
                const auto mid=output/"checkpoint.ini";
                if(!saveRuntimeState(state,pathString(mid),error)
                    || !loadRuntimeState(state,pathString(mid),error)) return false;
            }
            const auto text=capture(route==0?"continuous":"restored");
            if(text.empty()) return false;
            if(route==0) expected.push_back(text);
            else if(text!=expected.at(static_cast<std::size_t>(tick-begin))) {
                error="TimedBomb complete host authority differs after restore at tick "+std::to_string(tick);
                return false;
            }
        }
    }
    if(!loadRuntimeState(state,pathString(seed),error)) return false;
    std::cout<<"TimedBomb host continuation: "<<(end-begin+1)
             <<" complete authority pairs,active checkpoint restore,ordinary+extension bytes retained\n";
    return true;
}
