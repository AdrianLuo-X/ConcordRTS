#include "ui/network/RuntimeMultiplayerMenu.h"
#include "data/campaign/CooperativeScenarioDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/RuntimeRandomMap.h"
#include "runtime/campaign/RuntimeCampaignDataSource.h"
#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"
#include "network/maps/RuntimeMultiplayerMapCache.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace {

namespace fs = std::filesystem;

static_assert(!std::is_copy_constructible_v<RuntimeMultiplayerMenuResult>);
static_assert(!std::is_copy_assignable_v<RuntimeMultiplayerMenuResult>);
static_assert(std::is_nothrow_move_constructible_v<
    RuntimeMultiplayerMenuResult>);
static_assert(std::is_nothrow_move_assignable_v<
    RuntimeMultiplayerMenuResult>);
static_assert(std::is_same_v<
    decltype(RuntimeMultiplayerMenuResult::bridge),
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge>>);

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr
            << "runtime multiplayer menu smoke failure: "
            << message << '\n';
    }
};

bool writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

std::string fingerprintText(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text(16U, '0');
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const std::size_t shift = (text.size() - index - 1U) * 4U;
        text[index] = digits[(value >> shift) & 0x0fU];
    }
    return text;
}

struct FixtureFiles {
    fs::path root;
    fs::path exportRoot;
    fs::path mapPath;
    fs::path rulesPath;
    fs::path artPath;
    fs::path modesPath;
    fs::path battlePath;
    fs::path teamPath;

    ~FixtureFiles() {
        std::error_code ignored;
        fs::remove(mapPath, ignored);
        fs::remove(teamPath, ignored);
        fs::remove(battlePath, ignored);
        fs::remove(modesPath, ignored);
        fs::remove(artPath, ignored);
        fs::remove(rulesPath, ignored);
        fs::remove(exportRoot / "multimd", ignored);
        fs::remove(exportRoot, ignored);
        fs::remove(root / "resources", ignored);
        fs::remove(root, ignored);
    }
};

FixtureFiles makeFixture(SmokeContext& context) {
    FixtureFiles fixture;
    fixture.root = fs::temp_directory_path()
        / ("concord_multiplayer_menu_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch().count()
            ));
    fixture.exportRoot = fixture.root / "resources" / "mix_exports";
    fixture.mapPath = fixture.exportRoot / "multimd" / "arena.map";
    fixture.rulesPath = fixture.root / "rulesmd.ini";
    fixture.artPath = fixture.root / "artmd.ini";
    fixture.modesPath = fixture.root / "mpmodesmd.ini";
    fixture.battlePath = fixture.root / "mpbattlemd.ini";
    fixture.teamPath = fixture.root / "mpteammd.ini";
    std::error_code directoryError;
    fs::create_directories(fixture.mapPath.parent_path(), directoryError);
    context.expect(!directoryError, "fixture directories should be created");
    context.expect(
        writeFile(fixture.rulesPath, "[General]\nBuildSpeed=.7\n")
            && writeFile(fixture.artPath, "[Animations]\n1=TEST\n")
            && writeFile(
                fixture.modesPath,
                "[Battle]\n"
                "1=GUI:Battle,STT:Battle,MPBattleMD.ini,standard\n"
                "2=GUI:Team,STT:Team,MPTeamMD.ini,teamgame\n"
            )
            && writeFile(fixture.battlePath, "[General]\nMode=Battle\n")
            && writeFile(fixture.teamPath, "[General]\nMode=Team\n")
            && writeFile(
                fixture.mapPath,
                "[Map]\nTheater=TEMPERATE\n[IsoMapPack5]\n1=fixture\n"
            ),
        "fixture files should be written"
    );
    return fixture;
}

RuntimeConfig fixtureConfig(const FixtureFiles& fixture) {
    RuntimeConfig config;
    config.localDataRootPath = fixture.root.string();
    config.resourceRootPath = (fixture.root / "missing-game").string();
    config.defaultMapPath = fixture.mapPath.string();
    config.fixedTickHz = 30;
    config.techLevel = 10;
    return config;
}

