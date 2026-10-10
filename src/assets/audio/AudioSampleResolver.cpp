#include "assets/audio/AudioSampleResolver.h"

#include "assets/audio/AudioDecoder.h"
#include "assets/archive/MixArchive.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

struct AudioBagEntry {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    int sampleRate = 0;
    std::uint32_t flags = 0;
    std::uint32_t chunkSize = 0;
};

struct AudioBagLibrary {
    std::string mixPath;
    std::uint64_t bagAbsoluteOffset = 0;
    std::uint32_t bagSize = 0;
    std::unordered_map<std::string, AudioBagEntry> entriesByName;
};

bool audioBagEntryUsesImaAdpcm(const AudioBagEntry& entry) {
    // Original audio.idx uses flags 6/7 for PCM and 12/13 for IMA-ADPCM;
    // bit 0 is the stereo flag and bit 3 selects the compressed codec.
    return (entry.flags & 0x08U) != 0U;
}

int audioBagEntryChannels(const AudioBagEntry& entry) {
    return (entry.flags & 0x01U) != 0U ? 2 : 1;
}

int audioBagEntryPcmBits(const AudioBagEntry& entry) {
    return (entry.flags & 0x02U) != 0U ? 16 : 8;
}

AudioDecodeResult decodeAudioBagEntry(
    const AudioBagEntry& entry,
    const std::vector<std::uint8_t>& sourceBytes
) {
    if (audioBagEntryUsesImaAdpcm(entry)) {
        return decodeImaAdpcmBagSampleToWave(
            sourceBytes,
            entry.sampleRate,
            audioBagEntryChannels(entry),
            entry.chunkSize
        );
    }

    AudioDecodeResult decoded;
    decoded.sourceFormat = "idx/bag pcm";
    decoded.sampleRate = entry.sampleRate;
    decoded.bitsPerSample = audioBagEntryPcmBits(entry);
    decoded.channels = audioBagEntryChannels(entry);
    const std::size_t frameBytes = static_cast<std::size_t>(
        decoded.channels * std::max(1, decoded.bitsPerSample / 8)
    );
    if (decoded.sampleRate <= 0 || frameBytes == 0U
        || sourceBytes.empty() || sourceBytes.size() % frameBytes != 0U) {
        decoded.errorMessage = "BAG PCM sample metadata or frame alignment is invalid.";
        return decoded;
    }
    decoded.pcmBytes = sourceBytes;
    decoded.wavBytes = makePcmWaveFile(
        decoded.sampleRate,
        decoded.bitsPerSample,
        decoded.channels,
        decoded.pcmBytes
    );
    decoded.success = !decoded.wavBytes.empty();
    if (!decoded.success) {
        decoded.errorMessage = "BAG PCM sample could not be wrapped as WAV.";
    }
    return decoded;
}

bool imaAdpcmBlockDecoderSelfTest() {
    const auto appendInt16 = [](
        std::vector<std::uint8_t>& bytes,
        std::int16_t value
    ) {
        const std::uint16_t bits = static_cast<std::uint16_t>(value);
        bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    };
    const auto pcmSample = [](
        const std::vector<std::uint8_t>& bytes,
        std::size_t sample
    ) {
        const std::size_t offset = sample * 2U;
        const std::uint16_t bits = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset])
            | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U)
        );
        return static_cast<std::int16_t>(bits);
    };

    std::vector<std::uint8_t> mono;
    for (const std::int16_t predictor : {std::int16_t{1000}, std::int16_t{-1000}}) {
        appendInt16(mono, predictor);
        mono.push_back(0U); // step index
        mono.push_back(0U); // reserved
        mono.insert(mono.end(), 4U, 0U);
    }
    const AudioDecodeResult monoDecoded = decodeImaAdpcmBagSampleToWave(
        mono,
        22050,
        1,
        8U
    );
    if (!monoDecoded.success || monoDecoded.pcmBytes.size() != 36U) {
        return false;
    }
    for (std::size_t sample = 0; sample < 18U; ++sample) {
        const std::int16_t expected = sample < 9U ? 1000 : -1000;
        if (pcmSample(monoDecoded.pcmBytes, sample) != expected) {
            return false;
        }
    }

    std::vector<std::uint8_t> stereo;
    appendInt16(stereo, 1000);
    stereo.push_back(0U);
    stereo.push_back(0U);
    appendInt16(stereo, -1000);
    stereo.push_back(0U);
    stereo.push_back(0U);
    stereo.insert(stereo.end(), 8U, 0U);
    const AudioDecodeResult stereoDecoded = decodeImaAdpcmBagSampleToWave(
        stereo,
        22050,
        2,
        16U
    );
    if (!stereoDecoded.success || stereoDecoded.pcmBytes.size() != 36U) {
        return false;
    }
    for (std::size_t frame = 0; frame < 9U; ++frame) {
        if (pcmSample(stereoDecoded.pcmBytes, frame * 2U) != 1000
            || pcmSample(stereoDecoded.pcmBytes, frame * 2U + 1U) != -1000) {
            return false;
        }
    }
    return true;
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

std::string trimAscii(const std::string& value) {
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

std::string normalizeAudioCampaignSourceId(const std::string& sourceId) {
    return lowerAscii(trimAscii(sourceId));
}

std::string audioSourceCacheKey(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    return lowerAscii(assetRootPath)
        + "|source="
        + normalizeAudioCampaignSourceId(campaignSourceId);
}

std::uint32_t readUInt32LE(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::string pathString(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, errorCode);
    return (errorCode ? path.lexically_normal() : canonical).generic_string();
}

bool existingRegularFile(const std::filesystem::path& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode)
        && std::filesystem::is_regular_file(path, errorCode);
}

std::string stripAudioComment(const std::string& value) {
    const std::size_t semicolon = value.find(';');
    const std::size_t slash = value.find("//");
    std::size_t comment = std::string::npos;
    if (semicolon != std::string::npos) {
        comment = semicolon;
    }
    if (slash != std::string::npos
        && (comment == std::string::npos || slash < comment)) {
        comment = slash;
    }
    return trimAscii(comment == std::string::npos ? value : value.substr(0, comment));
}

