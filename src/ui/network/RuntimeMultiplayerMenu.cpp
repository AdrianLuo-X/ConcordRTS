#include "ui/network/RuntimeMultiplayerMenu.h"
#include "network/maps/RuntimeMultiplayerMapCache.h"

#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"
#include "network/lobby/RuntimeRoomPassword.h"
#include "data/campaign/CooperativeScenarioDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "data/maps/RuntimeRandomMap.h"
#if !defined(RUNTIME_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
#include "ui/network/RuntimeInternetMultiplayerMenu.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

namespace {

namespace fs = std::filesystem;

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

class RoomPasswordClearGuard {
public:
    explicit RoomPasswordClearGuard(std::string& value) : value_(value) {}
    ~RoomPasswordClearGuard() { runtimeRoomPasswordSecureClear(value_); }
    RoomPasswordClearGuard(const RoomPasswordClearGuard&) = delete;
    RoomPasswordClearGuard& operator=(const RoomPasswordClearGuard&) = delete;

private:
    std::string& value_;
};

bool asciiWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string trimAscii(std::string value) {
    std::size_t first = 0U;
    while (first < value.size() && asciiWhitespace(value[first])) ++first;
    std::size_t last = value.size();
    while (last > first && asciiWhitespace(value[last - 1U])) --last;
    return value.substr(first, last - first);
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

bool validProtocolIdentifier(
    std::string_view value,
    std::size_t maximumBytes
) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        const bool alphaNumeric =
            (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
        return alphaNumeric
            || byte == static_cast<unsigned char>('-')
            || byte == static_cast<unsigned char>('_')
            || byte == static_cast<unsigned char>('.');
    });
}

bool normalizeDisplayName(
    std::string& value,
    std::size_t maximumBytes,
    std::string& errorMessage
) {
    value = trimAscii(std::move(value));
    if (value.empty() || value.size() > maximumBytes
        || !std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char byte) {
                return byte >= 0x20U && byte != 0x7fU;
            }
        )) {
        return fail(
            errorMessage,
            "multiplayer player name is empty, too long, or contains controls"
        );
    }
    return true;
}

bool regularFile(const fs::path& path) {
    std::error_code errorCode;
    return fs::exists(path, errorCode)
        && fs::is_regular_file(path, errorCode);
}

fs::path firstExisting(const std::vector<fs::path>& candidates) {
    for (const fs::path& candidate : candidates) {
        if (regularFile(candidate)) return candidate;
    }
    return {};
}

std::vector<fs::path> multiplayerIniRoots(const RuntimeConfig& config) {
    const fs::path localRoot(config.localDataRootPath);
    const fs::path exportRoot =
        localRoot / "resources" / "mix_exports";
    const fs::path resourceRoot(config.resourceRootPath);
    return {
        localRoot,
        exportRoot / "expandmd01",
        exportRoot / "ra2md" / "localmd",
        exportRoot / "ra2md",
        resourceRoot / "GameMD",
        resourceRoot / "Game",
        resourceRoot
    };
}

fs::path resolveNamedIni(
    const RuntimeConfig& config,
    const std::string& filename
) {
    std::vector<fs::path> candidates;
    for (const fs::path& root : multiplayerIniRoots(config)) {
        candidates.push_back(root / filename);
    }
    return firstExisting(candidates);
}

bool safeIniFilename(std::string_view value) {
    if (value.empty()) return false;
    const fs::path path{std::string(value)};
    if (path.has_root_path() || path.has_parent_path()
        || path.filename().string() != value) {
        return false;
    }
    const std::string lowered = lowerAscii(path.extension().string());
    return lowered == ".ini"
        && validProtocolIdentifier(value, 128U);
}

std::vector<std::string> splitComma(std::string_view value) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string_view::npos
            ? value.size()
            : comma;
        fields.push_back(trimAscii(std::string(value.substr(start, end - start))));
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    return fields;
}

bool appendContentFile(
    const fs::path& path,
    std::string canonicalId,
    std::vector<RuntimeMultiplayerContentComponent>& components,
    std::string& errorMessage
) {
    RuntimeMultiplayerContentComponent component;
    if (!loadRuntimeMultiplayerContentComponentFile(
            path,
            std::move(canonicalId),
            component,
            errorMessage
        )) {
        return false;
    }
    components.push_back(std::move(component));
    return true;
}

bool readModeOverrideNames(
    const RuntimeMultiplayerContentComponent& modes,
    std::vector<std::string>& overrideNames,
    std::string& errorMessage
) {
    std::string text;
    text.reserve(modes.bytes.size());
    for (std::uint8_t byte : modes.bytes) {
        text.push_back(static_cast<char>(byte));
    }

    std::set<std::string> uniqueNames;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t comment = line.find(';');
        if (comment != std::string::npos) line.resize(comment);
        line = trimAscii(std::move(line));
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::vector<std::string> fields = splitComma(
            std::string_view(line).substr(equals + 1U)
        );
        if (fields.size() < 3U || fields[2].empty()) continue;
        if (!safeIniFilename(fields[2])) {
            return fail(
                errorMessage,
                "MPModesMD.ini contains an unsafe rules override name: "
                    + fields[2]
            );
        }
        uniqueNames.insert(lowerAscii(fields[2]));
    }
    if (uniqueNames.empty()) {
        return fail(
            errorMessage,
            "MPModesMD.ini does not declare any game-mode rules overrides"
        );
    }
    overrideNames.assign(uniqueNames.begin(), uniqueNames.end());
    errorMessage.clear();
    return true;
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

bool fillSecureRandom(
    std::span<std::uint8_t> bytes,
    std::string& errorMessage
) {
    if (bytes.empty()) return fail(errorMessage, "secure random request is empty");
#if defined(_WIN32)
    if (bytes.size() > static_cast<std::size_t>(
            std::numeric_limits<ULONG>::max())) {
        return fail(errorMessage, "secure random request is too large");
    }
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    if (status < 0) {
        return fail(errorMessage, "Windows secure random generation failed");
    }
#else
    std::random_device random;
    for (std::uint8_t& byte : bytes) {
        byte = static_cast<std::uint8_t>(random() & 0xffU);
    }
#endif
    errorMessage.clear();
    return true;
}

bool secureRandomU64(std::uint64_t& value, std::string& errorMessage) {
    std::array<std::uint8_t, 8> bytes{};
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!fillSecureRandom(bytes, errorMessage)) return false;
        std::uint64_t candidate = 0U;
        for (std::uint8_t byte : bytes) {
            candidate = (candidate << 8U) | static_cast<std::uint64_t>(byte);
        }
        if (candidate != 0U) {
            value = candidate;
            errorMessage.clear();
            return true;
        }
    }
    return fail(errorMessage, "secure random generator returned only zero");
}

bool validMapExtension(const fs::path& path) {
    const std::string extension = lowerAscii(path.extension().string());
    return extension == ".map" || extension == ".mpr"
        || extension == ".yrm" || extension == ".mmx"
        || extension == ".yro";
}

bool mapFromLocalFile(
    const fs::path& localPath,
    RuntimeMultiplayerMatchMap& map,
    std::string& errorMessage
) {
    if (!regularFile(localPath) || !validMapExtension(localPath)) {
        return fail(
            errorMessage,
            "selected multiplayer map is missing or has an unsupported extension"
        );
    }
    const std::string filename = lowerAscii(localPath.filename().string());
    if (!validProtocolIdentifier(filename, 192U)) {
        return fail(errorMessage, "selected multiplayer map filename is unsafe");
    }

    RuntimeMultiplayerContentComponent component;
    RuntimeMultiplayerMatchMap candidate;
    candidate.canonicalId = "multimd/" + filename;
    if (!loadRuntimeMultiplayerContentComponentFile(
            localPath,
            candidate.canonicalId,
            component,
            errorMessage
        )
        || !runtimeMultiplayerContentFingerprint(
            {component},
            candidate.contentFingerprint,
            errorMessage
        )) {
        return false;
    }
    map = std::move(candidate);
    errorMessage.clear();
    return true;
}

RuntimeMultiplayerMatchSlot closedSlot(std::size_t index) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = static_cast<std::uint8_t>(index);
    slot.controller = RuntimeMultiplayerMatchSlotController::Closed;
    return slot;
}

RuntimeMultiplayerMatchSlot activeSelectionSlot(
    std::size_t index,
    RuntimeMultiplayerMatchSlotController controller,
    std::string peerId,
    const RuntimeSkirmishPlayerSlot& source,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = static_cast<std::uint8_t>(index);
    slot.controller = controller;
    slot.peerId = std::move(peerId);
    slot.countryId = source.country.empty() ? "Random" : source.country;
    slot.colorId = source.color.empty() ? "Random" : source.color;
    slot.teamId = source.team.empty() ? "-" : source.team;
    slot.difficultyId = source.difficulty.empty()
        ? "Medium"
        : source.difficulty;
    if (source.startPosition < 0 || source.startPosition > 8) {
        errorMessage = "skirmish start position is outside 0..8";
        return {};
    }
    slot.startPosition = static_cast<std::uint8_t>(source.startPosition);
    return slot;
}

} // namespace

