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
    bool loadFromFile(const std::string& path);
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

std::uint32_t mixFileIdFromName(const std::string& fileName);
std::string formatMixId(std::uint32_t id);
