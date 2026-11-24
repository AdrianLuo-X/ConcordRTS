#pragma once

#include "render/core/RuntimeRenderSpatialIndex.h"
#include "render/geometry/RuntimeAircraftClearancePixels.h"
#include "simulation/effects/RuntimeParticlePresentationValues.h"
#include "world/model/GameWorld.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

struct ArtType;
struct UnitType;
class ArtDatabase;
class RulesDatabase;
class TheaterDatabase;

namespace concord::render::geometry {

struct AircraftClearanceFoundation {
    int width = 1;
    int height = 1;
};

struct AircraftClearanceStructure {
    const WorldObjectInstance* object = nullptr;
    AircraftClearanceFoundation foundation;
};

inline double aircraftClearanceObjectX(const WorldObjectInstance& object) {
    return object.hasRenderPosition ? object.renderX : static_cast<double>(object.x);
}

inline double aircraftClearanceObjectY(const WorldObjectInstance& object) {
    return object.hasRenderPosition ? object.renderY : static_cast<double>(object.y);
}

inline double aircraftClearanceObjectLevel(const WorldObjectInstance& object) {
    return object.renderMoveActive
            || worldObjectHasStoppedGroundPose(object)
            || (object.airborne && object.hasRenderPosition)
            || (object.factoryExitHostId >= 0 && object.hasRenderPosition)
        ? object.renderLevel
        : static_cast<double>(object.level);
}

inline double aircraftClearanceDrawLevel(const WorldObjectInstance& object) {
    constexpr double submergedVisualDepth = 1.0 / 3.0;
    return aircraftClearanceObjectLevel(object) - (object.submerged ? submergedVisualDepth : 0.0);
}

bool aircraftClearanceStructureRubble(const WorldObjectInstance& structure);
bool aircraftClearanceBodyPresent(const WorldObjectInstance& object);
bool aircraftClearanceStructureHidden(const RulesDatabase* rules, const WorldObjectInstance& object);
bool aircraftClearanceUsesArcticArt(const TheaterDatabase* theater, std::string_view mapTheater = {});
std::string aircraftClearanceStructureArtId(
    const RulesDatabase* rules, const WorldObjectInstance& structure, bool arcticTheater);
const UnitType* findAircraftClearanceUnit(const RulesDatabase* rules, const WorldObjectInstance& aircraft);
AircraftClearanceFoundation parseAircraftClearanceFoundation(const std::string& foundation);
RuntimeParticleClearanceRoof makeAircraftClearanceRoof(const WorldObjectInstance& structure, const ArtType* art);
bool aircraftOverClearanceStructure(
    const WorldObjectInstance* aircraft, const AircraftClearanceStructure& structure);
int aircraftFactoryClearanceCancellation(
    const WorldObjectInstance& aircraft, const WorldObjectInstance& structure, const UnitType* unit);

// One selection rule serves the existing draw records and the view-independent
// world query. The native adapter can add the optional SHP height when freezing a roof.
template<typename StructureAt, typename AircraftUnit, typename FreezeRoof>
bool captureAircraftClearance(
    const WorldObjectInstance* aircraft, bool airborneForeground, std::size_t candidateCount,
    StructureAt structureAt, AircraftUnit aircraftUnit, FreezeRoof freezeRoof,
    RuntimeParticleAirbornePresentation& presentation, bool enforceSavedLimit = true) {
    presentation = {};
    if (aircraft == nullptr || !airborneForeground) return true;

    presentation.enabled = true;
    const auto subCell = worldObjectSubCellOffset(*aircraft);
    if (aircraft->aircraftDockFlightProgress >= 0.0) {
        presentation.clearanceFraction = std::clamp(aircraft->aircraftDockFlightProgress, 0.0, 1.0);
    }
    presentation.sourceCellSum = (aircraftClearanceObjectX(*aircraft) + subCell.x)
        + (aircraftClearanceObjectY(*aircraft) + subCell.y);
    presentation.sourceLevel = aircraftClearanceDrawLevel(*aircraft);
    if (aircraft->aircraftBuildingClearanceLevel >= 0.0 && aircraft->factoryExitHostId < 0) {
        presentation.resolvedClearanceLevel = aircraft->aircraftBuildingClearanceLevel;
        return true;
    }

    for (std::size_t candidate = 0; candidate < candidateCount; ++candidate) {
        const auto structure = structureAt(candidate);
        if (!aircraftOverClearanceStructure(aircraft, structure)) continue;
        if (aircraft->factoryExitHostId == structure.object->instanceId) {
            const int cancellation = aircraftFactoryClearanceCancellation(
                *aircraft, *structure.object, aircraftUnit());
            if (cancellation >= 0) {
                presentation.factoryLiftCancellation = cancellation;
                presentation.roofs.clear();
                return true;
            }
            continue;
        }
        if (enforceSavedLimit && presentation.roofs.size() >= runtimeParticleMaximumClearanceRoofs) {
            presentation = {};
            return false;
        }
        presentation.roofs.push_back(freezeRoof(*structure.object));
    }
    return true;
}


bool aircraftEligibleForKirovClearance(const WorldObjectInstance& aircraft, const UnitType* unit);

const ArtType* findAircraftClearanceStructureArt(
    const RulesDatabase* rules, const ArtDatabase* art, const WorldObjectInstance& structure,
    bool arcticTheater);

// The caller already qualified the stock Kirov flight. Roof metadata may be
// backed by the original native SHP metrics when those are available.
template<typename FoundationFor, typename FreezeRoof>
double forecastAircraftClearance(const GameWorld& world, const WorldObjectInstance& aircraft,
    double directionX, double directionY, double approachSpeed, double nominalSpeed,
    double climb, int screenOffsetY, FoundationFor foundationFor, FreezeRoof freezeRoof) {
    constexpr int clearanceTileHeight = 30;
    const auto subCell = worldObjectSubCellOffset(aircraft);
    const double sourceX = aircraftClearanceObjectX(aircraft) + subCell.x;
    const double sourceY = aircraftClearanceObjectY(aircraft) + subCell.y;
    const double length = std::hypot(directionX, directionY);
    const bool forward = std::isfinite(length) && length > 0.000001;
    const double dx = forward ? directionX / length : 0.0;
    const double dy = forward ? directionY / length : 0.0;
    double target = 0.0;
    for (const auto& structure : world.getObjects()) {
        // The forecast uses world and art geometry independently of the view.
        if (structure.kind != WorldObjectKind::Structure || !structure.alive
            || aircraftClearanceStructureRubble(structure)) continue;
        const auto foundation = foundationFor(structure);
        const double minX = aircraftClearanceObjectX(structure) - 0.5;
        const double minY = aircraftClearanceObjectY(structure) - 0.5;
        const double maxX = minX + std::max(1, foundation.width);
        const double maxY = minY + std::max(1, foundation.height);
        const bool inside = sourceX >= minX && sourceX <= maxX && sourceY >= minY && sourceY <= maxY;
        double entry = 0.0;
        double exit = 0.0;
        if (forward) {
            exit = std::numeric_limits<double>::infinity();
            const auto clip = [&](double source, double direction, double low, double high) {
                if (std::abs(direction) <= 0.000001) return source >= low && source <= high;
                double slabEntry = (low-source)/direction, slabExit = (high-source)/direction;
                if (slabEntry > slabExit) std::swap(slabEntry, slabExit);
                entry = std::max(entry, slabEntry);
                exit = std::min(exit, slabExit);
                return entry <= exit;
            };
            if (!clip(sourceX, dx, minX, maxX) || !clip(sourceY, dy, minY, maxY) || exit < 0.0) continue;
        } else if (!inside) {
            continue;
        }

        RuntimeParticleAirbornePresentation required;
        required.enabled = true;
        // The worst point along the crossing can be later than entry.
        required.sourceCellSum = sourceX + sourceY + std::max(entry*(dx+dy), exit*(dx+dy));
        required.sourceLevel = aircraftClearanceDrawLevel(aircraft);
        required.roofs.push_back(freezeRoof(structure));
        const double lift = aircraftAirborneClearanceLiftPixels(required, clearanceTileHeight, screenOffsetY) / 15.0;
        if (!inside) {
            const double speed = std::isfinite(approachSpeed) && approachSpeed >= 0.0 ? approachSpeed : nominalSpeed;
            const double neededDistance = climb > 0.0 ? lift * std::max(0.0, speed) / climb + 1.0 : 0.0;
            if (entry > neededDistance) continue;
        }
        target = std::max(target, lift);
    }
    return target;
}

double forecastAircraftClearanceFromResources(const GameWorld& world, const WorldObjectInstance& aircraft,
    const RulesDatabase* rules, const ArtDatabase* art, bool arcticTheater,
    double directionX, double directionY, double approachSpeed, int screenOffsetY);

// A damage-smoke update may query several aircraft against the same world.
// The borrowed objects and the order of structures are stable for that update.
class AircraftClearanceBatch {
public:
    struct Statistics {
        std::size_t worldObjectsScanned = 0;
        std::size_t structuresIndexed = 0;
        std::size_t queries = 0;
        std::size_t candidatesVisited = 0;
    };

    AircraftClearanceBatch(const GameWorld& world, const RulesDatabase* rules,
        const ArtDatabase* art, bool arcticTheater);
    bool capture(const WorldObjectInstance& aircraft,
        RuntimeParticleAirbornePresentation& presentation, std::string* error);
    Statistics statistics() const { return stats_; }

private:
    const ArtType* structureArt(const WorldObjectInstance& structure) const;

    const RulesDatabase* rules_;
    const ArtDatabase* art_;
    bool arcticTheater_;
    std::vector<AircraftClearanceStructure> structures_;
    RuntimeRenderSpatialIndex spatialIndex_;
    std::vector<std::size_t> candidates_;
    Statistics stats_;
};
}