std::vector<std::string> splitWhitespace(const std::string& value) {
    std::vector<std::string> tokens;
    std::string normalized = stripAudioComment(value);
    for (char& character : normalized) {
        if (character == ',' || character == '|') {
            character = ' ';
        }
    }

    std::istringstream stream(normalized);
    std::string token;
    while (stream >> token) {
        token = trimAscii(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string stripAudioDecorators(std::string value) {
    value = trimAscii(stripAudioComment(value));
    while (value.size() >= 2U
        && ((value.front() == '"' && value.back() == '"')
            || (value.front() == '\'' && value.back() == '\''))) {
        value = trimAscii(value.substr(1U, value.size() - 2U));
    }
    while (!value.empty() && (value.front() == '$' || value.front() == '@')) {
        value.erase(value.begin());
        value = trimAscii(value);
    }
    return value;
}

bool isNullAudioSampleName(const std::string& value) {
    const std::string normalized = lowerAscii(trimAscii(value));
    return normalized.empty()
        || normalized == "none"
        || normalized == "<none>"
        || normalized == "null"
        || normalized == "dummy";
}

std::vector<std::string> sampleTokensForSection(
    const IniFile* ini,
    const std::string& sectionName
) {
    std::vector<std::string> samples;
    if (ini == nullptr) {
        return samples;
    }

    const IniFile::Section* section = ini->getSection(sectionName);
    if (section == nullptr) {
        return samples;
    }

    for (const auto& [key, value] : *section) {
        const std::string lowerKey = lowerAscii(trimAscii(key));
        if (lowerKey != "sounds"
            && lowerKey != "sound"
            && lowerKey != "samples"
            && lowerKey != "sample"
            && lowerKey != "file"
            && lowerKey != "files"
            && lowerKey != "russian"
            && lowerKey != "allied"
            && lowerKey != "yuri") {
            continue;
        }

        const std::vector<std::string> tokens = splitWhitespace(value);
        samples.insert(samples.end(), tokens.begin(), tokens.end());
    }

    return samples;
}

const std::vector<std::string>& audioMixPathCandidates(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    static std::unordered_map<std::string, std::vector<std::string>> cache;
    const std::string cacheKey = audioSourceCacheKey(
        assetRootPath,
        campaignSourceId
    );
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<std::string> paths;
    if (!assetRootPath.empty()) {
        const std::filesystem::path root(assetRootPath);
        const std::filesystem::path candidates[] = {
            root / "langmd" / "audiomd.mix",
            root / "langmd" / "audio.mix",
            root / "ra2md" / "localmd" / "audiomd.mix",
            root / "ra2md" / "localmd" / "audio.mix",
            root / "expandmd01" / "audiomd.mix",
            root / "expandmd01" / "audio.mix",
            root / "language" / "audio.mix",
            root / "language" / "audiomd.mix",
            root / "audiomd.mix",
            root / "audio.mix",
            root / "ra2md" / "audiomd.mix",
            root / "ra2" / "audio.mix"
        };

        const std::string sourceId =
            normalizeAudioCampaignSourceId(campaignSourceId);

        std::set<std::string> seen;
        for (const std::filesystem::path& candidate : candidates) {
            const std::string fileName = lowerAscii(
                candidate.filename().string()
            );
            if ((sourceId == "ra2" && fileName != "audio.mix")
                || (sourceId == "yr" && fileName != "audiomd.mix")
                || (!sourceId.empty()
                    && sourceId != "ra2"
                    && sourceId != "yr")) {
                continue;
            }
            if (!existingRegularFile(candidate)) {
                continue;
            }

            const std::string path = pathString(candidate);
            if (seen.insert(lowerAscii(path)).second) {
                paths.push_back(path);
            }
        }
    }

    auto [inserted, _] = cache.emplace(cacheKey, std::move(paths));
    return inserted->second;
}

bool readMixFileData(
    const MixArchive& archive,
    const std::string& fileName,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    const MixEntry* entry = archive.findEntryByName(fileName);
    if (entry == nullptr) {
        errorMessage = "MIX entry was not found: " + fileName;
        return false;
    }
    return archive.readEntryData(*entry, bytes, errorMessage);
}

const MixArchive* loadCachedAudioMixArchive(const std::string& mixPath) {
    static std::unordered_map<std::string, std::shared_ptr<MixArchive>> cache;
    const std::string cacheKey = lowerAscii(mixPath);
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second.get();
    }

    auto archive = std::make_shared<MixArchive>();
    if (!archive->loadFromFile(mixPath)) {
        cache.emplace(cacheKey, nullptr);
        return nullptr;
    }

    const MixArchive* result = archive.get();
    cache.emplace(cacheKey, std::move(archive));
    return result;
}

bool readFileSlice(
    const std::string& path,
    std::uint64_t absoluteOffset,
    std::uint32_t size,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    bytes.clear();
    if (absoluteOffset
        > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        errorMessage = "Audio data offset is too large.";
        return false;
    }
    if (static_cast<std::uint64_t>(size)
        > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        errorMessage = "Audio data slice is too large.";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "Failed to open audio MIX file: " + path;
        return false;
    }

    input.seekg(static_cast<std::streamoff>(absoluteOffset), std::ios::beg);
    if (!input) {
        errorMessage = "Failed to seek to audio data in MIX file: " + path;
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            errorMessage = "Audio data slice is truncated in MIX file: " + path;
            bytes.clear();
            return false;
        }
    }
    return true;
}

bool parseAudioIdx(
    const std::vector<std::uint8_t>& idxBytes,
    std::unordered_map<std::string, AudioBagEntry>& entries,
    std::string& errorMessage
) {
    entries.clear();
    if (idxBytes.size() < 12U) {
        errorMessage = "audio.idx is too small.";
        return false;
    }

    if (idxBytes[0] != 'G'
        || idxBytes[1] != 'A'
        || idxBytes[2] != 'B'
        || idxBytes[3] != 'A') {
        errorMessage = "audio.idx has an invalid GABA header.";
        return false;
    }

    const std::uint32_t count = readUInt32LE(idxBytes, 8U);
    constexpr std::size_t EntryBytes = 36U;
    if (count
        > (std::numeric_limits<std::size_t>::max() - 12U) / EntryBytes) {
        errorMessage = "audio.idx entry count is too large.";
        return false;
    }

    const std::size_t requiredSize =
        12U + static_cast<std::size_t>(count) * EntryBytes;
    if (requiredSize > idxBytes.size()) {
        errorMessage = "audio.idx entry table is truncated.";
        return false;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 12U + static_cast<std::size_t>(i) * EntryBytes;
        std::string name;
        for (std::size_t n = 0; n < 16U; ++n) {
            const std::uint8_t character = idxBytes[offset + n];
            if (character == 0U) {
                break;
            }
            name.push_back(static_cast<char>(character));
        }

        name = normalizeAudioSampleName(name);
        if (name.empty()) {
            continue;
        }

        AudioBagEntry entry;
        entry.name = name;
        entry.offset = readUInt32LE(idxBytes, offset + 16U);
        entry.size = readUInt32LE(idxBytes, offset + 20U);
        entry.sampleRate = static_cast<int>(readUInt32LE(idxBytes, offset + 24U));
        entry.flags = readUInt32LE(idxBytes, offset + 28U);
        entry.chunkSize = readUInt32LE(idxBytes, offset + 32U);
        entries[name] = entry;
    }

    return true;
}

const std::vector<AudioBagLibrary>& loadAudioBagLibraries(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    static std::unordered_map<std::string, std::vector<AudioBagLibrary>> cache;
    const std::string cacheKey = audioSourceCacheKey(
        assetRootPath,
        campaignSourceId
    );
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<AudioBagLibrary> libraries;
    for (const std::string& mixPath : audioMixPathCandidates(
            assetRootPath,
            campaignSourceId
        )) {
        const MixArchive* archive = loadCachedAudioMixArchive(mixPath);
        if (archive == nullptr) {
            continue;
        }

        std::vector<std::uint8_t> idxBytes;
        std::string errorMessage;
        const MixEntry* bagEntry = archive->findEntryByName("audio.bag");
        if (!readMixFileData(*archive, "audio.idx", idxBytes, errorMessage)
            || bagEntry == nullptr) {
            continue;
        }

        AudioBagLibrary library;
        library.mixPath = mixPath;
        library.bagAbsoluteOffset = bagEntry->absoluteOffset;
        library.bagSize = bagEntry->size;
        if (!parseAudioIdx(idxBytes, library.entriesByName, errorMessage)) {
            continue;
        }

        libraries.push_back(std::move(library));
    }

    auto [inserted, _] = cache.emplace(cacheKey, std::move(libraries));
    return inserted->second;
}

std::string safeFileName(std::string value) {
    value = lowerAscii(value);
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '-' && character != '_') {
            character = '_';
        }
    }
    return value.empty() ? "sample" : value;
}

std::string sourceHashTag(const std::string& value) {
    std::uint32_t hash = 2166136261U;
    const std::string normalized = lowerAscii(value);
    for (const unsigned char character : normalized) {
        hash ^= static_cast<std::uint32_t>(character);
        hash *= 16777619U;
    }

    std::ostringstream output;
    output << std::hex
           << std::setw(8)
           << std::setfill('0')
           << hash;
    return output.str();
}

bool writeBytesToFile(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code errorCode;
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode) {
            errorMessage = "Failed to create audio cache directory: "
                + errorCode.message();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "Failed to open audio cache file: " + pathString(path);
        return false;
    }

    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }

    if (!output) {
        errorMessage = "Failed to write audio cache file: " + pathString(path);
        return false;
    }
    return true;
}

bool readBytesFromFile(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    bytes.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "Failed to open audio file: " + pathString(path);
        return false;
    }

    bytes.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
    if (!input.eof() && input.fail()) {
        errorMessage = "Failed to read audio file: " + pathString(path);
        bytes.clear();
        return false;
    }
    return true;
}

