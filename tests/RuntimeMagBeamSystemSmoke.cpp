#include "IniFile.h"
#include "RuntimeMagBeamSystem.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

RuntimeMagBeamKinematics pose(
    int cell = 3020,
    double height = 0.0,
    double velocityZ = 0.0,
    bool bridge = false,
    bool water = false
) {
    RuntimeMagBeamKinematics value;
    value.location = {20, 30, cell, bridge ? 1 : 0, bridge, water};
    value.worldX = 20.25;
    value.worldY = 30.5;
    value.worldZ = height;
    value.velocityX = -0.25;
    value.velocityY = 0.5;
    value.velocityZ = velocityZ;
    return value;
}

RuntimeMagBeamSourceSnapshot source(
    int id = 1,
    bool alive = true,
    std::string owner = "Yuri"
) {
    RuntimeMagBeamSourceSnapshot value;
    value.objectId = id;
    value.owner = std::move(owner);
    value.typeId = "TELE";
    value.present = true;
    value.alive = alive;
    return value;
}

RuntimeMagBeamTargetSnapshot target(
    int id = 10,
    std::string owner = "Allies"
) {
    RuntimeMagBeamTargetSnapshot value;
    value.objectId = id;
    value.owner = std::move(owner);
    value.typeId = "MTNK";
    value.kind = RuntimeMagBeamTargetKind::Vehicle;
    value.present = true;
    value.alive = true;
    value.enemyToSource = true;
    value.onGround = true;
    value.airborne = false;
    value.liftable = true;
    value.weight = 3.5;
    value.currentStrength = 60;
    value.maximumStrength = 100;
    value.suppressionThreshold = 250;
    value.originalLocomotorId =
        "{4A582741-9839-11d1-B709-00A024DDAFD1}";
    value.kinematics = pose();
    return value;
}

RuntimeMagBeamWeaponSnapshot weapon() {
    RuntimeMagBeamWeaponSnapshot value;
    value.weaponId = "MagneticBeam";
    value.warheadId = "LocomotorBeam";
    value.locomotorId = "{92612C46-F71F-11d1-AC9F-006008055BB5}";
    value.weaponExists = true;
    value.isMagBeam = true;
    value.warheadExists = true;
    value.isLocomotor = true;
    value.rawDamage = 5000;
    return value;
}

RuntimeMagBeamAcquireRequest request(
    int tick = 0,
    int sourceId = 1,
    int targetId = 10
) {
    RuntimeMagBeamAcquireRequest value;
    value.currentTick = tick;
    value.source = source(sourceId);
    value.target = target(targetId);
    value.weapon = weapon();
    value.rules.fallingDamageMultiplier = 0.5;
    value.rules.currentStrengthDamage = true;
    return value;
}

RuntimeMagBeamAdvanceInput advanceInput(
    int tick,
    const RuntimeMagBeamSourceSnapshot& sourceValue,
    const RuntimeMagBeamTargetSnapshot& targetValue
) {
    RuntimeMagBeamAdvanceInput input;
    input.currentTick = tick;
    input.sources.push_back(sourceValue);
    input.targets.push_back(targetValue);
    return input;
}

RuntimeMagBeamHostUpdate hostUpdate(
    int bindingId,
    RuntimeMagBeamHostUpdateKind kind,
    const RuntimeMagBeamKinematics& kinematics
) {
    RuntimeMagBeamHostUpdate value;
    value.bindingId = bindingId;
    value.kind = kind;
    value.kinematics = kinematics;
    return value;
}

const RuntimeMagBeamAction* findAction(
    const std::vector<RuntimeMagBeamAction>& actions,
    RuntimeMagBeamActionKind kind
) {
    for (const RuntimeMagBeamAction& action : actions) {
        if (action.kind == kind) {
            return &action;
        }
    }
    return nullptr;
}

std::size_t countAction(
    const std::vector<RuntimeMagBeamAction>& actions,
    RuntimeMagBeamActionKind kind
) {
    std::size_t count = 0;
    for (const RuntimeMagBeamAction& action : actions) {
        if (action.kind == kind) {
            ++count;
        }
    }
    return count;
}

