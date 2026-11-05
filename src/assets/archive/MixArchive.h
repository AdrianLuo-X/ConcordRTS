#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MixEntry {
    std::uint32_t id = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint64_t absoluteOffset = 0;
};

class MixArchive {
public:
    // Compatibility filename in the native narrow OS encoding (ACP on Windows).
    // This is not a UTF-8 boundary or a MIX logical entry name.
    bool loadFromFile(const std::string& path);
    // Owns the native filesystem path for both index load and later entry reads.
    // UTF-8 callers explicitly construct a path from char8_t/u8string. getPath()
    // is a UTF-8 diagnostic label, never an input to reopen this archive.
    bool loadFromPath(const std::filesystem::path& path);

    const std::string& getPath() const;
    std::uint32_t getFlags() const;
    std::uint16_t getFileCount() const;
    std::uint32_t getDataSize() const;
    std::uint64_t getFileSize() const;
    std::uint64_t getDataStartOffset() const;
    bool hasChecksum() const;
    bool hasEncryptedIndex() const;
    bool isIndexReadable() const;
    const std::string& getErrorMessage() const;
    const std::vector<MixEntry>& getEntries() const;
    const MixEntry* findEntryById(std::uint32_t id) const;
    const MixEntry* findEntryByName(const std::string& fileName) const;
    bool readEntryData(
        const MixEntry& entry,
        std::vector<std::uint8_t>& data,
        std::string& errorMessage
    ) const;
    bool extractEntryToFile(
        const MixEntry& entry,
        const std::string& outputPath,
        std::string& errorMessage
    ) const;
    bool extractFileByName(
        const std::string& fileName,
        const std::string& outputPath,
        std::string& errorMessage
    ) const;

private:
    std::filesystem::path filePath_;
    std::string path_;
    std::string errorMessage_;
    std::uint32_t flags_ = 0;
    std::uint16_t fileCount_ = 0;
    std::uint32_t dataSize_ = 0;
    std::uint64_t fileSize_ = 0;
    std::uint64_t dataStartOffset_ = 0;
    bool hasChecksum_ = false;
    bool hasEncryptedIndex_ = false;
    bool indexReadable_ = false;
    std::vector<MixEntry> entries_;
};

// Logical MIX names retain the original byte rules: trim/quotes/basename,
// ASCII-only uppercasing and CRC padding. No filesystem or Unicode folding.
std::uint32_t mixFileIdFromName(const std::string& fileName);
std::string formatMixId(std::uint32_t id);
