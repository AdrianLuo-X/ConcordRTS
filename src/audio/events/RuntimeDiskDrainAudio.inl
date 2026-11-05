std::uint64_t runtimeDiskDrainSoundHandle(int objectId, int channel = 0) {
    return 0x4000000000000000ULL | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
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