bool nonEmptyFile(const std::filesystem::path& path) {
    if (!existingRegularFile(path)) {
        return false;
    }

    std::error_code errorCode;
    return std::filesystem::file_size(path, errorCode) > 44U && !errorCode;
}

bool waveFileHasNonEmptyData(
    const std::filesystem::path& path,
    std::size_t& payloadBytes,
    std::string& errorMessage
) {
    payloadBytes = 0;
    std::vector<std::uint8_t> bytes;
    if (!readBytesFromFile(path, bytes, errorMessage)) {
        return false;
    }
    if (!isWaveFileBytes(bytes)) {
        errorMessage = "Resolved audio has an invalid RIFF/WAVE header: "
            + pathString(path);
        return false;
    }

    std::size_t cursor = 12U;
    while (cursor + 8U <= bytes.size()) {
        const std::size_t chunkSize =
            static_cast<std::size_t>(bytes[cursor + 4U])
            | (static_cast<std::size_t>(bytes[cursor + 5U]) << 8U)
            | (static_cast<std::size_t>(bytes[cursor + 6U]) << 16U)
            | (static_cast<std::size_t>(bytes[cursor + 7U]) << 24U);
        const std::size_t dataOffset = cursor + 8U;
        if (chunkSize > bytes.size() - dataOffset) {
            errorMessage = "Resolved WAV contains a truncated chunk: "
                + pathString(path);
            return false;
        }
        const bool dataChunk = bytes[cursor] == 'd'
            && bytes[cursor + 1U] == 'a'
            && bytes[cursor + 2U] == 't'
            && bytes[cursor + 3U] == 'a';
        if (dataChunk) {
            payloadBytes = chunkSize;
            if (payloadBytes == 0U) {
                errorMessage = "Resolved WAV has an empty audio payload: "
                    + pathString(path);
                return false;
            }
            return true;
        }
        cursor = dataOffset + chunkSize + (chunkSize & 1U);
    }

    errorMessage = "Resolved WAV has no data chunk: " + pathString(path);
    return false;
}

std::filesystem::path cachePathForSample(
    const std::string& cacheRootPath,
    const std::string& prefix,
    const std::string& sourceId,
    const std::string& sampleName,
    std::uint32_t offset,
    std::uint32_t size,
    const std::string& codecTag
) {
    const std::string fileName =
        prefix
        + "_"
        + safeFileName(sampleName)
        + "_"
        + sourceHashTag(sourceId)
        + "_"
        + std::to_string(offset)
        + "_"
        + std::to_string(size)
        + "_"
        + codecTag
        + ".wav";
    return std::filesystem::path(cacheRootPath) / fileName;
}

std::vector<std::string> looseAudioFileCandidates(
    const std::string& sampleName
) {
    std::vector<std::string> names;
    std::set<std::string> seen;

    const std::string raw = stripAudioDecorators(sampleName);
    const std::string normalized = normalizeAudioSampleName(raw);
    const std::string stripped = normalized;
    const std::filesystem::path rawPath(raw);

    const auto add = [&](const std::string& name) {
        if (name.empty()) {
            return;
        }
        if (seen.insert(lowerAscii(name)).second) {
            names.push_back(name);
        }
    };

    add(raw);
    if (!rawPath.has_extension()) {
        add(raw + ".wav");
        add(raw + ".aud");
    }
    add(stripped);
    add(stripped + ".wav");
    add(stripped + ".aud");
    return names;
}

std::vector<std::filesystem::path> looseAudioRoots(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    const std::filesystem::path root(assetRootPath);
    const std::string sourceId =
        normalizeAudioCampaignSourceId(campaignSourceId);
    if (sourceId == "ra2") {
        return {
            root / "audio",
            root / "language",
            root / "ra2" / "local",
            root / "ra2"
        };
    }
    if (sourceId == "yr") {
        return {
            root / "audiomd",
            root / "langmd",
            root / "ra2md" / "localmd",
            root / "expandmd01"
        };
    }
    if (!sourceId.empty()) {
        return {};
    }
    return {
        root / "audio",
        root / "audiomd",
        root / "language",
        root / "langmd",
        root / "ra2md" / "localmd",
        root / "expandmd01",
        root
    };
}

AudioSampleResolveResult resolveLooseAudioFile(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& sampleName,
    const std::string& campaignSourceId
) {
    AudioSampleResolveResult result;
    result.sampleName = sampleName;
    result.normalizedName = normalizeAudioSampleName(sampleName);

    for (const std::filesystem::path& root : looseAudioRoots(
            assetRootPath,
            campaignSourceId
        )) {
        for (const std::string& fileName : looseAudioFileCandidates(sampleName)) {
            const std::filesystem::path candidate = root / fileName;
            if (!existingRegularFile(candidate)) {
                continue;
            }

            const std::string extension = lowerAscii(candidate.extension().string());
            if (extension == ".wav") {
                std::vector<std::uint8_t> bytes;
                std::string readError;
                if (!readBytesFromFile(candidate, bytes, readError)) {
                    result.errorMessage = readError;
                    continue;
                }
                if (!isWaveFileBytes(bytes)) {
                    result.errorMessage =
                        "WAV file has an invalid or truncated RIFF/WAVE header: "
                        + pathString(candidate);
                    continue;
                }

                result.success = true;
                result.sourcePath = pathString(candidate);
                result.sourceFormat = "wav";
                result.wavPath = result.sourcePath;
                result.decoded = false;
                return result;
            }

            if (extension == ".aud") {
                std::vector<std::uint8_t> bytes;
                if (!readBytesFromFile(candidate, bytes, result.errorMessage)) {
                    continue;
                }

                AudioDecodeResult decoded = decodeAudToWave(bytes);
                if (!decoded.success) {
                    result.errorMessage = decoded.errorMessage;
                    return result;
                }

                const std::filesystem::path cachePath = cachePathForSample(
                    cacheRootPath,
                    "aud",
                    pathString(candidate),
                    result.normalizedName,
                    0,
                    static_cast<std::uint32_t>(bytes.size()),
                    "aud_v3"
                );
                if (!nonEmptyFile(cachePath)) {
                    if (!writeBytesToFile(cachePath, decoded.wavBytes, result.errorMessage)) {
                        return result;
                    }
                } else {
                    result.fromCache = true;
                }

                result.success = true;
                result.sourcePath = pathString(candidate);
                result.sourceFormat = decoded.sourceFormat;
                result.wavPath = pathString(cachePath);
                result.decoded = true;
                result.sampleRate = decoded.sampleRate;
                result.bitsPerSample = decoded.bitsPerSample;
                result.channels = decoded.channels;
                result.pcmBytes = decoded.pcmBytes.size();
                return result;
            }
        }
    }

    return result;
}

