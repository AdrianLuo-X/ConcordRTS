#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../src/assets/video/BinkVideoStream.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct BinkHeader {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t frames;
    std::uint32_t frameNum;
    std::uint32_t lastFrameNum;
    std::uint32_t frameRate;
    std::uint32_t frameRateDiv;
};

using BinkOpenFn = void* (__stdcall*)(const char*, std::uint32_t);
using BinkCloseFn = void (__stdcall*)(void*);
using BinkGotoFn = void (__stdcall*)(void*, std::uint32_t, std::uint32_t);
using BinkDoFrameFn = std::int32_t (__stdcall*)(void*);
using BinkNextFrameFn = void (__stdcall*)(void*);
using BinkWaitFn = std::int32_t (__stdcall*)(void*);
using BinkServiceFn = void (__stdcall*)(void*);
using BinkSetSoundOnOffFn = std::int32_t (__stdcall*)(
    void*, std::int32_t
);
using BinkSetVolumeFn = void (__stdcall*)(void*, std::int32_t);
using BinkCopyToBufferFn = std::int32_t (__stdcall*)(
    void*, void*, std::int32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t
);
struct BinkSound;
using BinkSoundOpenFn = std::int32_t (__stdcall*)(
    BinkSound*, std::uint32_t, std::int32_t, std::int32_t,
    std::uint32_t, void*
);
using BinkSoundSystemOpenFn = BinkSoundOpenFn (__stdcall*)(std::uint32_t);
using BinkSetSoundSystemFn = std::int32_t (__stdcall*)(
    BinkSoundSystemOpenFn, std::uint32_t
);

enum class BinkSoundBackend {
    None,
    DirectSound,
    WaveOut
};

template <typename Function>
Function loadFunction(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

BinkSoundBackend selectBinkSoundSystem(
    HMODULE module,
    BinkSoundBackend backend
) {
    const auto setSoundSystem = loadFunction<BinkSetSoundSystemFn>(
        module,
        "_BinkSetSoundSystem@8"
    );
    if (setSoundSystem == nullptr) return BinkSoundBackend::None;

    const char* exportName = backend == BinkSoundBackend::DirectSound
        ? "_BinkOpenDirectSound@4"
        : "_BinkOpenWaveOut@4";
    const auto openSound = loadFunction<BinkSoundSystemOpenFn>(
        module,
        exportName
    );
    return openSound != nullptr && setSoundSystem(openSound, 0) != 0
        ? backend
        : BinkSoundBackend::None;
}

BinkSoundBackend initializeBinkSound(HMODULE module) {
    // This probe only verifies that a factory can be registered. The actual
    // stream path below validates the device after BinkOpen as well.
    BinkSoundBackend backend = selectBinkSoundSystem(
        module,
        BinkSoundBackend::WaveOut
    );
    if (backend != BinkSoundBackend::None) return backend;
    return selectBinkSoundSystem(module, BinkSoundBackend::DirectSound);
}

HWND createBinkDirectSoundWindow() {
    // Bink 1.0q finds a top-level window belonging to this process before it
    // sets DirectSound's cooperative level. It does not require visibility or
    // a message pump, so this hidden window keeps per-movie volume isolated
    // from the system-wide WaveOut device volume.
    return CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"STATIC",
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
}

const char* soundBackendName(BinkSoundBackend backend) {
    switch (backend) {
    case BinkSoundBackend::DirectSound:
        return "DirectSound";
    case BinkSoundBackend::WaveOut:
        return "WaveOut";
    case BinkSoundBackend::None:
        return "unavailable";
    }
    return "unavailable";
}

std::uint32_t binkAudioTrackCount(const char* path) {
    std::ifstream input(path, std::ios::binary);
    unsigned char header[44]{};
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(header))
        || header[0] != 'B' || header[1] != 'I' || header[2] != 'K') {
        return 0;
    }
    return static_cast<std::uint32_t>(header[40])
        | (static_cast<std::uint32_t>(header[41]) << 8U)
        | (static_cast<std::uint32_t>(header[42]) << 16U)
        | (static_cast<std::uint32_t>(header[43]) << 24U);
}

