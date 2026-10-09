#include "IniFile.h"
#include "RuntimePsychicStatusSystem.h"

#include <cstdint>
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

RuntimePsychicObjectSnapshot object(
    int id,
    std::string owner,
    std::string type,
    RuntimePsychicObjectKind kind = RuntimePsychicObjectKind::Vehicle
) {
    RuntimePsychicObjectSnapshot value;
    value.objectId = id;
    value.owner = std::move(owner);
    value.typeId = std::move(type);
    value.kind = kind;
    value.present = true;
    value.alive = true;
    return value;
}

RuntimePsychicWarheadSnapshot psychGas() {
    RuntimePsychicWarheadSnapshot value;
    value.warheadId = "PsychGas";
    value.warheadExists = true;
    value.psychedelic = true;
    return value;
}

RuntimePsychicStatusHitRequest hit(
    int tick,
    std::uint64_t eventId,
    int sourceId,
    int targetId,
    bool berserkFriendly = false,
    int duration = 5
) {
    RuntimePsychicStatusHitRequest request;
    request.currentTick = tick;
    request.hitEventId = eventId;
    request.durationTicks = duration;
    request.source = object(sourceId, "Yuri", "CAOS");
    request.target = object(targetId, "Allies", "MTNK");
    request.target.berserkFriendly = berserkFriendly;
    request.warhead = psychGas();
    return request;
}

RuntimePsychicTargetingResult query(
    const RuntimePsychicStatusSystemState& state,
    int tick,
    const RuntimePsychicObjectSnapshot& actor,
    const RuntimePsychicObjectSnapshot& candidate,
    RuntimePsychicRelation relation,
    bool legal = true
) {
    RuntimePsychicTargetingQuery value;
    value.currentTick = tick;
    value.actor = actor;
    value.candidate = candidate;
    value.originalRelation = relation;
    value.hostTargetLegal = legal;
    return state.queryTargeting(value);
}

RuntimePsychicStatusAdvanceInput advanceInput(
    int tick,
    std::vector<RuntimePsychicObjectSnapshot> objects
) {
    RuntimePsychicStatusAdvanceInput input;
    input.currentTick = tick;
    input.objects = std::move(objects);
    return input;
}

std::string saveText(const RuntimePsychicStatusSystemState& state) {
    std::ostringstream output;
    state.save(output);
    return output.str();
}

bool replaceFirst(
    std::string& text,
    const std::string& from,
    const std::string& to
) {
    const std::size_t position = text.find(from);
    if (position == std::string::npos) {
        return false;
    }
    text.replace(position, from.size(), to);
    return true;
}

bool sameActions(
    const std::vector<RuntimePsychicStatusAction>& left,
    const std::vector<RuntimePsychicStatusAction>& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& a = left[index];
        const auto& b = right[index];
        if (a.kind != b.kind
            || a.endReason != b.endReason
            || a.tick != b.tick
            || a.statusId != b.statusId
            || a.hitEventId != b.hitEventId
            || a.sourceObjectId != b.sourceObjectId
            || a.targetObjectId != b.targetObjectId
            || a.sourceOwner != b.sourceOwner
            || a.targetOwner != b.targetOwner
            || a.warheadId != b.warheadId
            || a.startedTick != b.startedTick
            || a.expiresTick != b.expiresTick
            || a.attackOriginalAllies != b.attackOriginalAllies
            || a.sourceValidatedAtLaunch
                != b.sourceValidatedAtLaunch) {
            return false;
        }
    }
    return true;
}

bool testLegacyFrameConversion() {
    bool ok = true;
    int ticks = -1;
    ok &= expect(
        runtimePsychicLegacyFramesToFixedTicks(600, 30, ticks)
            && ticks == 1200,
        "stock 600-frame psychedelic duration must become 1200 ticks at 30 Hz"
    );
    ok &= expect(
        runtimePsychicLegacyFramesToFixedTicks(1, 1, ticks)
            && ticks == 1,
        "legacy frame conversion must ceil fractional fixed ticks"
    );
    ok &= expect(
        runtimePsychicLegacyFramesToFixedTicks(0, 30, ticks)
            && ticks == 0,
        "zero authored frames must preserve the API zero sentinel"
    );
    ticks = 77;
    ok &= expect(
        !runtimePsychicLegacyFramesToFixedTicks(-1, 30, ticks)
            && ticks == 77,
        "negative authored frames must be rejected without changing output"
    );
    ok &= expect(
        !runtimePsychicLegacyFramesToFixedTicks(1, 0, ticks)
            && ticks == 77,
        "non-positive fixed tick rates must be rejected"
    );
    ok &= expect(
        !runtimePsychicLegacyFramesToFixedTicks(
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max(),
            ticks
        ) && ticks == 77,
        "legacy frame conversion must reject int result overflow"
    );
    return ok;
}

