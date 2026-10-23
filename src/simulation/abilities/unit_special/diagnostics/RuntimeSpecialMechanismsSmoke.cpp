#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismsPrivate.h"
#include "simulation/buildings/RuntimeStructureRepack.h"
#include "simulation/abilities/radiation/RuntimeRadiation.h"
#include "simulation/abilities/unit_special/RuntimeSpecialDetail.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace {
using runtime_special_mechanisms_detail::boolText;
using runtime_special_mechanisms_detail::objectCell;
using runtime_special_mechanisms_detail::kMaximumTimedBombRecords;
} // namespace

namespace {

WorldObjectInstance smokeObject(
    int id,
    WorldObjectKind kind,
    const std::string& owner,
    const std::string& type,
    int x,
    int y
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = kind;
    object.owner = owner;
    object.type = type;
    object.health = 100;
    object.x = x;
    object.y = y;
    object.cell = x + y * 512;
    object.alive = true;
    object.mission = "Guard";
    return object;
}

bool smokeAllied(const std::string& left, const std::string& right) {
    return left == right;
}

RulesDatabase smokeRules() {
    IniFile ini;
    ini.loadFromString(
        "[General]\n"
        "ChronoDelay=3\n"
        "ChronoMinimumDelay=2\n"
        "ChronoDistanceFactor=64\n"
        "ChronoTrigger=yes\n"
        "[AudioVisual]\n"
        "ChronoInSound=ChronoIn\n"
        "ChronoOutSound=ChronoOut\n"
        "YuriMindControlSound=YuriMindControl\n"
        "ChronoSparkle1=CHRONOSK\n"
        "WarpIn=WARPIN\n"
        "WarpOut=WARPOUT\n"
        "WarpAway=WARPAWAY\n"
        "[CombatDamage]\n"
        "MindControlAttackLineFrames=2\n"
        "DisguiseDelay=1\n"
        "DisguiseRearmDelay=1\n"
        "C4Delay=.03\n"
        "C4Warhead=Super\n"
        "IvanWarhead=IvanWH\n"
        "IvanDamage=450\n"
        "IvanTimedDelay=15\n"
        "[Radiation]\n"
        "RadDurationMultiple=2\n"
        "RadApplicationDelay=2\n"
        "RadLevelMax=10\n"
        "RadLevelDelay=4\n"
        "RadLevelFactor=0.2\n"
        "RadColor=0,255,0\n"
        "RadSiteWarhead=RadSite\n"
        "[InfantryTypes]\n"
        "0=CONTROL\n"
        "1=TARGET\n"
        "2=IMMUNE\n"
        "3=C4UNIT\n"
        "4=IVAN\n"
        "5=ENGINEER\n"
        "6=SPY\n"
        "[VehicleTypes]\n"
        "0=MIRAGE\n"
        "1=CHRONO\n"
        "2=CARRIER\n"
        "3=TDRONE\n"
        "4=PARASITEHOST\n"
        "5=SAFEHOST\n"
        "[AircraftTypes]\n"
        "0=CHILD\n"
        "[WeaponTypes]\n"
        "0=MINDGUN\n"
        "1=TEMPGUN\n"
        "2=ELECGUN\n"
        "3=RADGUN\n"
        "4=CHARGEGUN\n"
        "5=PARASITEGUN\n"
        "6=DISARMGUN\n"
        "[WarheadTypes]\n"
        "0=MindWH\n"
        "1=TempWH\n"
        "2=ElecWH\n"
        "3=RadSite\n"
        "4=ChargeWH\n"
        "5=ParasiteWH\n"
        "6=DisarmWH\n"
        "7=IvanWH\n"
        "8=Super\n"
        "[BuildingTypes]\n"
        "0=OVERPOWER\n"
        "1=BOMBTARGET\n"
        "2=NOC4TARGET\n"
        "[CONTROL]\n"
        "Strength=100\n"
        "Primary=MINDGUN\n"
        "ImmuneToPsionics=yes\n"
        "[TARGET]\n"
        "Strength=100\n"
        "ImmuneToPsionics=no\n"
        "[IMMUNE]\n"
        "Strength=100\n"
        "ImmuneToPsionics=yes\n"
        "[C4UNIT]\n"
        "Strength=100\n"
        "C4=yes\n"
        "[IVAN]\n"
        "Strength=100\n"
        "Ivan=yes\n"
        "[ENGINEER]\n"
        "Strength=100\n"
        "Engineer=yes\n"
        "Primary=DISARMGUN\n"
        "[SPY]\n"
        "Strength=100\n"
        "Agent=yes\n"
        "Infiltrate=yes\n"
        "CanDisguise=yes\n"
        "DisguiseWhenStill=yes\n"
        "PermaDisguise=yes\n"
        "[MIRAGE]\n"
        "Strength=100\n"
        "Speed=5\n"
        "DisguiseWhenStill=yes\n"
        "CanDisguise=yes\n"
        "[CHRONO]\n"
        "Strength=100\n"
        "Speed=5\n"
        "Teleporter=yes\n"
        "[CARRIER]\n"
        "Strength=200\n"
        "Spawns=CHILD\n"
        "SpawnsNumber=2\n"
        "SpawnRegenRate=3\n"
        "SpawnReloadRate=2\n"
        "[CHILD]\n"
        "Strength=50\n"
        "Speed=10\n"
        "Spawned=yes\n"
        "ImmuneToPsionics=yes\n"
        "[TDRONE]\n"
        "Strength=100\n"
        "Speed=10\n"
        "Organic=no\n"
        "Primary=PARASITEGUN\n"
        "[PARASITEHOST]\n"
        "Strength=200\n"
        "Speed=5\n"
        "Parasiteable=yes\n"
        "[SAFEHOST]\n"
        "Strength=200\n"
        "Speed=5\n"
        "Parasiteable=no\n"
        "[MINDGUN]\n"
        "Damage=1\n"
        "Range=7\n"
        "Projectile=NONE\n"
        "Warhead=MindWH\n"
        "[TEMPGUN]\n"
        "Damage=5\n"
        "Range=7\n"
        "Projectile=NONE\n"
        "Warhead=TempWH\n"
        "[ELECGUN]\n"
        "Damage=5\n"
        "Range=7\n"
        "Projectile=NONE\n"
        "Warhead=ElecWH\n"
        "IsElectricBolt=yes\n"
        "[RADGUN]\n"
        "Damage=5\n"
        "Range=7\n"
        "Projectile=NONE\n"
        "Warhead=RadSite\n"
        "IsRadBeam=yes\n"
        "[CHARGEGUN]\n"
        "Damage=1\n"
        "Range=2\n"
        "Projectile=NONE\n"
        "Warhead=ChargeWH\n"
        "IsElectricBolt=yes\n"
        "IsAlternateColor=yes\n"
        "[PARASITEGUN]\n"
        "Damage=35\n"
        "ROF=6\n"
        "Range=1.5\n"
        "Projectile=NONE\n"
        "Warhead=ParasiteWH\n"
        "LimboLaunch=yes\n"
        "[DISARMGUN]\n"
        "Damage=1\n"
        "Range=1.5\n"
        "Projectile=NONE\n"
        "Warhead=DisarmWH\n"
        "[MindWH]\n"
        "Verses=100%,100%,100%,100%,100%,100%,0%,0%,0%,100%,100%\n"
        "MindControl=yes\n"
        "AffectsAllies=no\n"
        "[TempWH]\n"
        "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
        "Temporal=yes\n"
        "AffectsAllies=no\n"
        "[ElecWH]\n"
        "Verses=0%,0%,0%,0%,0%,0%,100%,100%,100%,50%,100%\n"
        "ElectricAssault=yes\n"
        "AffectsAllies=no\n"
        "[RadSite]\n"
        "Verses=100%,100%,100%,50%,10%,10%,0%,0%,0%,100%,100%\n"
        "Radiation=yes\n"
        "AffectsAllies=no\n"
        "[ChargeWH]\n"
        "Verses=0%,0%,0%,0%,0%,0%,100%,100%,100%,100%,100%\n"
        "ElectricAssault=yes\n"
        "AffectsAllies=yes\n"
        "[ParasiteWH]\n"
        "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
        "Parasite=yes\n"
        "Culling=yes\n"
        "Paralyzes=32767\n"
        "[DisarmWH]\n"
        "Verses=0%,0%,0%,0%,0%,0%,0%,0%,0%,0%,0%\n"
        "BombDisarm=yes\n"
        "[IvanWH]\n"
        "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
        "IvanBomb=yes\n"
        "[Super]\n"
        "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
        "[OVERPOWER]\n"
        "Strength=200\n"
        "Powered=yes\n"
        "Overpowerable=yes\n"
        "[BOMBTARGET]\n"
        "Strength=500\n"
        "CanC4=yes\n"
        "Bombable=yes\n"
        "[NOC4TARGET]\n"
        "Strength=500\n"
        "CanC4=no\n"
        "Bombable=yes\n"
    );
    RulesDatabase rules;
    rules.loadFromIni(ini);
    return rules;
}

bool smokeMindControl(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(1, WorldObjectKind::Infantry, "A", "CONTROL", 2, 2),
        smokeObject(2, WorldObjectKind::Infantry, "B", "TARGET", 3, 2),
        smokeObject(3, WorldObjectKind::Infantry, "B", "IMMUNE", 4, 2),
        smokeObject(4, WorldObjectKind::Infantry, "B", "TARGET", 5, 2)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    std::string reason;
    const bool success = state.tryMindControl(
        objects,
        rules,
        1,
        2,
        1,
        false,
        false,
        false,
        1,
        smokeAllied,
        sink,
        reason
    );
    const bool immuneRejected = !state.tryMindControl(
        objects,
        rules,
        1,
        3,
        1,
        false,
        false,
        false,
        2,
        smokeAllied,
        sink,
        reason
    );
    const bool capacityRejected = !state.tryMindControl(
        objects,
        rules,
        1,
        4,
        1,
        false,
        false,
        false,
        2,
        smokeAllied,
        sink,
        reason
    );
    const bool ownerChanged = objects[1].owner == "A";
    const std::uint64_t stateHashBeforeRelease = state.stateHash();
    const std::string serialized = [&state]() {
        std::ostringstream output;
        state.save(output);
        return output.str();
    }();
    IniFile save;
    const bool saveLoaded = save.loadFromString(serialized);
    RuntimeSpecialMechanismState restored;
    restored.reset(rules.getSpecialMechanismRules());
    std::string loadError;
    const bool restoredOk = saveLoaded && restored.load(save, loadError)
        && restored.mindControlLinks().size() == 1
        && restored.stateHash() == stateHashBeforeRelease;
    const bool released = state.releaseMindControlByController(
        objects,
        1,
        3,
        sink,
        "controller removed"
    ) && objects[1].owner == "B";
    RuntimeSpecialMechanismState permanentState;
    permanentState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> permanentObjects{
        smokeObject(5, WorldObjectKind::Infantry, "A", "CONTROL", 2, 3),
        smokeObject(6, WorldObjectKind::Infantry, "B", "TARGET", 3, 3)
    };
    const bool permanentEstablished = permanentState.tryMindControl(
        permanentObjects,
        rules,
        5,
        6,
        1,
        true,
        false,
        false,
        1,
        smokeAllied,
        sink,
        reason
    );
    permanentState.onObjectRemoved(
        permanentObjects,
        5,
        2,
        sink,
        "permanent controller removed"
    );
    const bool permanentKept = permanentEstablished
        && permanentObjects[1].owner == "A"
        && permanentState.mindControlLinks().size() == 1;
    detail = "success=" + boolText(success)
        + " immune=" + boolText(immuneRejected)
        + " capacity=" + boolText(capacityRejected)
        + " save=" + boolText(restoredOk)
        + " release=" + boolText(released)
        + " permanent=" + boolText(permanentKept)
        + " hash=" + std::to_string(stateHashBeforeRelease);
    return success && immuneRejected && capacityRejected && ownerChanged
        && restoredOk && released && permanentKept;
}

std::uint64_t stateHashForSmokeMindControl(const RulesDatabase& rules) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(1, WorldObjectKind::Infantry, "A", "CONTROL", 2, 2),
        smokeObject(2, WorldObjectKind::Infantry, "B", "TARGET", 3, 2)
    };
    std::string reason;
    state.tryMindControl(
        objects,
        rules,
        1,
        2,
        1,
        false,
        false,
        false,
        1,
        smokeAllied,
        [](const SpecialMechanismEvent&) {},
        reason
    );
    return state.stateHash();
}

