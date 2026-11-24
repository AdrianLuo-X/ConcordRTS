#include "RuntimeCamouflageStationary.h"
#include "world/model/GameWorld.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/movement/RuntimeDeploymentMissions.h"
namespace concord::simulation::abilities {
using runtime_save_text::toLowerAsciiLocal;
bool runtimeCamouflageIsStationary(int objectId, const RuntimeCamouflageStationaryQueries& queries) {
        const WorldObjectInstance* object = queries.findObject(objectId);
        if (object == nullptr || !object->alive
            || object->dockedToObjectId >= 0
            || object->hasRenderPosition
            || object->deployed
            || movement::runtimeMissionIsDeploying(object->mission)
            || movement::runtimeMissionIsUndeploying(object->mission)) {
            return false;
        }
        const RuntimeObjectBehavior* behavior = queries.findBehavior(objectId);
        if (behavior != nullptr) {
            const auto& action = *behavior;
            // Shooting from rest must retain logical camouflage. The weapon's
            // fake-blink timer reveals its body; pathing/chasing still unmasks it.
            return action.kind == RuntimeBehaviorKind::Attack
                && action.path.empty() && !action.pathRequestPending
                && action.renderMoveTicksRemaining <= 0
                && action.state != RuntimeBehaviorState::Moving
                && action.state != RuntimeBehaviorState::Chasing
                && action.state != RuntimeBehaviorState::Blocked;
        }
        const std::string mission = toLowerAsciiLocal(object->mission);
        return mission == "guard"
            || mission == "idle"
            || mission == "idle1"
            || mission == "idle2"
            || mission == "deployedidle";
    }
}
