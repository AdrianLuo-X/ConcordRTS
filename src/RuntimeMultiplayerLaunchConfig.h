#pragma once

#include "RuntimeMultiplayerMatchSetup.h"
#include "RuntimeShell.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Immutable gameplay projection. It deliberately has no room peer identity,
// display name, process-local map path, or local-player marker. Two machines
// viewing the same match therefore produce equal authority projections even
// when their authenticated peers and filesystem layouts differ.
struct RuntimeMultiplayerLaunchAuthoritySlot {
    std::uint8_t slotIndex = 0U;
    RuntimeMultiplayerMatchSlotController controller =
        RuntimeMultiplayerMatchSlotController::Closed;
    bool enabled = false;
    bool human = false;
    std::string countryId;
    std::string colorId;
    std::string teamId;
    std::string difficultyId;
    std::uint8_t startPosition = 0U;

    bool operator==(
        const RuntimeMultiplayerLaunchAuthoritySlot&
    ) const = default;
};

struct RuntimeMultiplayerLaunchAuthorityProjection {
    std::uint64_t gameplaySetupFingerprint = 0U;
    RuntimeMultiplayerMatchMap map;
    RuntimeMultiplayerMatchRules rules;
    std::vector<RuntimeMultiplayerLaunchAuthoritySlot> slots;

    bool operator==(
        const RuntimeMultiplayerLaunchAuthorityProjection&
    ) const = default;
};

struct RuntimeMultiplayerLaunchConfigResult {
    RuntimeConfig config;
    RuntimeMultiplayerLaunchAuthorityProjection authority;
    // Full room hash retains revision, peer bindings, and display metadata for
    // room diagnostics. It is not used as deterministic gameplay authority.
    std::uint64_t fullRoomSetupHash = 0U;
    bool localPeerIsHost = false;
    bool localPeerIsSpectator = false;
};

// Builds a fresh RuntimeConfig transactionally. Only process-local resource,
// window/performance, and replay-output preferences are copied from baseConfig.
// Menu visibility is launch-flow state and is always disabled. All
// campaign/load state and gameplay fields are cleared or overwritten from the
// canonical setup. resolvedLocalMapPath is stored only in config.defaultMapPath
// and never enters authority or either setup fingerprint.
bool makeRuntimeMultiplayerLaunchConfig(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view localPeerId,
    const std::string& resolvedLocalMapPath,
    std::uint64_t actualMapContentFingerprint,
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerLaunchConfigResult& result,
    std::string& errorMessage,
    std::uint64_t multiplayerTimelineEpoch = 1U
);
