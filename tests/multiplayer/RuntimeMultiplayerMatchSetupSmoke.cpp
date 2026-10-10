#include "network/protocol/RuntimeMultiplayerMatchSetup.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Runtime multiplayer match setup smoke failure: "
            << message << '\n';
    }
    return condition;
}

RuntimeMultiplayerMatchMember member(
    std::string peerId,
    std::string displayName,
    RuntimeMultiplayerMatchMemberIdentity identity =
        RuntimeMultiplayerMatchMemberIdentity::Combatant
) {
    RuntimeMultiplayerMatchMember value;
    value.peerId = std::move(peerId);
    value.displayName = std::move(displayName);
    value.identity = identity;
    return value;
}

RuntimeMultiplayerMatchSlot activeSlot(
    std::uint8_t slotIndex,
    RuntimeMultiplayerMatchSlotController controller,
    std::string peerId,
    std::string country,
    std::string color,
    std::string team,
    std::string difficulty,
    std::uint8_t startPosition
) {
    RuntimeMultiplayerMatchSlot value;
    value.slotIndex = slotIndex;
    value.controller = controller;
    value.peerId = std::move(peerId);
    value.countryId = std::move(country);
    value.colorId = std::move(color);
    value.teamId = std::move(team);
    value.difficultyId = std::move(difficulty);
    value.startPosition = startPosition;
    return value;
}

RuntimeMultiplayerMatchSlot closedSlot(std::uint8_t slotIndex) {
    RuntimeMultiplayerMatchSlot value;
    value.slotIndex = slotIndex;
    return value;
}

RuntimeMultiplayerMatchSetup representativeSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 42U;
    setup.hostPeerId = "host-peer";
    setup.map.canonicalId = "maps/official/2peaks.map";
    setup.map.contentFingerprint = 0x1020304050607080ULL;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x8877665544332211ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;
    setup.rules.shortGame = true;
    setup.rules.mcvRepacks = false;
    setup.rules.crates = true;
    setup.rules.superweapons = false;
    setup.rules.buildOffAlly = true;
    setup.members = {
        member("guest-peer", "Guest"),
        member("host-peer", "Host"),
        member(
            "watch-peer",
            "Watcher",
            RuntimeMultiplayerMatchMemberIdentity::Spectator
        )
    };
    setup.slots = {
        activeSlot(
            0U,
            RuntimeMultiplayerMatchSlotController::Human,
            "host-peer",
            "americans",
            "gold",
            "one",
            "medium",
            1U
        ),
        activeSlot(
            1U,
            RuntimeMultiplayerMatchSlotController::Computer,
            "",
            "russians",
            "red",
            "two",
            "hard",
            2U
        ),
        activeSlot(
            2U,
            RuntimeMultiplayerMatchSlotController::Human,
            "guest-peer",
            "korea",
            "blue",
            "one",
            "medium",
            0U
        ),
        closedSlot(3U)
    };
    return setup;
}

RuntimeMultiplayerMatchSetup goldenSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 0x0102030405060708ULL;
    setup.hostPeerId = "h";
    setup.map.canonicalId = "m";
    setup.map.contentFingerprint = 0x1112131415161718ULL;
    setup.rules.gameMode = "b";
    setup.rules.rulesOverride = "r";
    setup.rules.randomSeed = 0x2122232425262728ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;
    setup.rules.shortGame = true;
    setup.rules.mcvRepacks = false;
    setup.rules.crates = true;
    setup.rules.superweapons = false;
    setup.rules.buildOffAlly = true;
    setup.members = {member("g", "G"), member("h", "H")};
    setup.slots = {
        activeSlot(
            0U,
            RuntimeMultiplayerMatchSlotController::Human,
            "h",
            "a",
            "x",
            "-",
            "m",
            1U
        ),
        activeSlot(
            1U,
            RuntimeMultiplayerMatchSlotController::Human,
            "g",
            "b",
            "y",
            "-",
            "m",
            2U
        )
    };
    return setup;
}

