#include "RuntimeMultiplayerLobbyModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Runtime multiplayer lobby model smoke failure: "
            << message << '\n';
    }
    return condition;
}

RuntimeMultiplayerMatchMap testMap() {
    RuntimeMultiplayerMatchMap map;
    map.canonicalId = "maps/official/2peaks.map";
    map.contentFingerprint = 0x1020304050607080ULL;
    return map;
}

RuntimeMultiplayerMatchRules testRules() {
    RuntimeMultiplayerMatchRules rules;
    rules.gameMode = "battle";
    rules.rulesOverride = "mpbattlemd.ini";
    rules.randomSeed = 0x8877665544332211ULL;
    rules.fixedTickHz = 30;
    rules.startingCredits = 10000;
    rules.startingUnits = 10;
    rules.techLevel = 10;
    rules.gameSpeed = 2;
    rules.shortGame = true;
    rules.mcvRepacks = false;
    rules.crates = true;
    rules.superweapons = false;
    rules.buildOffAlly = true;
    return rules;
}

RuntimeMultiplayerRoomMember roomMember(
    std::string peerId,
    std::string displayName,
    RuntimeMultiplayerParticipationRole participation =
        RuntimeMultiplayerParticipationRole::Combatant,
    bool ready = false
) {
    RuntimeMultiplayerRoomMember member;
    member.peerId = std::move(peerId);
    member.displayName = std::move(displayName);
    member.role = member.peerId == "host"
        ? RuntimeMultiplayerRoomMemberRole::Host
        : RuntimeMultiplayerRoomMemberRole::Client;
    member.participation = participation;
    member.ready = ready;
    return member;
}

RuntimeMultiplayerLobbyHostCreateOptions hostOptions(
    std::size_t slotCount
) {
    RuntimeMultiplayerLobbyHostCreateOptions options;
    options.map = testMap();
    options.rules = testRules();
    options.hostMember = roomMember("host", "Host");
    options.slotCount = slotCount;
    return options;
}

bool makeHost(
    std::size_t slotCount,
    RuntimeMultiplayerLobbyModel& model
) {
    std::string errorMessage;
    return RuntimeMultiplayerLobbyModel::createHost(
        hostOptions(slotCount),
        model,
        errorMessage
    );
}

const RuntimeMultiplayerMatchSlot* humanSlot(
    const RuntimeMultiplayerMatchSetup& setup,
    const std::string& peerId
) {
    const auto slot = std::find_if(
        setup.slots.begin(),
        setup.slots.end(),
        [&peerId](const RuntimeMultiplayerMatchSlot& candidate) {
            return candidate.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && candidate.peerId == peerId;
        }
    );
    return slot == setup.slots.end() ? nullptr : &*slot;
}

