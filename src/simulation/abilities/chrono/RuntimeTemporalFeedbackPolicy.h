#pragma once
#include "simulation/projectiles/RuntimeProjectileValues.h"
#include <map>
#include <unordered_map>
#include <string>
struct WorldObjectInstance;
struct WorldObjectCellOffset;
struct RuntimeObjectBehavior;
struct RuntimeCombatFireCursor;
struct WeaponType;
struct WarheadType;
struct RuntimeSpatialAudioPosition;
struct SpecialMechanismEvent;
namespace concord::simulation::abilities {
// Each field borrows its existing presentation snapshot for this synchronous call.
struct RuntimeTemporalBeamView {
    const std::string& kind;
    const int& objectId;
    const int& targetObjectId;
    double& sourceX;
    double& sourceY;
    double& sourceLevel;
    int& sourceScreenOffsetX;
    int& sourceNativeFixedX;
    int& sourceScreenOffsetY;
    int& sourceNativeFixedY;
    double& targetX;
    double& targetY;
    double& targetLevel;
    std::string& color;
    std::string& owner;
    bool& hasBeamEndpoints;
};
struct RuntimeTemporalBeamMuzzle {
    double x = 0.0, y = 0.0, z = 0.0;
    int screenOffsetX = 0, screenOffsetY = 0;
    int nativeFixedX = 0, nativeFixedY = 0;
};
struct RuntimeTemporalBeamWeaponView {
    const WeaponType* weapon = nullptr;
    const WarheadType* warhead = nullptr;
};
class RuntimeTemporalBeamQueries {
public:
    virtual ~RuntimeTemporalBeamQueries() = default;
    virtual const WorldObjectInstance* sourceIncludingInactive(int id) const = 0;
    virtual const WorldObjectInstance* object(int id) const = 0;
    virtual bool validPassenger(const WorldObjectInstance& source, const WorldObjectInstance& host) const = 0;
    // The original complete owned profile remains in the adapter until finish.
    virtual RuntimeTemporalBeamWeaponView selectWeapon(const WorldObjectInstance& source,
        const WorldObjectInstance* target, const WorldObjectInstance* host, bool forceFire) = 0;
    virtual void finishWeaponScope() noexcept = 0;
    virtual WorldObjectInstance firingPose(const WorldObjectInstance& source, const WorldObjectInstance* host) const = 0;
    virtual int firingFacing(const WorldObjectInstance& pose, const WorldObjectInstance& target) const = 0;
    virtual RuntimeTemporalBeamMuzzle sourcePosition(const WorldObjectInstance& pose, int facing) const = 0;
    virtual void alignMuzzle(const WorldObjectInstance& pose, int facing, RuntimeTemporalBeamMuzzle& muzzle) const = 0;
    virtual concord::runtime::session_detail::RuntimeProjectileTargetPosition targetPosition(const WorldObjectInstance& target) const = 0;
    virtual std::string beamColor() const = 0;
};
void resolveRuntimeTemporalBeamEndpoints(RuntimeTemporalBeamView snapshot,
    const std::unordered_map<int, RuntimeObjectBehavior>& behaviors,
    const std::map<int, RuntimeCombatFireCursor>& fireCursors,
    RuntimeTemporalBeamQueries& queries);

class RuntimeTemporalEraseQueries {
public:
    virtual ~RuntimeTemporalEraseQueries() = default;
    virtual const WorldObjectInstance* object(int id) const = 0;
    virtual WorldObjectCellOffset subcellOffset(const WorldObjectInstance& object) const = 0;
    virtual double renderX(const WorldObjectInstance& object) const = 0;
    virtual double renderY(const WorldObjectInstance& object) const = 0;
    virtual double drawLevel(const WorldObjectInstance& object) const = 0;
    virtual const std::string& warpAwayAnimation() const = 0;
    virtual const std::string* reportSound(const std::string& animation) const = 0;
};
// The original private effect and its moved-from lifetime remain adapter-owned.
class RuntimeTemporalEraseFeedback {
public:
    virtual ~RuntimeTemporalEraseFeedback() = default;
    virtual void beginEffect(int attackerId, const WorldObjectInstance& target) = 0;
    virtual void worldX(double value) noexcept = 0;
    virtual void worldY(double value) noexcept = 0;
    virtual void worldLevel(double value) noexcept = 0;
    virtual void facingAndLifecycle(int facing, const char* lifecycle, const char* intent) = 0;
    virtual void animation(const std::string& id) = 0;
    virtual const std::string& animation() const noexcept = 0;
    virtual void initializeTimeline() = 0;
    virtual RuntimeSpatialAudioPosition spatialPosition() const = 0;
    virtual void sound(const std::string& intent, RuntimeSpatialAudioPosition position, bool force) = 0;
    virtual void appendEffect() = 0;
};
void spawnRuntimeTemporalEraseEffect(const SpecialMechanismEvent& event,
    RuntimeTemporalEraseQueries& queries, RuntimeTemporalEraseFeedback& feedback);
}
