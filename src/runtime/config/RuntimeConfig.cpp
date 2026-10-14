#include "runtime/config/RuntimeConfig.h"

#include "core/io/IniFile.h"
#include "runtime/campaign/RuntimeCampaignDataSource.h"
#include "ui/input/RuntimePlayerCommands.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Keep the original configuration parsing and path precedence. These small
// private helpers deliberately retain RuntimeShell's existing semantics;
// unrelated runtime parsing is not changed by this module extraction.

int parsePositiveInt(const std::string& value, int fallback) {
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string chooseString(const std::string& overrideValue, const std::string& value) {
    return overrideValue.empty() ? value : overrideValue;
}

int choosePositiveInt(int overrideValue, int value) {
    return overrideValue > 0 ? overrideValue : value;
}

bool pathExists(const std::filesystem::path& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode);
}

bool isRuntimeProjectRoot(const std::filesystem::path& path) {
    return pathExists(path / "local_data")
        && pathExists(path / "CMakeLists.txt")
        && (pathExists(path / "RedAlert2") || pathExists(path / ".git"));
}

void addPathAncestors(
    std::vector<std::filesystem::path>& candidates,
    std::filesystem::path path
) {
    if (path.empty()) {
        return;
    }

    std::error_code errorCode;
    path = std::filesystem::weakly_canonical(path, errorCode);
    if (errorCode) {
        path = path.lexically_normal();
    }

    while (!path.empty()) {
        candidates.push_back(path);
        const std::filesystem::path parent = path.parent_path();
        if (parent.empty() || parent == path) {
            break;
        }
        path = parent;
    }
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );

    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );
    }

    if (length == 0) {
        return std::filesystem::current_path();
    }

    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path discoverRuntimeProjectRoot() {
    std::vector<std::filesystem::path> candidates;

    std::error_code errorCode;
    addPathAncestors(candidates, std::filesystem::current_path(errorCode));
    addPathAncestors(candidates, executableDirectory());

    for (const std::filesystem::path& candidate : candidates) {
        if (isRuntimeProjectRoot(candidate)) {
            return candidate;
        }
    }

    if (!candidates.empty()) {
        return candidates.front();
    }

    return std::filesystem::current_path();
}

std::string pathString(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, errorCode);
    return (errorCode ? path.lexically_normal() : canonical).generic_string();
}

std::string resolveRuntimePath(
    const std::string& value,
    const std::filesystem::path& projectRoot
) {
    if (value.empty()) {
        return "";
    }

    const std::filesystem::path path(value);
    if (path.is_absolute()) {
        return pathString(path);
    }

    const std::filesystem::path currentCandidate =
        std::filesystem::current_path() / path;
    if (pathExists(currentCandidate)) {
        return pathString(currentCandidate);
    }

    const std::filesystem::path projectCandidate = projectRoot / path;
    return pathString(projectCandidate);
}

std::string toLowerAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

std::string trimAsciiLocal(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

bool parseRuntimeInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trimAsciiLocal(value), &consumed);
        if (consumed != trimAsciiLocal(value).size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

int parseRuntimeIntOrDefault(const std::string& value, int fallback) {
    int parsed = fallback;
    return parseRuntimeInt(value, parsed) ? parsed : fallback;
}

bool parseRuntimeBool(const std::string& value) {
    const std::string normalized = toLowerAsciiLocal(trimAsciiLocal(value));
    return normalized == "yes"
        || normalized == "true"
        || normalized == "1";
}

int parseRuntimePercent(const std::string& value, int fallback) {
    const std::string trimmed = trimAsciiLocal(value);
    if (trimmed.empty()) {
        return std::clamp(fallback, 0, 100);
    }
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(trimmed, &consumed);
        if (consumed != trimmed.size() || !std::isfinite(parsed)) {
            return std::clamp(fallback, 0, 100);
        }
        // Accept both Concord's integer percentages and the retail ra2.ini
        // 0.0..1.0 volume representation for hand-copied preferences.
        const double percent = parsed >= 0.0 && parsed <= 1.0
            && trimmed.find('.') != std::string::npos
            ? parsed * 100.0
            : parsed;
        return std::clamp(
            static_cast<int>(std::lround(percent)),
            0,
            100
        );
    } catch (...) {
        return std::clamp(fallback, 0, 100);
    }
}

} // namespace