AudioSampleResolveResult resolveMixLooseAudioFile(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& sampleName,
    const std::string& campaignSourceId
) {
    AudioSampleResolveResult result;
    result.sampleName = sampleName;
    result.normalizedName = normalizeAudioSampleName(sampleName);

    if (result.normalizedName.empty()) {
        return result;
    }

    for (const std::string& mixPath : audioMixPathCandidates(
            assetRootPath,
            campaignSourceId
        )) {
        const MixArchive* archive = loadCachedAudioMixArchive(mixPath);
        if (archive == nullptr) {
            continue;
        }

        for (const std::string& fileName : looseAudioFileCandidates(sampleName)) {
            const std::string extension =
                lowerAscii(std::filesystem::path(fileName).extension().string());
            if (extension != ".wav" && extension != ".aud") {
                continue;
            }

            const MixEntry* entry = archive->findEntryByName(fileName);
            if (entry == nullptr) {
                continue;
            }

            std::vector<std::uint8_t> bytes;
            std::string errorMessage;
            if (!archive->readEntryData(*entry, bytes, errorMessage)) {
                result.errorMessage = errorMessage;
                continue;
            }

            const std::string sourceId = mixPath + "|" + fileName;
            if (extension == ".wav") {
                if (!isWaveFileBytes(bytes)) {
                    result.errorMessage =
                        "MIX WAV entry has an invalid or truncated RIFF/WAVE header: "
                        + fileName;
                    continue;
                }

                const std::filesystem::path cachePath = cachePathForSample(
                    cacheRootPath,
                    "mixwav",
                    sourceId,
                    result.normalizedName,
                    entry->offset,
                    entry->size,
                    "pcm_v2"
                );
                if (!nonEmptyFile(cachePath)) {
                    if (!writeBytesToFile(cachePath, bytes, result.errorMessage)) {
                        return result;
                    }
                } else {
                    result.fromCache = true;
                }

                result.success = true;
                result.sourcePath = mixPath;
                result.sourceFormat = "mix wav";
                result.wavPath = pathString(cachePath);
                result.decoded = false;
                return result;
            }

            AudioDecodeResult decoded = decodeAudToWave(bytes);
            if (!decoded.success) {
                result.errorMessage = decoded.errorMessage;
                continue;
            }

            const std::filesystem::path cachePath = cachePathForSample(
                cacheRootPath,
                "mixaud",
                sourceId,
                result.normalizedName,
                entry->offset,
                entry->size,
                "aud_v3"
            );
            if (!nonEmptyFile(cachePath)) {
                if (!writeBytesToFile(cachePath, decoded.wavBytes, result.errorMessage)) {
                    return result;
                }
            } else {
                result.fromCache = true;
            }

            result.success = true;
            result.sourcePath = mixPath;
            result.sourceFormat = decoded.sourceFormat + " from mix";
            result.wavPath = pathString(cachePath);
            result.decoded = true;
            result.sampleRate = decoded.sampleRate;
            result.bitsPerSample = decoded.bitsPerSample;
            result.channels = decoded.channels;
            result.pcmBytes = decoded.pcmBytes.size();
            return result;
        }
    }

    return result;
}

struct BackgroundMusicTrackCandidate {
    std::string themeIniPath;
    std::string themeId;
    std::string soundName;
    std::string side;
    bool normal = false;
};

struct BackgroundMusicIniSource {
    std::string sourcePath;
    IniFile ini;
};

std::vector<BackgroundMusicIniSource> backgroundMusicIniSources(
    const std::string& assetRootPath
) {
    std::vector<BackgroundMusicIniSource> sources;
    const std::filesystem::path root(assetRootPath);
    const std::filesystem::path candidates[] = {
        root / "ra2md" / "localmd" / "thememd.ini",
        root / "localmd" / "thememd.ini",
        root / "thememd.ini",
        root / "ra2" / "local" / "theme.ini",
        root / "local" / "theme.ini",
        root / "theme.ini"
    };
    std::unordered_set<std::string> seen;
    for (const std::filesystem::path& candidate : candidates) {
        if (!existingRegularFile(candidate)) {
            continue;
        }
        const std::string sourcePath = pathString(candidate);
        if (!seen.insert(lowerAscii(sourcePath)).second) {
            continue;
        }
        BackgroundMusicIniSource source;
        source.sourcePath = sourcePath;
        if (source.ini.loadFromFile(sourcePath)) {
            sources.push_back(std::move(source));
        }
    }

    // The base-game theme.ini normally remains inside the encrypted local.mix
    // when Yuri's Revenge is installed.  Read it in-place so the playable
    // runtime can use both original soundtracks without creating extracted
    // files or depending on a preparation step.
    const std::filesystem::path localMixCandidates[] = {
        root / "ra2" / "local.mix",
        root / "local.mix"
    };
    for (const std::filesystem::path& mixPath : localMixCandidates) {
        if (!existingRegularFile(mixPath)) {
            continue;
        }
        const std::string sourceKey =
            lowerAscii(pathString(mixPath) + "|theme.ini");
        if (!seen.insert(sourceKey).second) {
            continue;
        }
        MixArchive archive;
        if (!archive.loadFromFile(pathString(mixPath))) {
            continue;
        }
        const MixEntry* entry = archive.findEntryByName("theme.ini");
        if (entry == nullptr) {
            continue;
        }
        std::vector<std::uint8_t> bytes;
        std::string errorMessage;
        if (!archive.readEntryData(*entry, bytes, errorMessage)) {
            continue;
        }
        BackgroundMusicIniSource source;
        source.sourcePath = pathString(mixPath) + "|theme.ini";
        if (source.ini.loadFromString(std::string(
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size()
            ))) {
            sources.push_back(std::move(source));
        }
    }
    return sources;
}

std::vector<BackgroundMusicTrackCandidate> backgroundMusicTracks(
    const std::string& assetRootPath,
    bool normalOnly
) {
    std::vector<BackgroundMusicTrackCandidate> tracks;
    std::unordered_set<std::string> seen;
    for (BackgroundMusicIniSource& source :
            backgroundMusicIniSources(assetRootPath)) {
        IniFile& themeIni = source.ini;
        const IniFile::Section* themes = themeIni.getSection("Themes");
        if (themes == nullptr) {
            continue;
        }
        for (const auto& [index, rawThemeId] : *themes) {
            (void)index;
            const std::string themeId = stripAudioComment(rawThemeId);
            if (themeId.empty() || themeIni.getSection(themeId) == nullptr) {
                continue;
            }
            const std::string soundName = stripAudioComment(
                themeIni.getValue(themeId, "Sound", "")
            );
            if (soundName.empty()) {
                continue;
            }
            const std::string normalValue = lowerAscii(stripAudioComment(
                themeIni.getValue(themeId, "Normal", "yes")
            ));
            const bool normal = normalValue.empty()
                || normalValue == "yes"
                || normalValue == "true"
                || normalValue == "1";
            if (normalOnly && !normal) {
                continue;
            }
            const std::string key = lowerAscii(themeId + "|" + soundName);
            if (!seen.insert(key).second) {
                continue;
            }
            tracks.push_back({
                source.sourcePath,
                themeId,
                soundName,
                stripAudioComment(themeIni.getValue(themeId, "Side", "")),
                normal
            });
        }
    }

    // A direct RedAlert2 resource root exposes theme.ini only as the nested
    // ra2.mix -> local.mix -> theme.ini chain.  The runtime's prepared asset
    // root has ra2/local.mix available and uses the authored INI above; retain
    // the exact retail 13-track declaration as a no-extraction fallback for a
    // direct install root.  Track resolution still has to prove that the
    // corresponding Sound entry exists in the original Theme.mix.
    static const std::pair<const char*, const char*> retailRa2Themes[] = {
        {"200Meters", "200Meter"},
        {"Power", "Power"},
        {"Fortification", "Fortific"},
        {"InDeep", "InDeep"},
        {"Tension", "Tension"},
        {"EagleHunter", "EagleHun"},
        {"IndustroFunk", "Industro"},
        {"Grinder", "Grinder"},
        {"BlowItUp", "BlowItUp"},
        {"Destroy", "Destroy"},
        {"Burn", "Burn"},
        {"Motorized", "Motorize"},
        {"HM2", "HM2"}
    };
    for (const auto& [themeId, soundName] : retailRa2Themes) {
        const std::string key = lowerAscii(
            std::string(themeId) + "|" + soundName
        );
        if (!seen.insert(key).second) {
            continue;
        }
        tracks.push_back({
            "retail ra2.mix|local.mix|theme.ini",
            themeId,
            soundName,
            "",
            true
        });
    }
    static const std::pair<const char*, const char*> retailYrThemes[] = {
        {"BrainFreeze", "BrainFre"},
        {"Deceiver", "Deceiver"},
        {"PhatAttack", "PhatAtta"},
        {"BullyKit", "Bully"},
        {"DefendTheBase", "Defend"},
        {"Tactics", "Tactics"},
        {"TranceLVania", "TranceLV"}
    };
    for (const auto& [themeId, soundName] : retailYrThemes) {
        const std::string key = lowerAscii(
            std::string(themeId) + "|" + soundName
        );
        if (!seen.insert(key).second) {
            continue;
        }
        tracks.push_back({
            "retail ra2md.mix|localmd.mix|thememd.ini",
            themeId,
            soundName,
            "",
            true
        });
    }
    if (!normalOnly) {
        // These identifiers are authored front-end phases rather than
        // gameplay playlist entries.  Keep the exact retail mapping as the
        // direct-install fallback when thememd.ini remains nested in MIXes.
        static const std::pair<const char*, const char*> retailYrPhases[] = {
            {"INTRO", "Drok"},
            {"LOADING", "Bully"},
            {"SCORE", "ScoreX"},
            {"CREDITS", "OptionX"}
        };
        for (const auto& [themeId, soundName] : retailYrPhases) {
            const std::string key = lowerAscii(
                std::string(themeId) + "|" + soundName
            );
            if (!seen.insert(key).second) {
                continue;
            }
            tracks.push_back({
                "retail ra2md.mix|localmd.mix|thememd.ini",
                themeId,
                soundName,
                "",
                false
            });
        }
    }
    return tracks;
}

