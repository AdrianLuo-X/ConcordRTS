#include "CampaignProgress.h"

#include "IniFile.h"
#include "RuntimeAtomicFile.h"
#include "RuntimeObserver.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace {

namespace fs = std::filesystem;

std::string trimAscii(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string rootOrDefault(const std::string& root) {
    return root.empty() ? "local_data" : root;
}

std::string safeSegment(std::string value) {
    value = lowerAscii(trimAscii(value));
    if (value.empty()) {
        return "unknown";
    }
    for (char& character : value) {
        const bool safe = (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || character == '_' || character == '-';
        if (!safe) {
            character = '_';
        }
    }
    return value;
}

bool parseInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const std::string trimmed = trimAscii(value);
        const int parsed = std::stoi(trimmed, &consumed);
        if (consumed != trimmed.size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool(const std::string& value, bool& output) {
    const std::string normalized = lowerAscii(trimAscii(value));
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        output = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        output = false;
        return true;
    }
    return false;
}

bool readEncodedSaveField(
    const IniFile& ini,
    const std::string& section,
    const std::string& encodedKey,
    const std::string& legacyKey,
    std::string& value
) {
    const std::string encoded = trimAscii(
        ini.getValue(section, encodedKey, "")
    );
    if (!encoded.empty()) {
        if (!iniHexDecode(encoded, value, 32U * 1024U)) {
            return false;
        }
        value = trimAscii(value);
        return true;
    }
    value = trimAscii(ini.getValue(section, legacyKey, ""));
    return true;
}

std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trimAscii(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

std::vector<std::string> splitFields(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find('|', start);
        result.push_back(value.substr(
            start,
            separator == std::string::npos
                ? std::string::npos
                : separator - start
        ));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1U;
    }
    return result;
}

bool decodeHexField(const std::string& encoded, std::string& value) {
    return iniHexDecode(trimAscii(encoded), value, 32U * 1024U);
}

std::string joinCsv(const std::vector<std::string>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << values[index];
    }
    return output.str();
}

bool containsMission(
    const std::vector<std::string>& missions,
    const std::string& wanted
) {
    const std::string normalized = lowerAscii(trimAscii(wanted));
    return std::any_of(
        missions.begin(),
        missions.end(),
        [&normalized](const std::string& value) {
            return lowerAscii(trimAscii(value)) == normalized;
        }
    );
}

} // namespace

std::string campaignProgressPath(const std::string& localDataRoot) {
    return (fs::path(rootOrDefault(localDataRoot)) / "campaign_progress.ini").string();
}

std::string campaignSavePath(
    const std::string& localDataRoot,
    const std::string& campaignId,
    const std::string& missionId
) {
    return (
        fs::path(rootOrDefault(localDataRoot))
        / "campaign_saves"
        / (safeSegment(campaignId) + "_" + safeSegment(missionId) + ".ini")
    ).string();
}

std::string skirmishSavePath(const std::string& localDataRoot) {
    return (fs::path(rootOrDefault(localDataRoot)) / "runtime_save.ini").string();
}

bool loadCampaignProgress(
    const std::string& localDataRoot,
    CampaignProgressData& progress,
    std::string& errorMessage
) {
    progress = CampaignProgressData{};
    errorMessage.clear();
    const std::string path = campaignProgressPath(localDataRoot);
    std::error_code error;
    if (!fs::exists(path, error)) {
        return true;
    }

    IniFile ini;
    if (!ini.loadFromFile(path)) {
        errorMessage = "campaign progress file could not be read";
        return false;
    }
    int version = 0;
    if (!parseInt(ini.getValue("CampaignProgress", "Version", "0"), version)
        || version != 1) {
        errorMessage = "unsupported campaign progress version";
        return false;
    }
    progress.version = version;
    progress.campaignId = trimAscii(
        ini.getValue("CampaignProgress", "Campaign", "")
    );
    progress.difficulty = trimAscii(
        ini.getValue("CampaignProgress", "Difficulty", "Normal")
    );
    progress.currentMissionId = trimAscii(
        ini.getValue("CampaignProgress", "CurrentMission", "")
    );
    progress.campaignComplete = lowerAscii(
        ini.getValue("CampaignProgress", "Complete", "no")
    ) == "yes";
    progress.completedMissionIds = splitCsv(
        ini.getValue("CampaignProgress", "Completed", "")
    );

    for (const std::string& token : splitCsv(
             ini.getValue("CampaignProgress", "Attempts", "")
         )) {
        const std::size_t separator = token.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        int attempts = 0;
        if (parseInt(token.substr(separator + 1), attempts)) {
            progress.attemptsByMission[
                lowerAscii(trimAscii(token.substr(0, separator)))
            ] = std::max(0, attempts);
        }
    }
    return true;
}

