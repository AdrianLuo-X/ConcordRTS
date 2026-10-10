#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"

#include "network/session/RuntimeLanTransport.h"
#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "network/lockstep/RuntimeMultiplayerLockstep.h"
#include "network/protocol/RuntimeMultiplayerMatchSetup.h"
#include "network/protocol/RuntimeMultiplayerProtocol.h"
#include "network/session/RuntimeMultiplayerSessionSystem.h"
#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

using ManifestField = RuntimeMultiplayerProtocolManifestField;
using ManifestValue = RuntimeMultiplayerProtocolManifestValue;

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

constexpr std::array kExpectedManifestFields{
    ManifestField::CompatibilityManifestVersion,
    ManifestField::ContentFingerprintVersion,
    ManifestField::ContentMaximumCanonicalResourceIdBytes,
    ManifestField::ContentMaximumComponents,
    ManifestField::ContentDefaultMaximumComponentBytes,
    ManifestField::ContentHardMaximumComponentBytes,
    ManifestField::LanFramingVersion,
    ManifestField::LanMaximumPacketBytes,
    ManifestField::LanMaximumQueuedBytesPerPeer,
    ManifestField::MultiplayerMessageVersion,
    ManifestField::MultiplayerMaximumJoinRejectionReasonBytes,
    ManifestField::SessionProtocolVersion,
    ManifestField::SessionMaximumMembers,
    ManifestField::SessionMaximumPeerIdBytes,
    ManifestField::SessionMaximumDisplayNameBytes,
    ManifestField::SessionMaximumRoomIdBytes,
    ManifestField::SessionMaximumCommandPayloadBytes,
    ManifestField::SessionMaximumBufferedCommandBytes,
    ManifestField::SessionMaximumBufferedCommands,
    ManifestField::SessionMaximumBufferedCommandBytesPerPeer,
    ManifestField::SessionMaximumBufferedCommandsPerPeer,
    ManifestField::SessionMaximumStateHashFrames,
    ManifestField::SessionMaximumSaveLoadHandshakes,
    ManifestField::SessionMaximumSnapshotIdBytes,
    ManifestField::SessionMaximumTick,
    ManifestField::SessionMaximumCommandLeadWindowTicks,
    ManifestField::SessionMaximumStateHashLagWindowTicks,
    ManifestField::SessionCommandSequenceReorderWindow,
    ManifestField::MatchSetupMagic,
    ManifestField::MatchSetupVersion,
    ManifestField::MatchMaximumMembers,
    ManifestField::MatchMaximumSlots,
    ManifestField::MatchMaximumPayloadBytes,
    ManifestField::MatchMaximumPeerIdBytes,
    ManifestField::MatchMaximumDisplayNameBytes,
    ManifestField::MatchMaximumCanonicalMapIdBytes,
    ManifestField::MatchMaximumGameModeBytes,
    ManifestField::MatchMaximumRulesOverrideBytes,
    ManifestField::MatchMaximumSlotOptionBytes,
    ManifestField::MatchMaximumFixedTickHz,
    ManifestField::MatchMaximumStartingCredits,
    ManifestField::MatchMaximumStartingUnits,
    ManifestField::MatchMaximumTechLevel,
    ManifestField::MatchMaximumGameSpeed,
    ManifestField::GameplayCommandMagic,
    ManifestField::GameplayCommandVersion,
    ManifestField::GameplayMaximumPayloadBytes,
    ManifestField::GameplayMaximumObjectIds,
    ManifestField::GameplayMaximumIdentifierBytes,
    ManifestField::GameplayMaximumSuperWeaponTargets,
    ManifestField::GameplayMaximumCellCoordinate,
    ManifestField::LockstepFrameMagic,
    ManifestField::LockstepFrameVersion,
    ManifestField::LockstepFrameHeaderBytes,
    ManifestField::LockstepMaximumCommandsPerFrame,
    ManifestField::LockstepMaximumFrameBytes,
    ManifestField::LockstepMaximumInputDelayTicks,
    ManifestField::SnapshotDescriptorMagic,
    ManifestField::SnapshotChunkMagic,
    ManifestField::SnapshotAckMagic,
    ManifestField::SnapshotTransferVersion,
    ManifestField::SnapshotSha256Bytes,
    ManifestField::SnapshotMaximumTokenBytes,
    ManifestField::SnapshotChunkBytes,
    ManifestField::SnapshotDefaultMaximumBytes,
    ManifestField::SnapshotHardMaximumBytes,
    ManifestField::SnapshotMaximumChunks,
    ManifestField::SnapshotMaximumTick
};

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
static_assert(
    runtimeMultiplayerContentDefaultMaximumComponentBytes
        <= runtimeMultiplayerContentHardMaximumComponentBytes
);

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

