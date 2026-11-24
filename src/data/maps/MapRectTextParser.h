#pragma once
#include "data/maps/MapRectFields.h"
#include <string>
#include <vector>
namespace concord::map_data {
struct RuntimeMapRectTextReaders {
    std::vector<std::string> (*splitCommaList)(const std::string& value);
    int (*parseIntValue)(const std::string& value, const std::string& context,
        std::vector<std::string>& warnings);
};
inline MapRectFields parseRuntimeMapRectFields(
    const std::string& value,
    const std::string& context,
    std::vector<std::string>& warnings
,
    const RuntimeMapRectTextReaders& readers
) {
    MapRectFields rect;
    const std::vector<std::string> fields = readers.splitCommaList(value);
    if (fields.size() < 4) {
        if (!value.empty()) {
            warnings.push_back(context + " expected four comma-separated integers.");
        }
        return rect;
    }

    rect.x = readers.parseIntValue(fields[0], context + " x", warnings);
    rect.y = readers.parseIntValue(fields[1], context + " y", warnings);
    rect.width = readers.parseIntValue(fields[2], context + " width", warnings);
    rect.height = readers.parseIntValue(fields[3], context + " height", warnings);
    rect.valid = rect.x >= 0
        && rect.y >= 0
        && rect.width > 0
        && rect.height > 0;
    return rect;
}
}