bool testEightSlotRoomAndReadySet() {
    RuntimeMultiplayerLobbyModel model;
    if (!expect(makeHost(8U, model), "create eight-slot host")) {
        return false;
    }
    bool ok = true;
    ok &= expect(model.initialized(), "host model initialized");
    ok &= expect(model.hostAuthoritative(), "host model authority");
    ok &= expect(model.setup().revision == 1U, "host revision starts at one");
    ok &= expect(model.setup().slots.size() == 8U, "fixed eight-slot capacity");
    ok &= expect(
        model.setup().slots[0].controller
                == RuntimeMultiplayerMatchSlotController::Human
            && model.setup().slots[0].peerId == "host",
        "host defaults to human slot zero"
    );
    ok &= expect(
        std::all_of(
            model.setup().slots.begin() + 1,
            model.setup().slots.end(),
            [](const RuntimeMultiplayerMatchSlot& slot) {
                return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Closed;
            }
        ),
        "remaining slots default closed"
    );
    std::string errorMessage;
    const std::vector<std::string> hostReady = {"host"};
    ok &= expect(
        !model.canStart(hostReady, errorMessage),
        "single waiting host cannot start"
    );
    RuntimeMultiplayerLobbyModel draftClient;
    ok &= expect(
        RuntimeMultiplayerLobbyModel::createClient(
            model.setup(),
            draftClient,
            errorMessage
        ),
        "client accepts canonical one-host draft"
    );
    ok &= expect(
        draftClient.setup() == model.setup(),
        "client preserves canonical one-host draft"
    );
    RuntimeMultiplayerLobbyModel restoredDraftHost;
    ok &= expect(
        RuntimeMultiplayerLobbyModel::restoreHost(
            model.setup(),
            restoredDraftHost,
            errorMessage
        ),
        "host restores canonical one-host draft"
    );
    ok &= expect(
        restoredDraftHost.setup() == model.setup()
            && restoredDraftHost.hostAuthoritative(),
        "restored one-host draft preserves authority and value"
    );

    std::vector<RuntimeMultiplayerRoomMember> members;
    members.push_back(roomMember("host", "Host"));
    for (std::size_t index = 1U; index < 8U; ++index) {
        const std::string peerId = "peer" + std::to_string(index);
        members.push_back(roomMember(peerId, "Player " + std::to_string(index)));
        const RuntimeMultiplayerLobbyEditResult result =
            model.syncMembers(members);
        ok &= expect(
            result.accepted && result.changed
                && result.gameplayChanged && result.rosterChanged,
            "combatant join changes roster and gameplay"
        );
        const RuntimeMultiplayerMatchSlot* slot = humanSlot(
            model.setup(),
            peerId
        );
        ok &= expect(
            slot != nullptr
                && slot->slotIndex == static_cast<std::uint8_t>(index),
            "join takes lowest closed slot"
        );
    }
    ok &= expect(model.setup().revision == 8U, "seven joins advance revision");
    ok &= expect(
        std::all_of(
            model.setup().slots.begin(),
            model.setup().slots.end(),
            [](const RuntimeMultiplayerMatchSlot& slot) {
                return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Human;
            }
        ),
        "eight-slot room becomes full"
    );
    ok &= expect(
        validateRuntimeMultiplayerMatchSetup(model.setup(), errorMessage),
        "full room is canonical"
    );

    std::vector<std::string> readyPeers;
    for (const RuntimeMultiplayerMatchMember& member : model.setup().members) {
        readyPeers.push_back(member.peerId);
    }
    ok &= expect(model.canStart(readyPeers, errorMessage), "full ready room starts");
    readyPeers.pop_back();
    ok &= expect(
        !model.canStart(readyPeers, errorMessage),
        "missing ready combatant blocks start"
    );
    readyPeers.push_back("host");
    ok &= expect(
        !model.canStart(readyPeers, errorMessage),
        "duplicate ready peer is rejected"
    );
    readyPeers.back() = "unknown";
    ok &= expect(
        !model.canStart(readyPeers, errorMessage),
        "unknown ready peer is rejected"
    );
    return ok;
}