bool parseRuntimeMultiplayerDirectEndpoint(
    std::string_view text,
    RuntimeMultiplayerDirectEndpoint& endpoint,
    std::string& errorMessage
) {
    const std::string input = trimAscii(std::string(text));
    std::string host;
    std::string portText;
    if (!input.empty() && input.front() == '[') {
        const std::size_t close = input.find(']');
        if (close == std::string::npos || close + 1U >= input.size()
            || input[close + 1U] != ':') {
            return fail(
                errorMessage,
                "IPv6 direct address must use [address]:port"
            );
        }
        host = input.substr(1U, close - 1U);
        portText = input.substr(close + 2U);
    } else {
        const std::size_t colon = input.rfind(':');
        if (colon == std::string::npos
            || input.find(':') != colon) {
            return fail(
                errorMessage,
                "direct address must use host:port"
            );
        }
        host = input.substr(0U, colon);
        portText = input.substr(colon + 1U);
    }
    host = trimAscii(std::move(host));
    portText = trimAscii(std::move(portText));
    if (host.empty()
        || host.find_first_of("/\\\r\n\t ") != std::string::npos) {
        return fail(errorMessage, "direct host is empty or malformed");
    }
    if (portText.empty()
        || !std::all_of(portText.begin(), portText.end(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        return fail(errorMessage, "direct port is not a decimal number");
    }
    std::uint32_t port = 0U;
    for (char character : portText) {
        port = port * 10U + static_cast<std::uint32_t>(character - '0');
        if (port > 65535U) break;
    }
    if (port == 0U || port > 65535U) {
        return fail(errorMessage, "direct port must be in the range 1..65535");
    }

    RuntimeMultiplayerDirectEndpoint candidate;
    candidate.host = std::move(host);
    candidate.port = static_cast<std::uint16_t>(port);
    endpoint = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool generateRuntimeMultiplayerSecureId(
    std::string_view prefix,
    std::string& identifier,
    std::string& errorMessage
) {
    if (!validProtocolIdentifier(prefix, 31U)
        || prefix.size() + 32U > runtimeMultiplayerMaximumPeerIdBytes) {
        return fail(errorMessage, "secure identifier prefix is invalid");
    }
    std::array<std::uint8_t, 16> bytes{};
    if (!fillSecureRandom(bytes, errorMessage)) return false;

    constexpr char digits[] = "0123456789abcdef";
    std::string candidate(prefix);
    candidate.reserve(prefix.size() + bytes.size() * 2U);
    for (std::uint8_t byte : bytes) {
        candidate.push_back(digits[(byte >> 4U) & 0x0fU]);
        candidate.push_back(digits[byte & 0x0fU]);
    }
    identifier = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool makeRuntimeMultiplayerMenuCompatibility(
    const RuntimeConfig& config,
    RuntimeMultiplayerCompatibility& compatibility,
    std::string& errorMessage
) {
    const fs::path rulesPath = resolveNamedIni(config, "rulesmd.ini");
    const fs::path artPath = resolveNamedIni(config, "artmd.ini");
    const fs::path modesPath = resolveNamedIni(config, "mpmodesmd.ini");
    if (rulesPath.empty()) {
        return fail(
            errorMessage,
            "multiplayer compatibility requires a local rulesmd.ini"
        );
    }
    if (artPath.empty()) {
        return fail(
            errorMessage,
            "multiplayer compatibility requires a local artmd.ini"
        );
    }
    if (modesPath.empty()) {
        return fail(
            errorMessage,
            "multiplayer compatibility requires a local mpmodesmd.ini"
        );
    }

    std::vector<RuntimeMultiplayerContentComponent> components;
    if (!appendContentFile(
            rulesPath,
            "rules/rulesmd.ini",
            components,
            errorMessage
        )
        || !appendContentFile(
            artPath,
            "art/artmd.ini",
            components,
            errorMessage
        )
        || !appendContentFile(
            modesPath,
            "rules/mpmodesmd.ini",
            components,
            errorMessage
        )) {
        return false;
    }

    std::vector<std::string> overrideNames;
    if (!readModeOverrideNames(
            components.back(),
            overrideNames,
            errorMessage
        )) {
        return false;
    }
    for (const std::string& overrideName : overrideNames) {
        const fs::path overridePath = resolveNamedIni(config, overrideName);
        if (overridePath.empty()) {
            return fail(
                errorMessage,
                "multiplayer compatibility is missing game-mode rules file: "
                    + overrideName
            );
        }
        if (!appendContentFile(
                overridePath,
                "rules/modes/" + overrideName,
                components,
                errorMessage
            )) {
            return false;
        }
    }

    RuntimeMultiplayerCompatibility candidate;
    const fs::path cooperativePath = resolveNamedIni(config, "coopcampmd.ini");
    if (!cooperativePath.empty() && !appendContentFile(cooperativePath,
            "rules/coopcampmd.ini", components, errorMessage)) return false;
    if (!runtimeMultiplayerCurrentProtocolFingerprint(
            candidate.protocolFingerprint,
            errorMessage
        )
        || !runtimeMultiplayerContentFingerprint(
            components,
            candidate.contentFingerprint,
            errorMessage
        )) {
        return false;
    }
    compatibility = candidate;
    errorMessage.clear();
    return true;
}

bool projectRuntimeMultiplayerHostSelection(
    const RuntimeConfig& baseConfig,
    const RuntimeSkirmishSelection& selection,
    std::string playerName,
    std::string peerId,
    std::uint64_t randomSeed,
    RuntimeMultiplayerHostSelectionProjection& projection,
    std::string& errorMessage
) {
    if (!selection.accepted) {
        return fail(errorMessage, "skirmish host selection was not accepted");
    }
    if (!normalizeDisplayName(playerName, 64U, errorMessage)) return false;
    if (!validProtocolIdentifier(
            peerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return fail(errorMessage, "multiplayer peer id is invalid");
    }
    if (randomSeed == 0U) {
        return fail(errorMessage, "multiplayer random seed must be nonzero");
    }
    if (selection.slots.size() > runtimeMultiplayerMatchMaximumSlots) {
        return fail(errorMessage, "skirmish selection has more than eight slots");
    }

    RuntimeMultiplayerLobbyHostCreateOptions lobby;
    if (!mapFromLocalFile(selection.mapPath, lobby.map, errorMessage)) {
        return false;
    }
    lobby.rules.gameMode = selection.gameMode;
    lobby.rules.rulesOverride = selection.gameModeRulesOverride;
    lobby.rules.randomSeed = randomSeed;
    lobby.rules.fixedTickHz = baseConfig.fixedTickHz;
    lobby.rules.startingCredits = selection.startingCredits;
    lobby.rules.startingUnits = selection.unitCount;
    lobby.rules.techLevel = baseConfig.techLevel;
    lobby.rules.gameSpeed = selection.gameSpeed;
    lobby.rules.shortGame = selection.shortGame;
    lobby.rules.mcvRepacks = selection.mcvRepacks;
    lobby.rules.crates = selection.crates;
    lobby.rules.superweapons = selection.superweapons;
    lobby.rules.buildOffAlly = selection.buildOffAlly;
    const bool cooperative = lowerAscii(trimAscii(selection.gameMode)) == "cooperative";
    if (cooperative) {
        const auto scenarioPath = resolveNamedIni(baseConfig, "coopcampmd.ini");
        IniFile scenarioIni, mapIni;
        CooperativeScenarioDatabase scenarios;
        MapDatabase map;
        if (scenarioPath.empty() || !scenarioIni.loadFromFile(scenarioPath.generic_string())
            || !scenarios.loadFromIni(scenarioIni) || !mapIni.loadFromFile(selection.mapPath)
            || !map.loadFromIni(mapIni))
            return fail(errorMessage, "cooperative mission data is unavailable");
        const auto* scenario = scenarios.findByMapId(fs::path(selection.mapPath).stem().string());
        if (!scenario || !scenarios.validateMap(*scenario, map, mapIni).valid)
            return fail(errorMessage, "selected cooperative mission is invalid");
        auto& mission = lobby.rules.cooperativeMission;
        mission.mapId = scenario->mapId;
        mission.scenarioFingerprint = scenario->fingerprint;
        mission.playerCountries = scenario->playerCountries;
        mission.enemyCountries = scenario->enemyCountries;
    }
    lobby.hostMember.peerId = peerId;
    lobby.hostMember.displayName = playerName;
    lobby.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    lobby.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    lobby.slotCount = std::max<std::size_t>(2U, selection.slots.size());
    if (cooperative) lobby.slotCount = 4;

    std::size_t reservedClosed = lobby.slotCount;
    for (std::size_t index = 1U; index < lobby.slotCount; ++index) {
        if (index >= selection.slots.size()
            || !selection.slots[index].enabled) {
            reservedClosed = index;
            break;
        }
    }
    if (reservedClosed == lobby.slotCount) {
        reservedClosed = lobby.slotCount - 1U;
    }
    if (cooperative) reservedClosed = 1;
    for (std::size_t index = 1U; index < lobby.slotCount; ++index) {
        if (index == reservedClosed || index >= selection.slots.size()
            || !selection.slots[index].enabled) {
            continue;
        }
        lobby.computerSlotIndices.push_back(index);
    }

    RuntimeMultiplayerLobbyModel model;
    if (!RuntimeMultiplayerLobbyModel::createHost(
            lobby,
            model,
            errorMessage
        )) {
        return false;
    }

    RuntimeSkirmishPlayerSlot hostSource;
    if (!selection.slots.empty()) hostSource = selection.slots.front();
    if (hostSource.country.empty()) hostSource.country = selection.playerCountry;
    if (hostSource.color.empty()) hostSource.color = selection.playerColor;
    RuntimeMultiplayerMatchSlot hostSlot = activeSelectionSlot(
        0U,
        RuntimeMultiplayerMatchSlotController::Human,
        peerId,
        hostSource,
        errorMessage
    );
    if (!errorMessage.empty()) return false;
    RuntimeMultiplayerLobbyEditResult edit = model.setSlot(0U, hostSlot);
    if (!edit.accepted) return fail(errorMessage, edit.reason);

    for (std::size_t index = 1U; index < lobby.slotCount; ++index) {
        RuntimeMultiplayerMatchSlot slot = closedSlot(index);
        const bool computer = std::find(
            lobby.computerSlotIndices.begin(),
            lobby.computerSlotIndices.end(),
            index
        ) != lobby.computerSlotIndices.end();
        if (computer) {
            slot = activeSelectionSlot(
                index,
                RuntimeMultiplayerMatchSlotController::Computer,
                {},
                selection.slots[index],
                errorMessage
            );
            if (!errorMessage.empty()) return false;
        }
        edit = model.setSlot(index, slot);
        if (!edit.accepted) return fail(errorMessage, edit.reason);
    }

    RuntimeMultiplayerHostSelectionProjection candidate;
    candidate.lobby = std::move(lobby);
    candidate.lobby.map = model.setup().map;
    candidate.lobby.rules = model.setup().rules;
    candidate.slots = model.setup().slots;
    candidate.localMapPath = fs::path(selection.mapPath);
    const bool hasClosed = std::any_of(
        candidate.slots.begin() + 1,
        candidate.slots.end(),
        [](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                == RuntimeMultiplayerMatchSlotController::Closed;
        }
    );
    if (!hasClosed) {
        return fail(errorMessage, "host selection did not reserve a remote slot");
    }
    projection = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool resolveRuntimeMultiplayerMenuMap(
    const RuntimeConfig& config,
    const RuntimeMultiplayerMatchMap& map,
    fs::path& localMapPath,
    std::string& errorMessage
) {
    if (map.contentFingerprint == 0U) {
        return fail(errorMessage, "multiplayer map fingerprint is zero");
    }
    RuntimeMultiplayerContentComponent idProbe;
    idProbe.canonicalResourceId = map.canonicalId;
    std::vector<RuntimeMultiplayerContentComponent> probe{idProbe};
    if (!canonicalizeRuntimeMultiplayerContentComponents(
            probe,
            errorMessage
        )
        || probe.front().canonicalResourceId != map.canonicalId
        || !map.canonicalId.starts_with("multimd/")) {
        return fail(errorMessage, "multiplayer canonical map id is unsafe");
    }

    const fs::path relative(map.canonicalId);
    const fs::path filename = relative.filename();
    const fs::path localRoot(config.localDataRootPath);
    const fs::path exportRoot =
        localRoot / "resources" / "mix_exports";
    const fs::path resourceRoot(config.resourceRootPath);
    std::vector<fs::path> candidates{
        exportRoot / relative,
        exportRoot / "multimd" / filename,
        runtimeSavedRandomMapDirectory(config) / filename,
        runtimeRandomMapCacheDirectory(config) / filename,
        localRoot / relative,
        resourceRoot / "GameMD" / filename,
        resourceRoot / "Game" / filename,
        resourceRoot / filename
    };
    const fs::path configured(config.defaultMapPath);
    if (lowerAscii(configured.filename().string())
        == lowerAscii(filename.string())) {
        candidates.insert(candidates.begin(), configured);
    }

    bool foundCandidate = false;
    std::set<std::string> visited;
    for (const fs::path& candidatePath : candidates) {
        const std::string key = lowerAscii(candidatePath.lexically_normal().string());
        if (!visited.insert(key).second || !regularFile(candidatePath)) continue;
        foundCandidate = true;
        RuntimeMultiplayerContentComponent component;
        std::uint64_t fingerprint = 0U;
        std::string candidateError;
        if (!loadRuntimeMultiplayerContentComponentFile(
                candidatePath,
                map.canonicalId,
                component,
                candidateError
            )
            || !runtimeMultiplayerContentFingerprint(
                {component},
                fingerprint,
                candidateError
            )) {
            continue;
        }
        if (fingerprint == map.contentFingerprint) {
            localMapPath = candidatePath;
            errorMessage.clear();
            return true;
        }
    }
    std::string cacheError;
    if (findRuntimeMultiplayerMapCache(localRoot, map, localMapPath, cacheError)) { errorMessage.clear(); return true; }
    RuntimeRandomMapOptions randomOptions;
    if (runtimeRandomMapOptionsFromFileName(filename.string(), randomOptions)) {
        RuntimeRandomMapResult generated;
        std::string generateError;
        if (runtimeGenerateRandomMap(config, randomOptions, generated, generateError)) {
            fs::path generatedPath;
            if (runtimeStoreRandomMap(generated, runtimeRandomMapCacheDirectory(config), generatedPath, generateError)) {
                RuntimeMultiplayerContentComponent component;
                std::uint64_t fingerprint = 0;
                if (loadRuntimeMultiplayerContentComponentFile(generatedPath, map.canonicalId, component, generateError)
                    && runtimeMultiplayerContentFingerprint({component}, fingerprint, generateError)
                    && fingerprint == map.contentFingerprint) {
                    localMapPath = generatedPath;
                    errorMessage.clear();
                    return true;
                }
            }
        }
        return fail(errorMessage, "random map could not be recreated with matching original rules: " + generateError);
    }
    return fail(
        errorMessage,
        foundCandidate
            ? "local exported map bytes do not match the room fingerprint"
            : "canonical multiplayer map is not exported on this machine"
    );
}

bool makeRuntimeMultiplayerMenuJoinOptions(
    const RuntimeLanRoomAdvertisement& room,
    std::string playerName,
    std::string peerId,
    bool spectator,
    std::string password,
    const RuntimeMultiplayerCompatibility& compatibility,
    RuntimeMultiplayerLanJoinOptions& options,
    std::string& errorMessage
) {
    RoomPasswordClearGuard passwordClear(password);
    if (!normalizeDisplayName(
            playerName,
            runtimeMultiplayerMaximumDisplayNameBytes,
            errorMessage
        )) {
        return false;
    }
    if (!validProtocolIdentifier(
            peerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return fail(errorMessage, "multiplayer peer id is invalid");
    }
    if (room.roomId.empty() || room.hostName.empty()
        || room.hostAddress.empty() || room.sessionPort == 0U
        || room.protocolVersion != runtimeLanProtocolVersion) {
        return fail(errorMessage, "discovered room advertisement is incomplete");
    }
    if (compatibility.protocolFingerprint == 0U
        || compatibility.contentFingerprint == 0U) {
        return fail(errorMessage, "local multiplayer compatibility is incomplete");
    }
    if (room.contentFingerprint
        != fingerprintText(compatibility.contentFingerprint)) {
        return fail(
            errorMessage,
            "room content fingerprint does not match verified local files"
        );
    }
    if (room.passwordProtected
        && !runtimeRoomPasswordValid(password)) {
        runtimeRoomPasswordSecureClear(password);
        return fail(
            errorMessage,
            "selected room requires a password of at most 128 bytes"
        );
    }
    if (!room.passwordProtected) {
        runtimeRoomPasswordSecureClear(password);
    }

    RuntimeMultiplayerLanJoinOptions candidate;
    candidate.room = room;
    candidate.peerId = std::move(peerId);
    candidate.displayName = std::move(playerName);
    candidate.participation = spectator
        ? RuntimeMultiplayerParticipationRole::Spectator
        : RuntimeMultiplayerParticipationRole::Combatant;
    candidate.compatibility = compatibility;
    candidate.password = std::move(password);
    options = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool projectRuntimeMultiplayerResumeSelection(const RuntimeConfig& config,
    const RuntimeMultiplayerResumeSave& save, const RuntimeMultiplayerCompatibility& compatibility,
    std::string playerName, std::string peerId, RuntimeMultiplayerHostSelectionProjection& projection,
    std::string& errorMessage) {
    if (save.compatibility != compatibility)
        return fail(errorMessage, "存档的游戏版本或规则资源与当前游戏不一致。");
    if (!validateRuntimeMultiplayerMatchSetup(save.setup, errorMessage)
        || !normalizeDisplayName(playerName, 64U, errorMessage)
        || !validProtocolIdentifier(peerId, runtimeMultiplayerMaximumPeerIdBytes)) return false;
    RuntimeMultiplayerHostSelectionProjection candidate;
    if (!resolveRuntimeMultiplayerMenuMap(config, save.setup.map, candidate.localMapPath, errorMessage)) return false;
    candidate.lobby.map = save.setup.map;
    candidate.lobby.rules = save.setup.rules;
    candidate.lobby.slotCount = save.setup.slots.size();
    candidate.lobby.hostMember.peerId = std::move(peerId);
    candidate.lobby.hostMember.displayName = std::move(playerName);
    candidate.lobby.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    const auto host = std::find_if(save.setup.members.begin(), save.setup.members.end(),
        [&](const auto& member) { return member.peerId == save.setup.hostPeerId; });
    candidate.lobby.hostMember.participation = host->identity == RuntimeMultiplayerMatchMemberIdentity::Spectator
        ? RuntimeMultiplayerParticipationRole::Spectator : RuntimeMultiplayerParticipationRole::Combatant;
    candidate.slots = save.setup.slots;
    for (const auto& slot : candidate.slots)
        if (slot.controller == RuntimeMultiplayerMatchSlotController::Computer) candidate.lobby.computerSlotIndices.push_back(slot.slotIndex);
    projection = std::move(candidate);errorMessage.clear();return true;
}

#if defined(_WIN32) && !defined(RUNTIME_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
bool selectRuntimeMultiplayerResumeSave(const RuntimeConfig& config,
    const RuntimeMultiplayerCompatibility& compatibility, std::string playerName,
    std::string peerId, void* owner, RuntimeMultiplayerResumeSave& save,
    RuntimeMultiplayerHostSelectionProjection& projection, std::string& errorMessage) {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(owner);
    dialog.lpstrTitle = L"选择联机存档，重建房间续局";
    dialog.lpstrFilter = L"联机存档\0*.ini;*.sav;*.csav\0所有文件\0*.*\0\0";
    dialog.lpstrFile = path.data();dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return fail(errorMessage, "已取消选择联机存档。");
    RuntimeMultiplayerResumeSave candidate;
    if (!loadRuntimeMultiplayerResumeSave(fs::path(path.data()), candidate, errorMessage)) {
        if (errorMessage.find("no room recipe") != std::string::npos)
            errorMessage = "此旧存档没有完整开局配置，只能在原联机会话内读取。请使用新版游戏保存的联机存档。";
        return false;
    }
    if (!projectRuntimeMultiplayerResumeSelection(config, candidate, compatibility, std::move(playerName), std::move(peerId), projection, errorMessage)) return false;
    save = std::move(candidate);return true;
}

namespace {

enum class MultiplayerMenuPage {
    Browser,
    Lobby
};

enum MultiplayerMenuControlId : int {
    PlayerNameEdit = 4101,
    HostPortEdit = 4102,
    DirectAddressEdit = 4103,
    HostButton = 4104,
    RefreshButton = 4105,
    JoinButton = 4106,
    SpectatorCheck = 4107,
    BackButton = 4108,
    RoomList = 4109,
    RosterList = 4110,
    ReadyButton = 4111,
    StartButton = 4112,
    LeaveButton = 4113,
    StatusText = 4114,
    NetworkHint = 4115,
    CountryEdit = 4116,
    ColorEdit = 4117,
    TeamEdit = 4118,
    DifficultyEdit = 4119,
    StartPositionEdit = 4120,
    ApplySlotButton = 4121,
    KickButton = 4122,
    PasswordEdit = 4123,
    ResumeHostButton = 4124,
    ResumeSwapButton = 4125
};

constexpr UINT_PTR multiplayerMenuPollTimer = 1U;
constexpr UINT multiplayerMenuPollMilliseconds = 50U;
constexpr ULONGLONG directDiscoveryTimeoutMilliseconds = 5000U;

struct RuntimeMultiplayerMenuUiState {
    RuntimeConfig baseConfig;
    RuntimeMultiplayerMenuNetwork network =
        RuntimeMultiplayerMenuNetwork::Lan;
    HWND hostWindow = nullptr;
    HWND window = nullptr;
    HWND playerNameLabel = nullptr;
    HWND playerNameEdit = nullptr;
    HWND hostPortLabel = nullptr;
    HWND hostPortEdit = nullptr;
    HWND directAddressLabel = nullptr;
    HWND directAddressEdit = nullptr;
    HWND passwordLabel = nullptr;
    HWND passwordEdit = nullptr;
    HWND hostButton = nullptr;
    HWND resumeHostButton = nullptr;
    HWND resumeSwapButton = nullptr;
    HWND refreshButton = nullptr;
    HWND joinButton = nullptr;
    HWND spectatorCheck = nullptr;
    HWND backButton = nullptr;
    HWND roomList = nullptr;
    HWND rosterList = nullptr;
    HWND countryLabel = nullptr;
    HWND countryEdit = nullptr;
    HWND colorLabel = nullptr;
    HWND colorEdit = nullptr;
    HWND teamLabel = nullptr;
    HWND teamEdit = nullptr;
    HWND difficultyLabel = nullptr;
    HWND difficultyEdit = nullptr;
    HWND startPositionLabel = nullptr;
    HWND startPositionEdit = nullptr;
    HWND applySlotButton = nullptr;
    HWND kickButton = nullptr;
    HWND readyButton = nullptr;
    HWND startButton = nullptr;
    HWND leaveButton = nullptr;
    HWND statusText = nullptr;
    HWND networkHint = nullptr;
    RuntimeMultiplayerLobbyFlow flow;
    RuntimeMultiplayerCompatibility compatibility;
    bool compatibilityReady = false;
    std::string peerId;
    MultiplayerMenuPage page = MultiplayerMenuPage::Browser;
    std::vector<RuntimeLanRoomAdvertisement> rooms;
    std::vector<std::string> rosterPeerIds;
    std::string selectedRosterPeerId;
    std::uint64_t displayedSetupRevision = 0U;
    fs::path localMapPath;
    bool hosting = false;
    bool directDiscoveryPending = false;
    RuntimeMultiplayerDirectEndpoint directEndpoint;
    std::string pendingPlayerName;
    std::string pendingPassword;
    bool pendingSpectator = false;
    ULONGLONG directDeadline = 0U;
    ULONGLONG nextDirectProbe = 0U;
    bool done = false;
    RuntimeMultiplayerMenuResult result;
};

std::wstring wideFromUtf8(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return L"(text too long)";
    }
    const int inputSize = static_cast<int>(value.size());
    int count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        inputSize,
        nullptr,
        0
    );
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0U;
        count = MultiByteToWideChar(
            codePage,
            flags,
            value.data(),
            inputSize,
            nullptr,
            0
        );
    }
    if (count <= 0) return L"(invalid text)";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        value.data(),
        inputSize,
        result.data(),
        count
    );
    return result;
}

std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return {};
    }
    const int inputSize = static_cast<int>(value.size());
    const int count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        inputSize,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        inputSize,
        result.data(),
        count,
        nullptr,
        nullptr
    );
    return result;
}

std::string controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return utf8FromWide(value);
}

void setControlText(HWND control, const std::string& text) {
    const std::wstring wide = wideFromUtf8(text);
    SetWindowTextW(control, wide.c_str());
}

void setStatus(
    RuntimeMultiplayerMenuUiState& state,
    const std::string& text
) {
    setControlText(state.statusText, text);
}

HWND createControl(
    HWND parent,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int identifier
) {
    return CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        GetModuleHandleW(nullptr),
        nullptr
    );
}

