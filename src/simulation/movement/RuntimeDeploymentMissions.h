#pragma once
#include "core/io/RuntimeSaveTextFields.h"
namespace concord::simulation::movement {
using runtime_save_text::toLowerAsciiLocal;
inline bool runtimeMissionIsMcvDeployPending(const std::string& missionText) {
    const auto mission = toLowerAsciiLocal(missionText);
    return mission == "mcvdeployturning" || mission == "mcvdeployturningunselected"
        || mission == "mcvdeployready" || mission == "mcvdeployreadyunselected";
}
inline bool runtimeMissionIsDeploying(const std::string& missionText) {
    return toLowerAsciiLocal(missionText) == "deploying" || runtimeMissionIsMcvDeployPending(missionText);
}
inline bool runtimeMissionIsUndeploying(const std::string& missionText) {
    return toLowerAsciiLocal(missionText) == "undeploying";
}
}