bool samePose(
    const RuntimeMagBeamKinematics& left,
    const RuntimeMagBeamKinematics& right
) {
    return left.location.x == right.location.x
        && left.location.y == right.location.y
        && left.location.cell == right.location.cell
        && left.location.level == right.location.level
        && left.location.onBridge == right.location.onBridge
        && left.location.water == right.location.water
        && left.worldX == right.worldX
        && left.worldY == right.worldY
        && left.worldZ == right.worldZ
        && left.velocityX == right.velocityX
        && left.velocityY == right.velocityY
        && left.velocityZ == right.velocityZ;
}

std::string saveText(const RuntimeMagBeamSystemState& state) {
    std::ostringstream output;
    state.save(output);
    return output.str();
}

std::string hexEncode(const std::string& value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : value) {
        result.push_back(digits[(byte >> 4U) & 0x0FU]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

bool testAuthoritativeAcquireGates() {
    bool ok = true;
    auto rejected = [&](RuntimeMagBeamAcquireRequest value,
                        RuntimeMagBeamVeto expected,
                        const std::string& message) {
        RuntimeMagBeamSystemState state;
        const RuntimeMagBeamAcquireResult result = state.acquire(value);
        ok &= expect(!result.accepted && result.veto == expected, message);
    };

    RuntimeMagBeamAcquireRequest value = request();
    value.target.weight = 4999.999;
    RuntimeMagBeamSystemState belowLimit;
    ok &= expect(belowLimit.acquire(value).accepted,
        "Damage 5000 must lift Weight 4999.999");

    value = request();
    value.target.weight = 5000.0;
    rejected(value, RuntimeMagBeamVeto::TargetTooHeavy,
        "stock weight gate must be strict at equality");
    value.target.weight = 5000.001;
    rejected(value, RuntimeMagBeamVeto::TargetTooHeavy,
        "stock weight gate must reject above Damage");
    value = request();
    value.target.weight = std::numeric_limits<double>::quiet_NaN();
    rejected(value, RuntimeMagBeamVeto::InvalidTarget,
        "NaN target Weight must fail closed");

    value = request();
    value.weapon.isMagBeam = false;
    RuntimeMagBeamSystemState invisibleLocomotor;
    const RuntimeMagBeamAcquireResult invisible =
        invisibleLocomotor.acquire(value);
    ok &= expect(invisible.accepted
            && invisible.actions.size() == 2
            && !invisible.actions[1].magBeamVisual,
        "IsMagBeam must remain visual-only");

    value = request();
    value.weapon.isLocomotor = false;
    rejected(value, RuntimeMagBeamVeto::InvalidWeapon,
        "non-locomotor warhead must not bind");
    value = request();
    value.weapon.weaponId = "MagneShake";
    value.weapon.warheadId = "MagneShakeWH";
    value.weapon.isMagBeam = true;
    value.weapon.isLocomotor = false;
    value.weapon.rawDamage = 80;
    rejected(value, RuntimeMagBeamVeto::InvalidWeapon,
        "MagneShake must stay on normal damage path despite IsMagBeam");

    value = request();
    value.target.kind = RuntimeMagBeamTargetKind::Infantry;
    rejected(value, RuntimeMagBeamVeto::IllegalTargetKind,
        "infantry must not receive locomotor override");
    value = request();
    value.target.kind = RuntimeMagBeamTargetKind::Structure;
    rejected(value, RuntimeMagBeamVeto::IllegalTargetKind,
        "structures must not receive locomotor override");

    value = request();
    value.target.kind = RuntimeMagBeamTargetKind::Aircraft;
    value.target.typeId = "ORCA";
    value.target.originalLocomotorId =
        "{4A582742-9839-11d1-B709-00A024DDAFD1}";
    RuntimeMagBeamSystemState padAircraft;
    ok &= expect(padAircraft.acquire(value).accepted,
        "grounded pad aircraft must be accepted explicitly");
    value.target.onGround = false;
    value.target.airborne = true;
    rejected(value, RuntimeMagBeamVeto::TargetAirborne,
        "in-flight aircraft must fail closed");

    RuntimeMagBeamSystemState busy;
    const RuntimeMagBeamAcquireRequest first = request();
    ok &= expect(busy.acquire(first).accepted,
        "busy probe first binding must acquire");
    RuntimeMagBeamAcquireRequest sameSource = request(0, 1, 20);
    ok &= expect(busy.acquire(sameSource).veto
            == RuntimeMagBeamVeto::SourceBusy,
        "one source must not bind two targets");
    RuntimeMagBeamAcquireRequest sameTarget = request(0, 2, 10);
    ok &= expect(busy.acquire(sameTarget).veto
            == RuntimeMagBeamVeto::TargetBusy,
        "one target must not be bound by two sources");

    RuntimeMagBeamAcquireRequest transport = request();
    transport.target.typeId = "BFRT";
    RuntimeMagBeamSystemState opaqueTransport;
    const RuntimeMagBeamAcquireResult transportResult =
        opaqueTransport.acquire(transport);
    ok &= expect(transportResult.accepted
            && opaqueTransport.bindings().size() == 1
            && opaqueTransport.bindings()[0].targetObjectId
                == transport.target.objectId,
        "transport and its passenger manifest must remain one opaque target");
    return ok;
}

bool testExplicitFriendlyAcquisition() {
    bool ok = true;
    for (const std::string owner : {"Yuri", "AlliedPartner"}) {
        auto value = request();
        value.target.owner = owner;
        value.target.enemyToSource = false;
        RuntimeMagBeamSystemState state;
        ok &= expect(state.acquire(value).veto == RuntimeMagBeamVeto::FriendlyTarget
                && state.bindings().empty(), "ordinary friendly acquisition must remain rejected");
        value.forceFire = true;
        const auto acquired = state.acquire(value);
        ok &= expect(acquired.accepted && state.bindings().size() == 1,
            "explicit friendly force attack must establish a tractor link");
        std::ostringstream saved; state.save(saved); IniFile ini;
        RuntimeMagBeamSystemState restored; std::string error;
        ok &= expect(ini.loadFromString(saved.str()) && restored.load(ini, &error)
                && restored.stateHash() == state.stateHash(),
            "friendly ownership and tractor authority must survive saving");
        value.target.objectId = value.source.objectId;
        RuntimeMagBeamSystemState self;
        ok &= expect(self.acquire(value).veto == RuntimeMagBeamVeto::InvalidTarget,
            "force attack must not let a magnetron lift itself");
        value.target.objectId = 10; value.target.magBeamImmune = true;
        RuntimeMagBeamSystemState immune;
        ok &= expect(immune.acquire(value).veto == RuntimeMagBeamVeto::TargetImmune,
            "force attack must preserve immunity");
        value.target.magBeamImmune = false; value.target.kind = RuntimeMagBeamTargetKind::Infantry;
        RuntimeMagBeamSystemState infantry;
        ok &= expect(infantry.acquire(value).veto == RuntimeMagBeamVeto::IllegalTargetKind,
            "force attack must not make infantry liftable");
    }
    return ok;
}

bool testHostDrivenLifecycleAndImpact() {
    bool ok = true;
    RuntimeMagBeamSystemState state;
    const RuntimeMagBeamAcquireRequest acquireRequest = request();
    const RuntimeMagBeamAcquireResult acquired = state.acquire(acquireRequest);
    ok &= expect(acquired.accepted && acquired.actions.size() == 2,
        "legal stock binding must emit acquire/apply once");
    ok &= expect(acquired.actions[1].rawDamage == 5000
            && acquired.actions[1].originalLocomotorKnown,
        "apply action must carry weight payload and original locomotor");

    RuntimeMagBeamAdvanceResult idle = state.advance(advanceInput(
        1,
        acquireRequest.source,
        acquireRequest.target
    ));
    ok &= expect(idle.advanced && idle.actions.empty()
            && state.findBinding(1)->phase
                == RuntimeMagBeamPhase::Acquired,
        "core must not invent an 8-tick lift transition");

    RuntimeMagBeamAdvanceInput lift = advanceInput(
        2,
        acquireRequest.source,
        acquireRequest.target
    );
    const RuntimeMagBeamKinematics liftedPose = pose(3020, 128.0, 6.0);
    lift.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::LiftStarted,
        liftedPose
    ));
    RuntimeMagBeamAdvanceResult lifted = state.advance(lift);
    ok &= expect(findAction(
            lifted.actions,
            RuntimeMagBeamActionKind::LiftStarted
        ) != nullptr
            && state.findBinding(1)->phase == RuntimeMagBeamPhase::Lifting
            && samePose(state.findBinding(1)->kinematics, liftedPose),
        "host LiftStarted must own phase and kinematics");

    RuntimeMagBeamAdvanceInput carry = advanceInput(
        3,
        acquireRequest.source,
        acquireRequest.target
    );
    const RuntimeMagBeamKinematics carriedPose = pose(2920, 256.0, 0.0);
    carry.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::Carried,
        carriedPose
    ));
    RuntimeMagBeamAdvanceResult carried = state.advance(carry);
    ok &= expect(findAction(
            carried.actions,
            RuntimeMagBeamActionKind::Carried
        ) != nullptr
            && state.findBinding(1)->phase == RuntimeMagBeamPhase::Carried,
        "host Carried must explicitly enter carried phase");

    RuntimeMagBeamAdvanceInput carriedAgain = advanceInput(
        4,
        acquireRequest.source,
        acquireRequest.target
    );
    const RuntimeMagBeamKinematics latestPose = pose(2820, 220.0, -1.0);
    carriedAgain.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::Carried,
        latestPose
    ));
    RuntimeMagBeamAdvanceResult repeated = state.advance(carriedAgain);
    ok &= expect(repeated.actions.empty()
            && samePose(state.findBinding(1)->kinematics, latestPose),
        "repeated carried observation must update pose without replaying event");

    RuntimeMagBeamAdvanceInput stop = advanceInput(
        5,
        acquireRequest.source,
        acquireRequest.target
    );
    RuntimeMagBeamReleaseRequest release;
    release.bindingId = 1;
    release.reason = RuntimeMagBeamReleaseReason::Explicit;
    release.kinematics = pose(2820, 200.0, -8.0);
    stop.releases.push_back(release);
    RuntimeMagBeamAdvanceResult falling = state.advance(stop);
    ok &= expect(findAction(
            falling.actions,
            RuntimeMagBeamActionKind::BeginFall
        ) != nullptr
            && state.findBinding(1)->phase == RuntimeMagBeamPhase::Falling,
        "Stop must enter Falling immediately");

    RuntimeMagBeamAdvanceResult noGuessedImpact = state.advance(advanceInput(
        6,
        acquireRequest.source,
        acquireRequest.target
    ));
    ok &= expect(noGuessedImpact.actions.empty()
            && state.findBinding(1) != nullptr,
        "fall must wait indefinitely for host ImpactResolved");

    RuntimeMagBeamTargetSnapshot impactTarget = acquireRequest.target;
    impactTarget.currentStrength = 31;
    RuntimeMagBeamAdvanceInput impactInput = advanceInput(
        7,
        acquireRequest.source,
        impactTarget
    );
    RuntimeMagBeamHostUpdate impact;
    impact.bindingId = 1;
    impact.kind = RuntimeMagBeamHostUpdateKind::ImpactResolved;
    impact.impact.kinematics = pose(2720, 0.0, 0.0, true, false);
    impact.impact.cellLegal = false;
    impact.impact.targetCurrentStrength = 31;
    impact.impact.targetMaximumStrength = 100;
    impact.impact.occupancy.occupied = true;
    impact.impact.occupancy.objectId = 77;
    impact.impact.occupancy.kind = RuntimeMagBeamTargetKind::Structure;
    impact.impact.occupancy.owner = "Soviets";
    impact.impact.occupancy.typeId = "NATECH";
    impactInput.hostUpdates.push_back(impact);
    RuntimeMagBeamAdvanceResult resolved = state.advance(impactInput);
    const RuntimeMagBeamAction* resolve = findAction(
        resolved.actions,
        RuntimeMagBeamActionKind::ResolveLanding
    );
    ok &= expect(resolve != nullptr
            && countAction(
                resolved.actions,
                RuntimeMagBeamActionKind::ResolveLanding
            ) == 1
            && resolve->hasImpact
            && resolve->impact.targetCurrentStrength == 31
            && resolve->impact.targetMaximumStrength == 100
            && resolve->impact.occupancy.objectId == 77
            && !resolve->impact.cellLegal
            && resolve->fallingDamageMultiplier == 0.5
            && resolve->currentStrengthDamage
            && state.bindings().empty(),
        "impact must emit one unresolved value snapshot using impact-time HP");
    ok &= expect(findAction(
            resolved.actions,
            RuntimeMagBeamActionKind::RestoreLocomotor
        ) != nullptr
            && findAction(
                resolved.actions,
                RuntimeMagBeamActionKind::Released
            ) != nullptr,
        "impact must restore locomotor and atomically release the link");
    RuntimeMagBeamAdvanceResult sameTick = state.advance(impactInput);
    ok &= expect(!sameTick.advanced && sameTick.actions.empty(),
        "same-tick duplicate impact must never replay ResolveLanding");

    RuntimeMagBeamSystemState gentle;
    RuntimeMagBeamAcquireRequest gentleRequest = request();
    ok &= expect(gentle.acquire(gentleRequest).accepted,
        "gentle landing probe must acquire");
    RuntimeMagBeamAdvanceInput gentleLift = advanceInput(
        1,
        gentleRequest.source,
        gentleRequest.target
    );
    gentleLift.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::LiftStarted,
        pose(3020, 80.0, 3.0)
    ));
    ok &= expect(gentle.advance(gentleLift).advanced,
        "gentle probe must start lift");
    RuntimeMagBeamAdvanceInput landed = advanceInput(
        2,
        gentleRequest.source,
        gentleRequest.target
    );
    landed.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::GentleLanded,
        pose(2520)
    ));
    RuntimeMagBeamAdvanceResult gentleResult = gentle.advance(landed);
    ok &= expect(findAction(
            gentleResult.actions,
            RuntimeMagBeamActionKind::ResolveLanding
        ) == nullptr
            && findAction(
                gentleResult.actions,
                RuntimeMagBeamActionKind::RestoreLocomotor
            ) != nullptr
            && gentle.bindings().empty(),
        "gentle landing must release without crash resolution");
    return ok;
}

