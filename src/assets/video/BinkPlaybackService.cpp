#include "assets/video/BinkPlaybackService.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <new>

namespace {
namespace fs = std::filesystem;
bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

fs::path firstExisting(std::initializer_list<fs::path> paths) {
    for (const fs::path& path : paths) {
        if (!path.empty() && regularFile(path)) return path;
    }
    return {};
}

std::uint32_t readLe32(const std::vector<unsigned char>& bytes, std::size_t at) {
    if (at + 4 > bytes.size()) return 0;
    return static_cast<std::uint32_t>(bytes[at])
        | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

volatile LONG* binkInterlockedField(volatile std::int32_t& field) {
    static_assert(sizeof(LONG) == sizeof(std::int32_t));
    return reinterpret_cast<volatile LONG*>(&field);
}

fs::path runtimeExecutableDirectory() {
    std::array<wchar_t, 32768> path{};
    const DWORD copied = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size())
    );
    if (copied == 0 || copied >= path.size()) return {};
    return fs::path(std::wstring(path.data(), copied)).parent_path();
}

std::wstring quotedCommandArgument(const std::wstring& value) {
    // The generated object names contain no quotes.  Repository paths may
    // contain spaces, so every argument is quoted for CreateProcessW.
    return L"\"" + value + L"\"";
}

const wchar_t* binkStreamArgument(
    concord::bink_stream::PlaybackMode mode,
    bool soundEnabled = true
) {
    if (!soundEnabled) {
        return mode == concord::bink_stream::PlaybackMode::Once
            ? L"--stream-once-muted"
            : L"--stream-muted";
    }
    return mode == concord::bink_stream::PlaybackMode::Once
        ? L"--stream-once"
        : L"--stream";
}

bool readBinkGeometry(const fs::path& path, std::uint32_t& width, std::uint32_t& height) {
    width = 0;
    height = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<unsigned char> header(36, 0);
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    if (input.gcount() != static_cast<std::streamsize>(header.size())
        || header[0] != 'B' || header[1] != 'I' || header[2] != 'K') {
        return false;
    }
    width = readLe32(header, 20);
    height = readLe32(header, 24);
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    return width > 0 && height > 0 && width <= 4096 && height <= 4096
        && pixels <= std::numeric_limits<std::uint32_t>::max() / 4U;
}


} // namespace

class BinkPlaybackService::Impl {
public:
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() { stop(); }