class CanonicalFnv64 {
public:
    void writeU8(std::uint8_t value) {
        hash_ ^= value;
        hash_ *= kFnvPrime;
    }

    void writeU16(std::uint16_t value) {
        writeU8(static_cast<std::uint8_t>(value >> 8U));
        writeU8(static_cast<std::uint8_t>(value));
    }

    void writeU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            writeU8(static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(shift)
            ));
        }
    }

    void writeBytes(std::span<const std::uint8_t> bytes) {
        for (const std::uint8_t byte : bytes) writeU8(byte);
    }

    void writeText(std::string_view text) {
        for (const char character : text) {
            writeU8(static_cast<std::uint8_t>(
                static_cast<unsigned char>(character)
            ));
        }
    }

    std::uint64_t finishNonzero() const {
        // FNV-1a is a full-width fingerprint and zero is theoretically
        // possible. Reserve zero for "not computed" at public boundaries.
        return hash_ == 0U ? 1U : hash_;
    }

private:
    std::uint64_t hash_ = kFnvOffsetBasis;
};

bool validMaximumComponentBytes(std::size_t maximumComponentBytes) {
    return maximumComponentBytes > 0U
        && maximumComponentBytes
            <= runtimeMultiplayerContentHardMaximumComponentBytes;
}

bool isTrimWhitespace(char character) {
    return character == ' ' || character == '\t'
        || character == '\r' || character == '\n';
}

bool canonicalizeResourceId(
    const std::string& input,
    std::string& canonical,
    std::string& errorMessage
) {
    std::size_t begin = 0U;
    std::size_t end = input.size();
    while (begin < end && isTrimWhitespace(input[begin])) ++begin;
    while (end > begin && isTrimWhitespace(input[end - 1U])) --end;
    if (begin == end) {
        return fail(errorMessage, "content resource id is empty");
    }
    if (end - begin
        > runtimeMultiplayerContentMaximumCanonicalResourceIdBytes) {
        return fail(errorMessage, "content resource id exceeds the limit");
    }

    std::string value;
    try {
        value.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            const unsigned char byte = static_cast<unsigned char>(
                input[index]
            );
            if (byte < 0x20U || byte == 0x7fU) {
                return fail(
                    errorMessage,
                    "content resource id contains a control character"
                );
            }
            char character = input[index];
            if (character == '\\') character = '/';
            if (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z')) {
                character = static_cast<char>(
                    byte - static_cast<unsigned char>('A')
                    + static_cast<unsigned char>('a')
                );
            }
            if (character == ':') {
                return fail(
                    errorMessage,
                    "content resource id may not contain a drive separator"
                );
            }
            value.push_back(character);
        }
    } catch (...) {
        return fail(errorMessage, "content resource id could not be copied");
    }

    if (value.empty() || value.front() == '/' || value.back() == '/') {
        return fail(errorMessage, "content resource id is absolute or empty");
    }
    std::size_t segmentBegin = 0U;
    while (segmentBegin < value.size()) {
        const std::size_t slash = value.find('/', segmentBegin);
        const std::size_t segmentEnd =
            slash == std::string::npos ? value.size() : slash;
        const std::size_t segmentLength = segmentEnd - segmentBegin;
        if (segmentLength == 0U) {
            return fail(
                errorMessage,
                "content resource id contains an empty path segment"
            );
        }
        if ((segmentLength == 1U && value[segmentBegin] == '.')
            || (segmentLength == 2U
                && value[segmentBegin] == '.'
                && value[segmentBegin + 1U] == '.')) {
            return fail(
                errorMessage,
                "content resource id contains a dot path segment"
            );
        }
        if (slash == std::string::npos) break;
        segmentBegin = slash + 1U;
    }

    canonical = std::move(value);
    return true;
}

