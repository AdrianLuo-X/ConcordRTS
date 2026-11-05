#pragma once
#include "core/security/RuntimeSecureBytes.h"
namespace runtime_tunnel_prelude_detail {
// Synchronous borrowed raw bytes; zero output retains the original policy.
// Actual prelude width remains in the original public tunnel contract.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept = 0;
};
bool fill(std::span<std::uint8_t> bytes, Source& source) noexcept;
} // namespace runtime_tunnel_prelude_detail