bool testSpectatorsRosterAndHostSpectator() {
    RuntimeMultiplayerLobbyModel model;
    if (!expect(makeHost(4U, model), "create spectator test host")) {
        return false;
    }
    bool ok = true;
    std::vector<RuntimeMultiplayerRoomMember> members = {
        roomMember("host", "Host"),
        roomMember("alpha", "Alpha"),
        roomMember(
            "watch",
            "Watcher",
            RuntimeMultiplayerParticipationRole::Spectator
        )
    };
    RuntimeMultiplayerLobbyEditResult result = model.syncMembers(members);
    ok &= expect(result.accepted && result.changed, "join combatant and spectator");
    ok &= expect(humanSlot(model.setup(), "alpha") != nullptr, "combatant has slot");
    ok &= expect(humanSlot(model.setup(), "watch") == nullptr, "spectator has no slot");
    std::string errorMessage;
    const std::vector<std::string> allReady = {"host", "alpha", "watch"};
    ok &= expect(
        model.canStart(allReady, errorMessage),
        "ready spectator participates in ready barrier"
    );
    const std::vector<std::string> combatantsReady = {"host", "alpha"};
    ok &= expect(
        !model.canStart(combatantsReady, errorMessage),
        "unready spectator blocks start"
    );

    members[1].participation = RuntimeMultiplayerParticipationRole::Spectator;
    result = model.syncMembers(members);
    ok &= expect(
        result.accepted && result.changed
            && result.gameplayChanged && result.rosterChanged,
        "combatant to spectator atomically releases slot"
    );
    ok &= expect(humanSlot(model.setup(), "alpha") == nullptr, "released spectator slot");
    result = model.setSlotController(
        1U,
        RuntimeMultiplayerMatchSlotController::Computer
    );
    ok &= expect(result.accepted && result.changed, "released slot can become computer");
    result = model.setSlotController(
        1U,
        RuntimeMultiplayerMatchSlotController::Closed
    );
    ok &= expect(result.accepted && result.changed, "computer slot can become closed");
    members[1].participation = RuntimeMultiplayerParticipationRole::Combatant;
    result = model.syncMembers(members);
    ok &= expect(result.accepted && result.changed, "spectator can rejoin combat");
    ok &= expect(
        humanSlot(model.setup(), "alpha") != nullptr
            && humanSlot(model.setup(), "alpha")->slotIndex == 1U,
        "rejoin reuses lowest closed slot"
    );

    const std::uint64_t displayRevision = model.setup().revision;
    members[2].displayName = "Observer Renamed";
    result = model.syncMembers(members);
    ok &= expect(
        result.accepted && result.changed
            && !result.gameplayChanged && result.rosterChanged,
        "display-only edit is roster-only"
    );
    ok &= expect(
        model.setup().revision == displayRevision + 1U,
        "display edit advances revision"
    );
    members[2].ready = true;
    const std::uint64_t readyRevision = model.setup().revision;
    result = model.syncMembers(members);
    ok &= expect(
        result.accepted && !result.changed,
        "Session ready bit is not copied into setup"
    );
    ok &= expect(
        model.setup().revision == readyRevision,
        "ready-only sync does not advance revision"
    );

    std::vector<RuntimeMultiplayerRoomMember> afterLeave = {
        members[0],
        members[2]
    };
    result = model.syncMembers(afterLeave);
    ok &= expect(result.accepted && result.changed, "combatant leave accepted");
    ok &= expect(humanSlot(model.setup(), "alpha") == nullptr, "leave releases human slot");
    afterLeave.push_back(roomMember("alpha", "Alpha Rejoined"));
    result = model.syncMembers(afterLeave);
    ok &= expect(result.accepted && result.changed, "departed combatant rejoins");
    ok &= expect(
        humanSlot(model.setup(), "alpha") != nullptr
            && humanSlot(model.setup(), "alpha")->slotIndex == 1U,
        "rejoined combatant takes lowest released slot"
    );

    members = {
        roomMember(
            "host",
            "Host",
            RuntimeMultiplayerParticipationRole::Spectator
        ),
        roomMember("alpha", "Alpha"),
        roomMember("bravo", "Bravo"),
        roomMember(
            "watch",
            "Observer Renamed",
            RuntimeMultiplayerParticipationRole::Spectator
        )
    };
    result = model.syncMembers(members);
    ok &= expect(result.accepted && result.changed, "host becomes spectator atomically");
    ok &= expect(humanSlot(model.setup(), "host") == nullptr, "spectating host has no slot");
    ok &= expect(humanSlot(model.setup(), "bravo") != nullptr, "new combatant allocated slot");
    RuntimeMultiplayerMatchRules rules = model.setup().rules;
    rules.crates = !rules.crates;
    result = model.setRules(rules);
    ok &= expect(
        result.accepted && result.changed && result.gameplayChanged,
        "spectating host retains edit authority"
    );
    const std::vector<std::string> spectatorHostReady = {
        "host", "alpha", "bravo", "watch"
    };
    ok &= expect(
        model.canStart(spectatorHostReady, errorMessage),
        "spectating host room starts with two combatants"
    );
    return ok;
}

