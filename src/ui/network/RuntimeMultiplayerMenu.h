#pragma once

#include "runtime/contracts/RuntimeMultiplayerMenuTypes.h"
#include "runtime/window/RuntimeWindowId.h"
#include "runtime/contracts/RuntimeSkirmishSelection.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>









// Strict address parser used by the direct Internet page. IPv4/host names use
// "host:port" and IPv6 uses "[address]:port". A port is always required.
bool parseRuntimeMultiplayerDirectEndpoint(
    std::string_view text,
    RuntimeMultiplayerDirectEndpoint& endpoint,
    std::string& errorMessage
);

// Generates a cryptographically random protocol-safe identifier such as a
// peer or room id. prefix must itself contain only [A-Za-z0-9_.-].
bool generateRuntimeMultiplayerSecureId(
    std::string_view prefix,
    std::string& identifier,
    std::string& errorMessage
);

// Builds the join compatibility from local authority files. The manifest
// requires rulesmd.ini, artmd.ini, mpmodesmd.ini, and every rules override
// named by MPModesMD. Missing or malformed inputs fail explicitly.
bool makeRuntimeMultiplayerMenuCompatibility(
    const RuntimeConfig& config,
    RuntimeMultiplayerCompatibility& compatibility,
    std::string& errorMessage
);

// Converts an accepted skirmish choice into canonical multiplayer host data.
// randomSeed is supplied explicitly so the helper stays deterministic and is
// testable; the production UI supplies it from the secure system RNG.
bool projectRuntimeMultiplayerHostSelection(
    const RuntimeConfig& baseConfig,
    const RuntimeSkirmishSelection& selection,
    std::string playerName,
    std::string peerId,
    std::uint64_t randomSeed,
    RuntimeMultiplayerHostSelectionProjection& projection,
    std::string& errorMessage
);

// Resolves only safe canonical exported-map ids and proves the local bytes
// match the host's published fingerprint. The room string is never joined to
// a filesystem root before canonical validation.
bool resolveRuntimeMultiplayerMenuMap(
    const RuntimeConfig& config,
    const RuntimeMultiplayerMatchMap& map,
    std::filesystem::path& localMapPath,
    std::string& errorMessage
);

bool projectRuntimeMultiplayerResumeSelection(const RuntimeConfig& config,
    const RuntimeMultiplayerResumeSave& save, const RuntimeMultiplayerCompatibility& compatibility,
    std::string playerName, std::string peerId, RuntimeMultiplayerHostSelectionProjection& projection,
    std::string& errorMessage);

// Modal file selection shared by LAN and Internet recovery buttons.
bool selectRuntimeMultiplayerResumeSave(const RuntimeConfig& config,
    const RuntimeMultiplayerCompatibility& compatibility, std::string playerName,
    std::string peerId, RuntimeWindowId owner, RuntimeMultiplayerResumeSave& save,
    RuntimeMultiplayerHostSelectionProjection& projection, std::string& errorMessage);

// Constructs join options only from a discovered/target-probed advertisement
// and independently computed local compatibility. It does not trust the
// advertisement's printable fingerprint as authority.
bool makeRuntimeMultiplayerMenuJoinOptions(
    const RuntimeLanRoomAdvertisement& room,
    std::string playerName,
    std::string peerId,
    bool spectator,
    std::string password,
    const RuntimeMultiplayerCompatibility& compatibility,
    RuntimeMultiplayerLanJoinOptions& options,
    std::string& errorMessage
);

// Modal Win32 front-end. LAN keeps the local discovery/lobby path. Internet
// dispatches to the configured authenticated TLS service, formal room browser,
// matchmaking, and TCP-relay lobby; it never falls back to direct transport.
RuntimeMultiplayerMenuResult runRuntimeMultiplayerMenu(
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerMenuNetwork network,
    RuntimeWindowId hostWindow = {}
);

// Optional interactive entry for manual window smoke runs.
int runRuntimeMultiplayerMenuUiSmoke(
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerMenuNetwork network,
    RuntimeWindowId hostWindow = {}
);
