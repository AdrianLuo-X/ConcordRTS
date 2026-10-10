#include "simulation/combat/damage/RuntimeDeathWeaponSystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "death weapon system smoke failure: " << message << '\n';
    }
    return condition;
}

bool closeTo(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

WorldObjectInstance deadObject(
    int id,
    WorldObjectKind kind,
    const std::string& owner,
    const std::string& type,
    int x,
    int y,
    bool authoredAircraft = false
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = kind;
    object.owner = owner;
    object.type = type;
    object.mapId = "SMOKE";
    object.tag = "Tag" + std::to_string(id);
    object.health = 0;
    object.alive = false;
    object.x = x;
    object.y = y;
    object.cell = y * 100 + x;
    object.level = 2;
    object.onBridge = true;
    object.authoredAircraft = authoredAircraft;
    return object;
}

bool validateSuicideWarheadAllies() {
    const std::string fixture = R"INI(
[VehicleTypes]
0=RENAMED
1=LIMITED
2=ORDINARY
[RENAMED]
Strength=100
Primary=RenamedEmitter
DeathWeapon=RenamedEmitter
[LIMITED]
Strength=100
Primary=LimitedEmitter
[ORDINARY]
Strength=100
Primary=OrdinaryEmitter
[RenamedEmitter]
Damage=10
Projectile=Invisible
Warhead=rEnAmEdBlAsT
Suicide=yes
[LimitedEmitter]
Damage=10
Projectile=Invisible
Warhead=LimitedBlast
Suicide=yes
[OrdinaryEmitter]
Damage=10
Projectile=Invisible
Warhead=OrdinaryBlast
[Invisible]
Inviso=yes
[RenamedBlast]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
[LimitedBlast]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
AffectsAllies=no
[OrdinaryBlast]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)INI";
    bool ok = true;
    for (const bool listed : {false, true}) {
        IniFile ini;
        RulesDatabase rules;
        const std::string list = listed
            ? "\n[Warheads]\n0=RenamedBlast\n1=LimitedBlast\n2=OrdinaryBlast\n" : "";
        if (!ini.loadFromString(fixture + list) || !rules.loadFromIni(ini)) return false;
        const auto* renamed = rules.findWarheadType("RenamedBlast");
        const auto* limited = rules.findWarheadType("LimitedBlast");
        const auto* ordinary = rules.findWarheadType("OrdinaryBlast");
        ok &= expect(renamed && renamed->affectsAllies && renamed->profile.affectsAllies,
            "renamed Suicide warhead must default to allied damage in list and discovery paths");
        ok &= expect(limited && !limited->affectsAllies && !limited->profile.affectsAllies,
            "explicit AffectsAllies=no must override the Suicide default");
        ok &= expect(ordinary && !ordinary->affectsAllies && !ordinary->profile.affectsAllies,
            "ordinary unspecified warhead must retain its existing ally policy");
    }
    if (ok) std::cout << "death weapon Suicide ally rules passed: renamed/list/discovered/default/explicit-no/ordinary\n";
    return ok;
}

