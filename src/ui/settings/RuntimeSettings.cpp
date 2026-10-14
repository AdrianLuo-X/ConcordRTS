#include "runtime/config/RuntimeConfig.h"

#include "core/io/RuntimeAtomicFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct PreferenceValue {
    std::string key;
    std::string value;
};

using PreferenceSections =
    std::vector<std::pair<std::string, std::vector<PreferenceValue>>>;

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) { return lowerAsciiCharacter(character); }
    );
    return value;
}

std::string trimAscii(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::string sectionNameFromLine(const std::string& source) {
    std::string line = trimAscii(source);
    if (line.size() >= 3U
        && static_cast<unsigned char>(line[0]) == 0xefU
        && static_cast<unsigned char>(line[1]) == 0xbbU
        && static_cast<unsigned char>(line[2]) == 0xbfU) {
        line.erase(0, 3U);
        line = trimAscii(line);
    }
    if (line.size() < 3U || line.front() != '[') {
        return {};
    }
    const std::size_t close = line.find(']');
    if (close == std::string::npos) {
        return {};
    }
    const std::string suffix = trimAscii(line.substr(close + 1U));
    if (!suffix.empty() && suffix.front() != ';') {
        return {};
    }
    return trimAscii(line.substr(1U, close - 1U));
}

std::string keyNameFromLine(const std::string& source) {
    const std::string line = trimAscii(source);
    if (line.empty() || line.front() == ';' || line.front() == '#') {
        return {};
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        return {};
    }
    return trimAscii(line.substr(0, equals));
}

std::vector<std::string> splitLines(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t newline = text.find('\n', begin);
        const std::size_t end = newline == std::string::npos
            ? text.size()
            : newline;
        std::string line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (newline == std::string::npos) {
            break;
        }
        begin = newline + 1U;
    }
    return lines;
}

void updateSection(
    std::vector<std::string>& lines,
    const std::string& sectionName,
    const std::vector<PreferenceValue>& values
) {
    const std::string wantedSection = lowerAscii(sectionName);
    std::size_t header = std::string::npos;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string parsed = sectionNameFromLine(lines[index]);
        if (!parsed.empty() && lowerAscii(parsed) == wantedSection) {
            // The INI reader gives later duplicate sections precedence, so
            // update the final occurrence for the same behavior.
            header = index;
        }
    }

    if (header == std::string::npos) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.emplace_back();
        }
        lines.push_back("[" + sectionName + "]");
        for (const PreferenceValue& value : values) {
            lines.push_back(value.key + "=" + value.value);
        }
        return;
    }

    std::size_t end = lines.size();
    for (std::size_t index = header + 1U; index < lines.size(); ++index) {
        if (!sectionNameFromLine(lines[index]).empty()) {
            end = index;
            break;
        }
    }

    std::vector<PreferenceValue> missing;
    for (const PreferenceValue& value : values) {
        const std::string wantedKey = lowerAscii(value.key);
        std::size_t keyLine = std::string::npos;
        for (std::size_t index = header + 1U; index < end; ++index) {
            const std::string parsed = keyNameFromLine(lines[index]);
            if (!parsed.empty() && lowerAscii(parsed) == wantedKey) {
                keyLine = index;
            }
        }
        if (keyLine == std::string::npos) {
            missing.push_back(value);
        } else {
            lines[keyLine] = value.key + "=" + value.value;
        }
    }

    lines.insert(
        lines.begin() + static_cast<std::ptrdiff_t>(end),
        missing.size(),
        std::string{}
    );
    for (std::size_t index = 0; index < missing.size(); ++index) {
        lines[end + index] = missing[index].key + "=" + missing[index].value;
    }
}

const char* boolText(bool value) {
    return value ? "yes" : "no";
}

