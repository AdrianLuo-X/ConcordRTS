#include "RuntimeAtomicFile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using NativeFileHandle = HANDLE;
using NativeError = DWORD;
const NativeFileHandle invalidNativeFileHandle = INVALID_HANDLE_VALUE;
constexpr std::size_t maximumNativePathUnits = 32760;
#else
using NativeFileHandle = int;
using NativeError = int;
constexpr NativeFileHandle invalidNativeFileHandle = -1;
constexpr std::size_t maximumNativePathUnits = 4096;
#endif

constexpr std::size_t streamBufferBytes = 64U * 1024U;
constexpr std::size_t publicationStripeCount = 64U;
constexpr std::size_t maximumReplaceAttempts = 4U;
std::atomic<std::uint64_t> nextTemporarySerial{0};
std::array<std::mutex, publicationStripeCount> publicationStripes;

std::size_t publicationStripeIndex(
    const std::filesystem::path& targetPath
) noexcept {
    // A fixed stripe table bounds synchronization state while ensuring that
    // equal target spellings publish serially.  Normalize the Windows-only
    // path distinctions that the filesystem itself ignores without allocating
    // or canonicalizing through a potentially missing target.
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::filesystem::path::value_type character : targetPath.native()) {
#ifdef _WIN32
        if (character >= L'A' && character <= L'Z') {
            character = static_cast<std::filesystem::path::value_type>(
                character - L'A' + L'a'
            );
        } else if (character == L'/') {
            character = L'\\';
        }
#endif
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash % publicationStripeCount);
}

void setBoundedError(std::string& destination, std::string message) {
    if (message.size() > runtimeAtomicFileMaximumErrorBytes) {
        message.resize(runtimeAtomicFileMaximumErrorBytes);
    }
    destination = std::move(message);
}

void appendBoundedError(std::string& destination, const std::string& suffix) {
    if (destination.size() >= runtimeAtomicFileMaximumErrorBytes) {
        return;
    }
    const std::size_t available =
        runtimeAtomicFileMaximumErrorBytes - destination.size();
    destination.append(suffix.data(), std::min(available, suffix.size()));
}

void setBoundedErrorWithDetail(
    std::string& destination,
    const std::string& prefix,
    const std::string& detail
) {
    setBoundedError(destination, prefix);
    appendBoundedError(destination, detail);
}

void setBoundedErrorWithException(
    std::string& destination,
    const std::string& prefix,
    const char* detail
) {
    setBoundedError(destination, prefix);
    if (detail == nullptr) {
        return;
    }
    while (*detail != '\0'
        && destination.size() < runtimeAtomicFileMaximumErrorBytes) {
        destination.push_back(*detail);
        ++detail;
    }
}

std::string nativeErrorMessage(NativeError error) {
    if (error == 0) {
        return "unknown system error";
    }
#ifdef _WIN32
    return "Windows error " + std::to_string(error);
#else
    return std::generic_category().message(error)
        + " (error " + std::to_string(error) + ')';
#endif
}

bool pathContainsNull(const std::filesystem::path& path) {
    const auto& native = path.native();
    return std::find(native.begin(), native.end(),
        std::filesystem::path::value_type{}) != native.end();
}

bool pathWithinNativeLimit(const std::filesystem::path& path) {
    return path.native().size() <= maximumNativePathUnits;
}

bool isMissingPathError(const std::error_code& error) {
    return error == std::errc::no_such_file_or_directory;
}

std::string temporaryLeafName(std::uint64_t serial) {
    std::ostringstream name;
    name << ".concord-atomic-" << std::hex << std::setfill('0');
#ifdef _WIN32
    name << std::setw(8) << static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    name << std::setw(8) << static_cast<std::uint64_t>(getpid());
#endif
    name << '-' << std::setw(16) << serial << ".tmp";
    return name.str();
}

struct TemporaryOpenResult {
    bool opened = false;
    bool pathTooLong = false;
    bool attemptsExhausted = false;
    NativeError error = 0;
    NativeFileHandle handle = invalidNativeFileHandle;
    std::filesystem::path path;
};