RuntimeConfig loadRuntimeConfig(const RuntimeConfigOverrides& overrides) {
    RuntimeConfig config;
    const std::filesystem::path projectRoot = discoverRuntimeProjectRoot();
    config.configPath = resolveRuntimePath(
        chooseString(overrides.configPath, config.configPath),
        projectRoot
    );

    std::error_code errorCode;
    if (std::filesystem::exists(config.configPath, errorCode)) {
        IniFile ini;
        if (ini.loadFromFile(config.configPath)) {
            config.resourceRootPath =
                ini.getValue("Runtime", "ResourceRoot", config.resourceRootPath);
            config.localDataRootPath =
                ini.getValue("Runtime", "LocalDataRoot", config.localDataRootPath);
            config.modRootPath =
                ini.getValue("Runtime", "ModRoot", config.modRootPath);
            config.defaultMapPath =
                ini.getValue("Runtime", "DefaultMap", config.defaultMapPath);
            config.windowWidth = parsePositiveInt(
                ini.getValue("Runtime", "WindowWidth", ""),
                config.windowWidth
            );
            config.windowHeight = parsePositiveInt(
                ini.getValue("Runtime", "WindowHeight", ""),
                config.windowHeight
            );
            config.fixedTickHz = parsePositiveInt(
                ini.getValue("Runtime", "FixedTickHz", ""),
                config.fixedTickHz
            );
            config.targetFps = parsePositiveInt(
                ini.getValue("Runtime", "TargetFps", ""),
                config.targetFps
            );
            config.showMainMenu = parseRuntimeBool(
                ini.getValue(
                    "Runtime",
                    "ShowMainMenu",
                    config.showMainMenu ? "yes" : "no"
                )
            );
            config.showSkirmishMenu = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "ShowSetup",
                    config.showSkirmishMenu ? "yes" : "no"
                )
            );
            config.gameMode = ini.getValue(
                "Skirmish",
                "GameMode",
                config.gameMode
            );
            config.gameModeRulesOverride = ini.getValue(
                "Skirmish",
                "GameModeRulesOverride",
                config.gameModeRulesOverride
            );
            config.playerCountry = ini.getValue(
                "Skirmish",
                "Country",
                config.playerCountry
            );
            config.playerColor = ini.getValue(
                "Skirmish",
                "Color",
                config.playerColor
            );
            config.startingCredits = parsePositiveInt(
                ini.getValue("Skirmish", "Credits", ""),
                config.startingCredits
            );
            config.startingUnits = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("Skirmish", "UnitCount", ""),
                    config.startingUnits
                )
            );
            config.techLevel = std::clamp(
                parseRuntimeIntOrDefault(
                    ini.getValue("Skirmish", "TechLevel", ""),
                    config.techLevel
                ),
                1,
                10
            );
            config.gameSpeed = std::clamp(
                parseRuntimeIntOrDefault(
                    ini.getValue("Skirmish", "GameSpeed", ""),
                    config.gameSpeed
                ),
                0,
                6
            );
            config.scrollRate = std::clamp(
                parseRuntimeIntOrDefault(
                    ini.getValue("Options", "ScrollRate", ""),
                    config.scrollRate
                ),
                0,
                6
            );
            config.visualDetail = std::clamp(
                parseRuntimeIntOrDefault(
                    ini.getValue("Options", "DetailLevel", ""),
                    config.visualDetail
                ),
                0,
                2
            );
            config.tooltipsEnabled = parseRuntimeBool(
                ini.getValue(
                    "Options",
                    "ToolTips",
                    config.tooltipsEnabled ? "yes" : "no"
                )
            );
            config.targetLinesEnabled = parseRuntimeBool(
                ini.getValue(
                    "Options",
                    "UnitActionLines",
                    config.targetLinesEnabled ? "yes" : "no"
                )
            );
            config.seeHiddenObjects = parseRuntimeBool(ini.getValue("Options",
                "SeeHiddenObjects", config.seeHiddenObjects ? "yes" : "no"));
            config.scrollCoastingEnabled = parseRuntimeBool(
                ini.getValue(
                    "Options",
                    "ScrollCoasting",
                    config.scrollCoastingEnabled ? "yes" : "no"
                )
            );
            config.musicVolume = parseRuntimePercent(
                ini.getValue("Audio", "MusicVolume", ""),
                config.musicVolume
            );
            config.soundVolume = parseRuntimePercent(
                ini.getValue("Audio", "SoundVolume", ""),
                config.soundVolume
            );
            config.voiceVolume = parseRuntimePercent(
                ini.getValue("Audio", "VoiceVolume", ""),
                config.voiceVolume
            );
            config.musicEnabled = parseRuntimeBool(
                ini.getValue(
                    "Audio",
                    "InGameMusic",
                    config.musicEnabled ? "yes" : "no"
                )
            );
            config.soundEffectsEnabled = parseRuntimeBool(
                ini.getValue(
                    "Audio",
                    "SoundEnabled",
                    config.soundEffectsEnabled ? "yes" : "no"
                )
            );
            std::vector<std::string> preferenceBindingIds = {
                "Options", "StopObject", "GuardObject", "ScatterObject", "DeployObject", "CenterView"
            };
            for (const auto& binding : runtimePlayerCommandBindings())
                preferenceBindingIds.push_back(binding.first);
            config.keyboardReferenceLayout = ini.getValue("Hotkey", "ReferenceLayout", "") == "20260913";
            config.keyBindings.clear();
            for (const std::string& bindingId : preferenceBindingIds) {
                const std::string raw = trimAsciiLocal(
                    ini.getValue("Hotkey", bindingId, "")
                );
                if (raw.empty()) {
                    continue;
                }
                const int keyCode = parseRuntimeIntOrDefault(raw, 0);
                if (runtimePlayerValidKeyCode(keyCode)) {
                    config.keyBindings.emplace_back(bindingId, keyCode);
                }
            }
            config.shortGame = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "ShortGame",
                    config.shortGame ? "yes" : "no"
                )
            );
            config.mcvRepacks = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "MCVRepacks",
                    config.mcvRepacks ? "yes" : "no"
                )
            );
            config.crates = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "Crates",
                    config.crates ? "yes" : "no"
                )
            );
            config.superweapons = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "Superweapons",
                    config.superweapons ? "yes" : "no"
                )
            );
            config.buildOffAlly = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "BuildOffAlly",
                    config.buildOffAlly ? "yes" : "no"
                )
            );
            config.computerAiEnabled = parseRuntimeBool(
                ini.getValue(
                    "Skirmish",
                    "ComputerAI",
                    config.computerAiEnabled ? "yes" : "no"
                )
            );
            config.noCooldown = parseRuntimeBool(ini.getValue("Skirmish", "NoCooldown", "no"));
            config.campaignId = ini.getValue(
                "Campaign",
                "Campaign",
                config.campaignId
            );
            config.campaignMissionId = ini.getValue(
                "Campaign",
                "Mission",
                config.campaignMissionId
            );
            config.campaignSourceId = normalizeRuntimeCampaignSourceId(
                ini.getValue(
                    "Campaign",
                    "SourceId",
                    config.campaignSourceId
                )
            );
            config.campaignDifficulty = ini.getValue(
                "Campaign",
                "Difficulty",
                config.campaignDifficulty
            );
            config.campaignPlayerHouse = ini.getValue(
                "Campaign",
                "PlayerHouse",
                config.campaignPlayerHouse
            );
            config.campaignNextMissionId = ini.getValue(
                "Campaign",
                "NextMission",
                config.campaignNextMissionId
            );
            config.campaignSavePath = ini.getValue(
                "Campaign",
                "SavePath",
                config.campaignSavePath
            );
            config.runtimeReplayEventOutputPath =
                ini.getValue(
                    "Runtime",
                    "RuntimeReplayEvents",
                    config.runtimeReplayEventOutputPath
                );
        }
    }

    config.resourceRootPath = resolveRuntimePath(
        chooseString(overrides.resourceRootPath, config.resourceRootPath),
        projectRoot
    );
    config.localDataRootPath = resolveRuntimePath(
        chooseString(overrides.localDataRootPath, config.localDataRootPath),
        projectRoot
    );
    config.modRootPath = resolveRuntimePath(
        chooseString(overrides.modRootPath, config.modRootPath),
        projectRoot
    );
    config.defaultMapPath = resolveRuntimePath(
        chooseString(overrides.defaultMapPath, config.defaultMapPath),
        projectRoot
    );
    config.campaignIntroVideoPath = resolveRuntimePath(
        config.campaignIntroVideoPath,
        projectRoot
    );
    config.campaignPostScoreVideoPath = resolveRuntimePath(
        config.campaignPostScoreVideoPath,
        projectRoot
    );
    config.campaignLoadSavePath = resolveRuntimePath(
        config.campaignLoadSavePath,
        projectRoot
    );
    config.campaignSavePath = resolveRuntimePath(
        config.campaignSavePath,
        projectRoot
    );
    config.windowWidth =
        choosePositiveInt(overrides.windowWidth, config.windowWidth);
    config.windowHeight =
        choosePositiveInt(overrides.windowHeight, config.windowHeight);
    config.fixedTickHz =
        choosePositiveInt(overrides.fixedTickHz, config.fixedTickHz);
    config.targetFps =
        choosePositiveInt(overrides.targetFps, config.targetFps);
    config.runtimeReplayEventOutputPath = resolveRuntimePath(
        chooseString(
            overrides.runtimeReplayEventOutputPath,
            config.runtimeReplayEventOutputPath
        ),
        projectRoot
    );

    return config;
}

