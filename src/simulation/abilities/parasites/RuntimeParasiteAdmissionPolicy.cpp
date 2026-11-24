#include "RuntimeParasiteAdmissionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteStunState.h"
#include "world/model/GameWorld.h"
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
namespace concord::simulation::abilities {
using runtime_save_text::trimAsciiLocal;
bool runtimeParasiteObjectIsContained(
    const RuntimeParasiteAdmissionQueries& queries,
    const WorldObjectInstance& object) {
    if (object.garrisonHostObjectId >= 0
        || !object.garrisonOccupantIds.empty()
        || !object.transportPassengerIds.empty()) {
        return true;
    }
    for (const WorldObjectInstance& candidate : queries.objects()) {
        if (std::find(
                candidate.transportPassengerIds.begin(),
                candidate.transportPassengerIds.end(),
                object.instanceId
            ) != candidate.transportPassengerIds.end()) {
            return true;
        }
    }
    return false;
}

bool runtimeParasiteObjectHasExclusiveAuthority(
    const RuntimeParasiteAdmissionQueries& queries,
    const WorldObjectInstance& object,
    bool asHost) {
    if (queries.stunState().isActive(object.instanceId, queries.tick())
        || runtimeParasiteObjectIsContained(queries, object)
        || object.dockedToObjectId >= 0 || object.dockIndex >= 0
        || queries.hasPendingEngineerCapture(object.instanceId)
        || queries.hasPendingOccupyTarget(object.instanceId)
        || queries.hasPendingTransportTarget(object.instanceId)
        || queries.hasPendingTimedBombIntent(object.instanceId)) {
        return true;
    }
    if (queries.repairDepotsInitialized()) {
        for (const RuntimeRepairDepotRelation& relation :
                queries.repairRelations()) {
            if (relation.unitId == object.instanceId
                || relation.hostId == object.instanceId) {
                return true;
            }
        }
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            queries.factoryLeases()) {
        if (lease.producedObjectId == object.instanceId
            || lease.hostId == object.instanceId) {
            return true;
        }
    }
    if (queries.specialMechanismsInitialized()
        && (queries.isCarrierChild(object.instanceId)
            // The host stays in the world, so its aircraft can remain attached.
            // A parasite source would enter limbo and cannot own such children.
            || (!asHost && queries.isCarrierParent(object.instanceId)))) {
        return true;
    }
    return false;
}

bool runtimeParasiteWarheadIsExclusive(
    const WeaponType& weapon,
    const WarheadType& warhead) {
    return weapon.limboLaunch && warhead.parasite
        && std::max(1, weapon.burst) == 1
        && !weapon.spawner
        && !weapon.sonic
        && !weapon.radBeam
        && !weapon.electricBolt
        && !weapon.magBeam
        && !warhead.psychedelic
        && !warhead.psychicDamage
        && !warhead.ivanBomb
        && !warhead.bombDisarm
        && !warhead.airstrike
        && !warhead.isLocomotor
        && !warhead.mindControl
        && !warhead.temporal
        && !warhead.radiation
        && !warhead.electricAssault
        && trimAsciiLocal(warhead.particle).empty()
        && trimAsciiLocal(warhead.particleSystem).empty();
}

}
