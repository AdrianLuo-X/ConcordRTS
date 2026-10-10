#include "network/snapshots/RuntimeMultiplayerResumeSave.h"
#include "core/io/IniFile.h"
#include <iostream>
#include <sstream>

namespace {
RuntimeMultiplayerMatchSetup setup() {
    RuntimeMultiplayerMatchSetup value;
    value.revision = 8;
    value.hostPeerId = "saved-host";
    value.map = {"multimd/dryheat.map", 1234567};
    value.rules.gameMode = "battle";
    value.rules.rulesOverride = "mpbattle.ini";
    value.rules.randomSeed = 987654321;
    value.members = {
        {"saved-host", "Original host", RuntimeMultiplayerMatchMemberIdentity::Combatant},
        {"saved-client", "Original client", RuntimeMultiplayerMatchMemberIdentity::Combatant}
    };
    value.slots = {
        {0, RuntimeMultiplayerMatchSlotController::Human, "saved-host", "americans", "gold", "a", "normal", 1},
        {1, RuntimeMultiplayerMatchSlotController::Human, "saved-client", "russians", "red", "b", "normal", 2}
    };
    std::string error;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(value, error)) std::cerr << error << '\n';
    return value;
}

std::string snapshot(const RuntimeMultiplayerMatchSetup& worldSetup, std::string metadata) {
    std::string error;
    std::uint64_t fingerprint = 0;
    if (!runtimeMultiplayerGameplaySetupFingerprint(worldSetup, fingerprint, error)) return {};
    std::ostringstream out;
    out << "[RuntimeSave]\nVersion=15\nTick=900\n\n[MultiplayerSnapshot]\nSchema=15\nKind=Save"
        << "\nProposalId=7\nSourceTimelineEpoch=4\nBarrierTick=900\nSnapshotTick=900"
        << "\nGameplaySetupFingerprint=" << fingerprint << "\nAuthoritativeStateHash=123456789"
        << "\nFixedTickHz=" << worldSetup.rules.fixedTickHz
        << "\nSimulationRandomSeed=" << worldSetup.rules.randomSeed
        << "\nCanonicalMapIdHex=" << iniHexEncode(worldSetup.map.canonicalId)
        << "\nMapContentFingerprint=" << worldSetup.map.contentFingerprint
        << "\nSnapshotTokenHex=" << iniHexEncode("saved-match-7") << "\n\n" << metadata;
    return out.str();
}

bool read(const std::string& text, RuntimeMultiplayerResumeSave& value, std::string& error) {
    return readRuntimeMultiplayerResumeSave(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), value, error);
}
}

int main() {
    const auto original = setup();
    RuntimeMultiplayerCompatibility compatibility;
    compatibility.protocolFingerprint = 12345;
    compatibility.contentFingerprint = 67890;
    std::string metadata, error;
    if (!writeRuntimeMultiplayerResumeMetadata(original, compatibility, metadata, error)) {
        std::cerr << error << '\n';return 1;
    }
    RuntimeMultiplayerResumeSave restored;
    const auto saved = snapshot(original, metadata);
    if (!read(saved, restored, error) || restored.setup != original || restored.compatibility != compatibility
        || restored.descriptor.identity.snapshotTick != 900 || restored.descriptor.identity.sourceTimelineEpoch != 4
        || restored.bytes != std::vector<std::uint8_t>(saved.begin(), saved.end())) {
        std::cerr << "resume recipe roundtrip: " << error << '\n';return 1;
    }
    const auto validBytes = restored.bytes;
    auto changed = original;
    changed.rules.randomSeed++;
    std::string wrongMetadata;
    if (!writeRuntimeMultiplayerResumeMetadata(changed, compatibility, wrongMetadata, error)) return 1;
    for (const auto& bad : {snapshot(original, ""), snapshot(original, wrongMetadata), saved + "Schema=1\n",
            saved + "\n[MultiplayerSnapshot]\nKind=Load\n", saved + std::string(1, '\0')}) {
        if (read(bad, restored, error) || restored.bytes != validBytes) {
            std::cerr << "invalid resume save accepted or replaced output\n";return 1;
        }
    }
    std::cout << "Multiplayer resume save smoke passed: complete original slots, map, seed, saved tick, envelope binding and malformed input rejection\n";
    return 0;
}