bool encode(
    const RuntimeMultiplayerMatchSetup& setup,
    std::vector<std::uint8_t>& bytes,
    const std::string& label
) {
    std::string error;
    return expect(
        encodeRuntimeMultiplayerMatchSetup(setup, bytes, error),
        label + " did not encode: " + error
    );
}

bool encodeRejectedAtomically(
    const RuntimeMultiplayerMatchSetup& setup,
    const std::string& label
) {
    std::vector<std::uint8_t> output{0xdeU, 0xadU};
    const std::vector<std::uint8_t> before = output;
    std::string error;
    return expect(
        !encodeRuntimeMultiplayerMatchSetup(setup, output, error)
            && output == before && !error.empty(),
        label + " was not rejected transactionally"
    );
}

bool decodeRejectedAtomically(
    const std::vector<std::uint8_t>& bytes,
    const std::string& label
) {
    RuntimeMultiplayerMatchSetup output = representativeSetup();
    const RuntimeMultiplayerMatchSetup before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerMatchSetup(bytes, output, error)
            && output == before && !error.empty(),
        label + " was not rejected without changing decode output"
    );
}

void writeU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

void writeU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
}

bool testRoundTripAndCanonicalHash() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup canonical = representativeSetup();
    std::string error;
    success &= expect(
        validateRuntimeMultiplayerMatchSetup(canonical, error),
        "representative canonical setup did not validate: " + error
    );

    RuntimeMultiplayerMatchSetup unordered = canonical;
    std::reverse(unordered.members.begin(), unordered.members.end());
    std::reverse(unordered.slots.begin(), unordered.slots.end());
    unordered.map.canonicalId = "  MAPS\\OFFICIAL\\2PEAKS.MAP  ";
    unordered.rules.gameMode = " Battle ";
    unordered.rules.rulesOverride = " MPBattleMD.INI ";
    unordered.members.front().displayName = "  Watcher  ";
    for (RuntimeMultiplayerMatchSlot& slot : unordered.slots) {
        if (!slot.countryId.empty()) {
            slot.countryId[0] = static_cast<char>(
                slot.countryId[0] - 'a' + 'A'
            );
        }
        if (!slot.colorId.empty()) {
            slot.colorId[0] = static_cast<char>(
                slot.colorId[0] - 'a' + 'A'
            );
        }
    }
    const RuntimeMultiplayerMatchSetup unorderedBefore = unordered;
    error.clear();
    success &= expect(
        !validateRuntimeMultiplayerMatchSetup(unordered, error)
            && !error.empty(),
        "strict validation accepted a noncanonical setup"
    );

    std::vector<std::uint8_t> canonicalBytes;
    std::vector<std::uint8_t> unorderedBytes;
    success &= encode(canonical, canonicalBytes, "canonical setup");
    success &= encode(unordered, unorderedBytes, "unordered setup");
    success &= expect(
        canonicalBytes == unorderedBytes && unordered == unorderedBefore,
        "encode did not canonicalize a copy or changed caller state"
    );

    RuntimeMultiplayerMatchSetup normalized = unordered;
    error.clear();
    success &= expect(
        canonicalizeRuntimeMultiplayerMatchSetup(normalized, error)
            && normalized == canonical,
        "public canonicalization did not normalize setup: " + error
    );

    RuntimeMultiplayerMatchSetup decoded;
    error.clear();
    success &= expect(
        decodeRuntimeMultiplayerMatchSetup(
            canonicalBytes,
            decoded,
            error
        )
            && decoded == canonical,
        "canonical setup did not round-trip: " + error
    );
    std::vector<std::uint8_t> replayBytes;
    success &= encode(decoded, replayBytes, "decoded replay setup");
    success &= expect(
        replayBytes == canonicalBytes
            && canonicalBytes.size()
                <= runtimeMultiplayerMatchMaximumPayloadBytes,
        "round-trip wire image was not stable and bounded"
    );

    std::uint64_t canonicalHash = 0;
    std::uint64_t unorderedHash = 0;
    error.clear();
    const bool canonicalHashed = runtimeMultiplayerMatchSetupHash(
        canonical,
        canonicalHash,
        error
    );
    const bool unorderedHashed = runtimeMultiplayerMatchSetupHash(
        unordered,
        unorderedHash,
        error
    );
    success &= expect(
        canonicalHashed && unorderedHashed
            && canonicalHash != 0U && canonicalHash == unorderedHash,
        "canonical hash changed with input order/spelling: " + error
    );
    return success;
}