bool canonicalizeComponentsInPlace(
    std::vector<RuntimeMultiplayerContentComponent>& components,
    std::string& errorMessage,
    std::size_t maximumComponentBytes
) {
    if (!validMaximumComponentBytes(maximumComponentBytes)) {
        return fail(
            errorMessage,
            "content configured component byte limit is invalid"
        );
    }
    if (components.empty()
        || components.size() > runtimeMultiplayerContentMaximumComponents) {
        return fail(errorMessage, "content component count is invalid");
    }
    for (RuntimeMultiplayerContentComponent& component : components) {
        if (component.bytes.size() > maximumComponentBytes
            || component.bytes.size()
                > runtimeMultiplayerContentHardMaximumComponentBytes) {
            return fail(errorMessage, "content component exceeds the byte limit");
        }
        std::string canonicalId;
        if (!canonicalizeResourceId(
                component.canonicalResourceId,
                canonicalId,
                errorMessage
            )) {
            return false;
        }
        component.canonicalResourceId = std::move(canonicalId);
    }
    std::sort(
        components.begin(),
        components.end(),
        [](const RuntimeMultiplayerContentComponent& left,
           const RuntimeMultiplayerContentComponent& right) {
            return left.canonicalResourceId < right.canonicalResourceId;
        }
    );
    for (std::size_t index = 1U; index < components.size(); ++index) {
        if (components[index - 1U].canonicalResourceId
            == components[index].canonicalResourceId) {
            return fail(
                errorMessage,
                "content component canonical resource id is duplicated"
            );
        }
    }
    return true;
}

} // namespace