bool smokeCamouflage(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(10, WorldObjectKind::Unit, "A", "MIRAGE", 2, 2),
        smokeObject(11, WorldObjectKind::Infantry, "A", "SPY", 3, 2)
    };
    std::vector<SpecialMechanismEvent> events;
    const auto sink = [&events](const SpecialMechanismEvent& event) {
        events.push_back(event);
    };
    state.notifyActivity(10, 0, "spawn");
    state.notifyActivity(11, 0, "spawn");
    state.tickCamouflage(objects, rules, 1, [](int) { return true; }, sink);
    const bool entered = state.isCamouflaged(10);
    const bool spyExcludedAfterSpawn = !state.isCamouflaged(11)
        && !state.camouflageStates().contains(11)
        && std::none_of(
            events.begin(),
            events.end(),
            [](const SpecialMechanismEvent& event) {
                return event.objectId == 11;
            }
        );
    state.notifyActivity(10, 2, "move");
    state.notifyActivity(11, 2, "move");
    state.tickCamouflage(objects, rules, 2, [](int) { return false; }, sink);
    const bool movedOut = !state.isCamouflaged(10);
    const bool spyExcludedAfterMove = !state.isCamouflaged(11)
        && !state.camouflageStates().contains(11);
    state.tickCamouflage(objects, rules, 3, [](int) { return true; }, sink);
    const bool reentered = state.isCamouflaged(10);
    const bool spyExcluded = spyExcludedAfterSpawn
        && spyExcludedAfterMove
        && !state.isCamouflaged(11)
        && !state.camouflageStates().contains(11)
        && std::none_of(
            events.begin(),
            events.end(),
            [](const SpecialMechanismEvent& event) {
                return event.objectId == 11;
            }
        );
    const std::uint64_t camouflageHash = state.stateHash();
    std::ostringstream camouflageSaveText;
    state.save(camouflageSaveText);
    IniFile camouflageSave;
    RuntimeSpecialMechanismState camouflageRestored;
    camouflageRestored.reset(rules.getSpecialMechanismRules());
    std::string camouflageLoadError;
    const bool camouflageSaveRestored = camouflageSave.loadFromString(
            camouflageSaveText.str()
        )
        && camouflageRestored.load(camouflageSave, camouflageLoadError)
        && camouflageRestored.stateHash() == camouflageHash
        && !camouflageRestored.isCamouflaged(11)
        && !camouflageRestored.camouflageStates().contains(11);
    state.notifyActivity(10, 4, "fire");
    const bool fireBlinks = state.isCamouflaged(10)
        && state.isCamouflageBlinking(10, 4);
    const auto blinkSnapshots = state.renderSnapshots(4);
    const bool blinkShowsBody = std::none_of(blinkSnapshots.begin(), blinkSnapshots.end(),
        [](const auto& snapshot) { return snapshot.kind == "camouflage" && snapshot.objectId == 10; });
    std::ostringstream blinkSaveText;
    state.save(blinkSaveText);
    IniFile blinkSave;
    const bool blinkRestored = blinkSave.loadFromString(blinkSaveText.str())
        && camouflageRestored.load(blinkSave, camouflageLoadError)
        && camouflageRestored.stateHash() == state.stateHash()
        && camouflageRestored.isCamouflaged(10)
        && camouflageRestored.isCamouflageBlinking(10, 4);
    const auto disguise = state.camouflageStates().find(10);
    const bool authoredDisguise = disguise != state.camouflageStates().end()
        && disguise->second.disguiseType == "TREE03";
    detail = "entered=" + boolText(entered)
        + " movedOut=" + boolText(movedOut)
        + " reentered=" + boolText(reentered)
        + " save=" + boolText(camouflageSaveRestored)
        + " fireBlinks=" + boolText(fireBlinks)
        + " blinkSave=" + boolText(blinkRestored)
        + " spyExcluded=" + boolText(spyExcluded)
        + " disguise=" + (disguise == state.camouflageStates().end()
            ? std::string("missing")
            : disguise->second.disguiseType);
    return entered && movedOut && reentered && camouflageSaveRestored
        && fireBlinks && blinkShowsBody && blinkRestored && authoredDisguise && spyExcluded;
}

bool smokeChronoAndTemporal(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(20, WorldObjectKind::Unit, "A", "CHRONO", 1, 1),
        smokeObject(21, WorldObjectKind::Unit, "B", "TARGET", 8, 1),
        smokeObject(22, WorldObjectKind::Infantry, "A", "CONTROL", 2, 1)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    const auto legal = [](int x, int y, int) { return x >= 0 && y >= 0; };
    const auto occupied = [](int, int, int) { return false; };
    std::string reason;
    const bool requested = state.requestChronoMove(
        objects,
        rules,
        20,
        {5, 5},
        0,
        0,
        legal,
        occupied,
        sink,
        reason
    );
    const std::uint64_t chronoHash = state.stateHash();
    std::ostringstream chronoSaveText;
    state.save(chronoSaveText);
    IniFile chronoSave;
    RuntimeSpecialMechanismState chronoRestored;
    chronoRestored.reset(rules.getSpecialMechanismRules());
    std::string chronoLoadError;
    const bool chronoSaveRestored = chronoSave.loadFromString(
            chronoSaveText.str()
        )
        && chronoRestored.load(chronoSave, chronoLoadError)
        && chronoRestored.stateHash() == chronoHash;
    const bool held = objects[0].x == 1;
    state.tickChronoMoves(objects, 40, legal, occupied, sink);
    const bool moved = objects[0].x == 5 && objects[0].y == 5;
    const bool illegalRejected = !state.requestChronoMove(
        objects,
        rules,
        20,
        {-1, 0},
        0,
        41,
        legal,
        occupied,
        sink,
        reason
    );
    bool completed = false;
    const bool temporalFirst = state.applyTemporalHit(
        objects,
        rules,
        22,
        21,
        40,
        100,
        50,
        smokeAllied,
        sink,
        completed,
        reason
    );
    const std::uint64_t temporalHash = state.stateHash();
    std::ostringstream temporalSaveText;
    state.save(temporalSaveText);
    IniFile temporalSave;
    RuntimeSpecialMechanismState temporalRestored;
    temporalRestored.reset(rules.getSpecialMechanismRules());
    std::string temporalLoadError;
    const bool temporalSaveRestored = temporalSave.loadFromString(
            temporalSaveText.str()
        )
        && temporalRestored.load(temporalSave, temporalLoadError)
        && temporalRestored.stateHash() == temporalHash;
    const bool temporalSecond = state.applyTemporalHit(
        objects,
        rules,
        22,
        21,
        60,
        100,
        51,
        smokeAllied,
        sink,
        completed,
        reason
    ) && completed && objects[1].health == 0;
    // Chronosphere recovery applies even to a non-teleporter and survives
    // save/load with precisely the same unlock tick (3 frames at 30 Hz).
    auto recoveryObject = smokeObject(23, WorldObjectKind::Unit, "A", "TARGET", 7, 7);
    RuntimeSpecialMechanismState recovery;
    recovery.reset(rules.getSpecialMechanismRules());
    recovery.beginChronosphereRecovery(recoveryObject, 100, 30);
    std::ostringstream recoveryText;
    recovery.save(recoveryText);
    IniFile recoveryIni;
    const bool recoverySaved = recoveryIni.loadFromString(recoveryText.str())
        && chronoRestored.load(recoveryIni, chronoLoadError)
        && chronoRestored.stateHash() == recovery.stateHash();
    std::vector<WorldObjectInstance> recoveryObjects{recoveryObject};
    const bool recoveryHeld = recoveryObject.mission == "ChronoFrozen"
        && !chronoRestored.isChronoInTransit(23)
        && chronoRestored.isChronoMovementLocked(23, 105);
    chronoRestored.tickChronoMoves(recoveryObjects, 106, legal, occupied, sink);
    const bool recoveryReleased = !chronoRestored.isChronoMovementLocked(23, 106)
        && recoveryObjects.front().mission == "Guard";
    detail = "requested=" + boolText(requested)
        + " held=" + boolText(held)
        + " moved=" + boolText(moved)
        + " illegal=" + boolText(illegalRejected)
        + " chronoSave=" + boolText(chronoSaveRestored)
        + " temporal=" + boolText(temporalFirst && temporalSecond)
        + " temporalSave=" + boolText(temporalSaveRestored)
        + " sphereRecovery=" + boolText(recoverySaved && recoveryHeld && recoveryReleased);
    return requested && held && moved && illegalRejected
        && chronoSaveRestored && temporalFirst && temporalSecond
        && temporalSaveRestored && recoverySaved && recoveryHeld && recoveryReleased;
}

bool smokeElectricRadiation(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(30, WorldObjectKind::Unit, "A", "CARRIER", 1, 1),
        smokeObject(31, WorldObjectKind::Infantry, "B", "TARGET", 2, 1),
        smokeObject(32, WorldObjectKind::Structure, "B", "UNKNOWN", 4, 4)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    state.addElectricArc(
        30,
        31,
        {1.0, 1.0, 0.0},
        {2.0, 1.0, 0.0},
        "128,200,255",
        false,
        false,
        0,
        3,
        sink
    );
    state.disableElectricTarget(32, 3, 0, sink);
    const bool disabled = state.isElectricDisabled(32, 1);
    const int areaId = state.spawnRadiationArea(
        30,
        "A",
        {2, 1},
        2,
        10,
        5,
        "RadSite",
        0,
        true,
        sink
    );
    const int repeatedAreaId = state.spawnRadiationArea(
        30,
        "A",
        {2, 1},
        2,
        10,
        5,
        "RadSite",
        0,
        true,
        sink
    );
    const bool repeatedSiteMerged = repeatedAreaId == areaId
        && state.radiationAreas().size() == 1U;
    const auto damage = state.tickRadiation(
        objects,
        rules,
        0,
        smokeAllied,
        [](int) { return true; },
        sink
    );
    const bool damaged = std::any_of(
        damage.begin(),
        damage.end(),
        [](const RadiationDamageEvent& event) {
            return event.targetObjectId == 31 && event.damage > 0;
        }
    );
    state.tickElectric(3, sink);
    const bool restored = !state.isElectricDisabled(32, 3);
    detail = "disabled=" + boolText(disabled)
        + " damaged=" + boolText(damaged)
        + " merged=" + boolText(repeatedSiteMerged)
        + " restored=" + boolText(restored)
        + " areas=" + std::to_string(state.radiationAreas().size());
    return disabled && damaged && repeatedSiteMerged && restored;
}

bool smokeElectricOnly(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(30, WorldObjectKind::Unit, "A", "CARRIER", 1, 1),
        smokeObject(32, WorldObjectKind::Structure, "A", "OVERPOWER", 2, 2)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    state.addElectricArc(
        30,
        31,
        {1.0, 1.0, 0.0},
        {2.0, 1.0, 0.0},
        "128,200,255",
        false,
        false,
        0,
        3,
        sink
    );
    state.disableElectricTarget(32, 3, 0, sink);
    const bool disabled = state.isElectricDisabled(32, 1);
    std::string chargeReason;
    const bool chargeStarted = state.applyElectricCharge(
        objects,
        rules,
        30,
        32,
        0,
        5,
        2,
        smokeAllied,
        sink,
        chargeReason
    );
    const bool charged = state.isElectricCharged(32, 1);
    const std::uint64_t chargeHash = state.stateHash();
    std::ostringstream chargeSaveText;
    state.save(chargeSaveText);
    IniFile chargeSave;
    RuntimeSpecialMechanismState chargeRestored;
    chargeRestored.reset(rules.getSpecialMechanismRules());
    std::string chargeLoadError;
    const bool chargeSaveRestored = chargeSave.loadFromString(
            chargeSaveText.str()
        )
        && chargeRestored.load(chargeSave, chargeLoadError)
        && chargeRestored.stateHash() == chargeHash
        && chargeRestored.isElectricCharged(32, 1);
    state.tickElectric(3, sink);
    const bool restored = !state.isElectricDisabled(32, 3);
    state.tickElectric(5, sink);
    const bool chargeExpired = !state.isElectricCharged(32, 5);
    detail = "disabled=" + boolText(disabled)
        + " charge=" + boolText(chargeStarted && charged)
        + " chargeSave=" + boolText(chargeSaveRestored)
        + " restored=" + boolText(restored)
        + " chargeExpired=" + boolText(chargeExpired)
        + " arcs=" + std::to_string(state.stateHash());
    return disabled && chargeStarted && charged && chargeSaveRestored
        && restored && chargeExpired;
}

bool smokeRadiationOnly(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(30, WorldObjectKind::Unit, "A", "CARRIER", 1, 1),
        smokeObject(31, WorldObjectKind::Infantry, "B", "TARGET", 2, 1),
        smokeObject(32, WorldObjectKind::Structure, "B", "UNKNOWN", 4, 4)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    const int areaId = state.spawnRadiationArea(
        30,
        "A",
        {2, 1},
        2,
        10,
        5,
        "RadSite",
        0,
        true,
        sink
    );
    const int repeatedAreaId = state.spawnRadiationArea(
        30,
        "A",
        {2, 1},
        2,
        10,
        5,
        "RadSite",
        0,
        true,
        sink
    );
    const bool repeatedSiteMerged = repeatedAreaId == areaId
        && state.radiationAreas().size() == 1U;
    const auto damage = state.tickRadiation(
        objects,
        rules,
        0,
        smokeAllied,
        [](int) { return true; },
        sink
    );
    const bool damaged = std::any_of(
        damage.begin(),
        damage.end(),
        [](const RadiationDamageEvent& event) {
            return event.targetObjectId == 31 && event.damage > 0;
        }
    );
    const std::uint64_t hash = state.stateHash();
    std::ostringstream saveText;
    state.save(saveText);
    IniFile save;
    RuntimeSpecialMechanismState restored;
    restored.reset(rules.getSpecialMechanismRules());
    std::string loadError;
    const bool saveRestored = save.loadFromString(saveText.str())
        && restored.load(save, loadError)
        && restored.stateHash() == hash;
    detail = "damaged=" + boolText(damaged)
        + " merged=" + boolText(repeatedSiteMerged)
        + " save=" + boolText(saveRestored)
        + " hash=" + std::to_string(hash);
    return damaged && repeatedSiteMerged && saveRestored;
}

