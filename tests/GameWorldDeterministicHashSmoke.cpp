#include "GameWorld.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "GameWorld deterministic hash smoke failed: "
                  << label << '\n';
    }
    return condition;
}

TerrainCell makeResourceTerrain() {
    TerrainCell terrain;
    terrain.x = 14;
    terrain.y = 9;
    terrain.cellIndex512 = 9 * 512 + 14;
    terrain.tileIndex = 31;
    terrain.subTileIndex = 2;
    terrain.level = 1;
    terrain.iceGrowth = 3;
    terrain.hasOverlay = true;
    terrain.overlayIndex = 105;
    terrain.overlayFrame = 8;
    terrain.overlayType = "GOLD01";
    terrain.overlayImage = "TIB01";
    terrain.overlayIsOre = true;
    return terrain;
}

TerrainCell makePlainTerrain() {
    TerrainCell terrain;
    terrain.x = 13;
    terrain.y = 9;
    terrain.cellIndex512 = 9 * 512 + 13;
    terrain.tileIndex = 30;
    terrain.subTileIndex = 1;
    return terrain;
}

WorldTerrainObject makeTree() {
    WorldTerrainObject object;
    object.mapId = "9014";
    object.type = "TREE01";
    object.tag = "TreeTrigger";
    object.x = 14;
    object.y = 9;
    object.cell = 9014;
    object.level = 1;
    object.strength = 250;
    object.health = 175;
    object.occupationBits = 5;
    object.occupiedSubCells = {4, 0, 2};
    object.footprintWidth = 2;
    object.footprintHeight = 1;
    return object;
}

WorldTerrainObject makeRock() {
    WorldTerrainObject object;
    object.mapId = "9013";
    object.type = "ROCK01";
    object.x = 13;
    object.y = 9;
    object.cell = 9013;
    object.strength = 400;
    object.health = 400;
    object.immune = true;
    return object;
}

WorldSmudge makeAuthoredSmudge() {
    WorldSmudge smudge;
    smudge.mapId = "17";
    smudge.type = "CR1";
    smudge.x = 13;
    smudge.y = 9;
    smudge.cell = 9013;
    smudge.level = 0;
    smudge.authored = true;
    return smudge;
}

WorldSmudge makeRuntimeSmudge() {
    WorldSmudge smudge;
    smudge.mapId = "runtime:40:9014:0";
    smudge.type = "SC2";
    smudge.x = 14;
    smudge.y = 9;
    smudge.cell = 9014;
    smudge.level = 1;
    return smudge;
}

WorldObjectInstance makeTank() {
    WorldObjectInstance object;
    object.instanceId = 7;
    object.kind = WorldObjectKind::Unit;
    object.mapId = "Unit7";
    object.owner = "Allies";
    object.type = "MTNK";
    object.health = 425;
    object.x = 14;
    object.y = 9;
    object.cell = 9014;
    object.level = 1;
    object.facing = 96;
    object.turretFacing = 64;
    object.mission = "Guard";
    object.tag = "PatrolA";
    object.veterancy = 1;
    object.veterancyExperience = 27;
    object.crateArmorMultiplier = 1.1250001;
    object.crateFirepowerMultiplier = 1.25;
    object.crateSpeedMultiplier = 1.5;
    object.crateCloakable = true;
    object.onBridge = true;
    object.cloaked = true;
    object.cloakCooldownTicks = 8;
    object.submergeCooldownTicks = 2;
    object.deployed = true;
    object.deployStateTicksRemaining = 73;
    object.weaponCooldownTicksRemaining = 5;
    object.buildAnimTicksTotal = 36;
    object.buildAnimTicksRemaining = 4;
    object.poweredOperational = true;
    object.specialCashNextTick = 300;
    object.specialCloneDeliveryObjectId = 11;
    object.transportPassengerIds = {11, 12};
    object.activeGunnerMode = 2;
    object.activeGunnerWeaponIndex = 1;
    object.activeGunnerTurretIndex = 3;
    object.dockedToObjectId = 20;
    object.dockIndex = 1;
    object.garrisonHostObjectId = 30;
    object.garrisonOccupantIds = {31, 32};
    object.garrisonFireCursor = 1;
    object.currentAmmo = 4;
    return object;
}

