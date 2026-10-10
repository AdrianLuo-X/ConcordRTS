#include "assets/archive/MixArchive.h"
#include "assets/archive/WestwoodCrypto.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace {

constexpr std::uint32_t MixFlagChecksum = 0x00010000;
constexpr std::uint32_t MixFlagEncryptedIndex = 0x00020000;
constexpr std::uint64_t ChecksumSize = 20;
constexpr std::uint64_t EntrySize = 12;

std::string trimString(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);

    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool readUInt16(std::ifstream& input, std::uint16_t& value) {
    unsigned char bytes[2] = {};
    input.read(reinterpret_cast<char*>(bytes), 2);

    if (!input) {
        return false;
    }

    value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0])
        | (static_cast<std::uint16_t>(bytes[1]) << 8)
    );
    return true;
}

bool readUInt32(std::ifstream& input, std::uint32_t& value) {
    unsigned char bytes[4] = {};
    input.read(reinterpret_cast<char*>(bytes), 4);

    if (!input) {
        return false;
    }

    value =
        static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool checkedAdd(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        return false;
    }

    result = a + b;
    return true;
}

bool checkedMul(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        return false;
    }

    result = a * b;
    return true;
}

bool readExactBytes(
    std::ifstream& input,
    std::uint64_t count,
    std::vector<std::uint8_t>& bytes
) {
    if (count > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    bytes.assign(static_cast<std::size_t>(count), 0);
    if (bytes.empty()) {
        return true;
    }

    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return input.gcount() == static_cast<std::streamsize>(bytes.size());
}

std::uint16_t readUInt16FromBytes(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U)
    );
}

std::uint32_t readUInt32FromBytes(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

char toUpperAscii(char value) {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - 'a' + 'A');
    }

    return value;
}

std::string normalizeMixFileName(const std::string& fileName) {
    std::string normalized = trimString(fileName);

    if (normalized.size() >= 2
        && ((normalized.front() == '"' && normalized.back() == '"')
            || (normalized.front() == '\'' && normalized.back() == '\''))
    ) {
        normalized = trimString(normalized.substr(1, normalized.size() - 2));
    }

    while (!normalized.empty()
        && (normalized.back() == '/' || normalized.back() == '\\')
    ) {
        normalized.pop_back();
    }

    const std::size_t separator = normalized.find_last_of("/\\");
    if (separator != std::string::npos) {
        normalized = normalized.substr(separator + 1);
    }

    return normalized;
}

bool hasFileExtension(const std::string& fileName) {
    const std::string normalized = normalizeMixFileName(fileName);
    const std::size_t dot = normalized.find_last_of('.');
    return dot != std::string::npos && dot + 1 < normalized.size();
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size()
    );
}

std::uint32_t updateCrc32(std::uint32_t crc, unsigned char value) {
    crc ^= static_cast<std::uint32_t>(value);

    for (int bit = 0; bit < 8; ++bit) {
        if ((crc & 1U) != 0U) {
            crc = (crc >> 1U) ^ 0xEDB88320U;
        } else {
            crc >>= 1U;
        }
    }

    return crc;
}

} // namespace

bool MixArchive::loadFromFile(const std::string& path) {
    return loadFromPath(std::filesystem::path(path));
}