bool smokeCarrier(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(40, WorldObjectKind::Unit, "A", "CARRIER", 4, 4)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    const auto target = [](int parentId) { return parentId == 40 ? 41 : -1; };
    std::vector<CarrierAction> spawn = state.tickCarriers(
        objects,
        rules,
        0,
        target,
        sink
    );
    int slot0 = -1;
    int slot1 = -1;
    for (const CarrierAction& action : spawn) {
        if (action.kind != CarrierAction::Kind::Spawn) {
            continue;
        }
        const int childId = action.slot == 0 ? 50 : 51;
        if (!state.attachCarrierChild(40, action.slot, childId, 0, sink)) {
            continue;
        }
        if (action.slot == 0) {
            slot0 = childId;
        } else {
            slot1 = childId;
        }
    }
    const std::vector<CarrierAction> launch = state.tickCarriers(
        objects,
        rules,
        1,
        target,
        sink
    );
    const bool launched = std::any_of(
        launch.begin(),
        launch.end(),
        [](const CarrierAction& action) {
            return action.kind == CarrierAction::Kind::Launch;
        }
    );
    state.notifyCarrierChildFired(slot0, 2, sink);
    const std::vector<CarrierAction> returning = state.tickCarriers(
        objects,
        rules,
        10,
        [](int) { return -1; },
        sink
    );
    const bool noTimerDock = std::none_of(
        returning.begin(),
        returning.end(),
        [](const CarrierAction& action) {
            return action.kind == CarrierAction::Kind::Dock;
        }
    ) && state.carrierChildren().at(slot0).phase
        == CarrierChildPhase::Returning;
    const bool docked = state.notifyCarrierChildDocked(slot0, 10, sink)
        && state.carrierChildren().at(slot0).phase == CarrierChildPhase::Docked
        && state.carrierParents().at(40).slotReadyTick.at(0)
            == 10 + state.carrierParents().at(40).reloadRate;
    state.notifyCarrierChildDestroyed(slot1, 10, sink);
    const std::vector<CarrierAction> replenish = state.tickCarriers(
        objects,
        rules,
        14,
        [](int) { return -1; },
        sink
    );
    const bool replenished = std::any_of(
        replenish.begin(),
        replenish.end(),
        [](const CarrierAction& action) {
            return action.kind == CarrierAction::Kind::Spawn;
        }
    );
    const std::uint64_t carrierHash = state.stateHash();
    std::ostringstream carrierSaveText;
    state.save(carrierSaveText);
    IniFile carrierSave;
    RuntimeSpecialMechanismState carrierRestored;
    carrierRestored.reset(rules.getSpecialMechanismRules());
    std::string carrierLoadError;
    const bool carrierSaveRestored = carrierSave.loadFromString(
            carrierSaveText.str()
        )
        && carrierRestored.load(carrierSave, carrierLoadError)
        && carrierRestored.stateHash() == carrierHash;

    const std::string strictV2CarrierPrefix =
        "[SpecialMechanisms]\n"
        "Version=2\n"
        "[SpecialParasitePending]\n"
        "Count=0\n"
        "[SpecialParasites]\n"
        "Count=0\n"
        "[SpecialParasiteResolved]\n"
        "Count=0\n";
    const auto rejectedAtomically = [
        &state,
        carrierHash,
        &strictV2CarrierPrefix
    ](const std::string& carrierSections) {
        IniFile invalidSave;
        std::string invalidError;
        return invalidSave.loadFromString(
                strictV2CarrierPrefix + carrierSections
            )
            && !state.load(invalidSave, invalidError)
            && !invalidError.empty()
            && state.stateHash() == carrierHash
            && state.carrierParents().size() == 1
            && state.carrierChildren().size() == 1;
    };
    const std::string validChildren =
        "[SpecialCarrierChildren]\n"
        "Count=1\n"
        "0=50|40|0|Docked|-1|10|1\n";
    const bool badCountRejected = rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=bad\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=4097\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "1=41|CHILD|A|1|3|2|0,-1,0\n"
            + validChildren
        );
    const bool badParentRejected = rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|two|3|2|0,50,10~1,-1,13\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=2\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "1=40|CHILD|A|1|3|2|0,-1,0\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~0,-1,13\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10\n"
            + validChildren
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=0|CHILD|A|1|3|2|0,50,10\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|0|0|Docked|-1|10|1\n"
        );
    const bool badChildRejected = rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|40|0|Unknown|-1|10|1\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|40|0|Destroyed|-1|10|0\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|40|0|Docked|41|10|1\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "[SpecialCarrierChildren]\n"
            "Count=2\n"
            "0=50|40|0|Docked|-1|10|1\n"
            "1=50|40|0|Docked|-1|10|1\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|3|2|0,50,10~1,-1,13\n"
            "[SpecialCarrierChildren]\n"
            "Count=0\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|1|3|2|0,0,10\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=0|40|0|Docked|-1|10|1\n"
        )
        && rejectedAtomically(
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|1|3|2|0,50,10\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|40|0|Launching|0|10|1\n"
        );
    const bool cycleRejected = rejectedAtomically(
        "[SpecialCarrierParents]\n"
        "Count=2\n"
        "0=40|CHILD|A|1|3|2|0,41,10\n"
        "1=41|CHILD|A|1|3|2|0,40,10\n"
        "[SpecialCarrierChildren]\n"
        "Count=2\n"
        "0=41|40|0|Docked|-1|10|1\n"
        "1=40|41|0|Docked|-1|10|1\n"
    );
    IniFile missingCarrierSections;
    RuntimeSpecialMechanismState emptyCarrierState;
    emptyCarrierState.reset(rules.getSpecialMechanismRules());
    std::string emptyCarrierError;
    const bool missingSectionsAreEmpty = missingCarrierSections.loadFromString(
            strictV2CarrierPrefix
        )
        && emptyCarrierState.load(
            missingCarrierSections,
            emptyCarrierError
        )
        && emptyCarrierState.carrierParents().empty()
        && emptyCarrierState.carrierChildren().empty();
    IniFile legacyCarrierSave;
    RuntimeSpecialMechanismState legacyCarrierState;
    legacyCarrierState.reset(rules.getSpecialMechanismRules());
    std::string legacyCarrierError;
    const bool legacyCarrierLoaded = legacyCarrierSave.loadFromString(
            "[SpecialMechanisms]\n"
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|1|3|2|0,50,10\n"
            "[SpecialCarrierChildren]\n"
            "Count=1\n"
            "0=50|40|0|Docked|-1|10|1\n"
        )
        && legacyCarrierState.load(
            legacyCarrierSave,
            legacyCarrierError
        )
        && legacyCarrierState.carrierParents().size() == 1
        && legacyCarrierState.carrierChildren().size() == 1;

    RuntimeSpecialMechanismState hashProbe;
    hashProbe.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> hashObjects{
        smokeObject(40, WorldObjectKind::Unit, "A", "CARRIER", 4, 4)
    };
    hashProbe.ensureCarrierParents(hashObjects, rules, 0);
    const std::uint64_t carrierConfigurationHash = hashProbe.stateHash();
    IniFile changedConfigurationSave;
    std::string changedConfigurationError;
    const bool configurationAffectsHash =
        changedConfigurationSave.loadFromString(
            strictV2CarrierPrefix
            +
            "[SpecialCarrierParents]\n"
            "Count=1\n"
            "0=40|CHILD|A|2|30|20|0,-1,0~1,-1,0\n"
            "[SpecialCarrierChildren]\n"
            "Count=0\n"
        )
        && hashProbe.load(
            changedConfigurationSave,
            changedConfigurationError
        )
        && hashProbe.stateHash() != carrierConfigurationHash;
    objects[0].alive = false;
    state.onObjectRemoved(objects, 40, 15, sink, "parent death");
    const bool childrenCleaned = state.carrierChildren().empty();
    detail = "spawned=" + std::to_string(spawn.size())
        + " launched=" + boolText(launched)
        + " physicalReturn=" + boolText(noTimerDock)
        + " docked=" + boolText(docked)
        + " replenished=" + boolText(replenished)
        + " save=" + boolText(carrierSaveRestored)
        + " strict=" + boolText(
            badCountRejected && badParentRejected && badChildRejected
                && cycleRejected
        )
        + " compatible=" + boolText(
            missingSectionsAreEmpty && legacyCarrierLoaded
        )
        + " configHash=" + boolText(configurationAffectsHash)
        + " cleaned=" + boolText(childrenCleaned);
    return slot0 >= 0 && slot1 >= 0 && launched && noTimerDock && docked
        && replenished && carrierSaveRestored && badCountRejected
        && badParentRejected && badChildRejected && cycleRejected
        && missingSectionsAreEmpty && legacyCarrierLoaded
        && configurationAffectsHash && childrenCleaned;
}

bool smokeCarrierHornetSortie(const RulesDatabase&, std::string& detail) {
    IniFile native; RulesDatabase rules;
    if (!native.loadFromString("[VehicleTypes]\n0=CARRIER\n[AircraftTypes]\n0=HORNET\n"
            "[CARRIER]\nStrength=800\nSpawns=HORNET\nSpawnsNumber=1\nSpawnRegenRate=600\nSpawnReloadRate=150\n"
            "[HORNET]\nStrength=75\nSpawned=yes\nAmmo=1\n") || !rules.loadFromIni(native)) return false;
    RuntimeSpecialMechanismState state; state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{smokeObject(40, WorldObjectKind::Unit, "A", "CARRIER", 4, 4)};
    const auto sink = [](const SpecialMechanismEvent&) {};
    const auto target = [](int) { return 41; };
    const auto spawn = state.tickCarriers(objects, rules, 0, target, sink);
    if (spawn.size() != 1 || !state.attachCarrierChild(40, 0, 50, 0, sink)) return false;
    const auto launch = state.tickCarriers(objects, rules, 1, target, sink);
    (void)state.tickCarriers(objects, rules, 20, target, sink);
    const bool waitForPhysicalLaunch = launch.size() == 1
        && state.carrierChildren().at(50).phase == CarrierChildPhase::Launching
        && state.carrierChildren().at(50).sortieShotsRemaining == 3
        && state.notifyCarrierChildLaunched(50, 20, sink);
    const bool first = state.notifyCarrierChildFired(50, 21, sink)
        && state.carrierChildren().at(50).phase == CarrierChildPhase::Attacking
        && state.carrierChildren().at(50).ammoRemaining == 1
        && state.carrierChildren().at(50).sortieShotsRemaining == 2;
    std::ostringstream saved; state.save(saved);
    IniFile ini; std::string error; RuntimeSpecialMechanismState copy;
    copy.reset(rules.getSpecialMechanismRules());
    const bool resumed = ini.loadFromString(saved.str()) && copy.load(ini, error)
        && copy.stateHash() == state.stateHash()
        && copy.carrierChildren().at(50).sortieShotsRemaining == 2;
    const auto remainingTwo = copy.stateHash();
    const bool second = copy.notifyCarrierChildFired(50, 22, sink)
        && copy.carrierChildren().at(50).phase == CarrierChildPhase::Attacking
        && copy.carrierChildren().at(50).ammoRemaining == 1
        && copy.carrierChildren().at(50).sortieShotsRemaining == 1
        && copy.stateHash() != remainingTwo;
    const bool third = copy.notifyCarrierChildFired(50, 23, sink)
        && copy.carrierChildren().at(50).phase == CarrierChildPhase::Returning
        && copy.carrierChildren().at(50).ammoRemaining == 0
        && copy.carrierChildren().at(50).sortieShotsRemaining == 0
        && !copy.notifyCarrierChildFired(50, 24, sink);
    const bool dock = copy.notifyCarrierChildDocked(50, 25, sink)
        && copy.carrierChildren().at(50).sortieShotsRemaining == 1
        && copy.carrierParents().at(40).slotReadyTick.at(0) == 175;
    const auto reloaded = copy.tickCarriers(objects, rules, 175, target, sink);
    const bool nextSortie = reloaded.size() == 1
        && copy.carrierChildren().at(50).sortieShotsRemaining == 3;
    const std::string prefix = "[SpecialMechanisms]\nVersion=2\n[SpecialParasitePending]\nCount=0\n"
        "[SpecialParasites]\nCount=0\n[SpecialParasiteResolved]\nCount=0\n"
        "[SpecialCarrierParents]\nCount=1\n0=40|HORNET|A|1|600|150|0,50,1\n"
        "[SpecialCarrierChildren]\nCount=1\n";
    const auto rejected = [&](const std::string& row) {
        IniFile malformed; std::string why; const auto before = copy.stateHash();
        return malformed.loadFromString(prefix + "0=" + row + "\n")
            && !copy.load(malformed, why) && !why.empty() && copy.stateHash() == before;
    };
    const bool invalidAtomic = rejected("50|40|0|Attacking|41|20|1|4")
        && rejected("50|40|0|Attacking|41|20|1|0")
        && rejected("50|40|0|Docked|-1|20|1|2")
        && rejected("50|40|0|Returning|-1|20|0|1");
    IniFile legacy; RuntimeSpecialMechanismState old; old.reset(rules.getSpecialMechanismRules());
    const bool legacyOne = legacy.loadFromString(prefix + "0=50|40|0|Attacking|41|20|1\n")
        && old.load(legacy, error) && old.carrierChildren().at(50).sortieShotsRemaining == 1
        && old.notifyCarrierChildFired(50, 21, sink)
        && old.carrierChildren().at(50).phase == CarrierChildPhase::Returning
        && old.carrierChildren().at(50).sortieShotsRemaining == 0;
    detail = "physicalLaunch=" + boolText(waitForPhysicalLaunch) + " first=" + boolText(first)
        + " resumeTwo=" + boolText(resumed) + " second=" + boolText(second)
        + " third=" + boolText(third) + " dock=" + boolText(dock) + " nextSortie=" + boolText(nextSortie)
        + " invalidAtomic=" + boolText(invalidAtomic) + " legacyOne=" + boolText(legacyOne);
    return waitForPhysicalLaunch && first && resumed && second && third && dock && nextSortie && invalidAtomic && legacyOne;
}

