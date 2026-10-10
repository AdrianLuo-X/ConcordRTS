#include "core/io/IniFile.h"
#include "simulation/ai/RuntimeAirstrikeSystem.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "airstrike system smoke failure: " << message << '\n';
    }
    return condition;
}

int integer(const IniFile& ini, const std::string& section, const std::string& key) {
    try {
        return std::stoi(ini.getValue(section, key, ""));
    } catch (...) {
        return INT_MIN;
    }
}

bool yes(const IniFile& ini, const std::string& section, const std::string& key) {
    const std::string value = ini.getValue(section, key, "");
    return value == "yes" || value == "Yes" || value == "true";
}

RuntimeAirstrikeLimits testLimits() {
    RuntimeAirstrikeLimits limits;
    limits.spawnDelayTicks = 1;
    limits.launchSpacingTicks = 1;
    limits.ingressTicks = 2;
    limits.egressTicks = 2;
    limits.routeMarginCells = 3;
    return limits;
}

RuntimeAirstrikeCallerRules callerRules(const IniFile& ini) {
    RuntimeAirstrikeCallerRules rules;
    rules.normalAircraftCount = integer(ini, "BORIS", "AirstrikeTeam");
    rules.eliteAircraftCount = integer(
        ini,
        "BORIS",
        "EliteAirstrikeTeam"
    );
    rules.normalAircraftTypeId = ini.getValue(
        "BORIS",
        "AirstrikeTeamType",
        ""
    );
    rules.eliteAircraftTypeId = ini.getValue(
        "BORIS",
        "EliteAirstrikeTeamType",
        ""
    );
    rules.normalRechargeTicks = integer(
        ini,
        "BORIS",
        "AirstrikeRechargeTime"
    );
    rules.eliteRechargeTicks = integer(
        ini,
        "BORIS",
        "EliteAirstrikeRechargeTime"
    );
    return rules;
}

RuntimeAirstrikeAircraftDefinition aircraft(const IniFile& ini) {
    RuntimeAirstrikeAircraftDefinition result;
    result.typeId = "BPLN";
    result.exists = yes(ini, "BPLN", "Exists");
    result.isAircraft = yes(ini, "BPLN", "IsAircraft");
    result.weaponId = ini.getValue("BPLN", "Primary", "");
    result.hasAttackWeapon = !result.weaponId.empty();
    result.warheadId = ini.getValue(result.weaponId, "Warhead", "");
    return result;
}

RuntimeAirstrikeTargetSnapshot target(
    bool alive = true,
    bool visible = true,
    bool targetable = true
) {
    return {200, {50, 40}, alive, visible, targetable};
}

RuntimeAirstrikeRequest request(
    const RuntimeAirstrikeCallerRules& rules,
    const RuntimeAirstrikeAircraftDefinition& plane,
    int callerId,
    int tick,
    bool elite = false
) {
    RuntimeAirstrikeRequest result;
    result.callerObjectId = callerId;
    result.callerOwner = "Player";
    result.targetObjectId = 200;
    result.currentTick = tick;
    result.elite = elite;
    result.rules = rules;
    result.aircraft = plane;
    result.target = target();
    result.bounds = {0, 0, 99, 79};
    return result;
}

RuntimeAirstrikeAdvanceResult advance(
    RuntimeAirstrikeSystemState& state,
    int tick,
    const std::vector<RuntimeAirstrikeTargetSnapshot>& targets,
    std::vector<int> destroyed = {}
) {
    return state.advance({tick, targets, std::move(destroyed)});
}

int actionCount(
    const std::vector<RuntimeAirstrikeAction>& actions,
    RuntimeAirstrikeActionKind kind
) {
    return static_cast<int>(std::count_if(
        actions.begin(),
        actions.end(),
        [kind](const RuntimeAirstrikeAction& action) {
            return action.kind == kind;
        }
    ));
}

const RuntimeAirstrikeAction* findAction(
    const std::vector<RuntimeAirstrikeAction>& actions,
    RuntimeAirstrikeActionKind kind
) {
    const auto found = std::find_if(
        actions.begin(),
        actions.end(),
        [kind](const RuntimeAirstrikeAction& action) {
            return action.kind == kind;
        }
    );
    return found == actions.end() ? nullptr : &*found;
}