WorldObjectInstance makeInfantry() {
    WorldObjectInstance object;
    object.instanceId = 3;
    object.kind = WorldObjectKind::Infantry;
    object.mapId = "Inf3";
    object.owner = "Soviets";
    object.type = "E2";
    object.health = 125;
    object.x = 13;
    object.y = 9;
    object.cell = 9013;
    object.subCell = 2;
    object.facing = 160;
    object.mission = "Area Guard";
    object.prone = true;
    object.proneTicksRemaining = 9;
    object.airborne = true;
    object.flightLevel = 3;
    return object;
}

GameWorld makeWorld(bool reverseInsertion) {
    GameWorld world;

    std::vector<TerrainCell> terrain{makeResourceTerrain(), makePlainTerrain()};
    std::vector<WorldTerrainObject> terrainObjects{makeTree(), makeRock()};
    std::vector<WorldSmudge> smudges{makeAuthoredSmudge(), makeRuntimeSmudge()};
    std::vector<WorldObjectInstance> objects{makeTank(), makeInfantry()};
    if (reverseInsertion) {
        std::reverse(terrain.begin(), terrain.end());
        std::reverse(terrainObjects.begin(), terrainObjects.end());
        std::reverse(smudges.begin(), smudges.end());
        std::reverse(objects.begin(), objects.end());
        // Occupation is set membership, so construction order is irrelevant.
        std::reverse(terrainObjects.front().occupiedSubCells.begin(),
            terrainObjects.front().occupiedSubCells.end());
        std::reverse(terrainObjects.back().occupiedSubCells.begin(),
            terrainObjects.back().occupiedSubCells.end());
    }

    world.accessTerrainCells() = std::move(terrain);
    world.accessTerrainObjects() = std::move(terrainObjects);
    world.accessSmudges() = std::move(smudges);
    world.accessObjects() = std::move(objects);
    return world;
}

WorldObjectInstance& objectById(GameWorld& world, int objectId) {
    const auto found = std::find_if(
        world.accessObjects().begin(),
        world.accessObjects().end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return *found;
}

template <typename Mutator>
bool expectObjectMutationChanges(
    const GameWorld& baseline,
    std::uint64_t baselineHash,
    const char* fieldName,
    Mutator mutate
) {
    GameWorld changed = baseline;
    mutate(objectById(changed, 7));
    return expect(
        changed.deterministicSimulationStateHash() != baselineHash,
        std::string(fieldName) + " participates"
    );
}

template <typename Mutator>
bool expectObjectMutationDoesNotChangeHash(
    const GameWorld& baseline,
    std::uint64_t baselineHash,
    const char* fieldName,
    Mutator mutate
) {
    GameWorld changed = baseline;
    mutate(objectById(changed, 7));
    return expect(
        changed.deterministicSimulationStateHash() == baselineHash,
        std::string(fieldName) + " does not change the canonical hash"
    );
}

} // namespace

