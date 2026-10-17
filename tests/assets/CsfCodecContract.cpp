#include "assets/text/CsfCodec.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
using Entries = std::vector<concord::csf::Entry>;
std::size_t checks = 0;

bool expect(bool condition, const std::string& description) {
    ++checks;
    if (!condition) std::cerr << "CSF codec contract failed: " << description << '\n';
    return condition;
}

void append32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xffU));
    }
}
void patch32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes[at + shift / 8] = static_cast<unsigned char>((value >> shift) & 0xffU);
    }
}
void appendBytes(Bytes& bytes, std::string_view value) {
    for (unsigned char character : value) bytes.push_back(character);
}
Bytes header(std::uint32_t labels) {
    Bytes bytes;
    appendBytes(bytes, " FSC");
    append32(bytes, 3);
    append32(bytes, labels);
    append32(bytes, 0);
    append32(bytes, 0);
    append32(bytes, 0);
    return bytes;
}
struct Value { std::u16string text; bool extra = false; std::string extraBytes; };
std::size_t appendValue(Bytes& bytes, const Value& value) {
    appendBytes(bytes, value.extra ? "WRTS" : " RTS");
    append32(bytes, static_cast<std::uint32_t>(value.text.size()));
    for (char16_t unit : value.text) {
        const auto encoded = static_cast<std::uint32_t>(unit) ^ 0xffffU;
        bytes.push_back(static_cast<unsigned char>(encoded & 0xffU));
        bytes.push_back(static_cast<unsigned char>((encoded >> 8) & 0xffU));
    }
    if (value.extra) {
        append32(bytes, static_cast<std::uint32_t>(value.extraBytes.size()));
        appendBytes(bytes, value.extraBytes);
    }
    return bytes.size();
}
std::size_t appendLabel(Bytes& bytes, const std::string& label, const std::vector<Value>& values) {
    appendBytes(bytes, " LBL");
    append32(bytes, static_cast<std::uint32_t>(values.size()));
    append32(bytes, static_cast<std::uint32_t>(label.size()));
    appendBytes(bytes, label);
    std::size_t firstCompleted = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t completed = appendValue(bytes, values[index]);
        if (index == 0) firstCompleted = completed;
    }
    return firstCompleted;
}

bool testHeadersAndIgnoredFields() {
    bool ok = true;
    const Bytes empty = header(0);
    for (std::size_t length = 0; length < 24; ++length) {
        ok &= expect(concord::csf::decodeEntries(std::span(empty.data(), length)).empty(),
            "short header " + std::to_string(length));
    }
    ok &= expect(concord::csf::decodeEntries(empty).empty(), "valid empty catalog");
    Bytes bytes = header(1);
    appendLabel(bytes, "Key", {{u"value", false, {}}});
    const Entries expected{{"Key", u"value"}};
    for (const std::size_t field : {std::size_t{4}, std::size_t{12}, std::size_t{16}, std::size_t{20}}) {
        Bytes altered = bytes;
        patch32(altered, field, std::numeric_limits<std::uint32_t>::max());
        ok &= expect(concord::csf::decodeEntries(altered) == expected, "unused header field " + std::to_string(field));
    }
    Bytes altered = bytes;
    altered[0] = 'X';
    ok &= expect(concord::csf::decodeEntries(altered).empty(), "bad header tag");
    altered = bytes;
    appendBytes(altered, "ignored tail");
    ok &= expect(concord::csf::decodeEntries(altered) == expected, "trailing bytes ignored");
    patch32(altered, 8, 0);
    ok &= expect(concord::csf::decodeEntries(altered).empty(), "zero label count ignores body");
    patch32(bytes, 8, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == expected, "huge label count bounded by available body");
    return ok;
}

bool testEveryTruncationAndMultipleValues() {
    bool ok = true;
    Bytes bytes = header(3);
    const std::u16string text{u'A', u'\r', u'\n', u'\0', char16_t{0xd800}, char16_t{0xdc00}, char16_t{0xffff}};
    const std::size_t a = appendLabel(bytes, " First ", {{text, true, std::string("x\0y", 3)}, {u"ignored", false, {}}});
    const std::size_t b = appendLabel(bytes, "Second", {{u"two", false, {}}, {u"also ignored", true, "metadata"}});
    const std::size_t c = appendLabel(bytes, "Third", {{u"\u4e2d\u6587", true, {}}});
    const Entries all{{" First ", text}, {"Second", u"two"}, {"Third", u"\u4e2d\u6587"}};
    const std::vector<std::size_t> commits{a, b, c};
    for (std::size_t length = 0; length <= bytes.size(); ++length) {
        Entries expected;
        for (std::size_t index = 0; index < commits.size(); ++index) {
            if (length >= commits[index]) expected.push_back(all[index]);
        }
        ok &= expect(concord::csf::decodeEntries(std::span(bytes.data(), length)) == expected,
            "every-byte truncation retained wrong completed first values at " + std::to_string(length));
    }
    ok &= expect(concord::csf::decodeEntries(bytes) == all, "raw key bytes, UTF-16 units and first-only multi-value output");
    return ok;
}