bool testGoldenBigEndianWireAndHash() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup setup = goldenSetup();
    std::vector<std::uint8_t> bytes;
    success &= encode(setup, bytes, "golden setup");
    const std::vector<std::uint8_t> expected{
        0x43U, 0x4dU, 0x53U, 0x54U,
        0x00U, 0x03U,
        0x01U, 0x02U, 0x03U, 0x04U,
        0x05U, 0x06U, 0x07U, 0x08U,
        0x00U, 0x01U, 'h',
        0x00U, 0x01U, 'm',
        0x11U, 0x12U, 0x13U, 0x14U,
        0x15U, 0x16U, 0x17U, 0x18U,
        0x00U, 0x01U, 'b',
        0x00U, 0x01U, 'r',
        0x21U, 0x22U, 0x23U, 0x24U,
        0x25U, 0x26U, 0x27U, 0x28U,
        0x00U, 0x00U, 0x00U, 0x1eU,
        0x00U, 0x00U, 0x27U, 0x10U,
        0x00U, 0x00U, 0x00U, 0x0aU,
        0x00U, 0x00U, 0x00U, 0x0aU,
        0x00U, 0x00U, 0x00U, 0x02U,
        0x01U, 0x00U, 0x01U, 0x00U, 0x01U,
        // Version 3: empty cooperative map, fingerprint and country lists.
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U,
        0x02U,
        0x00U, 0x01U, 'g',
        0x00U, 0x01U, 'G',
        0x01U,
        0x00U, 0x01U, 'h',
        0x00U, 0x01U, 'H',
        0x01U,
        0x02U,
        0x00U, 0x01U,
        0x00U, 0x01U, 'h',
        0x00U, 0x01U, 'a',
        0x00U, 0x01U, 'x',
        0x00U, 0x01U, '-',
        0x00U, 0x01U, 'm',
        0x01U,
        0x01U, 0x01U,
        0x00U, 0x01U, 'g',
        0x00U, 0x01U, 'b',
        0x00U, 0x01U, 'y',
        0x00U, 0x01U, '-',
        0x00U, 0x01U, 'm',
        0x02U
    };
    success &= expect(
        bytes == expected,
        "golden wire image did not use the specified big-endian layout"
    );

    std::uint64_t hash = 0;
    std::string error;
    const bool hashed = runtimeMultiplayerMatchSetupHash(
        setup,
        hash,
        error
    );
    success &= expect(
        hashed && hash == 0xc7527ee84cc31279ULL,
        "golden deterministic hash changed: actual="
            + std::to_string(hash) + " error=" + error
    );
    auto legacyBytes = expected;
    legacyBytes[5] = 2;
    legacyBytes.erase(legacyBytes.begin() + 67, legacyBytes.begin() + 79);
    RuntimeMultiplayerMatchSetup legacy;
    success &= expect(decodeRuntimeMultiplayerMatchSetup(legacyBytes, legacy, error)
        && legacy == setup, "ordinary version 2 saved setup remains readable");

    return success;
}

