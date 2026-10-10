#include "world/model/GameWorld.h"
#include "world/model/GameWorldSubCellOffset.h"
#include "simulation/mod_extensions/RuntimeModExtensions.h"

#include "data/rules/RulesDatabase.h"
#include "simulation/buildings/RuntimeInfantryServiceSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

int cellIndex512(int x, int y) {
    if (x < 0 || y < 0) {
        return -1;
    }

    return (y * 512) + x;
}

int objectCellIndex(int x, int y) {
    if (x < 0 || y < 0) {
        return -1;
    }

    return (y * 1000) + x;
}

void updateBounds(GameWorldSummary& summary, int x, int y) {
    if (x < 0 || y < 0) {
        return;
    }

    if (summary.minCellX < 0 || x < summary.minCellX) {
        summary.minCellX = x;
    }
    if (summary.minCellY < 0 || y < summary.minCellY) {
        summary.minCellY = y;
    }
    if (summary.maxCellX < 0 || x > summary.maxCellX) {
        summary.maxCellX = x;
    }
    if (summary.maxCellY < 0 || y > summary.maxCellY) {
        summary.maxCellY = y;
    }
}

std::unordered_map<int, const MapOverlayCell*> buildOverlayLookup(
    const std::vector<MapOverlayCell>& overlayCells
) {
    std::unordered_map<int, const MapOverlayCell*> lookup;
    lookup.reserve(overlayCells.size());

    for (const MapOverlayCell& overlay : overlayCells) {
        lookup[overlay.cellIndex] = &overlay;
    }

    return lookup;
}

std::unordered_map<int, int> buildTerrainLevelLookup(
    const std::vector<TerrainCell>& terrainCells
) {
    std::unordered_map<int, int> lookup;
    lookup.reserve(terrainCells.size());

    for (const TerrainCell& cell : terrainCells) {
        lookup[objectCellIndex(cell.x, cell.y)] = cell.level;
    }

    return lookup;
}

int terrainLevelAt(
    const std::unordered_map<int, int>& terrainLevelByCell,
    int x,
    int y
) {
    const auto it = terrainLevelByCell.find(objectCellIndex(x, y));
    if (it == terrainLevelByCell.end()) {
        return 0;
    }

    return it->second;
}

std::string lowercaseAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

constexpr std::uint64_t deterministicHashOffset = 14695981039346656037ULL;
constexpr std::uint64_t deterministicHashPrime = 1099511628211ULL;
constexpr double deterministicDoubleScale = 1000000.0;

bool asciiWhitespace(char value) {
    return value == ' '
        || value == '\t'
        || value == '\r'
        || value == '\n'
        || value == '\f'
        || value == '\v';
}

std::string canonicalSimulationString(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && asciiWhitespace(value[first])) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && asciiWhitespace(value[last - 1])) {
        --last;
    }

    std::string canonical;
    canonical.reserve(last - first);
    for (std::size_t index = first; index < last; ++index) {
        const char character = value[index];
        canonical.push_back(
            character >= 'a' && character <= 'z'
                ? static_cast<char>(character - 'a' + 'A')
                : character
        );
    }
    return canonical;
}

class CanonicalSimulationRecord {
public:
    void integer(std::int64_t value) {
        unsignedInteger(static_cast<std::uint64_t>(value));
    }