bool testNoSlotAndSlotEditing() {
    RuntimeMultiplayerLobbyModel model;
    if (!expect(makeHost(8U, model), "create no-slot test host")) {
        return false;
    }
    bool ok = true;
    std::vector<RuntimeMultiplayerRoomMember> members = {
        roomMember("host", "Host")
    };
    for (std::size_t index = 1U; index <= 6U; ++index) {
        members.push_back(roomMember(
            "peer" + std::to_string(index),
            "Player " + std::to_string(index)
        ));
    }
    RuntimeMultiplayerLobbyEditResult result = model.syncMembers(members);
    ok &= expect(result.accepted, "fill seven human slots");
    result = model.setSlotController(
        7U,
        RuntimeMultiplayerMatchSlotController::Computer
    );
    ok &= expect(result.accepted && result.changed, "last slot becomes computer");
    const RuntimeMultiplayerMatchSetup fullBefore = model.setup();
    members.push_back(roomMember("peer7", "Player 7"));
    result = model.syncMembers(members);
    ok &= expect(!result.accepted, "combatant join rejects when no Closed slot");
    ok &= expect(model.setup() == fullBefore, "no-slot rejection is transactional");

    result = model.setSlotOptions(
        0U,
        "americans",
        "gold",
        "one",
        "easy",
        1U
    );
    ok &= expect(
        result.accepted && result.changed
            && result.gameplayChanged && !result.rosterChanged,
        "set all authored slot options"
    );
    result = model.setSlotOptions(
        1U,
        "russians",
        "red",
        "two",
        "hard",
        2U
    );
    ok &= expect(result.accepted, "set second explicit options");
    const RuntimeMultiplayerMatchSetup optionsBefore = model.setup();
    result = model.setSlotOptions(
        1U,
        "russians",
        "gold",
        "two",
        "hard",
        2U
    );
    ok &= expect(!result.accepted, "duplicate active color rejected");
    ok &= expect(model.setup() == optionsBefore, "duplicate color rolls back");
    result = model.setSlotOptions(
        1U,
        "russians",
        "red",
        "two",
        "hard",
        1U
    );
    ok &= expect(!result.accepted, "duplicate explicit start rejected");
    ok &= expect(model.setup() == optionsBefore, "duplicate start rolls back");
    result = model.setSlotController(
        0U,
        RuntimeMultiplayerMatchSlotController::Closed
    );
    ok &= expect(!result.accepted, "cannot strand a combatant by closing human slot");
    ok &= expect(model.setup() == optionsBefore, "stranded combatant rolls back");

    std::vector<RuntimeMultiplayerMatchSlot> movedSlots = model.setup().slots;
    std::swap(movedSlots[0].peerId, movedSlots[1].peerId);
    result = model.replaceSlots(movedSlots);
    ok &= expect(
        result.accepted && result.changed
            && !result.gameplayChanged && result.rosterChanged,
        "atomic human peer slot move is roster-only"
    );
    std::vector<RuntimeMultiplayerMatchSlot> wrongCapacity = model.setup().slots;
    wrongCapacity.pop_back();
    const RuntimeMultiplayerMatchSetup capacityBefore = model.setup();
    result = model.replaceSlots(wrongCapacity);
    ok &= expect(!result.accepted, "slot capacity cannot change");
    ok &= expect(model.setup() == capacityBefore, "capacity rejection rolls back");
    return ok;
}