bool testApplicationAndTargeting() {
    bool ok = true;
    RuntimePsychicStatusSystemState state;
    const RuntimePsychicStatusHitResult applied = state.applyHit(
        hit(0, 100, 1, 10)
    );
    ok &= expect(
        applied.accepted
            && !applied.idempotent
            && applied.statusId == 1
            && applied.actions.size() == 1
            && applied.actions[0].kind
                == RuntimePsychicStatusActionKind::Applied,
        "psychedelic hit must apply one value-only status action"
    );

    const RuntimePsychicObjectSnapshot actor = object(
        10,
        "Allies",
        "MTNK"
    );
    const RuntimePsychicObjectSnapshot enemy = object(
        20,
        "Soviets",
        "HTNK"
    );
    const RuntimePsychicObjectSnapshot ally = object(
        21,
        "Allies",
        "FV"
    );
    const auto enemyDecision = query(
        state,
        0,
        actor,
        enemy,
        RuntimePsychicRelation::Enemy
    );
    const auto allyDecision = query(
        state,
        0,
        actor,
        ally,
        RuntimePsychicRelation::Allied
    );
    ok &= expect(
        enemyDecision.berserkActive
            && enemyDecision.overridesRelation
            && enemyDecision.canAttack
            && enemyDecision.effectiveRelation
                == RuntimePsychicRelation::Enemy,
        "berserk actor must continue to surface original enemies"
    );
    ok &= expect(
        allyDecision.berserkActive
            && allyDecision.attackOriginalAllies
            && allyDecision.canAttack
            && allyDecision.effectiveRelation
                == RuntimePsychicRelation::Enemy,
        "ordinary berserk units must attack original allies"
    );

    RuntimePsychicStatusSystemState friendlyState;
    ok &= expect(
        friendlyState.applyHit(hit(0, 101, 1, 11, true)).accepted,
        "BerserkFriendly=yes target must accept status"
    );
    RuntimePsychicObjectSnapshot friendlyActor = object(
        11,
        "Allies",
        "MTNK"
    );
    friendlyActor.berserkFriendly = true;
    const auto friendlyDecision = query(
        friendlyState,
        0,
        friendlyActor,
        ally,
        RuntimePsychicRelation::Allied
    );
    ok &= expect(
        friendlyDecision.berserkActive
            && friendlyDecision.attackOriginalAllies
            && friendlyDecision.canAttack
            && friendlyDecision.effectiveRelation
                == RuntimePsychicRelation::Enemy,
        "the actor BerserkFriendly flag must not decide its allegiance override"
    );
    const auto weaponVeto = query(
        friendlyState,
        0,
        friendlyActor,
        ally,
        RuntimePsychicRelation::Allied,
        false
    );
    ok &= expect(
        !weaponVeto.canAttack,
        "allegiance override must not bypass normal weapon legality"
    );
    auto protectedCandidate = ally;
    protectedCandidate.berserkFriendly = true;
    for (auto relation : {RuntimePsychicRelation::Allied, RuntimePsychicRelation::Enemy}) {
        ok &= expect(!query(state, 0, actor, protectedCandidate, relation).canAttack,
            "BerserkFriendly must protect the candidate regardless of its owner");
    }
    std::ostringstream legacySave;
    state.save(legacySave);
    std::string legacy = legacySave.str();
    const auto flag = legacy.find("AttackOriginalAllies=1");
    ok &= expect(flag != std::string::npos, "new berserk effect must serialize its unrestricted allegiance");
    if (flag != std::string::npos) legacy.replace(flag, sizeof("AttackOriginalAllies=1") - 1, "AttackOriginalAllies=0");
    IniFile legacyIni;
    RuntimePsychicStatusSystemState legacyState;
    std::string error;
    ok &= expect(legacyIni.loadFromString(legacy) && legacyState.load(legacyIni, &error),
        "old false-allegiance metadata must remain loadable");
    const auto* legacyStatus = legacyState.findByTarget(actor.objectId);
    ok &= expect(legacyStatus && !legacyStatus->attackOriginalAllies
            && query(legacyState, 0, actor, ally, RuntimePsychicRelation::Allied).canAttack,
        "old metadata must preserve its hash value without preserving the targeting bug");
    return ok;
}

