#include "RuntimeTemporalFeedbackPolicy.h"
#include "world/model/GameWorld.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/combat/targeting/RuntimeCombatDetails.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
namespace concord::simulation::abilities {
namespace {
class BeamWeaponScope {
public:
    explicit BeamWeaponScope(RuntimeTemporalBeamQueries& value) noexcept : queries(value) {}
    ~BeamWeaponScope() { queries.finishWeaponScope(); }
    BeamWeaponScope(const BeamWeaponScope&) = delete;
    BeamWeaponScope& operator=(const BeamWeaponScope&) = delete;
private:
    RuntimeTemporalBeamQueries& queries;
};
}
void resolveRuntimeTemporalBeamEndpoints(RuntimeTemporalBeamView snapshot,
    const std::unordered_map<int, RuntimeObjectBehavior>& behaviors,
    const std::map<int, RuntimeCombatFireCursor>& fireCursors,
    RuntimeTemporalBeamQueries& queries) {
    if (snapshot.kind != "temporalBeam") return;
    const auto* source = queries.sourceIncludingInactive(snapshot.objectId);
    const auto* target = queries.object(snapshot.targetObjectId);
    if (source == nullptr || target == nullptr) return;
    const WorldObjectInstance* host = nullptr;
    if (!source->alive) {
        const auto cursor = fireCursors.find(source->instanceId);
        if (cursor != fireCursors.end())
            host = queries.object(cursor->second.sourceAnchorObjectId);
        if (!host || !queries.validPassenger(*source, *host)) return;
    }
    const auto sourceOrder = behaviors.find(snapshot.objectId);
    const auto hostOrder = host == nullptr ? behaviors.end() : behaviors.find(host->instanceId);
    const bool profileForceFire = (sourceOrder != behaviors.end()
        && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
        && sourceOrder->second.targetObjectId == snapshot.targetObjectId
        && sourceOrder->second.forceFire)
        || (hostOrder != behaviors.end()
            && hostOrder->second.kind == RuntimeBehaviorKind::Attack
            && hostOrder->second.targetObjectId == snapshot.targetObjectId
            && hostOrder->second.forceFire);
    const auto profile = queries.selectWeapon(*source, target, host, profileForceFire);
    const BeamWeaponScope scope(queries);
    if (profile.weapon == nullptr || !profile.weapon->radBeam
        || profile.warhead == nullptr || !profile.warhead->temporal) return;
    const auto pose = queries.firingPose(*source, host);
    auto muzzle = queries.sourcePosition(pose, queries.firingFacing(pose, *target));
    queries.alignMuzzle(pose, queries.firingFacing(pose, *target), muzzle);
    const auto endpoint = queries.targetPosition(*target);
    snapshot.sourceX = muzzle.x;
    snapshot.sourceY = muzzle.y;
    snapshot.sourceLevel = muzzle.z;
    snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
    snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
    snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
    snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
    snapshot.targetX = endpoint.x;
    snapshot.targetY = endpoint.y;
    snapshot.targetLevel = endpoint.level;
    snapshot.color = queries.beamColor();
    if (snapshot.color.empty()) snapshot.color = "128,200,255";
    snapshot.owner = source->owner;
    snapshot.hasBeamEndpoints = true;
}
void spawnRuntimeTemporalEraseEffect(const SpecialMechanismEvent& event,
    RuntimeTemporalEraseQueries& queries, RuntimeTemporalEraseFeedback& feedback) {
    const auto* target = queries.object(event.targetObjectId);
    if (target == nullptr) return;
    feedback.beginEffect(event.objectId, *target);
    const auto offset = queries.subcellOffset(*target);
    feedback.worldX(queries.renderX(*target) + offset.x);
    feedback.worldY(queries.renderY(*target) + offset.y);
    feedback.worldLevel(queries.drawLevel(*target));
    feedback.facingAndLifecycle(target->facing, "TemporalErase", "temporalErase");
    feedback.animation(queries.warpAwayAnimation());
    if (feedback.animation().empty()
        || runtime_save_text::toLowerAsciiLocal(feedback.animation()) == "none") return;
    feedback.initializeTimeline();
    if (const auto* report = queries.reportSound(feedback.animation())) {
        if (!report->empty()) feedback.sound("sound:" + *report, feedback.spatialPosition(), true);
    }
    feedback.appendEffect();
}
}