TemporaryOpenResult openUniqueTemporaryFile(
    const std::filesystem::path& parent
) {
    TemporaryOpenResult result;
    for (std::size_t attempt = 0;
         attempt < runtimeAtomicFileMaximumTemporaryAttempts;
         ++attempt) {
        const std::uint64_t serial =
            nextTemporarySerial.fetch_add(1, std::memory_order_relaxed) + 1U;
        result.path = parent / temporaryLeafName(serial);
        if (!pathWithinNativeLimit(result.path)) {
            result.pathTooLong = true;
            return result;
        }
#ifdef _WIN32
        result.handle = CreateFileW(
            result.path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (result.handle != invalidNativeFileHandle) {
            result.opened = true;
            return result;
        }
        result.error = GetLastError();
        if (result.error == ERROR_FILE_EXISTS
            || result.error == ERROR_ALREADY_EXISTS) {
            continue;
        }
        if (result.error == ERROR_ACCESS_DENIED
            && GetFileAttributesW(result.path.c_str())
                != INVALID_FILE_ATTRIBUTES) {
            continue;
        }
#else
        result.handle = ::open(
            result.path.c_str(),
            O_WRONLY | O_CREAT | O_EXCL,
            static_cast<mode_t>(0600)
        );
        if (result.handle != invalidNativeFileHandle) {
            result.opened = true;
            return result;
        }
        result.error = errno;
        if (result.error == EEXIST) {
            continue;
        }
#endif
        return result;
    }
    result.attemptsExhausted = true;
    return result;
}

class NativeFileStreamBuffer final : public std::streambuf {
public:
    explicit NativeFileStreamBuffer(NativeFileHandle handle)
        : handle_(handle) {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    ~NativeFileStreamBuffer() override {
        if (handle_ != invalidNativeFileHandle) {
#ifdef _WIN32
            CloseHandle(handle_);
#else
            ::close(handle_);
#endif
        }
    }

    NativeFileStreamBuffer(const NativeFileStreamBuffer&) = delete;
    NativeFileStreamBuffer& operator=(const NativeFileStreamBuffer&) = delete;

    bool close() {
        if (handle_ == invalidNativeFileHandle) {
            return true;
        }
        const NativeFileHandle closing = handle_;
        handle_ = invalidNativeFileHandle;
#ifdef _WIN32
        if (CloseHandle(closing) != FALSE) {
            return true;
        }
        lastError_ = GetLastError();
#else
        if (::close(closing) == 0) {
            return true;
        }
        lastError_ = errno;
#endif
        failed_ = true;
        return false;
    }

    void abandonBufferedBytes() {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    bool failed() const {
        return failed_;
    }

    bool finish() {
        return pubsync() == 0 && !failed_;
    }

    NativeError lastError() const {
        return lastError_;
    }

protected:
    int_type overflow(int_type character) override {
        if (!flushBufferedBytes()) {
            return traits_type::eof();
        }
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(character);
            pbump(1);
        }
        return traits_type::not_eof(character);
    }

    std::streamsize xsputn(
        const char_type* source,
        std::streamsize count
    ) override {
        if (count <= 0 || failed_) {
            return 0;
        }
        std::streamsize written = 0;
        while (written < count) {
            if (pptr() == epptr() && !flushBufferedBytes()) {
                break;
            }
            const std::size_t available =
                static_cast<std::size_t>(epptr() - pptr());
            const std::streamsize remaining = count - written;
            const std::size_t chunk = std::min(
                available,
                static_cast<std::size_t>(remaining)
            );
            std::memcpy(
                pptr(),
                source + written,
                chunk
            );
            pbump(static_cast<int>(chunk));
            written += static_cast<std::streamsize>(chunk);
        }
        return written;
    }

    int sync() override {
        if (!flushBufferedBytes() || !flushNativeBuffers()) {
            return -1;
        }
        return 0;
    }

private:
    bool flushBufferedBytes() {
        if (failed_ || handle_ == invalidNativeFileHandle) {
            failed_ = true;
            return false;
        }
        const std::size_t byteCount =
            static_cast<std::size_t>(pptr() - pbase());
        std::size_t offset = 0;
        while (offset < byteCount) {
#ifdef _WIN32
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
                byteCount - offset,
                static_cast<std::size_t>(
                    std::numeric_limits<DWORD>::max()
                )
            ));
            DWORD completed = 0;
            if (WriteFile(
                    handle_,
                    buffer_.data() + offset,
                    request,
                    &completed,
                    nullptr
                ) == FALSE
                || completed == 0) {
                lastError_ = GetLastError();
                failed_ = true;
                return false;
            }
            offset += static_cast<std::size_t>(completed);
#else
            const ssize_t completed = ::write(
                handle_,
                buffer_.data() + offset,
                byteCount - offset
            );
            if (completed < 0 && errno == EINTR) {
                continue;
            }
            if (completed <= 0) {
                lastError_ = errno;
                failed_ = true;
                return false;
            }
            offset += static_cast<std::size_t>(completed);
#endif
        }
        setp(buffer_.data(), buffer_.data() + buffer_.size());
        return true;
    }

    bool flushNativeBuffers() {
#ifdef _WIN32
        if (FlushFileBuffers(handle_) != FALSE) {
            return true;
        }
        lastError_ = GetLastError();
#else
        if (::fsync(handle_) == 0) {
            return true;
        }
        lastError_ = errno;
#endif
        failed_ = true;
        return false;
    }

    NativeFileHandle handle_ = invalidNativeFileHandle;
    NativeError lastError_ = 0;
    bool failed_ = false;
    std::array<char, streamBufferBytes> buffer_{};
};