bool testDeathAndAtomicRelease() {
    bool ok = true;
    RuntimeMagBeamSystemState sourceDeath;
    RuntimeMagBeamAcquireRequest sourceRequest = request();
    ok &= expect(sourceDeath.acquire(sourceRequest).accepted,
        "source death probe must acquire");
    RuntimeMagBeamSourceSnapshot deadSource = sourceRequest.source;
    deadSource.alive = false;
    RuntimeMagBeamAdvanceResult sourceResult = sourceDeath.advance(
        advanceInput(1, deadSource, sourceRequest.target)
    );
    const RuntimeMagBeamAction* fall = findAction(
        sourceResult.actions,
        RuntimeMagBeamActionKind::BeginFall
    );
    ok &= expect(fall != nullptr
            && fall->releaseReason == RuntimeMagBeamReleaseReason::SourceDead
            && sourceDeath.findBinding(1)->phase
                == RuntimeMagBeamPhase::Falling,
        "source death must release into host-driven fall");

    RuntimeMagBeamSystemState targetDeath;
    RuntimeMagBeamAcquireRequest targetRequest = request();
    ok &= expect(targetDeath.acquire(targetRequest).accepted,
        "target death probe must acquire");
    RuntimeMagBeamTargetSnapshot deadTarget = targetRequest.target;
    deadTarget.alive = false;
    RuntimeMagBeamAdvanceResult targetResult = targetDeath.advance(
        advanceInput(1, targetRequest.source, deadTarget)
    );
    const RuntimeMagBeamAction* cancelled = findAction(
        targetResult.actions,
        RuntimeMagBeamActionKind::Cancelled
    );
    ok &= expect(cancelled != nullptr
            && cancelled->releaseReason
                == RuntimeMagBeamReleaseReason::TargetDead
            && findAction(
                targetResult.actions,
                RuntimeMagBeamActionKind::ResolveLanding
            ) == nullptr
            && targetDeath.bindings().empty(),
        "target death must atomically clear link without stale impact");
    return ok;
}

