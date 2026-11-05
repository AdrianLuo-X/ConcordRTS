#pragma once
#include "core/security/RuntimeSecureBytes.h"
#include "network/transport/RuntimeInternetServiceTransport.h"

namespace runtime_service_nonce_detail {
// Original nonce alias/width stays with the original wire contract.
// Borrowed synchronously; production always chooses the actual system source.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept = 0;
};
bool fill(RuntimeInternetServiceNonce& nonce, Source& source) noexcept;
} // namespace runtime_service_nonce_detail