bool deleteTemporaryFile(
    const std::filesystem::path& temporaryPath,
    NativeError& error
) {
#ifdef _WIN32
    if (DeleteFileW(temporaryPath.c_str()) != FALSE) {
        return true;
    }
    error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
    if (::unlink(temporaryPath.c_str()) == 0) {
        return true;
    }
    error = errno;
    return error == ENOENT;
#endif
}

bool replaceTargetFile(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& targetPath,
    NativeError& error
) {
#ifdef _WIN32
    for (std::size_t attempt = 0; attempt < maximumReplaceAttempts; ++attempt) {
        if (MoveFileExW(
                temporaryPath.c_str(),
                targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            ) != FALSE) {
            return true;
        }
        error = GetLastError();
        const bool retryable = error == ERROR_ACCESS_DENIED
            || error == ERROR_SHARING_VIOLATION
            || error == ERROR_LOCK_VIOLATION;
        if (!retryable || attempt + 1U >= maximumReplaceAttempts) {
            return false;
        }
        Sleep(0);
    }
    return false;
#else
    if (std::rename(temporaryPath.c_str(), targetPath.c_str()) == 0) {
        return true;
    }
    error = errno;
#endif
    return false;
}

void reportCleanupFailure(
    std::string& error,
    NativeError cleanupError
) {
    appendBoundedError(
        error,
        "; temporary-file cleanup also failed: "
            + nativeErrorMessage(cleanupError)
    );
}

} // namespace

