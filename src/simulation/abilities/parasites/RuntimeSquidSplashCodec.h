#pragma once
#include "simulation/effects/RuntimeImpactEffectValues.h"
#include <iosfwd>
#include <string>
#include <vector>
class IniFile;
namespace concord::simulation::abilities {
using concord::runtime::session_detail::RuntimeImpactEffect;
// The exact existing complete impact-record codec and frame query only.
// No host state, partial record representation or alternate serializer is exposed.
class RuntimeSquidSplashCodecQueries {
public:
    virtual ~RuntimeSquidSplashCodecQueries() = default;
    virtual std::string runtimeMultiplayerSnapshotImpactRecord(const RuntimeImpactEffect&) const = 0;
    virtual bool parseRuntimeMultiplayerSnapshotImpactRecord(const std::string&, RuntimeImpactEffect&) const = 0;
    virtual int runtimeImpactAnimationFrameForStep(const RuntimeImpactEffect&, int step) const = 0;
};
void writeRuntimeSquidSplashes(const std::vector<RuntimeImpactEffect>& impactEffects,
    std::ostream& output, const RuntimeSquidSplashCodecQueries& codecs);
bool loadRuntimeSquidSplashes(std::vector<RuntimeImpactEffect>& impactEffects,
    const IniFile& ini, std::string& error, const RuntimeSquidSplashCodecQueries& codecs);
}
