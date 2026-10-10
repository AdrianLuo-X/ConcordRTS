#include "simulation/abilities/psionic/RuntimeSpySystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "spy system smoke failure: " << message << '\n';
    }
    return condition;
}

WorldObjectInstance object(
    int id,
    WorldObjectKind kind,
    const std::string& owner,
    const std::string& type,
    int x,
    int y,
    int health = 100
) {
    WorldObjectInstance result;
    result.instanceId = id;
    result.kind = kind;
    result.owner = owner;
    result.type = type;
    result.x = x;
    result.y = y;
    result.health = health;
    result.alive = health > 0;
    result.poweredOperational = true;
    return result;
}

WorldObjectInstance* findMutable(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (WorldObjectInstance& candidate : objects) {
        if (candidate.instanceId == objectId) {
            return &candidate;
        }
    }
    return nullptr;
}

const RuntimeSpyAction* findAction(
    const std::vector<RuntimeSpyAction>& actions,
    RuntimeSpyActionKind kind,
    RuntimeSpyVeteranProduction production =
        RuntimeSpyVeteranProduction::Infantry
) {
    for (const RuntimeSpyAction& action : actions) {
        if (action.kind == kind
            && (kind != RuntimeSpyActionKind::GrantVeteranProduction
                || action.veteranProduction == production)) {
            return &action;
        }
    }
    return nullptr;
}

int actionCount(
    const std::vector<RuntimeSpyAction>& actions,
    RuntimeSpyActionKind kind
) {
    return static_cast<int>(std::count_if(
        actions.begin(),
        actions.end(),
        [kind](const RuntimeSpyAction& action) {
            return action.kind == kind;
        }
    ));
}

bool hasDiagnostic(
    const std::vector<RuntimeSpyDiagnostic>& diagnostics,
    RuntimeSpyDiagnosticKind kind
) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [kind](const RuntimeSpyDiagnostic& diagnostic) {
            return diagnostic.kind == kind;
        }
    );
}