std::vector<BackgroundMusicTrackCandidate> normalBackgroundMusicTracks(
    const std::string& assetRootPath
) {
    return backgroundMusicTracks(assetRootPath, true);
}

AudioBackgroundMusicFaction soundtrackFactionForTrack(
    const BackgroundMusicTrackCandidate& track
) {
    const std::string side = lowerAscii(trimAscii(track.side));
    if (!side.empty()) {
        if (side.find("yuri") != std::string::npos
            || side.find("third") != std::string::npos) {
            return AudioBackgroundMusicFaction::Yuri;
        }
        if (side.find("sov") != std::string::npos
            || side.find("russ") != std::string::npos
            || side.find("nod") != std::string::npos) {
            return AudioBackgroundMusicFaction::Soviet;
        }
        if (side.find("all") != std::string::npos
            || side.find("amer") != std::string::npos
            || side.find("gdi") != std::string::npos) {
            return AudioBackgroundMusicFaction::Allied;
        }
    }

    // Retail RA2 and YR describe Side= in both theme INIs but leave it blank
    // for every normal track.  Keep explicit, disjoint gameplay playlists so
    // choosing Soviets does not silently play the same fixed Allied track.
    static const std::unordered_set<std::string> alliedThemes = {
        "brainfreeze", "deceiver", "phatattack", "trancelvania",
        "power", "200meters", "eaglehunter", "industrofunk",
        "burn", "motorized"
    };
    static const std::unordered_set<std::string> sovietThemes = {
        "drok", "bullykit", "defendthebase", "tactics",
        "grinder", "fortification", "indeep", "tension",
        "blowitup", "destroy", "hm2"
    };
    const std::string themeId = lowerAscii(trimAscii(track.themeId));
    if (alliedThemes.find(themeId) != alliedThemes.end()) {
        return AudioBackgroundMusicFaction::Allied;
    }
    if (sovietThemes.find(themeId) != sovietThemes.end()) {
        return AudioBackgroundMusicFaction::Soviet;
    }
    return AudioBackgroundMusicFaction::Yuri;
}

const std::vector<std::string>& themeMixPathCandidates(
    const std::string& assetRootPath
) {
    static std::unordered_map<std::string, std::vector<std::string>> cache;
    const std::string cacheKey = lowerAscii(assetRootPath);
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path root(assetRootPath);
    candidates.push_back(root / "thememd.mix");
    candidates.push_back(root / "theme.mix");
    candidates.push_back(root / "ra2md" / "thememd.mix");
    candidates.push_back(root / "ra2" / "theme.mix");
    candidates.push_back(root / "GameMD" / "thememd.mix");
    candidates.push_back(root / "Game" / "theme.mix");
    std::filesystem::path parent = root;
    for (int depth = 0; depth < 6 && !parent.empty(); ++depth) {
        candidates.push_back(parent / "RedAlert2" / "GameMD" / "thememd.mix");
        candidates.push_back(parent / "RedAlert2" / "Game" / "theme.mix");
        parent = parent.parent_path();
    }

    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    for (const std::filesystem::path& candidate : candidates) {
        if (!existingRegularFile(candidate)) {
            continue;
        }
        const std::string path = pathString(candidate);
        if (seen.insert(lowerAscii(path)).second) {
            paths.push_back(path);
        }
    }
    auto [inserted, _] = cache.emplace(cacheKey, std::move(paths));
    return inserted->second;
}

AudioBackgroundMusicResolveResult resolveBackgroundMusicTrackToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const BackgroundMusicTrackCandidate& track
) {
    AudioBackgroundMusicResolveResult result;
    result.themeIniPath = track.themeIniPath;
    result.themeId = track.themeId;
    result.soundName = track.soundName;
    result.normalTrack = track.normal;
    const std::filesystem::path root(assetRootPath);
    const std::filesystem::path looseCandidates[] = {
        root / "thememd" / (track.soundName + ".wav"),
        root / "theme" / (track.soundName + ".wav"),
        root / "thememd" / (track.soundName + ".aud"),
        root / "theme" / (track.soundName + ".aud")
    };
    for (const std::filesystem::path& candidate : looseCandidates) {
        if (!existingRegularFile(candidate)) {
            continue;
        }
        const std::string extension =
            lowerAscii(candidate.extension().string());
        std::vector<std::uint8_t> bytes;
        if (!readBytesFromFile(candidate, bytes, result.errorMessage)) {
            continue;
        }
        if (extension == ".wav") {
            if (!isWaveFileBytes(bytes)) {
                result.errorMessage = "Theme WAV has an invalid RIFF/WAVE payload.";
                continue;
            }
            result.success = true;
            result.sourcePath = pathString(candidate);
            result.sourceFormat = "theme wav";
            result.wavPath = result.sourcePath;
            return result;
        }
        AudioDecodeResult decoded = decodeAudToWave(bytes);
        if (!decoded.success) {
            result.errorMessage = decoded.errorMessage;
            continue;
        }
        if (cacheRootPath.empty()) {
            result.errorMessage = "A cache root is required to decode theme AUD files.";
            return result;
        }
        const std::filesystem::path cachePath = cachePathForSample(
            cacheRootPath,
            "music",
            pathString(candidate),
            track.soundName,
            0,
            static_cast<std::uint32_t>(bytes.size()),
            "aud_v3"
        );
        const bool cached = nonEmptyFile(cachePath);
        if (!cached
            && !writeBytesToFile(cachePath, decoded.wavBytes, result.errorMessage)) {
            return result;
        }
        result.success = true;
        result.fromCache = cached;
        result.decoded = true;
        result.sourcePath = pathString(candidate);
        result.sourceFormat = decoded.sourceFormat;
        result.wavPath = pathString(cachePath);
        return result;
    }

    for (const std::string& mixPath : themeMixPathCandidates(assetRootPath)) {
        const MixArchive* archive = loadCachedAudioMixArchive(mixPath);
        if (archive == nullptr) {
            continue;
        }
        const std::pair<std::string, bool> candidates[] = {
            {track.soundName + ".wav", false},
            {track.soundName + ".aud", true}
        };
        for (const auto& [fileName, aud] : candidates) {
            const MixEntry* entry = archive->findEntryByName(fileName);
            if (entry == nullptr) {
                continue;
            }
            if (cacheRootPath.empty()) {
                result.errorMessage =
                    "A cache root is required to extract theme MIX audio.";
                return result;
            }
            const std::filesystem::path cachePath = cachePathForSample(
                cacheRootPath,
                "mixmusic",
                mixPath + "|" + fileName,
                track.soundName,
                entry->offset,
                entry->size,
                aud ? "aud_v3" : "wav_v1"
            );
            if (nonEmptyFile(cachePath)) {
                result.success = true;
                result.fromCache = true;
                result.decoded = aud;
                result.sourcePath = mixPath + "|" + fileName;
                result.sourceFormat = aud
                    ? "theme mix aud cached wav"
                    : "theme mix wav cached";
                result.wavPath = pathString(cachePath);
                return result;
            }

            std::vector<std::uint8_t> bytes;
            if (!archive->readEntryData(*entry, bytes, result.errorMessage)) {
                continue;
            }
            std::vector<std::uint8_t> wavBytes;
            if (aud) {
                AudioDecodeResult decoded = decodeAudToWave(bytes);
                if (!decoded.success) {
                    result.errorMessage = decoded.errorMessage;
                    continue;
                }
                wavBytes = std::move(decoded.wavBytes);
            } else {
                if (!isWaveFileBytes(bytes)) {
                    result.errorMessage =
                        "Theme MIX WAV entry has an invalid RIFF/WAVE payload.";
                    continue;
                }
                wavBytes = std::move(bytes);
            }
            if (!writeBytesToFile(cachePath, wavBytes, result.errorMessage)) {
                return result;
            }
            result.success = true;
            result.decoded = aud;
            result.sourcePath = mixPath + "|" + fileName;
            result.sourceFormat = aud ? "theme mix aud" : "theme mix wav";
            result.wavPath = pathString(cachePath);
            return result;
        }
    }

    if (result.errorMessage.empty()) {
        result.errorMessage =
            "Theme track was not found in loose thememd/theme files or theme MIX archives.";
    }
    return result;
}

