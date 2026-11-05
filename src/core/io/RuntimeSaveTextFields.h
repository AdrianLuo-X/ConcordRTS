#pragma once

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

// Original save-text operations shared by the host and domain codecs.
// Keep trimming, integer whole-token and permissive finite double-prefix rules.
namespace runtime_save_text {
inline std::string toLowerAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

inline std::string trimAsciiLocal(const std::string& value) {
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

inline std::vector<std::string> splitRuntimeSaveFields(const std::string& value) {
    std::vector<std::string> fields;
    std::string current;
    for (char character : value) {
        if (character == '|') {
            fields.push_back(trimAsciiLocal(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trimAsciiLocal(current));
    return fields;
}

inline std::string runtimeSaveField(std::string value) {
    for (char& character : value) {
        if (character == '|' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    return value;
}

inline bool parseRuntimeInt(const std::string& value, int& output) {
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

inline double parseRuntimeDoubleOrDefault(
    const std::string& value,
    double fallback
) {
    try {
        std::size_t parsed = 0;
        const double result = std::stod(trimAsciiLocal(value), &parsed);
        return parsed > 0 && std::isfinite(result) ? result : fallback;
    } catch (...) {
        return fallback;
    }
}
inline std::vector<std::string> splitRuntimeTokenList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char character : value) {
        if (character == ',' || character == ';') {
            const std::string token = trimAsciiLocal(current);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
        } else {
            current.push_back(character);
        }
    }

    const std::string token = trimAsciiLocal(current);
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

inline int parseRuntimeIntOrDefault(const std::string& value, int fallback) {
    int parsed = fallback;
    return parseRuntimeInt(value, parsed) ? parsed : fallback;
}

inline bool parseRuntimeBool(const std::string& value) {
    const std::string normalized = toLowerAsciiLocal(trimAsciiLocal(value));
    return normalized == "yes"
        || normalized == "true"
        || normalized == "1";
}

inline std::string runtimeBoolText(bool value) {
    return value ? "yes" : "no";
}
}