int main() {
    bool ok = true;
    const GameWorld baseline = makeWorld(false);
    const std::uint64_t baselineHash =
        baseline.deterministicSimulationStateHash();

    ok &= expect(
        baselineHash != 0
            && baselineHash == baseline.deterministicSimulationStateHash(),
        "repeated hashing is stable"
    );
    ok &= expect(
        makeWorld(true).deterministicSimulationStateHash() == baselineHash,
        "container insertion order and occupation-set order are canonical"
    );

    GameWorld resourceChanged = baseline;
    ++resourceChanged.accessTerrainCells().front().overlayFrame;
    ok &= expect(
        resourceChanged.deterministicSimulationStateHash() != baselineHash,
        "resource amount/frame participates"
    );

    GameWorld terrainObjectChanged = baseline;
    --terrainObjectChanged.accessTerrainObjects().front().health;
    ok &= expect(
        terrainObjectChanged.deterministicSimulationStateHash() != baselineHash,
        "terrain-object health participates"
    );

    GameWorld smudgeChanged = baseline;
    smudgeChanged.accessSmudges().front().data = 1;
    ok &= expect(
        smudgeChanged.deterministicSimulationStateHash() != baselineHash,
        "smudge authority participates"
    );

    // Every simulation-bearing WorldObjectInstance member is changed in
    // isolation. This makes omissions visible when fields are added or the
    // record layout is revised.
    ok &= expectObjectMutationChanges(baseline, baselineHash, "instanceId",
        [](WorldObjectInstance& object) { object.instanceId = 8; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "kind",
        [](WorldObjectInstance& object) {
            object.kind = WorldObjectKind::Structure;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "mapId",
        [](WorldObjectInstance& object) { object.mapId = "Unit8"; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "owner",
        [](WorldObjectInstance& object) { object.owner = "Soviets"; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "type",
        [](WorldObjectInstance& object) { object.type = "FV"; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "health",
        [](WorldObjectInstance& object) { --object.health; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "x",
        [](WorldObjectInstance& object) { ++object.x; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "y",
        [](WorldObjectInstance& object) { ++object.y; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "cell",
        [](WorldObjectInstance& object) { ++object.cell; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "level",
        [](WorldObjectInstance& object) { ++object.level; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "facing",
        [](WorldObjectInstance& object) { ++object.facing; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "turretFacing",
        [](WorldObjectInstance& object) { ++object.turretFacing; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "subCell",
        [](WorldObjectInstance& object) { ++object.subCell; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "mission case",
        [](WorldObjectInstance& object) { object.mission = "guard"; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "mission edge whitespace",
        [](WorldObjectInstance& object) { object.mission = " Guard "; }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "tag",
        [](WorldObjectInstance& object) { object.tag = "PatrolB"; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "veterancy",
        [](WorldObjectInstance& object) { ++object.veterancy; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "veterancyExperience",
        [](WorldObjectInstance& object) { ++object.veterancyExperience; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "crateArmorMultiplier",
        [](WorldObjectInstance& object) { object.crateArmorMultiplier += 0.01; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "crateFirepowerMultiplier",
        [](WorldObjectInstance& object) {
            object.crateFirepowerMultiplier += 0.01;
        }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "crateSpeedMultiplier",
        [](WorldObjectInstance& object) { object.crateSpeedMultiplier += 0.01; }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "crateCloakable",
        [](WorldObjectInstance& object) {
            object.crateCloakable = !object.crateCloakable;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "onBridge",
        [](WorldObjectInstance& object) { object.onBridge = !object.onBridge; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "harvestAutoSearchSuspended",
        [](WorldObjectInstance& object) { object.harvestAutoSearchSuspended = true; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "factoryExitHostId",
        [](WorldObjectInstance& object) { object.factoryExitHostId = 99; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "authoredAircraft",
        [](WorldObjectInstance& object) {
            object.authoredAircraft = !object.authoredAircraft;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "alive",
        [](WorldObjectInstance& object) { object.alive = !object.alive; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "rubble",
        [](WorldObjectInstance& object) { object.rubble = !object.rubble; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "submerged",
        [](WorldObjectInstance& object) { object.submerged = !object.submerged; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "cloaked",
        [](WorldObjectInstance& object) { object.cloaked = !object.cloaked; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "cloakCooldownTicks",
        [](WorldObjectInstance& object) { ++object.cloakCooldownTicks; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "submergeCooldownTicks",
        [](WorldObjectInstance& object) { ++object.submergeCooldownTicks; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "deployStateTicksRemaining",
        [](WorldObjectInstance& object) {
            ++object.deployStateTicksRemaining;
        }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "deployed",
        [](WorldObjectInstance& object) { object.deployed = !object.deployed; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "prone",
        [](WorldObjectInstance& object) { object.prone = !object.prone; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "proneTicksRemaining",
        [](WorldObjectInstance& object) { ++object.proneTicksRemaining; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "weaponCooldownTicksRemaining",
        [](WorldObjectInstance& object) {
            ++object.weaponCooldownTicksRemaining;
        }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "hasRenderPosition",
        [](WorldObjectInstance& object) {
            object.hasRenderPosition = !object.hasRenderPosition;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "renderX",
        [](WorldObjectInstance& object) { object.renderX += 0.25; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "renderY",
        [](WorldObjectInstance& object) { object.renderY -= 0.25; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "renderMoveActive",
        [](WorldObjectInstance& object) {
            object.renderMoveActive = !object.renderMoveActive;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "renderLevel",
        [](WorldObjectInstance& object) { object.renderLevel += 0.5; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "renderOnBridge",
        [](WorldObjectInstance& object) {
            object.renderOnBridge = !object.renderOnBridge;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "buildAnimTicksTotal",
        [](WorldObjectInstance& object) { ++object.buildAnimTicksTotal; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "buildAnimTicksRemaining",
        [](WorldObjectInstance& object) { ++object.buildAnimTicksRemaining; }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "poweredOperational",
        [](WorldObjectInstance& object) {
            object.poweredOperational = !object.poweredOperational;
        });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "factoryExitAnimationStartTick",
        [](WorldObjectInstance& object) {
            ++object.factoryExitAnimationStartTick;
        }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "factoryExitAnimationEndTick",
        [](WorldObjectInstance& object) { ++object.factoryExitAnimationEndTick; }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "factoryExitAirborne",
        [](WorldObjectInstance& object) {
            object.factoryExitAirborne = !object.factoryExitAirborne;
        });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "specialCashNextTick",
        [](WorldObjectInstance& object) { ++object.specialCashNextTick; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "specialCloneDeliveryObjectId",
        [](WorldObjectInstance& object) {
            ++object.specialCloneDeliveryObjectId;
        }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "transportPassengerIds",
        [](WorldObjectInstance& object) {
            object.transportPassengerIds.push_back(13);
        }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "activeGunnerMode",
        [](WorldObjectInstance& object) { ++object.activeGunnerMode; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "activeGunnerWeaponIndex",
        [](WorldObjectInstance& object) { ++object.activeGunnerWeaponIndex; }
    );
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "activeGunnerTurretIndex",
        [](WorldObjectInstance& object) { ++object.activeGunnerTurretIndex; }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "dockedToObjectId",
        [](WorldObjectInstance& object) { ++object.dockedToObjectId; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "dockIndex",
        [](WorldObjectInstance& object) { ++object.dockIndex; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "airborne",
        [](WorldObjectInstance& object) { object.airborne = !object.airborne; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "flightLevel",
        [](WorldObjectInstance& object) { ++object.flightLevel; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "garrisonHostObjectId",
        [](WorldObjectInstance& object) { ++object.garrisonHostObjectId; });
    ok &= expectObjectMutationChanges(
        baseline,
        baselineHash,
        "garrisonOccupantIds",
        [](WorldObjectInstance& object) {
            object.garrisonOccupantIds.push_back(33);
        }
    );
    ok &= expectObjectMutationChanges(baseline, baselineHash, "garrisonFireCursor",
        [](WorldObjectInstance& object) { ++object.garrisonFireCursor; });
    ok &= expectObjectMutationChanges(baseline, baselineHash, "currentAmmo",
        [](WorldObjectInstance& object) { ++object.currentAmmo; });

    GameWorld manifestOrderChanged = baseline;
    std::reverse(
        objectById(manifestOrderChanged, 7).garrisonOccupantIds.begin(),
        objectById(manifestOrderChanged, 7).garrisonOccupantIds.end()
    );
    ok &= expect(
        manifestOrderChanged.deterministicSimulationStateHash() != baselineHash,
        "authoritative manifest order participates"
    );

    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "owner identifier normalization",
        [](WorldObjectInstance& object) { object.owner = "  aLlIeS\t"; }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "type identifier normalization",
        [](WorldObjectInstance& object) { object.type = "mTnK "; }
    );

    GameWorld subQuantumDouble = baseline;
    objectById(subQuantumDouble, 7).crateArmorMultiplier += 0.0000002;
    ok &= expect(
        subQuantumDouble.deterministicSimulationStateHash() == baselineHash,
        "sub-quantum floating-point noise is ignored"
    );

    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "movementAnimationDistance",
        [](WorldObjectInstance& object) {
            object.movementAnimationDistance = 800.5;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "structureRecoilStartTick",
        [](WorldObjectInstance& object) {
            object.structureRecoilStartTick = 700;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "chargeTurretStartTick",
        [](WorldObjectInstance& object) {
            object.chargeTurretStartTick = 700;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "workAnimationActive",
        [](WorldObjectInstance& object) {
            object.workAnimationActive = !object.workAnimationActive;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "powerAnimationPausedAtTick",
        [](WorldObjectInstance& object) {
            object.powerAnimationPausedAtTick = 600;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "powerAnimationPausedTicks",
        [](WorldObjectInstance& object) {
            object.powerAnimationPausedTicks = 50;
        }
    );
    ok &= expectObjectMutationDoesNotChangeHash(
        baseline,
        baselineHash,
        "wallConnectionMask",
        [](WorldObjectInstance& object) { object.wallConnectionMask = 15; }
    );

    if (!ok) {
        return 1;
    }
    std::cout << "GameWorld deterministic hash smoke passed: hash="
              << baselineHash << '\n';
    return 0;
}