bool sameAction(
    const RuntimeAirstrikeAction& left,
    const RuntimeAirstrikeAction& right
) {
    return left.kind == right.kind
        && left.tick == right.tick
        && left.strikeId == right.strikeId
        && left.callerObjectId == right.callerObjectId
        && left.targetObjectId == right.targetObjectId
        && left.planeToken == right.planeToken
        && left.planeSlot == right.planeSlot
        && left.aircraftCount == right.aircraftCount
        && left.readyTick == right.readyTick
        && left.elite == right.elite
        && left.owner == right.owner
        && left.aircraftTypeId == right.aircraftTypeId
        && left.weaponId == right.weaponId
        && left.warheadId == right.warheadId
        && left.route.entry.x == right.route.entry.x
        && left.route.entry.y == right.route.entry.y
        && left.route.target.x == right.route.target.x
        && left.route.target.y == right.route.target.y
        && left.route.exit.x == right.route.exit.x
        && left.route.exit.y == right.route.exit.y
        && left.cancelReason == right.cancelReason;
}

bool sameActions(
    const std::vector<RuntimeAirstrikeAction>& left,
    const std::vector<RuntimeAirstrikeAction>& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!sameAction(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

std::string saved(const RuntimeAirstrikeSystemState& state) {
    std::ostringstream output;
    state.save(output);
    return output.str();
}

bool replaceOnce(
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_airstrike_system_smoke <fixture.ini>\n";
        return 2;
    }
    IniFile ini;
    if (!ini.loadFromFile(argv[1])) {
        std::cerr << "failed to load airstrike fixture\n";
        return 2;
    }
    const RuntimeAirstrikeCallerRules rules = callerRules(ini);
    const RuntimeAirstrikeAircraftDefinition plane = aircraft(ini);
    bool ok = true;
    ok &= expect(
        rules.normalAircraftCount == 2
            && rules.eliteAircraftCount == 4
            && rules.normalAircraftTypeId == "BPLN"
            && rules.eliteAircraftTypeId == "BPLN"
            && rules.normalRechargeTicks == 100
            && rules.eliteRechargeTicks == 50
            && plane.exists
            && plane.isAircraft
            && yes(ini, "BPLN", "Spawned")
            && yes(ini, "BPLN", "FlyBy")
            && plane.weaponId == "Maverick3"
            && plane.warheadId == "MIGWH"
            && yes(ini, "AirstrikeFlare", "Airstrike"),
        "fixture must preserve stock Boris 2/4 BPLN and 100/50 semantics"
    );

    // Every veto must be mutation-free, including type substitution and
    // recursive requests from a managed strike aircraft.
    RuntimeAirstrikeSystemState rejected(testLimits());
    RuntimeAirstrikeRequest invalid = request(rules, plane, 1, 10);
    invalid.targetObjectId = 201;
    const std::uint64_t rejectedHash = rejected.stateHash();
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::InvalidTarget
            && rejected.stateHash() == rejectedHash,
        "request target id must match its snapshot without mutating state"
    );
    invalid = request(rules, plane, 1, 10);
    invalid.aircraft.typeId = "OTHER";
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::InvalidAircraftType
            && rejected.stateHash() == rejectedHash,
        "host must not substitute a different legal aircraft type"
    );
    invalid = request(rules, plane, 1, 10);
    invalid.aircraft.exists = false;
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::IllegalAircraftDefinition
            && rejected.stateHash() == rejectedHash,
        "missing aircraft definitions must be rejected without mutating state"
    );
    invalid = request(rules, plane, 1, 10);
    invalid.aircraft.hasAttackWeapon = false;
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::IllegalAircraftDefinition
            && rejected.stateHash() == rejectedHash,
        "unarmed aircraft definitions must be rejected without mutating state"
    );
    invalid = request(rules, plane, 1, 10);
    invalid.sourceStrikeId = 77;
    invalid.sourcePlaneToken = 4;
    invalid.recursionDepth = 1;
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::RecursiveRequest
            && rejected.stateHash() == rejectedHash,
        "managed aircraft must not recursively create airstrikes"
    );
    invalid = request(rules, plane, 1, INT_MAX);
    ok &= expect(
        rejected.requestStrike(invalid).veto
                == RuntimeAirstrikeVeto::InvalidTick
            && rejected.stateHash() == rejectedHash,
        "schedule arithmetic overflow must be rejected atomically"
    );

    RuntimeAirstrikeSystemState state(testLimits());
    RuntimeAirstrikeSystemState deterministic(testLimits());
    const RuntimeAirstrikeRequest normalRequest = request(rules, plane, 1, 10);
    const RuntimeAirstrikeRequestResult marked = state.requestStrike(normalRequest);
    const RuntimeAirstrikeRequestResult markedAgain =
        deterministic.requestStrike(normalRequest);
    ok &= expect(
        marked.accepted
            && marked.strikeId == 1
            && marked.actions.size() == 1
            && marked.actions.front().kind
                == RuntimeAirstrikeActionKind::TargetMarked
            && state.findStrike(1) != nullptr
            && state.findStrike(1)->planes.size() == 2
            && state.stateHash() == deterministic.stateHash()
            && sameActions(marked.actions, markedAgain.actions),
        "normal request must mark a deterministic two-plane strike"
    );
    ok &= expect(
        state.requestStrike(request(rules, plane, 1, 10)).veto
            == RuntimeAirstrikeVeto::CallerBusy,
        "one caller must not own two active strikes"
    );

    const RuntimeAirstrikeAdvanceResult tick11 = advance(state, 11, {target()});
    const std::uint64_t tick11Hash = state.stateHash();
    const RuntimeAirstrikeAdvanceResult tick11Again = advance(
        state,
        11,
        {target()}
    );
    const bool repeatedTickStable = state.stateHash() == tick11Hash;
    const RuntimeAirstrikeAdvanceResult tick12 = advance(state, 12, {target()});
    const RuntimeAirstrikeAdvanceResult tick13 = advance(state, 13, {target()});
    const RuntimeAirstrikeAdvanceResult tick14 = advance(state, 14, {target()});
    const RuntimeAirstrikeAdvanceResult tick15 = advance(state, 15, {target()});
    const RuntimeAirstrikeAdvanceResult tick16 = advance(state, 16, {target()});
    const RuntimeAirstrikeAction* route = findAction(
        tick11.actions,
        RuntimeAirstrikeActionKind::AssignIngressRoute
    );
    const RuntimeAirstrikeAction* cooldown = findAction(
        tick16.actions,
        RuntimeAirstrikeActionKind::StartCallerCooldown
    );
    ok &= expect(
        tick11.advanced
            && actionCount(
                tick11.actions,
                RuntimeAirstrikeActionKind::SpawnAircraft
            ) == 1
            && route != nullptr
            && route->route.entry.x != route->route.exit.x
            && !tick11Again.advanced
            && tick11Again.actions.empty()
            && repeatedTickStable
            && state.lastAdvancedTick() == 16
            && tick12.actions.size() == 2
            && actionCount(
                tick13.actions,
                RuntimeAirstrikeActionKind::DropBomb
            ) == 1
            && actionCount(
                tick14.actions,
                RuntimeAirstrikeActionKind::DropBomb
            ) == 1
            && actionCount(
                tick15.actions,
                RuntimeAirstrikeActionKind::DespawnAircraft
            ) == 1
            && findAction(
                tick15.actions,
                RuntimeAirstrikeActionKind::StartCallerCooldown
            ) == nullptr
            && cooldown != nullptr
            && cooldown->readyTick == 116
            && state.strikes().empty()
            && state.callerReadyTick(1) == 116,
        "spawn/route/bomb/egress/despawn must be ordered and cooldown must start after final exit"
    );
    ok &= expect(
        state.strikes().empty()
            && state.projectileReceipts().size() == 2
            && state.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending
            && state.rootProjectileReceiptStatus(99, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::IdentityConflict,
        "terminal strike removal must retain authoritative root projectile identities"
    );
    const std::uint64_t beforeConflictingAck = state.stateHash();
    ok &= expect(
        state.acknowledgeRootProjectile(99, 1, 16)
                == RuntimeAirstrikeProjectileAckResult::IdentityConflict
            && state.stateHash() == beforeConflictingAck,
        "conflicting projectile acknowledgement must be mutation-free"
    );
    ok &= expect(
        state.acknowledgeRootProjectile(1, 1, 15)
                == RuntimeAirstrikeProjectileAckResult::InvalidTick
            && state.stateHash() == beforeConflictingAck,
        "retroactive projectile acknowledgement must be mutation-free"
    );
    ok &= expect(
        state.acknowledgeRootProjectile(1, 1, 16)
                == RuntimeAirstrikeProjectileAckResult::Accepted
            && state.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Terminal,
        "matching root projectile acknowledgement must terminate its receipt"
    );
    const std::uint64_t afterAcceptedAck = state.stateHash();
    ok &= expect(
        state.acknowledgeRootProjectile(1, 1, 15)
                == RuntimeAirstrikeProjectileAckResult::AlreadyAcknowledged
            && state.stateHash() == afterAcceptedAck,
        "duplicate root projectile acknowledgement must be idempotent"
    );
    IniFile terminalReceiptIni;
    terminalReceiptIni.loadFromString(saved(state));
    RuntimeAirstrikeSystemState terminalReceiptRestored(testLimits());
    std::string terminalReceiptError;
    ok &= expect(
        terminalReceiptRestored.load(
            terminalReceiptIni,
            &terminalReceiptError
        )
            && terminalReceiptRestored.strikes().empty()
            && terminalReceiptRestored.stateHash() == state.stateHash()
            && terminalReceiptRestored.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Terminal
            && terminalReceiptRestored.rootProjectileReceiptStatus(1, 2)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending,
        "terminal and pending receipts must round-trip after strike removal"
    );
    (void)tick11Hash;
    ok &= expect(
        state.requestStrike(request(rules, plane, 1, 115)).veto
                == RuntimeAirstrikeVeto::CooldownActive
            && state.requestStrike(request(rules, plane, 1, 116)).accepted,
        "caller must become ready exactly at terminal tick plus recharge"
    );

    RuntimeAirstrikeSystemState elite(testLimits());
    const RuntimeAirstrikeRequestResult eliteMarked = elite.requestStrike(
        request(rules, plane, 2, 20, true)
    );
    ok &= expect(
        eliteMarked.accepted
            && elite.findStrike(eliteMarked.strikeId)->planes.size() == 4
            && elite.findStrike(eliteMarked.strikeId)->rechargeTicks == 50
            && elite.findStrike(eliteMarked.strikeId)->elite,
        "elite caller must select four aircraft and elite recharge"
    );

    RuntimeAirstrikeSystemState preSpawnCancel(testLimits());
    preSpawnCancel.requestStrike(request(rules, plane, 3, 30));
    const RuntimeAirstrikeAdvanceResult hidden = advance(
        preSpawnCancel,
        31,
        {target(true, false, true)}
    );
    ok &= expect(
        actionCount(hidden.actions, RuntimeAirstrikeActionKind::CancelStrike) == 1
            && actionCount(
                hidden.actions,
                RuntimeAirstrikeActionKind::SpawnAircraft
            ) == 0
            && actionCount(
                hidden.actions,
                RuntimeAirstrikeActionKind::StartCallerCooldown
            ) == 0
            && preSpawnCancel.strikes().empty()
            && preSpawnCancel.callerReady(3, 31),
        "hidden target before spawn must cancel without spawning or cooldown"
    );

    RuntimeAirstrikeSystemState postSpawnCancel(testLimits());
    postSpawnCancel.requestStrike(request(rules, plane, 4, 40));
    const RuntimeAirstrikeAdvanceResult spawned = advance(
        postSpawnCancel,
        41,
        {target()}
    );
    const RuntimeAirstrikeAdvanceResult dead = advance(
        postSpawnCancel,
        42,
        {target(false, true, true)}
    );
    advance(postSpawnCancel, 43, {});
    const RuntimeAirstrikeAdvanceResult cancelledExit = advance(
        postSpawnCancel,
        44,
        {}
    );
    ok &= expect(
        actionCount(spawned.actions, RuntimeAirstrikeActionKind::SpawnAircraft) == 1
            && actionCount(dead.actions, RuntimeAirstrikeActionKind::CancelStrike) == 1
            && actionCount(dead.actions, RuntimeAirstrikeActionKind::BeginEgress) == 1
            && actionCount(dead.actions, RuntimeAirstrikeActionKind::DropBomb) == 0
            && actionCount(
                cancelledExit.actions,
                RuntimeAirstrikeActionKind::DespawnAircraft
            ) == 1
            && postSpawnCancel.callerReadyTick(4) == 144,
        "dead target after spawn must egress without bombing then begin cooldown"
    );

    RuntimeAirstrikeSystemState destroyed(testLimits());
    destroyed.requestStrike(request(rules, plane, 5, 50));
    advance(destroyed, 51, {target()});
    advance(destroyed, 52, {target()});
    const RuntimeAirstrikeAdvanceResult allDestroyed = advance(
        destroyed,
        53,
        {target()},
        {1, 2, 2, -1}
    );
    ok &= expect(
        actionCount(
            allDestroyed.actions,
            RuntimeAirstrikeActionKind::DropBomb
        ) == 0
            && actionCount(
                allDestroyed.actions,
                RuntimeAirstrikeActionKind::StartCallerCooldown
            ) == 1
            && destroyed.callerReadyTick(5) == 153
            && destroyed.strikes().empty(),
        "all destroyed aircraft must terminate strike and start cooldown immediately"
    );

    // Mid-flight persistence is canonical and malformed input is atomic.
    RuntimeAirstrikeSystemState saveSource(testLimits());
    saveSource.requestStrike(request(rules, plane, 6, 60));
    advance(saveSource, 61, {target()});
    const std::string saveText = saved(saveSource);
    IniFile savedIni;
    ok &= expect(savedIni.loadFromString(saveText), "saved state must parse as INI");
    RuntimeAirstrikeSystemState restored(testLimits());
    std::string loadError;
    ok &= expect(
        restored.load(savedIni, &loadError)
            && loadError.empty()
            && restored.stateHash() == saveSource.stateHash(),
        "mid-ingress save/load must preserve canonical hash"
    );
    const RuntimeAirstrikeAdvanceResult sourceNext = advance(
        saveSource,
        62,
        {target()}
    );
    const RuntimeAirstrikeAdvanceResult restoredNext = advance(
        restored,
        62,
        {target()}
    );
    ok &= expect(
        sameActions(sourceNext.actions, restoredNext.actions)
            && restored.stateHash() == saveSource.stateHash(),
        "loaded strike must emit identical next-tick actions"
    );
    const std::uint64_t beforeBadLoad = restored.stateHash();
    std::string malformedText = saveText;
    ok &= expect(
        replaceOnce(malformedText, "PlaneCount=2", "PlaneCount=999"),
        "test must locate PlaneCount"
    );
    IniFile malformedIni;
    malformedIni.loadFromString(malformedText);
    ok &= expect(
        !restored.load(malformedIni, &loadError)
            && !loadError.empty()
            && restored.stateHash() == beforeBadLoad,
        "oversized serialized member count must fail transactionally"
    );
    malformedText = saveText;
    const std::string longOwner(600, 'A');
    ok &= expect(
        replaceOnce(
            malformedText,
            "OwnerHex=506C61796572",
            "OwnerHex=" + longOwner
        ),
        "test must locate encoded owner"
    );
    malformedIni.loadFromString(malformedText);
    ok &= expect(
        !restored.load(malformedIni, &loadError)
            && restored.stateHash() == beforeBadLoad,
        "oversized serialized text must fail transactionally"
    );

    // Version 1 had no root-projectile receipts.  A pre-drop v1 save migrates
    // to an empty receipt set and ordinary targeting without changing the state.
    std::string legacyText = saveText;
    ok &= expect(
        replaceOnce(legacyText, "Version=3", "Version=1")
            && replaceOnce(legacyText, "ForceFire=0\n", "")
            && replaceOnce(
                legacyText,
                "ProjectileReceiptCount=0\n\n",
                ""
            ),
        "test must construct a receipt-free version 1 save"
    );
    IniFile legacyIni;
    legacyIni.loadFromString(legacyText);
    RuntimeAirstrikeSystemState legacyRestored(testLimits());
    ok &= expect(
        legacyRestored.load(legacyIni, &loadError)
            && legacyRestored.projectileReceipts().empty()
            && saved(legacyRestored) == saveText,
        "version 1 save must migrate atomically to an empty receipt set"
    );

    // Version 2 carried projectile receipts but no force-fire authority.
    std::string legacyV2Text = saveText;
    ok &= expect(replaceOnce(legacyV2Text,"Version=3","Version=2")
        && replaceOnce(legacyV2Text,"ForceFire=0\n",""), "construct version 2 save");
    IniFile legacyV2Ini;legacyV2Ini.loadFromString(legacyV2Text);
    RuntimeAirstrikeSystemState legacyV2(testLimits());
    ok &= expect(legacyV2.load(legacyV2Ini,&loadError)&&saved(legacyV2)==saveText,
        "version 2 must default to ordinary hostile targeting");

    RuntimeAirstrikeSystemState forced(testLimits());
    auto forceRequest=normalRequest;forceRequest.forceFire=true;
    ok &= expect(forced.requestStrike(forceRequest).accepted&&forced.findStrike(1)
        &&forced.findStrike(1)->forceFire,"explicit force-fire request must be retained");
    const auto forcedText=saved(forced);IniFile forcedIni;forcedIni.loadFromString(forcedText);
    RuntimeAirstrikeSystemState forcedRestored(testLimits());
    ok &= expect(forcedRestored.load(forcedIni,&loadError)&&saved(forcedRestored)==forcedText,
        "force-fire save must round trip");
    for(const std::string replacement:{"ForceFire=0","ForceFire=2","ForceFire="}) {
        auto altered=forcedText;replaceOnce(altered,"ForceFire=1",replacement);
        IniFile alteredIni;alteredIni.loadFromString(altered);const auto before=forcedRestored.stateHash();
        if(replacement=="ForceFire=0") {
            RuntimeAirstrikeSystemState ordinary(testLimits());
            ok &= expect(ordinary.load(alteredIni,&loadError)&&ordinary.stateHash()!=before,
                "force-fire flag must participate in the canonical state hash");
        } else ok &= expect(!forcedRestored.load(alteredIni,&loadError)&&forcedRestored.stateHash()==before,
            "invalid force-fire fields must fail transactionally");
    }
    std::cout<<"Airstrike force-fire authority: request/save/hash, v1/v2 migration, malformed rejection PASS\n";

    RuntimeAirstrikeCallerRules onePlaneRules = rules;
    onePlaneRules.normalAircraftCount = 1;
    RuntimeAirstrikeLimits receiptLimits = testLimits();
    receiptLimits.terminalReceiptRetentionTicks = 2;
    RuntimeAirstrikeSystemState receiptState(receiptLimits);
    receiptState.requestStrike(request(onePlaneRules, plane, 60, 200));
    advance(receiptState, 201, {target()});
    advance(receiptState, 202, {target()});
    const RuntimeAirstrikeAdvanceResult dropped = advance(
        receiptState,
        203,
        {target()}
    );
    ok &= expect(
        actionCount(dropped.actions, RuntimeAirstrikeActionKind::DropBomb) == 1
            && receiptState.projectileReceipts().size() == 1
            && receiptState.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending,
        "DropBomb must atomically create one pending root projectile receipt"
    );
    const std::string midProjectileText = saved(receiptState);
    IniFile midProjectileIni;
    midProjectileIni.loadFromString(midProjectileText);
    RuntimeAirstrikeSystemState midProjectileRestored(receiptLimits);
    ok &= expect(
        midProjectileRestored.load(midProjectileIni, &loadError)
            && midProjectileRestored.stateHash() == receiptState.stateHash()
            && midProjectileRestored.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending,
        "mid-projectile v2 save/load must preserve root launch authority"
    );
    RuntimeAirstrikeReconcileInput liveProjectileReconcile;
    liveProjectileReconcile.currentTick = 203;
    liveProjectileReconcile.validateProjectileReceipts = true;
    liveProjectileReconcile.liveRootProjectiles = {{1, 1}};
    const RuntimeAirstrikeReconcileReport liveProjectileReport =
        midProjectileRestored.reconcile(liveProjectileReconcile);
    ok &= expect(
        !liveProjectileReport.inputRejected
            && liveProjectileReport.terminatedProjectileReceipts == 0
            && liveProjectileReport.retainedProjectileReceipts == 1
            && midProjectileRestored.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending,
        "explicit live-projectile reconcile must retain pending authority"
    );
    const std::uint64_t beforeBadReceipt = midProjectileRestored.stateHash();
    std::string badReceiptText = midProjectileText;
    ok &= expect(
        replaceOnce(
            badReceiptText,
            "[AirstrikeProjectileReceipt0]\nStrikeId=1\nPlaneToken=1",
            "[AirstrikeProjectileReceipt0]\nStrikeId=2\nPlaneToken=1"
        ),
        "test must locate the projectile receipt identity"
    );
    IniFile badReceiptIni;
    badReceiptIni.loadFromString(badReceiptText);
    ok &= expect(
        !midProjectileRestored.load(badReceiptIni, &loadError)
            && !loadError.empty()
            && midProjectileRestored.stateHash() == beforeBadReceipt,
        "cross-strike receipt identity must fail load transactionally"
    );

    RuntimeAirstrikeReconcileInput projectileReconcile;
    projectileReconcile.currentTick = 203;
    projectileReconcile.validateProjectileReceipts = true;
    const RuntimeAirstrikeReconcileReport sameTickMissing =
        receiptState.reconcile(projectileReconcile);
    ok &= expect(
        !sameTickMissing.inputRejected
            && sameTickMissing.terminatedProjectileReceipts == 0
            && receiptState.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Pending,
        "same-tick reconcile must retain a just-emitted receipt across host ordering"
    );
    projectileReconcile.currentTick = 204;
    projectileReconcile.liveRootProjectiles = {{2, 1}};
    const std::uint64_t beforeBadReconcile = receiptState.stateHash();
    const RuntimeAirstrikeReconcileReport badProjectileReconcile =
        receiptState.reconcile(projectileReconcile);
    ok &= expect(
        badProjectileReconcile.inputRejected
            && receiptState.stateHash() == beforeBadReconcile,
        "projectile reconcile identity conflict must reject atomically"
    );
    projectileReconcile.liveRootProjectiles.clear();
    const RuntimeAirstrikeReconcileReport missingProjectile =
        receiptState.reconcile(projectileReconcile);
    ok &= expect(
        !missingProjectile.inputRejected
            && missingProjectile.terminatedProjectileReceipts == 1
            && receiptState.rootProjectileReceiptStatus(1, 1)
                == RuntimeAirstrikeProjectileReceiptStatus::Terminal,
        "explicit missing-projectile reconcile must terminate a pending receipt"
    );
    projectileReconcile.currentTick = 205;
    const RuntimeAirstrikeReconcileReport retainedTombstone =
        receiptState.reconcile(projectileReconcile);
    projectileReconcile.currentTick = 206;
    const RuntimeAirstrikeReconcileReport expiredTombstone =
        receiptState.reconcile(projectileReconcile);
    ok &= expect(
        retainedTombstone.retainedProjectileReceipts == 1
            && retainedTombstone.expiredProjectileReceipts == 0
            && expiredTombstone.expiredProjectileReceipts == 1
            && receiptState.projectileReceipts().empty(),
        "only an explicitly terminal receipt may expire after bounded retention"
    );

    RuntimeAirstrikeLimits receiptCapLimits = testLimits();
    receiptCapLimits.maxTotalPlanes = 2;
    receiptCapLimits.maxProjectileReceipts = 2;
    RuntimeAirstrikeSystemState receiptCapped(receiptCapLimits);
    receiptCapped.requestStrike(request(rules, plane, 61, 300));
    for (int tick = 301; tick <= 306; ++tick) {
        advance(receiptCapped, tick, {target()});
    }
    ok &= expect(
        receiptCapped.strikes().empty()
            && receiptCapped.projectileReceipts().size() == 2
            && receiptCapped.requestStrike(
                request(rules, plane, 62, 306)
            ).veto == RuntimeAirstrikeVeto::ProjectileReceiptLimitReached,
        "accepted strikes must reserve bounded receipt capacity before launch"
    );

    RuntimeAirstrikeSystemState reconciled(testLimits());
    reconciled.requestStrike(request(rules, plane, 7, 70));
    advance(reconciled, 71, {target()});
    RuntimeAirstrikeReconcileInput reconcileInput;
    reconcileInput.currentTick = 71;
    reconcileInput.validateCallers = true;
    reconcileInput.liveCallerObjectIds = {7};
    reconcileInput.validateTargets = true;
    reconcileInput.targets = {target(true, false, true)};
    reconcileInput.validateAircraft = true;
    reconcileInput.aircraft = {plane};
    const RuntimeAirstrikeReconcileReport reconcileReport =
        reconciled.reconcile(reconcileInput);
    ok &= expect(
        reconcileReport.cancelledStrikes == 1
            && actionCount(
                reconcileReport.actions,
                RuntimeAirstrikeActionKind::CancelStrike
            ) == 1
            && actionCount(
                reconcileReport.actions,
                RuntimeAirstrikeActionKind::BeginEgress
            ) == 1,
        "reconcile must safely cancel and egress a now-hidden target"
    );

    // Exhausting the action budget before a second strike's Cancel action
    // must retain that strike, serialize it, and retry the action next tick.
    RuntimeAirstrikeLimits cappedLimits = testLimits();
    cappedLimits.maxActionsPerAdvance = 2;
    RuntimeAirstrikeSystemState capped(cappedLimits);
    capped.requestStrike(request(rules, plane, 8, 80));
    RuntimeAirstrikeRequest cappedSecond = request(rules, plane, 9, 80);
    cappedSecond.targetObjectId = 201;
    cappedSecond.target.objectId = 201;
    cappedSecond.target.cell = {60, 40};
    const RuntimeAirstrikeRequestResult cappedSecondMarked =
        capped.requestStrike(cappedSecond);
    RuntimeAirstrikeTargetSnapshot hiddenSecond = cappedSecond.target;
    hiddenSecond.visibleToCaller = false;
    const RuntimeAirstrikeAdvanceResult cappedAdvance = capped.advance(
        {81, {target(), hiddenSecond}, {}}
    );
    const RuntimeAirstrikeStrikeState* delayedCancel = capped.findStrike(
        cappedSecondMarked.strikeId
    );
    ok &= expect(
        cappedSecondMarked.accepted
            && cappedAdvance.actionLimitReached
            && cappedAdvance.actions.size() == 2
            && actionCount(
                cappedAdvance.actions,
                RuntimeAirstrikeActionKind::SpawnAircraft
            ) == 1
            && delayedCancel != nullptr
            && delayedCancel->cancelled
            && !delayedCancel->everSpawned
            && !delayedCancel->cancelActionEmitted,
        "exhausted action budget must retain an unannounced pre-spawn cancellation"
    );
    const std::string cappedSave = saved(capped);
    IniFile cappedIni;
    cappedIni.loadFromString(cappedSave);
    RuntimeAirstrikeSystemState cappedRestored(cappedLimits);
    ok &= expect(
        cappedRestored.load(cappedIni, &loadError)
            && cappedRestored.stateHash() == capped.stateHash(),
        "cancelled pre-spawn state awaiting action budget must round-trip"
    );
    advance(cappedRestored, 82, {target()});
    advance(cappedRestored, 83, {target()});
    advance(cappedRestored, 84, {target()});
    const RuntimeAirstrikeAdvanceResult delayedCancelRetry = advance(
        cappedRestored,
        85,
        {target()}
    );
    ok &= expect(
        actionCount(
            delayedCancelRetry.actions,
            RuntimeAirstrikeActionKind::CancelStrike
        ) == 1
            && cappedRestored.findStrike(cappedSecondMarked.strikeId) == nullptr,
        "delayed Cancel action must be emitted before the strike is removed"
    );

    RuntimeAirstrikeLimits memberLimits = testLimits();
    memberLimits.maxPlanesPerStrike = 2;
    RuntimeAirstrikeSystemState memberLimited(memberLimits);
    ok &= expect(
        memberLimited.requestStrike(request(rules, plane, 10, 90, true)).veto
                == RuntimeAirstrikeVeto::PlaneLimitReached
            && memberLimited.strikes().empty(),
        "per-strike member cap must reject oversized elite team atomically"
    );
    RuntimeAirstrikeLimits strikeLimits = testLimits();
    strikeLimits.maxActiveStrikes = 1;
    RuntimeAirstrikeSystemState strikeLimited(strikeLimits);
    ok &= expect(
        strikeLimited.requestStrike(request(rules, plane, 11, 100)).accepted
            && strikeLimited.requestStrike(request(rules, plane, 12, 100)).veto
                == RuntimeAirstrikeVeto::StrikeLimitReached,
        "active strike cap must be enforced across callers"
    );
    RuntimeAirstrikeRequest longText = request(rules, plane, 13, 110);
    longText.callerOwner.assign(300, 'X');
    RuntimeAirstrikeSystemState textLimited(testLimits());
    ok &= expect(
        textLimited.requestStrike(longText).veto
                == RuntimeAirstrikeVeto::InvalidOwner
            && textLimited.strikes().empty(),
        "request text cap must prevent unbounded owner storage"
    );

    if (!ok) {
        return 1;
    }
    std::cout << "runtime airstrike system smoke passed\n";
    return 0;
}