bool smokeDestroyerAswSortie(const RulesDatabase&, std::string& detail) {
    IniFile native;
    RulesDatabase rules;
    if (!native.loadFromString(
            "[VehicleTypes]\n0=DEST\n[AircraftTypes]\n0=ASW\n1=AIRPORTJET\n"
            "[DEST]\nStrength=600\nSpawns=ASW\nSpawnsNumber=1\n"
            "SpawnRegenRate=400\nSpawnReloadRate=150\n"
            "[ASW]\nStrength=30\nSpawned=yes\nAmmo=1\nPrimary=ASWBomb\n"
            "[AIRPORTJET]\nStrength=100\nAmmo=1\n"
            "[ASWBomb]\nDamage=50\nROF=3\nRange=3\n")
        || !rules.loadFromIni(native)) return false;
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(40, WorldObjectKind::Unit, "A", "DEST", 4, 4),
        smokeObject(50, WorldObjectKind::Unit, "A", "ASW", 4, 4),
        smokeObject(60, WorldObjectKind::Unit, "A", "AIRPORTJET", 8, 8)
    };
    std::vector<SpecialMechanismEvent> events;
    const auto sink = [&events](const SpecialMechanismEvent& event) { events.push_back(event); };
    const auto target = [](int) { return 41; };
    const auto noTarget = [](int) { return -1; };
    const auto spawn = state.tickCarriers(objects, rules, 0, target, sink);
    if (spawn.size() != 1 || !state.attachCarrierChild(40, 0, 50, 0, sink)) return false;
    const auto* airport = rules.findUnitType("AircraftType", "AIRPORTJET");
    const bool airportOutsideCarrierState = airport && airport->ammo == 1
        && state.carrierParents().size() == 1 && !state.isCarrierChild(60);
    const auto launch = state.tickCarriers(objects, rules, 1, target, sink);
    const bool launchBudget = launch.size() == 1
        && state.carrierChildren().at(50).phase == CarrierChildPhase::Launching
        && state.carrierChildren().at(50).sortieShotsRemaining == 3;
    (void)state.tickCarriers(objects, rules, 2, target, sink);
    const bool originalLaunchClock = state.carrierChildren().at(50).phase
            == CarrierChildPhase::Attacking
        && state.carrierChildren().at(50).actionTick == 2;
    const bool first = state.notifyCarrierChildFired(50, 3, sink)
        && state.carrierChildren().at(50).phase == CarrierChildPhase::Attacking
        && state.carrierChildren().at(50).ammoRemaining == 1
        && state.carrierChildren().at(50).sortieShotsRemaining == 2;
    const auto restore = [&rules](const std::string& text, RuntimeSpecialMechanismState& copy) {
        IniFile ini;
        std::string error;
        copy.reset(rules.getSpecialMechanismRules());
        return ini.loadFromString(text) && copy.load(ini, error);
    };
    std::ostringstream firstText;
    state.save(firstText);
    RuntimeSpecialMechanismState firstCopy;
    const bool resumeTwo = restore(firstText.str(), firstCopy)
        && firstCopy.stateHash() == state.stateHash()
        && firstCopy.carrierChildren().at(50).sortieShotsRemaining == 2;
    if (!launchBudget || !originalLaunchClock || !first || !resumeTwo) return false;
    const auto twoHash = firstCopy.stateHash();
    const bool second = firstCopy.notifyCarrierChildFired(50, 4, sink)
        && firstCopy.carrierChildren().at(50).phase == CarrierChildPhase::Attacking
        && firstCopy.carrierChildren().at(50).ammoRemaining == 1
        && firstCopy.carrierChildren().at(50).sortieShotsRemaining == 1
        && firstCopy.stateHash() != twoHash;
    std::ostringstream secondText;
    firstCopy.save(secondText);
    RuntimeSpecialMechanismState secondCopy;
    const bool resumeOne = restore(secondText.str(), secondCopy)
        && secondCopy.stateHash() == firstCopy.stateHash()
        && secondCopy.carrierChildren().at(50).sortieShotsRemaining == 1;
    if (!second || !resumeOne) return false;
    const bool third = secondCopy.notifyCarrierChildFired(50, 5, sink)
        && secondCopy.carrierChildren().at(50).phase == CarrierChildPhase::Returning
        && secondCopy.carrierChildren().at(50).ammoRemaining == 0
        && secondCopy.carrierChildren().at(50).sortieShotsRemaining == 0
        && secondCopy.carrierChildren().at(50).actionTick == 5
        && !secondCopy.notifyCarrierChildFired(50, 6, sink);
    const bool returnOnce = std::count_if(events.begin(), events.end(), [](const auto& event) {
        return event.kind == "CarrierChildReturn";
    }) == 1;
    const bool dock = secondCopy.notifyCarrierChildDocked(50, 6, sink)
        && secondCopy.carrierChildren().at(50).ammoRemaining == 1
        && secondCopy.carrierChildren().at(50).sortieShotsRemaining == 1
        && secondCopy.carrierParents().at(40).slotReadyTick.at(0) == 156;
    const bool noEarlyReload = secondCopy.tickCarriers(objects, rules, 155, target, sink).empty();
    const auto next = secondCopy.tickCarriers(objects, rules, 156, target, sink);
    const bool nextSortie = next.size() == 1
        && secondCopy.carrierChildren().at(50).sortieShotsRemaining == 3;
    const bool destroyed = secondCopy.notifyCarrierChildDestroyed(50, 160, sink)
        && secondCopy.carrierParents().at(40).slotReadyTick.at(0) == 560;
    const bool noEarlyRegen = secondCopy.tickCarriers(objects, rules, 559, noTarget, sink).empty();
    const auto regen = secondCopy.tickCarriers(objects, rules, 560, noTarget, sink);
    const bool originalRegen = regen.size() == 1 && regen.front().kind == CarrierAction::Kind::Spawn;

    RuntimeSpecialMechanismState cancelled;
    RuntimeSpecialMechanismState cancelledCopy;
    const bool requestedReturn = restore(firstText.str(), cancelled)
        && cancelled.requestCarrierChildReturn(50, 4, sink, "target cleared")
        && cancelled.carrierChildren().at(50).targetObjectId == -1
        && cancelled.carrierChildren().at(50).ammoRemaining == 1
        && cancelled.carrierChildren().at(50).sortieShotsRemaining == 2;
    std::ostringstream cancelledText;
    cancelled.save(cancelledText);
    const bool cancelledRestored = requestedReturn && restore(cancelledText.str(), cancelledCopy)
        && cancelledCopy.stateHash() == cancelled.stateHash()
        && cancelledCopy.tickCarriers(objects, rules, 5, noTarget, sink).empty()
        && cancelledCopy.carrierChildren().at(50).phase == CarrierChildPhase::Returning;

    const std::string prefix = "[SpecialMechanisms]\nVersion=2\n[SpecialParasitePending]\nCount=0\n"
        "[SpecialParasites]\nCount=0\n[SpecialParasiteResolved]\nCount=0\n"
        "[SpecialCarrierParents]\nCount=1\n0=40|ASW|A|1|400|150|0,50,1\n"
        "[SpecialCarrierChildren]\nCount=1\n";
    const auto rejected = [&](const std::string& row) {
        IniFile malformed;
        std::string error;
        const auto before = firstCopy.stateHash();
        std::ostringstream beforeText;
        firstCopy.save(beforeText);
        const bool failed = malformed.loadFromString(prefix + "0=" + row + "\n")
            && !firstCopy.load(malformed, error) && !error.empty()
            && firstCopy.stateHash() == before;
        std::ostringstream afterText;
        firstCopy.save(afterText);
        return failed && afterText.str() == beforeText.str();
    };
    const bool invalidAtomic = rejected("50|40|0|Attacking|41|2|1|4")
        && rejected("50|40|0|Attacking|41|2|1|0")
        && rejected("50|40|0|Docked|-1|2|1|2")
        && rejected("50|40|0|Returning|-1|2|0|1")
        && rejected("50|40|0|Attacking|41|2|2|3");
    RuntimeSpecialMechanismState legacy;
    const bool legacyOne = restore(prefix + "0=50|40|0|Attacking|41|2|1\n", legacy)
        && legacy.carrierChildren().at(50).sortieShotsRemaining == 1
        && legacy.notifyCarrierChildFired(50, 3, sink)
        && legacy.carrierChildren().at(50).phase == CarrierChildPhase::Returning
        && legacy.carrierChildren().at(50).sortieShotsRemaining == 0;
    RuntimeSpecialMechanismState legacySpent;
    const bool legacyZero = restore(prefix + "0=50|40|0|Returning|41|3|0\n", legacySpent)
        && legacySpent.carrierChildren().at(50).sortieShotsRemaining == 0
        && !legacySpent.notifyCarrierChildFired(50, 4, sink);

    const auto singleRoundPair = [&](const std::string& parentType, const std::string& childType) {
        IniFile ini;
        RulesDatabase singleRules;
        if (!ini.loadFromString("[VehicleTypes]\n0=" + parentType + "\n[AircraftTypes]\n0=" + childType
                + "\n[" + parentType + "]\nStrength=600\nSpawns=" + childType
                + "\nSpawnsNumber=1\nSpawnRegenRate=400\nSpawnReloadRate=150\n[" + childType
                + "]\nStrength=30\nSpawned=yes\nAmmo=1\n")
            || !singleRules.loadFromIni(ini)) return false;
        RuntimeSpecialMechanismState single;
        single.reset(singleRules.getSpecialMechanismRules());
        const std::vector<WorldObjectInstance> parents{
            smokeObject(40, WorldObjectKind::Unit, "A", parentType, 4, 4)
        };
        const auto oneSpawn = single.tickCarriers(parents, singleRules, 0, target, sink);
        if (oneSpawn.size() != 1 || !single.attachCarrierChild(40, 0, 50, 0, sink)) return false;
        const auto oneLaunch = single.tickCarriers(parents, singleRules, 1, target, sink);
        (void)single.tickCarriers(parents, singleRules, 2, target, sink);
        return oneLaunch.size() == 1 && single.carrierChildren().at(50).sortieShotsRemaining == 1
            && single.notifyCarrierChildFired(50, 3, sink)
            && single.carrierChildren().at(50).phase == CarrierChildPhase::Returning
            && single.carrierChildren().at(50).ammoRemaining == 0;
    };
    const bool exactPairsOnly = singleRoundPair("CUSTOMASW", "ASW")
        && singleRoundPair("DEST", "OTHERCHILD");
    objects[0].alive = false;
    objects[0].health = 0;
    state.onObjectRemoved(objects, 40, 7, sink, "parent death");
    const bool parentDeath = state.carrierParents().empty() && state.carrierChildren().empty()
        && !objects[1].alive && objects[1].health == 0;
    detail = "launchClock=" + boolText(launchBudget && originalLaunchClock)
        + " threeShots=" + boolText(first && second && third && returnOnce)
        + " resumeTwoOne=" + boolText(resumeTwo && resumeOne)
        + " reload150=" + boolText(dock && noEarlyReload && nextSortie)
        + " regen400=" + boolText(destroyed && noEarlyRegen && originalRegen)
        + " cancel=" + boolText(cancelledRestored) + " invalidAtomic=" + boolText(invalidAtomic)
        + " legacyOneZero=" + boolText(legacyOne && legacyZero)
        + " exactPairs=" + boolText(exactPairsOnly) + " airport=" + boolText(airportOutsideCarrierState)
        + " parentDeath=" + boolText(parentDeath);
    return launchBudget && originalLaunchClock && first && second && third && returnOnce
        && resumeTwo && resumeOne && dock && noEarlyReload && nextSortie
        && destroyed && noEarlyRegen && originalRegen && cancelledRestored
        && invalidAtomic && legacyOne && legacyZero && exactPairsOnly && airportOutsideCarrierState && parentDeath;
}

