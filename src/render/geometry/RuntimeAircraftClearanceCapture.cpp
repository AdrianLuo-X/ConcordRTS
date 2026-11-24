#include "RuntimeAircraftClearanceCapture.h"

#include "data/art/ArtDatabase.h"
#include "data/art/TheaterDatabase.h"
#include "data/rules/RulesDatabase.h"
#include "render/core/detail/RuntimeProductionResources.h"
#include "render/lighting/RuntimeMapLight.h"
#include "render/units/RuntimeArcticArt.h"
#include "simulation/buildings/RuntimeBuildingSale.h"

namespace concord::render::geometry {
using concord::runtime::resource_detail::runtimeObjectAircraftRuleClassification;
using concord::runtime::resource_detail::toLowerAscii;

bool aircraftClearanceStructureRubble(const WorldObjectInstance& structure) {
    return structure.kind == WorldObjectKind::Structure && structure.rubble && !structure.alive;
}

bool aircraftClearanceBodyPresent(const WorldObjectInstance& object) {
    return object.alive || aircraftClearanceStructureRubble(object) || runtimeStructureIsSelling(object);
}

bool aircraftClearanceStructureHidden(const RulesDatabase* rules, const WorldObjectInstance& object) {
    if (runtimeInvisibleMapBuilding(rules, object)) return true;
    if (object.kind != WorldObjectKind::Structure || rules == nullptr) return false;
    const auto* building = rules->findBuildingType(object.type);
    return building != nullptr && toLowerAscii(building->image) == "null";
}

bool aircraftClearanceUsesArcticArt(const TheaterDatabase* theater, std::string_view mapTheater) {
    if (theater != nullptr && !theater->getTileExtension().empty())
        return runtimeArcticTheater(theater->getTileExtension());
    return runtimeArcticTheater(mapTheater);
}

std::string aircraftClearanceStructureArtId(
    const RulesDatabase* rules, const WorldObjectInstance& structure, bool arcticTheater) {
    const auto* building = rules == nullptr ? nullptr : rules->findBuildingType(structure.type);
    if (building == nullptr) return structure.type;
    return runtimeArcticArtId(building->image.empty() ? structure.type : building->image,
        building->alternateArcticArt, arcticTheater);
}

const UnitType* findAircraftClearanceUnit(const RulesDatabase* rules, const WorldObjectInstance& aircraft) {
    if (rules == nullptr) return nullptr;
    if (aircraft.kind == WorldObjectKind::Infantry) return rules->findUnitType("infantry", aircraft.type);
    if (aircraft.kind != WorldObjectKind::Unit) return nullptr;
    const auto* vehicle = rules->findUnitType("vehicle", aircraft.type);
    return vehicle == nullptr ? rules->findUnitType("aircraft", aircraft.type) : vehicle;
}

AircraftClearanceFoundation parseAircraftClearanceFoundation(const std::string& foundation) {
    if (foundation.empty()) return {};
    const std::string value = toLowerAscii(foundation);
    const std::size_t separator = value.find('x');
    if (separator == std::string::npos) {
        if (value == "custom") return {2, 2};
        return {};
    }
    try {
        const int width = std::stoi(value.substr(0, separator));
        const int height = std::stoi(value.substr(separator + 1));
        return {std::max(1, width), std::max(1, height)};
    } catch (...) {
        return {};
    }
}

RuntimeParticleClearanceRoof makeAircraftClearanceRoof(const WorldObjectInstance& structure, const ArtType* art) {
    RuntimeParticleClearanceRoof roof;
    const auto subCell = worldObjectSubCellOffset(structure);
    roof.cellSum = aircraftClearanceObjectX(structure) + subCell.x
        + aircraftClearanceObjectY(structure) + subCell.y;
    roof.level = aircraftClearanceDrawLevel(structure);
    roof.rubble = aircraftClearanceStructureRubble(structure);
    if (!roof.rubble && art != nullptr) roof.authoredHeight = std::clamp(art->height, 0, 512);
    return roof;
}

bool aircraftOverClearanceStructure(
    const WorldObjectInstance* aircraft, const AircraftClearanceStructure& structure) {
    if (aircraft == nullptr || structure.object == nullptr
        || structure.object->kind != WorldObjectKind::Structure) return false;

    const auto subCell = worldObjectSubCellOffset(*aircraft);
    const double aircraftX = aircraftClearanceObjectX(*aircraft) + subCell.x;
    const double aircraftY = aircraftClearanceObjectY(*aircraft) + subCell.y;
    const double structureX = aircraftClearanceObjectX(*structure.object);
    const double structureY = aircraftClearanceObjectY(*structure.object);
    const double halfCell = 0.5;
    const double epsilon = 0.000001;
    const double maxStructureX = structureX + static_cast<double>(std::max(1, structure.foundation.width)) - halfCell;
    const double maxStructureY = structureY + static_cast<double>(std::max(1, structure.foundation.height)) - halfCell;
    return aircraftX + epsilon >= structureX - halfCell && aircraftX - epsilon <= maxStructureX
        && aircraftY + epsilon >= structureY - halfCell && aircraftY - epsilon <= maxStructureY;
}

int aircraftFactoryClearanceCancellation(
    const WorldObjectInstance& aircraft, const WorldObjectInstance& structure, const UnitType* unit) {
    if (unit == nullptr || unit->jumpJetHeight <= 0) return -1;
    // The producing roof is crossed by real climb. Recover the normal sprite
    // lift gradually while the factory association is still active.
    const double height = std::max(0.0, aircraftClearanceObjectLevel(aircraft) - structure.level);
    const double progress = std::clamp(height * 256.0 / unit->jumpJetHeight, 0.0, 1.0);
    return progress < 1.0 ? static_cast<int>(std::lround(34.0 * (1.0 - progress))) : -1;
}

AircraftClearanceBatch::AircraftClearanceBatch(const GameWorld& world, const RulesDatabase* rules,
    const ArtDatabase* art, bool arcticTheater) : rules_(rules), art_(art), arcticTheater_(arcticTheater) {
    stats_.worldObjectsScanned = world.getObjects().size();
    for (const auto& object : world.getObjects()) {
        if (object.kind != WorldObjectKind::Structure || !aircraftClearanceBodyPresent(object)
            || aircraftClearanceStructureHidden(rules_, object)) continue;
        const auto* artType = structureArt(object);
        const auto foundation = artType == nullptr ? AircraftClearanceFoundation{} :
            parseAircraftClearanceFoundation(artType->foundation);
        structures_.push_back({&object, foundation});
        const auto x = aircraftClearanceObjectX(object);
        const auto y = aircraftClearanceObjectY(object);
        spatialIndex_.add(x - 1.0, y - 1.0, x + std::max(1, foundation.width), y + std::max(1, foundation.height));
    }
    stats_.structuresIndexed = structures_.size();
}

const ArtType* findAircraftClearanceStructureArt(
    const RulesDatabase* rules, const ArtDatabase* art, const WorldObjectInstance& structure,
    bool arcticTheater) {
    if (art == nullptr) return nullptr;
    if (const auto* selected = art->findArtType(aircraftClearanceStructureArtId(rules, structure, arcticTheater)))
        return selected;
    return art->findArtType(structure.type);
}

const ArtType* AircraftClearanceBatch::structureArt(const WorldObjectInstance& structure) const {
    return findAircraftClearanceStructureArt(rules_, art_, structure, arcticTheater_);
}

bool aircraftEligibleForKirovClearance(const WorldObjectInstance& aircraft, const UnitType* unit) {
    return unit != nullptr && toLowerAscii(unit->id) == "zep" && aircraft.alive
        && aircraft.airborne && aircraft.factoryExitHostId < 0;
}

double forecastAircraftClearanceFromResources(const GameWorld& world, const WorldObjectInstance& aircraft,
    const RulesDatabase* rules, const ArtDatabase* art, bool arcticTheater,
    double directionX, double directionY, double approachSpeed, int screenOffsetY) {
    const auto* unit = findAircraftClearanceUnit(rules, aircraft);
    if (!aircraftEligibleForKirovClearance(aircraft, unit)) return 0.0;
    const auto roofArt = [&](const WorldObjectInstance& structure) {
        return findAircraftClearanceStructureArt(rules, art, structure, arcticTheater);
    };
    return forecastAircraftClearance(world, aircraft, directionX, directionY, approachSpeed,
        unit->jumpjetSpeed, unit->jumpjetClimb, screenOffsetY,
        [&](const WorldObjectInstance& structure) {
            const auto* definition = roofArt(structure);
            return definition == nullptr ? AircraftClearanceFoundation{} :
                parseAircraftClearanceFoundation(definition->foundation);
        },
        [&](const WorldObjectInstance& structure) { return makeAircraftClearanceRoof(structure, roofArt(structure)); });
}

bool AircraftClearanceBatch::capture(const WorldObjectInstance& aircraft,
    RuntimeParticleAirbornePresentation& presentation, std::string* error) {
    const bool foreground = runtimeObjectAircraftRuleClassification(aircraft, rules_) && aircraft.airborne;
    if (!foreground) {
        presentation = {};
        return true;
    }

    const auto subCell = worldObjectSubCellOffset(aircraft);
    spatialIndex_.query(aircraftClearanceObjectX(aircraft) + subCell.x,
        aircraftClearanceObjectY(aircraft) + subCell.y, candidates_);
    ++stats_.queries;
    stats_.candidatesVisited += candidates_.size();
    const bool captured = captureAircraftClearance(&aircraft, foreground, candidates_.size(),
        [&](std::size_t candidate) { return structures_[candidates_[candidate]]; },
        [&] { return findAircraftClearanceUnit(rules_, aircraft); },
        [&](const WorldObjectInstance& structure) {
            const auto* artType = aircraftClearanceStructureRubble(structure) ? nullptr : structureArt(structure);
            return makeAircraftClearanceRoof(structure, artType);
        }, presentation);
    if (!captured && error != nullptr) *error = "aircraft presentation exceeds the clearance roof limit";
    return captured;
}
}