void testEndpointAndSecureIds(SmokeContext& context) {
    RuntimeMultiplayerDirectEndpoint endpoint;
    std::string error;
    context.expect(
        parseRuntimeMultiplayerDirectEndpoint(
            "example.net:17816",
            endpoint,
            error
        )
            && endpoint
                == RuntimeMultiplayerDirectEndpoint{"example.net", 17816U},
        "host:port should parse: " + error
    );
    context.expect(
        parseRuntimeMultiplayerDirectEndpoint(
            "[2001:db8::1]:25000",
            endpoint,
            error
        )
            && endpoint
                == RuntimeMultiplayerDirectEndpoint{"2001:db8::1", 25000U},
        "bracketed IPv6 should parse: " + error
    );
    const RuntimeMultiplayerDirectEndpoint before = endpoint;
    context.expect(
        !parseRuntimeMultiplayerDirectEndpoint("example.net", endpoint, error)
            && endpoint == before
            && !error.empty(),
        "missing direct port is rejected transactionally"
    );
    context.expect(
        !parseRuntimeMultiplayerDirectEndpoint(
            "example.net:65536",
            endpoint,
            error
        )
            && endpoint == before,
        "out-of-range direct port is rejected transactionally"
    );

    std::string firstId;
    std::string secondId;
    context.expect(
        generateRuntimeMultiplayerSecureId("peer-", firstId, error)
            && generateRuntimeMultiplayerSecureId(
                "peer-",
                secondId,
                error
            )
            && firstId.starts_with("peer-")
            && secondId.starts_with("peer-")
            && firstId.size() == 37U
            && secondId.size() == 37U
            && firstId != secondId,
        "peer ids use independent secure 128-bit nonces: " + error
    );
}

void testCompatibilityManifest(
    SmokeContext& context,
    FixtureFiles& fixture,
    const RuntimeConfig& config,
    RuntimeMultiplayerCompatibility& compatibility
) {
    std::string error;
    context.expect(
        makeRuntimeMultiplayerMenuCompatibility(
            config,
            compatibility,
            error
        )
            && compatibility.protocolVersion
                == runtimeMultiplayerProtocolVersion
            && compatibility.protocolFingerprint != 0U
            && compatibility.contentFingerprint != 0U,
        "rules/art/all declared game-mode files produce compatibility: "
            + error
    );

    const RuntimeMultiplayerCompatibility original = compatibility;
    context.expect(
        writeFile(fixture.teamPath, "[General]\nMode=TeamChanged\n"),
        "changed game-mode fixture should write"
    );
    RuntimeMultiplayerCompatibility changed;
    context.expect(
        makeRuntimeMultiplayerMenuCompatibility(config, changed, error)
            && changed.protocolFingerprint == original.protocolFingerprint
            && changed.contentFingerprint != original.contentFingerprint,
        "a declared game-mode rules edit changes content compatibility"
    );

    std::error_code removeError;
    fs::remove(fixture.teamPath, removeError);
    RuntimeMultiplayerCompatibility unchanged = original;
    context.expect(
        !removeError
            && !makeRuntimeMultiplayerMenuCompatibility(
                config,
                unchanged,
                error
            )
            && unchanged == original
            && error.find("mpteammd.ini") != std::string::npos,
        "missing declared game-mode file fails clearly and transactionally"
    );
    context.expect(
        writeFile(fixture.teamPath, "[General]\nMode=Team\n"),
        "removed game-mode fixture should be restored"
    );
    compatibility = original;
}

RuntimeSkirmishSelection hostSelection(const FixtureFiles& fixture) {
    constexpr const char* colors[] = {"Gold", "Red", "Blue", "Green"};
    constexpr const char* countries[] = {
        "Americans", "Russians", "French", "Germans"
    };
    RuntimeSkirmishSelection selection;
    selection.accepted = true;
    selection.mapPath = fixture.mapPath.string();
    selection.gameMode = "Battle";
    selection.gameModeRulesOverride = "MPBattleMD.ini";
    selection.startingCredits = 25000;
    selection.unitCount = 12;
    selection.gameSpeed = 2;
    selection.shortGame = true;
    selection.mcvRepacks = true;
    selection.crates = false;
    selection.superweapons = true;
    selection.buildOffAlly = false;
    for (int index = 0; index < 4; ++index) {
        RuntimeSkirmishPlayerSlot slot;
        slot.enabled = true;
        slot.human = index == 0;
        slot.name = index == 0 ? "Host" : "Computer";
        slot.country = countries[index];
        slot.color = colors[index];
        slot.team = "-";
        slot.difficulty = index == 0 ? "Medium" : "Hard";
        slot.startPosition = index + 1;
        selection.slots.push_back(std::move(slot));
    }
    selection.playerCountry = "Americans";
    selection.playerColor = "Gold";
    return selection;
}