bool MixArchive::loadFromPath(const std::filesystem::path& path) {
    filePath_ = path;
    path_ = pathToUtf8(path);
    errorMessage_.clear();
    flags_ = 0;
    fileCount_ = 0;
    dataSize_ = 0;
    fileSize_ = 0;
    dataStartOffset_ = 0;
    hasChecksum_ = false;
    hasEncryptedIndex_ = false;
    indexReadable_ = false;
    entries_.clear();

    std::ifstream input(filePath_, std::ios::binary);
    if (!input.is_open()) {
        errorMessage_ = "Failed to open MIX file: " + path_;
        return false;
    }

    try {
        fileSize_ = std::filesystem::file_size(filePath_);
    } catch (const std::exception& exception) {
        errorMessage_ = "Failed to read MIX file size: ";
        errorMessage_ += exception.what();
        return false;
    }

    if (!readUInt32(input, flags_)) {
        errorMessage_ = "Failed to read MIX flags.";
        return false;
    }

    const std::uint32_t knownFlags = MixFlagChecksum | MixFlagEncryptedIndex;
    const bool legacyUnflaggedHeader = (flags_ & ~knownFlags) != 0;

    if (legacyUnflaggedHeader) {
        const std::uint16_t fileCountLow =
            static_cast<std::uint16_t>(flags_ & 0xFFFFU);
        const std::uint16_t dataSizeLow =
            static_cast<std::uint16_t>((flags_ >> 16U) & 0xFFFFU);
        std::uint16_t dataSizeHigh = 0;
        if (!readUInt16(input, dataSizeHigh)) {
            errorMessage_ = "Failed to read legacy MIX data size.";
            return false;
        }

        flags_ = 0;
        fileCount_ = fileCountLow;
        dataSize_ =
            static_cast<std::uint32_t>(dataSizeLow)
            | (static_cast<std::uint32_t>(dataSizeHigh) << 16U);
        hasChecksum_ = false;
        hasEncryptedIndex_ = false;
    } else {
        hasChecksum_ = (flags_ & MixFlagChecksum) != 0;
        hasEncryptedIndex_ = (flags_ & MixFlagEncryptedIndex) != 0;
    }

    if ((flags_ & ~knownFlags) != 0) {
        errorMessage_ =
            "Unsupported MIX header flags. This reader currently supports RA2/YR-style "
            "flagged MIX files only.";
        return false;
    }

    std::vector<std::uint8_t> decryptedIndex;
    std::size_t entryOffset = 0;

    if (hasEncryptedIndex_) {
        constexpr std::uint64_t FlagBytes = 4;
        constexpr std::uint64_t KeyBlockBytes = 80;
        constexpr std::uint64_t BlockBytes = 8;

        std::vector<std::uint8_t> keyBlock;
        if (!readExactBytes(input, KeyBlockBytes, keyBlock)) {
            errorMessage_ = "Failed to read MIX encrypted index key block.";
            return false;
        }

        std::uint64_t encryptedIndexOffset = 0;
        if (!checkedAdd(FlagBytes, KeyBlockBytes, encryptedIndexOffset)) {
            errorMessage_ = "MIX encrypted index offset overflow.";
            return false;
        }

        std::uint64_t firstEncryptedBlockEnd = 0;
        if (!checkedAdd(encryptedIndexOffset, BlockBytes, firstEncryptedBlockEnd)
            || firstEncryptedBlockEnd > fileSize_) {
            errorMessage_ = "MIX encrypted index first block points beyond the end of the file.";
            return false;
        }

        input.seekg(static_cast<std::streamoff>(encryptedIndexOffset), std::ios::beg);
        if (!input) {
            errorMessage_ = "Failed to seek to MIX encrypted index.";
            return false;
        }

        std::vector<std::uint8_t> firstEncryptedBlock;
        if (!readExactBytes(input, BlockBytes, firstEncryptedBlock)) {
            errorMessage_ = "Failed to read first MIX encrypted index block.";
            return false;
        }

        std::vector<std::uint8_t> firstDecryptedBlock;
        if (!westwood::decryptMixIndexBlocks(
                keyBlock,
                firstEncryptedBlock,
                firstDecryptedBlock,
                errorMessage_
            )) {
            return false;
        }

        if (firstDecryptedBlock.size() < 6U) {
            errorMessage_ = "MIX encrypted index first block decrypted too short.";
            return false;
        }

        fileCount_ = readUInt16FromBytes(firstDecryptedBlock, 0);

        std::uint64_t indexPayloadBytes = 0;
        if (!checkedMul(static_cast<std::uint64_t>(fileCount_), EntrySize, indexPayloadBytes)) {
            errorMessage_ = "MIX encrypted index entry count overflow.";
            return false;
        }

        // Directory plaintext is 2 bytes count, 4 bytes data size, and 12 bytes per
        // entry. Encrypted data is padded to Blowfish's 8 byte block size.
        std::uint64_t plaintextHeaderBytes = 0;
        if (!checkedAdd(6, indexPayloadBytes, plaintextHeaderBytes)) {
            errorMessage_ = "MIX encrypted index plaintext size overflow.";
            return false;
        }

        const std::uint64_t encryptedIndexBytes =
            ((plaintextHeaderBytes + (BlockBytes - 1U)) / BlockBytes) * BlockBytes;

        std::uint64_t encryptedIndexEnd = 0;
        if (!checkedAdd(encryptedIndexOffset, encryptedIndexBytes, encryptedIndexEnd)
            || encryptedIndexEnd > fileSize_) {
            errorMessage_ = "MIX encrypted index points beyond the end of the file.";
            return false;
        }

        input.seekg(static_cast<std::streamoff>(encryptedIndexOffset), std::ios::beg);
        if (!input) {
            errorMessage_ = "Failed to seek to full MIX encrypted index.";
            return false;
        }

        std::vector<std::uint8_t> encryptedIndex;
        if (!readExactBytes(input, encryptedIndexBytes, encryptedIndex)) {
            errorMessage_ = "Failed to read complete MIX encrypted index.";
            return false;
        }

        if (!westwood::decryptMixIndexBlocks(
                keyBlock,
                encryptedIndex,
                decryptedIndex,
                errorMessage_
            )) {
            return false;
        }

        if (decryptedIndex.size() < plaintextHeaderBytes) {
            errorMessage_ = "MIX encrypted index decrypted smaller than header.";
            return false;
        }

        dataSize_ = readUInt32FromBytes(decryptedIndex, 2);
        entryOffset = 6;
        dataStartOffset_ = encryptedIndexEnd;
    } else {
        const std::uint64_t headerSize = legacyUnflaggedHeader ? 2 + 4 : 4 + 2 + 4;
        if (!legacyUnflaggedHeader) {
            if (!readUInt16(input, fileCount_)) {
                errorMessage_ = "Failed to read MIX file count.";
                return false;
            }

            if (!readUInt32(input, dataSize_)) {
                errorMessage_ = "Failed to read MIX data size.";
                return false;
            }
        }

        std::uint64_t indexSize = 0;
        if (!checkedMul(static_cast<std::uint64_t>(fileCount_), EntrySize, indexSize)) {
            errorMessage_ = "MIX index entry count overflow.";
            return false;
        }

        if (!checkedAdd(headerSize, indexSize, dataStartOffset_)) {
            errorMessage_ = "MIX data start offset overflow.";
            return false;
        }

        if (dataStartOffset_ > fileSize_) {
            errorMessage_ = "MIX index points beyond the end of the file.";
            return false;
        }
    }

    const std::uint64_t checksumSize = hasChecksum_ ? ChecksumSize : 0;

    std::uint64_t dataAreaEnd = 0;
    if (!checkedAdd(dataStartOffset_, dataSize_, dataAreaEnd)
        || dataAreaEnd > fileSize_) {
        errorMessage_ = "MIX data area points beyond the end of the file.";
        return false;
    }

    std::uint64_t archiveEnd = 0;
    if (hasChecksum_
        && (!checkedAdd(dataAreaEnd, checksumSize, archiveEnd) || archiveEnd > fileSize_)) {
        errorMessage_ = "MIX checksum points beyond the end of the file.";
        return false;
    }

    entries_.reserve(fileCount_);

    for (std::uint16_t i = 0; i < fileCount_; ++i) {
        MixEntry entry;

        if (hasEncryptedIndex_) {
            if (entryOffset + EntrySize > decryptedIndex.size()) {
                errorMessage_ = "MIX encrypted directory ended before all entries.";
                return false;
            }

            entry.id = readUInt32FromBytes(decryptedIndex, entryOffset);
            entry.offset = readUInt32FromBytes(decryptedIndex, entryOffset + 4U);
            entry.size = readUInt32FromBytes(decryptedIndex, entryOffset + 8U);
            entryOffset += static_cast<std::size_t>(EntrySize);
        } else {
            if (!readUInt32(input, entry.id)
                || !readUInt32(input, entry.offset)
                || !readUInt32(input, entry.size)) {
                errorMessage_ = "Failed to read MIX directory entry.";
                return false;
            }
        }

        if (!checkedAdd(dataStartOffset_, entry.offset, entry.absoluteOffset)) {
            errorMessage_ = "MIX entry offset overflow.";
            return false;
        }

        std::uint64_t entryEnd = 0;
        if (!checkedAdd(entry.absoluteOffset, entry.size, entryEnd) || entryEnd > fileSize_) {
            errorMessage_ = "MIX entry points beyond the end of the file.";
            return false;
        }

        std::uint64_t relativeEntryEnd = 0;
        if (!checkedAdd(entry.offset, entry.size, relativeEntryEnd)
            || relativeEntryEnd > dataSize_) {
            errorMessage_ = "MIX entry points beyond the declared data area.";
            return false;
        }

        entries_.push_back(entry);
    }

    indexReadable_ = true;
    return true;
}

