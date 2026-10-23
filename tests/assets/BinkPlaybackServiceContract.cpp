#include "assets/video/BinkPlaybackService.h"

#include <iostream>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<BinkPlaybackService>);
static_assert(!std::is_copy_assignable_v<BinkPlaybackService>);
static_assert(!std::is_move_constructible_v<BinkPlaybackService>);
static_assert(!std::is_move_assignable_v<BinkPlaybackService>);
static_assert(sizeof(concord::bink_stream::SharedHeader) == 64);

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    BinkVideoFrame frame{1, 1, {9, 8, 7, 6}, "owned-frame"};
    BinkPlaybackRequest request;
    request.resourceRoot = argv[1];
    request.movie = request.resourceRoot / "missing.bik";
    if (std::filesystem::exists(request.movie)) return 3;
    for (int attempt = 0; attempt < 32; ++attempt) {
        BinkPlaybackService service;
        if (service.sequence() != 0 || service.finished() || !service.failure().empty()) return 4;
        if (service.start(request)) return 5;
        const std::string expected = BinkPlaybackService::supported()
            ? "requested Bink movie was not found" : "Bink playback is unsupported on this platform";
        if (service.failure() != expected || service.poll(frame, 0)) return 6;
        service.stop(); service.stop();
        if (service.failure() != expected || service.finished() || service.sequence() != 0) return 7;
        if (frame.width != 1 || frame.height != 1 || frame.source != "owned-frame"
            || frame.bgra != std::vector<unsigned char>({9, 8, 7, 6})) return 8;
    }
    if (!BinkPlaybackService::supported()) {
        std::cout << "Bink playback unsupported; no playback success claimed\n";
        return 77;
    }
    std::cout << "Bink playback isolated production service:32 negative lifecycle cases passed\n";
    return 0;
}