bool testRevisionRollbackAndOverflow() {
    RuntimeMultiplayerLobbyHostCreateOptions options = hostOptions(2U);
    options.computerSlotIndices = {1U};
    RuntimeMultiplayerLobbyModel model;
    std::string errorMessage;
    if (!expect(
            RuntimeMultiplayerLobbyModel::createHost(
                options,
                model,
                errorMessage
            ),
            "create host with computer"
        )) {
        return false;
    }
    bool ok = true;
    const RuntimeMultiplayerLobbyEditResult noop = model.setMap(model.setup().map);
    ok &= expect(noop.accepted && !noop.changed, "host map noop accepted");
    ok &= expect(model.setup().revision == 1U, "noop does not advance revision");

    RuntimeMultiplayerMatchMap changedMap = model.setup().map;
    changedMap.contentFingerprint ^= 0x55U;
    RuntimeMultiplayerLobbyEditResult result = model.setMap(changedMap);
    ok &= expect(
        result.accepted && result.changed && result.gameplayChanged,
        "map change accepted"
    );
    ok &= expect(model.setup().revision == 2U, "map change advances once");
    RuntimeMultiplayerMatchMap invalidMap = changedMap;
    invalidMap.contentFingerprint = 0U;
    const RuntimeMultiplayerMatchSetup beforeInvalid = model.setup();
    result = model.setMap(invalidMap);
    ok &= expect(!result.accepted, "invalid map rejected");
    ok &= expect(model.setup() == beforeInvalid, "invalid map rolls back");

    RuntimeMultiplayerMatchRules changedRules = model.setup().rules;
    changedRules.startingCredits += 500;
    result = model.setMapAndRules(model.setup().map, changedRules);
    ok &= expect(result.accepted && result.changed, "map/rules transaction accepted");
    ok &= expect(model.setup().revision == 3U, "map/rules advances once");
    RuntimeMultiplayerMatchRules invalidRules = changedRules;
    invalidRules.fixedTickHz = 0;
    const RuntimeMultiplayerMatchSetup beforeInvalidRules = model.setup();
    result = model.setRules(invalidRules);
    ok &= expect(!result.accepted, "invalid rules rejected");
    ok &= expect(model.setup() == beforeInvalidRules, "invalid rules roll back");

    RuntimeMultiplayerMatchSetup maximumRevision = model.setup();
    maximumRevision.revision = std::numeric_limits<std::uint64_t>::max();
    RuntimeMultiplayerLobbyModel restored;
    ok &= expect(
        RuntimeMultiplayerLobbyModel::restoreHost(
            maximumRevision,
            restored,
            errorMessage
        ),
        "restore canonical maximum revision"
    );
    changedRules = restored.setup().rules;
    changedRules.crates = !changedRules.crates;
    const RuntimeMultiplayerMatchSetup beforeOverflow = restored.setup();
    result = restored.setRules(changedRules);
    ok &= expect(!result.accepted, "material edit rejects revision overflow");
    ok &= expect(restored.setup() == beforeOverflow, "overflow rolls back");
    result = restored.setRules(restored.setup().rules);
    ok &= expect(result.accepted && !result.changed, "maximum revision noop accepted");
    return ok;
}

bool testClientReplacementRules() {
    RuntimeMultiplayerLobbyModel host;
    if (!expect(makeHost(3U, host), "create client source host")) return false;
    std::vector<RuntimeMultiplayerRoomMember> members = {
        roomMember("host", "Host"),
        roomMember("guest", "Guest")
    };
    if (!expect(host.syncMembers(members).accepted, "make canonical client source")) {
        return false;
    }
    bool ok = true;
    std::string errorMessage;
    RuntimeMultiplayerLobbyModel client;
    ok &= expect(
        RuntimeMultiplayerLobbyModel::createClient(
            host.setup(),
            client,
            errorMessage
        ),
        "create client from canonical setup"
    );
    ok &= expect(
        client.authority() == RuntimeMultiplayerLobbyAuthority::Client,
        "client authority"
    );
    RuntimeMultiplayerMatchRules clientRules = client.setup().rules;
    clientRules.crates = !clientRules.crates;
    const RuntimeMultiplayerMatchSetup original = client.setup();
    RuntimeMultiplayerLobbyEditResult result = client.setRules(clientRules);
    ok &= expect(!result.accepted, "client host-only edit rejected");
    ok &= expect(client.setup() == original, "client edit leaves state unchanged");
    result = client.replaceCanonicalSetup(original);
    ok &= expect(result.accepted && !result.changed, "exact client replay idempotent");

    RuntimeMultiplayerMatchSetup conflict = original;
    conflict.rules.crates = !conflict.rules.crates;
    result = client.replaceCanonicalSetup(conflict);
    ok &= expect(!result.accepted, "same revision conflict rejected");
    ok &= expect(client.setup() == original, "conflict rolls back");
    RuntimeMultiplayerMatchSetup rollback = original;
    rollback.revision -= 1U;
    result = client.replaceCanonicalSetup(rollback);
    ok &= expect(!result.accepted, "client revision rollback rejected");

    RuntimeMultiplayerMatchSetup displayUpdate = original;
    displayUpdate.revision += 1U;
    const auto guest = std::find_if(
        displayUpdate.members.begin(),
        displayUpdate.members.end(),
        [](const RuntimeMultiplayerMatchMember& member) {
            return member.peerId == "guest";
        }
    );
    if (guest != displayUpdate.members.end()) guest->displayName = "Renamed";
    result = client.replaceCanonicalSetup(displayUpdate);
    ok &= expect(
        result.accepted && result.changed
            && !result.gameplayChanged && result.rosterChanged,
        "client accepts higher display-only revision"
    );
    RuntimeMultiplayerMatchSetup gameplayUpdate = client.setup();
    gameplayUpdate.revision += 1U;
    gameplayUpdate.rules.superweapons = !gameplayUpdate.rules.superweapons;
    result = client.replaceCanonicalSetup(gameplayUpdate);
    ok &= expect(
        result.accepted && result.changed && result.gameplayChanged,
        "client accepts higher gameplay revision"
    );
    RuntimeMultiplayerMatchSetup nonCanonical = client.setup();
    nonCanonical.revision += 1U;
    std::reverse(nonCanonical.members.begin(), nonCanonical.members.end());
    const RuntimeMultiplayerMatchSetup beforeNonCanonical = client.setup();
    result = client.replaceCanonicalSetup(nonCanonical);
    ok &= expect(!result.accepted, "client rejects noncanonical host image");
    ok &= expect(client.setup() == beforeNonCanonical, "noncanonical image rolls back");
    result = host.replaceCanonicalSetup(host.setup());
    ok &= expect(!result.accepted, "host rejects client replacement API");
    return ok;
}

