#include "data/maps/MapDatabase.h"

#include "data/art/TheaterDatabase.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

constexpr int MapCoordinateLimit = 512;
constexpr int ObjectCellCoordinateBase = 1000;
constexpr int MaxMapLevel = 14;

std::size_t countSectionEntries(const IniFile& ini, const std::string& sectionName) {
    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) {
        return 0;
    }

    return section->size();
}

bool hasSection(const IniFile& ini, const std::string& sectionName) {
    return ini.getSection(sectionName) != nullptr;
}

std::string trimString(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);

    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);

        if (comma == std::string::npos) {
            parts.push_back(trimString(value.substr(start)));
            break;
        }

        parts.push_back(trimString(value.substr(start, comma - start)));
        start = comma + 1;
    }

    return parts;
}

std::string toLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            if (ch >= 'A' && ch <= 'Z') {
                return static_cast<char>(ch - 'A' + 'a');
            }

            return static_cast<char>(ch);
        }
    );

    return value;
}

void addUniqueString(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::string joinStrings(const std::vector<std::string>& values, const std::string& delimiter = ",") {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            output << delimiter;
        }
        output << values[i];
    }
    return output.str();
}

int mapCellIndex(int x, int y) {
    return y * MapCoordinateLimit + x;
}

bool isMapCoordinateInBounds(int x, int y) {
    return x >= 0 && y >= 0 && x < MapCoordinateLimit && y < MapCoordinateLimit;
}

int parseIntValue(
    const std::string& value,
    const std::string& context,
    std::vector<std::string>& warnings
) {
    const std::string trimmed = trimString(value);
    if (trimmed.empty()) {
        return -1;
    }

    int parsed = -1;
    const char* begin = trimmed.data();
    const char* end = trimmed.data() + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc() || result.ptr != end) {
        warnings.push_back(context + " is not an integer: " + trimmed);
        return -1;
    }

    return parsed;
}

int readIntField(
    const IniFile& ini,
    const std::string& sectionName,
    const std::string& key,
    std::vector<std::string>& warnings
) {
    const std::string value = ini.getValue(sectionName, key);
    if (value.empty()) {
        return -1;
    }

    return parseIntValue(value, "Map field [" + sectionName + "] " + key, warnings);
}

double readDoubleField(
    const IniFile& ini,
    const std::string& sectionName,
    const std::string& key,
    double defaultValue,
    std::vector<std::string>& warnings
) {
    const std::string value = trimString(ini.getValue(sectionName, key));
    if (value.empty()) {
        return defaultValue;
    }

    double parsed = defaultValue;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const std::from_chars_result result =
        std::from_chars(begin, end, parsed, std::chars_format::general);
    if (result.ec != std::errc()
        || result.ptr != end
        || !std::isfinite(parsed)) {
        warnings.push_back(
            "Map field [" + sectionName + "] " + key
            + " is not a finite number: " + value
        );
        return defaultValue;
    }

    return parsed;
}

void readLightingProfile(
    const IniFile& ini,
    const std::string& prefix,
    MapLightingProfile& profile,
    std::vector<std::string>& warnings
) {
    profile.ambient = readDoubleField(
        ini,
        "Lighting",
        prefix + "Ambient",
        profile.ambient,
        warnings
    );
    profile.red = readDoubleField(
        ini,
        "Lighting",
        prefix + "Red",
        profile.red,
        warnings
    );
    profile.green = readDoubleField(
        ini,
        "Lighting",
        prefix + "Green",
        profile.green,
        warnings
    );
    profile.blue = readDoubleField(
        ini,
        "Lighting",
        prefix + "Blue",
        profile.blue,
        warnings
    );
    profile.ground = readDoubleField(
        ini,
        "Lighting",
        prefix + "Ground",
        profile.ground,
        warnings
    );
    profile.level = readDoubleField(
        ini,
        "Lighting",
        prefix + "Level",
        profile.level,
        warnings
    );
}

MapLighting parseLightingSection(
    const IniFile& ini,
    std::vector<std::string>& warnings
) {
    MapLighting lighting;
    const IniFile::Section* section = ini.getSection("Lighting");
    lighting.sectionPresent = section != nullptr;
    lighting.authoredFieldCount = section == nullptr ? 0 : section->size();

    readLightingProfile(ini, "", lighting.base, warnings);
    readLightingProfile(ini, "Ion", lighting.ion, warnings);
    readLightingProfile(ini, "Dominator", lighting.dominator, warnings);
    lighting.dominatorAmbientChangeRate = readDoubleField(
        ini,
        "Lighting",
        "DominatorAmbientChangeRate",
        lighting.dominatorAmbientChangeRate,
        warnings
    );
    return lighting;
}

struct MapRectFields {
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    bool valid = false;
};

MapRectFields parseMapRectFields(
    const std::string& value,
    const std::string& context,
    std::vector<std::string>& warnings
) {
    MapRectFields rect;
    const std::vector<std::string> fields = splitCommaList(value);
    if (fields.size() < 4) {
        if (!value.empty()) {
            warnings.push_back(context + " expected four comma-separated integers.");
        }
        return rect;
    }

    rect.x = parseIntValue(fields[0], context + " x", warnings);
    rect.y = parseIntValue(fields[1], context + " y", warnings);
    rect.width = parseIntValue(fields[2], context + " width", warnings);
    rect.height = parseIntValue(fields[3], context + " height", warnings);
    rect.valid = rect.x >= 0
        && rect.y >= 0
        && rect.width > 0
        && rect.height > 0;
    return rect;
}

bool requireFieldCount(
    const std::vector<std::string>& fields,
    std::size_t minimum,
    const std::string& context,
    std::vector<std::string>& warnings
) {
    if (fields.size() >= minimum) {
        return true;
    }

    warnings.push_back(context + " has too few fields.");
    return false;
}

bool isOneValue(const std::string& value) {
    const std::string trimmed = trimString(value);
    return trimmed == "1"
        || trimmed == "yes"
        || trimmed == "Yes"
        || trimmed == "YES"
        || trimmed == "true"
        || trimmed == "True"
        || trimmed == "TRUE";
}

int base64Value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }

    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }

    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }

    if (value == '+') {
        return 62;
    }

    if (value == '/') {
        return 63;
    }

    return -1;
}

bool decodeBase64(
    const std::string& encoded,
    std::vector<unsigned char>& decoded,
    std::string& errorMessage
) {
    decoded.clear();
    errorMessage.clear();

    unsigned int value = 0;
    int bitCount = -8;
    bool sawPadding = false;

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(encoded[i]);

        if (std::isspace(ch) != 0) {
            continue;
        }

        if (encoded[i] == '=') {
            sawPadding = true;
            continue;
        }

        if (sawPadding) {
            errorMessage = "found data after base64 padding at offset "
                + std::to_string(i);
            return false;
        }

        const int digit = base64Value(encoded[i]);
        if (digit < 0) {
            errorMessage = "invalid base64 character at offset "
                + std::to_string(i);
            return false;
        }

        value = ((value << 6) | static_cast<unsigned int>(digit)) & 0x00FFFFFFu;
        bitCount += 6;

        if (bitCount >= 0) {
            decoded.push_back(static_cast<unsigned char>((value >> bitCount) & 0xFFu));
            bitCount -= 8;
        }
    }

    return true;
}

unsigned int readUInt16LE(const std::vector<unsigned char>& data, std::size_t offset) {
    return static_cast<unsigned int>(data[offset])
        | (static_cast<unsigned int>(data[offset + 1]) << 8);
}

int readInt16LE(const std::vector<unsigned char>& data, std::size_t offset) {
    const std::uint16_t raw = static_cast<std::uint16_t>(readUInt16LE(data, offset));
    return static_cast<int>(static_cast<std::int16_t>(raw));
}

int readInt32LE(const std::vector<unsigned char>& data, std::size_t offset) {
    const std::uint32_t raw =
        static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return static_cast<int>(static_cast<std::int32_t>(raw));
}

bool appendLcwByte(
    std::vector<unsigned char>& output,
    unsigned char value,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (output.size() >= expectedBytes) {
        errorMessage = "LCW output exceeded expected byte count";
        return false;
    }

    output.push_back(value);
    return true;
}

