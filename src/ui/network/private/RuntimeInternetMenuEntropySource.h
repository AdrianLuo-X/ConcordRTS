#pragma once
#include "core/security/RuntimeSecureBytes.h"
#include "runtime/contracts/RuntimeInternetMenuData.h"
#include <string>

namespace runtime_internet_menu_entropy_detail {
// Borrowed for one synchronous transaction. The production wrapper always
// chooses the real system provider; this seam lets its original owner test
// failures and retry/commit policy without a second assembly algorithm.
class Source {
public:
    virtual ~Source() = default;
    virtual RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept = 0;
};
bool generate(RuntimeInternetMultiplayerMenuEntropy& entropy,
    std::string& errorMessage, Source& source);
} // namespace runtime_internet_menu_entropy_detail