void showControl(HWND control, bool visible) {
    if (control != nullptr) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

void showPage(
    RuntimeMultiplayerMenuUiState& state,
    MultiplayerMenuPage page
) {
    state.page = page;
    const bool browser = page == MultiplayerMenuPage::Browser;
    const bool lan = state.network == RuntimeMultiplayerMenuNetwork::Lan;
    showControl(state.playerNameLabel, browser);
    showControl(state.playerNameEdit, browser);
    showControl(state.hostPortLabel, browser && !lan);
    showControl(state.hostPortEdit, browser && !lan);
    showControl(state.directAddressLabel, browser && !lan);
    showControl(state.directAddressEdit, browser && !lan);
    showControl(state.passwordLabel, browser);
    showControl(state.passwordEdit, browser);
    showControl(state.hostButton, browser);
    showControl(state.resumeHostButton, browser);
    showControl(state.resumeSwapButton, !browser && state.hosting && state.flow.resuming());
    showControl(state.refreshButton, browser && lan);
    showControl(state.joinButton, browser);
    showControl(state.spectatorCheck, browser);
    showControl(state.backButton, browser);
    showControl(state.roomList, browser && lan);
    showControl(state.rosterList, !browser);
    showControl(state.countryLabel, !browser);
    showControl(state.countryEdit, !browser);
    showControl(state.colorLabel, !browser);
    showControl(state.colorEdit, !browser);
    showControl(state.teamLabel, !browser);
    showControl(state.teamEdit, !browser);
    showControl(state.difficultyLabel, !browser);
    showControl(state.difficultyEdit, !browser);
    showControl(state.startPositionLabel, !browser);
    showControl(state.startPositionEdit, !browser);
    showControl(state.applySlotButton, !browser);
    showControl(state.kickButton, !browser && state.hosting);
    showControl(state.readyButton, !browser);
    showControl(state.startButton, !browser && state.hosting);
    showControl(state.leaveButton, !browser);
    showControl(state.networkHint, browser);
    if (!browser) {
        state.displayedSetupRevision = 0U;
        setStatus(state, "Lobby connected; verifying local map...");
    }
}

void createMenuControls(RuntimeMultiplayerMenuUiState& state) {
    HWND window = state.window;
    state.playerNameLabel = createControl(
        window, L"STATIC", L"Player name", SS_LEFT,
        24, 22, 110, 22, 0);
    state.playerNameEdit = createControl(
        window,
        L"EDIT",
        L"Player",
        WS_BORDER | ES_AUTOHSCROLL,
        138,
        18,
        260,
        26,
        PlayerNameEdit
    );
    state.hostPortLabel = createControl(
        window, L"STATIC", L"Host port", SS_LEFT,
        24, 58, 110, 22, 0);
    state.hostPortEdit = createControl(
        window,
        L"EDIT",
        L"17816",
        WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        138,
        54,
        120,
        26,
        HostPortEdit
    );
    state.directAddressLabel = createControl(
        window, L"STATIC", L"Direct address", SS_LEFT,
        24, 94, 110, 22, 0);
    state.directAddressEdit = createControl(
        window,
        L"EDIT",
        L"127.0.0.1:17816",
        WS_BORDER | ES_AUTOHSCROLL,
        138,
        90,
        260,
        26,
        DirectAddressEdit
    );
    state.passwordLabel = createControl(
        window, L"STATIC", L"Room password", SS_LEFT,
        24, 130, 110, 22, 0);
    state.passwordEdit = createControl(
        window,
        L"EDIT",
        L"",
        WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
        138,
        124,
        260,
        26,
        PasswordEdit
    );
    state.hostButton = createControl(
        window,
        L"BUTTON",
        state.network == RuntimeMultiplayerMenuNetwork::Lan
            ? L"Host LAN Room"
            : L"Host Direct Room",
        BS_PUSHBUTTON,
        420,
        18,
        150,
        30,
        HostButton
    );
    state.refreshButton = createControl(
        window,
        L"BUTTON",
        L"Refresh LAN",
        BS_PUSHBUTTON,
        420,
        54,
        150,
        30,
        RefreshButton
    );
    state.resumeHostButton = createControl(window, L"BUTTON", L"从存档恢复", BS_PUSHBUTTON,
        588, 18, 152, 30, ResumeHostButton);
    state.resumeSwapButton = createControl(window, L"BUTTON", L"与选中玩家交换席位", BS_PUSHBUTTON,
        464, 410, 276, 28, ResumeSwapButton);
    state.joinButton = createControl(
        window,
        L"BUTTON",
        state.network == RuntimeMultiplayerMenuNetwork::Lan
            ? L"Join Selected"
            : L"Direct Connect",
        BS_DEFPUSHBUTTON,
        420,
        90,
        150,
        30,
        JoinButton
    );
    state.spectatorCheck = createControl(
        window,
        L"BUTTON",
        L"Join as spectator",
        BS_AUTOCHECKBOX,
        420,
        124,
        180,
        24,
        SpectatorCheck
    );
    state.networkHint = createControl(
        window,
        L"STATIC",
        state.network == RuntimeMultiplayerMenuNetwork::Lan
            ? L"LAN discovery uses UDP 17815; gameplay uses TCP 17816."
            : L"Direct hosting requires the chosen port forwarded for both UDP and TCP. No matchmaking or NAT traversal is provided.",
        SS_LEFT,
        24,
        158,
        716,
        36,
        NetworkHint
    );
    state.roomList = createControl(
        window,
        L"LISTBOX",
        L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24,
        202,
        716,
        220,
        RoomList
    );
    state.rosterList = createControl(
        window,
        L"LISTBOX",
        L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24,
        58,
        716,
        238,
        RosterList
    );
    state.countryLabel = createControl(
        window, L"STATIC", L"Country", SS_LEFT,
        24, 302, 100, 20, 0);
    state.countryEdit = createControl(
        window, L"EDIT", L"Random", WS_BORDER | ES_AUTOHSCROLL,
        24, 322, 110, 26, CountryEdit);
    state.colorLabel = createControl(
        window, L"STATIC", L"Color", SS_LEFT,
        144, 302, 90, 20, 0);
    state.colorEdit = createControl(
        window, L"EDIT", L"Random", WS_BORDER | ES_AUTOHSCROLL,
        144, 322, 96, 26, ColorEdit);
    state.teamLabel = createControl(
        window, L"STATIC", L"Team", SS_LEFT,
        250, 302, 80, 20, 0);
    state.teamEdit = createControl(
        window, L"EDIT", L"-", WS_BORDER | ES_AUTOHSCROLL,
        250, 322, 76, 26, TeamEdit);
    state.difficultyLabel = createControl(
        window, L"STATIC", L"Difficulty", SS_LEFT,
        336, 302, 100, 20, 0);
    state.difficultyEdit = createControl(
        window, L"EDIT", L"Medium", WS_BORDER | ES_AUTOHSCROLL,
        336, 322, 104, 26, DifficultyEdit);
    state.startPositionLabel = createControl(
        window, L"STATIC", L"Start (0-8)", SS_LEFT,
        450, 302, 110, 20, 0);
    state.startPositionEdit = createControl(
        window,
        L"EDIT",
        L"0",
        WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        450,
        322,
        78,
        26,
        StartPositionEdit
    );
    state.applySlotButton = createControl(
        window,
        L"BUTTON",
        L"Apply Player Setup",
        BS_PUSHBUTTON,
        544,
        318,
        196,
        32,
        ApplySlotButton
    );
    state.readyButton = createControl(
        window,
        L"BUTTON",
        L"Ready",
        BS_PUSHBUTTON,
        24,
        374,
        130,
        32,
        ReadyButton
    );
    state.startButton = createControl(
        window,
        L"BUTTON",
        L"Start Match",
        BS_DEFPUSHBUTTON,
        166,
        374,
        130,
        32,
        StartButton
    );
    state.kickButton = createControl(
        window,
        L"BUTTON",
        L"Remove Selected",
        BS_PUSHBUTTON,
        308,
        374,
        150,
        32,
        KickButton
    );
    state.leaveButton = createControl(
        window,
        L"BUTTON",
        L"Leave Room",
        BS_PUSHBUTTON,
        610,
        374,
        130,
        32,
        LeaveButton
    );
    state.backButton = createControl(
        window,
        L"BUTTON",
        L"Back",
        BS_PUSHBUTTON,
        610,
        438,
        130,
        32,
        BackButton
    );
    state.statusText = createControl(
        window,
        L"STATIC",
        L"",
        SS_LEFT,
        24,
        432,
        570,
        58,
        StatusText
    );
    showPage(state, MultiplayerMenuPage::Browser);
}

void updateRoomList(RuntimeMultiplayerMenuUiState& state) {
    state.rooms = state.flow.discoveredRooms();
    SendMessageW(state.roomList, LB_RESETCONTENT, 0U, 0);
    for (const RuntimeLanRoomAdvertisement& room : state.rooms) {
        std::ostringstream line;
        line << room.roomName << "  ["
             << static_cast<unsigned int>(room.playerCount) << '/'
             << static_cast<unsigned int>(room.maximumPlayers) << "]  "
             << room.hostName << "  " << room.hostAddress << ':'
             << room.sessionPort;
        if (room.passwordProtected) line << "  [Password]";
        const std::wstring wide = wideFromUtf8(line.str());
        SendMessageW(
            state.roomList,
            LB_ADDSTRING,
            0U,
            reinterpret_cast<LPARAM>(wide.c_str())
        );
    }
}

bool localMemberReady(const RuntimeMultiplayerMenuUiState& state) {
    if (state.flow.bridge() == nullptr
        || !state.flow.bridge()->lan().session().active()) {
        return false;
    }
    const std::string& localPeerId =
        state.flow.bridge()->lan().localPeerId();
    for (const RuntimeMultiplayerRoomMember& member
         : state.flow.bridge()->lan().session().members()) {
        if (member.peerId == localPeerId) return member.ready;
    }
    return false;
}

const RuntimeMultiplayerMatchSlot* humanSlotForPeer(
    const RuntimeMultiplayerMenuUiState& state,
    std::string_view peerId
) {
    if (!state.flow.lobby().initialized()) return nullptr;
    const auto& slots = state.flow.lobby().setup().slots;
    const auto found = std::find_if(
        slots.begin(),
        slots.end(),
        [peerId](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && slot.peerId == peerId;
        }
    );
    return found == slots.end() ? nullptr : &*found;
}

void populateLocalSlotControls(RuntimeMultiplayerMenuUiState& state) {
    if (state.flow.bridge() == nullptr
        || !state.flow.lobby().initialized()) {
        return;
    }
    const RuntimeMultiplayerMatchSlot* slot = humanSlotForPeer(
        state,
        state.flow.bridge()->lan().localPeerId()
    );
    const bool editable = slot != nullptr && !state.flow.resuming();
    for (HWND control : {
            state.countryEdit,
            state.colorEdit,
            state.teamEdit,
            state.difficultyEdit,
            state.startPositionEdit,
            state.applySlotButton
        }) {
        EnableWindow(control, editable ? TRUE : FALSE);
    }
    if (!slot) return;
    if (state.flow.lobby().setup().rules.gameMode == "cooperative") {
        EnableWindow(state.teamEdit, FALSE);
        EnableWindow(state.startPositionEdit, FALSE);
    }
    setControlText(state.countryEdit, slot->countryId);
    setControlText(state.colorEdit, slot->colorId);
    setControlText(state.teamEdit, slot->teamId);
    setControlText(state.difficultyEdit, slot->difficultyId);
    setControlText(
        state.startPositionEdit,
        std::to_string(static_cast<unsigned int>(slot->startPosition))
    );
}

void updateRoster(RuntimeMultiplayerMenuUiState& state) {
    SendMessageW(state.rosterList, LB_RESETCONTENT, 0U, 0);
    state.rosterPeerIds.clear();
    if (state.flow.bridge() == nullptr
        || !state.flow.bridge()->lan().session().active()) {
        return;
    }
    for (const RuntimeMultiplayerRoomMember& member
         : state.flow.bridge()->lan().session().members()) {
        std::string line = member.displayName;
        line += member.role == RuntimeMultiplayerRoomMemberRole::Host
            ? "  [Host]"
            : "  [Client]";
        line += member.participation
                == RuntimeMultiplayerParticipationRole::Spectator
            ? " [Spectator]"
            : " [Combatant]";
        line += member.ready ? " [Ready]" : " [Not Ready]";
        if (const RuntimeMultiplayerMatchSlot* slot = humanSlotForPeer(
                state,
                member.peerId
            )) {
            line += "  [" + slot->countryId + " / " + slot->colorId
                + " / Team " + slot->teamId + " / "
                + slot->difficultyId + " / Start "
                + std::to_string(
                    static_cast<unsigned int>(slot->startPosition)
                ) + "]";
        }
        const std::wstring wide = wideFromUtf8(line);
        SendMessageW(
            state.rosterList,
            LB_ADDSTRING,
            0U,
            reinterpret_cast<LPARAM>(wide.c_str())
        );
        state.rosterPeerIds.push_back(member.peerId);
    }
    const auto selected = std::find(
        state.rosterPeerIds.begin(),
        state.rosterPeerIds.end(),
        state.selectedRosterPeerId
    );
    if (selected != state.rosterPeerIds.end()) {
        SendMessageW(
            state.rosterList,
            LB_SETCURSEL,
            static_cast<WPARAM>(std::distance(
                state.rosterPeerIds.begin(),
                selected
            )),
            0
        );
    }
    if (state.flow.lobby().initialized()
        && state.displayedSetupRevision
            != state.flow.lobby().setup().revision) {
        state.displayedSetupRevision = state.flow.lobby().setup().revision;
        populateLocalSlotControls(state);
    }
    SetWindowTextW(
        state.readyButton,
        localMemberReady(state) ? L"Unready" : L"Ready"
    );
    std::string resumeError;
    const bool resumeReady = state.flow.resumeReady(resumeError);
    if (state.flow.resuming()) populateLocalSlotControls(state);
    EnableWindow(
        state.readyButton,
        state.flow.localMapVerified() && resumeReady ? TRUE : FALSE
    );
    if (state.hosting) {
        std::string startError;
        EnableWindow(
            state.startButton,
            state.flow.canStart(startError) ? TRUE : FALSE
        );
    }
    const std::string& localPeerId =
        state.flow.bridge()->lan().localPeerId();
    EnableWindow(
        state.kickButton,
        state.hosting && !state.selectedRosterPeerId.empty()
                && state.selectedRosterPeerId != localPeerId
                && std::find(
                    state.rosterPeerIds.begin(),
                    state.rosterPeerIds.end(),
                    state.selectedRosterPeerId
                ) != state.rosterPeerIds.end()
            ? TRUE
            : FALSE
    );
}

bool ensureLocalMapVerified(
    RuntimeMultiplayerMenuUiState& state,
    std::string& errorMessage
) {
    if (state.flow.localMapVerified()) return true;
    if (!state.flow.canonicalSetupPublished()) {
        return fail(
            errorMessage,
            "waiting for the canonical room setup before map verification"
        );
    }
    if (!state.localMapPath.empty() && !state.flow.verifyLocalMap(state.localMapPath, errorMessage)) state.localMapPath.clear();
    if (state.localMapPath.empty()) {
        if (!resolveRuntimeMultiplayerMenuMap(
                state.baseConfig,
                state.flow.lobby().setup().map,
                state.localMapPath,
                errorMessage
            )) {
            if (!state.flow.hostAuthoritative())
                return state.flow.downloadMissingMap(state.baseConfig.localDataRootPath, state.localMapPath, errorMessage);
            return false;
        }
    }
    if (!state.flow.verifyLocalMap(state.localMapPath, errorMessage)) {
        state.localMapPath.clear();
        return false;
    }
    return true;
}

void closeMenu(RuntimeMultiplayerMenuUiState& state) {
    state.directDiscoveryPending = false;
    runtimeRoomPasswordSecureClear(state.pendingPassword);
    if (state.passwordEdit != nullptr) SetWindowTextW(state.passwordEdit, L"");
    if (!state.result.accepted) state.flow.close();
    state.done = true;
    if (state.window != nullptr && IsWindow(state.window)) {
        DestroyWindow(state.window);
    }
}

bool beginLanBrowsing(
    RuntimeMultiplayerMenuUiState& state,
    std::string& errorMessage
) {
    if (state.flow.bridge() == nullptr
        || state.flow.bridge()->lan().phase()
            != RuntimeMultiplayerLanPhase::Browsing) {
        if (!state.flow.startBrowsing(
                runtimeLanDefaultDiscoveryPort,
                errorMessage
            )) {
            return false;
        }
    }
    std::string loopbackError;
    const bool broadcast = state.flow.sendDiscoveryProbe(
        "255.255.255.255",
        errorMessage
    );
    const bool loopback = state.flow.sendDiscoveryProbe(
        "127.0.0.1",
        loopbackError
    );
    if (!broadcast && !loopback) {
        if (errorMessage.empty()) errorMessage = std::move(loopbackError);
        return false;
    }
    errorMessage.clear();
    return true;
}

bool enterLobbyWithRoom(
    RuntimeMultiplayerMenuUiState& state,
    const RuntimeLanRoomAdvertisement& room,
    std::string playerName,
    bool spectator,
    std::string password,
    std::string& errorMessage
) {
    RuntimeMultiplayerLanJoinOptions join;
    if (!makeRuntimeMultiplayerMenuJoinOptions(
            room,
            std::move(playerName),
            state.peerId,
            spectator,
            std::move(password),
            state.compatibility,
            join,
            errorMessage
        )
        || !state.flow.join(std::move(join), errorMessage)) {
        return false;
    }
    state.hosting = false;
    SetWindowTextW(state.passwordEdit, L"");
    state.localMapPath.clear();
    state.directDiscoveryPending = false;
    showPage(state, MultiplayerMenuPage::Lobby);
    setStatus(state, "Connecting and waiting for canonical room setup...");
    return true;
}

bool startHosting(
    RuntimeMultiplayerMenuUiState& state,
    std::string& errorMessage,
    bool fromSave = false
) {
    if (!state.compatibilityReady) {
        return fail(errorMessage, "local compatibility files are not verified");
    }
    std::string playerName = controlText(state.playerNameEdit);
    if (!normalizeDisplayName(playerName, 64U, errorMessage)) return false;

    std::uint16_t hostPort = runtimeLanDefaultSessionPort;
    if (state.network == RuntimeMultiplayerMenuNetwork::Internet) {
        RuntimeMultiplayerDirectEndpoint endpoint;
        if (!parseRuntimeMultiplayerDirectEndpoint(
                "host:" + controlText(state.hostPortEdit),
                endpoint,
                errorMessage
            )) {
            return false;
        }
        hostPort = endpoint.port;
    }

    RuntimeMultiplayerResumeSave resumeSave;
    RuntimeMultiplayerHostSelectionProjection projection;
    RuntimeSkirmishSelection selection;
    if (fromSave) {
        if (!selectRuntimeMultiplayerResumeSave(state.baseConfig, state.compatibility, playerName, state.peerId,
            state.window, resumeSave, projection, errorMessage)) return false;
    } else {
        ShowWindow(state.window, SW_HIDE);
        auto hostConfig = state.baseConfig;
        hostConfig.multiplayerActive = true;
        hostConfig.noCooldown = false;
        selection = runRuntimeSkirmishMenu(hostConfig, state.hostWindow);
        if (state.window != nullptr && IsWindow(state.window)) {
            ShowWindow(state.window, SW_SHOW);SetForegroundWindow(state.window);
        }
        if (!selection.accepted) return fail(errorMessage, "host setup selection was cancelled");
    }

    std::uint64_t randomSeed = 0U;
    std::uint64_t roomGeneration = 0U;
    std::string roomId;
    if (!secureRandomU64(randomSeed, errorMessage)
        || !secureRandomU64(roomGeneration, errorMessage)
        || !generateRuntimeMultiplayerSecureId(
            "room-",
            roomId,
            errorMessage
        )) {
        return false;
    }

    if (!fromSave && !projectRuntimeMultiplayerHostSelection(
            state.baseConfig,
            selection,
            playerName,
            state.peerId,
            randomSeed,
            projection,
            errorMessage
        )) {
        return false;
    }

    RuntimeMultiplayerLanHostOptions network;
    network.session.roomId = std::move(roomId);
    network.session.hostPeerId = state.peerId;
    network.session.hostDisplayName = playerName;
    network.session.hostParticipation = projection.lobby.hostMember.participation;
    network.session.compatibility = state.compatibility;
    network.roomName = playerName + (fromSave ? " · 恢复存档" : " Room");
    network.discoveryPort =
        state.network == RuntimeMultiplayerMenuNetwork::Internet
        ? hostPort
        : runtimeLanDefaultDiscoveryPort;
    network.sessionPort = hostPort;
    network.roomGeneration = roomGeneration;
    network.password = controlText(state.passwordEdit);
    network.passwordProtected = !network.password.empty();
    if (!state.flow.startHost(
            std::move(network),
            projection.lobby,
            errorMessage,
            fromSave ? &resumeSave : nullptr
        )) {
        return false;
    }
    for (std::size_t index = 0U; !fromSave && index < projection.slots.size(); ++index) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit = state.flow.setSlot(
            index,
            projection.slots[index],
            publishError
        );
        if (!edit.accepted || !publishError.empty()) {
            state.flow.close();
            return fail(
                errorMessage,
                !publishError.empty() ? publishError : edit.reason
            );
        }
    }

    state.hosting = true;
    SetWindowTextW(state.passwordEdit, L"");
    state.localMapPath = projection.localMapPath;
    showPage(state, MultiplayerMenuPage::Lobby);
    std::string verificationError;
    if (ensureLocalMapVerified(state, verificationError)) {
        setStatus(
            state,
            state.network == RuntimeMultiplayerMenuNetwork::Internet
                ? "Direct room ready. Forward the displayed port for both UDP and TCP."
                : "LAN room ready. Waiting for players."
        );
    } else {
        setStatus(state, verificationError);
    }
    updateRoster(state);
    if (fromSave) setStatus(state, "恢复房间已建立。等待原有席位到齐并接收存档；选中玩家后可交换席位。");
    return true;
}

