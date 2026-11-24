#pragma once

#include <optional>
#include <string>

struct WorldObjectInstance;
struct WorldObjectCellOffset;
struct RuntimeCellPoint;
struct RuntimeSpatialAudioPosition;
struct SpecialMechanismEvent;
struct SpecialMechanismRules;
struct UnitType;

namespace concord::simulation::abilities {

struct RuntimeChronoBoardingPose {
    double x = 0.0;
    double y = 0.0;
    double level = 0.0;
};

// Synchronous borrows end with this event. Callers keep object/rule storage live.
class RuntimeChronoWarpQueries {
public:
    virtual ~RuntimeChronoWarpQueries() = default;
    virtual WorldObjectInstance* mutableObject(int objectId) = 0;
    virtual std::optional<bool> destinationAdmission(
        const WorldObjectInstance& object, const RuntimeCellPoint& destination) const = 0;
    virtual const int* pendingTarget(int objectId) const = 0;
    virtual const WorldObjectInstance* object(int objectId) const = 0;
    virtual int canonicalCell(int x, int y) const = 0;
    virtual bool bridge(int x, int y) const = 0;
    virtual bool groundSubcells(const WorldObjectInstance& object) const = 0;
    virtual const SpecialMechanismRules& specialRules() const = 0;
    virtual WorldObjectCellOffset subcellOffset(const WorldObjectInstance& object) const = 0;
    virtual RuntimeChronoBoardingPose boardingPose(const WorldObjectInstance& object) const = 0;
    virtual const UnitType* unitType(const WorldObjectInstance& object) const = 0;
    virtual RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance& object) const = 0;
};

class RuntimeChronoWarpArrivalActions {
public:
    virtual ~RuntimeChronoWarpArrivalActions() = default;
    virtual void releaseSubcellReservation(int objectId) = 0;
    virtual void assignSubcell(WorldObjectInstance& object) = 0;
    virtual void clearRenderPosition(WorldObjectInstance& object) = 0;
    virtual void refreshOccupancy() = 0;
    virtual void invalidateProjection() = 0;
};

// The adapter owns the original effect, including its moved-from lifetime.
// These operations only assemble that chrono effect and forward its feedback.
class RuntimeChronoWarpFeedback {
public:
    virtual ~RuntimeChronoWarpFeedback() = default;
    virtual void beginEffect(const WorldObjectInstance& object) = 0;
    virtual void worldPosition(double x, double y, double level) noexcept = 0;
    virtual void facingAndLifecycle(int facing, const char* lifecycle) = 0;
    virtual void animation(const std::string& animationId) = 0;
    virtual const std::string& animation() const noexcept = 0;
    virtual void initializeAndAppend() = 0;
    virtual void sound(const std::string& intent, RuntimeSpatialAudioPosition position) = 0;
};

void applyRuntimeChronoWarpEvent(
    const SpecialMechanismEvent& event, bool playSound,
    RuntimeChronoWarpQueries& queries,
    RuntimeChronoWarpArrivalActions& arrival,
    RuntimeChronoWarpFeedback& feedback);

} // namespace concord::simulation::abilities