bool smokeTimedBomb(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(60, WorldObjectKind::Infantry, "A", "C4UNIT", 1, 1),
        smokeObject(61, WorldObjectKind::Infantry, "A", "IVAN", 2, 1),
        smokeObject(62, WorldObjectKind::Infantry, "A", "ENGINEER", 3, 1),
        smokeObject(70, WorldObjectKind::Structure, "B", "BOMBTARGET", 4, 1),
        smokeObject(71, WorldObjectKind::Structure, "B", "NOC4TARGET", 5, 1),
        smokeObject(72, WorldObjectKind::Unit, "B", "PARASITEHOST", 6, 1)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    std::string reason;
    const bool c4Placed = state.tryPlantC4(
        objects,
        rules,
        60,
        70,
        0,
        10,
        sink,
        reason
    );
    RuntimeSpecialMechanismState buildingPolicyState;
    buildingPolicyState.reset(rules.getSpecialMechanismRules());
    const bool allBuildingsAcceptC4 = buildingPolicyState.tryPlantC4(
        objects,
        rules,
        60,
        71,
        0,
        10,
        sink,
        reason
    );
    const bool nonBuildingC4Rejected = !state.tryPlantC4(
        objects, rules, 60, 72, 0, 10, sink, reason);
    const bool wrongDisarmRejected = !state.tryDisarmTimedBomb(
        objects,
        rules,
        62,
        70,
        "IvanWH",
        1,
        sink,
        reason
    );
    const bool disarmed = state.tryDisarmTimedBomb(
        objects,
        rules,
        62,
        70,
        "DisarmWH",
        1,
        sink,
        reason
    ) && state.timedBombs().empty();

    const bool ivanPlaced = state.tryAttachIvanBomb(
        objects,
        rules,
        61,
        72,
        5,
        10,
        sink,
        reason
    );
    const auto bombSnapshots = state.renderSnapshots(5);
    const bool bombRendered = std::any_of(
        bombSnapshots.begin(),
        bombSnapshots.end(),
        [](const SpecialMechanismRenderSnapshot& snapshot) {
            return snapshot.kind == "timedBomb"
                && snapshot.objectId == 61
                && snapshot.targetObjectId == 72
                && snapshot.resourceId == "IvanWH";
        }
    );
    const std::uint64_t savedHash = state.stateHash();
    std::ostringstream saveText;
    state.save(saveText);
    IniFile save;
    RuntimeSpecialMechanismState restored;
    restored.reset(rules.getSpecialMechanismRules());
    std::string loadError;
    const bool saveRestored = save.loadFromString(saveText.str())
        && restored.load(save, loadError)
        && restored.stateHash() == savedHash;
    const bool earlyQuiet = restored.tickTimedBombs(14, sink).empty();
    const auto ivanActions = restored.tickTimedBombs(15, sink);
    const bool ivanDetonated = ivanActions.size() == 1
        && ivanActions.front().kind
            == SpecialMechanismActionKind::TimedBombDetonate
        && ivanActions.front().targetObjectId == 72
        && ivanActions.front().damage == 450
        && ivanActions.front().warheadId == "IvanWH"
        && !ivanActions.front().forceDestruction;
    const bool ivanOnlyOnce = restored.tickTimedBombs(16, sink).empty()
        && restored.timedBombs().empty();

    const bool c4Again = restored.tryPlantC4(
        objects,
        rules,
        60,
        70,
        20,
        10,
        sink,
        reason
    );
    const auto c4Actions = restored.tickTimedBombs(38, sink);
    const bool c4Absolute = c4Actions.size() == 1
        && c4Actions.front().targetObjectId == 70
        && c4Actions.front().forceDestruction
        && c4Actions.front().warheadId == "Super";

    RuntimeSpecialMechanismState removalState;
    removalState.reset(rules.getSpecialMechanismRules());
    const bool removalAttached = removalState.tryAttachIvanBomb(
        objects,
        rules,
        61,
        72,
        0,
        10,
        sink,
        reason
    );
    std::vector<SpecialMechanismAction> removalActions;
    removalState.onObjectRemoved(
        objects,
        72,
        1,
        sink,
        "target destroyed",
        &removalActions
    );
    const bool removalDetonated = removalAttached
        && removalActions.size() == 1
        && removalActions.front().kind
            == SpecialMechanismActionKind::TimedBombDetonate
        && removalState.timedBombs().empty();

    RuntimeSpecialMechanismState reconcileState;
    reconcileState.reset(rules.getSpecialMechanismRules());
    const bool reconcileAttached = reconcileState.tryAttachIvanBomb(
        objects,
        rules,
        61,
        72,
        0,
        10,
        sink,
        reason
    );
    objects[5].alive = false;
    objects[5].health = 0;
    std::vector<SpecialMechanismAction> reconcileActions;
    reconcileState.reconcile(objects, 1, sink, &reconcileActions);
    const bool reconciled = reconcileAttached
        && reconcileActions.size() == 1
        && reconcileActions.front().kind
            == SpecialMechanismActionKind::TimedBombDetonate
        && reconcileState.timedBombs().empty();
    objects[5].alive = true;
    objects[5].health = 100;

    const std::string strictV2Prefix =
        "[SpecialMechanisms]\n"
        "Version=2\n";
    const std::string strictV2Suffix =
        "[SpecialParasitePending]\n"
        "Count=0\n"
        "[SpecialParasites]\n"
        "Count=0\n"
        "[SpecialParasiteResolved]\n"
        "Count=0\n";
    RuntimeSpecialMechanismState atomicState;
    atomicState.reset(rules.getSpecialMechanismRules());
    const bool atomicSeeded = atomicState.tryAttachIvanBomb(
        objects,
        rules,
        61,
        72,
        5,
        10,
        sink,
        reason
    );
    const std::uint64_t atomicHash = atomicState.stateHash();
    const auto rejectedAtomically = [
        &atomicState,
        atomicHash,
        &strictV2Prefix,
        &strictV2Suffix
    ](const std::string& timedBombSection) {
        IniFile invalidSave;
        std::string invalidError;
        return invalidSave.loadFromString(
                strictV2Prefix + timedBombSection + strictV2Suffix
            )
            && !atomicState.load(invalidSave, invalidError)
            && !invalidError.empty()
            && atomicState.stateHash() == atomicHash
            && atomicState.timedBombs().size() == 1
            && atomicState.timedBombs().begin()->first == 72;
    };
    const bool badCountRejected = rejectedAtomically(
            "[SpecialTimedBombs]\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=not-a-count\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=-1\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count="
                + std::to_string(kMaximumTimedBombRecords + 1) + "\n"
        );
    const bool badWidthRejected = rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|60|A|C4|1|18|2147483647|Super\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|60|A|C4|1|18|2147483647|Super|yes|extra\n"
        );
    const bool badNumericOrRangeRejected = rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=72|61|A|Ivan|4|not-a-tick|450|IvanWH|no\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=72|61|A|Ivan|14|4|450|IvanWH|no\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=72|61|A|Ivan|4|14|-1|IvanWH|no\n"
        );
    const bool duplicateRejected = rejectedAtomically(
        "[SpecialTimedBombs]\n"
        "Count=2\n"
        "0=70|60|A|C4|1|18|2147483647|Super|yes\n"
        "1=70|61|A|Ivan|4|14|450|IvanWH|no\n"
    );
    const bool badIdentityRejected = rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=-1|60|A|C4|1|18|2147483647|Super|yes\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|70|A|C4|1|18|2147483647|Super|yes\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|60||C4|1|18|2147483647|Super|yes\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|60|A|C4|1|18|2147483647||yes\n"
        )
        && rejectedAtomically(
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=70|60|A|C4|1|18|2147483647|Super|no\n"
        );

    IniFile legacyBombSave;
    RuntimeSpecialMechanismState legacyBombState;
    legacyBombState.reset(rules.getSpecialMechanismRules());
    std::string compatibilityError;
    const bool legacyBombLoaded = legacyBombSave.loadFromString(
            "[SpecialMechanisms]\n"
            "Version=1\n"
            "[SpecialTimedBombs]\n"
            "Count=1\n"
            "0=72|61|A|Ivan|5|15|450|IvanWH|no\n"
            "[SpecialParasites]\n"
            "Count=0\n"
        )
        && legacyBombState.load(legacyBombSave, compatibilityError)
        && legacyBombState.timedBombs().size() == 1
        && legacyBombState.timedBombs().begin()->first == 72;
    IniFile missingBombSectionSave;
    RuntimeSpecialMechanismState missingBombSectionState;
    missingBombSectionState.reset(rules.getSpecialMechanismRules());
    const bool missingBombSectionLoaded =
        missingBombSectionSave.loadFromString(
            strictV2Prefix + strictV2Suffix
        )
        && missingBombSectionState.load(
            missingBombSectionSave,
            compatibilityError
        )
        && missingBombSectionState.timedBombs().empty();
    const bool strictLoadRejected = atomicSeeded && badCountRejected
        && badWidthRejected && badNumericOrRangeRejected
        && duplicateRejected && badIdentityRejected;
    const bool saveCompatibility = saveRestored && legacyBombLoaded
        && missingBombSectionLoaded;

    detail = "placed=" + boolText(c4Placed && ivanPlaced && allBuildingsAcceptC4)
        + " rejected=" + boolText(nonBuildingC4Rejected && wrongDisarmRejected)
        + " disarmed=" + boolText(disarmed)
        + " save=" + boolText(saveRestored)
        + " once=" + boolText(earlyQuiet && ivanDetonated && ivanOnlyOnce)
        + " render=" + boolText(bombRendered)
        + " lifecycle=" + boolText(removalDetonated && reconciled)
        + " c4Absolute=" + boolText(c4Again && c4Absolute)
        + " strictLoad=" + boolText(strictLoadRejected)
        + " compatibility=" + boolText(saveCompatibility);
    return c4Placed && allBuildingsAcceptC4 && nonBuildingC4Rejected && wrongDisarmRejected && disarmed
        && ivanPlaced && saveRestored && earlyQuiet && ivanDetonated
        && ivanOnlyOnce && bombRendered && removalDetonated && reconciled
        && c4Again && c4Absolute && strictLoadRejected
        && saveCompatibility;
}