bool testRefreshIdempotencyAndExpiry() {
    bool ok = true;
    RuntimePsychicStatusSystemState state;
    RuntimePsychicStatusHitRequest first = hit(0, 200, 1, 10);
    first.durationTicks = 10;
    ok &= expect(state.applyHit(first).accepted, "initial refresh probe hit");
    const std::uint64_t hashBeforeReplay = state.stateHash();
    const RuntimePsychicStatusHitResult replay = state.applyHit(first);
    ok &= expect(
        replay.accepted
            && replay.idempotent
            && replay.actions.empty()
            && state.stateHash() == hashBeforeReplay,
        "same-tick event replay must be idempotent"
    );

    const auto tick0 = state.advance(advanceInput(
        0,
        {first.source, first.target}
    ));
    ok &= expect(tick0.advanced && tick0.retained == 1, "tick zero advance");
    RuntimePsychicStatusHitRequest refresh = hit(3, 201, 2, 10);
    refresh.durationTicks = 2;
    const RuntimePsychicStatusHitResult refreshed = state.applyHit(refresh);
    const RuntimePsychicStatusRecord* status = state.findByTarget(10);
    ok &= expect(
        refreshed.accepted
            && refreshed.statusId == 1
            && refreshed.actions.size() == 1
            && refreshed.actions[0].kind
                == RuntimePsychicStatusActionKind::Refreshed
            && status != nullptr
            && status->expiresTick == 10
            && status->sourceObjectId == 2,
        "a shorter refresh must preserve status identity and never shorten expiry"
    );
    RuntimePsychicStatusHitRequest extend = hit(4, 202, 3, 10);
    extend.durationTicks = 10;
    ok &= expect(
        state.applyHit(extend).accepted
            && state.findByTarget(10) != nullptr
            && state.findByTarget(10)->expiresTick == 14,
        "a longer refresh must extend from the new hit tick"
    );
    const auto tick13 = state.advance(advanceInput(
        13,
        {extend.source, extend.target}
    ));
    ok &= expect(
        tick13.advanced && tick13.retained == 1,
        "pre-expiry retention"
    );
    const auto tick14 = state.advance(advanceInput(
        14,
        {extend.source, extend.target}
    ));
    ok &= expect(
        tick14.advanced
            && tick14.ended == 1
            && tick14.actions.size() == 1
            && tick14.actions[0].endReason
                == RuntimePsychicStatusEndReason::Expired
            && state.statuses().empty(),
        "status must end exactly at its exclusive expiry tick"
    );
    return ok;
}

bool testApplicationGates() {
    bool ok = true;
    auto expectVeto = [&](RuntimePsychicStatusHitRequest request,
                          RuntimePsychicStatusVeto expected,
                          const std::string& label) {
        RuntimePsychicStatusSystemState state;
        const RuntimePsychicStatusHitResult result = state.applyHit(request);
        ok &= expect(
            !result.accepted
                && result.veto == expected
                && state.statuses().empty(),
            label
        );
    };

    RuntimePsychicStatusHitRequest immune = hit(0, 300, 1, 10);
    immune.target.immuneToPsionics = true;
    expectVeto(immune, RuntimePsychicStatusVeto::TargetImmune,
        "immune target gate");

    RuntimePsychicStatusHitRequest structure = hit(0, 301, 1, 10);
    structure.target.kind = RuntimePsychicObjectKind::Structure;
    expectVeto(structure, RuntimePsychicStatusVeto::IllegalTargetKind,
        "building target gate");

    RuntimePsychicStatusHitRequest aircraft = hit(0, 302, 1, 10);
    aircraft.target.kind = RuntimePsychicObjectKind::Aircraft;
    aircraft.target.airborne = true;
    expectVeto(aircraft, RuntimePsychicStatusVeto::IllegalTargetKind,
        "aircraft target gate");

    RuntimePsychicStatusHitRequest airborne = hit(0, 303, 1, 10);
    airborne.target.airborne = true;
    expectVeto(airborne, RuntimePsychicStatusVeto::TargetAirborne,
        "airborne vehicle gate");

    RuntimePsychicStatusHitRequest dead = hit(0, 304, 1, 10);
    dead.target.alive = false;
    expectVeto(dead, RuntimePsychicStatusVeto::InvalidTarget,
        "dead target gate");

    RuntimePsychicStatusHitRequest ordinary = hit(0, 305, 1, 10);
    ordinary.warhead.psychedelic = false;
    expectVeto(ordinary, RuntimePsychicStatusVeto::InvalidWarhead,
        "non-psychedelic warhead gate");

    RuntimePsychicStatusHitRequest recursive = hit(0, 306, 1, 10);
    recursive.recursionDepth = 1;
    expectVeto(recursive, RuntimePsychicStatusVeto::RecursiveRequest,
        "recursive request gate");
    return ok;
}