bool testCreationAndSyncFailuresAreTransactional() {
    RuntimeMultiplayerLobbyModel stable;
    if (!expect(makeHost(4U, stable), "create stable model")) return false;
    bool ok = true;
    std::string errorMessage;
    RuntimeMultiplayerLobbyHostCreateOptions invalid = hostOptions(1U);
    const RuntimeMultiplayerLobbyModel before = stable;
    ok &= expect(
        !RuntimeMultiplayerLobbyModel::createHost(
            invalid,
            stable,
            errorMessage
        ),
        "one-slot host rejected"
    );
    ok &= expect(
        stable.authority() == before.authority()
            && stable.setup() == before.setup(),
        "failed creation leaves output unchanged"
    );
    invalid = hostOptions(4U);
    invalid.hostMember.role = RuntimeMultiplayerRoomMemberRole::Client;
    ok &= expect(
        !RuntimeMultiplayerLobbyModel::createHost(
            invalid,
            stable,
            errorMessage
        ),
        "non-Host creator rejected"
    );
    invalid = hostOptions(4U);
    invalid.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    ok &= expect(
        !RuntimeMultiplayerLobbyModel::createHost(
            invalid,
            stable,
            errorMessage
        ),
        "new host cannot begin as spectator"
    );
    invalid = hostOptions(4U);
    invalid.computerSlotIndices = {2U, 2U};
    ok &= expect(
        !RuntimeMultiplayerLobbyModel::createHost(
            invalid,
            stable,
            errorMessage
        ),
        "duplicate initial computer slot rejected"
    );

    std::vector<RuntimeMultiplayerRoomMember> invalidMembers = {
        roomMember("host", "Host"),
        roomMember("host", "Duplicate")
    };
    const RuntimeMultiplayerMatchSetup beforeSync = stable.setup();
    RuntimeMultiplayerLobbyEditResult result = stable.syncMembers(invalidMembers);
    ok &= expect(!result.accepted, "duplicate authoritative peer rejected");
    ok &= expect(stable.setup() == beforeSync, "duplicate roster rolls back");
    invalidMembers = {
        roomMember("host", "Host"),
        roomMember("guest", "Guest")
    };
    invalidMembers[0].role = RuntimeMultiplayerRoomMemberRole::Client;
    invalidMembers[1].role = RuntimeMultiplayerRoomMemberRole::Host;
    result = stable.syncMembers(invalidMembers);
    ok &= expect(!result.accepted, "authoritative host mismatch rejected");
    ok &= expect(stable.setup() == beforeSync, "host mismatch rolls back");
    return ok;
}

#include "RuntimeMultiplayerCooperativeLobbySmoke.inl"

} // namespace

int main() {
    bool ok = true;
    ok &= testEightSlotRoomAndReadySet();
    ok &= testSpectatorsRosterAndHostSpectator();
    ok &= testNoSlotAndSlotEditing();
    ok &= testRevisionRollbackAndOverflow();
    ok &= testClientReplacementRules();
    ok &= testCreationAndSyncFailuresAreTransactional();
    ok &= testCooperativePlayersAndMissionRules();
    if (!ok) return 1;
    std::cout << "Runtime multiplayer lobby model smoke passed\n";
    return 0;
}