void beginDirectDiscovery(RuntimeMultiplayerMenuUiState& state) {
    std::string errorMessage;
    RuntimeMultiplayerDirectEndpoint endpoint;
    if (!state.compatibilityReady) {
        setStatus(state, "Local compatibility files are not verified.");
        return;
    }
    std::string playerName = controlText(state.playerNameEdit);
    if (!normalizeDisplayName(
            playerName,
            runtimeMultiplayerMaximumDisplayNameBytes,
            errorMessage
        )
        || !parseRuntimeMultiplayerDirectEndpoint(
            controlText(state.directAddressEdit),
            endpoint,
            errorMessage
        )
        || !state.flow.startBrowsing(endpoint.port, errorMessage)
        || !state.flow.sendDiscoveryProbe(endpoint.host, errorMessage)) {
        setStatus(state, errorMessage);
        return;
    }
    state.directEndpoint = std::move(endpoint);
    state.pendingPlayerName = std::move(playerName);
    runtimeRoomPasswordSecureClear(state.pendingPassword);
    state.pendingPassword = controlText(state.passwordEdit);
    state.pendingSpectator = SendMessageW(
        state.spectatorCheck,
        BM_GETCHECK,
        0U,
        0
    ) == BST_CHECKED;
    state.directDiscoveryPending = true;
    const ULONGLONG now = GetTickCount64();
    state.directDeadline = now + directDiscoveryTimeoutMilliseconds;
    state.nextDirectProbe = now + 1000U;
    setStatus(
        state,
        "Probing the direct host over UDP before the verified TCP join..."
    );
}

