#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <span>
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

    // Compatibility entry: the narrow OS filename encoding is unchanged.
    // It is independent of the INI payload's encoding.
    bool loadFromFile(const std::string& path);
    // Opt-in opaque numeric records for a caller-owned section format.
    // Other keys/sections keep ordinary INI comments; options are not retained.
    bool loadFromFile(const std::string& path,
        std::span<const std::string_view> literalNumericSections);
    // Native filesystem path (UTF-16 on Windows, native bytes on POSIX).
    // UTF-8 callers construct the path explicitly from u8string/u8string_view.
    // Content still uses the original text-mode stream and byte parser.
    bool loadFromPath(const std::filesystem::path& path);
    // A consumer may retain its original diagnostic spelling/encoding. This
    // label is never used to open the file; the one-argument form reports UTF-8.
    bool loadFromPath(const std::filesystem::path& path, std::string_view diagnosticPath);
    bool loadFromString(const std::string& text);
    bool loadFromString(const std::string& text,
        std::span<const std::string_view> literalNumericSections);
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
    static bool literalNumericRecord(const std::string& line, const std::string& section,
        std::span<const std::string_view> literalNumericSections);
    static std::string removeComment(const std::string& line, bool stripSlashComments = true);
};
