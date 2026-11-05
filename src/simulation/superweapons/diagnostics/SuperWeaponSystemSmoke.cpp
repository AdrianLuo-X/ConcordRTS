#include "simulation/superweapons/private/SuperWeaponObjectCellKey.h"
using superweapon_detail::superWeaponObjectCellKey;
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/superweapons/RuntimeChronosphereRelocation.h"
#include "simulation/superweapons/SuperWeaponNuclearFlight.h"
#include "simulation/superweapons/BuildingSuperWeapons.h"
#include "simulation/mod_extensions/RuntimeModExtensions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <iterator>
#include <set>
#include <sstream>
#include <type_traits>
#include <tuple>
#include <unordered_set>

int SuperWeaponSystem::runOneShotSmokeTest() {
    SuperWeaponRule rule;
    rule.id = "CrateNukeSpecial";
    rule.name = rule.id;
    rule.type = "MultiMissile";
    rule.action = "Nuke";
    rule.targetMode = "Area";
    rule.rechargeTimeMinutes = 1.0 / 60.0;
    rule.damage = 600;
    rule.radius = 2;
    rule.targetEnemy = true;
    rule.targetFriendly = true;
    rule.targetInfantry = true;
    rule.targetUnits = true;
    rule.targetBuildings = true;

    SuperWeaponSystem system;
    system.reset(std::vector<SuperWeaponRule>{rule});
    SuperWeaponWorldContext context;
    context.tick = 100;
    context.fixedTickHz = 30;
    context.mapWidth = 16;
    context.mapHeight = 16;
    context.superWeaponsAllowed = true;
    context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
        return x >= 0 && y >= 0 && x < 16 && y < 16;
    };
    int grantedEventCount = 0;
    context.emit = [&](const SuperWeaponEvent& event) {
        if (event.kind == "SuperWeaponGranted") {
            ++grantedEventCount;
        }
    };

    if (!system.grantOneShot("player", rule.id, &context)) {
        std::cerr << "superweapon one-shot smoke: grant failed\n";
        return 1;
    }
    const Instance* virtualInstance = system.findInstance("player", rule.id);
    if (virtualInstance == nullptr
        || !virtualInstance->oneShot
        || virtualInstance->sourceBuildingIds.size() != 1U
        || virtualInstance->sourceBuildingIds.front() != -1
        || grantedEventCount != 1) {
        std::cerr << "superweapon one-shot smoke: invalid virtual source\n";
        return 1;
    }
    const std::uint64_t beforeDuplicateGrant = system.stateHash();
    if (!system.grantOneShot("player", rule.id, &context)
        || system.stateHash() != beforeDuplicateGrant
        || grantedEventCount != 1) {
        std::cerr << "superweapon one-shot smoke: duplicate virtual grant\n";
        return 1;
    }
    if (!system.forceReady("player", rule.id, &context)) {
        std::cerr << "superweapon one-shot smoke: ready failed\n";
        return 1;
    }

    WorldObjectInstance liveSilo;
    liveSilo.instanceId = 7001;
    liveSilo.kind = WorldObjectKind::Structure;
    liveSilo.owner = "player";
    liveSilo.type = "NAMISL";
    liveSilo.health = 100;
    liveSilo.alive = true;
    liveSilo.poweredOperational = true;
    std::vector<WorldObjectInstance> liveObjects{liveSilo};
    SuperWeaponWorldContext liveContext = context;
    liveContext.objects = &liveObjects;
    liveContext.emit = {};
    SuperWeaponSystem liveSourceSystem;
    liveSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!liveSourceSystem.grant(
            "player",
            rule.id,
            liveSilo.instanceId,
            &liveContext
        )
        || !liveSourceSystem.grantOneShot("player", rule.id, &liveContext)) {
        std::cerr << "superweapon one-shot smoke: live source grant failed\n";
        return 1;
    }
    const Instance* liveSource = liveSourceSystem.findInstance(
        "player",
        rule.id
    );
    if (liveSource == nullptr
        || liveSource->oneShot
        || liveSource->sourceBuildingIds.size() != 1U
        || liveSource->sourceBuildingIds.front() != liveSilo.instanceId) {
        std::cerr << "superweapon one-shot smoke: live source lifetime changed\n";
        return 1;
    }

    SuperWeaponSystem repeatingSourceSystem;
    repeatingSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!repeatingSourceSystem.grant("player", rule.id, -1, &liveContext)) {
        std::cerr << "superweapon one-shot smoke: repeating setup failed\n";
        return 1;
    }
    const std::uint64_t beforeRepeatingGrant =
        repeatingSourceSystem.stateHash();
    if (!repeatingSourceSystem.grantOneShot("player", rule.id, &liveContext)
        || repeatingSourceSystem.stateHash() != beforeRepeatingGrant) {
        std::cerr << "superweapon one-shot smoke: repeating lifetime changed\n";
        return 1;
    }

    std::vector<WorldObjectInstance> staleObjects{liveSilo};
    SuperWeaponWorldContext staleContext = context;
    staleContext.objects = &staleObjects;
    staleContext.emit = {};
    SuperWeaponSystem staleSourceSystem;
    staleSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!staleSourceSystem.grant(
            "player",
            rule.id,
            liveSilo.instanceId,
            &staleContext
        )
        || !staleSourceSystem.forceReady("player", rule.id, &staleContext)) {
        std::cerr << "superweapon one-shot smoke: stale source setup failed\n";
        return 1;
    }
    staleObjects.front().alive = false;
    staleObjects.front().health = 0;
    if (!staleSourceSystem.grantOneShot("player", rule.id, &staleContext)) {
        std::cerr << "superweapon one-shot smoke: stale source grant failed\n";
        return 1;
    }
    const Instance* staleSource = staleSourceSystem.findInstance(
        "player",
        rule.id
    );
    if (staleSource == nullptr
        || !staleSource->oneShot
        || staleSource->sourceBuildingIds.size() != 1U
        || staleSource->sourceBuildingIds.front() != -1) {
        std::cerr << "superweapon one-shot smoke: stale source survived\n";
        return 1;
    }
    std::string staleReason;
    if (!staleSourceSystem.beginTargetSelection(
            "player",
            rule.id,
            staleContext,
            &staleReason
        )
        || !staleSourceSystem.submitTarget(
            "player",
            rule.id,
            {{4, 4}, -1},
            staleContext,
            &staleReason
        )) {
        std::cerr << "superweapon one-shot smoke: stale source launch failed: "
                  << staleReason << "\n";
        return 1;
    }
    for (int tick = 101; tick <= 220; ++tick) {
        staleContext.tick = tick;
        staleSourceSystem.tick(staleContext);
    }
    if (staleSourceSystem.stateOf("player", rule.id)
            != SuperWeaponRuntimeState::Unavailable) {
        std::cerr << "superweapon one-shot smoke: stale source recharged\n";
        return 1;
    }

    std::ostringstream encoded;
    system.save(encoded);
    IniFile ini;
    SuperWeaponSystem restored;
    restored.reset(std::vector<SuperWeaponRule>{rule});
    std::string loadError;
    if (!ini.loadFromString(encoded.str())
        || !restored.load(ini, &loadError)
        || restored.stateHash() != system.stateHash()) {
        std::cerr << "superweapon one-shot smoke: roundtrip failed: "
                  << loadError << "\n";
        return 1;
    }

    std::string reason;
    if (!restored.beginTargetSelection(
            "player",
            rule.id,
            context,
            &reason
        )
        || !restored.submitTarget(
            "player",
            rule.id,
            {{4, 4}, -1},
            context,
            &reason
        )) {
        std::cerr << "superweapon one-shot smoke: launch failed: "
                  << reason << "\n";
        return 1;
    }
    for (int tick = 101; tick <= 220; ++tick) {
        context.tick = tick;
        restored.tick(context);
    }
    if (restored.stateOf("player", rule.id)
            != SuperWeaponRuntimeState::Unavailable) {
        std::cerr << "superweapon one-shot smoke: virtual source recharged\n";
        return 1;
    }
    std::cout << "superweapon one-shot smoke: passed\n";
    return 0;
}

#include "simulation/superweapons/diagnostics/SuperWeaponDetailSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponRechargeSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponNoCooldownSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponForceShieldGroundSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponChronosphereGroundSmoke.inl"

