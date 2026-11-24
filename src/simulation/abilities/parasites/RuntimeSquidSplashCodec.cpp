#include "RuntimeSquidSplashCodec.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <cmath>
#include <ostream>
#include <utility>
namespace concord::simulation::abilities {
using runtime_save_text::parseRuntimeInt;
void writeRuntimeSquidSplashes(const std::vector<RuntimeImpactEffect>& impactEffects, std::ostream& output, const RuntimeSquidSplashCodecQueries& codecs) {
    const auto count=std::count_if(impactEffects.begin(),impactEffects.end(),
        [](const auto& impact){return impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0;});
    output<<"\n[SquidSplashAnimations]\nSchema=1\nCount="<<count<<'\n';
    int index=0;
    for (const auto& impact:impactEffects) {
        if (impact.lifecycleState=="SquidSplash" && impact.remainingTicks>0)
            output<<index++<<'='<<codecs.runtimeMultiplayerSnapshotImpactRecord(impact)<<'\n';
    }
}

bool loadRuntimeSquidSplashes(std::vector<RuntimeImpactEffect>& impactEffects, const IniFile& ini, std::string& error, const RuntimeSquidSplashCodecQueries& codecs) {
    const auto schema=ini.getValue("SquidSplashAnimations","Schema","");
    if (schema.empty()) return true;
    int count=0;
    if (schema!="1" || !parseRuntimeInt(ini.getValue("SquidSplashAnimations","Count",""),count)
        || count<0 || count>65536) { error="invalid SquidSplashAnimations section"; return false; }
    std::vector<RuntimeImpactEffect> restored;
    for (int index=0;index<count;++index) {
        RuntimeImpactEffect impact;
        if (!codecs.parseRuntimeMultiplayerSnapshotImpactRecord(
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
        impact.frameIndex=codecs.runtimeImpactAnimationFrameForStep(impact,
            (impact.totalTicks-impact.remainingTicks)/impact.animationFrameDelayTicks);
        restored.push_back(std::move(impact));
    }
    for (auto& impact:restored) impactEffects.push_back(std::move(impact));
    return true;
}

}