std::vector<std::string> voiceCsfPathCandidates(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path root(assetRootPath);
    if (!assetRootPath.empty()) {
        candidates.push_back(root / "langmd" / "ra2md.csf");
        candidates.push_back(root / "language" / "ra2.csf");
        candidates.push_back(root / "GameMD" / "ra2md.csf");
        candidates.push_back(root / "Game" / "ra2.csf");

        std::filesystem::path parent = root;
        for (int depth = 0; depth < 5 && !parent.empty(); ++depth) {
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "ra2md.csf");
            candidates.push_back(parent / "RedAlert2" / "Game" / "ra2.csf");
            parent = parent.parent_path();
        }
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (!error) {
        candidates.push_back(cwd / "RedAlert2" / "GameMD" / "ra2md.csf");
        candidates.push_back(cwd / "RedAlert2" / "Game" / "ra2.csf");
    }

    std::vector<std::string> paths;
    std::set<std::string> seen;
    const std::string sourceId =
        normalizeAudioCampaignSourceId(campaignSourceId);
    for (const std::filesystem::path& candidate : candidates) {
        const std::string fileName = lowerAscii(
            candidate.filename().string()
        );
        if ((sourceId == "ra2" && fileName != "ra2.csf")
            || (sourceId == "yr" && fileName != "ra2md.csf")
            || (!sourceId.empty()
                && sourceId != "ra2"
                && sourceId != "yr")) {
            continue;
        }
        if (!existingRegularFile(candidate)) {
            continue;
        }

        const std::string path = pathString(candidate);
        if (seen.insert(lowerAscii(path)).second) {
            paths.push_back(path);
        }
    }
    return paths;
}

std::vector<std::string> voiceMixPathCandidates(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path root(assetRootPath);
    if (!assetRootPath.empty()) {
        candidates.push_back(root / "langmd" / "langmd.mix");
        candidates.push_back(root / "language" / "language.mix");
        candidates.push_back(root / "ra2md" / "langmd.mix");
        candidates.push_back(root / "ra2" / "language.mix");
        candidates.push_back(root / "GameMD" / "langmd.mix");
        candidates.push_back(root / "GameMD" / "language.mix");
        candidates.push_back(root / "Game" / "language.mix");

        std::filesystem::path parent = root;
        for (int depth = 0; depth < 5 && !parent.empty(); ++depth) {
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "langmd.mix");
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "language.mix");
            candidates.push_back(parent / "RedAlert2" / "Game" / "language.mix");
            parent = parent.parent_path();
        }
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    candidates.push_back(cwd / "RedAlert2" / "GameMD" / "langmd.mix");
    candidates.push_back(cwd / "RedAlert2" / "GameMD" / "language.mix");
    candidates.push_back(cwd / "RedAlert2" / "Game" / "language.mix");

    std::vector<std::string> paths;
    std::set<std::string> seen;
    const std::string sourceId =
        normalizeAudioCampaignSourceId(campaignSourceId);
    for (const std::filesystem::path& candidate : candidates) {
        const std::string fileName = lowerAscii(
            candidate.filename().string()
        );
        if ((sourceId == "ra2" && fileName != "language.mix")
            || (sourceId == "yr" && fileName != "langmd.mix")
            || (!sourceId.empty()
                && sourceId != "ra2"
                && sourceId != "yr")) {
            continue;
        }
        if (!existingRegularFile(candidate)) {
            continue;
        }

        const std::string path = pathString(candidate);
        if (seen.insert(lowerAscii(path)).second) {
            paths.push_back(path);
        }
    }
    return paths;
}

std::vector<AudioIndexedSample> scanVoxLabelSamples(
    const std::vector<std::string>& paths
) {
    std::vector<AudioIndexedSample> labels;
    std::unordered_set<std::string> seen;
    for (const std::string& path : paths) {
        std::vector<std::uint8_t> bytes;
        std::string errorMessage;
        if (!readBytesFromFile(path, bytes, errorMessage)) {
            continue;
        }

        for (std::size_t index = 0; index + 4U < bytes.size(); ++index) {
            if (bytes[index] != 'V'
                || bytes[index + 1U] != 'O'
                || bytes[index + 2U] != 'X'
                || bytes[index + 3U] != ':') {
                continue;
            }

            std::string label;
            std::size_t cursor = index + 4U;
            while (cursor < bytes.size()) {
                const unsigned char value = bytes[cursor];
                if ((value >= 'a' && value <= 'z')
                    || (value >= '0' && value <= '9')
                    || value == '_') {
                    label.push_back(static_cast<char>(value));
                    ++cursor;
                    continue;
                }
                break;
            }

            label = normalizeAudioSampleName(label);
            if (label.empty() || !seen.insert(label).second) {
                continue;
            }

            AudioIndexedSample sample;
            sample.name = label;
            sample.sourceMixPath = path;
            sample.offset = static_cast<std::uint32_t>(
                std::min<std::size_t>(
                    index,
                    std::numeric_limits<std::uint32_t>::max()
                )
            );
            labels.push_back(std::move(sample));
        }
    }
    return labels;
}

const std::vector<AudioIndexedSample>& loadVoxLabelSamples(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    static std::unordered_map<std::string, std::vector<AudioIndexedSample>> cache;
    const std::string cacheKey = audioSourceCacheKey(
        assetRootPath,
        campaignSourceId
    );
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<AudioIndexedSample> labels =
        scanVoxLabelSamples(voiceCsfPathCandidates(
            assetRootPath,
            campaignSourceId
        ));
    if (labels.empty()) {
        labels = scanVoxLabelSamples(voiceMixPathCandidates(
            assetRootPath,
            campaignSourceId
        ));
    }

    auto [inserted, _] = cache.emplace(cacheKey, std::move(labels));
    return inserted->second;
}