    bool start(
        const fs::path& resourceRoot,
        const fs::path& exportRoot,
        const fs::path& requestedMovie = {},
        concord::bink_stream::PlaybackMode playbackMode =
            concord::bink_stream::PlaybackMode::Loop,
        bool soundEnabled = true,
        int soundVolume = 32768
    ) {
        stop();
        failure_.clear();
        finished_ = false;

        const fs::path movie = requestedMovie.empty()
            ? firstExisting({
                exportRoot / "langmd" / "ra2ts_l.bik",
                exportRoot / "lang" / "ra2ts_l.bik"
            })
            : firstExisting({requestedMovie});
        const fs::path decoderDll = firstExisting({
            resourceRoot / "GameMD" / "Binkw32.dll",
            resourceRoot / "Game" / "Binkw32.dll"
        });
        const fs::path executableDirectory = runtimeExecutableDirectory();
        std::error_code currentError;
        const fs::path current = fs::current_path(currentError);
        const fs::path helper = firstExisting({
            executableDirectory / "BinkFrameExtractor.exe",
            executableDirectory.parent_path() / "BinkFrameExtractor.exe",
            executableDirectory.parent_path() / "bink_decoder32" / "Debug"
                / "BinkFrameExtractor.exe",
            current / "build" / "BinkFrameExtractor.exe",
            current / "BinkFrameExtractor.exe"
        });
        if (movie.empty()) return fail(
            requestedMovie.empty()
                ? "ra2ts_l.bik was not found"
                : "requested Bink movie was not found"
        );
        if (decoderDll.empty()) return fail("Binkw32.dll was not found");
        if (helper.empty()) return fail("the 32-bit Bink decoder helper was not found");

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (!readBinkGeometry(movie, width, height)) {
            return fail("the Bink movie has an invalid header");
        }
        const std::uint64_t capacity64 = static_cast<std::uint64_t>(width)
            * static_cast<std::uint64_t>(height) * 4ULL;
        if (capacity64 == 0
            || capacity64 > std::numeric_limits<std::uint32_t>::max()) {
            return fail("the Bink movie exceeds the decoder surface limit");
        }
        frameCapacityBytes_ = static_cast<std::uint32_t>(capacity64);

        static volatile LONG serial = 0;
        const LONG instance = InterlockedIncrement(&serial);
        const std::wstring suffix = std::to_wstring(GetCurrentProcessId())
            + L"_" + std::to_wstring(GetTickCount64())
            + L"_" + std::to_wstring(instance);
        const std::wstring mappingName = L"Local\\ConcordRTS_Bink_Map_" + suffix;
        const std::wstring frameEventName = L"Local\\ConcordRTS_Bink_Frame_" + suffix;
        const std::wstring stopEventName = L"Local\\ConcordRTS_Bink_Stop_" + suffix;

        const std::uint64_t mappingSize = concord::bink_stream::mappingBytes(
            frameCapacityBytes_
        );
        mapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>(mappingSize >> 32U),
            static_cast<DWORD>(mappingSize & 0xffffffffULL),
            mappingName.c_str()
        );
        if (mapping_ == nullptr) return fail("CreateFileMapping failed");
        mapped_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (mapped_ == nullptr) return fail("MapViewOfFile failed");
        shared_ = new (mapped_) concord::bink_stream::SharedHeader{};
        shared_->capacityWidth = width;
        shared_->capacityHeight = height;
        shared_->frameCapacityBytes = frameCapacityBytes_;

        frameEvent_ = CreateEventW(nullptr, FALSE, FALSE, frameEventName.c_str());
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, stopEventName.c_str());
        if (frameEvent_ == nullptr || stopEvent_ == nullptr) {
            return fail("CreateEvent failed");
        }

        soundVolume = std::clamp(soundVolume, 0, 32768);
        const bool effectiveSoundEnabled = soundEnabled && soundVolume > 0;
        std::wstring command = quotedCommandArgument(helper.wstring())
            + L" " + binkStreamArgument(playbackMode, effectiveSoundEnabled)
            + L" " + quotedCommandArgument(decoderDll.wstring())
            + L" " + quotedCommandArgument(movie.wstring())
            + L" " + quotedCommandArgument(mappingName)
            + L" " + quotedCommandArgument(frameEventName)
            + L" " + quotedCommandArgument(stopEventName)
            + L" " + std::to_wstring(
                effectiveSoundEnabled ? soundVolume : 0
            );
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(
            helper.wstring().c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process
        );
        if (created == FALSE) {
            return fail("the 32-bit Bink decoder helper could not start");
        }
        process_ = process.hProcess;
        CloseHandle(process.hThread);
        movieSource_ = movie.string();
        return true;
    }

    bool poll(BinkVideoFrame& output, std::uint32_t waitMilliseconds = 0) {
        if (process_ == nullptr || shared_ == nullptr || frameEvent_ == nullptr) {
            return false;
        }
        const DWORD frameReady = WaitForSingleObject(frameEvent_, waitMilliseconds);
        if (frameReady != WAIT_OBJECT_0) {
            if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
                finished_ = true;
                stop();
            }
            return false;
        }

        const LONG status = InterlockedCompareExchange(
            binkInterlockedField(shared_->status), 0, 0
        );
        if (status < 0) {
            failure_ = "the Bink decoder helper reported error "
                + std::to_string(-status);
            stop();
            return false;
        }
        const LONG sequence = InterlockedCompareExchange(
            binkInterlockedField(shared_->sequence), 0, 0
        );
        const LONG active = InterlockedCompareExchange(
            binkInterlockedField(shared_->activeBuffer), 0, 0
        );
        const std::uint64_t expected = static_cast<std::uint64_t>(shared_->width)
            * static_cast<std::uint64_t>(shared_->height) * 4ULL;
        if (status != 1 || sequence <= 0 || sequence == lastSequence_
            || (active != 0 && active != 1)
            || shared_->width == 0 || shared_->height == 0
            || expected != shared_->frameBytes
            || expected > frameCapacityBytes_) {
            return false;
        }

        const auto* surfaces = static_cast<const unsigned char*>(mapped_)
            + sizeof(concord::bink_stream::SharedHeader);
        const unsigned char* pixels = surfaces
            + static_cast<std::size_t>(active) * frameCapacityBytes_;
        output.width = static_cast<int>(shared_->width);
        output.height = static_cast<int>(shared_->height);
        output.bgra.assign(pixels, pixels + static_cast<std::size_t>(expected));
        output.source = movieSource_ + "!frame="
            + std::to_string(shared_->frameNumber);
        lastSequence_ = sequence;
        return true;
    }

    void stop() {
        if (stopEvent_ != nullptr) SetEvent(stopEvent_);
        if (process_ != nullptr) {
            const DWORD stopped = WaitForSingleObject(process_, 1500);
            if (stopped == WAIT_TIMEOUT) {
                // This is exclusively our decoder child.  It normally polls
                // the stop event every millisecond; terminate only if the
                // third-party decoder is wedged during shutdown.
                TerminateProcess(process_, 0);
                WaitForSingleObject(process_, 250);
            }
            CloseHandle(process_);
        }
        process_ = nullptr;
        if (mapped_ != nullptr) UnmapViewOfFile(mapped_);
        mapped_ = nullptr;
        shared_ = nullptr;
        if (mapping_ != nullptr) CloseHandle(mapping_);
        if (frameEvent_ != nullptr) CloseHandle(frameEvent_);
        if (stopEvent_ != nullptr) CloseHandle(stopEvent_);
        mapping_ = nullptr;
        frameEvent_ = nullptr;
        stopEvent_ = nullptr;
        frameCapacityBytes_ = 0;
        lastSequence_ = 0;
        movieSource_.clear();
    }

    const std::string& failure() const { return failure_; }
    std::int32_t sequence() const { return lastSequence_; }
    bool finished() const { return finished_; }