DWORD finalFrameHoldMilliseconds(const BinkHeader& header) {
    constexpr std::uint64_t outputBufferMarginMilliseconds = 125ULL;
    if (header.frameRate == 0 || header.frameRateDiv == 0) {
        return static_cast<DWORD>(outputBufferMarginMilliseconds);
    }
    const std::uint64_t frameMilliseconds =
        (1000ULL * header.frameRateDiv + header.frameRate - 1ULL)
        / header.frameRate;
    return static_cast<DWORD>(std::clamp<std::uint64_t>(
        frameMilliseconds + outputBufferMarginMilliseconds,
        1ULL,
        static_cast<std::uint64_t>(std::numeric_limits<DWORD>::max() - 1U)
    ));
}

void serviceFinalFrameAudio(
    void* handle,
    HANDLE stopEvent,
    DWORD holdMilliseconds,
    BinkServiceFn service
) {
    const ULONGLONG deadline = GetTickCount64() + holdMilliseconds;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return;
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(
            deadline - now,
            8ULL
        ));
        if (WaitForSingleObject(stopEvent, remaining) == WAIT_OBJECT_0) return;
        if (service != nullptr) service(handle);
    }
}

volatile LONG* interlockedField(volatile std::int32_t& field) {
    static_assert(sizeof(LONG) == sizeof(std::int32_t));
    return reinterpret_cast<volatile LONG*>(&field);
}

void publishStreamError(
    concord::bink_stream::SharedHeader* shared,
    HANDLE frameEvent,
    int code
) {
    if (shared != nullptr) {
        InterlockedExchange(interlockedField(shared->status), -std::abs(code));
    }
    if (frameEvent != nullptr) SetEvent(frameEvent);
}

