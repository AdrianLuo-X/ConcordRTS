#include "RuntimeMultiplayerMapTransfer.h"
#include "RuntimeMultiplayerCompatibilityFingerprint.h"

namespace {
bool fail(std::string& error, const char* reason) { error = reason; return false; }
RuntimeMultiplayerSnapshotIdentity mapIdentity(const RuntimeMultiplayerMapRequest& request) {
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = request.requestId;
    identity.kind = RuntimeMultiplayerSnapshotKind::Save;
    identity.sourceTimelineEpoch = 1;
    identity.barrierTick = 0;
    identity.snapshotTick = 0;
    identity.snapshotToken = "map-" + std::to_string(request.requestId);
    identity.authoritativeStateHash = request.map.contentFingerprint;
    identity.gameplaySetupFingerprint = request.map.contentFingerprint;
    return identity;
}
}

bool validateRuntimeMultiplayerMapRequest(const RuntimeMultiplayerMapRequest& request, std::string& error) {
    if (!request.requestId || !request.map.contentFingerprint
        || request.map.canonicalId.size() > runtimeMultiplayerMatchMaximumCanonicalMapIdBytes
        || !request.map.canonicalId.starts_with("multimd/"))
        return fail(error, "invalid map transfer request identity");
    std::vector<RuntimeMultiplayerContentComponent> probe{{request.map.canonicalId, {}}};
    if (!canonicalizeRuntimeMultiplayerContentComponents(probe, error)
        || probe.front().canonicalResourceId != request.map.canonicalId)
        return fail(error, "unsafe or noncanonical map transfer resource id");
    error.clear(); return true;
}

bool validateRuntimeMultiplayerMapOffer(const RuntimeMultiplayerMapOffer& offer, std::string& error) {
    if (!validateRuntimeMultiplayerMapRequest(offer.request, error)
        || !validateRuntimeMultiplayerSnapshotDescriptor(offer.transfer, error)) return false;
    if (offer.transfer.identity != mapIdentity(offer.request))
        return fail(error, "map transfer descriptor does not match its request");
    error.clear(); return true;
}

bool validateRuntimeMultiplayerMapBytes(const RuntimeMultiplayerMatchMap& map,
    std::span<const std::uint8_t> bytes, std::string& error) {
    if (!validateRuntimeMultiplayerMapRequest({1, map}, error)) return false;
    if (bytes.empty() || bytes.size() > runtimeMultiplayerSnapshotDefaultMaximumBytes)
        return fail(error, "map file must contain between 1 byte and 64 MiB");
    RuntimeMultiplayerContentComponent component{map.canonicalId, {bytes.begin(), bytes.end()}};
    std::uint64_t fingerprint = 0;
    if (!runtimeMultiplayerContentFingerprint({component}, fingerprint, error)) return false;
    if (fingerprint != map.contentFingerprint) return fail(error, "received map content does not match the room fingerprint");
    error.clear(); return true;
}

bool makeRuntimeMultiplayerMapOffer(const RuntimeMultiplayerMapRequest& request,
    std::span<const std::uint8_t> bytes, RuntimeMultiplayerMapOffer& offer, std::string& error) {
    if (!validateRuntimeMultiplayerMapRequest(request, error)
        || !validateRuntimeMultiplayerMapBytes(request.map, bytes, error)) return false;
    RuntimeMultiplayerMapOffer candidate;
    candidate.request = request;
    if (!makeRuntimeMultiplayerSnapshotDescriptor(mapIdentity(request), bytes, candidate.transfer, error)) return false;
    offer = std::move(candidate); error.clear(); return true;
}
