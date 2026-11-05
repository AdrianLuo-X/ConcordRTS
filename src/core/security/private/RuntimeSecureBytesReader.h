#pragma once

#include "core/security/RuntimeSecureBytes.h"

namespace runtime_secure_bytes_detail {

enum class ReadStatus : std::uint8_t { Success, Interrupted, Unavailable, Failure };

struct ReadResult {
    ReadStatus status = ReadStatus::Unavailable;
    std::size_t generated = 0;
    std::int32_t errorCode = 0;
};

// Private policy seam: tests inject read outcomes, while the production
// wrapper always selects its real OS reader. No test reader is a CSPRNG claim.
class Reader {
public:
    virtual ~Reader() = default;
    virtual ReadResult read(std::span<std::uint8_t> output) noexcept = 0;
};

RuntimeSecureBytesResult fillWithReader(
    std::span<std::uint8_t> output,
    Reader& reader
) noexcept;

} // namespace runtime_secure_bytes_detail