int SuperWeaponSystem::runSmokeTest() {
    if (!smokeSuperWeaponChronosphereGround()) return 1;
    if (!smokeSuperWeaponForceShieldGround()) return 1;
    if (!smokeSuperWeaponConcurrentRecharge()) return 1;
    if (!smokeSuperWeaponNoCooldown()) return 1;
    if (!smokeSuperWeaponDetails()) {
        std::cerr << "superweapon detail timing/cloud/save-resume FAIL\n";
        return 1;
    }
    std::vector<SuperWeaponRule> rules;
    auto addRule = [&](const std::string& id, const std::string& type, const std::string& mode) {
        SuperWeaponRule rule;
        rule.id = id;
        rule.name = id;
        rule.type = type;
        rule.action = type;
        rule.targetMode = mode;
        rule.rechargeTimeMinutes = 1.0 / 60.0;
        rule.targetEnemy = true;
        rule.targetFriendly = false;
        rule.targetInfantry = true;
        rule.targetUnits = true;
        rule.targetBuildings = true;
        rules.push_back(std::move(rule));
    };
    addRule("NukeSpecial", "MultiMissile", "Area");
    rules.back().damage = 600;
    rules.back().radius = 2;
    rules.back().payloadWarhead = "NUKE";
    rules.back().payloadProjectile = "NukeDown";
    rules.back().impactAnim = "NUKEANIM";
    addRule("LightningStormSpecial", "LightningStorm", "Area");
    rules.back().damage = 30;
    rules.back().payloadWarhead = "BOLTWH";
    rules.back().boltAnim = "BOLTANIM";
    rules.back().cloudAnim = "CLOUDANIM";
    rules.back().cellSpread = 2;
    rules.back().defermentFrames = 2;
    rules.back().durationFrames = 20;
    rules.back().scatterDelayFrames = 2;
    rules.back().hitDelayFrames = 4;
    addRule("IronCurtainSpecial", "IronCurtain", "Area");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().radius = 2;
    rules.back().durationFrames = 30;
    addRule("ForceShieldSpecial", "ForceShield", "Area");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().targetInfantry = false;
    rules.back().targetUnits = false;
    rules.back().targetBuildings = true;
    rules.back().radius = 2;
    rules.back().durationFrames = 30;
    rules.back().blackoutDurationFrames = 60;
    addRule("ChronoSphereSpecial", "ChronoSphere", "TwoStage");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().radius = 1;
    addRule("PsychicDominatorSpecial", "PsychicDominator", "Area");
    rules.back().captureRange = 1;
    rules.back().durationFrames = 15;
    rules.back().warhead = "PSYWH";
    rules.back().firstAnim = "PDFXCLD";
    rules.back().secondAnim = "PDFXEND";
    addRule("GeneticConverterSpecial", "GeneticConverter", "Area");
    rules.back().range = 2;
    rules.back().targetFriendly = true;
    rules.back().mutationType = "BRUTE";
    addRule("ParaDropSpecial", "ParaDrop", "Cell");
    rules.back().planeType = "DROPPLANE";
    rules.back().infantryTypes = "E1";
    rules.back().infantryCounts = "2";
    addRule("SpyPlaneSpecial", "SpyPlane", "Cell");
    rules.back().radius = 4;
    rules.back().planeType = "DROPPLANE";
    addRule("PsychicRevealSpecial", "PsychicReveal", "Cell");
    rules.back().radius = 4;

    IniFile semanticIni;
    if (!semanticIni.loadFromString(R"ini(
[InfantryTypes]
0=E1
1=PSIIMMUNE
2=DOG
3=ROBOT
4=BRUTE

[VehicleTypes]
0=TANK

[AircraftTypes]
0=DROPPLANE

[BuildingTypes]
0=ALLYHQ

[General]
AISuperDefenseProbability=120,50,-10
AISuperDefenseFrames=25
AISuperDefenseDistance=-4

[SuperWeaponTypes]
0=ForceShieldSpecial
1=GeneticConverterSpecial
2=ChronoPrimarySpecial
3=ChronoPostSpecial

[E1]
Strength=100

[PSIIMMUNE]
Strength=100
ImmuneToPsionics=yes

[DOG]
Strength=100
NotHuman=yes

[ROBOT]
Strength=100
Organic=no

[BRUTE]
Strength=200
ImmuneToPsionics=yes

[TANK]
Strength=300

[DROPPLANE]
Strength=300
Speed=15

[ALLYHQ]
Strength=1000

[ForceShieldSpecial]
Type=ForceShield
Action=ForceShield
Range=3.4

[GeneticConverterSpecial]
Type=GeneticConverter
Action=GeneticConverter
Range=5

[ChronoPrimarySpecial]
UIName=Name:ChronoPrimary
Name=Chrono Primary
Type=ChronoPrimary
Action=ChronoPrimary
RechargeTime=.25
RechargeVoice=ReadyVoice
ChargingVoice=ChargingVoice
ImpatientVoice=ImpatientVoice
SuspendVoice=SuspendVoice
Range=1.5
LineMultiplier=0
PreClick=yes
ShowTimer=yes
AIDefendAgainst=yes

[ChronoPostSpecial]
Type=ChronoWarp
Action=ChronoWarp
PostClick=yes
PreDependent=ChronoPrimary
Range=8
LineMultiplier=4
FlashSidebarTabFrames=0
)ini")) {
        std::cerr << "superweapon smoke: semantic fixture parse failed\n";
        return 1;
    }
    RulesDatabase semanticRules;
    if (!semanticRules.loadFromIni(semanticIni)) {
        std::cerr << "superweapon smoke: semantic rules load failed\n";
        return 1;
    }
    const UnitType* humanType = semanticRules.findUnitType("InfantryType", "E1");
    const UnitType* dogType = semanticRules.findUnitType("InfantryType", "DOG");
    const UnitType* robotType = semanticRules.findUnitType("InfantryType", "ROBOT");
    const SuperWeaponRule* parsedForce =
        semanticRules.findSuperWeaponType("ForceShieldSpecial");
    const SuperWeaponRule* parsedMutator =
        semanticRules.findSuperWeaponType("GeneticConverterSpecial");
    const SuperWeaponRule* parsedPrimary =
        semanticRules.findSuperWeaponType("ChronoPrimarySpecial");
    const SuperWeaponRule* parsedPost =
        semanticRules.findSuperWeaponType("ChronoPostSpecial");
    const AiSuperDefenseRules& parsedAiDefense =
        semanticRules.getSpecialMechanismRules().aiSuperDefense;
    if (humanType == nullptr || !humanType->organic || humanType->notHuman
        || dogType == nullptr || !dogType->notHuman
        || robotType == nullptr || robotType->organic
        || parsedForce == nullptr || parsedForce->targetInfantry
        || parsedForce->targetUnits || !parsedForce->targetBuildings
        || parsedMutator == nullptr || parsedMutator->mutationType != "BRUTE"
        || !parsedMutator->targetFriendly || !parsedMutator->targetEnemy
        || parsedPrimary == nullptr || !parsedPrimary->preClick
        || parsedPrimary->postClick
        || parsedPrimary->targetMode != "TwoStage"
        || std::abs(parsedPrimary->rechargeTimeMinutes - 0.25) > 0.0001
        || parsedPrimary->lineMultiplier != 0
        || parsedPrimary->flashSidebarTabFrames != -1
        || !parsedPrimary->showTimer || !parsedPrimary->aiDefendAgainst
        || parsedPost == nullptr || !parsedPost->postClick
        || parsedPost->targetMode != "Cell"
        || parsedPost->flashSidebarTabFrames != 0
        || parsedAiDefense.probabilityPercent
            != std::array<int, 3>{{100, 50, 0}}
        || parsedAiDefense.alertFrames != 25
        || parsedAiDefense.distanceCells != 0
        || std::abs(parsedForce->rechargeTimeMinutes - 5.0) > 0.0001) {
        std::cerr << "superweapon smoke: stock target/type parsing mismatch\n";
        return 1;
    }

    SuperWeaponSystem system;
    system.reset(rules);
    std::vector<WorldObjectInstance> objects;
    objects.push_back({100, WorldObjectKind::Unit, "", "enemy", "TANK", 100, 10, 10, 330, 0});
    objects.push_back({101, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 12, 12, 396, 0});
    objects.push_back({102, WorldObjectKind::Unit, "", "player", "TANK", 100, 4, 4, 132, 0});
    objects.push_back({103, WorldObjectKind::Infantry, "", "enemy", "E1", 73, 20, 8, 276, 0});
    objects.push_back({104, WorldObjectKind::Structure, "", "player", "ALLYHQ", 1000, 4, 5, 164, 0});
    objects.push_back({105, WorldObjectKind::Infantry, "", "player", "E1", 61, 20, 9, 308, 0});
    objects.push_back({106, WorldObjectKind::Infantry, "", "enemy", "PSIIMMUNE", 100, 19, 8, 275, 0});
    objects.push_back({107, WorldObjectKind::Infantry, "", "enemy", "DOG", 100, 19, 9, 307, 0});
    objects.push_back({108, WorldObjectKind::Infantry, "", "enemy", "ROBOT", 100, 21, 8, 277, 0});
    objects.push_back({109, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 21, 9, 309, 0});
    objects.back().airborne = true;
    objects.push_back({110, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 20, 7, 244, 0});
    objects.push_back({111, WorldObjectKind::Structure, "", "ally", "ALLYHQ", 1000, 5, 5, 165, 0});
    objects.push_back({112, WorldObjectKind::Unit, "", "enemy", "TANK", 1000, 9, 10, 329, 0});
    objects.push_back({113, WorldObjectKind::Infantry, "", "enemy", "E1", 20, 15, 15, 495, 0});
    objects.push_back({114, WorldObjectKind::Infantry, "", "player", "E1", 100, 2, 4, 130, 0});
    objects.push_back({115, WorldObjectKind::Unit, "", "player", "TANK", 100, 5, 4, 133, 0});
    objects.push_back({116, WorldObjectKind::Unit, "", "enemy", "TANK", 1000, 21, 20, 661, 0});

    std::vector<SuperWeaponEvent> events;
    int nextObjectId = 1000;
    SuperWeaponWorldContext context;
    context.fixedTickHz = 30;
    context.mapWidth = 32;
    context.mapHeight = 32;
    context.objects = &objects;
    context.rules = &semanticRules;
    context.superWeaponsAllowed = true;
    context.areAllied = [](const std::string& left, const std::string& right) {
        const std::string leftKey = lowerAscii(left);
        const std::string rightKey = lowerAscii(right);
        return leftKey == rightKey
            || (leftKey == "player" && rightKey == "ally")
            || (leftKey == "ally" && rightKey == "player");
    };
    context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
        return x >= 0 && y >= 0 && x < 32 && y < 32;
    };
    context.cellOccupied = [&](int x, int y) {
        for (const WorldObjectInstance& object : objects) {
            if (object.alive && object.x == x && object.y == y) {
                return true;
            }
        }
        return false;
    };
    context.ownerPowered = [](const std::string&) { return true; };
    context.geneticMutatorEligible = [](const WorldObjectInstance& object) {
        return object.instanceId != 110;
    };
    context.allocateObjectId = [&]() { return ++nextObjectId; };
    std::vector<std::pair<int, std::string>> createdObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> damagedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> removedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> relocatedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> ownerChangedObjects;
    std::vector<SuperWeaponImpactEvent> impactEvents;
    std::vector<SuperWeaponActiveLifecycleEvent> activeLifecycleEvents;
    std::vector<std::pair<std::string, int>> lifecycleOrder;
    int nextCallbackSentinelId = -1000;
    context.onObjectCreated = [&](WorldObjectInstance& object) {
        const int createdId = object.instanceId;
        const std::string createdType = object.type;
        createdObjects.emplace_back(createdId, createdType);

        // Deliberately make back() refer to a different object and permit a
        // vector reallocation.  Creation paths must retain the created ID
        // before invoking this callback.
        WorldObjectInstance sentinel;
        sentinel.instanceId = nextCallbackSentinelId--;
        sentinel.type = "CALLBACK_SENTINEL";
        sentinel.health = 0;
        sentinel.alive = false;
        objects.push_back(std::move(sentinel));
    };
    context.onObjectDamaged = [&](SuperWeaponObjectLifecycleEvent event) {
        damagedObjects.push_back(std::move(event));
    };
    context.onObjectRemoved = [&](SuperWeaponObjectLifecycleEvent event) {
        removedObjects.push_back(std::move(event));
    };
    context.onObjectRelocated = [&](SuperWeaponObjectLifecycleEvent event) {
        lifecycleOrder.emplace_back("Relocated", event.objectId);
        relocatedObjects.push_back(std::move(event));
    };
    context.onObjectOwnerChanged = [&](SuperWeaponObjectLifecycleEvent event) {
        lifecycleOrder.emplace_back("OwnerChanged", event.objectId);
        ownerChangedObjects.push_back(std::move(event));
    };
    context.onSuperWeaponImpact = [&](SuperWeaponImpactEvent event) {
        impactEvents.push_back(std::move(event));
    };
    context.onSuperWeaponActiveLifecycle = [
        &activeLifecycleEvents
    ](SuperWeaponActiveLifecycleEvent event) {
        activeLifecycleEvents.push_back(std::move(event));
    };
    context.emit = [&](const SuperWeaponEvent& event) {
        if (event.kind == "ChronoWarpIn"
            || event.kind == "PsychicDominatorControlled") {
            lifecycleOrder.emplace_back(event.kind, event.objectId);
        }
        events.push_back(event);
    };

    {
        SuperWeaponRule voiceRule;
        voiceRule.id = "VoiceSpecial";
        voiceRule.uiName = "Name:VoiceSpecial";
        voiceRule.name = "Voice Test";
        voiceRule.type = "MultiMissile";
        voiceRule.action = "MultiMissile";
        voiceRule.targetMode = "Area";
        voiceRule.rechargeTimeMinutes = 2.0 / (60.0 * 30.0);
        voiceRule.rechargeVoice = "ReadyVoice";
        voiceRule.chargingVoice = "ChargingVoice";
        voiceRule.impatientVoice = "ImpatientVoice";
        voiceRule.suspendVoice = "SuspendVoice";
        voiceRule.range = 9.0;
        voiceRule.lineMultiplier = 0;
        voiceRule.flashSidebarTabFrames = 17;
        voiceRule.showTimer = true;

        SuperWeaponSystem voiceSystem;
        voiceSystem.reset(std::vector<SuperWeaponRule>{voiceRule});
        std::vector<SuperWeaponEvent> voiceEvents;
        std::vector<WorldObjectInstance> noVoiceTargets;
        bool voicePowered = true;
        SuperWeaponWorldContext voiceContext;
        voiceContext.fixedTickHz = 30;
        voiceContext.mapWidth = 16;
        voiceContext.mapHeight = 16;
        voiceContext.objects = &noVoiceTargets;
        voiceContext.ownerPowered = [&](const std::string&) {
            return voicePowered;
        };
        voiceContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 16 && y < 16;
        };
        voiceContext.emit = [&](const SuperWeaponEvent& event) {
            voiceEvents.push_back(event);
        };

        std::string voiceReason;
        voiceContext.tick = 0;
        if (!voiceSystem.grant(
                "voice",
                "VoiceSpecial",
                -1,
                &voiceContext
            )) {
            std::cerr << "superweapon smoke: voice grant failed\n";
            return 1;
        }
        voiceSystem.tick(voiceContext);
        const SuperWeaponSidebarSnapshot* chargingSnapshot =
            voiceSystem.findSidebarSnapshot("voice", "VoiceSpecial");
        if (chargingSnapshot == nullptr
            || chargingSnapshot->uiName != "Name:VoiceSpecial"
            || chargingSnapshot->name != "Voice Test"
            || chargingSnapshot->state != SuperWeaponRuntimeState::Charging
            || chargingSnapshot->rechargeTicks != 2
            || chargingSnapshot->remainingTicks != 1
            || chargingSnapshot->stateStartTick != 0
            || chargingSnapshot->flashSidebarTabFrames != 17
            || std::abs(chargingSnapshot->targetRange - 9.0) > 0.0001
            || chargingSnapshot->targetLineMultiplier != 0
            || !chargingSnapshot->showTimer
            || chargingSnapshot->firstTargetAccepted) {
            std::cerr << "superweapon smoke: sidebar timer metadata mismatch\n";
            return 1;
        }
        if (voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: charging click was accepted\n";
            return 1;
        }

        voicePowered = false;
        voiceContext.tick = 1;
        voiceSystem.tick(voiceContext);
        voiceContext.tick = 2;
        voiceSystem.tick(voiceContext);
        voicePowered = true;
        voiceContext.tick = 3;
        voiceSystem.tick(voiceContext);
        voiceContext.tick = 4;
        voiceSystem.tick(voiceContext);
        if (!voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )
            || !voiceSystem.submitTarget(
                "voice",
                "VoiceSpecial",
                {{3, 3}, -1},
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: ready voice launch failed: "
                      << voiceReason << "\n";
            return 1;
        }
        const int impatientBeforeBlockedStates = static_cast<int>(
            std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [](const SuperWeaponEvent& event) {
                    return event.kind == "SuperWeaponImpatient";
                }
            )
        );
        voiceContext.tick = 5;
        voiceSystem.tick(voiceContext);
        if (voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )
            || !voiceSystem.disable(
                "voice",
                "VoiceSpecial",
                &voiceContext
            )
            || voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: active/unavailable click edge mismatch\n";
            return 1;
        }

        const auto voiceCount = [&](const std::string& intent) {
            return static_cast<int>(std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [&](const SuperWeaponEvent& event) {
                    return event.evaIntent == intent;
                }
            ));
        };
        const int impatientAfterBlockedStates = static_cast<int>(
            std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [](const SuperWeaponEvent& event) {
                    return event.kind == "SuperWeaponImpatient";
                }
            )
        );
        const bool voiceLeakedToSound = std::any_of(
            voiceEvents.begin(),
            voiceEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.soundIntent == "ChargingVoice"
                    || event.soundIntent == "SuspendVoice"
                    || event.soundIntent == "ReadyVoice"
                    || event.soundIntent == "ImpatientVoice";
            }
        );
        if (voiceCount("ChargingVoice") != 3
            || voiceCount("SuspendVoice") != 1
            || voiceCount("ReadyVoice") != 1
            || voiceCount("ImpatientVoice") != 1
            || impatientBeforeBlockedStates != 1
            || impatientAfterBlockedStates != impatientBeforeBlockedStates
            || voiceLeakedToSound) {
            std::cerr << "superweapon smoke: voice edge/dedup mismatch\n";
            return 1;
        }
    }

    {
        SuperWeaponRule primary;
        primary.id = "ChronoPrimarySpecial";
        primary.uiName = "Name:ChronoPrimary";
        primary.name = "Chrono Primary";
        primary.type = "ChronoPrimary";
        primary.action = "ChronoPrimary";
        primary.targetMode = "TwoStage";
        primary.preClick = true;
        primary.targetEnemy = false;
        primary.targetFriendly = true;
        primary.targetInfantry = true;
        primary.targetUnits = true;
        primary.targetBuildings = false;
        primary.range = 1.5;
        primary.lineMultiplier = 0;
        primary.showTimer = true;

        SuperWeaponRule helper;
        helper.id = "ChronoPostSpecial";
        helper.type = "ChronoWarp";
        helper.action = "ChronoWarp";
        helper.targetMode = "Cell";
        helper.postClick = true;
        // Match by the primary Type rather than its section ID.
        helper.preDependent = "ChronoPrimary";
        helper.targetEnemy = true;
        helper.targetFriendly = false;
        helper.targetInfantry = false;
        helper.targetUnits = false;
        helper.targetBuildings = true;
        helper.range = 8.0;
        helper.lineMultiplier = 4;

        std::vector<WorldObjectInstance> pairTargets;
        WorldObjectInstance firstTarget;
        firstTarget.instanceId = 701;
        firstTarget.kind = WorldObjectKind::Unit;
        firstTarget.owner = "player";
        firstTarget.type = "TANK";
        firstTarget.health = 100;
        firstTarget.x = 2;
        firstTarget.y = 2;
        firstTarget.alive = true;
        pairTargets.push_back(firstTarget);
        WorldObjectInstance secondTarget;
        secondTarget.instanceId = 702;
        secondTarget.kind = WorldObjectKind::Structure;
        secondTarget.owner = "enemy";
        secondTarget.type = "ENEMYHQ";
        secondTarget.health = 1000;
        secondTarget.x = 10;
        secondTarget.y = 10;
        secondTarget.alive = true;
        pairTargets.push_back(secondTarget);

        SuperWeaponWorldContext pairContext;
        pairContext.fixedTickHz = 30;
        pairContext.mapWidth = 16;
        pairContext.mapHeight = 16;
        pairContext.objects = &pairTargets;
        pairContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) {
            return lowerAscii(left) == lowerAscii(right);
        };
        pairContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 16 && y < 16;
        };

        SuperWeaponSystem pairSystem;
        pairSystem.reset(std::vector<SuperWeaponRule>{primary, helper});
        if (!pairSystem.grant("player", primary.id)
            || !pairSystem.grant("player", helper.id)
            || !pairSystem.forceReady("player", primary.id)
            || !pairSystem.forceReady("player", helper.id)) {
            std::cerr << "superweapon smoke: pre/post grant failed\n";
            return 1;
        }
        pairSystem.tick(pairContext);
        if (pairSystem.sidebarSnapshots("player").size() != 1U
            || pairSystem.findSidebarSnapshot("player", helper.id) != nullptr) {
            std::cerr << "superweapon smoke: post-click helper leaked to sidebar\n";
            return 1;
        }

        std::string pairReason;
        if (!pairSystem.beginTargetSelection(
                "player",
                primary.id,
                pairContext,
                &pairReason
            )
            || !pairSystem.submitTarget(
                "player",
                primary.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: first pre-click target failed: "
                      << pairReason << "\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* secondStageSnapshot =
            pairSystem.findSidebarSnapshot("player", primary.id);
        if (secondStageSnapshot == nullptr
            || !secondStageSnapshot->firstTargetAccepted
            || secondStageSnapshot->firstTargetCell.x != 2
            || secondStageSnapshot->firstTargetCell.y != 2
            || std::abs(secondStageSnapshot->targetRange - 8.0) > 0.0001
            || secondStageSnapshot->targetLineMultiplier != 4
            || pairSystem.previewTarget(
                "player",
                primary.id,
                {{2, 2}, 701},
                pairContext,
                nullptr
            )
            || !pairSystem.submitTarget(
                "player",
                primary.id,
                {{10, 10}, 702},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: post-click helper targeting mismatch: "
                      << pairReason << "\n";
            return 1;
        }

        SuperWeaponRule explicitTwoStage = primary;
        explicitTwoStage.id = "ExplicitTwoStage";
        explicitTwoStage.type = "CustomTwoStage";
        explicitTwoStage.action = "CustomTwoStage";
        explicitTwoStage.preClick = false;
        SuperWeaponSystem fallbackSystem;
        fallbackSystem.reset(
            std::vector<SuperWeaponRule>{explicitTwoStage}
        );
        if (!fallbackSystem.grant("player", explicitTwoStage.id)
            || !fallbackSystem.forceReady("player", explicitTwoStage.id)
            || !fallbackSystem.beginTargetSelection(
                "player",
                explicitTwoStage.id,
                pairContext,
                &pairReason
            )
            || !fallbackSystem.submitTarget(
                "player",
                explicitTwoStage.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )
            || !fallbackSystem.submitTarget(
                "player",
                explicitTwoStage.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: explicit two-stage fallback failed: "
                      << pairReason << "\n";
            return 1;
        }
    }

    {
        SuperWeaponRule threatRule;
        threatRule.id = "DefenseThreat";
        threatRule.type = "MultiMissile";
        threatRule.action = "MultiMissile";
        threatRule.targetMode = "Area";
        threatRule.aiDefendAgainst = true;
        threatRule.targetEnemy = true;
        threatRule.targetFriendly = false;

        SuperWeaponRule shieldRule;
        shieldRule.id = "DefenseShield";
        shieldRule.type = "ForceShield";
        shieldRule.action = "ForceShield";
        shieldRule.targetMode = "Area";
        shieldRule.targetEnemy = false;
        shieldRule.targetFriendly = true;
        shieldRule.targetInfantry = false;
        shieldRule.targetUnits = false;
        shieldRule.targetBuildings = true;

        std::vector<WorldObjectInstance> defenseTargets;
        WorldObjectInstance distantStructure;
        distantStructure.instanceId = 801;
        distantStructure.kind = WorldObjectKind::Structure;
        distantStructure.owner = "defender";
        distantStructure.type = "HQ";
        distantStructure.health = 1000;
        distantStructure.x = 2;
        distantStructure.y = 2;
        distantStructure.alive = true;
        defenseTargets.push_back(distantStructure);
        WorldObjectInstance nearestAlliedStructure = distantStructure;
        nearestAlliedStructure.instanceId = 802;
        nearestAlliedStructure.owner = "ally";
        nearestAlliedStructure.x = 12;
        nearestAlliedStructure.y = 10;
        defenseTargets.push_back(nearestAlliedStructure);
        WorldObjectInstance hostileStructure = distantStructure;
        hostileStructure.instanceId = 803;
        hostileStructure.owner = "attacker";
        hostileStructure.x = 13;
        hostileStructure.y = 10;
        defenseTargets.push_back(hostileStructure);

        std::vector<SuperWeaponEvent> defenseEvents;
        SuperWeaponWorldContext defenseContext;
        defenseContext.tick = 100;
        defenseContext.fixedTickHz = 30;
        defenseContext.mapWidth = 32;
        defenseContext.mapHeight = 32;
        defenseContext.objects = &defenseTargets;
        defenseContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) {
            const std::string leftKey = lowerAscii(left);
            const std::string rightKey = lowerAscii(right);
            return leftKey == rightKey
                || (leftKey == "defender" && rightKey == "ally")
                || (leftKey == "ally" && rightKey == "defender");
        };
        defenseContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 32 && y < 32;
        };
        defenseContext.emit = [&](const SuperWeaponEvent& event) {
            defenseEvents.push_back(event);
        };

        SuperWeaponSystem defenseBase;
        defenseBase.reset(
            std::vector<SuperWeaponRule>{threatRule, shieldRule}
        );
        std::string defenseReason;
        if (!defenseBase.grant("attacker", threatRule.id)
            || !defenseBase.forceReady("attacker", threatRule.id)
            || !defenseBase.grant("defender", shieldRule.id)
            || !defenseBase.forceReady("defender", shieldRule.id)
            || !defenseBase.beginTargetSelection(
                "attacker",
                threatRule.id,
                defenseContext,
                &defenseReason
            )
            || !defenseBase.submitTarget(
                "attacker",
                threatRule.id,
                {{13, 10}, -1},
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense threat setup failed: "
                      << defenseReason << "\n";
            return 1;
        }

        SuperWeaponSystem zeroProbability = defenseBase;
        defenseEvents.clear();
        if (zeroProbability.fireAiDefense(
                "defender",
                {10, 10},
                0,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense accepted zero percent\n";
            return 1;
        }

        SuperWeaponSystem outsideDistance = defenseBase;
        defenseEvents.clear();
        if (outsideDistance.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                2,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense ignored distance\n";
            return 1;
        }

        SuperWeaponSystem boundaryWindow = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 200;
        if (!boundaryWindow.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense 15fps window failed: "
                      << defenseReason << "\n";
            return 1;
        }

        SuperWeaponSystem expiredWindow = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 201;
        if (expiredWindow.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense accepted expired alert\n";
            return 1;
        }

        SuperWeaponSystem deterministicLeft = defenseBase;
        SuperWeaponSystem deterministicRight = defenseBase;
        defenseContext.tick = 100;
        defenseEvents.clear();
        const bool leftRoll = deterministicLeft.fireAiDefense(
            "defender",
            {10, 10},
            37,
            50,
            12,
            defenseContext,
            &defenseReason
        );
        defenseEvents.clear();
        const bool rightRoll = deterministicRight.fireAiDefense(
            "defender",
            {10, 10},
            37,
            50,
            12,
            defenseContext,
            &defenseReason
        );
        if (leftRoll != rightRoll) {
            std::cerr << "superweapon smoke: AI defense roll was not deterministic\n";
            return 1;
        }

        SuperWeaponSystem activeThreat = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 101;
        activeThreat.tick(defenseContext);
        defenseEvents.clear();
        if (!activeThreat.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: active AI defense failed: "
                      << defenseReason << "\n";
            return 1;
        }
        const auto defenseTriggered = std::find_if(
            defenseEvents.begin(),
            defenseEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "SuperWeaponAIDefenseTriggered";
            }
        );
        const SuperWeaponSidebarSnapshot* defenseSnapshot =
            activeThreat.findSidebarSnapshot("defender", shieldRule.id);
        if (defenseTriggered == defenseEvents.end()
            || defenseTriggered->targetObjectId != 802
            || defenseTriggered->targetCell.x != 12
            || defenseTriggered->targetCell.y != 10
            || defenseSnapshot == nullptr
            || defenseSnapshot->state != SuperWeaponRuntimeState::Launching) {
            std::cerr << "superweapon smoke: AI defense target/event mismatch\n";
            return 1;
        }

        // No-cooldown defense submissions reallocate/sort the instance
        // vector; both event identities must still refer to the actual cast.
        SuperWeaponSystem repeatDefense = defenseBase;
        defenseContext.noCooldown = [](const std::string& owner) {
            return owner == "defender";
        };
        std::set<int> defenseCastIds;
        for (int use = 0; use < 3; ++use) {
            defenseEvents.clear();
            if (!repeatDefense.fireAiDefense("defender", {10, 10}, 100, 50,
                    12, defenseContext, &defenseReason)) {
                std::cerr << "superweapon smoke: repeat AI defense failed: "
                          << defenseReason << "\n";
                return 1;
            }
            const auto event = std::find_if(defenseEvents.begin(), defenseEvents.end(),
                [](const auto& entry) { return entry.kind == "SuperWeaponAIDefenseTriggered"; });
            if (event == defenseEvents.end() || event->owner != "defender"
                || event->superWeaponId != shieldRule.id || event->objectId != 1
                || event->targetObjectId != 802 || event->targetCell.x != 12
                || event->targetCell2.x != 13 || !defenseCastIds.insert(event->instanceId).second
                || repeatDefense.stateOf("defender", shieldRule.id) != SuperWeaponRuntimeState::Ready) {
                std::cerr << "superweapon smoke: repeat AI defense identity mismatch\n";
                return 1;
            }
        }
    }

    {
        SuperWeaponSystem stealthAi;
        stealthAi.reset(std::vector<SuperWeaponRule>{rules.front()});
        std::vector<WorldObjectInstance> stealthTargets;
        WorldObjectInstance visibleTarget;
        visibleTarget.instanceId = 900;
        visibleTarget.kind = WorldObjectKind::Unit;
        visibleTarget.owner = "player";
        visibleTarget.type = "TANK";
        visibleTarget.health = 100;
        visibleTarget.x = 2;
        visibleTarget.y = 2;
        visibleTarget.alive = true;
        stealthTargets.push_back(visibleTarget);
        WorldObjectInstance hiddenTarget;
        hiddenTarget.instanceId = 901;
        hiddenTarget.kind = WorldObjectKind::Structure;
        hiddenTarget.owner = "player";
        hiddenTarget.type = "HIDDENHQ";
        hiddenTarget.health = 2000;
        hiddenTarget.x = 20;
        hiddenTarget.y = 20;
        hiddenTarget.alive = true;
        stealthTargets.push_back(hiddenTarget);
        WorldObjectInstance cloakedTarget = hiddenTarget;
        cloakedTarget.instanceId = 902;
        cloakedTarget.type = "CLOAKEDHQ";
        cloakedTarget.health = 3000;
        cloakedTarget.x = 25;
        cloakedTarget.y = 25;
        cloakedTarget.cloaked = true;
        stealthTargets.push_back(cloakedTarget);

        std::vector<SuperWeaponEvent> stealthEvents;
        SuperWeaponWorldContext stealthContext = context;
        stealthContext.objects = &stealthTargets;
        stealthContext.aiTargetVisible = [](
            const std::string&,
            const WorldObjectInstance& object
        ) {
            return object.instanceId != 901 && !object.cloaked;
        };
        stealthContext.emit = [&stealthEvents](const SuperWeaponEvent& event) {
            stealthEvents.push_back(event);
        };
        std::string reason;
        if (!stealthAi.grant("enemy", "NukeSpecial")
            || !stealthAi.forceReady("enemy", "NukeSpecial")
            || !stealthAi.fireAi(
                "enemy",
                "NukeSpecial",
                stealthContext,
                &reason
            )) {
            std::cerr << "superweapon smoke: stealth AI setup failed: "
                      << reason << "\n";
            return 1;
        }
        const auto accepted = std::find_if(
            stealthEvents.rbegin(),
            stealthEvents.rend(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "SuperWeaponCommandAccepted";
            }
        );
        if (accepted == stealthEvents.rend()
            || accepted->targetObjectId != visibleTarget.instanceId
            || accepted->targetCell.x != visibleTarget.x
            || accepted->targetCell.y != visibleTarget.y) {
            std::cerr << "superweapon smoke: AI targeted a cloaked hostile\n";
            return 1;
        }
    }

    for (const SuperWeaponRule& rule : rules) {
        if (!system.grant("player", rule.id)) {
            std::cerr << "superweapon smoke: grant failed for " << rule.id << "\n";
            return 1;
        }
        if (!system.forceReady("player", rule.id)) {
            std::cerr << "superweapon smoke: ready failed for " << rule.id << "\n";
            return 1;
        }
    }
    {
        std::string reason;
        if (!system.beginTargetSelection(
                "player",
                "SpyPlaneSpecial",
                context,
                &reason
            )) {
            std::cerr << "superweapon smoke: immediate selection failed: "
                      << reason << "\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* selected =
            system.findSidebarSnapshot("player", "SpyPlaneSpecial");
        if (selected == nullptr || !selected->targetSelectionActive) {
            std::cerr << "superweapon smoke: selection snapshot was stale\n";
            return 1;
        }
        if (!system.cancelTargetSelection(
                "player",
                "SpyPlaneSpecial",
                context,
                "smoke right-click"
            )) {
            std::cerr << "superweapon smoke: immediate cancel failed\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* canceled =
            system.findSidebarSnapshot("player", "SpyPlaneSpecial");
        if (canceled == nullptr
            || canceled->targetSelectionActive
            || canceled->state != SuperWeaponRuntimeState::Ready) {
            std::cerr << "superweapon smoke: cancel snapshot was stale\n";
            return 1;
        }
    }
    {
        std::string reason;
        if (!system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 4}, 102},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 5}, -1},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 5}, 104},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "GeneticConverterSpecial",
                {{20, 9}, 105},
                context,
                &reason
            )) {
            std::cerr << "superweapon smoke: stock target validation mismatch: "
                      << reason << "\n";
            return 1;
        }
    }
    auto fire = [&](const std::string& id, const SuperWeaponTarget& target) {
        std::string reason;
        if (!system.beginTargetSelection("player", id, context, &reason)) {
            std::cerr << "superweapon smoke: fire failed for " << id
                      << ": " << reason << "\n";
            return false;
        }
        const SuperWeaponSidebarSnapshot* selected =
            system.findSidebarSnapshot("player", id);
        if (selected == nullptr || !selected->targetSelectionActive) {
            std::cerr << "superweapon smoke: active snapshot missing for "
                      << id << "\n";
            return false;
        }
        if (!system.submitTarget("player", id, target, context, &reason)) {
            std::cerr << "superweapon smoke: fire failed for " << id
                      << ": " << reason << "\n";
            return false;
        }
        const SuperWeaponSidebarSnapshot* submitted =
            system.findSidebarSnapshot("player", id);
        if (submitted == nullptr || submitted->targetSelectionActive) {
            std::cerr << "superweapon smoke: submitted snapshot stale for "
                      << id << "\n";
            return false;
        }
        return true;
    };
    if (!fire("NukeSpecial", {{10, 10}, 100})
        || !fire("LightningStormSpecial", {{15, 15}, 113})
        || !fire("IronCurtainSpecial", {{4, 4}, 102})
        || !fire("ForceShieldSpecial", {{4, 5}, 104})
        || !fire("PsychicDominatorSpecial", {{10, 10}, 100})
        || !fire("GeneticConverterSpecial", {{20, 8}, 103})
        || !fire("ParaDropSpecial", {{16, 16}, -1})
        || !fire("SpyPlaneSpecial", {{16, 16}, -1})
        || !fire("PsychicRevealSpecial", {{16, 16}, -1})) {
        return 1;
    }
    {
        std::string reason;
        if (!system.beginTargetSelection("player", "ChronoSphereSpecial", context, &reason)
            || !system.submitTarget("player", "ChronoSphereSpecial", {{4, 4}, 102}, context, &reason)
            || !system.submitTarget("player", "ChronoSphereSpecial", {{20, 20}, -1}, context, &reason)) {
            std::cerr << "superweapon smoke: chrono failed: " << reason << "\n";
            return 1;
        }
    }
    for (int tick = 1; tick <= 140; ++tick) {
        context.tick = tick;
        system.tick(context);
    }
    const auto objectById = [&](int objectId) -> const WorldObjectInstance* {
        const auto found = std::find_if(
            objects.begin(),
            objects.end(),
            [objectId](const WorldObjectInstance& object) {
                return object.instanceId == objectId;
            }
        );
        return found == objects.end() ? nullptr : &*found;
    };
    const WorldObjectInstance* oldEnemyHuman = objectById(103);
    const WorldObjectInstance* oldFriendlyHuman = objectById(105);
    const WorldObjectInstance* immuneInfantry = objectById(106);
    const WorldObjectInstance* notHumanInfantry = objectById(107);
    const WorldObjectInstance* nonOrganicInfantry = objectById(108);
    const WorldObjectInstance* airborneInfantry = objectById(109);
    const WorldObjectInstance* vetoedInfantry = objectById(110);
    const WorldObjectInstance* radiationTarget = objectById(112);
    std::vector<const WorldObjectInstance*> createdBrutes;
    for (const WorldObjectInstance& object : objects) {
        if (object.alive && object.type == "BRUTE") {
            createdBrutes.push_back(&object);
        }
    }
    std::sort(
        createdBrutes.begin(),
        createdBrutes.end(),
        [](const WorldObjectInstance* left, const WorldObjectInstance* right) {
            return left->health < right->health;
        }
    );
    if (objects[0].alive || objects[2].x != 20 || objects[2].y != 20
        || oldEnemyHuman == nullptr || oldEnemyHuman->alive
        || oldFriendlyHuman == nullptr || oldFriendlyHuman->alive
        || createdBrutes.size() != 3
        || createdBrutes[0]->owner != "player" || createdBrutes[0]->health != 200
        || createdBrutes[1]->owner != "player" || createdBrutes[1]->health != 200
        || createdBrutes[2]->owner != "player" || createdBrutes[2]->health != 200
        || immuneInfantry == nullptr || immuneInfantry->alive
        || notHumanInfantry == nullptr || notHumanInfantry->alive
        || nonOrganicInfantry == nullptr || !nonOrganicInfantry->alive
        || airborneInfantry == nullptr || !airborneInfantry->alive
        || vetoedInfantry == nullptr || !vetoedInfantry->alive
        || radiationTarget == nullptr || !radiationTarget->alive
        || radiationTarget->health >= 800) {
        std::cerr << "superweapon smoke: effect assertion failed"
                  << " nukeAlive=" << objects[0].alive
                  << " chrono=" << objects[2].x << "," << objects[2].y
                  << " brutes=" << createdBrutes.size() << "\n";
        return 1;
    }
    bool sawStorm = false;
    bool sawDrop = false;
    bool sawReveal = false;
    bool protectedPlayerBuilding = false;
    bool protectedAlliedBuilding = false;
    bool protectedNonBuilding = false;
    int mutationEvents = 0;
    int radiationDamageEvents = 0;
    for (const SuperWeaponEvent& event : events) {
        sawStorm = sawStorm || event.kind == "LightningBolt";
        sawDrop = sawDrop || event.kind == "ParadropInfantryDropped";
        sawReveal = sawReveal || event.kind == "SpyPlaneRevealArea"
            || event.kind == "PsychicRevealArea";
        if (event.kind == "ForceShieldProtected") {
            protectedPlayerBuilding = protectedPlayerBuilding
                || event.objectId == 104;
            protectedAlliedBuilding = protectedAlliedBuilding
                || event.objectId == 111;
            protectedNonBuilding = protectedNonBuilding
                || event.objectId == 102;
        }
        if (event.kind == "GeneticMutationApplied") {
            ++mutationEvents;
        }
        if (event.kind == "RadiationDamage" && event.objectId == 112) {
            ++radiationDamageEvents;
        }
    }
    const bool sawParadropPlaneLifecycle = std::any_of(
        createdObjects.begin(),
        createdObjects.end(),
        [](const auto& created) { return created.second == "DROPPLANE"; }
    );
    const auto createdPlane = std::find_if(
        createdObjects.begin(),
        createdObjects.end(),
        [](const auto& created) { return created.second == "DROPPLANE"; }
    );
    const bool stableParadropPlaneId = createdPlane != createdObjects.end()
        && std::any_of(
            events.begin(),
            events.end(),
            [&createdPlane](const SuperWeaponEvent& event) {
                return event.kind == "ParadropAircraftLaunched"
                    && event.objectId == createdPlane->first;
            }
        );
    int stableParadropInfantryIds = 0;
    for (const SuperWeaponEvent& event : events) {
        if (event.kind != "ParadropInfantryDropped") {
            continue;
        }
        const bool matchesCreatedInfantry = std::any_of(
            createdObjects.begin(),
            createdObjects.end(),
            [&event](const auto& created) {
                return created.first == event.objectId && created.second == "E1";
            }
        );
        stableParadropInfantryIds += matchesCreatedInfantry ? 1 : 0;
    }
    const bool sawDamageLifecycle = std::any_of(
        damagedObjects.begin(),
        damagedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& damaged) {
            return damaged.kind == SuperWeaponObjectLifecycleKind::Damaged
                && damaged.objectId == 100
                && damaged.healthBefore > damaged.healthAfter
                && damaged.sourceOwner == "player"
                && damaged.superWeaponId == "NukeSpecial"
                && damaged.superWeaponInstanceId > 0
                && damaged.warheadId == "NUKE"
                && damaged.reason == "superweapon radial damage";
        }
    );
    const auto sawRemoval = [&removedObjects](
        int objectId,
        const std::string& superWeaponId,
        SuperWeaponObjectRemovalDisposition removalDisposition,
        const std::string& reason
    ) {
        return std::any_of(
            removedObjects.begin(),
            removedObjects.end(),
            [objectId, &superWeaponId, removalDisposition, &reason](
                const SuperWeaponObjectLifecycleEvent& removed
            ) {
                return removed.kind == SuperWeaponObjectLifecycleKind::Removed
                    && removed.removalDisposition == removalDisposition
                    && removed.objectId == objectId
                    && removed.healthBefore > removed.healthAfter
                    && removed.healthAfter == 0
                    && removed.sourceOwner == "player"
                    && removed.superWeaponId == superWeaponId
                    && removed.superWeaponInstanceId > 0
                    && removed.reason == reason;
            }
        );
    };
    const auto allHaveNoRemovalDisposition = [](
        const std::vector<SuperWeaponObjectLifecycleEvent>& lifecycleEvents
    ) {
        return std::all_of(
            lifecycleEvents.begin(),
            lifecycleEvents.end(),
            [](const SuperWeaponObjectLifecycleEvent& event) {
                return event.kind != SuperWeaponObjectLifecycleKind::Removed
                    && event.removalDisposition
                        == SuperWeaponObjectRemovalDisposition::None;
            }
        );
    };
    const bool lifecycleDispositionsAreExplicit = std::all_of(
            removedObjects.begin(),
            removedObjects.end(),
            [](const SuperWeaponObjectLifecycleEvent& event) {
                return event.kind == SuperWeaponObjectLifecycleKind::Removed
                    && event.removalDisposition
                        != SuperWeaponObjectRemovalDisposition::None;
            }
        )
        && allHaveNoRemovalDisposition(damagedObjects)
        && allHaveNoRemovalDisposition(relocatedObjects)
        && allHaveNoRemovalDisposition(ownerChangedObjects);
    const bool sawRelocationLifecycle = std::any_of(
        relocatedObjects.begin(),
        relocatedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& relocated) {
            return relocated.kind == SuperWeaponObjectLifecycleKind::Relocated
                && relocated.objectId == 102
                && relocated.oldCell == SuperWeaponCell{4, 4}
                && relocated.newCell == SuperWeaponCell{20, 20}
                && relocated.sourceOwner == "player"
                && relocated.superWeaponId == "ChronoSphereSpecial"
                && relocated.superWeaponInstanceId > 0
                && relocated.reason == "chronosphere formation transfer";
        }
    );
    const bool sawOwnerLifecycle = std::any_of(
        ownerChangedObjects.begin(),
        ownerChangedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& changed) {
            return changed.kind == SuperWeaponObjectLifecycleKind::OwnerChanged
                && changed.objectId == 100
                && changed.oldOwner == "enemy"
                && changed.newOwner == "player"
                && changed.sourceOwner == "player"
                && changed.superWeaponId == "PsychicDominatorSpecial"
                && changed.superWeaponInstanceId > 0
                && changed.reason == "psychic dominator control";
        }
    );
    const auto lifecycleIndex = [&lifecycleOrder](
        const std::string& kind,
        int objectId
    ) {
        const auto found = std::find(
            lifecycleOrder.begin(),
            lifecycleOrder.end(),
            std::make_pair(kind, objectId)
        );
        return static_cast<std::size_t>(
            std::distance(lifecycleOrder.begin(), found)
        );
    };
    const std::size_t relocationCallbackIndex = lifecycleIndex("Relocated", 102);
    const std::size_t relocationEventIndex = lifecycleIndex("ChronoWarpIn", 102);
    const std::size_t ownerCallbackIndex = lifecycleIndex("OwnerChanged", 100);
    const std::size_t ownerEventIndex = lifecycleIndex(
        "PsychicDominatorControlled",
        100
    );
    const bool lifecycleCallbackOrder =
        relocationCallbackIndex < relocationEventIndex
        && relocationEventIndex < lifecycleOrder.size()
        && ownerCallbackIndex < ownerEventIndex
        && ownerEventIndex < lifecycleOrder.size();
    const auto impactCount = [&impactEvents](const std::string& kind) {
        return static_cast<int>(std::count_if(
            impactEvents.begin(),
            impactEvents.end(),
            [&kind](const SuperWeaponImpactEvent& event) {
                return event.kind == kind;
            }
        ));
    };
    const auto nukeImpact = std::find_if(
        impactEvents.begin(),
        impactEvents.end(),
        [](const SuperWeaponImpactEvent& event) {
            return event.kind == "NukeImpact";
        }
    );
    const auto dominatorImpact = std::find_if(
        impactEvents.begin(),
        impactEvents.end(),
        [](const SuperWeaponImpactEvent& event) {
            return event.kind == "PsychicDominatorImpact";
        }
    );
    const int lightningBoltEvents = static_cast<int>(std::count_if(
        events.begin(),
        events.end(),
        [](const SuperWeaponEvent& event) {
            return event.kind == "LightningBolt";
        }
    ));
    const bool impactPayloadsAreStable = impactCount("NukeImpact") == 1
        && nukeImpact != impactEvents.end()
        && nukeImpact->instanceId > 0
        && nukeImpact->superWeaponId == "NukeSpecial"
        && nukeImpact->superWeaponType == "MultiMissile"
        && nukeImpact->sourceOwner == "player"
        && nukeImpact->sourceObjectId == -1
        && nukeImpact->cell == SuperWeaponCell{10, 10}
        && nukeImpact->radius == 2
        && nukeImpact->damage == 600
        && nukeImpact->warheadId == "NUKE"
        && nukeImpact->animationId == "NUKEANIM"
        && impactCount("LightningBolt") == lightningBoltEvents
        && lightningBoltEvents > 0
        && impactCount("PsychicDominatorImpact") == 1
        && dominatorImpact != impactEvents.end()
        && dominatorImpact->activeToken > 0
        && dominatorImpact->warheadId == "PSYWH"
        && dominatorImpact->animationId == "PDFXCLD";
    const bool perTargetIntentsAreClean = std::all_of(
        events.begin(),
        events.end(),
        [](const SuperWeaponEvent& event) {
            if (event.kind != "NukeDamage"
                && event.kind != "LightningBoltDamage"
                && event.kind != "RadiationDamage"
                && event.kind != "PsychicDominatorControlled") {
                return true;
            }
            return event.soundIntent.empty() && event.animationIntent.empty();
        }
    );
    const auto lifecyclePairIsStable = [
        &activeLifecycleEvents,
        &events
    ](const std::string& id, const std::string& startKind, const std::string& endKind) {
        const auto started = std::find_if(
            activeLifecycleEvents.begin(),
            activeLifecycleEvents.end(),
            [&id](const SuperWeaponActiveLifecycleEvent& event) {
                return event.superWeaponId == id
                    && event.phase == SuperWeaponActiveLifecyclePhase::Started;
            }
        );
        const auto ended = std::find_if(
            activeLifecycleEvents.begin(),
            activeLifecycleEvents.end(),
            [&id](const SuperWeaponActiveLifecycleEvent& event) {
                return event.superWeaponId == id
                    && event.phase == SuperWeaponActiveLifecyclePhase::Ended;
            }
        );
        if (started == activeLifecycleEvents.end()
            || ended == activeLifecycleEvents.end()
            || started->activeToken <= 0
            || started->activeToken != ended->activeToken
            || started->instanceId != ended->instanceId
            || started->startedTick != ended->startedTick
            || started->endsTick != ended->endsTick
            || started->durationTicks <= 0
            || ended->tick != ended->endsTick) {
            return false;
        }
        const bool genericStarted = std::any_of(
            events.begin(),
            events.end(),
            [&](const SuperWeaponEvent& event) {
                return event.kind == startKind
                    && event.instanceId == started->instanceId
                    && event.activeToken == started->activeToken;
            }
        );
        const bool genericEnded = std::any_of(
            events.begin(),
            events.end(),
            [&](const SuperWeaponEvent& event) {
                return event.kind == endKind
                    && event.instanceId == ended->instanceId
                    && event.activeToken == ended->activeToken;
            }
        );
        return genericStarted && genericEnded;
    };
    const bool stableActiveLifecycles = lifecyclePairIsStable(
            "LightningStormSpecial",
            "LightningStormStarted",
            "LightningStormEnded"
        )
        && lifecyclePairIsStable(
            "PsychicDominatorSpecial",
            "PsychicDominatorStarted",
            "PsychicDominatorEnded"
        );
    if (!sawStorm || !sawDrop || !sawReveal
        || !protectedPlayerBuilding || !protectedAlliedBuilding
        || protectedNonBuilding || mutationEvents != 3
        || radiationDamageEvents <= 0
        || !sawParadropPlaneLifecycle || !stableParadropPlaneId
        || stableParadropInfantryIds != 2
        || !sawDamageLifecycle
        || !sawRemoval(
            100,
            "NukeSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "superweapon radial damage"
        )
        || !sawRemoval(
            113,
            "LightningStormSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "superweapon radial damage"
        )
        || !sawRemoval(
            114,
            "IronCurtainSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "iron curtain organic destruction"
        )
        || !sawRemoval(
            115,
            "ChronoSphereSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "chronosphere blocked destination"
        )
        || !sawRemoval(
            103,
            "GeneticConverterSpecial",
            SuperWeaponObjectRemovalDisposition::TransformSource,
            "genetic mutation replacement"
        )
        || !sawRemoval(
            105,
            "GeneticConverterSpecial",
            SuperWeaponObjectRemovalDisposition::TransformSource,
            "genetic mutation replacement"
        )
        || !lifecycleDispositionsAreExplicit
        || !sawRelocationLifecycle || !sawOwnerLifecycle
        || !lifecycleCallbackOrder
        || !impactPayloadsAreStable || !perTargetIntentsAreClean
        || !stableActiveLifecycles) {
        std::cerr << "superweapon smoke: missing independent effect events\n";
        return 1;
    }
    const std::uint64_t beforeSaveHash = system.stateHash();
    std::ostringstream saved;
    system.save(saved);
    IniFile savedIni;
    if (!savedIni.loadFromString(saved.str())) {
        std::cerr << "superweapon smoke: save parse failed\n";
        return 1;
    }
    SuperWeaponSystem restored;
    restored.reset(rules);
    std::string loadError;
    if (!restored.load(savedIni, &loadError)
        || restored.stateHash() != beforeSaveHash) {
        std::cerr << "superweapon smoke: save/load mismatch " << loadError << "\n";
        return 1;
    }

    // A Dominator lifecycle exists independently of captured targets. Two
    // owners may overlap, each activation must keep a distinct stable token,
    // and a mid-active save must resume with only the pending Ended callback.
    {
        const auto dominatorRule = std::find_if(
            rules.begin(),
            rules.end(),
            [](const SuperWeaponRule& rule) {
                return rule.id == "PsychicDominatorSpecial";
            }
        );
        if (dominatorRule == rules.end()) {
            std::cerr << "superweapon smoke: dominator rule missing\n";
            return 1;
        }
        std::vector<SuperWeaponRule> lifecycleRules{*dominatorRule};
        lifecycleRules.front().durationFrames = 15;
        SuperWeaponSystem lifecycleSystem;
        lifecycleSystem.reset(lifecycleRules);
        std::vector<WorldObjectInstance> noTargets;
        std::vector<SuperWeaponImpactEvent> lifecycleImpacts;
        std::vector<SuperWeaponActiveLifecycleEvent> lifecycleCallbacks;
        std::vector<SuperWeaponEvent> lifecycleGenericEvents;
        SuperWeaponWorldContext lifecycleContext;
        lifecycleContext.fixedTickHz = 30;
        lifecycleContext.mapWidth = 32;
        lifecycleContext.mapHeight = 32;
        lifecycleContext.objects = &noTargets;
        lifecycleContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 32 && y < 32;
        };
        lifecycleContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) { return lowerAscii(left) == lowerAscii(right); };
        lifecycleContext.ownerPowered = [](const std::string&) { return true; };
        lifecycleContext.onSuperWeaponImpact = [
            &lifecycleImpacts
        ](SuperWeaponImpactEvent event) {
            lifecycleImpacts.push_back(std::move(event));
        };
        lifecycleContext.onSuperWeaponActiveLifecycle = [
            &lifecycleCallbacks
        ](SuperWeaponActiveLifecycleEvent event) {
            lifecycleCallbacks.push_back(std::move(event));
        };
        lifecycleContext.emit = [
            &lifecycleGenericEvents
        ](const SuperWeaponEvent& event) {
            lifecycleGenericEvents.push_back(event);
        };
        const auto armDominator = [
            &lifecycleSystem,
            &lifecycleContext
        ](const std::string& owner, const SuperWeaponCell& cell) {
            std::string reason;
            return lifecycleSystem.grant(
                    owner,
                    "PsychicDominatorSpecial"
                )
                && lifecycleSystem.forceReady(
                    owner,
                    "PsychicDominatorSpecial"
                )
                && lifecycleSystem.beginTargetSelection(
                    owner,
                    "PsychicDominatorSpecial",
                    lifecycleContext,
                    &reason
                )
                && lifecycleSystem.submitTarget(
                    owner,
                    "PsychicDominatorSpecial",
                    {cell, -1},
                    lifecycleContext,
                    &reason
                );
        };
        if (!armDominator("alpha", {6, 6})
            || !armDominator("beta", {20, 20})) {
            std::cerr << "superweapon smoke: overlapping dominator setup failed\n";
            return 1;
        }
        lifecycleContext.tick = 1;
        lifecycleSystem.tick(lifecycleContext);
        std::vector<int> startedTokens;
        std::vector<int> startedInstances;
        for (const SuperWeaponActiveLifecycleEvent& event : lifecycleCallbacks) {
            if (event.phase != SuperWeaponActiveLifecyclePhase::Started) {
                continue;
            }
            startedTokens.push_back(event.activeToken);
            startedInstances.push_back(event.instanceId);
            if (event.startedTick != 1 || event.endsTick != 31
                || event.durationTicks != 30
                || event.sourceObjectId != -1
                || event.superWeaponType != "PsychicDominator") {
                std::cerr << "superweapon smoke: dominator lifecycle payload mismatch\n";
                return 1;
            }
        }
        std::sort(startedTokens.begin(), startedTokens.end());
        std::sort(startedInstances.begin(), startedInstances.end());
        if (!noTargets.empty() || !lifecycleImpacts.empty()
            || startedTokens.size() != 2U
            || startedTokens[0] <= 0 || startedTokens[0] == startedTokens[1]
            || startedInstances[0] <= 0
            || startedInstances[0] == startedInstances[1]
            || lifecycleSystem.stateOf(
                "alpha",
                "PsychicDominatorSpecial"
            ) != SuperWeaponRuntimeState::Active
            || lifecycleSystem.stateOf(
                "beta",
                "PsychicDominatorSpecial"
            ) != SuperWeaponRuntimeState::Active) {
            std::cerr << "superweapon smoke: zero-target/overlap lifecycle mismatch\n";
            return 1;
        }
        const auto sameActiveSnapshots = [](
            const std::vector<SuperWeaponActiveLifecycleSnapshot>& left,
            const std::vector<SuperWeaponActiveLifecycleSnapshot>& right
        ) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                const SuperWeaponActiveLifecycleSnapshot& a = left[index];
                const SuperWeaponActiveLifecycleSnapshot& b = right[index];
                if (a.instanceId != b.instanceId
                    || a.activeToken != b.activeToken
                    || a.superWeaponId != b.superWeaponId
                    || a.superWeaponType != b.superWeaponType
                    || a.sourceOwner != b.sourceOwner
                    || a.sourceObjectId != b.sourceObjectId
                    || a.cell != b.cell
                    || a.startedTick != b.startedTick
                    || a.activeUntilTick != b.activeUntilTick) {
                    return false;
                }
            }
            return true;
        };
        const std::uint64_t beforeSnapshotHash = lifecycleSystem.stateHash();
        const auto activeSnapshots = lifecycleSystem.activeLifecycleSnapshots();
        bool snapshotPayloadsMatch = activeSnapshots.size() == 2U;
        for (std::size_t index = 0;
             snapshotPayloadsMatch && index < activeSnapshots.size();
             ++index) {
            const SuperWeaponActiveLifecycleSnapshot& snapshot =
                activeSnapshots[index];
            snapshotPayloadsMatch = snapshot.activeToken == startedTokens[index]
                && snapshot.instanceId > 0
                && snapshot.superWeaponId == "PsychicDominatorSpecial"
                && snapshot.superWeaponType == "PsychicDominator"
                && (snapshot.sourceOwner == "alpha"
                    || snapshot.sourceOwner == "beta")
                && snapshot.sourceObjectId == -1
                && snapshot.startedTick == 1
                && snapshot.activeUntilTick == 31;
        }
        auto detachedSnapshots = activeSnapshots;
        if (!detachedSnapshots.empty()) {
            detachedSnapshots.front().activeToken = -1;
            detachedSnapshots.front().superWeaponType.clear();
        }
        const auto activeSnapshotsAgain =
            lifecycleSystem.activeLifecycleSnapshots();
        if (!snapshotPayloadsMatch
            || !sameActiveSnapshots(activeSnapshots, activeSnapshotsAgain)
            || lifecycleSystem.stateHash() != beforeSnapshotHash) {
            std::cerr << "superweapon smoke: active snapshot value semantics mismatch\n";
            return 1;
        }
        for (int tick = 2; tick <= 5; ++tick) {
            lifecycleContext.tick = tick;
            lifecycleSystem.tick(lifecycleContext);
        }
        const std::uint64_t midActiveHash = lifecycleSystem.stateHash();
        std::ostringstream midActiveSave;
        lifecycleSystem.save(midActiveSave);
        IniFile midActiveIni;
        if (!midActiveIni.loadFromString(midActiveSave.str())) {
            std::cerr << "superweapon smoke: mid-active save parse failed\n";
            return 1;
        }
        SuperWeaponSystem lifecycleRestored;
        lifecycleRestored.reset(lifecycleRules);
        if (!lifecycleRestored.load(midActiveIni, &loadError)
            || lifecycleRestored.stateHash() != midActiveHash
            || !sameActiveSnapshots(
                activeSnapshots,
                lifecycleRestored.activeLifecycleSnapshots()
            )) {
            std::cerr << "superweapon smoke: mid-active roundtrip mismatch "
                      << loadError << "\n";
            return 1;
        }

        // Versions 1-3 applied Dominator payloads at launch, without setting
        // EffectApplied. They cannot represent this v4 pre-impact state.
        // Migration must preserve the active visual lifetime/tokens while
        // marking that historical payload consumed, rather than firing again.
        SuperWeaponSystem expectedLegacy = lifecycleSystem;
        for (Instance& instance : expectedLegacy.instances_) {
            instance.chargeTicks = 0;
            instance.effectApplied = true;
            instance.nextImpactTick = -1;
        }
        const std::uint64_t expectedLegacyHash = expectedLegacy.stateHash();
        for (int legacyVersion = 1; legacyVersion <= 3; ++legacyVersion) {
            std::istringstream currentLines(midActiveSave.str());
            std::ostringstream legacySave;
            std::string saveLine;
            while (std::getline(currentLines, saveLine)) {
                if ((legacyVersion == 1
                        && (saveLine.rfind("NextActiveToken=", 0) == 0
                            || saveLine.rfind("ActiveToken=", 0) == 0
                            || saveLine.rfind("ActiveStartedTick=", 0) == 0))
                    || (legacyVersion < 3 && saveLine.rfind("OneShot=", 0) == 0)
                    || saveLine.rfind("StormCloud", 0) == 0) {
                    continue;
                }
                if (saveLine == "Version=8") {
                    saveLine = "Version=" + std::to_string(legacyVersion);
                } else if (saveLine.rfind("NextImpactTick=", 0) == 0) {
                    saveLine = "NextImpactTick=-1";
                }
                legacySave << saveLine << '\n';
            }
            IniFile legacyIni;
            SuperWeaponSystem legacyRestored;
            legacyRestored.reset(lifecycleRules);
            if (!legacyIni.loadFromString(legacySave.str())
                || !legacyRestored.load(legacyIni, &loadError)
                || legacyRestored.stateHash() != expectedLegacyHash
                || legacyRestored.stateHash() == midActiveHash
                || !sameActiveSnapshots(activeSnapshots,
                    legacyRestored.activeLifecycleSnapshots())) {
                std::cerr << "superweapon smoke: v" << legacyVersion
                          << " lifecycle migration failed " << loadError << "\n";
                return 1;
            }
            SuperWeaponWorldContext legacyContext = lifecycleContext;
            std::vector<SuperWeaponActiveLifecycleEvent> legacyCallbacks;
            int legacyImpacts = 0;
            legacyContext.emit = {};
            legacyContext.onSuperWeaponImpact = [&](SuperWeaponImpactEvent) {
                ++legacyImpacts;
            };
            legacyContext.onSuperWeaponActiveLifecycle = [&](SuperWeaponActiveLifecycleEvent event) {
                legacyCallbacks.push_back(std::move(event));
            };
            for (int tick = 6; tick <= 32; ++tick) {
                legacyContext.tick = tick;
                legacyRestored.tick(legacyContext);
            }
            std::vector<int> legacyEndedTokens;
            for (const auto& event : legacyCallbacks) {
                if (event.phase != SuperWeaponActiveLifecyclePhase::Ended
                    || event.tick != 31 || event.endsTick != 31
                    || event.durationTicks != 30) {
                    std::cerr << "superweapon smoke: legacy lifecycle replay/timing mismatch\n";
                    return 1;
                }
                legacyEndedTokens.push_back(event.activeToken);
            }
            std::sort(legacyEndedTokens.begin(), legacyEndedTokens.end());
            if (legacyImpacts != 0 || legacyEndedTokens != startedTokens
                || !legacyRestored.activeLifecycleSnapshots().empty()) {
                std::cerr << "superweapon smoke: legacy payload replay or missing lifecycle end\n";
                return 1;
            }
        }

        std::string malformedSave = midActiveSave.str();
        const std::size_t tokenLine = malformedSave.find("\nActiveToken=");
        const std::size_t tokenEnd = tokenLine == std::string::npos
            ? std::string::npos
            : malformedSave.find('\n', tokenLine + 1);
        if (tokenLine == std::string::npos || tokenEnd == std::string::npos) {
            std::cerr << "superweapon smoke: lifecycle token save field missing\n";
            return 1;
        }
        malformedSave.replace(
            tokenLine + 1,
            tokenEnd - tokenLine - 1,
            "ActiveToken=0"
        );
        IniFile malformedIni;
        const std::uint64_t beforeMalformedLoad = lifecycleRestored.stateHash();
        if (!malformedIni.loadFromString(malformedSave)
            || lifecycleRestored.load(malformedIni, &loadError)
            || lifecycleRestored.stateHash() != beforeMalformedLoad) {
            std::cerr << "superweapon smoke: bad save was not atomic\n";
            return 1;
        }

        std::vector<SuperWeaponActiveLifecycleEvent> resumedCallbacks;
        std::vector<SuperWeaponEvent> resumedGenericEvents;
        lifecycleContext.onSuperWeaponActiveLifecycle = [
            &resumedCallbacks
        ](SuperWeaponActiveLifecycleEvent event) {
            resumedCallbacks.push_back(std::move(event));
        };
        lifecycleContext.emit = [
            &resumedGenericEvents
        ](const SuperWeaponEvent& event) {
            resumedGenericEvents.push_back(event);
        };
        for (int tick = 6; tick <= 32; ++tick) {
            lifecycleContext.tick = tick;
            lifecycleRestored.tick(lifecycleContext);
        }
        std::vector<int> endedTokens;
        for (const SuperWeaponActiveLifecycleEvent& event : resumedCallbacks) {
            if (event.phase == SuperWeaponActiveLifecyclePhase::Ended) {
                endedTokens.push_back(event.activeToken);
                if (event.tick != 31 || event.endsTick != 31
                    || event.durationTicks != 30) {
                    std::cerr << "superweapon smoke: resumed lifecycle timing mismatch\n";
                    return 1;
                }
            }
        }
        std::sort(endedTokens.begin(), endedTokens.end());
        const int resumedEnds = static_cast<int>(std::count_if(
            resumedGenericEvents.begin(),
            resumedGenericEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "PsychicDominatorEnded"
                    && event.activeToken > 0;
            }
        ));
        if (endedTokens != startedTokens || resumedEnds != 2
            || lifecycleImpacts.size() != 2U
            || std::any_of(lifecycleImpacts.begin(), lifecycleImpacts.end(),
                [](const auto& impact) { return impact.tick != 7; })
            || !lifecycleRestored.activeLifecycleSnapshots().empty()) {
            std::cerr << "superweapon smoke: resumed lifecycle token mismatch\n";
            return 1;
        }
    }

    const auto fixtureHash = [&rules](
        const std::vector<std::string>& radiation,
        const std::vector<std::string>& lockouts,
        std::uint64_t& result
    ) {
        std::ostringstream fixture;
        fixture << "[SuperWeapons]\n"
                << "Version=1\n"
                << "Count=0\n"
                << "NextInstanceId=1\n"
                << "LastTick=40\n\n"
                << "[SuperWeaponProtections]\n"
                << "Count=0\n\n"
                << "[SuperWeaponRadiation]\n"
                << "Count=" << radiation.size() << "\n";
        for (std::size_t index = 0; index < radiation.size(); ++index) {
            fixture << "Effect" << index << "=" << radiation[index] << "\n";
        }
        fixture << "\n[SuperWeaponLockouts]\n"
                << "Count=" << lockouts.size() << "\n";
        for (std::size_t index = 0; index < lockouts.size(); ++index) {
            fixture << "Owner" << index << "=" << lockouts[index] << "\n";
        }

        IniFile fixtureIni;
        if (!fixtureIni.loadFromString(fixture.str())) {
            return false;
        }
        SuperWeaponSystem fixtureSystem;
        fixtureSystem.reset(rules);
        std::string fixtureError;
        if (!fixtureSystem.load(fixtureIni, &fixtureError)) {
            return false;
        }
        result = fixtureSystem.stateHash();
        return true;
    };
    std::uint64_t radiationBaseHash = 0;
    std::uint64_t radiationInstanceHash = 0;
    std::uint64_t radiationRadiusHash = 0;
    std::uint64_t radiationNextTickHash = 0;
    std::uint64_t lockoutBaseHash = 0;
    std::uint64_t lockoutOwnerHash = 0;
    std::uint64_t lockoutBlackoutHash = 0;
    std::uint64_t lockoutPenaltyUntilHash = 0;
    std::uint64_t lockoutPenaltyHash = 0;
    std::uint64_t lockoutCanonicalOrderHash = 0;
    std::uint64_t lockoutCanonicalReverseHash = 0;
    if (!fixtureHash(
            {"player|7|3|4|5|6|200|50"},
            {},
            radiationBaseHash
        )
        || !fixtureHash(
            {"player|8|3|4|5|6|200|50"},
            {},
            radiationInstanceHash
        )
        || !fixtureHash(
            {"player|7|3|4|6|6|200|50"},
            {},
            radiationRadiusHash
        )
        || !fixtureHash(
            {"player|7|3|4|5|6|200|51"},
            {},
            radiationNextTickHash
        )
        || !fixtureHash({}, {"player|100|110|-50"}, lockoutBaseHash)
        || !fixtureHash({}, {"enemy|100|110|-50"}, lockoutOwnerHash)
        || !fixtureHash({}, {"player|101|110|-50"}, lockoutBlackoutHash)
        || !fixtureHash(
            {},
            {"player|100|111|-50"},
            lockoutPenaltyUntilHash
        )
        || !fixtureHash({}, {"player|100|110|-51"}, lockoutPenaltyHash)
        || !fixtureHash(
            {},
            {"Zulu|100|110|-50", "alpha|120|130|-30"},
            lockoutCanonicalOrderHash
        )
        || !fixtureHash(
            {},
            {"ALPHA|120|130|-30", "zULU|100|110|-50"},
            lockoutCanonicalReverseHash
        )) {
        std::cerr << "superweapon smoke: hash fixture load failed\n";
        return 1;
    }
    if (radiationBaseHash == radiationInstanceHash
        || radiationBaseHash == radiationRadiusHash
        || radiationBaseHash == radiationNextTickHash
        || lockoutBaseHash == lockoutOwnerHash
        || lockoutBaseHash == lockoutBlackoutHash
        || lockoutBaseHash == lockoutPenaltyUntilHash
        || lockoutBaseHash == lockoutPenaltyHash
        || lockoutCanonicalOrderHash != lockoutCanonicalReverseHash) {
        std::cerr << "superweapon smoke: state hash coverage mismatch\n";
        return 1;
    }
    if (!system.applyTriggerAction(
            "player",
            "NukeSpecial",
            "95",
            {{2, 2}, -1},
            context
        )) {
        // The nuke is cooling down in this probe; the action is expected to
        // be rejected rather than silently firing without a charge.
        if (system.stateOf("player", "NukeSpecial") != SuperWeaponRuntimeState::Cooldown
            && system.stateOf("player", "NukeSpecial") != SuperWeaponRuntimeState::Charging) {
            return 1;
        }
    }

    if (!system.grant("enemy", "ChronoSphereSpecial")
        || !system.forceReady("enemy", "ChronoSphereSpecial")) {
        std::cerr << "superweapon smoke: AI chrono setup failed\n";
        return 1;
    }
    {
        std::string reason;
        if (!system.fireAi(
                "enemy",
                "ChronoSphereSpecial",
                context,
                &reason
            )
            || system.stateOf("enemy", "ChronoSphereSpecial")
                != SuperWeaponRuntimeState::Launching) {
            std::cerr << "superweapon smoke: AI chrono did not complete "
                         "two-stage targeting: " << reason << "\n";
            return 1;
        }
    }
    if (!system.grant("scripted", "NukeSpecial")
        || !system.setRechargeTicks(
            "scripted",
            "NukeSpecial",
            240,
            &context
        )
        || !system.setChargePercent(
            "scripted",
            "NukeSpecial",
            50,
            &context
        )
        || system.stateOf("scripted", "NukeSpecial")
            != SuperWeaponRuntimeState::Charging
        || !system.setChargePercent(
            "scripted",
            "NukeSpecial",
            100,
            &context
        )
        || system.stateOf("scripted", "NukeSpecial")
            != SuperWeaponRuntimeState::Ready) {
        std::cerr << "superweapon smoke: scripted charge control failed\n";
        return 1;
    }

    std::cout << "superweapon smoke: passed definitions=" << rules.size()
              << " events=" << events.size()
              << " hash=" << beforeSaveHash << "\n";
    return 0;
}