bool testGameSpeedSliderBoundaries() {
    bool success = true;
    std::uint64_t boundaryHashes[2]{};
    for (int boundary = 0; boundary < 2; ++boundary) {
        auto setup = goldenSetup();
        setup.rules.gameSpeed = boundary == 0 ? 0 : 6;
        std::string error;
        success &= expect(validateRuntimeMultiplayerMatchSetup(setup, error),
            "legal game-speed slider boundary did not validate: " + error);
        std::vector<std::uint8_t> bytes;
        success &= encode(setup, bytes, "game-speed slider boundary");
        if (bytes.size() < 62U) return false;
        // The independent golden wire layout above puts GameSpeed at 58.
        success &= expect(bytes[58] == 0 && bytes[59] == 0 && bytes[60] == 0
            && bytes[61] == static_cast<std::uint8_t>(setup.rules.gameSpeed),
            "game speed was shifted to a different numbering convention");
        RuntimeMultiplayerMatchSetup decoded;
        success &= expect(decodeRuntimeMultiplayerMatchSetup(bytes, decoded, error)
            && decoded == setup, "game-speed boundary failed its wire round-trip");
        RuntimeMultiplayerMatchLaunchDescription launch;
        success &= expect(makeRuntimeMultiplayerMatchLaunchDescription(
                decoded, "h", launch, error)
            && launch.rules.gameSpeed == setup.rules.gameSpeed,
            "host launch changed the selected game-speed boundary");
        success &= expect(runtimeMultiplayerMatchSetupHash(
                setup, boundaryHashes[boundary], error),
            "game-speed boundary could not be fingerprinted");
    }
    success &= expect(boundaryHashes[0] != boundaryHashes[1],
        "slowest and unlimited game speeds have identical setup hashes");

    auto invalid = goldenSetup();
    invalid.rules.gameSpeed = -1;
    success &= encodeRejectedAtomically(invalid, "negative game-speed slider");
    std::vector<std::uint8_t> negativeWire;
    success &= encode(goldenSetup(), negativeWire, "negative speed wire seed");
    if (negativeWire.size() < 62U) return false;
    writeU32(negativeWire, 58U, 0xffffffffU);
    success &= decodeRejectedAtomically(negativeWire, "negative game-speed wire");
    return success;
}

bool testGameplaySetupFingerprint() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup setup = representativeSetup();
    std::string error;
    std::uint64_t baseline = 0U;
    success &= expect(
        runtimeMultiplayerGameplaySetupFingerprint(
            setup,
            baseline,
            error
        )
            && baseline != 0U,
        "gameplay setup fingerprint failed: " + error
    );

    RuntimeMultiplayerMatchSetup rebound = setup;
    rebound.revision += 9U;
    rebound.hostPeerId = "new-host";
    rebound.members = {
        member("new-guest", "Renamed Guest"),
        member("new-host", "Renamed Host"),
        member(
            "new-watch",
            "Renamed Watcher",
            RuntimeMultiplayerMatchMemberIdentity::Spectator
        )
    };
    rebound.slots[0].peerId = "new-host";
    rebound.slots[2].peerId = "new-guest";
    std::uint64_t reboundFingerprint = 0U;
    std::uint64_t baselineRoomHash = 0U;
    std::uint64_t reboundRoomHash = 0U;
    error.clear();
    const bool reboundHashed =
        runtimeMultiplayerGameplaySetupFingerprint(
            rebound,
            reboundFingerprint,
            error
        );
    const bool roomHashesMade = runtimeMultiplayerMatchSetupHash(
            setup,
            baselineRoomHash,
            error
        )
        && runtimeMultiplayerMatchSetupHash(
            rebound,
            reboundRoomHash,
            error
        );
    success &= expect(
        reboundHashed && roomHashesMade
            && reboundFingerprint == baseline
            && baselineRoomHash != reboundRoomHash,
        "network/display/revision rebinding changed gameplay identity: "
            + error
    );

    const auto differs = [&baseline, &success](
        RuntimeMultiplayerMatchSetup candidate,
        const std::string& label
    ) {
        std::uint64_t value = 0U;
        std::string localError;
        const bool made = runtimeMultiplayerGameplaySetupFingerprint(
            candidate,
            value,
            localError
        );
        success &= expect(
            made && value != baseline,
            label + " was not covered by gameplay fingerprint: "
                + localError
        );
    };

    RuntimeMultiplayerMatchSetup changed = setup;
    changed.map.canonicalId = "maps/official/other.map";
    differs(changed, "map canonical id");
    changed = setup;
    changed.map.contentFingerprint ^= 1U;
    differs(changed, "map content fingerprint");

    changed = setup;
    changed.rules.gameMode = "unholy";
    differs(changed, "game mode");
    changed = setup;
    changed.rules.rulesOverride = "custom.ini";
    differs(changed, "rules override");
    changed = setup;
    changed.rules.randomSeed ^= 1U;
    differs(changed, "random seed");
    changed = setup;
    ++changed.rules.fixedTickHz;
    differs(changed, "fixed tick rate");
    changed = setup;
    ++changed.rules.startingCredits;
    differs(changed, "starting credits");
    changed = setup;
    ++changed.rules.startingUnits;
    differs(changed, "starting units");
    changed = setup;
    --changed.rules.techLevel;
    differs(changed, "tech level");
    changed = setup;
    ++changed.rules.gameSpeed;
    differs(changed, "game speed");
    changed = setup;
    changed.rules.shortGame = !changed.rules.shortGame;
    differs(changed, "short game flag");
    changed = setup;
    changed.rules.mcvRepacks = !changed.rules.mcvRepacks;
    differs(changed, "MCV repack flag");
    changed = setup;
    changed.rules.crates = !changed.rules.crates;
    differs(changed, "crate flag");
    changed = setup;
    changed.rules.superweapons = !changed.rules.superweapons;
    differs(changed, "superweapon flag");
    changed = setup;
    changed.rules.buildOffAlly = !changed.rules.buildOffAlly;
    differs(changed, "build-off-ally flag");

    changed = setup;
    changed.slots[0].countryId = "british";
    differs(changed, "slot country");
    changed = setup;
    changed.slots[0].colorId = "green";
    differs(changed, "slot color");
    changed = setup;
    changed.slots[0].teamId = "three";
    differs(changed, "slot team");
    changed = setup;
    changed.slots[0].difficultyId = "hard";
    differs(changed, "slot difficulty");
    changed = setup;
    changed.slots[2].startPosition = 3U;
    differs(changed, "slot start position");
    changed = setup;
    changed.slots[1] = closedSlot(1U);
    differs(changed, "slot controller");
    changed = setup;
    changed.slots.pop_back();
    differs(changed, "slot shape");

    changed = setup;
    std::swap(changed.slots[0], changed.slots[1]);
    changed.slots[0].slotIndex = 0U;
    changed.slots[1].slotIndex = 1U;
    differs(changed, "slot-index gameplay assignment");

    changed = setup;
    changed.revision = 0U;
    std::uint64_t unchanged = 0xfeedfacecafebeefULL;
    error.clear();
    success &= expect(
        !runtimeMultiplayerGameplaySetupFingerprint(
            changed,
            unchanged,
            error
        )
            && unchanged == 0xfeedfacecafebeefULL
            && !error.empty(),
        "invalid gameplay setup changed fingerprint output"
    );
    return success;
}