bool copyLcwOutputRange(
    std::vector<unsigned char>& output,
    std::size_t sourceOffset,
    std::size_t count,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (sourceOffset >= output.size()) {
        errorMessage = "LCW copy source is outside decoded output";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readOffset = sourceOffset + i;
        if (readOffset >= output.size()) {
            errorMessage = "LCW copy source overran decoded output";
            return false;
        }

        if (!appendLcwByte(output, output[readOffset], expectedBytes, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool appendLzoByte(
    std::vector<unsigned char>& output,
    unsigned char value,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (output.size() >= expectedBytes) {
        errorMessage = "LZO output exceeded expected byte count";
        return false;
    }

    output.push_back(value);
    return true;
}

bool copyLzoLiterals(
    const std::vector<unsigned char>& data,
    std::size_t& offset,
    std::size_t endOffset,
    std::size_t count,
    std::vector<unsigned char>& output,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (offset + count > endOffset) {
        errorMessage = "LZO literal run exceeds compressed chunk";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!appendLzoByte(output, data[offset + i], expectedBytes, errorMessage)) {
            return false;
        }
    }

    offset += count;
    return true;
}

bool copyLzoOutputRange(
    std::vector<unsigned char>& output,
    std::size_t sourceOffset,
    std::size_t count,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (sourceOffset >= output.size()) {
        errorMessage = "LZO match source is outside decoded output";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readOffset = sourceOffset + i;
        if (readOffset >= output.size()) {
            errorMessage = "LZO match source overran decoded output";
            return false;
        }

        if (!appendLzoByte(output, output[readOffset], expectedBytes, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool subtractLzoBackReference(
    std::size_t outputSize,
    std::size_t distance,
    std::size_t& sourceOffset,
    std::string& errorMessage
) {
    if (distance > outputSize) {
        errorMessage = "LZO match points before decoded output";
        return false;
    }

    sourceOffset = outputSize - distance;
    return true;
}

bool readLzoLengthExtension(
    const std::vector<unsigned char>& data,
    std::size_t& offset,
    std::size_t endOffset,
    std::size_t baseLength,
    unsigned int& length,
    std::string& errorMessage
) {
    length = 0;

    while (offset < endOffset && data[offset] == 0) {
        length += 255;
        ++offset;
    }

    if (offset >= endOffset) {
        errorMessage = "LZO extended length is truncated";
        return false;
    }

    length += static_cast<unsigned int>(baseLength + data[offset]);
    ++offset;
    return true;
}

bool decodeLzo1xBlock(
    const std::vector<unsigned char>& data,
    std::size_t compressedOffset,
    std::size_t compressedBytes,
    std::size_t expectedBytes,
    std::size_t& decodedBytes,
    std::vector<unsigned char>* decodedOutput,
    std::string& errorMessage
) {
    decodedBytes = 0;
    errorMessage.clear();

    std::vector<unsigned char> output;
    output.reserve(expectedBytes);

    std::size_t offset = compressedOffset;
    const std::size_t endOffset = compressedOffset + compressedBytes;
    unsigned int token = 0;
    bool matchDone = false;

    if (offset >= endOffset) {
        errorMessage = "LZO chunk is empty";
        return false;
    }

    if (data[offset] > 17) {
        token = static_cast<unsigned int>(data[offset] - 17);
        ++offset;

        if (token < 4) {
            if (!copyLzoLiterals(
                    data,
                    offset,
                    endOffset,
                    token,
                    output,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }

            if (offset >= endOffset) {
                errorMessage = "LZO stream ended after initial literal run";
                return false;
            }

            token = data[offset];
            ++offset;
            goto lzo_match;
        }

        if (!copyLzoLiterals(
                data,
                offset,
                endOffset,
                token,
                output,
                expectedBytes,
                errorMessage
            )) {
            return false;
        }

        goto lzo_first_literal_run;
    }

    while (true) {
        if (offset >= endOffset) {
            errorMessage = "LZO stream ended before command";
            return false;
        }

        token = data[offset];
        ++offset;

        if (token >= 16) {
            goto lzo_match;
        }

        if (token == 0) {
            if (!readLzoLengthExtension(
                    data,
                    offset,
                    endOffset,
                    15,
                    token,
                    errorMessage
                )) {
                return false;
            }
        }

        if (!copyLzoLiterals(
                data,
                offset,
                endOffset,
                static_cast<std::size_t>(token) + 3,
                output,
                expectedBytes,
                errorMessage
            )) {
            return false;
        }

lzo_first_literal_run:
        if (offset >= endOffset) {
            errorMessage = "LZO stream ended before match command";
            return false;
        }

        token = data[offset];
        ++offset;

        if (token >= 16) {
            goto lzo_match;
        }

        if (offset >= endOffset) {
            errorMessage = "LZO short match is missing its offset byte";
            return false;
        }

        {
            const std::size_t distance =
                1 + 0x0800u + (token >> 2)
                + (static_cast<std::size_t>(data[offset]) << 2);
            ++offset;

            std::size_t sourceOffset = 0;
            if (!subtractLzoBackReference(
                    output.size(),
                    distance,
                    sourceOffset,
                    errorMessage
                )) {
                return false;
            }

            if (!copyLzoOutputRange(
                    output,
                    sourceOffset,
                    3,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        }

        matchDone = true;

lzo_match:
        do {
            if (matchDone) {
                matchDone = false;
                goto lzo_match_done;
            }

            if (token >= 64) {
                if (offset >= endOffset) {
                    errorMessage = "LZO M2 match is missing its offset byte";
                    return false;
                }

                const std::size_t distance =
                    1 + ((token >> 2) & 7)
                    + (static_cast<std::size_t>(data[offset]) << 3);
                ++offset;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                const std::size_t count = (token >> 5) + 1;
                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        count,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }

                goto lzo_match_done;
            }

            if (token >= 32) {
                unsigned int countToken = token & 31u;
                if (countToken == 0) {
                    if (!readLzoLengthExtension(
                            data,
                            offset,
                            endOffset,
                            31,
                            countToken,
                            errorMessage
                        )) {
                        return false;
                    }
                }

                if (offset + 2 > endOffset) {
                    errorMessage = "LZO M3 match is missing its offset";
                    return false;
                }

                const std::size_t distance =
                    1 + (readUInt16LE(data, offset) >> 2);
                offset += 2;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        static_cast<std::size_t>(countToken) + 2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            } else if (token >= 16) {
                const std::size_t highDistance = (token & 8u) << 11;
                unsigned int countToken = token & 7u;

                if (countToken == 0) {
                    if (!readLzoLengthExtension(
                            data,
                            offset,
                            endOffset,
                            7,
                            countToken,
                            errorMessage
                        )) {
                        return false;
                    }
                }

                if (offset + 2 > endOffset) {
                    errorMessage = "LZO M4 match is missing its offset";
                    return false;
                }

                const std::size_t lowDistance = readUInt16LE(data, offset) >> 2;
                offset += 2;

                if (highDistance == 0 && lowDistance == 0) {
                    decodedBytes = output.size();

                    if (decodedBytes != expectedBytes) {
                        errorMessage = "LZO decoded byte count does not match chunk header";
                        return false;
                    }

                    if (offset != endOffset) {
                        errorMessage = "LZO stream has trailing bytes after end marker";
                        return false;
                    }

                    if (decodedOutput != nullptr) {
                        decodedOutput->insert(
                            decodedOutput->end(),
                            output.begin(),
                            output.end()
                        );
                    }

                    return true;
                }

                const std::size_t distance = highDistance + lowDistance + 0x4000u;
                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        static_cast<std::size_t>(countToken) + 2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            } else {
                if (offset >= endOffset) {
                    errorMessage = "LZO M1 match is missing its offset byte";
                    return false;
                }

                const std::size_t distance =
                    1 + (token >> 2)
                    + (static_cast<std::size_t>(data[offset]) << 2);
                ++offset;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            }

lzo_match_done:
            if (offset < 2) {
                errorMessage = "LZO match state is invalid";
                return false;
            }

            token = data[offset - 2] & 3u;
            if (token == 0) {
                break;
            }

            if (!copyLzoLiterals(
                    data,
                    offset,
                    endOffset,
                    token,
                    output,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }

            if (offset >= endOffset) {
                errorMessage = "LZO stream ended after match literals";
                return false;
            }

            token = data[offset];
            ++offset;
        } while (true);
    }
}

bool decodeLcwBlock(
    const std::vector<unsigned char>& data,
    std::size_t compressedOffset,
    std::size_t compressedBytes,
    std::size_t expectedBytes,
    std::size_t& decodedBytes,
    std::vector<unsigned char>* decodedOutput,
    std::string& errorMessage
) {
    decodedBytes = 0;
    errorMessage.clear();

    std::vector<unsigned char> output;
    output.reserve(expectedBytes);

    std::size_t offset = compressedOffset;
    const std::size_t endOffset = compressedOffset + compressedBytes;
    bool sawEndCommand = false;

    while (offset < endOffset) {
        const unsigned int command = data[offset];
        ++offset;

        if ((command & 0x80u) == 0) {
            if (offset >= endOffset) {
                errorMessage = "LCW short copy is missing its offset byte";
                return false;
            }

            const std::size_t count = (command >> 4) + 3;
            const std::size_t displacement =
                ((command & 0x0Fu) << 8) | data[offset];
            ++offset;

            if (displacement > output.size()) {
                errorMessage = "LCW short copy points before decoded output";
                return false;
            }

            if (!copyLcwOutputRange(
                    output,
                    output.size() - displacement,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        } else if ((command & 0x40u) == 0) {
            const std::size_t count = command & 0x3Fu;
            if (count == 0) {
                sawEndCommand = true;
                break;
            }

            if (offset + count > endOffset) {
                errorMessage = "LCW literal copy exceeds compressed chunk";
                return false;
            }

            for (std::size_t i = 0; i < count; ++i) {
                if (!appendLcwByte(output, data[offset + i], expectedBytes, errorMessage)) {
                    return false;
                }
            }
            offset += count;
        } else if (command < 0xFEu) {
            if (offset + 2 > endOffset) {
                errorMessage = "LCW absolute copy is missing its source offset";
                return false;
            }

            const std::size_t count = (command & 0x3Fu) + 3;
            const std::size_t sourceOffset = readUInt16LE(data, offset);
            offset += 2;

            if (!copyLcwOutputRange(
                    output,
                    sourceOffset,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        } else if (command == 0xFEu) {
            if (offset + 3 > endOffset) {
                errorMessage = "LCW fill command is truncated";
                return false;
            }

            const std::size_t count = readUInt16LE(data, offset);
            offset += 2;
            const unsigned char value = data[offset];
            ++offset;

            for (std::size_t i = 0; i < count; ++i) {
                if (!appendLcwByte(output, value, expectedBytes, errorMessage)) {
                    return false;
                }
            }
        } else {
            if (offset + 4 > endOffset) {
                errorMessage = "LCW long copy command is truncated";
                return false;
            }

            const std::size_t count = readUInt16LE(data, offset);
            offset += 2;
            const std::size_t sourceOffset = readUInt16LE(data, offset);
            offset += 2;

            if (!copyLcwOutputRange(
                    output,
                    sourceOffset,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        }
    }

    decodedBytes = output.size();

    if (!sawEndCommand) {
        errorMessage = "LCW stream ended without an end command";
        return false;
    }

    if (decodedBytes != expectedBytes) {
        errorMessage = "LCW decoded byte count does not match chunk header";
        return false;
    }

    if (decodedOutput != nullptr) {
        decodedOutput->insert(decodedOutput->end(), output.begin(), output.end());
    }

    return true;
}

void decodeLzoPayload(
    const std::vector<unsigned char>& decodedSectionBytes,
    MapPackedSectionSummary& summary,
    std::vector<unsigned char>* payloadBytes,
    std::vector<std::string>& warnings
) {
    if (summary.lineCount == 0 || !summary.chunkTableValid) {
        return;
    }

    summary.payloadDecodeAttempted = true;

    for (const MapPackedChunkSummary& chunk : summary.chunks) {
        std::size_t decodedBytes = 0;
        std::string errorMessage;
        if (!decodeLzo1xBlock(
                decodedSectionBytes,
                chunk.offset + 4,
                chunk.compressedBytes,
                chunk.uncompressedBytes,
                decodedBytes,
                payloadBytes,
                errorMessage
            )) {
            ++summary.payloadDecodeFailureCount;
            warnings.push_back("Packed section [" + summary.sectionName
                + "] LZO chunk at decoded offset " + std::to_string(chunk.offset)
                + " failed: " + errorMessage + ".");
            continue;
        }

        ++summary.payloadDecodedChunks;
        summary.payloadDecodedBytes += decodedBytes;
    }

    summary.payloadDecoded =
        summary.payloadDecodeFailureCount == 0
        && summary.payloadDecodedChunks == summary.chunkCount
        && summary.payloadDecodedBytes == summary.totalUncompressedBytes;
}

void decodeLcwPayload(
    const std::vector<unsigned char>& decodedSectionBytes,
    MapPackedSectionSummary& summary,
    std::vector<unsigned char>* payloadBytes,
    std::vector<std::string>& warnings
) {
    if (summary.lineCount == 0 || !summary.chunkTableValid) {
        return;
    }

    summary.payloadDecodeAttempted = true;

    for (const MapPackedChunkSummary& chunk : summary.chunks) {
        std::size_t decodedBytes = 0;
        std::string errorMessage;
        if (!decodeLcwBlock(
                decodedSectionBytes,
                chunk.offset + 4,
                chunk.compressedBytes,
                chunk.uncompressedBytes,
                decodedBytes,
                payloadBytes,
                errorMessage
            )) {
            ++summary.payloadDecodeFailureCount;
            warnings.push_back("Packed section [" + summary.sectionName
                + "] LCW chunk at decoded offset " + std::to_string(chunk.offset)
                + " failed: " + errorMessage + ".");
            continue;
        }

        ++summary.payloadDecodedChunks;
        summary.payloadDecodedBytes += decodedBytes;
    }

    summary.payloadDecoded =
        summary.payloadDecodeFailureCount == 0
        && summary.payloadDecodedChunks == summary.chunkCount
        && summary.payloadDecodedBytes == summary.totalUncompressedBytes;
}

MapPackedSectionSummary summarizePackedSection(
    const IniFile& ini,
    const std::string& sectionName,
    const std::string& payloadCodec,
    std::vector<unsigned char>* payloadBytes,
    std::vector<std::string>& warnings
) {
    MapPackedSectionSummary summary;
    summary.sectionName = sectionName;
    summary.payloadCodec = payloadCodec;

    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) {
        return summary;
    }

    std::string encoded;
    summary.lineCount = section->size();

    for (const IniFile::KeyValue& entry : *section) {
        encoded += entry.second;
    }

    summary.encodedChars = encoded.size();

    if (encoded.empty()) {
        warnings.push_back("Packed section [" + sectionName + "] has no encoded payload.");
        return summary;
    }

    std::vector<unsigned char> decoded;
    std::string errorMessage;
    if (!decodeBase64(encoded, decoded, errorMessage)) {
        warnings.push_back("Packed section [" + sectionName
            + "] base64 decode failed: " + errorMessage + ".");
        return summary;
    }

    summary.base64Decoded = true;
    summary.decodedBytes = decoded.size();

    std::size_t offset = 0;
    bool valid = true;

    while (offset < decoded.size()) {
        if (offset + 4 > decoded.size()) {
            warnings.push_back("Packed section [" + sectionName
                + "] has a truncated chunk header at decoded offset "
                + std::to_string(offset) + ".");
            valid = false;
            break;
        }

        const unsigned int compressedBytes = readUInt16LE(decoded, offset);
        const unsigned int uncompressedBytes = readUInt16LE(decoded, offset + 2);
        const std::size_t chunkEnd = offset + 4 + compressedBytes;

        if (compressedBytes == 0) {
            warnings.push_back("Packed section [" + sectionName
                + "] has a zero-length compressed chunk at decoded offset "
                + std::to_string(offset) + ".");
            valid = false;
            break;
        }

        if (chunkEnd > decoded.size()) {
            warnings.push_back("Packed section [" + sectionName
                + "] chunk at decoded offset " + std::to_string(offset)
                + " exceeds decoded data size.");
            valid = false;
            break;
        }

        MapPackedChunkSummary chunk;
        chunk.offset = offset;
        chunk.compressedBytes = compressedBytes;
        chunk.uncompressedBytes = uncompressedBytes;
        summary.chunks.push_back(chunk);
        summary.totalCompressedBytes += compressedBytes;
        summary.totalUncompressedBytes += uncompressedBytes;

        offset = chunkEnd;
    }

    summary.chunkCount = summary.chunks.size();
    summary.chunkTableValid = valid && offset == decoded.size();

    if (summary.payloadCodec == "lcw") {
        decodeLcwPayload(decoded, summary, payloadBytes, warnings);
    } else if (summary.payloadCodec == "lzo1x") {
        decodeLzoPayload(decoded, summary, payloadBytes, warnings);
    }

    return summary;
}

void parseIsoTiles(
    const std::vector<unsigned char>& payloadBytes,
    std::vector<MapIsoTile>& isoTiles,
    MapPackedSectionSummary& summary,
    std::vector<std::string>& warnings
) {
    isoTiles.clear();

    if (!summary.payloadDecoded) {
        return;
    }

    constexpr std::size_t isoTileRecordSize = 11;
    const std::size_t rawRemainder = payloadBytes.size() % isoTileRecordSize;
    const std::size_t payloadRecordBytes =
        rawRemainder == 4
            ? payloadBytes.size() - 4
            : payloadBytes.size() - rawRemainder;
    summary.payloadRecordSize = isoTileRecordSize;
    summary.payloadRecordCount = payloadRecordBytes / isoTileRecordSize;
    summary.payloadRemainderBytes = payloadBytes.size() - (summary.payloadRecordCount * isoTileRecordSize);

    if (summary.payloadRemainderBytes != 4) {
        warnings.push_back("Packed section [IsoMapPack5] decoded tile payload has "
            + std::to_string(summary.payloadRemainderBytes)
            + " trailing bytes; expected 4.");
    }

    isoTiles.reserve(summary.payloadRecordCount);

    std::size_t invalidCoordinateCount = 0;
    std::size_t invalidTileIndexCount = 0;
    std::size_t clampedLevelCount = 0;

    for (
        std::size_t offset = 0;
        offset + isoTileRecordSize <= payloadRecordBytes;
        offset += isoTileRecordSize
    ) {
        MapIsoTile tile;
        tile.x = readInt16LE(payloadBytes, offset);
        tile.y = readInt16LE(payloadBytes, offset + 2);
        tile.tileIndex = readInt32LE(payloadBytes, offset + 4);
        tile.subTileIndex = payloadBytes[offset + 8];
        tile.level = payloadBytes[offset + 9];
        tile.iceGrowth = payloadBytes[offset + 10];

        if (tile.x < 0
            || tile.y < 0
            || tile.x >= MapCoordinateLimit
            || tile.y >= MapCoordinateLimit
        ) {
            ++invalidCoordinateCount;
            continue;
        }

        if (tile.tileIndex < 0 || tile.tileIndex > 65535) {
            tile.tileIndex = 65535;
            tile.subTileIndex = 0;
            ++invalidTileIndexCount;
        }

        if (tile.level < 0 || tile.level > MaxMapLevel) {
            tile.level = std::clamp(tile.level, 0, MaxMapLevel);
            ++clampedLevelCount;
        }

        isoTiles.push_back(tile);
    }

    if (invalidCoordinateCount > 0) {
        warnings.push_back("Packed section [IsoMapPack5] skipped "
            + std::to_string(invalidCoordinateCount)
            + " tile records with coordinates outside the 512x512 map grid.");
    }

    if (invalidTileIndexCount > 0) {
        warnings.push_back("Packed section [IsoMapPack5] treated "
            + std::to_string(invalidTileIndexCount)
            + " tile records with invalid tile indexes as empty tiles.");
    }

    if (clampedLevelCount > 0) {
        warnings.push_back("Packed section [IsoMapPack5] clamped "
            + std::to_string(clampedLevelCount)
            + " tile records with terrain levels outside 0-14.");
    }
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool containsAscii(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

bool isLowBridgeOverlayName(const std::string& value) {
    const std::string lower = toLowerAscii(value);
    return startsWith(lower, "lobrdg") || startsWith(lower, "lobrdb");
}

bool isHighBridgeOverlayName(const std::string& value) {
    const std::string lower = toLowerAscii(value);
    return startsWith(lower, "bridge")
        || startsWith(lower, "bridg")
        || startsWith(lower, "bridgb");
}

bool isLowBridgeOverlayIndex(int overlayIndex) {
    return (overlayIndex >= 74 && overlayIndex <= 99)
        || (overlayIndex >= 205 && overlayIndex <= 230);
}

bool isBridgePlaceholderOverlayIndex(int overlayIndex) {
    return overlayIndex == 100
        || overlayIndex == 101
        || overlayIndex == 231
        || overlayIndex == 232;
}

bool isRa2GemOverlayIndex(int overlayIndex) {
    return overlayIndex >= 27 && overlayIndex <= 38;
}

std::string numberedOverlayResourceName(const std::string& prefix, int number) {
    std::ostringstream stream;
    stream << prefix;
    if (number < 10) {
        stream << '0';
    }
    stream << number;
    return stream.str();
}

std::string resourceOverlayNameForIndex(int overlayIndex) {
    if (isRa2GemOverlayIndex(overlayIndex)) {
        return numberedOverlayResourceName("GEM", overlayIndex - 26);
    }

    if (overlayIndex >= 127 && overlayIndex <= 146) {
        return numberedOverlayResourceName("TIB2_", overlayIndex - 126);
    }

    if (overlayIndex >= 147 && overlayIndex <= 166) {
        return numberedOverlayResourceName("TIB3_", overlayIndex - 146);
    }

    if (overlayIndex >= 102 && overlayIndex <= 121) {
        return numberedOverlayResourceName("TIB", overlayIndex - 101);
    }

    return "";
}

bool isLowBridgeOverlayFact(const MapOverlayCell& overlay) {
    if (isBridgePlaceholderOverlayIndex(overlay.overlayIndex)
        || overlay.isOre
        || overlay.isGem) {
        return false;
    }

    return isLowBridgeOverlayIndex(overlay.overlayIndex)
        || isLowBridgeOverlayName(overlay.type)
        || isLowBridgeOverlayName(overlay.image);
}

bool isHighBridgeOverlayFact(const MapOverlayCell& overlay) {
    if (overlay.isOre || overlay.isGem) {
        return false;
    }

    return isHighBridgeOverlayName(overlay.type)
        || isHighBridgeOverlayName(overlay.image);
}

std::string theaterReferenceText(const MapTheaterTileFact& fact) {
    if (!fact.present) {
        return "tile=none";
    }

    std::string text = "tile="
        + std::to_string(fact.tileIndex)
        + ":"
        + std::to_string(fact.subTileIndex);
    if (fact.validTileSet) {
        text += " set="
            + (fact.tileSetName.empty() ? "-" : fact.tileSetName)
            + " file="
            + (fact.expectedFileName.empty() ? "-" : fact.expectedFileName);
    } else {
        text += " set=unresolved";
    }
    if (fact.fileExists) {
        text += " resolved=" + fact.resolvedPath;
    }
    return text;
}

std::string tileReferenceName(const MapTheaterTileFact& fact) {
    return toLowerAscii(
        fact.tileSetName
        + " "
        + fact.baseFileName
        + " "
        + fact.expectedFileName
    );
}

bool isBridgeTileFact(const MapTheaterTileFact& fact) {
    const std::string name = tileReferenceName(fact);
    return containsAscii(name, "bridge")
        || containsAscii(name, "brdg")
        || containsAscii(name, "w-b")
        || containsAscii(name, "wbrdg");
}

bool isWaterTileFact(const MapTheaterTileFact& fact) {
    const std::string name = tileReferenceName(fact);
    return containsAscii(name, "water")
        || containsAscii(name, "shore")
        || containsAscii(name, "wclif");
}

bool isShoreTileFact(const MapTheaterTileFact& fact) {
    const std::string name = tileReferenceName(fact);
    return containsAscii(name, "shore")
        || containsAscii(name, "wclif");
}

bool isCliffTileFact(const MapTheaterTileFact& fact) {
    const std::string name = tileReferenceName(fact);
    return containsAscii(name, "cliff")
        || containsAscii(name, "wclif")
        || containsAscii(name, "clif")
        || containsAscii(name, "slope");
}

bool isRampTileFact(const MapTheaterTileFact& fact) {
    const std::string name = tileReferenceName(fact);
    return containsAscii(name, "ramp")
        || containsAscii(name, "slope");
}

bool isTunnelOverlayFact(const MapOverlayCell& overlay) {
    const std::string name = toLowerAscii(overlay.type + " " + overlay.image);
    return containsAscii(name, "tunnel")
        || containsAscii(name, "tunl");
}

std::string fileNameOnly(const std::string& value) {
    const std::size_t slash = value.find_last_of("/\\");
    if (slash == std::string::npos) {
        return value;
    }
    return value.substr(slash + 1);
}

bool hasFileExtension(const std::string& value) {
    const std::string fileName = fileNameOnly(value);
    const std::size_t dot = fileName.find_last_of('.');
    return dot != std::string::npos && dot + 1U < fileName.size();
}

std::string overlayResourceBaseName(const MapOverlayCell& overlay) {
    if (!overlay.image.empty()) {
        return overlay.image;
    }
    return overlay.type;
}

bool isLowBridgeAggregateBaseName(const std::string& baseName) {
    const std::string lower = toLowerAscii(baseName);
    return lower == "lobrdg26" || lower == "lobrdb26";
}

std::string lowBridgeSegmentBaseNameForOverlay(const MapOverlayCell& overlay) {
    const std::string baseName = overlayResourceBaseName(overlay);
    if (!isLowBridgeAggregateBaseName(baseName)
        || overlay.frame < 0
        || overlay.frame >= 28) {
        return "";
    }

    const std::string lower = toLowerAscii(baseName);
    const std::string prefix =
        lower.rfind("lobrdb", 0) == 0 ? "lobrdb" : "lobrdg";
    std::ostringstream stream;
    stream << prefix;
    const int segment = overlay.frame + 1;
    if (segment < 10) {
        stream << '0';
    }
    stream << segment;
    return stream.str();
}

void addOverlayResourceCandidatesForBaseName(
    std::vector<std::string>& candidates,
    const std::string& lowerBaseName,
    const TheaterDatabase* theaterDatabase
) {
    if (lowerBaseName.empty()) {
        return;
    }

    if (theaterDatabase != nullptr) {
        for (const std::string& candidate :
            theaterDatabase->getResourceFileNameCandidates(lowerBaseName, true)) {
            addUniqueString(candidates, candidate);
        }
        if (lowerBaseName == "bridge") {
            for (const std::string& candidate :
                theaterDatabase->getResourceFileNameCandidates("bridgb", true)) {
                addUniqueString(candidates, candidate);
            }
        }
    } else {
        addUniqueString(candidates, lowerBaseName + ".tmp");
        addUniqueString(candidates, lowerBaseName + ".shp");
    }
}

std::string overlayDisambiguationKindForFact(const MapOverlayCell& overlay) {
    if (overlay.isGem) {
        return "gem";
    }
    if (overlay.isOre) {
        return "ore";
    }
    if (isLowBridgeOverlayFact(overlay)) {
        return "bridge";
    }

    const std::string name = toLowerAscii(overlay.type + " " + overlay.image);
    if (isHighBridgeOverlayName(name)) {
        return "bridge";
    }
    if (containsAscii(name, "wall")) {
        return "wall";
    }
    return "other";
}

std::string overlayPaletteHintForKind(const std::string& kind) {
    if (kind == "ore" || kind == "gem" || kind == "bridge") {
        return "theater terrain palette";
    }
    if (kind == "wall" || kind == "other") {
        return "theater TMP or overlay SHP palette";
    }
    return "overlay palette follows selected resource kind";
}

std::string overlayRendererLayerHintForKind(const std::string& kind) {
    if (kind == "ore" || kind == "gem") {
        return "resource-overlay; use tiberium image/frame/resourceFrame and never bridge candidates";
    }
    if (kind == "bridge") {
        return "bridge-overlay; compose bridge TMP/SHP candidates and never resource/ore candidates";
    }
    if (kind == "wall") {
        return "wall-overlay; use wall art/theater candidates";
    }
    return "ordinary-overlay; do not treat as ore/gem/bridge without explicit kind";
}

std::vector<std::string> overlayResourceCandidatesForFact(
    const MapOverlayCell& overlay,
    const TheaterDatabase* theaterDatabase
) {
    std::vector<std::string> candidates;
    const std::string baseName = overlayResourceBaseName(overlay);
    if (baseName.empty()) {
        return candidates;
    }

    if (hasFileExtension(baseName)) {
        addUniqueString(candidates, toLowerAscii(fileNameOnly(baseName)));
        return candidates;
    }

    const std::string lowerBaseName = toLowerAscii(baseName);
    addOverlayResourceCandidatesForBaseName(
        candidates,
        lowBridgeSegmentBaseNameForOverlay(overlay),
        theaterDatabase
    );
    addOverlayResourceCandidatesForBaseName(
        candidates,
        lowerBaseName,
        theaterDatabase
    );

    return candidates;
}

int overlayResourceFrameForFact(const MapOverlayCell& overlay) {
    if (!lowBridgeSegmentBaseNameForOverlay(overlay).empty()) {
        return 1;
    }

    if (isLowBridgeOverlayFact(overlay)) {
        return 1;
    }

    if (!overlay.isOre && !overlay.isGem) {
        return overlay.frame;
    }

    return overlay.frameIndex >= 0 ? overlay.frameIndex : overlay.frame;
}

MapOverlayResourceFact makeOverlayResourceFact(
    const MapOverlayCell& overlay,
    const TheaterDatabase* theaterDatabase,
    const std::string& theaterName
) {
    MapOverlayResourceFact fact;
    fact.present = true;
    fact.overlayKind = overlayDisambiguationKindForFact(overlay);
    fact.kind = fact.overlayKind;
    fact.baseName = overlayResourceBaseName(overlay);
    fact.image = overlay.image.empty() ? overlay.type : overlay.image;
    fact.requestedFrame = overlay.frameIndex;
    fact.fallbackFrame = overlay.fallbackFrame;
    fact.resourceFrame = overlayResourceFrameForFact(overlay);
    fact.paletteHint = overlayPaletteHintForKind(fact.kind);
    fact.theaterHint = theaterName.empty() ? "theater=(unknown)" : "theater=" + theaterName;
    fact.rendererLayerHint = overlayRendererLayerHintForKind(fact.overlayKind);
    fact.candidateFileNames =
        overlayResourceCandidatesForFact(overlay, theaterDatabase);
    return fact;
}

std::string terrainClassForFact(const MapCellFacts& fact) {
    if (!fact.hasTerrain) {
        return "none";
    }
    if (fact.isHighBridge) {
        return "bridge";
    }
    if (fact.isShore) {
        return "shore";
    }
    if (fact.isWater) {
        return "water";
    }
    if (fact.isRamp) {
        return "ramp";
    }
    if (fact.isCliff) {
        return "cliff";
    }
    if (fact.isTunnel) {
        return "tunnel";
    }
    if (!fact.theaterTile.tileSetName.empty()) {
        return fact.theaterTile.tileSetName;
    }
    return "terrain";
}

std::string passabilityHintForFact(const MapCellFacts& fact) {
    if (fact.isHighBridge || fact.isLowBridge) {
        return "bridge-passable-check-bridge-state";
    }
    if (fact.isWater && !fact.isShore) {
        return "water-requires-naval-or-amphibious";
    }
    if (fact.isRamp) {
        return "ramp-check-movement-zone-and-height";
    }
    if (fact.isCliff) {
        return "cliff-or-slope-check-movement-zone";
    }
    if (fact.isShore) {
        return "shore-amphibious-transition";
    }
    if (fact.isTunnel) {
        return "tunnel-overlay-check-movement-zone";
    }
    if (fact.isResourceCell) {
        return "resource-overlay-check-harvester";
    }
    if (fact.hasTerrain) {
        return "terrain-default-passability";
    }
    if (fact.hasOverlay) {
        return "overlay-only-check-overlay-rules";
    }
    return "unknown";
}

MapTheaterTileFact makeTheaterTileFact(
    const MapIsoTile& tile,
    const TheaterDatabase* theaterDatabase,
    const std::string& tileRootPath
) {
    MapTheaterTileFact fact;
    fact.present = true;
    fact.tileIndex = tile.tileIndex;
    fact.subTileIndex = tile.subTileIndex;

    if (theaterDatabase == nullptr || tile.tileIndex < 0 || tile.tileIndex == 65535) {
        return fact;
    }

    const TheaterTileReference reference =
        theaterDatabase->resolveTileIndex(tile.tileIndex, tileRootPath);
    fact.validTileSet = reference.validTileSet;
    fact.fileExists = reference.fileExists;
    fact.tileSetIndex = reference.tileSetIndex;
    fact.tileIndexInSet = reference.tileIndexInSet;
    fact.tileSetName = reference.tileSetName;
    fact.baseFileName = reference.baseFileName;
    fact.expectedFileName = reference.expectedFileName;
    fact.resolvedPath = reference.resolvedPath;
    fact.candidateFileNames = reference.candidateFileNames;
    return fact;
}

MapTheaterTileFact makeTheaterTileFact(
    const MapIsoTile& tile,
    const TheaterTileReference& reference
) {
    MapTheaterTileFact fact;
    fact.present = true;
    fact.tileIndex = tile.tileIndex;
    fact.subTileIndex = tile.subTileIndex;
    fact.validTileSet = reference.validTileSet;
    fact.fileExists = reference.fileExists;
    fact.tileSetIndex = reference.tileSetIndex;
    fact.tileIndexInSet = reference.tileIndexInSet;
    fact.tileSetName = reference.tileSetName;
    fact.baseFileName = reference.baseFileName;
    fact.expectedFileName = reference.expectedFileName;
    fact.resolvedPath = reference.resolvedPath;
    fact.candidateFileNames = reference.candidateFileNames;
    return fact;
}

std::unordered_map<std::string, MapStructureFootprintOverride> buildFootprintOverrideLookup(
    const std::vector<MapStructureFootprintOverride>& overrides
) {
    std::unordered_map<std::string, MapStructureFootprintOverride> lookup;
    lookup.reserve(overrides.size());
    for (const MapStructureFootprintOverride& overrideFact : overrides) {
        if (overrideFact.type.empty()) {
            continue;
        }

        MapStructureFootprintOverride normalized = overrideFact;
        normalized.width = std::max(1, normalized.width);
        normalized.height = std::max(1, normalized.height);
        lookup[toLowerAscii(normalized.type)] = normalized;
    }
    return lookup;
}

struct StructureOccupancyEntry {
    std::vector<std::string> ids;
    std::vector<std::string> types;
    std::vector<std::string> owners;
};

std::string structureOccupantId(const MapStructureObject& structure) {
    if (!structure.id.empty()) {
        return structure.id;
    }
    return structure.type
        + "@"
        + std::to_string(structure.x)
        + ","
        + std::to_string(structure.y);
}

int structureFootprintWidth(
    const MapStructureObject& structure,
    const std::unordered_map<std::string, MapStructureFootprintOverride>& footprintLookup
) {
    const auto footprintIt = footprintLookup.find(toLowerAscii(structure.type));
    return footprintIt == footprintLookup.end()
        ? 1
        : std::max(1, footprintIt->second.width);
}

int structureFootprintHeight(
    const MapStructureObject& structure,
    const std::unordered_map<std::string, MapStructureFootprintOverride>& footprintLookup
) {
    const auto footprintIt = footprintLookup.find(toLowerAscii(structure.type));
    return footprintIt == footprintLookup.end()
        ? 1
        : std::max(1, footprintIt->second.height);
}

std::unordered_map<int, StructureOccupancyEntry> buildStructureOccupancyLookup(
    const std::vector<MapStructureObject>& structures,
    const std::unordered_map<std::string, MapStructureFootprintOverride>& footprintLookup
) {
    std::unordered_map<int, StructureOccupancyEntry> lookup;
    for (const MapStructureObject& structure : structures) {
        if (!isMapCoordinateInBounds(structure.x, structure.y)) {
            continue;
        }

        const int width = structureFootprintWidth(structure, footprintLookup);
        const int height = structureFootprintHeight(structure, footprintLookup);
        const std::string occupantId = structureOccupantId(structure);
        for (int dy = 0; dy < height; ++dy) {
            for (int dx = 0; dx < width; ++dx) {
                const int x = structure.x + dx;
                const int y = structure.y + dy;
                if (!isMapCoordinateInBounds(x, y)) {
                    continue;
                }

                StructureOccupancyEntry& entry = lookup[mapCellIndex(x, y)];
                addUniqueString(entry.ids, occupantId);
                addUniqueString(entry.types, structure.type);
                addUniqueString(entry.owners, structure.owner);
            }
        }
    }
    return lookup;
}

bool commaSeparatedListContains(const std::string& list, const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (const std::string& token : splitCommaList(list)) {
        if (token == value) {
            return true;
        }
    }
    return false;
}

std::string footprintBuildableReasonForCell(
    const MapCellFacts* mapCell,
    bool inBounds
) {
    if (!inBounds) {
        return "outside-map-coordinate-range";
    }
    if (mapCell == nullptr) {
        return "no-map-cell-fact";
    }
    if (mapCell->isWater && !mapCell->isShore) {
        return "water-cell-not-buildable-for-normal-building";
    }
    if (mapCell->isShore) {
        return "shore-transition-not-buildable-for-normal-building";
    }
    if (mapCell->isBridge) {
        return "bridge-cell-not-buildable-for-normal-building";
    }
    if (mapCell->isCliff) {
        return "cliff-cell-not-buildable-for-normal-building";
    }
    if (mapCell->isRamp) {
        return "ramp-height-transition-not-buildable-for-normal-building";
    }
    if (mapCell->isTunnel) {
        return "tunnel-overlay-not-buildable-for-normal-building";
    }
    if (mapCell->isResourceCell) {
        return "resource-overlay-not-buildable-until-cleared";
    }
    const std::string overlayKind = mapCell->overlayResource.overlayKind;
    if (mapCell->hasOverlay && overlayKind == "wall") {
        return "wall-overlay-not-buildable-until-cleared";
    }
    if (mapCell->hasOverlay && overlayKind == "other") {
        return "overlay-present-requires-overlay-rules-before-building";
    }
    return "buildable-terrain-candidate-path-rules-still-validate";
}

bool footprintOverlayBlocksPlacement(const MapCellFacts& mapCell) {
    if (!mapCell.hasOverlay) {
        return false;
    }
    const std::string& kind = mapCell.overlayResource.overlayKind;
    return kind == "ore" || kind == "gem" || kind == "bridge" || kind == "wall"
        || kind == "other" || mapCell.isResourceCell || mapCell.isBridge;
}

MapFootprintCellFact makeFootprintCellFact(
    const MapFacts& facts,
    int x,
    int y,
    bool occupiedByFootprint,
    bool edge,
    bool approach,
    const std::string& purpose,
    const std::string& selfStructureId
) {
    MapFootprintCellFact cell;
    cell.x = x;
    cell.y = y;
    cell.cellIndex = isMapCoordinateInBounds(x, y) ? mapCellIndex(x, y) : -1;
    cell.inBounds = cell.cellIndex >= 0;
    cell.occupiedByFootprint = occupiedByFootprint;
    cell.edge = edge;
    cell.approach = approach;
    cell.purpose = purpose;

    if (!cell.inBounds) {
        cell.blockedReason = "outside-map-coordinate-range";
        cell.buildable = false;
        cell.buildableReason = footprintBuildableReasonForCell(nullptr, false);
        cell.fallbackHint = "try nearest in-bounds edge/ring cell";
        return cell;
    }

    const auto iterator = facts.cellIndexLookup.find(cell.cellIndex);
    if (iterator != facts.cellIndexLookup.end() && iterator->second < facts.cells.size()) {
        const MapCellFacts& mapCell = facts.cells[iterator->second];
        cell.hasMapCell = true;
        cell.heightLevel = mapCell.heightLevel;
        cell.isWater = mapCell.isWater;
        cell.isShore = mapCell.isShore;
        cell.isBridge = mapCell.isBridge;
        cell.isCliff = mapCell.isCliff;
        cell.isRamp = mapCell.isRamp;
        cell.isTunnel = mapCell.isTunnel;
        cell.isResourceCell = mapCell.isResourceCell;
        cell.hasOverlay = mapCell.hasOverlay;
        cell.isOre = mapCell.isOre;
        cell.isGem = mapCell.isGem;
        cell.overlayKind = mapCell.overlayResource.overlayKind;
        cell.isWall = cell.overlayKind == "wall";
        cell.isOverlayBlocker = footprintOverlayBlocksPlacement(mapCell);
        cell.occupiedByStructure = mapCell.occupiedByStructure;
        cell.occupyingStructureCount = mapCell.occupyingStructureCount;
        cell.occupyingStructureIds = mapCell.occupyingStructureIds;
        cell.occupyingStructureTypes = mapCell.occupyingStructureTypes;
        cell.occupyingStructureOwners = mapCell.occupyingStructureOwners;
        const bool occupiedBySelf =
            commaSeparatedListContains(mapCell.occupyingStructureIds, selfStructureId);
        cell.occupiedByOtherStructure =
            mapCell.occupiedByStructure && (!occupiedBySelf || selfStructureId.empty());
        cell.occupancyReason =
            mapCell.occupiedByStructure
                ? (cell.occupiedByOtherStructure
                    ? "occupied-by-other-structure:" + mapCell.occupyingStructureIds
                    : "occupied-by-target-structure:" + mapCell.occupyingStructureIds)
                : "unoccupied";
        cell.overlayIndex = mapCell.hasOverlay ? mapCell.overlay.overlayIndex : -1;
        cell.overlayFrame = mapCell.hasOverlay ? mapCell.overlay.frameIndex : -1;
        cell.overlayResourceFrame =
            mapCell.hasOverlay ? mapCell.overlayResource.resourceFrame : -1;
        cell.overlayFallbackFrame =
            mapCell.hasOverlay ? mapCell.overlayResource.fallbackFrame : -1;
        cell.overlayType = mapCell.hasOverlay ? mapCell.overlay.type : std::string{};
        cell.overlayImage = mapCell.hasOverlay
            ? (mapCell.overlayResource.image.empty()
                ? mapCell.overlay.image
                : mapCell.overlayResource.image)
            : std::string{};
        cell.terrainClass = mapCell.terrainClass;
        cell.passabilityHint = mapCell.passabilityHint;
        cell.buildableReason = footprintBuildableReasonForCell(&mapCell, true);
        cell.buildable = cell.buildableReason.rfind("buildable-", 0) == 0;
        if (cell.occupiedByOtherStructure) {
            cell.buildable = false;
            cell.buildableReason = "occupied-by-other-structure-not-buildable";
        } else if (!occupiedByFootprint && cell.occupiedByStructure) {
            cell.buildable = false;
            cell.buildableReason = "occupied-by-structure-not-buildable";
        }
        if (occupiedByFootprint) {
            cell.blockedReason = "occupied-by-target-footprint";
            cell.fallbackHint = "use approach ring for target interaction";
        } else if (cell.occupiedByStructure) {
            cell.blockedReason = "occupied-by-structure";
            cell.fallbackHint = "try another purpose-specific approach ring cell";
        } else {
            cell.blockedReason = "path-algorithm-must-validate";
            cell.fallbackHint = "candidate target-adjacent cell";
        }
        return cell;
    }

    cell.blockedReason = "no-map-cell-fact";
    cell.buildable = false;
    cell.buildableReason = footprintBuildableReasonForCell(nullptr, true);
    cell.fallbackHint = "try another footprint edge/ring cell";
    return cell;
}

MapIsoDiamondBounds structureFootprintIsoDiamondBounds(
    const std::vector<MapFootprintCellFact>& footprintCells
) {
    constexpr int tileWidth = 60;
    constexpr int tileHeight = 30;
    const int halfWidth = tileWidth / 2;
    const int halfHeight = tileHeight / 2;

    MapIsoDiamondBounds bounds;
    bounds.coordinateSpace =
        "relative iso screen pixels; tile=60x30; no viewport/camera margin applied";
    bool initialized = false;
    const auto includePoint = [&bounds, &initialized](int x, int y) {
        if (!initialized) {
            bounds.minX = x;
            bounds.maxX = x;
            bounds.minY = y;
            bounds.maxY = y;
            initialized = true;
            return;
        }
        bounds.minX = std::min(bounds.minX, x);
        bounds.maxX = std::max(bounds.maxX, x);
        bounds.minY = std::min(bounds.minY, y);
        bounds.maxY = std::max(bounds.maxY, y);
    };

    for (const MapFootprintCellFact& cell : footprintCells) {
        if (!cell.inBounds) {
            continue;
        }
        const int centerX = (cell.x - cell.y) * halfWidth;
        const int centerY = (cell.x + cell.y - cell.heightLevel) * halfHeight;
        includePoint(centerX, centerY - halfHeight);
        includePoint(centerX + halfWidth, centerY);
        includePoint(centerX, centerY + halfHeight);
        includePoint(centerX - halfWidth, centerY);
    }

    bounds.valid = initialized;
    if (bounds.valid) {
        bounds.width = bounds.maxX - bounds.minX;
        bounds.height = bounds.maxY - bounds.minY;
    }
    return bounds;
}

void finalizeStructureFootprintLegality(MapStructureAnchorFact& anchor) {
    anchor.footprintBuildableCellCount = 0;
    anchor.footprintIllegalCellCount = 0;
    anchor.footprintWaterCellCount = 0;
    anchor.footprintShoreCellCount = 0;
    anchor.footprintBridgeCellCount = 0;
    anchor.footprintResourceCellCount = 0;
    anchor.footprintOverlayBlockerCellCount = 0;
    anchor.footprintOccupiedCellCount = 0;
    anchor.footprintOtherStructureCellCount = 0;

    std::vector<std::string> illegalReasons;
    for (const MapFootprintCellFact& cell : anchor.footprintCells) {
        if (cell.buildable) {
            ++anchor.footprintBuildableCellCount;
        } else {
            ++anchor.footprintIllegalCellCount;
            addUniqueString(illegalReasons, cell.buildableReason);
        }
        if (cell.isWater) {
            ++anchor.footprintWaterCellCount;
        }
        if (cell.isShore) {
            ++anchor.footprintShoreCellCount;
        }
        if (cell.isBridge) {
            ++anchor.footprintBridgeCellCount;
        }
        if (cell.isResourceCell) {
            ++anchor.footprintResourceCellCount;
        }
        if (cell.isOverlayBlocker) {
            ++anchor.footprintOverlayBlockerCellCount;
        }
        if (cell.occupiedByStructure) {
            ++anchor.footprintOccupiedCellCount;
        }
        if (cell.occupiedByOtherStructure) {
            ++anchor.footprintOtherStructureCellCount;
        }
    }

    anchor.footprintBuildable =
        !anchor.footprintCells.empty() && anchor.footprintIllegalCellCount == 0;
    anchor.footprintLegality =
        anchor.footprintBuildable
            ? "all-footprint-cells-buildable-candidates"
            : "illegal-footprint-cells=" + joinStrings(illegalReasons);
}

std::vector<MapFootprintCellFact> structureFootprintCellsForAnchor(
    const MapFacts& facts,
    const MapStructureAnchorFact& anchor
) {
    std::vector<MapFootprintCellFact> cells;
    const int width = std::max(1, anchor.footprintWidth);
    const int height = std::max(1, anchor.footprintHeight);
    cells.reserve(static_cast<std::size_t>(width * height));
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            const bool edge =
                dx == 0 || dy == 0 || dx == width - 1 || dy == height - 1;
            cells.push_back(makeFootprintCellFact(
                facts,
                anchor.x + dx,
                anchor.y + dy,
                true,
                edge,
                false,
                "footprint",
                anchor.id
            ));
        }
    }
    return cells;
}

std::vector<MapFootprintCellFact> structureEdgeCellsForAnchor(
    const MapFacts& facts,
    const MapStructureAnchorFact& anchor
) {
    std::vector<MapFootprintCellFact> cells;
    for (const MapFootprintCellFact& cell :
        structureFootprintCellsForAnchor(facts, anchor)) {
        if (cell.edge) {
            cells.push_back(cell);
        }
    }
    return cells;
}

std::vector<MapFootprintCellFact> structureEdgeCellsFromFootprint(
    const std::vector<MapFootprintCellFact>& footprintCells
) {
    std::vector<MapFootprintCellFact> cells;
    cells.reserve(footprintCells.size());
    for (const MapFootprintCellFact& cell : footprintCells) {
        if (cell.edge) {
            cells.push_back(cell);
        }
    }
    return cells;
}

std::vector<MapFootprintCellFact> structureApproachCellsForAnchor(
    const MapFacts& facts,
    const MapStructureAnchorFact& anchor,
    const std::string& purpose
) {
    std::vector<MapFootprintCellFact> cells;
    const int width = std::max(1, anchor.footprintWidth);
    const int height = std::max(1, anchor.footprintHeight);
    cells.reserve(static_cast<std::size_t>((width + height + 2) * 2));
    for (int y = anchor.y - 1; y <= anchor.y + height; ++y) {
        for (int x = anchor.x - 1; x <= anchor.x + width; ++x) {
            const bool inside =
                x >= anchor.x && x < anchor.x + width
                && y >= anchor.y && y < anchor.y + height;
            if (inside) {
                continue;
            }

            const bool edge =
                x == anchor.x - 1 || x == anchor.x + width
                || y == anchor.y - 1 || y == anchor.y + height;
            cells.push_back(makeFootprintCellFact(
                facts,
                x,
                y,
                false,
                edge,
                true,
                purpose,
                anchor.id
            ));
        }
    }
    return cells;
}

std::vector<MapFootprintCellFact> structureCellsWithPurpose(
    const std::vector<MapFootprintCellFact>& sourceCells,
    const std::string& purpose
) {
    std::vector<MapFootprintCellFact> cells = sourceCells;
    for (MapFootprintCellFact& cell : cells) {
        cell.purpose = purpose;
    }
    return cells;
}

std::string normalizeApproachPurpose(const std::string& purpose) {
    const std::string lower = toLowerAscii(purpose);
    if (lower == "engineer" || lower == "engineer-enter" || lower == "enter") {
        return "engineer-enter";
    }
    if (lower == "repair") {
        return "repair";
    }
    if (lower == "occupy") {
        return "occupy";
    }
    if (lower == "attack") {
        return "attack";
    }
    return purpose.empty() ? "attack/engineer-enter/repair/occupy" : purpose;
}

bool isYesValue(const std::string& value) {
    return isOneValue(value);
}

std::vector<std::string> loadOverlayTypeNames(
    const IniFile* rulesIni,
    std::vector<std::string>& warnings
) {
    std::vector<std::string> overlayTypeNames;

    if (rulesIni == nullptr) {
        return overlayTypeNames;
    }

    const IniFile::Section* section = rulesIni->getSection("OverlayTypes");
    if (section == nullptr) {
        warnings.push_back("Rules file is missing [OverlayTypes]; overlay names are unavailable.");
        return overlayTypeNames;
    }

    overlayTypeNames.reserve(section->size());
    for (const IniFile::KeyValue& entry : *section) {
        // OverlayPack stores the zero-based ordinal in [OverlayTypes]. The
        // numeric INI keys contain intentional gaps and are not enum values.
        overlayTypeNames.push_back(entry.second);
    }

    return overlayTypeNames;
}

bool isGemOverlayName(const std::string& overlayName) {
    return startsWith(toLowerAscii(overlayName), "gem");
}

bool isOreOverlayName(const std::string& overlayName) {
    return startsWith(toLowerAscii(overlayName), "tib");
}

bool isGemOverlay(
    const std::string& overlayTypeName,
    const std::string& overlayImageName = ""
) {
    return isGemOverlayName(overlayTypeName) || isGemOverlayName(overlayImageName);
}

bool isOreOverlay(
    const IniFile* rulesIni,
    const std::string& overlayTypeName,
    const std::string& overlayImageName = ""
) {
    if (isGemOverlay(overlayTypeName, overlayImageName)) {
        return false;
    }

    if (isOreOverlayName(overlayTypeName) || isOreOverlayName(overlayImageName)) {
        return true;
    }

    if (rulesIni == nullptr || overlayTypeName.empty()) {
        return false;
    }

    return isYesValue(rulesIni->getValue(overlayTypeName, "Tiberium"));
}

std::string lookupOverlayTypeName(
    const std::vector<std::string>& overlayTypeNames,
    int overlayIndex
) {
    if (overlayIndex < 0) {
        return "";
    }

    const std::size_t index = static_cast<std::size_t>(overlayIndex);
    if (index < overlayTypeNames.size() && !overlayTypeNames[index].empty()) {
        return overlayTypeNames[index];
    }

    if (overlayIndex == 0) {
        return "OVERLAY_0";
    }

    return "";
}

std::string lookupOverlayImageName(
    const IniFile* rulesIni,
    const std::string& overlayTypeName
) {
    if (rulesIni == nullptr || overlayTypeName.empty()) {
        return "";
    }

    return rulesIni->getValue(overlayTypeName, "Image", "");
}

void parseOverlayCells(
    const std::vector<unsigned char>& overlayPackPayload,
    const std::vector<unsigned char>& overlayDataPayload,
    const IniFile* rulesIni,
    const MapRectFields& mapSize,
    std::vector<MapOverlayCell>& overlayCells,
    MapOverlaySummary& overlaySummary,
    std::vector<std::string>& warnings
) {
    overlayCells.clear();
    overlaySummary = MapOverlaySummary{};

    if (overlayPackPayload.empty()) {
        return;
    }

    constexpr std::size_t maxOverlayCells = 512 * 512;
    std::size_t bytesPerCell = 1;

    if (overlayPackPayload.size() == maxOverlayCells * 2) {
        bytesPerCell = 2;
    } else if (overlayPackPayload.size() > maxOverlayCells) {
        warnings.push_back("OverlayPack decoded size is "
            + std::to_string(overlayPackPayload.size())
            + " bytes; expected 262144 or 524288.");
    }

    const std::size_t overlayPackCellCount = overlayPackPayload.size() / bytesPerCell;
    const std::size_t cellCount = std::min(overlayPackCellCount, maxOverlayCells);

    if (!overlayDataPayload.empty() && overlayDataPayload.size() < cellCount) {
        warnings.push_back("OverlayDataPack decoded size is "
            + std::to_string(overlayDataPayload.size())
            + " bytes for "
            + std::to_string(cellCount)
            + " overlay cells; missing frames default to 0.");
    } else if (overlayDataPayload.size() > maxOverlayCells) {
        warnings.push_back("OverlayDataPack decoded size is "
            + std::to_string(overlayDataPayload.size())
            + " bytes; expected at most 262144.");
    }

    overlaySummary.cellCount = cellCount;
    overlaySummary.bytesPerCell = bytesPerCell;

    const std::vector<std::string> overlayTypeNames =
        loadOverlayTypeNames(rulesIni, warnings);
    std::unordered_map<int, std::size_t> typeCounts;

    const auto appendOverlayCell = [&](std::size_t cellIndex, int x, int y) {
        int overlayIndex = -1;

        if (bytesPerCell == 2) {
            overlayIndex = static_cast<int>(readUInt16LE(
                overlayPackPayload,
                cellIndex * bytesPerCell
            ));

            if (overlayIndex == 0xFFFF) {
                return;
            }
        } else {
            overlayIndex = overlayPackPayload[cellIndex];

            if (overlayIndex == 0xFF) {
                return;
            }
        }

        std::string overlayTypeName =
            lookupOverlayTypeName(overlayTypeNames, overlayIndex);
        std::string overlayImageName =
            lookupOverlayImageName(rulesIni, overlayTypeName);
        bool gemOverlay = isGemOverlay(overlayTypeName, overlayImageName);
        bool oreOverlay = isOreOverlay(rulesIni, overlayTypeName, overlayImageName);

        const std::string resourceOverlayName =
            overlayTypeName.empty()
                ? resourceOverlayNameForIndex(overlayIndex)
                : "";
        if (!resourceOverlayName.empty()) {
            overlayTypeName = resourceOverlayName;
            overlayImageName = resourceOverlayName;
            gemOverlay = isRa2GemOverlayIndex(overlayIndex);
            oreOverlay = !gemOverlay;
        } else if (isBridgePlaceholderOverlayIndex(overlayIndex)) {
            oreOverlay = false;
            gemOverlay = false;
        }

        MapOverlayCell cell;
        cell.cellIndex = static_cast<int>(cellIndex);
        cell.x = x;
        cell.y = y;
        cell.overlayIndex = overlayIndex;
        cell.frameIndex =
            cellIndex < overlayDataPayload.size()
                ? overlayDataPayload[cellIndex]
                : 0;
        cell.frame = cell.frameIndex;
        cell.fallbackFrame = cell.frameIndex;
        cell.overlayData = cell.frameIndex;
        cell.type = overlayTypeName;
        cell.image = overlayImageName;
        cell.isOre = oreOverlay;
        cell.isGem = gemOverlay;
        if (cell.isOre || cell.isGem) {
            cell.frame = overlayResourceFrameForFact(cell);
        }
        overlayCells.push_back(cell);

        ++overlaySummary.nonEmptyCount;
        typeCounts[overlayIndex] += 1;

        if (overlayTypeName.empty()) {
            ++overlaySummary.unknownTypeCount;
        }

        if (oreOverlay) {
            ++overlaySummary.oreCount;
        }

        if (gemOverlay) {
            ++overlaySummary.gemCount;
        }
    };

    if (!mapSize.valid) {
        warnings.push_back(
            "Map [Map] Size is unavailable; OverlayPack uses raw 512x512 coordinates."
        );
    }
    for (std::size_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        appendOverlayCell(
            cellIndex,
            static_cast<int>(cellIndex % MapCoordinateLimit),
            static_cast<int>(cellIndex / MapCoordinateLimit)
        );
    }

    overlaySummary.typeCounts.reserve(typeCounts.size());

    for (const auto& [overlayIndex, count] : typeCounts) {
        MapOverlayTypeCount typeCount;
        typeCount.overlayIndex = overlayIndex;
        typeCount.type = lookupOverlayTypeName(overlayTypeNames, overlayIndex);
        typeCount.count = count;
        const std::string imageName = lookupOverlayImageName(rulesIni, typeCount.type);
        typeCount.isOre = isOreOverlay(rulesIni, typeCount.type, imageName);
        typeCount.isGem = isGemOverlay(typeCount.type, imageName);
        overlaySummary.typeCounts.push_back(typeCount);
    }

    std::sort(
        overlaySummary.typeCounts.begin(),
        overlaySummary.typeCounts.end(),
        [](const MapOverlayTypeCount& left, const MapOverlayTypeCount& right) {
            if (left.count != right.count) {
                return left.count > right.count;
            }

            return left.overlayIndex < right.overlayIndex;
        }
    );
}

MapPosition decodeCellPosition(
    const std::string& value,
    const std::string& context,
    std::vector<std::string>& warnings
) {
    const int cell = parseIntValue(value, context + " cell", warnings);

    if (cell < 0) {
        return {};
    }

    const int x = cell % ObjectCellCoordinateBase;
    const int y = cell / ObjectCellCoordinateBase;
    if (x < 0
        || y < 0
        || x >= MapCoordinateLimit
        || y >= MapCoordinateLimit
    ) {
        warnings.push_back(context + " cell is outside the 512x512 map grid: "
            + std::to_string(cell));
        return {};
    }

    MapPosition position;
    position.cell = cell;
    position.x = x;
    position.y = y;
    position.valid = true;
    return position;
}

MapPosition makePositionFromXY(
    int x,
    int y,
    const std::string& context,
    std::vector<std::string>& warnings
) {
    if (x < 0 || y < 0) {
        return {};
    }

    if (x >= MapCoordinateLimit || y >= MapCoordinateLimit) {
        warnings.push_back(context + " coordinates are outside the 512x512 map grid: "
            + std::to_string(x)
            + ","
            + std::to_string(y));
        return {};
    }

    if (y > (std::numeric_limits<int>::max() - x) / ObjectCellCoordinateBase) {
        warnings.push_back(context + " coordinates overflow the object cell encoding.");
        return {};
    }

    MapPosition position;
    position.x = x;
    position.y = y;
    position.cell = (y * ObjectCellCoordinateBase) + x;
    position.valid = true;
    return position;
}

void invalidateObjectCoordinates(int& x, int& y, const MapPosition& position) {
    if (!position.valid) {
        x = -1;
        y = -1;
    }
}

void parseWaypointsSection(
    const IniFile& ini,
    std::vector<MapWaypoint>& waypoints,
    std::vector<std::string>& warnings
) {
    waypoints.clear();

    const IniFile::Section* section = ini.getSection("Waypoints");
    if (section == nullptr) {
        return;
    }

    waypoints.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        MapWaypoint waypoint;
        waypoint.id = entry.first;
        waypoint.cellValue = entry.second;
        waypoint.position = decodeCellPosition(
            entry.second,
            "Waypoint [" + entry.first + "]",
            warnings
        );

        waypoints.push_back(waypoint);
    }
}

void parseSmudgeSection(
    const IniFile& ini,
    std::vector<MapSmudgeObject>& smudges,
    std::vector<std::string>& warnings
) {
    smudges.clear();

    const IniFile::Section* section = ini.getSection("Smudge");
    if (section == nullptr) {
        return;
    }

    smudges.reserve(section->size());
    for (const IniFile::KeyValue& entry : *section) {
        const std::vector<std::string> fields = splitCommaList(entry.second);
        const std::string context = "Smudge [" + entry.first + "]";
        if (!requireFieldCount(fields, 4, context, warnings)) {
            continue;
        }

        MapSmudgeObject object;
        object.id = entry.first;
        object.type = fields[0];
        // Map records use the same raw RX,RY order as IsoMapPack5 and
        // ordinary object placement: type,x,y,data.
        object.x = parseIntValue(fields[1], context + " x", warnings);
        object.y = parseIntValue(fields[2], context + " y", warnings);
        object.data = parseIntValue(fields[3], context + " data", warnings);
        object.position = makePositionFromXY(
            object.x,
            object.y,
            context,
            warnings
        );
        invalidateObjectCoordinates(object.x, object.y, object.position);

        if (object.type.empty()) {
            warnings.push_back(context + " type is empty.");
        }
        smudges.push_back(object);
    }
}

std::string mapObjectRuleValue(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& type,
    const std::string& key
) {
    const std::string mapValue = mapIni.getValue(type, key);
    if (!mapValue.empty()) {
        return mapValue;
    }
    return rulesIni == nullptr ? std::string{} : rulesIni->getValue(type, key);
}

int normalizedTerrainOccupationBits(int bits) {
    constexpr int occupationBitCount = 8;
    return ((bits % occupationBitCount) + occupationBitCount)
        % occupationBitCount;
}

std::vector<int> terrainOccupiedSubCells(int occupationBits) {
    constexpr int rightBit = 1;
    constexpr int leftBit = 2;
    constexpr int bottomBit = 4;
    const int bits = normalizedTerrainOccupationBits(occupationBits);

    std::vector<int> occupied{0, 1};
    if ((bits & rightBit) != 0) {
        occupied.push_back(2);
    }
    if ((bits & leftBit) != 0) {
        occupied.push_back(3);
    }
    if ((bits & bottomBit) != 0) {
        occupied.push_back(4);
    }
    return occupied;
}

int resolveTerrainRuleInt(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& section,
    const std::string& key,
    int defaultValue,
    std::vector<std::string>& warnings
) {
    const std::string value = mapObjectRuleValue(
        mapIni,
        rulesIni,
        section,
        key
    );
    if (value.empty()) {
        return defaultValue;
    }
    return parseIntValue(
        value,
        "Terrain rules [" + section + "] " + key,
        warnings
    );
}

int resolveTerrainStrength(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& type,
    std::vector<std::string>& warnings
) {
    int strength = resolveTerrainRuleInt(
        mapIni,
        rulesIni,
        type,
        "Strength",
        0,
        warnings
    );
    if (strength == 0) {
        strength = resolveTerrainRuleInt(
            mapIni,
            rulesIni,
            "General",
            "TreeStrength",
            0,
            warnings
        );
    }
    return std::max(1, strength);
}

int resolveTerrainOccupationBits(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& type,
    const std::string& key,
    std::vector<std::string>& warnings
) {
    return normalizedTerrainOccupationBits(resolveTerrainRuleInt(
        mapIni,
        rulesIni,
        type,
        key,
        7,
        warnings
    ));
}

void parseTerrainSection(
    const IniFile& ini,
    const IniFile* rulesIni,
    const std::string& theater,
    std::vector<MapTerrainObject>& terrainObjects,
    std::vector<std::string>& warnings
) {
    terrainObjects.clear();

    const IniFile::Section* section = ini.getSection("Terrain");
    if (section == nullptr) {
        return;
    }

    terrainObjects.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        MapTerrainObject object;
        object.mapId = entry.first;
        object.cell = entry.first;
        object.type = trimString(entry.second);
        object.strength = resolveTerrainStrength(
            ini,
            rulesIni,
            object.type,
            warnings
        );
        object.health = object.strength;
        object.immune = isOneValue(mapObjectRuleValue(
            ini,
            rulesIni,
            object.type,
            "Immune"
        ));
        object.alive = object.health > 0;
        object.temperateOccupationBits = resolveTerrainOccupationBits(
            ini,
            rulesIni,
            object.type,
            "TemperateOccupationBits",
            warnings
        );
        object.snowOccupationBits = resolveTerrainOccupationBits(
            ini,
            rulesIni,
            object.type,
            "SnowOccupationBits",
            warnings
        );
        object.occupationBits = toLowerAscii(theater) == "snow"
            ? object.snowOccupationBits
            : object.temperateOccupationBits;
        object.occupiedSubCells = terrainOccupiedSubCells(
            object.occupationBits
        );
        object.position = decodeCellPosition(
            entry.first,
            "Terrain [" + entry.first + "]",
            warnings
        );

        terrainObjects.push_back(object);
    }
}

int resolveMapObjectStrength(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& type,
    std::vector<std::string>& warnings
) {
    const std::string value = mapObjectRuleValue(
        mapIni,
        rulesIni,
        type,
        "Strength"
    );
    if (value.empty()) {
        return -1;
    }

    const int strength = parseIntValue(
        value,
        "Map object rules [" + type + "] Strength",
        warnings
    );
    if (strength <= 0) {
        if (strength == 0) {
            warnings.push_back(
                "Map object rules [" + type + "] Strength must be positive."
            );
        }
        return -1;
    }
    return strength;
}

bool resolveMapObjectLeaveRubble(
    const IniFile& mapIni,
    const IniFile* rulesIni,
    const std::string& type
) {
    return isOneValue(mapObjectRuleValue(
        mapIni,
        rulesIni,
        type,
        "LeaveRubble"
    ));
}

void parseStructuresSection(
    const IniFile& ini,
    const IniFile* rulesIni,
    std::vector<MapStructureObject>& structures,
    std::vector<std::string>& warnings
) {
    structures.clear();

    const IniFile::Section* section = ini.getSection("Structures");
    if (section == nullptr) {
        return;
    }

    structures.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        const std::vector<std::string> fields = splitCommaList(entry.second);
        const std::string context = "Structure [" + entry.first + "]";

        if (!requireFieldCount(fields, 7, context, warnings)) {
            continue;
        }

        MapStructureObject object;
        object.id = entry.first;
        object.owner = fields[0];
        object.type = fields[1];
        object.health = parseIntValue(fields[2], context + " health", warnings);
        object.strength = resolveMapObjectStrength(
            ini,
            rulesIni,
            object.type,
            warnings
        );
        object.x = parseIntValue(fields[3], context + " x", warnings);
        object.y = parseIntValue(fields[4], context + " y", warnings);
        object.facing = parseIntValue(fields[5], context + " facing", warnings);
        object.tag = fields[6];
        object.leaveRubble = resolveMapObjectLeaveRubble(
            ini,
            rulesIni,
            object.type
        );
        object.position = makePositionFromXY(object.x, object.y, context, warnings);
        invalidateObjectCoordinates(object.x, object.y, object.position);

        structures.push_back(object);
    }
}

void parseUnitsSection(
    const IniFile& ini,
    const IniFile* rulesIni,
    std::vector<MapUnitObject>& units,
    std::vector<std::string>& warnings
) {
    units.clear();

    const IniFile::Section* section = ini.getSection("Units");
    if (section == nullptr) {
        return;
    }

    units.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        const std::vector<std::string> fields = splitCommaList(entry.second);
        const std::string context = "Unit [" + entry.first + "]";

        if (!requireFieldCount(fields, 8, context, warnings)) {
            continue;
        }

        MapUnitObject object;
        object.id = entry.first;
        object.owner = fields[0];
        object.type = fields[1];
        object.health = parseIntValue(fields[2], context + " health", warnings);
        object.strength = resolveMapObjectStrength(
            ini,
            rulesIni,
            object.type,
            warnings
        );
        object.x = parseIntValue(fields[3], context + " x", warnings);
        object.y = parseIntValue(fields[4], context + " y", warnings);
        object.facing = parseIntValue(fields[5], context + " facing", warnings);
        object.mission = fields[6];
        object.tag = fields[7];
        if (fields.size() > 8) {
            object.veterancy = std::clamp(
                parseIntValue(fields[8], context + " veterancy", warnings),
                0,
                2
            );
        }
        object.onBridge = fields.size() > 10 && isOneValue(fields[10]);
        object.position = makePositionFromXY(object.x, object.y, context, warnings);
        invalidateObjectCoordinates(object.x, object.y, object.position);

        units.push_back(object);
    }
}

void parseInfantrySection(
    const IniFile& ini,
    const IniFile* rulesIni,
    std::vector<MapInfantryObject>& infantry,
    std::vector<std::string>& warnings
) {
    infantry.clear();

    const IniFile::Section* section = ini.getSection("Infantry");
    if (section == nullptr) {
        return;
    }

    infantry.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        const std::vector<std::string> fields = splitCommaList(entry.second);
        const std::string context = "Infantry [" + entry.first + "]";

        if (!requireFieldCount(fields, 9, context, warnings)) {
            continue;
        }

        MapInfantryObject object;
        object.id = entry.first;
        object.owner = fields[0];
        object.type = fields[1];
        object.health = parseIntValue(fields[2], context + " health", warnings);
        object.strength = resolveMapObjectStrength(
            ini,
            rulesIni,
            object.type,
            warnings
        );
        object.x = parseIntValue(fields[3], context + " x", warnings);
        object.y = parseIntValue(fields[4], context + " y", warnings);
        object.subCell = parseIntValue(fields[5], context + " subCell", warnings);
        object.mission = fields[6];
        object.facing = parseIntValue(fields[7], context + " facing", warnings);
        object.tag = fields[8];
        if (fields.size() > 9) {
            object.veterancy = std::clamp(
                parseIntValue(fields[9], context + " veterancy", warnings),
                0,
                2
            );
        }
        object.onBridge = fields.size() > 11 && isOneValue(fields[11]);
        object.position = makePositionFromXY(object.x, object.y, context, warnings);
        invalidateObjectCoordinates(object.x, object.y, object.position);

        infantry.push_back(object);
    }
}

void parseAircraftSection(
    const IniFile& ini,
    const IniFile* rulesIni,
    std::vector<MapAircraftObject>& aircraft,
    std::vector<std::string>& warnings
) {
    aircraft.clear();

    const IniFile::Section* section = ini.getSection("Aircraft");
    if (section == nullptr) {
        return;
    }

    aircraft.reserve(section->size());

    for (const IniFile::KeyValue& entry : *section) {
        const std::vector<std::string> fields = splitCommaList(entry.second);
        const std::string context = "Aircraft [" + entry.first + "]";

        if (!requireFieldCount(fields, 8, context, warnings)) {
            continue;
        }

        MapAircraftObject object;
        object.id = entry.first;
        object.owner = fields[0];
        object.type = fields[1];
        object.health = parseIntValue(fields[2], context + " health", warnings);
        object.strength = resolveMapObjectStrength(
            ini,
            rulesIni,
            object.type,
            warnings
        );
        object.x = parseIntValue(fields[3], context + " x", warnings);
        object.y = parseIntValue(fields[4], context + " y", warnings);
        object.facing = parseIntValue(fields[5], context + " facing", warnings);
        object.mission = fields[6];
        object.tag = fields[7];
        if (fields.size() > 8) {
            object.veterancy = std::clamp(
                parseIntValue(fields[8], context + " veterancy", warnings),
                0,
                2
            );
        }
        object.position = makePositionFromXY(object.x, object.y, context, warnings);
        invalidateObjectCoordinates(object.x, object.y, object.position);

        aircraft.push_back(object);
    }
}

std::vector<const MapCellFacts*> cellsFromOffsets(
    const MapFacts* facts,
    const std::vector<std::size_t>& offsets
) {
    std::vector<const MapCellFacts*> cells;
    if (facts == nullptr) {
        return cells;
    }

    cells.reserve(offsets.size());
    for (const std::size_t cellOffset : offsets) {
        if (cellOffset < facts->cells.size()) {
            cells.push_back(&facts->cells[cellOffset]);
        }
    }
    return cells;
}

void appendMapFactsHashByte(std::uint64_t& hash, unsigned char value) {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ULL;
}

void appendMapFactsHashString(std::uint64_t& hash, const std::string& value) {
    for (const unsigned char character : value) {
        appendMapFactsHashByte(hash, character);
    }
    appendMapFactsHashByte(hash, 0xffU);
}

void appendMapFactsHashInt(std::uint64_t& hash, int value) {
    appendMapFactsHashString(hash, std::to_string(value));
}

void appendMapFactsHashSize(std::uint64_t& hash, std::size_t value) {
    appendMapFactsHashString(hash, std::to_string(value));
}

void appendMapFactsHashBool(std::uint64_t& hash, bool value) {
    appendMapFactsHashByte(hash, value ? 1U : 0U);
}

void appendMapFactsHashDouble(std::uint64_t& hash, double value) {
    // Treat signed zero as one semantic value so equivalent INI spellings
    // produce the same snapshot identity.
    if (value == 0.0) {
        value = 0.0;
    }
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int shift = 0; shift < 64; shift += 8) {
        appendMapFactsHashByte(
            hash,
            static_cast<unsigned char>((bits >> shift) & 0xffU)
        );
    }
}

void appendMapFactsHashLightingProfile(
    std::uint64_t& hash,
    const MapLightingProfile& profile
) {
    appendMapFactsHashDouble(hash, profile.ambient);
    appendMapFactsHashDouble(hash, profile.red);
    appendMapFactsHashDouble(hash, profile.green);
    appendMapFactsHashDouble(hash, profile.blue);
    appendMapFactsHashDouble(hash, profile.ground);
    appendMapFactsHashDouble(hash, profile.level);
}

std::uint64_t computeMapFactsHash(const MapFacts& facts) {
    std::uint64_t hash = 1469598103934665603ULL;
    appendMapFactsHashString(hash, facts.schemaVersion);
    appendMapFactsHashString(hash, facts.theater);
    appendMapFactsHashInt(hash, facts.width);
    appendMapFactsHashInt(hash, facts.height);
    appendMapFactsHashBool(hash, facts.lighting.sectionPresent);
    appendMapFactsHashSize(hash, facts.lighting.authoredFieldCount);
    appendMapFactsHashLightingProfile(hash, facts.lighting.base);
    appendMapFactsHashLightingProfile(hash, facts.lighting.ion);
    appendMapFactsHashLightingProfile(hash, facts.lighting.dominator);
    appendMapFactsHashDouble(
        hash,
        facts.lighting.dominatorAmbientChangeRate
    );
    appendMapFactsHashSize(hash, facts.cells.size());

    for (const MapCellFacts& cell : facts.cells) {
        appendMapFactsHashInt(hash, cell.x);
        appendMapFactsHashInt(hash, cell.y);
        appendMapFactsHashInt(hash, cell.cellIndex);
        appendMapFactsHashBool(hash, cell.hasTerrain);
        appendMapFactsHashBool(hash, cell.hasOverlay);
        appendMapFactsHashInt(hash, cell.terrain.tileIndex);
        appendMapFactsHashInt(hash, cell.terrain.subTileIndex);
        appendMapFactsHashInt(hash, cell.heightLevel);
        appendMapFactsHashInt(hash, cell.overlay.overlayIndex);
        appendMapFactsHashString(hash, cell.overlay.type);
        appendMapFactsHashString(hash, cell.overlay.image);
        appendMapFactsHashInt(hash, cell.overlay.frame);
        appendMapFactsHashInt(hash, cell.overlay.fallbackFrame);
        appendMapFactsHashString(hash, cell.overlayResource.overlayKind);
        appendMapFactsHashInt(hash, cell.overlayResource.resourceFrame);
        appendMapFactsHashString(hash, cell.overlayResource.kind);
        appendMapFactsHashString(hash, cell.overlayResource.baseName);
        appendMapFactsHashString(hash, cell.overlayResource.image);
        appendMapFactsHashString(hash, cell.overlayResource.rendererLayerHint);
        appendMapFactsHashString(hash, cell.terrainClass);
        appendMapFactsHashString(hash, cell.passabilityHint);
        appendMapFactsHashString(hash, cell.theaterTile.expectedFileName);
        appendMapFactsHashBool(hash, cell.isResourceCell);
        appendMapFactsHashBool(hash, cell.isOre);
        appendMapFactsHashBool(hash, cell.isGem);
        appendMapFactsHashBool(hash, cell.isBridge);
        appendMapFactsHashBool(hash, cell.isHighBridge);
        appendMapFactsHashBool(hash, cell.isLowBridge);
        appendMapFactsHashBool(hash, cell.isWater);
        appendMapFactsHashBool(hash, cell.isShore);
        appendMapFactsHashBool(hash, cell.isCliff);
        appendMapFactsHashBool(hash, cell.isRamp);
        appendMapFactsHashBool(hash, cell.isTunnel);
        appendMapFactsHashBool(hash, cell.occupiedByStructure);
        appendMapFactsHashInt(hash, cell.occupyingStructureCount);
        appendMapFactsHashString(hash, cell.occupyingStructureIds);
        appendMapFactsHashString(hash, cell.occupyingStructureTypes);
        appendMapFactsHashString(hash, cell.occupyingStructureOwners);
    }

    appendMapFactsHashSize(hash, facts.structureAnchors.size());
    for (const MapStructureAnchorFact& anchor : facts.structureAnchors) {
        appendMapFactsHashString(hash, anchor.id);
        appendMapFactsHashString(hash, anchor.owner);
        appendMapFactsHashString(hash, anchor.type);
        appendMapFactsHashInt(hash, anchor.x);
        appendMapFactsHashInt(hash, anchor.y);
        appendMapFactsHashInt(hash, anchor.footprintWidth);
        appendMapFactsHashInt(hash, anchor.footprintHeight);
        appendMapFactsHashInt(hash, anchor.rearCellX);
        appendMapFactsHashInt(hash, anchor.rearCellY);
        appendMapFactsHashString(hash, anchor.footprintSource);
        appendMapFactsHashBool(hash, anchor.footprintIsoDiamondBounds.valid);
        appendMapFactsHashInt(hash, anchor.footprintIsoDiamondBounds.minX);
        appendMapFactsHashInt(hash, anchor.footprintIsoDiamondBounds.minY);
        appendMapFactsHashInt(hash, anchor.footprintIsoDiamondBounds.maxX);
        appendMapFactsHashInt(hash, anchor.footprintIsoDiamondBounds.maxY);
        appendMapFactsHashBool(hash, anchor.footprintBuildable);
        appendMapFactsHashInt(hash, anchor.footprintBuildableCellCount);
        appendMapFactsHashInt(hash, anchor.footprintIllegalCellCount);
        appendMapFactsHashInt(hash, anchor.footprintWaterCellCount);
        appendMapFactsHashInt(hash, anchor.footprintShoreCellCount);
        appendMapFactsHashInt(hash, anchor.footprintBridgeCellCount);
        appendMapFactsHashInt(hash, anchor.footprintResourceCellCount);
        appendMapFactsHashInt(hash, anchor.footprintOverlayBlockerCellCount);
        appendMapFactsHashInt(hash, anchor.footprintOccupiedCellCount);
        appendMapFactsHashInt(hash, anchor.footprintOtherStructureCellCount);
        appendMapFactsHashString(hash, anchor.footprintLegality);
        appendMapFactsHashString(hash, anchor.placementPreviewFormula);
        appendMapFactsHashString(hash, anchor.hitTestFormula);
        appendMapFactsHashString(hash, anchor.selectionFormula);
        appendMapFactsHashString(hash, anchor.finalStructureRectFormula);
        appendMapFactsHashSize(hash, anchor.footprintCells.size());
        for (const MapFootprintCellFact& cell : anchor.footprintCells) {
            appendMapFactsHashInt(hash, cell.x);
            appendMapFactsHashInt(hash, cell.y);
            appendMapFactsHashInt(hash, cell.cellIndex);
            appendMapFactsHashInt(hash, cell.heightLevel);
            appendMapFactsHashBool(hash, cell.inBounds);
            appendMapFactsHashBool(hash, cell.hasMapCell);
            appendMapFactsHashBool(hash, cell.buildable);
            appendMapFactsHashBool(hash, cell.isWater);
            appendMapFactsHashBool(hash, cell.isShore);
            appendMapFactsHashBool(hash, cell.isBridge);
            appendMapFactsHashBool(hash, cell.isCliff);
            appendMapFactsHashBool(hash, cell.isRamp);
            appendMapFactsHashBool(hash, cell.isTunnel);
            appendMapFactsHashBool(hash, cell.isResourceCell);
            appendMapFactsHashBool(hash, cell.hasOverlay);
            appendMapFactsHashBool(hash, cell.isOre);
            appendMapFactsHashBool(hash, cell.isGem);
            appendMapFactsHashBool(hash, cell.isWall);
            appendMapFactsHashBool(hash, cell.isOverlayBlocker);
            appendMapFactsHashBool(hash, cell.occupiedByStructure);
            appendMapFactsHashBool(hash, cell.occupiedByOtherStructure);
            appendMapFactsHashInt(hash, cell.occupyingStructureCount);
            appendMapFactsHashInt(hash, cell.overlayIndex);
            appendMapFactsHashInt(hash, cell.overlayFrame);
            appendMapFactsHashInt(hash, cell.overlayResourceFrame);
            appendMapFactsHashInt(hash, cell.overlayFallbackFrame);
            appendMapFactsHashString(hash, cell.terrainClass);
            appendMapFactsHashString(hash, cell.overlayKind);
            appendMapFactsHashString(hash, cell.overlayType);
            appendMapFactsHashString(hash, cell.overlayImage);
            appendMapFactsHashString(hash, cell.occupyingStructureIds);
            appendMapFactsHashString(hash, cell.occupyingStructureTypes);
            appendMapFactsHashString(hash, cell.occupyingStructureOwners);
            appendMapFactsHashString(hash, cell.occupancyReason);
            appendMapFactsHashString(hash, cell.buildableReason);
        }
        appendMapFactsHashSize(hash, anchor.footprintEdgeCells.size());
        appendMapFactsHashSize(hash, anchor.approachCells.size());
        appendMapFactsHashSize(hash, anchor.attackApproachCells.size());
        appendMapFactsHashSize(hash, anchor.engineerApproachCells.size());
        appendMapFactsHashSize(hash, anchor.repairApproachCells.size());
        appendMapFactsHashSize(hash, anchor.occupyApproachCells.size());
    }

    return hash;
}

} // namespace

bool decodeMapPreviewImage(
    const IniFile& ini,
    MapPreviewImage& image,
    std::string& errorMessage
) {
    image = MapPreviewImage{};
    errorMessage.clear();

    const std::string sizeValue = ini.getValue("Preview", "Size");
    if (sizeValue.empty()) {
        errorMessage = "Map preview is missing [Preview] Size.";
        return false;
    }

    std::vector<std::string> warnings;
    const MapRectFields previewRect = parseMapRectFields(
        sizeValue,
        "Map field [Preview] Size",
        warnings
    );
    if (!previewRect.valid) {
        errorMessage = "Map preview [Preview] Size must contain non-negative x/y "
            "and positive width/height.";
        if (!warnings.empty()) {
            errorMessage += " " + joinStrings(warnings, " ");
        }
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(previewRect.width);
    const std::size_t height = static_cast<std::size_t>(previewRect.height);
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        errorMessage = "Map preview dimensions overflow the pixel count.";
        return false;
    }

    const std::size_t pixelCount = width * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 3) {
        errorMessage = "Map preview dimensions overflow the RGB888 byte count.";
        return false;
    }
    const std::size_t expectedBytes = pixelCount * 3;

    if (ini.getSection("PreviewPack") == nullptr) {
        errorMessage = "Map preview is missing [PreviewPack].";
        return false;
    }

    std::vector<unsigned char> rgbPixels;
    const MapPackedSectionSummary packedSummary = summarizePackedSection(
        ini,
        "PreviewPack",
        "lzo1x",
        &rgbPixels,
        warnings
    );
    if (!packedSummary.payloadDecoded) {
        errorMessage = "Map preview [PreviewPack] Format5/LZO1X decode failed.";
        if (!warnings.empty()) {
            errorMessage += " " + joinStrings(warnings, " ");
        }
        return false;
    }

    if (rgbPixels.size() != expectedBytes) {
        errorMessage = "Map preview decoded byte count is "
            + std::to_string(rgbPixels.size())
            + "; expected exactly " + std::to_string(expectedBytes)
            + " for " + std::to_string(previewRect.width)
            + "x" + std::to_string(previewRect.height) + " RGB888 pixels.";
        return false;
    }

    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        errorMessage = "Map preview dimensions overflow the BGRA32 byte count.";
        return false;
    }

    image.width = previewRect.width;
    image.height = previewRect.height;
    image.pixels.resize(pixelCount * 4);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t sourceOffset = pixel * 3;
        const std::size_t targetOffset = pixel * 4;
        // PreviewPack stores pixels as RGB. Win32 BI_RGB 32-bit DIBs use
        // byte order B,G,R,A, so swap the red and blue channels here.
        image.pixels[targetOffset] = rgbPixels[sourceOffset + 2];
        image.pixels[targetOffset + 1] = rgbPixels[sourceOffset + 1];
        image.pixels[targetOffset + 2] = rgbPixels[sourceOffset];
        image.pixels[targetOffset + 3] = 255;
    }

    return true;
}

bool MapDatabase::loadFromIni(const IniFile& ini, const IniFile* rulesIni) {
    summary_ = MapSummary{};
    lighting_ = MapLighting{};
    waypoints_.clear();
    smudges_.clear();
    terrainObjects_.clear();
    structures_.clear();
    units_.clear();
    infantry_.clear();
    aircraft_.clear();
    isoTiles_.clear();
    overlayCells_.clear();
    overlaySummary_ = MapOverlaySummary{};
    isoMapPackSummary_ = MapPackedSectionSummary{};
    overlayPackSummary_ = MapPackedSectionSummary{};
    overlayDataPackSummary_ = MapPackedSectionSummary{};
    warnings_.clear();

    summary_.sectionCount = ini.getSectionNames().size();

    if (!hasSection(ini, "Header")) {
        warnings_.push_back("Map is missing [Header].");
    }

    if (!hasSection(ini, "Map")) {
        warnings_.push_back("Map is missing [Map].");
    }

    summary_.name = ini.getValue("Basic", "Name");
    summary_.briefing = ini.getValue("Basic", "Briefing");
    summary_.nextScenario = ini.getValue("Basic", "NextScenario");
    summary_.multiplayerOnly = ini.getValue("Basic", "MultiplayerOnly");

    summary_.theater = ini.getValue("Map", "Theater");
    summary_.size = ini.getValue("Map", "Size");
    summary_.localSize = ini.getValue("Map", "LocalSize");
    const MapRectFields mapSizeFields =
        parseMapRectFields(summary_.size, "Map field [Map] Size", warnings_);

    summary_.startX = readIntField(ini, "Header", "StartX", warnings_);
    summary_.startY = readIntField(ini, "Header", "StartY", warnings_);
    summary_.width = readIntField(ini, "Header", "Width", warnings_);
    summary_.height = readIntField(ini, "Header", "Height", warnings_);
    summary_.numberStartingPoints = readIntField(
        ini,
        "Header",
        "NumberStartingPoints",
        warnings_
    );
    summary_.coopHumanStartSpots = readIntField(
        ini,
        "Header",
        "NumCoopHumanStartSpots",
        warnings_
    );

    lighting_ = parseLightingSection(ini, warnings_);
    summary_.lightingSectionPresent = lighting_.sectionPresent;
    summary_.lightingFieldCount = lighting_.authoredFieldCount;

    summary_.houseCount = countSectionEntries(ini, "Houses");
    summary_.waypointCount = countSectionEntries(ini, "Waypoints");
    summary_.terrainCount = countSectionEntries(ini, "Terrain");
    summary_.structureCount = countSectionEntries(ini, "Structures");
    summary_.unitCount = countSectionEntries(ini, "Units");
    summary_.infantryCount = countSectionEntries(ini, "Infantry");
    summary_.aircraftCount = countSectionEntries(ini, "Aircraft");
    summary_.smudgeCount = countSectionEntries(ini, "Smudge");
    summary_.cellTagCount = countSectionEntries(ini, "CellTags");
    summary_.triggerCount = countSectionEntries(ini, "Triggers");
    summary_.eventCount = countSectionEntries(ini, "Events");
    summary_.actionCount = countSectionEntries(ini, "Actions");
    summary_.tagCount = countSectionEntries(ini, "Tags");
    summary_.scriptTypeCount = countSectionEntries(ini, "ScriptTypes");
    summary_.taskForceCount = countSectionEntries(ini, "TaskForces");
    summary_.teamTypeCount = countSectionEntries(ini, "TeamTypes");
    summary_.aiTriggerEnableCount = countSectionEntries(ini, "AITriggerTypesEnable");
    summary_.isoMapPackLineCount = countSectionEntries(ini, "IsoMapPack5");
    summary_.overlayPackLineCount = countSectionEntries(ini, "OverlayPack");
    summary_.overlayDataPackLineCount = countSectionEntries(ini, "OverlayDataPack");
    std::vector<unsigned char> isoMapPackPayload;
    std::vector<unsigned char> overlayPackPayload;
    std::vector<unsigned char> overlayDataPackPayload;
    isoMapPackSummary_ = summarizePackedSection(
        ini,
        "IsoMapPack5",
        "lzo1x",
        &isoMapPackPayload,
        warnings_
    );
    overlayPackSummary_ = summarizePackedSection(
        ini,
        "OverlayPack",
        "lcw",
        &overlayPackPayload,
        warnings_
    );
    overlayDataPackSummary_ = summarizePackedSection(
        ini,
        "OverlayDataPack",
        "lcw",
        &overlayDataPackPayload,
        warnings_
    );
    parseIsoTiles(isoMapPackPayload, isoTiles_, isoMapPackSummary_, warnings_);
    parseOverlayCells(
        overlayPackPayload,
        overlayDataPackPayload,
        rulesIni,
        mapSizeFields,
        overlayCells_,
        overlaySummary_,
        warnings_
    );

    parseWaypointsSection(ini, waypoints_, warnings_);
    parseSmudgeSection(ini, smudges_, warnings_);
    parseTerrainSection(
        ini,
        rulesIni,
        summary_.theater,
        terrainObjects_,
        warnings_
    );
    parseStructuresSection(ini, rulesIni, structures_, warnings_);
    parseUnitsSection(ini, rulesIni, units_, warnings_);
    parseInfantrySection(ini, rulesIni, infantry_, warnings_);
    parseAircraftSection(ini, rulesIni, aircraft_, warnings_);

    if (summary_.theater.empty()) {
        warnings_.push_back("Map theater is empty.");
    }

    if (summary_.size.empty()) {
        warnings_.push_back("Map Size is empty.");
    }

    if (summary_.localSize.empty()) {
        warnings_.push_back("Map LocalSize is empty.");
    }

    return true;
}

const MapSummary& MapDatabase::getSummary() const {
    return summary_;
}

const MapLighting& MapDatabase::getLighting() const {
    return lighting_;
}

const std::vector<MapWaypoint>& MapDatabase::getWaypoints() const {
    return waypoints_;
}

const std::vector<MapSmudgeObject>& MapDatabase::getSmudges() const {
    return smudges_;
}

const std::vector<MapTerrainObject>& MapDatabase::getTerrainObjects() const {
    return terrainObjects_;
}

const std::vector<MapStructureObject>& MapDatabase::getStructures() const {
    return structures_;
}

const std::vector<MapUnitObject>& MapDatabase::getUnits() const {
    return units_;
}

const std::vector<MapInfantryObject>& MapDatabase::getInfantry() const {
    return infantry_;
}

const std::vector<MapAircraftObject>& MapDatabase::getAircraft() const {
    return aircraft_;
}

const std::vector<MapIsoTile>& MapDatabase::getIsoTiles() const {
    return isoTiles_;
}

const std::vector<MapOverlayCell>& MapDatabase::getOverlayCells() const {
    return overlayCells_;
}

const MapOverlaySummary& MapDatabase::getOverlaySummary() const {
    return overlaySummary_;
}

const MapPackedSectionSummary& MapDatabase::getIsoMapPackSummary() const {
    return isoMapPackSummary_;
}

const MapPackedSectionSummary& MapDatabase::getOverlayPackSummary() const {
    return overlayPackSummary_;
}

const MapPackedSectionSummary& MapDatabase::getOverlayDataPackSummary() const {
    return overlayDataPackSummary_;
}

const std::vector<std::string>& MapDatabase::getWarnings() const {
    return warnings_;
}

MapFacts MapDatabase::buildMapFacts(
    const TheaterDatabase* theaterDatabase,
    const std::string& tileRootPath
) const {
    return buildMapFacts(theaterDatabase, tileRootPath, {});
}

MapFacts MapDatabase::buildMapFacts(
    const TheaterDatabase* theaterDatabase,
    const std::string& tileRootPath,
    const std::vector<MapStructureFootprintOverride>& footprintOverrides
) const {
    MapFacts facts;
    facts.theater = summary_.theater;
    facts.width = summary_.width;
    facts.height = summary_.height;
    facts.lighting = lighting_;
    const std::unordered_map<std::string, MapStructureFootprintOverride> footprintLookup =
        buildFootprintOverrideLookup(footprintOverrides);
    const std::unordered_map<int, StructureOccupancyEntry> structureOccupancyLookup =
        buildStructureOccupancyLookup(structures_, footprintLookup);

    if (theaterDatabase == nullptr) {
        facts.warnings.push_back(
            "MapFacts built without TheaterDatabase; theater tile references and water/shore/cliff hints are limited."
        );
    }

    facts.cells.reserve(isoTiles_.size() + overlayCells_.size());
    facts.cellIndexLookup.reserve(isoTiles_.size() + overlayCells_.size());

    std::unordered_map<int, MapTheaterTileFact> theaterTileFactLookup;
    if (theaterDatabase != nullptr) {
        if (!tileRootPath.empty()) {
            const MapTileUsageReport tileUsageReport =
                theaterDatabase->buildMapTileUsageReport(*this, tileRootPath);
            theaterTileFactLookup.reserve(tileUsageReport.usedTiles.size());
            for (const TheaterTileReference& reference : tileUsageReport.usedTiles) {
                MapIsoTile tile;
                tile.tileIndex = reference.tileIndex;
                tile.subTileIndex = reference.tileIndexInSet;
                theaterTileFactLookup.emplace(
                    reference.tileIndex,
                    makeTheaterTileFact(tile, reference)
                );
            }
        } else {
            theaterTileFactLookup.reserve(isoTiles_.size());
        }
    }

    for (const MapIsoTile& tile : isoTiles_) {
        if (tile.x < 0
            || tile.y < 0
            || tile.x >= MapCoordinateLimit
            || tile.y >= MapCoordinateLimit) {
            continue;
        }

        const int cellIndex = mapCellIndex(tile.x, tile.y);
        MapCellFacts cell;
        cell.x = tile.x;
        cell.y = tile.y;
        cell.cellIndex = cellIndex;
        cell.hasTerrain = true;
        cell.terrain = tile;
        cell.heightLevel = tile.level;
        const auto cachedTileIt = theaterTileFactLookup.find(tile.tileIndex);
        if (cachedTileIt != theaterTileFactLookup.end()) {
            cell.theaterTile = cachedTileIt->second;
            cell.theaterTile.subTileIndex = tile.subTileIndex;
        } else {
            cell.theaterTile = makeTheaterTileFact(tile, theaterDatabase, "");
            if (theaterDatabase != nullptr && tile.tileIndex >= 0 && tile.tileIndex != 65535) {
                theaterTileFactLookup.emplace(tile.tileIndex, cell.theaterTile);
            }
        }
        cell.theaterTileReference = theaterReferenceText(cell.theaterTile);
        cell.isHighBridge = isBridgeTileFact(cell.theaterTile);
        cell.isWater = isWaterTileFact(cell.theaterTile);
        cell.isShore = isShoreTileFact(cell.theaterTile);
        cell.isCliff = isCliffTileFact(cell.theaterTile);
        cell.isRamp = isRampTileFact(cell.theaterTile);
        cell.isBridge = cell.isHighBridge;
        cell.terrainClass = terrainClassForFact(cell);
        cell.passabilityHint = passabilityHintForFact(cell);

        if (!cell.theaterTile.validTileSet && tile.tileIndex != 65535) {
            ++facts.missingTheaterTileCount;
        } else if (!tileRootPath.empty()
            && cell.theaterTile.validTileSet
            && !cell.theaterTile.fileExists) {
            ++facts.missingTheaterTileCount;
        }

        facts.cellIndexLookup[cellIndex] = facts.cells.size();
        facts.cells.push_back(cell);
    }

    for (const MapOverlayCell& overlay : overlayCells_) {
        int cellIndex = overlay.cellIndex;
        if (cellIndex < 0 && overlay.x >= 0 && overlay.y >= 0) {
            cellIndex = mapCellIndex(overlay.x, overlay.y);
        }
        if (cellIndex < 0) {
            continue;
        }

        const int x = overlay.x >= 0 ? overlay.x : cellIndex % MapCoordinateLimit;
        const int y = overlay.y >= 0 ? overlay.y : cellIndex / MapCoordinateLimit;

        MapCellFacts* cell = nullptr;
        const auto existingIt = facts.cellIndexLookup.find(cellIndex);
        if (existingIt != facts.cellIndexLookup.end()) {
            cell = &facts.cells[existingIt->second];
        } else {
            MapCellFacts overlayOnly;
            overlayOnly.x = x;
            overlayOnly.y = y;
            overlayOnly.cellIndex = cellIndex;
            facts.cellIndexLookup[cellIndex] = facts.cells.size();
            facts.cells.push_back(overlayOnly);
            cell = &facts.cells.back();
        }

        cell->hasOverlay = true;
        cell->overlay = overlay;
        cell->isOre = overlay.isOre;
        cell->isGem = overlay.isGem;
        cell->isResourceCell = overlay.isOre || overlay.isGem;
        cell->overlayResource =
            makeOverlayResourceFact(overlay, theaterDatabase, summary_.theater);
        // Visible low-bridge overlays are authoritative. Original maps place
        // them over several terrain classes, including clear and road cells.
        cell->isLowBridge = isLowBridgeOverlayFact(overlay);
        [[maybe_unused]] const bool overlayIsBridge =
            cell->overlayResource.overlayKind == "bridge";
        const bool overlayIsHighBridge =
            isHighBridgeOverlayFact(overlay);
        cell->isHighBridge =
            cell->isHighBridge
            || overlayIsHighBridge;
        cell->isBridge = cell->isBridge
            || cell->isLowBridge
            || overlayIsHighBridge;
        cell->isTunnel = isTunnelOverlayFact(overlay);
        cell->terrainClass = terrainClassForFact(*cell);
        cell->passabilityHint = passabilityHintForFact(*cell);
    }

    for (const auto& [cellIndex, occupancy] : structureOccupancyLookup) {
        if (cellIndex < 0) {
            continue;
        }

        MapCellFacts* cell = nullptr;
        const auto existingIt = facts.cellIndexLookup.find(cellIndex);
        if (existingIt != facts.cellIndexLookup.end()) {
            cell = &facts.cells[existingIt->second];
        } else {
            MapCellFacts occupancyOnly;
            occupancyOnly.cellIndex = cellIndex;
            occupancyOnly.x = cellIndex % MapCoordinateLimit;
            occupancyOnly.y = cellIndex / MapCoordinateLimit;
            facts.cellIndexLookup[cellIndex] = facts.cells.size();
            facts.cells.push_back(occupancyOnly);
            cell = &facts.cells.back();
        }

        cell->occupiedByStructure = !occupancy.ids.empty();
        cell->occupyingStructureCount = static_cast<int>(occupancy.ids.size());
        cell->occupyingStructureIds = joinStrings(occupancy.ids);
        cell->occupyingStructureTypes = joinStrings(occupancy.types);
        cell->occupyingStructureOwners = joinStrings(occupancy.owners);
        cell->passabilityHint = passabilityHintForFact(*cell);
    }

    for (std::size_t cellOffset = 0; cellOffset < facts.cells.size(); ++cellOffset) {
        const MapCellFacts& cell = facts.cells[cellOffset];
        if (cell.hasTerrain) {
            ++facts.terrainCellCount;
        }
        if (cell.hasOverlay) {
            ++facts.overlayCellCount;
        }
        if (cell.isResourceCell) {
            ++facts.resourceCellCount;
            facts.resourceCellOffsets.push_back(cellOffset);
        }
        if (cell.isOre) {
            ++facts.oreCellCount;
            facts.oreCellOffsets.push_back(cellOffset);
        }
        if (cell.isGem) {
            ++facts.gemCellCount;
            facts.gemCellOffsets.push_back(cellOffset);
        }
        if (cell.isBridge) {
            ++facts.bridgeCellCount;
            facts.bridgeCellOffsets.push_back(cellOffset);
        }
        if (cell.isHighBridge) {
            ++facts.highBridgeCellCount;
            facts.highBridgeCellOffsets.push_back(cellOffset);
        }
        if (cell.isLowBridge) {
            ++facts.lowBridgeCellCount;
            facts.lowBridgeCellOffsets.push_back(cellOffset);
        }
        if (cell.isWater) {
            ++facts.waterCellCount;
            facts.waterCellOffsets.push_back(cellOffset);
        }
        if (cell.isShore) {
            ++facts.shoreCellCount;
            facts.shoreCellOffsets.push_back(cellOffset);
        }
        if (cell.isCliff) {
            ++facts.cliffCellCount;
            facts.cliffCellOffsets.push_back(cellOffset);
        }
        if (cell.isRamp) {
            ++facts.rampCellCount;
            facts.rampCellOffsets.push_back(cellOffset);
        }
        if (cell.isTunnel) {
            ++facts.tunnelCellCount;
            facts.tunnelCellOffsets.push_back(cellOffset);
        }
        if (cell.occupiedByStructure) {
            ++facts.structureOccupiedMapCellCount;
        }
    }
    facts.lookupIndexSize = facts.cellIndexLookup.size();

    facts.structureAnchors.reserve(structures_.size());
    for (const MapStructureObject& structure : structures_) {
        MapStructureAnchorFact anchor;
        anchor.id = structure.id;
        anchor.owner = structure.owner;
        anchor.type = structure.type;
        anchor.x = structure.x;
        anchor.y = structure.y;
        const auto footprintIt = footprintLookup.find(toLowerAscii(structure.type));
        if (footprintIt != footprintLookup.end()) {
            anchor.footprintWidth = std::max(1, footprintIt->second.width);
            anchor.footprintHeight = std::max(1, footprintIt->second.height);
            anchor.footprintSource = footprintIt->second.source.empty()
                ? "caller footprint override"
                : footprintIt->second.source;
        } else {
            anchor.footprintWidth = 1;
            anchor.footprintHeight = 1;
            anchor.footprintSource =
                "default 1x1 until Rules/Art foundation is joined by the caller";
        }
        anchor.rearCellX = structure.x + std::max(0, anchor.footprintWidth - 1);
        anchor.rearCellY = structure.y + std::max(0, anchor.footprintHeight - 1);
        anchor.anchorFormula =
            "rearCell=(x+footprintWidth-1,y+footprintHeight-1); spriteTopLeft=project(rearCell)+frame/global/layer offsets";
        anchor.footprintCells = structureFootprintCellsForAnchor(facts, anchor);
        anchor.footprintIsoDiamondBounds =
            structureFootprintIsoDiamondBounds(anchor.footprintCells);
        anchor.placementPreviewIsoDiamondBounds = anchor.footprintIsoDiamondBounds;
        anchor.hitTestIsoDiamondBounds = anchor.footprintIsoDiamondBounds;
        anchor.selectionIsoDiamondBounds = anchor.footprintIsoDiamondBounds;
        anchor.placementPreviewFormula =
            "draw one 60x29/60x30 iso diamond per cached footprint cell; red if any cell buildable=false";
        anchor.hitTestFormula =
            "point-in-diamond over cached footprintCells; do not use sprite local/global rect";
        anchor.selectionFormula =
            "selection and target acquisition use the same cached footprint iso diamonds";
        anchor.finalStructureRectFormula =
            "project(rearCell)+(-globalWidth/2+frameOffsetX,tileH/2-globalHeight+frameOffsetY)+layerOffset";
        finalizeStructureFootprintLegality(anchor);
        anchor.footprintEdgeCells = structureEdgeCellsFromFootprint(anchor.footprintCells);
        anchor.approachCells =
            structureApproachCellsForAnchor(facts, anchor, "attack/engineer-enter/repair/occupy");
        anchor.attackApproachCells = structureCellsWithPurpose(anchor.approachCells, "attack");
        anchor.engineerApproachCells = structureCellsWithPurpose(anchor.approachCells, "engineer-enter");
        anchor.repairApproachCells = structureCellsWithPurpose(anchor.approachCells, "repair");
        anchor.occupyApproachCells = structureCellsWithPurpose(anchor.approachCells, "occupy");
        anchor.blockedTargetFallbackHint =
            "if target cell is blocked by footprint, choose purpose-specific approach ring candidates then let Path validate movement zone";
        facts.structureFootprintCellCount += anchor.footprintCells.size();
        facts.structureEdgeCellCount += anchor.footprintEdgeCells.size();
        facts.structureApproachCellCount += anchor.approachCells.size();
        facts.structureAttackApproachCellCount += anchor.attackApproachCells.size();
        facts.structureEngineerApproachCellCount += anchor.engineerApproachCells.size();
        facts.structureRepairApproachCellCount += anchor.repairApproachCells.size();
        facts.structureOccupyApproachCellCount += anchor.occupyApproachCells.size();
        facts.structureBuildableFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintBuildableCellCount));
        facts.structureIllegalFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintIllegalCellCount));
        facts.structureWaterFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintWaterCellCount));
        facts.structureShoreFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintShoreCellCount));
        facts.structureBridgeFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintBridgeCellCount));
        facts.structureResourceFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintResourceCellCount));
        facts.structureOverlayBlockerFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintOverlayBlockerCellCount));
        facts.structureOccupiedFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintOccupiedCellCount));
        facts.structureOtherOccupiedFootprintCellCount +=
            static_cast<std::size_t>(std::max(0, anchor.footprintOtherStructureCellCount));
        if (!anchor.id.empty()) {
            facts.structureAnchorIdLookup[toLowerAscii(anchor.id)] = facts.structureAnchors.size();
        }
        facts.structureAnchors.push_back(anchor);
    }

    facts.contentHash = computeMapFactsHash(facts);

    return facts;
}