void joinSelectedLanRoom(RuntimeMultiplayerMenuUiState& state) {
    const LRESULT selection = SendMessageW(
        state.roomList,
        LB_GETCURSEL,
        0U,
        0
    );
    if (selection == LB_ERR
        || static_cast<std::size_t>(selection) >= state.rooms.size()) {
        setStatus(state, "Select a discovered LAN room first.");
        return;
    }
    const bool spectator = SendMessageW(
        state.spectatorCheck,
        BM_GETCHECK,
        0U,
        0
    ) == BST_CHECKED;
    std::string errorMessage;
    if (!enterLobbyWithRoom(
            state,
            state.rooms[static_cast<std::size_t>(selection)],
            controlText(state.playerNameEdit),
            spectator,
            controlText(state.passwordEdit),
            errorMessage
        )) {
        setStatus(state, errorMessage);
    }
}

void applyLocalSlotOptions(RuntimeMultiplayerMenuUiState& state) {
    if (state.flow.bridge() == nullptr
        || !state.flow.lobby().initialized()) {
        setStatus(state, "Canonical lobby setup is not available yet.");
        return;
    }
    const RuntimeMultiplayerMatchSlot* localSlot = humanSlotForPeer(
        state,
        state.flow.bridge()->lan().localPeerId()
    );
    if (!localSlot) {
        setStatus(state, "Spectators do not own a player setup slot.");
        return;
    }
    std::string country = trimAscii(controlText(state.countryEdit));
    std::string color = trimAscii(controlText(state.colorEdit));
    std::string team = trimAscii(controlText(state.teamEdit));
    std::string difficulty = trimAscii(
        controlText(state.difficultyEdit)
    );
    const std::string startText = trimAscii(
        controlText(state.startPositionEdit)
    );
    if (startText.size() != 1U || startText.front() < '0'
        || startText.front() > '8') {
        setStatus(state, "Start position must be one digit from 0 to 8.");
        return;
    }
    const auto startPosition = static_cast<std::uint8_t>(
        startText.front() - '0'
    );
    if (state.hosting) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit =
            state.flow.setSlotOptions(
                localSlot->slotIndex,
                std::move(country),
                std::move(color),
                std::move(team),
                std::move(difficulty),
                startPosition,
                publishError
            );
        if (!edit.accepted || !publishError.empty()) {
            setStatus(
                state,
                !publishError.empty() ? publishError : edit.reason
            );
            return;
        }
        setStatus(state, "Player setup updated for the whole lobby.");
    } else {
        const RuntimeMultiplayerLanOperationResult result =
            state.flow.requestLocalSlotOptions(
                std::move(country),
                std::move(color),
                std::move(team),
                std::move(difficulty),
                startPosition
            );
        if (!result.succeeded) {
            setStatus(state, result.reason);
            return;
        }
        setStatus(state, "Player setup request sent to the host...");
    }
    updateRoster(state);
}