const std::string& MixArchive::getPath() const {
    return path_;
}

std::uint32_t MixArchive::getFlags() const {
    return flags_;
}

std::uint16_t MixArchive::getFileCount() const {
    return fileCount_;
}

std::uint32_t MixArchive::getDataSize() const {
    return dataSize_;
}

std::uint64_t MixArchive::getFileSize() const {
    return fileSize_;
}

std::uint64_t MixArchive::getDataStartOffset() const {
    return dataStartOffset_;
}

bool MixArchive::hasChecksum() const {
    return hasChecksum_;
}

bool MixArchive::hasEncryptedIndex() const {
    return hasEncryptedIndex_;
}

bool MixArchive::isIndexReadable() const {
    return indexReadable_;
}

const std::string& MixArchive::getErrorMessage() const {
    return errorMessage_;
}

const std::vector<MixEntry>& MixArchive::getEntries() const {
    return entries_;
}

const MixEntry* MixArchive::findEntryById(std::uint32_t id) const {
    const MixEntry* firstMatch = nullptr;

    for (const MixEntry& entry : entries_) {
        if (entry.id == id) {
            if (firstMatch == nullptr) {
                firstMatch = &entry;
            }

            if (entry.size > 0) {
                return &entry;
            }
        }
    }

    return firstMatch;
}