bool smokeParasiteLegacy(const RulesDatabase& rules, std::string& detail) {
    RuntimeSpecialMechanismState state;
    state.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> objects{
        smokeObject(80, WorldObjectKind::Unit, "A", "TDRONE", 1, 3),
        smokeObject(81, WorldObjectKind::Unit, "B", "PARASITEHOST", 2, 3),
        smokeObject(82, WorldObjectKind::Unit, "B", "SAFEHOST", 3, 3)
    };
    const auto sink = [](const SpecialMechanismEvent&) {};
    SpecialMechanismAction limboAction;
    std::string reason;
    const bool rejected = !state.tryAttachParasite(
        objects,
        rules,
        80,
        82,
        "PARASITEGUN",
        0,
        10,
        limboAction,
        sink,
        reason
    );
    const bool attached = state.tryAttachParasite(
        objects,
        rules,
        80,
        81,
        "PARASITEGUN",
        0,
        10,
        limboAction,
        sink,
        reason
    ) && limboAction.kind
        == SpecialMechanismActionKind::ParasiteEnterLimbo
        && limboAction.paralyzes == 32767
        && limboAction.culling
        && state.isParasiteSourceLimboed(80)
        && state.parasiteTargetForSource(80) == 81;

    // Apply the action exactly as a runtime adapter would: the source leaves
    // the active world but retains hit points while the persistent link owns
    // its limbo lifetime.
    objects[0].alive = false;
    objects[0].mission = "ParasiteLimbo";
    const auto initialDamage = state.tickParasites(objects, rules, 0, sink);
    const bool limboSurvived = initialDamage.size() == 1
        && initialDamage.front().kind
            == SpecialMechanismActionKind::ParasiteDamage
        && initialDamage.front().damage == 35
        && state.parasiteLinks().size() == 1;
    const bool inactiveLimboSourceSurvived = state.tickParasites(
        objects,
        rules,
        1,
        sink
    ).empty() && state.parasiteLinks().size() == 1;
    const auto repeatedDamage = state.tickParasites(
        objects,
        rules,
        4,
        sink
    );
    const bool periodicDamage = repeatedDamage.size() == 1
        && repeatedDamage.front().kind
            == SpecialMechanismActionKind::ParasiteDamage;

    const std::uint64_t savedHash = state.stateHash();
    std::ostringstream saveText;
    state.save(saveText);
    IniFile save;
    RuntimeSpecialMechanismState restored;
    restored.reset(rules.getSpecialMechanismRules());
    std::string loadError;
    const bool saveRestored = save.loadFromString(saveText.str())
        && restored.load(save, loadError)
        && restored.stateHash() == savedHash
        && restored.isParasiteSourceLimboed(80);
    const auto snapshots = restored.renderSnapshots(4);
    const bool rendered = std::any_of(
        snapshots.begin(),
        snapshots.end(),
        [](const SpecialMechanismRenderSnapshot& snapshot) {
            return snapshot.kind == "parasite"
                && snapshot.objectId == 80
                && snapshot.targetObjectId == 81;
        }
    );

    RuntimeSpecialMechanismState missingSourceRestored;
    missingSourceRestored.reset(rules.getSpecialMechanismRules());
    std::string missingSourceLoadError;
    const bool missingSourceLoaded = missingSourceRestored.load(
        save,
        missingSourceLoadError
    );
    std::vector<WorldObjectInstance> hostOnly{objects[1], objects[2]};
    const auto missingSourceActions = missingSourceRestored.tickParasites(
        hostOnly,
        rules,
        5,
        sink
    );
    const bool missingSourceCleared = missingSourceLoaded
        && missingSourceActions.size() == 1
        && missingSourceActions.front().kind
            == SpecialMechanismActionKind::ParasiteReleaseTarget
        && missingSourceRestored.parasiteLinks().empty();

    RuntimeSpecialMechanismState reconcileRestored;
    reconcileRestored.reset(rules.getSpecialMechanismRules());
    std::string reconcileLoadError;
    const bool reconcileLoaded = reconcileRestored.load(
        save,
        reconcileLoadError
    );
    std::vector<SpecialMechanismAction> reconcileActions;
    reconcileRestored.reconcile(
        hostOnly,
        5,
        sink,
        &reconcileActions
    );
    const bool missingSourceReconciled = reconcileLoaded
        && reconcileActions.size() == 1
        && reconcileActions.front().kind
            == SpecialMechanismActionKind::ParasiteReleaseTarget
        && reconcileRestored.parasiteLinks().empty();

    objects[1].alive = false;
    objects[1].health = 0;
    const auto hostDeath = restored.tickParasites(objects, rules, 5, sink);
    const bool hostEjected = hostDeath.size() == 1
        && hostDeath.front().kind
            == SpecialMechanismActionKind::ParasiteEject
        && restored.parasiteLinks().empty();

    RuntimeSpecialMechanismState parasiteDeathState;
    parasiteDeathState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> deathObjects{
        smokeObject(83, WorldObjectKind::Unit, "A", "TDRONE", 1, 4),
        smokeObject(84, WorldObjectKind::Unit, "B", "PARASITEHOST", 2, 4)
    };
    const bool deathAttached = parasiteDeathState.tryAttachParasite(
        deathObjects,
        rules,
        83,
        84,
        "PARASITEGUN",
        0,
        10,
        limboAction,
        sink,
        reason
    );
    deathObjects[0].alive = false;
    deathObjects[0].mission = "ParasiteLimbo";
    parasiteDeathState.tickParasites(deathObjects, rules, 0, sink);
    deathObjects[0].health = 0;
    const auto parasiteDeath = parasiteDeathState.tickParasites(
        deathObjects,
        rules,
        1,
        sink
    );
    const bool deathCleared = deathAttached && parasiteDeath.size() == 1
        && parasiteDeath.front().kind
            == SpecialMechanismActionKind::ParasiteReleaseTarget
        && parasiteDeathState.parasiteLinks().empty();

    RuntimeSpecialMechanismState removalState;
    removalState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> removalObjects{
        smokeObject(85, WorldObjectKind::Unit, "A", "TDRONE", 1, 5),
        smokeObject(86, WorldObjectKind::Unit, "B", "PARASITEHOST", 2, 5)
    };
    const bool removalAttached = removalState.tryAttachParasite(
        removalObjects,
        rules,
        85,
        86,
        "PARASITEGUN",
        0,
        10,
        limboAction,
        sink,
        reason
    );
    std::vector<SpecialMechanismAction> removalActions;
    removalState.onObjectRemoved(
        removalObjects,
        86,
        1,
        sink,
        "host destroyed",
        &removalActions
    );
    const bool removalEjected = removalAttached
        && removalActions.size() == 1
        && removalActions.front().kind
            == SpecialMechanismActionKind::ParasiteEject
        && removalState.parasiteLinks().empty();

    IniFile damagedSave;
    const bool damagedParsed = damagedSave.loadFromString(
        "[SpecialTimedBombs]\n"
        "Count=0\n"
        "[SpecialParasites]\n"
        "Count=4\n"
        "0=81|80|A|PARASITEGUN|ParasiteWH|35|0|4|4|no|32767|yes|yes\n"
        "1=-1|80|A|PARASITEGUN|ParasiteWH|35|0|4|4|no|32767|yes|yes\n"
        "2=82|80|A|PARASITEGUN|ParasiteWH|35|0|4|4|no|32767|yes|yes\n"
        "3=83|84|A|PARASITEGUN|ParasiteWH|35|bad|4|4|no|32767|yes|yes\n"
    );
    RuntimeSpecialMechanismState filtered;
    filtered.reset(rules.getSpecialMechanismRules());
    std::string damagedError;
    const bool damagedFiltered = damagedParsed
        && filtered.load(damagedSave, damagedError)
        && filtered.parasiteLinks().size() == 1
        && filtered.parasiteLinks().begin()->first == 81;

    detail = "attached=" + boolText(attached)
        + " rejected=" + boolText(rejected)
        + " limbo=" + boolText(
            limboSurvived && inactiveLimboSourceSurvived
        )
        + " damage=" + boolText(periodicDamage)
        + " save=" + boolText(saveRestored && rendered)
        + " hostEject=" + boolText(hostEjected && removalEjected)
        + " parasiteDeath=" + boolText(deathCleared)
        + " missingSource=" + boolText(
            missingSourceCleared && missingSourceReconciled
        )
        + " damagedFiltered=" + boolText(damagedFiltered);
    return rejected && attached && limboSurvived
        && inactiveLimboSourceSurvived && periodicDamage
        && saveRestored && rendered && hostEjected && deathCleared
        && removalEjected && missingSourceCleared
        && missingSourceReconciled && damagedFiltered;
}