bool runtimeMultiplayerCurrentProtocolManifest(
    std::vector<RuntimeMultiplayerProtocolManifestValue>& manifest,
    std::string& errorMessage
) {
    errorMessage.clear();
    try {
        std::vector<ManifestValue> candidate{
            {ManifestField::CompatibilityManifestVersion,
             runtimeMultiplayerCompatibilityManifestVersion},
            {ManifestField::ContentFingerprintVersion,
             runtimeMultiplayerContentFingerprintVersion},
            {ManifestField::ContentMaximumCanonicalResourceIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerContentMaximumCanonicalResourceIdBytes
             )},
            {ManifestField::ContentMaximumComponents,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerContentMaximumComponents
             )},
            {ManifestField::ContentDefaultMaximumComponentBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerContentDefaultMaximumComponentBytes
             )},
            {ManifestField::ContentHardMaximumComponentBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerContentHardMaximumComponentBytes
             )},
            {ManifestField::LanFramingVersion, runtimeLanProtocolVersion},
            {ManifestField::LanMaximumPacketBytes,
             static_cast<std::uint64_t>(runtimeLanMaximumPacketBytes)},
            {ManifestField::LanMaximumQueuedBytesPerPeer,
             static_cast<std::uint64_t>(
                 runtimeLanMaximumQueuedBytesPerPeer
             )},
            {ManifestField::MultiplayerMessageVersion,
             runtimeMultiplayerMessageVersion},
            {ManifestField::MultiplayerMaximumJoinRejectionReasonBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumJoinRejectionReasonBytes
             )},
            {ManifestField::SessionProtocolVersion,
             runtimeMultiplayerProtocolVersion},
            {ManifestField::SessionMaximumMembers,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumMembers
             )},
            {ManifestField::SessionMaximumPeerIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumPeerIdBytes
             )},
            {ManifestField::SessionMaximumDisplayNameBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumDisplayNameBytes
             )},
            {ManifestField::SessionMaximumRoomIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumRoomIdBytes
             )},
            {ManifestField::SessionMaximumCommandPayloadBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumCommandPayloadBytes
             )},
            {ManifestField::SessionMaximumBufferedCommandBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumBufferedCommandBytes
             )},
            {ManifestField::SessionMaximumBufferedCommands,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumBufferedCommands
             )},
            {ManifestField::SessionMaximumBufferedCommandBytesPerPeer,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumBufferedCommandBytesPerPeer
             )},
            {ManifestField::SessionMaximumBufferedCommandsPerPeer,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumBufferedCommandsPerPeer
             )},
            {ManifestField::SessionMaximumStateHashFrames,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumStateHashFrames
             )},
            {ManifestField::SessionMaximumSaveLoadHandshakes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumSaveLoadHandshakes
             )},
            {ManifestField::SessionMaximumSnapshotIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumSnapshotIdBytes
             )},
            {ManifestField::SessionMaximumTick,
             static_cast<std::uint64_t>(runtimeMultiplayerMaximumTick)},
            {ManifestField::SessionMaximumCommandLeadWindowTicks,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumCommandLeadWindowTicks
             )},
            {ManifestField::SessionMaximumStateHashLagWindowTicks,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMaximumStateHashLagWindowTicks
             )},
            {ManifestField::SessionCommandSequenceReorderWindow,
             runtimeMultiplayerCommandSequenceReorderWindow},
            {ManifestField::MatchSetupMagic,
             runtimeMultiplayerMatchSetupMagic},
            {ManifestField::MatchSetupVersion,
             runtimeMultiplayerMatchSetupVersion},
            {ManifestField::MatchMaximumMembers,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumMembers
             )},
            {ManifestField::MatchMaximumSlots,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumSlots
             )},
            {ManifestField::MatchMaximumPayloadBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumPayloadBytes
             )},
            {ManifestField::MatchMaximumPeerIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumPeerIdBytes
             )},
            {ManifestField::MatchMaximumDisplayNameBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumDisplayNameBytes
             )},
            {ManifestField::MatchMaximumCanonicalMapIdBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumCanonicalMapIdBytes
             )},
            {ManifestField::MatchMaximumGameModeBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumGameModeBytes
             )},
            {ManifestField::MatchMaximumRulesOverrideBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumRulesOverrideBytes
             )},
            {ManifestField::MatchMaximumSlotOptionBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumSlotOptionBytes
             )},
            {ManifestField::MatchMaximumFixedTickHz,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumFixedTickHz
             )},
            {ManifestField::MatchMaximumStartingCredits,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumStartingCredits
             )},
            {ManifestField::MatchMaximumStartingUnits,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumStartingUnits
             )},
            {ManifestField::MatchMaximumTechLevel,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumTechLevel
             )},
            {ManifestField::MatchMaximumGameSpeed,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerMatchMaximumGameSpeed
             )},
            {ManifestField::GameplayCommandMagic,
             runtimeMultiplayerGameplayCommandMagic},
            {ManifestField::GameplayCommandVersion,
             runtimeMultiplayerGameplayCommandVersion},
            {ManifestField::GameplayMaximumPayloadBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayMaximumPayloadBytes
             )},
            {ManifestField::GameplayMaximumObjectIds,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayMaximumObjectIds
             )},
            {ManifestField::GameplayMaximumIdentifierBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayMaximumIdentifierBytes
             )},
            {ManifestField::GameplayMaximumSuperWeaponTargets,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayMaximumSuperWeaponTargets
             )},
            {ManifestField::GameplayMaximumCellCoordinate,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayMaximumCellCoordinate
             )},
            {ManifestField::LockstepFrameMagic,
             runtimeMultiplayerGameplayFrameMagic},
            {ManifestField::LockstepFrameVersion,
             runtimeMultiplayerGameplayFrameVersion},
            {ManifestField::LockstepFrameHeaderBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerGameplayFrameHeaderBytes
             )},
            {ManifestField::LockstepMaximumCommandsPerFrame,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerLockstepMaximumCommandsPerFrame
             )},
            {ManifestField::LockstepMaximumFrameBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerLockstepMaximumFrameBytes
             )},
            {ManifestField::LockstepMaximumInputDelayTicks,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerLockstepMaximumInputDelayTicks
             )},
            {ManifestField::SnapshotDescriptorMagic,
             runtimeMultiplayerSnapshotDescriptorMagic},
            {ManifestField::SnapshotChunkMagic,
             runtimeMultiplayerSnapshotChunkMagic},
            {ManifestField::SnapshotAckMagic,
             runtimeMultiplayerSnapshotAckMagic},
            {ManifestField::SnapshotTransferVersion,
             runtimeMultiplayerSnapshotTransferVersion},
            {ManifestField::SnapshotSha256Bytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotSha256Bytes
             )},
            {ManifestField::SnapshotMaximumTokenBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotMaximumTokenBytes
             )},
            {ManifestField::SnapshotChunkBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotChunkBytes
             )},
            {ManifestField::SnapshotDefaultMaximumBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotDefaultMaximumBytes
             )},
            {ManifestField::SnapshotHardMaximumBytes,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotHardMaximumBytes
             )},
            {ManifestField::SnapshotMaximumChunks,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotMaximumChunks
             )},
            {ManifestField::SnapshotMaximumTick,
             static_cast<std::uint64_t>(
                 runtimeMultiplayerSnapshotMaximumTick
             )}
        };
        manifest = std::move(candidate);
    } catch (...) {
        return fail(errorMessage, "current protocol manifest could not be built");
    }
    errorMessage.clear();
    return true;
}

