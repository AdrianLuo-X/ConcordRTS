#pragma once
#include "RuntimeMultiplayerResumeSave.h"
#include "IniFile.h"
#include <sstream>

inline bool makeResumeSaveFixture(const RuntimeMultiplayerMatchSetup& setup,
    const RuntimeMultiplayerCompatibility& compatibility, RuntimeMultiplayerResumeSave& save,
    std::string& error, int tick = 900, bool multiChunk = true) {
    std::string metadata;
    std::uint64_t fingerprint = 0;
    if (!writeRuntimeMultiplayerResumeMetadata(setup, compatibility, metadata, error)
        || !runtimeMultiplayerGameplaySetupFingerprint(setup, fingerprint, error)) return false;
    std::ostringstream out;
    out << "[RuntimeSave]\nVersion=15\nTick=" << tick
        << "\n[MultiplayerSnapshot]\nSchema=15\nKind=Save\nProposalId=7\nSourceTimelineEpoch=4\nBarrierTick=" << tick
        << "\nSnapshotTick=" << tick << "\nGameplaySetupFingerprint=" << fingerprint
        << "\nAuthoritativeStateHash=123456789\nFixedTickHz=" << setup.rules.fixedTickHz
        << "\nSimulationRandomSeed=" << setup.rules.randomSeed << "\nCanonicalMapIdHex=" << iniHexEncode(setup.map.canonicalId)
        << "\nMapContentFingerprint=" << setup.map.contentFingerprint << "\nSnapshotTokenHex=" << iniHexEncode("resume-fixture-7")
        << "\n\n" << metadata;
    if (multiChunk) out << "[FixturePadding]\nBytes=" << std::string(runtimeMultiplayerSnapshotChunkBytes * 2 + 123, 'x') << '\n';
    const auto text = out.str();
    return readRuntimeMultiplayerResumeSave(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), save, error);
}