bool smokeParasite(const RulesDatabase& rules, std::string& detail) {
    const auto sink = [](const SpecialMechanismEvent&) {};
    const auto objectsFor = [](
        int sourceId,
        int targetId,
        ParasiteTargetKind targetKind
    ) {
        return std::vector<WorldObjectInstance>{
            smokeObject(
                sourceId,
                WorldObjectKind::Unit,
                "A",
                "TDRONE",
                4,
                sourceId % 11 + 2
            ),
            smokeObject(
                targetId,
                targetKind == ParasiteTargetKind::Infantry
                    ? WorldObjectKind::Infantry
                    : WorldObjectKind::Unit,
                "B",
                targetKind == ParasiteTargetKind::Infantry
                    ? "TARGET"
                    : "PARASITEHOST",
                5,
                targetId % 11 + 2
            )
        };
    };
    const auto launchFor = [](
        const std::vector<WorldObjectInstance>& objects,
        int projectileId,
        int tick,
        ParasiteTargetKind targetKind,
        bool organic,
        bool elite
    ) {
        ParasiteLaunchRequest request;
        request.projectileId = projectileId;
        request.currentTick = tick;
        request.sourceObjectId = objects[0].instanceId;
        request.targetObjectId = objects[1].instanceId;
        request.sourceOwner = objects[0].owner;
        request.sourceTypeId = objects[0].type;
        request.targetTypeId = objects[1].type;
        request.weaponId = "PARASITEGUN";
        request.warheadId = "ParasiteWH";
        request.targetKind = targetKind;
        request.targetCell = objectCell(&objects[1]);
        request.damage = 35;
        request.damageIntervalTicks = 4;
        request.paralyzes = 32767;
        request.targetMaximumHealth =
            targetKind == ParasiteTargetKind::Infantry ? 100 : 200;
        request.targetParasiteable =
            targetKind == ParasiteTargetKind::Vehicle;
        request.sourceOrganic = organic;
        request.sourceElite = elite;
        request.culling = true;
        request.normalCullingThresholdPermyriad = 2500;
        request.eliteCullingThresholdPermyriad = 5000;
        return request;
    };
    const auto impactFor = [](
        const std::vector<WorldObjectInstance>& objects,
        int projectileId,
        int tick,
        ParasiteTargetKind targetKind
    ) {
        ParasiteImpactRequest impact;
        impact.projectileId = projectileId;
        impact.currentTick = tick;
        impact.impact = ParasiteImpactKind::Hit;
        impact.targetObjectId = objects[1].instanceId;
        impact.targetTypeId = objects[1].type;
        impact.targetKind = targetKind;
        impact.targetPresent = true;
        impact.targetAlive = objects[1].alive && objects[1].health > 0;
        impact.targetParasiteable =
            targetKind == ParasiteTargetKind::Vehicle;
        impact.targetMaximumHealth =
            targetKind == ParasiteTargetKind::Infantry ? 100 : 200;
        impact.impactCell = objectCell(&objects[1]);
        impact.returnCellAvailable = true;
        impact.returnCell = {objects[1].x + 1, objects[1].y};
        return impact;
    };
    const auto beginAndAttach = [
        &launchFor,
        &impactFor,
        &sink
    ](
        RuntimeSpecialMechanismState& state,
        std::vector<WorldObjectInstance>& objects,
        int projectileId,
        int tick,
        bool organic,
        bool elite
    ) {
        const ParasiteLaunchRequest launch = launchFor(
            objects,
            projectileId,
            tick,
            ParasiteTargetKind::Vehicle,
            organic,
            elite
        );
        const ParasiteOperationResult began = state.beginParasiteLaunch(
            launch,
            sink
        );
        if (!began.accepted || began.idempotent
            || began.actions.size() != 1
            || began.actions.front().kind
                != SpecialMechanismActionKind::ParasiteEnterLimbo) {
            return false;
        }
        objects[0].alive = false;
        objects[0].mission = "ParasiteLimbo";
        const ParasiteOperationResult resolved = state.resolveParasiteImpact(
            impactFor(
                objects,
                projectileId,
                tick,
                ParasiteTargetKind::Vehicle
            ),
            sink
        );
        return resolved.accepted && !resolved.idempotent
            && resolved.actions.size() == 1
            && resolved.actions.front().kind
                == SpecialMechanismActionKind::ParasiteAttachHost
            && state.isParasiteHostParalyzed(objects[1].instanceId);
    };

    RuntimeSpecialMechanismState launchState;
    launchState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> launchObjects = objectsFor(
        80,
        81,
        ParasiteTargetKind::Vehicle
    );
    const ParasiteLaunchRequest launch = launchFor(
        launchObjects,
        1001,
        10,
        ParasiteTargetKind::Vehicle,
        false,
        false
    );
    const ParasiteOperationResult began = launchState.beginParasiteLaunch(
        launch,
        sink
    );
    const ParasiteOperationResult repeatedBegin =
        launchState.beginParasiteLaunch(launch, sink);
    ParasiteLaunchRequest projectileConflict = launch;
    projectileConflict.sourceObjectId = 82;
    const bool projectileIdentityRejected =
        !launchState.beginParasiteLaunch(projectileConflict, sink).accepted;
    ParasiteLaunchRequest sourceConflict = launch;
    sourceConflict.projectileId = 1002;
    const bool duplicateSourceRejected =
        !launchState.beginParasiteLaunch(sourceConflict, sink).accepted;
    ParasiteLaunchRequest targetConflict = launch;
    targetConflict.projectileId = 1003;
    targetConflict.sourceObjectId = 82;
    const bool duplicateTargetRejected =
        !launchState.beginParasiteLaunch(targetConflict, sink).accepted;
    const bool beginContract = began.accepted && !began.idempotent
        && began.actions.size() == 1
        && began.actions.front().kind
            == SpecialMechanismActionKind::ParasiteEnterLimbo
        && launchState.pendingParasiteLaunches().size() == 1
        && launchState.isParasiteSourceLimboed(80)
        && launchState.parasiteTargetForSource(80) == 81
        && repeatedBegin.accepted && repeatedBegin.idempotent
        && repeatedBegin.actions.empty() && projectileIdentityRejected
        && duplicateSourceRejected && duplicateTargetRejected;

    const std::uint64_t midFlightHash = launchState.stateHash();
    std::ostringstream midFlightText;
    launchState.save(midFlightText);
    IniFile midFlightIni;
    RuntimeSpecialMechanismState midFlightRestored;
    midFlightRestored.reset(rules.getSpecialMechanismRules());
    std::string loadError;
    const bool midFlightRoundTrip = midFlightIni.loadFromString(
        midFlightText.str()
    ) && midFlightRestored.load(midFlightIni, loadError)
        && midFlightRestored.stateHash() == midFlightHash
        && midFlightRestored.pendingParasiteLaunches().size() == 1;

    launchObjects[0].alive = false;
    launchObjects[0].mission = "ParasiteLimbo";
    const ParasiteImpactRequest vehicleImpact = impactFor(
        launchObjects,
        1001,
        10,
        ParasiteTargetKind::Vehicle
    );
    const ParasiteOperationResult attached = launchState.resolveParasiteImpact(
        vehicleImpact,
        sink
    );
    const ParasiteOperationResult repeatedImpact =
        launchState.resolveParasiteImpact(vehicleImpact, sink);
    const bool vehicleAttached = attached.accepted && !attached.idempotent
        && attached.actions.size() == 1
        && attached.actions.front().kind
            == SpecialMechanismActionKind::ParasiteAttachHost
        && launchState.pendingParasiteLaunches().empty()
        && launchState.parasiteLinks().size() == 1
        && launchState.isParasiteHostParalyzed(81)
        && repeatedImpact.accepted && repeatedImpact.idempotent
        && repeatedImpact.actions.empty();

    const std::uint64_t midLinkHash = launchState.stateHash();
    std::ostringstream midLinkText;
    launchState.save(midLinkText);
    IniFile midLinkIni;
    RuntimeSpecialMechanismState midLinkRestored;
    midLinkRestored.reset(rules.getSpecialMechanismRules());
    const bool midLinkRoundTrip = midLinkIni.loadFromString(midLinkText.str())
        && midLinkRestored.load(midLinkIni, loadError)
        && midLinkRestored.stateHash() == midLinkHash
        && midLinkRestored.parasiteLinks().size() == 1;
    const std::vector<SpecialMechanismAction> firstBite =
        launchState.tickParasites(launchObjects, rules, 10, sink);
    const std::vector<SpecialMechanismAction> duplicateBite =
        launchState.tickParasites(launchObjects, rules, 10, sink);
    const bool biteOnce = firstBite.size() == 1
        && firstBite.front().kind
            == SpecialMechanismActionKind::ParasiteDamage
        && firstBite.front().damage == 35 && duplicateBite.empty();

    RuntimeSpecialMechanismState missState;
    missState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> missObjects = objectsFor(
        90,
        91,
        ParasiteTargetKind::Vehicle
    );
    const ParasiteLaunchRequest missLaunch = launchFor(
        missObjects,
        1101,
        20,
        ParasiteTargetKind::Vehicle,
        false,
        false
    );
    const bool missBegan = missState.beginParasiteLaunch(
        missLaunch,
        sink
    ).accepted;
    missObjects[0].alive = false;
    missObjects[0].mission = "ParasiteLimbo";
    ParasiteImpactRequest missImpact = impactFor(
        missObjects,
        1101,
        21,
        ParasiteTargetKind::Vehicle
    );
    missImpact.impact = ParasiteImpactKind::Miss;
    missImpact.targetPresent = false;
    missImpact.targetAlive = false;
    const ParasiteOperationResult missed = missState.resolveParasiteImpact(
        missImpact,
        sink
    );
    const ParasiteOperationResult repeatedMiss =
        missState.resolveParasiteImpact(missImpact, sink);
    const bool missReturned = missBegan && missed.accepted
        && missed.actions.size() == 1
        && missed.actions.front().kind
            == SpecialMechanismActionKind::ParasiteReturnFromLimbo
        && !missed.actions.front().replacesTerminatingHost
        && repeatedMiss.accepted && repeatedMiss.idempotent
        && missState.pendingParasiteLaunches().empty();

    RuntimeSpecialMechanismState targetDeathState;
    targetDeathState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> targetDeathObjects = objectsFor(
        92,
        93,
        ParasiteTargetKind::Vehicle
    );
    const bool targetDeathBegan = targetDeathState.beginParasiteLaunch(
        launchFor(
            targetDeathObjects,
            1102,
            30,
            ParasiteTargetKind::Vehicle,
            false,
            false
        ),
        sink
    ).accepted;
    targetDeathObjects[0].alive = false;
    targetDeathObjects[0].mission = "ParasiteLimbo";
    targetDeathObjects[1].alive = false;
    targetDeathObjects[1].health = 0;
    const std::vector<SpecialMechanismAction> targetDeathActions =
        targetDeathState.reconcileParasites(
            targetDeathObjects,
            {1102},
            31,
            sink
        );
    const bool targetDeathReturned = targetDeathBegan
        && targetDeathActions.size() == 1
        && targetDeathActions.front().kind
            == SpecialMechanismActionKind::ParasiteReturnFromLimbo;

    RuntimeSpecialMechanismState missingProjectileState;
    missingProjectileState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> missingProjectileObjects = objectsFor(
        94,
        95,
        ParasiteTargetKind::Vehicle
    );
    const bool missingProjectileBegan =
        missingProjectileState.beginParasiteLaunch(
            launchFor(
                missingProjectileObjects,
                1103,
                40,
                ParasiteTargetKind::Vehicle,
                false,
                false
            ),
            sink
        ).accepted;
    missingProjectileObjects[0].alive = false;
    missingProjectileObjects[0].mission = "ParasiteLimbo";
    const std::vector<SpecialMechanismAction> missingProjectileActions =
        missingProjectileState.reconcileParasites(
            missingProjectileObjects,
            {},
            41,
            sink
        );
    const bool missingProjectileReturned = missingProjectileBegan
        && missingProjectileActions.size() == 1
        && missingProjectileActions.front().kind
            == SpecialMechanismActionKind::ParasiteReturnFromLimbo;

    RuntimeSpecialMechanismState infantryState;
    infantryState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> infantryObjects = objectsFor(
        100,
        101,
        ParasiteTargetKind::Infantry
    );
    const bool infantryBegan = infantryState.beginParasiteLaunch(
        launchFor(
            infantryObjects,
            1201,
            50,
            ParasiteTargetKind::Infantry,
            false,
            false
        ),
        sink
    ).accepted;
    infantryObjects[0].alive = false;
    infantryObjects[0].mission = "ParasiteLimbo";
    const ParasiteOperationResult infantryHit =
        infantryState.resolveParasiteImpact(
            impactFor(
                infantryObjects,
                1201,
                51,
                ParasiteTargetKind::Infantry
            ),
            sink
        );
    const bool infantryKilledAndReturned = infantryBegan
        && infantryHit.accepted && infantryHit.actions.size() == 2
        && infantryHit.actions[0].kind
            == SpecialMechanismActionKind::ParasiteKillTarget
        && infantryHit.actions[0].forceDestruction
        && infantryHit.actions[1].kind
            == SpecialMechanismActionKind::ParasiteReturnFromLimbo
        && infantryHit.actions[1].replacesTerminatingHost
        && infantryState.parasiteLinks().empty();

    const auto cullingProbe = [
        &rules,
        &objectsFor,
        &beginAndAttach,
        &sink
    ](bool elite, int baseId) {
        RuntimeSpecialMechanismState state;
        state.reset(rules.getSpecialMechanismRules());
        std::vector<WorldObjectInstance> objects = objectsFor(
            baseId,
            baseId + 1,
            ParasiteTargetKind::Vehicle
        );
        if (!beginAndAttach(
                state,
                objects,
                baseId + 5000,
                60,
                false,
                elite
            )) {
            return false;
        }
        objects[1].health = 70;
        const std::vector<SpecialMechanismAction> actions =
            state.tickParasites(objects, rules, 60, sink);
        return actions.size() == 1
            && actions.front().kind == (elite
                ? SpecialMechanismActionKind::ParasiteKillTarget
                : SpecialMechanismActionKind::ParasiteDamage);
    };
    const bool cullingSnapshots = cullingProbe(false, 110)
        && cullingProbe(true, 112);

    const auto hostEventProbe = [
        &rules,
        &objectsFor,
        &beginAndAttach,
        &sink
    ](
        ParasiteHostEventKind event,
        bool organic,
        bool ejectCellAvailable,
        SpecialMechanismActionKind expected,
        bool expectedSuppression,
        bool expectedStun,
        int baseId
    ) {
        RuntimeSpecialMechanismState state;
        state.reset(rules.getSpecialMechanismRules());
        std::vector<WorldObjectInstance> objects = objectsFor(
            baseId,
            baseId + 1,
            ParasiteTargetKind::Vehicle
        );
        if (!beginAndAttach(
                state,
                objects,
                baseId + 6000,
                70,
                organic,
                false
            )) {
            return false;
        }
        ParasiteHostEventRequest request;
        request.currentTick = 71;
        request.targetObjectId = baseId + 1;
        request.event = event;
        request.ejectCellAvailable = ejectCellAvailable;
        request.ejectCell = {8, 9};
        request.stunTicks = 150;
        const ParasiteOperationResult result = state.handleParasiteHostEvent(
            request,
            sink
        );
        const ParasiteOperationResult repeated = state.handleParasiteHostEvent(
            request,
            sink
        );
        return result.accepted && result.actions.size() == 1
            && result.actions.front().kind == expected
            && result.actions.front().suppressDeathWeapon
                == expectedSuppression
            && (result.actions.front().stunTicks > 0) == expectedStun
            && result.actions.front().replacesTerminatingHost
                == (expected == SpecialMechanismActionKind::ParasiteEject
                    && event == ParasiteHostEventKind::DestroyedByParasite)
            && result.actions.front().sourceWasAttachedToHost
            && repeated.accepted && repeated.idempotent
            && repeated.actions.empty() && state.parasiteLinks().empty();
    };
    const bool hostEvents =
        hostEventProbe(
            ParasiteHostEventKind::Healed,
            false,
            true,
            SpecialMechanismActionKind::ParasiteKillParasite,
            true,
            false,
            120
        )
        && hostEventProbe(
            ParasiteHostEventKind::Healed,
            true,
            true,
            SpecialMechanismActionKind::ParasiteEject,
            false,
            true,
            122
        )
        && hostEventProbe(
            ParasiteHostEventKind::SonicHit,
            false,
            true,
            SpecialMechanismActionKind::ParasiteEject,
            false,
            true,
            124
        )
        && hostEventProbe(
            ParasiteHostEventKind::Teleporting,
            false,
            true,
            SpecialMechanismActionKind::ParasiteEject,
            false,
            true,
            126
        )
        && hostEventProbe(
            ParasiteHostEventKind::DestroyedExternally,
            true,
            true,
            SpecialMechanismActionKind::ParasiteKillParasite,
            true,
            false,
            128
        )
        && hostEventProbe(
            ParasiteHostEventKind::DestroyedByParasite,
            false,
            true,
            SpecialMechanismActionKind::ParasiteEject,
            false,
            false,
            130
        )
        && hostEventProbe(
            ParasiteHostEventKind::Teleporting,
            true,
            false,
            SpecialMechanismActionKind::ParasiteKillParasite,
            true,
            false,
            132
        );

    RuntimeSpecialMechanismState sourceDeathState;
    sourceDeathState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> sourceDeathObjects = objectsFor(
        140,
        141,
        ParasiteTargetKind::Vehicle
    );
    const bool sourceDeathAttached = beginAndAttach(
        sourceDeathState,
        sourceDeathObjects,
        1401,
        80,
        false,
        false
    );
    sourceDeathObjects[0].health = 0;
    const std::vector<SpecialMechanismAction> sourceDeathActions =
        sourceDeathState.tickParasites(
            sourceDeathObjects,
            rules,
            81,
            sink
        );
    const bool sourceDeathReleased = sourceDeathAttached
        && sourceDeathActions.size() == 1
        && sourceDeathActions.front().kind
            == SpecialMechanismActionKind::ParasiteReleaseTarget
        && sourceDeathState.parasiteLinks().empty();

    IniFile legacyIni;
    const bool legacyParsed = legacyIni.loadFromString(
        "[SpecialMechanisms]\n"
        "NextElectricArcId=1\n"
        "NextRadiationAreaId=1\n"
        "[SpecialParasites]\n"
        "Count=1\n"
        "0=151|150|A|PARASITEGUN|ParasiteWH|35|0|4|4|no|32767|yes|yes\n"
    );
    RuntimeSpecialMechanismState legacyState;
    legacyState.reset(rules.getSpecialMechanismRules());
    const bool legacyLoaded = legacyParsed
        && legacyState.load(legacyIni, loadError)
        && legacyState.parasiteLinks().size() == 1
        && legacyState.parasiteLinks().begin()->second
            .cullingThresholdPerMyriad == 0;

    RuntimeSpecialMechanismState atomicState;
    atomicState.reset(rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> atomicObjects = objectsFor(
        160,
        161,
        ParasiteTargetKind::Vehicle
    );
    const bool atomicAttached = beginAndAttach(
        atomicState,
        atomicObjects,
        1601,
        90,
        false,
        false
    );
    const std::uint64_t atomicHash = atomicState.stateHash();
    IniFile badIni;
    const bool badParsed = badIni.loadFromString(
        "[SpecialMechanisms]\n"
        "Version=2\n"
        "[SpecialParasitePending]\n"
        "Count=1\n"
        "0=bad\n"
        "[SpecialParasites]\n"
        "Count=0\n"
        "[SpecialParasiteResolved]\n"
        "Count=0\n"
    );
    const bool badRejectedAtomically = badParsed
        && !atomicState.load(badIni, loadError)
        && atomicState.stateHash() == atomicHash;
    IniFile capIni;
    const bool capParsed = capIni.loadFromString(
        "[SpecialMechanisms]\n"
        "Version=2\n"
        "[SpecialParasitePending]\n"
        "Count=4097\n"
        "[SpecialParasites]\n"
        "Count=0\n"
        "[SpecialParasiteResolved]\n"
        "Count=0\n"
    );
    const bool capRejectedAtomically = capParsed
        && !atomicState.load(capIni, loadError)
        && atomicState.stateHash() == atomicHash;

    detail = "begin=" + boolText(beginContract)
        + " flightSave=" + boolText(midFlightRoundTrip)
        + " attach=" + boolText(vehicleAttached)
        + " linkSave=" + boolText(midLinkRoundTrip)
        + " bite=" + boolText(biteOnce)
        + " return=" + boolText(
            missReturned && targetDeathReturned
                && missingProjectileReturned
        )
        + " infantry=" + boolText(infantryKilledAndReturned)
        + " culling=" + boolText(cullingSnapshots)
        + " hostEvents=" + boolText(hostEvents)
        + " release=" + boolText(sourceDeathReleased)
        + " legacy=" + boolText(legacyLoaded)
        + " atomic=" + boolText(
            atomicAttached && badRejectedAtomically
                && capRejectedAtomically
        );
    return beginContract && midFlightRoundTrip && vehicleAttached
        && midLinkRoundTrip && biteOnce && missReturned
        && targetDeathReturned && missingProjectileReturned
        && infantryKilledAndReturned && cullingSnapshots && hostEvents
        && sourceDeathReleased && legacyLoaded && atomicAttached
        && badRejectedAtomically && capRejectedAtomically;
}

} // namespace