MapQuery::MapQuery(const MapFacts& facts)
    : facts_(&facts) {
}

bool MapQuery::isValid() const {
    return facts_ != nullptr;
}

const MapFacts* MapQuery::getFacts() const {
    return facts_;
}

std::uint64_t MapQuery::getFactsContentHash() const {
    return facts_ == nullptr ? 0 : facts_->contentHash;
}

const MapCellFacts* MapQuery::getCell(int x, int y) const {
    if (facts_ == nullptr) {
        return nullptr;
    }

    if (x < 0 || y < 0 || x >= MapCoordinateLimit || y >= MapCoordinateLimit) {
        return nullptr;
    }
    return getCellByCellIndex(mapCellIndex(x, y));
}

const MapCellFacts* MapQuery::getCellByIndex(int cellIndex) const {
    return getCellByCellIndex(cellIndex);
}

const MapCellFacts* MapQuery::getCellByCellIndex(int cellIndex) const {
    if (facts_ == nullptr) {
        return nullptr;
    }

    const auto iterator = facts_->cellIndexLookup.find(cellIndex);
    if (iterator == facts_->cellIndexLookup.end()
        || iterator->second >= facts_->cells.size()) {
        return nullptr;
    }
    return &facts_->cells[iterator->second];
}

std::vector<const MapCellFacts*> MapQuery::getResourceCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->resourceCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getOreCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->oreCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getGemCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->gemCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getBridgeCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->bridgeCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getHighBridgeCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->highBridgeCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getLowBridgeCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->lowBridgeCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getWaterCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->waterCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getShoreCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->shoreCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getCliffCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->cliffCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getRampCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->rampCellOffsets);
}

