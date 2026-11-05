#include "runtime/persistence/RuntimeSaveDocument.h"
#include "core/io/IniFile.h"
#include <array>
#include <string_view>

namespace {
constexpr std::array<std::string_view, 1> literalNumericSections{"TeamMissions"};
}
bool loadRuntimeSaveDocument(const std::string& path, IniFile& document) {
    return document.loadFromFile(path, literalNumericSections);
}
bool parseRuntimeSaveDocument(const std::string& text, IniFile& document) {
    return document.loadFromString(text, literalNumericSections);
}