void kickSelectedPeer(RuntimeMultiplayerMenuUiState& state) {
    if (!state.hosting || state.selectedRosterPeerId.empty()) {
        setStatus(state, "Select a remote player or spectator first.");
        return;
    }
    const RuntimeMultiplayerLanOperationResult result = state.flow.kickPeer(
        state.selectedRosterPeerId,
        "removed by the host"
    );
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    state.selectedRosterPeerId.clear();
    setStatus(state, "Selected lobby member was removed.");
    updateRoster(state);
}

void pollDirectDiscovery(RuntimeMultiplayerMenuUiState& state) {
    if (!state.directDiscoveryPending) return;
    const ULONGLONG now = GetTickCount64();
    const std::vector<RuntimeLanRoomAdvertisement> discovered =
        state.flow.discoveredRooms();
    const auto matching = std::find_if(
        discovered.begin(),
        discovered.end(),
        [&state](const RuntimeLanRoomAdvertisement& room) {
            return room.sessionPort == state.directEndpoint.port;
        }
    );
    if (matching != discovered.end()) {
        std::string errorMessage;
        if (!enterLobbyWithRoom(
                state,
                *matching,
                state.pendingPlayerName,
                state.pendingSpectator,
                std::move(state.pendingPassword),
                errorMessage
            )) {
            state.directDiscoveryPending = false;
            setStatus(state, errorMessage);
        }
        return;
    }
    if (now >= state.directDeadline) {
        state.directDiscoveryPending = false;
        runtimeRoomPasswordSecureClear(state.pendingPassword);
        setStatus(
            state,
            "Direct host was not discovered. Confirm the address and forward the same port for UDP and TCP."
        );
        return;
    }
    if (now >= state.nextDirectProbe) {
        std::string ignored;
        state.flow.sendDiscoveryProbe(state.directEndpoint.host, ignored);
        state.nextDirectProbe = now + 1000U;
    }
}