std::vector<const MapCellFacts*> MapQuery::getTunnelCells() const {
    return facts_ == nullptr
        ? std::vector<const MapCellFacts*>{}
        : cellsFromOffsets(facts_, facts_->tunnelCellOffsets);
}

const MapStructureAnchorFact* MapQuery::getStructureAnchorById(
    const std::string& id
) const {
    if (facts_ == nullptr) {
        return nullptr;
    }

    const auto iterator = facts_->structureAnchorIdLookup.find(toLowerAscii(id));
    if (iterator == facts_->structureAnchorIdLookup.end()
        || iterator->second >= facts_->structureAnchors.size()) {
        return nullptr;
    }
    return &facts_->structureAnchors[iterator->second];
}

std::vector<MapFootprintCellFact> MapQuery::getStructureFootprintCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.footprintCells.empty()) {
        return anchor.footprintCells;
    }
    if (facts_ == nullptr) {
        return {};
    }
    return structureFootprintCellsForAnchor(*facts_, anchor);
}

std::vector<MapFootprintCellFact> MapQuery::getStructureFootprintCellsById(
    const std::string& id
) const {
    const MapStructureAnchorFact* anchor = getStructureAnchorById(id);
    return anchor == nullptr ? std::vector<MapFootprintCellFact>{} : anchor->footprintCells;
}

