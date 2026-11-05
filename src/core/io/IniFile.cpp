#include "core/io/IniFile.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsAsciiIgnoreCase(
    std::string_view left,
    std::string_view right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

bool isPackedMapSection(const std::string& sectionName) {
    return equalsAsciiIgnoreCase(sectionName, "IsoMapPack5")
        || equalsAsciiIgnoreCase(sectionName, "OverlayPack")
        || equalsAsciiIgnoreCase(sectionName, "OverlayDataPack")
        || equalsAsciiIgnoreCase(sectionName, "PreviewPack");
}

int iniHexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

} // namespace

std::string iniHexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(digits[(byte >> 4) & 0x0f]);
        encoded.push_back(digits[byte & 0x0f]);
    }
    return encoded;
}

bool iniHexDecode(
    std::string_view encoded,
    std::string& value,
    std::size_t maximumDecodedSize
) {
    if ((encoded.size() % 2U) != 0U
        || encoded.size() / 2U > maximumDecodedSize) {
        return false;
    }
    std::string decoded;
    decoded.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const int high = iniHexNibble(encoded[index]);
        const int low = iniHexNibble(encoded[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    value = std::move(decoded);
    return true;
}

std::string IniFile::trim(const std::string& text) {
    const char* whitespace = " \t\r\n";

    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const auto end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

std::string IniFile::removeComment(const std::string& line, bool stripSlashComments) {
    const auto semicolonPos = line.find(';');

    std::size_t commentPos = std::string::npos;

    if (semicolonPos != std::string::npos) {
        commentPos = semicolonPos;
    }

    const auto slashPos = stripSlashComments ? line.find("//") : std::string::npos;
    if (slashPos != std::string::npos) {
        if (commentPos == std::string::npos || slashPos < commentPos) {
            commentPos = slashPos;
        }
    }

    if (commentPos == std::string::npos) {
        return line;
    }

    return line.substr(0, commentPos);
}

bool IniFile::loadFromFile(const std::string& path) {
    return loadFromFile(path, {});
}

bool IniFile::loadFromFile(const std::string& path,
    std::span<const std::string_view> literalNumericSections) {
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cerr << "Failed to open INI file: " << path << "\n";
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str(), literalNumericSections);
}

bool IniFile::loadFromPath(const std::filesystem::path& path) {
    const std::u8string diagnostic = path.generic_u8string();
    return loadFromPath(path, std::string_view(
        reinterpret_cast<const char*>(diagnostic.data()), diagnostic.size()));
}

bool IniFile::loadFromPath(
    const std::filesystem::path& path,
    std::string_view diagnosticPath
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open INI file: " << diagnosticPath << "\n";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str());
}

bool IniFile::literalNumericRecord(const std::string& line, const std::string& section,
    std::span<const std::string_view> literalNumericSections) {
    if (literalNumericSections.empty()) return false;
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) return false;
    const std::string key = trim(line.substr(0, equals));
    if (key.empty()) return false;
    for (char character : key) {
        if (character < '0' || character > '9') return false;
    }
    for (std::string_view candidate : literalNumericSections) {
        if (equalsAsciiIgnoreCase(section, candidate)) return true;
    }
    return false;
}

bool IniFile::loadFromString(const std::string& text) {
    return loadFromString(text, {});
}

bool IniFile::loadFromString(const std::string& text,
    std::span<const std::string_view> literalNumericSections) {
    sections_.clear();

    std::istringstream file(text);
    std::string currentSection;
    std::string line;

    while (std::getline(file, line)) {
        const std::string sectionLine = trim(removeComment(line));

        if (sectionLine.empty()) {
            continue;
        }

        if (sectionLine.front() == '[' && sectionLine.back() == ']') {
            const std::string parsedSection =
                trim(sectionLine.substr(1, sectionLine.size() - 2));
            currentSection = parsedSection;
            for (const auto& [existingName, values] : sections_) {
                (void)values;
                if (equalsAsciiIgnoreCase(existingName, parsedSection)) {
                    currentSection = existingName;
                    break;
                }
            }
            sections_[currentSection];
            continue;
        }

        line = literalNumericRecord(line, currentSection, literalNumericSections)
            ? trim(line)
            : trim(removeComment(line, !isPackedMapSection(currentSection)));
        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        if (currentSection.empty()) {
            continue;
        }

        std::string key = trim(line.substr(0, equalsPos));
        std::string value = trim(line.substr(equalsPos + 1));

        sections_[currentSection].push_back({key, value});
    }

    return true;
}

void IniFile::appendFrom(const IniFile& overlay) {
    for (const auto& [overlayName, overlayValues] : overlay.sections_) {
        auto destination = sections_.end();
        for (auto it = sections_.begin(); it != sections_.end(); ++it) {
            if (equalsAsciiIgnoreCase(it->first, overlayName)) {
                destination = it;
                break;
            }
        }
        if (destination == sections_.end()) {
            sections_.emplace(overlayName, overlayValues);
            continue;
        }
        destination->second.insert(
            destination->second.end(),
            overlayValues.begin(),
            overlayValues.end()
        );
    }
}

const IniFile::Section* IniFile::getSection(const std::string& sectionName) const {
    const auto it = sections_.find(sectionName);
    if (it != sections_.end()) {
        return &it->second;
    }
    for (const auto& [existingName, section] : sections_) {
        if (equalsAsciiIgnoreCase(existingName, sectionName)) {
            return &section;
        }
    }
    return nullptr;
}

std::vector<std::string> IniFile::getSectionNames() const {
    std::vector<std::string> names;
    names.reserve(sections_.size());

    for (const auto& [sectionName, values] : sections_) {
        names.push_back(sectionName);
    }

    return names;
}

std::string IniFile::getValue(
    const std::string& sectionName,
    const std::string& key,
    const std::string& defaultValue
) const {
    const Section* section = getSection(sectionName);
    if (section == nullptr) {
        return defaultValue;
    }

    // 从后往前找。这样如果同一个 key 出现多次，后面的覆盖前面的。
    for (auto it = section->rbegin(); it != section->rend(); ++it) {
        if (equalsAsciiIgnoreCase(it->first, key)) {
            return it->second;
        }
    }

    return defaultValue;
}