bool testStrictInvalidSetupsAreAtomic() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup valid = representativeSetup();
    RuntimeMultiplayerMatchSetup invalid = valid;

    invalid.revision = 0U;
    success &= encodeRejectedAtomically(invalid, "zero revision");
    invalid = valid;
    invalid.map.contentFingerprint = 0U;
    success &= encodeRejectedAtomically(invalid, "zero map fingerprint");
    invalid = valid;
    invalid.rules.randomSeed = 0U;
    success &= encodeRejectedAtomically(invalid, "zero random seed");
    invalid = valid;
    invalid.hostPeerId = "missing-peer";
    success &= encodeRejectedAtomically(invalid, "missing host member");

    invalid = valid;
    invalid.members.push_back(invalid.members.front());
    success &= encodeRejectedAtomically(invalid, "duplicate peer identity");
    invalid = valid;
    invalid.members[0].identity = static_cast<
        RuntimeMultiplayerMatchMemberIdentity>(0xffU);
    success &= encodeRejectedAtomically(invalid, "unknown member identity");
    invalid = valid;
    invalid.members[0].displayName = "bad\nname";
    success &= encodeRejectedAtomically(invalid, "control in display name");
    invalid = valid;
    invalid.members.resize(runtimeMultiplayerMatchMaximumMembers + 1U);
    success &= encodeRejectedAtomically(invalid, "too many room members");

    invalid = valid;
    invalid.slots[0].controller = static_cast<
        RuntimeMultiplayerMatchSlotController>(0xffU);
    success &= encodeRejectedAtomically(invalid, "unknown slot controller");
    invalid = valid;
    invalid.slots[0].peerId.clear();
    success &= encodeRejectedAtomically(invalid, "unbound human slot");
    invalid = valid;
    invalid.slots[1].peerId = "guest-peer";
    success &= encodeRejectedAtomically(invalid, "computer peer binding");
    invalid = valid;
    invalid.members[2].identity =
        RuntimeMultiplayerMatchMemberIdentity::Spectator;
    invalid.slots[1].peerId = "watch-peer";
    success &= encodeRejectedAtomically(invalid, "computer spectator");
    invalid = valid;
    invalid.members[0].identity =
        RuntimeMultiplayerMatchMemberIdentity::Spectator;
    success &= encodeRejectedAtomically(invalid, "spectator combat slot");
    invalid = valid;
    invalid.slots[3].countryId = "random";
    success &= encodeRejectedAtomically(invalid, "closed slot combat data");
    invalid = valid;
    invalid.slots[2].slotIndex = 7U;
    success &= encodeRejectedAtomically(invalid, "noncontiguous slot index");
    invalid = valid;
    invalid.slots.resize(runtimeMultiplayerMatchMaximumSlots + 1U);
    success &= encodeRejectedAtomically(invalid, "too many battle slots");

    invalid = valid;
    invalid.slots[2].colorId = invalid.slots[0].colorId;
    success &= encodeRejectedAtomically(invalid, "duplicate explicit color");
    invalid = valid;
    invalid.slots[2].startPosition = invalid.slots[0].startPosition;
    success &= encodeRejectedAtomically(invalid, "duplicate explicit start");

    invalid = goldenSetup();
    invalid.members[0].identity =
        RuntimeMultiplayerMatchMemberIdentity::Spectator;
    invalid.slots[1] = closedSlot(1U);
    success &= encodeRejectedAtomically(invalid, "fewer than two combatants");

    invalid = valid;
    invalid.rules.fixedTickHz =
        runtimeMultiplayerMatchMaximumFixedTickHz + 1;
    success &= encodeRejectedAtomically(invalid, "excessive fixed tick rate");
    invalid = valid;
    invalid.rules.startingCredits = -1;
    success &= encodeRejectedAtomically(invalid, "negative starting credits");
    invalid = valid;
    invalid.rules.startingUnits =
        runtimeMultiplayerMatchMaximumStartingUnits + 1;
    success &= encodeRejectedAtomically(invalid, "excessive starting units");
    invalid = valid;
    invalid.rules.techLevel = 0;
    success &= encodeRejectedAtomically(invalid, "zero tech level");
    invalid = valid;
    invalid.rules.gameSpeed =
        runtimeMultiplayerMatchMaximumGameSpeed + 1;
    success &= encodeRejectedAtomically(invalid, "excessive game speed");
    invalid = valid;
    invalid.map.canonicalId = "../escape.map";
    success &= encodeRejectedAtomically(invalid, "parent map resource id");
    invalid = valid;
    invalid.rules.gameMode.assign(
        runtimeMultiplayerMatchMaximumGameModeBytes + 1U,
        'a'
    );
    success &= encodeRejectedAtomically(invalid, "oversized game mode");
    invalid = valid;
    invalid.slots[0].countryId.assign(
        runtimeMultiplayerMatchMaximumSlotOptionBytes + 1U,
        'a'
    );
    success &= encodeRejectedAtomically(invalid, "oversized slot option");

    invalid = valid;
    invalid.revision = 0U;
    const RuntimeMultiplayerMatchSetup before = invalid;
    std::string error;
    success &= expect(
        !canonicalizeRuntimeMultiplayerMatchSetup(invalid, error)
            && invalid == before && !error.empty(),
        "failed canonicalization changed its input"
    );
    std::uint64_t unchangedHash = 0xfeedfaceULL;
    success &= expect(
        !runtimeMultiplayerMatchSetupHash(invalid, unchangedHash, error)
            && unchangedHash == 0xfeedfaceULL && !error.empty(),
        "failed deterministic hash changed its output"
    );
    return success;
}

