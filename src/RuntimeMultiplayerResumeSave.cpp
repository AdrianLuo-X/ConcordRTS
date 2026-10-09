#include "RuntimeMultiplayerResumeSave.h"
#include "IniFile.h"

#include <charconv>
#include <algorithm>
#include <set>
#include <sstream>
#include <string_view>
#include <fstream>

namespace {
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(bytes.size() * 2U);
    for (auto byte : bytes) { value.push_back(digits[byte >> 4U]); value.push_back(digits[byte & 15U]); }
    return value;
}

bool unhex(std::string_view text, std::vector<std::uint8_t>& bytes, std::size_t limit) {
    if (text.size() % 2U || text.size() > limit * 2U) return false;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    bytes.clear();bytes.reserve(text.size() / 2U);
    for (std::size_t i = 0; i < text.size(); i += 2U) {
        const int a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0) return false;
        bytes.push_back(static_cast<std::uint8_t>((a << 4) | b));
    }
    return true;
}

template<class T> bool number(const IniFile& ini, const char* section, const char* key, T& value) {
    const auto text = ini.getValue(section, key, "");
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool fail(std::string& error, const char* reason) { error = reason;return false; }
}

bool writeRuntimeMultiplayerResumeMetadata(const RuntimeMultiplayerMatchSetup& setup,
    const RuntimeMultiplayerCompatibility& compatibility, std::string& iniSection, std::string& error) {
    std::vector<std::uint8_t> encoded;
    if (compatibility.protocolVersion != runtimeMultiplayerProtocolVersion
        || compatibility.protocolFingerprint == 0 || compatibility.contentFingerprint == 0)
        return fail(error, "resume save compatibility is unavailable");
    if (!encodeRuntimeMultiplayerMatchSetup(setup, encoded, error)) return false;
    std::ostringstream out;
    out << "[MultiplayerResume]\nSchema=1\nSetupHex=" << hex(encoded)
        << "\nProtocolVersion=" << compatibility.protocolVersion
        << "\nProtocolFingerprint=" << compatibility.protocolFingerprint
        << "\nContentFingerprint=" << compatibility.contentFingerprint << "\n\n";
    iniSection = out.str();error.clear();return true;
}