void finishStartedMatch(RuntimeMultiplayerMenuUiState& state) {
    RuntimeMultiplayerLaunchConfigResult launch;
    std::string errorMessage;
    if (!state.flow.makeLaunchConfig(
            state.baseConfig,
            launch,
            errorMessage
        )) {
        setStatus(state, errorMessage);
        return;
    }
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> bridge =
        state.flow.takeRunningBridge(errorMessage);
    if (!bridge) {
        setStatus(state, errorMessage);
        return;
    }
    if (!bridge->beginPreAttachMaintenance(errorMessage)) {
        setStatus(state, errorMessage);
        return;
    }
    state.result.launchConfig = std::move(launch);
    state.result.bridge = std::move(bridge);
    state.result.accepted = true;
    state.result.error.clear();
    closeMenu(state);
}

void pollMenu(RuntimeMultiplayerMenuUiState& state) {
    if (!state.flow.initialized()) return;
    const RuntimeMultiplayerLobbyFlowPollResult polled = state.flow.poll();
    if (!polled.succeeded) {
        state.directDiscoveryPending = false;
        setStatus(state, polled.error);
        return;
    }
    for (const RuntimeMultiplayerLanEvent& event : polled.events) {
        if (event.kind == RuntimeMultiplayerLanEventKind::Kicked) {
            const std::wstring message = wideFromUtf8(
                event.reason.empty()
                    ? "You were removed from the lobby by the host."
                    : event.reason
            );
            MessageBoxW(
                state.window,
                message.c_str(),
                L"Removed from lobby",
                MB_OK | MB_ICONINFORMATION
            );
            closeMenu(state);
            return;
        }
        if (event.kind
                == RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult
            && event.lobbySlotOptionsResult.has_value()) {
            const auto& response = *event.lobbySlotOptionsResult;
            setStatus(
                state,
                response.accepted
                    ? "Host accepted the player setup."
                    : (response.reason.empty()
                        ? "Host rejected the player setup."
                        : response.reason)
            );
        }
    }
    if (state.page == MultiplayerMenuPage::Browser) {
        if (state.network == RuntimeMultiplayerMenuNetwork::Lan) {
            updateRoomList(state);
        } else {
            pollDirectDiscovery(state);
        }
        return;
    }

    std::string verificationError;
    if (!state.flow.localMapVerified()
        && state.flow.canonicalSetupPublished()) {
        if (ensureLocalMapVerified(state, verificationError)) {
            setStatus(state, "Local map verified. Ready is now available.");
        } else {
            setStatus(state, verificationError);
        }
    }
    updateRoster(state);
    if (polled.matchStarted) finishStartedMatch(state);
}