int streamBinkFrames(
    int argc,
    char** argv,
    concord::bink_stream::PlaybackMode playbackMode,
    bool soundEnabled
) {
    if (argc != 7 && argc != 8) {
        std::cerr << "Usage: BinkFrameExtractor "
                     "--stream|--stream-once|--stream-muted|"
                     "--stream-once-muted "
                     "<Binkw32.dll> "
                     "<input.bik> <mapping> <frame-event> <stop-event> "
                     "[volume-0..32768]\n";
        return 2;
    }

    std::int32_t binkVolume = 32768;
    if (argc == 8) {
        char* end = nullptr;
        const long parsed = std::strtol(argv[7], &end, 10);
        if (end == argv[7] || end == nullptr || *end != '\0') {
            std::cerr << "Invalid Bink sound volume: " << argv[7] << "\n";
            return 2;
        }
        binkVolume = static_cast<std::int32_t>(std::clamp<long>(
            parsed,
            0,
            32768
        ));
    }
    soundEnabled = soundEnabled && binkVolume > 0;

    HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[4]);
    HANDLE frameEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[5]);
    HANDLE stopEvent = OpenEventA(SYNCHRONIZE, FALSE, argv[6]);
    if (mapping == nullptr || frameEvent == nullptr || stopEvent == nullptr) {
        std::cerr << "Unable to open the Bink stream IPC objects.\n";
        if (stopEvent != nullptr) CloseHandle(stopEvent);
        if (frameEvent != nullptr) CloseHandle(frameEvent);
        if (mapping != nullptr) CloseHandle(mapping);
        return 1;
    }

    void* mapped = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (mapped == nullptr) {
        std::cerr << "Unable to map the Bink stream surface.\n";
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }
    auto* shared = static_cast<concord::bink_stream::SharedHeader*>(mapped);
    if (shared->magic != concord::bink_stream::kMagic
        || shared->version != concord::bink_stream::kVersion
        || shared->frameCapacityBytes == 0) {
        std::cerr << "Invalid Bink stream header.\n";
        publishStreamError(shared, frameEvent, 10);
        UnmapViewOfFile(mapped);
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }

    HMODULE module = LoadLibraryA(argv[2]);
    if (module == nullptr) {
        std::cerr << "Unable to load Binkw32.dll: " << GetLastError() << "\n";
        publishStreamError(shared, frameEvent, 11);
        UnmapViewOfFile(mapped);
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }
    const auto open = loadFunction<BinkOpenFn>(module, "_BinkOpen@8");
    const auto close = loadFunction<BinkCloseFn>(module, "_BinkClose@4");
    const auto doFrame = loadFunction<BinkDoFrameFn>(module, "_BinkDoFrame@4");
    const auto nextFrame = loadFunction<BinkNextFrameFn>(module, "_BinkNextFrame@4");
    const auto wait = loadFunction<BinkWaitFn>(module, "_BinkWait@4");
    const auto service = loadFunction<BinkServiceFn>(module, "_BinkService@4");
    const auto setSoundOnOff = loadFunction<BinkSetSoundOnOffFn>(
        module,
        "_BinkSetSoundOnOff@8"
    );
    const auto setVolume = loadFunction<BinkSetVolumeFn>(
        module,
        "_BinkSetVolume@8"
    );
    const auto copyToBuffer = loadFunction<BinkCopyToBufferFn>(
        module,
        "_BinkCopyToBuffer@28"
    );
    if (open == nullptr || close == nullptr || doFrame == nullptr
        || nextFrame == nullptr || wait == nullptr || copyToBuffer == nullptr) {
        std::cerr << "Binkw32.dll is missing a streaming decoder export.\n";
        publishStreamError(shared, frameEvent, 12);
        FreeLibrary(module);
        UnmapViewOfFile(mapped);
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }

    const std::uint32_t audioTrackCount = soundEnabled
        ? binkAudioTrackCount(argv[3])
        : 0;
    BinkSoundBackend soundBackend = BinkSoundBackend::None;
    HWND soundWindow = nullptr;
    void* handle = nullptr;
    if (audioTrackCount > 0) {
        // DirectSound gives Bink a private secondary-buffer volume control.
        // If its actual device open fails, close that attempt and retry at
        // full volume with WaveOut. We intentionally do not call BinkSetVolume
        // on WaveOut because old Bink changes the system-wide device volume.
        soundWindow = createBinkDirectSoundWindow();
        if (soundWindow != nullptr
            && selectBinkSoundSystem(
                module,
                BinkSoundBackend::DirectSound
            ) != BinkSoundBackend::None) {
            handle = open(argv[3], 0);
            if (handle != nullptr && setVolume != nullptr) {
                // Match the retail call order: apply the per-buffer volume
                // immediately after BinkOpen and before decoding starts.
                setVolume(handle, binkVolume);
            }
            const bool volumeSupported = binkVolume == 32768
                || setVolume != nullptr;
            if (handle != nullptr && volumeSupported
                && (setSoundOnOff == nullptr
                    || setSoundOnOff(handle, 1) != 0)) {
                soundBackend = BinkSoundBackend::DirectSound;
            } else {
                if (handle != nullptr) close(handle);
                handle = nullptr;
            }
        }
        if (soundBackend == BinkSoundBackend::None) {
            if (soundWindow != nullptr) DestroyWindow(soundWindow);
            soundWindow = nullptr;
            if (binkVolume == 32768
                && selectBinkSoundSystem(
                    module,
                    BinkSoundBackend::WaveOut
                ) != BinkSoundBackend::None) {
                handle = open(argv[3], 0);
                if (handle != nullptr
                    && (setSoundOnOff == nullptr
                        || setSoundOnOff(handle, 1) != 0)) {
                    soundBackend = BinkSoundBackend::WaveOut;
                }
            }
        }
    }
    if (handle == nullptr) handle = open(argv[3], 0);
    if (handle == nullptr) {
        std::cerr << "BinkOpen failed for " << argv[3] << "\n";
        publishStreamError(shared, frameEvent, 13);
        if (soundWindow != nullptr) DestroyWindow(soundWindow);
        FreeLibrary(module);
        UnmapViewOfFile(mapped);
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }
    if (audioTrackCount > 0 && soundBackend == BinkSoundBackend::None
        && setSoundOnOff != nullptr) {
        setSoundOnOff(handle, 0);
    }
    if (audioTrackCount > 0 && soundBackend == BinkSoundBackend::None) {
        std::cerr << "Unable to initialize Bink sound output; "
                     "continuing with video only.\n";
    } else if (audioTrackCount > 0) {
        std::cout << "Bink sound output: "
                  << soundBackendName(soundBackend)
                  << " volume=" << binkVolume << "/32768\n";
    }

    const BinkHeader& header = *static_cast<const BinkHeader*>(handle);
    const std::uint64_t frameBytes64 = static_cast<std::uint64_t>(header.width)
        * static_cast<std::uint64_t>(header.height) * 4ULL;
    if (header.width == 0 || header.height == 0 || header.frames == 0
        || header.width > shared->capacityWidth
        || header.height > shared->capacityHeight
        || frameBytes64 > shared->frameCapacityBytes) {
        std::cerr << "The Bink frame exceeds the shared surface capacity.\n";
        publishStreamError(shared, frameEvent, 14);
        close(handle);
        if (soundWindow != nullptr) DestroyWindow(soundWindow);
        FreeLibrary(module);
        UnmapViewOfFile(mapped);
        CloseHandle(stopEvent);
        CloseHandle(frameEvent);
        CloseHandle(mapping);
        return 1;
    }

    const std::uint32_t frameBytes = static_cast<std::uint32_t>(frameBytes64);
    shared->width = header.width;
    shared->height = header.height;
    shared->frameBytes = frameBytes;
    shared->frames = header.frames;
    shared->frameRate = header.frameRate;
    shared->frameRateDiv = header.frameRateDiv;
    auto* surfaces = static_cast<unsigned char*>(mapped)
        + sizeof(concord::bink_stream::SharedHeader);
    constexpr std::uint32_t binkSurface32 = 3;
    constexpr std::uint32_t binkCopyAll = 0x80000000u;

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
        if (wait(handle) != 0) {
            if (WaitForSingleObject(stopEvent, 1) == WAIT_OBJECT_0) break;
            continue;
        }

        const LONG active = InterlockedCompareExchange(
            interlockedField(shared->activeBuffer), 0, 0
        );
        const LONG writeIndex = active == 0 ? 1 : 0;
        unsigned char* pixels = surfaces
            + static_cast<std::size_t>(writeIndex) * shared->frameCapacityBytes;
        doFrame(handle);
        copyToBuffer(
            handle,
            pixels,
            static_cast<std::int32_t>(header.width * 4U),
            header.height,
            0,
            0,
            binkSurface32 | binkCopyAll
        );
        for (std::uint32_t at = 3; at < frameBytes; at += 4) pixels[at] = 255;
        shared->frameNumber = header.frameNum;
        MemoryBarrier();
        InterlockedExchange(interlockedField(shared->activeBuffer), writeIndex);
        InterlockedIncrement(interlockedField(shared->sequence));
        InterlockedExchange(interlockedField(shared->status), 1);
        SetEvent(frameEvent);
        if (concord::bink_stream::completesAfterFrame(
                playbackMode,
                header.frameNum,
                header.frames
            )) {
            // BinkWait gates the start of a frame, while the WaveOut close
            // callback resets queued buffers. Keep the final image and audio
            // alive for one frame plus a small output-buffer margin before
            // BinkClose; Esc/the stop event can still interrupt immediately.
            serviceFinalFrameAudio(
                handle,
                stopEvent,
                finalFrameHoldMilliseconds(header),
                service
            );
            break;
        }
        // Bink 1.0q wraps FrameNum from Frames back to one here, preserving
        // the menu background's native 15 Hz timing through BinkWait. The
        // explicit --stream-once mode exits above before that wrap.
        nextFrame(handle);
    }

    close(handle);
    if (soundWindow != nullptr) DestroyWindow(soundWindow);
    FreeLibrary(module);
    UnmapViewOfFile(mapped);
    CloseHandle(stopEvent);
    CloseHandle(frameEvent);
    CloseHandle(mapping);
    return 0;
}