bool testLaunchAttributionAndSourceLifetime() {
    bool ok = true;

    RuntimePsychicStatusHitRequest ordinaryDead = hit(0, 350, 1, 10);
    ordinaryDead.source.alive = false;
    RuntimePsychicStatusSystemState ordinary;
    ok &= expect(
        ordinary.applyHit(ordinaryDead).veto
            == RuntimePsychicStatusVeto::InvalidSource,
        "an unverified dead source must still be rejected"
    );

    RuntimePsychicStatusSystemState state;
    RuntimePsychicStatusHitRequest deadAtImpact = ordinaryDead;
    deadAtImpact.hitEventId = 351;
    deadAtImpact.sourceValidatedAtLaunch = true;
    const auto deadAccepted = state.applyHit(deadAtImpact);
    ok &= expect(
        deadAccepted.accepted
            && deadAccepted.actions.size() == 1
            && deadAccepted.actions[0].sourceValidatedAtLaunch,
        "a projectile with cached launch attribution must survive source death"
    );

    RuntimePsychicStatusHitRequest missingAtImpact = hit(0, 352, 2, 11);
    missingAtImpact.source.present = false;
    missingAtImpact.source.alive = false;
    missingAtImpact.sourceValidatedAtLaunch = true;
    ok &= expect(
        state.applyHit(missingAtImpact).accepted,
        "a projectile with a complete cached source identity must survive source removal"
    );

    RuntimePsychicObjectSnapshot changedSource = deadAtImpact.source;
    changedSource.present = true;
    changedSource.alive = true;
    changedSource.owner = "Captured";
    const auto diagnosed = state.advance(advanceInput(
        1,
        {changedSource, deadAtImpact.target, missingAtImpact.target}
    ));
    ok &= expect(
        diagnosed.advanced
            && diagnosed.retained == 2
            && diagnosed.ended == 0
            && diagnosed.sourceIdentityChangedStatusIds.size() == 1
            && diagnosed.sourceIdentityChangedStatusIds[0]
                == deadAccepted.statusId,
        "present source identity reuse must be diagnostic without ending gas"
    );

    const auto sourceAbsent = state.advance(advanceInput(
        2,
        {deadAtImpact.target, missingAtImpact.target}
    ));
    ok &= expect(
        sourceAbsent.advanced
            && sourceAbsent.retained == 2
            && sourceAbsent.ended == 0
            && sourceAbsent.sourceIdentityChangedStatusIds.empty(),
        "missing sources must not end or refresh an established gas status"
    );
    return ok;
}

