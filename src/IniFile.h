#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

std::string iniHexEncode(std::string_view value);
bool iniHexDecode(
    std::string_view encoded,
    std::string& value,
    std::size_t maximumDecodedSize = 16U * 1024U * 1024U
);

class IniFile {
public:
    using KeyValue = std::pair<std::string, std::string>;
    using Section = std::vector<KeyValue>;

    bool loadFromFile(const std::string& path);
    bool loadFromString(const std::string& text);
    void appendFrom(const IniFile& overlay);

    const Section* getSection(const std::string& sectionName) const;
    std::vector<std::string> getSectionNames() const;

    std::string getValue(
        const std::string& sectionName,
        const std::string& key,
        const std::string& defaultValue = ""
    ) const;

private:
    std::unordered_map<std::string, Section> sections_;

    static std::string trim(const std::string& text);
    static std::string removeComment(const std::string& line, bool stripSlashComments = true);
};
