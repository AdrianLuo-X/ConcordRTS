#pragma once
#include "core/security/RuntimeSecureBytes.h"
namespace runtime_service_random_detail {
// Borrowed synchronously by the original raw callback. No size-zero OS read:
// original Windows qualification is null+0=false, validpointer+0=true.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept = 0;
};
bool fill(std::uint8_t* destination, std::size_t size, Source& source) noexcept;
bool fillDefault(std::uint8_t* destination, std::size_t size) noexcept;
} // namespace runtime_service_random_detail