std::vector<std::string> smokeSampleNamesFromIni(const IniFile* soundIni) {
    const char* preferredEvents[] = {
        "GISelect",
        "GIMove",
        "GIAttackCommand",
        "GenAllVehicleSelect",
        "GenAllVehicleMove",
        "GenAllVehicleAttackCommand",
        "ConscriptSelect",
        "ConscriptMove",
        "ConscriptAttackCommand",
        "FlakTroopSelect",
        "FlakTrackSelect",
        "GenSovVehicleSelect",
        "GenSovVehicleMove",
        "GenSovVehicleAttackCommand",
        "WarMinerSelect",
        "WarMinerHarvest",
        "InitiateSelect",
        "InitiateMove",
        "InitiateAttackCommand",
        "GattlingTankSelect",
        "GattlingTankMove",
        "GattlingTankAttackCommand",
        "IFVSelect",
        "IFVMove",
        "GrizzlyTankAttack",
        "MenuClick",
        "PlaceBuilding",
        "SellBuilding",
        "PowerOn",
        "PowerOff",
        "BuildingRepaired",
        "BuildingGarrisoned"
    };

    std::vector<std::string> samples;
    std::set<std::string> seen;
    const auto add = [&](const std::string& sample) {
        const std::string normalized = normalizeAudioSampleName(sample);
        if (normalized.empty()) {
            return;
        }
        if (seen.insert(normalized).second) {
            samples.push_back(sample);
        }
    };

    if (soundIni != nullptr) {
        for (const char* eventName : preferredEvents) {
            const std::vector<std::string> tokens =
                sampleTokensForSection(soundIni, eventName);
            if (!tokens.empty()) {
                add(tokens.front());
            }
        }

        // Exhaustively decode every unique sample declared by soundmd.ini.
        // The catalog report alone only proves that an IDX entry exists; this
        // path proves that every unit voice and UI/gameplay sound can actually
        // become a non-empty WAV payload through the runtime decoder.
        for (const std::string& sectionName : soundIni->getSectionNames()) {
            for (const std::string& token :
                    sampleTokensForSection(soundIni, sectionName)) {
                add(token);
            }
        }
    }

    // Keep the exact samples involved in the reported regressions in every
    // decode run even when SoundList ordering changes: sidebar/tab and dialog
    // clicks plus the War Miner/Chrono Miner harvesting SFX and voice.
    for (const char* required : {
            "umenucl1",
            "utab",
            "uacbopen",
            "vorehara",
            "vchrhaa"
        }) {
        add(required);
    }

    if (samples.empty()) {
        samples = {"umenucl1", "ichratta", "ibruatta"};
    }

    return samples;
}

} // namespace

std::string normalizeAudioSampleName(const std::string& sampleName) {
    std::string value = stripAudioDecorators(sampleName);
    if (isNullAudioSampleName(value)) {
        return "";
    }

    const std::filesystem::path path(value);
    if (path.has_extension()) {
        value = path.stem().string();
    }
    value = lowerAscii(trimAscii(value));
    return isNullAudioSampleName(value) ? "" : value;
}

AudioBackgroundMusicResolveResult resolveBackgroundMusicToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::vector<std::string>& preferredThemeIds
) {
    std::vector<BackgroundMusicTrackCandidate> tracks =
        normalBackgroundMusicTracks(assetRootPath);
    AudioBackgroundMusicResolveResult result;
    if (tracks.empty()) {
        result.errorMessage =
            "No normal background music tracks were declared by thememd.ini/theme.ini.";
        return result;
    }

    std::vector<std::size_t> order;
    std::unordered_set<std::size_t> seen;
    for (const std::string& preferred : preferredThemeIds) {
        const std::string normalized = lowerAscii(trimAscii(preferred));
        for (std::size_t index = 0; index < tracks.size(); ++index) {
            if ((lowerAscii(tracks[index].themeId) == normalized
                    || lowerAscii(tracks[index].soundName) == normalized)
                && seen.insert(index).second) {
                order.push_back(index);
            }
        }
    }
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (seen.insert(index).second) {
            order.push_back(index);
        }
    }

    std::string lastError;
    for (const std::size_t index : order) {
        result = resolveBackgroundMusicTrackToWave(
            assetRootPath,
            cacheRootPath,
            tracks[index]
        );
        if (result.success) {
            return result;
        }
        lastError = result.errorMessage;
    }
    result.errorMessage = lastError.empty()
        ? "No declared background music track resolved."
        : lastError;
    return result;
}

std::vector<std::string> collectBackgroundMusicThemeIds(
    const std::string& assetRootPath,
    AudioBackgroundMusicFaction faction
) {
    std::vector<std::string> themeIds;
    static const std::unordered_set<std::string> yuriThemes = {
        "brainfreeze", "deceiver", "trancelvania", "bullykit"
    };
    for (const BackgroundMusicTrackCandidate& track :
            normalBackgroundMusicTracks(assetRootPath)) {
        const std::string normalized =
            lowerAscii(trimAscii(track.themeId));
        const bool included = faction == AudioBackgroundMusicFaction::Any
            || (faction == AudioBackgroundMusicFaction::Yuri
                ? yuriThemes.find(normalized) != yuriThemes.end()
                : soundtrackFactionForTrack(track) == faction);
        if (included) {
            themeIds.push_back(track.themeId);
        }
    }
    return themeIds;
}

AudioBackgroundMusicResolveResult resolveBackgroundMusicThemeToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& themeId
) {
    const std::string normalized = lowerAscii(trimAscii(themeId));
    // Explicit lookup is also used for authored front-end phases such as
    // INTRO/LOADING/SCORE/CREDITS, whose retail entries are Normal=no.
    for (const BackgroundMusicTrackCandidate& track :
            backgroundMusicTracks(assetRootPath, false)) {
        if (lowerAscii(trimAscii(track.themeId)) == normalized
            || lowerAscii(trimAscii(track.soundName)) == normalized) {
            return resolveBackgroundMusicTrackToWave(
                assetRootPath,
                cacheRootPath,
                track
            );
        }
    }
    AudioBackgroundMusicResolveResult result;
    result.themeId = themeId;
    result.errorMessage =
        "Requested background music theme is not declared.";
    return result;
}

std::vector<AudioIndexedSample> collectIndexedAudioSamples(
    const std::string& assetRootPath,
    const std::string& campaignSourceId
) {
    std::vector<AudioIndexedSample> samples;
    for (const AudioBagLibrary& library : loadAudioBagLibraries(
            assetRootPath,
            campaignSourceId
        )) {
        for (const auto& [name, entry] : library.entriesByName) {
            AudioIndexedSample sample;
            sample.name = name;
            sample.sourceMixPath = library.mixPath;
            sample.offset = entry.offset;
            sample.size = entry.size;
            sample.sampleRate = entry.sampleRate;
            sample.flags = entry.flags;
            sample.chunkSize = entry.chunkSize;
            samples.push_back(std::move(sample));
        }
    }
    return samples;
}