int runRuntimeSpecialMechanismsSmokeTest() {
    const RulesDatabase rules = smokeRules();
    const char* requestedRaw = std::getenv("CONCORD_SPECIAL_MECHANISM");
    const std::string requested = requestedRaw == nullptr ? "all" : requestedRaw;
    std::map<std::string, std::pair<bool, std::string>> results;
    auto run = [&](const std::string& name, const auto& function) {
        if (requested != "all" && requested != name) {
            return;
        }
        std::string detail;
        results[name] = {function(rules, detail), detail};
    };
    run("mind-control", smokeMindControl);
    run("camouflage", smokeCamouflage);
    run("chrono-temporal", smokeChronoAndTemporal);
    run("electric", smokeElectricOnly);
    run("muzzle-presentation", [](const RulesDatabase& rules, std::string& detail) {
        const auto makeState = [&](int offset, int nativeFixed) {
            RuntimeSpecialMechanismState state;
            state.reset(rules.getSpecialMechanismRules());
            SpawnedMissileState missile;
            missile.objectId = 4; missile.parentObjectId = 1; missile.slot = 0;
            missile.released = true; missile.pitch = 0.5;
            missile.typeId = "DMISL"; missile.parentType = "DRED";
            missile.owner = "A"; missile.weaponId = "DredWeapon";
            missile.warheadId = "HE"; missile.damage = 100;
            missile.originScreenOffsetX = offset; missile.originScreenOffsetY = -offset;
            missile.originNativeFixedX = nativeFixed; missile.originNativeFixedY = -nativeFixed;
            state.accessSpawnedMissiles().emplace(missile.objectId, missile);
            state.addElectricArc(1, 2, {3, 4, 1}, {7, 8, 0}, "0,0,255",
                false, false, 0, 10, nullptr, offset, -offset, nativeFixed, -nativeFixed);
            return state;
        };
        auto state = makeState(7, 11);
        const auto hash = state.stateHash();
        auto differentDisplay = makeState(25, 31);
        const bool presentationExcluded = differentDisplay.stateHash() == hash
            && makeState(7, 31).stateHash() == hash;
        differentDisplay.accessSpawnedMissiles().at(4).x += 1;
        const bool flightIncluded = differentDisplay.stateHash() != hash;
        std::ostringstream saved; state.save(saved);
        std::string loadError;
        const auto load = [&](const std::string& text, RuntimeSpecialMechanismState& restored) {
            IniFile ini; std::string error;
            restored.reset(rules.getSpecialMechanismRules());
            const bool success = ini.loadFromString(text) && restored.load(ini, error);
            if (!success) loadError = error;
            return success;
        };
        RuntimeSpecialMechanismState restored;
        const bool loaded = load(saved.str(), restored);
        const auto snapshots = restored.renderSnapshots(0, 15);
        const bool arcOffset = std::any_of(snapshots.begin(), snapshots.end(), [](const auto& snapshot) {
            return snapshot.kind == "electricArc" && snapshot.sourceScreenOffsetX == 7
                && snapshot.sourceScreenOffsetY == -7
                && snapshot.sourceNativeFixedX == 11 && snapshot.sourceNativeFixedY == -11
                && snapshot.sourceX == 3 && snapshot.sourceY == 4;
        });
        const bool roundTrip = loaded && restored.stateHash() == hash && arcOffset
            && restored.spawnedMissiles().at(4).originScreenOffsetX == 7
            && restored.spawnedMissiles().at(4).originScreenOffsetY == -7
            && restored.spawnedMissiles().at(4).originNativeFixedX == 11
            && restored.spawnedMissiles().at(4).originNativeFixedY == -11;
        const auto legacyText = [&](int version) {
            std::istringstream input(saved.str()); std::ostringstream output;
            std::string line, section;
            while (std::getline(input, line)) {
                if (!line.empty() && line.front() == '[') section = line;
                if (section == "[SpecialSpawnedMissiles]" && line.rfind("Version=", 0) == 0)
                    line = "Version=" + std::to_string(version);
                if ((section == "[SpecialSpawnedMissiles]" || section == "[SpecialElectricArcs]")
                    && line.rfind("0=", 0) == 0) {
                    const int trim = section == "[SpecialSpawnedMissiles]"
                        ? (version == 1 ? 5 : version == 2 ? 4 : 2)
                        : (version == 3 ? 2 : 4);
                    for (int i = 0; i < trim; ++i) line.resize(line.find_last_of('|'));
                }
                output << line << '\n';
            }
            return output.str();
        };
        bool legacy = true;
        for (int version : {1, 2, 3}) {
            RuntimeSpecialMechanismState older;
            legacy = legacy && load(legacyText(version), older) && older.stateHash() == hash
                && older.spawnedMissiles().at(4).originScreenOffsetX == (version == 3 ? 7 : 0)
                && older.spawnedMissiles().at(4).originScreenOffsetY == (version == 3 ? -7 : 0)
                && older.spawnedMissiles().at(4).originNativeFixedX == 0
                && older.spawnedMissiles().at(4).originNativeFixedY == 0;
            for (const auto& snapshot : older.renderSnapshots(0, 15))
                if (snapshot.kind == "electricArc")
                    legacy = legacy && snapshot.sourceScreenOffsetX == (version == 3 ? 7 : 0)
                        && snapshot.sourceScreenOffsetY == (version == 3 ? -7 : 0)
                        && snapshot.sourceNativeFixedX == 0 && snapshot.sourceNativeFixedY == 0;
        }
        bool malformed = true;
        for (const std::string section : {"SpecialSpawnedMissiles", "SpecialElectricArcs"}) {
            for (int fromEnd = 0; fromEnd < 4; ++fromEnd) {
                for (const std::string badValue : {"2147483648", "-65537", "65537", "1.5", "bad"}) {
                    std::string bad = saved.str();
                    const auto row = bad.find("0=", bad.find("[" + section + "]"));
                    const auto end = bad.find('\n', row);
                    auto fieldEnd = end;
                    for (int i = 0; i < fromEnd; ++i) fieldEnd = bad.rfind('|', fieldEnd - 1);
                    const auto field = bad.rfind('|', fieldEnd - 1);
                    bad.replace(field + 1, fieldEnd - field - 1, badValue);
                    IniFile ini; std::string error;
                    malformed = malformed && ini.loadFromString(bad) && !restored.load(ini, error)
                        && restored.stateHash() == hash;
                    std::ostringstream afterFailure; restored.save(afterFailure);
                    malformed = malformed && afterFailure.str() == saved.str();
                }
            }
        }
        detail = "displayHash=" + boolText(presentationExcluded) + " flightHash=" + boolText(flightIncluded)
            + " saveV4=" + boolText(roundTrip) + " legacyV1V2V3=" + boolText(legacy)
            + " invalidAtomic=" + boolText(malformed) + (loadError.empty() ? "" : " error=" + loadError);
        return presentationExcluded && flightIncluded && roundTrip && legacy && malformed;
    });
    run("radiation", smokeRadiationOnly);
    run("carrier", smokeCarrier);
    run("carrier-hornet-sortie", smokeCarrierHornetSortie);
    run("destroyer-asw-sortie", smokeDestroyerAswSortie);
    run("timed-bomb", smokeTimedBomb);
    run("parasite", smokeParasite);
    run("psychic-detection", [](const RulesDatabase&, std::string& detail) {
        IniFile ini;
        RulesDatabase detectionRules;
        if (!ini.loadFromString("[BuildingTypes]\n0=SENSOR\n[SENSOR]\n"
            "Strength=1000\nPsychicDetectionRadius=5\n")
            || !detectionRules.loadFromIni(ini)) return false;
        std::vector<WorldObjectInstance> objects{
            smokeObject(1, WorldObjectKind::Structure, "A", "SENSOR", 10, 10),
            smokeObject(2, WorldObjectKind::Unit, "B", "TANK", 13, 14),
            smokeObject(3, WorldObjectKind::Unit, "B", "TANK", 16, 10),
            smokeObject(4, WorldObjectKind::Unit, "A", "TANK", 11, 10)
        };
        std::vector<RuntimePsychicIntent> intents{
            {3, -1, {10, 10}, true}, {2, 4, {}, true}, {4, -1, {20, 20}, false}
        };
        const auto snapshots = runtimePsychicDetectionSnapshots(objects,
            detectionRules, intents, "A", smokeAllied);
        const bool boundary = snapshots.size() == 1 && snapshots.front().objectId == 2
            && snapshots.front().targetCell.x == 11 && snapshots.front().phase == "Attack";
        objects.front().poweredOperational = false;
        const bool unpowered = runtimePsychicDetectionSnapshots(objects,
            detectionRules, intents, "A", smokeAllied).empty();
        objects.front().poweredOperational = true;
        objects.front().owner = "B";
        const bool lostSensor = runtimePsychicDetectionSnapshots(objects,
            detectionRules, intents, "A", smokeAllied).empty();
        detail = "radiusBoundary=" + boolText(boundary) + " unpowered="
            + boolText(unpowered) + " ownership=" + boolText(lostSensor);
        return boundary && unpowered && lostSensor;
    });
    run("open-topped-special", [](const RulesDatabase&, std::string& detail) {
        IniFile ini;
        RulesDatabase rules;
        if (!ini.loadFromString("[InfantryTypes]\n0=CONTROL\n1=TARGET\n"
            "[VehicleTypes]\n0=HOST\n[CONTROL]\nStrength=100\n"
            "[TARGET]\nStrength=100\n[HOST]\nStrength=1000\nOpenTopped=yes\n")
            || !rules.loadFromIni(ini)) return false;
        RuntimeSpecialMechanismState state;
        state.reset(rules.getSpecialMechanismRules());
        std::vector<WorldObjectInstance> objects{
            smokeObject(1, WorldObjectKind::Infantry, "A", "CONTROL", 5, 5),
            smokeObject(2, WorldObjectKind::Infantry, "ENEMY", "TARGET", 6, 5),
            smokeObject(3, WorldObjectKind::Infantry, "ENEMY", "TARGET", 7, 5),
            smokeObject(4, WorldObjectKind::Unit, "ALLY", "HOST", 5, 5)
        };
        objects[0].alive = false;
        objects[0].mission = "InsideTransport";
        objects[3].transportPassengerIds = {1};
        const SpecialAlliancePredicate allied = [](const auto& a, const auto& b) {
            return a == b || (a != "ENEMY" && b != "ENEMY");
        };
        std::string reason;
        const bool control = state.tryMindControl(objects, rules, 1, 2, 1,
            false, false, false, 1, allied, {}, reason);
        state.reconcile(objects, 2, {});
        bool completed = false;
        const bool temporal = state.applyTemporalHit(objects, rules, 1, 3,
            10, 100, 2, allied, {}, completed, reason);
        const bool retained = state.mindControlLinks().contains(2)
            && objects[1].owner == "A" && !objects[0].alive && !completed;
        objects[3].alive = false;
        state.reconcile(objects, 3, {});
        const bool released = !state.mindControlLinks().contains(2)
            && objects[1].owner == "ENEMY"
            && !state.applyTemporalHit(objects, rules, 1, 3, 10, 100, 3,
                allied, {}, completed, reason);
        detail = "alliedHost=" + boolText(control && temporal)
            + " hiddenSource=" + boolText(retained) + " hostLoss=" + boolText(released);
        return control && temporal && retained && released;
    });

    bool valid = !results.empty();
    for (const auto& [name, result] : results) {
        std::cout << "special mechanism " << name << " "
                  << (result.first ? "PASS" : "FAIL")
                  << " " << result.second << "\n";
        valid = valid && result.first;
    }
    const RulesDatabase rulesAgain = smokeRules();
    if (requested == "all") {
        std::string firstDetail;
        std::string secondDetail;
        const bool first = smokeMindControl(rules, firstDetail);
        const bool second = smokeMindControl(rulesAgain, secondDetail);
        const std::uint64_t firstHash = stateHashForSmokeMindControl(rules);
        const std::uint64_t secondHash =
            stateHashForSmokeMindControl(rulesAgain);
        valid = valid
            && first
            && second
            && firstDetail == secondDetail
            && firstHash == secondHash;
        std::cout << "special mechanism deterministic "
                  << (firstDetail == secondDetail && firstHash == secondHash
                        ? "PASS" : "FAIL")
                  << " hash=" << firstHash
                  << "\n";
    }
    return valid ? 0 : 1;
}