bool testSaveLoadLegacyAndBadAtomic() {
    bool ok = true;
    RuntimeMagBeamSystemState original;
    RuntimeMagBeamAcquireRequest acquireRequest = request();
    ok &= expect(original.acquire(acquireRequest).accepted,
        "roundtrip probe must acquire");
    RuntimeMagBeamAdvanceInput lift = advanceInput(
        1,
        acquireRequest.source,
        acquireRequest.target
    );
    lift.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::LiftStarted,
        pose(3020, 100.0, 5.0)
    ));
    ok &= expect(original.advance(lift).advanced,
        "roundtrip probe must reach mid-lift");
    RuntimeMagBeamAdvanceInput carry = advanceInput(
        2,
        acquireRequest.source,
        acquireRequest.target
    );
    carry.hostUpdates.push_back(hostUpdate(
        1,
        RuntimeMagBeamHostUpdateKind::Carried,
        pose(2920, 180.0, 0.0)
    ));
    ok &= expect(original.advance(carry).advanced,
        "roundtrip probe must reach carried phase");

    const std::string saved = saveText(original);
    IniFile ini;
    ok &= expect(ini.loadFromString(saved), "schema-2 save must parse");
    RuntimeMagBeamSystemState restored;
    std::string error;
    ok &= expect(restored.load(ini, &error),
        "schema-2 load must succeed: " + error);
    ok &= expect(restored.stateHash() == original.stateHash()
            && restored.findBinding(1)->phase
                == RuntimeMagBeamPhase::Carried,
        "mid-carried save/load/hash must be exact");

    const std::uint64_t beforeBad = restored.stateHash();
    std::string malformed = saved;
    const std::string phaseNeedle = "Phase=2";
    const std::size_t phasePosition = malformed.find(phaseNeedle);
    ok &= expect(phasePosition != std::string::npos,
        "saved fixture must expose phase");
    if (phasePosition != std::string::npos) {
        malformed.replace(phasePosition, phaseNeedle.size(), "Phase=99");
    }
    IniFile bad;
    ok &= expect(bad.loadFromString(malformed),
        "bad schema fixture must remain parseable INI");
    ok &= expect(!restored.load(bad, &error)
            && restored.stateHash() == beforeBad,
        "bad load must be rejected atomically");

    std::ostringstream legacyText;
    legacyText
        << "[RuntimeMagBeam]\n"
        << "Schema=1\n"
        << "LastAdvancedTick=5\n"
        << "NextBindingId=2\n"
        << "Count=1\n\n"
        << "[RuntimeMagBeam.Binding.0]\n"
        << "BindingId=1\n"
        << "SourceObjectId=1\n"
        << "SourceOwner=" << hexEncode("Yuri") << "\n"
        << "SourceType=" << hexEncode("TELE") << "\n"
        << "TargetObjectId=10\n"
        << "TargetOwner=" << hexEncode("Allies") << "\n"
        << "TargetType=" << hexEncode("MTNK") << "\n"
        << "TargetKind=0\n"
        << "TargetWeight=3.5\n"
        << "TargetCurrentStrength=60\n"
        << "TargetMaximumStrength=100\n"
        << "TargetSuppressionThreshold=250\n"
        << "TargetLocation=20,30,3020,0,0,0\n"
        << "Weapon=" << hexEncode("MagneticBeam") << "\n"
        << "Warhead=" << hexEncode("LocomotorBeam") << "\n"
        << "Locomotor="
        << hexEncode("{92612C46-F71F-11d1-AC9F-006008055BB5}")
        << "\n"
        << "ContinuousDamage=7\n"
        << "DamageInterval=2\n"
        << "LiftTicks=8\n"
        << "FallTicks=8\n"
        << "FallingDamageMultiplier=1\n"
        << "CurrentStrengthDamage=1\n"
        << "Phase=1\n"
        << "ReleaseReason=0\n"
        << "AcquiredTick=0\n"
        << "PhaseStartedTick=1\n"
        << "NextDamageTick=2\n"
        << "Landing=20,30,3020,0,0,0\n";
    IniFile legacyIni;
    ok &= expect(legacyIni.loadFromString(legacyText.str()),
        "schema-1 fixture must parse");
    RuntimeMagBeamSystemState migrated;
    ok &= expect(migrated.load(legacyIni, &error)
            && migrated.findBinding(1) != nullptr
            && migrated.findBinding(1)->phase
                == RuntimeMagBeamPhase::Lifting
            && !migrated.findBinding(1)->originalLocomotorKnown,
        "schema-1 fixed phase must migrate without fabricated original data");
    RuntimeMagBeamAdvanceInput legacyIdle;
    legacyIdle.currentTick = 6;
    legacyIdle.validateSources = false;
    legacyIdle.validateTargets = false;
    RuntimeMagBeamAdvanceResult legacyAdvanced = migrated.advance(legacyIdle);
    ok &= expect(legacyAdvanced.advanced
            && legacyAdvanced.actions.empty()
            && migrated.findBinding(1)->phase
                == RuntimeMagBeamPhase::Lifting,
        "legacy ticks must not auto-transition or auto-damage after migration");
    return ok;
}

