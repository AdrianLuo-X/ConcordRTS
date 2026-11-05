#include "network/internet/private/RuntimeInternetServiceRandomBytes.h"
#include <algorithm>

#ifdef _WIN32
namespace {
class RuntimeServiceSystemRandomSource final : public runtime_service_random_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        return fillRuntimeSecureBytes(output);
    }
};
} // namespace
#endif

bool runtime_service_random_detail::fill(
    std::uint8_t* destination, std::size_t size, Source& source
) noexcept {
    // Validate the raw tuple before forming a span. Preserve both measured
    // Windows zero-size outcomes; neither case consumes a system read.
    if (destination == nullptr || size > runtimeSecureBytesMaximumRequest) return false;
    if (size == 0) return true;
    const std::span<std::uint8_t> output(destination, size);
    const RuntimeSecureBytesResult result = source.fill(output);
    if (!result.succeeded() || result.generated != size) {
        std::fill(output.begin(), output.end(), static_cast<std::uint8_t>(0U));
        return false;
    }
    return true;
}

bool runtime_service_random_detail::fillDefault(std::uint8_t* destination, std::size_t size) noexcept {
#ifdef _WIN32
    RuntimeServiceSystemRandomSource source;
    return fill(destination, size, source);
#else
    // The original service callback is unsupported outside Windows Schannel.
    // A Linux byte provider does not imply a supported Internet service.
    (void)destination;
    (void)size;
    return false;
#endif
}