void toggleReady(RuntimeMultiplayerMenuUiState& state) {
    const RuntimeMultiplayerLanOperationResult result = state.flow.setReady(
        !localMemberReady(state)
    );
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    setStatus(
        state,
        result.queued ? "Ready change sent to host..." : "Ready state updated."
    );
    updateRoster(state);
}

void startMatch(RuntimeMultiplayerMenuUiState& state) {
    const RuntimeMultiplayerLanOperationResult result = state.flow.startMatch();
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    setStatus(state, "Starting the verified room...");
}

void leaveLobby(RuntimeMultiplayerMenuUiState& state) {
    if (state.flow.bridge() != nullptr
        && state.flow.bridge()->lan().role()
            == RuntimeMultiplayerLanRole::Client
        && state.flow.bridge()->lan().phase()
            == RuntimeMultiplayerLanPhase::Lobby) {
        state.flow.bridge()->lan().leave();
    }
    closeMenu(state);
}

LRESULT CALLBACK multiplayerMenuWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    RuntimeMultiplayerMenuUiState* state =
        reinterpret_cast<RuntimeMultiplayerMenuUiState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<RuntimeMultiplayerMenuUiState*>(
            create->lpCreateParams
        );
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)
        );
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wParam) == RosterList
            && HIWORD(wParam) == LBN_SELCHANGE) {
            const LRESULT selected = SendMessageW(
                state->rosterList,
                LB_GETCURSEL,
                0U,
                0
            );
            state->selectedRosterPeerId =
                selected != LB_ERR
                    && static_cast<std::size_t>(selected)
                        < state->rosterPeerIds.size()
                ? state->rosterPeerIds[static_cast<std::size_t>(selected)]
                : std::string{};
            updateRoster(*state);
            return 0;
        }
        if (HIWORD(wParam) != BN_CLICKED) break;
        switch (LOWORD(wParam)) {
        case HostButton: {
            std::string errorMessage;
            if (!startHosting(*state, errorMessage)) {
                setStatus(*state, errorMessage);
            }
            return 0;
        }
        case RefreshButton: {
            std::string errorMessage;
            if (!beginLanBrowsing(*state, errorMessage)) {
                setStatus(*state, errorMessage);
            } else {
                setStatus(*state, "LAN discovery probe sent.");
            }
            return 0;
        }
        case JoinButton:
            if (state->network == RuntimeMultiplayerMenuNetwork::Lan) {
                joinSelectedLanRoom(*state);
            } else {
                beginDirectDiscovery(*state);
            }
            return 0;
        case ReadyButton:
            toggleReady(*state);
            return 0;
        case ResumeHostButton: {
            std::string error;
            if (!startHosting(*state, error, true)) setStatus(*state, error);
            return 0;
        }
        case ResumeSwapButton: {
            const auto* slot = humanSlotForPeer(*state, state->selectedRosterPeerId);
            std::string error;
            if (!slot) { setStatus(*state, "请先选中一位占有席位的玩家。");return 0; }
            const auto edit = state->flow.setSlotController(slot->slotIndex, RuntimeMultiplayerMatchSlotController::Human, state->peerId, error);
            setStatus(*state, edit.accepted && error.empty() ? "席位已交换，请重新准备。" : error);
            updateRoster(*state);return 0;
        }
        case StartButton:
            startMatch(*state);
            return 0;
        case ApplySlotButton:
            applyLocalSlotOptions(*state);
            return 0;
        case KickButton:
            kickSelectedPeer(*state);
            return 0;
        case LeaveButton:
        case BackButton:
            leaveLobby(*state);
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wParam == multiplayerMenuPollTimer) pollMenu(*state);
        return 0;
    case WM_CLOSE:
        leaveLobby(*state);
        return 0;
    case WM_DESTROY:
        KillTimer(window, multiplayerMenuPollTimer);
        state->window = nullptr;
        state->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerMultiplayerMenuClass(std::string& errorMessage) {
    constexpr const wchar_t* className =
        L"ConcordRTS.RuntimeMultiplayerMenu";
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(GetModuleHandleW(nullptr), className, &existing)) {
        errorMessage.clear();
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = multiplayerMenuWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(32512U)
    );
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0U) {
        return fail(errorMessage, "could not register multiplayer menu window");
    }
    errorMessage.clear();
    return true;
}

HWND createMultiplayerMenuWindow(
    RuntimeMultiplayerMenuUiState& state,
    std::string& errorMessage
) {
    if (!registerMultiplayerMenuClass(errorMessage)) return nullptr;
    constexpr const wchar_t* className =
        L"ConcordRTS.RuntimeMultiplayerMenu";
    const bool hosted = state.hostWindow != nullptr
        && IsWindow(state.hostWindow);
    DWORD style = hosted
        ? WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN
        : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int width = 780;
    int height = 540;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (hosted) {
        RECT client{};
        GetClientRect(state.hostWindow, &client);
        width = std::max(1L, client.right - client.left);
        height = std::max(1L, client.bottom - client.top);
        x = 0;
        y = 0;
    }
    HWND window = CreateWindowExW(
        hosted ? 0U : WS_EX_APPWINDOW,
        className,
        state.network == RuntimeMultiplayerMenuNetwork::Lan
            ? L"Concord RTS - LAN Multiplayer"
            : L"Concord RTS - Direct Internet Multiplayer",
        style,
        x,
        y,
        width,
        height,
        hosted ? state.hostWindow : nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state
    );
    if (window == nullptr) {
        fail(errorMessage, "could not create multiplayer menu window");
        return nullptr;
    }
    errorMessage.clear();
    return window;
}

#include "ui/network/RuntimeResumeMenuCapture.inl"

} // namespace
#endif

#if !defined(RUNTIME_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
RuntimeMultiplayerMenuResult runRuntimeMultiplayerMenu(
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerMenuNetwork network,
    void* hostWindow
) {
    if (network == RuntimeMultiplayerMenuNetwork::Internet) {
        return runRuntimeInternetMultiplayerMenu(baseConfig, hostWindow);
    }
#if !defined(_WIN32)
    RuntimeMultiplayerMenuResult unavailable;
    (void)baseConfig;
    (void)hostWindow;
    unavailable.error = "multiplayer menu requires Win32";
    return unavailable;
#else
    RuntimeMultiplayerMenuUiState state;
    state.baseConfig = baseConfig;
    state.network = network;
    state.hostWindow = static_cast<HWND>(hostWindow);
    std::string initializationError;
    state.compatibilityReady = makeRuntimeMultiplayerMenuCompatibility(
        baseConfig,
        state.compatibility,
        initializationError
    );
    if (!generateRuntimeMultiplayerSecureId(
            "peer-",
            state.peerId,
            state.result.error
        )) {
        return std::move(state.result);
    }
    state.window = createMultiplayerMenuWindow(state, state.result.error);
    if (state.window == nullptr) return std::move(state.result);
    createMenuControls(state);
    if (state.compatibilityReady) {
        if (network == RuntimeMultiplayerMenuNetwork::Lan) {
            std::string browseError;
            if (!beginLanBrowsing(state, browseError)) {
                setStatus(state, browseError);
            } else {
                setStatus(state, "LAN browser ready. Refresh to discover rooms.");
            }
        } else {
            setStatus(
                state,
                "Enter a forwarded address:port for a direct UDP probe and TCP connection."
            );
        }
    } else {
        setStatus(state, initializationError);
    }
    SetTimer(
        state.window,
        multiplayerMenuPollTimer,
        multiplayerMenuPollMilliseconds,
        nullptr
    );
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);

    MSG message{};
    while (!state.done && state.window != nullptr
        && IsWindow(state.window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0U, 0U);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!state.result.accepted) state.flow.close();
    return std::move(state.result);
#endif
}

int runRuntimeMultiplayerMenuUiSmoke(
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerMenuNetwork network,
    void* hostWindow
) {
    RuntimeMultiplayerMenuResult result = runRuntimeMultiplayerMenu(
        baseConfig,
        network,
        hostWindow
    );
    if (result.bridge) result.bridge->lan().close();
    return result.error.empty() ? 0 : 1;
}

#if defined(_WIN32)
int runRuntimeMultiplayerResumeMenuWindowSmoke(const std::string& outputPath) {
    RuntimeMultiplayerMenuUiState state;
    std::string error;
    state.window = createMultiplayerMenuWindow(state, error);
    if (!state.window) return 1;
    createMenuControls(state);
    std::array<wchar_t, 64> label{};GetWindowTextW(state.resumeHostButton, label.data(), static_cast<int>(label.size()));
    const bool valid = std::wstring(label.data()) == L"从存档恢复"
        && (GetWindowLongPtrW(state.resumeHostButton, GWL_STYLE) & WS_VISIBLE)
        && (GetWindowLongPtrW(state.rosterList, GWL_STYLE) & LBS_NOTIFY)
        && captureRuntimeResumeMenu(state.window, std::filesystem::path(outputPath));
    DestroyWindow(state.window);
    return valid ? 0 : 1;
}
#endif
#endif
