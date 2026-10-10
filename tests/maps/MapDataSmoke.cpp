#include "world/model/GameWorld.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "data/art/TheaterDatabase.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "map data smoke failure: " << message << '\n';
    }
    return condition;
}

bool nearlyEqual(double left, double right) {
    return std::abs(left - right) <= 0.000000001;
}

const WorldObjectInstance* findObjectByType(
    const GameWorld& world,
    const std::string& type,
    bool alive
) {
    for (const WorldObjectInstance& object : world.getObjects()) {
        if (object.type == type && object.alive == alive) {
            return &object;
        }
    }
    return nullptr;
}

const MapTerrainObject* findMapTerrainByType(
    const MapDatabase& map,
    const std::string& type
) {
    for (const MapTerrainObject& object : map.getTerrainObjects()) {
        if (object.type == type) {
            return &object;
        }
    }
    return nullptr;
}

const WorldTerrainObject* findWorldTerrainByType(
    const GameWorld& world,
    const std::string& type
) {
    for (const WorldTerrainObject& object : world.getTerrainObjects()) {
        if (object.type == type) {
            return &object;
        }
    }
    return nullptr;
}

bool warningsContain(
    const std::vector<std::string>& warnings,
    const std::string& text
) {
    for (const std::string& warning : warnings) {
        if (warning.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: concord_map_data_smoke <map> <rules>\n";
        return 2;
    }

    IniFile mapIni;
    IniFile rulesIni;
    bool success = true;
    success &= expect(mapIni.loadFromFile(argv[1]), "map fixture did not load");
    success &= expect(rulesIni.loadFromFile(argv[2]), "rules fixture did not load");
    if (!success) {
        return 1;
    }

    MapDatabase map;
    success &= expect(map.loadFromIni(mapIni, &rulesIni), "MapDatabase load failed");
    success &= expect(
        map.getSummary().lightingSectionPresent
            && map.getSummary().lightingFieldCount == 19,
        "lighting summary"
    );
    const MapLighting& lighting = map.getLighting();
    success &= expect(
        lighting.sectionPresent
            && lighting.authoredFieldCount == 19
            && nearlyEqual(lighting.base.ambient, 0.75)
            && nearlyEqual(lighting.base.red, 0.80)
            && nearlyEqual(lighting.base.green, 0.90)
            && nearlyEqual(lighting.base.blue, 1.10)
            && nearlyEqual(lighting.base.ground, 0.05)
            && nearlyEqual(lighting.base.level, 0.012),
        "base lighting parse"
    );
    success &= expect(
        nearlyEqual(lighting.ion.ambient, 0.60)
            && nearlyEqual(lighting.ion.red, 0.70)
            && nearlyEqual(lighting.ion.green, 0.80)
            && nearlyEqual(lighting.ion.blue, 0.90)
            && nearlyEqual(lighting.ion.ground, 0.04)
            && nearlyEqual(lighting.ion.level, 0.020),
        "ion lighting parse"
    );
    success &= expect(
        nearlyEqual(lighting.dominator.ambient, 1.40)
            && nearlyEqual(lighting.dominator.red, 0.81)
            && nearlyEqual(lighting.dominator.green, 0.22)
            && nearlyEqual(lighting.dominator.blue, 0.33)
            && nearlyEqual(lighting.dominator.ground, 0.06)
            && nearlyEqual(lighting.dominator.level, 0.004)
            && nearlyEqual(lighting.dominatorAmbientChangeRate, 0.002),
        "Psychic Dominator lighting parse"
    );
    success &= expect(map.getStructures().size() == 3, "structure parse count");
    success &= expect(map.getUnits().size() == 2, "unit parse count");
    success &= expect(map.getInfantry().size() == 1, "infantry parse count");
    success &= expect(map.getAircraft().size() == 2, "aircraft parse count");
    success &= expect(map.getTerrainObjects().size() == 3, "terrain parse count");
    success &= expect(map.getSummary().smudgeCount == 2, "smudge summary count");
    success &= expect(map.getSmudges().size() == 2, "smudge parse count");
    if (map.getSmudges().size() == 2) {
        const MapSmudgeObject& crater = map.getSmudges()[0];
        const MapSmudgeObject& scorch = map.getSmudges()[1];
        success &= expect(
            crater.id == "0"
                && crater.type == "CR1"
                && crater.x == 13
                && crater.y == 25
                && crater.position.cell == (25 * 1000) + 13
                && crater.data == 3,
            "crater smudge fields and authored data"
        );
        success &= expect(
            scorch.id == "1"
                && scorch.type == "BURN01"
                && scorch.x == 14
                && scorch.y == 26
                && scorch.data == 0,
            "scorch smudge fields"
        );
    }

    IniFile invalidSmudgeIni;
    success &= expect(
        invalidSmudgeIni.loadFromString(
            "[Header]\n"
            "Width=1\n"
            "Height=1\n"
            "[Map]\n"
            "Theater=TEMPERATE\n"
            "Size=0,0,1,1\n"
            "LocalSize=0,0,1,1\n"
            "[Smudge]\n"
            "0=CR1,1\n"
            "1=CR1,bad,4,nope\n"
            "2=CR1,999,999,0\n"
            "3=,5,5,0\n"
            "4=CR1,5,5,1\n"
        ),
        "invalid smudge fixture parse"
    );
    MapDatabase invalidSmudgeMap;
    success &= expect(
        invalidSmudgeMap.loadFromIni(invalidSmudgeIni),
        "invalid smudge map load"
    );
    success &= expect(
        warningsContain(invalidSmudgeMap.getWarnings(),
            "Smudge [0] has too few fields")
            && warningsContain(invalidSmudgeMap.getWarnings(),
                "Smudge [1] x is not an integer")
            && warningsContain(invalidSmudgeMap.getWarnings(),
                "Smudge [1] data is not an integer")
            && warningsContain(invalidSmudgeMap.getWarnings(),
                "Smudge [2] coordinates are outside"),
        "malformed smudge warnings"
    );
    GameWorld invalidSmudgeWorld;
    success &= expect(
        invalidSmudgeWorld.loadFromMap(invalidSmudgeMap),
        "invalid smudge world load"
    );
    success &= expect(
        invalidSmudgeWorld.getSmudges().empty()
            && invalidSmudgeWorld.getSummary().smudges == 0
            && invalidSmudgeWorld.getSummary().countsMatchMap,
        "invalid, empty-type, and ignored smudges stay out of world"
    );

    IniFile theaterIni;
    success &= expect(
        theaterIni.loadFromString(
            "[TileSet0000]\n"
            "SetName=Clear\n"
            "FileName=Clear\n"
            "TilesInSet=1\n"
            "Morphable=yes\n"
            "[TileSet0001]\n"
            "SetName=Road\n"
            "FileName=Road\n"
            "TilesInSet=1\n"
        ),
        "theater morphable fixture parse"
    );
    TheaterDatabase theater;
    success &= expect(
        theater.loadFromIni(theaterIni, "temperatmd.ini")
            && theater.resolveTileIndex(0).morphable
            && !theater.resolveTileIndex(1).morphable,
        "TileSet Morphable semantics"
    );

    IniFile defaultLightingIni;
    success &= expect(
        defaultLightingIni.loadFromString(
            "[Header]\n"
            "Width=1\n"
            "Height=1\n"
            "[Map]\n"
            "Theater=TEMPERATE\n"
            "Size=0,0,1,1\n"
            "LocalSize=0,0,1,1\n"
        ),
        "default lighting fixture parse"
    );
    MapDatabase defaultLightingMap;
    success &= expect(
        defaultLightingMap.loadFromIni(defaultLightingIni),
        "default lighting map load"
    );
    const MapLighting& defaultLighting = defaultLightingMap.getLighting();
    success &= expect(
        !defaultLightingMap.getSummary().lightingSectionPresent
            && defaultLightingMap.getSummary().lightingFieldCount == 0
            && !defaultLighting.sectionPresent
            && defaultLighting.authoredFieldCount == 0
            && nearlyEqual(defaultLighting.base.ambient, 1.0)
            && nearlyEqual(defaultLighting.base.red, 1.0)
            && nearlyEqual(defaultLighting.base.green, 1.0)
            && nearlyEqual(defaultLighting.base.blue, 1.0)
            && nearlyEqual(defaultLighting.base.ground, 0.0)
            && nearlyEqual(defaultLighting.base.level, 0.032)
            && nearlyEqual(defaultLighting.ion.ambient, 1.0)
            && nearlyEqual(defaultLighting.ion.level, 0.032)
            && nearlyEqual(defaultLighting.dominator.ambient, 1.5)
            && nearlyEqual(defaultLighting.dominator.red, 0.85)
            && nearlyEqual(defaultLighting.dominator.green, 0.2)
            && nearlyEqual(defaultLighting.dominator.blue, 0.3)
            && nearlyEqual(defaultLighting.dominator.ground, 0.0)
            && nearlyEqual(defaultLighting.dominator.level, 0.0)
            && nearlyEqual(defaultLighting.dominatorAmbientChangeRate, 0.009),
        "FinalAlert scenario lighting defaults"
    );

    IniFile changedLightingIni;
    success &= expect(
        changedLightingIni.loadFromString(
            "[Header]\n"
            "Width=1\n"
            "Height=1\n"
            "[Map]\n"
            "Theater=TEMPERATE\n"
            "Size=0,0,1,1\n"
            "LocalSize=0,0,1,1\n"
            "[Lighting]\n"
            "Ambient=0.75\n"
        ),
        "changed lighting fixture parse"
    );
    MapDatabase changedLightingMap;
    success &= expect(
        changedLightingMap.loadFromIni(changedLightingIni),
        "changed lighting map load"
    );
    IniFile alternateLightingIni;
    success &= expect(
        alternateLightingIni.loadFromString(
            "[Header]\n"
            "Width=1\n"
            "Height=1\n"
            "[Map]\n"
            "Theater=TEMPERATE\n"
            "Size=0,0,1,1\n"
            "LocalSize=0,0,1,1\n"
            "[Lighting]\n"
            "Ambient=0.76\n"
        ),
        "alternate lighting fixture parse"
    );
    MapDatabase alternateLightingMap;
    success &= expect(
        alternateLightingMap.loadFromIni(alternateLightingIni),
        "alternate lighting map load"
    );
    const MapFacts changedLightingFacts = changedLightingMap.buildMapFacts();
    const MapFacts alternateLightingFacts =
        alternateLightingMap.buildMapFacts();
    success &= expect(
        changedLightingFacts.schemaVersion == "MapFacts.v8"
            && nearlyEqual(changedLightingFacts.lighting.base.ambient, 0.75)
            && changedLightingFacts.lighting.sectionPresent
            && alternateLightingFacts.lighting.sectionPresent
            && changedLightingFacts.lighting.authoredFieldCount == 1
            && alternateLightingFacts.lighting.authoredFieldCount == 1
            && changedLightingFacts.contentHash
                != alternateLightingFacts.contentHash,
        "lighting participates in MapFacts snapshot and content hash"
    );
    if (map.getAircraft().size() == 2) {
        const MapAircraftObject& aircraft = map.getAircraft().front();
        success &= expect(aircraft.id == "0", "aircraft id");
        success &= expect(aircraft.owner == "Neutral", "aircraft owner");
        success &= expect(aircraft.type == "TESTAIR", "aircraft type");
        success &= expect(aircraft.health == 192, "aircraft authored health");
        success &= expect(aircraft.strength == 200, "aircraft Strength lookup");
        success &= expect(aircraft.x == 10 && aircraft.y == 23, "aircraft position");
        success &= expect(aircraft.facing == 96, "aircraft authored facing");
        success &= expect(aircraft.mission == "Area Guard", "aircraft mission");
        success &= expect(aircraft.tag == "AIRTAG", "aircraft tag");
        success &= expect(aircraft.veterancy == 2, "aircraft veterancy");
    }

    const MapTerrainObject* mapTree = findMapTerrainByType(map, "TESTTREE");
    const MapTerrainObject* mapRock = findMapTerrainByType(map, "TESTROCK");
    const MapTerrainObject* mapImmune = findMapTerrainByType(map, "TESTIMMUNE");
    success &= expect(mapTree != nullptr, "tree terrain parsed");
    if (mapTree != nullptr) {
        success &= expect(mapTree->mapId == "24010" && mapTree->cell == "24010",
            "terrain map identity");
        success &= expect(mapTree->tag.empty(), "stock terrain tag is empty");
        success &= expect(mapTree->position.x == 10 && mapTree->position.y == 24,
            "terrain position decode");
        success &= expect(mapTree->strength == 200 && mapTree->health == 200,
            "tree TreeStrength fallback");
        success &= expect(mapTree->alive && !mapTree->immune,
            "tree initial health state");
        success &= expect(
            mapTree->temperateOccupationBits == 4
                && mapTree->snowOccupationBits == 6
                && mapTree->occupationBits == 4,
            "theater terrain occupation bits"
        );
        success &= expect(mapTree->occupiedSubCells == std::vector<int>({0, 1, 4}),
            "tree occupied subcells");
        success &= expect(
            mapTree->footprintWidth == 1 && mapTree->footprintHeight == 1,
            "terrain single-cell footprint"
        );
    }
    success &= expect(
        mapRock != nullptr
            && mapRock->strength == 450
            && mapRock->health == 450
            && mapRock->occupiedSubCells == std::vector<int>({0, 1, 2, 3, 4}),
        "rock explicit Strength and full occupation"
    );
    success &= expect(
        mapImmune != nullptr
            && mapImmune->strength == 200
            && mapImmune->health == 200
            && mapImmune->alive
            && mapImmune->immune
            && mapImmune->occupationBits == 0
            && mapImmune->occupiedSubCells == std::vector<int>({0, 1}),
        "zero Strength fallback, immunity, and partial occupation"
    );

    GameWorld world;
    success &= expect(world.loadFromMap(map), "GameWorld load failed");
    const GameWorldSummary& summary = world.getSummary();
    success &= expect(summary.terrainObjects == 3, "world terrain count");
    success &= expect(summary.smudges == 1, "world smudge count");
    success &= expect(summary.structures == 3, "world structure count");
    success &= expect(summary.units == 2, "world unit count");
    success &= expect(summary.infantry == 1, "world infantry count");
    success &= expect(summary.aircraft == 2, "world aircraft count");
    success &= expect(summary.objectInstances == 8, "world object instance count");
    success &= expect(summary.countsMatchMap, "world/map count validation");
    success &= expect(world.getSmudges().size() == 1, "world smudge storage");
    if (world.getSmudges().size() == 1) {
        const WorldSmudge& scorch = world.getSmudges()[0];
        success &= expect(
            scorch.mapId == "1"
                && scorch.type == "BURN01"
                && scorch.x == 14
                && scorch.y == 26
                && scorch.cell == (26 * 1000) + 14
                && scorch.level == 0
                && scorch.data == 0
                && scorch.authored,
            "non-ignored map smudge materialized into world"
        );
        world.accessSmudges()[0].data = 2;
        success &= expect(
            world.getSmudges()[0].data == 2,
            "runtime smudge mutable access"
        );
    }

    const WorldTerrainObject* worldTree =
        findWorldTerrainByType(world, "TESTTREE");
    const WorldTerrainObject* worldRock =
        findWorldTerrainByType(world, "TESTROCK");
    const WorldTerrainObject* worldImmune =
        findWorldTerrainByType(world, "TESTIMMUNE");
    success &= expect(
        worldTree != nullptr
            && worldTree->mapId == "24010"
            && worldTree->tag.empty()
            && worldTree->health == 200
            && worldTree->strength == 200
            && worldTree->alive
            && !worldTree->immune
            && worldTree->occupationBits == 4
            && worldTree->occupiedSubCells == std::vector<int>({0, 1, 4})
            && worldTree->footprintWidth == 1
            && worldTree->footprintHeight == 1,
        "tree world state"
    );
    success &= expect(
        worldRock != nullptr
            && worldRock->health == 450
            && worldRock->strength == 450,
        "rock world state"
    );
    success &= expect(
        worldImmune != nullptr
            && worldImmune->health == 200
            && worldImmune->alive
            && worldImmune->immune
            && worldImmune->occupationBits == 0
            && worldImmune->occupiedSubCells == std::vector<int>({0, 1}),
        "immune world terrain state"
    );

    const WorldObjectInstance* building =
        findObjectByType(world, "TESTBUILD", true);
    const WorldObjectInstance* rubble =
        findObjectByType(world, "TESTRUBBLE", false);
    const WorldObjectInstance* noRubble =
        findObjectByType(world, "TESTNORUBBLE", false);
    const WorldObjectInstance* fullUnit =
        findObjectByType(world, "TESTUNIT", true);
    const WorldObjectInstance* infantry =
        findObjectByType(world, "TESTINF", true);
    const WorldObjectInstance* aircraft =
        findObjectByType(world, "TESTAIR", true);
    const WorldObjectInstance* deadAircraft =
        findObjectByType(world, "TESTAIR", false);

    success &= expect(building != nullptr && building->health == 500,
        "128/256 building health conversion");
    success &= expect(rubble != nullptr && rubble->health == 0 && rubble->rubble,
        "Health=0 LeaveRubble structure state");
    success &= expect(noRubble != nullptr && !noRubble->rubble,
        "Health=0 non-rubble structure state");
    success &= expect(fullUnit != nullptr && fullUnit->health == 125,
        "full unit health conversion");
    success &= expect(infantry != nullptr && infantry->health == 25,
        "infantry health conversion");
    success &= expect(aircraft != nullptr && aircraft->health == 150,
        "aircraft health conversion");
    success &= expect(deadAircraft != nullptr && deadAircraft->health == 0,
        "Health=0 aircraft death state");

    if (aircraft != nullptr) {
        success &= expect(aircraft->authoredAircraft, "authored aircraft marker");
        success &= expect(!aircraft->airborne, "pre-hydration aircraft state");
        success &= expect(aircraft->flightLevel == aircraft->level,
            "pre-hydration terrain flight baseline");
        success &= expect(aircraft->mission == "Area Guard", "world aircraft mission");
        success &= expect(aircraft->tag == "AIRTAG", "world aircraft tag");
        success &= expect(aircraft->veterancy == 2, "world aircraft veterancy");
        success &= expect(aircraft->facing == 128, "world aircraft facing conversion");
    }

    success &= expect(world.getPlayers().size() == 1, "live player count");
    if (world.getPlayers().size() == 1) {
        const PlayerState& neutral = world.getPlayers().front();
        success &= expect(neutral.structureCount == 1, "live structure player count");
        success &= expect(neutral.unitCount == 2, "live unit player count");
        success &= expect(neutral.infantryCount == 1, "live infantry player count");
        success &= expect(neutral.aircraftCount == 1, "live aircraft player count");
        success &= expect(neutral.objectCount == 5, "live total player count");
    }

    if (!success) {
        return 1;
    }
    std::cout << "map data smoke passed\n";
    return 0;
}