bool runtimeWriteFileAtomically(
    const std::filesystem::path& targetPath,
    const RuntimeAtomicFileWriter& writer,
    std::string& error
) {
    error.clear();
    if (targetPath.empty() || targetPath.filename().empty()) {
        setBoundedError(error, "atomic file target path is empty or incomplete");
        return false;
    }
    if (pathContainsNull(targetPath)) {
        setBoundedError(error, "atomic file target path contains a null character");
        return false;
    }
    if (!pathWithinNativeLimit(targetPath)) {
        setBoundedError(error, "atomic file target path is too long");
        return false;
    }
    if (!writer) {
        setBoundedError(error, "atomic file writer callback is missing");
        return false;
    }

    std::filesystem::path parent = targetPath.parent_path();
    if (parent.empty()) {
        parent = std::filesystem::path{"."};
    }
    if (pathContainsNull(parent) || !pathWithinNativeLimit(parent)) {
        setBoundedError(error, "atomic file parent path is invalid or too long");
        return false;
    }

    std::error_code statusError;
    std::filesystem::file_status parentStatus =
        std::filesystem::status(parent, statusError);
    if (statusError && !isMissingPathError(statusError)) {
        setBoundedError(
            error,
            "could not inspect atomic file parent directory: "
                + statusError.message()
        );
        return false;
    }
    if (statusError || !std::filesystem::exists(parentStatus)) {
        setBoundedError(error, "atomic file parent directory does not exist");
        return false;
    }
    if (!std::filesystem::is_directory(parentStatus)) {
        setBoundedError(error, "atomic file parent path is not a directory");
        return false;
    }

    statusError.clear();
    const std::filesystem::file_status targetStatus =
        std::filesystem::status(targetPath, statusError);
    if (statusError && !isMissingPathError(statusError)) {
        setBoundedError(
            error,
            "could not inspect atomic file target: " + statusError.message()
        );
        return false;
    }
    if (!statusError
        && std::filesystem::exists(targetStatus)
        && std::filesystem::is_directory(targetStatus)) {
        setBoundedError(error, "atomic file target is a directory");
        return false;
    }

    TemporaryOpenResult temporary = openUniqueTemporaryFile(parent);
    if (!temporary.opened) {
        if (temporary.pathTooLong) {
            setBoundedError(error, "atomic temporary file path is too long");
        } else if (temporary.attemptsExhausted) {
            setBoundedError(
                error,
                "atomic temporary file name attempts were exhausted"
            );
        } else {
            setBoundedError(
                error,
                "could not exclusively create atomic temporary file: "
                    + nativeErrorMessage(temporary.error)
            );
        }
        return false;
    }

    NativeFileStreamBuffer streamBuffer(temporary.handle);
    std::ostream output(&streamBuffer);
    std::string writerError;
    bool writerAccepted = false;
    bool writerThrew = false;
    try {
        writerAccepted = writer(output, writerError);
    } catch (const std::exception& exception) {
        writerThrew = true;
        setBoundedErrorWithException(
            error,
            "atomic file writer threw an exception: ",
            exception.what()
        );
    } catch (...) {
        writerThrew = true;
        setBoundedError(error, "atomic file writer threw an unknown exception");
    }

    if (writerThrew || !writerAccepted) {
        if (!writerThrew) {
            if (writerError.empty()) {
                setBoundedError(error, "atomic file writer rejected the save");
            } else {
                setBoundedErrorWithDetail(
                    error,
                    "atomic file writer rejected the save: ",
                    writerError
                );
            }
        }
        streamBuffer.abandonBufferedBytes();
        const bool closeSucceeded = streamBuffer.close();
        NativeError cleanupError = 0;
        if (!deleteTemporaryFile(temporary.path, cleanupError)) {
            reportCleanupFailure(error, cleanupError);
        } else if (!closeSucceeded) {
            appendBoundedError(
                error,
                "; closing the rejected temporary file failed: "
                    + nativeErrorMessage(streamBuffer.lastError())
            );
        }
        return false;
    }

    bool flushSucceeded = false;
    bool flushThrew = false;
    try {
        output.flush();
    } catch (const std::exception& exception) {
        flushThrew = true;
        setBoundedErrorWithException(
            error,
            "flushing atomic temporary file threw an exception: ",
            exception.what()
        );
    } catch (...) {
        flushThrew = true;
        setBoundedError(
            error,
            "flushing atomic temporary file threw an unknown exception"
        );
    }
    const bool nativeFinishSucceeded = streamBuffer.finish();
    flushSucceeded = !flushThrew
        && output.good()
        && nativeFinishSucceeded
        && !streamBuffer.failed();
    if (!flushSucceeded) {
        if (error.empty()) {
            setBoundedError(
                error,
                "flushing atomic temporary file failed: "
                    + nativeErrorMessage(streamBuffer.lastError())
            );
        }
        streamBuffer.abandonBufferedBytes();
        streamBuffer.close();
        NativeError cleanupError = 0;
        if (!deleteTemporaryFile(temporary.path, cleanupError)) {
            reportCleanupFailure(error, cleanupError);
        }
        return false;
    }

    if (!streamBuffer.close()) {
        setBoundedError(
            error,
            "closing atomic temporary file failed: "
                + nativeErrorMessage(streamBuffer.lastError())
        );
        NativeError cleanupError = 0;
        if (!deleteTemporaryFile(temporary.path, cleanupError)) {
            reportCleanupFailure(error, cleanupError);
        }
        return false;
    }

    NativeError replaceError = 0;
    std::lock_guard<std::mutex> publicationLock(
        publicationStripes[publicationStripeIndex(targetPath)]
    );
    if (!replaceTargetFile(temporary.path, targetPath, replaceError)) {
        setBoundedError(
            error,
            "atomically replacing target file failed: "
                + nativeErrorMessage(replaceError)
        );
        NativeError cleanupError = 0;
        if (!deleteTemporaryFile(temporary.path, cleanupError)) {
            reportCleanupFailure(error, cleanupError);
        }
        return false;
    }

    error.clear();
    return true;
}