bool readRuntimeMultiplayerResumeSave(std::span<const std::uint8_t> bytes,
    RuntimeMultiplayerResumeSave& save, std::string& error) {
    if (bytes.empty() || bytes.size() > runtimeMultiplayerSnapshotDefaultMaximumBytes)
        return fail(error, "multiplayer resume save exceeds its size limit");
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        return fail(error, "multiplayer resume save contains a binary NUL");
    IniFile ini;
    if (!ini.loadFromString(std::string(bytes.begin(), bytes.end())))
        return fail(error, "multiplayer resume save INI is invalid");
    for (const auto& name : ini.getSectionNames()) {
        std::set<std::string> keys;
        for (const auto& entry : *ini.getSection(name)) {
            auto key = entry.first;
            for (auto& c : key) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (key.empty() || !keys.insert(key).second)
                return fail(error, "multiplayer resume save contains a duplicate key");
        }
    }
    if (ini.getValue("RuntimeSave", "Version", "") != "15"
        || ini.getValue("MultiplayerSnapshot", "Schema", "") != "15"
        || ini.getValue("MultiplayerSnapshot", "Kind", "") != "Save")
        return fail(error, "resume room requires a complete multiplayer Save snapshot");
    if (ini.getValue("MultiplayerResume", "Schema", "") != "1")
        return fail(error, "this older multiplayer save has no room recipe; it can only be loaded inside its original session");
    RuntimeMultiplayerResumeSave candidate;
    std::vector<std::uint8_t> encoded;
    if (!unhex(ini.getValue("MultiplayerResume", "SetupHex", ""), encoded, runtimeMultiplayerMatchMaximumPayloadBytes)
        || !decodeRuntimeMultiplayerMatchSetup(encoded, candidate.setup, error))
        return fail(error, "resume save lobby recipe is invalid");
    if (!number(ini, "MultiplayerResume", "ProtocolVersion", candidate.compatibility.protocolVersion)
        || candidate.compatibility.protocolVersion != runtimeMultiplayerProtocolVersion
        || !number(ini, "MultiplayerResume", "ProtocolFingerprint", candidate.compatibility.protocolFingerprint)
        || !number(ini, "MultiplayerResume", "ContentFingerprint", candidate.compatibility.contentFingerprint)
        || candidate.compatibility.protocolFingerprint == 0 || candidate.compatibility.contentFingerprint == 0)
        return fail(error, "resume save compatibility is invalid");
    RuntimeMultiplayerSnapshotIdentity identity;
    int savedTick = -1, fixedTickHz = 0;
    std::uint64_t seed = 0, mapFingerprint = 0, setupFingerprint = 0;
    if (!number(ini, "RuntimeSave", "Tick", savedTick)
        || !number(ini, "MultiplayerSnapshot", "ProposalId", identity.proposalId)
        || !number(ini, "MultiplayerSnapshot", "SourceTimelineEpoch", identity.sourceTimelineEpoch)
        || !number(ini, "MultiplayerSnapshot", "BarrierTick", identity.barrierTick)
        || !number(ini, "MultiplayerSnapshot", "SnapshotTick", identity.snapshotTick)
        || !number(ini, "MultiplayerSnapshot", "GameplaySetupFingerprint", identity.gameplaySetupFingerprint)
        || !number(ini, "MultiplayerSnapshot", "AuthoritativeStateHash", identity.authoritativeStateHash)
        || !number(ini, "MultiplayerSnapshot", "FixedTickHz", fixedTickHz)
        || !number(ini, "MultiplayerSnapshot", "SimulationRandomSeed", seed)
        || !number(ini, "MultiplayerSnapshot", "MapContentFingerprint", mapFingerprint)
        || !unhex(ini.getValue("MultiplayerSnapshot", "SnapshotTokenHex", ""), encoded,
            runtimeMultiplayerSnapshotMaximumTokenBytes))
        return fail(error, "resume save snapshot envelope is invalid");
    identity.snapshotToken.assign(encoded.begin(), encoded.end());
    if (!unhex(ini.getValue("MultiplayerSnapshot", "CanonicalMapIdHex", ""), encoded,
            runtimeMultiplayerMatchMaximumCanonicalMapIdBytes))
        return fail(error, "resume save canonical map is invalid");
    std::string mapId(encoded.begin(), encoded.end());
    // The existing v15 world envelope writes case-insensitive resource ids
    // in upper case; MatchSetup's wire representation uses lower case.
    for (auto& c : mapId) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (!runtimeMultiplayerGameplaySetupFingerprint(candidate.setup, setupFingerprint, error)) return false;
    if (identity.snapshotTick != savedTick || identity.barrierTick != savedTick
        || savedTick >= runtimeMultiplayerSnapshotMaximumTick
        || identity.gameplaySetupFingerprint != setupFingerprint
        || candidate.setup.map.canonicalId != mapId || candidate.setup.map.contentFingerprint != mapFingerprint
        || candidate.setup.rules.randomSeed != seed || candidate.setup.rules.fixedTickHz != fixedTickHz) {
        std::ostringstream details;
        details << "resume save lobby recipe differs from its snapshot authority: tick=" << savedTick << '/' << identity.snapshotTick
            << '/' << identity.barrierTick << " setup=" << setupFingerprint << '/' << identity.gameplaySetupFingerprint
            << " map=" << candidate.setup.map.canonicalId << '/' << mapId
            << " mapContent=" << candidate.setup.map.contentFingerprint << '/' << mapFingerprint
            << " seed=" << candidate.setup.rules.randomSeed << '/' << seed << " Hz=" << candidate.setup.rules.fixedTickHz << '/' << fixedTickHz;
        error = details.str();return false;
    }
    if (!makeRuntimeMultiplayerSnapshotDescriptor(identity, bytes, candidate.descriptor, error)) return false;
    candidate.bytes.assign(bytes.begin(), bytes.end());
    save = std::move(candidate);error.clear();return true;
}

bool loadRuntimeMultiplayerResumeSave(const std::filesystem::path& path,
    RuntimeMultiplayerResumeSave& save, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return fail(error, "could not open multiplayer resume save");
    const auto count = stream.tellg();
    if (count <= 0 || count > static_cast<std::streamoff>(runtimeMultiplayerSnapshotDefaultMaximumBytes))
        return fail(error, "multiplayer resume save exceeds its size limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))
        || stream.peek() != std::char_traits<char>::eof()) return fail(error, "multiplayer resume save changed while reading");
    return readRuntimeMultiplayerResumeSave(bytes, save, error);
}