void testHostProjectionAndMapResolution(
    SmokeContext& context,
    const FixtureFiles& fixture,
    const RuntimeConfig& config
) {
    RuntimeMultiplayerHostSelectionProjection projection;
    std::string error;
    const RuntimeSkirmishSelection selection = hostSelection(fixture);
    const bool projected = projectRuntimeMultiplayerHostSelection(
        config,
        selection,
        "  Host Player  ",
        "peer-host-0123456789",
        0x1020304050607080ULL,
        projection,
        error
    );
    context.expect(
        projected,
        "skirmish selection should project: " + error
    );
    const std::size_t closedCount = static_cast<std::size_t>(std::count_if(
        projection.slots.begin(),
        projection.slots.end(),
        [](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                == RuntimeMultiplayerMatchSlotController::Closed;
        }
    ));
    context.expect(
        projection.lobby.hostMember.role
                == RuntimeMultiplayerRoomMemberRole::Host
            && projection.lobby.hostMember.displayName == "Host Player"
            && projection.lobby.map.canonicalId == "multimd/arena.map"
            && projection.lobby.map.contentFingerprint != 0U
            && projection.lobby.rules.gameMode == "battle"
            && projection.lobby.rules.rulesOverride == "mpbattlemd.ini"
            && projection.slots.size() == 4U
            && projection.slots[0].controller
                == RuntimeMultiplayerMatchSlotController::Human
            && projection.slots[1].controller
                == RuntimeMultiplayerMatchSlotController::Computer
            && projection.slots[2].controller
                == RuntimeMultiplayerMatchSlotController::Computer
            && projection.slots[3].controller
                == RuntimeMultiplayerMatchSlotController::Closed
            && closedCount >= 1U,
        "projection preserves map/rules/AI slots and reserves a remote slot"
    );

    const RuntimeMultiplayerHostSelectionProjection before = projection;
    RuntimeMultiplayerLobbyModel savedLobby;
    RuntimeMultiplayerResumeSave saved;
    RuntimeMultiplayerCompatibility savedCompatibility;
    savedCompatibility.protocolFingerprint = 123;savedCompatibility.contentFingerprint = 456;
    context.expect(RuntimeMultiplayerLobbyModel::createHost(before.lobby, savedLobby, error)
        && savedLobby.replaceSlots(before.slots).accepted, "saved menu room fixture");
    saved.setup = savedLobby.setup();saved.compatibility = savedCompatibility;
    RuntimeMultiplayerHostSelectionProjection resumed;
    context.expect(projectRuntimeMultiplayerResumeSelection(config, saved, savedCompatibility, "New Host", "new-host", resumed, error)
        && resumed.lobby.hostMember.peerId == "new-host" && resumed.lobby.rules == before.lobby.rules
        && resumed.slots == before.slots && resumed.localMapPath == before.localMapPath,
        "resume menu uses saved rules/slots/seed and verifies local map: " + error);
    const auto validResume = resumed;
    auto incompatible = savedCompatibility;incompatible.contentFingerprint++;
    context.expect(!projectRuntimeMultiplayerResumeSelection(config, saved, incompatible, "New Host", "new-host", resumed, error)
        && resumed == validResume, "resume menu rejects changed authority resources transactionally");
    saved.setup.map.contentFingerprint++;
    context.expect(!projectRuntimeMultiplayerResumeSelection(config, saved, savedCompatibility, "New Host", "new-host", resumed, error)
        && resumed == validResume, "resume menu rejects different map bytes");
    context.expect(
        !projectRuntimeMultiplayerHostSelection(
            config,
            selection,
            "Host Player",
            "peer-host-0123456789",
            0U,
            projection,
            error
        )
            && projection == before,
        "invalid host seed is rejected transactionally"
    );

    fs::path resolved;
    const bool resolvedMap = resolveRuntimeMultiplayerMenuMap(
        config,
        before.lobby.map,
        resolved,
        error
    );
    context.expect(
        resolvedMap && fs::equivalent(resolved, fixture.mapPath),
        "client resolves canonical exported map and verifies its bytes: "
            + error
    );

    RuntimeMultiplayerMatchMap unsafeMap = before.lobby.map;
    unsafeMap.canonicalId = "multimd/../arena.map";
    const fs::path resolvedBefore = resolved;
    context.expect(
        !resolveRuntimeMultiplayerMenuMap(
            config,
            unsafeMap,
            resolved,
            error
        )
            && resolved == resolvedBefore,
        "unsafe room map id is rejected before filesystem resolution"
    );
    RuntimeMultiplayerMatchMap wrongMap = before.lobby.map;
    wrongMap.contentFingerprint ^= 1U;
    context.expect(
        !resolveRuntimeMultiplayerMenuMap(
            config,
            wrongMap,
            resolved,
            error
        )
            && resolved == resolvedBefore
            && error.find("do not match") != std::string::npos,
        "mismatched local map bytes cannot pass room verification"
    );
}

