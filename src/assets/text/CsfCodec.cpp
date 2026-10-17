#include "assets/text/CsfCodec.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace concord::csf {
namespace {

bool hasBytes(std::span<const unsigned char> bytes, std::size_t at, std::size_t count) {
    return at <= bytes.size() && count <= bytes.size() - at;
}

std::uint32_t readLe32(std::span<const unsigned char> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at])
        | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

bool fourCc(std::span<const unsigned char> bytes, std::size_t at, const char (&tag)[5]) {
    return hasBytes(bytes, at, 4)
        && bytes[at] == static_cast<unsigned char>(tag[0])
        && bytes[at + 1] == static_cast<unsigned char>(tag[1])
        && bytes[at + 2] == static_cast<unsigned char>(tag[2])
        && bytes[at + 3] == static_cast<unsigned char>(tag[3]);
}

} // namespace

std::vector<Entry> decodeEntries(std::span<const unsigned char> bytes) {
    std::vector<Entry> entries;
    if (!hasBytes(bytes, 0, 24) || !fourCc(bytes, 0, " FSC")) return entries;
    const std::uint32_t labelCount = readLe32(bytes, 8);
    std::size_t cursor = 24;
    for (std::uint32_t labelIndex = 0;
        labelIndex < labelCount && hasBytes(bytes, cursor, 12);
        ++labelIndex) {
        if (!fourCc(bytes, cursor, " LBL")) break;
        cursor += 4;
        const std::uint32_t stringCount = readLe32(bytes, cursor);
        cursor += 4;
        const std::uint32_t labelLength = readLe32(bytes, cursor);
        cursor += 4;
        if (!hasBytes(bytes, cursor, labelLength)) break;
        std::string label(
            reinterpret_cast<const char*>(bytes.data() + cursor), labelLength);
        cursor += labelLength;
        std::u16string firstValue;
        for (std::uint32_t stringIndex = 0;
            stringIndex < stringCount && hasBytes(bytes, cursor, 8);
            ++stringIndex) {
            const bool plain = fourCc(bytes, cursor, " RTS");
            const bool extra = fourCc(bytes, cursor, "WRTS");
            if (!plain && !extra) {
                cursor = bytes.size();
                break;
            }
            cursor += 4;
            const std::uint32_t characterCount = readLe32(bytes, cursor);
            cursor += 4;
            const std::uint64_t encodedBytes =
                static_cast<std::uint64_t>(characterCount) * 2ULL;
            if (encodedBytes > bytes.size() - cursor) {
                cursor = bytes.size();
                break;
            }
            std::u16string decoded;
            decoded.reserve(characterCount);
            for (std::uint32_t index = 0; index < characterCount; ++index) {
                const std::size_t at = cursor + static_cast<std::size_t>(index) * 2U;
                const std::uint16_t encoded = static_cast<std::uint16_t>(bytes[at])
                    | (static_cast<std::uint16_t>(bytes[at + 1]) << 8);
                decoded.push_back(static_cast<char16_t>(encoded ^ 0xffffU));
            }
            cursor += static_cast<std::size_t>(encodedBytes);
            if (extra) {
                if (!hasBytes(bytes, cursor, 4)) {
                    cursor = bytes.size();
                    break;
                }
                const std::uint32_t extraLength = readLe32(bytes, cursor);
                cursor += 4;
                if (!hasBytes(bytes, cursor, extraLength)) {
                    cursor = bytes.size();
                    break;
                }
                cursor += extraLength;
            }
            // A WRTS value is complete only after its extra bytes were consumed.
            if (stringIndex == 0) firstValue = std::move(decoded);
        }
        if (!label.empty() && !firstValue.empty()) {
            entries.push_back({std::move(label), std::move(firstValue)});
        }
    }
    return entries;
}

std::u16string projectCarriageReturns(std::u16string value, CarriageReturnPolicy policy) {
    const char16_t replacement = policy == CarriageReturnPolicy::Space ? u' ' : u'\n';
    for (char16_t& character : value) {
        if (character == u'\r') character = replacement;
    }
    return value;
}

} // namespace concord::csf