bool writeBmp(
    const std::string& path,
    const std::vector<std::uint8_t>& pixels,
    int width,
    int height
) {
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(pixels.size());
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(&fileHeader),
        sizeof(fileHeader)
    );
    output.write(
        reinterpret_cast<const char*>(&infoHeader),
        sizeof(infoHeader)
    );
    output.write(
        reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size())
    );
    return output.good();
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--stream") {
        return streamBinkFrames(
            argc,
            argv,
            concord::bink_stream::PlaybackMode::Loop,
            true
        );
    }
    if (argc >= 2 && std::string(argv[1]) == "--stream-once") {
        return streamBinkFrames(
            argc,
            argv,
            concord::bink_stream::PlaybackMode::Once,
            true
        );
    }
    if (argc >= 2 && std::string(argv[1]) == "--stream-muted") {
        return streamBinkFrames(
            argc,
            argv,
            concord::bink_stream::PlaybackMode::Loop,
            false
        );
    }
    if (argc >= 2 && std::string(argv[1]) == "--stream-once-muted") {
        return streamBinkFrames(
            argc,
            argv,
            concord::bink_stream::PlaybackMode::Once,
            false
        );
    }
    if (argc == 3 && std::string(argv[1]) == "--audio-probe") {
        HMODULE module = LoadLibraryA(argv[2]);
        if (module == nullptr) {
            std::cerr << "Unable to load Binkw32.dll: "
                      << GetLastError() << "\n";
            return 1;
        }
        const BinkSoundBackend backend = initializeBinkSound(module);
        std::cout << "Bink sound backend factory: "
                  << soundBackendName(backend) << "\n";
        FreeLibrary(module);
        return backend == BinkSoundBackend::None ? 1 : 0;
    }
    if (argc < 4) {
        std::cerr << "Usage: BinkFrameExtractor <Binkw32.dll> <input.bik> "
                     "<output.bmp> [frame]\n"
                     "       BinkFrameExtractor --audio-probe "
                     "<Binkw32.dll>\n";
        return 2;
    }

    HMODULE module = LoadLibraryA(argv[1]);
    if (module == nullptr) {
        std::cerr << "Unable to load Binkw32.dll: " << GetLastError() << "\n";
        return 1;
    }
    const auto open = loadFunction<BinkOpenFn>(module, "_BinkOpen@8");
    const auto close = loadFunction<BinkCloseFn>(module, "_BinkClose@4");
    const auto goTo = loadFunction<BinkGotoFn>(module, "_BinkGoto@12");
    const auto doFrame = loadFunction<BinkDoFrameFn>(module, "_BinkDoFrame@4");
    const auto copyToBuffer = loadFunction<BinkCopyToBufferFn>(
        module,
        "_BinkCopyToBuffer@28"
    );
    if (open == nullptr || close == nullptr || goTo == nullptr
        || doFrame == nullptr || copyToBuffer == nullptr) {
        std::cerr << "Binkw32.dll is missing a required decoder export.\n";
        FreeLibrary(module);
        return 1;
    }

    void* handle = open(argv[2], 0);
    if (handle == nullptr) {
        std::cerr << "BinkOpen failed for " << argv[2] << "\n";
        FreeLibrary(module);
        return 1;
    }
    const BinkHeader& header = *static_cast<const BinkHeader*>(handle);
    if (header.width == 0 || header.height == 0
        || header.width > 4096 || header.height > 4096) {
        std::cerr << "Unexpected Bink dimensions.\n";
        close(handle);
        FreeLibrary(module);
        return 1;
    }

    std::uint32_t frame = 1;
    if (argc >= 5) {
        frame = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[4])));
    }
    frame = std::min(frame, std::max(1u, header.frames));
    goTo(handle, frame, 0);
    doFrame(handle);

    const int width = static_cast<int>(header.width);
    const int height = static_cast<int>(header.height);
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height)
            * 4u,
        0
    );
    // Bink 1.0q's 32-bit surfaces use BGRA byte order for BINKSURFACE32
    // and RGBA byte order for BINKSURFACE32R.  A BI_RGB bitmap stores its
    // pixels as BGRA, so requesting the reversed surface swaps red and blue.
    constexpr std::uint32_t binkSurface32 = 3;
    constexpr std::uint32_t binkCopyAll = 0x80000000u;
    copyToBuffer(
        handle,
        pixels.data(),
        width * 4,
        static_cast<std::uint32_t>(height),
        0,
        0,
        binkSurface32 | binkCopyAll
    );

    const bool written = writeBmp(argv[3], pixels, width, height);
    std::cout << "Bink " << width << "x" << height
              << " frames=" << header.frames
              << " extracted=" << frame << "\n";
    close(handle);
    FreeLibrary(module);
    return written ? 0 : 1;
}