bool runtimeMultiplayerProtocolManifestFingerprint(
    std::span<const RuntimeMultiplayerProtocolManifestValue> manifest,
    std::uint64_t& fingerprint,
    std::string& errorMessage
) {
    errorMessage.clear();
    std::vector<ManifestValue> canonical;
    try {
        canonical.assign(manifest.begin(), manifest.end());
        std::sort(
            canonical.begin(),
            canonical.end(),
            [](const ManifestValue& left, const ManifestValue& right) {
                return static_cast<std::uint16_t>(left.field)
                    < static_cast<std::uint16_t>(right.field);
            }
        );
    } catch (...) {
        return fail(errorMessage, "protocol manifest could not be canonicalized");
    }
    if (canonical.size() != kExpectedManifestFields.size()) {
        return fail(errorMessage, "protocol manifest field count is invalid");
    }
    for (std::size_t index = 0U; index < canonical.size(); ++index) {
        if (canonical[index].field != kExpectedManifestFields[index]) {
            return fail(
                errorMessage,
                "protocol manifest has a missing, duplicate, or unknown field"
            );
        }
        if (canonical[index].value == 0U) {
            return fail(errorMessage, "protocol manifest value is zero");
        }
    }

    CanonicalFnv64 hash;
    hash.writeText("ConcordRTS/MultiplayerProtocolManifest");
    hash.writeU8(0U);
    hash.writeU16(runtimeMultiplayerCompatibilityManifestVersion);
    hash.writeU64(static_cast<std::uint64_t>(canonical.size()));
    for (const ManifestValue& value : canonical) {
        hash.writeU16(static_cast<std::uint16_t>(value.field));
        hash.writeU64(value.value);
    }
    fingerprint = hash.finishNonzero();
    errorMessage.clear();
    return true;
}

bool runtimeMultiplayerCurrentProtocolFingerprint(
    std::uint64_t& fingerprint,
    std::string& errorMessage
) {
    std::vector<ManifestValue> manifest;
    if (!runtimeMultiplayerCurrentProtocolManifest(
            manifest,
            errorMessage
        )) {
        return false;
    }
    return runtimeMultiplayerProtocolManifestFingerprint(
        manifest,
        fingerprint,
        errorMessage
    );
}