bool testCompositeHitEventIdentity() {
    bool ok = true;
    RuntimePsychicStatusSystemState state;
    const RuntimePsychicStatusHitRequest first = hit(0, 370, 1, 10);
    ok &= expect(state.applyHit(first).accepted, "first event target setup");
    const std::uint64_t replayHash = state.stateHash();
    const auto replay = state.applyHit(first);
    ok &= expect(
        replay.accepted
            && replay.idempotent
            && replay.actions.empty()
            && state.stateHash() == replayHash,
        "same projectile-target event and attribution must be idempotent"
    );

    RuntimePsychicStatusHitRequest secondTarget = hit(0, 370, 1, 11);
    ok &= expect(
        state.applyHit(secondTarget).accepted
            && state.statuses().size() == 2
            && state.hitEvents().size() == 2,
        "one projectile actor may produce independent events for N targets"
    );

    RuntimePsychicStatusHitRequest conflictingSource = first;
    conflictingSource.source = object(2, "Yuri", "CAOS");
    ok &= expect(
        state.applyHit(conflictingSource).veto
            == RuntimePsychicStatusVeto::EventIdConflict,
        "the same projectile-target event cannot change source attribution"
    );
    RuntimePsychicStatusHitRequest conflictingTargetIdentity = first;
    conflictingTargetIdentity.target.owner = "Captured";
    ok &= expect(
        state.applyHit(conflictingTargetIdentity).veto
            == RuntimePsychicStatusVeto::EventIdConflict,
        "the same projectile-target event cannot change target identity"
    );

    RuntimePsychicStatusHitRequest refresh = hit(1, 371, 2, 10);
    ok &= expect(state.applyHit(refresh).accepted, "event receipt refresh setup");
    const auto oldReplay = state.applyHit(first);
    ok &= expect(
        oldReplay.accepted && oldReplay.idempotent,
        "an earlier receipt must remain idempotent during the active status"
    );
    return ok;
}

bool testReconcileInvalidation() {
    bool ok = true;
    RuntimePsychicStatusSystemState state;
    for (int index = 0; index < 5; ++index) {
        ok &= expect(
            state.applyHit(hit(
                0,
                static_cast<std::uint64_t>(400 + index),
                index + 1,
                10 + index
            )).accepted,
            "reconcile setup hit"
        );
    }

    std::vector<RuntimePsychicObjectSnapshot> objects;
    for (int index = 0; index < 5; ++index) {
        objects.push_back(object(10 + index, "Allies", "MTNK"));
    }
    objects[0].alive = false;       // target 10
    objects[1].owner = "Captured"; // target 11
    objects[2].typeId = "CHANGED"; // target 12
    objects.erase(objects.begin() + 3); // target 13 missing
    objects[3].immuneToPsionics = true; // target 14

    RuntimePsychicStatusReconcileInput input;
    input.currentTick = 0;
    input.validateSources = false;
    input.objects = objects;
    const RuntimePsychicStatusReconcileReport report = state.reconcile(input);
    ok &= expect(
        !report.inputRejected
            && report.ended == 5
            && report.actions.size() == 5
            && report.actions[0].endReason
                == RuntimePsychicStatusEndReason::TargetDead
            && report.actions[1].endReason
                == RuntimePsychicStatusEndReason::TargetIdentityChanged
            && report.actions[2].endReason
                == RuntimePsychicStatusEndReason::TargetIdentityChanged
            && report.actions[3].endReason
                == RuntimePsychicStatusEndReason::TargetMissing
            && report.actions[4].endReason
                == RuntimePsychicStatusEndReason::TargetBecameIneligible
            && state.statuses().empty()
            && state.hitEvents().empty(),
        "target death, identity, absence and eligibility must end gas deterministically"
    );
    return ok;
}

