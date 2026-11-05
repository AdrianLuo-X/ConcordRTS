#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>

// Atomic-save diagnostics are deliberately bounded so a failed save cannot
// turn an untrusted writer message into an unbounded UI/log payload.
inline constexpr std::size_t runtimeAtomicFileMaximumErrorBytes = 2048;
inline constexpr std::size_t runtimeAtomicFileMaximumTemporaryAttempts = 128;

using RuntimeAtomicFileWriter =
    std::function<bool(std::ostream&, std::string&)>;

// Writes to a newly-created temporary file in targetPath's directory, makes
// that file durable, closes it, and atomically replaces targetPath. On every
// failure the previous target is left in place and only the temporary file
// created by this invocation is considered for cleanup.
//
// The callback may explain a deliberate rejection through writerError. Its
// text, exceptions, stream failures, and all platform errors are converted to
// a bounded diagnostic in error. The function never opens targetPath for
// truncation.
// POSIX currently fsyncs the file and renames it, without fsyncing its parent
// directory. Successful publication is atomic; crash/power-loss persistence of
// the directory entry is not promised to match Windows WRITE_THROUGH.
bool runtimeWriteFileAtomically(
    const std::filesystem::path& targetPath,
    const RuntimeAtomicFileWriter& writer,
    std::string& error
);
