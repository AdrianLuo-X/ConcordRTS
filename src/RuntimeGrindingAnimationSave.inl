void writeRuntimeGrindingAnimations(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<const RuntimeImpactEffect*> cycles;
    for (const auto& cycle:state.impactEffects)
        if (cycle.remainingTicks>0 && (cycle.lifecycleState=="GrindingAnimation"
            || cycle.lifecycleState=="GrindingAnimationQueued")) cycles.push_back(&cycle);
    if (cycles.empty()) return;
    output << "\n[GrindingAnimations]\nSchema=1\nCount=" << cycles.size() << '\n';
    for (std::size_t i=0;i<cycles.size();++i)
        output << i << '=' << runtimeMultiplayerSnapshotImpactRecord(*cycles[i])
            << '|' << iniHexEncode(cycles[i]->animId) << '\n';
}

bool restoreRuntimeGrindingAnimations(RuntimeWindowState& state, const IniFile& ini,
    std::string& error) {
    if (!ini.getSection("GrindingAnimations")) return true;
    const auto fail=[&](){error="invalid grinder animation queue";return false;};
    int schema=0,count=0;
    if (!parseRuntimeInt(ini.getValue("GrindingAnimations","Schema",""),schema) || schema!=1
        || !parseRuntimeInt(ini.getValue("GrindingAnimations","Count",""),count)
        || count<0 || count>100000) return fail();
    std::vector<RuntimeImpactEffect> restored;
    restored.reserve(static_cast<std::size_t>(count));
    std::unordered_set<int> activeHosts;
    for (int i=0;i<count;++i) {
        const std::string record=ini.getValue("GrindingAnimations",std::to_string(i),"");
        const auto separator=record.rfind('|');
        RuntimeImpactEffect cycle;
        if (separator==std::string::npos
            || !parseRuntimeMultiplayerSnapshotImpactRecord(record.substr(0,separator),cycle)
            || !iniHexDecode(record.substr(separator+1),cycle.animId,256U)) return fail();
        const bool queued=cycle.lifecycleState=="GrindingAnimationQueued";
        if ((!queued && cycle.lifecycleState!="GrindingAnimation")
            || cycle.targetObjectId<0 || cycle.attackerObjectId!=cycle.targetObjectId
            || cycle.remainingTicks<=0 || cycle.damage!=0 || cycle.death
            || cycle.animationSequenceSteps<=0 || cycle.animationStartFrame<0
            || cycle.animationEndFrame<=cycle.animationStartFrame
            || cycle.animationEndFrame>cycle.animationFrameCount
            || cycle.animationLoopEndFrame!=cycle.animationEndFrame
            || cycle.animationLoopCount!=1
            || cycle.animationSequenceSteps!=cycle.animationEndFrame-cycle.animationStartFrame
            || static_cast<long long>(cycle.animationSequenceSteps)*cycle.animationFrameDelayTicks
                !=cycle.totalTicks
            || (queued && (cycle.remainingTicks!=cycle.totalTicks
                || !activeHosts.contains(cycle.targetObjectId)))
            || (!queued && !activeHosts.insert(cycle.targetObjectId).second)) return fail();
        const auto* host=findObjectById(state,cycle.targetObjectId);
        const auto* building=host ? findRuntimeBuildingType(state.rules,*host) : nullptr;
        const auto* art=host ? runtimeArtTypeForObject(state,*host) : nullptr;
        bool matchingImage=false;
        if (art) for (const auto& layer:art->specialAnimations) {
            if (layer.key=="SpecialAnim" && (cycle.animId==layer.image
                || (!layer.damagedImage.empty() && cycle.animId==layer.damagedImage))) {
                matchingImage=true;
                break;
            }
        }
        if (!host || host->kind!=WorldObjectKind::Structure || !building || !building->grinding
            || !matchingImage || !state.art.findArtType(cycle.animId)) return fail();
        cycle.renderIntent=queued ? "grinderCycleQueued" : "grinderCycle";
        cycle.frameIndex=runtimeImpactAnimationFrameForStep(cycle,
            (cycle.totalTicks-cycle.remainingTicks)/cycle.animationFrameDelayTicks);
        restored.push_back(std::move(cycle));
    }
    state.impactEffects=std::move(restored);
    return true;
}