bool testActionCapRollback() {
    bool ok = true;
    RuntimeMagBeamLimits limits;
    limits.maxBindings = 2;
    limits.maxActionsPerAdvance = 3;
    RuntimeMagBeamSystemState state(limits);
    RuntimeMagBeamAcquireRequest first = request(0, 1, 10);
    RuntimeMagBeamAcquireRequest second = request(0, 2, 20);
    ok &= expect(state.acquire(first).accepted && state.acquire(second).accepted,
        "action cap probe must acquire two bindings");

    RuntimeMagBeamAdvanceInput fall;
    fall.currentTick = 1;
    fall.sources = {first.source, second.source};
    fall.targets = {first.target, second.target};
    RuntimeMagBeamReleaseRequest firstRelease;
    firstRelease.bindingId = 1;
    firstRelease.kinematics = pose(3020, 50.0, -5.0);
    RuntimeMagBeamReleaseRequest secondRelease = firstRelease;
    secondRelease.bindingId = 2;
    secondRelease.kinematics = pose(3021, 50.0, -5.0);
    fall.releases = {firstRelease, secondRelease};
    ok &= expect(state.advance(fall).advanced,
        "action cap probe must enter falling");

    RuntimeMagBeamAdvanceInput impacts;
    impacts.currentTick = 2;
    impacts.sources = {first.source, second.source};
    impacts.targets = {first.target, second.target};
    for (int bindingId = 1; bindingId <= 2; ++bindingId) {
        RuntimeMagBeamHostUpdate impact;
        impact.bindingId = bindingId;
        impact.kind = RuntimeMagBeamHostUpdateKind::ImpactResolved;
        impact.impact.kinematics = pose(3100 + bindingId);
        impact.impact.targetCurrentStrength = 60;
        impact.impact.targetMaximumStrength = 100;
        impacts.hostUpdates.push_back(impact);
    }
    const std::uint64_t before = state.stateHash();
    RuntimeMagBeamAdvanceResult overflow = state.advance(impacts);
    ok &= expect(overflow.actionLimitReached
            && !overflow.advanced
            && overflow.actions.empty()
            && state.stateHash() == before,
        "action overflow must roll back the complete tick");
    return ok;
}

