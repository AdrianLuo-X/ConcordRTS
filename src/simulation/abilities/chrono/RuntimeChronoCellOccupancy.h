#pragma once
#include "world/values/RuntimeCellPoint.h"
#include <unordered_map>
#include <vector>
struct WorldObjectInstance;
namespace concord::simulation::abilities {
class RuntimeChronoOccupancyQueries {
public:
    virtual ~RuntimeChronoOccupancyQueries() = default;
    virtual const WorldObjectInstance* findObject(int objectId) const = 0;
    virtual bool usesGroundSubcells(const WorldObjectInstance& object) const = 0;
    virtual std::vector<int> availableSubcells(const WorldObjectInstance& object, const RuntimeCellPoint& destination) const = 0;
    virtual int cellKey(int x, int y) const = 0;
};
bool runtimeChronoCellIsOccupied(const std::unordered_map<int, int>& occupied,
    int x, int y, int objectId, const RuntimeChronoOccupancyQueries& queries);
}
