#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

enum class RuntimeSecureBytesStatus : std::uint8_t {
    Success,
    InvalidRequest,
    Unavailable,
    SystemFailure,
    Incomplete,
    RetryLimit
};

enum class RuntimeSecureBytesBackend : std::uint8_t {
    Unsupported,
    WindowsCng,
    LinuxGetrandom
};

inline constexpr std::size_t runtimeSecureBytesMaximumRequest = 0xffffffffULL;
inline constexpr std::size_t runtimeSecureBytesMaximumReadAttempts = 32U;

struct RuntimeSecureBytesResult {
    RuntimeSecureBytesStatus status = RuntimeSecureBytesStatus::Unavailable;
    std::int32_t errorCode = 0;
    std::size_t generated = 0;

    bool succeeded() const noexcept {
        return status == RuntimeSecureBytesStatus::Success;
    }
};

// Identifies the compiled system adapter, not evidence of a successful read.
RuntimeSecureBytesBackend runtimeSecureBytesBackend() noexcept;

// Borrows only this call's output. Success fills it completely using the OS
// cryptographic source. Zero bytes are valid random output; nonzero/retry and
// transaction-commit policies belong to callers. Empty/oversized requests are
// rejected without writing. Other failures wipe the entire requested span,
// report generated=0 and never substitute a clock/counter/random_device.
// Interrupted/partial reads have a fixed attempt bound, including progress.
RuntimeSecureBytesResult fillRuntimeSecureBytes(
    std::span<std::uint8_t> output
) noexcept;