bool testMaximumCountsAndRandomReservations() {
    RuntimeMultiplayerMatchSetup setup = representativeSetup();
    setup.hostPeerId = "p00";
    setup.members.clear();
    setup.slots.clear();
    for (std::size_t index = 0;
        index < runtimeMultiplayerMatchMaximumMembers;
        ++index) {
        const std::string peerId = "p"
            + std::string(index < 10U ? "0" : "")
            + std::to_string(index);
        const bool combatant = index < runtimeMultiplayerMatchMaximumSlots;
        setup.members.push_back(member(
            peerId,
            "Player " + peerId,
            combatant
                ? RuntimeMultiplayerMatchMemberIdentity::Combatant
                : RuntimeMultiplayerMatchMemberIdentity::Spectator
        ));
        if (combatant) {
            setup.slots.push_back(activeSlot(
                static_cast<std::uint8_t>(index),
                RuntimeMultiplayerMatchSlotController::Human,
                peerId,
                index % 2U == 0U ? "americans" : "russians",
                "color" + std::to_string(index),
                index % 2U == 0U ? "one" : "two",
                "medium",
                static_cast<std::uint8_t>(index + 1U)
            ));
        }
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    RuntimeMultiplayerMatchSetup decoded;
    const bool encoded = encodeRuntimeMultiplayerMatchSetup(
        setup,
        bytes,
        error
    );
    const bool decodedOk = encoded
        && decodeRuntimeMultiplayerMatchSetup(bytes, decoded, error);
    bool success = expect(
        decodedOk && decoded == setup,
        "maximum member/slot setup did not round-trip: " + error
    );

    setup.slots[0].colorId = "random";
    setup.slots[1].colorId = "random";
    setup.slots[0].startPosition = 0U;
    setup.slots[1].startPosition = 0U;
    bytes.clear();
    success &= expect(
        encodeRuntimeMultiplayerMatchSetup(setup, bytes, error),
        "repeated random color/start reservations were rejected: " + error
    );
    return success;
}

bool testMalformedWireIsAtomic() {
    bool success = true;
    std::vector<std::uint8_t> bytes;
    success &= encode(goldenSetup(), bytes, "malformed-wire seed");
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        success &= decodeRejectedAtomically(
            std::vector<std::uint8_t>(
                bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(size)
            ),
            "truncated prefix " + std::to_string(size)
        );
    }

    std::vector<std::uint8_t> malformed = bytes;
    malformed.push_back(0xffU);
    success &= decodeRejectedAtomically(malformed, "trailing byte");

    malformed = bytes;
    malformed[0] ^= 0xffU;
    success &= decodeRejectedAtomically(malformed, "invalid magic");
    malformed = bytes;
    writeU16(
        malformed,
        4U,
        static_cast<std::uint16_t>(runtimeMultiplayerMatchSetupVersion + 1U)
    );
    success &= decodeRejectedAtomically(malformed, "unsupported version");

    // Golden offsets are independently fixed above: flags start at 62,
    // cooperative data at 67, members at 79, and slots at 94.
    malformed = bytes;
    malformed[62U] = 2U;
    success &= decodeRejectedAtomically(malformed, "invalid boolean");
    malformed = bytes;
    malformed[79U] = static_cast<std::uint8_t>(
        runtimeMultiplayerMatchMaximumMembers + 1U
    );
    success &= decodeRejectedAtomically(malformed, "oversized member count");
    malformed = bytes;
    malformed[86U] = 0xffU;
    success &= decodeRejectedAtomically(malformed, "unknown wire identity");
    malformed = bytes;
    malformed[94U] = 9U;
    success &= decodeRejectedAtomically(malformed, "oversized slot count");
    malformed = bytes;
    malformed[96U] = 0xffU;
    success &= decodeRejectedAtomically(malformed, "unknown wire controller");

    malformed = bytes;
    std::swap_ranges(
        malformed.begin() + 80,
        malformed.begin() + 87,
        malformed.begin() + 87
    );
    success &= decodeRejectedAtomically(
        malformed,
        "noncanonical member order"
    );
    malformed = bytes;
    malformed[30U] = static_cast<std::uint8_t>('B');
    success &= decodeRejectedAtomically(
        malformed,
        "noncanonical mode spelling"
    );
    malformed = bytes;
    writeU16(
        malformed,
        14U,
        static_cast<std::uint16_t>(
            runtimeMultiplayerMatchMaximumPeerIdBytes + 1U
        )
    );
    success &= decodeRejectedAtomically(
        malformed,
        "oversized wire peer id"
    );
    malformed = bytes;
    writeU32(malformed, 42U, 0x80000000U);
    success &= decodeRejectedAtomically(
        malformed,
        "wire integer above INT32_MAX"
    );
    malformed.assign(runtimeMultiplayerMatchMaximumPayloadBytes + 1U, 0U);
    success &= decodeRejectedAtomically(malformed, "oversized payload");
    return success;
}