bool validateStockReferences(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(path) || !rules.loadFromIni(ini)) {
        std::cerr << "failed to load stock death weapon rules\n";
        return false;
    }

    bool ok = true;
    const auto* truck = rules.findUnitType("UnitType", "DTRUCK");
    const auto* bomb = rules.findWeaponType("Demobomb");
    const auto* blast = rules.findWarheadType("DemobombWH");
    const auto* ordinary = rules.findWarheadType("AP");
    const auto* pulse = rules.findWarheadType("PsiPulse");
    ok &= expect(truck && truck->primaryWeapon == "Demobomb" && truck->deathWeapon == "Demobomb"
            && bomb && bomb->suicide && bomb->damage == 300 && bomb->radLevel == 100
            && bomb->warhead == "DemobombWH" && blast && blast->cellSpread == 8
            && ini.getValue("DemobombWH", "AffectsAllies", "").empty()
            && blast->affectsAllies && blast->profile.affectsAllies && blast->affectsEnemies,
        "stock DTRUCK primary/death Demobomb must retain original damage/radiation and allied blast");
    ok &= expect(ordinary && !ordinary->affectsAllies && pulse && !pulse->affectsAllies,
        "stock ordinary AP default and explicit PsiPulse ally exclusion must remain unchanged");
    for (const auto* warheadId : {"TerrorBombWH", "CRNUKEWH"}) {
        const auto* suicideBlast = rules.findWarheadType(warheadId);
        ok &= expect(suicideBlast && suicideBlast->affectsAllies && suicideBlast->profile.affectsAllies
                && ini.getValue(warheadId, "AffectsAllies", "").empty(),
            std::string("stock authored Suicide default must affect allies: ") + warheadId);
    }
    const std::string& defaultWeapon =
        rules.getSpecialMechanismRules().defaultDeathWeapon;
    ok &= expect(
        !defaultWeapon.empty()
            && rules.findWeaponType(defaultWeapon) != nullptr,
        "stock global DeathWeapon must resolve to a parsed weapon"
    );

    std::size_t explicitReferences = 0;
    std::size_t modifiedReferences = 0;
    std::size_t unresolvedReferences = 0;
    const auto inspectUnit = [&](const UnitType& type) {
        if (type.deathWeapon.empty()) {
            return;
        }
        ++explicitReferences;
        if (!closeTo(type.deathWeaponDamageModifier, 1.0)) {
            ++modifiedReferences;
        }
        if (rules.findWeaponType(type.deathWeapon) == nullptr) {
            ++unresolvedReferences;
        }
    };
    for (const UnitType& type : rules.getInfantryTypes()) {
        inspectUnit(type);
    }
    for (const UnitType& type : rules.getVehicleTypes()) {
        inspectUnit(type);
    }
    for (const UnitType& type : rules.getAircraftTypes()) {
        inspectUnit(type);
    }
    for (const BuildingType& type : rules.getBuildingTypes()) {
        if (type.deathWeapon.empty()) {
            continue;
        }
        ++explicitReferences;
        if (!closeTo(type.deathWeaponDamageModifier, 1.0)) {
            ++modifiedReferences;
        }
        if (rules.findWeaponType(type.deathWeapon) == nullptr) {
            ++unresolvedReferences;
        }
    }
    ok &= expect(
        explicitReferences >= 5,
        "stock rules inventory must expose several authored DeathWeapon references"
    );
    ok &= expect(
        modifiedReferences >= 2,
        "stock rules inventory must retain fractional DeathWeapon modifiers"
    );
    ok &= expect(
        unresolvedReferences == 0,
        "every stock DeathWeapon reference must resolve through weapon discovery"
    );

    // Stock FV authors CRNuke as a conditional Ivan-mode death weapon. Hidden
    // passengers remain in the authoritative world even though alive is false.
    const std::vector<std::string> passengers = {
        "", "DOG", "ENGINEER", "E1", "FLAKT", "GHOST", "SNIPE", "SHK",
        "IVAN", "YURI", "DESO", "CLEG", "TERROR", "PRES", "INIT",
        "VIRUS", "YURIPR", "GGI"
    };
    for (const std::string& passengerType : passengers) {
        std::vector<WorldObjectInstance> objects{deadObject(
            0, WorldObjectKind::Unit, "Player", "FV", 10, 10)};
        if (!passengerType.empty()) {
            objects.push_back(deadObject(1, WorldObjectKind::Infantry,
                "Player", passengerType, 10, 10));
            objects.back().health = 100;
            const auto* passenger = rules.findUnitType("InfantryType", passengerType);
            objects.front().transportPassengerIds = {1};
            objects.front().activeGunnerMode = passenger->ifvMode;
            objects.front().activeGunnerWeaponIndex = passenger->ifvMode;
        } else {
            // A stale presentation cache must never turn an empty FV nuclear.
            objects.front().activeGunnerMode = 7;
            objects.front().activeGunnerWeaponIndex = 7;
        }
        RuntimeDeathWeaponContext context{&rules, &objects};
        RuntimeDeathWeaponSystemState state;
        const auto result = state.notifyDeath(context, {0, 10, {}});
        ok &= expect(result.accepted
            && result.fireRequest.has_value() == (passengerType == "IVAN"),
            "stock FV death radiation gate: " + (passengerType.empty() ? "empty" : passengerType));
        const auto duplicate = state.notifyDeath(context, {0, 11, {}});
        ok &= expect(!duplicate.accepted && !duplicate.fireRequest,
            "FV duplicate death must stay consumed");
    }

    if (ok) {
        std::cout
            << "death weapon stock rules passed: default and "
            << explicitReferences << " explicit references ("
            << modifiedReferences << " modified) all resolve\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--benchmark") {
        IniFile ini;
        RulesDatabase rules;
        if (!ini.loadFromFile(argv[2]) || !rules.loadFromIni(ini)) {
            return 2;
        }
        constexpr int count = 8192;
        std::vector<WorldObjectInstance> objects;
        objects.reserve(count);
        for (int index = 0; index < count; ++index) {
            objects.push_back(deadObject(index, WorldObjectKind::Unit,
                "Player", "EXPLICIT", index % 128, index / 128));
        }
        RuntimeDeathWeaponContext context;
        context.rules = &rules;
        context.objects = &objects;
        RuntimeDeathWeaponSystemState state;
        std::uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < count; ++index) {
            // Odd multiplier visits every power-of-two slot once, in a
            // non-monotonic order like simultaneous destruction callbacks.
            const int objectId = (index * 4051) % count;
            const auto result = state.notifyDeath(context, {objectId, 10, {}});
            if (!result.accepted || !result.fireRequest.has_value()) {
                return 3;
            }
            checksum += result.fireRequest->requestId
                * static_cast<std::uint64_t>(objectId + 1);
        }
        const auto finished = std::chrono::steady_clock::now();
        std::cout << "death benchmark objects=" << count
            << " total_ms=" << std::chrono::duration<double, std::milli>(
                finished - start).count()
            << " checksum=" << checksum
            << " state_hash=" << state.stateHash() << '\n';
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--stock") {
        return validateSuicideWarheadAllies() && validateStockReferences(argv[2]) ? 0 : 1;
    }
    if (argc != 2) {
        std::cerr
            << "usage: concord_death_weapon_system_smoke <fixture.ini>\n"
               "   or: concord_death_weapon_system_smoke --stock <rulesmd.ini>\n";
        return 2;
    }

    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(argv[1]) || !rules.loadFromIni(ini)) {
        std::cerr << "failed to load death weapon system fixture\n";
        return 2;
    }

    bool ok = validateSuicideWarheadAllies();
    ok &= expect(
        rules.findWeaponType(
            rules.getSpecialMechanismRules().defaultDeathWeapon
        ) != nullptr
            && rules.findWeaponType("ExplicitDeathWeapon") != nullptr
            && rules.findWeaponType("PlaneDeathWeapon") != nullptr
            && rules.findWeaponType("BuildingDeathWeapon") != nullptr,
        "fixture inventory DeathWeapon references must all be discovered"
    );

    std::vector<WorldObjectInstance> objects{
        deadObject(1, WorldObjectKind::Unit, "Player", "EXPLICIT", 7, 9),
        deadObject(2, WorldObjectKind::Unit, "Enemy", "GLOBALFALLBACK", 8, 9),
        deadObject(3, WorldObjectKind::Unit, "Neutral", "PLAIN", 9, 9),
        deadObject(4, WorldObjectKind::Unit, "Player", "DEATHPLANE", 10, 9, true),
        deadObject(5, WorldObjectKind::Structure, "Enemy", "DEATHBUILDING", 11, 9),
        deadObject(10, WorldObjectKind::Unit, "Enemy", "CHAIN", 20, 20),
        deadObject(11, WorldObjectKind::Infantry, "Enemy", "CHAININF", 21, 20),
        deadObject(12, WorldObjectKind::Unit, "Enemy", "CHAIN", 22, 20),
        deadObject(13, WorldObjectKind::Unit, "Enemy", "CHAIN", 23, 20),
        deadObject(14, WorldObjectKind::Unit, "Enemy", "CHAIN", 24, 20),
        deadObject(15, WorldObjectKind::Unit, "Enemy", "CHAIN", 25, 20)
    };
    RuntimeDeathWeaponContext context;
    context.rules = &rules;
    context.objects = &objects;
    context.maximumCascadeDepth = 3;

    RuntimeDeathWeaponSystemState state;
    const RuntimeDeathWeaponResult explicitResult = state.notifyDeath(
        context,
        {1, 100, {}}
    );
    ok &= expect(
        explicitResult.accepted
            && explicitResult.veto == RuntimeDeathWeaponVeto::None
            && explicitResult.fireRequest.has_value(),
        "explicit DeathWeapon must emit a value-only fire request"
    );
    if (explicitResult.fireRequest.has_value()) {
        const RuntimeDeathWeaponFireRequest& request =
            *explicitResult.fireRequest;
        ok &= expect(
            request.requestId == 1
                && request.cascadeId == request.requestId
                && request.parentRequestId == 0
                && request.cascadeDepth == 0,
            "root request must establish a deterministic cascade identity"
        );
        ok &= expect(
            request.sourceObjectId == 1
                && request.sourceKind == WorldObjectKind::Unit
                && request.sourceOwner == "Player"
                && request.sourceType == "EXPLICIT"
                && request.sourceMapId == "SMOKE"
                && request.sourceTag == "Tag1"
                && request.x == 7
                && request.y == 9
                && request.cell == 907
                && request.level == 2
                && request.onBridge,
            "source, owner and impact position must come from the dying object"
        );
        ok &= expect(
            request.weaponId == "ExplicitDeathWeapon"
                && request.resolution
                    == RuntimeDeathWeaponResolution::Explicit
                && request.authoredDamage == 400
                && closeTo(request.damageModifier, 0.25)
                && request.modifiedDamage == 100,
            "explicit weapon damage must apply the object's authored modifier"
        );
    }

    const std::uint64_t hashBeforeDuplicate = state.stateHash();
    const std::uint64_t sequenceBeforeDuplicate = state.nextRequestId();
    const RuntimeDeathWeaponResult duplicate = state.notifyDeath(
        context,
        {1, 101, {}}
    );
    ok &= expect(
        !duplicate.accepted
            && duplicate.veto
                == RuntimeDeathWeaponVeto::ObjectAlreadyTriggered
            && !duplicate.fireRequest.has_value()
            && state.nextRequestId() == sequenceBeforeDuplicate
            && state.stateHash() == hashBeforeDuplicate,
        "one object may trigger exactly once, including repeated death callbacks"
    );

    const RuntimeDeathWeaponResult globalResult = state.notifyDeath(
        context,
        {2, 102, {}}
    );
    ok &= expect(
        globalResult.accepted
            && globalResult.fireRequest.has_value()
            && globalResult.fireRequest->weaponId == "DefaultDeathWeapon"
            && globalResult.fireRequest->resolution
                == RuntimeDeathWeaponResolution::GlobalDefault
            && globalResult.fireRequest->authoredDamage == 75
            && closeTo(globalResult.fireRequest->damageModifier, 0.5)
            && globalResult.fireRequest->modifiedDamage == 37,
        "Explodes without an override must use global DeathWeapon and truncate modified damage"
    );

    const RuntimeDeathWeaponResult plainResult = state.notifyDeath(
        context,
        {3, 103, {}}
    );
    ok &= expect(
        plainResult.accepted
            && plainResult.veto == RuntimeDeathWeaponVeto::NotConfigured
            && !plainResult.fireRequest.has_value()
            && state.hasTriggered(3),
        "ordinary non-exploding objects must record death without firing the global default"
    );

    const RuntimeDeathWeaponResult planeResult = state.notifyDeath(
        context,
        {4, 104, {}}
    );
    const RuntimeDeathWeaponResult buildingResult = state.notifyDeath(
        context,
        {5, 105, {}}
    );
    ok &= expect(
        planeResult.fireRequest.has_value()
            && planeResult.fireRequest->sourceAuthoredAircraft
            && planeResult.fireRequest->weaponId == "PlaneDeathWeapon"
            && planeResult.fireRequest->modifiedDamage == 20,
        "aircraft rules and modifier must resolve independently of vehicles"
    );
    ok &= expect(
        buildingResult.fireRequest.has_value()
            && buildingResult.fireRequest->sourceKind
                == WorldObjectKind::Structure
            && buildingResult.fireRequest->weaponId == "BuildingDeathWeapon"
            && buildingResult.fireRequest->modifiedDamage == 150,
        "building DeathWeapon must retain structure source attribution"
    );

    const RuntimeDeathWeaponResult chainA = state.notifyDeath(
        context,
        {10, 110, {}}
    );
    const RuntimeDeathWeaponCause causeA = chainA.fireRequest.has_value()
        ? runtimeDeathWeaponCauseFrom(*chainA.fireRequest)
        : RuntimeDeathWeaponCause{};
    const std::uint64_t sequenceBeforeSuppressed = state.nextRequestId();
    const RuntimeDeathWeaponResult suppressed = state.notifyDeath(
        context,
        {15, 111, causeA, true}
    );
    const std::uint64_t hashAfterSuppressed = state.stateHash();
    const RuntimeDeathWeaponResult suppressedDuplicate = state.notifyDeath(
        context,
        {15, 112, causeA}
    );
    ok &= expect(
        suppressed.accepted
            && suppressed.veto
                == RuntimeDeathWeaponVeto::SuppressedByMechanism
            && std::string(runtimeDeathWeaponVetoName(suppressed.veto))
                == "SuppressedByMechanism"
            && !suppressed.fireRequest.has_value()
            && state.hasTriggered(15)
            && state.nextRequestId() == sequenceBeforeSuppressed,
        "mechanism-suppressed death must be consumed without emitting or advancing the request sequence"
    );
    ok &= expect(
        !suppressedDuplicate.accepted
            && suppressedDuplicate.veto
                == RuntimeDeathWeaponVeto::ObjectAlreadyTriggered
            && !suppressedDuplicate.fireRequest.has_value()
            && state.nextRequestId() == sequenceBeforeSuppressed
            && state.stateHash() == hashAfterSuppressed,
        "a later non-suppressed duplicate must not retroactively emit a suppressed DeathWeapon"
    );
    const RuntimeDeathWeaponResult chainB = state.notifyDeath(
        context,
        {
            11,
            113,
            causeA
        }
    );
    const RuntimeDeathWeaponCause causeB = chainB.fireRequest.has_value()
        ? runtimeDeathWeaponCauseFrom(*chainB.fireRequest)
        : RuntimeDeathWeaponCause{};
    const RuntimeDeathWeaponResult chainC = state.notifyDeath(
        context,
        {
            12,
            114,
            causeB
        }
    );
    const RuntimeDeathWeaponCause causeC = chainC.fireRequest.has_value()
        ? runtimeDeathWeaponCauseFrom(*chainC.fireRequest)
        : RuntimeDeathWeaponCause{};
    const std::uint64_t sequenceBeforeLimit = state.nextRequestId();
    const RuntimeDeathWeaponResult chainLimit = state.notifyDeath(
        context,
        {
            13,
            115,
            causeC
        }
    );
    ok &= expect(
        chainA.fireRequest.has_value()
            && chainB.fireRequest.has_value()
            && chainC.fireRequest.has_value()
            && chainA.fireRequest->cascadeDepth == 0
            && chainB.fireRequest->cascadeDepth == 1
            && chainC.fireRequest->cascadeDepth == 2
            && chainA.fireRequest->cascadeId == chainB.fireRequest->cascadeId
            && chainB.fireRequest->cascadeId == chainC.fireRequest->cascadeId,
        "recursive requests must preserve one cascade id and advance depth deterministically"
    );
    ok &= expect(
        chainLimit.accepted
            && chainLimit.veto
                == RuntimeDeathWeaponVeto::CascadeDepthExceeded
            && !chainLimit.fireRequest.has_value()
            && state.hasTriggered(13)
            && state.nextRequestId() == sequenceBeforeLimit,
        "cascade limit must suppress the next fire while permanently consuming that death"
    );

    const RuntimeDeathWeaponResult invalidCause = state.notifyDeath(
        context,
        {14, 116, {123, 0, 0}}
    );
    ok &= expect(
        invalidCause.accepted
            && invalidCause.veto
                == RuntimeDeathWeaponVeto::InvalidCascadeCause
            && !invalidCause.fireRequest.has_value()
            && state.hasTriggered(14),
        "partial cascade tokens must be suppressed and cannot be retried recursively"
    );

    const std::uint64_t savedHash = state.stateHash();
    const std::uint64_t savedNextRequestId = state.nextRequestId();
    std::ostringstream savedText;
    state.save(savedText);
    IniFile savedIni;
    RuntimeDeathWeaponSystemState restored;
    std::string loadError;
    RuntimeDeathWeaponReconcileReport loadReport;
    const bool loaded = savedIni.loadFromString(savedText.str())
        && restored.load(
            savedIni,
            context,
            &loadError,
            &loadReport
        );
    ok &= expect(
        loaded
            && loadError.empty()
            && loadReport.removedMissingObjects == 0
            && loadReport.removedIdentityMismatches == 0
            && restored.stateHash() == savedHash
            && restored.nextRequestId() == savedNextRequestId
            && restored.hasTriggered(1)
            && restored.hasTriggered(13)
            && restored.hasTriggered(15),
        "save/load/hash must preserve ordinary and suppressed once-only records and request sequence exactly"
    );

    const std::uint64_t restoredHashBeforeSuppressedDuplicate =
        restored.stateHash();
    const RuntimeDeathWeaponResult restoredSuppressedDuplicate =
        restored.notifyDeath(context, {15, 117, causeA});
    ok &= expect(
        !restoredSuppressedDuplicate.accepted
            && restoredSuppressedDuplicate.veto
                == RuntimeDeathWeaponVeto::ObjectAlreadyTriggered
            && !restoredSuppressedDuplicate.fireRequest.has_value()
            && restored.nextRequestId() == savedNextRequestId
            && restored.stateHash()
                == restoredHashBeforeSuppressedDuplicate,
        "suppressed once-only consumption must survive roundtrip without a separate schema field"
    );

    IniFile malformed;
    malformed.loadFromString(
        "[DeathWeaponRuntime]\n"
        "Version=99\n"
        "NextRequestId=1\n"
        "TriggeredCount=0\n"
    );
    const std::uint64_t hashBeforeBadLoad = restored.stateHash();
    ok &= expect(
        !restored.load(malformed, context, &loadError)
            && !loadError.empty()
            && restored.stateHash() == hashBeforeBadLoad,
        "malformed save must fail atomically without replacing live state"
    );

    objects.erase(
        std::remove_if(
            objects.begin(),
            objects.end(),
            [](const WorldObjectInstance& object) {
                return object.instanceId == 2;
            }
        ),
        objects.end()
    );
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == 3) {
            object.type = "CHAIN";
        }
    }
    const std::uint64_t sequenceBeforeReconcile = restored.nextRequestId();
    const RuntimeDeathWeaponReconcileReport reconcile =
        restored.reconcile(context);
    ok &= expect(
        reconcile.removedMissingObjects == 1
            && reconcile.removedIdentityMismatches == 1
            && !restored.hasTriggered(2)
            && !restored.hasTriggered(3)
            && restored.hasTriggered(1)
            && restored.nextRequestId() == sequenceBeforeReconcile,
        "reconcile must remove missing/reused object ids without rewinding request ids"
    );

    RuntimeDeathWeaponSystemState shuffledState;
    const std::vector<int> shuffledIds{14, 3, 13, 1, 12, 4, 10, 5, 15, 11};
    for (const int objectId : shuffledIds) {
        ok &= expect(shuffledState.notifyDeath(
            context, {objectId, 200, {}}).accepted,
            "out-of-order death notification was not accepted");
    }
    ok &= expect(std::is_sorted(shuffledState.triggeredObjects().begin(),
            shuffledState.triggeredObjects().end(),
            [](const auto& left, const auto& right) {
                return left.objectId < right.objectId;
            })
            && !shuffledState.hasTriggered(-1)
            && !shuffledState.hasTriggered(2)
            && !shuffledState.hasTriggered(16),
        "out-of-order deaths lost canonical ordering or lookup boundaries");
    const auto shuffledHash = shuffledState.stateHash();
    for (const int objectId : shuffledIds) {
        ok &= expect(shuffledState.hasTriggered(objectId)
                && shuffledState.notifyDeath(context, {objectId, 201, {}}).veto
                    == RuntimeDeathWeaponVeto::ObjectAlreadyTriggered,
            "out-of-order death allowed a duplicate trigger");
    }
    ok &= expect(shuffledState.stateHash() == shuffledHash,
        "out-of-order duplicate callbacks changed deterministic state");

    if (!ok) {
        return 1;
    }
    std::cout
        << "death weapon system smoke passed: inventory references, explicit/global "
           "resolution, modifiers, value-only attribution, once-only and explicitly "
           "suppressed deaths, bounded recursion, save/load/hash, atomic rejection "
           "and reconcile validated\n";
    return 0;
}