    void unsignedInteger(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void boolean(bool value) {
        bytes_.push_back(value ? 1U : 0U);
    }

    void string(std::string_view value) {
        const std::string canonical = canonicalSimulationString(value);
        unsignedInteger(static_cast<std::uint64_t>(canonical.size()));
        bytes_.insert(bytes_.end(), canonical.begin(), canonical.end());
    }

    void exactString(std::string_view value) {
        unsignedInteger(static_cast<std::uint64_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void quantizedDouble(double value) {
        if (std::isnan(value)) {
            bytes_.push_back(0U);
            return;
        }
        if (value == -std::numeric_limits<double>::infinity()) {
            bytes_.push_back(1U);
            return;
        }
        if (value == std::numeric_limits<double>::infinity()) {
            bytes_.push_back(3U);
            return;
        }

        bytes_.push_back(2U);
        constexpr double maximum =
            static_cast<double>(std::numeric_limits<std::int64_t>::max());
        constexpr double minimum =
            static_cast<double>(std::numeric_limits<std::int64_t>::min());
        const double scaled = value * deterministicDoubleScale;
        if (scaled >= maximum) {
            integer(std::numeric_limits<std::int64_t>::max());
        } else if (scaled <= minimum) {
            integer(std::numeric_limits<std::int64_t>::min());
        } else {
            integer(static_cast<std::int64_t>(std::llround(scaled)));
        }
    }

    void orderedIntegers(const std::vector<int>& values) {
        unsignedInteger(static_cast<std::uint64_t>(values.size()));
        for (const int value : values) {
            integer(value);
        }
    }

    void unorderedIntegers(const std::vector<int>& values) {
        std::vector<int> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        orderedIntegers(sorted);
    }

    void bytes(const std::vector<std::uint8_t>& value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    const std::vector<std::uint8_t>& value() const {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

void appendCanonicalCollection(
    CanonicalSimulationRecord& output,
    std::string_view name,
    std::vector<std::vector<std::uint8_t>> records
) {
    std::sort(records.begin(), records.end());
    output.string(name);
    output.unsignedInteger(static_cast<std::uint64_t>(records.size()));
    for (const std::vector<std::uint8_t>& record : records) {
        output.unsignedInteger(static_cast<std::uint64_t>(record.size()));
        output.bytes(record);
    }
}

std::uint64_t hashCanonicalBytes(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = deterministicHashOffset;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= deterministicHashPrime;
    }
    return hash;
}

bool missionLooksDeployed(const std::string& mission) {
    const std::string normalized = lowercaseAscii(mission);
    return normalized == "deployed"
        || normalized == "deployedidle"
        || normalized == "deployedfire";
}

void applyInitialAmmo(
    WorldObjectInstance& object,
    const RulesDatabase* rules,
    const std::string& rulesKind
) {
    object.currentAmmo = -1;
    if (rules == nullptr) {
        return;
    }

    const UnitType* type = rules->findUnitType(rulesKind, object.type);
    if (type == nullptr && rulesKind == "vehicle") {
        // Some maps author aircraft-like entries in [Units]. VehicleTypes
        // retain precedence so an ambiguous identifier remains stable.
        type = rules->findUnitType("aircraft", object.type);
    }
    if (type == nullptr || type->ammo < 0) {
        return;
    }

    const int initial = type->initialAmmo < 0
        ? type->ammo
        : type->initialAmmo;
    object.currentAmmo = std::clamp(initial, 0, type->ammo);
}

int absoluteHealthFromMapScale(int authoredHealth, int strength) {
    if (authoredHealth < 0 || strength <= 0) {
        return authoredHealth;
    }
    if (authoredHealth == 0) {
        return 0;
    }

    constexpr int fullMapHealth = 256;
    const int clampedHealth = std::min(authoredHealth, fullMapHealth);
    const long long scaled =
        static_cast<long long>(clampedHealth) * strength / fullMapHealth;
    return std::clamp(
        static_cast<int>(std::max(1LL, scaled)),
        1,
        strength
    );
}

void applyInitialMapHealth(
    WorldObjectInstance& object,
    int authoredHealth,
    int strength,
    bool leaveRubble = false
) {
    object.health = absoluteHealthFromMapScale(authoredHealth, strength);
    object.alive = object.health != 0;
    object.rubble = !object.alive
        && object.kind == WorldObjectKind::Structure
        && leaveRubble;
}

void addPlayerObjectCount(
    std::map<std::string, PlayerState>& playersByOwner,
    const WorldObjectInstance& object
) {
    if (object.owner.empty() || !object.alive) {
        return;
    }

    PlayerState& player = playersByOwner[object.owner];
    player.owner = object.owner;
    ++player.objectCount;

    switch (object.kind) {
    case WorldObjectKind::Structure:
        ++player.structureCount;
        break;
    case WorldObjectKind::Unit:
        if (object.authoredAircraft) {
            ++player.aircraftCount;
        } else {
            ++player.unitCount;
        }
        break;
    case WorldObjectKind::Infantry:
        ++player.infantryCount;
        break;
    }
}

WorldObjectInstance makeStructureInstance(
    int instanceId,
    const MapStructureObject& structure
) {
    WorldObjectInstance object;
    object.instanceId = instanceId;
    object.kind = WorldObjectKind::Structure;
    object.mapId = structure.id;
    object.owner = structure.owner;
    object.type = structure.type;
    applyInitialMapHealth(
        object,
        structure.health,
        structure.strength,
        structure.leaveRubble
    );
    object.x = structure.x;
    object.y = structure.y;
    object.cell = objectCellIndex(structure.x, structure.y);
    object.facing = worldObjectFacingFromMapFacing(structure.facing);
    object.tag = structure.tag;
    return object;
}

WorldObjectInstance makeUnitInstance(
    int instanceId,
    const MapUnitObject& unit
) {
    WorldObjectInstance object;
    object.instanceId = instanceId;
    object.kind = WorldObjectKind::Unit;
    object.mapId = unit.id;
    object.owner = unit.owner;
    object.type = unit.type;
    applyInitialMapHealth(object, unit.health, unit.strength);
    object.x = unit.x;
    object.y = unit.y;
    object.cell = objectCellIndex(unit.x, unit.y);
    object.facing = worldObjectFacingFromMapFacing(unit.facing);
    object.mission = unit.mission;
    object.deployed = missionLooksDeployed(object.mission);
    object.tag = unit.tag;
    object.veterancy = unit.veterancy;
    object.onBridge = unit.onBridge;
    return object;
}

WorldObjectInstance makeInfantryInstance(
    int instanceId,
    const MapInfantryObject& infantry
) {
    WorldObjectInstance object;
    object.instanceId = instanceId;
    object.kind = WorldObjectKind::Infantry;
    object.mapId = infantry.id;
    object.owner = infantry.owner;
    object.type = infantry.type;
    applyInitialMapHealth(object, infantry.health, infantry.strength);
    object.x = infantry.x;
    object.y = infantry.y;
    object.cell = objectCellIndex(infantry.x, infantry.y);
    object.facing = worldObjectFacingFromMapFacing(infantry.facing);
    object.subCell = infantry.subCell;
    object.mission = infantry.mission;
    object.deployed = missionLooksDeployed(object.mission);
    object.tag = infantry.tag;
    object.veterancy = infantry.veterancy;
    object.onBridge = infantry.onBridge;
    return object;
}

WorldObjectInstance makeAircraftInstance(
    int instanceId,
    const MapAircraftObject& aircraft
) {
    WorldObjectInstance object;
    object.instanceId = instanceId;
    object.kind = WorldObjectKind::Unit;
    object.mapId = aircraft.id;
    object.owner = aircraft.owner;
    object.type = aircraft.type;
    applyInitialMapHealth(object, aircraft.health, aircraft.strength);
    object.x = aircraft.x;
    object.y = aircraft.y;
    object.cell = objectCellIndex(aircraft.x, aircraft.y);
    object.facing = worldObjectFacingFromMapFacing(aircraft.facing);
    object.mission = aircraft.mission;
    object.deployed = missionLooksDeployed(object.mission);
    object.tag = aircraft.tag;
    object.veterancy = aircraft.veterancy;
    object.authoredAircraft = true;
    return object;
}

bool isActiveMapSmudge(const MapSmudgeObject& smudge) {
    return smudge.position.valid
        && !smudge.type.empty()
        && smudge.data == 0;
}

int activeMapSmudgeCount(const MapDatabase& mapDatabase) {
    return static_cast<int>(std::count_if(
        mapDatabase.getSmudges().begin(),
        mapDatabase.getSmudges().end(),
        isActiveMapSmudge
    ));
}

bool countsMatchMapSummary(
    const GameWorldSummary& worldSummary,
    const MapDatabase& mapDatabase
) {
    const MapSummary& mapSummary = mapDatabase.getSummary();
    return worldSummary.terrainCells == static_cast<int>(mapDatabase.getIsoTiles().size())
        && worldSummary.overlayCells == static_cast<int>(mapDatabase.getOverlayCells().size())
        && worldSummary.terrainObjects == static_cast<int>(mapDatabase.getTerrainObjects().size())
        && worldSummary.smudges == activeMapSmudgeCount(mapDatabase)
        && worldSummary.structures == static_cast<int>(mapSummary.structureCount)
        && worldSummary.units == static_cast<int>(mapSummary.unitCount)
        && worldSummary.infantry == static_cast<int>(mapSummary.infantryCount)
        && worldSummary.aircraft == static_cast<int>(mapSummary.aircraftCount)
        && worldSummary.waypoints == static_cast<int>(mapSummary.waypointCount);
}

void addCountMismatchWarnings(
    const GameWorldSummary& worldSummary,
    const MapDatabase& mapDatabase,
    std::vector<std::string>& warnings
) {
    const MapSummary& mapSummary = mapDatabase.getSummary();

    const auto check = [&warnings](const std::string& label, int worldCount, int mapCount) {
        if (worldCount != mapCount) {
            warnings.push_back(
                "GameWorld " + label + " count "
                + std::to_string(worldCount)
                + " does not match map summary "
                + std::to_string(mapCount)
            );
        }
    };

    check("terrain cell", worldSummary.terrainCells,
        static_cast<int>(mapDatabase.getIsoTiles().size()));
    check("overlay cell", worldSummary.overlayCells,
        static_cast<int>(mapDatabase.getOverlayCells().size()));
    check("terrain object", worldSummary.terrainObjects,
        static_cast<int>(mapDatabase.getTerrainObjects().size()));
    check("smudge", worldSummary.smudges,
        activeMapSmudgeCount(mapDatabase));
    check("structure", worldSummary.structures,
        static_cast<int>(mapSummary.structureCount));
    check("unit", worldSummary.units,
        static_cast<int>(mapSummary.unitCount));
    check("infantry", worldSummary.infantry,
        static_cast<int>(mapSummary.infantryCount));
    check("aircraft", worldSummary.aircraft,
        static_cast<int>(mapSummary.aircraftCount));
    check("waypoint", worldSummary.waypoints,
        static_cast<int>(mapSummary.waypointCount));
}

} // namespace

int worldObjectFacingFromMapFacing(int facing) {
    if (facing < 0) {
        return facing;
    }
    return ((facing + 32) % 256 + 256) % 256;
}

int worldObjectArtFacingBucket(int runtimeFacing, int facingCount) {
    if (facingCount <= 1 || runtimeFacing < 0) {
        return 0;
    }

    const int authoredFacing =
        (((runtimeFacing - 32) % 256) + 256) % 256;
    return ((authoredFacing * facingCount) + 128) / 256 % facingCount;
}

int worldObjectMovementAnimationPhase(
    const WorldObjectInstance& object,
    int frameCount
) {
    if (frameCount <= 1) {
        return 0;
    }

    const double distance = std::max(0.0, object.movementAnimationDistance);
    const double cycle = std::fmod(distance, 1.0);
    return std::clamp(
        static_cast<int>(std::floor(cycle * frameCount)),
        0,
        frameCount - 1
    );
}

WorldObjectCellOffset worldObjectSubCellOffset(
    const WorldObjectInstance& object
) {
    return object.kind == WorldObjectKind::Infantry
        ? worldInfantrySubCellOffset(object.subCell) : WorldObjectCellOffset{};
}

void initializeWorldOverlayWallState(TerrainCell& cell, const RulesDatabase& rules) {
    cell.overlayWall = {};
    if (!cell.hasOverlay || cell.overlayIndex < 0
        || cell.overlayIndex >= static_cast<int>(rules.getOverlayTypes().size())
        || cell.overlayFrame < 0 || cell.overlayFrame > 255) return;
    const auto& overlay = rules.getOverlayTypes()[static_cast<std::size_t>(cell.overlayIndex)];
    const auto canonical = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    };
    if (!overlay.wall || (!cell.overlayType.empty()
        && canonical(cell.overlayType) != canonical(overlay.id))) return;
    const auto* building = rules.findBuildingType(overlay.id);
    if (!building || !building->wall || building->strength <= 0) return;
    cell.overlayWall.originalIndex = cell.overlayIndex;
    cell.overlayWall.originalFrame = cell.overlayFrame;
    cell.overlayWall.originalType = overlay.id;
    cell.overlayWall.originalImage = cell.overlayImage;
    // OverlayPack carries a damage bank, not original exact HP. These are
    // representative values at the existing half/quarter render thresholds.
    const int bank = cell.overlayFrame / 16;
    cell.overlayWall.health = bank >= 2 ? std::max(1, building->strength / 4)
        : bank == 1 ? std::max(1, building->strength / 2) : building->strength;
}

bool GameWorld::loadFromMap(
    const MapDatabase& mapDatabase,
    const RulesDatabase* rules
) {
    summary_ = GameWorldSummary{};
    terrainCells_.clear();
    overlayCells_.clear();
    terrainObjects_.clear();
    smudges_.clear();
    objects_.clear();
    players_.clear();
    waypoints_.clear();
    warnings_.clear();

    const MapSummary& mapSummary = mapDatabase.getSummary();
    summary_.mapName = mapSummary.name;
    summary_.theater = mapSummary.theater;
    summary_.localSize = mapSummary.localSize;
    summary_.width = mapSummary.width;
    summary_.height = mapSummary.height;

    overlayCells_ = mapDatabase.getOverlayCells();
    const std::unordered_map<int, const MapOverlayCell*> overlayByCell =
        buildOverlayLookup(overlayCells_);

    const std::vector<MapIsoTile>& isoTiles = mapDatabase.getIsoTiles();
    terrainCells_.reserve(isoTiles.size());

    for (const MapIsoTile& sourceTile : isoTiles) {
        TerrainCell cell;
        cell.x = sourceTile.x;
        cell.y = sourceTile.y;
        cell.cellIndex512 = cellIndex512(sourceTile.x, sourceTile.y);
        cell.tileIndex = sourceTile.tileIndex;
        cell.subTileIndex = sourceTile.subTileIndex;
        cell.level = sourceTile.level;
        cell.iceGrowth = sourceTile.iceGrowth;

        const auto overlayIt = overlayByCell.find(cell.cellIndex512);
        if (overlayIt != overlayByCell.end() && overlayIt->second != nullptr) {
            const MapOverlayCell& overlay = *overlayIt->second;
            cell.hasOverlay = true;
            cell.overlayIndex = overlay.overlayIndex;
            cell.overlayFrame = overlay.frameIndex;
            cell.overlayType = overlay.type;
            cell.overlayImage = overlay.image;
            cell.overlayIsOre = overlay.isOre;
            cell.overlayIsGem = overlay.isGem;
        }

        if (rules) initializeWorldOverlayWallState(cell, *rules);
        terrainCells_.push_back(cell);
        updateBounds(summary_, cell.x, cell.y);
    }

    const std::unordered_map<int, int> terrainLevelByCell =
        buildTerrainLevelLookup(terrainCells_);

    const std::vector<MapTerrainObject>& mapTerrainObjects =
        mapDatabase.getTerrainObjects();
    terrainObjects_.reserve(mapTerrainObjects.size());

    for (const MapTerrainObject& sourceObject : mapTerrainObjects) {
        WorldTerrainObject object;
        object.mapId = sourceObject.mapId;
        object.type = sourceObject.type;
        object.tag = sourceObject.tag;
        object.x = sourceObject.position.x;
        object.y = sourceObject.position.y;
        object.cell = sourceObject.position.cell;
        object.level = terrainLevelAt(terrainLevelByCell, object.x, object.y);
        object.strength = sourceObject.strength;
        object.health = sourceObject.health;
        object.immune = sourceObject.immune;
        object.alive = sourceObject.alive;
        object.occupationBits = sourceObject.occupationBits;
        object.occupiedSubCells = sourceObject.occupiedSubCells;
        object.footprintWidth = sourceObject.footprintWidth;
        object.footprintHeight = sourceObject.footprintHeight;
        terrainObjects_.push_back(object);
    }

    const std::vector<MapSmudgeObject>& mapSmudges = mapDatabase.getSmudges();
    smudges_.reserve(mapSmudges.size());
    for (const MapSmudgeObject& sourceSmudge : mapSmudges) {
        // The fourth authored field is an IGNORE marker in RA2/YR maps.
        // Keep it in MapDatabase for diagnostics, but do not materialize it.
        if (!isActiveMapSmudge(sourceSmudge)) {
            continue;
        }
        WorldSmudge smudge;
        smudge.mapId = sourceSmudge.id;
        smudge.type = sourceSmudge.type;
        smudge.x = sourceSmudge.position.x;
        smudge.y = sourceSmudge.position.y;
        smudge.cell = sourceSmudge.position.cell;
        smudge.level = terrainLevelAt(
            terrainLevelByCell,
            smudge.x,
            smudge.y
        );
        smudge.data = sourceSmudge.data;
        smudge.authored = true;
        smudges_.push_back(smudge);
    }

    std::map<std::string, PlayerState> playersByOwner;
    int nextInstanceId = 1;

    for (const MapStructureObject& structure : mapDatabase.getStructures()) {
        WorldObjectInstance object =
            makeStructureInstance(nextInstanceId++, structure);
        initializeRuntimeInfantryServiceAmmo(
            object,
            rules == nullptr
                ? nullptr
                : rules->findBuildingType(object.type)
        );
        object.level = terrainLevelAt(terrainLevelByCell, object.x, object.y);
        addPlayerObjectCount(playersByOwner, object);
        objects_.push_back(object);
    }

    for (const MapUnitObject& unit : mapDatabase.getUnits()) {
        WorldObjectInstance object = makeUnitInstance(nextInstanceId++, unit);
        applyInitialAmmo(object, rules, "vehicle");
        object.level = terrainLevelAt(terrainLevelByCell, object.x, object.y);
        addPlayerObjectCount(playersByOwner, object);
        if (object.onBridge) {
            ++summary_.bridgeObjects;
        }
        objects_.push_back(object);
    }

    for (const MapInfantryObject& infantry : mapDatabase.getInfantry()) {
        WorldObjectInstance object =
            makeInfantryInstance(nextInstanceId++, infantry);
        applyInitialAmmo(object, rules, "infantry");
        object.level = terrainLevelAt(terrainLevelByCell, object.x, object.y);
        addPlayerObjectCount(playersByOwner, object);
        if (object.onBridge) {
            ++summary_.bridgeObjects;
        }
        objects_.push_back(object);
    }

    for (const MapAircraftObject& aircraft : mapDatabase.getAircraft()) {
        WorldObjectInstance object =
            makeAircraftInstance(nextInstanceId++, aircraft);
        applyInitialAmmo(object, rules, "aircraft");
        object.level = terrainLevelAt(terrainLevelByCell, object.x, object.y);
        // Runtime flight hydration starts from this terrain baseline and
        // applies the authored aircraft's flight-level offset exactly once.
        object.flightLevel = object.level;
        addPlayerObjectCount(playersByOwner, object);
        objects_.push_back(object);
    }

    players_.reserve(playersByOwner.size());
    for (const auto& [owner, player] : playersByOwner) {
        players_.push_back(player);
    }

    const std::vector<MapWaypoint>& mapWaypoints = mapDatabase.getWaypoints();
    waypoints_.reserve(mapWaypoints.size());

    for (const MapWaypoint& sourceWaypoint : mapWaypoints) {
        WorldWaypoint waypoint;
        waypoint.id = sourceWaypoint.id;
        waypoint.x = sourceWaypoint.position.x;
        waypoint.y = sourceWaypoint.position.y;
        waypoint.cell = sourceWaypoint.position.cell;
        waypoints_.push_back(waypoint);
    }

    const MapOverlaySummary& overlaySummary = mapDatabase.getOverlaySummary();
    summary_.loaded = true;
    summary_.terrainCells = static_cast<int>(terrainCells_.size());
    summary_.overlayCells = static_cast<int>(overlayCells_.size());
    summary_.overlayOreCells = static_cast<int>(overlaySummary.oreCount);
    summary_.overlayGemCells = static_cast<int>(overlaySummary.gemCount);
    summary_.terrainObjects = static_cast<int>(terrainObjects_.size());
    summary_.smudges = static_cast<int>(smudges_.size());
    summary_.structures = static_cast<int>(mapDatabase.getStructures().size());
    summary_.units = static_cast<int>(mapDatabase.getUnits().size());
    summary_.infantry = static_cast<int>(mapDatabase.getInfantry().size());
    summary_.aircraft = static_cast<int>(mapDatabase.getAircraft().size());
    summary_.objectInstances = static_cast<int>(objects_.size());
    summary_.waypoints = static_cast<int>(waypoints_.size());
    summary_.players = static_cast<int>(players_.size());
    summary_.countsMatchMap = countsMatchMapSummary(summary_, mapDatabase);

    addCountMismatchWarnings(summary_, mapDatabase, warnings_);
    return summary_.loaded;
}

const GameWorldSummary& GameWorld::getSummary() const {
    return summary_;
}

const std::vector<TerrainCell>& GameWorld::getTerrainCells() const {
    return terrainCells_;
}

const std::vector<MapOverlayCell>& GameWorld::getOverlayCells() const {
    return overlayCells_;
}

const std::vector<WorldTerrainObject>& GameWorld::getTerrainObjects() const {
    return terrainObjects_;
}

std::vector<WorldTerrainObject>& GameWorld::accessTerrainObjects() {
    return terrainObjects_;
}

const std::vector<WorldSmudge>& GameWorld::getSmudges() const {
    return smudges_;
}

std::vector<WorldSmudge>& GameWorld::accessSmudges() {
    return smudges_;
}

const std::vector<WorldObjectInstance>& GameWorld::getObjects() const {
    return objects_;
}

std::vector<TerrainCell>& GameWorld::accessTerrainCells() {
    return terrainCells_;
}

std::vector<WorldObjectInstance>& GameWorld::accessObjects() {
    return objects_;
}

const std::vector<PlayerState>& GameWorld::getPlayers() const {
    return players_;
}

const std::vector<WorldWaypoint>& GameWorld::getWaypoints() const {
    return waypoints_;
}

const std::vector<std::string>& GameWorld::getWarnings() const {
    return warnings_;
}

std::uint64_t GameWorld::deterministicSimulationStateHash() const {
    CanonicalSimulationRecord canonical;
    canonical.string("GameWorldSimulationState/v2");

    std::vector<std::vector<std::uint8_t>> terrainRecords;
    terrainRecords.reserve(terrainCells_.size());
    for (const TerrainCell& terrain : terrainCells_) {
        CanonicalSimulationRecord record;
        record.integer(terrain.x);
        record.integer(terrain.y);
        record.integer(terrain.cellIndex512);
        record.integer(terrain.tileIndex);
        record.integer(terrain.subTileIndex);
        record.integer(terrain.level);
        record.integer(terrain.iceGrowth);
        record.boolean(terrain.hasOverlay);
        record.integer(terrain.overlayIndex);
        record.integer(terrain.overlayFrame);
        record.string(terrain.overlayType);
        record.string(terrain.overlayImage);
        record.boolean(terrain.overlayIsOre);
        record.boolean(terrain.overlayIsGem);
        record.integer(terrain.overlayWall.originalIndex);
        record.integer(terrain.overlayWall.originalFrame);
        record.string(terrain.overlayWall.originalType);
        record.string(terrain.overlayWall.originalImage);
        record.integer(terrain.overlayWall.health);
        terrainRecords.push_back(record.value());
    }
    appendCanonicalCollection(
        canonical,
        "TerrainAndResources/v1",
        std::move(terrainRecords)
    );

    std::vector<std::vector<std::uint8_t>> terrainObjectRecords;
    terrainObjectRecords.reserve(terrainObjects_.size());
    for (const WorldTerrainObject& object : terrainObjects_) {
        CanonicalSimulationRecord record;
        record.string(object.mapId);
        record.string(object.type);
        record.string(object.tag);
        record.integer(object.x);
        record.integer(object.y);
        record.integer(object.cell);
        record.integer(object.level);
        record.integer(object.strength);
        record.integer(object.health);
        record.boolean(object.immune);
        record.boolean(object.alive);
        record.integer(object.occupationBits);
        // Occupied subcells are a set. Their vector order is not gameplay
        // authority and must not make peer hashes diverge.
        record.unorderedIntegers(object.occupiedSubCells);
        record.integer(object.footprintWidth);
        record.integer(object.footprintHeight);
        terrainObjectRecords.push_back(record.value());
    }
    appendCanonicalCollection(
        canonical,
        "TerrainObjects/v1",
        std::move(terrainObjectRecords)
    );

    std::vector<std::vector<std::uint8_t>> smudgeRecords;
    smudgeRecords.reserve(smudges_.size());
    for (const WorldSmudge& smudge : smudges_) {
        CanonicalSimulationRecord record;
        record.string(smudge.mapId);
        record.string(smudge.type);
        record.integer(smudge.x);
        record.integer(smudge.y);
        record.integer(smudge.cell);
        record.integer(smudge.level);
        record.integer(smudge.data);
        record.boolean(smudge.authored);
        smudgeRecords.push_back(record.value());
    }
    appendCanonicalCollection(
        canonical,
        "Smudges/v1",
        std::move(smudgeRecords)
    );

    std::vector<std::vector<std::uint8_t>> objectRecords;
    objectRecords.reserve(objects_.size());
    for (const WorldObjectInstance& object : objects_) {
        CanonicalSimulationRecord record;
        record.integer(object.instanceId);
        record.integer(static_cast<int>(object.kind));
        record.string(object.mapId);
        record.string(object.owner);
        record.string(object.type);
        record.integer(object.health);
        record.integer(object.x);
        record.integer(object.y);
        record.integer(object.cell);
        record.integer(object.level);
        record.integer(object.facing);
        record.integer(object.turretFacing);
        if (object.bodyFacingClock.active() || object.turretFacingClock.active()) {
            record.string("FacingClocks/v1");
            record.string(runtimeSerializeFacingClock(object.bodyFacingClock));
            record.string(runtimeSerializeFacingClock(object.turretFacingClock));
        }
        record.integer(object.subCell);
        // Mission dispatch preserves authored bytes. In particular, mission
        // labels are not identifiers whose case or edge whitespace can be
        // normalized without changing subsequent runtime behavior.
        record.exactString(object.mission);
        record.string(object.tag);
        record.integer(object.veterancy);
        record.integer(object.veterancyExperience);
        record.quantizedDouble(object.crateArmorMultiplier);
        record.quantizedDouble(object.crateFirepowerMultiplier);
        record.quantizedDouble(object.crateSpeedMultiplier);
        record.boolean(object.crateCloakable);
        if (object.modExtensions.initialized || !object.modExtensions.effects.empty()) {
            record.string("ModExtensions/v1");
            record.string(runtimeSerializeModExtensions(object.modExtensions));
        }
        record.boolean(object.onBridge);
        record.boolean(object.authoredAircraft);
        record.boolean(object.alive);
        record.boolean(object.rubble);
        record.boolean(object.submerged);
        record.boolean(object.cloaked);
        record.integer(object.cloakCooldownTicks);
        record.integer(object.submergeCooldownTicks);
        record.boolean(object.deployed);
        record.integer(object.deployStateTicksRemaining);
        record.boolean(object.prone);
        record.integer(object.proneTicksRemaining);
        record.integer(object.weaponCooldownTicksRemaining);
        if (object.radiationEruptionCooldownTicksRemaining > 0) {
            record.string("RadiationEruptionCooldown/v1");
            record.integer(object.radiationEruptionCooldownTicksRemaining);
        }
        // These names predate the authoritative consumers: current runtime
        // vision, targeting, projectile origins, and movement completion all
        // read the interpolated pose directly.
        record.boolean(object.hasRenderPosition);
        record.quantizedDouble(object.renderX);
        record.quantizedDouble(object.renderY);
        record.boolean(object.renderMoveActive);
        record.quantizedDouble(object.renderLevel);
        record.boolean(object.renderOnBridge);
        // Buildup progress gates gameplay, unlike the render-only animation
        // clocks intentionally omitted below.
        record.integer(object.buildAnimTicksTotal);
        record.integer(object.buildAnimTicksRemaining);
        record.boolean(object.poweredOperational);
        // The exit deadline gates cloning-vat delivery, so the whole active
        // window must remain synchronized even though rendering also uses it.
        record.integer(object.factoryExitAnimationStartTick);
        record.integer(object.factoryExitAnimationEndTick);
        record.boolean(object.factoryExitAirborne);
        if (object.factoryExitHostId >= 0) {
            record.string("FactoryAirExit/v1");
            record.integer(object.factoryExitHostId);
        }
        record.integer(object.specialCashNextTick);
        record.integer(object.specialCloneDeliveryObjectId);
        // Manifest order is authoritative for unloading/gunner/garrison fire.
        record.orderedIntegers(object.transportPassengerIds);
        if (object.bioReactorAnimationStartTick >= 0) {
            record.string("BioReactorAnimation/v1");
            record.integer(object.bioReactorAnimationStartTick);
        }
        if (object.bioReactorAnimationPoweredStartTick >= 0) {
            record.string("BioReactorPoweredAnimation/v1");
            record.integer(object.bioReactorAnimationPoweredStartTick);
        }
        record.integer(object.activeGunnerMode);
        record.integer(object.activeGunnerWeaponIndex);
        record.integer(object.activeGunnerTurretIndex);
        record.integer(object.tankBunkerHostId);
        record.integer(object.tankBunkerOccupantId);
        record.integer(object.tankBunkerAnimationStartTick);
        record.boolean(object.tankBunkerLowering);
        record.integer(object.dockedToObjectId);
        record.integer(object.dockIndex);
        record.boolean(object.airborne);
        record.integer(object.flightLevel);
        record.integer(object.garrisonHostObjectId);
        record.orderedIntegers(object.garrisonOccupantIds);
        record.integer(object.garrisonFireCursor);
        record.integer(object.currentAmmo);
        // Preserve hashes for legacy objects with no manual mining override.
        if (object.harvestAutoSearchSuspended) {
            record.string("HarvesterManualMove/v1");
            record.boolean(true);
        }
        if (object.hasBeenCaptured) {
            record.string("CapturedCrew/v1");
            record.boolean(true);
        }
        objectRecords.push_back(record.value());
    }
    appendCanonicalCollection(
        canonical,
        "WorldObjects/v3",
        std::move(objectRecords)
    );

    std::vector<std::vector<std::uint8_t>> waypointRecords;
    waypointRecords.reserve(waypoints_.size());
    for (const WorldWaypoint& waypoint : waypoints_) {
        CanonicalSimulationRecord record;
        record.string(waypoint.id);
        record.integer(waypoint.x);
        record.integer(waypoint.y);
        record.integer(waypoint.cell);
        waypointRecords.push_back(record.value());
    }
    appendCanonicalCollection(
        canonical,
        "Waypoints/v1",
        std::move(waypointRecords)
    );

    // overlayCells_ is the immutable import mirror; terrainCells_ above is the
    // live resource/bridge authority. players_ and summary_ are derived counts,
    // warnings_ is diagnostic text. movementAnimationDistance, recoil/work/
    // power animation clocks, and the derived wall mask are render-only and
    // deliberately absent. Simulation-consumed pose and factory-exit state is
    // included above despite its legacy visual naming.
    return hashCanonicalBytes(canonical.value());
}

std::string toString(WorldObjectKind kind) {
    switch (kind) {
    case WorldObjectKind::Structure:
        return "Structure";
    case WorldObjectKind::Unit:
        return "Unit";
    case WorldObjectKind::Infantry:
        return "Infantry";
    }

    return "Unknown";
}
