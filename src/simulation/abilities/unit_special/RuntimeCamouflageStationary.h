#pragma once
struct WorldObjectInstance;
struct RuntimeObjectBehavior;
namespace concord::simulation::abilities {
class RuntimeCamouflageStationaryQueries {
public:
    virtual ~RuntimeCamouflageStationaryQueries() = default;
    virtual const WorldObjectInstance* findObject(int objectId) const = 0;
    virtual const RuntimeObjectBehavior* findBehavior(int objectId) const = 0;
};
bool runtimeCamouflageIsStationary(int objectId, const RuntimeCamouflageStationaryQueries& queries);
}