void testJoinOptionsAndResultOwnership(
    SmokeContext& context,
    const RuntimeMultiplayerCompatibility& compatibility
) {
    RuntimeLanRoomAdvertisement room;
    room.roomId = "room-secure-0123456789";
    room.roomName = "Verified Room";
    room.hostName = "Host Player";
    room.hostAddress = "203.0.113.8";
    room.contentFingerprint = fingerprintText(
        compatibility.contentFingerprint
    );
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = 24000U;
    room.playerCount = 1U;
    room.maximumPlayers = 8U;

    RuntimeMultiplayerLanJoinOptions join;
    std::string error;
    context.expect(
        makeRuntimeMultiplayerMenuJoinOptions(
            room,
            " Guest Player ",
            "peer-guest-0123456789",
            true,
            "",
            compatibility,
            join,
            error
        )
            && join.room.hostAddress == "203.0.113.8"
            && join.displayName == "Guest Player"
            && join.participation
                == RuntimeMultiplayerParticipationRole::Spectator
            && join.compatibility == compatibility,
        "verified advertisement constructs spectator join options: " + error
    );
    RuntimeLanRoomAdvertisement lockedRoom = room;
    lockedRoom.passwordProtected = true;
    RuntimeMultiplayerLanJoinOptions lockedJoin;
    error.clear();
    context.expect(
        !makeRuntimeMultiplayerMenuJoinOptions(
            lockedRoom,
            "Guest Player",
            "peer-locked-empty",
            false,
            "",
            compatibility,
            lockedJoin,
            error
        )
            && error.find("password") != std::string::npos,
        "locked LAN room should require UI password input"
    );
    error.clear();
    context.expect(
        makeRuntimeMultiplayerMenuJoinOptions(
            lockedRoom,
            "Guest Player",
            "peer-locked-valid",
            false,
            "LAN secret",
            compatibility,
            lockedJoin,
            error
        )
            && lockedJoin.password == "LAN secret",
        "locked LAN room should pass password only into transient join options: "
            + error
    );
    const std::string peerBefore = join.peerId;
    room.contentFingerprint = "0000000000000000";
    context.expect(
        !makeRuntimeMultiplayerMenuJoinOptions(
            room,
            "Guest Player",
            "peer-other-0123456789",
            false,
            "",
            compatibility,
            join,
            error
        )
            && join.peerId == peerBefore
            && error.find("fingerprint") != std::string::npos,
        "room text cannot override independently verified compatibility"
    );

    context.expect(
        std::is_nothrow_move_constructible_v<
            RuntimeMultiplayerMenuResult>
            && !std::is_copy_constructible_v<
                RuntimeMultiplayerMenuResult>,
        "menu result is move-only and carries launch plus bridge ownership"
    );
}

#include "RuntimeMultiplayerCooperativeMenuSmoke.inl"
#include "RuntimeRandomMapLanMenuSmoke.inl"
#include "RuntimeMultiplayerMapCacheSmoke.inl"

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--map-cache-reparse") return testMapCacheReparse(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--random-map-peer") return testRandomMapPeerRecreation(argv[2]);
    SmokeContext context;
    if (argc == 2 && std::string(argv[1]) == "--cooperative-stock") {
        testOriginalCooperativeHostSelections(context);
        return context.failures == 0 ? 0 : 1;
    }
    testEndpointAndSecureIds(context);
    FixtureFiles fixture = makeFixture(context);
    const RuntimeConfig config = fixtureConfig(fixture);
    RuntimeMultiplayerCompatibility compatibility;
    testCompatibilityManifest(
        context,
        fixture,
        config,
        compatibility
    );
    testHostProjectionAndMapResolution(context, fixture, config);
    testDownloadedMapResolution(context);
    testJoinOptionsAndResultOwnership(context, compatibility);
    if (context.failures != 0) return 1;

    std::cout
        << "Runtime multiplayer menu smoke: endpoint=strict"
        << " compatibility=rules+art+all-modes"
        << " host-projection=canonical remote-slot=yes"
        << " map=verified result=move-only ui-entry=optional\n";
    return 0;
}
