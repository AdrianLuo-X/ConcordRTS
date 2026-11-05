#pragma once
#include "core/security/RuntimeSecureBytes.h"
namespace runtime_lan_nonce_noise_detail {
// Only synchronous OS-noise acquisition. The original S26 clock/counter,
// finalizer and uniqueness policy remains with RuntimeMultiplayerLanSession.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept = 0;
};
// Full two-native-uint32-word input, firstword high, secondword low.
// Failure yields zero noise and false; this is not a secure-fallback claim.
bool acquire(std::uint64_t& noise, Source& source) noexcept;
bool acquire(std::uint64_t& noise) noexcept;
} // namespace runtime_lan_nonce_noise_detail