bool testMalformedTails() {
    bool ok = true;
    Bytes prefix = header(2);
    appendLabel(prefix, "good", {{u"retained", false, {}}});
    const Entries previous{{"good", u"retained"}};
    const std::size_t labelAt = prefix.size();
    Bytes full = prefix;
    const std::size_t firstAt = labelAt + 12 + 3;
    appendLabel(full, "bad", {{u"first", false, {}}, {u"second", true, "extra"}});
    const std::size_t secondAt = firstAt + 8 + 10;
    for (const auto& alteration : std::vector<std::pair<std::size_t, std::string>>{
            {labelAt, "bad label tag"}, {firstAt, "bad first string tag"}}) {
        Bytes bytes = full;
        bytes[alteration.first] = 'X';
        ok &= expect(concord::csf::decodeEntries(bytes) == previous, alteration.second);
    }
    Bytes bytes = full;
    patch32(bytes, labelAt + 8, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == previous, "huge label byte length");
    bytes = full;
    patch32(bytes, firstAt + 4, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == previous, "huge first text length");
    const Entries firstAlso{{"good", u"retained"}, {"bad", u"first"}};
    bytes = full;
    bytes[secondAt] = 'X';
    ok &= expect(concord::csf::decodeEntries(bytes) == firstAlso, "bad second tag retains completed first value");
    bytes = full;
    patch32(bytes, secondAt + 4, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == firstAlso, "huge second text retains completed first value");
    bytes = full;
    patch32(bytes, secondAt + 8 + 12, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == firstAlso, "bad second extra length retains first value");
    bytes = full;
    patch32(bytes, labelAt + 4, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == firstAlso, "huge string count bounded by available bytes");

    bytes = prefix;
    appendLabel(bytes, "bad", {{u"first", true, "extra"}});
    const std::size_t extraLengthAt = firstAt + 8 + 10;
    patch32(bytes, extraLengthAt, std::numeric_limits<std::uint32_t>::max());
    ok &= expect(concord::csf::decodeEntries(bytes) == previous, "first WRTS extra must finish before first value is retained");
    bytes = prefix;
    appendBytes(bytes, " LBL"); append32(bytes, 1); append32(bytes, 0);
    appendBytes(bytes, " RTS"); append32(bytes, 0xffffffffU);
    ok &= expect(concord::csf::decodeEntries(bytes) == previous, "oversized text in empty-key label is still parsed and rejected");
    return ok;
}

bool testEmptyAndDuplicateEntries() {
    bool ok = true;
    Bytes bytes = header(7);
    appendLabel(bytes, "Same", {{u"first", false, {}}});
    appendLabel(bytes, "same", {{u"later", false, {}}});
    appendLabel(bytes, "SAME", {{u"", false, {}}, {u"not promoted", false, {}}});
    appendLabel(bytes, "", {{u"discarded", false, {}}});
    appendLabel(bytes, "zero", {});
    appendLabel(bytes, std::string("N\0\xff", 3), {{u"nul-key", false, {}}});
    appendLabel(bytes, " spaced ", {{u"last", false, {}}});
    ok &= expect(concord::csf::decodeEntries(bytes) == Entries{
            {"Same", u"first"}, {"same", u"later"},
            {std::string("N\0\xff", 3), u"nul-key"}, {" spaced ", u"last"}},
        "raw duplicate order/case and key bytes retained; empty first value not replaced by later value");
    return ok;
}

bool testNamedCarriageReturnPolicies() {
    bool ok = true;
    const std::u16string raw{u'A', u'\r', u'\n', u'\r', u'B', u'\0', char16_t{0xd800}, char16_t{0xdc00}};
    const std::u16string spaces{u'A', u' ', u'\n', u' ', u'B', u'\0', char16_t{0xd800}, char16_t{0xdc00}};
    const std::u16string lines{u'A', u'\n', u'\n', u'\n', u'B', u'\0', char16_t{0xd800}, char16_t{0xdc00}};
    ok &= expect(concord::csf::projectCarriageReturns(raw, concord::csf::CarriageReturnPolicy::Space) == spaces,
        "main/skirmish policy changes only CR to space");
    ok &= expect(concord::csf::projectCarriageReturns(raw, concord::csf::CarriageReturnPolicy::LineFeed) == lines,
        "campaign policy changes only CR to LF");
    ok &= expect(concord::csf::projectCarriageReturns({}, concord::csf::CarriageReturnPolicy::Space).empty(), "empty projection");
    return ok;
}
} // namespace

int main() {
    bool ok = true;
    ok &= testHeadersAndIgnoredFields();
    ok &= testEveryTruncationAndMultipleValues();
    ok &= testMalformedTails();
    ok &= testEmptyAndDuplicateEntries();
    ok &= testNamedCarriageReturnPolicies();
    if (!ok) return 1;
    std::cout << "CSF codec contract passed: " << checks
        << " checks; raw entries, every-byte truncation, multi-value/extra tails, "
        "duplicate order, byte/code-unit preservation and named CR policies.\n";
    return 0;
}
