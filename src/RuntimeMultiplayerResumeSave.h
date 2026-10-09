#pragma once

#include "RuntimeMultiplayerSessionSystem.h"
#include "RuntimeMultiplayerSnapshotTransfer.h"

#include <span>
#include <filesystem>
#include <string>
#include <vector>

// A portable Save plus its original immutable lobby recipe. The recipe has
// no process-local paths or credentials. A new room may rebind human peer
// identities, but must preserve every gameplay slot and rule.
struct RuntimeMultiplayerResumeSave {
    RuntimeMultiplayerMatchSetup setup;
    RuntimeMultiplayerCompatibility compatibility;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    std::vector<std::uint8_t> bytes;
};

bool writeRuntimeMultiplayerResumeMetadata(const RuntimeMultiplayerMatchSetup& setup,
    const RuntimeMultiplayerCompatibility& compatibility, std::string& iniSection, std::string& error);

// Validates the Save envelope and its complete setup before exposing a room
// recipe. Full world materialization and its authority hash are additionally
// verified by RuntimeShell before a resumed world is attached.
bool readRuntimeMultiplayerResumeSave(std::span<const std::uint8_t> bytes,
    RuntimeMultiplayerResumeSave& save, std::string& error);
bool loadRuntimeMultiplayerResumeSave(const std::filesystem::path& path,
    RuntimeMultiplayerResumeSave& save, std::string& error);
