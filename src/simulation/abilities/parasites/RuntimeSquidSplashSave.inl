int runtimeImpactAnimationFrameForStep(const RuntimeImpactEffect& impact, int step);

void writeRuntimeSquidSplashes(const RuntimeWindowState& state, std::ostream& output) {
    const auto count=std::count_if(state.impactEffects.begin(),state.impactEffects.end(),
        [](const auto& impact){return impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0;});
    output<<"\n[SquidSplashAnimations]\nSchema=1\nCount="<<count<<'\n';
    int index=0;
    for (const auto& impact:state.impactEffects) {
        if (impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0)
            output<<index++<<'='<<runtimeMultiplayerSnapshotImpactRecord(impact)<<'\n';
    }
}

bool loadRuntimeSquidSplashes(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    const auto schema=ini.getValue("SquidSplashAnimations","Schema","");
    if (schema.empty()) return true;
    int count=0;
    if (schema!="1" || !parseRuntimeInt(ini.getValue("SquidSplashAnimations","Count",""),count)
        || count<0 || count>65536) { error="invalid SquidSplashAnimations section"; return false; }
    std::vector<RuntimeImpactEffect> restored;
    for (int index=0;index<count;++index) {
        RuntimeImpactEffect impact;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("SquidSplashAnimations",std::to_string(index),""),impact)
            || impact.lifecycleState!="SquidSplash" || impact.remainingTicks<=0
            || impact.death || impact.damage!=0 || !impact.warheadId.empty()
            || !impact.hasWorldPosition || !std::isfinite(impact.worldX) || !std::isfinite(impact.worldY)
            || !std::isfinite(impact.worldLevel) || impact.warheadImpactIntentId.empty()
            || impact.animationSequenceSteps<=0 || impact.animationFrameCount<=0) {
            error="invalid squid splash animation"; return false;
        }
        impact.animId=impact.warheadImpactIntentId;
        impact.renderIntent="squidSplash";
        impact.frameIndex=runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks-impact.remainingTicks)/impact.animationFrameDelayTicks);
        restored.push_back(std::move(impact));
    }
    for (auto& impact:restored) state.impactEffects.push_back(std::move(impact));
    return true;
}