bool writeCampaignProgress(
    std::ostream& output,
    const CampaignProgressData& progress,
    std::string& errorMessage
) {
    errorMessage.clear();
    output << "[CampaignProgress]\n";
    output << "Version=1\n";
    output << "Campaign=" << progress.campaignId << "\n";
    output << "Difficulty=" << progress.difficulty << "\n";
    output << "CurrentMission=" << progress.currentMissionId << "\n";
    output << "Complete=" << (progress.campaignComplete ? "yes" : "no") << "\n";
    output << "Completed=" << joinCsv(progress.completedMissionIds) << "\n";
    output << "Attempts=";
    bool first = true;
    for (const auto& [missionId, attempts] : progress.attemptsByMission) {
        if (!first) {
            output << ',';
        }
        first = false;
        output << missionId << ':' << std::max(0, attempts);
    }
    output << "\n";
    if (!output.good()) {
        errorMessage = "campaign progress write failed";
        return false;
    }
    return true;
}

bool saveCampaignProgress(
    const std::string& localDataRoot,
    const CampaignProgressData& progress,
    std::string& errorMessage
) {
    errorMessage.clear();
    const fs::path path(campaignProgressPath(localDataRoot));
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        errorMessage = "could not create campaign progress directory: "
            + error.message();
        return false;
    }

    return runtimeWriteFileAtomically(
        path,
        [&progress](std::ostream& output, std::string& writerError) {
            return writeCampaignProgress(output, progress, writerError);
        },
        errorMessage
    );
}

bool inspectCampaignSave(
    const std::string& path,
    CampaignSaveInfo& info
) {
    info = CampaignSaveInfo{};
    info.path = path;
    IniFile ini;
    if (!ini.loadFromFile(path)) {
        info.error = "save file could not be read";
        return false;
    }
    const std::string mode = lowerAscii(trimAscii(
        ini.getValue("Campaign", "Mode", "")
    ));
    const bool identitiesDecoded = readEncodedSaveField(
        ini,
        "Campaign",
        "CampaignHex",
        "Campaign",
        info.campaignId
    ) && readEncodedSaveField(
        ini,
        "Campaign",
        "MissionHex",
        "Mission",
        info.missionId
    ) && readEncodedSaveField(
        ini,
        "RuntimeSave",
        "MapHex",
        "Map",
        info.mapPath
    );
    info.difficulty = trimAscii(
        ini.getValue("Campaign", "Difficulty", "Normal")
    );
    parseInt(ini.getValue("RuntimeSave", "Tick", "0"), info.tick);
    if (!identitiesDecoded
        || mode != "campaignmission"
        || info.campaignId.empty()
        || info.missionId.empty()
        || info.mapPath.empty()) {
        info.error = "not a campaign mission save";
        return false;
    }
    info.valid = true;
    return true;
}

std::vector<CampaignSaveInfo> listCampaignSaves(
    const std::string& localDataRoot
) {
    std::vector<CampaignSaveInfo> saves;
    const fs::path directory = fs::path(rootOrDefault(localDataRoot))
        / "campaign_saves";
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        return saves;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(
             directory,
             fs::directory_options::skip_permission_denied,
             error
         )) {
        if (error || !entry.is_regular_file(error)
            || lowerAscii(entry.path().extension().string()) != ".ini") {
            continue;
        }
        CampaignSaveInfo info;
        if (inspectCampaignSave(entry.path().string(), info) && info.valid) {
            saves.push_back(std::move(info));
        }
    }
    std::sort(
        saves.begin(),
        saves.end(),
        [](const CampaignSaveInfo& left, const CampaignSaveInfo& right) {
            if (lowerAscii(left.campaignId) != lowerAscii(right.campaignId)) {
                return lowerAscii(left.campaignId) < lowerAscii(right.campaignId);
            }
            if (left.tick != right.tick) {
                return left.tick > right.tick;
            }
            return lowerAscii(left.path) < lowerAscii(right.path);
        }
    );
    return saves;
}