const MixEntry* MixArchive::findEntryByName(const std::string& fileName) const {
    const std::string normalized = normalizeMixFileName(fileName);
    if (normalized.empty()) {
        return nullptr;
    }

    const MixEntry* entry = findEntryById(mixFileIdFromName(normalized));
    if (entry != nullptr || hasFileExtension(fileName)) {
        return entry;
    }

    static const char* extensionCandidates[] = {
        ".shp",
        ".vxl",
        ".hva",
        ".tmp",
        ".pal",
        ".ini",
        ".map",
        ".mpr",
        ".mmx",
        ".yrm",
        ".yro",
        ".yrx",
        ".tem",
        ".sno",
        ".urb",
        ".ubn",
        ".des",
        ".lun",
        ".pcx",
        ".fnt",
        ".csf",
        ".wav",
        ".aud"
    };

    for (const char* extension : extensionCandidates) {
        entry = findEntryById(mixFileIdFromName(normalized + extension));
        if (entry != nullptr) {
            return entry;
        }
    }

    return nullptr;
}

bool MixArchive::readEntryData(
    const MixEntry& entry,
    std::vector<std::uint8_t>& data,
    std::string& errorMessage
) const {
    errorMessage.clear();
    data.clear();

    if (!indexReadable_) {
        errorMessage = "MIX index is not readable.";
        return false;
    }

    if (entry.size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        errorMessage = "MIX entry is too large to read on this platform.";
        return false;
    }

    std::uint64_t entryEnd = 0;
    std::uint64_t dataAreaEnd = 0;
    if (!checkedAdd(entry.absoluteOffset, entry.size, entryEnd)
        || !checkedAdd(dataStartOffset_, static_cast<std::uint64_t>(dataSize_), dataAreaEnd)
        || entry.absoluteOffset < dataStartOffset_
        || entryEnd > dataAreaEnd
        || entryEnd > fileSize_) {
        errorMessage = "MIX entry is outside the readable data area.";
        return false;
    }

    std::ifstream input(filePath_, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "Failed to open MIX file: " + path_;
        return false;
    }

    input.seekg(static_cast<std::streamoff>(entry.absoluteOffset), std::ios::beg);
    if (!input) {
        errorMessage = "Failed to seek to MIX entry data.";
        return false;
    }

    data.resize(entry.size);
    if (!data.empty()) {
        input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );

        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            errorMessage = "Failed to read complete MIX entry data.";
            return false;
        }
    }

    return true;
}

