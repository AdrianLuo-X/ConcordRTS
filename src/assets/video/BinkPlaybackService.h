#pragma once

#include "assets/video/BinkVideoStream.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Owned, top-down BGRA pixels. A successful poll replaces the complete frame;
// an unsuccessful poll leaves the caller's frame unchanged. No mapping escapes.
struct BinkVideoFrame {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
    std::string source;
};

struct BinkPlaybackRequest {
    std::filesystem::path resourceRoot;
    std::filesystem::path exportRoot;
    std::filesystem::path movie;
    concord::bink_stream::PlaybackMode mode = concord::bink_stream::PlaybackMode::Loop;
    bool soundEnabled = true;
    int soundVolume = 32768;
};

// The single owner of one decoder child and its IPC. Use synchronously on the
// owning thread; no concurrent start/poll/stop contract. Paths are copied during
// start. stop/destruction signal, wait for and release only this session's child.
// stop resets the sequence but retains completion/error until the next start.
// The Windows helper supplies the original Bink clock and final-frame hold.
class BinkPlaybackService {
public:
    BinkPlaybackService();
    ~BinkPlaybackService();
    BinkPlaybackService(const BinkPlaybackService&) = delete;
    BinkPlaybackService& operator=(const BinkPlaybackService&) = delete;
    BinkPlaybackService(BinkPlaybackService&&) = delete;
    BinkPlaybackService& operator=(BinkPlaybackService&&) = delete;

    static bool supported() noexcept;
    bool start(const BinkPlaybackRequest& request);
    bool poll(BinkVideoFrame& output, std::uint32_t waitMilliseconds = 0);
    void stop() noexcept;
    const std::string& failure() const;
    std::int32_t sequence() const;
    bool finished() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