AudioSampleResolveResult resolveAudioSampleToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& sampleName,
    const std::string& campaignSourceId
) {
    AudioSampleResolveResult looseResult =
        resolveLooseAudioFile(
            assetRootPath,
            cacheRootPath,
            sampleName,
            campaignSourceId
        );
    if (looseResult.success) {
        return looseResult;
    }

    AudioSampleResolveResult mixLooseResult =
        resolveMixLooseAudioFile(
            assetRootPath,
            cacheRootPath,
            sampleName,
            campaignSourceId
        );
    if (mixLooseResult.success) {
        return mixLooseResult;
    }

    AudioSampleResolveResult result;
    result.sampleName = sampleName;
    result.normalizedName = normalizeAudioSampleName(sampleName);
    if (result.normalizedName.empty()) {
        result.errorMessage = "Audio sample name is empty.";
        return result;
    }

    for (const AudioBagLibrary& library : loadAudioBagLibraries(
            assetRootPath,
            campaignSourceId
        )) {
        const auto entryIt = library.entriesByName.find(result.normalizedName);
        if (entryIt == library.entriesByName.end()) {
            continue;
        }

        const AudioBagEntry& entry = entryIt->second;
        if (entry.offset > library.bagSize
            || entry.size > library.bagSize - entry.offset) {
            result.errorMessage = "BAG sample points beyond the end of audio.bag.";
            return result;
        }

        const bool imaAdpcm = audioBagEntryUsesImaAdpcm(entry);
        // v4 decodes the Microsoft IMA ADPCM block headers declared by the
        // IDX chunk size.  Keep the old cache files untouched, but never reuse
        // their header-as-audio PCM payload.
        const std::string codecTag = imaAdpcm ? "ima_block_v4" : "pcm_v3";
        const std::filesystem::path cachePath = cachePathForSample(
            cacheRootPath,
            "bag",
            library.mixPath,
            result.normalizedName,
            entry.offset,
            entry.size,
            codecTag
        );
        if (nonEmptyFile(cachePath)) {
            result.success = true;
            result.fromCache = true;
            result.decoded = true;
            result.sourcePath = library.mixPath;
            result.sourceFormat = imaAdpcm
                ? "idx/bag ima-adpcm cached wav"
                : "idx/bag pcm cached wav";
            result.wavPath = pathString(cachePath);
            result.sampleRate = entry.sampleRate;
            result.bitsPerSample = imaAdpcm
                ? 16
                : audioBagEntryPcmBits(entry);
            result.channels = audioBagEntryChannels(entry);
            return result;
        }

        if (library.bagAbsoluteOffset
            > std::numeric_limits<std::uint64_t>::max() - entry.offset) {
            result.errorMessage = "BAG sample offset overflow.";
            return result;
        }

        std::vector<std::uint8_t> sourceBytes;
        if (!readFileSlice(
                library.mixPath,
                library.bagAbsoluteOffset + entry.offset,
                entry.size,
                sourceBytes,
                result.errorMessage
            )) {
            return result;
        }
        AudioDecodeResult decoded = decodeAudioBagEntry(entry, sourceBytes);
        if (!decoded.success) {
            result.errorMessage = decoded.errorMessage;
            return result;
        }

        if (!writeBytesToFile(cachePath, decoded.wavBytes, result.errorMessage)) {
            return result;
        }

        result.success = true;
        result.sourcePath = library.mixPath;
        result.sourceFormat = decoded.sourceFormat;
        result.wavPath = pathString(cachePath);
        result.decoded = true;
        result.sampleRate = decoded.sampleRate;
        result.bitsPerSample = decoded.bitsPerSample;
        result.channels = decoded.channels;
        result.pcmBytes = decoded.pcmBytes.size();
        return result;
    }

    for (const AudioIndexedSample& sample : loadVoxLabelSamples(
            assetRootPath,
            campaignSourceId
        )) {
        if (sample.name != result.normalizedName) {
            continue;
        }

        result.sourcePath = sample.sourceMixPath + "|VOX:" + sample.name;
        result.sourceFormat = "csf vox label";
        result.errorMessage =
            "VOX label was found in langmd/language MIX, but no embedded WAV/AUD "
            "payload is exposed through the current decoder path.";
        return result;
    }

    result.errorMessage =
        "Audio sample was not found in loose files, audio.idx/audio.bag, "
        "or langmd/language VOX labels.";
    return result;
}

AudioDecodeSmokeReport runAudioDecodeSmoke(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const IniFile* soundIni
) {
    AudioDecodeSmokeReport report;
    report.assetRootPath = assetRootPath;
    report.cacheRootPath = cacheRootPath;

    const bool blockDecoderSelfTest = imaAdpcmBlockDecoderSelfTest();
    if (!blockDecoderSelfTest) {
        report.warnings.push_back(
            "Microsoft IMA ADPCM block decoder self-test failed."
        );
    }

    const std::vector<AudioIndexedSample> indexedSamples =
        collectIndexedAudioSamples(assetRootPath);
    report.indexedSamples = static_cast<int>(indexedSamples.size());

    std::set<std::string> libraries;
    for (const AudioIndexedSample& sample : indexedSamples) {
        libraries.insert(sample.sourceMixPath);
    }
    report.indexedLibraries = static_cast<int>(libraries.size());

    const std::vector<std::string> samples = smokeSampleNamesFromIni(soundIni);
    for (const std::string& sample : samples) {
        AudioSampleResolveResult result =
            resolveAudioSampleToWave(assetRootPath, cacheRootPath, sample);
        ++report.attemptedSamples;
        if (result.success) {
            std::size_t payloadBytes = 0;
            std::string validationError;
            if (!waveFileHasNonEmptyData(
                    result.wavPath,
                    payloadBytes,
                    validationError
                )) {
                result.success = false;
                result.errorMessage = validationError;
            } else {
                result.pcmBytes = payloadBytes;
                ++report.resolvedSamples;
                ++report.validatedWaveSamples;
                if (result.decoded) {
                    ++report.decodedSamples;
                }
            }
        }
        report.samples.push_back(std::move(result));
    }

    for (const BackgroundMusicTrackCandidate& track :
            normalBackgroundMusicTracks(assetRootPath)) {
        AudioBackgroundMusicResolveResult result =
            resolveBackgroundMusicTrackToWave(
                assetRootPath,
                cacheRootPath,
                track
            );
        ++report.attemptedMusicTracks;
        if (result.success) {
            ++report.resolvedMusicTracks;
        }
        report.musicTracks.push_back(std::move(result));
    }

    if (report.indexedSamples == 0) {
        report.warnings.push_back(
            "No audio.idx/audio.bag samples were indexed; loose WAV/AUD fallback was used."
        );
    }

    report.success = blockDecoderSelfTest
        && report.attemptedSamples > 0
        && report.resolvedSamples == report.attemptedSamples
        && report.validatedWaveSamples == report.attemptedSamples
        && report.attemptedMusicTracks > 0
        && report.resolvedMusicTracks == report.attemptedMusicTracks;
    if (!report.success) {
        report.errorMessage =
            "Audio samples or declared normal background music tracks did not resolve.";
    }

    return report;
}

void printAudioDecodeSmokeReport(
    std::ostream& output,
    const AudioDecodeSmokeReport& report
) {
    output << "\n========== Audio Decode Smoke ==========\n";
    output << "Success:           " << (report.success ? "yes" : "no") << "\n";
    if (!report.success && !report.errorMessage.empty()) {
        output << "Reason:            " << report.errorMessage << "\n";
    }
    output << "Asset root:        " << report.assetRootPath << "\n";
    output << "Cache root:        " << report.cacheRootPath << "\n";
    output << "Indexed libraries: " << report.indexedLibraries << "\n";
    output << "Indexed samples:   " << report.indexedSamples << "\n";
    output << "Attempted samples: " << report.attemptedSamples << "\n";
    output << "Resolved samples:  " << report.resolvedSamples << "\n";
    output << "Decoded samples:   " << report.decodedSamples << "\n";
    output << "Validated WAVs:    " << report.validatedWaveSamples << "\n";
    output << "Music tracks:      " << report.attemptedMusicTracks
           << " resolved=" << report.resolvedMusicTracks << "\n";
    if (!report.warnings.empty()) {
        output << "\nWarnings:\n";
        for (const std::string& warning : report.warnings) {
            output << "- " << warning << "\n";
        }
    }

    output << "\nSamples:\n";
    for (const AudioSampleResolveResult& sample : report.samples) {
        output << "- "
               << sample.sampleName
               << " -> "
               << (sample.success ? "ok" : "missing")
               << " | normalized="
               << sample.normalizedName
               << " | format="
               << (sample.sourceFormat.empty() ? "-" : sample.sourceFormat)
               << " | rate="
               << sample.sampleRate
               << " | wav="
               << (sample.wavPath.empty() ? "-" : sample.wavPath);
        if (!sample.success && !sample.errorMessage.empty()) {
            output << " | " << sample.errorMessage;
        }
        output << "\n";
    }
    output << "\nBackground music:\n";
    for (const AudioBackgroundMusicResolveResult& track : report.musicTracks) {
        output << "- " << track.themeId
               << " (" << track.soundName << ")"
               << " -> " << (track.success ? "ok" : "missing")
               << " | format="
               << (track.sourceFormat.empty() ? "-" : track.sourceFormat)
               << " | source="
               << (track.sourcePath.empty() ? "-" : track.sourcePath)
               << " | wav="
               << (track.wavPath.empty() ? "-" : track.wavPath)
               << " | cache=" << (track.fromCache ? "yes" : "no");
        if (!track.success && !track.errorMessage.empty()) {
            output << " | " << track.errorMessage;
        }
        output << "\n";
    }
}