bool MixArchive::extractEntryToFile(
    const MixEntry& entry,
    const std::string& outputPath,
    std::string& errorMessage
) const {
    std::vector<std::uint8_t> data;
    if (!readEntryData(entry, data, errorMessage)) {
        return false;
    }

    const std::filesystem::path targetPath(outputPath);
    const std::filesystem::path parentPath = targetPath.parent_path();
    if (!parentPath.empty()) {
        std::error_code errorCode;
        std::filesystem::create_directories(parentPath, errorCode);
        if (errorCode) {
            errorMessage = "Failed to create output directory: ";
            errorMessage += errorCode.message();
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "Failed to open output file: " + outputPath;
        return false;
    }

    if (!data.empty()) {
        output.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
    }

    if (!output) {
        errorMessage = "Failed to write output file: " + outputPath;
        return false;
    }

    return true;
}

bool MixArchive::extractFileByName(
    const std::string& fileName,
    const std::string& outputPath,
    std::string& errorMessage
) const {
    const MixEntry* entry = findEntryByName(fileName);
    if (entry == nullptr) {
        errorMessage = "MIX file entry was not found: " + fileName;
        return false;
    }

    return extractEntryToFile(*entry, outputPath, errorMessage);
}

std::uint32_t mixFileIdFromName(const std::string& fileName) {
    const std::string normalizedFileName = normalizeMixFileName(fileName);
    std::string paddedName;
    paddedName.reserve(normalizedFileName.size() + 4);

    for (const char character : normalizedFileName) {
        paddedName.push_back(toUpperAscii(character));
    }

    const std::size_t wholeGroups = paddedName.size() >> 2U;
    if ((paddedName.size() & 3U) != 0U) {
        const std::size_t originalRemainder = paddedName.size() - (wholeGroups << 2U);
        paddedName.push_back(static_cast<char>(originalRemainder));

        int paddingCount = 3 - static_cast<int>((paddedName.size() - 1U) & 3U);
        while (paddingCount-- > 0) {
            paddedName.push_back(paddedName[wholeGroups << 2U]);
        }
    }

    std::uint32_t crc = 0xFFFFFFFFU;
    for (const unsigned char character : paddedName) {
        crc = updateCrc32(crc, character);
    }

    return ~crc;
}

std::string formatMixId(std::uint32_t id) {
    std::ostringstream output;
    output << "0x"
           << std::uppercase
           << std::hex
           << std::setw(8)
           << std::setfill('0')
           << id;
    return output.str();
}
