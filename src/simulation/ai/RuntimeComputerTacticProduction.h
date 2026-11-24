#pragma once
#include "simulation/production/RuntimeProductionSpawnValues.h"
#include "simulation/production/RuntimeBuildCatalogItem.h"
#include "simulation/ai/RuntimeComputerAiState.h"
#include "simulation/ai/RuntimeComputerTactics.h"
#include <map>
#include <optional>
#include <string>
struct WorldObjectInstance;
struct BuildingType;
namespace concord::simulation::ai {
using RuntimeProductionSpawn=concord::runtime::session_detail::RuntimeProductionSpawn;
using RuntimeAircraftProductionMatchInputBuild=concord::runtime::session_detail::RuntimeAircraftProductionMatchInputBuild;
// All views are synchronous to original owners; no Root/World/Rules snapshots.
class RuntimeComputerTacticAircraftQueries {
public:
    virtual ~RuntimeComputerTacticAircraftQueries()=default;
    virtual RuntimeAircraftProductionMatchInputBuild buildAircraftInput(const std::string&,std::optional<RuntimeAircraftProductionOrder>) const=0;
    virtual RuntimeAircraftProductionMatchResult matchAircraftDocks(const RuntimeAircraftProductionMatchInput&) const=0;
    virtual const WorldObjectInstance* object(int) const=0;
    virtual const BuildingType* buildingType(const std::string&) const=0;
    virtual RuntimeProductionSpawn dockSpawn(const WorldObjectInstance&,int) const=0;
};
class RuntimeComputerTacticQueueQueries : public RuntimeComputerTacticAircraftQueries {
public:
    virtual bool itemAllowed(const RuntimeComputerAiState&,const RuntimeBuildCatalogItem&) const=0;
};
class RuntimeComputerTacticProductionQueries : public RuntimeComputerTacticQueueQueries {
public:
    virtual std::optional<RuntimeBuildCatalogItem> buildItem(const std::string&,const std::string&) const=0;
    virtual const WorldObjectInstance* factoryAnchor(const RuntimeComputerAiState&,const RuntimeBuildCatalogItem&) const=0;
};
class RuntimeComputerTacticQueueEffects {
public:
    virtual ~RuntimeComputerTacticQueueEffects()=default;
    virtual void spendStoredOre(const std::string&,int)=0;
};
class RuntimeComputerTacticCompletionEffects {
public:
    virtual ~RuntimeComputerTacticCompletionEffects()=default;
    virtual bool buildUnit(RuntimeComputerAiState&,const RuntimeBuildCatalogItem&,int*)=0;
    virtual void refreshDocks()=0;
    virtual WorldObjectInstance* addObject(const std::string&,RuntimeBuildKind,const std::string&,const RuntimeCellPoint&)=0;
    virtual void applyVeterancy(WorldObjectInstance&)=0;
    virtual void scaleAiHealth(const RuntimeComputerAiState&,WorldObjectInstance&)=0;
    virtual void applyDockPose(WorldObjectInstance&,const WorldObjectInstance&,int)=0;
    virtual void recordCompletedBuild(const std::string&,const RuntimeBuildCatalogItem&)=0;
    virtual void refreshOccupancy()=0;
};
int runtimeComputerTacticLane(const RuntimeBuildCatalogItem&);
std::optional<RuntimeProductionSpawn> runtimeComputerTacticAircraftSpawn(const RuntimeComputerTacticAircraftQueries&,const std::string&,const RuntimeBuildCatalogItem&);
bool completeRuntimeComputerTacticUnit(const RuntimeComputerTacticAircraftQueries&,RuntimeComputerTacticCompletionEffects&,RuntimeComputerAiState&,const RuntimeBuildCatalogItem&,int&);
// Subscript insertion is retained before validation, including rejected queues.
bool queueRuntimeComputerTacticProduction(std::map<std::string,RuntimeComputerTacticsState>&,const RuntimeComputerTacticQueueQueries&,RuntimeComputerTacticQueueEffects&,RuntimeComputerAiState&,const RuntimeBuildCatalogItem&,int teamInstanceId=-1);
// Borrow the original tick field; completion effects may change it before use.
bool advanceRuntimeComputerTacticProduction(const int&,const RuntimeComputerTacticProductionQueries&,RuntimeComputerTacticCompletionEffects&,RuntimeComputerAiState&,RuntimeComputerTacticsState&);
}
