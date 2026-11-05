#pragma once

#include <filesystem>

// Owned OS paths only. The caller supplies discovered process directories;
// this policy neither launches a helper nor touches a playback session.
struct BinkPlaybackPathInputs {
    std::filesystem::path resourceRoot;
    std::filesystem::path exportRoot;
    std::filesystem::path requestedMovie;
    std::filesystem::path executableDirectory;
    std::filesystem::path currentDirectory;
};

struct BinkPlaybackPaths {
    std::filesystem::path movie;
    std::filesystem::path decoderDll;
    std::filesystem::path helper;
};

// Preserve first regular file, including the historical relative candidates
// when directory discovery fails. No canonicalization, sorting or fallback
// to another movie after a selected file fails decoding.
BinkPlaybackPaths resolveBinkPlaybackPaths(const BinkPlaybackPathInputs& inputs);