const MapIsoDiamondBounds* MapQuery::getStructureFootprintIsoDiamondBoundsById(
    const std::string& id
) const {
    const MapStructureAnchorFact* anchor = getStructureAnchorById(id);
    return anchor == nullptr ? nullptr : &anchor->footprintIsoDiamondBounds;
}

bool MapQuery::isStructureFootprintBuildableById(const std::string& id) const {
    const MapStructureAnchorFact* anchor = getStructureAnchorById(id);
    return anchor != nullptr && anchor->footprintBuildable;
}

std::vector<MapFootprintCellFact> MapQuery::getStructureEdgeCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.footprintEdgeCells.empty()) {
        return anchor.footprintEdgeCells;
    }
    if (facts_ == nullptr) {
        return {};
    }
    return structureEdgeCellsForAnchor(*facts_, anchor);
}

std::vector<MapFootprintCellFact> MapQuery::getStructureEdgeCellsById(
    const std::string& id
) const {
    const MapStructureAnchorFact* anchor = getStructureAnchorById(id);
    return anchor == nullptr ? std::vector<MapFootprintCellFact>{} : anchor->footprintEdgeCells;
}

std::vector<MapFootprintCellFact> MapQuery::getStructureApproachCells(
    const MapStructureAnchorFact& anchor,
    const std::string& purpose
) const {
    const std::string normalizedPurpose = normalizeApproachPurpose(purpose);
    if (normalizedPurpose == "attack" && !anchor.attackApproachCells.empty()) {
        return anchor.attackApproachCells;
    }
    if (normalizedPurpose == "engineer-enter" && !anchor.engineerApproachCells.empty()) {
        return anchor.engineerApproachCells;
    }
    if (normalizedPurpose == "repair" && !anchor.repairApproachCells.empty()) {
        return anchor.repairApproachCells;
    }
    if (normalizedPurpose == "occupy" && !anchor.occupyApproachCells.empty()) {
        return anchor.occupyApproachCells;
    }
    if (!anchor.approachCells.empty()) {
        return structureCellsWithPurpose(anchor.approachCells, normalizedPurpose);
    }
    if (facts_ == nullptr) {
        return {};
    }
    return structureApproachCellsForAnchor(*facts_, anchor, normalizedPurpose);
}