bool inspectSkirmishSave(
    const std::string& path,
    SkirmishSaveInfo& info
) {
    info = SkirmishSaveInfo{};
    info.path = path;
    const fs::path savePath(path);
    std::error_code fileError;
    constexpr std::uintmax_t maximumSaveBytes = 128U * 1024U * 1024U;
    if (lowerAscii(savePath.extension().string()) != ".ini"
        || !fs::is_regular_file(savePath, fileError)
        || fileError) {
        info.error = "skirmish save path is not a regular INI file";
        return false;
    }
    const std::uintmax_t saveBytes = fs::file_size(savePath, fileError);
    if (fileError || saveBytes > maximumSaveBytes) {
        info.error = fileError
            ? "skirmish save size could not be read"
            : "skirmish save exceeds the 128 MiB inspection limit";
        return false;
    }
    IniFile ini;
    if (!ini.loadFromFile(path)) {
        info.error = "save file could not be read";
        return false;
    }

    if (!parseInt(
            ini.getValue("RuntimeSave", "Version", "0"),
            info.saveVersion
        )
        || info.saveVersion < 1
        || info.saveVersion > 14) {
        info.error = "unsupported runtime save version";
        return false;
    }
    if (ini.getSection("Campaign") != nullptr) {
        info.error = "save is a campaign mission";
        return false;
    }
    if (!readEncodedSaveField(
            ini,
            "RuntimeSave",
            "MapHex",
            "Map",
            info.mapPath
        )
        || info.mapPath.empty()) {
        info.error = "skirmish save has no valid map identity";
        return false;
    }
    if (!parseInt(ini.getValue("RuntimeSave", "Tick", "0"), info.tick)
        || info.tick < 0) {
        info.error = "skirmish save has an invalid tick";
        return false;
    }

    const IniFile::Section* session = ini.getSection("SkirmishSession");
    int schema = 0;
    if (session == nullptr
        || !parseInt(
            ini.getValue("SkirmishSession", "Schema", "0"),
            schema
        )
        || schema != 1) {
        info.error = "legacy skirmish save has no main-menu startup context";
        return false;
    }

    const bool identitiesDecoded = readEncodedSaveField(
        ini,
        "SkirmishSession",
        "GameModeHex",
        "GameMode",
        info.gameMode
    ) && readEncodedSaveField(
        ini,
        "SkirmishSession",
        "GameModeRulesOverrideHex",
        "GameModeRulesOverride",
        info.gameModeRulesOverride
    ) && readEncodedSaveField(
        ini,
        "SkirmishSession",
        "PlayerCountryHex",
        "PlayerCountry",
        info.playerCountry
    ) && readEncodedSaveField(
        ini,
        "SkirmishSession",
        "PlayerColorHex",
        "PlayerColor",
        info.playerColor
    );
    if (!identitiesDecoded
        || info.gameMode.empty()
        || info.playerCountry.empty()
        || info.playerColor.empty()) {
        info.error = "skirmish startup identity is incomplete";
        return false;
    }
    if (lowerAscii(trimAscii(info.gameMode)) == "campaignmission") {
        info.error = "campaign mode cannot be restored as a skirmish save";
        return false;
    }

    int slotCount = -1;
    if (!parseInt(
            ini.getValue("SkirmishSession", "FixedTickHz", "0"),
            info.fixedTickHz
        )
        || info.fixedTickHz < 1
        || info.fixedTickHz > 240
        || !parseInt(
            ini.getValue("SkirmishSession", "GameSpeed", "-1"),
            info.gameSpeed
        )
        || info.gameSpeed < 0
        || info.gameSpeed > 6
        || !parseInt(
            ini.getValue("SkirmishSession", "TechLevel", "0"),
            info.techLevel
        )
        || info.techLevel < 1
        || info.techLevel > 10
        || !parseInt(
            ini.getValue("SkirmishSession", "StartingCredits", "-1"),
            info.startingCredits
        )
        || info.startingCredits < 0
        || info.startingCredits > 1000000000
        || !parseInt(
            ini.getValue("SkirmishSession", "StartingUnits", "-1"),
            info.startingUnits
        )
        || info.startingUnits < 0
        || info.startingUnits > 50
        || !parseBool(
            ini.getValue("SkirmishSession", "ShortGame", ""),
            info.shortGame
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "McvRepacks", ""),
            info.mcvRepacks
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "Crates", ""),
            info.crates
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "Superweapons", ""),
            info.superweapons
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "BuildOffAlly", ""),
            info.buildOffAlly
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "ComputerAI", "yes"),
            info.computerAiEnabled
        )
        || !parseBool(
            ini.getValue("SkirmishSession", "NoCooldown", "no"),
            info.noCooldown
        )
        || !parseInt(
            ini.getValue("SkirmishSession", "SlotCount", "-1"),
            slotCount
        )
        || slotCount < 0
        || slotCount > 8) {
        info.error = "skirmish startup settings are invalid";
        return false;
    }

    info.players.reserve(static_cast<std::size_t>(slotCount));
    bool enabledHuman = false;
    std::unordered_set<std::string> enabledColors;
    std::unordered_set<int> enabledStartPositions;
    for (int index = 0; index < slotCount; ++index) {
        const std::vector<std::string> fields = splitFields(
            ini.getValue("SkirmishSession", std::to_string(index), "")
        );
        SkirmishSavePlayerInfo player;
        if (fields.size() != 8U
            || !parseBool(fields[0], player.enabled)
            || !parseBool(fields[1], player.human)
            || !decodeHexField(fields[2], player.name)
            || !decodeHexField(fields[3], player.country)
            || !decodeHexField(fields[4], player.color)
            || !decodeHexField(fields[5], player.team)
            || !decodeHexField(fields[6], player.difficulty)
            || !parseInt(fields[7], player.startPosition)
            || player.startPosition < 0
            || player.startPosition > 8
            || (player.enabled
                && (trimAscii(player.country).empty()
                    || trimAscii(player.color).empty()
                    || trimAscii(player.difficulty).empty()))) {
            info.error = "skirmish player slot metadata is invalid";
            return false;
        }
        const std::string normalizedColor = lowerAscii(trimAscii(player.color));
        if (player.enabled
            && normalizedColor != "random"
            && !enabledColors.insert(normalizedColor).second) {
            info.error = "skirmish player colors are not unique";
            return false;
        }
        if (player.enabled
            && !runtimeObserverSlot(player)
            && player.startPosition > 0
            && !enabledStartPositions.insert(player.startPosition).second) {
            info.error = "skirmish start positions are not unique";
            return false;
        }
        enabledHuman = enabledHuman || (player.enabled && player.human);
        info.players.push_back(std::move(player));
    }
    // SlotCount=0 is an intentional compatibility path for command-line
    // single-player sessions, whose authoritative owner/country is carried by
    // PlayerCountry. Lobby-created sessions always contain an enabled human.
    if (!info.players.empty() && !enabledHuman) {
        info.error = "skirmish startup context has no enabled human slot";
        return false;
    }

    info.valid = true;
    return true;
}