PreferenceSections preferenceSections(const RuntimeConfig& config) {
    PreferenceSections sections{
        {
            "Runtime",
            {
                {"WindowWidth", std::to_string(std::max(1, config.windowWidth))},
                {"WindowHeight", std::to_string(std::max(1, config.windowHeight))},
                {"TargetFps", std::to_string(std::max(1, config.targetFps))}
            }
        },
        {
            "Skirmish",
            {
                {"GameSpeed", std::to_string(std::clamp(config.gameSpeed, 0, 6))}
            }
        },
        {
            "Options",
            {
                {"ScrollRate", std::to_string(std::clamp(config.scrollRate, 0, 6))},
                {"DetailLevel", std::to_string(std::clamp(config.visualDetail, 0, 2))},
                {"ToolTips", boolText(config.tooltipsEnabled)},
                {"UnitActionLines", boolText(config.targetLinesEnabled)},
                {"ScrollCoasting", boolText(config.scrollCoastingEnabled)},
                {"SeeHiddenObjects", boolText(config.seeHiddenObjects)}
            }
        },
        {
            "Audio",
            {
                {"MusicVolume", std::to_string(std::clamp(config.musicVolume, 0, 100))},
                {"SoundVolume", std::to_string(std::clamp(config.soundVolume, 0, 100))},
                {"VoiceVolume", std::to_string(std::clamp(config.voiceVolume, 0, 100))},
                {"InGameMusic", boolText(config.musicEnabled)},
                {"SoundEnabled", boolText(config.soundEffectsEnabled)}
            }
        }
    };

    if (!config.keyBindings.empty()) {
        std::vector<PreferenceValue> hotkeys;
        hotkeys.reserve(config.keyBindings.size());
        if (config.keyboardReferenceLayout)
            hotkeys.push_back({"ReferenceLayout", "20260913"});
        for (const auto& [id, keyCode] : config.keyBindings) {
            // keyboardmd.ini stores Shift/Ctrl/Alt in bits 8/9/10. Keep the
            // complete combination when saving a user's key assignment.
            if (!trimAscii(id).empty() && keyCode > 0
                && keyCode <= 0x7ff && (keyCode & 0xff) != 0) {
                hotkeys.push_back({id, std::to_string(keyCode)});
            }
        }
        if (!hotkeys.empty()) {
            sections.push_back({"Hotkey", std::move(hotkeys)});
        }
    }
    return sections;
}

} // namespace

static bool saveRuntimePreferenceSections(
    const RuntimeConfig& config,
    const PreferenceSections& sections,
    std::string& errorMessage
) {
    errorMessage.clear();
    const std::filesystem::path path(config.configPath);
    if (path.empty() || path.filename().empty()) {
        errorMessage = "runtime config path is empty or incomplete";
        return false;
    }

    std::string existing;
    std::error_code errorCode;
    if (std::filesystem::exists(path, errorCode)) {
        if (errorCode || !std::filesystem::is_regular_file(path, errorCode)) {
            errorMessage = "runtime config path is not a regular file";
            return false;
        }
        constexpr std::uintmax_t maximumConfigBytes = 4U * 1024U * 1024U;
        const std::uintmax_t fileSize = std::filesystem::file_size(
            path,
            errorCode
        );
        if (errorCode || fileSize > maximumConfigBytes) {
            errorMessage = errorCode
                ? "could not inspect runtime config size: "
                    + errorCode.message()
                : "runtime config exceeds the 4 MiB preference limit";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            errorMessage = "could not open runtime config for reading";
            return false;
        }
        existing.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
        if (input.bad()) {
            errorMessage = "could not read the complete runtime config";
            return false;
        }
    } else if (errorCode) {
        errorMessage = "could not inspect runtime config: "
            + errorCode.message();
        return false;
    }

    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    if (!std::filesystem::exists(parent, errorCode)) {
        errorCode.clear();
        if (!std::filesystem::create_directories(parent, errorCode)
            || errorCode) {
            errorMessage = "could not create runtime config directory: "
                + errorCode.message();
            return false;
        }
    } else if (errorCode || !std::filesystem::is_directory(parent, errorCode)) {
        errorMessage = "runtime config parent is not a directory";
        return false;
    }

    std::vector<std::string> lines = splitLines(existing);
    for (const auto& [section, values] : sections) {
        updateSection(lines, section, values);
    }

    return runtimeWriteFileAtomically(
        path,
        [&lines](std::ostream& output, std::string&) {
            for (const std::string& line : lines) {
                output << line << '\n';
            }
            return output.good();
        },
        errorMessage
    );
}

bool saveRuntimeConfigPreferences(
    const RuntimeConfig& config,
    std::string& errorMessage
) {
    return saveRuntimePreferenceSections(config, preferenceSections(config), errorMessage);
}

bool saveRuntimeSkirmishCratePreference(
    const RuntimeConfig& config,
    bool enabled,
    std::string& errorMessage
) {
    // Keep match settings separate from in-game Options: campaign defaults
    // and multiplayer room authority must not overwrite the offline choice.
    return saveRuntimePreferenceSections(
        config, {{"Skirmish", {{"Crates", boolText(enabled)}}}}, errorMessage);
}

bool saveRuntimeSkirmishNoCooldownPreference(
    const RuntimeConfig& config, bool enabled, std::string& errorMessage
) {
    return saveRuntimePreferenceSections(
        config, {{"Skirmish", {{"NoCooldown", boolText(enabled)}}}}, errorMessage);
}