#pragma once
#include <string>
#include <vector>
struct WorldObjectInstance;
struct SuperWeaponObjectLifecycleEvent;
namespace concord::simulation::psionic {
class RuntimeDominatorControlTargets {
public:
    virtual ~RuntimeDominatorControlTargets()=default;
    virtual WorldObjectInstance* findTarget(int objectId)=0;
};
class RuntimeDominatorControlLinks {
public:
    virtual ~RuntimeDominatorControlLinks()=default;
    virtual void establishPermanentControl(int objectId,const std::string& newOwner,
        const std::string& originalOwner,int tick)=0;
};
// Called after the original owner transaction succeeds. Borrow events and
// target storage synchronously; keep the original lookup/guard/link/mission
// order and partial effects on exceptions, with no rollback or retry.
void completeRuntimeDominatorControl(
    const std::vector<SuperWeaponObjectLifecycleEvent>& events,
    RuntimeDominatorControlTargets& targets,RuntimeDominatorControlLinks& links);
}