bool canonicalizeRuntimeMultiplayerContentComponents(
    std::vector<RuntimeMultiplayerContentComponent>& components,
    std::string& errorMessage,
    std::size_t maximumComponentBytes
) {
    errorMessage.clear();
    if (!validMaximumComponentBytes(maximumComponentBytes)) {
        return fail(
            errorMessage,
            "content configured component byte limit is invalid"
        );
    }
    std::vector<RuntimeMultiplayerContentComponent> candidate;
    try {
        candidate = components;
    } catch (...) {
        return fail(errorMessage, "content components could not be copied");
    }
    if (!canonicalizeComponentsInPlace(
            candidate,
            errorMessage,
            maximumComponentBytes
        )) {
        return false;
    }
    components = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool runtimeMultiplayerContentFingerprint(
    const std::vector<RuntimeMultiplayerContentComponent>& components,
    std::uint64_t& fingerprint,
    std::string& errorMessage,
    std::size_t maximumComponentBytes
) {
    errorMessage.clear();
    if (!validMaximumComponentBytes(maximumComponentBytes)) {
        return fail(
            errorMessage,
            "content configured component byte limit is invalid"
        );
    }
    std::vector<RuntimeMultiplayerContentComponent> canonical;
    try {
        canonical = components;
    } catch (...) {
        return fail(errorMessage, "content components could not be copied");
    }
    if (!canonicalizeComponentsInPlace(
            canonical,
            errorMessage,
            maximumComponentBytes
        )) {
        return false;
    }

    CanonicalFnv64 hash;
    hash.writeText("ConcordRTS/MultiplayerContentFingerprint");
    hash.writeU8(0U);
    hash.writeU16(runtimeMultiplayerContentFingerprintVersion);
    hash.writeU64(static_cast<std::uint64_t>(canonical.size()));
    for (const RuntimeMultiplayerContentComponent& component : canonical) {
        hash.writeU64(static_cast<std::uint64_t>(
            component.canonicalResourceId.size()
        ));
        hash.writeText(component.canonicalResourceId);
        hash.writeU64(static_cast<std::uint64_t>(component.bytes.size()));
        hash.writeBytes(component.bytes);
    }
    fingerprint = hash.finishNonzero();
    errorMessage.clear();
    return true;
}

bool loadRuntimeMultiplayerContentComponentFile(
    const std::filesystem::path& filePath,
    std::string canonicalResourceId,
    RuntimeMultiplayerContentComponent& component,
    std::string& errorMessage,
    std::size_t maximumComponentBytes
) {
    errorMessage.clear();
    if (!validMaximumComponentBytes(maximumComponentBytes)) {
        return fail(
            errorMessage,
            "content configured component byte limit is invalid"
        );
    }
    if (filePath.empty()) {
        return fail(errorMessage, "content file path is empty");
    }
    std::string canonicalId;
    if (!canonicalizeResourceId(
            canonicalResourceId,
            canonicalId,
            errorMessage
        )) {
        return false;
    }

    std::error_code fileError;
    if (!std::filesystem::is_regular_file(filePath, fileError)
        || fileError) {
        return fail(errorMessage, "content file is missing or not regular");
    }
    const std::uintmax_t fileBytes = std::filesystem::file_size(
        filePath,
        fileError
    );
    if (fileError) {
        return fail(errorMessage, "content file size could not be read");
    }
    if (fileBytes > static_cast<std::uintmax_t>(maximumComponentBytes)
        || fileBytes > static_cast<std::uintmax_t>(
            runtimeMultiplayerContentHardMaximumComponentBytes
        )
        || fileBytes > static_cast<std::uintmax_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        return fail(errorMessage, "content file exceeds the byte limit");
    }

    RuntimeMultiplayerContentComponent candidate;
    candidate.canonicalResourceId = std::move(canonicalId);
    try {
        candidate.bytes.resize(static_cast<std::size_t>(fileBytes));
    } catch (...) {
        return fail(errorMessage, "content file storage could not be allocated");
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return fail(errorMessage, "content file could not be opened");
    }
    if (!candidate.bytes.empty()) {
        const std::streamsize expected = static_cast<std::streamsize>(
            candidate.bytes.size()
        );
        input.read(
            reinterpret_cast<char*>(candidate.bytes.data()),
            expected
        );
        if (input.gcount() != expected) {
            return fail(errorMessage, "content file changed or was truncated");
        }
    }
    const std::ifstream::int_type extra = input.peek();
    if (input.bad()) {
        return fail(errorMessage, "content file could not be read completely");
    }
    if (extra != std::ifstream::traits_type::eof()) {
        return fail(errorMessage, "content file grew while it was read");
    }

    component = std::move(candidate);
    errorMessage.clear();
    return true;
}