private:
    bool fail(const std::string& message) {
        failure_ = message;
        stop();
        return false;
    }

    HANDLE mapping_ = nullptr;
    HANDLE frameEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE process_ = nullptr;
    void* mapped_ = nullptr;
    concord::bink_stream::SharedHeader* shared_ = nullptr;
    std::uint32_t frameCapacityBytes_ = 0;
    LONG lastSequence_ = 0;
    std::string movieSource_;
    std::string failure_;
    bool finished_ = false;
};

#else
class BinkPlaybackService::Impl {
public:
    bool start(const std::filesystem::path&, const std::filesystem::path&,
        const std::filesystem::path&, concord::bink_stream::PlaybackMode, bool, int) {
        failure_ = "Bink playback is unsupported on this platform";
        return false;
    }
    bool poll(BinkVideoFrame&, std::uint32_t) { return false; }
    void stop() {}
    const std::string& failure() const { return failure_; }
    std::int32_t sequence() const { return 0; }
    bool finished() const { return false; }
private:
    std::string failure_;
};
#endif

BinkPlaybackService::BinkPlaybackService() : impl_(std::make_unique<Impl>()) {}
BinkPlaybackService::~BinkPlaybackService() = default;

bool BinkPlaybackService::supported() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool BinkPlaybackService::start(const BinkPlaybackRequest& request) {
    return impl_->start(request.resourceRoot, request.exportRoot, request.movie,
        request.mode, request.soundEnabled, request.soundVolume);
}
bool BinkPlaybackService::poll(BinkVideoFrame& output, std::uint32_t waitMilliseconds) {
    return impl_->poll(output, waitMilliseconds);
}
void BinkPlaybackService::stop() noexcept { impl_->stop(); }
const std::string& BinkPlaybackService::failure() const { return impl_->failure(); }
std::int32_t BinkPlaybackService::sequence() const { return impl_->sequence(); }
bool BinkPlaybackService::finished() const { return impl_->finished(); }