RuntimeSpyInfiltrationResult infiltrate(
    RuntimeSpySystemState& state,
    const RuntimeSpyContext& context,
    int targetId,
    int tick,
    int targetCredits = 0
) {
    return state.infiltrate(
        context,
        {1, targetId, tick, targetCredits}
    );
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_spy_system_smoke <rules.ini>\n";
        return 2;
    }

    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(argv[1]) || !rules.loadFromIni(ini)) {
        std::cerr << "failed to load spy system fixture\n";
        return 2;
    }

    std::vector<WorldObjectInstance> objects{
        object(1, WorldObjectKind::Infantry, "Player", "SPY", 0, 0),
        object(2, WorldObjectKind::Infantry, "Ally", "E1", 1, 0),
        object(3, WorldObjectKind::Infantry, "Enemy", "E2", 2, 0),
        object(4, WorldObjectKind::Unit, "Player", "MIRAGE", 5, 5, 200),
        object(5, WorldObjectKind::Unit, "Enemy", "TANK", 6, 5, 300),
        object(6, WorldObjectKind::Infantry, "EnemyAlly", "E2", 3, 0),
        object(7, WorldObjectKind::Infantry, "Enemy", "DETECTINF", 20, 20),
        object(100, WorldObjectKind::Structure, "Enemy", "POWER", 30, 1, 1000),
        object(101, WorldObjectKind::Structure, "Enemy", "REFINERY", 31, 1, 1000),
        object(102, WorldObjectKind::Structure, "Enemy", "BARRACKS", 32, 1, 1000),
        object(103, WorldObjectKind::Structure, "Enemy", "WARFACTORY", 33, 1, 1000),
        object(104, WorldObjectKind::Structure, "Enemy", "AIRPORT", 34, 1, 1000),
        object(105, WorldObjectKind::Structure, "Enemy", "RADAR", 35, 1, 1000),
        object(106, WorldObjectKind::Structure, "Enemy", "SPYSAT", 36, 1, 1000),
        object(107, WorldObjectKind::Structure, "Enemy", "SUPERHOST", 37, 1, 1000),
        object(108, WorldObjectKind::Structure, "Enemy", "DETECTOR", 2, 1, 1000),
        object(109, WorldObjectKind::Structure, "Enemy", "UNKNOWNFACTORY", 38, 1, 1000),
        object(110, WorldObjectKind::Structure, "Enemy", "SAFEHOUSE", 39, 1, 1000),
        object(111, WorldObjectKind::Structure, "Ally", "BARRACKS", 40, 1, 1000),
        object(112, WorldObjectKind::Structure, "Enemy", "DETECTOR", 20, 21, 1000),
        object(120, WorldObjectKind::Structure, "Enemy", "LAB_ALLIED", 60, 1, 1000),
        object(121, WorldObjectKind::Structure, "Enemy", "LAB_SOVIET", 61, 1, 1000),
        object(122, WorldObjectKind::Structure, "Enemy", "LAB_THIRD", 62, 1, 1000)
    };

    const auto allied = [](const std::string& left, const std::string& right) {
        const std::set<std::string> playerCoalition{"Player", "Ally"};
        const std::set<std::string> enemyCoalition{"Enemy", "EnemyAlly"};
        return (playerCoalition.contains(left)
                && playerCoalition.contains(right))
            || (enemyCoalition.contains(left)
                && enemyCoalition.contains(right));
    };
    RuntimeSpyContext context;
    context.rules = &rules;
    context.objects = &objects;
    context.areAllied = allied;
    context.canonicalOwnerId = [](const std::string& owner) {
        return owner;
    };
    context.isKnownOwner = [](const std::string& owner) {
        return owner == "Player"
            || owner == "Ally"
            || owner == "Enemy"
            || owner == "EnemyAlly";
    };
    context.combatantOwners = {
        "Player", "Ally", "Enemy", "EnemyAlly"
    };

    bool ok = true;
    RuntimeSpySystemState state;

    const RuntimeSpyInfiltrationResult friendly = infiltrate(
        state,
        context,
        111,
        1
    );
    ok &= expect(
        !friendly.accepted
            && friendly.veto == RuntimeSpyVeto::FriendlyTarget,
        "allied Spyable building must reject infiltration"
    );
    const RuntimeSpyInfiltrationResult notSpyable = infiltrate(
        state,
        context,
        110,
        2
    );
    ok &= expect(
        !notSpyable.accepted
            && notSpyable.veto == RuntimeSpyVeto::TargetNotSpyable,
        "enemy building without Spyable must reject infiltration"
    );

    const RuntimeSpyInfiltrationResult power = infiltrate(
        state,
        context,
        100,
        10
    );
    const RuntimeSpyAction* blackout = findAction(
        power.actions,
        RuntimeSpyActionKind::SetPowerBlackout
    );
    ok &= expect(
        power.accepted
            && !power.actions.empty()
            && power.actions.front().kind
                == RuntimeSpyActionKind::ConsumeAgent
            && blackout != nullptr
            && blackout->durationTicks == 90
            && blackout->untilTick == 100
            && state.blackoutUntilTick("Enemy") == 100,
        "power infiltration must emit deterministic 90-frame blackout"
    );

    RuntimeSpySystemState blackoutStackingState;
    const RuntimeSpyInfiltrationResult laterDeadline = infiltrate(
        blackoutStackingState,
        context,
        100,
        100
    );
    const RuntimeSpyAction* laterBlackout = findAction(
        laterDeadline.actions,
        RuntimeSpyActionKind::SetPowerBlackout
    );
    const RuntimeSpyInfiltrationResult shorterDeadline = infiltrate(
        blackoutStackingState,
        context,
        100,
        50
    );
    const RuntimeSpyAction* retainedBlackout = findAction(
        shorterDeadline.actions,
        RuntimeSpyActionKind::SetPowerBlackout
    );
    ok &= expect(
        laterDeadline.accepted
            && laterBlackout != nullptr
            && laterBlackout->untilTick == 190
            && shorterDeadline.accepted
            && retainedBlackout != nullptr
            && retainedBlackout->durationTicks == 90
            && retainedBlackout->untilTick == 190
            && !retainedBlackout->changed
            && blackoutStackingState.blackoutUntilTick("Enemy") == 190,
        "a shorter repeated power infiltration deadline must not shorten an active blackout"
    );

    const RuntimeSpyInfiltrationResult refinery = infiltrate(
        state,
        context,
        101,
        11,
        1001
    );
    const RuntimeSpyAction* transfer = findAction(
        refinery.actions,
        RuntimeSpyActionKind::TransferCredits
    );
    ok &= expect(
        refinery.accepted
            && transfer != nullptr
            && transfer->amount == 250
            && transfer->sourceOwner == "Enemy"
            && transfer->targetOwner == "Player",
        "storage infiltration must floor and transfer configured credit percent"
    );

    const RuntimeSpyInfiltrationResult barracks = infiltrate(
        state,
        context,
        102,
        12
    );
    const RuntimeSpyInfiltrationResult warFactory = infiltrate(
        state,
        context,
        103,
        13
    );
    const RuntimeSpyInfiltrationResult airport = infiltrate(
        state,
        context,
        104,
        14
    );
    ok &= expect(
        findAction(
            barracks.actions,
            RuntimeSpyActionKind::GrantVeteranProduction,
            RuntimeSpyVeteranProduction::Infantry
        ) != nullptr
            && findAction(
                warFactory.actions,
                RuntimeSpyActionKind::GrantVeteranProduction,
                RuntimeSpyVeteranProduction::Vehicles
            ) != nullptr
            && findAction(
                airport.actions,
                RuntimeSpyActionKind::GrantVeteranProduction,
                RuntimeSpyVeteranProduction::Aircraft
            ) != nullptr
            && state.hasVeteranProduction(
                "Player",
                RuntimeSpyVeteranProduction::Infantry
            )
            && state.hasVeteranProduction(
                "Player",
                RuntimeSpyVeteranProduction::Vehicles
            )
            && state.hasVeteranProduction(
                "Player",
                RuntimeSpyVeteranProduction::Aircraft
            ),
        "barracks, vehicle factory and airport must persist veteran production markers"
    );

    const auto* chronoCommando = rules.findUnitType("Infantry", "CCOMAND");
    const auto* chronoIvan = rules.findUnitType("Infantry", "CIVAN");
    const auto* psiCommando = rules.findUnitType("Infantry", "PTROOP");
    const auto* combined = rules.findUnitType("Infantry", "COMBINED");
    if (!chronoCommando || !chronoIvan || !psiCommando || !combined) return 1;
    ok &= expect(!state.meetsStolenTechRequirements("Player", *chronoCommando)
        && !state.meetsStolenTechRequirements("Player", *chronoIvan)
        && !state.meetsStolenTechRequirements("Player", *psiCommando),
        "infiltrating production buildings must not unlock stolen-technology units");
    const auto alliedLab = infiltrate(state, context, 120, 14);
    const auto* alliedGrant = findAction(alliedLab.actions, RuntimeSpyActionKind::GrantStolenTechnology);
    ok &= expect(alliedLab.accepted && alliedGrant && alliedGrant->changed
        && alliedGrant->stolenTechnology == RuntimeSpySide::Allied
        && state.hasStolenTechnology("pLaYeR", RuntimeSpySide::Allied)
        && !state.hasStolenTechnology("Enemy", RuntimeSpySide::Allied)
        && state.meetsStolenTechRequirements("Player", *chronoCommando)
        && !state.meetsStolenTechRequirements("Player", *chronoIvan)
        && !state.meetsStolenTechRequirements("Player", *psiCommando)
        && !state.meetsStolenTechRequirements("Player", *combined),
        "a captured/renamed allied lab must unlock only allied technology for the infiltrator");
    const auto repeatedLab = infiltrate(state, context, 120, 14);
    const auto* repeatedGrant = findAction(repeatedLab.actions, RuntimeSpyActionKind::GrantStolenTechnology);
    ok &= expect(repeatedGrant && !repeatedGrant->changed,
        "repeated laboratory infiltration must keep the permanent unlock idempotent");
    const auto sovietLab = infiltrate(state, context, 121, 14);
    ok &= expect(sovietLab.accepted && state.meetsStolenTechRequirements("Player", *chronoIvan)
        && !state.meetsStolenTechRequirements("Player", *psiCommando),
        "soviet lab must not unlock third-side technology");
    const auto thirdLab = infiltrate(state, context, 122, 14);
    ok &= expect(thirdLab.accepted && state.meetsStolenTechRequirements("Player", *psiCommando)
        && state.meetsStolenTechRequirements("Player", *combined),
        "combined stolen technology requirements must require every authored side");

    RuntimeSpySystemState legacyTechnology;
    (void)infiltrate(legacyTechnology, context, 102, 14);
    std::ostringstream legacyTechnologyText;
    legacyTechnology.save(legacyTechnologyText);
    IniFile legacyTechnologyIni;
    RuntimeSpySystemState legacyTechnologyReload;
    ok &= expect(legacyTechnologyText.str().find("Version=2\n") != std::string::npos
        && legacyTechnologyText.str().find("StolenTechMask") == std::string::npos
        && legacyTechnologyIni.loadFromString(legacyTechnologyText.str())
        && legacyTechnologyReload.load(legacyTechnologyIni, context, 14)
        && legacyTechnologyReload.stateHash() == legacyTechnology.stateHash()
        && !legacyTechnologyReload.meetsStolenTechRequirements("Player", *chronoCommando),
        "legacy saves without stolen technology must keep their hash and start locked");

    const RuntimeSpyInfiltrationResult radarProtected = infiltrate(
        state,
        context,
        105,
        15
    );
    ok &= expect(
        radarProtected.accepted
            && findAction(
                radarProtected.actions,
                RuntimeSpyActionKind::ResetRadarShroud
            ) == nullptr,
        "live SpySat must protect radar from shroud reset"
    );
    WorldObjectInstance* spySat = findMutable(objects, 106);
    spySat->poweredOperational = false;
    const RuntimeSpyInfiltrationResult radarUnpowered = infiltrate(
        state,
        context,
        105,
        16
    );
    ok &= expect(
        findAction(
            radarUnpowered.actions,
            RuntimeSpyActionKind::ResetRadarShroud
        ) != nullptr,
        "unpowered SpySat must not protect radar from shroud reset"
    );
    spySat->poweredOperational = true;
    spySat->alive = false;
    const RuntimeSpyInfiltrationResult radarOpen = infiltrate(
        state,
        context,
        105,
        17
    );
    ok &= expect(
        findAction(
            radarOpen.actions,
            RuntimeSpyActionKind::ResetRadarShroud
        ) != nullptr,
        "radar without SpySat must emit shroud reset"
    );

    const RuntimeSpyInfiltrationResult superHost = infiltrate(
        state,
        context,
        107,
        17
    );
    ok &= expect(
        actionCount(
            superHost.actions,
            RuntimeSpyActionKind::ResetSuperWeapon
        ) == 2,
        "both distinct super weapon slots must reset in stable order"
    );
    const RuntimeSpyInfiltrationResult unknown = infiltrate(
        state,
        context,
        109,
        18
    );
    ok &= expect(
        unknown.accepted
            && hasDiagnostic(
                unknown.diagnostics,
                RuntimeSpyDiagnosticKind::UnsupportedFactoryType
            )
            && findAction(
                unknown.actions,
                RuntimeSpyActionKind::GrantVeteranProduction
            ) == nullptr,
        "unknown Factory classification must be diagnosed and remain a no-op"
    );

    const RuntimeSpyDisguiseResult alliedDefault =
        state.initializeDefaultDisguise(
            context,
            1,
            RuntimeSpySide::Allied,
            20
        );
    const RuntimeSpyDisguiseState* spyDisguise = state.disguiseState(1);
    ok &= expect(
        alliedDefault.accepted
            && spyDisguise != nullptr
            && spyDisguise->active
            && spyDisguise->observerRelative
            && spyDisguise->disguiseType == "E1",
        "Allied permanent disguise must resolve AlliedDisguise as observer-relative"
    );
    ok &= expect(
        state.initializeDefaultDisguise(
            context,
            1,
            RuntimeSpySide::Soviet,
            21
        ).accepted
            && state.disguiseState(1)->disguiseType == "E2"
            && state.initializeDefaultDisguise(
                context,
                1,
                RuntimeSpySide::Third,
                22
            ).accepted
            && state.disguiseState(1)->disguiseType == "INIT",
        "all three side defaults must resolve without fallback guessing"
    );

    const RuntimeSpyDisguiseResult ordinary = state.applyMakesDisguise(
        context,
        1,
        3,
        "OrdinaryWH",
        23
    );
    ok &= expect(
        !ordinary.accepted
            && ordinary.veto
                == RuntimeSpyVeto::WarheadDoesNotMakeDisguise,
        "ordinary warhead must not copy disguise"
    );
    const RuntimeSpyDisguiseResult copied = state.applyMakesDisguise(
        context,
        1,
        3,
        "Snapshot",
        24
    );
    spyDisguise = state.disguiseState(1);
    ok &= expect(
        copied.accepted
            && copied.changed
            && spyDisguise != nullptr
            && spyDisguise->active
            && spyDisguise->identity
                == RuntimeSpyDisguiseIdentity::Infantry
            && spyDisguise->disguiseType == "E2"
            && spyDisguise->disguiseOwner == "Enemy"
            && !spyDisguise->observerRelative,
        "MakesDisguise must copy an absolute same-class target identity"
    );

    const std::vector<WorldObjectInstance> disguiseSourceObjects{
        object(1, WorldObjectKind::Infantry, "Player", "SPY", 0, 0),
        object(2, WorldObjectKind::Infantry, "Ally", "E1", 1, 0)
    };
    RuntimeSpyContext disguiseSourceContext = context;
    disguiseSourceContext.objects = &disguiseSourceObjects;
    disguiseSourceContext.canonicalOwnerId = [](const std::string& owner) {
        return owner == "Ally" ? "Player" : owner;
    };
    RuntimeSpySystemState disguiseSourceState;
    const RuntimeSpyDisguiseResult sourceDefault =
        disguiseSourceState.initializeDefaultDisguise(
            disguiseSourceContext,
            1,
            RuntimeSpySide::Allied,
            24
        );
    const RuntimeSpyDisguiseState* sourceDisguise =
        disguiseSourceState.disguiseState(1);
    const bool observerRelativeDefaultMatches = sourceDisguise != nullptr
        && sourceDisguise->active
        && sourceDisguise->observerRelative
        && sourceDisguise->disguiseType == "E1"
        && sourceDisguise->disguiseOwner == "Player"
        && sourceDisguise->rearmUntilTick == 24
        && sourceDisguise->lastChangeTick == 24;
    const std::uint64_t observerRelativeHash =
        disguiseSourceState.stateHash();

    std::ostringstream observerRelativeSaveText;
    disguiseSourceState.save(observerRelativeSaveText);
    IniFile observerRelativeSaveIni;
    RuntimeSpySystemState restoredObserverRelativeState;
    std::string observerRelativeLoadError;
    const bool observerRelativeLoaded =
        observerRelativeSaveIni.loadFromString(
            observerRelativeSaveText.str()
        )
        && restoredObserverRelativeState.load(
            observerRelativeSaveIni,
            disguiseSourceContext,
            24,
            &observerRelativeLoadError
        );
    const RuntimeSpyDisguiseState* restoredObserverRelativeDisguise =
        restoredObserverRelativeState.disguiseState(1);
    ok &= expect(
        sourceDefault.accepted
            && observerRelativeDefaultMatches
            && observerRelativeLoaded
            && observerRelativeLoadError.empty()
            && restoredObserverRelativeDisguise != nullptr
            && restoredObserverRelativeDisguise->observerRelative
            && restoredObserverRelativeState.stateHash()
                == observerRelativeHash,
        "save/load must preserve observer-relative default disguise provenance and hash"
    );

    const RuntimeSpyDisguiseResult sourceCopied =
        disguiseSourceState.applyMakesDisguise(
            disguiseSourceContext,
            1,
            2,
            "Snapshot",
            24
        );
    sourceDisguise = disguiseSourceState.disguiseState(1);
    const std::uint64_t absoluteSourceHash = disguiseSourceState.stateHash();
    ok &= expect(
        sourceCopied.accepted
            && sourceCopied.changed
            && sourceDisguise != nullptr
            && sourceDisguise->active
            && sourceDisguise->disguiseType == "E1"
            && sourceDisguise->disguiseOwner == "Player"
            && !sourceDisguise->observerRelative
            && absoluteSourceHash != observerRelativeHash,
        "observer-relative source provenance must participate in disguise changes and state hash"
    );

    std::ostringstream disguiseSourceSaveText;
    disguiseSourceState.save(disguiseSourceSaveText);
    IniFile disguiseSourceSaveIni;
    RuntimeSpySystemState restoredDisguiseSourceState;
    std::string disguiseSourceLoadError;
    const bool disguiseSourceLoaded =
        disguiseSourceSaveIni.loadFromString(disguiseSourceSaveText.str())
        && restoredDisguiseSourceState.load(
            disguiseSourceSaveIni,
            disguiseSourceContext,
            24,
            &disguiseSourceLoadError
        );
    sourceDisguise = restoredDisguiseSourceState.disguiseState(1);
    ok &= expect(
        disguiseSourceLoaded
            && disguiseSourceLoadError.empty()
            && sourceDisguise != nullptr
            && !sourceDisguise->observerRelative
            && restoredDisguiseSourceState.stateHash() == absoluteSourceHash,
        "save/load must preserve absolute MakesDisguise source provenance and hash"
    );

    RuntimeSpySystemState damagedPermaDisguiseState =
        restoredObserverRelativeState;
    constexpr int damageTick = 30;
    const int blinkDisguiseTime =
        rules.getSpecialMechanismRules().infantryBlinkDisguiseTime;
    const int blinkBoundaryTick = damageTick + blinkDisguiseTime;
    const RuntimeSpyDisguiseResult damagedPermaDisguise =
        damagedPermaDisguiseState.notifyDamaged(
            disguiseSourceContext,
            1,
            damageTick
        );
    const RuntimeSpyDisguiseState* damagedDisguise =
        damagedPermaDisguiseState.disguiseState(1);
    ok &= expect(
        blinkDisguiseTime > 0
            && damagedPermaDisguise.accepted
            && damagedPermaDisguise.changed
            && actionCount(
                damagedPermaDisguise.actions,
                RuntimeSpyActionKind::DisguiseChanged
            ) == 1
            && damagedDisguise != nullptr
            && !damagedDisguise->active
            && damagedDisguise->observerRelative
            && damagedDisguise->rearmUntilTick == blinkBoundaryTick,
        "damage must reveal a permanent Spy until InfantryBlinkDisguiseTime expires"
    );

    const std::uint64_t damagedPermaDisguiseHash =
        damagedPermaDisguiseState.stateHash();
    std::ostringstream damagedPermaDisguiseSaveText;
    damagedPermaDisguiseState.save(damagedPermaDisguiseSaveText);
    IniFile damagedPermaDisguiseSaveIni;
    RuntimeSpySystemState restoredDamagedPermaDisguiseState;
    std::string damagedPermaDisguiseLoadError;
    const bool damagedPermaDisguiseLoaded =
        damagedPermaDisguiseSaveIni.loadFromString(
            damagedPermaDisguiseSaveText.str()
        )
        && restoredDamagedPermaDisguiseState.load(
            damagedPermaDisguiseSaveIni,
            disguiseSourceContext,
            damageTick,
            &damagedPermaDisguiseLoadError
        );
    damagedDisguise = restoredDamagedPermaDisguiseState.disguiseState(1);
    ok &= expect(
        damagedPermaDisguiseLoaded
            && damagedPermaDisguiseLoadError.empty()
            && damagedDisguise != nullptr
            && !damagedDisguise->active
            && damagedDisguise->rearmUntilTick == blinkBoundaryTick
            && restoredDamagedPermaDisguiseState.stateHash()
                == damagedPermaDisguiseHash,
        "save/load during permanent disguise blink must preserve reveal deadline and hash"
    );

    const std::vector<RuntimeSpyAction> beforeBlinkBoundary =
        restoredDamagedPermaDisguiseState.advance(blinkBoundaryTick - 1);
    damagedDisguise = restoredDamagedPermaDisguiseState.disguiseState(1);
    ok &= expect(
        beforeBlinkBoundary.empty()
            && damagedDisguise != nullptr
            && !damagedDisguise->active,
        "permanent Spy must remain revealed immediately before the blink boundary"
    );

    const std::vector<RuntimeSpyAction> atBlinkBoundary =
        restoredDamagedPermaDisguiseState.advance(blinkBoundaryTick);
    const RuntimeSpyAction* rearmedDisguise = findAction(
        atBlinkBoundary,
        RuntimeSpyActionKind::DisguiseChanged
    );
    damagedDisguise = restoredDamagedPermaDisguiseState.disguiseState(1);
    ok &= expect(
        actionCount(
            atBlinkBoundary,
            RuntimeSpyActionKind::DisguiseChanged
        ) == 1
            && rearmedDisguise != nullptr
            && rearmedDisguise->tick == blinkBoundaryTick
            && rearmedDisguise->sourceObjectId == 1
            && rearmedDisguise->active
            && damagedDisguise != nullptr
            && damagedDisguise->active
            && damagedDisguise->observerRelative
            && damagedDisguise->lastChangeTick == blinkBoundaryTick
            && restoredDamagedPermaDisguiseState
                .advance(blinkBoundaryTick).empty(),
        "advance must restore permanent disguise exactly at the blink boundary once"
    );

    const RuntimeSpyDisguiseResult mismatch = state.applyMakesDisguise(
        context,
        1,
        5,
        "Snapshot",
        25
    );
    ok &= expect(
        !mismatch.accepted
            && mismatch.veto == RuntimeSpyVeto::DisguiseClassMismatch,
        "MakesDisguise must reject infantry/vehicle class mismatch"
    );

    const RuntimeSpyDisguiseResult stationary =
        state.updateStationaryDisguises(
            context,
            30,
            [](int objectId) {
                return objectId == 4;
            }
        );
    const RuntimeSpyDisguiseState* mirageDisguise = state.disguiseState(4);
    ok &= expect(
        stationary.accepted
            && mirageDisguise != nullptr
            && mirageDisguise->active
            && mirageDisguise->identity
                == RuntimeSpyDisguiseIdentity::Terrain
            && mirageDisguise->disguiseType == "TREE01",
        "stationary Mirage must choose deterministic valid terrain disguise"
    );
    const RuntimeSpyDisguiseResult fired = state.notifyFired(
        context,
        4,
        31
    );
    ok &= expect(
        fired.accepted
            && fired.changed
            && !state.isDisguised(4)
            && state.disguiseState(4)->rearmUntilTick == 43,
        "firing must reveal non-permanent disguise and start blink cooldown"
    );
    state.updateStationaryDisguises(
        context,
        42,
        [](int objectId) { return objectId == 4; }
    );
    ok &= expect(
        !state.isDisguised(4),
        "terrain disguise must not rearm before blink cooldown"
    );
    state.updateStationaryDisguises(
        context,
        43,
        [](int objectId) { return objectId == 4; }
    );
    ok &= expect(
        state.isDisguised(4),
        "terrain disguise must rearm deterministically at cooldown boundary"
    );

    // Restore a copied infantry disguise before exercising shared detection.
    state.applyMakesDisguise(context, 1, 3, "Snapshot", 44);
    RuntimeSpyDetectionUpdate detected = state.updateDetection(context, 44);
    ok &= expect(
        detected.detectedPairs >= 2
            && state.isDetectedBy("Enemy", 1)
            && state.isDetectedBy("EnemyAlly", 1)
            && !state.isDetectedBy("Ally", 1)
            && actionCount(
                detected.actions,
                RuntimeSpyActionKind::RevealDisguiseToOwner
            ) >= 2
            && state.canObserverSeeThroughDisguise(context, 6, 1),
        "range detector must reveal disguise to its owner and allied intelligence"
    );
    findMutable(objects, 108)->poweredOperational = false;
    RuntimeSpyDetectionUpdate concealed = state.updateDetection(context, 45);
    ok &= expect(
        concealed.detectedPairs == 0
            && actionCount(
                concealed.actions,
                RuntimeSpyActionKind::ConcealDisguiseFromOwner
            ) >= 2,
        "unpowered range detector must withdraw shared detection"
    );
    ok &= expect(
        state.canObserverSeeThroughDisguise(context, 7, 1),
        "DetectDisguise must grant direct personal detection without shared range"
    );
    ok &= expect(
        state.canObserverSeeThroughDisguise(context, 112, 1),
        "powered building direct detection must retain its existing distance-free personal semantics"
    );
    findMutable(objects, 112)->poweredOperational = false;
    ok &= expect(
        !state.canObserverSeeThroughDisguise(context, 112, 1),
        "unpowered building direct detection must not see through disguise"
    );
    findMutable(objects, 112)->poweredOperational = true;
    findMutable(objects, 108)->poweredOperational = true;
    state.updateDetection(context, 46);

    const std::vector<RuntimeSpyAction> earlyAdvance = state.advance(50);
    ok &= expect(
        earlyAdvance.empty()
            && state.blackoutUntilTick("Enemy") == 100,
        "blackout must survive before its expiry tick"
    );
    const std::uint64_t hashBeforeSave = state.stateHash();
    std::ostringstream saveText;
    state.save(saveText);
    IniFile saveIni;
    RuntimeSpySystemState restored;
    std::string loadError;
    RuntimeSpyReconcileReport loadReport;
    const bool loaded = saveIni.loadFromString(saveText.str())
        && restored.load(
            saveIni,
            context,
            50,
            &loadError,
            &loadReport
        );
    ok &= expect(
        loaded
            && loadError.empty()
            && restored.stateHash() == hashBeforeSave
            && restored.isDisguised(1)
            && restored.isDetectedBy("EnemyAlly", 1)
            && restored.hasVeteranProduction(
                "Player",
                RuntimeSpyVeteranProduction::Aircraft
            )
            && restored.meetsStolenTechRequirements("Player", *combined),
        "save/load must preserve disguise, detection, blackout, veterancy and stolen technology exactly"
    );

    IniFile malformed;
    malformed.loadFromString(
        "[SpyRuntime]\n"
        "Version=99\n"
        "LastAdvancedTick=-1\n"
        "OwnerCount=0\n"
        "DisguiseCount=0\n"
        "DetectionCount=0\n"
    );
    const std::uint64_t hashBeforeBadLoad = restored.stateHash();
    ok &= expect(
        !restored.load(malformed, context, 50, &loadError)
            && !loadError.empty()
            && restored.stateHash() == hashBeforeBadLoad,
        "invalid save must fail atomically without replacing live state"
    );

    const std::vector<RuntimeSpyAction> expired = restored.advance(100);
    ok &= expect(
        findAction(expired, RuntimeSpyActionKind::ClearPowerBlackout)
            != nullptr
            && restored.blackoutUntilTick("Enemy") == 0,
        "advance must emit one stable blackout-clear action at expiry"
    );
    ok &= expect(
        restored.advance(100).empty(),
        "same tick must not emit duplicate blackout-clear actions"
    );

    findMutable(objects, 1)->alive = false;
    RuntimeSpyContext retainedContext = context;
    retainedContext.isObjectRetained = [](int objectId) {
        return objectId == 1;
    };
    RuntimeSpyReconcileReport retainedReconcile = restored.reconcile(
        retainedContext,
        101
    );
    ok &= expect(
        retainedReconcile.retainedDisguises >= 1
            && retainedReconcile.retainedDetections >= 2
            && restored.isDisguised(1)
            && restored.isDetectedBy("Enemy", 1)
            && restored.isDetectedBy("EnemyAlly", 1),
        "explicitly retained inactive snapshot must preserve compatible disguise and detection state"
    );

    RuntimeSpySystemState legacyReconcileProbe = restored;
    RuntimeSpyReconcileReport legacyReconcile =
        legacyReconcileProbe.reconcile(context, 101);
    ok &= expect(
        legacyReconcile.removedDisguises >= 1
            && !legacyReconcileProbe.isDisguised(1)
            && !legacyReconcileProbe.isDetectedBy("Enemy", 1),
        "missing retained callback must preserve the legacy live-only reconcile behavior"
    );

    RuntimeSpySystemState detectorExitProbe = restored;
    const RuntimeSpyObjectExitResult detectorExit =
        detectorExitProbe.onObjectExited(108, 102);
    const std::uint64_t detectorExitHash = detectorExitProbe.stateHash();
    const RuntimeSpyObjectExitResult repeatedDetectorExit =
        detectorExitProbe.onObjectExited(108, 103);
    ok &= expect(
        detectorExit.changed
            && detectorExit.removedDisguises == 0
            && detectorExit.removedDetections >= 2
            && detectorExitProbe.isDisguised(1)
            && !detectorExitProbe.isDetectedBy("Enemy", 1)
            && !repeatedDetectorExit.changed
            && repeatedDetectorExit.actions.empty()
            && detectorExitProbe.stateHash() == detectorExitHash,
        "detector exit must clear every related pair once without forgetting its target"
    );

    const RuntimeSpyObjectExitResult targetExit =
        restored.onObjectExited(1, 102);
    const std::uint64_t targetExitHash = restored.stateHash();
    const RuntimeSpyObjectExitResult repeatedTargetExit =
        restored.onObjectExited(1, 103);
    ok &= expect(
        targetExit.changed
            && targetExit.removedDisguises == 1
            && targetExit.removedDetections >= 2
            && actionCount(
                targetExit.actions,
                RuntimeSpyActionKind::DisguiseChanged
            ) == 1
            && actionCount(
                targetExit.actions,
                RuntimeSpyActionKind::ConcealDisguiseFromOwner
            ) == static_cast<int>(targetExit.removedDetections)
            && !restored.isDisguised(1)
            && !restored.isDetectedBy("Enemy", 1)
            && !repeatedTargetExit.changed
            && repeatedTargetExit.actions.empty()
            && restored.stateHash() == targetExitHash,
        "target/agent exit must forget disguise and detections deterministically and idempotently"
    );

    if (!ok) {
        return 1;
    }
    std::cout
        << "spy system smoke passed: legal infiltration, blackout/money, "
           "factory veterancy, radar/super reset, three-side and MakesDisguise "
           "identities, fire reveal, allied range detection, retained snapshots, "
           "explicit exits, powered direct detection and save/load/hash validated\n";
    return 0;
}
