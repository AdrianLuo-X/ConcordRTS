#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The capture sink normally forwards to
// RuntimeMultiplayerRuntimeBridge::captureGameplayCommand. Keeping it as a
// callback leaves this local input facade independent of RuntimeShell, LAN,
// and session types.
using RuntimeMultiplayerGameplayCaptureCallback = std::function<bool(
    const RuntimeMultiplayerGameplayCommand&,
    std::string&
)>;

struct RuntimeMultiplayerPlayerIntentAuthorityState {
    bool spectator = false;
    bool terminal = false;
    bool localDefeated = false;

    bool operator==(
        const RuntimeMultiplayerPlayerIntentAuthorityState&
    ) const = default;
};

// Building placement is UI state until confirmPlacement succeeds. A false
// previewValid value covers both an unpreviewed selection and a currently
// illegal cell.
struct RuntimeMultiplayerPlacementIntentState {
    RuntimeMultiplayerGameplayProductionLane lane =
        RuntimeMultiplayerGameplayProductionLane::Structures;
    std::uint64_t productionSerial = 0;
    std::string buildingTypeId;
    RuntimeMultiplayerGameplayCell cell;
    bool previewValid = false;

    bool operator==(
        const RuntimeMultiplayerPlacementIntentState&
    ) const = default;
};

// expectedTargetCount is authored as 0, 1, or 2. Collected targets retain
// order because two-stage super weapons assign different meanings to them.
struct RuntimeMultiplayerSuperWeaponIntentState {
    std::string superWeaponId;
    std::size_t expectedTargetCount = 0;
    std::vector<RuntimeMultiplayerGameplaySuperWeaponTarget> targets;

    bool operator==(
        const RuntimeMultiplayerSuperWeaponIntentState&
    ) const = default;
};

class RuntimeMultiplayerPlayerIntentRouter {
public:
    explicit RuntimeMultiplayerPlayerIntentRouter(
        RuntimeMultiplayerGameplayCaptureCallback capture = {}
    );

    void setCaptureCallback(
        RuntimeMultiplayerGameplayCaptureCallback capture
    );

    void setAuthorityState(
        RuntimeMultiplayerPlayerIntentAuthorityState state
    );
    const RuntimeMultiplayerPlayerIntentAuthorityState& authorityState(
    ) const;
    void setSpectator(bool spectator);
    void setTerminal(bool terminal);
    void setLocalDefeated(bool defeated);
    bool mayIssueAuthoritativeIntent() const;

    // Authoritative intents. Every method constructs the corresponding strong
    // GameplayCommand body, canonicalizes it, then invokes capture exactly
    // once. No method mutates authoritative simulation state directly.
    bool targetOrder(
        RuntimeMultiplayerGameplayTargetOrderKind order,
        std::vector<std::int32_t> objectIds,
        RuntimeMultiplayerGameplayTarget target,
        std::string& errorMessage,
        bool append = false
    );
    bool follow(std::vector<std::int32_t> objectIds, std::int32_t targetId,
        std::string& errorMessage, bool append = false);
    bool setPrimaryFactory(std::int32_t objectId, std::string& errorMessage);
    bool setRallyPoint(std::int32_t objectId, RuntimeMultiplayerGameplayCell cell,
        std::string& errorMessage);
    bool communicate(RuntimeMultiplayerGameplayPlayerCommunicationCommand command, std::string& errorMessage);
    bool planWaypoints(RuntimeMultiplayerGameplayWaypointCommand command, std::string& errorMessage);
    bool stop(
        std::vector<std::int32_t> objectIds,
        std::string& errorMessage
    );
    bool secondaryAction(
        RuntimeMultiplayerGameplaySecondaryActionKind action,
        std::vector<std::int32_t> objectIds,
        std::string& errorMessage
    );
    bool queueProduction(
        std::string typeId,
        RuntimeMultiplayerGameplayProductionInsertMode insertMode,
        std::string& errorMessage
    );
    bool cancelProduction(
        RuntimeMultiplayerGameplayProductionLane lane,
        std::uint64_t productionSerial,
        std::string& errorMessage
    );
    bool sell(std::int32_t objectId, std::string& errorMessage);
    bool repair(std::int32_t objectId, std::string& errorMessage);
    bool guard(
        std::vector<std::int32_t> objectIds,
        std::string& errorMessage
    );
    bool scatter(
        std::vector<std::int32_t> objectIds,
        std::string& errorMessage
    );
    bool setProductionHold(
        RuntimeMultiplayerGameplayProductionLane lane,
        std::uint64_t activeProductionSerial,
        bool onHold,
        std::string& errorMessage
    );
    bool holdProduction(
        RuntimeMultiplayerGameplayProductionLane lane,
        std::uint64_t activeProductionSerial,
        std::string& errorMessage
    );
    bool resumeProduction(
        RuntimeMultiplayerGameplayProductionLane lane,
        std::uint64_t activeProductionSerial,
        std::string& errorMessage
    );

    // Placement selection and preview are local-only. Confirmation clears the
    // state only after capture accepts the canonical PlaceBuilding command.
    bool beginPlacement(
        RuntimeMultiplayerGameplayProductionLane lane,
        std::uint64_t productionSerial,
        std::string buildingTypeId,
        std::string& errorMessage
    );
    bool previewPlacement(
        RuntimeMultiplayerGameplayCell cell,
        bool valid,
        std::string& errorMessage
    );
    bool confirmPlacement(std::string& errorMessage);
    void cancelPlacement();
    const std::optional<RuntimeMultiplayerPlacementIntentState>& placement(
    ) const;

    // begin/cancel and incomplete target collection are local-only. A final
    // target captures one FireSuperWeapon command. If capture rejects it, the
    // complete buffer is retained and confirmSuperWeapon retries it verbatim.
    bool beginSuperWeapon(
        std::string superWeaponId,
        std::size_t expectedTargetCount,
        std::string& errorMessage
    );
    bool targetSuperWeapon(
        RuntimeMultiplayerGameplaySuperWeaponTarget target,
        std::string& errorMessage
    );
    bool confirmSuperWeapon(std::string& errorMessage);
    void cancelSuperWeapon();
    const std::optional<RuntimeMultiplayerSuperWeaponIntentState>&
        superWeapon() const;

    // These values belong to the local UI and never invoke capture.
    void setLocalCameraCell(RuntimeMultiplayerGameplayCell cell);
    const RuntimeMultiplayerGameplayCell& localCameraCell() const;
    void setLocalMenuOpen(bool open);
    bool localMenuOpen() const;
    void setLocalSelection(std::vector<std::int32_t> objectIds);
    void clearLocalSelection();
    const std::vector<std::int32_t>& localSelection() const;

private:
    bool captureCommand(
        RuntimeMultiplayerGameplayCommand command,
        std::string& errorMessage
    );

    RuntimeMultiplayerGameplayCaptureCallback capture_;
    RuntimeMultiplayerPlayerIntentAuthorityState authority_;
    std::optional<RuntimeMultiplayerPlacementIntentState> placement_;
    std::optional<RuntimeMultiplayerSuperWeaponIntentState> superWeapon_;
    RuntimeMultiplayerGameplayCell localCameraCell_;
    bool localMenuOpen_ = false;
    std::vector<std::int32_t> localSelection_;
};