std::vector<SkirmishSaveInfo> listSkirmishSaves(
    const std::string& localDataRoot
) {
    std::vector<SkirmishSaveInfo> saves;
    const fs::path path(skirmishSavePath(localDataRoot));
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        return saves;
    }
    SkirmishSaveInfo info;
    if (inspectSkirmishSave(path.string(), info) && info.valid) {
        saves.push_back(std::move(info));
    }
    return saves;
}

void markCampaignMissionResult(
    CampaignProgressData& progress,
    const std::string& campaignId,
    const std::string& missionId,
    const std::string& difficulty,
    const std::string& nextMissionId,
    bool victory
) {
    progress.campaignId = campaignId;
    progress.difficulty = difficulty.empty() ? "Normal" : difficulty;
    const std::string normalizedMission = lowerAscii(trimAscii(missionId));
    progress.attemptsByMission[normalizedMission] += 1;
    if (victory) {
        if (!containsMission(progress.completedMissionIds, normalizedMission)) {
            progress.completedMissionIds.push_back(normalizedMission);
        }
        progress.currentMissionId = lowerAscii(trimAscii(nextMissionId));
        progress.campaignComplete = progress.currentMissionId.empty();
    } else {
        progress.currentMissionId = normalizedMission;
        progress.campaignComplete = false;
    }
}