bool testEmptyStateCanonicalTick() {
    bool ok = true;

    RuntimeMagBeamSystemState empty;
    const std::uint64_t emptyHash = empty.stateHash();
    RuntimeMagBeamAdvanceInput idle;
    idle.currentTick = 99;
    const RuntimeMagBeamAdvanceResult idleResult = empty.advance(idle);
    ok &= expect(!idleResult.inputRejected
            && !idleResult.advanced
            && idleResult.actions.empty()
            && empty.lastAdvancedTick() == -1
            && empty.stateHash() == emptyHash,
        "empty advance at an arbitrary world tick must be a canonical no-op");

    RuntimeMagBeamAdvanceInput invalid = idle;
    RuntimeMagBeamReleaseRequest missingRelease;
    missingRelease.bindingId = 1;
    missingRelease.reason = RuntimeMagBeamReleaseReason::Explicit;
    missingRelease.kinematics = pose();
    invalid.releases.push_back(missingRelease);
    const RuntimeMagBeamAdvanceResult invalidResult = empty.advance(invalid);
    ok &= expect(invalidResult.inputRejected
            && !invalidResult.advanced
            && invalidResult.actions.empty()
            && empty.lastAdvancedTick() == -1
            && empty.stateHash() == emptyHash,
        "empty canonicalization must not hide invalid binding input");

    RuntimeMagBeamSystemState released;
    const RuntimeMagBeamAcquireRequest acquired = request();
    ok &= expect(released.acquire(acquired).accepted,
        "empty canonical release probe must acquire");
    RuntimeMagBeamTargetSnapshot deadTarget = acquired.target;
    deadTarget.alive = false;
    const RuntimeMagBeamAdvanceResult cancelled = released.advance(
        advanceInput(1, acquired.source, deadTarget)
    );
    ok &= expect(cancelled.advanced
            && released.bindings().empty()
            && released.lastAdvancedTick() == -1
            && released.nextBindingId() == 2,
        "removing the last binding must clear only the empty tick cursor");

    const std::uint64_t releasedHash = released.stateHash();
    IniFile releasedIni;
    RuntimeMagBeamSystemState releasedRoundTrip;
    std::string error;
    ok &= expect(releasedIni.loadFromString(saveText(released))
            && releasedRoundTrip.load(releasedIni, &error)
            && releasedRoundTrip.stateHash() == releasedHash
            && releasedRoundTrip.lastAdvancedTick() == -1
            && releasedRoundTrip.nextBindingId() == 2,
        "canonical empty roundtrip must preserve hash and binding sequence");

    RuntimeMagBeamSystemState reconciled;
    ok &= expect(reconciled.acquire(request()).accepted,
        "empty canonical reconcile probe must acquire");
    RuntimeMagBeamReconcileInput reconcile;
    reconcile.currentTick = 1;
    const RuntimeMagBeamReconcileReport reconcileReport =
        reconciled.reconcile(reconcile);
    ok &= expect(!reconcileReport.inputRejected
            && reconcileReport.cancelled == 1
            && reconciled.bindings().empty()
            && reconciled.lastAdvancedTick() == -1
            && reconciled.nextBindingId() == 2,
        "reconcile removing the last binding must canonicalize the empty tick");

    IniFile staleEmptyIni;
    ok &= expect(staleEmptyIni.loadFromString(
            "[RuntimeMagBeam]\n"
            "Schema=2\n"
            "LastAdvancedTick=1289\n"
            "NextBindingId=17\n"
            "Count=0\n")
            && releasedRoundTrip.load(staleEmptyIni, &error)
            && releasedRoundTrip.bindings().empty()
            && releasedRoundTrip.lastAdvancedTick() == -1
            && releasedRoundTrip.nextBindingId() == 17,
        "legacy empty saves must normalize a stale tick without rewinding ids");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= testAuthoritativeAcquireGates();
    ok &= testExplicitFriendlyAcquisition();
    ok &= testHostDrivenLifecycleAndImpact();
    ok &= testDeathAndAtomicRelease();
    ok &= testSaveLoadLegacyAndBadAtomic();
    ok &= testActionCapRollback();
    ok &= testEmptyStateCanonicalTick();
    if (!ok) {
        return 1;
    }
    std::cout << "Runtime MagBeam system smoke passed\n";
    return 0;
}