bool testLaunchProjectionAndLocalMarker() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup setup = representativeSetup();
    RuntimeMultiplayerMatchLaunchDescription launch;
    std::string error;
    success &= expect(
        makeRuntimeMultiplayerMatchLaunchDescription(
            setup,
            "guest-peer",
            launch,
            error
        ),
        "combatant launch projection failed: " + error
    );
    std::size_t localSlots = 0;
    for (const RuntimeMultiplayerMatchLaunchSlot& slot : launch.slots) {
        if (slot.local) ++localSlots;
    }
    success &= expect(
        launch.setupRevision == setup.revision
            && launch.setupHash != 0U
            && launch.hostPeerId == setup.hostPeerId
            && !launch.localPeerIsHost
            && !launch.localPeerIsSpectator
            && launch.map == setup.map
            && launch.rules == setup.rules
            && launch.slots.size() == setup.slots.size()
            && localSlots == 1U
            && !launch.slots[0].local
            && launch.slots[0].displayName == "Host"
            && !launch.slots[1].human
            && launch.slots[1].enabled
            && launch.slots[2].local
            && launch.slots[2].displayName == "Guest"
            && !launch.slots[3].enabled,
        "launch projection lost global values, slot shape, or local marker"
    );

    RuntimeMultiplayerMatchSetup spectatingHost = setup;
    spectatingHost.members[1].identity =
        RuntimeMultiplayerMatchMemberIdentity::Spectator;
    spectatingHost.slots[0].controller =
        RuntimeMultiplayerMatchSlotController::Computer;
    spectatingHost.slots[0].peerId.clear();
    RuntimeMultiplayerMatchLaunchDescription spectatorLaunch;
    error.clear();
    const bool spectatorProjected =
        makeRuntimeMultiplayerMatchLaunchDescription(
            spectatingHost,
            "host-peer",
            spectatorLaunch,
            error
        );
    const bool anySpectatorLocal = std::any_of(
        spectatorLaunch.slots.begin(),
        spectatorLaunch.slots.end(),
        [](const RuntimeMultiplayerMatchLaunchSlot& slot) {
            return slot.local;
        }
    );
    success &= expect(
        spectatorProjected
            && spectatorLaunch.localPeerIsHost
            && spectatorLaunch.localPeerIsSpectator
            && !anySpectatorLocal,
        "spectating host did not project without a local combat slot: " + error
    );

    RuntimeMultiplayerMatchLaunchDescription unchanged = launch;
    const RuntimeMultiplayerMatchLaunchDescription before = unchanged;
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerMatchLaunchDescription(
            setup,
            "unknown-peer",
            unchanged,
            error
        )
            && unchanged == before && !error.empty(),
        "unknown local peer changed launch output"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testRoundTripAndCanonicalHash();
    success &= testGoldenBigEndianWireAndHash();
    success &= testGameSpeedSliderBoundaries();
    success &= testGameplaySetupFingerprint();
    success &= testStrictInvalidSetupsAreAtomic();
    success &= testMaximumCountsAndRandomReservations();
    success &= testMalformedWireIsAtomic();
    success &= testLaunchProjectionAndLocalMarker();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer match setup smoke: version="
        << runtimeMultiplayerMatchSetupVersion
        << " members=16 slots=8 endian=big canonical=yes"
        << " hash=fnv1a launchLocal=yes\n";
    return 0;
}
