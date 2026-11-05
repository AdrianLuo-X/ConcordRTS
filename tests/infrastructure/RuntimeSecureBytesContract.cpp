#include "core/security/RuntimeSecureBytes.h"
#include "core/security/private/RuntimeSecureBytesReader.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <type_traits>

namespace {
using runtime_secure_bytes_detail::ReadResult;
using runtime_secure_bytes_detail::ReadStatus;
enum class Mode { Complete, Zero, Failure, Unavailable, InterruptedThenComplete,
    InterruptedForever, Partial, PartialThenFailure, NoProgress, Overreported, OneAtATime };

class ScriptedReader final : public runtime_secure_bytes_detail::Reader {
public:
    explicit ScriptedReader(Mode mode) : mode_(mode) {}
    ReadResult read(std::span<std::uint8_t> output) noexcept override {
        ++calls;
        if (mode_ == Mode::InterruptedForever
            || (mode_ == Mode::InterruptedThenComplete && calls <= 2))
            return {ReadStatus::Interrupted, 0, 4};
        if (mode_ == Mode::Unavailable) {
            std::fill(output.begin(), output.end(), 0xa5);
            return {ReadStatus::Unavailable, 0, 38};
        }
        if (mode_ == Mode::Failure || (mode_ == Mode::PartialThenFailure && calls == 2)) {
            std::fill_n(output.begin(), (std::min)(output.size(), std::size_t{2}), 0xa5);
            return {ReadStatus::Failure, 0, 17};
        }
        if (mode_ == Mode::NoProgress) return {ReadStatus::Success, 0, 0};
        if (mode_ == Mode::Overreported) return {ReadStatus::Success, output.size() + 1, 0};
        const auto count = mode_ == Mode::Partial || mode_ == Mode::PartialThenFailure
            ? (std::min)(output.size(), std::size_t{3})
            : mode_ == Mode::OneAtATime ? std::size_t{1} : output.size();
        for (std::size_t i = 0; i != count; ++i)
            output[i] = mode_ == Mode::Zero ? 0 : static_cast<std::uint8_t>(++produced);
        return {ReadStatus::Success, count, 0};
    }
    std::size_t calls = 0;
    std::size_t produced = 0;
private:
    Mode mode_;
};
} // namespace

int main() {
    static_assert(noexcept(fillRuntimeSecureBytes({})));
    static_assert(std::is_trivially_copyable_v<RuntimeSecureBytesResult>);
    int checks = 0, failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL secure bytes: " << message << '\n'; }
    };
    ScriptedReader unused(Mode::Complete);
    const auto empty = runtime_secure_bytes_detail::fillWithReader({}, unused);
    expect(empty.status == RuntimeSecureBytesStatus::InvalidRequest
        && empty.generated == 0 && unused.calls == 0, "empty request never reaches reader");
    expect(fillRuntimeSecureBytes({}).status == RuntimeSecureBytesStatus::InvalidRequest,
        "public empty request is rejected");
    for (const auto mode : {Mode::Complete, Mode::Zero, Mode::InterruptedThenComplete, Mode::Partial}) {
        std::array<std::uint8_t, 12> guarded{};
        guarded.fill(0xcc);
        auto output = std::span(guarded).subspan(2, 8);
        ScriptedReader reader(mode);
        const auto result = runtime_secure_bytes_detail::fillWithReader(output, reader);
        expect(result.succeeded() && result.generated == output.size()
            && result.errorCode == 0, "complete/zero/interrupted/partial policy success");
        expect(guarded[0] == 0xcc && guarded[1] == 0xcc
            && guarded[10] == 0xcc && guarded[11] == 0xcc, "success writes only borrowed span");
        for (std::size_t i = 0; i != output.size(); ++i)
            expect(output[i] == (mode == Mode::Zero ? 0 : i + 1), "partial completion ordering and zero success");
        expect(reader.calls == (mode == Mode::InterruptedThenComplete || mode == Mode::Partial ? 3U : 1U),
            "only required reads occur");
    }
    for (const auto mode : {Mode::Failure, Mode::Unavailable, Mode::PartialThenFailure,
             Mode::NoProgress, Mode::Overreported, Mode::InterruptedForever, Mode::OneAtATime}) {
        std::array<std::uint8_t, 40> guarded{};
        guarded.fill(0xcc);
        auto output = std::span(guarded).subspan(2, mode == Mode::OneAtATime ? 33 : 8);
        ScriptedReader reader(mode);
        const auto result = runtime_secure_bytes_detail::fillWithReader(output, reader);
        const auto expected = mode == Mode::Unavailable ? RuntimeSecureBytesStatus::Unavailable
            : mode == Mode::InterruptedForever || mode == Mode::OneAtATime ? RuntimeSecureBytesStatus::RetryLimit
            : mode == Mode::NoProgress || mode == Mode::Overreported ? RuntimeSecureBytesStatus::Incomplete
            : RuntimeSecureBytesStatus::SystemFailure;
        expect(!result.succeeded() && result.status == expected && result.generated == 0,
            "failure category and atomic generated count");
        expect(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == 0; }),
            "failure wipes all output including any earlier partial read");
        expect(guarded[0] == 0xcc && guarded[1] == 0xcc
            && guarded[output.size() + 2] == 0xcc, "failure wipe stays inside requested span");
        expect(reader.calls <= runtimeSecureBytesMaximumReadAttempts, "all retries and progress bounded");
        if (mode == Mode::InterruptedForever || mode == Mode::OneAtATime)
            expect(reader.calls == runtimeSecureBytesMaximumReadAttempts, "fixed retry bound includes progress");
        if (mode == Mode::Failure || mode == Mode::PartialThenFailure)
            expect(result.errorCode == 17, "system error preserved");
    }
    // These are actual default-system calls, not the scripted-reader policy.
    const auto backend = runtimeSecureBytesBackend();
#ifdef _WIN32
    expect(backend == RuntimeSecureBytesBackend::WindowsCng, "actual Windows system backend");
#elif defined(__linux__)
    expect(backend == RuntimeSecureBytesBackend::LinuxGetrandom, "actual Linux system backend");
#else
    expect(backend == RuntimeSecureBytesBackend::Unsupported, "explicit unsupported platform");
#endif
    for (const std::size_t size : {8U, 16U, 88U, 1024U}) {
        std::array<std::uint8_t, 1040> guarded{};
        guarded.fill(0xcc);
        const auto output = std::span(guarded).subspan(8, size);
        const auto result = fillRuntimeSecureBytes(output);
        if (backend == RuntimeSecureBytesBackend::Unsupported)
            expect(result.status == RuntimeSecureBytesStatus::Unavailable, "unsupported is not success");
        else expect(result.succeeded() && result.generated == size, "real OS completely fills original request sizes");
        expect(std::all_of(guarded.begin(), guarded.begin() + 8, [](auto b) { return b == 0xcc; })
            && std::all_of(guarded.begin() + 8 + size, guarded.end(), [](auto b) { return b == 0xcc; }),
            "actual OS writes bounded span");
    }
    std::cout << "Secure bytes: " << checks << " checks, scripted policy separate from4realOSreads, backend="
        << static_cast<unsigned int>(backend) << '\n';
    if (backend == RuntimeSecureBytesBackend::Unsupported) {
        std::cerr << "Secure bytes native backend unsupported; not passed\n";
        return 77;
    }
    return failures == 0 ? 0 : 1;
}