bool testSaveLoadDeterminismAndAtomicFailure() {
    bool ok = true;
    RuntimePsychicStatusSystemState original;
    RuntimePsychicStatusHitRequest request = hit(2, 500, 1, 10, true, 10);
    request.sourceValidatedAtLaunch = true;
    request.source.alive = false;
    ok &= expect(original.applyHit(request).accepted, "save setup hit");
    ok &= expect(
        original.advance(advanceInput(
            4,
            {request.source, request.target}
        )).advanced,
        "save setup advance"
    );

    const std::string encoded = saveText(original);
    ok &= expect(
        encoded.find("Schema=2") != std::string::npos
            && encoded.find("HitEventCount=1") != std::string::npos
            && encoded.find("SourceValidatedAtLaunch=1")
                != std::string::npos,
        "schema 2 must persist launch attribution and hit receipts"
    );
    IniFile ini;
    ok &= expect(ini.loadFromString(encoded), "saved status INI parses");
    RuntimePsychicStatusSystemState loaded;
    std::string error;
    ok &= expect(
        loaded.load(ini, &error)
            && error.empty()
            && loaded.stateHash() == original.stateHash(),
        "mid-status save/load must preserve deterministic state"
    );

    RuntimePsychicStatusAdvanceInput finish;
    finish.currentTick = 12;
    finish.validateSources = false;
    finish.validateTargets = false;
    const auto originalEnd = original.advance(finish);
    const auto loadedEnd = loaded.advance(finish);
    ok &= expect(
        originalEnd.advanced
            && loadedEnd.advanced
            && sameActions(originalEnd.actions, loadedEnd.actions)
            && original.stateHash() == loaded.stateHash(),
        "restored and uninterrupted states must end identically"
    );

    const std::string legacyText =
        "[RuntimePsychicStatus]\n"
        "Schema=1\n"
        "LastAdvancedTick=-1\n"
        "NextStatusId=2\n"
        "Count=1\n\n"
        "[RuntimePsychicStatus.Record.0]\n"
        "StatusId=1\n"
        "SourceObjectId=1\n"
        "SourceOwner=59757269\n"
        "SourceType=43414F53\n"
        "SourceKind=1\n"
        "TargetObjectId=10\n"
        "TargetOwner=416C6C696573\n"
        "TargetType=4D544E4B\n"
        "TargetKind=1\n"
        "Warhead=5073796368476173\n"
        "AttackOriginalAllies=0\n"
        "StartedTick=0\n"
        "LastRefreshTick=0\n"
        "ExpiresTick=5\n"
        "LastHitEventId=900\n";
    IniFile legacyIni;
    RuntimePsychicStatusSystemState legacy;
    ok &= expect(
        legacyIni.loadFromString(legacyText)
            && legacy.load(legacyIni, &error)
            && legacy.statuses().size() == 1
            && legacy.hitEvents().size() == 1
            && !legacy.statuses()[0].sourceValidatedAtLaunch,
        "schema 1 status must migrate with a deterministic event receipt"
    );
    RuntimePsychicStatusReconcileInput legacyReconcile;
    legacyReconcile.currentTick = 0;
    legacyReconcile.objects = {object(10, "Allies", "MTNK")};
    const auto legacyReport = legacy.reconcile(legacyReconcile);
    ok &= expect(
        legacyReport.retained == 1 && legacyReport.ended == 0,
        "migrated gas must not depend on its legacy source remaining alive"
    );

    RuntimePsychicStatusSystemState atomic;
    ok &= expect(atomic.applyHit(hit(0, 501, 2, 20)).accepted,
        "atomic failure setup");
    const std::uint64_t beforeBadLoad = atomic.stateHash();
    std::string malformed = encoded;
    ok &= expect(replaceFirst(
        malformed,
        "TargetOwner=416C6C696573",
        "TargetOwner=GG"
    ), "locate encoded owner");
    IniFile badIni;
    ok &= expect(badIni.loadFromString(malformed), "malformed payload parses as INI");
    ok &= expect(
        !atomic.load(badIni, &error)
            && !error.empty()
            && atomic.stateHash() == beforeBadLoad,
        "bad save must be rejected atomically"
    );

    std::string badReceipt = encoded;
    ok &= expect(replaceFirst(
        badReceipt,
        "StatusId=1\nAcceptedTick=2\n",
        "StatusId=999\nAcceptedTick=2\n"
    ), "locate hit receipt status reference");
    IniFile badReceiptIni;
    ok &= expect(
        badReceiptIni.loadFromString(badReceipt)
            && !atomic.load(badReceiptIni, &error)
            && !error.empty()
            && atomic.stateHash() == beforeBadLoad,
        "orphaned schema 2 hit receipt must be rejected atomically"
    );
    return ok;
}

