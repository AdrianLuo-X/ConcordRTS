#pragma once
#include "core/security/RuntimeSecureBytes.h"
#include <string>
#include <string_view>

namespace runtime_lan_menu_secure_detail {
// Synchronous borrowed source for the original owner's two distinct policies:
// one16byte hexID draw accepts zero;8byteU64 retries zero at most4times.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept = 0;
};
bool fill(std::span<std::uint8_t> bytes, std::string& error, Source& source);
bool generateU64(std::uint64_t& value, std::string& error, Source& source);
bool generateId(std::string_view prefix, std::string& identifier,
    std::string& error, Source& source);
} // namespace runtime_lan_menu_secure_detail
