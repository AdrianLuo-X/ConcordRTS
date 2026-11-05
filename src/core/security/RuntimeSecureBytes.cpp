#include "core/security/RuntimeSecureBytes.h"
#include "core/security/private/RuntimeSecureBytesReader.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#endif

namespace {
using runtime_secure_bytes_detail::ReadResult;
using runtime_secure_bytes_detail::ReadStatus;

void wipe(std::span<std::uint8_t> output) noexcept {
    volatile std::uint8_t* bytes = output.data();
    for (std::size_t i = 0; i != output.size(); ++i) bytes[i] = 0;
}

class SystemReader final : public runtime_secure_bytes_detail::Reader {
public:
    ReadResult read(std::span<std::uint8_t> output) noexcept override {
#ifdef _WIN32
        const NTSTATUS status = BCryptGenRandom(nullptr, output.data(),
            static_cast<ULONG>(output.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return status == 0
            ? ReadResult{ReadStatus::Success, output.size(), 0}
            : ReadResult{ReadStatus::Failure, 0, static_cast<std::int32_t>(status)};
#elif defined(__linux__)
        const auto count = ::getrandom(output.data(), output.size(), 0U);
        if (count >= 0) return {ReadStatus::Success, static_cast<std::size_t>(count), 0};
        const int code = errno;
        if (code == EINTR) return {ReadStatus::Interrupted, 0, code};
        return {code == ENOSYS ? ReadStatus::Unavailable : ReadStatus::Failure, 0, code};
#else
        (void)output;
        return {ReadStatus::Unavailable, 0, 0};
#endif
    }
};
} // namespace

RuntimeSecureBytesResult runtime_secure_bytes_detail::fillWithReader(
    std::span<std::uint8_t> output,
    Reader& reader
) noexcept {
    if (output.empty() || output.size() > runtimeSecureBytesMaximumRequest)
        return {RuntimeSecureBytesStatus::InvalidRequest, 0, 0};
    std::size_t generated = 0;
    for (std::size_t attempt = 0; attempt != runtimeSecureBytesMaximumReadAttempts; ++attempt) {
        const auto remaining = output.subspan(generated);
        const ReadResult result = reader.read(remaining);
        if (result.status == ReadStatus::Interrupted) continue;
        if (result.status != ReadStatus::Success) {
            wipe(output);
            return {result.status == ReadStatus::Unavailable
                ? RuntimeSecureBytesStatus::Unavailable : RuntimeSecureBytesStatus::SystemFailure,
                result.errorCode, 0};
        }
        if (result.generated == 0 || result.generated > remaining.size()) {
            wipe(output);
            return {RuntimeSecureBytesStatus::Incomplete, result.errorCode, 0};
        }
        generated += result.generated;
        if (generated == output.size())
            return {RuntimeSecureBytesStatus::Success, 0, generated};
    }
    wipe(output);
    return {RuntimeSecureBytesStatus::RetryLimit, 0, 0};
}

RuntimeSecureBytesBackend runtimeSecureBytesBackend() noexcept {
#ifdef _WIN32
    return RuntimeSecureBytesBackend::WindowsCng;
#elif defined(__linux__)
    return RuntimeSecureBytesBackend::LinuxGetrandom;
#else
    return RuntimeSecureBytesBackend::Unsupported;
#endif
}

RuntimeSecureBytesResult fillRuntimeSecureBytes(std::span<std::uint8_t> output) noexcept {
    SystemReader reader;
    return runtime_secure_bytes_detail::fillWithReader(output, reader);
}