bool testLimitsAndTransactions() {
    bool ok = true;
    RuntimePsychicStatusLimits oneStatus;
    oneStatus.maxStatuses = 1;
    RuntimePsychicStatusSystemState capacity(oneStatus);
    ok &= expect(capacity.applyHit(hit(0, 600, 1, 10)).accepted,
        "capacity setup");
    const std::uint64_t capacityHash = capacity.stateHash();
    const auto overCapacity = capacity.applyHit(hit(0, 601, 2, 11));
    ok &= expect(
        !overCapacity.accepted
            && overCapacity.veto
                == RuntimePsychicStatusVeto::CapacityReached
            && capacity.stateHash() == capacityHash,
        "status capacity veto must be atomic"
    );

    RuntimePsychicStatusLimits shortText;
    shortText.maxTextLength = 8;
    RuntimePsychicStatusSystemState boundedText(shortText);
    RuntimePsychicStatusHitRequest longText = hit(0, 602, 1, 10);
    longText.source.owner = "owner-text-is-too-long";
    ok &= expect(
        boundedText.applyHit(longText).veto
            == RuntimePsychicStatusVeto::InvalidSource,
        "text cap must reject unbounded identity storage"
    );

    RuntimePsychicStatusLimits oneAction;
    oneAction.maxActionsPerOperation = 1;
    RuntimePsychicStatusSystemState actionCap(oneAction);
    ok &= expect(actionCap.applyHit(hit(0, 603, 1, 10)).accepted,
        "action cap first setup");
    ok &= expect(actionCap.applyHit(hit(0, 604, 2, 11)).accepted,
        "action cap second setup");
    const std::uint64_t beforeActionCap = actionCap.stateHash();
    RuntimePsychicStatusAdvanceInput expire;
    expire.currentTick = 5;
    expire.validateSources = false;
    expire.validateTargets = false;
    const auto limited = actionCap.advance(expire);
    ok &= expect(
        !limited.advanced
            && limited.actionLimitReached
            && limited.actions.empty()
            && actionCap.stateHash() == beforeActionCap,
        "multi-end action overflow must reject the whole tick atomically"
    );

    RuntimePsychicStatusSystemState duplicateInput;
    const auto duplicateRequest = hit(0, 605, 1, 10);
    ok &= expect(duplicateInput.applyHit(duplicateRequest).accepted,
        "duplicate input setup");
    const std::uint64_t beforeDuplicate = duplicateInput.stateHash();
    const auto duplicateAdvance = duplicateInput.advance(advanceInput(
        0,
        {duplicateRequest.source, duplicateRequest.source,
         duplicateRequest.target}
    ));
    ok &= expect(
        duplicateAdvance.inputRejected
            && duplicateInput.stateHash() == beforeDuplicate,
        "duplicate world records must reject the tick atomically"
    );

    const std::string exhaustedText =
        "[RuntimePsychicStatus]\n"
        "Schema=1\n"
        "LastAdvancedTick=-1\n"
        "NextStatusId=18446744073709551615\n"
        "Count=0\n";
    IniFile exhaustedIni;
    RuntimePsychicStatusSystemState exhausted;
    std::string error;
    ok &= expect(
        exhaustedIni.loadFromString(exhaustedText)
            && exhausted.load(exhaustedIni, &error),
        "ID exhaustion state must load safely"
    );
    ok &= expect(
        exhausted.applyHit(hit(0, 606, 1, 10)).veto
            == RuntimePsychicStatusVeto::IdExhausted,
        "status ID exhaustion must be explicit"
    );

    RuntimePsychicStatusLimits oneEvent;
    oneEvent.maxStatuses = 2;
    oneEvent.maxHitEvents = 1;
    RuntimePsychicStatusSystemState eventCapacity(oneEvent);
    ok &= expect(eventCapacity.applyHit(hit(0, 607, 1, 10)).accepted,
        "event capacity setup");
    const std::uint64_t beforeEventCapacity = eventCapacity.stateHash();
    ok &= expect(
        eventCapacity.applyHit(hit(0, 608, 2, 11)).veto
            == RuntimePsychicStatusVeto::EventCapacityReached
            && eventCapacity.stateHash() == beforeEventCapacity,
        "hit receipt capacity must reject the whole application atomically"
    );

    RuntimePsychicStatusSystemState eventConflict;
    ok &= expect(eventConflict.applyHit(hit(0, 609, 1, 10)).accepted,
        "event conflict setup");
    ok &= expect(
        eventConflict.applyHit(hit(0, 609, 2, 10)).veto
            == RuntimePsychicStatusVeto::EventIdConflict,
        "one projectile-target event cannot change source attribution"
    );
    return ok;
}

}  // namespace

int main() {
    bool success = true;
    success &= testLegacyFrameConversion();
    success &= testApplicationAndTargeting();
    success &= testRefreshIdempotencyAndExpiry();
    success &= testApplicationGates();
    success &= testLaunchAttributionAndSourceLifetime();
    success &= testCompositeHitEventIdentity();
    success &= testReconcileInvalidation();
    success &= testSaveLoadDeterminismAndAtomicFailure();
    success &= testLimitsAndTransactions();
    if (!success) {
        return 1;
    }
    std::cout << "Runtime psychic status smoke test passed\n";
    return 0;
}
