#pragma once

#include <span>
#include <string>
#include <vector>

namespace concord::csf {

// File order preserves the adapters' original last-valid-duplicate behavior.
// Text contains UTF-16 code units, not Unicode scalar values.
struct Entry {
    std::string label;
    std::u16string firstValue;
    bool operator==(const Entry&) const = default;
};

enum class CarriageReturnPolicy { Space, LineFeed };

// A malformed tail retains complete earlier entries and a complete first value
// from its current label. Empty labels/first values are not emitted.
std::vector<Entry> decodeEntries(std::span<const unsigned char> bytes);

// Display policy stays separate from parsing. LF, NUL and surrogates are kept.
std::u16string projectCarriageReturns(std::u16string value, CarriageReturnPolicy policy);

} // namespace concord::csf