std::vector<MapFootprintCellFact> MapQuery::getStructureApproachCellsById(
    const std::string& id,
    const std::string& purpose
) const {
    const MapStructureAnchorFact* anchor = getStructureAnchorById(id);
    if (anchor == nullptr) {
        return {};
    }

    const std::string normalizedPurpose = normalizeApproachPurpose(purpose);
    if (normalizedPurpose == "attack") {
        return anchor->attackApproachCells;
    }
    if (normalizedPurpose == "engineer-enter") {
        return anchor->engineerApproachCells;
    }
    if (normalizedPurpose == "repair") {
        return anchor->repairApproachCells;
    }
    if (normalizedPurpose == "occupy") {
        return anchor->occupyApproachCells;
    }
    if (normalizedPurpose == "attack/engineer-enter/repair/occupy") {
        return anchor->approachCells;
    }
    return {};
}

std::vector<MapFootprintCellFact> MapQuery::getStructureAttackApproachCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.attackApproachCells.empty()) {
        return anchor.attackApproachCells;
    }
    return getStructureApproachCells(anchor, "attack");
}

std::vector<MapFootprintCellFact> MapQuery::getStructureEngineerApproachCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.engineerApproachCells.empty()) {
        return anchor.engineerApproachCells;
    }
    return getStructureApproachCells(anchor, "engineer-enter");
}

std::vector<MapFootprintCellFact> MapQuery::getStructureRepairApproachCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.repairApproachCells.empty()) {
        return anchor.repairApproachCells;
    }
    return getStructureApproachCells(anchor, "repair");
}

std::vector<MapFootprintCellFact> MapQuery::getStructureOccupyApproachCells(
    const MapStructureAnchorFact& anchor
) const {
    if (!anchor.occupyApproachCells.empty()) {
        return anchor.occupyApproachCells;
    }
    return getStructureApproachCells(anchor, "occupy");
}
